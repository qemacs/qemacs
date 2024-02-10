/*
 * QEmacs, extra commands non full version
 *
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

#include <time.h>

#include "qe.h"
#include "variables.h"

static int qe_skip_style(EditState *s, int offset, int *offsetp, QETermStyle style)
{
    char32_t buf[COLORED_MAX_LINE_SIZE];
    QETermStyle sbuf[COLORED_MAX_LINE_SIZE];
    int line_num, col_num, len, pos;
    int offset0, offset1;

    if (!s->colorize_func && !s->b->b_styles)
        return 0;

    eb_get_pos(s->b, &line_num, &col_num, offset);
    offset0 = eb_goto_bol2(s->b, offset, &pos);
    len = get_colorized_line(s, buf, countof(buf), sbuf,
                             offset0, &offset1, line_num);
    if (len > countof(buf))
        len = countof(buf);
    if (pos >= len)
        return 0;
    if (sbuf[pos] != style)
        return 0;
    while (pos < len && sbuf[pos] == style) {
        offset = eb_next(s->b, offset);
        pos++;
    }
    *offsetp = offset;
    return 1;
}

static int eb_skip_spaces(EditBuffer *b, int offset, int *offsetp)
{
    int offset0 = offset, offset1;

    while (offset < b->total_size
        && qe_isspace(eb_nextc(b, offset, &offset1))) {
        offset = offset1;
    }
    if (offset != offset0) {
        *offsetp = offset;
        return 1;
    }
    return 0;
}

static void compare_resync(EditState *s1, EditState *s2,
                           int save1, int save2,
                           int *offset1_ptr, int *offset2_ptr)
{
    int pos1, off1, pos2, off2;
    char32_t ch1, ch2;

    off1 = save1;
    off2 = save2;
    /* try skipping blanks */
    while (qe_isblank(ch1 = eb_nextc(s1->b, pos1 = off1, &off1)))
        continue;
    while (qe_isblank(ch2 = eb_nextc(s2->b, pos2 = off2, &off2)))
        continue;
    /* XXX: should try and detect a simple insertion first
       by comparing from the end of both lines */
    if (ch1 != ch2) {
        /* try skipping current words and subsequent blanks */
        off1 = pos1;
        off2 = pos2;
        while (!qe_isspace(ch1 = eb_nextc(s1->b, pos1 = off1, &off1)))
            continue;
        while (!qe_isspace(ch2 = eb_nextc(s2->b, pos2 = off2, &off2)))
            continue;
        while (qe_isblank(ch1 = eb_nextc(s1->b, pos1 = off1, &off1)))
            continue;
        while (qe_isblank(ch2 = eb_nextc(s2->b, pos2 = off2, &off2)))
            continue;
        if (ch1 != ch2) {
            /* Try to resync from end of line */
            pos1 = eb_goto_eol(s1->b, save1);
            pos2 = eb_goto_eol(s2->b, save2);
        }
    }
    while (pos1 > save1 && pos2 > save2
       &&  eb_prevc(s1->b, pos1, &off1) == eb_prevc(s2->b, pos2, &off2)) {
           pos1 = off1;
           pos2 = off2;
    }
    *offset1_ptr = pos1;
    *offset2_ptr = pos2;
}

static char *utf8_char32_to_string(char *buf, char32_t c) {
    char *p = buf;
    if (qe_isaccent(c))
        *p++ = ' ';
    p[utf8_encode(p, c)] = '\0';
    return buf;
}

void do_compare_windows(EditState *s, int argval)
{
    QEmacsState *qs = s->qe_state;
    EditState *s1;
    EditState *s2;
    int offset1, offset2, size1, size2;
    char32_t ch1, ch2;
    int tries, resync = 0;
    char buf1[MAX_CHAR_BYTES + 2], buf2[MAX_CHAR_BYTES + 2];
    const char *comment1 = "";
    const char *comment2 = "";
    const char *comment3 = "";

    s1 = s;
    /* Should use same internal function as for next_window */
    for (s2 = s1;;) {
        s2 = s2->next_window;
        if (s2 == NULL)
            s2 = qs->first_window;
        if (s2 == s1) {
            /* single window: bail out */
            return;
        }
        if (s2->b->flags & BF_DIRED)
            continue;
        break;
    }
    if (argval == 0) {
        qs->ignore_spaces = 0;
        qs->ignore_comments = 0;
        qs->ignore_case = 0;
        qs->ignore_preproc = 0;
    }
    if (argval & 4)
        qs->ignore_spaces ^= 1;
    if (argval & 16)
        qs->ignore_comments ^= 1;
    if (argval & 64)
        qs->ignore_case ^= 1;
    if (argval & 256)
        qs->ignore_preproc ^= 1;

    size1 = s1->b->total_size;
    size2 = s2->b->total_size;

    if (qs->last_cmd_func == (CmdFunc)do_compare_windows) {
        resync = 1;
    }

    for (tries = 0;; resync = 0) {

        if (++tries >= 100000) {
            tries = 0;
            //if (check_input())
            //    return;
        }

        if (s1->offset >= size1) {
            if (s2->offset >= size2) {
                put_status(s, "%s%s%sNo difference",
                           comment1, comment2, comment3);
                return;
            }
            offset1 = s1->offset;
            ch1 = 0;
            ch2 = eb_nextc(s2->b, s2->offset, &offset2);
        } else {
            ch1 = eb_nextc(s1->b, s1->offset, &offset1);
            if (s2->offset >= size2) {
                offset2 = s2->offset;
                ch2 = 0;
            } else {
                ch2 = eb_nextc(s2->b, s2->offset, &offset2);
                if (ch1 == ch2) {
                    s1->offset = offset1;
                    s2->offset = offset2;
                    continue;
                }
                if (qs->ignore_case) {
                    // XXX: should also ignore accents
                    if (qe_wtolower(ch1) == qe_wtolower(ch2)) {
                        s1->offset = offset1;
                        s2->offset = offset2;
                        comment3 = "Matched case, ";
                        continue;
                    }
                }
            }
        }
        if (qs->ignore_spaces) {
            /* UTF-8 issue for combining code points? */
            if (eb_skip_spaces(s1->b, s1->offset, &s1->offset) |
                eb_skip_spaces(s2->b, s2->offset, &s2->offset))
            {
                comment1 = "Skipped spaces, ";
                continue;
            }
        }
        if (qs->ignore_comments) {
            if (qe_skip_style(s1, s1->offset, &s1->offset, QE_STYLE_COMMENT) |
                qe_skip_style(s2, s2->offset, &s2->offset, QE_STYLE_COMMENT))
            {
                comment2 = "Skipped comments, ";
                continue;
            }
        }
        if (qs->ignore_preproc) {
            if (qe_skip_style(s1, s1->offset, &s1->offset, QE_STYLE_PREPROCESS) |
                qe_skip_style(s2, s2->offset, &s2->offset, QE_STYLE_PREPROCESS))
            {
                comment2 = "Skipped preproc, ";
                continue;
            }
        }
        if (s1->offset >= size1 || s2->offset >= size2) {
            put_status(s, "%s%s%sExtra characters",
                       comment1, comment2, comment3);
            break;
        }
        if (resync) {
            int save1 = s1->offset, save2 = s2->offset;
            compare_resync(s1, s2, save1, save2, &s1->offset, &s2->offset);
            put_status(s, "Skipped %d and %d bytes",
                       s1->offset - save1, s2->offset - save2);
        } else {
            put_status(s, "%s%s%sDifference: '%s' [0x%02X] <-> '%s' [0x%02X]",
                       comment1, comment2, comment3,
                       utf8_char32_to_string(buf1, ch1), ch1,
                       utf8_char32_to_string(buf2, ch2), ch2);
        }
        return;
    }
}

void do_compare_files(EditState *s, const char *filename, int bflags)
{
    char buf[MAX_FILENAME_SIZE + 3];
    char dir[MAX_FILENAME_SIZE];
    int pathlen, parent_pathlen;
    const char *tail;
    EditState *e;

    pathlen = get_basename_offset(filename);
    get_default_path(s->b, s->offset, dir, sizeof(dir));

    if (strstart(filename, dir, &tail)) {
        snprintf(buf, sizeof(buf), "%s../%s", dir, tail);
    } else
    if (pathlen == 0) {
        snprintf(buf, sizeof(buf), "../%s", filename);
    } else
    if (pathlen == 1) {
        put_status(s, "Reference file is in root directory: %s", filename);
        return;
    } else
    if (pathlen >= MAX_FILENAME_SIZE) {
        put_status(s, "Filename too long: %s", filename);
        return;
    } else {
        pstrcpy(buf, sizeof(buf), filename);
        buf[pathlen - 1] = '\0';  /* overwite the path separator */
        parent_pathlen = get_basename_offset(buf);
        pstrcpy(buf + parent_pathlen, sizeof(buf) - parent_pathlen, filename + pathlen);
    }

    // XXX: should check for regular file
    if (access(filename, R_OK)) {
        put_status(s, "Cannot access file %s: %s",
                   filename, strerror(errno));
        return;
    }
    if (access(buf, R_OK)) {
        put_status(s, "Cannot access file %s: %s",
                   buf, strerror(errno));
        return;
    }

    do_find_file(s, filename, bflags);
    do_delete_other_windows(s, 0);
    e = qe_split_window(s, SW_STACKED, 50);
    if (e) {
        s->qe_state->active_window = e;
        do_find_file(e, buf, bflags);
    }
}

void do_delete_horizontal_space(EditState *s)
{
    int from, to, offset;

    /* boundary check unnecessary because eb_prevc returns '\n'
     * at bof and eof and qe_isblank return true only on SPC and TAB.
     */
    from = to = s->offset;
    while (qe_isblank(eb_prevc(s->b, from, &offset)))
        from = offset;

    while (qe_isblank(eb_nextc(s->b, to, &offset)))
        to = offset;

    eb_delete_range(s->b, from, to);
}

static void do_delete_blank_lines(EditState *s) {
    /* Delete blank lines:
     * On blank line, delete all surrounding blank lines, leaving just one.
     * On isolated blank line, delete that one.
     * On nonblank line, delete any immediately following blank lines.
     */
    int p0, p1, p2, p3;
    EditBuffer *b = s->b;

    p0 = p1 = eb_goto_bol(b, s->offset);
    if (eb_is_blank_line(b, p1, &p2)) {
        while (p0 > 0) {
            int offset0 = eb_prev_line(b, p0);
            if (!eb_is_blank_line(b, offset0, NULL))
                break;
            p0 = offset0;
        }
    } else {
        p0 = p1 = p2 = eb_next_line(b, s->offset);
    }

    p3 = p2;
    while (p3 < s->b->total_size) {
        if (!eb_is_blank_line(b, p3, &p3))
            break;
    }
    if (p0 < p1 || p2 < p3) {
        /* delete the second block first so p0 and p1 stay correct */
        eb_delete_range(b, p2, p3);
        eb_delete_range(b, p0, p1);
    } else {
        eb_delete_range(b, p1, p2);
    }
}

static void do_tabify(EditState *s, int p1, int p2)
{
    /* We implement a complete analysis of the region instead of
     * scanning for certain space patterns (such as / [ \t]/).  It is
     * fast enough, sometimes even faster, more concise and more
     * correct for pathological case where initial space falls on tab
     * column.
     */
    /* XXX: should extend for language modes to not replace spaces
     * inside character constants, strings, regex, comments,
     * preprocessor, etc.  Implementation is not too difficult with a
     * new buffer reader eb_nextc_style() using colorizer and a
     * one line cache.
     */
    EditBuffer *b = s->b;
    int tw = b->tab_width > 0 ? b->tab_width : 8;
    int start = max_offset(0, min_offset(p1, p2));
    int stop = min_offset(b->total_size, max_offset(p1, p2));
    int col;
    int offset, offset1, offset2, delta;

    /* deactivate region hilite */
    s->region_style = 0;

    col = 0;
    offset = eb_goto_bol(b, start);

    for (; offset < stop; offset = offset1) {
        char32_t c = eb_nextc(b, offset, &offset1);
        if (c == '\r' || c == '\n') {
            col = 0;
            continue;
        }
        if (c == '\t') {
            col += tw - col % tw;
            continue;
        }
        col += qe_wcwidth(c);
        if (c != ' ' || offset < start || col % tw == 0)
            continue;
        while (offset1 < stop) {
            c = eb_nextc(b, offset1, &offset2);
            if (c == ' ') {
                col += 1;
                offset1 = offset2;
                if (col % tw == 0) {
                    delta = -eb_delete_range(b, offset, offset1);
                    delta += eb_insert_char32(b, offset, '\t');
                    offset1 += delta;
                    stop += delta;
                    break;
                }
                continue;
            } else
            if (c == '\t') {
                col += tw - col % tw;
                delta = -eb_delete_range(b, offset, offset1);
                offset1 = offset2 + delta;
                stop += delta;
            }
            break;
        }
    }
}
#if 0
static void do_tabify_buffer(EditState *s)
{
    /* deactivate region hilite */
    s->region_style = 0;

    eb_tabify(s->b, 0, s->b->total_size);
}

static void do_tabify_region(EditState *s)
{
    /* deactivate region hilite */
    s->region_style = 0;

    eb_tabify(s->b, s->b->mark, s->offset);
}
#endif
static void do_untabify(EditState *s, int p1, int p2)
{
    /* We implement a complete analysis of the region instead of
     * potentially faster scan for '\t'.  It is fast enough and even
     * faster if there are lots of tabs.
     */
    EditBuffer *b = s->b;
    int tw = b->tab_width > 0 ? b->tab_width : 8;
    int start = max_offset(0, min_offset(p1, p2));
    int stop = min_offset(b->total_size, max_offset(p1, p2));
    int col, col0;
    int offset, offset1, offset2, delta;

    /* deactivate region hilite */
    s->region_style = 0;

    col = 0;
    offset = eb_goto_bol(b, start);

    for (; offset < stop; offset = offset1) {
        char32_t c = eb_nextc(b, offset, &offset1);
        if (c == '\r' || c == '\n') {
            col = 0;
            continue;
        }
        if (c != '\t') {
            col += qe_wcwidth(c);
            continue;
        }
        col0 = col;
        col += tw - col % tw;
        if (offset < start)
            continue;
        while (eb_nextc(b, offset1, &offset2) == '\t') {
            col += tw;
            offset1 = offset2;
        }
        delta = -eb_delete_range(b, offset, offset1);
        delta += eb_insert_spaces(b, offset, col - col0);
        offset1 += delta;
        stop += delta;
    }
}
#if 0
static void do_untabify_buffer(EditState *s)
{
    /* deactivate region hilite */
    s->region_style = 0;

    eb_untabify(s->b, 0, s->b->total_size);
}

static void do_untabify_region(EditState *s)
{
    /* deactivate region hilite */
    s->region_style = 0;

    eb_untabify(s->b, s->b->mark, s->offset);
}
#endif
static void do_indent_region(EditState *s, int start, int end)
{
    int col_num, line1, line2;

    /* deactivate region hilite */
    s->region_style = 0;

    /* Swap point and mark so mark <= point */
    if (end < start) {
        int tmp = start;
        end = start;
        start = tmp;
    }
    /* We do it with lines to avoid offset variations during indenting */
    eb_get_pos(s->b, &line1, &col_num, start);
    eb_get_pos(s->b, &line2, &col_num, end);

    if (col_num == 0)
        line2--;

    /* Iterate over all lines inside block */
    s->b->mark = eb_goto_bol(s->b, start);
    for (; line1 <= line2; line1++) {
        if (s->mode->indent_func) {
            (s->mode->indent_func)(s, eb_goto_pos(s->b, line1, 0));
        } else {
            s->offset = eb_goto_pos(s->b, line1, 0);
            do_tab(s, 1);
        }
    }
    s->offset = eb_goto_eol(s->b, s->offset);
}

void do_show_date_and_time(EditState *s, int argval)
{
    time_t t = argval;

    if (argval == NO_ARG)
        time(&t);

    put_status(s, "%.24s", ctime(&t));
}

/* forward / backward block */
#define MAX_LEVEL     32

/* Return the matching delimiter for all pairs */
static char32_t matching_delimiter(char32_t c) {
    const u8 *pairs = (const u8 *)"(){}[]<>";
    int i;

    for (i = 0; pairs[i]; i++) {
        if (pairs[i] == c)
            return pairs[i ^ 1];
    }
    return c;
}

static void forward_block(EditState *s, int dir)
{
    char32_t buf[COLORED_MAX_LINE_SIZE];
    QETermStyle sbuf[COLORED_MAX_LINE_SIZE];
    char32_t balance[MAX_LEVEL];
    int use_colors;
    int line_num, col_num, style, style0, level;
    int pos;      /* position of the current character on line */
    int len;      /* number of colorized positions */
    int offset;   /* offset of the current character */
    int offset0;  /* offset of the beginning of line */
    int offset1;  /* offset of the beginning of the next line */
    char32_t c;

    offset = s->offset;
    eb_get_pos(s->b, &line_num, &col_num, offset);
    offset1 = offset0 = eb_goto_bol2(s->b, offset, &pos);
    use_colors = s->colorize_func || s->b->b_styles;
    style0 = 0;
    len = 0;
    if (use_colors) {
        len = get_colorized_line(s, buf, countof(buf), sbuf,
                                 offset1, &offset1, line_num);
        if (len < countof(buf) - 2) {
            if (pos > 0
            &&  ((c = buf[pos - 1]) == ']' || c == '}' || c == ')')) {
                style0 = sbuf[pos - 1];
            } else
            if (pos < len) {
                style0 = sbuf[pos];
            }
        } else {
            /* very long line detected, use fallback version */
            use_colors = 0;
            len = 0;
        }
    }
    level = 0;

    if (dir < 0) {
        for (;;) {
            c = eb_prevc(s->b, offset, &offset);
            if (c == '\n') {
                if (offset <= 0)
                    break;
                line_num--;
                offset1 = offset0 = eb_goto_bol2(s->b, offset, &pos);
                len = 0;
                if (use_colors) {
                    len = get_colorized_line(s, buf, countof(buf), sbuf,
                                             offset1, &offset1, line_num);
                    if (len >= countof(buf) - 2) {
                        /* very long line detected, use fallback version */
                        use_colors = 0;
                        len = 0;
                        style0 = 0;
                    }
                }
                continue;
            }
            style = 0;
            --pos;
            if (pos >= 0 && pos < len) {
                style = sbuf[pos];
            }
            if (style != style0 && style != QE_STYLE_KEYWORD && style != QE_STYLE_FUNCTION) {
                if (style0 == 0)
                    continue;
                style0 = 0;
                if (style != 0)
                    continue;
            }
            switch (c) {
            case '\"':
            case '\'':
                if (pos >= len) {
                    /* simplistic string skip with escape char */
                    int off;
                    char32_t c1;
                    while ((c1 = eb_prevc(s->b, offset, &off)) != '\n') {
                        offset = off;
                        pos--;
                        if (c1 == c && eb_prevc(s->b, offset, &off) != '\\')
                            break;
                    }
                }
                break;
            case ')':
            case ']':
            case '}':
                if (level < MAX_LEVEL) {
                    balance[level] = matching_delimiter(c);
                }
                level++;
                break;
            case '(':
            case '[':
            case '{':
                if (level > 0) {
                    --level;
                    if (level < MAX_LEVEL && balance[level] != c) {
                        /* XXX: should set mark and offset */
                        put_status(s, "Unmatched delimiter %c <> %c",
                                   c, balance[level]);
                        return;
                    }
                    if (level == 0) {
                        s->offset = offset;
                        return;
                    }
                } else {
                    /* silently move up one level */
                }
                break;
            }
        }
    } else {
        for (;;) {
            c = eb_nextc(s->b, offset, &offset);
            if (c == '\n') {
                line_num++;
                if (offset >= s->b->total_size)
                    break;
                offset1 = offset0 = offset;
                len = 0;
                if (use_colors) {
                    len = get_colorized_line(s, buf, countof(buf), sbuf,
                                             offset1, &offset1, line_num);
                    if (len >= countof(buf) - 2) {
                        /* very long line detected, use fallback version */
                        use_colors = 0;
                        len = 0;
                        style0 = 0;
                    }
                }
                pos = 0;
                continue;
            }
            style = 0;
            if (pos < len) {
                style = sbuf[pos];
            }
            pos++;
            if (style0 != style && style != QE_STYLE_KEYWORD && style != QE_STYLE_FUNCTION) {
                if (style0 == 0)
                    continue;
                style0 = 0;
                if (style != 0)
                    continue;
            }
            switch (c) {
            case '\"':
            case '\'':
                if (pos >= len) {
                    /* simplistic string skip with escape char */
                    int off;
                    char32_t c1;
                    while ((c1 = eb_nextc(s->b, offset, &off)) != '\n') {
                        offset = off;
                        pos++;
                        if (c1 == '\\') {
                            if (eb_nextc(s->b, offset, &off) == '\n')
                                break;
                            offset = off;
                            pos++;
                        } else
                        if (c1 == c) {
                            break;
                        }
                    }
                }
                break;
            case '(':
            case '[':
            case '{':
                if (level < MAX_LEVEL) {
                    balance[level] = matching_delimiter(c);
                }
                level++;
                break;
            case ')':
            case ']':
            case '}':
                if (level > 0) {
                    --level;
                    if (level < MAX_LEVEL && balance[level] != c) {
                        /* XXX: should set mark and offset */
                        put_status(s, "Unmatched delimiter %c <> %c",
                                   c, balance[level]);
                        return;
                    }
                    if (level == 0) {
                        s->offset = offset;
                        return;
                    }
                } else {
                    /* silently move up one level */
                }
                break;
            }
        }
    }
    if (level != 0) {
        /* XXX: should set mark and offset */
        put_status(s, "Unmatched delimiter");
    } else {
        s->offset = offset;
    }
}

static void do_forward_block(EditState *s, int n)
{
    int dir = n < 0 ? -1 : 1;

    for (; n != 0; n -= dir) {
        forward_block(s, dir);
    }
}

static void do_kill_block(EditState *s, int n)
{
    int start = s->offset;

    if (n != 0) {
        do_forward_block(s, n);
        do_kill(s, start, s->offset, n, 0);
    }
}

void do_transpose(EditState *s, int cmd)
{
    QEmacsState *qs = s->qe_state;
    int offset0, offset1, offset2, offset3, end_offset;
    int size0, size1, size2;
    EditBuffer *b = s->b;

    if (check_read_only(s))
        return;

    /* compute positions of ranges to swap:
       offset0..offset1 and offset2..offset3
       end_offset is the ending position after the swap
    */
    switch (cmd) {
    case CMD_TRANSPOSE_CHARS:
        /* at end of line, transpose previous 2 characters,
         * otherwise transpose characters before and after point.
         */
        offset1 = offset2 = s->offset;
        offset0 = eb_prev(b, offset1);
        if (eb_nextc(b, offset2, &offset3) == '\n') {
            end_offset = offset3 = offset2;
            offset1 = offset2 = offset0;
            offset0 = eb_prev(b, offset1);
        } else {
            /* XXX: should have specific flag */
            if (qs->emulation_flags == 1) {
                /* keep current position between characters */
                end_offset = offset0 + offset3 - offset2;
            } else {
                /* set position past second character (emacs behaviour) */
                end_offset = offset3;
            }
        }
        break;
    case CMD_TRANSPOSE_WORDS:
        word_right(s, 1);
        offset3 = word_right(s, 0);
        offset2 = word_left(s, 0);
        offset1 = word_left(s, 1);
        offset0 = word_left(s, 0);
        if (qs->emulation_flags == 1) {
            /* set position to end of first word */
            end_offset = offset0 + offset3 - offset2;
        } else {
            /* set position past last word (emacs behaviour) */
            end_offset = offset3;
        }
        break;
    case CMD_TRANSPOSE_LINES:
        do_eol(s);
        offset3 = s->offset;
        do_bol(s);
        offset2 = s->offset;
        offset1 = eb_prev(b, offset2);    /* skip line feed */
        s->offset = offset1;
        do_bol(s);
        offset0 = s->offset;
        if (qs->emulation_flags == 1) {
            /* set position to start of second line */
            end_offset = offset0 + offset3 - offset1;
        } else {
            /* set position past second line (emacs behaviour) */
            end_offset = offset3;
        }
        break;
    default:
        return;
    }
    size0 = offset1 - offset0;
    size1 = offset2 - offset1;
    size2 = offset3 - offset2;

    if (!b->b_styles && size0 + size1 + size2 <= 1024) {
        u8 buf[1024];
        /* Use fast method and generate single undo record */
        eb_read(b, offset2, buf, size2);
        eb_read(b, offset1, buf + size2, size1);
        eb_read(b, offset0, buf + size2 + size1, size0);
        eb_write(b, offset0, buf, size0 + size1 + size2);
    } else {
        EditBuffer *b1 = eb_new("*tmp*", BF_SYSTEM | (b->flags & BF_STYLES));

        eb_set_charset(b1, b->charset, b->eol_type);
        /* Use eb_insert_buffer_convert to copy styles.
         * This conversion should not change sizes */
        eb_insert_buffer_convert(b1, 0, b, offset2, size2);
        eb_insert_buffer_convert(b1, size2, b, offset1, size1);
        eb_insert_buffer_convert(b1, size2 + size1, b, offset0, size0);
        /* XXX: This will create 2 undo records */
        eb_delete(b, offset0, size0 + size1 + size2);
        eb_insert_buffer_convert(b, offset0, b1, 0, b1->total_size);
        eb_free(&b1);
    }
    s->offset = end_offset;
}

/*---------------- help ----------------*/

int qe_list_bindings(const CmdDef *d, ModeDef *mode, int inherit, char *buf, int size)
{
    buf_t outbuf, *out;
    ModeDef *mode0 = mode;

    out = buf_init(&outbuf, buf, size);
    for (;;) {
        KeyDef *kd = mode ? mode->first_key : qe_state.first_key;

        for (; kd != NULL; kd = kd->next) {
            /* do not list overridden bindings */
            if (kd->cmd == d
            &&  qe_find_current_binding(kd->keys, kd->nb_keys, mode0, 1) == kd) {
                if (out->len > 0)
                    buf_puts(out, ", ");

                buf_put_keys(out, kd->keys, kd->nb_keys);
            }
        }
        if (!inherit || !mode)
            break;
        /* Move up to base mode */
        mode = mode->fallback;
    }
    return out->len;
}

void do_show_bindings(EditState *s, const char *cmd_name)
{
    char buf[256];
    const CmdDef *d;

    if ((d = qe_find_cmd(cmd_name)) == NULL) {
        put_status(s, "No command %s", cmd_name);
        return;
    }
    if (qe_list_bindings(d, s->mode, 1, buf, sizeof(buf))) {
        put_status(s, "%s is bound to %s", cmd_name, buf);
    } else {
        put_status(s, "%s is not bound to any key", cmd_name);
    }
}

static void do_describe_function(EditState *s, const char *cmd_name) {
    EditBuffer *b;
    const CmdDef *d;
    const char *desc;

    if ((d = qe_find_cmd(cmd_name)) == NULL) {
        put_status(s, "No command %s", cmd_name);
        return;
    }
    b = new_help_buffer();
    if (!b)
        return;

    eb_putc(b, '\n');
    desc = d->spec + strlen(d->spec) + 1;
    /* print name, prototype, bindings and description */
    eb_command_print_entry(b, d, s);
    eb_putc(b, '\n');
    if (*desc) {
        /* print short description */
        eb_printf(b, "  %s\n", desc);
    }
    // XXX: should look up markdown documentation
    show_popup(s, b, "Help");
}

// Sort Flags: do not use 1 for argval compatibility
#define SF_FOLD       0x02
#define SF_REVERSE    0x04
#define SF_DICT       0x08
#define SF_NUMBER     0x10
#define SF_COLUMN     0x20
#define SF_BASENAME   0x40
#define SF_PARAGRAPH  0x80
#define SF_SILENT     0x100
static int eb_sort_span(EditBuffer *b, int *pp1, int *pp2, int cur_offset, int flags);

static void print_bindings(EditBuffer *b, ModeDef *mode)
{
    struct QEmacsState *qs = &qe_state;
    char buf[256];
    const CmdDef *d;
    int gfound, start, stop, i, j;

    start = 0;
    gfound = 0;
    for (i = 0; i < qs->cmd_array_count; i++) {
        for (j = qs->cmd_array[i].count, d = qs->cmd_array[i].array; j-- > 0; d++) {
            if (qe_list_bindings(d, mode, 0, buf, sizeof(buf))) {
                if (!gfound) {
                    if (mode) {
                        eb_printf(b, "\n%s mode bindings:\n\n", mode->name);
                    } else {
                        eb_printf(b, "\nGlobal bindings:\n\n");
                    }
                    start = b->offset;
                    gfound = 1;
                }
                eb_printf(b, "%24s : %s\n", d->name, buf);
            }
        }
    }
    if (gfound) {
        stop = b->offset;
        eb_sort_span(b, &start, &stop, stop, SF_DICT | SF_SILENT);
    }
}

void do_describe_bindings(EditState *s, int argval)
{
    EditBuffer *b;

    b = new_help_buffer();
    if (!b)
        return;

    print_bindings(b, s->mode);
    print_bindings(b, NULL);

    show_popup(s, b, "Bindings");
}

void do_apropos(EditState *s, const char *str)
{
    QEmacsState *qs = s->qe_state;
    char buf[256];
    EditBuffer *b;
    const CmdDef *d;
    VarDef *vp;
    int found_command, found_variable, start, stop, i, j;

    b = new_help_buffer();
    if (!b)
        return;

    eb_putc(b, '\n');

    start = b->offset;
    found_command = 0;
    for (i = 0; i < qs->cmd_array_count; i++) {
        for (j = qs->cmd_array[i].count, d = qs->cmd_array[i].array; j-- > 0; d++) {
            const char *desc = d->spec + strlen(d->spec) + 1;
            if (strstr(d->name, str) || strstr(desc, str)) {
                /* print name, prototype, bindings */
                eb_command_print_entry(b, d, s);
                eb_putc(b, '\n');
                if (*desc) {
                    /* print short description */
                    eb_printf(b, "  %s\n", desc);
                }
                found_command = 1;
            }
        }
    }
    if (found_command) {
        stop = b->offset;
        eb_sort_span(b, &start, &stop, stop, SF_DICT | SF_PARAGRAPH | SF_SILENT);
    }

    start = b->offset;
    found_variable = 0;
    for (vp = qs->first_variable; vp; vp = vp->next) {
        if (strstr(vp->name, str)) {
            /* print class, name and current value */
            eb_variable_print_entry(b, vp, s);
            eb_putc(b, '\n');
            if (vp->desc && *vp->desc) {
                /* print short description */
                eb_printf(b, "  %s\n", vp->desc);
            }
            found_variable = 1;
        }
    }
    if (found_variable) {
        stop = b->offset;
        eb_sort_span(b, &start, &stop, stop, SF_DICT | SF_PARAGRAPH | SF_SILENT);
    }

    if (found_command + found_variable) {
        snprintf(buf, sizeof buf, "Apropos '%s'", str);
        show_popup(s, b, buf);
    } else {
        eb_free(&b);
        put_status(s, "No apropos matches for `%s'", str);
    }
}

#ifndef CONFIG_CYGWIN
extern char **environ;
#endif

static void do_about_qemacs(EditState *s)
{
    QEmacsState *qs = s->qe_state;
    char buf[256];
    EditBuffer *b;
    ModeDef *m;
    const CmdDef *d;
    int start, stop, i, j;

    b = eb_scratch("*About QEmacs*", BF_UTF8);
    eb_printf(b, "\n  %s\n\n%s\n", str_version, str_credits);

    /* list current bindings */
    print_bindings(b, s->mode);
    print_bindings(b, NULL);

    /* other mode bindings */
    for (m = qs->first_mode; m; m = m->next) {
        if (m != s->mode)
            print_bindings(b, m);
    }

    /* list commands */
    eb_printf(b, "\nCommands:\n\n");

    start = b->offset;
    for (i = 0; i < qs->cmd_array_count; i++) {
        for (j = qs->cmd_array[i].count, d = qs->cmd_array[i].array; j-- > 0; d++) {
            qe_get_prototype(d, buf, sizeof(buf));
            eb_printf(b, "    %s%s\n", d->name, buf);
        }
    }
    stop = b->offset;
    eb_sort_span(b, &start, &stop, stop, SF_DICT | SF_SILENT);

    qe_list_variables(s, b);

    /* list environment */
    {
        char **envp;

        eb_printf(b, "\nEnvironment:\n\n");
        start = b->offset;
        for (envp = environ; *envp; envp++) {
            eb_printf(b, "    %s\n", *envp);
        }
        stop = b->offset;
        eb_sort_span(b, &start, &stop, stop, SF_DICT | SF_SILENT);
    }

    show_popup(s, b, "About QEmacs");
}

/* extract the next word from the ASCII string. ignore spaces, stop on '/' */
static int str_get_word7(char *buf, int size, const char *p, const char **pp)
{
    int len = 0;

    while (*p == ' ')
        p++;

    if (*p == '/') {
        /* special case meta character '/' */
        if (len + 1 < size) {
            buf[len] = *p;
        }
        p++;
        len++;
    } else {
        for (; *p != '\0' && *p != ' ' && *p != '/'; p++, len++) {
            if (len + 1 < size) {
                buf[len] = qe_tolower((u8)*p);
            }
        }
    }
    if (size > 0) {
        if (len < size)
            buf[len] = '\0';
        else
            buf[size - 1] = '\0';
    }

    while (*p == ' ')
        p++;

    if (pp)
        *pp = p;

    return len;
}

static int qe_term_get_style(const char *str, QETermStyle *style)
{
    char buf[128];
    QEColor fg_color, bg_color;
    unsigned int fg, bg, attr;
    int len;
    const char *p = str;

    attr = 0;
    for (;;) {
        len = str_get_word7(buf, sizeof(buf), p, &p);

        if (strfind("bold|strong", buf)) {
            attr |= QE_TERM_BOLD;
            continue;
        }
        if (strfind("italic|italics", buf)) {
            attr |= QE_TERM_ITALIC;
            continue;
        }
        if (strfind("underlined|underline", buf)) {
            attr |= QE_TERM_UNDERLINE;
            continue;
        }
        if (strfind("blinking|blink", buf)) {
            attr |= QE_TERM_BLINK;
            continue;
        }
        break;
    }
    fg_color = QERGB(0xbb, 0xbb, 0xbb);
    bg_color = QERGB(0x00, 0x00, 0x00);
    if (len > 0) {
        if (css_get_color(&fg_color, buf))
            return 1;
        len = str_get_word7(buf, sizeof(buf), p, &p);
        if (strfind("/|on", buf)) {
            str_get_word7(buf, sizeof(buf), p, &p);
            if (css_get_color(&bg_color, buf))
                return 2;
        }
    }
    fg = qe_map_color(fg_color, xterm_colors, QE_TERM_FG_COLORS, NULL);
    bg = qe_map_color(bg_color, xterm_colors, QE_TERM_BG_COLORS, NULL);

    *style = QE_TERM_COMPOSITE | attr | QE_TERM_MAKE_COLOR(fg, bg);
    return 0;
}

static void do_set_region_color(EditState *s, const char *str)
{
    int offset, size;
    QETermStyle style;

    /* deactivate region hilite */
    s->region_style = 0;

    if (qe_term_get_style(str, &style)) {
        put_status(s, "Invalid color '%s'", str);
        return;
    }

    offset = s->b->mark;
    size = s->offset - offset;
    if (size < 0) {
        offset += size;
        size = -size;
    }
    if (size > 0) {
        eb_create_style_buffer(s->b, QE_TERM_STYLE_BITS <= 16 ? BF_STYLE2 :
                               QE_TERM_STYLE_BITS <= 32 ? BF_STYLE4 : BF_STYLE8);
        eb_set_style(s->b, style, LOGOP_WRITE, offset, size);
    }
}

static void do_set_region_style(EditState *s, const char *str)
{
    int offset, size;
    QETermStyle style;
    QEStyleDef *st;

    /* deactivate region hilite */
    s->region_style = 0;

    st = find_style(str);
    if (!st) {
        put_status(s, "Invalid style '%s'", str);
        return;
    }
    style = st - qe_styles;

    offset = s->b->mark;
    size = s->offset - offset;
    if (size < 0) {
        offset += size;
        size = -size;
    }
    if (size > 0) {
        eb_create_style_buffer(s->b, QE_TERM_STYLE_BITS <= 16 ? BF_STYLE2 :
                               QE_TERM_STYLE_BITS <= 32 ? BF_STYLE4 : BF_STYLE8);
        eb_set_style(s->b, style, LOGOP_WRITE, offset, size);
    }
}

static void do_drop_styles(EditState *s)
{
    eb_free_style_buffer(s->b);
    s->b->flags &= ~BF_STYLES;
}

static void do_set_eol_type(EditState *s, int eol_type)
{
    eb_set_charset(s->b, s->b->charset, eol_type);
}

static void do_describe_buffer(EditState *s, int argval)
{
    char buf[256];
    buf_t descbuf, *desc;
    EditBuffer *b = s->b;
    EditBuffer *b1;

    b1 = new_help_buffer();
    if (!b1)
        return;

    eb_putc(b1, '\n');

    eb_printf(b1, "        name: %s\n", b->name);
    eb_printf(b1, "    filename: %s\n", b->filename);
    eb_printf(b1, "    modified: %d\n", b->modified);
    eb_printf(b1, "  total_size: %d\n", b->total_size);
    eb_printf(b1, "        mark: %d\n", b->mark);
    eb_printf(b1, "   s->offset: %d\n", s->offset);
    eb_printf(b1, "   b->offset: %d\n", b->offset);

    eb_printf(b1, "   tab_width: %d\n", b->tab_width);
    eb_printf(b1, " fill_column: %d\n", b->fill_column);
    if (b->linum_mode_set) {
        eb_printf(b1, "  linum_mode: %d\n", b->linum_mode);
    }

    desc = buf_init(&descbuf, buf, countof(buf));
    if (b->eol_type == EOL_UNIX)
        buf_puts(desc, " unix");
    if (b->eol_type == EOL_DOS)
        buf_puts(desc, " dos");
    if (b->eol_type == EOL_MAC)
        buf_puts(desc, " mac");

    eb_printf(b1, "    eol_type: %d %s\n", b->eol_type, buf);
    eb_printf(b1, "     charset: %s  (bytes=%d, shift=%d)\n",
              b->charset->name, b->char_bytes, b->char_shift);

    desc = buf_init(&descbuf, buf, countof(buf));
    if (b->flags & BF_SAVELOG)
        buf_puts(desc, " SAVELOG");
    if (b->flags & BF_SYSTEM)
        buf_puts(desc, " SYSTEM");
    if (b->flags & BF_READONLY)
        buf_puts(desc, " READONLY");
    if (b->flags & BF_PREVIEW)
        buf_puts(desc, " PREVIEW");
    if (b->flags & BF_LOADING)
        buf_puts(desc, " LOADING");
    if (b->flags & BF_SAVING)
        buf_puts(desc, " SAVING");
    if (b->flags & BF_DIRED)
        buf_puts(desc, " DIRED");
    if (b->flags & BF_UTF8)
        buf_puts(desc, " UTF8");
    if (b->flags & BF_RAW)
        buf_puts(desc, " RAW");
    if (b->flags & BF_TRANSIENT)
        buf_puts(desc, " TRANSIENT");
    if (b->flags & BF_STYLES)
        buf_puts(desc, " STYLES");

    eb_printf(b1, "       flags: 0x%02x %s\n", b->flags, buf);
#if 0
    eb_printf(b1, "      probed: %d\n", b->probed);
#endif

    if (b->data_mode)
        eb_printf(b1, "   data_mode: %s\n", b->data_mode->name);
    if (b->syntax_mode)
        eb_printf(b1, " syntax_mode: %s\n", b->syntax_mode->name);
    if (s->mode)
        eb_printf(b1, "     s->mode: %s\n", s->mode->name);
    if (b->default_mode)
        eb_printf(b1, "default_mode: %s\n", b->default_mode->name);
    if (b->saved_mode)
        eb_printf(b1, "  saved_mode: %s\n", b->saved_mode->name);

    eb_printf(b1, "   data_type: %s\n", b->data_type->name);
    eb_printf(b1, "       pages: %d\n", b->nb_pages);

    if (b->map_address) {
        eb_printf(b1, " map_address: %p  (length=%d, handle=%d)\n",
                  b->map_address, b->map_length, b->map_handle);
    }

    eb_printf(b1, "    save_log: %d  (new_index=%d, current=%d, nb_logs=%d)\n",
              b->save_log, b->log_new_index, b->log_current, b->nb_logs);
    eb_printf(b1, "      styles: %d  (cur_style=%lld, bytes=%d, shift=%d)\n",
              !!b->b_styles, (long long)b->cur_style,
              b->style_bytes, b->style_shift);

    if (b->total_size > 0) {
        u8 iobuf[4096];
        int count[256];
        int total_size = b->total_size;
        int offset, c, i, col, max_count, count_width;
        int word_char, word_count, nb_chars, line, column;

        eb_get_pos(b, &line, &column, total_size);
        nb_chars = eb_get_char_offset(b, total_size);

        memset(count, 0, sizeof(count));
        word_count = 0;
        word_char = 0;
        for (offset = 0; offset < total_size;) {
            int size = eb_read(b, offset, iobuf, countof(iobuf));
            if (size == 0)
                break;
            for (i = 0; i < size; i++) {
                c = iobuf[i];
                count[c] += 1;
                if (c <= 32) {
                    word_count += word_char;
                    word_char = 0;
                } else {
                    word_char = 1;
                }
            }
            offset += size;
        }
        max_count = 0;
        for (i = 0; i < 256; i++) {
            max_count = max_offset(max_count, count[i]);
        }
        count_width = snprintf(NULL, 0, "%d", max_count);

        eb_printf(b1, "       chars: %d\n", nb_chars);
        eb_printf(b1, "       words: %d\n", word_count);
        eb_printf(b1, "       lines: %d\n", line + (column > 0));

        eb_printf(b1, "\nByte stats:\n");

        for (col = i = 0; i < 256; i++) {
            if (count[i] == 0)
                continue;

            col += eb_printf(b1, "   %*d  ", count_width, count[i]);
            if (i > 0 && i < 0x7f) {
                char cbuf[8];
                byte_quote(cbuf, sizeof cbuf, i);
                col += eb_printf(b1, "'%s'", cbuf);
            } else {
                col += eb_printf(b1, "0x%02x", i);
            }
            if (col >= 60) {
                eb_putc(b1, '\n');
                col = 0;
            }
        }
        if (col)
            eb_putc(b1, '\n');
    }

    if (b->nb_pages) {
        Page *p;
        const u8 *pc;
        int i, n;

        eb_printf(b1, "\nBuffer page layout:\n");

        eb_printf(b1, "    page  size  flags  lines   col  chars  addr\n");
        for (i = 0, p = b->page_table; i < b->nb_pages && i < 100; i++, p++) {
            eb_printf(b1, "    %4d  %4d  %5x  %5d  %4d  %5d  %p  |",
                      i, p->size, p->flags, p->nb_lines, p->col, p->nb_chars,
                      (void *)p->data);
            pc = p->data;
            n = min_offset(p->size, 16);
            while (n-- > 0) {
                char cbuf[8];
                byte_quote(cbuf, sizeof cbuf, *pc++);
                eb_puts(b1, cbuf);
            }
            eb_printf(b1, "|%s\n", p->size > 16 ? "..." : "");
        }
        eb_putc(b1, '\n');
    }

    show_popup(s, b1, "Buffer Description");
}

static void do_describe_window(EditState *s, int argval)
{
    EditBuffer *b1;
    int w;

    b1 = new_help_buffer();
    if (!b1)
        return;

    eb_putc(b1, '\n');

    w = 28;
    eb_printf(b1, "%*s: %d, %d\n", w, "xleft, ytop", s->xleft, s->ytop);
    eb_printf(b1, "%*s: %d, %d\n", w, "width, height", s->width, s->height);
    eb_printf(b1, "%*s: %d, %d, %d, %d\n", w, "x1, y1, x2, y2", s->x1, s->y1, s->x2, s->y2);
    eb_printf(b1, "%*s: %#x%s%s%s%s%s%s%s\n", w, "flags", s->flags,
              (s->flags & WF_POPUP) ? " POPUP" : "",
              (s->flags & WF_MODELINE) ? " MODELINE" : "",
              (s->flags & WF_RSEPARATOR) ? " RSEPARATOR" : "",
              (s->flags & WF_POPLEFT) ? " POPLEFT" : "",
              (s->flags & WF_MINIBUF) ? " MINIBUF" : "",
              (s->flags & WF_HIDDEN) ? " HIDDEN" : "",
              (s->flags & WF_FILELIST) ? " FILELIST" : "");
    eb_printf(b1, "%*s: %d\n", w, "offset", s->offset);
    eb_printf(b1, "%*s: %d\n", w, "offset_top", s->offset_top);
    eb_printf(b1, "%*s: %d\n", w, "offset_bottom", s->offset_bottom);
    eb_printf(b1, "%*s: %d\n", w, "y_disp", s->y_disp);
    eb_printf(b1, "%*s: %d, %d\n", w, "x_disp[]", s->x_disp[0], s->x_disp[1]);
    eb_printf(b1, "%*s: %d\n", w, "dump_width", s->dump_width);
    eb_printf(b1, "%*s: %d\n", w, "hex_mode", s->hex_mode);
    eb_printf(b1, "%*s: %d\n", w, "unihex_mode", s->unihex_mode);
    eb_printf(b1, "%*s: %d\n", w, "hex_nibble", s->hex_nibble);
    eb_printf(b1, "%*s: %d\n", w, "overwrite", s->overwrite);
    eb_printf(b1, "%*s: %d\n", w, "bidir", s->bidir);
    eb_printf(b1, "%*s: %d\n", w, "cur_rtl", s->cur_rtl);
    eb_printf(b1, "%*s: %d  %s\n", w, "wrap", s->wrap,
              s->wrap == WRAP_AUTO ? "AUTO" :
              s->wrap == WRAP_TRUNCATE ? "TRUNCATE" :
              s->wrap == WRAP_LINE ? "LINE" :
              s->wrap == WRAP_TERM ? "TERM" :
              s->wrap == WRAP_WORD ? "WORD" : "???");
    eb_printf(b1, "%*s: %d\n", w, "indent_size", s->indent_size);
    eb_printf(b1, "%*s: %d\n", w, "indent_tabs_mode", s->indent_tabs_mode);
    eb_printf(b1, "%*s: %d\n", w, "interactive", s->interactive);
    eb_printf(b1, "%*s: %d\n", w, "force_highlight", s->force_highlight);
    eb_printf(b1, "%*s: %d\n", w, "mouse_force_highlight", s->mouse_force_highlight);
    eb_printf(b1, "%*s: %p\n", w, "colorize_func", (void*)s->colorize_func);
    eb_printf(b1, "%*s: %lld\n", w, "default_style", (long long)s->default_style);
    eb_printf(b1, "%*s: %s\n", w, "buffer", s->b->name);
    if (s->last_buffer)
        eb_printf(b1, "%*s: %s\n", w, "last_buffer", s->last_buffer->name);
    eb_printf(b1, "%*s: %s\n", w, "mode", s->mode->name);
    eb_printf(b1, "%*s: %d\n", w, "colorize_nb_lines", s->colorize_nb_lines);
    eb_printf(b1, "%*s: %d\n", w, "colorize_nb_valid_lines", s->colorize_nb_valid_lines);
    eb_printf(b1, "%*s: %d\n", w, "colorize_max_valid_offset", s->colorize_max_valid_offset);
    if (s->colorize_nb_lines) {
        int pos = eb_printf(b1, "%*s: {", w, "colorize_states");
        int i;
        for (i = 0; i < s->colorize_nb_lines; i++) {
            if (s->colorize_states[i]) {
                if (pos > 60)
                    pos = eb_printf(b1, "\n%*s   ", w, "");
                pos += eb_printf(b1, " %d: %x,", i, s->colorize_states[i]);
            }
        }
        eb_printf(b1, "\n");
    }
    eb_printf(b1, "%*s: %d\n", w, "busy", s->busy);
    eb_printf(b1, "%*s: %d\n", w, "display_invalid", s->display_invalid);
    eb_printf(b1, "%*s: %d\n", w, "borders_invalid", s->borders_invalid);
    eb_printf(b1, "%*s: %d\n", w, "show_selection", s->show_selection);
    eb_printf(b1, "%*s: %d\n", w, "region_style", s->region_style);
    eb_printf(b1, "%*s: %d\n", w, "curline_style", s->curline_style);
    eb_putc(b1, '\n');

    show_popup(s, b1, "Window Description");
}

static void do_describe_screen(EditState *e, int argval)
{
    QEditScreen *s = e->screen;
    EditBuffer *b1;
    int w;

    b1 = new_help_buffer();
    if (!b1)
        return;

    eb_putc(b1, '\n');

    w = 16;
    eb_printf(b1, "%*s: %s\n", w, "dpy.name", s->dpy.name);
    eb_printf(b1, "%*s: %d, %d\n", w, "width, height", s->width, s->height);
    eb_printf(b1, "%*s: %s\n", w, "charset", s->charset->name);
    if (s->unicode_version) {
        eb_printf(b1, "%*s: %d.%d.0\n", w, "Unicode version",
                  s->unicode_version / 10, s->unicode_version % 10);
    }
    eb_printf(b1, "%*s: %d\n", w, "media", s->media);
    eb_printf(b1, "%*s: %d\n", w, "bitmap_format", s->bitmap_format);
    eb_printf(b1, "%*s: %d\n\n", w, "video_format", s->video_format);

    eb_printf(b1, "%*s: %d\n", w, "QE_TERM_STYLE_BITS", QE_TERM_STYLE_BITS);
    eb_printf(b1, "%*s: %x << %d\n", w, "QE_TERM_FG_COLORS", QE_TERM_FG_COLORS, QE_TERM_FG_SHIFT);
    eb_printf(b1, "%*s: %x << %d\n\n", w, "QE_TERM_BG_COLORS", QE_TERM_BG_COLORS, QE_TERM_BG_SHIFT);

    dpy_describe(s, b1);

    show_popup(e, b1, "Screen Description");
}

/*---------------- buffer contents sorting ----------------*/

struct chunk_ctx {
    EditBuffer *b;
    int flags;
    int col;
    int nlines;
    long ncmp;
    long total_cmp;
};

struct chunk {
    int start, end, offset;
    unsigned short c[2];
};

static int eb_skip_to_basename(EditBuffer *b, int pos) {
    int base = pos;
    char32_t c;
    while ((c = eb_nextc(b, pos, &pos)) != '\n') {
        if (c == '/' || c == '\\')
            base = pos;
    }
    return base;
}

static int chunk_cmp(void *vp0, const void *vp1, const void *vp2) {
    struct chunk_ctx *cp = vp0;
    const struct chunk *p1 = vp1;
    const struct chunk *p2 = vp2;
    int pos1, pos2;

    if ((++cp->ncmp & 8191) == 8191) {
        QEmacsState *qs = &qe_state;
        put_status(NULL, "Sorting: %d%%", (int)((cp->ncmp * 90LL) / cp->total_cmp));
        dpy_flush(qs->screen);
    }
    if (cp->flags & SF_REVERSE) {
        p1 = vp2;
        p2 = vp1;
    }

    if (p1->c[0] != p2->c[0]) {
        return p1->c[0] < p2->c[0] ? -1 : 1;
    }
    if (p1->c[1] != p2->c[1]) {
        return p1->c[1] < p2->c[1] ? -1 : 1;
    }
    pos1 = p1->start + p1->offset;
    pos2 = p2->start + p2->offset;
    for (;;) {
        // XXX: should compute offset to first significant character in the setup phase
        char32_t c1 = 0, c2 = 0;
        while (pos1 < p1->end) {
            c1 = eb_nextc(cp->b, pos1, &pos1);
            /* XXX: incorrect for non ASCII contents */
            if (!(cp->flags & SF_DICT) || qe_iswalpha(c1))
                break;
            c1 = 0;
        }
        while (pos2 < p2->end) {
            c2 = eb_nextc(cp->b, pos2, &pos2);
            /* XXX: incorrect for non ASCII contents */
            if (!(cp->flags & SF_DICT) || qe_iswalpha(c2))
                break;
            c2 = 0;
        }
        // XXX: number conversion should not occur after decimal point
        if ((cp->flags & SF_NUMBER) && qe_isdigit(c1) && qe_isdigit(c2)) {
            unsigned long long n1 = c1 - '0';
            unsigned long long n2 = c2 - '0';
            c1 = 0;
            while (pos1 < p1->end) {
                c1 = eb_nextc(cp->b, pos1, &pos1);
                if (!qe_isdigit(c1))
                    break;
                n1 = n1 * 10 + c1 - '0';
                c1 = 0;
            }
            c2 = 0;
            while (pos2 < p2->end) {
                c2 = eb_nextc(cp->b, pos2, &pos2);
                if (!qe_isdigit(c2))
                    break;
                n2 = n2 * 10 + c2 - '0';
                c2 = 0;
            }
            if (n1 < n2)
                return -1;
            if (n1 > n2)
                return +1;
        }
        if (cp->flags & SF_FOLD) {
            // XXX: should also ignore accents
            c1 = qe_wtoupper(c1);
            c2 = qe_wtoupper(c2);
        }
        if (c1 < c2)
            return -1;
        if (c1 > c2)
            return +1;
        if (c1 == 0)
            break;
    }
    /* make sort stable by comparing offsets of equal elements */
    return (p1->start > p2->start) - (p1->start < p2->start);
}

static int eb_sort_span(EditBuffer *b, int *pp1, int *pp2, int cur_offset, int flags) {
    struct chunk_ctx ctx;
    EditBuffer *b1;
    int p1 = *pp1, p2 = *pp2;
    int i, j, offset, line1, line2, col1, col2, line, col, lines;
    char32_t c;
    struct chunk *chunk_array;

    if (p1 > p2) {
        int tmp = p1;
        p1 = p2;
        p2 = tmp;
    }
    ctx.b = b;
    ctx.flags = flags;
    ctx.col = 0;
    eb_get_pos(b, &line1, &col1, p1); /* line1 is included */
    eb_get_pos(b, &line2, &col2, p2); /* line2 is excluded */
    if (col1 > 0) {
        p1 = eb_goto_bol(b, p1);
    }
    if (col2 > 0) { /* include incomplete end line */
        line2++;
        p2 = eb_next_line(b, p2);
    }
    /* XXX: should also support rectangular selection */
    // columns of the mark and point should determine the column range
    if (flags & SF_COLUMN) {
        eb_get_pos(b, &line, &col, cur_offset);
        ctx.col = col ? col : col1;
    }
    lines = line2 - line1;
    if (lines <= 1) {
        *pp1 = p2;
        *pp2 = p2;
        goto done;
    }
    chunk_array = qe_malloc_array(struct chunk, lines);
    if (!chunk_array) {
        return -1;
    }
    offset = p1;
    for (i = 0; i < lines && offset < p2; i++) {
        int pos, pos1;
        pos = offset;
        if (flags & SF_COLUMN) {
            for (col = ctx.col; col-- > 0;) {
                c = eb_nextc(b, pos, &pos1);
                if (c == '\n')
                    break;
                pos = pos1;
            }
        }
        if (flags & SF_BASENAME) {
            pos = eb_skip_to_basename(b, pos);
        }
        /* read first 2 significant characters into cp->c,
           skipping according to SF_DICT.
         */
        for (j = 0; j < countof(chunk_array[i].c); j++) {
            for (;;) {
                c = eb_nextc(b, pos, &pos1);
                if (c == '\n') {
                    c = 0;
                    break;
                }
                if ((flags & SF_DICT) && !qe_iswalpha(c)) {
                    pos = pos1;
                    continue;
                }
                if ((flags & SF_NUMBER) && qe_isdigit(c)) {
                    c = '0';
                    break;
                }
                if (flags & SF_FOLD) {
                    c = qe_wtoupper(c);
                }
                if (c > 0xFFFF)
                    c = 0xFFFF;
                else
                    pos = pos1;
                break;
            }
            chunk_array[i].c[j] = c;
        }
        chunk_array[i].start = offset;
        chunk_array[i].offset = pos - offset;
        chunk_array[i].end = offset = eb_goto_eol(b, pos);
        offset = eb_next(b, offset);
        if (flags & SF_PARAGRAPH) {
            int offset1;
            /* paragraph sorting: skip continuation lines */
            // XXX: Should ignore initial indent
            while (offset < p2 && qe_isspace(eb_nextc(b, offset, &offset1))) {
                chunk_array[i].end = offset = eb_goto_eol(b, offset);
                offset = eb_next(b, offset);
            }
        }
    }
    /* for progress meter: n.log n comparisons + n insertions */
    ctx.nlines = lines = i;
    ctx.ncmp = 0;
    /* evaluate the total number of comparisons: n.log2(n) */
    for (ctx.total_cmp = 0; i > 0; i >>= 1) {
        ctx.total_cmp += lines;
    }
    qe_qsort_r(chunk_array, lines, sizeof(*chunk_array), &ctx, chunk_cmp);

    b1 = eb_new("*sorted*", BF_SYSTEM | (b->flags & BF_STYLES));
    eb_set_charset(b1, b->charset, b->eol_type);

    for (i = 0; i < lines; i++) {
        /* XXX: should keep track of point if sorting full buffer */
        eb_insert_buffer_convert(b1, b1->total_size, b, chunk_array[i].start,
                                 chunk_array[i].end - chunk_array[i].start);
        // XXX: style issue. Should include newline from source buffer
        eb_putc(b1, '\n');
        if ((i & 8191) == 8191 && !(flags & SF_SILENT)) {
            QEmacsState *qs = &qe_state;
            put_status(NULL, "Sorting: %d%%", (int)(90 + i * 10LL / lines));
            dpy_flush(qs->screen);
        }
    }
    eb_delete_range(b, p1, p2);
    *pp1 = p1;
    *pp2 = p1 + eb_insert_buffer_convert(b, p1, b1, 0, b1->total_size);
    eb_free(&b1);
    qe_free(&chunk_array);
done:
    if (!(flags & SF_SILENT))
        put_status(NULL, "%d lines sorted", lines);
    return 0;
}

static void do_sort_span(EditState *s, int p1, int p2, int argval, int flags) {
    s->region_style = 0;
    if (eb_sort_span(s->b, &p1, &p2, s->offset, flags | argval) < 0) {
        put_status(s, "Out of memory");
        return;
    }
    s->b->mark = p1;
    s->offset = p2;
}

static void do_sort_region(EditState *s, int argval, int flags) {
    do_sort_span(s, s->b->mark, s->offset, argval, flags);
}

static void do_sort_buffer(EditState *s, int argval, int flags) {
    do_sort_span(s, 0, s->b->total_size, argval, flags);
}

/*---------------- tag handling ----------------*/

static void tag_buffer(EditState *s) {
    char32_t buf[COLORED_MAX_LINE_SIZE];
    QETermStyle sbuf[COLORED_MAX_LINE_SIZE];
    int offset, line_num, col_num;

    if (s->colorize_func || s->b->b_styles) {
        /* force complete buffer colorization */
        eb_get_pos(s->b, &line_num, &col_num, s->b->total_size);
        get_colorized_line(s, buf, countof(buf), sbuf,
                           s->b->total_size, &offset, line_num);
    }
}

static void tag_complete(CompleteState *cp, CompleteFunc enumerate) {
    /* XXX: only support current buffer */
    QEProperty *p;

    if (cp->target) {
        tag_buffer(cp->target);

        for (p = cp->target->b->property_list; p; p = p->next) {
            if (p->type == QE_PROP_TAG) {
                enumerate(cp, p->data, CT_GLOB);
            }
        }
    }
}

static int tag_print_entry(CompleteState *cp, EditState *s, const char *name) {
    if (cp->target) {
        EditBuffer *b = cp->target->b;
        QEProperty *p;
        if (!s->colorize_func && cp->target->colorize_func) {
            set_colorize_func(s, cp->target->colorize_func, cp->target->colorize_mode);
        }
        for (p = b->property_list; p; p = p->next) {
            if (p->type == QE_PROP_TAG && strequal(p->data, name)) {
                int offset = eb_goto_bol(b, p->offset);
                int offset1 = eb_goto_eol(b, p->offset);
                return eb_insert_buffer_convert(s->b, s->b->total_size,
                                                b, offset, offset1 - offset);
            }
        }
    }
    return eb_puts(s->b, name);
}

static int tag_get_entry(EditState *s, char *dest, int size, int offset)
{
    int len = eb_fgets(s->b, dest, size, offset, &offset);
    int p2 = strcspn(dest, "=[{(,;");
    int p1;
    while (p2 > 0 && !qe_isalnum_(dest[p2 - 1]))
        p2--;
    p1 = p2;
    while (p1 > 0 && (qe_isalnum_(dest[p1 - 1]) || dest[p1 - 1] == '-'))
        p1--;
    memmove(dest, dest + p1, len = p2 - p1);
    dest[len] = '\0';   /* strip the prototype or trailing newline if any */
    return len;
}

static void do_find_tag(EditState *s, const char *str) {
    QEProperty *p;

    tag_buffer(s);

    for (p = s->b->property_list; p; p = p->next) {
        if (p->type == QE_PROP_TAG && strequal(p->data, str)) {
            s->offset = p->offset;
            return;
        }
    }
    put_status(s, "Tag not found: %s", str);
}

static void do_goto_tag(EditState *s) {
    char buf[80];
    size_t len;

    len = qe_get_word(s, buf, sizeof buf, s->offset, NULL);
    if (len >= sizeof(buf)) {
        put_status(s, "Tag too large");
        return;
    } else
    if (len == 0) {
        put_status(s, "No tag");
        return;
    } else {
        do_find_tag(s, buf);
    }
}

/* XXX: should have next-tag and previous-tag */

static void do_list_tags(EditState *s, int argval) {
    char buf[256];
    EditBuffer *b;
    QEProperty *p;
    EditState *e1;

    b = new_help_buffer();
    if (!b)
        return;

    tag_buffer(s);

    snprintf(buf, sizeof buf, "Tags in file %.*s", 242, s->b->filename);
    for (p = s->b->property_list; p; p = p->next) {
        if (p->type == QE_PROP_TAG) {
            //eb_printf(b, "%12d  %s\n", p->offset, (char*)p->data);
            int offset = eb_goto_bol(s->b, p->offset);
            int offset1 = eb_goto_eol(s->b, p->offset);
            eb_insert_buffer_convert(b, b->offset, s->b, offset, offset1 - offset);
            eb_putc(b, '\n');
        }
    }

    e1 = show_popup(s, b, buf);
    if (s->colorize_func) {
        set_colorize_func(e1, s->colorize_func, s->colorize_mode);
    }
}

static CompletionDef tag_completion = {
    "tag", tag_complete, tag_print_entry, tag_get_entry
};

/*---------------- Unicode character name completion ----------------*/

static void charname_complete(CompleteState *cp, CompleteFunc enumerate) {
    char buf[256];
    char entry[264]; // silence compiler warning on snprintf calls below
    FILE *fp;

    /* enumerate Unicode character names from Unicode consortium data */
    if ((fp = open_resource_file("DerivedName-15.0.0.txt")) != NULL
    ||  (fp = open_resource_file("DerivedName.txt")) != NULL
    ||  (fp = open_resource_file("extracted/DerivedName.txt")) != NULL) {
        while (fgets(buf, sizeof buf, fp)) {
            char *p1, *p2, *p3;
            if ((p1 = strchr(buf, ';')) != NULL
            &&  p1[1] == ' '
            &&  !strchr(p1 + 2, '*')
            &&  (p2 = strchr(p1 + 2, '\n')) != NULL) {
                *p1 = '\0';
                p1 += 2;
                *p2 = '\0';
                p3 = strchr(buf, ' ');
                if (p3)
                    *p3 = '\0';
                snprintf(entry, sizeof entry, "%s\t%5.6s", p1, buf);
                enumerate(cp, entry, CT_IGLOB);
            }
        }
        fclose(fp);
    } else
    if ((fp = open_resource_file("UnicodeData-15.0.0.txt")) != NULL
    ||  (fp = open_resource_file("UnicodeData.txt")) != NULL) {
        while (fgets(buf, sizeof buf, fp)) {
            char *p1, *p2;
            if ((p1 = strchr(buf, ';')) != NULL
            &&  p1[1] != ';'
            &&  p1[1] != '<'
            &&  (p2 = strchr(p1 + 1, ';')) != NULL) {
                *p1++ = '\0';
                *p2 = '\0';
                snprintf(entry, sizeof entry, "%s\t%5.6s", p1, buf);
                enumerate(cp, entry, CT_IGLOB);
            }
        }
        fclose(fp);
    } else {
        put_status(cp->s, "cannot find DerivedName.txt or UnicodeData.txt in QEPATH");
    }
}

static long charname_convert_entry(const char *str, const char **endp) {
    char buf[256];
    FILE *fp;
    long code = strtol_c(str, endp, 0);
    if (**endp == '\0')
        return code;

    /* enumerate Unicode character names from Unicode consortium data */
    if ((fp = open_resource_file("DerivedName-15.0.0.txt")) != NULL
    ||  (fp = open_resource_file("DerivedName.txt")) != NULL
    ||  (fp = open_resource_file("extracted/DerivedName.txt")) != NULL) {
        while (fgets(buf, sizeof buf, fp)) {
            char *p1, *p2;
            if ((p1 = strchr(buf, ';')) != NULL
            &&  p1[1] == ' '
            &&  !strchr(p1 + 2, '*')
            &&  (p2 = strchr(p1 + 2, '\n')) != NULL) {
                *p2 = '\0';
                if (utf8_strimatch_pat(p1 + 2, str, 0)) {
                    *endp = strchr(str, '\0');
                    return strtol(buf, NULL, 16);
                }
            }
        }
        fclose(fp);
    } else
    if ((fp = open_resource_file("UnicodeData-15.0.0.txt")) != NULL
    ||  (fp = open_resource_file("UnicodeData.txt")) != NULL) {
        while (fgets(buf, sizeof buf, fp)) {
            char *p1, *p2;
            if ((p1 = strchr(buf, ';')) != NULL
            &&  p1[1] != ';'
            &&  p1[1] != '<'
            &&  (p2 = strchr(p1 + 1, ';')) != NULL) {
                *p2 = '\0';
                if (utf8_strimatch_pat(p1 + 1, str, 0)) {
                    *endp = strchr(str, '\0');
                    return strtol(buf, NULL, 16);
                }
            }
        }
        fclose(fp);
    }
    *endp = str;
    return 0;
}

static int charname_print_entry(CompleteState *cp, EditState *s, const char *name) {
    char *p = strchr(name, '\t');
    if (p != NULL) {
        char cbuf[MAX_CHAR_BYTES + 1];
        char32_t code = strtol(p + 1, NULL, 16);
        s->b->tab_width = max_int(s->b->tab_width, min_int(60, 2 + (p - name)));
        return eb_printf(s->b, "%s  %s", name,
                         utf8_char32_to_string(cbuf, code));
    } else {
        return eb_puts(s->b, name);
    }
}

static int charname_get_entry(EditState *s, char *dest, int size, int offset) {
    char entry[256];
    char *p;
    int len;

    eb_fgets(s->b, entry, sizeof entry, offset, &offset);
    p = strchr(entry, '\t');
    if (p) {
        p += strspn(p, " \t");
        len = strcspn(p, " \t\n");
        return snprintf(dest, size, "0x%.*s", len, p);
    } else {
        if (size > 0)
            *dest = '\0';
        return 0;
    }
}

static CompletionDef charname_completion = {
    "charname", charname_complete, charname_print_entry, charname_get_entry,
    charname_convert_entry
};

/*---------------- paragraph handling ----------------*/

int eb_next_paragraph(EditBuffer *b, int offset) {
    /* find end of paragraph around or after point:
       skip blank lines, if any, then skip non blank lines
       and return start of blank line before text.
     */
    int text_found = 0;
    offset = eb_goto_bol(b, offset);
    while (offset < b->total_size) {
        if (eb_is_blank_line(b, offset, NULL)) {
            if (text_found)
                break;
        } else {
            text_found = 1;
        }
        offset = eb_next_line(b, offset);
    }
    return offset;
}

void do_mark_paragraph(EditState *s, int n) {
    /* mark-paragraph (bound to M-h):

       mark_paragraph(n = arg_val)

       Put point at beginning of this paragraph, mark at end.
       The paragraph marked is the one that contains point or follows point.

       With argument ARG, puts mark at end of a following paragraph, so that
       the number of paragraphs marked equals ARG.

       If ARG is negative, point is put at end of this paragraph, mark is put
       at beginning of this or a previous paragraph.
       The paragraph marked is the one that contains point or precedes point.

       Interactively if the current region is highlighted, it marks
       the next ARG paragraphs after the ones already marked.
     */
    int start = s->offset;
    int end = s->region_style ? s->b->mark : s->offset;
    if (n < 0) {
        end = eb_prev_paragraph(s->b, end);
        if (!s->region_style)
            start = eb_next_paragraph(s->b, end);
        while (++n < 0 && end > 0) {
            end = eb_prev_paragraph(s->b, end);
        }
    } else
    if (n > 0) {
        end = eb_next_paragraph(s->b, end);
        if (!s->region_style)
            start = eb_prev_paragraph(s->b, end);
        while (--n > 0 && end < s->b->total_size) {
            end = eb_next_paragraph(s->b, end);
        }
    }
    do_mark_region(s, end, start);
}

int eb_prev_paragraph(EditBuffer *b, int offset) {
    /* find start of paragraph around or before point:
       skip blank lines, if any, then skip non blank lines
       and return start of blank line after end of text.
     */
    int text_found = 0;
    offset = eb_goto_bol(b, offset);
    while (offset > 0) {
        if (eb_is_blank_line(b, offset, NULL)) {
            if (text_found)
                break;
        } else {
            text_found = 1;
        }
        offset = eb_prev_line(b, offset);
    }
    return offset;
}

void do_forward_paragraph(EditState *s, int n) {
    for (; n < 0 && s->offset > 0; n++) {
        s->offset = eb_prev_paragraph(s->b, s->offset);
    }
    for (; n > 0 && s->offset < s->b->total_size; n--) {
        s->offset = eb_next_paragraph(s->b, s->offset);
    }
}

void do_kill_paragraph(EditState *s, int n) {
    /*
       kill_paragraph(n = ARG)

       Kill forward to end of paragraph.
       With arg N, kill forward to Nth end of paragraph;
       negative arg -N means kill backward to Nth start of paragraph.
     */
    if (n != 0) {
        int start = s->offset;
        do_forward_paragraph(s, n);
        do_kill(s, start, s->offset, n, 0);
    }
}

/* replace the contents between p1 and p2 with a specified number
   of newlines and spaces */
static int eb_respace(EditBuffer *b, int p1, int p2, int newlines, int spaces) {
    int adjust = 0, nb, offset1;
    char32_t c;
    while (newlines > 0 && p1 < p2) {
        c = eb_nextc(b, p1, &offset1);
        if (c != '\n')
            break;
        p1 = offset1;
        newlines--;
    }
    if (newlines > 0) {
        nb = eb_insert_char32_n(b, p1, '\n', newlines);
        adjust += nb;
        p1 += nb;
        p2 += nb;
    }
    while (spaces > 0 && p1 < p2) {
        c = eb_nextc(b, p1, &offset1);
        if (c == ' ') {
            p1 = offset1;
            spaces--;
        } else {
            nb = eb_delete(b, p1, offset1 - p1);
            adjust -= nb;
            p2 -= nb;
        }
    }
    if (p1 < p2) {
        nb = eb_delete(b, p1, p2 - p1);
        adjust -= nb;
        p2 -= nb;
    }
    if (spaces) {
        nb = eb_insert_spaces(b, p1, spaces);
        adjust += nb;
        p1 += nb;
        p2 += nb;
    }
    return adjust;
}

static int get_indent_size(EditState *s, int p1, int p2) {
    int indent_size = 0;
    while (p1 < p2) {
        char32_t c = eb_nextc(s->b, p1, &p1);
        if (!qe_isblank(c))
            break;
        if (c == '\t') {
            int tw = s->b->tab_width > 0 ? s->b->tab_width : DEFAULT_TAB_WIDTH;
            indent_size += tw - indent_size % tw;
        } else {
            indent_size++;
        }
    }
    return indent_size;
}

void do_fill_paragraph(EditState *s)
{
    /* buffer offsets, byte counts */
    int par_start, par_end, offset, offset1, chunk_start, word_start;
    /* number of characters / screen positions */
    int col, indent0_size, indent_size, word_size;

    /* find start & end of paragraph */
    par_end = eb_next_paragraph(s->b, s->offset);
    par_start = eb_prev_paragraph(s->b, par_end);
    /* skip the blank line if any */
    eb_is_blank_line(s->b, par_start, &par_start);

    /* compute indent sizes for first and second lines */
    indent0_size = get_indent_size(s, par_start, par_end);
    offset = eb_next_line(s->b, par_start);
    indent_size = get_indent_size(s, offset, par_end);

    /* reflow words to fill lines */
    col = 0;
    offset = par_start;
    while (offset < par_end) {
        /* skip spaces */
        chunk_start = offset;
        while (offset < par_end) {
            char32_t c = eb_nextc(s->b, offset, &offset1);
            if (!qe_isspace(c))
                break;
            offset = offset1;
        }
        /* skip word */
        word_start = offset;
        word_size = 0;
        while (offset < par_end) {
            char32_t c = eb_nextc(s->b, offset, &offset1);
            if (qe_isspace(c))
                break;
            offset = offset1;
            /* handle variable width and combining glyphs */
            word_size += qe_wcwidth(c);
        }

        if (chunk_start == par_start) {
            /* preserve spaces at paragraph start */
            col += indent0_size + word_size;
        } else {
            if (word_start == offset) {
                /* space at end of paragraph: append a newline */
                eb_respace(s->b, chunk_start, word_start, 1, 0);
                break;
            }
            if (col + 1 + word_size > s->b->fill_column) {
                /* insert newline and indentation */
                int nb = eb_respace(s->b, chunk_start, word_start, 1, indent_size);
                offset += nb;
                par_end += nb;
                col = indent_size + word_size;
            } else {
                /* single space the word */
                int nb = eb_respace(s->b, chunk_start, word_start, 0, 1);
                offset += nb;
                par_end += nb;
                col += 1 + word_size;
            }
        }
    }
}

/*---------------- command and binding definitions ----------------*/

static const CmdDef extra_commands[] = {

    CMD2( "compare-windows", "M-=",
          "Compare windows, optionally ignoring white space, comments and case",
          do_compare_windows, ESi, "p")
    CMD3( "compare-files", "C-x RET C-l",
          "Compare file and other version in parent directory",
          do_compare_files, ESsi,
          "s{Compare file: }[file]|file|"
          "v", 0) /* p? */
    // XXX: delete-leading-space (mg) Delete any leading whitespace on the current line
    // XXX: delete-trailing-space (mg) Delete any trailing whitespace on the current line
    // XXX: delete-trailing-whitespace (emacs) Delete all the trailing whitespace across the current buffer.
    CMD2( "delete-horizontal-space", "M-\\",
          "Delete blanks around point",
          do_delete_horizontal_space, ES, "*")
    CMD2( "delete-blank-lines", "C-x C-o",
          "Delete blank lines on and/or around point",
          do_delete_blank_lines, ES, "*")
    CMD2( "tabify-region", "",
          "Convert multiple spaces in region to tabs when possible",
          do_tabify, ESii, "*" "md")
    CMD2( "untabify-region", "",
          "Convert all tabs in region to spaces, preserving columns",
          do_untabify, ESii, "*" "md")
    CMD2( "tabify-buffer", "",
          "Convert multiple spaces in buffer to tabs when possible",
          do_tabify, ESii, "*" "ze")
    CMD2( "untabify-buffer", "",
          "Convert all tabs in buffer to multiple spaces, preserving columns",
          do_untabify, ESii, "*" "ze")

    CMD2( "indent-region", "M-C-\\",
          "Indent each nonblank line in the region",
          do_indent_region, ESii, "*" "md")
    CMD2( "indent-buffer", "",
          "Indent each nonblank line in the buffer",
          do_indent_region, ESii, "*" "ze")

    CMD2( "show-date-and-time", "C-x t",
          "Show current date and time",
          do_show_date_and_time, ESi, "P")

    CMD2( "backward-block", "M-C-b, ESC C-left",
          "Move backwards past parenthesized expression, ignoring comments and strings",
          do_forward_block, ESi, "q")
    CMD2( "forward-block", "M-C-f, ESC C-right",
          "Move past parenthesized expression, ignoring comments and strings",
          do_forward_block, ESi, "p")
    CMD2( "backward-kill-block", "ESC delete",
          "Kill from point to the beginning of the previous block",
          do_kill_block, ESi, "q")
    CMD2( "kill-block", "M-C-k",
          "Kill from point to the end of the next block",
          do_kill_block, ESi, "p")
          /* Should also have mark-block on C-M-@ */

    CMD3( "transpose-chars", "C-t",
          "Swap character at point and before it",
          do_transpose, ESi, "*" "v", CMD_TRANSPOSE_CHARS)
    CMD3( "transpose-lines", "C-x C-t",
          "Swap line at point and previous line",
          do_transpose, ESi, "*" "v", CMD_TRANSPOSE_LINES)
    CMD3( "transpose-words", "M-t",
          "Swap words before and after point",
          do_transpose, ESi, "*" "v", CMD_TRANSPOSE_WORDS)

    CMD2( "show-bindings", "C-h B, f5",
          "Show current bindings for a given command",
          do_show_bindings, ESs,
          "s{Show bindings of command: }[command]|command|")
    CMD3( "global-unset-key", "",
          "Remove global key binding",
          do_unset_key, ESsi,
          "s{Unset key globally: }[key]"
          "v", 0)
    CMD3( "local-unset-key", "",
          "Remove local (mode specific) key binding",
          do_unset_key, ESsi,
          "s{Unset key locally: }[key]"
          "v", 1)

    // XXX: should have `qemacs-hello` on `C-h h` testing charsets
    // XXX: should have `qemacs-manual` on `C-h m`
    // XXX: should have `qemacs-faq` on `C-h C-f`
    //      use do_load_file_from_path() to load the above
    CMD0( "about-qemacs", "C-h ?, f1",
          "Display information about Quick Emacs",
          do_about_qemacs)
    CMD2( "apropos", "C-h a, C-h C-a",
          "List commands and variables matching a topic",
          do_apropos, ESs,
          "s{Apropos: }[symbol]|apropos|")
    CMD2( "describe-bindings", "C-h b",
          "List local and global key bindings",
          do_describe_bindings, ESi, "p")
    CMD2( "describe-buffer", "C-h C-b",
          "Show information about the current buffer",
          do_describe_buffer, ESi, "p")
    CMD2( "describe-function", "C-h f",
          "Show information and bindings for a command",
          do_describe_function, ESs,
          "s{Describe function: }[command]|command|")
    CMD2( "describe-screen", "C-h s, C-h C-s",
          "Show information about the current screen",
          do_describe_screen, ESi, "p")
    CMD2( "describe-window", "C-h w, C-h C-w",
          "Show information about the current window",
          do_describe_window, ESi, "p")

    /* XXX: should take region as argument, implicit from keyboard */
    CMD2( "set-region-color", "C-c c",
          "Set the color for the current region",
          do_set_region_color, ESs,
          "s{Select color: }[color]|color|")
    CMD2( "set-region-style", "C-c s",
          "Set the style for the current region",
          do_set_region_style, ESs,
          "s{Select style: }[style]|style|")
    CMD0( "drop-styles", "",
          "Remove all styles for the current buffer",
          do_drop_styles)

    CMD2( "set-eol-type", "",
          "Set the end of line style: [0=Unix, 1=Dos, 2=Mac]",
          do_set_eol_type, ESi,
          "N{EOL Type [0=Unix, 1=Dos, 2=Mac]: }")

    // XXX: should take region as argument, implicit from keyboard
    // XXX: should have sort-fields and sort-numeric-fields
    CMD3( "sort-buffer", "",
          "Sort the buffer contents according to sorting options",
          do_sort_buffer, ESii, "*" "p" "v", 0)
    CMD3( "sort-columns", "",
          "Sort the lines in the region from the current column",
          do_sort_region, ESii, "*" "p" "v", SF_COLUMN)
    CMD3( "sort-lines", "",
          "Sort the lines in the region according to sorting options",
          do_sort_region, ESii, "*" "p" "v", 0)
    CMD3( "sort-region", "",
          "Sort the lines in the region according to sorting options",
          do_sort_region, ESii, "*" "p" "v", 0)
    CMD3( "sort-numbers", "",
          "Sort the lines in the region as numbers",
          do_sort_region, ESii, "*" "p" "v", SF_NUMBER)
    CMD3( "sort-paragraphs", "",
          "Sort the paragraphs in the region as numbers",
          do_sort_region, ESii, "*" "p" "v", SF_PARAGRAPH)

    CMD2( "list-tags", "",
          "List the buffer tags detected automatically",
          do_list_tags, ESi, "p")
    CMD0( "goto-tag", "C-x ,, M-f1",
          "Move point to the tag for the word at point",
          do_goto_tag)
    CMD2( "find-tag", "C-x .",
          "Move point to a given tag",
          do_find_tag, ESs,
          "s{Find tag: }[tag]|tag|")

    /*---------------- Paragraph handling ----------------*/

    CMD2( "mark-paragraph", "M-h",
          "Mark the paragraph at or after point",
          do_mark_paragraph, ESi, "p")
    CMD2( "backward-paragraph", "M-{, C-up",
          "Move point to the beginning of the paragraph at or before point",
          do_forward_paragraph, ESi, "q")
    CMD2( "forward-paragraph", "M-}, C-down",
          "Move point to the end of the paragraph at or before point",
          do_forward_paragraph, ESi, "p")
    CMD2( "fill-paragraph", "M-q",
          "Fill the current paragraph, preserving indentation of the first 2 lines",
          do_fill_paragraph, ES, "*")
    /* should have fill-region, fill-buffer */
    CMD2( "kill-paragraph", "",
          "Kill the paragraph at or after point",
          do_kill_paragraph, ESi, "p")
};

static int extras_init(void) {
    qe_register_commands(NULL, extra_commands, countof(extra_commands));
    qe_register_completion(&tag_completion);
    qe_register_completion(&charname_completion);

    return 0;
}

qe_module_init(extras_init);
