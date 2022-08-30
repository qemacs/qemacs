/*
 * Incremental search and replace for QEmacs.
 *
 * Copyright (c) 2000-2002 Fabrice Bellard.
 * Copyright (c) 2000-2022 Charlie Gordon.
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

#define SEARCH_FLAG_IGNORECASE 0x0001
#define SEARCH_FLAG_SMARTCASE  0x0002  /* case fold unless upper case present */
#define SEARCH_FLAG_WORD       0x0004
#define SEARCH_FLAG_WRAPPED    0x0008
#define SEARCH_FLAG_HEX        0x0010
#define SEARCH_FLAG_UNIHEX     0x0020
#define SEARCH_FLAG_REGEX      0x0040
#define SEARCH_FLAG_ACTIVE     0x1000

/* should separate search string length and number of match positions */
#define SEARCH_LENGTH  256
#define FOUND_TAG      0x80000000
#define FOUND_REV      0x40000000

struct ISearchState {
    EditState *s;
    int search_flags;
    int start_offset;
    int found_offset, found_end;
    int search_u32_len;
    /* isearch */
    int saved_mark;
    int start_dir;
    int quoting;
    int dir;
    int pos;  /* position in search_u32_flags */
    unsigned int search_u32_flags[SEARCH_LENGTH];
    unsigned int search_u32[SEARCH_LENGTH];
};

static ModeDef isearch_mode;

/* XXX: should store to screen */
static ISearchState global_isearch_state;

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
    int search_offset, flags, dir;
    int start_time, elapsed_time;

    flags = is->search_flags;
    if (!(flags & SEARCH_FLAG_ACTIVE))
        return;

    start_time = get_clock_ms();

    /* prepare the search bytes */
    len = 0;
    dir = is->start_dir;
    search_offset = is->start_offset;
    max_nibble = hex_nibble = hc = 0;

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
                h = qe_digit_value(c);
                if (h < 16) {
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

static void isearch_yank_word(ISearchState *is) {
    EditState *s = is->s;
    int offset0, offset1;
    offset0 = s->offset;
    do_word_left_right(s, 1);
    offset1 = s->offset;
    s->offset = offset0;
    isearch_grab(is, s->b, offset0, offset1);
}

static void isearch_yank_line(ISearchState *is) {
    EditState *s = is->s;
    int offset0, offset1;
    offset0 = s->offset;
    if (eb_nextc(s->b, offset0, &offset1) == '\n')
        offset0 = offset1;
    do_eol(s);
    offset1 = s->offset;
    s->offset = offset0;
    isearch_grab(is, s->b, offset0, offset1);
}

static void isearch_yank_kill(ISearchState *is) {
    QEmacsState *qs = is->s->qe_state;
    isearch_grab(is, qs->yank_buffers[qs->yank_current], 0, -1);
}

static void isearch_addpos(ISearchState *is, int dir) {
    /* use last searched string if no input */
    int curdir = is->dir;
    is->dir = dir;
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
                v |= is->s->b->total_size;
        } else {
            v |= is->s->offset;
        }
        is->search_u32_flags[is->pos++] = v;
    }
}

static void isearch_printing_char(ISearchState *is, int key) {
    if (is->pos < SEARCH_LENGTH)
        is->search_u32_flags[is->pos++] = key;
}

static void isearch_quote_char(ISearchState *is) {
    is->quoting = 1;
}

static void isearch_delete_char(ISearchState *is) {
    if (is->pos > 0)
        is->pos--;
}

static void isearch_center(ISearchState *is) {
    do_center_cursor(is->s, 1);
}

static void isearch_cycle_flags(ISearchState *is, int f1) {
    /* split the bits */
    int f2 = f1 & (f1 - 1);
    f1 &= ~f2;
    /* cycle search flags through 2 or 3 possibilities */
    if (is->search_flags & f1) {
        is->search_flags &= ~f1;
        is->search_flags |= f2;
    } else
    if (is->search_flags & f2) {
        is->search_flags &= ~f2;
    } else {
        is->search_flags |= f1;
    }
}

static void isearch_toggle_case_fold(ISearchState *is) {
    isearch_cycle_flags(is, SEARCH_FLAG_IGNORECASE | SEARCH_FLAG_SMARTCASE);
}

static void isearch_toggle_hex(ISearchState *is) {
    isearch_cycle_flags(is, SEARCH_FLAG_HEX | SEARCH_FLAG_UNIHEX);
}

static void isearch_toggle_regexp(ISearchState *is) {
    isearch_cycle_flags(is, SEARCH_FLAG_REGEX);
}

static void isearch_toggle_word_match(ISearchState *is) {
    isearch_cycle_flags(is, SEARCH_FLAG_WORD);
}

static void isearch_end(ISearchState *is) {
    EditState *s = is->s;
    /* save current searched string */
    // XXX: should save search strings to a history buffer
    if (is->search_u32_len > 0) {
        memcpy(last_search_u32, is->search_u32,
               is->search_u32_len * sizeof(*is->search_u32));
        last_search_u32_len = is->search_u32_len;
        last_search_u32_flags = is->search_flags;
    }
    is->search_flags &= ~SEARCH_FLAG_ACTIVE;
    qe_ungrab_keys();
    edit_display(s->qe_state);
    dpy_flush(s->screen);
}

static void isearch_abort(ISearchState *is) {
    EditState *s = is->s;
    /* XXX: when search has failed should cancel input back to what has been
     * found successfully.
     * when search is successful aborts and moves point to starting point.
     */
    s->b->mark = is->saved_mark;
    s->offset = is->start_offset;
    s->region_style = 0;
    s->isearch_state = NULL;
    put_status(s, "Quit");
    isearch_end(is);
}

static void isearch_cancel(ISearchState *is, int key) {
    EditState *s = is->s;
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
    if (key != KEY_RET) {
        unget_key(key);
    }
    isearch_end(is);
}

static void isearch_exit(ISearchState *is) {
    isearch_cancel(is, 0);
}

static void isearch_key(void *opaque, int key) {
#if 1
    ISearchState *is = opaque;
    unsigned int keys[1] = { key };

    if (is->quoting) {
        is->quoting = 0;
        if (!KEY_IS_SPECIAL(key)) {
            isearch_printing_char(is, key);
        }
    } else {
        KeyDef *kd = qe_find_binding(keys, 1, isearch_mode.first_key, 1);
        if (kd && kd->cmd->sig >= CMD_ISS) {
            exec_command(is->s, kd->cmd, NO_ARG, key, is);
        } else {
            if (KEY_IS_SPECIAL(key) || KEY_IS_CONTROL(key)) {
                isearch_cancel(is, key);
            } else {
                isearch_printing_char(is, key);
            }
        }
    }
    isearch_run(is);
#else
    ISearchState *is = opaque;
    int emacs_behaviour = !is->s->qe_state->emulation_flags;

    if (is->quoting) {
        is->quoting = 0;
        if (!KEY_IS_SPECIAL(key)) {
            isearch_printing_char(is, key);
            isearch_run(is);
            return;
        }
    }
    /* XXX: all these should be isearch-mode bindings */
    switch (key) {
#if 0
    case KEY_F1:    /* isearch-mode-help */
    case KEY_CTRL('h'):  // KEY_BS?
        // XXX: should display help on searching and ungrab keys temporarily
        isearch_mode_help(is);
        break;
    case KEY_META('p'): /* isearch-previous-string */
        isearch_previous_string(is);
        break;
    case KEY_META('n'): /* isearch-next-string */
        isearch_next_string(is);
        break;
#endif
    case KEY_DEL:
    case KEY_BS:
        /* isearch-delete-char: cancel last input item from search string */
        isearch_delete_char(is);
        break;
    case KEY_CTRL('g'):  /* isearch-abort */
        isearch_abort(is);
        return;
    case KEY_CTRL('s'):         /* isearch-next-match */
        isearch_addpos(is, 1);
        break;
    case KEY_CTRL('r'):         /* isearch-previous-match */
        isearch_addpos(is, -1);
        break;
    case KEY_CTRL('q'):     /* isearch-quote-char */
        isearch_quote_char(is);
        break;
    case KEY_META('w'):
        emacs_behaviour ^= 1;
        /* fall thru */
    case KEY_CTRL('w'):
        if (emacs_behaviour) {
            /* isearch-yank-word: grab word at cursor */
            isearch_yank_word(is);
        } else {
            /* isearch-toggle-word-match */
            isearch_toggle_word_match(is);
        }
        break;
    case KEY_META('y'):
        emacs_behaviour ^= 1;
        /* fall thru */
    case KEY_CTRL('y'):
        if (emacs_behaviour) {
            /* isearch-yank-line: grab line at cursor */
            isearch_yank_line(is);
        } else {
            /* isearch-yank-kill: grap from kill buffer */
            isearch_yank_kill(is);
        }
        break;
    case KEY_META(KEY_CTRL('b')):
        /* isearch-toggle-hex: cycle hex, unihex, normal search */
        isearch_toggle_hex(is);
        break;
    case KEY_META('c'):
    case KEY_CTRL('c'):
        /* isearch-toggle-case-fold: toggle case sensitivity */
        isearch_toggle_case_fold(is);
        break;
    case KEY_META('r'):
    case KEY_CTRL('t'):
        /* isearch-toggle-regexp */
        isearch_toggle_regexp(is);
        break;
    case KEY_CTRL('l'):
        do_center_cursor(is->s, 1);
        break;
    case KEY_TAB:
    case KEY_CTRL('j'):  // XXX: pb with KEY_RET under lldb
        /* make it easy to search for TABs and newlines */
        isearch_printing_char(is, key);
        break;
    case KEY_RET:   /* isearch-exit */
        isearch_exit(is);
        break;
    default:
        if (KEY_IS_SPECIAL(key) || KEY_IS_CONTROL(key)) {
            /* isearch-cancel */
            isearch_cancel(is, key);
        } else {
            isearch_printing_char(is, key);
        }
        break;
    }
    isearch_run(is);
#endif
}

/* XXX: handle busy */
void do_isearch(EditState *s, int argval, int dir)
{
    ISearchState *is = &global_isearch_state;
    EditState *e;
    int flags = SEARCH_FLAG_SMARTCASE | SEARCH_FLAG_ACTIVE;

    /* prevent search from minibuffer */
    if (s->flags & WF_MINIBUF)
        return;

    /* stop displaying search matches on last window */
    e = check_window(&is->s);
    if (e) {
        e->isearch_state = NULL;
    }

    memset(is, 0, sizeof(*is));
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
    if (argval != 1)
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
            h = qe_digit_value(c);
            if (h < 16) {
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
    int search_flags;
    int start_offset;
    int found_offset, found_end;
    int search_u32_len;
    /* query-replace */
    int replace_all;
    int nb_reps;
    int replace_u32_len;
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

static void query_replace_key(void *opaque, int key)
{
    QueryReplaceState *is = opaque;
    EditState *s = is->s;
    QEmacsState *qs = &qe_state;

    switch (key) {
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
        //isearch_toggle_word_match(is);
        is->search_flags ^= SEARCH_FLAG_WORD;
        is->found_offset = is->last_offset;
        break;
    case KEY_META('b'):
    case KEY_CTRL('b'):
        /* cycle unihex, hex, normal search */
        //isearch_toggle_hex(is);
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
        //isearch_toggle_case_fold(is);
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
    if (argval != 1)
        flags |= SEARCH_FLAG_WORD;
    query_replace(s, search_str, replace_str, 1, flags);
}

/* dir = 0, -1, 1, 2, 3 -> count-matches, reverse, forward,
   delete-matching-lines, filter-matching-lines */
void do_search_string(EditState *s, const char *search_str, int dir)
{
    unsigned int search_u32[SEARCH_LENGTH];
    int search_u32_len;
    int found_offset, found_end;
    int flags = SEARCH_FLAG_SMARTCASE;
    int offset, offset1, count = 0;

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

    offset = s->offset;
    if (dir == 2 || dir == 3)
        offset = eb_goto_bol(s->b, offset);

    while (eb_search(s->b, dir, flags,
                     offset, s->b->total_size,
                     search_u32, search_u32_len,
                     NULL, NULL, &found_offset, &found_end) > 0)
    {
        count++;
        switch (dir) {
        case 0:
            offset = found_end;
            continue;
        case -1:
            s->offset = found_offset;
            do_center_cursor(s, 0);
            return;
        case 1:
            s->offset = found_end;
            do_center_cursor(s, 0);
            return;
        case 2:
            offset = eb_goto_bol(s->b, found_offset);
            eb_delete_range(s->b, offset, eb_next_line(s->b, found_offset));
            continue;
        case 3:
            offset1 = eb_goto_bol(s->b, found_offset);
            eb_delete_range(s->b, offset, offset1);
            offset = eb_next_line(s->b, offset);
            continue;
        }
    }
    switch (dir) {
    case 0:
        put_status(s, "%d matches", count);
        break;
    case 2:
        put_status(s, "deleted %d lines", count);
        break;
    case 3:
        eb_delete_range(s->b, offset, s->b->total_size);
        put_status(s, "filtered %d lines", count);
        break;
    case -1:
    case 1:
        put_status(s, "Search failed: \"%s\"", search_str);
        break;
    }
}

static const CmdDef isearch_commands[] = {
    CMD2( "isearch-abort", "C-g",
          "abort isearch and move point to starting point",
           isearch_abort, ISS, "")
    CMD2( "isearch-cancel", "",
          "Exit isearch and run regular command",
           isearch_cancel, ISSi, "k")
    CMDx( "isearch-complete", "M-TAB",
          "complete the search string from the history buffer",
           isearch_edit_string, ISS, "")
    CMD2( "isearch-center", "C-l",
          "center the window around point",
           isearch_center, ISS, "")
    CMDx( "isearch-del-char", "C-M-w",
          "Delete character from end of search string",
           isearch_del_char, ISS, "")
    CMD2( "isearch-delete-char", "DEL",
          "Cancel last input item from end of search string",
           isearch_delete_char, ISS, "")
    CMD2( "isearch-exit", "RET",
          "Exit isearch, leave point at location found",
           isearch_exit, ISS, "")
    CMDx( "isearch-mode-help", "f1, C-h",
          "show the help page for isearch",
           isearch_mode_help, ISS, "")
    CMD3( "isearch-next-match", "C-s",
          "Search again forward",
           isearch_addpos, ISSi, "v", 1)
    CMDx( "isearch-next-string", "M-n",
          "get the next item from history",
           isearch_next_string, ISS, "")
    CMD3( "isearch-previous-match", "C-r",
          "Search again backward",
           isearch_addpos, ISSi, "v", -1)
    CMDx( "isearch-previous-string", "M-p",
          "get the previous item from history.",
           isearch_previous_string, ISS, "")
    CMD2( "isearch-printing-char", "TAB, C-j",
          "append the character to the search string",
           isearch_printing_char, ISSi, "k")
    CMDx( "isearch-query-replace", "M-%",
          "start 'query-replace' with current string to replace",
          isearch_query_replace, ISS, "")
    CMDx( "isearch-query-replace-regexp", "",  // C-M-% invalid tty binding?
          "start 'query-replace-regexp' with current string to replace"
           isearch_query_replace, ISS, "")
    CMD2( "isearch-quote-char", "C-q",
          "quote a control character and search for it",
           isearch_quote_char, ISS, "")
    CMD2( "isearch-toggle-case-fold", "M-c, C-c",
          "toggle search case-sensitivity",
           isearch_toggle_case_fold, ISS, "")
    CMD2( "isearch-toggle-hex", "M-C-b",
          "toggle normal/hex/unihex searching",
           isearch_toggle_hex, ISS, "")
    CMD2( "isearch-toggle-regexp", "M-r, C-t",
          "toggle regular-expression mode",
           isearch_toggle_regexp, ISS, "")
    CMD2( "isearch-toggle-word-match", "M-w",
          "toggle word match",
           isearch_toggle_word_match, ISS, "")
    CMDx( "isearch-yank-char", "C-M-y",
          "Yank char from buffer onto end of search string",
           isearch_yank_char, ISS, "")
    CMD2( "isearch-yank-kill", "M-y",
          "yank the last string of killed text",
           isearch_yank_kill, ISS, "")
    CMD2( "isearch-yank-line", "C-y",
          "yank rest of line onto end of search string",
           isearch_yank_line, ISS, "")
    CMD2( "isearch-yank-word", "C-w",
          "yank next word or character in buffer",
           isearch_yank_word, ISS, "")
    CMDx( "isearch-edit-string", "M-e",
          "edit the search string in the minibuffer",
           isearch_edit_string, ISS, "")
};

static const CmdDef search_commands[] = {

    /* M-C-s should be bound to isearch-forward-regex */
    /* mg binds search-forward to M-s */
    CMD3( "search-forward", "M-S",
          "Search for a string in the current buffer",
          do_search_string, ESsi,
          "s{Search forward: }|search|"
          "v", 1)
    /* M-C-r should be bound to isearch-backward-regex */
    /* mg binds search-forward to M-r */
    CMD3( "search-backward", "M-R",
          "Search backwards for a string in the current buffer",
          do_search_string, ESsi,
          "s{Search backward: }|search|"
          "v", -1)
    CMD3( "count-matches", "M-C",
          "Count string matches from point to the end of the current buffer",
          do_search_string, ESsi,
          "s{Count Matches: }|search|"
          "v", 0)
    CMD3( "delete-matching-lines", "",
          "Delete lines containing a string from point to the end of the current buffer",
          do_search_string, ESsi, "*"
          "s{Delete lines containing: }|search|"
          "v", 2)
    CMD3( "filter-matching-lines", "",
          "Delete lines NOT containing a string from point to the end of the current buffer",
          do_search_string, ESsi, "*"
          "s{Filter lines containing: }|search|"
          "v", 3)
    /* passing argument should switch to regex incremental search */
    CMD3( "isearch-backward", "C-r",
          "Search backward incrementally",
          do_isearch, ESii, "p" "v", -1)
    CMD3( "isearch-forward", "C-s",
          "Search forward incrementally",
          do_isearch, ESii, "p" "v", 1)
    CMD2( "query-replace", "M-%",
          "Replace a string with another interactively",
          do_query_replace, ESss, "*"
          "s{Query replace: }|search|"
          "s{With: }|replace|")
    /* passing argument restricts replace to word matches */
    /* XXX: non standard binding */
    CMD2( "replace-string", "M-r",
          "Replace a string with another till the end of the buffer",
          do_replace_string, ESssi, "*"
          "s{Replace String: }|search|"
          "s{With: }|replace|"
          "p")
};

static ModeDef isearch_mode = {
    .name = "Isearch",
    //.desc = "";
};

static int search_init(void) {
    qe_register_mode(&isearch_mode, MODEF_NOCMD);
    qe_register_commands(&isearch_mode, isearch_commands, countof(isearch_commands));
    qe_register_commands(NULL, search_commands, countof(search_commands));
    return 0;
}

qe_module_init(search_init);
