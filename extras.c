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

void eb_tabify(EditBuffer *b, int p1, int p2)
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
     * new buffer reader eb_nextc_style() using with colorizer and a
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
        col += unicode_glyph_tty_width(c);
        if (c != ' ' || offset < start || col % tw == 0)
            continue;
        while (offset1 < stop) {
            c = eb_nextc(b, offset1, &offset2);
            if (c == ' ') {
                col += unicode_glyph_tty_width(c);
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

void do_tabify_buffer(EditState *s)
{
    /* deactivate region hilite */
    s->region_style = 0;

    eb_tabify(s->b, 0, s->b->total_size);
}

void do_tabify_region(EditState *s)
{
    /* deactivate region hilite */
    s->region_style = 0;

    eb_tabify(s->b, s->b->mark, s->offset);
}

void eb_untabify(EditBuffer *b, int p1, int p2)
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
            col += unicode_glyph_tty_width(c);
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

void do_untabify_buffer(EditState *s)
{
    /* deactivate region hilite */
    s->region_style = 0;

    eb_untabify(s->b, 0, s->b->total_size);
}

void do_untabify_region(EditState *s)
{
    /* deactivate region hilite */
    s->region_style = 0;

    eb_untabify(s->b, s->b->mark, s->offset);
}

void do_indent_region(EditState *s)
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

    if (t == 0)
        time(&t);

    put_status(s, "%.24s", ctime(&t));
}

/* forward / backward block */
#define MAX_LEVEL     20

static void do_forward_block(EditState *s, int dir)
{
    unsigned int buf[COLORED_MAX_LINE_SIZE];
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
    KeyDef *kd1 = mode ? mode->first_key : NULL;
    KeyDef *kd2 = inherit ? qe_state.first_key : NULL;

    out = buf_init(&outbuf, buf, size);
    pos = 0;
    for (;;) {
        KeyDef *kd = mode ? mode->first_key : qe_state.first_key;

        for (; kd != NULL; kd = kd->next) {
            if (kd->cmd == d
            &&  qe_find_binding(kd->keys, kd->nb_keys, 2, kd1, kd2) == kd) {
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
    int show;
    int total_size;

    b1 = new_help_buffer(&show);
    if (!b1)
        return;

    total_size = s->b->total_size;

    eb_printf(b1, "Buffer Statistics\n\n");

    eb_printf(b1, "        name: %s\n", b->name);
    eb_printf(b1, "    filename: %s\n", b->filename);
    eb_printf(b1, "    modified: %d\n", b->modified);
    eb_printf(b1, "  total_size: %d\n", total_size);
    if (total_size > 0) {
        int nb_chars, line, col;
        
        eb_get_pos(b, &line, &col, total_size);
        nb_chars = eb_get_char_offset(b, total_size);

        eb_printf(b1, "       lines: %d\n", line);
        eb_printf(b1, "       chars: %d\n", nb_chars);
    }
    eb_printf(b1, "        mark: %d\n", b->mark);
    eb_printf(b1, "      offset: %d\n", b->offset);

    eb_printf(b1, "   tab_width: %d\n", b->tab_width);
    eb_printf(b1, " fill_column: %d\n", b->fill_column);

    desc = buf_init(&descbuf, buf, countof(buf));
    if (b->eol_type == EOL_UNIX)
        buf_printf(desc, " unix");
    if (b->eol_type == EOL_DOS)
        buf_printf(desc, " dos");
    if (b->eol_type == EOL_MAC)
        buf_printf(desc, " mac");
        
    eb_printf(b1, "    eol_type: %d %s\n", b->eol_type, buf);
    eb_printf(b1, "     charset: %s (bytes=%d, shift=%d)\n",
              b->charset->name, b->char_bytes, b->char_shift);
    eb_printf(b1, "default_mode: %s, saved_mode: %s\n",
              b->default_mode ? b->default_mode->name : "",
              b->saved_mode ? b->saved_mode->name : "");

    desc = buf_init(&descbuf, buf, countof(buf));
    if (b->flags & BF_SAVELOG)
        buf_printf(desc, " SAVELOG");
    if (b->flags & BF_SYSTEM)
        buf_printf(desc, " SYSTEM");
    if (b->flags & BF_READONLY)
        buf_printf(desc, " READONLY");
    if (b->flags & BF_PREVIEW)
        buf_printf(desc, " PREVIEW");
    if (b->flags & BF_LOADING)
        buf_printf(desc, " LOADING");
    if (b->flags & BF_SAVING)
        buf_printf(desc, " SAVING");
    if (b->flags & BF_DIRED)
        buf_printf(desc, " DIRED");
    if (b->flags & BF_UTF8)
        buf_printf(desc, " UTF8");
    if (b->flags & BF_RAW)
        buf_printf(desc, " RAW");
    if (b->flags & BF_TRANSIENT)
        buf_printf(desc, " TRANSIENT");
    if (b->flags & BF_STYLES)
        buf_printf(desc, " STYLES");

    eb_printf(b1, "       flags: 0x%02x %s\n", b->flags, buf);
    eb_printf(b1, "      probed: %d\n", b->probed);

    eb_printf(b1, "   data_type: %s\n", b->data_type->name);
    eb_printf(b1, "       pages: %d\n", b->nb_pages);
    eb_printf(b1, " file_handle: %d\n", b->file_handle);

    eb_printf(b1, "    save_log: %d (new_index=%d, current=%d, nb_logs=%d)\n",
              b->save_log, b->log_new_index, b->log_current, b->nb_logs);
    eb_printf(b1, "      styles: %d (cur_style=%d, bytes=%d, shift=%d)\n",
              !!b->b_styles, b->cur_style, b->style_bytes, b->style_shift);

    if (total_size > 0) {
        u8 buf[4096];
        int count[256];
        int offset, c, i, col;
        
        memset(count, 0, sizeof(count));
        for (offset = 0; offset < total_size;) {
            int size = eb_read(b, offset, buf, countof(buf));
            for (i = 0; i < size; i++)
                count[buf[i]] += 1;
            offset += size;
        }
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
            col += eb_printf(b1, " %5d", count[i]);

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

    b1->flags |= BF_READONLY;
    if (show) {
        show_popup(b1);
    }
}

static CmdDef extra_commands[] = {
    CMD2( KEY_META('='), KEY_NONE,
          "compare-windows", do_compare_windows, ESi, "ui" )
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

    CMD2( KEY_NONE, KEY_NONE,
          "set-eol-type", do_set_eol_type, ESi,
    	  "ui{EOL Type [0=Unix, 1=Dos, 2=Mac]: }")
    CMD2( KEY_NONE, KEY_NONE,
          "describe-buffer", do_describe_buffer, ESi, "ui")

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
