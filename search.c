/*
 * Incremental search and replace for QEmacs.
 *
 * Copyright (c) 2000-2002 Fabrice Bellard.
 * Copyright (c) 2000-2024 Charlie Gordon.
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
#include "variables.h"

#ifdef CONFIG_REGEX
#include "libregexp.h"

BOOL lre_check_stack_overflow(void *opaque, size_t alloca_size) {
    return FALSE;
}

void *lre_realloc(void *opaque, void *ptr, size_t size) {
    return qe_realloc(&ptr, size);
}
#endif

/* Search stuff */

#define SEARCH_FLAG_DEFAULT    SEARCH_FLAG_SMARTCASE
#define SEARCH_FLAG_EXACTCASE  0x0000
#define SEARCH_FLAG_IGNORECASE 0x0001
#define SEARCH_FLAG_SMARTCASE  0x0002  /* case fold unless upper case present */
#define SEARCH_FLAG_CASE_MASK  0x0003
#define SEARCH_FLAG_WORD       0x0004
#define SEARCH_FLAG_REGEX      0x0008
#define SEARCH_FLAG_HEX        0x0010
#define SEARCH_FLAG_UNIHEX     0x0020
#define SEARCH_FLAG_HEX_MASK   0x0030
#define SEARCH_FLAG_MASK       0x003f
#define SEARCH_FLAG_WRAPPED    0x0040
#define SEARCH_FLAG_ACTIVE     0x1000

/* should separate search string length and number of match positions */
#define SEARCH_LENGTH  2048
#define SEARCH_STEPS   512
#define FOUND_TAG      0x80000000
#define FOUND_REV      0x40000000

struct ISearchState {
    EditState *s;
    int search_flags;
    int start_offset;
    int found_offset, found_end;
    int search_u32_len;
    /* isearch */
    EditState *minibuffer;     /* set if delegated from minibuffer */
    int saved_mark;
    int start_dir;
    int quoting;
    int dir;
    int pos;  /* position in search_u32_steps */
    unsigned int search_u32_steps[SEARCH_STEPS];
    /* common */
    char search_str[SEARCH_LENGTH * 3];     /* may be in hex */
    char32_t search_u32[SEARCH_LENGTH];
};

static ModeDef isearch_mode;

/* XXX: should store to screen */
static ISearchState global_isearch_state;

static int eb_search(EditBuffer *b, int dir, int flags,
                     int start_offset, int end_offset,
                     const char32_t *buf, int len,
                     CSSAbortFunc *abort_func, void *abort_opaque,
                     int *found_offset, int *found_end)
{
    /*@API search
       Search a buffer for contents. Return true if contents was found.
       @argument `b` a valid EditBuffer pointer
       @argument `dir` search direction: -1 for backward, 1 for forward
       @argument `flags` a combination of SEARCH_FLAG_xxx values
       @argument `start_offset` the starting offset in buffer
       @argument `end_offset` the maximum offset in buffer
       @argument `buf` a valid pointer to an array of `char32_t`
       @argument `len` the length of the array `buf`
       @argument `abort_func` a function pointer to test for abort request
       @argument `abort_opaque` an opaque argument for `abort_func`
       @argument `found_offset` a valid pointer to store the match
         starting offset
       @argument `found_end` a valid pointer to store the match
         ending offset
       @return non zero if the search was successful. Match starting and
       ending offsets are stored to `start_offset` and `end_offset`.
       Return `0` if search failed or `len` is zero.
       Return `-1` if search was aborted.
     */
    int total_size = b->total_size;
    int offset = start_offset, offset1, offset2, offset3, pos;
    char32_t c, c2;

    if (len == 0)
        return 0;

    if (dir < 0)
        end_offset = offset;
    if (end_offset > total_size)
        end_offset = total_size;

    *found_offset = -1;
    *found_end = -1;

    /* analyze buffer if smart case */
    if ((flags & SEARCH_FLAG_CASE_MASK) == SEARCH_FLAG_SMARTCASE) {
        int upper_count = 0;
        int lower_count = 0;
        for (pos = 0; pos < len; pos++) {
            lower_count += qe_iswlower(buf[pos]);
            upper_count += qe_iswupper(buf[pos]);
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

#ifdef CONFIG_REGEX
    if (flags & SEARCH_FLAG_REGEX) {
        char error_message[100];
        char source[SEARCH_LENGTH];
        int source_len;
        uint8_t *regexp_bytes = NULL;
        int regexp_len;
        uint8_t **capture = NULL;
        int capture_num;
        int res = 0;
        int re_flags = 0;
        int found;

        re_flags |= LRE_FLAG_MULTILINE;
        if (flags & SEARCH_FLAG_IGNORECASE)
            re_flags |= LRE_FLAG_IGNORECASE;
        if (dir < 0)
            re_flags |= LRE_FLAG_STICKY;

        source_len = char32_to_utf8(source, countof(source), buf, len);
        if (source_len >= countof(source))
            return -1;
        regexp_bytes = lre_compile(&regexp_len, error_message, sizeof(error_message),
                                   source, source_len, re_flags, NULL);
        if (regexp_bytes == NULL) {
            //put_status(NULL, "regexp compile error: %s", error_message);
            return -1;
        }
        capture_num = lre_get_capture_count(regexp_bytes);
        capture = qe_malloc_array(uint8_t *, 2 * capture_num);
        if (capture_num == 0 || capture == NULL) {
            qe_free(&regexp_bytes);
            qe_free(&capture);
            //put_status(NULL, "cannot allocate capture array for %d entries", capture_num);
            return -1;
        }
        for (offset1 = offset;;) {
            if (dir < 0) {
                if (offset == 0)
                    break;
                offset = eb_prev(b, offset);
            } else {
                offset = offset1;
                if (offset >= end_offset)
                    break;
                offset1 = eb_next(b, offset);
            }
            if ((offset & 0xffff) == 0) {
                /* check for search abort every 64K */
                if (abort_func && abort_func(abort_opaque)) {
                    res = -1;
                    break;
                }
            }
            /* Pass boundary characters to match $ and \b or \B */
            found = lre_exec(capture, regexp_bytes,
                             (const uint8_t *)b, offset, end_offset, 0, NULL,
                             eb_prevc(b, offset, &offset3), eb_nextc(b, end_offset, &offset3),
                             (unsigned int (*)(const uint8_t *bc_buf, int offset, int *offsetp))eb_nextc,
                             (unsigned int (*)(const uint8_t *bc_buf, int offset, int *offsetp))eb_prevc);
            if (found < 0) {
                res = -1;
                break;
            }
            if (found > 0) {
                int start = capture[0] - (uint8_t *)(void *)b;
                int end = capture[1] - (uint8_t *)(void *)b;
                if ((dir >= 0 || end <= end_offset)
                &&  (!(flags & SEARCH_FLAG_WORD) ||
                     (qe_isword(eb_prevc(b, start, &offset3)) &&
                      qe_isword(eb_nextc(b, end, &offset3)))))
                {
                    *found_offset = start;
                    *found_end = end;
                    res = 1;
                    break;
                }
            }
            if (dir >= 0)
                break;
        }
        qe_free(&regexp_bytes);
        qe_free(&capture);
        return res;
    }
#endif

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
            if (c != c2) {
                if (!(flags & SEARCH_FLAG_IGNORECASE)
                ||  qe_wtoupper(c) != qe_wtoupper(c2))
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

#if 0
static void buf_encode_search_u32(buf_t *out, const char32_t *str, int len)
{
    int i;

    for (i = 0; i < len; i++) {
        char32_t v = str[i];
        if (v < 32 || v == 127) {
            buf_printf(out, "^%c", (v + '@') & 127);
        } else {
            buf_putc_utf8(out, v);
        }
        if (buf_avail(out) <= 0)
            break;
    }
}
#endif

static void buf_encode_search_str(buf_t *out, const char *str)
{
    /* Encode the search string for display: control characters are
       converted to ^X format.
     */
    while (*str) {
        char32_t c = utf8_decode(&str);
        if (c < 32 || c == 127) {
            buf_printf(out, "^%c", (c + '@') & 127);
        } else {
            buf_putc_utf8(out, c);
        }
        if (buf_avail(out) <= 0)
            break;
    }
}

static struct search_tags {
    /* List of tags for search options */
    char tag[14];
    unsigned char bits, mask;
} const search_tags[] = {
    { "[Unihex] ",  SEARCH_FLAG_UNIHEX,     SEARCH_FLAG_HEX_MASK  },
    { "[Hex] ",     SEARCH_FLAG_HEX,        SEARCH_FLAG_HEX_MASK  },
    { "[Folding] ", SEARCH_FLAG_IGNORECASE, SEARCH_FLAG_CASE_MASK },
    { "[Exact] ",   SEARCH_FLAG_EXACTCASE,  SEARCH_FLAG_CASE_MASK },
    { "[Regex] ",   SEARCH_FLAG_REGEX,      SEARCH_FLAG_REGEX     },
    { "[Word] ",    SEARCH_FLAG_WORD,       SEARCH_FLAG_WORD      },
};

static int search_string_get_flags(const char *str, int flags, const char **endp) {
    /* Parse search tags at the start of a search string */
    int i;
    for (i = 0; i < countof(search_tags); i++) {
        if (strstart(str, search_tags[i].tag, &str)) {
            flags &= ~search_tags[i].mask;
            flags |= search_tags[i].bits;
        }
    }
    if (endp)
        *endp = str;
    return flags;
}

static void buf_disp_search_flags(buf_t *out, int search_flags) {
    /* Encode search flags as a sequence of search tags */
    int i;
    for (i = 0; i < countof(search_tags); i++) {
        if ((search_flags & search_tags[i].mask) == search_tags[i].bits)
            buf_puts(out, search_tags[i].tag);
    }
}

static void isearch_run(ISearchState *is) {
    /* Incremental search engine: this function is run after all
       incremental search commands. It updates the search flags
       and search string and searches for the next match.
       Fields updated:
       - is->search_flags: the current search mode and matching options
       - is->search_str: the string representation of the search string
         and options.
       - is->search_u32: the search string as an array of code points
       - is->search_u32_len: the length of the is->search_u32 array
       - is->dir: the search direction (>0 forward, <0 backward)
       - is->found_offset / is->found_end: the new match found
       - s->b->mark / s->offset: the new match found in the search direction
     */
    char ubuf[SEARCH_LENGTH * 3];
    buf_t outbuf, *out;
    int i, len, hex_nibble, max_nibble, h;
    char32_t c, hc;
    unsigned int v;
    int search_offset, flags, dir;
    int start_time, elapsed_time;
    EditState *s = is->s;

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

    out = buf_init(&outbuf, ubuf, countof(ubuf));
    // XXX: should have steps to update flags, and retrieve history strings too
    for (i = 0; i < is->pos; i++) {
        v = is->search_u32_steps[i];
        if (v & FOUND_TAG) {
            dir = (v & FOUND_REV) ? -1 : 1;
            search_offset = v & ~(FOUND_TAG | FOUND_REV);
            continue;
        }
        c = v;
        if (c == 0) {
            /* special case: use redundant utf8x encoding */
            buf_write(out, "\xC0\x80", 2);
        } else {
            buf_putc_utf8(out, c);
        }
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

    /* construct search_str with search_flags prefix */
    out = buf_init(&outbuf, is->search_str, countof(is->search_str));
    buf_disp_search_flags(out, is->search_flags);
    buf_puts(out, ubuf);

    // XXX: should test for SEARCH_FLAG_SUSPENDED

    if (len == 0) {
        s->b->mark = is->saved_mark;
        s->offset = is->start_offset;
        s->region_style = 0;
        is->found_offset = -1;
    } else {
        if (search_offset == is->found_offset
        &&  search_offset == is->found_end) {
            /* zero width match: skip one character */
            if (is->dir > 0)
                search_offset = eb_next(s->b, search_offset);
        }
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
    buf_puts(out, "I-search");
    if (is->dir < 0)
        buf_puts(out, " backward");
    buf_puts(out, ": ");
    /* display control characters as ^c */
    buf_encode_search_str(out, is->search_str);
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

static int isearch_grab(ISearchState *is, EditBuffer *b, int from, int to) {
    /* Retrieve search bytes from the buffer contents */
    // XXX: should special case hex search modes
    int offset, last = is->pos;
    if (b) {
        if (to < 0 || to > b->total_size)
            to = b->total_size;
        for (offset = from; is->pos < countof(is->search_u32_steps) && offset < to;) {
            char32_t c = eb_nextc(b, offset, &offset);
            is->search_u32_steps[is->pos++] = c;
        }
    }
    return is->pos - last;
}

static void isearch_yank_word_or_char(EditState *s) {
    /*@CMD isearch-yank-word-or-char
       ### `isearch-yank-word-or-char()`
       Extract the current character or word from the buffer and append it
       to the search string.
     */
    // XXX: does not work for hex search modes
    ISearchState *is = s->isearch_state;
    if (is) {
        int offset0, offset1;
        offset0 = s->offset;
        do_word_left_right(s, 1);
        offset1 = s->offset;
        s->offset = offset0;
        isearch_grab(is, s->b, offset0, offset1);
    }
}

static void isearch_yank_char(EditState *s) {
    /*@CMD isearch-yank-char
       ### `isearch-yank-char()`
       Extract the current character from the buffer and append it
       to the search string.
     */
    // XXX: does not work for hex search modes
    ISearchState *is = s->isearch_state;
    if (is) {
        int offset0 = s->offset;
        int offset1 = eb_next(s->b, offset0);
        isearch_grab(is, s->b, offset0, offset1);
    }
}

static void isearch_yank_line(EditState *s) {
    /*@CMD isearch-yank-line
       ### `isearch-yank-line()`
       Extract the current line from the buffer and append it to the
       search string.
     */
    // XXX: does not work for hex search modes
    ISearchState *is = s->isearch_state;
    if (is) {
        int offset0, offset1;
        offset0 = s->offset;
        if (eb_nextc(s->b, offset0, &offset1) == '\n')
            offset0 = offset1;
        do_eol(s);
        offset1 = s->offset;
        s->offset = offset0;
        isearch_grab(is, s->b, offset0, offset1);
    }
}

static void isearch_yank_kill(EditState *s) {
    /*@CMD isearch-yank-kill
       ### `isearch-yank-kill()`
       Append the contents of the last kill to the search string.
     */
    // XXX: does not work for hex search modes
    ISearchState *is = s->isearch_state;
    if (is) {
        QEmacsState *qs = is->s->qe_state;
        isearch_grab(is, qs->yank_buffers[qs->yank_current], 0, -1);
    }
}

static void isearch_addpos(EditState *s, int dir) {
    /*@CMD isearch-repeat-forward
       ### `isearch-repeat-forward()`
       Search for the next match forward.
       Retrieve the last search string if search string is empty.
     */
    /*@CMD isearch-repeat-backward
       ### `isearch-repeat-backward()`
       Search for the next match backward.
       Retrieve the last search string if search string is empty.
     */
    int curdir;
    ISearchState *is = s->isearch_state;
    if (!is)
        return;

    curdir = is->dir;
    is->dir = dir;
    if (is->search_u32_len == 0 && is->dir == curdir) {
        /* retrieve last search string from search history list */
        StringArray *hist = qe_get_history("search");
        if (hist && hist->nb_items) {
            const char *str = hist->items[hist->nb_items - 1]->str;
            if (str) {
                is->search_flags = SEARCH_FLAG_ACTIVE |
                    search_string_get_flags(str, SEARCH_FLAG_DEFAULT, &str);
                while (*str && is->pos < countof(is->search_u32_steps)) {
                    char32_t c = utf8_decode(&str);
                    is->search_u32_steps[is->pos++] = c;
                }
            }
        }
    } else
    if (is->pos < countof(is->search_u32_steps)) {
        /* add the match position, if any */
        unsigned long v = (is->dir >= 0) ? FOUND_TAG : FOUND_TAG | FOUND_REV;
        if (is->found_offset < 0 && is->search_u32_len > 0) {
            is->search_flags |= SEARCH_FLAG_WRAPPED;
            if (is->dir < 0)
                v |= is->s->b->total_size;
        } else {
            v |= is->s->offset;
        }
        is->search_u32_steps[is->pos++] = v;
    }
}

static void isearch_printing_char(EditState *s, int key) {
    ISearchState *is = s->isearch_state;
    if (is) {
        if (is->pos < countof(is->search_u32_steps))
            is->search_u32_steps[is->pos++] = key;
    }
}

static void isearch_quote_char(EditState *s) {
    ISearchState *is = s->isearch_state;
    if (is) {
        is->quoting = 1;
    }
}

static void isearch_delete_char(EditState *s) {
    ISearchState *is = s->isearch_state;
    if (is) {
        if (is->pos > 0)
            is->pos--;
    }
}

static int search_flags_cycle(int flags, int f1) {
    /* split the bits */
    int f2 = f1 & (f1 - 1);
    f1 &= ~f2;
    /* cycle search flags through 2 or 3 possibilities */
    if (flags & f1) {
        flags &= ~f1;
        flags |= f2;
    } else
    if (flags & f2) {
        flags &= ~f2;
    } else {
        flags |= f1;
    }
    return flags;
}

static void isearch_cycle_flags(EditState *s, int f1) {
    ISearchState *is = s->isearch_state;

    if (s->flags & WF_MINIBUF) {
        if (s->prompt && s->target_window)
            is = s->target_window->isearch_state;
    }
    if (is) {
        is->search_flags = search_flags_cycle(is->search_flags, f1);

        if (s->flags & WF_MINIBUF) {
            /* update minibuffer prompt from flags */
            char contents[1024];
            char ubuf[80];
            buf_t outbuf, *out;
            const char *str;

            eb_get_contents(s->b, contents, sizeof(contents), 1);
            /* strip current prefixes */
            search_string_get_flags(contents, SEARCH_FLAG_DEFAULT, &str);
            out = buf_init(&outbuf, ubuf, sizeof(ubuf));
            buf_disp_search_flags(out, is->search_flags);
            eb_delete_range(s->b, 0, s->b->total_size);
            eb_puts(s->b, ubuf);
            eb_puts(s->b, str);
            s->offset = s->b->total_size;
            do_refresh(s);
        }
    }
}

void isearch_toggle_case_fold(EditState *s) {
    isearch_cycle_flags(s, SEARCH_FLAG_CASE_MASK);
}

void isearch_toggle_hex(EditState *s) {
    isearch_cycle_flags(s, SEARCH_FLAG_HEX_MASK);
}

#ifdef CONFIG_REGEX
void isearch_toggle_regexp(EditState *s) {
    isearch_cycle_flags(s, SEARCH_FLAG_REGEX);
}
#endif

void isearch_toggle_word_match(EditState *s) {
    isearch_cycle_flags(s, SEARCH_FLAG_WORD);
}

static void isearch_end(ISearchState *is) {
    /* save current search string to the search history buffer */
    StringArray *hist = qe_get_history("search");
    if (*is->search_str && hist) {
        remove_string(hist, is->search_str);
        add_string(hist, is->search_str, 0);
    }
    is->search_flags &= ~SEARCH_FLAG_ACTIVE;
    edit_display(is->s->qe_state);
    dpy_flush(is->s->screen);
}

static void isearch_cancel(EditState *s) {
    ISearchState *is = s->isearch_state;
    if (is) {
        s->b->mark = is->saved_mark;
        s->offset = is->start_offset;
        s->region_style = 0;
        s->isearch_state = NULL;
        isearch_end(is);
    }
}

static void isearch_abort(EditState *s) {
    /* XXX: when search has failed should cancel input back to what has been
     * found successfully.
     * when search is successful aborts and moves point to starting point.
     * and signal quit (to abort macros?)
     */
    put_status(s, "Quit");
    isearch_cancel(s);
}

static void isearch_exit(EditState *s, int key) {
    ISearchState *is = s->isearch_state;
    if (is) {
        /* exit search mode */
        s->b->mark = min_offset(is->start_offset, s->b->total_size);
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
}

static void isearch_key(void *opaque, int key) {
    ISearchState *is = opaque;
    unsigned int keys[1] = { key };

    if (is->quoting) {
        is->quoting = 0;
        if (!KEY_IS_SPECIAL(key)) {
            isearch_printing_char(is->s, key);
        }
    } else {
        KeyDef *kd = qe_find_binding(keys, 1, isearch_mode.first_key, 1);
        if (kd) {
            exec_command(is->s, kd->cmd, NO_ARG, key);
        } else {
            if (KEY_IS_SPECIAL(key) || KEY_IS_CONTROL(key)) {
                isearch_exit(is->s, key);
            } else {
                isearch_printing_char(is->s, key);
            }
        }
    }
    if (is->search_flags & SEARCH_FLAG_ACTIVE) {
        isearch_run(is);
    } else {
        /* This should free the ISearchState grab data if allocated */
        qe_ungrab_keys();
    }
}

static ISearchState *set_search_state(EditState *s, int argval, int dir) {
    ISearchState *is = &global_isearch_state;
    EditState *e;
    int flags = SEARCH_FLAG_DEFAULT | SEARCH_FLAG_ACTIVE;

    /* stop displaying search matches on last window */
    e = check_window(&is->s);
    if (e) {
        e->isearch_state = NULL;
    }

    if (s == NULL)
        return NULL;

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
#ifdef CONFIG_REGEX
    if (argval != 1)
        flags |= SEARCH_FLAG_REGEX;
#endif
    is->search_flags = flags;
    return is;
}

/* XXX: handle busy */
void do_isearch(EditState *s, int argval, int dir) {
    ISearchState *is;

    /* prevent search from minibuffer */
    if (s->flags & WF_MINIBUF)
        return;

    is = set_search_state(s, argval, dir);
    if (is != NULL) {
        qe_grab_keys(isearch_key, is);
        isearch_run(is);
    }
}

static int search_to_u32(char32_t *buf, int size,
                         const char *str, int flags)
{
    if (flags & (SEARCH_FLAG_HEX | SEARCH_FLAG_UNIHEX)) {
        /* CG: XXX: Should mix utf8 and hex syntax in hex modes */
        const u8 *s;
        char32_t c;
        int hex_nibble, max_nibble, h, hc, len;

        max_nibble = (flags & SEARCH_FLAG_UNIHEX) ? 6 : 2;
        s = (const u8 *)str;
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
        return utf8_to_char32(buf, size, str);
    }
}

void isearch_colorize_matches(EditState *s, char32_t *buf, int len,
                              QETermStyle *sbuf, int offset_start)
{
    ISearchState *is = s->isearch_state;
    EditBuffer *b = s->b;
    int offset, char_offset, found_offset, found_end, offset_end;
    int search_flags;

    if (!is)
        return;

    search_flags = is->search_flags;
    if (check_window(&is->minibuffer)) {
        /* refresh target window for search matches */
        // XXX: should perform this once per window with a NULL buf
        char contents[1024];
        const char *str;

        eb_get_contents(is->minibuffer->b, contents, sizeof(contents), 1);
        search_flags = search_string_get_flags(contents, SEARCH_FLAG_DEFAULT, &str);
        is->search_u32_len = search_to_u32(is->search_u32, countof(is->search_u32),
                                           str, search_flags);
    }

    if (is->search_u32_len <= 0)
        return;

    char_offset = eb_get_char_offset(b, offset_start);
    offset_end = eb_goto_char(b, char_offset + len);
    offset = 0;
    if (char_offset > is->search_u32_len + 1)
        offset = eb_goto_char(b, char_offset - is->search_u32_len - 1);

    while (eb_search(b, 1, search_flags, offset, offset_end,
                     is->search_u32, is->search_u32_len, NULL, NULL,
                     &found_offset, &found_end) > 0) {
        int line, start, stop, i;

        if (found_offset >= offset_end)
            break;
        if (found_end <= found_offset) {
            /* zero width match: skip one character.
             * for example `a*` finds matches everywhere but should highlight
             * all sequences of letters a
             */
            offset = eb_next(b, found_end);
            continue;
        }
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

typedef struct QueryReplaceState {
    EditState *s;
    int search_flags;
    int start_offset;
    int found_offset, found_end;
    int search_u32_len;
    /* query-replace */
    EditState *help_window;
    int replace_all;
    int nb_reps;
    int replace_u32_len;
    int last_offset;
    /* common */
    char search_str[SEARCH_LENGTH * 3];   /* may be in hex */
    char32_t search_u32[SEARCH_LENGTH];   /* code points */
    /* query-replace */
    char replace_str[SEARCH_LENGTH * 3];  /* may be in hex */
    char32_t replace_u32[SEARCH_LENGTH];  /* code points */
} QueryReplaceState;

static void query_replace_help(QueryReplaceState *is) {
    EditState *s = is->s;
    EditBuffer *b;

    if (is->help_window)
        return;

    b = new_help_buffer();
    if (!b)
        return;

    // XXX: encode strings?
    eb_printf(b, "Query replacing %s with %s\n",
              is->search_str, is->replace_str);

    eb_printf(b, "Type Space or `y' to replace one match, Delete or `n' to skip to next,\n"
              "RET or `q' to exit, Period to replace one match and exit,\n"
              //"Comma to replace but not move point immediately,\n"
              //"C-r to enter recursive edit (C-M-c to get out again),\n"
              //"C-w to delete match and recursive edit,\n"
              "C-w to toggle word match,\n"
              "C-b to cycle hex and unihex searching,\n"
              "C-c to cycle case sensitivity (ignore, smart, exact),\n"
              "C-g to stop replacing and move point back to where search started,\n"
              "C-l to clear the screen, redisplay, and center the screen,\n"
              "! to replace all remaining matches with no more questions,\n"
              //"^ to move point back to previous match,\n"
              //"E to edit the replacement string\n"
              );

    // XXX: should look up markdown documentation
    is->help_window = show_popup(s, b, "Query Replace Help");
}

static void query_replace_abort(QueryReplaceState *is)
{
    EditState *s = is->s;

    s->b->mark = is->start_offset;
    s->region_style = 0;
    put_status(NULL, "Replaced %d occurrences", is->nb_reps);
    /* Achtung: should free the grab data */
    qe_ungrab_keys();
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
    is->found_offset += eb_insert_char32_buf(s->b, is->found_offset,
                                             is->replace_u32, is->replace_u32_len);
}

static void query_replace_run(QueryReplaceState *is)
{
    EditState *s = is->s;
    char ubuf[4096];
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
    buf_puts(out, "Query replace ");
    buf_disp_search_flags(out, is->search_flags);
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
    QEmacsState *qs = s->qe_state;

    if (check_window(&is->help_window)) {
        do_delete_window(is->help_window, 0);
        is->help_window = NULL;
        qs->active_window = s;
        edit_display(is->s->qe_state);
        dpy_flush(is->s->screen);
        return;
    }

    switch (key) {
    case '?':
    case KEY_CTRL('h'):
    case KEY_F1:
        query_replace_help(is);
        break;
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
        is->search_flags = search_flags_cycle(is->search_flags, SEARCH_FLAG_HEX_MASK);
        is->found_offset = is->last_offset;
        break;
    case KEY_META('c'):
    case KEY_CTRL('c'):
        /* toggle case sensitivity */
        //isearch_toggle_case_fold(is);
        is->search_flags = search_flags_cycle(is->search_flags, SEARCH_FLAG_CASE_MASK);
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
        fallthrough;
    default:
        query_replace_abort(is);
        return;
    }
    query_replace_run(is);
}

static void query_replace(EditState *s, const char *search_str,
                          const char *replace_str, int all, int flags)
{
    /* search_str starts with encoded search flags */
    // TODO: merge QueryReplaceState and ISearchState
    // TODO: use pseudo mode bindings like isearch
    // XXX: should restrict the search to the current region if highlighted
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
    flags = search_string_get_flags(search_str, flags, &search_str);
    // XXX: allocate strings?
    pstrcpy(is->search_str, sizeof(is->search_str), search_str);
    pstrcpy(is->replace_str, sizeof(is->replace_str), replace_str);

    is->search_flags = flags;
    is->replace_all = all;
    is->start_offset = is->last_offset = s->offset;
    is->found_offset = is->found_end = s->offset;

    qe_grab_keys(query_replace_key, is);
    query_replace_run(is);
}

void do_query_replace(EditState *s, const char *search_str,
                      const char *replace_str, int argval)
{
    /*@CMD query-replace
       ### `query-replace(string FROM-STRING, string TO-STRING,
                      int DELIMITED=argval, int START=point, int END=end)`

       Replace some occurrences of FROM-STRING with TO-STRING.
       As each match is found, the user must type a character saying
       what to do with it.  For directions, type '?' at that time.

       FROM-STRING is analyzed for search flag names with determine how
       matches are found.  Supported flags are [UniHex], [Hex], [Folding],
       [Exact], [Regex] and [Word].

       If case matching is either Folding or Smart, replacement transfers
       the case pattern of the old text to the new text.  For example
       if the old text matched is all caps, or capitalized, then its
       replacement is upcased or capitalized.

       Third arg DELIMITED (prefix arg if interactive), if non-zero, means
       replace only matches surrounded by word boundaries.

       Fourth and fifth arg START and END specify the region to operate on:
       if these arguments are not provided, if the current region is
       highlighted, operate on the contents of the region, otherwise,
       operate from point to the end of the buffer.

       To customize possible responses, change the "bindings" in
       `query-replace-mode`.
     */
    // TODO: region restriction
    int flags = SEARCH_FLAG_SMARTCASE;
    if (argval != 1)
        flags |= SEARCH_FLAG_WORD;
    query_replace(s, search_str, replace_str, 0, flags);
}

void do_replace_string(EditState *s, const char *search_str,
                       const char *replace_str, int argval)
{
    /*@CMD replace-string
       ### `replace-string(string FROM-STRING, string TO-STRING,
                       int DELIMITED=argval, int START=point, int END=end)`

       Replace occurrences of FROM-STRING with TO-STRING.
       Preserve case in each match if `case-replace' and `case-fold-search'
       are non-zero and FROM-STRING has no uppercase letters.
       (Preserving case means that if the string matched is all caps, or
       capitalized, then its replacement is upcased or capitalized.)

       Third arg DELIMITED (prefix arg if interactive), if non-zero, means
       replace only matches surrounded by word boundaries.

       Fourth and fifth arg START and END specify the region to operate on:
       if these arguments are not provided, if the current region is
       highlighted, operate on the contents of the region, otherwise,
       operate from point to the end of the buffer.

       This function is usually the wrong thing to use in a qscript program.
       What you probably want is a loop like this:

           while (search_forward(FROM-STRING))
               replace_match(TO-STRING);

       which will run faster and will not set the mark or print anything.
       The loop will not work if FROM-STRING can match the null string
       and TO-STRING is also null
     */
    // TODO: region restriction
    int flags = SEARCH_FLAG_SMARTCASE;
    if (argval != 1)
        flags |= SEARCH_FLAG_WORD;
    query_replace(s, search_str, replace_str, 1, flags);
}

enum {
    CMD_SEARCH_BACKWARD = -1,
    CMD_COUNT_MATCHES = 0,
    CMD_SEARCH_FORWARD = 1,
    CMD_DELETE_MATCHING_LINES,
    CMD_DELETE_NON_MATCHING_LINES,
    CMD_COPY_MATCHING_LINES,
    CMD_KILL_MATCHING_LINES,
    CMD_LIST_MATCHING_LINES,
};

void do_search_string(EditState *s, const char *search_str, int mode)
{
    char32_t search_u32[SEARCH_LENGTH];
    int search_u32_len;
    int min_offset, max_offset;
    int found_offset, found_end;
    int flags;
    int offset, count = 0, p1 = 0, p2 = 0, p3, last, start = 0;
    EditBuffer *b1 = NULL;
    EditState *e;

    /* get the flags from search_str */
    flags = search_string_get_flags(search_str, SEARCH_FLAG_DEFAULT, &search_str);
    search_u32_len = search_to_u32(search_u32, countof(search_u32), search_str, flags);
    /* empty string matches */
    if (search_u32_len <= 0)
        return;

    min_offset = 0;
    max_offset = s->b->total_size;
    offset = s->offset;

    if (s->region_style) {
        /* restrict the search to the current region */
        s->region_style = 0;
        min_offset = s->b->mark;
        max_offset = s->offset;
        if (min_offset > max_offset) {
            min_offset = s->offset;
            max_offset = s->b->mark;
        }
        if (mode == CMD_SEARCH_BACKWARD) {
            offset = max_offset;
        } else {
            offset = min_offset;
        }
    }
    last = offset;
    if (mode == CMD_DELETE_MATCHING_LINES
    ||  mode == CMD_DELETE_NON_MATCHING_LINES
    ||  mode == CMD_KILL_MATCHING_LINES) {
        if (s->b->flags & BF_READONLY)
            return;
        last = eb_goto_bol(s->b, offset);
    }
    if (mode == CMD_LIST_MATCHING_LINES) {
        // XXX: should check prefix argument to clear buffer
        b1 = eb_find_new("*occur*", BF_UTF8 | (s->b->flags & BF_STYLES));
        start = b1->total_size;
    }

    while (eb_search(s->b, mode == CMD_SEARCH_BACKWARD ? -1 : 1,
                     flags, offset, max_offset,
                     search_u32, search_u32_len,
                     NULL, NULL, &found_offset, &found_end) > 0
       &&  found_offset >= min_offset)
    {
        count++;
        if (mode > CMD_SEARCH_FORWARD) {
            p1 = eb_goto_bol(s->b, found_offset);
            p2 = found_end;
            if (eb_prevc(s->b, p2, &p3) != '\n')
                p2 = eb_next_line(s->b, p2);
        }
        /* handle match and update offset carefully,
           accounting for buffer modification */
        switch (mode) {
        case CMD_COUNT_MATCHES:
            offset = found_end;
            continue;
        case CMD_SEARCH_BACKWARD:
            s->offset = found_offset;
            do_center_cursor(s, 0);
            return;
        case CMD_SEARCH_FORWARD:
            s->offset = found_end;
            do_center_cursor(s, 0);
            return;
        case CMD_DELETE_MATCHING_LINES:
            max_offset -= eb_delete_range(s->b, p1, p2);
            offset = p1;
            continue;
        case CMD_DELETE_NON_MATCHING_LINES:
            max_offset -= eb_delete_range(s->b, last, p1);
            offset = last += p2 - p1;
            continue;
        case CMD_COPY_MATCHING_LINES:
            /* first kill should use dir=0 */
            do_kill(s, p1, p2, 1, 1);
            offset = p2;
            continue;
        case CMD_KILL_MATCHING_LINES:
            do_kill(s, p1, p2, 1, 0);
            max_offset -= p2 - p1;
            offset = p1;
            continue;
        case CMD_LIST_MATCHING_LINES:
            // XXX: should store line number, colorize match and create locus
            eb_insert_buffer_convert(b1, b1->total_size, s->b, p1, p2 - p1);
            offset = p2;
            continue;
        }
    }
    switch (mode) {
    case CMD_COUNT_MATCHES:
        put_status(s, "%d matches", count);
        break;
    case CMD_DELETE_MATCHING_LINES:
        put_status(s, "deleted %d lines", count);
        break;
    case CMD_DELETE_NON_MATCHING_LINES:
        eb_delete_range(s->b, offset, s->b->total_size);
        put_status(s, "kept %d lines", count);
        break;
    case CMD_SEARCH_BACKWARD:
    case CMD_SEARCH_FORWARD:
        put_status(s, "Search failed: \"%s\"", search_str);
        break;
    case CMD_COPY_MATCHING_LINES:
        put_status(s, "copied %d lines", count);
        break;
    case CMD_KILL_MATCHING_LINES:
        put_status(s, "killed %d lines", count);
        break;
    case CMD_LIST_MATCHING_LINES:
        if (!count) {
            put_status(s, "no matches");
        } else {
            b1->offset = start;
            eb_printf(b1, "// %d lines in buffer %s:\n", count, s->b->name);
            b1->offset = b1->total_size;
            e = show_popup(s, b1, "Matches");
            // XXX: should only set the syntax mode
            edit_set_mode(e, s->mode);
        }
        break;
    }
}

static void minibuffer_search_start_edit(EditState *s) {
    ISearchState *is = set_search_state(s->target_window, 1, 1);
    if (is != NULL) {
        is->minibuffer = s;
        isearch_cycle_flags(s, 0);
    }
}

static void minibuffer_search_end_edit(EditState *s, char *dest, int size) {
    EditState *s1;
    if ((s1 = s->target_window) != NULL && s1->isearch_state) {
        // XXX: prefix the output string with search flags?
        s1->isearch_state->minibuffer = NULL;
        s1->isearch_state = NULL;
        // XXX: should free the ISearchState structure
    }
}

static CompletionDef search_completion = {
    "search", NULL, NULL, NULL, NULL, 0,
    minibuffer_search_start_edit,
    minibuffer_search_end_edit,
};

static const CmdDef isearch_commands[] = {
    CMD2( "isearch-abort", "C-g",
          "abort isearch and move point to starting point",
           isearch_abort, ES, "")
    CMD2( "isearch-cancel", "",
          "Exit isearch and run regular command",
           isearch_cancel, ES, "")
    CMDx( "isearch-complete", "M-TAB",
          "complete the search string from the search ring",
           isearch_edit_string, ES, "")
    CMD1( "isearch-center", "C-l",
          "center the window around point",
          do_center_cursor, 1)
    CMDx( "isearch-del-char", "C-M-w",
          "Delete character from end of search string",
           isearch_del_char, ES, "")
    CMD2( "isearch-delete-char", "DEL",
          "Cancel last input item from end of search string",
           isearch_delete_char, ES, "")
    CMDx( "isearch-edit-string", "M-e",
          "show the help page for isearch",
           isearch_edit_string, ES, "")
    CMD2( "isearch-exit", "RET",
          "Exit isearch, leave point at location found",
           isearch_exit, ESi, "k")
    CMDx( "isearch-mode-help", "f1, C-h",
          "show the help page for isearch",
           isearch_mode_help, ES, "")
    CMD2( "isearch-printing-char", "TAB, C-j",
          "append the character to the search string",
           isearch_printing_char, ESi, "k")
    CMDx( "isearch-query-replace", "M-%",
          "start 'query-replace' with current string to replace",
          isearch_query_replace, ES, "")
#ifdef CONFIG_REGEX
    CMDx( "isearch-query-replace-regexp", "",  // C-M-% invalid tty binding?
          "start 'query-replace-regexp' with current string to replace"
           isearch_query_replace, ES, "")
#endif
    CMD2( "isearch-quote-char", "C-q",
          "quote a control character and search for it",
           isearch_quote_char, ES, "")
    CMD3( "isearch-repeat-forward", "C-s",
          "Find next match forward",
           isearch_addpos, ESi, "v", 1)
    CMD3( "isearch-repeat-backward", "C-r",
          "Search again backward",
           isearch_addpos, ESi, "v", -1)
    CMDx( "isearch-ring-advance", "M-n, M-down",
          "get the next item from search ring",
           isearch_next_string, ES, "")
    CMDx( "isearch-ring-retreat", "M-p, M-up",
          "get the previous item from search ring.",
           isearch_previous_string, ES, "")
    CMD2( "isearch-toggle-case-fold", "M-c, C-c",
          "toggle search case-sensitivity",
           isearch_toggle_case_fold, ES, "")
    CMD2( "isearch-toggle-hex", "M-C-b",
          "toggle normal/hex/unihex searching",
           isearch_toggle_hex, ES, "")
    CMDx( "isearch-toggle-incremental", "C-o",
          "toggle incremental search",
           isearch_toggle_incremental, ES, "")
#ifdef CONFIG_REGEX
    CMD2( "isearch-toggle-regexp", "M-r, C-t",
          "toggle regular-expression mode",
           isearch_toggle_regexp, ES, "")
#endif
    CMD2( "isearch-toggle-word-match", "M-w",
          "toggle word match",
           isearch_toggle_word_match, ES, "")
    CMD2( "isearch-yank-char", "M-C-y",
          "Yank char from buffer onto end of search string",
           isearch_yank_char, ES, "")
    CMD2( "isearch-yank-kill", "M-y",  // XXX: should be C-y
          "yank the last string of killed text",
           isearch_yank_kill, ES, "")
    CMD2( "isearch-yank-line", "C-y",
          "yank rest of line onto end of search string",
           isearch_yank_line, ES, "")
    CMDx( "isearch-yank-pop-only", "M-y",
          "Replace just-yanked search string with previously killed string",
           isearch_yank_pop_only, ES, "")
    CMD2( "isearch-yank-word-or-char", "C-w",
          "yank next word or character in buffer",
           isearch_yank_word_or_char, ES, "")
    CMDx( "isearch-edit-string", "M-e",
          "edit the search string in the minibuffer",
           isearch_edit_string, ES, "")
};

static const CmdDef search_commands[] = {
    /* M-C-s should be bound to isearch-forward-regex */
    /* mg binds search-forward to M-s */
    CMD3( "search-forward", "M-S",
          "Search for a string in the current buffer",
          do_search_string, ESsi,
          "s{Search forward: }[search]|search|"
          "v", CMD_SEARCH_FORWARD)
    /* M-C-r should be bound to isearch-backward-regex */
    /* mg binds search-forward to M-r */
    CMD3( "search-backward", "M-R",
          "Search backwards for a string in the current buffer",
          do_search_string, ESsi,
          "s{Search backward: }[search]|search|"
          "v", CMD_SEARCH_BACKWARD)
    CMD3( "count-matches", "M-C",
          "Count string matches from point to the end of the current buffer",
          do_search_string, ESsi,
          "s{Count Matches: }[search]|search|"
          "v", CMD_COUNT_MATCHES)
    CMD3( "delete-matching-lines", "",
          "Delete lines containing a string from point to the end of the current buffer",
          do_search_string, ESsi, "*"
          "s{Delete lines containing: }[search]|search|"
          "v", CMD_DELETE_MATCHING_LINES)
    CMD3( "delete-non-matching-lines", "",
          "Delete lines NOT containing a string from point to the end of the current buffer",
          do_search_string, ESsi, "*"
          "s{Keep lines containing: }[search]|search|"
          "v", CMD_DELETE_NON_MATCHING_LINES)
    CMD3( "copy-matching-lines", "",
          "Copy lines containing a string to the kill buffer",
          do_search_string, ESsi, "*"
          "s{Copy lines containing: }[search]|search|"
          "v", CMD_COPY_MATCHING_LINES)
    CMD3( "kill-matching-lines", "",
          "Kill lines containing a string to the kill buffer",
          do_search_string, ESsi, "*"
          "s{Kill lines containing: }[search]|search|"
          "v", CMD_KILL_MATCHING_LINES)
    CMD3( "list-matching-lines", "",
          "List lines containing a string in popup window",
          do_search_string, ESsi, "*"
          "s{List lines containing: }[search]|search|"
          "v", CMD_LIST_MATCHING_LINES)
    /* list-matching-lines and occur are aliases */
    CMD3( "occur", "",
          "List lines containing a string in popup window",
          do_search_string, ESsi, "*"
          "s{List lines containing: }[search]|search|"
          "v", CMD_LIST_MATCHING_LINES)
    /* passing argument should switch to regex incremental search */
    CMD3( "isearch-backward", "C-r",
          "Search backward incrementally",
          do_isearch, ESii, "p" "v", -1)
    CMD3( "isearch-forward", "C-s",
          "Search forward incrementally",
          do_isearch, ESii, "p" "v", 1)
    CMD2( "query-replace", "M-%",
          "Replace a string with another interactively",
          do_query_replace, ESssi, "*"
          "s{Query replace: }[search]|search|"
          "s{With: }|replace|"
          "p")
    /* passing argument restricts replace to word matches */
    /* XXX: non standard binding */
    CMD2( "replace-string", "M-r",
          "Replace a string with another till the end of the buffer",
          do_replace_string, ESssi, "*"
          "s{Replace String: }[search]|search|"
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
    qe_register_completion(&search_completion);
    return 0;
}

qe_module_init(search_init);
