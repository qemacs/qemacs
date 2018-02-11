/*
 * QEmacs, extra commands non full version
 *
 * Copyright (c) 2000-2017 Charlie Gordon.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <time.h>

#include "qe.h"
#include "qfribidi.h"
#include "variables.h"

static int qe_skip_comments(EditState *s, int offset, int *offsetp)
{
    unsigned int buf[COLORED_MAX_LINE_SIZE];
    QETermStyle sbuf[COLORED_MAX_LINE_SIZE];
    int line_num, col_num, len, pos;
    int offset0, offset1;

    if (!s->colorize_func && !s->b->b_styles)
        return 0;

    eb_get_pos(s->b, &line_num, &col_num, offset);
    offset0 = eb_goto_bol2(s->b, offset, &pos);
    /* XXX: should only query the syntax colorizer */
    len = s->get_colorized_line(s, buf, countof(buf), sbuf,
                                offset0, &offset1, line_num);
    if (len > countof(buf))
        len = countof(buf);
    if (pos >= len)
        return 0;
    if (sbuf[pos] != QE_STYLE_COMMENT)
        return 0;
    while (pos < len && sbuf[pos] == QE_STYLE_COMMENT) {
        offset = eb_next(s->b, offset);
        pos++;
    }
    *offsetp = offset;
    return 1;
}

static int qe_skip_spaces(EditState *s, int offset, int *offsetp)
{
    int offset0 = offset, offset1;

    while (offset < s->b->total_size
        && qe_isspace(eb_nextc(s->b, offset, &offset1))) {
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
    int ch1, ch2;

    off1 = save1;
    off2 = save2;
    /* try skipping blanks */
    while (qe_isblank(ch1 = eb_nextc(s1->b, pos1 = off1, &off1)))
        continue;
    while (qe_isblank(ch2 = eb_nextc(s2->b, pos2 = off2, &off2)))
        continue;
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

void do_compare_windows(EditState *s, int argval)
{
    QEmacsState *qs = s->qe_state;
    EditState *s1;
    EditState *s2;
    int offset1, offset2, size1, size2, ch1, ch2;
    int tries, resync = 0;
    char buf1[MAX_CHAR_BYTES + 2], buf2[MAX_CHAR_BYTES + 2];
    const char *comment = "";

    s1 = s;
    /* Should use same internal function as for next_window */
    if (s1->next_window)
        s2 = s1->next_window;
    else
        s2 = qs->first_window;

    if (argval != NO_ARG) {
        if (argval & 4)
            qs->ignore_spaces ^= 1;
        if (argval & 16)
            qs->ignore_comments ^= 1;
    }
    if (s1 == s2) {
        /* single window: bail out */
        return;
    }

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
            offset1 = s1->offset;
            ch1 = EOF;
        } else {
            ch1 = eb_nextc(s1->b, s1->offset, &offset1);
        }
        if (s2->offset >= size2) {
            offset2 = s2->offset;
            ch2 = EOF;
        } else {
            ch2 = eb_nextc(s2->b, s2->offset, &offset2);
        }
        if (ch1 != ch2) {
            if (qs->ignore_spaces) {
                /* UTF-8 issue */
                if (qe_isspace(ch1) || qe_isspace(ch2)) {
                    qe_skip_spaces(s1, s1->offset, &s1->offset);
                    qe_skip_spaces(s2, s2->offset, &s2->offset);
                    if (!*comment)
                        comment = "Skipped spaces, ";
                    continue;
                }
            }
            if (qs->ignore_comments) {
                if (qe_skip_comments(s1, s1->offset, &s1->offset) |
                    qe_skip_comments(s2, s2->offset, &s2->offset)) {
                    comment = "Skipped comments, ";
                    continue;
                }
            }
            if (ch1 == EOF || ch2 == EOF) {
                put_status(s, "%sExtra characters", comment);
                break;
            }
            if (resync) {
                int save1 = s1->offset, save2 = s2->offset;
                compare_resync(s1, s2, save1, save2, &s1->offset, &s2->offset);
                put_status(s, "Skipped %d and %d bytes",
                           s1->offset - save1, s2->offset - save2);
                break;
            }
            put_status(s, "%sDifference: '%s' [0x%02X] <-> '%s' [0x%02X]", comment,
                       utf8_char_to_string(buf1, ch1), ch1,
                       utf8_char_to_string(buf2, ch2), ch2);
            break;
        }
        if (ch1 != EOF) {
            s1->offset = offset1;
            s2->offset = offset2;
            continue;
        }
        put_status(s, "%sNo difference", comment);
        break;
    }
}

void do_compare_files(EditState *s, const char *filename, int bflags)
{
    char buf[MAX_FILENAME_SIZE];
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
    e = qe_split_window(s, 50, SW_STACKED);
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

static void do_delete_blank_lines(EditState *s)
{
    /* Delete blank lines:
     * On blank line, delete all surrounding blank lines, leaving just one.
     * On isolated blank line, delete that one.
     * On nonblank line, delete any immediately following blank lines.
     */
    /* XXX: should simplify */
    int from, offset, offset0, offset1, all = 0;
    EditBuffer *b = s->b;

    offset = eb_goto_bol(b, s->offset);
    if (eb_is_blank_line(b, offset, &offset1)) {
        if ((offset == 0 || !eb_is_blank_line(b,
                             eb_prev_line(b, offset), NULL))
        &&  (offset1 >= b->total_size || !eb_is_blank_line(b,
                            offset1, NULL))) {
            all = 1;
        }
    } else {
        offset = eb_next_paragraph(b, offset);
        all = 1;
    }

    from = offset;
    while (from > 0) {
        offset0 = eb_prev_line(b, from);
        if (!eb_is_blank_line(b, offset0, NULL))
            break;
        from = offset0;
    }
    if (!all) {
        eb_delete_range(b, from, offset);
        /* Keep current blank line */
        from = offset = eb_next_line(b, from);
    }
    while (offset < s->b->total_size) {
        if (!eb_is_blank_line(b, offset, &offset))
            break;
    }
    eb_delete_range(b, from, offset);
}

static void eb_tabify(EditBuffer *b, int p1, int p2)
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
    int tw = b->tab_width > 0 ? b->tab_width : 8;
    int start = max(0, min(p1, p2));
    int stop = min(b->total_size, max(p1, p2));
    int col;
    int offset, offset1, offset2, delta;

    col = 0;
    offset = eb_goto_bol(b, start);

    for (; offset < stop; offset = offset1) {
        int c = eb_nextc(b, offset, &offset1);
        if (c == '\r' || c == '\n') {
            col = 0;
            continue;
        }
        if (c == '\t') {
            col += tw - col % tw;
            continue;
        }
        col += unicode_tty_glyph_width(c);
        if (c != ' ' || offset < start || col % tw == 0)
            continue;
        while (offset1 < stop) {
            c = eb_nextc(b, offset1, &offset2);
            if (c == ' ') {
                col += unicode_tty_glyph_width(c);
                offset1 = offset2;
                if (col % tw == 0) {
                    delta = eb_delete_range(b, offset, offset1);
                    delta = eb_insert_uchar(b, offset, '\t') - delta;
                    offset1 += delta;
                    stop += delta;
                    break;
                }
                continue;
            } else
            if (c == '\t') {
                col += tw - col % tw;
                delta = -eb_delete_range(b, offset, offset1);
                offset1 = offset2;
                offset1 += delta;
                stop += delta;
            }
            break;
        }
    }
}

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

static void eb_untabify(EditBuffer *b, int p1, int p2)
{
    /* We implement a complete analysis of the region instead of
     * potentially faster scan for '\t'.  It is fast enough and even
     * faster if there are lots of tabs.
     */
    int tw = b->tab_width > 0 ? b->tab_width : 8;
    int start = max(0, min(p1, p2));
    int stop = min(b->total_size, max(p1, p2));
    int col, col0;
    int offset, offset1, offset2, delta;

    col = 0;
    offset = eb_goto_bol(b, start);

    for (; offset < stop; offset = offset1) {
        int c = eb_nextc(b, offset, &offset1);
        if (c == '\r' || c == '\n') {
            col = 0;
            continue;
        }
        if (c != '\t') {
            col += unicode_tty_glyph_width(c);
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
        delta = eb_delete_range(b, offset, offset1);
        delta = eb_insert_spaces(b, offset, col - col0) - delta;
        offset1 += delta;
        stop += delta;
    }
}

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

static void do_indent_region(EditState *s)
{
    int col_num, line1, line2;

    /* deactivate region hilite */
    s->region_style = 0;

    /* Swap point and mark so mark <= point */
    if (s->offset < s->b->mark) {
        int tmp = s->b->mark;
        s->b->mark = s->offset;
        s->offset = tmp;
    }
    /* We do it with lines to avoid offset variations during indenting */
    eb_get_pos(s->b, &line1, &col_num, s->b->mark);
    eb_get_pos(s->b, &line2, &col_num, s->offset);

    if (col_num == 0)
        line2--;

    /* Iterate over all lines inside block */
    for (; line1 <= line2; line1++) {
        if (s->mode->indent_func) {
            (s->mode->indent_func)(s, eb_goto_pos(s->b, line1, 0));
        } else {
            s->offset = eb_goto_pos(s->b, line1, 0);
            do_tab(s, 1);
        }
    }
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
static int matching_delimiter(int c) {
    const char *pairs = "(){}[]<>";
    int i;

    for (i = 0; pairs[i]; i++) {
        if (pairs[i] == c)
            return pairs[i ^ 1];
    }
    return c;
}

static void do_forward_block(EditState *s, int dir)
{
    unsigned int buf[COLORED_MAX_LINE_SIZE];
    QETermStyle sbuf[COLORED_MAX_LINE_SIZE];
    char balance[MAX_LEVEL];
    int use_colors;
    int line_num, col_num, style, style0, c, level;
    int pos;      /* position of the current character on line */
    int len;      /* number of colorized positions */
    int offset;   /* offset of the current character */
    int offset0;  /* offset of the beginning of line */
    int offset1;  /* offset of the beginning of the next line */

    offset = s->offset;
    eb_get_pos(s->b, &line_num, &col_num, offset);
    offset1 = offset0 = eb_goto_bol2(s->b, offset, &pos);
    use_colors = s->colorize_func || s->b->b_styles;
    style0 = 0;
    len = 0;
    if (use_colors) {
        /* XXX: should only query the syntax colorizer */
        len = s->get_colorized_line(s, buf, countof(buf), sbuf,
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
                    /* XXX: should only query the syntax colorizer */
                    len = s->get_colorized_line(s, buf, countof(buf), sbuf,
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
                    int c1, off;
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
                    /* XXX: should only query the syntax colorizer */
                    len = s->get_colorized_line(s, buf, countof(buf), sbuf,
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
                    int c1, off;
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

static void do_kill_block(EditState *s, int dir)
{
    int start = s->offset;

    do_forward_block(s, dir);
    do_kill(s, start, s->offset, dir, 0);
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
        word_right(s, 0);
        offset3 = s->offset;
        word_left(s, 0);
        offset2 = s->offset;
        word_left(s, 1);
        offset1 = s->offset;
        word_left(s, 0);
        offset0 = s->offset;
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

/* remove a key binding from mode or globally */
static int qe_unregister_binding1(unsigned int *keys, int nb_keys, ModeDef *m)
{
    QEmacsState *qs = &qe_state;
    KeyDef **lp, *p;

    lp = m ? &m->first_key : &qs->first_key;
    while (*lp) {
        if ((*lp)->nb_keys == nb_keys
        &&  !memcmp((*lp)->keys, keys, nb_keys * sizeof(*keys)))
        {
            p = *lp;
            *lp = (*lp)->next;
            qe_free(&p);
            return 1;
        }
        lp = &(*lp)->next;
    }
    return 0;
}

static void do_unset_key(EditState *s, const char *keystr, int local)
{
    unsigned int keys[MAX_KEYS];
    int nb_keys;

    nb_keys = strtokeys(keystr, keys, MAX_KEYS);
    if (!nb_keys)
        return;

    qe_unregister_binding1(keys, nb_keys, local ? s->mode : NULL);
}

/*---------------- help ----------------*/

static int qe_list_bindings(char *buf, int size, CmdDef *d,
                            ModeDef *mode, int inherit)
{
    int pos;
    buf_t outbuf, *out;

    out = buf_init(&outbuf, buf, size);
    pos = 0;
    for (;;) {
        KeyDef *kd = mode ? mode->first_key : qe_state.first_key;

        for (; kd != NULL; kd = kd->next) {
            if (kd->cmd == d
            &&  qe_find_current_binding(kd->keys, kd->nb_keys, mode) == kd) {
                if (out->len > pos)
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
    CmdDef *d;

    if ((d = qe_find_cmd(cmd_name)) == NULL) {
        put_status(s, "No command %s", cmd_name);
        return;
    }
    if (qe_list_bindings(buf, sizeof(buf), d, s->mode, 1)) {
        put_status(s, "%s is bound to %s", cmd_name, buf);
    } else {
        put_status(s, "%s is not bound to any key", cmd_name);
    }
}

static void print_bindings(EditBuffer *b, const char *title,
                           qe__unused__ int type, ModeDef *mode)
{
    char buf[256];
    CmdDef *d;
    int gfound;

    gfound = 0;
    d = qe_state.first_cmd;
    while (d != NULL) {
        while (d->name != NULL) {
            if (qe_list_bindings(buf, sizeof(buf), d, mode, 0)) {
                if (!gfound) {
                    if (title) {
                        eb_printf(b, "%s:\n\n", title);
                    } else {
                        eb_printf(b, "\n%s mode bindings:\n\n", mode->name);
                    }
                    gfound = 1;
                }
                eb_printf(b, "%24s : %s\n", d->name, buf);
            }
            d++;
        }
        d = d->action.next;
    }
}

void do_describe_bindings(EditState *s)
{
    EditBuffer *b;

    b = new_help_buffer();
    if (!b)
        return;

    print_bindings(b, NULL, 0, s->mode);
    print_bindings(b, "\nGlobal bindings", 0, NULL);

    b->flags |= BF_READONLY;
    show_popup(s, b);
}

void do_apropos(EditState *s, const char *str)
{
    QEmacsState *qs = s->qe_state;
    char buf[256];
    EditBuffer *b;
    CmdDef *d;
    VarDef *vp;
    int found;

    b = new_help_buffer();
    if (!b)
        return;

    eb_printf(b, "apropos '%s':\n\n", str);

    found = 0;
    d = qs->first_cmd;
    while (d != NULL) {
        while (d->name != NULL) {
            if (strstr(d->name, str)) {
                /* print name and prototype */
                qe_get_prototype(d, buf, sizeof(buf));
                eb_printf(b, "command: %s(%s)", d->name, buf);
                if (qe_list_bindings(buf, sizeof(buf), d, s->mode, 1))
                    eb_printf(b, " bound to %s", buf);
                eb_printf(b, "\n");
                /* TODO: print short description */
                eb_printf(b, "\n");
                found = 1;
            }
            d++;
        }
        d = d->action.next;
    }
    for (vp = qs->first_variable; vp; vp = vp->next) {
        if (strstr(vp->name, str)) {
            /* print class, name and current value */
            qe_get_variable(s, vp->name, buf, sizeof(buf), NULL, 1);
            eb_printf(b, "%s variable: %s -> %s\n",
                      var_domain[vp->domain], vp->name, buf);
            /* TODO: print short description */
            eb_printf(b, "\n");
            found = 1;
        }
    }
    if (found) {
        b->flags |= BF_READONLY;
        show_popup(s, b);
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
    CmdDef *d;

    b = eb_scratch("*About QEmacs*", BF_UTF8);
    eb_printf(b, "\n  %s\n\n%s\n", str_version, str_credits);

    /* list commands */
    print_bindings(b, NULL, 0, s->mode);
    print_bindings(b, "\nGlobal bindings", 0, NULL);

    /* other mode bindings */
    for (m = qs->first_mode; m; m = m->next) {
        if (m != s->mode)
            print_bindings(b, NULL, 0, m);
    }

    /* list commands */
    eb_printf(b, "\nCommands:\n\n");
    d = qs->first_cmd;
    while (d != NULL) {
        while (d->name != NULL) {
            qe_get_prototype(d, buf, sizeof(buf));
            eb_printf(b, "    %s(%s);\n", d->name, buf);
            d++;
        }
        d = d->action.next;
    }

    qe_list_variables(s, b);

    /* list environment */
    {
        char **envp;

        eb_printf(b, "\nEnvironment:\n\n");
        for (envp = environ; *envp; envp++) {
            eb_printf(b, "    %s\n", *envp);
        }
    }
    b->offset = 0;
    b->flags |= BF_READONLY;

    /* Should show window caption "About QEmacs" */
    show_popup(s, b);
}

/* extract the next word from the string. ignore spaces, stop on '/' */
static int str_get_word(char *buf, int size, const char *p, const char **pp)
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
                buf[len] = qe_tolower((unsigned char)*p);
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
        len = str_get_word(buf, sizeof(buf), p, &p);

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
        len = str_get_word(buf, sizeof(buf), p, &p);
        if (strfind("/|on", buf)) {
            str_get_word(buf, sizeof(buf), p, &p);
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

    eb_printf(b1, "Buffer Description\n\n");

    eb_printf(b1, "        name: %s\n", b->name);
    eb_printf(b1, "    filename: %s\n", b->filename);
    eb_printf(b1, "    modified: %d\n", b->modified);
    eb_printf(b1, "  total_size: %d\n", b->total_size);
    eb_printf(b1, "        mark: %d\n", b->mark);
    eb_printf(b1, "   s->offset: %d\n", s->offset);
    eb_printf(b1, "   b->offset: %d\n", b->offset);

    eb_printf(b1, "   tab_width: %d\n", b->tab_width);
    eb_printf(b1, " fill_column: %d\n", b->fill_column);

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
        u8 buf[4096];
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
            int size = eb_read(b, offset, buf, countof(buf));
            if (size == 0)
                break;
            for (i = 0; i < size; i++) {
                c = buf[i];
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
            max_count = max(max_count, count[i]);
        }
        count_width = snprintf(NULL, 0, "%d", max_count);

        eb_printf(b1, "       chars: %d\n", nb_chars);
        eb_printf(b1, "       words: %d\n", word_count);
        eb_printf(b1, "       lines: %d\n", line + (column > 0));

        eb_printf(b1, "\nByte stats:\n");

        for (col = i = 0; i < 256; i++) {
            if (count[i] == 0)
                continue;
            switch (i) {
            case '\b':  c = 'b'; break;
            case '\f':  c = 'f'; break;
            case '\t':  c = 't'; break;
            case '\r':  c = 'r'; break;
            case '\n':  c = 'n'; break;
            case '\\':  c = '\\'; break;
            case '\'':  c = '\''; break;
            default: c = 0; break;
            }
            col += eb_printf(b1, "   %*d", count_width, count[i]);

            if (c != 0)
                col += eb_printf(b1, "  '\\%c'", c);
            else
            if (i >= ' ' && i < 0x7f)
                col += eb_printf(b1, "  '%c' ", i);
            else
                col += eb_printf(b1, "  0x%02x", i);

            if (col >= 60) {
                eb_printf(b1, "\n");
                col = 0;
            }
        }
        if (col) {
            eb_printf(b1, "\n");
        }
    }

    if (b->nb_pages) {
        Page *p;
        const u8 *pc;
        int i, c, n;

        eb_printf(b1, "\nBuffer page layout:\n");

        eb_printf(b1, "    page  size  flags  lines   col  chars  addr\n");
        for (i = 0, p = b->page_table; i < b->nb_pages && i < 100; i++, p++) {
            eb_printf(b1, "    %4d  %4d  %5x  %5d  %4d  %5d  %p  |",
                      i, p->size, p->flags, p->nb_lines, p->col, p->nb_chars, p->data);
            pc = p->data;
            n = min(p->size, 16);
            while (n-- > 0) {
                switch (c = *pc++) {
                case '\r': c = 'r'; break;
                case '\n': c = 'n'; break;
                case '\t': c = 't'; break;
                case '\\': break;
                default:
                    if (c < 32 || c >= 127)
                        eb_printf(b1, "\\%03o", c);
                    else
                        eb_printf(b1, "%c", c);
                    continue;
                }
                eb_printf(b1, "\\%c", c);
            }
            eb_printf(b1, "|%s\n", p->size > 16 ? "..." : "");
        }
        eb_printf(b1, "\n");
    }

    b1->flags |= BF_READONLY;
    show_popup(s, b1);
}

static void do_describe_window(EditState *s, int argval)
{
    EditBuffer *b1;
    int w;

    b1 = new_help_buffer();
    if (!b1)
        return;

    eb_printf(b1, "Window Description\n\n");

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
    eb_printf(b1, "%*s: %d\n", w, "insert", s->insert);
    eb_printf(b1, "%*s: %d\n", w, "bidir", s->bidir);
    eb_printf(b1, "%*s: %d\n", w, "cur_rtl", s->cur_rtl);
    eb_printf(b1, "%*s: %d  %s\n", w, "wrap", s->wrap,
              s->wrap == WRAP_AUTO ? "AUTO" :
              s->wrap == WRAP_TRUNCATE ? "TRUNCATE" :
              s->wrap == WRAP_LINE ? "LINE" : 
              s->wrap == WRAP_TERM ? "TERM" : 
              s->wrap == WRAP_WORD ? "WORD" : "???");
    eb_printf(b1, "%*s: %d\n", w, "line_numbers", s->line_numbers);
    eb_printf(b1, "%*s: %d\n", w, "indent_size", s->indent_size);
    eb_printf(b1, "%*s: %d\n", w, "indent_tabs_mode", s->indent_tabs_mode);
    eb_printf(b1, "%*s: %d\n", w, "interactive", s->interactive);
    eb_printf(b1, "%*s: %d\n", w, "force_highlight", s->force_highlight);
    eb_printf(b1, "%*s: %d\n", w, "mouse_force_highlight", s->mouse_force_highlight);
    eb_printf(b1, "%*s: %p\n", w, "get_colorized_line", (void*)s->get_colorized_line);
    eb_printf(b1, "%*s: %p\n", w, "colorize_func", (void*)s->colorize_func);
    eb_printf(b1, "%*s: %lld\n", w, "default_style", (long long)s->default_style);
    eb_printf(b1, "%*s: %s\n", w, "buffer", s->b->name);
    if (s->last_buffer)
        eb_printf(b1, "%*s: %s\n", w, "last_buffer", s->last_buffer->name);
    eb_printf(b1, "%*s: %s\n", w, "mode", s->mode->name);
    eb_printf(b1, "%*s: %d\n", w, "colorize_nb_lines", s->colorize_nb_lines);
    eb_printf(b1, "%*s: %d\n", w, "colorize_nb_valid_lines", s->colorize_nb_valid_lines);
    eb_printf(b1, "%*s: %d\n", w, "colorize_max_valid_offset", s->colorize_max_valid_offset);
    eb_printf(b1, "%*s: %d\n", w, "busy", s->busy);
    eb_printf(b1, "%*s: %d\n", w, "display_invalid", s->display_invalid);
    eb_printf(b1, "%*s: %d\n", w, "borders_invalid", s->borders_invalid);
    eb_printf(b1, "%*s: %d\n", w, "show_selection", s->show_selection);
    eb_printf(b1, "%*s: %d\n", w, "region_style", s->region_style);
    eb_printf(b1, "%*s: %d\n", w, "curline_style", s->curline_style);
    eb_printf(b1, "\n");

    b1->flags |= BF_READONLY;
    show_popup(s, b1);
}

static void do_describe_screen(EditState *e, int argval)
{
    QEditScreen *s = e->screen;
    EditBuffer *b1;
    int w;

    b1 = new_help_buffer();
    if (!b1)
        return;

    eb_printf(b1, "Screen Description\n\n");

    w = 16;
    eb_printf(b1, "%*s: %s\n", w, "dpy.name", s->dpy.name);
    eb_printf(b1, "%*s: %d, %d\n", w, "width, height", s->width, s->height);
    eb_printf(b1, "%*s: %s\n", w, "charset", s->charset->name);
    eb_printf(b1, "%*s: %d\n", w, "media", s->media);
    eb_printf(b1, "%*s: %d\n", w, "bitmap_format", s->bitmap_format);
    eb_printf(b1, "%*s: %d\n\n", w, "video_format", s->video_format);

    eb_printf(b1, "%*s: %d\n", w, "QE_TERM_STYLE_BITS", QE_TERM_STYLE_BITS);
    eb_printf(b1, "%*s: %x << %d\n", w, "QE_TERM_FG_COLORS", QE_TERM_FG_COLORS, QE_TERM_FG_SHIFT);
    eb_printf(b1, "%*s: %x << %d\n\n", w, "QE_TERM_BG_COLORS", QE_TERM_BG_COLORS, QE_TERM_BG_SHIFT);

    dpy_describe(s, b1);

    b1->flags |= BF_READONLY;
    show_popup(e, b1);
}

/*---------------- buffer contents sorting ----------------*/

struct chunk_ctx {
    EditBuffer *b;
    int flags;
#define SF_REVERSE  1
#define SF_FOLD     2
#define SF_DICT     4
#define SF_NUMBER   8
};

struct chunk {
    int start, end;
};

static int chunk_cmp(void *vp0, const void *vp1, const void *vp2) {
    const struct chunk_ctx *cp = vp0;
    const struct chunk *p1 = vp1;
    const struct chunk *p2 = vp2;
    int pos1, pos2;

    if (cp->flags & SF_REVERSE) {
        p1 = vp2;
        p2 = vp1;
    }

    pos1 = p1->start;
    pos2 = p2->start;
    for (;;) {
        int c1 = 0, c2 = 0;
        while (pos1 < p1->end) {
            c1 = eb_nextc(cp->b, pos1, &pos1);
            if (!(cp->flags & SF_DICT) || qe_isalpha(c1))
                break;
            c1 = 0;
        }
        while (pos2 < p2->end) {
            c2 = eb_nextc(cp->b, pos2, &pos2);
            if (!(cp->flags & SF_DICT) || qe_isalpha(c2))
                break;
            c2 = 0;
        }
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
            // XXX: should support unicode case folding
            c1 = qe_toupper(c1);
            c2 = qe_toupper(c2);
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

static void do_sort_span(EditState *s, int p1, int p2, int flags, int argval) {
    struct chunk_ctx ctx;
    EditBuffer *b;
    int i, offset, line1, line2, col1, col2, lines;
    struct chunk *chunk_array;

    s->region_style = 0;

    if (p1 > p2) {
        int tmp = p1;
        p1 = p2;
        p2 = tmp;
    }
    ctx.b = s->b;
    if (argval != NO_ARG)
        flags |= argval;
    ctx.flags = flags;
    eb_get_pos(s->b, &line1, &col1, p1); /* line1 is included */
    eb_get_pos(s->b, &line2, &col2, p2); /* line2 is excluded */
    lines = line2 - line1;
    chunk_array = qe_malloc_array(struct chunk, lines);
    if (!chunk_array) {
        put_status(s, "Out of memory");
        return;
    }
    offset = eb_goto_bol(s->b, p1);
    for (i = 0; i < lines; i++) {
        chunk_array[i].start = offset;
        chunk_array[i].end = offset = eb_goto_eol(s->b, offset);
        offset = eb_next(s->b, offset);
    }
    qe_qsort_r(chunk_array, lines, sizeof(*chunk_array), &ctx, chunk_cmp);
        
    b = eb_new("*sorted*", BF_SYSTEM);
    eb_set_charset(b, s->b->charset, s->b->eol_type);

    for (i = 0; i < lines; i++) {
        eb_insert_buffer(b, b->total_size, s->b, chunk_array[i].start,
                         chunk_array[i].end - chunk_array[i].start);
        eb_putc(b, '\n');
    }
    eb_delete_range(s->b, p1, p2);
    s->b->mark = p1;
    s->offset = p1 + eb_insert_buffer(s->b, p1, b, 0, b->total_size);
    eb_free(&b);
    qe_free(&chunk_array);
}

static void do_sort_region(EditState *s, int flags, int argval) {
    do_sort_span(s, s->b->mark, s->offset, flags, argval);
}

static void do_sort_buffer(EditState *s, int flags, int argval) {
    do_sort_span(s, 0, s->b->total_size, flags, argval);
}

static void tag_buffer(EditState *s) {
    unsigned int buf[100];
    QETermStyle sbuf[100];
    int offset, line_num, col_num;

    if (s->colorize_func || s->b->b_styles) {
        /* force complete buffer colorizarion */
        eb_get_pos(s->b, &line_num, &col_num, s->b->total_size);
        s->get_colorized_line(s, buf, countof(buf), sbuf,
                              s->b->total_size, &offset, line_num);
    }
}

static void tag_completion(CompleteState *cp) {
    /* XXX: only support current buffer */
    QEProperty *p;

    if (cp->target) {
        tag_buffer(cp->target);

        for (p = cp->target->b->property_list; p; p = p->next) {
            if (p->type == QE_PROP_TAG) {
                complete_test(cp, p->data);
            }
        }
    }
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

static void do_list_tags(EditState *s) {
    EditBuffer *b;
    QEProperty *p;

    b = new_help_buffer();
    if (!b)
        return;

    tag_buffer(s);

    eb_printf(b, "\nTags in file %s:\n\n", s->b->filename);
    for (p = s->b->property_list; p; p = p->next) {
        if (p->type == QE_PROP_TAG) {
            eb_printf(b, "%12d  %s\n", p->offset, (char*)p->data);
        }
    }

    b->flags |= BF_READONLY;
    show_popup(s, b);
}

static CmdDef extra_commands[] = {
    CMD2( KEY_META('='), KEY_NONE,
          "compare-windows", do_compare_windows, ESi, "ui" )
    CMD3( KEY_CTRLX(KEY_CTRL('l')), KEY_NONE,
          "compare-files", do_compare_files, ESsi, 0,
          "s{Compare file: }[file]|file|"
          "v") /* u? */
    CMD2( KEY_META('\\'), KEY_NONE,
          "delete-horizontal-space", do_delete_horizontal_space, ES, "*")
    CMD2( KEY_CTRLX(KEY_CTRL('o')), KEY_NONE,
          "delete-blank-lines", do_delete_blank_lines, ES, "*")
    CMD2( KEY_NONE, KEY_NONE,
          "tabify-region", do_tabify_region, ES, "*")
    CMD2( KEY_NONE, KEY_NONE,
          "tabify-buffer", do_tabify_buffer, ES, "*")
    CMD2( KEY_NONE, KEY_NONE,
          "untabify-region", do_untabify_region, ES, "*")
    CMD2( KEY_NONE, KEY_NONE,
          "untabify-buffer", do_untabify_buffer, ES, "*")
    CMD2( KEY_META(KEY_CTRL('\\')), KEY_NONE,
          "indent-region", do_indent_region, ES, "*")

    CMD2( KEY_CTRLX('t'), KEY_NONE,
          "show-date-and-time", do_show_date_and_time, ESi, "ui")

          /* Should map to KEY_META + KEY_CTRL_LEFT */
    CMD3( KEY_META(KEY_CTRL('b')), KEY_NONE,
          "backward-block", do_forward_block, ESi, -1, "v")
          /* Should map to KEY_META + KEY_CTRL_RIGHT */
    CMD3( KEY_META(KEY_CTRL('f')), KEY_NONE,
          "forward-block", do_forward_block, ESi, 1, "v")
    CMD3( KEY_ESC, KEY_DELETE,
          "backward-kill-block", do_kill_block, ESi, -1, "v")
    CMD3( KEY_META(KEY_CTRL('k')), KEY_NONE,
          "kill-block", do_kill_block, ESi, 1, "v")
          /* Should also have mark-block on C-M-@ */

    CMD3( KEY_CTRL('t'), KEY_NONE,
          "transpose-chars", do_transpose, ESi, CMD_TRANSPOSE_CHARS, "*v")
    CMD3( KEY_CTRLX(KEY_CTRL('t')), KEY_NONE,
          "transpose-lines", do_transpose, ESi, CMD_TRANSPOSE_LINES, "*v")
    CMD3( KEY_META('t'), KEY_NONE,
          "transpose-words", do_transpose, ESi, CMD_TRANSPOSE_WORDS, "*v")

    CMD3( KEY_NONE, KEY_NONE,
          "global-unset-key", do_unset_key, ESsi, 0,
          "s{Unset key globally: }[key]"
          "v")
    CMD3( KEY_NONE, KEY_NONE,
          "local-unset-key", do_unset_key, ESsi, 1,
          "s{Unset key locally: }[key]"
          "v")

    CMD0( KEY_CTRLH('?'), KEY_F1,
          "about-qemacs", do_about_qemacs)
    CMD2( KEY_CTRLH('a'), KEY_CTRLH(KEY_CTRL('A')),
          "apropos", do_apropos, ESs,
          "s{Apropos: }|apropos|")
    CMD0( KEY_CTRLH('b'), KEY_NONE,
          "describe-bindings", do_describe_bindings)
    CMD2( KEY_CTRLH('B'), KEY_NONE,
          "show-bindings", do_show_bindings, ESs,
          "s{Show bindings of command: }[command]|command|")
    CMD2( KEY_CTRLH(KEY_CTRL('B')), KEY_NONE,
          "describe-buffer", do_describe_buffer, ESi, "ui")
    CMD2( KEY_CTRLH('w'), KEY_CTRLH(KEY_CTRL('W')),
          "describe-window", do_describe_window, ESi, "ui")
    CMD2( KEY_CTRLH('s'), KEY_CTRLH(KEY_CTRL('S')),
          "describe-screen", do_describe_screen, ESi, "ui")

    CMD2( KEY_CTRLC('c'), KEY_NONE,
          "set-region-color", do_set_region_color, ESs,
          "s{Select color: }[color]|color|")
    CMD2( KEY_CTRLC('s'), KEY_NONE,
          "set-region-style", do_set_region_style, ESs,
          "s{Select style: }[style]|style|")
    CMD0( KEY_NONE, KEY_NONE,
          "drop-styles", do_drop_styles)

    CMD2( KEY_NONE, KEY_NONE,
          "set-eol-type", do_set_eol_type, ESi,
          "ui{EOL Type [0=Unix, 1=Dos, 2=Mac]: }")

    CMD3( KEY_NONE, KEY_NONE,
          "sort-buffer", do_sort_buffer, ESii, 0, "*vui")
    CMD3( KEY_NONE, KEY_NONE,
          "sort-region", do_sort_region, ESii, 0, "*vui")
    CMD3( KEY_NONE, KEY_NONE,
          "reverse-sort-buffer", do_sort_buffer, ESii, SF_REVERSE, "*vui")
    CMD3( KEY_NONE, KEY_NONE,
          "reverse-sort-region", do_sort_region, ESii, SF_REVERSE, "*vui")

    CMD0( KEY_NONE, KEY_NONE,
          "list-tags", do_list_tags)
    CMD0( KEY_CTRLX(','), KEY_META(KEY_F1),
          "goto-tag", do_goto_tag)
    CMD2( KEY_CTRLX('.'), KEY_NONE,
          "find-tag", do_find_tag, ESs,
          "s{Find tag: }[tag]|tag|")

    CMD_DEF_END,
};

static int extras_init(void)
{
    int key;

    qe_register_cmd_table(extra_commands, NULL);
    for (key = KEY_META('0'); key <= KEY_META('9'); key++) {
        qe_register_binding(key, "numeric-argument", NULL);
    }
    register_completion("tag", tag_completion);

    return 0;
}

qe_module_init(extras_init);
