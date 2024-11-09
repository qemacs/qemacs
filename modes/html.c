/*
 * Graphical HTML mode for QEmacs.
 *
 * Copyright (c) 2001-2002 Fabrice Bellard.
 * Copyright (c) 2003-2023 Charlie Gordon.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qe.h"
#include "css.h"

/* define to enable timers */
//#define HTML_PROFILE

#define SCROLL_MHEIGHT     10
#define HTML_ERROR_BUFFER       "*xml-error*"

/* mode state */
typedef struct HTMLState {
    QEModeData base;
    /* default style sheet */
    CSSStyleSheet *default_style_sheet;
    CSSContext *css_ctx;
    CSSBox *top_box; /* top box of the display HTML page */
    CSSColor bgcolor; /* global bgcolor */
    int total_width;  /* document size */
    int total_height;
    int last_width, last_ydisp, last_xdisp;
    QECharset *last_charset;
    CSSRect invalid_rect; /* this rectangle should be redrawn */
    int up_to_date;    /* true if css representation is synced with
                          buffer content */
    int parse_flags;   /* can contain XML_HTML and XML_IGNORE_CASE */
} HTMLState;

/* recompute cursor offset so that it is visible (find closest box) */
typedef struct {
    CSSContext *ctx;
    int wanted_offset;
    int closest_offset;
    int dmin;
} RecomputeOffsetData;

#define MAX_LINE_SIZE 256

static int recompute_offset_func(void *opaque, CSSBox *box,
                                 qe__unused__ int x0, qe__unused__ int y0)
{
    RecomputeOffsetData *data = opaque;
    int offsets[MAX_LINE_SIZE+1];
    char32_t line_buf[MAX_LINE_SIZE];
    int len, d, i, offset;

    /* XXX: we do not accept empty boxes with spaces. need further
       fixes */
    if (box->height == 0)
        return 0;

    len = box_get_text(data->ctx, line_buf, MAX_LINE_SIZE, offsets, box);
    if (len == 0)
        return 0;
    offset = data->wanted_offset;
    for (i = 0; i < len; i++) {
        d = abs(offset - offsets[i]);
        if (d < data->dmin) {
            data->dmin = d;
            data->closest_offset = offsets[i];
        }
    }
    return 0;
}

static inline HTMLState *html_get_state(EditState *e, int status)
{
    return qe_get_buffer_mode_data(e->b, &html_mode, status ? e : NULL);
}

static void recompute_offset(EditState *s)
{
    HTMLState *hs;
    RecomputeOffsetData data;

    if (!(hs = html_get_state(s, 0)))
        return;

    data.ctx = hs->css_ctx;
    data.wanted_offset = s->offset;
    data.closest_offset = 0;
    data.dmin = INT_MAX;
    css_box_iterate(hs->css_ctx, hs->top_box,
                    &data, recompute_offset_func);
    s->offset = data.closest_offset;
}

/* output error message in error buffer */
void css_error(const char *filename, int line_num, const char *msg)
{
    EditBuffer *b;

    b = eb_find_new(HTML_ERROR_BUFFER, BF_READONLY | BF_UTF8);
    if (!b)
        return;
    b->flags &= ~BF_READONLY;
    b->offset = b->total_size;
    eb_printf(b, "%s:%d: %s\n", get_basename(filename), line_num, msg);
    b->flags |= ~BF_READONLY;
}


#ifdef HTML_PROFILE

static int timer_val;

static int get_time(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

#define timer_start() timer_val = get_time()
#define timer_stop(str) \
 printf("timer %s: %0.3f ms\n", str, (double)(get_time() - timer_val) / 1000.0)

#else

#define timer_start()
#define timer_stop(str)

#endif

static int html_test_abort(qe__unused__ void *opaque)
{
    return is_user_input_pending();
}

static void html_display(EditState *s)
{
    HTMLState *hs;
    CSSRect cursor_pos;
    DirType dirc;
    int n, cursor_found, d, ret, sel_start, sel_end;
    CSSRect rect;
    EditBuffer *b;

    if (!(hs = html_get_state(s, 0)))
        return;

    /* XXX: should be generic ? */
    if (hs->last_width != s->width) {
        hs->last_width = s->width;
        hs->up_to_date = 0;
    }
    if (s->b->charset != hs->last_charset) {
        hs->last_charset = s->b->charset;
        hs->up_to_date = 0;
    }

    /* reparse & layout if needed */
    if (!hs->up_to_date) {
        /* display busy message */
        if (!s->busy) {
            s->busy = 1;
            display_mode_line(s);
            dpy_flush(s->screen);
        }

        /* delete previous document */
        css_delete_box(&hs->top_box);
        css_delete_document(&hs->css_ctx);

        /* find error message buffer */
        b = eb_find(HTML_ERROR_BUFFER);
        if (b) {
            eb_delete(b, 0, b->total_size);
        }

        hs->css_ctx = css_new_document(s->screen, s->b);
        if (!hs->css_ctx)
            return;

        /* prepare default style sheet */
        hs->css_ctx->style_sheet = css_new_style_sheet();
        css_merge_style_sheet(hs->css_ctx->style_sheet, hs->default_style_sheet);

        /* default colors */
        hs->css_ctx->selection_bgcolor = qe_styles[QE_STYLE_SELECTION].bg_color;
        hs->css_ctx->selection_fgcolor = qe_styles[QE_STYLE_SELECTION].fg_color;
        hs->css_ctx->default_bgcolor = qe_styles[QE_STYLE_CSS_DEFAULT].bg_color;

        timer_start();
        hs->top_box = xml_parse_buffer(s->b, s->b->name, 0, s->b->total_size,
                                       hs->css_ctx->style_sheet,
                                       hs->parse_flags,
                                       html_test_abort, NULL);
        timer_stop("xml_parse_buffer");
        if (!hs->top_box)
            return;

        timer_start();
        css_compute(hs->css_ctx, hs->top_box);
        timer_stop("css_compute");

        timer_start();
        ret = css_layout(hs->css_ctx, hs->top_box, s->width,
                         html_test_abort, NULL);
        timer_stop("css_layout");
        if (ret) {
            return;
        }

        /* extract document size */
        hs->total_width = hs->top_box->bbox.x2;
        hs->total_height = hs->top_box->bbox.y2;

        /* set invalid rectangle to the whole window */
        css_set_rect(&hs->invalid_rect, s->xleft, s->ytop,
                     s->xleft + s->width, s->ytop + s->height);
        hs->up_to_date = 1;
        s->busy = 0;
    }

    /* draw if possible */
    if (hs->up_to_date) {
        n = 0;
    redo:
        timer_start();
        cursor_found = css_get_cursor_pos(hs->css_ctx, hs->top_box,
                                          NULL, NULL, NULL,
                                          &cursor_pos, &dirc, s->offset);
        timer_stop("css_get_cursor_pos");
        //        printf("cursor_found=%d offset=%d\n", cursor_found, s->offset);
        if (!cursor_found) {
            if (++n == 1) {
                /* move the cursor to the closest visible position */
                recompute_offset(s);
                goto redo;
            }
        }

        if (cursor_found) {
            /* if cursor not visible, adjust offsets */
            d = cursor_pos.y1 + s->y_disp;
            if (d < 0)
                s->y_disp -= d;
            d = cursor_pos.y2 + s->y_disp - s->height;
            if (d > 0)
                s->y_disp -= d;

            d = cursor_pos.x1 + s->x_disp[0];
            if (d < 0)
                s->x_disp[0] -= d;
            d = cursor_pos.x2 + s->x_disp[0] - s->width;
            if (d > 0)
                s->x_disp[0] -= d;
        }


        /* selection handling */
        if (s->show_selection || s->region_style) {
            sel_start = s->b->mark;
            sel_end = s->offset;
            /* sort selection */
            if (sel_end < sel_start) {
                sel_end = s->b->mark;
                sel_start = s->offset;
            }
        } else {
            /* no active selection */
            sel_start = 0;
            sel_end = 0;
        }

        if (sel_start != hs->css_ctx->selection_start ||
            sel_end != hs->css_ctx->selection_end) {
            hs->css_ctx->selection_start = sel_start;
            hs->css_ctx->selection_end = sel_end;
            s->display_invalid = 1;
        }

        if (hs->last_ydisp != s->y_disp ||
            hs->last_xdisp != s->x_disp[0] ||
            s->display_invalid) {
            /* set invalid rectangle to the whole window */
            css_set_rect(&hs->invalid_rect, s->xleft, s->ytop,
                         s->xleft + s->width, s->ytop + s->height);
            hs->last_ydisp = s->y_disp;
            hs->last_xdisp = s->x_disp[0];
            s->display_invalid = 0;
        }

        /* set the clipping rectangle to the whole window */
        rect.x1 = s->xleft;
        rect.y1 = s->ytop;
        rect.x2 = rect.x1 + s->width;
        rect.y2 = rect.y1 + s->height;
        set_clip_rectangle(s->screen, &rect);

        /* compute clip rectangle */
        if (!css_is_null_rect(&hs->invalid_rect)) {
            CSSRect old_clip;
            rect = hs->invalid_rect;

            push_clip_rectangle(s->screen, &old_clip, &rect);

            timer_start();

            css_display(hs->css_ctx, hs->top_box,
                        &rect, s->xleft + s->x_disp[0], s->ytop + s->y_disp);
            timer_stop("css_display");

            set_clip_rectangle(s->screen, &old_clip);

            /* no longer invalid, so set invalid_rect to null */
            css_set_rect(&hs->invalid_rect, 0, 0, 0, 0);
        }

        /* display cursor */
        if (cursor_found && s->qe_state->active_window == s) {
            int x, y, w, h;

            x = cursor_pos.x1;
            y = cursor_pos.y1;
            w = cursor_pos.x2 - cursor_pos.x1;
            h = cursor_pos.y2 - cursor_pos.y1;
            x += s->xleft + s->x_disp[0];
            y += s->ytop + s->y_disp;
            if (s->screen->dpy.dpy_cursor_at) {
                /* hardware cursor */
                s->screen->dpy.dpy_cursor_at(s->screen, x, y, w, h);
            } else {
                xor_rectangle(s->screen, x, y, w, h, QERGB(0xFF, 0xFF, 0xFF));
                /* invalidate rectangle modified by cursor */
                css_set_rect(&rect, x, y, x + w, y + h);
                css_union_rect(&hs->invalid_rect, &rect);
            }
        }
    }
}

typedef struct {
    int y_found;
    int y_disp;
    int height;
    int offset_found;
    int dir; /* -1: cursor up, 1: cursor bottom */
    int offsetc;
} ScrollContext;

static int scroll_func(void *opaque, CSSBox *box, qe__unused__ int x, int y)
{
    ScrollContext *m = opaque;
    int y1;

    if (box->height == 0)
        return 0;
    y += m->y_disp;
    y1 = y + box->height;
    /* XXX: add bidir handling : position cursor on left / right */
    if (m->dir < 0) {
        if (y >= 0 && y < m->y_found) {
            m->y_found = y;
            m->offset_found = box->u.buffer.start;
        }
    } else {
        if (y1 <= m->height && y1 > m->y_found) {
            m->y_found = y1;
            m->offset_found = box->u.buffer.start;
        }
    }
    if (m->offsetc >= box->u.buffer.start &&
        m->offsetc <= box->u.buffer.end &&
        y >= 0 && y1 <= m->height) {
        m->offset_found = m->offsetc;
        return 1;
    }
    return 0;
}


static void html_scroll_up_down(EditState *s, int dir)
{
    HTMLState *hs;
    ScrollContext m1, *m = &m1;
    int h;

    if (!(hs = html_get_state(s, 1)))
        return;

    if (!hs->up_to_date)
        return;

    h = SCROLL_MHEIGHT;
    if (abs(dir) == 2) {
        h = s->height - SCROLL_MHEIGHT;
        dir /= 2;
    }
    if (h < SCROLL_MHEIGHT)
        h = s->height;
    h = -dir * h;
    s->y_disp += h;
    if (s->y_disp > 0 || hs->total_height <= s->height) {
        s->y_disp = 0;
    } else if (hs->total_height + s->y_disp < s->height) {
        s->y_disp = s->height - hs->total_height;
    }

    /* XXX: max height ? */

    /* now update cursor position so that it is on screen */
    m->offsetc = s->offset;
    m->dir = -dir;
    m->y_found = 0x7fffffff * dir;
    m->offset_found = s->offset; /* default offset */
    m->y_disp = s->y_disp;
    m->height = s->height;
    css_box_iterate(hs->css_ctx, hs->top_box, m, scroll_func);
    s->offset = m->offset_found;
}

/* visual UP/DOWN handling */

typedef struct {
    int dir;
    int yd;
    int xdbase; /* x origin of box */
    int xd;
    int xdmin;
    int ydmin;
    int y1;
    int y2;
    int offsetd;
    CSSBox *box;
} MoveContext;

/* distance from x to segment [x1,x2-1] */
static int seg_dist(int x, int x1, int x2)
{
    if (x >= x1 && x < x2)
        return 0;
    else if (x < x1)
        return x1 - x;
    else
        return x - x2 + 1;
}

static int up_down_func(void *opaque, CSSBox *box, int x, int y)
{
    MoveContext *m = opaque;
    int d, y1;

    if (box->height == 0 || box->width == 0)
        return 0;

    if (m->dir < 0) {
        y1 = y + box->height;
        if (y1 <= m->yd)
            goto ytest;
    } else {
        y1 = y;
        if (y1 >= m->yd)
            goto ytest;
    }
    return 0;

 ytest:
    /* if no y intersection with selected box, then see if it is closer */
    if (m->ydmin == INT_MAX ||
        y >= m->y2 || (y + box->height) <= m->y1) {

        d = abs(y1 - m->yd);
        if (d < m->ydmin) {
            m->ydmin = d;
            m->y1 = y;
            m->y2 = y + box->height;
            m->xdmin = INT_MAX;
        } else if (d == m->ydmin) {
            /* also do x test if on the same line */
        } else {
            return 0;
        }
    }

    /* if the box passed the y test, then select the closest box along the x axis */
    d = seg_dist(m->xd, x, x + box->width);
    if (d < m->xdmin) {
        m->xdbase = x;
        m->xdmin = d;
        m->box = box;
    }
    return 0;
}

static void html_move_up_down1(EditState *s, int dir, int xtarget)
{
    HTMLState *hs;
    MoveContext m1, *m = &m1;
    CSSRect cursor_pos;
    int dirc, offset;

    if (!(hs = html_get_state(s, 1)))
        return;

    /* get the cursor position in the current chunk */
    if (!css_get_cursor_pos(hs->css_ctx, hs->top_box, NULL, NULL, NULL,
                            &cursor_pos, &dirc, s->offset))
        return;

    /* compute the position to which we would like to go */
    if (xtarget == 0) {
        if (s->up_down_last_x == -1)
            s->up_down_last_x = cursor_pos.x1;
    }

    if (dir > 0)
        m->yd = cursor_pos.y2;
    else
        m->yd = cursor_pos.y1;

    /* find a suitable box upward or downward */
    if (xtarget == 0) {
        m->xd = s->up_down_last_x;
    } else {
        m->xd = xtarget;
    }
    m->dir = dir;
    m->xdmin = INT_MAX;
    m->ydmin = INT_MAX;
    m->box = NULL;
    m->xdbase = 0;

    css_box_iterate(hs->css_ctx, hs->top_box, m, up_down_func);

    /* if no box found, then compose the next text chunk */
    if (m->box) {
        /* the box was found : find exact cursor offset */
        offset = css_get_offset_pos(hs->css_ctx, m->box, m->xd - m->xdbase, 0);
        if (offset >= 0) {
            s->offset = offset;
        }
    }
}

static void html_move_up_down(EditState *s, int dir)
{
    HTMLState *hs;

    if (!(hs = html_get_state(s, 1)))
        return;

    if (!hs->up_to_date)
        return;

    if (s->qe_state->last_cmd_func != (CmdFunc)do_up_down)
        s->up_down_last_x = -1;

    html_move_up_down1(s, dir, 0);
}

/* visual LEFT/RIGHT handling */

typedef struct {
    int dir;
    int y1;
    int y2;
    int xd;
    int xdmin;
    CSSBox *box;
    int x0;
} LeftRightMoveContext;

static int left_right_func(void *opaque, CSSBox *box, int x, int y)
{
    LeftRightMoveContext *m = opaque;
    int d, x1;

    /* only examine boxes which intersect the current one on y axis */
    if (!(y + box->height <= m->y1 ||
          y >= m->y2)) {
        if ((m->dir < 0 && (x1 = x + box->width) <= m->xd) ||
            (m->dir > 0 && (x1 = x) >= m->xd)) {
            /* find the closest box in the correct direction */
            d = abs(x1 - m->xd);
            if (d < m->xdmin) {
                m->xdmin = d;
                m->box = box;
                m->x0 = x;
            }
        }
    }
    return 0;
}

/* go to left or right in visual order */
static void html_move_left_right_visual(EditState *s, int dir)
{
    HTMLState *hs;
    LeftRightMoveContext m1, *m = &m1;
    CSSRect cursor_pos;
    int dirc, offset, x0;
    CSSBox *box;

    if (!(hs = html_get_state(s, 1)))
        return;

    if (!hs->up_to_date)
        return;

    /* get the cursor position. If not found, do nothing */
    if (!css_get_cursor_pos(hs->css_ctx, hs->top_box,
                            &box, &x0, NULL,
                            &cursor_pos, &dirc, s->offset))
        return;

    offset = css_get_offset_pos(hs->css_ctx,
                                box, cursor_pos.x1 - x0, dir);
    if (offset >= 0) {
        /* match found : finished ! */
        s->offset = offset;
    } else {
        /* find the closest box in the correct direction */
        if (dir > 0)
            m->xd = cursor_pos.x2;
        else
            m->xd = cursor_pos.x1;
        m->y1 = cursor_pos.y1;
        m->y2 = cursor_pos.y2;

        m->dir = dir;
        m->xdmin = INT_MAX;
        m->box = NULL;

        css_box_iterate(hs->css_ctx, hs->top_box, m, left_right_func);
        if (!m->box) {
            /* no box found : go up or down */
            html_move_up_down1(s, dir, -dir * (INT_MAX / 2));
        } else {
            offset = css_get_offset_pos(hs->css_ctx, m->box,
                                        cursor_pos.x1 - m->x0, dir);
            if (offset >= 0) {
                s->offset = offset;
            }
        }
    }
}

static void html_move_bol_eol(EditState *s, int dir)
{
    HTMLState *hs;
    LeftRightMoveContext m1, *m = &m1;
    CSSRect cursor_pos;
    int dirc, offset, x0, xtarget;
    CSSBox *box;

    if (!(hs = html_get_state(s, 1)))
        return;

    if (!hs->up_to_date)
        return;

    /* get the cursor position. If not found, do nothing */
    if (!css_get_cursor_pos(hs->css_ctx, hs->top_box,
                            &box, &x0, NULL,
                            &cursor_pos, &dirc, s->offset))
        return;

    /* find the box closest to x */
    xtarget = -dir * (INT_MAX / 2);
    m->xd = xtarget;
    m->y1 = cursor_pos.y1;
    m->y2 = cursor_pos.y2;

    m->dir = dir;
    m->xdmin = INT_MAX;
    m->box = NULL;
    css_box_iterate(hs->css_ctx, hs->top_box, m, left_right_func);
    if (m->box) {

        offset = css_get_offset_pos(hs->css_ctx, m->box, xtarget, dir);
        if (offset >= 0) {
            s->offset = offset;
        }
    }
}

static void html_move_bol(EditState *s)
{
    int offset;
    offset = s->offset;
    html_move_bol_eol(s, 1);
    /* XXX: hack to allow to go back on left side */
    if (offset == s->offset) {
        s->x_disp[0] = 0;
    }
}

static void html_move_eol(EditState *s)
{
    html_move_bol_eol(s, -1);
}

/* mouse handling */

typedef struct {
    int yd;
    int xd;
    int dy_min;
    int dx_min;
    CSSBox *box;
    int x0;
    int dx, dy;
} MouseGotoContext;

static int mouse_goto_func(void *opaque, CSSBox *box, int x, int y)
{
    MouseGotoContext *m = opaque;
    int dy, dx;

    x += m->dx;
    y += m->dy;

    dy = seg_dist(m->yd, y, y + box->height);
    if (dy < m->dy_min) {
        m->dy_min = dy;
        m->dx_min = 0x3fffffff;
    }
    if (dy == m->dy_min) {
        dx = seg_dist(m->xd, x, x + box->width);
        if (dx < m->dx_min) {
            m->dx_min = dx;
            m->box = box;
            m->x0 = x;
            /* fast exit test */
            if (dy == 0 && dx == 0)
                return -1;
        }
    }
    return 0;
}

static void html_mouse_goto(EditState *s, int x, int y, QEEvent *ev)
{
    HTMLState *hs;
    MouseGotoContext m1, *m = &m1;
    int offset;

    if (!(hs = html_get_state(s, 1)))
        return;

    if (!hs->up_to_date)
        return;

    m->dx_min = 0x3fffffff;
    m->dy_min = 0x3fffffff;
    m->xd = x;
    m->yd = y;
    m->box = NULL;
    m->dx = s->x_disp[0];
    m->dy = s->y_disp;
    css_box_iterate(hs->css_ctx, hs->top_box, m, mouse_goto_func);
    if (m->box) {
        offset = css_get_offset_pos(hs->css_ctx, m->box, x - m->x0, 0);
        if (offset >= 0) {
            s->offset = offset;
        }
    }
}

/* invalidate the html data if modification done (XXX: be more precise) */
static void html_callback(qe__unused__ EditBuffer *b,
                          void *opaque, qe__unused__ int arg,
                          qe__unused__ enum LogOperation op,
                          qe__unused__ int offset,
                          qe__unused__ int size)
{
    HTMLState *hs = opaque;

    if (hs)
        hs->up_to_date = 0;
}

static void load_default_style_sheet(HTMLState *hs, const char *stylesheet_str,
                                     int flags)
{
    CSSStyleSheet *style_sheet;

    style_sheet = css_new_style_sheet();

    css_parse_style_sheet_str(style_sheet, stylesheet_str, flags);

    hs->default_style_sheet = style_sheet;
}

/* graphical XML/CSS mode init. is_html is TRUE to tell that specific HTML
   quirks are needed in the parser. */
int gxml_mode_init(EditBuffer *b, int flags, const char *default_stylesheet)
{
    HTMLState *hs = qe_get_buffer_mode_data(b, &html_mode, NULL);

    if (!hs)
        return -1;

    /* XXX: unregister callbacks for s->offset and s->top_offset ? */

    eb_add_callback(b, html_callback, hs, 0);
    hs->parse_flags = flags;
    load_default_style_sheet(hs, default_stylesheet, flags);
    hs->up_to_date = 0;

    return 0;
}

/* XXX: should keep parsed data for buffer lifetime? */
static int html_mode_init(EditState *s, EditBuffer *b, int flags)
{
    HTMLState *hs = qe_get_buffer_mode_data(b, &html_mode, NULL);

    if (!hs)
        return -1;

    if (flags & MODEF_NEWINSTANCE) {
        return gxml_mode_init(b, XML_HTML | XML_HTML_SYNTAX | XML_IGNORE_CASE,
                              html_style);
    }
    hs->up_to_date = 0;
    return 0;
}

static void html_mode_close(EditState *s)
{
    s->busy = 0; /* make it a buffer flag? */
}

static void html_mode_free(EditBuffer *b, void *state)
{
    HTMLState *hs = state;

    eb_free_callback(b, html_callback, hs);

    //s->busy = 0; /* make it a buffer flag? */
    css_delete_box(&hs->top_box);
    css_delete_document(&hs->css_ctx);
    css_free_style_sheet(&hs->default_style_sheet);
}

/* search for HTML tag */
static int html_mode_probe(ModeDef *mode, ModeProbeData *p1)
{
    const uint32_t magic = (1U << '\r') | (1U << '\n') | (1U << '\t') | (1U << '\033');
    const unsigned char *p = p1->buf;
    int c, score;

    score = 0;

    if (!use_html)
        return 0;

    while (qe_isspace(*p))
        p++;
    if (*p != '<')
        return 0;
    if (p[1] != '!' && p[1] != '?' && !qe_isalpha(p[1]))
        return 0;

    for (;;) {
        c = *p;
        if (c == '\0')
            break;
        if (c < 32 && !(magic & (1U << c)))
            return 0;
        if (c == '<' && stristart(cs8(p), "<html", NULL))
            score = 95;
        p++;
    }

    if (match_extension(p1->filename, "php"))
        return 75;

    return score;
}

/* XXX: only works in insert mode */
static void do_html_electric_key(EditState *s, int key)
{
    const char *str;
    str = find_entity_str(key);
    if (str) {
        do_char(s, '&', 1);
        while (*str) {
            do_char(s, *str++, 1);
        }
        do_char(s, ';', 1);
    }
}


/* specific html commands */
static const CmdDef html_commands[] = {
    /* should use 'k' intrinsic argument */
    CMD2( "html-electric-key", "<, >, &",
          "Insert the entity for special character",
          do_html_electric_key, ESi, "*" "k")
};

ModeDef html_mode = {
    .name = "html",
    .buffer_instance_size = sizeof(HTMLState),
    .mode_probe = html_mode_probe,
    .mode_init = html_mode_init,
    .mode_close = html_mode_close,
    .mode_free = html_mode_free,
    .display = html_display,
    .move_up_down = html_move_up_down,
    .move_left_right = html_move_left_right_visual,
    .move_bol = html_move_bol,
    .move_eol = html_move_eol,
    .move_bof = text_move_bof,  /* XXX: should refine */
    .move_eof = text_move_eof,  /* XXX: should refine */
    .move_word_left_right = text_move_word_left_right,  /* XXX: refine */
    .scroll_up_down = html_scroll_up_down,
    .mouse_goto = html_mouse_goto,
    .write_char = text_write_char,
};

static int html_init(QEmacsState *qs)
{
    css_init();

    qe_register_mode(&html_mode, MODEF_VIEW);
    qe_register_commands(&html_mode, html_commands, countof(html_commands));

    return 0;
}

qe_module_init(html_init);
