/*
 * Markdown mode for QEmacs.
 *
 * Copyright (c) 2014 Charlie Gordon.
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

ModeDef mkd_mode;

enum {
    QE_STYLE_MKD_HEADING1 = QE_STYLE_FUNCTION,
    QE_STYLE_MKD_HEADING2 = QE_STYLE_STRING,
    QE_STYLE_MKD_HEADING3 = QE_STYLE_VARIABLE,
    QE_STYLE_MKD_HEADING4 = QE_STYLE_TYPE,
    QE_STYLE_MKD_TILDE = QE_STYLE_PREPROCESS,
    QE_STYLE_MKD_COMMENT = QE_STYLE_COMMENT,
    QE_STYLE_MKD_BLOCK_QUOTE = QE_STYLE_FUNCTION,
    QE_STYLE_MKD_TABLE = QE_STYLE_TYPE,
    QE_STYLE_MKD_HBAR = QE_STYLE_VARIABLE,
    QE_STYLE_MKD_STRONG2 = QE_STYLE_FUNCTION,
    QE_STYLE_MKD_STRONG1 = QE_STYLE_FUNCTION,
    QE_STYLE_MKD_EMPHASIS2 = QE_STYLE_VARIABLE,
    QE_STYLE_MKD_EMPHASIS1 = QE_STYLE_VARIABLE,
    QE_STYLE_MKD_CODE = QE_STYLE_STRING,
    QE_STYLE_MKD_IMAGE_LINK = QE_STYLE_KEYWORD,
    QE_STYLE_MKD_REF_LINK = QE_STYLE_KEYWORD,
    QE_STYLE_MKD_DLIST = QE_STYLE_NUMBER,
    QE_STYLE_MKD_LIST = QE_STYLE_NUMBER,

#if 0
QE_STYLE_COMMENT,
QE_STYLE_PREPROCESS,
QE_STYLE_STRING,
QE_STYLE_STRING_Q,
QE_STYLE_KEYWORD,
QE_STYLE_NUMBER,
QE_STYLE_FUNCTION,
QE_STYLE_VARIABLE,
QE_STYLE_TYPE,
QE_STYLE_TAG,
#endif
};

#define IN_HTML_BLOCK  0x8000
#define IN_BLOCK       0x4000
#define IN_LANG        0x3800
#define IN_C           0x0800
#define IN_PYTHON      0x1000
#define IN_RUBY        0x1800
#define IN_HASKELL     0x2000
#define IN_LUA         0x2800
#define IN_LEVEL       0x0700
#define LEVEL_SHIFT  8

#define MAX_LEVEL       128

/* TODO: define specific styles */
#define BULLET_STYLES 4
static int MkdBulletStyles[BULLET_STYLES] = {
    QE_STYLE_MKD_HEADING1,
    QE_STYLE_MKD_HEADING2,
    QE_STYLE_MKD_HEADING3,
    QE_STYLE_MKD_HEADING4,
};

static int mkd_scan_chunk(const unsigned int *str,
                          const char *begin, const char *end, int min_width)
{
    int i, j;

    for (i = 0; begin[i]; i++) {
        if (str[i] != begin[i])
            return 0;
    }
    for (j = 0; j < min_width; j++) {
        if (str[i + j] == '\0')
            return 0;
    }
    for (i += j; str[i] != '\0'; i++) {
        for (j = 0; end[j]; j++) {
            if (str[i + j] != end[j])
                break;
        }
        if (!end[j])
            return i + j;
    }
    return 0;
}

static void mkd_colorize_line(unsigned int *str, int n, int *statep,
                              __unused__ int state_only)
{
    int colstate = *statep;
    int level, indent, i = 0, j = 0, base_style = 0;

    if (colstate & IN_HTML_BLOCK) {
        if (str[i] != '<' && str[i] != '\0' && !qe_isblank(str[i]))
            colstate &= ~IN_HTML_BLOCK;
    }

    if ((colstate & IN_HTML_BLOCK)
    ||  (str[i] == '<' && str[i + 1] != '/')) {
        /* block level HTML markup */
        colstate &= ~IN_HTML_BLOCK;
        htmlsrc_colorize_line(str, n, &colstate, state_only);
        colstate |= IN_HTML_BLOCK;
        if ((str[i] & CHAR_MASK) == '<' && (str[i + 1] & CHAR_MASK) == '/')
            colstate = 0;
        *statep = colstate;
        return;
    }

    if (colstate & IN_BLOCK) {
        /* Should count number of ~ to detect end of block */
        if (ustrstart(str + i, "~~~", NULL)
        ||  ustrstart(str + i, "```", NULL)) {
            colstate &= ~(IN_BLOCK | IN_LANG);
            SET_COLOR(str, i, n, QE_STYLE_MKD_TILDE);
            i = n;
        } else {
            int lang = colstate & IN_LANG;
            colstate &= ~(IN_BLOCK | IN_LANG);
            switch (lang) {
            case IN_C:
                c_colorize_line(str, n, &colstate, state_only);
                break;
            case IN_PYTHON:
                python_colorize_line(str, n, &colstate, state_only);
                break;
            case IN_RUBY:
                ruby_colorize_line(str, n, &colstate, state_only);
                break;
            case IN_HASKELL:
                haskell_colorize_line(str, n, &colstate, state_only);
                break;
            case IN_LUA:
                lua_colorize_line(str, n, &colstate, state_only);
                break;
            default:
                SET_COLOR(str, i, n, QE_STYLE_MKD_CODE);
                break;
            }
            colstate &= ~(IN_BLOCK | IN_LANG);
            colstate |= (IN_BLOCK | lang);
        }
        *statep = colstate;
        return;
    }

    if (str[i] == '#') {
        /* Check for heading: initial string of '#' followed by ' ' */
        for (j = i + 1; str[j] == '#'; j++)
            continue;

        if (qe_isblank(str[j])) {
            base_style = MkdBulletStyles[(j - i - 1) % BULLET_STYLES];
            SET_COLOR(str, i, j + 1, base_style);
            i = j + 1;
        }
    } else
    if (str[i] == '%') {
        /* pandoc extension: line comment */
        SET_COLOR(str, i, n, QE_STYLE_MKD_COMMENT);
        i = n;
    } else
    if (str[i] == '>') {
        /* block quoting */
        SET_COLOR(str, i, n, QE_STYLE_MKD_BLOCK_QUOTE);
        i = n;
    } else
    if (ustrstart(str + i, "~~~", NULL)
    ||  ustrstart(str + i, "```", NULL)) {
        /* verbatim block */
        colstate |= IN_BLOCK;
        if (ustrstr(str + i + 3, "c")
        ||  ustrstr(str + i + 3, "java")) {
            colstate |= IN_C;
        } else
        if (ustrstr(str + i + 3, "haskell")) {
            colstate |= IN_HASKELL;
        } else
        if (ustrstr(str + i + 3, "lua")) {
            colstate |= IN_LUA;
        } else
        if (ustrstr(str + i + 3, "python")) {
            colstate |= IN_PYTHON;
        } else
        if (ustrstr(str + i + 3, "ruby")) {
            colstate |= IN_RUBY;
        }
        SET_COLOR(str, i, n, QE_STYLE_MKD_TILDE);
        i = n;
    } else
    if (str[i] == '-') {
        /* dashes underline a heading */
        for (j = i + 1; str[j] == '-'; j++)
            continue;
        if (j == n) {
            SET_COLOR(str, i, n, QE_STYLE_MKD_HEADING2);
            i = n;
        }
    } else
    if (str[i] == '=') {
        /* equal signs indicate a heading */
        for (j = i + 1; str[j] == '='; j++)
            continue;
        if (j == n) {
            SET_COLOR(str, i, n, QE_STYLE_MKD_HEADING1);
            i = n;
        }
    } else
    if (str[i] == '|') {
        base_style = QE_STYLE_MKD_TABLE;
    }

    /* [X] unordered lists: /[-*+] / */
    /* [X] ordered lists: /[0-9]+[.] / */
    /* [ ] list continuation lines are indented 1 level */
    /* [ ] code blocks are indented one extra level */
    /* [X] horizontal rules: /^ *([-*_][ ]*){3-}$/ */
    /* [/] inline links: /[[].*[]]([(].*[)])?/ */
    /* [/] reference links: /[[].*[]][ ]*[[][a-zA-Z0-9 ,.;:?]*[]])/ */
    /* [/] references: /[ ]{0-3}[[][a-zA-Z0-9 ,.;:?]+[]]:.*)/ */
    /* [/] images: same as links, preceded by ! */
    /* [X] automatic links and email addresses: <http://adress> */
    /* [X] emphasis: _.*_  \*.*\*  __.*__  \*\*.*\*\*  */
    /* [X] code span: `code` */
    /* [X] code span with embedded `: ``code`` or `` code `` */
    /* [X] litteral chars: isolate them or escape them with \ */
    /*                 \ ` * _ { } [ ] ( ) # + - . ! */

    level = (colstate & IN_LEVEL) >> LEVEL_SHIFT;
    for (indent = 0;; i++) {
        if (str[i] == ' ')
            indent++;
        else
        if (str[i] == '\t')
            indent += 4;
        else
            break;
    }

    if (i < n) {
        /* ignore blank lines for level and code triggers */
        if (indent < level * 4) {
            level = indent >> 2;
        }
        indent -= level * 4;

        if (indent >= 4) {
            /* Should detect sequel lines in ordered/unordered lists */
            SET_COLOR(str, i, n, QE_STYLE_MKD_CODE);
            i = n;
        }
    }

    if (str[i] == '*' || str[i] == '-' || str[i] == '_') {
        int count = 1;
        for (j = i + 1; j < n; j++) {
            if (qe_isblank(str[j]))
                continue;
            if (str[j] == str[i])
                count++;
            else
                break;
        }
        if (j == n && count >= 3) {
            /* Horizontal rule */
            SET_COLOR(str, i, n, QE_STYLE_MKD_HBAR);
            i = n;
        }
    }

    if (level) {
        base_style = QE_STYLE_MKD_LIST;
    }

    if (qe_isdigit(str[i])) {
        for (j = i + 1; qe_isdigit(str[j]); j++)
            continue;
        if (str[j] == '.' && qe_isblank(str[j + 1])) {
            level++;
            base_style = QE_STYLE_MKD_DLIST;
            SET_COLOR(str, i, j, base_style);
            i = j;
        }
    } else
    if ((str[i] == '-' || str[i] == '*' || str[i] == '+')
    &&  qe_isblank(str[i + 1])) {
        level++;
        base_style = QE_STYLE_MKD_LIST;
        SET_COLOR(str, i, j, base_style);
        i = j;
    }

    for (;;) {
        int chunk = 0, chunk_style = base_style;
        int flags;
        int c = str[i];

        if (c == '\0')
            break;

        switch (c) {
        case '#':
            break;
        case '*':  /* bold */
            if (str[i + 1] == '*') {
                chunk_style = QE_STYLE_MKD_STRONG2;
                chunk = mkd_scan_chunk(str + i, "**", "**", 1);
            } else {
                chunk_style = QE_STYLE_MKD_STRONG1;
                chunk = mkd_scan_chunk(str + i, "*", "*", 1);
            }
            break;
        case '_':  /* emphasis */
            if (str[i + 1] == '_') {
                chunk_style = QE_STYLE_MKD_EMPHASIS2;
                chunk = mkd_scan_chunk(str + i, "__", "__", 1);
            } else {
                chunk_style = QE_STYLE_MKD_EMPHASIS1;
                chunk = mkd_scan_chunk(str + i, "_", "_", 1);
            }
            break;
        case '`':  /* code */
            chunk_style = QE_STYLE_MKD_CODE;
            if (str[i + 1] == '`') {
                if (str[i + 2] == ' ') {
                    chunk = mkd_scan_chunk(str + i, "`` ", " ``", 1);
                } else {
                    chunk = mkd_scan_chunk(str + i, "``", "``", 1);
                }
            } else {
                chunk = mkd_scan_chunk(str + i, "`", "`", 1);
            }
            break;
        case '!':  /* image link ^[...: <...>] */
            chunk_style = QE_STYLE_MKD_IMAGE_LINK;
            chunk = mkd_scan_chunk(str + i, "![", "]", 1);
            break;
        case '[':  /* link ^[...: <...>] */
            chunk_style = QE_STYLE_MKD_REF_LINK;
            chunk = mkd_scan_chunk(str + i, "[", "]", 1);
            break;
        case '<':  /* automatic link <http://address> */
            chunk_style = QE_STYLE_MKD_REF_LINK;
            chunk = mkd_scan_chunk(str + i, "<http", ">", 1);
            if (chunk)
                break;
            for (flags = 0, j = i + 1; j < n; j++) {
                if (str[j] == '@')
                    flags |= 1;
                if (str[j] == '>') {
                    flags |= 2;
                    break;
                }
            }
            if (flags == 3) {
                chunk = j + 1;
                break;
            }
            break;
        case '\\':  /* escape */
            if (strchr("\\`*_{}[]()#+-.!", str[i + 1])) {
                SET_COLOR(str, i, i + 2, base_style);
                i += 2;
                continue;
            }
            break;
        }
        if (chunk) {
            SET_COLOR(str, i, i + chunk, chunk_style);
            i += chunk;
        } else {
            SET_COLOR1(str, i, base_style);
            i++;
        }
    }

    colstate &= ~IN_LEVEL;
    colstate |= (level << LEVEL_SHIFT) & IN_LEVEL;
    *statep = colstate;
}

static int mkd_is_header_line(EditState *s, int offset)
{
    /* Check if line starts with '#' */
    /* XXX: should ignore blocks using colorstate */
    return eb_nextc(s->b, eb_goto_bol(s->b, offset), &offset) == '#';
}

static int mkd_find_heading(EditState *s, int offset, int *level, int silent)
{
    int offset1, nb, c;

    offset = eb_goto_bol(s->b, offset);
    for (;;) {
        /* Find line starting with '#' */
        /* XXX: should ignore blocks using colorstate */
        if (eb_nextc(s->b, offset, &offset1) == '#') {
            for (nb = 1; (c = eb_nextc(s->b, offset1, &offset1)) == '#'; nb++)
                continue;
            if (qe_isblank(c)) {
                *level = nb;
                return offset;
            }
        }
        if (offset == 0)
            break;
        offset = eb_prev_line(s->b, offset);
    }
    if (!silent)
        put_status(s, "Before first heading");

    return -1;
}

static int mkd_next_heading(EditState *s, int offset, int target, int *level)
{
    int offset1, nb, c;

    for (;;) {
        offset = eb_next_line(s->b, offset);
        if (offset >= s->b->total_size) {
            nb = 0;
            break;
        }
        /* XXX: should ignore blocks using colorstate */
        if (eb_nextc(s->b, offset, &offset1) == '#') {
            for (nb = 1; (c = eb_nextc(s->b, offset1, &offset1)) == '#'; nb++)
                continue;
            if (qe_isblank(c) && nb <= target) {
                break;
            }
        }
    }
    if (level)
        *level = nb;
    return offset;
}

static int mkd_prev_heading(EditState *s, int offset, int target, int *level)
{
    int offset1, nb, c;

    for (;;) {
        if (offset == 0) {
            nb = 0;
            break;
        }
        offset = eb_prev_line(s->b, offset);
        /* XXX: should ignore blocks using colorstate */
        if (eb_nextc(s->b, offset, &offset1) == '#') {
            for (nb = 1; (c = eb_nextc(s->b, offset1, &offset1)) == '#'; nb++)
                continue;
            if (qe_isblank(c) && nb <= target) {
                break;
            }
        }
    }
    if (level)
        *level = nb;
    return offset;
}

static void do_outline_next_vsible_heading(EditState *s)
{
    s->offset = mkd_next_heading(s, s->offset, MAX_LEVEL, NULL);
}

static void do_outline_previous_vsible_heading(EditState *s)
{
    s->offset = mkd_prev_heading(s, s->offset, MAX_LEVEL, NULL);
}

static void do_outline_up_heading(EditState *s)
{
    int offset, level;

    offset = mkd_find_heading(s, s->offset, &level, 0);
    if (offset < 0)
        return;

    if (level <= 1) {
        put_status(s, "Already at top level of the outline");
        return;
    }

    s->offset = mkd_prev_heading(s, offset, level - 1, &level);
}

static void do_mkd_backward_same_level(EditState *s)
{
    int offset, level, level1;

    offset = mkd_find_heading(s, s->offset, &level, 0);
    if (offset < 0)
        return;

    offset = mkd_prev_heading(s, offset, level, &level1);
    if (level1 != level) {
        put_status(s, "No previous same-level heading");
        return;
    }
    s->offset = offset;
}

static void do_mkd_forward_same_level(EditState *s)
{
    int offset, level, level1;

    offset = mkd_find_heading(s, s->offset, &level, 0);
    if (offset < 0)
        return;

    offset = mkd_next_heading(s, offset, level, &level1);
    if (level1 != level) {
        put_status(s, "No following same-level heading");
        return;
    }
    s->offset = offset;
}

static void do_mkd_goto(EditState *s, const char *dest)
{
    int offset, level, level1, nb;
    const char *p = dest;

    /* XXX: Should pop up a window with numbered outline index
     * and let the user select the target interactively.
     */

    /* Jump to numbered destination. */
    for (offset = 0, level = 0; qe_isdigit(*p); ) {
        nb = strtol(p, (char **)&p, 10);
        if (*p == '.')
            p++;
        level++;
        for (; nb > 0; nb--) {
            offset = mkd_next_heading(s, offset, level, &level1);
            if (level != level1) {
                put_status(s, "Heading not found");
                return;
            }
        }
    }
    if (level)
        s->offset = offset;
}

static void do_mkd_mark_element(EditState *s, int subtree)
{
    QEmacsState *qs = s->qe_state;
    int offset, offset1, level;

    offset = mkd_find_heading(s, s->offset, &level, 0);
    if (offset < 0)
        return;

    offset1 = mkd_next_heading(s, offset, subtree ? level : MAX_LEVEL, NULL);

    /* XXX: if repeating last command, add subtree to region */
    if (qs->last_cmd_func != qs->this_cmd_func)
        s->b->mark = offset;

    s->offset = offset1;
    /* activate region hilite */
    if (s->qe_state->hilite_region)
        s->region_style = QE_STYLE_REGION_HILITE;
}

static void do_mkd_insert_heading(EditState *s, int flags)
{
    int offset, offset0, offset1, level = 1;

    if (check_read_only(s))
        return;

    offset = mkd_find_heading(s, s->offset, &level, 1);
    offset0 = eb_goto_bol(s->b, s->offset);
    offset1 = eb_goto_eol(s->b, s->offset);

    /* if at beginning of heading line, insert sibling heading before,
     * if in the middle of a heading line, split the heading,
     * otherwise, make the current line a heading line at current level.
     */
    if (flags & 2) {
        /* respect-content: insert heading at end of subtree */
        offset = mkd_next_heading(s, offset, level, NULL);
        eb_insert_uchar(s->b, offset, '\n');
        eb_insert_uchar(s->b, offset, '\n');
    } else
    if (s->offset <= offset + level + 1) {
        eb_insert_uchar(s->b, offset, '\n');
    } else
    if (offset == offset0 || offset == offset1) {
        offset = s->offset;
        offset += eb_insert_uchar(s->b, offset, '\n');
    } else {
        offset = offset0;
    }        
    while (eb_nextc(s->b, offset, &offset1) == ' ') {
        eb_delete_uchar(s->b, offset);
    }
    while (level-- > 0) {
        offset += eb_insert_uchar(s->b, offset, '#');
    }
    offset += eb_insert_uchar(s->b, offset, ' ');
    s->offset = eb_goto_eol(s->b, offset);
}

static void do_mkd_promote(EditState *s, int dir)
{
    int offset, level;

    if (check_read_only(s))
        return;

    offset = mkd_find_heading(s, s->offset, &level, 0);
    if (offset < 0)
        return;

    if (dir < 0) {
        eb_insert_uchar(s->b, offset, '#');
    } else
    if (dir > 0) {
        if (level > 1)
            eb_delete_uchar(s->b, offset);
        else
            put_status(s, "Cannot promote to level 0");
    }
}

static void do_mkd_promote_subtree(EditState *s, int dir)
{
    int offset, level, level1;

    if (check_read_only(s))
        return;

    offset = mkd_find_heading(s, s->offset, &level, 0);
    if (offset < 0)
        return;

    for (;;) {
        if (dir < 0) {
            eb_insert_uchar(s->b, offset, '#');
        } else
        if (dir > 0) {
            if (level > 1) {
                eb_delete_uchar(s->b, offset);
            } else {
                put_status(s, "Cannot promote to level 0");
                return;
            }
        }
        offset = mkd_next_heading(s, offset, MAX_LEVEL, &level1);
        if (level1 <= level)
            break;
    }
}

static void do_mkd_move_subtree(EditState *s, int dir)
{
    int offset, offset1, offset2, level, level1, level2, size;
    EditBuffer *b1;

    if (check_read_only(s))
        return;

    if (!mkd_is_header_line(s, s->offset)) {
        put_status(s, "Not on header line");
        return;
    }

    offset = mkd_find_heading(s, s->offset, &level, 0);
    if (offset < 0)
        return;

    offset1 = mkd_next_heading(s, offset, level, &level1);
    size = offset1 - offset;

    if (dir < 0) {
        offset2 = mkd_prev_heading(s, offset, level, &level2);
        if (level2 < level) {
            put_status(s, "Cannot move substree");
            return;
        }
    } else {
        if (offset1 == s->b->total_size || level1 < level) {
            put_status(s, "Cannot move substree");
            return;
        }
        offset2 = mkd_next_heading(s, offset1, level, &level2);
    }
    b1 = eb_new("*tmp*", BF_SYSTEM | (s->b->flags & BF_STYLES));
    eb_set_charset(b1, s->b->charset, s->b->eol_type);
    eb_insert_buffer_convert(b1, 0, s->b, offset, size);
    eb_delete(s->b, offset, size);
    if (offset2 > offset)
        offset2 -= size;
    eb_insert_buffer_convert(s->b, offset2, b1, 0, b1->total_size);
    eb_free(&b1);
    s->offset = offset2;
}

static void do_mkd_meta_return(EditState *s)
{
    do_mkd_insert_heading(s, 0);
}

static void do_mkd_metaleft(EditState *s)
{
    if (mkd_is_header_line(s, s->offset))
        do_mkd_promote(s, +1);
    else
        do_word_right(s, -1);
}

static void do_mkd_metaright(EditState *s)
{
    if (mkd_is_header_line(s, s->offset))
        do_mkd_promote(s, -1);
    else
        do_word_right(s, +1);
}

static void do_mkd_metadown(EditState *s)
{
    do_mkd_move_subtree(s, +1);
}

static void do_mkd_metaup(EditState *s)
{
    do_mkd_move_subtree(s, -1);
}

static int mkd_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    /* just check file extension */
    if (match_extension(p->filename, mode->extensions))
        return 80;

    return 1;
}

/* Mkd mode specific commands */
static CmdDef mkd_commands[] = {
    /* Motion */
    CMD2( KEY_CTRLC(KEY_CTRL('n')), KEY_NONE,   /* C-c C-n */
          "mkd-next-visible-heading",
          do_outline_next_vsible_heading, ES, "")
    CMD2( KEY_CTRLC(KEY_CTRL('p')), KEY_NONE,   /* C-c C-p */
          "mkd-previous-visible-heading",
          do_outline_previous_vsible_heading, ES, "")
    CMD2( KEY_CTRLC(KEY_CTRL('u')), KEY_NONE,   /* C-c C-u */
          "mkd-up-heading", do_outline_up_heading, ES, "")
    CMD2( KEY_CTRLC(KEY_CTRL('b')), KEY_NONE,   /* C-c C-b */
          "mkd-backward-same-level", do_mkd_backward_same_level, ES, "")
    CMD2( KEY_CTRLC(KEY_CTRL('f')), KEY_NONE,   /* C-c C-f */
          "mkd-forward-same-level", do_mkd_forward_same_level, ES, "")
    CMD2( KEY_CTRLC(KEY_CTRL('j')), KEY_NONE,   /* C-c C-j */
          "mkd-goto", do_mkd_goto, ESs,
          "s{select location to jump to: }[mkdjump]|mkdjump|")
    CMD3( KEY_META('h'), KEY_NONE,   /* M-h */
          "mkd-mark-element", do_mkd_mark_element, ESi, 0, "v")
    CMD3( KEY_CTRLC('@'), KEY_NONE,   /* C-c @ */
          "mkd-mark-subtree", do_mkd_mark_element, ESi, 1, "v")
    /* Editing */
    CMD3( KEY_NONE, KEY_NONE,    /* indirect through M-RET */
          "mkd-insert-heading", do_mkd_insert_heading, ESi, 0, "*v")
    CMD3( KEY_CTRL('j'), KEY_NONE,    /* actually C-RET */
          "mkd-insert-heading-respect-content", do_mkd_insert_heading, ESi, 2, "*v")
    CMD3( KEY_NONE, KEY_NONE,
          "mkd-do-demote", do_mkd_promote, ESi, -1, "*v")
    CMD3( KEY_NONE, KEY_NONE,
          "mkd-do-promote", do_mkd_promote, ESi, +1, "*v")
    CMD3( KEY_CTRLX('>'), KEY_NONE,    /* actually M-S-right | C-c C-x R */
          "mkd-demote-subtree", do_mkd_promote_subtree, ESi, -1, "*v")
    CMD3( KEY_CTRLX('<'), KEY_NONE,    /* actually M-S-left | C-c C-x L */
          "mkd-promote-subtree", do_mkd_promote_subtree, ESi, +1, "*v")
    CMD3( KEY_NONE, KEY_NONE,
          "mkd-move-subtree-down", do_mkd_move_subtree, ESi, +1, "*v")
    CMD3( KEY_NONE, KEY_NONE,
          "mkd-move-subtree-up", do_mkd_move_subtree, ESi, -1, "*v")
    CMD2( KEY_META(KEY_RET), KEY_NONE,    /* Actually M-RET | C-c C-x m */
          "mkd-meta-return", do_mkd_meta_return, ES, "*")
    CMD2( KEY_ESC, KEY_LEFT,    /* actually M-left | C-c C-x l */
          "mkd-metaleft", do_mkd_metaleft, ES, "")
    CMD2( KEY_ESC, KEY_RIGHT,    /* actually M-right | C-c C-x r */
          "mkd-metaright", do_mkd_metaright, ES, "")
    CMD2( KEY_ESC, KEY_DOWN,    /* actually M-down | C-c C-x d */
          "mkd-metadown", do_mkd_metadown, ES, "")
    CMD2( KEY_ESC, KEY_UP,    /* actually M-up | C-c C-x u */
          "mkd-metaup", do_mkd_metaup, ES, "")
    CMD_DEF_END,
};

static int mkd_mode_init(EditState *s, ModeSavedData *saved_data)
{
    text_mode_init(s, saved_data);
    s->b->tab_width = 4;
    s->indent_tabs_mode = 0;
    s->wrap = WRAP_WORD;
    return 0;
}

static int mkd_init(void)
{
    memcpy(&mkd_mode, &text_mode, sizeof(ModeDef));
    mkd_mode.name = "markdown";
    mkd_mode.extensions = "mkd|md";
    mkd_mode.mode_probe = mkd_mode_probe;
    mkd_mode.mode_init = mkd_mode_init;
    mkd_mode.colorize_func = mkd_colorize_line;

    qe_register_mode(&mkd_mode);
    qe_register_cmd_table(mkd_commands, &mkd_mode);

    return 0;
}

qe_module_init(mkd_init);
