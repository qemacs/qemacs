/*
 * QEmacs, extra commands non full version
 *
 * Copyright (c) 2000-2008 Charlie Gordon.
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
    int from, to, offset, ch;

    from = to = s->offset;
    while (from > 0) {
        ch = eb_prevc(s->b, from, &offset);
        if (!qe_isblank(ch))
            break;
        from = offset;
    }
    while (to < s->b->total_size) {
        ch = eb_nextc(s->b, to, &offset);
        if (!qe_isblank(ch))
            break;
        to = offset;
    }
    s->offset = eb_delete_range(s->b, from, to);
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
    char buf[1024];
    int offset0, offset1, offset2, offset3;
    int size0, size1, size2;

    switch (cmd) {
    case CMD_TRANSPOSE_CHARS:
        offset3 = s->offset;
        eb_prevc(s->b, offset3, &offset2);
        offset1 = offset2;
        eb_prevc(s->b, offset1, &offset0);
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
            s->offset = offset0 + offset3 - offset2;
        } else {
            /* set position past last word (emacs behaviour) */
            s->offset = offset3;
        }
        break;
    case CMD_TRANSPOSE_LINES:
        do_eol(s);
        offset3 = s->offset;
        do_bol(s);
        offset2 = s->offset;
        if (offset2)
            s->offset--;
        offset1 = s->offset;
        do_bol(s);
        offset0 = s->offset;
        if (s->qe_state->flag_split_window_change_focus) {
            /* set position to start of second line */
            s->offset = offset0 + offset3 - offset1;
        } else {
            /* set position past second line (emacs behaviour) */
            s->offset = offset3;
        }
        break;
    default:
        return;
    }
    size0 = offset1 - offset0;
    size1 = offset2 - offset1;
    size2 = offset3 - offset2;
    if (size0 + size1 + size2 > ssizeof(buf)) {
        /* Should use temporary buffers */
        return;
    }
    eb_read(s->b, offset2, buf, size2);
    eb_read(s->b, offset1, buf + size2, size1);
    eb_read(s->b, offset0, buf + size2 + size1, size0);
    eb_write(s->b, offset0, buf, size0 + size1 + size2);
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
            qe_free(p);
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

static CmdDef extra_commands[] = {
    CMD_( KEY_META('='), KEY_NONE,
          "compare-windows", do_compare_windows, ESi, "ui" )
    CMD_( KEY_META('\\'), KEY_NONE,
          "delete-horizontal-space", do_delete_horizontal_space, ES, "*")
    CMD_( KEY_CTRLX('t'), KEY_NONE,
          "show-date-and-time", do_show_date_and_time, ESi, "ui")

          /* Should map to KEY_META + KEY_CTRL_LEFT */
    CMDV( KEY_META(KEY_CTRL('b')), KEY_NONE,
          "backward-block", do_forward_block, ESi, -1, "v")
          /* Should map to KEY_META + KEY_CTRL_RIGHT */
    CMDV( KEY_META(KEY_CTRL('f')), KEY_NONE,
          "forward-block", do_forward_block, ESi, 1, "v")
    CMDV( KEY_ESC, KEY_DELETE,
          "backward-kill-block", do_kill_block, ESi, -1, "*v")
    CMDV( KEY_META(KEY_CTRL('k')), KEY_NONE,
          "kill-block", do_kill_block, ESi, 1, "*v")
          /* Should also have mark-block on C-M-@ */

    CMDV( KEY_CTRL('t'), KEY_NONE,
          "transpose-chars", do_transpose, ESi, CMD_TRANSPOSE_CHARS, "*v")
    CMDV( KEY_CTRLX(KEY_CTRL('t')), KEY_NONE,
          "transpose-lines", do_transpose, ESi, CMD_TRANSPOSE_LINES, "*v")
    CMDV( KEY_META('t'), KEY_NONE,
          "transpose-words", do_transpose, ESi, CMD_TRANSPOSE_WORDS, "*v")

    CMDV( KEY_NONE, KEY_NONE,
          "global-unset-key", do_unset_key, ESsi, 0,
          "s{Unset key globally: }[key]"
	  "v")
    CMDV( KEY_NONE, KEY_NONE,
          "local-unset-key", do_unset_key, ESsi, 1,
          "s{Unset key locally: }[key]"
	  "v")

    CMD_DEF_END,
};

static int extras_init(void)
{
    int key;

    qe_register_cmd_table(extra_commands, NULL);
    for (key = KEY_META('0'); key <= KEY_META('9'); key++) {
        qe_register_binding(key, "universal-argument", NULL);
    }
    return 0;
}

qe_module_init(extras_init);
