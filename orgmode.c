/*
 * Org mode for QEmacs.
 *
 * Copyright (c) 2001-2002 Fabrice Bellard.
 * Copyright (c) 2002-2014 Charlie Gordon.
 * Copyright (c) 2014 Francois Revol.
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

ModeDef org_mode;

#define IN_BLOCK       0x80
#define IN_LISP        0x40
#define IN_TABLE       0x20

#define MAX_LEVEL       128

/* TODO: define specific styles */
static struct OrgTodoKeywords {
    const char *keyword;
    int style;
} OrgTodoKeywords [] = {
    { "TODO", QE_STYLE_STRING },
    { "DONE", QE_STYLE_TYPE },
};

/* TODO: define specific styles */
#define BULLET_STYLES 5
static int OrgBulletStyles[BULLET_STYLES] = {
    QE_STYLE_FUNCTION,
    QE_STYLE_STRING,
    QE_STYLE_VARIABLE,
    QE_STYLE_TYPE,
};

static int org_todo_keyword(const unsigned int *str)
{
    const unsigned int *p;
    int kw;

    for (kw = 0; kw < countof(OrgTodoKeywords); kw++) {
        if (ustrstart(str, OrgTodoKeywords[kw].keyword, &p) && *p == ' ')
            return kw;
    }
    return -1;
}

static int org_scan_chunk(const unsigned int *str,
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

static void org_colorize_line(unsigned int *str, int n, int mode_flags,
                              int *statep, __unused__ int state_only)
{
    int colstate = *statep;
    int i = 0, j = 0, kw, base_style = 0, has_space;

    if (colstate & IN_BLOCK) {
        for (j = i; str[j] == ' '; )
            j++;
        if (ustristart(str + j, "#+end_", NULL)) {
            colstate &= ~(IN_BLOCK | IN_LISP);
        } else {
            if (colstate & IN_LISP) {
                colstate &= ~(IN_LISP | IN_BLOCK);
                lisp_mode.colorize_func(str, n, 0, &colstate, state_only);
                colstate |= IN_LISP | IN_BLOCK;
            }
            *statep = colstate;
            return;
        }
    }

    if (str[i] == '*') {
        /* Check for heading: initial string of '*' followed by ' ' */
        for (j = i + 1; str[j] == '*'; j++)
            continue;

        if (str[j] == ' ') {
            base_style = OrgBulletStyles[(j - i - 1) % BULLET_STYLES];
            SET_COLOR(str, i, j + 1, base_style);
            i = j + 1;

            kw = org_todo_keyword(str + i);
            if (kw > -1) {
                j = i + strlen(OrgTodoKeywords[kw].keyword) + 1;
                SET_COLOR(str, i, j, OrgTodoKeywords[kw].style);
                i = j;
            }
        }
    } else {
        while (str[i] == ' ')
            i++;

        if (str[i] == '#') {
            if (str[i + 1] == ' ') {  /* [ \t]*[#][ ] -> comment */
                SET_COLOR(str, i, n, QE_STYLE_COMMENT);
                i = n;
            } else
            if (str[i + 1] == '+') {  /* [ \t]*[#][+] -> metadata */
                /* Should interpret litteral examples:
                 * #+BEGIN_xxx / #+END_xxx
                 * #+BEGIN_LATEX / #+END_LATEX
                 * #+BEGIN_SRC / #+END_SRC
                 */
                if (ustristart(str + i, "#+begin_", NULL)) {
                    colstate |= IN_BLOCK;
                    if (ustristr(str + i, "lisp")) {
                        colstate |= IN_LISP;
                    }
                }
                SET_COLOR(str, i, n, QE_STYLE_PREPROCESS);
                i = n;
            }
        } else
        if (str[i] == ':') {
            if (str[i + 1] == ' ') {
                /* code snipplet, should use code colorizer */
                SET_COLOR(str, i, n, QE_STYLE_FUNCTION);
                i = n;
            } else {
                /* property */
                SET_COLOR(str, i, n, QE_STYLE_KEYWORD);
                i = n;
            }
        } else
        if (str[i] == '-') {
            /* five or more dashes indicate a horizontal bar */
        } else
        if (str[i] == '|') {
            colstate |= IN_TABLE;
            base_style = QE_STYLE_TYPE;
        }
    }

    has_space = 1;

    for (;;) {
        int chunk = 0;
        int c = str[i];

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
                    SET_COLOR(str, i, i + 2, base_style);
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
            SET_COLOR(str, i, i + chunk, QE_STYLE_STRING);
            i += chunk;
        } else {
            SET_COLOR1(str, i, base_style);
            i++;
        }
    }

    colstate &= ~IN_TABLE;
    *statep = colstate;
}

static int org_is_header_line(EditState *s, int offset)
{
    /* Check if line starts with '*' */
    /* XXX: should ignore blocks using colorstate */
    return eb_nextc(s->b, eb_goto_bol(s->b, offset), &offset) == '*';
}

static int org_find_heading(EditState *s, int offset, int *level, int silent)
{
    int offset1, nb, c;

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
        put_status(s, "Before first heading");

    return -1;
}

static int org_next_heading(EditState *s, int offset, int target, int *level)
{
    int offset1, nb, c;

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
    int offset1, nb, c;

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
        put_status(s, "Already at top level of the outline");
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
        put_status(s, "No previous same-level heading");
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
        put_status(s, "No following same-level heading");
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
        nb = strtol(p, (char **)&p, 10);
        if (*p == '.')
            p++;
        level++;
        for (; nb > 0; nb--) {
            offset = org_next_heading(s, offset, level, &level1);
            if (level != level1) {
                put_status(s, "Heading not found");
                return;
            }
        }
    }
    if (level)
        s->offset = offset;
}

static void do_org_mark_element(EditState *s, int subtree)
{
    QEmacsState *qs = s->qe_state;
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
    if (s->qe_state->hilite_region)
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
        if (eb_match_str(s->b, offset, OrgTodoKeywords[kw].keyword, &offset1)
        &&  eb_match_uchar(s->b, offset1, ' ', &offset1)) {
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
        eb_insert_uchar(s->b, offset, ' ');
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
    offset1 = offset;
    while (eb_match_uchar(s->b, offset1, ' ', &offset1))
        continue;
    eb_delete(s->b, offset, offset1 - offset);

    while (level-- > 0) {
        offset += eb_insert_uchar(s->b, offset, '*');
    }
    offset += eb_insert_uchar(s->b, offset, ' ');
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
        eb_insert_uchar(s->b, offset, '*');
    } else
    if (dir > 0) {
        if (level > 1)
            eb_delete_uchar(s->b, offset);
        else
            put_status(s, "Cannot promote to level 0");
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
            eb_insert_uchar(s->b, offset, '*');
        } else
        if (dir > 0) {
            if (level > 1) {
                eb_delete_uchar(s->b, offset);
            } else {
                put_status(s, "Cannot promote to level 0");
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
        put_status(s, "Not on header line");
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
            put_status(s, "Cannot move substree");
            return;
        }
    } else {
        if (offset1 == s->b->total_size || level1 < level) {
            put_status(s, "Cannot move substree");
            return;
        }
        offset2 = org_next_heading(s, offset1, level, &level2);
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

static void do_org_meta_return(EditState *s)
{
    do_org_insert_heading(s, 0);
}

static void do_org_metaleft(EditState *s)
{
    if (org_is_header_line(s, s->offset))
        do_org_promote(s, +1);
    else
        do_word_right(s, -1);
}

static void do_org_metaright(EditState *s)
{
    if (org_is_header_line(s, s->offset))
        do_org_promote(s, -1);
    else
        do_word_right(s, +1);
}

static void do_org_metadown(EditState *s)
{
    do_org_move_subtree(s, +1);
}

static void do_org_metaup(EditState *s)
{
    do_org_move_subtree(s, -1);
}

static int org_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    /* just check file extension */
    if (match_extension(p->filename, mode->extensions))
        return 80;

    return 1;
}

/* Org mode specific commands */
static CmdDef org_commands[] = {
    /* Motion */
    CMD2( KEY_CTRLC(KEY_CTRL('n')), KEY_NONE,   /* C-c C-n */
          "outline-next-visible-heading", do_outline_next_vsible_heading, ES, "")
    CMD2( KEY_CTRLC(KEY_CTRL('p')), KEY_NONE,   /* C-c C-p */
          "outline-previous-visible-heading", do_outline_previous_vsible_heading, ES, "")
    CMD2( KEY_CTRLC(KEY_CTRL('u')), KEY_NONE,   /* C-c C-u */
          "outline-up-heading", do_outline_up_heading, ES, "")
    CMD2( KEY_CTRLC(KEY_CTRL('b')), KEY_NONE,   /* C-c C-b */
          "org-backward-same-level", do_org_backward_same_level, ES, "")
    CMD2( KEY_CTRLC(KEY_CTRL('f')), KEY_NONE,   /* C-c C-f */
          "org-forward-same-level", do_org_forward_same_level, ES, "")
    CMD2( KEY_CTRLC(KEY_CTRL('j')), KEY_NONE,   /* C-c C-j */
          "org-goto", do_org_goto, ESs,
          "s{select location to jump to: }[orgjump]|orgjump|")
    CMD3( KEY_META('h'), KEY_NONE,   /* M-h */
          "org-mark-element", do_org_mark_element, ESi, 0, "v")
    CMD3( KEY_CTRLC('@'), KEY_NONE,   /* C-c @ */
          "org-mark-subtree", do_org_mark_element, ESi, 1, "v")
    /* Editing */
    CMD2( KEY_CTRLC(KEY_CTRL('t')), KEY_NONE,   /* C-c C-t */
          "org-todo", do_org_todo, ES, "*")
    CMD3( KEY_NONE, KEY_NONE,    /* indirect through M-RET */
          "org-insert-heading", do_org_insert_heading, ESi, 0, "*v")
    CMD3( KEY_NONE, KEY_NONE,    /* actually M-S-RET and C-c C-x M */
          "org-insert-todo-heading", do_org_insert_heading, ESi, 1, "*v")
    CMD3( KEY_CTRL('j'), KEY_NONE,    /* actually C-RET */
          "org-insert-heading-respect-content", do_org_insert_heading, ESi, 2, "*v")
    CMD3( KEY_NONE, KEY_NONE,    /* actually C-S-RET */
          "org-insert-todo-heading-respect-content", do_org_insert_heading, ESi, 3, "*v")
    CMD3( KEY_NONE, KEY_NONE,
          "org-do-demote", do_org_promote, ESi, -1, "*v")
    CMD3( KEY_NONE, KEY_NONE,
          "org-do-promote", do_org_promote, ESi, +1, "*v")
    CMD3( KEY_CTRLX('>'), KEY_NONE,    /* actually M-S-right | C-c C-x R */
          "org-demote-subtree", do_org_promote_subtree, ESi, -1, "*v")
    CMD3( KEY_CTRLX('<'), KEY_NONE,    /* actually M-S-left | C-c C-x L */
          "org-promote-subtree", do_org_promote_subtree, ESi, +1, "*v")
    CMD3( KEY_NONE, KEY_NONE,
          "org-move-subtree-down", do_org_move_subtree, ESi, +1, "*v")
    CMD3( KEY_NONE, KEY_NONE,
          "org-move-subtree-up", do_org_move_subtree, ESi, -1, "*v")
    CMD2( KEY_META(KEY_RET), KEY_NONE,    /* Actually M-RET | C-c C-x m */
          "org-meta-return", do_org_meta_return, ES, "*")
    CMD2( KEY_ESC, KEY_LEFT,    /* actually M-left | C-c C-x l */
          "org-metaleft", do_org_metaleft, ES, "")
    CMD2( KEY_ESC, KEY_RIGHT,    /* actually M-right | C-c C-x r */
          "org-metaright", do_org_metaright, ES, "")
    CMD2( KEY_ESC, KEY_DOWN,    /* actually M-down | C-c C-x d */
          "org-metadown", do_org_metadown, ES, "")
    CMD2( KEY_ESC, KEY_UP,    /* actually M-up | C-c C-x u */
          "org-metaup", do_org_metaup, ES, "")
    CMD_DEF_END,
};

static int org_init(void)
{
    memcpy(&org_mode, &text_mode, sizeof(ModeDef));
    org_mode.name = "org";
    org_mode.extensions = "org";
    org_mode.mode_probe = org_mode_probe;
    org_mode.colorize_func = org_colorize_line;

    qe_register_mode(&org_mode);
    qe_register_cmd_table(org_commands, &org_mode);

    return 0;
}

qe_module_init(org_init);
