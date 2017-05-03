/*
 * Display system for QEmacs
 *
 * Copyright (c) 2000 Fabrice Bellard.
 * Copyright (c) 2002-2017 Charlie Gordon.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "qe.h"

static QEDisplay *first_dpy;

/* dummy display driver for initialization time */

static int dummy_dpy_init(QEditScreen *s, qe__unused__ int w, qe__unused__ int h)
{
    s->charset = &charset_8859_1;

    return 0;
}

static void dummy_dpy_close(qe__unused__ QEditScreen *s)
{
}

static void dummy_dpy_flush(qe__unused__ QEditScreen *s)
{
}

static int dummy_dpy_is_user_input_pending(qe__unused__ QEditScreen *s)
{
    return 0;
}

static void dummy_dpy_fill_rectangle(qe__unused__ QEditScreen *s,
                                     qe__unused__ int x1, qe__unused__ int y1,
                                     qe__unused__ int w, qe__unused__ int h,
                                     qe__unused__ QEColor color)
{
}

static void dummy_dpy_xor_rectangle(qe__unused__ QEditScreen *s,
                                    qe__unused__ int x1, qe__unused__ int y1,
                                    qe__unused__ int w, qe__unused__ int h,
                                    qe__unused__ QEColor color)
{
}

static QEFont *dummy_dpy_open_font(qe__unused__ QEditScreen *s,
                                   qe__unused__ int style, qe__unused__ int size)
{
    return NULL;
}

static void dummy_dpy_close_font(qe__unused__ QEditScreen *s,
                                 qe__unused__ QEFont **fontp)
{
}

static void dummy_dpy_text_metrics(qe__unused__ QEditScreen *s,
                                   qe__unused__ QEFont *font,
                                   QECharMetrics *metrics,
                                   qe__unused__ const unsigned int *str,
                                   qe__unused__ int len)
{
    metrics->font_ascent = 1;
    metrics->font_descent = 0;
    metrics->width = len;
}

static void dummy_dpy_draw_text(qe__unused__ QEditScreen *s,
                                qe__unused__ QEFont *font,
                                qe__unused__ int x, qe__unused__ int y,
                                qe__unused__ const unsigned int *str,
                                qe__unused__ int len,
                                qe__unused__ QEColor color)
{
}

static void dummy_dpy_set_clip(qe__unused__ QEditScreen *s,
                               qe__unused__ int x, qe__unused__ int y,
                               qe__unused__ int w, qe__unused__ int h)
{
}

static QEDisplay const dummy_dpy = {
    "dummy",
    NULL, /* dpy_probe */
    dummy_dpy_init,
    dummy_dpy_close,
    dummy_dpy_flush,
    dummy_dpy_is_user_input_pending,
    dummy_dpy_fill_rectangle,
    dummy_dpy_xor_rectangle,
    dummy_dpy_open_font,
    dummy_dpy_close_font,
    dummy_dpy_text_metrics,
    dummy_dpy_draw_text,
    dummy_dpy_set_clip,

    NULL, /* dpy_selection_activate */
    NULL, /* dpy_selection_request */
    NULL, /* dpy_invalidate */
    NULL, /* dpy_cursor_at */
    NULL, /* dpy_bmp_alloc */
    NULL, /* dpy_bmp_free */
    NULL, /* dpy_bmp_draw */
    NULL, /* dpy_bmp_lock */
    NULL, /* dpy_bmp_unlock */
    NULL, /* dpy_full_screen */
    NULL, /* dpy_describe */
    NULL, /* next */
};

/*----------------*/

void fill_rectangle(QEditScreen *s,
                    int x1, int y1, int w, int h, QEColor color)
{
    int x2 = x1 + w;
    int y2 = y1 + h;

    /* quick clip rejection */
    if (x2 <= s->clip_x1 || y2 <= s->clip_y1 ||
        x1 >= s->clip_x2 || y1 >= s->clip_y2)
        return;

    /* region update */
    if (x2 > s->clip_x2)
        x2 = s->clip_x2;
    if (y2 > s->clip_y2)
        y2 = s->clip_y2;
    if (x1 < s->clip_x1)
        x1 = s->clip_x1;
    if (y1 < s->clip_y1)
        y1 = s->clip_y1;

    /* rejection if zero size */
    if (x1 >= x2 || y1 >= y2)
        return;

    s->dpy.dpy_fill_rectangle(s, x1, y1, x2 - x1, y2 - y1, color);
}

void xor_rectangle(QEditScreen *s,
                   int x, int y, int w, int h, QEColor color)
{
    /* intersect with clip region */
    int x1 = max(s->clip_x1, x);
    int y1 = max(s->clip_y1, y);
    int x2 = min(s->clip_x2, x1 + w);
    int y2 = min(s->clip_y2, y1 + h);

    if (x1 < x2 && y1 < y2) {
        s->dpy.dpy_xor_rectangle(s, x1, y1, x2 - x1, y2 - y1, color);
    }
}

/* set the clip rectangle (and does not clip by the previous one) */
void set_clip_rectangle(QEditScreen *s, CSSRect *r)
{
    int x1, y1, x2, y2;

    x1 = r->x1;
    y1 = r->y1;
    x2 = r->x2;
    y2 = r->y2;

    if (x2 > s->width)
        x2 = s->width;
    if (y2 > s->height)
        y2 = s->height;
    if (x1 < 0)
        x1 = 0;
    if (y1 < 0)
        y1 = 0;

    s->clip_x1 = x1;
    s->clip_y1 = y1;
    s->clip_x2 = x2;
    s->clip_y2 = y2;

    s->dpy.dpy_set_clip(s, x1, y1, x2 - x1, y2 - y1);
}

void push_clip_rectangle(QEditScreen *s, CSSRect *r0, CSSRect *r)
{
    int x1, y1, x2, y2;

    /* save old rectangle */
    r0->x1 = s->clip_x1;
    r0->y1 = s->clip_y1;
    r0->x2 = s->clip_x2;
    r0->y2 = s->clip_y2;

    /* load and clip new rectangle against the current one */
    x1 = r->x1;
    y1 = r->y1;
    x2 = r->x2;
    y2 = r->y2;

    if (x2 > s->clip_x2)
        x2 = s->clip_x2;
    if (y2 > s->clip_y2)
        y2 = s->clip_y2;
    if (x1 < s->clip_x1)
        x1 = s->clip_x1;
    if (y1 < s->clip_y1)
        y1 = s->clip_y1;

    /* set new rectangle */
    s->clip_x1 = x1;
    s->clip_y1 = y1;
    s->clip_x2 = x2;
    s->clip_y2 = y2;

    s->dpy.dpy_set_clip(s, x1, y1, x2 - x1, y2 - y1);
}

int qe_register_display(QEDisplay *dpy)
{
    QEDisplay **pp;

    pp = &first_dpy;
    while (*pp != NULL)
        pp = &(*pp)->next;
    *pp = dpy;
    dpy->next = NULL;
    return 0;
}

QEDisplay *probe_display(void)
{
    QEDisplay *p, *dpy;
    int probe_max, probe;

    p = first_dpy;
    dpy = NULL;
    probe_max = 0;
    while (p != NULL) {
        probe = p->dpy_probe ? p->dpy_probe() : 0;
        if (probe >= probe_max) {
            probe_max = probe;
            dpy = p;
        }
        p = p->next;
    }
    return dpy;
}

int screen_init(QEditScreen *s, QEDisplay *dpy, int w, int h)
{
    s->dpy = dpy ? *dpy : dummy_dpy;
    return s->dpy.dpy_init(s, w, h);
}

/* simple font cache */

#define FONT_CACHE_SIZE 32
static QEFont dummy_font;
static QEFont *font_cache[FONT_CACHE_SIZE];
static int font_cache_timestamp = 0;

void free_font_cache(QEditScreen *s)
{
    int i;
    for (i = 0; i < FONT_CACHE_SIZE; i++) {
        close_font(s, &font_cache[i]);
    }
}

QEFont *select_font(QEditScreen *s, int style, int size)
{
    QEFont *fc;
    int i, min_ts, min_index;

    min_ts = INT_MAX;
    min_index = -1;
    for (i = 0; i < FONT_CACHE_SIZE; i++) {
        fc = font_cache[i];
        if (fc) {
            if (fc->style == style && fc->size == size) {
                goto found;
            }
            if (fc->timestamp < min_ts && fc->refcount <= 0) {
                min_ts = fc->timestamp;
                min_index = i;
            }
        } else {
            min_ts = 0;
            min_index = i;
        }
    }
    /* not found : open new font */
    if (min_index < 0) {
        put_error(NULL, "Font cache full");
        goto fail;
    }
    if (font_cache[min_index]) {
        close_font(s, &font_cache[min_index]);
    }
    fc = open_font(s, style, size);
    if (!fc) {
        if (style & QE_FONT_FAMILY_FALLBACK_MASK)
            return NULL;

        put_error(NULL, "open_font: cannot open style=%X size=%d",
                  style, size);
        goto fail;
    }

    fc->style = style;
    fc->size = size;
    font_cache[min_index] = fc;
 found:
    fc->timestamp = font_cache_timestamp;
    font_cache_timestamp++;
    fc->refcount++;
    return fc;

 fail:
    /* select_font never returns NULL */
    /* CG: This is bogus, dummy font is not device compatible? */
    fc = &dummy_font;
    fc->system_font = 1;
    return fc;
}

QEBitmap *bmp_alloc(QEditScreen *s, int width, int height, int flags)
{
    QEBitmap *b;

    if (!s->dpy.dpy_bmp_alloc)
        return NULL;

    b = qe_mallocz(QEBitmap);
    if (!b)
        return NULL;
    b->width = width;
    b->height = height;
    b->flags = flags;
    if (s->dpy.dpy_bmp_alloc(s, b) < 0) {
        qe_free(&b);
        return NULL;
    }
    return b;
}

void bmp_free(QEditScreen *s, QEBitmap **bp)
{
    if (*bp) {
        s->dpy.dpy_bmp_free(s, *bp);
        qe_free(bp);
    }
}

#if 0
/* bitmap cache */
typedef struct QECachedBitmap {
    QEBitmap *bitmap;
    char url[1];
    int refcount;
    struct QECachedBitmap *next;
} QECachedBitmap;

/* dst_w or dst_h can be zero if unspecified */
/* XXX: add scaling for printing */
QECachedBitmap *cbmp_open(QEditScreen *s,
                          const char *url,
                          int dst_w, int dst_h)
{
}

/* return the DESTINATION size of the bitmap. return non zero if
   unknown size */
int cbmp_get_size(QECachedBitmap *bitmap, int *w_ptr, int *h_ptr)
{
}

void cbmp_draw(QEditScreen *s, QECachedBitmap *bitmap,
               int dst_x, int dst_y, int dst_w, int dst_h,
               int offset_x, int offset_y, int flags)
{
}

void cbmp_close(QEditScreen *cbmp)
{
}
#endif

/* draw only the border of a rectangle */
void fill_border(EditState *s, int x, int y, int w, int h, int color)
{
    int x0, y0, w0, h0, w1, w2, h1, h2;

    /* fill the background */
    x0 = s->xleft;
    y0 = s->ytop;
    w0 = s->width;
    h0 = s->height;
    w1 = max(0, x);
    w2 = max(0, w0 - (x + w));
    h1 = max(0, y);
    h2 = max(0, h0 - (y + h));

    if (w1) fill_rectangle(s->screen, x0, y0, w1, h0, color);
    if (w2) fill_rectangle(s->screen, x0 + w0 - w2, y0, w2, h0, color);
    if (h1) fill_rectangle(s->screen, x0 + w1, y0, w0 - w1 - w2, h1, color);
    if (h2) fill_rectangle(s->screen, x0 + w1, y0 + h0 - h2, w0 - w1 - w2, h2, color);
}

