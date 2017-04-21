/*
 * QEmacs, tiny but powerful multimode editor
 *
 * Copyright (c) 2000-2002 Fabrice Bellard.
 * Copyright (c) 2000-2017 Charlie Gordon.
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
#include "variables.h"

/* Search stuff */

#define SEARCH_FLAG_SMARTCASE  0x0001  /* case fold unless upper case present */
#define SEARCH_FLAG_IGNORECASE 0x0002
#define SEARCH_FLAG_WORD       0x0004
#define SEARCH_FLAG_WRAPPED    0x0008
#define SEARCH_FLAG_HEX        0x0010
#define SEARCH_FLAG_UNIHEX     0x0020
#define SEARCH_FLAG_REGEX      0x0040

/* should separate search string length and number of match positions */
#define SEARCH_LENGTH  256
#define FOUND_TAG      0x80000000
#define FOUND_REV      0x40000000

struct ISearchState {
    EditState *s;
    int saved_mark;
    int start_offset;
    int start_dir;
    int quoting;
    int dir;
    int pos;  /* position in search_u32_flags */
    int search_u32_len;
    int search_flags;
    int found_offset, found_end;
    unsigned int search_u32_flags[SEARCH_LENGTH];
    unsigned int search_u32[SEARCH_LENGTH];
};

/* XXX: should store to screen */
ISearchState isearch_state;

/* last searched string */
/* XXX: should store in a buffer as a list */
static unsigned int last_search_u32[SEARCH_LENGTH];
static int last_search_u32_len = 0;
static int last_search_u32_flags = 0;

static int eb_search(EditBuffer *b, int dir, int flags,
                     int start_offset, int end_offset,
                     const unsigned int *buf, int len,
                     CSSAbortFunc *abort_func, void *abort_opaque,
                     int *found_offset, int *found_end)
{
    int total_size = b->total_size;
    int c, c2, offset = start_offset, offset1, offset2, offset3, pos;

    if (len == 0)
        return 0;

    if (end_offset > total_size)
        end_offset = total_size;

    *found_offset = -1;
    *found_end = -1;

    /* analyze buffer if smart case */
    if (flags & SEARCH_FLAG_SMARTCASE) {
        int upper_count = 0;
        int lower_count = 0;
        for (pos = 0; pos < len; pos++) {
            lower_count += qe_islower(buf[pos]);
            upper_count += qe_isupper(buf[pos]);
        }
        if (lower_count > 0 && upper_count == 0)
            flags |= SEARCH_FLAG_IGNORECASE;
    }

    if (flags & SEARCH_FLAG_HEX) {
        /* handle buffer as single bytes */
        /* XXX: should handle ucs2 and ucs4 as words */
        if (dir >= 0)
              offset--;
        for (;;) {
            if (dir < 0) {
                if (offset == 0)
                    return 0;
                offset--;
            } else {
                offset++;
                if (offset >= end_offset)
                    return 0;
            }
            if ((offset & 0xfffff) == 0) {
                /* check for search abort every megabyte */
                if (abort_func && abort_func(abort_opaque))
                    return -1;
            }

            pos = 0;
            for (offset2 = offset; offset2 < total_size;) {
                /* CG: Should bufferize a bit ? */
                c = eb_read_one_byte(b, offset2++);
                c2 = buf[pos++];
                if (c != c2)
                    break;
                if (pos >= len) {
                    if (dir >= 0 || offset2 <= start_offset) {
                        *found_offset = offset;
                        *found_end = offset2;
                        return 1;
                    }
                }
            }
        }
    }

    for (offset1 = offset;;) {
        if (dir < 0) {
            if (offset == 0)
                return 0;
            offset = eb_prev(b, offset);
        } else {
            offset = offset1;
            if (offset >= end_offset)
                return 0;
        }
        if ((offset & 0xfffff) == 0) {
            /* check for search abort every megabyte */
            if (abort_func && abort_func(abort_opaque))
                return -1;
        }

        /* CG: XXX: Should use buffer specific accelerator */
        /* Get first char separately to compute offset1 */
        c = eb_nextc(b, offset, &offset1);

        pos = 0;
        for (offset2 = offset1;;) {
            c2 = buf[pos++];
            if (flags & SEARCH_FLAG_IGNORECASE) {
                if (qe_toupper(c) != qe_toupper(c2))
                    break;
            } else {
                if (c != c2)
                    break;
            }
            if (pos >= len) {
                if (flags & SEARCH_FLAG_WORD) {
                    /* check for word boundaries */
                    if (qe_isword(eb_prevc(b, offset, &offset3))
                    ||  qe_isword(eb_nextc(b, offset2, &offset3)))
                        break;
                }
                if (dir >= 0 || offset2 <= start_offset) {
                    *found_offset = offset;
                    *found_end = offset2;
                    return 1;
                }
            }
            if (offset2 >= total_size)
                break;
            c = eb_nextc(b, offset2, &offset2);
        }
    }
}

static int search_abort_func(qe__unused__ void *opaque)
{
    return is_user_input_pending();
}

static void buf_encode_search_u32(buf_t *out, const unsigned int *str, int len)
{
    int i;

    for (i = 0; i < len; i++) {
        unsigned int v = str[i];
        if (v < 32 || v == 127) {
            buf_printf(out, "^%c", (v + '@') & 127);
        } else {
            buf_putc_utf8(out, v);
        }
        if (buf_avail(out) <= 0)
            break;
    }
}

static void buf_encode_search_str(buf_t *out, const char *str)
{
    while (*str) {
        int c = utf8_decode(&str);
        if (c < 32 || c == 127) {
            buf_printf(out, "^%c", (c + '@') & 127);
        } else {
            buf_putc_utf8(out, c);
        }
        if (buf_avail(out) <= 0)
            break;
    }
}

static void buf_disp_search_flags(buf_t *out, int search_flags) {
    if (search_flags & SEARCH_FLAG_UNIHEX)
        buf_puts(out, "Unihex ");
    if (search_flags & SEARCH_FLAG_HEX)
        buf_puts(out, "Hex ");
    if (search_flags & SEARCH_FLAG_IGNORECASE)
        buf_puts(out, "Folding ");
    else
    if (!(search_flags & SEARCH_FLAG_SMARTCASE))
        buf_puts(out, "Exact ");
    if (search_flags & SEARCH_FLAG_REGEX)
        buf_puts(out, "Regex ");
    if (search_flags & SEARCH_FLAG_WORD)
        buf_puts(out, "Word ");
}

static void isearch_run(ISearchState *is)
{
    EditState *s = is->s;
    char ubuf[256];
    buf_t outbuf, *out;
    int c, i, len, hex_nibble, max_nibble, h, hc;
    unsigned int v;
    int search_offset, flags, dir = is->start_dir;
    int start_time, elapsed_time;

    start_time = get_clock_ms();

    /* prepare the search bytes */
    len = 0;
    search_offset = is->start_offset;
    max_nibble = hex_nibble = hc = 0;
    flags = is->search_flags;
    if (flags & SEARCH_FLAG_UNIHEX)
        max_nibble = 6;
    if (flags & SEARCH_FLAG_HEX)
        max_nibble = 2;

    for (i = 0; i < is->pos; i++) {
        v = is->search_u32_flags[i];
        if (v & FOUND_TAG) {
            dir = (v & FOUND_REV) ? -1 : 1;
            search_offset = v & ~(FOUND_TAG | FOUND_REV);
            continue;
        }
        c = v;
        if (len < countof(is->search_u32)) {
            if (max_nibble) {
                h = to_hex(c);
                if (h >= 0) {
                    hc = (hc << 4) | h;
                    if (++hex_nibble == max_nibble) {
                        is->search_u32[len++] = hc;
                        hex_nibble = hc = 0;
                    }
                } else {
                    if (c == ' ' && hex_nibble) {
                        is->search_u32[len++] = hc;
                        hex_nibble = hc = 0;
                    }
                }
            } else {
                is->search_u32[len++] = c;
            }
        }
    }
    if (hex_nibble >= 2 && len < countof(is->search_u32)) {
        is->search_u32[len++] = hc;
        hex_nibble = hc = 0;
    }

    is->search_u32_len = len;
    is->dir = dir;

    if (len == 0) {
        s->b->mark = is->saved_mark;
        s->offset = is->start_offset;
        s->region_style = 0;
        is->found_offset = -1;
    } else {
        if (eb_search(s->b, is->dir, flags,
                      search_offset, s->b->total_size,
                      is->search_u32, is->search_u32_len,
                      search_abort_func, NULL,
                      &is->found_offset, &is->found_end) > 0) {
            s->region_style = QE_STYLE_SEARCH_MATCH;
            if (is->dir >= 0) {
                s->b->mark = is->found_offset;
                s->offset = is->found_end;
            } else {
                s->b->mark = is->found_end;
                s->offset = is->found_offset;
            }
        }
    }

    /* display search string */
    out = buf_init(&outbuf, ubuf, sizeof(ubuf));
    if (is->found_offset < 0 && len > 0)
        buf_puts(out, "Failing ");
    else
    if (is->search_flags & SEARCH_FLAG_WRAPPED) {
        buf_puts(out, "Wrapped ");
        is->search_flags &= ~SEARCH_FLAG_WRAPPED;
    }
    buf_disp_search_flags(out, is->search_flags);
    buf_puts(out, "I-search");
    if (is->dir < 0)
        buf_puts(out, " backward");
    buf_puts(out, ": ");
    buf_encode_search_u32(out, is->search_u32, is->search_u32_len);
    if (is->quoting)
        buf_puts(out, "^Q-");

    /* display text */
    do_center_cursor(s, 0);
    edit_display(s->qe_state);
    put_status(NULL, "%s", out->buf);   /* XXX: why NULL? */
    elapsed_time = get_clock_ms() - start_time;
    if (elapsed_time >= 100)
        put_status(s, "|isearch_run: %dms", elapsed_time);

    dpy_flush(s->screen);
}

static int isearch_grab(ISearchState *is, EditBuffer *b, int from, int to)
{
    int offset, c, last = is->pos;
    if (b) {
        if (to < 0 || to > b->total_size)
            to = b->total_size;
        for (offset = from; is->pos < SEARCH_LENGTH && offset < to;) {
            c = eb_nextc(b, offset, &offset);
            is->search_u32_flags[is->pos++] = c;
        }
    }
    return is->pos - last;
}

static void isearch_key(void *opaque, int ch)
{
    ISearchState *is = opaque;
    EditState *s = is->s;
    QEmacsState *qs = &qe_state;
    int offset0, offset1, curdir = is->dir;
    int emacs_behaviour = !qs->emulation_flags;

    if (is->quoting) {
        is->quoting = 0;
        if (!KEY_IS_SPECIAL(ch))
            goto addch;
    }
    /* XXX: all these should be isearch-mode bindings */
    switch (ch) {
    case KEY_DEL:
    case KEY_BS:
        /* cancel last input item from search string */
        if (is->pos > 0)
            is->pos--;
        break;
    case KEY_CTRL('g'):
        /* XXX: when search has failed should cancel input back to what has been
         * found successfully.
         * when search is successful aborts and moves point to starting point.
         */
        s->b->mark = is->saved_mark;
        s->offset = is->start_offset;
        s->region_style = 0;
        s->isearch_state = NULL;
        put_status(s, "Quit");
    the_end:
        /* save current searched string */
        if (is->search_u32_len > 0) {
            memcpy(last_search_u32, is->search_u32,
                   is->search_u32_len * sizeof(*is->search_u32));
            last_search_u32_len = is->search_u32_len;
            last_search_u32_flags = is->search_flags;
        }
        qe_ungrab_keys();
        edit_display(s->qe_state);
        dpy_flush(s->screen);
        return;
    case KEY_CTRL('s'):         /* next match */
        is->dir = 1;
        goto addpos;
    case KEY_CTRL('r'):         /* previous match */
        is->dir = -1;
    addpos:
        /* use last searched string if no input */
        if (is->search_u32_len == 0 && is->dir == curdir) {
            int len = min(last_search_u32_len, SEARCH_LENGTH - is->pos);
            memcpy(is->search_u32_flags + is->pos, last_search_u32,
                   len * sizeof(*is->search_u32_flags));
            is->pos += len;
            is->search_flags = last_search_u32_flags;
        } else
        if (is->pos < SEARCH_LENGTH) {
            /* add the match position, if any */
            unsigned long v = (is->dir >= 0) ? FOUND_TAG : FOUND_TAG | FOUND_REV;
            if (is->found_offset < 0 && is->search_u32_len > 0) {
                is->search_flags |= SEARCH_FLAG_WRAPPED;
                if (is->dir < 0)
                    v |= s->b->total_size;
            } else {
                v |= s->offset;
            }
            is->search_u32_flags[is->pos++] = v;
        }
        break;
    case KEY_CTRL('q'):
        is->quoting = 1;
        break;
    case KEY_META('w'):
        emacs_behaviour ^= 1;
        /* fall thru */
    case KEY_CTRL('w'):
        if (emacs_behaviour) {
            /* grab word at cursor */
            offset0 = s->offset;
            do_word_right(s, 1);
            offset1 = s->offset;
            s->offset = offset0;
            isearch_grab(is, s->b, offset0, offset1);
        } else {
            /* toggle word match */
            is->search_flags ^= SEARCH_FLAG_WORD;
        }
        break;
    case KEY_META('y'):
        emacs_behaviour ^= 1;
        /* fall thru */
    case KEY_CTRL('y'):
        if (emacs_behaviour) {
            /* grab line at cursor */
            offset0 = s->offset;
            if (eb_nextc(s->b, offset0, &offset1) == '\n')
                offset0 = offset1;
            do_eol(s);
            offset1 = s->offset;
            s->offset = offset0;
            isearch_grab(is, s->b, offset0, offset1);
        } else {
            /* yank into search string */
            isearch_grab(is, qs->yank_buffers[qs->yank_current], 0, -1);
        }
        break;
    case KEY_META(KEY_CTRL('b')):
        /* cycle unihex, hex, normal search */
        if (is->search_flags & SEARCH_FLAG_UNIHEX)
            is->search_flags ^= SEARCH_FLAG_HEX | SEARCH_FLAG_UNIHEX;
        else
        if (is->search_flags & SEARCH_FLAG_HEX)
            is->search_flags ^= SEARCH_FLAG_HEX;
        else
            is->search_flags ^= SEARCH_FLAG_UNIHEX;
        break;
    case KEY_META('c'):
    case KEY_CTRL('c'):
        /* toggle case sensitivity */
        if (is->search_flags & (SEARCH_FLAG_IGNORECASE | SEARCH_FLAG_SMARTCASE)) {
            is->search_flags &= ~SEARCH_FLAG_IGNORECASE;
        } else {
            is->search_flags |= SEARCH_FLAG_IGNORECASE;
        }
        is->search_flags &= ~SEARCH_FLAG_SMARTCASE;
        break;
    case KEY_META('r'):
    case KEY_CTRL('t'):
        is->search_flags ^= ~SEARCH_FLAG_REGEX;
        break;
    case KEY_CTRL('l'):
        do_center_cursor(s, 1);
        break;
    default:
        if ((KEY_IS_SPECIAL(ch) || KEY_IS_CONTROL(ch)) &&
            ch != '\t' && ch != KEY_CTRL('j')) {
            /* exit search mode */
#if 0
            // FIXME: behaviour from qemacs-0.3pre13
            if (is->found_offset >= 0) {
                s->b->mark = is->found_offset;
            } else {
                s->b->mark = is->start_offset;
            }
            put_status(s, "Marked");
#endif
            s->b->mark = is->start_offset;
            s->region_style = 0;
            put_status(s, "Mark saved where search started");
            /* repost key */
            /* do not keep search matches lingering */
            s->isearch_state = NULL;
            if (ch != KEY_RET) {
                unget_key(ch);
            }
            goto the_end;
        } else {
        addch:
            if (is->pos < SEARCH_LENGTH) {
                is->search_u32_flags[is->pos++] = ch;
            }
        }
        break;
    }
    isearch_run(is);
}

/* XXX: handle busy */
void do_isearch(EditState *s, int dir, int argval)
{
    ISearchState *is = &isearch_state;
    EditState *e;
    int flags = SEARCH_FLAG_SMARTCASE;

    /* prevent search from minibuffer */
    if (s->flags & WF_MINIBUF)
        return;

    /* stop displaying search matches on last window */
    e = check_window(&is->s);
    if (e) {
        e->isearch_state = NULL;
    }

    memset(is, 0, sizeof(isearch_state));
    s->isearch_state = is;
    is->s = s;
    is->saved_mark = s->b->mark;
    is->start_offset = s->offset;
    is->start_dir = is->dir = dir;
    if (s->hex_mode) {
        if (s->unihex_mode)
            flags |= SEARCH_FLAG_UNIHEX;
        else
            flags |= SEARCH_FLAG_HEX;
    }
    if (argval != NO_ARG)
        flags |= SEARCH_FLAG_REGEX;

    is->search_flags = flags;

    qe_grab_keys(isearch_key, is);
    isearch_run(is);
}

void isearch_colorize_matches(EditState *s, unsigned int *buf, int len,
                              QETermStyle *sbuf, int offset_start)
{
    ISearchState *is = s->isearch_state;
    EditBuffer *b = s->b;
    int offset, char_offset, found_offset, found_end, offset_end;

    if (!is || is->search_u32_len <= 0)
        return;

    char_offset = eb_get_char_offset(b, offset_start);
    offset_end = eb_goto_char(b, char_offset + len);
    offset = 0;
    if (char_offset > is->search_u32_len + 1)
        offset = eb_goto_char(b, char_offset - is->search_u32_len - 1);

    while (eb_search(b, 1, is->search_flags, offset, offset_end,
                     is->search_u32, is->search_u32_len, NULL, NULL,
                     &found_offset, &found_end) > 0) {
        int line, start, stop, i;

        if (found_offset >= offset_end)
            break;
        if (found_end > offset_start) {
            /* Compute character positions */
            start = 0;
            if (found_offset > offset_start)
                eb_get_pos(b, &line, &start, found_offset);
            stop = len;
            if (found_end < offset_end) {
                eb_get_pos(b, &line, &stop, found_end);
                if (stop > len)
                    stop = len;
            }
            for (i = start; i < stop; i++) {
                sbuf[i] = QE_STYLE_SEARCH_HILITE;
            }
        }
        offset = found_end;
    }
}

static int search_to_u32(unsigned int *buf, int size,
                         const char *str, int flags)
{
    if (flags & (SEARCH_FLAG_HEX | SEARCH_FLAG_UNIHEX)) {
        /* CG: XXX: Should mix utf8 and hex syntax in hex modes */
        const char *s;
        int c, hex_nibble, max_nibble, h, hc, len;

        max_nibble = (flags & SEARCH_FLAG_UNIHEX) ? 6 : 2;
        s = str;
        hex_nibble = hc = 0;
        for (len = 0; len < size;) {
            c = *s++;
            if (c == '\0') {
                if (hex_nibble >= 2) {
                    buf[len++] = hc;
                    hex_nibble = hc = 0;
                }
                break;
            }
            h = to_hex(c);
            if (h >= 0) {
                hc = (hc << 4) | h;
                if (++hex_nibble == max_nibble) {
                    buf[len++] = hc;
                    hex_nibble = hc = 0;
                }
            } else {
                if (c == ' ' && hex_nibble) {
                    buf[len++] = hc;
                    hex_nibble = hc = 0;
                }
            }
        }
        return len;
    } else {
        return utf8_to_unicode(buf, size, str);
    }
}

typedef struct QueryReplaceState {
    EditState *s;
    int start_offset;
    int search_flags;
    int replace_all;
    int nb_reps;
    int search_u32_len, replace_u32_len;
    int found_offset, found_end;
    int last_offset;
    char search_str[SEARCH_LENGTH];     /* may be in hex */
    char replace_str[SEARCH_LENGTH];    /* may be in hex */
    unsigned int search_u32[SEARCH_LENGTH];   /* code points */
    unsigned int replace_u32[SEARCH_LENGTH];  /* code points */
} QueryReplaceState;

static void query_replace_abort(QueryReplaceState *is)
{
    EditState *s = is->s;

    qe_ungrab_keys();
    s->b->mark = is->start_offset;
    s->region_style = 0;
    put_status(NULL, "Replaced %d occurrences", is->nb_reps);
    qe_free(&is);
    edit_display(s->qe_state);
    dpy_flush(s->screen);
}

static void query_replace_replace(QueryReplaceState *is)
{
    EditState *s = is->s;

    /* XXX: handle smart case replacement */
    is->nb_reps++;
    eb_delete_range(s->b, is->found_offset, is->found_end);
    is->found_offset += eb_insert_u32_buf(s->b, is->found_offset,
        is->replace_u32, is->replace_u32_len);
}

static void query_replace_display(QueryReplaceState *is)
{
    EditState *s = is->s;
    char ubuf[256];
    buf_t outbuf, *out;

    is->last_offset = is->found_offset;
    is->search_u32_len = search_to_u32(is->search_u32,
                                       countof(is->search_u32),
                                       is->search_str, is->search_flags);
    is->replace_u32_len = search_to_u32(is->replace_u32,
                                        countof(is->replace_u32),
                                        is->replace_str, is->search_flags);

    for (;;) {
        if (eb_search(s->b, 1, is->search_flags,
                      is->found_offset, s->b->total_size,
                      is->search_u32, is->search_u32_len,
                      NULL, NULL, &is->found_offset, &is->found_end) <= 0) {
            query_replace_abort(is);
            return;
        }
        if (is->replace_all) {
            query_replace_replace(is);
            continue;
        }
        break;
    }
    /* display prompt string */
    out = buf_init(&outbuf, ubuf, sizeof(ubuf));
    buf_disp_search_flags(out, is->search_flags);
    buf_puts(out, "Query replace ");
    buf_encode_search_str(out, is->search_str);
    buf_puts(out, " with ");
    buf_encode_search_str(out, is->replace_str);
    buf_puts(out, ": ");

    s->offset = is->found_end;
    s->b->mark = is->found_offset;
    s->region_style = QE_STYLE_SEARCH_MATCH;
    do_center_cursor(s, 0);
    edit_display(s->qe_state);
    put_status(NULL, "%s", out->buf);
    dpy_flush(s->screen);
}

static void query_replace_key(void *opaque, int ch)
{
    QueryReplaceState *is = opaque;
    EditState *s = is->s;
    QEmacsState *qs = &qe_state;

    switch (ch) {
    case 'Y':
    case 'y':
    case KEY_SPC:
        query_replace_replace(is);
        s->offset = is->found_offset;
        break;
    case '!':
        is->replace_all = 1;
        break;
    case 'N':
    case 'n':
    case KEY_DELETE:
        is->found_offset = is->found_end;
        break;
    case KEY_META('w'):
    case KEY_CTRL('w'):
        /* toggle word match */
        is->search_flags ^= SEARCH_FLAG_WORD;
        is->found_offset = is->last_offset;
        break;
    case KEY_META('b'):
    case KEY_CTRL('b'):
        /* cycle unihex, hex, normal search */
        if (is->search_flags & SEARCH_FLAG_UNIHEX)
            is->search_flags ^= SEARCH_FLAG_HEX | SEARCH_FLAG_UNIHEX;
        else
        if (is->search_flags & SEARCH_FLAG_HEX)
            is->search_flags ^= SEARCH_FLAG_HEX;
        else
            is->search_flags ^= SEARCH_FLAG_UNIHEX;
        is->found_offset = is->last_offset;
        break;
    case KEY_META('c'):
    case KEY_CTRL('c'):
        /* toggle case sensitivity */
        if (is->search_flags & (SEARCH_FLAG_IGNORECASE | SEARCH_FLAG_SMARTCASE)) {
            is->search_flags &= ~SEARCH_FLAG_IGNORECASE;
        } else {
            is->search_flags |= SEARCH_FLAG_IGNORECASE;
        }
        is->search_flags &= ~SEARCH_FLAG_SMARTCASE;
        is->found_offset = is->last_offset;
        break;
    case KEY_CTRL('g'):
        /* abort */
        if (qs->emulation_flags) {
            /* restore point to original location */
            s->offset = is->start_offset;
        }
        query_replace_abort(is);
        return;
    case KEY_CTRL('l'):
        do_center_cursor(s, 1);
        break;
    case '.':
        query_replace_replace(is);
        s->offset = is->found_offset;
        /* FALL THRU */
    default:
        query_replace_abort(is);
        return;
    }
    query_replace_display(is);
}

static void query_replace(EditState *s, const char *search_str,
                          const char *replace_str, int all, int flags)
{
    QueryReplaceState *is;

    /* prevent replace from minibuffer */
    if (s->flags & WF_MINIBUF)
        return;

    if (s->b->flags & BF_READONLY)
        return;

    is = qe_mallocz(QueryReplaceState);
    if (!is)
        return;
    is->s = s;
    pstrcpy(is->search_str, sizeof(is->search_str), search_str);
    pstrcpy(is->replace_str, sizeof(is->replace_str), replace_str);

    if (s->hex_mode) {
        if (s->unihex_mode)
            flags = SEARCH_FLAG_UNIHEX;
        else
            flags = SEARCH_FLAG_HEX;
    }
    is->search_flags = flags;
    is->replace_all = all;
    is->start_offset = is->last_offset = s->offset;
    is->found_offset = is->found_end = s->offset;

    qe_grab_keys(query_replace_key, is);
    query_replace_display(is);
}

void do_query_replace(EditState *s, const char *search_str,
                      const char *replace_str)
{
    int flags = SEARCH_FLAG_SMARTCASE;
    query_replace(s, search_str, replace_str, 0, flags);
}

void do_replace_string(EditState *s, const char *search_str,
                       const char *replace_str, int argval)
{
    int flags = SEARCH_FLAG_SMARTCASE;
    if (argval != NO_ARG)
        flags |= SEARCH_FLAG_WORD;
    query_replace(s, search_str, replace_str, 1, flags);
}

/* dir = 0, -1, 1, 2 -> count matches, reverse, forward, delete-matching-lines */
void do_search_string(EditState *s, const char *search_str, int dir)
{
    unsigned int search_u32[SEARCH_LENGTH];
    int search_u32_len;
    int found_offset, found_end;
    int flags = SEARCH_FLAG_SMARTCASE;
    int offset, count = 0;

    if (s->hex_mode) {
        if (s->unihex_mode)
            flags |= SEARCH_FLAG_UNIHEX;
        else
            flags |= SEARCH_FLAG_HEX;
    }
    search_u32_len = search_to_u32(search_u32, countof(search_u32),
                                   search_str, flags);
    /* empty string matches */
    if (search_u32_len <= 0)
        return;

    for (offset = s->offset;;) {
        if (eb_search(s->b, dir, flags,
                      offset, s->b->total_size,
                      search_u32, search_u32_len,
                      NULL, NULL, &found_offset, &found_end) > 0) {
            count++;
            if (dir == 0) {
                offset = found_end;
                continue;
            } else
            if (dir == 2) {
                offset = eb_goto_bol(s->b, found_offset);
                eb_delete_range(s->b, offset,
                                eb_next_line(s->b, found_offset));
                continue;
            } else {
                s->offset = (dir < 0) ? found_offset : found_end;
                do_center_cursor(s, 0);
                return;
            }
        } else {
            if (dir == 0) {
                put_status(s, "%d matches", count);
            } else
            if (dir == 2) {
                put_status(s, "deleted %d lines", count);
            } else {
                put_status(s, "Search failed: \"%s\"", search_str);
            }
            return;
        }
    }
}

static CmdDef search_commands[] = {

    /*---------------- Search and replace ----------------*/

    /* M-C-s should be bound to isearch-forward-regex */
    /* mg binds search-forward to M-s */
    CMD3( KEY_META('S'), KEY_NONE,
          "search-forward", do_search_string, ESsi, 1,
          "s{Search forward: }|search|"
          "v")
    /* M-C-r should be bound to isearch-backward-regex */
    /* mg binds search-forward to M-r */
    CMD3( KEY_META('R'), KEY_NONE,
          "search-backward", do_search_string, ESsi, -1,
          "s{Search backward: }|search|"
          "v")
    CMD3( KEY_META('C'), KEY_NONE,
          "count-matches", do_search_string, ESsi, 0,
          "s{Count Matches: }|search|"
          "v")
    CMD3( KEY_NONE, KEY_NONE,
          "delete-matching-lines", do_search_string, ESsi, 2,
          "s{Delete lines containing: }|search|"
          "v")
    /* passing argument should switch to regex incremental search */
    CMD3( KEY_CTRL('r'), KEY_NONE,
          "isearch-backward", do_isearch, ESii, -1, "vui" )
    CMD3( KEY_CTRL('s'), KEY_NONE,
          "isearch-forward", do_isearch, ESii, 1, "vui" )
    CMD2( KEY_META('%'), KEY_NONE,
          "query-replace", do_query_replace, ESss,
          "*" "s{Query replace: }|search|"
          "s{With: }|replace|")
    /* passing argument restricts replace to word matches */
    /* XXX: non standard binding */
    CMD2( KEY_META('r'), KEY_NONE,
          "replace-string", do_replace_string, ESssi,
          "*" "s{Replace String: }|search|"
          "s{With: }|replace|"
          "ui")

    CMD_DEF_END,
};

static int search_init(void) {
    qe_register_cmd_table(search_commands, NULL);
    return 0;
}

qe_module_init(search_init);
