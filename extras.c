/*
 * QEmacs, extra commands non full version
 *
 * Copyright (c) 2000-2014 Charlie Gordon.
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

void do_compare_windows(EditState *s, int argval)
{
    QEmacsState *qs = s->qe_state;
    EditState *s1;
    EditState *s2;
    int offset1, offset2, size1, size2, ch1, ch2;
    int tries;

    s1 = s;
    /* Should use same internal function as for next_window */
    if (s1->next_window)
        s2 = s1->next_window;
    else
        s2 = qs->first_window;

    if (argval)
        qs->ignore_spaces ^= 1;

    if (s1 == s2)
        return;

    size1 = s1->b->total_size;
    size2 = s2->b->total_size;

    if (qs->last_cmd_func == (CmdFunc)do_compare_windows
    &&  (eb_nextc(s1->b, s1->offset, &offset1) !=
         eb_nextc(s2->b, s2->offset, &offset2))) {
        /* Try to resync: just skip in parallel */
        s1->offset = offset1;
        s2->offset = offset2;
    }

    for (tries = 0;;) {

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
                if (qe_isspace(ch1)) {
                    s1->offset = offset1;
                    continue;
                }
                if (qe_isspace(ch2)) {
                    s2->offset = offset2;
                    continue;
                }
            }
            if (ch1 == EOF || ch2 == EOF)
                put_status(s, "Extra characters");
            else
                put_status(s, "Difference: %c <-> %c", ch1, ch2);
            break;
        }
        if (ch1 != EOF) {
            s1->offset = offset1;
            s2->offset = offset2;
            continue;
        }
        put_status(s, "No difference");
        break;
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

void do_delete_blank_lines(EditState *s)
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

void do_show_date_and_time(EditState *s, int argval)
{
    time_t t = argval;

    if (t == 0)
        time(&t);

    put_status(s, "%.24s", ctime(&t));
}

/* forward / backward block */
#define MAX_BUF_SIZE  512
#define MAX_LEVEL     20

static void do_forward_block(EditState *s, int dir)
{
    unsigned int buf[MAX_BUF_SIZE];
    char balance[MAX_LEVEL];
    int line_num, col_num, offset, offset1, len, pos, style, c, c1, level;

    eb_get_pos(s->b, &line_num, &col_num, s->offset);
    offset = eb_goto_bol2(s->b, s->offset, &pos);
    offset1 = offset;
    len = s->get_colorized_line(s, buf, countof(buf), &offset1, line_num);
    style = buf[pos] >> STYLE_SHIFT;
    level = 0;

    if (dir < 0) {
        for (;;) {
            if (pos == 0) {
                if (offset <= 0)
                    break;
                line_num--;
                offset = eb_prev_line(s->b, offset);
                offset1 = offset;
                pos = s->get_colorized_line(s, buf, countof(buf), &offset1, line_num);
                continue;
            }
            c = buf[--pos];
            if (style != c >> STYLE_SHIFT) {
                if (style == 0)
                    continue;
                style = 0;
                if ((c >> STYLE_SHIFT) != 0)
                    continue;
            }
            switch (c &= CHAR_MASK) {
            case ')':
                c1 = '(';
                goto push;
            case ']':
                c1 = '[';
                goto push;
            case '}':
                c1 = '{';
            push:
                if (level < MAX_LEVEL) {
                    balance[level] = c1;
                }
                level++;
                break;
            case '(':
            case '[':
            case '{':
                if (level > 0) {
                    --level;
                    if (balance[level] != c) {
                        put_status(s, "Unmatched delimiter");
                        return;
                    }
                    if (level <= 0)
                        goto the_end;
                }
                break;
            }
        }
    } else {
        for (;;) {
            if (pos >= len) {
                /* Should simplify with get_colorized_line updating
                 * offset
                 */
                line_num++;
                pos = 0;
                offset = eb_next_line(s->b, offset);
                if (offset >= s->b->total_size)
                    break;
                offset1 = offset;
                len = s->get_colorized_line(s, buf, countof(buf), &offset1, line_num);
                continue;
            }
            c = buf[pos];
            pos++;
            if (style != c >> STYLE_SHIFT) {
                if (style == 0)
                    continue;
                style = 0;
                if ((c >> STYLE_SHIFT) != 0)
                    continue;
            }
            switch (c &= CHAR_MASK) {
            case '(':
                c1 = ')';
                goto push1;
            case '[':
                c1 = ']';
                goto push1;
            case '{':
                c1 = '}';
            push1:
                if (level < MAX_LEVEL) {
                    balance[level] = c1;
                }
                level++;
                break;
            case ')':
            case ']':
            case '}':
                if (level > 0) {
                    --level;
                    if (balance[level] != c) {
                        put_status(s, "Unmatched delimiter");
                        return;
                    }
                    if (level <= 0)
                        goto the_end;
                }
                break;
            }
        }
    }
the_end:
    while (pos > 0) {
        eb_nextc(s->b, offset, &offset);
        pos--;
    }
    s->offset = offset;
}

static void do_kill_block(EditState *s, int dir)
{
    int start = s->offset;

    if (s->b->flags & BF_READONLY)
        return;

    do_forward_block(s, dir);
    do_kill(s, start, s->offset, dir);
}

enum {
    CMD_TRANSPOSE_CHARS = 1,
    CMD_TRANSPOSE_WORDS,
    CMD_TRANSPOSE_LINES,
};

static void do_transpose(EditState *s, int cmd)
{
    int offset0, offset1, offset2, offset3, end_offset;
    int size0, size1, size2;
    EditBuffer *b = s->b;

    if (check_read_only(s))
        return;

    switch (cmd) {
    case CMD_TRANSPOSE_CHARS:
        /* at end of line, transpose previous 2 characters,
         * otherwise transpose characters before and after point.
         */
        end_offset = offset2 = s->offset;
        if (eb_nextc(b, offset2, &offset3) == '\n') {
            offset3 = offset2;
            eb_prevc(b, offset3, &offset2);
            end_offset = s->offset;
            offset1 = offset2;
            eb_prevc(b, offset1, &offset0);
        } else {
            offset1 = offset2;
            eb_prevc(b, offset1, &offset0);
            if (s->qe_state->flag_split_window_change_focus) {
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
        if (s->qe_state->flag_split_window_change_focus) {
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
        eb_prevc(b, offset2, &offset1);    /* skip line feed */
        s->offset = offset1;
        do_bol(s);
        offset0 = s->offset;
        if (s->qe_state->flag_split_window_change_focus) {
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

        eb_set_charset(b1, b->charset);
        eb_insert_buffer_convert(b1, 0, b, offset2, size2);
        eb_insert_buffer_convert(b1, size2, b, offset1, size1);
        eb_insert_buffer_convert(b1, size2 + size1, b, offset0, size0);
        /* XXX: This will create 2 undo records */
        eb_delete(b, offset0, size0 + size1 + size2);
        eb_insert_buffer_convert(b, offset0, b1, 0, size0 + size1 + size2);
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
            if (kd->cmd == d) {
                if (out->len > pos)
                    buf_puts(out, ", ");

                buf_put_keys(out, kd->keys, kd->nb_keys);
            }
        }
        if (!inherit || !mode)
            break;
        /* Should move up to base mode */
        mode = NULL;
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
                           __unused__ int type, ModeDef *mode)
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
    int show;

    b = new_help_buffer(&show);
    if (!b)
        return;

    print_bindings(b, NULL, 0, s->mode);
    print_bindings(b, "\nGlobal bindings", 0, NULL);

    b->flags |= BF_READONLY;
    if (show) {
        show_popup(b);
    }
}

void do_apropos(EditState *s, const char *str)
{
    QEmacsState *qs = s->qe_state;
    char buf[256];
    EditBuffer *b;
    CmdDef *d;
    VarDef *vp;
    int found, show;

    b = new_help_buffer(&show);
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
        if (show) {
            show_popup(b);
        }
    } else {
        if (show)
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
    show_popup(b);
}

static void do_set_region_color(EditState *s, const char *str)
{
    int offset, size, style;

    /* deactivate region hilite */
    s->region_style = 0;

    style = get_tty_style(str);
    if (style < 0) {
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
        eb_create_style_buffer(s->b, BF_STYLE2);
        eb_set_style(s->b, style, LOGOP_WRITE, offset, size);
    }
}

static void do_set_region_style(EditState *s, const char *str)
{
    int offset, size, style;
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
        eb_create_style_buffer(s->b, BF_STYLE2);
        eb_set_style(s->b, style, LOGOP_WRITE, offset, size);
    }
}

static void do_drop_styles(EditState *s)
{
    eb_free_style_buffer(s->b);
    s->b->flags &= ~BF_STYLES;
}

static CmdDef extra_commands[] = {
    CMD2( KEY_META('='), KEY_NONE,
          "compare-windows", do_compare_windows, ESi, "ui" )
    CMD2( KEY_META('\\'), KEY_NONE,
          "delete-horizontal-space", do_delete_horizontal_space, ES, "*")
    CMD2( KEY_CTRLX(KEY_CTRL('o')), KEY_NONE,
          "delete-blank-lines", do_delete_blank_lines, ES, "*")
    CMD2( KEY_CTRLX('t'), KEY_NONE,
          "show-date-and-time", do_show_date_and_time, ESi, "ui")

          /* Should map to KEY_META + KEY_CTRL_LEFT */
    CMD3( KEY_META(KEY_CTRL('b')), KEY_NONE,
          "backward-block", do_forward_block, ESi, -1, "v")
          /* Should map to KEY_META + KEY_CTRL_RIGHT */
    CMD3( KEY_META(KEY_CTRL('f')), KEY_NONE,
          "forward-block", do_forward_block, ESi, 1, "v")
    CMD3( KEY_ESC, KEY_DELETE,
          "backward-kill-block", do_kill_block, ESi, -1, "*v")
    CMD3( KEY_META(KEY_CTRL('k')), KEY_NONE,
          "kill-block", do_kill_block, ESi, 1, "*v")
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
    CMD2( KEY_CTRLH('a'), KEY_NONE,
          "apropos", do_apropos, ESs,
	  "s{Apropos: }|apropos|")
    CMD0( KEY_CTRLH('b'), KEY_NONE,
          "describe-bindings", do_describe_bindings)
    CMD2( KEY_CTRLH('B'), KEY_NONE,
          "show-bindings", do_show_bindings, ESs,
	  "s{Show bindings of command: }[command]|command|")

    CMD2( KEY_CTRLC('c'), KEY_NONE,
          "set-region-color", do_set_region_color, ESs,
	  "s{Select color: }[color]|color|")
    CMD2( KEY_CTRLC('s'), KEY_NONE,
          "set-region-style", do_set_region_style, ESs,
	  "s{Select style: }[style]|style|")
    CMD0( KEY_NONE, KEY_NONE,
          "drop-styles", do_drop_styles)

    CMD_DEF_END,
};

static int extras_init(void)
{
    int key;

    qe_register_cmd_table(extra_commands, NULL);
    for (key = KEY_META('0'); key <= KEY_META('9'); key++) {
        qe_register_binding(key, "numeric-argument", NULL);
    }
    return 0;
}

qe_module_init(extras_init);
