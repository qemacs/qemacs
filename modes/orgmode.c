/*
 * Org mode for QEmacs.
 *
 * Copyright (c) 2001-2002 Fabrice Bellard.
 * Copyright (c) 2002-2024 Charlie Gordon.
 * Copyright (c) 2014 Francois Revol.
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

enum {
    IN_ORG_BLOCK = 0x80,
    IN_ORG_LISP  = 0x40,
    IN_ORG_TABLE = 0x20,
};

enum {
    ORG_STYLE_TODO       = QE_STYLE_STRING,
    ORG_STYLE_DONE       = QE_STYLE_TYPE,
    ORG_STYLE_BULLET1    = QE_STYLE_FUNCTION,
    ORG_STYLE_BULLET2    = QE_STYLE_STRING,
    ORG_STYLE_BULLET3    = QE_STYLE_VARIABLE,
    ORG_STYLE_BULLET4    = QE_STYLE_TYPE,
    ORG_STYLE_COMMENT    = QE_STYLE_COMMENT,
    ORG_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    ORG_STYLE_CODE       = QE_STYLE_FUNCTION,
    ORG_STYLE_PROPERTY   = QE_STYLE_KEYWORD,
    ORG_STYLE_TABLE      = QE_STYLE_TYPE,
    ORG_STYLE_EMPHASIS   = QE_STYLE_STRING,
};

#define MAX_LEVEL       128

/* TODO: define specific styles */
static struct OrgTodoKeywords {
    const char *keyword;
    int style;
} OrgTodoKeywords [] = {
    { "TODO", ORG_STYLE_TODO },
    { "DONE", ORG_STYLE_DONE },
};

/* TODO: define specific styles */
#define BULLET_STYLES 5
static int OrgBulletStyles[BULLET_STYLES] = {
    ORG_STYLE_BULLET1,
    ORG_STYLE_BULLET2,
    ORG_STYLE_BULLET3,
    ORG_STYLE_BULLET4,
};

static int org_todo_keyword(const char32_t *str)
{
    int kw, len;

    for (kw = 0; kw < countof(OrgTodoKeywords); kw++) {
        if (ustrstart(str, OrgTodoKeywords[kw].keyword, &len) && str[len] == ' ')
            return kw;
    }
    return -1;
}

static int org_scan_chunk(const char32_t *str,
                          const char *begin, const char *end, int min_width)
{
    int i, j;

    for (i = 0; begin[i]; i++) {
        if (str[i] != (u8)begin[i])
            return 0;
    }
    for (j = 0; j < min_width; j++) {
        if (str[i + j] == '\0')
            return 0;
    }
    for (i += j; str[i] != '\0'; i++) {
        for (j = 0; end[j]; j++) {
            if (str[i + j] != (u8)end[j])
                break;
        }
        if (!end[j])
            return i + j;
    }
    return 0;
}

static void org_colorize_line(QEColorizeContext *cp,
                              const char32_t *str, int n,
                              QETermStyle *sbuf, ModeDef *syn)
{
    int colstate = cp->colorize_state;
    int i = 0, j = 0, kw, base_style = 0, has_space;

    if (colstate & IN_ORG_BLOCK) {
        for (j = i; str[j] == ' '; )
            j++;
        if (ustristart(str + j, "#+end_", NULL)) {
            colstate &= ~(IN_ORG_BLOCK | IN_ORG_LISP);
        } else {
            if (colstate & IN_ORG_LISP) {
                colstate &= ~(IN_ORG_LISP | IN_ORG_BLOCK);
                cp->colorize_state = colstate;
                cp_colorize_line(cp, str, 0, n, sbuf, &lisp_mode);
                colstate = cp->colorize_state;
                colstate |= IN_ORG_LISP | IN_ORG_BLOCK;
            }
            cp->colorize_state = colstate;
            return;
        }
    }

    if (str[i] == '*') {
        /* Check for heading: initial string of '*' followed by ' ' */
        for (j = i + 1; str[j] == '*'; j++)
            continue;

        if (str[j] == ' ') {
            base_style = OrgBulletStyles[(j - i - 1) % BULLET_STYLES];
            SET_STYLE(sbuf, i, j + 1, base_style);
            i = j + 1;

            kw = org_todo_keyword(str + i);
            if (kw > -1) {
                j = i + strlen(OrgTodoKeywords[kw].keyword) + 1;
                SET_STYLE(sbuf, i, j, OrgTodoKeywords[kw].style);
                i = j;
            }
        }
    } else {
        while (str[i] == ' ')
            i++;

        if (str[i] == '#') {
            if (str[i + 1] == ' ') {  /* [ \t]*[#][ ] -> comment */
                SET_STYLE(sbuf, i, n, ORG_STYLE_COMMENT);
                i = n;
            } else
            if (str[i + 1] == '+') {  /* [ \t]*[#][+] -> metadata */
                /* Should interpret litteral examples:
                 * #+BEGIN_xxx / #+END_xxx
                 * #+BEGIN_LATEX / #+END_LATEX
                 * #+BEGIN_SRC / #+END_SRC
                 */
                if (ustristart(str + i, "#+begin_", NULL)) {
                    colstate |= IN_ORG_BLOCK;
                    if (ustristr(str + i, "lisp")) {
                        colstate |= IN_ORG_LISP;
                    }
                }
                SET_STYLE(sbuf, i, n, ORG_STYLE_PREPROCESS);
                i = n;
            }
        } else
        if (str[i] == ':') {
            if (str[i + 1] == ' ') {
                /* code snipplet, should use code colorizer */
                SET_STYLE(sbuf, i, n, ORG_STYLE_CODE);
                i = n;
            } else {
                /* property */
                SET_STYLE(sbuf, i, n, ORG_STYLE_PROPERTY);
                i = n;
            }
        } else
        if (str[i] == '-') {
            /* five or more dashes indicate a horizontal bar */
        } else
        if (str[i] == '|') {
            colstate |= IN_ORG_TABLE;
            base_style = ORG_STYLE_TABLE;
        }
    }

    has_space = 1;

    for (;;) {
        int chunk = 0;
        char32_t c = str[i];

        if (c == '\0')
            break;

        if (has_space || c == '\\') {
            switch (c) {
            case '#':
                break;
            case '*':  /* bold */
                chunk = org_scan_chunk(str + i, "*", "*", 1);
                break;
            case '/':  /* italic */
                chunk = org_scan_chunk(str + i, "/", "/", 1);
                break;
            case '_':  /* underline */
                chunk = org_scan_chunk(str + i, "_", "_", 1);
                break;
            case '=':  /* code */
                chunk = org_scan_chunk(str + i, "=", "=", 1);
                break;
            case '~':  /* verbatim */
                chunk = org_scan_chunk(str + i, "~", "~", 1);
                break;
            case '+':  /* strike-through */
                chunk = org_scan_chunk(str + i, "+", "+", 1);
                break;
            case '@':  /* litteral stuff @@...@@ */
                chunk = org_scan_chunk(str + i, "@@", "@@", 1);
                break;
            case '[':  /* wiki syntax for links [[...]..[...]] */
                chunk = org_scan_chunk(str + i, "[[", "]]", 1);
                break;
            case '{': /* LaTeX syntax for macros {{{...}}} and {} */
                if (str[i + 1] == '}')
                    chunk = 2;
                else
                    chunk = org_scan_chunk(str + i, "{{{", "}}}", 1);
                break;
            case '\\':  /* TeX syntax: \keyword \- \[ \] \( \) */
                if (str[i + 1] == '\\') {  /* \\ escape */
                    SET_STYLE(sbuf, i, i + 2, base_style);
                    i += 2;
                    continue;
                }
                if (str[i + 1] == '-') {
                    chunk = 2;
                    break;
                }
                for (chunk = 1; qe_isalnum(str[i + chunk]); chunk++) {
                    continue;
                }
                if (chunk > 0)
                    break;
                chunk = org_scan_chunk(str + i, "\\(", "\\)", 1);
                if (chunk > 0)
                    break;
                chunk = org_scan_chunk(str + i, "\\[", "\\]", 1);
                if (chunk > 0)
                    break;
                break;
            case '-':  /* Colorize special glyphs -- and --- */
                if (str[i + 1] == '-') {
                    chunk = 2;
                    if (str[i + 2] == '-')
                        chunk++;
                    break;
                }
                break;
            case '.':  /* Colorize special glyph ... */
                if (str[i + 1] == '.' && str[i + 2] == '.') {
                    chunk = 3;
                    break;
                }
                break;
            case ' ':
                has_space = 1;
                break;
            default:
                has_space = 0;
                break;
            }
        } else {
            has_space = (str[i] == ' ');
        }
        if (chunk) {
            SET_STYLE(sbuf, i, i + chunk, ORG_STYLE_EMPHASIS);
            i += chunk;
        } else {
            SET_STYLE1(sbuf, i, base_style);
            i++;
        }
    }

    colstate &= ~IN_ORG_TABLE;
    cp->colorize_state = colstate;
}

static int org_is_header_line(EditState *s, int offset)
{
    /* Check if line starts with '*' */
    /* XXX: should ignore blocks using colorstate */
    return eb_nextc(s->b, eb_goto_bol(s->b, offset), &offset) == '*';
}

static int org_find_heading(EditState *s, int offset, int *level, int silent)
{
    int offset1, nb;
    char32_t c;

    offset = eb_goto_bol(s->b, offset);
    for (;;) {
        /* Find line starting with '*' */
        /* XXX: should ignore blocks using colorstate */
        if (eb_nextc(s->b, offset, &offset1) == '*') {
            for (nb = 1; (c = eb_nextc(s->b, offset1, &offset1)) == '*'; nb++)
                continue;
            if (c == ' ') {
                *level = nb;
                return offset;
            }
        }
        if (offset == 0)
            break;
        offset = eb_prev_line(s->b, offset);
    }
    if (!silent)
        put_error(s, "Before first heading");

    return -1;
}

static int org_next_heading(EditState *s, int offset, int target, int *level)
{
    int offset1, nb;
    char32_t c;

    for (;;) {
        offset = eb_next_line(s->b, offset);
        if (offset >= s->b->total_size) {
            nb = 0;
            break;
        }
        /* XXX: should ignore blocks using colorstate */
        if (eb_nextc(s->b, offset, &offset1) == '*') {
            for (nb = 1; (c = eb_nextc(s->b, offset1, &offset1)) == '*'; nb++)
                continue;
            if (c == ' ' && nb <= target) {
                break;
            }
        }
    }
    if (level)
        *level = nb;
    return offset;
}

static int org_prev_heading(EditState *s, int offset, int target, int *level)
{
    int offset1, nb;
    char32_t c;

    for (;;) {
        if (offset == 0) {
            nb = 0;
            break;
        }
        offset = eb_prev_line(s->b, offset);
        /* XXX: should ignore blocks using colorstate */
        if (eb_nextc(s->b, offset, &offset1) == '*') {
            for (nb = 1; (c = eb_nextc(s->b, offset1, &offset1)) == '*'; nb++)
                continue;
            if (c == ' ' && nb <= target) {
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
    s->offset = org_next_heading(s, s->offset, MAX_LEVEL, NULL);
}

static void do_outline_previous_vsible_heading(EditState *s)
{
    s->offset = org_prev_heading(s, s->offset, MAX_LEVEL, NULL);
}

static void do_outline_up_heading(EditState *s)
{
    int offset, level;

    offset = org_find_heading(s, s->offset, &level, 0);
    if (offset < 0)
        return;

    if (level <= 1) {
        put_error(s, "Already at top level of the outline");
        return;
    }

    s->offset = org_prev_heading(s, offset, level - 1, &level);
}

static void do_org_backward_same_level(EditState *s)
{
    int offset, level, level1;

    offset = org_find_heading(s, s->offset, &level, 0);
    if (offset < 0)
        return;

    offset = org_prev_heading(s, offset, level, &level1);
    if (level1 != level) {
        put_error(s, "No previous same-level heading");
        return;
    }
    s->offset = offset;
}

static void do_org_forward_same_level(EditState *s)
{
    int offset, level, level1;

    offset = org_find_heading(s, s->offset, &level, 0);
    if (offset < 0)
        return;

    offset = org_next_heading(s, offset, level, &level1);
    if (level1 != level) {
        put_error(s, "No following same-level heading");
        return;
    }
    s->offset = offset;
}

static void do_org_goto(EditState *s, const char *dest)
{
    int offset, level, level1, nb;
    const char *p = dest;

    /* XXX: Should pop up a window with numbered outline index
     * and let the user select the target interactively.
     */

    /* Jump to numbered destination. */
    for (offset = 0, level = 0; qe_isdigit(*p); ) {
        nb = strtol_c(p, &p, 10);
        if (*p == '.')
            p++;
        level++;
        for (; nb > 0; nb--) {
            offset = org_next_heading(s, offset, level, &level1);
            if (level != level1) {
                put_error(s, "Heading not found");
                return;
            }
        }
    }
    if (level)
        s->offset = offset;
}

static void do_org_mark_element(EditState *s, int subtree)
{
    QEmacsState *qs = s->qs;
    int offset, offset1, level;

    offset = org_find_heading(s, s->offset, &level, 0);
    if (offset < 0)
        return;

    offset1 = org_next_heading(s, offset, subtree ? level : MAX_LEVEL, NULL);

    /* XXX: if repeating last command, add subtree to region */
    if (qs->last_cmd_func != qs->this_cmd_func)
        s->b->mark = offset;

    s->offset = offset1;
    /* activate region hilite */
    if (s->qs->hilite_region)
        s->region_style = QE_STYLE_REGION_HILITE;
}

static void do_org_todo(EditState *s)
{
    int offset, offset1, bullets, kw;

    if (check_read_only(s))
        return;

    offset = org_find_heading(s, s->offset, &bullets, 0);
    if (offset < 0)
        return;

    offset = eb_skip_chars(s->b, offset, bullets + 1);
    for (kw = 0; kw < countof(OrgTodoKeywords); kw++) {
        if (eb_match_str_utf8(s->b, offset, OrgTodoKeywords[kw].keyword, &offset1)
        &&  eb_match_char32(s->b, offset1, ' ', &offset1)) {
            eb_delete_range(s->b, offset, offset1);
            break;
        }
    }
    if (kw == countof(OrgTodoKeywords))
        kw = 0;
    else
        kw++;

    if (kw < countof(OrgTodoKeywords)) {
        offset += eb_insert_str(s->b, offset, OrgTodoKeywords[kw].keyword);
        eb_insert_char32(s->b, offset, ' ');
    }
}

static void do_org_insert_heading(EditState *s, int flags)
{
    int offset, offset0, offset1, level = 1;

    if (check_read_only(s))
        return;

    offset = org_find_heading(s, s->offset, &level, 1);
    offset0 = eb_goto_bol(s->b, s->offset);
    offset1 = eb_goto_eol(s->b, s->offset);

    /* if at beginning of heading line, insert sibling heading before,
     * if in the middle of a heading line, split the heading,
     * otherwise, make the current line a heading line at current level.
     */
    if (flags & 2) {
        /* respect-content: insert heading at end of subtree */
        offset = org_next_heading(s, offset, level, NULL);
        eb_insert_char32_n(s->b, offset, '\n', 2);
    } else
    if (s->offset <= offset + level + 1) {
        eb_insert_char32(s->b, offset, '\n');
    } else
    if (offset == offset0 || offset == offset1) {
        offset = s->offset;
        offset += eb_insert_char32(s->b, offset, '\n');
    } else {
        offset = offset0;
    }
    offset1 = offset;
    while (eb_match_char32(s->b, offset1, ' ', &offset1))
        continue;
    eb_delete(s->b, offset, offset1 - offset);

    offset += eb_insert_char32_n(s->b, offset, '*', level);
    offset += eb_insert_char32(s->b, offset, ' ');
    s->offset = eb_goto_eol(s->b, offset);
    if (flags & 1) {
        /* insert-todo-heading */
        do_org_todo(s);
    }
}

static void do_org_promote(EditState *s, int dir)
{
    int offset, level;

    if (check_read_only(s))
        return;

    offset = org_find_heading(s, s->offset, &level, 0);
    if (offset < 0)
        return;

    if (dir < 0) {
        eb_insert_char32(s->b, offset, '*');
    } else
    if (dir > 0) {
        if (level > 1)
            eb_delete_char32(s->b, offset);
        else
            put_error(s, "Cannot promote to level 0");
    }
}

static void do_org_promote_subtree(EditState *s, int dir)
{
    int offset, level, level1;

    if (check_read_only(s))
        return;

    offset = org_find_heading(s, s->offset, &level, 0);
    if (offset < 0)
        return;

    for (;;) {
        if (dir < 0) {
            eb_insert_char32(s->b, offset, '*');
        } else
        if (dir > 0) {
            if (level > 1) {
                eb_delete_char32(s->b, offset);
            } else {
                put_error(s, "Cannot promote to level 0");
                return;
            }
        }
        offset = org_next_heading(s, offset, MAX_LEVEL, &level1);
        if (level1 <= level)
            break;
    }
}

static void do_org_move_subtree(EditState *s, int dir)
{
    int offset, offset1, offset2, level, level1, level2, size;
    EditBuffer *b1;

    if (check_read_only(s))
        return;

    if (!org_is_header_line(s, s->offset)) {
        put_error(s, "Not on header line");
        return;
    }

    offset = org_find_heading(s, s->offset, &level, 0);
    if (offset < 0)
        return;

    offset1 = org_next_heading(s, offset, level, &level1);
    size = offset1 - offset;

    if (dir < 0) {
        offset2 = org_prev_heading(s, offset, level, &level2);
        if (level2 < level) {
            put_error(s, "Cannot move substree");
            return;
        }
    } else {
        if (offset1 == s->b->total_size || level1 < level) {
            put_error(s, "Cannot move substree");
            return;
        }
        offset2 = org_next_heading(s, offset1, level, &level2);
    }
    // XXX: should have a way to move buffer contents
    b1 = qe_new_buffer(s->qs, "*tmp*", BF_SYSTEM | (s->b->flags & BF_STYLES));
    if (!b1)
        return;
    eb_set_charset(b1, s->b->charset, s->b->eol_type);
    eb_insert_buffer_convert(b1, 0, s->b, offset, size);
    eb_delete(s->b, offset, size);
    if (offset2 > offset)
        offset2 -= size;
    eb_insert_buffer_convert(s->b, offset2, b1, 0, b1->total_size);
    eb_free(&b1);
    s->offset = offset2;
}

static void do_org_meta_return(EditState *s)
{
    do_org_insert_heading(s, 0);
}

static void do_org_metaleft(EditState *s)
{
    if (org_is_header_line(s, s->offset))
        do_org_promote(s, +1);
    else
        do_word_left_right(s, -1);
}

static void do_org_metaright(EditState *s)
{
    if (org_is_header_line(s, s->offset))
        do_org_promote(s, -1);
    else
        do_word_left_right(s, +1);
}

static void do_org_metadown(EditState *s)
{
    do_org_move_subtree(s, +1);
}

static void do_org_metaup(EditState *s)
{
    do_org_move_subtree(s, -1);
}

/* Org mode specific commands */
static const CmdDef org_commands[] = {
    /* Motion */
    CMD0( "outline-next-visible-heading", "C-c C-n",
          "",
          do_outline_next_vsible_heading)
    CMD0( "outline-previous-visible-heading", "C-c C-p",
          "",
          do_outline_previous_vsible_heading)
    CMD0( "outline-up-heading", "C-c C-u",
          "",
          do_outline_up_heading)
    CMD0( "org-backward-same-level", "C-c C-b",
          "",
          do_org_backward_same_level)
    CMD0( "org-forward-same-level", "C-c C-f",
          "",
          do_org_forward_same_level)
    CMD2( "org-goto", "C-c C-j",
          "",
          do_org_goto, ESs,
          "s{select location to jump to: }[orgjump]|orgjump|")
    CMD3( "org-mark-element", "M-h",
          "",
          do_org_mark_element, ESi, "v", 0)
    CMD3( "org-mark-subtree", "C-c @",
          "",
          do_org_mark_element, ESi, "v", 1)
    /* Editing */
    CMD2( "org-todo", "C-c C-t",
          "",
          do_org_todo, ES, "*")
    CMD3( "org-insert-heading", "", /* indirect through M-RET */
          "",
          do_org_insert_heading, ESi, "*" "v", 0)
    CMD3( "org-insert-todo-heading", "", /* actually M-S-RET and C-c C-x M */
          "",
          do_org_insert_heading, ESi, "*" "v", 1)
    CMD3( "org-insert-heading-respect-content", "C-j, C-RET", /* actually C-RET */
          "",
          do_org_insert_heading, ESi, "*" "v", 2)
    CMD3( "org-insert-todo-heading-respect-content", "", /* actually C-S-RET */
          "",
          do_org_insert_heading, ESi, "*" "v", 3)
    CMD3( "org-do-demote", "",
          "",
          do_org_promote, ESi, "*" "v", -1)
    CMD3( "org-do-promote", "",
          "",
          do_org_promote, ESi, "*" "v", +1)
    CMD3( "org-demote-subtree", "C-x >", /* actually M-S-right | C-c C-x R */
          "",
          do_org_promote_subtree, ESi, "*" "v", -1)
    CMD3( "org-promote-subtree", "C-x <", /* actually M-S-left | C-c C-x L */
          "",
          do_org_promote_subtree, ESi, "*" "v", +1)
    CMD3( "org-move-subtree-down", "",
          "",
          do_org_move_subtree, ESi, "*" "v", +1)
    CMD3( "org-move-subtree-up", "",
          "",
          do_org_move_subtree, ESi, "*" "v", -1)
    CMD2( "org-meta-return", "M-RET", /* Actually M-RET | C-c C-x m */
          "",
          do_org_meta_return, ES, "*")
    CMD0( "org-metaleft", "ESC left", /* actually M-left | C-c C-x l */
          "",
          do_org_metaleft)
    CMD0( "org-metaright", "ESC right", /* actually M-right | C-c C-x r */
          "",
          do_org_metaright)
    CMD0( "org-metadown", "ESC down", /* actually M-down | C-c C-x d */
          "",
          do_org_metadown)
    CMD0( "org-metaup", "ESC up", /* actually M-up | C-c C-x u */
          "",
          do_org_metaup)
};

static ModeDef org_mode = {
    .name = "org",
    .extensions = "org",
    .colorize_func = org_colorize_line,
};

static int org_init(QEmacsState *qs)
{
    qe_register_mode(qs, &org_mode, MODEF_SYNTAX);
    qe_register_commands(qs, &org_mode, org_commands, countof(org_commands));

    return 0;
}

qe_module_init(org_init);
