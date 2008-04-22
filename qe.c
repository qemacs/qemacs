/*
 * QEmacs, tiny but powerful multimode editor
 *
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard.
 * Copyright (c) 2000-2008 Charlie Gordon.
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

#ifdef CONFIG_TINY
#undef CONFIG_DLL
#undef CONFIG_ALL_KMAPS
#undef CONFIG_UNICODE_JOIN
#endif

#include "qfribidi.h"

#include "variables.h"

#ifdef CONFIG_DLL
#include <dlfcn.h>
#endif

/* each history list */
typedef struct HistoryEntry {
    struct HistoryEntry *next;
    StringArray history;
    char name[32];
} HistoryEntry;

#ifdef CONFIG_INIT_CALLS
static int (*__initcall_first)(void) __init_call = NULL;
static void (*__exitcall_first)(void) __exit_call = NULL;
#endif

static int get_line_height(QEditScreen *screen, int style_index);
void print_at_byte(QEditScreen *screen,
                   int x, int y, int width, int height,
                   const char *str, int style_index);
static void get_default_path(EditState *s, char *buf, int buf_size);
static EditBuffer *predict_switch_to_buffer(EditState *s);
static StringArray *get_history(const char *name);
static void qe_key_process(int key);

ModeSavedData *generic_mode_save_data(EditState *s);
static void generic_text_display(EditState *s);
static void display1(DisplayState *s);
#ifndef CONFIG_TINY
static void save_selection(void);
#endif
static CompletionFunc find_completion(const char *name);

QEmacsState qe_state;
/* should handle multiple screens, and multiple sessions */
static QEditScreen global_screen;
static int screen_width = 0;
static int screen_height = 0;
static int no_init_file;
static const char *user_option;

/* mode handling */

void qe_register_mode(ModeDef *m)
{
    QEmacsState *qs = &qe_state;
    ModeDef **p;
    CmdDef *def;

    /* register mode in mode list (at end) */
    p = &qs->first_mode;
    while (*p != NULL)
        p = &(*p)->next;
    m->next = NULL;
    *p = m;

    /* add missing functions */
    if (!m->display)
        m->display = generic_text_display;
    if (!m->mode_save_data)
        m->mode_save_data = generic_mode_save_data;
    if (!m->data_type)
        m->data_type = &raw_data_type;
    if (!m->get_mode_line)
        m->get_mode_line = text_mode_line;

    /* add a new command to switch to that mode */
    if (!(m->mode_flags & MODEF_NOCMD)) {
        char buf[64];
        int size;

        /* lower case convert for C mode, Perl... */
        qe_strtolower(buf, sizeof(buf) - 10, m->name);
        pstrcat(buf, sizeof(buf), "-mode");
        size = strlen(buf) + 1;
        /* constant immediate string parameter */
        size += snprintf(buf + size, sizeof(buf) - size,
                         "S{%s}", m->name) + 1;
        def = qe_mallocz_array(CmdDef, 2);
        def->name = qe_malloc_dup(buf, size);
        def->key = def->alt_key = KEY_NONE;
        def->sig = CMD_ESs;
        def->val = 0;
        def->action.ESs = do_set_mode;
        qe_register_cmd_table(def, NULL);
    }
}

void mode_completion(CompleteState *cp)
{
    QEmacsState *qs = cp->s->qe_state;
    ModeDef *m;

    for (m = qs->first_mode; m != NULL; m = m->next) {
        complete_test(cp, m->name);
    }
}

static ModeDef *find_mode(const char *name)
{
    QEmacsState *qs = &qe_state;
    ModeDef *m;

    for (m = qs->first_mode; m != NULL; m = m->next) {
        if (strequal(m->name, name))
            return m;
    }
    return NULL;
}

/* commands handling */

CmdDef *qe_find_cmd(const char *cmd_name)
{
    QEmacsState *qs = &qe_state;
    CmdDef *d;

    d = qs->first_cmd;
    while (d != NULL) {
        while (d->name != NULL) {
            if (strequal(cmd_name, d->name))
                return d;
            d++;
        }
        d = d->action.next;
    }
    return NULL;
}

void command_completion(CompleteState *cp)
{
    QEmacsState *qs = cp->s->qe_state;
    CmdDef *d;

    d = qs->first_cmd;
    while (d != NULL) {
        while (d->name != NULL) {
            complete_test(cp, d->name);
            d++;
        }
        d = d->action.next;
    }
}

static int qe_register_binding1(unsigned int *keys, int nb_keys,
                                CmdDef *d, ModeDef *m)
{
    QEmacsState *qs = &qe_state;
    KeyDef **lp, *p;
    int i;

    if (!d)
        return -1;

    /* add key */
    p = qe_malloc_hack(KeyDef, (nb_keys - 1) * sizeof(p->keys[0]));
    if (!p)
        return -1;
    p->cmd = d;
    p->nb_keys = nb_keys;
    for (i = 0; i < nb_keys; i++) {
        p->keys[i] = keys[i];
    }
    lp = m ? &m->first_key : &qs->first_key;
    /* Bindings must be prepended to override previous bindings */
#if 0
    while (*lp != NULL && (*lp)->mode != NULL)
        lp = &(*lp)->next;
#endif
    p->next = *lp;
    *lp = p;
    return 0;
}

/* convert compressed mappings to real ones */
static int qe_register_binding2(int key, CmdDef *d, ModeDef *m)
{
    int nb_keys;
    unsigned int keys[3];

    nb_keys = 0;
    if (key >= KEY_CTRLX(0) && key <= KEY_CTRLX(0xff)) {
        keys[nb_keys++] = KEY_CTRL('x');
        keys[nb_keys++] = key & 0xff;
    } else
    if (key >= KEY_CTRLXRET(0) && key <= KEY_CTRLXRET(0xff)) {
        keys[nb_keys++] = KEY_CTRL('x');
        keys[nb_keys++] = KEY_RET;
        keys[nb_keys++] = key & 0xff;
    } else
    if (key >= KEY_CTRLH(0) && key <= KEY_CTRLH(0xff)) {
        keys[nb_keys++] = KEY_CTRL('h');
        keys[nb_keys++] = key & 0xff;
    } else {
        keys[nb_keys++] = key;
    }
    return qe_register_binding1(keys, nb_keys, d, m);
}

/* if mode is non NULL, the defined keys are only active in this mode */
void qe_register_cmd_table(CmdDef *cmds, ModeDef *m)
{
    QEmacsState *qs = &qe_state;
    CmdDef **ld, *d;

    /* find last command table */
    for (ld = &qs->first_cmd;;) {
        d = *ld;
        if (d == NULL) {
            /* link new command table */
            *ld = cmds;
            break;
        }
        if (d == cmds) {
            /* Command table already registered, still do the binding
             * phase to allow multiple mode bindings.
             */
            break;
        }
        while (d->name != NULL) {
            d++;
        }
        ld = &d->action.next;
    }

    /* add default bindings */
    for (d = cmds; d->name != NULL; d++) {
        if (d->key == KEY_CTRL('x') || d->key == KEY_ESC) {
            unsigned int keys[2];
            keys[0] = d->key;
            keys[1] = d->alt_key;
            qe_register_binding1(keys, 2, d, m);
        } else {
            if (d->key != KEY_NONE)
                qe_register_binding2(d->key, d, m);
            if (d->alt_key != KEY_NONE)
                qe_register_binding2(d->alt_key, d, m);
        }
    }
}

/* key binding handling */

int qe_register_binding(int key, const char *cmd_name, ModeDef *m)
{
    return qe_register_binding2(key, qe_find_cmd(cmd_name), m);
}

void do_set_key(EditState *s, const char *keystr,
                const char *cmd_name, int local)
{
    unsigned int keys[MAX_KEYS];
    int nb_keys;
    CmdDef *d;

    nb_keys = strtokeys(keystr, keys, MAX_KEYS);
    if (!nb_keys)
        return;

    d = qe_find_cmd(cmd_name);
    if (!d) {
        put_status(s, "No command %s", cmd_name);
        return;
    }
    qe_register_binding1(keys, nb_keys, d, local ? s->mode : NULL);
}

void do_toggle_control_h(EditState *s, int set)
{
    /* Achtung Minen! do_toggle_control_h can be called from tty_init
     * with a NULL EditState.
     */
    QEmacsState *qs = s ? s->qe_state : &qe_state;
    ModeDef *m;
    KeyDef *kd;
    int i;

    if (set)
        set = (set > 0);
    else
        set = !qs->backspace_is_control_h;

    if (qs->backspace_is_control_h == set)
        return;

    qs->backspace_is_control_h = set;

    /* CG: This hack in incompatible with support for multiple
     * concurrent input consoles.
     */
    for (m = qs->first_mode;; m = m->next) {
        for (kd = m ? m->first_key : qs->first_key; kd; kd = kd->next) {
            for (i = 0; i < kd->nb_keys; i++) {
                switch (kd->keys[i]) {
                case KEY_CTRL('h'):
                    kd->keys[i] = set ? KEY_META('h') : 127;
                    break;
                case 127:
                    if (set)
                        kd->keys[i] = KEY_CTRL('h');
                    break;
                case KEY_META('h'):
                    if (!set)
                        kd->keys[i] = KEY_CTRL('h');
                    break;
                }
            }
        }
        if (!m)
            break;
    }
}

void do_set_emulation(EditState *s, const char *name)
{
    QEmacsState *qs = s->qe_state;

    if (strequal(name, "epsilon")) {
        qs->flag_split_window_change_focus = 1;
    } else
    if (strequal(name, "emacs") || strequal(name, "xemacs")) {
        qs->flag_split_window_change_focus = 0;
    } else
    if (strequal(name, "vi") || strequal(name, "vim")) {
        put_status(s, "emulation '%s' not available yet", name);
    } else {
        put_status(s, "unknown emulation '%s'", name);
    }
}

void do_set_trace(EditState *s)
{
    do_split_window(s, 0);
    do_switch_to_buffer(s, "*trace*");
    do_previous_window(s);
}

void do_cd(__unused__ EditState *s, const char *name)
{
    chdir(name);
    /* CG: Should issue diagnostics upon failure */
    /* CG: Should display current directory after chdir */
}

/* basic editing functions */
/* CG: should indirect these through mode ! */
void do_bof(EditState *s)
{
    s->offset = 0;
}

void do_eof(EditState *s)
{
    s->offset = s->b->total_size;
}

void do_bol(EditState *s)
{
    if (s->mode->move_bol)
        s->mode->move_bol(s);
}

void do_eol(EditState *s)
{
    if (s->mode->move_eol)
        s->mode->move_eol(s);
}

void do_word_right(EditState *s, int dir)
{
    if (s->mode->move_word_left_right)
        s->mode->move_word_left_right(s, dir);
}

void text_move_bol(EditState *s)
{
    s->offset = eb_goto_bol(s->b, s->offset);
}

void text_move_eol(EditState *s)
{
    s->offset = eb_goto_eol(s->b, s->offset);
}

void word_right(EditState *s, int w)
{
    int c, offset1;

    for (;;) {
        if (s->offset >= s->b->total_size)
            break;
        c = eb_nextc(s->b, s->offset, &offset1);
        if (qe_isword(c) == w)
            break;
        s->offset = offset1;
    }
}

void word_left(EditState *s, int w)
{
    int c, offset1;

    for (;;) {
        if (s->offset == 0)
            break;
        c = eb_prevc(s->b, s->offset, &offset1);
        if (qe_isword(c) == w)
            break;
        s->offset = offset1;
    }
}

void text_move_word_left_right(EditState *s, int dir)
{
    if (dir > 0) {
        word_right(s, 1);
        word_right(s, 0);
    } else {
        word_left(s, 1);
        word_left(s, 0);
    }
}

/* paragraph handling */

int eb_next_paragraph(EditBuffer *b, int offset)
{
    int text_found;

    offset = eb_goto_bol(b, offset);
    /* find end of paragraph */
    text_found = 0;
    for (;;) {
        if (offset >= b->total_size)
            break;
        if (eb_is_empty_line(b, offset)) {
            if (text_found)
                break;
        } else {
            text_found = 1;
        }
        offset = eb_next_line(b, offset);
    }
    return offset;
}

int eb_start_paragraph(EditBuffer *b, int offset)
{
    for (;;) {
        offset = eb_goto_bol(b, offset);
        if (offset <= 0)
            break;
        /* check if only spaces */
        if (eb_is_empty_line(b, offset)) {
            offset = eb_next_line(b, offset);
            break;
        }
        eb_prevc(b, offset, &offset);
    }
    return offset;
}

void do_backward_paragraph(EditState *s)
{
    int offset;

    offset = s->offset;
    /* skip empty lines */
    for (;;) {
        if (offset <= 0)
            break;
        offset = eb_goto_bol(s->b, offset);
        if (!eb_is_empty_line(s->b, offset))
            break;
        /* line just before */
        eb_prevc(s->b, offset, &offset);
    }

    offset = eb_start_paragraph(s->b, offset);

    /* line just before */
    offset = eb_prev_line(s->b, offset);

    s->offset = offset;
}

void do_forward_paragraph(EditState *s)
{
    s->offset = eb_next_paragraph(s->b, s->offset);
}

void do_kill_paragraph(EditState *s, int dir)
{
    int start = s->offset;

    if (s->b->flags & BF_READONLY)
        return;

    if (dir < 0)
        do_backward_paragraph(s);
    else
        do_forward_paragraph(s);

    do_kill(s, start, s->offset, dir);
}

#define PARAGRAPH_WIDTH 76

void do_fill_paragraph(EditState *s)
{
    int par_start, par_end, col;
    int offset, offset1, n, c, line_count, indent_size;
    int chunk_start, word_start, word_size, word_count, space_size;
    unsigned char buf[1];

    /* find start & end of paragraph */
    par_start = eb_start_paragraph(s->b, s->offset);
    par_end = eb_next_paragraph(s->b, par_start);

    /* compute indent size */
    indent_size = 0;
    offset = eb_next_line(s->b, par_start);
    if (!eb_is_empty_line(s->b, offset)) {
        while (offset < par_end) {
            c = eb_nextc(s->b, offset, &offset);
            if (!qe_isspace(c))
                break;
            indent_size++;
        }
    }

    /* suppress any spaces in between */
    col = 0;
    offset = par_start;
    word_count = 0;
    line_count = 0;
    while (offset < par_end) {
        /* skip spaces */
        chunk_start = offset;
        space_size = 0;
        while (offset < par_end) {
            c = eb_nextc(s->b, offset, &offset1);
            if (!qe_isspace(c))
                break;
            offset = offset1;
            space_size++;
        }
        /* skip word */
        word_start = offset;
        word_size = 0;
        while (offset < par_end) {
            c = eb_nextc(s->b, offset, &offset1);
            if (qe_isspace(c))
                break;
            offset = offset1;
            word_size++;
        }

        if (word_count == 0) {
            /* first word: preserve spaces */
            col += space_size + word_size;
        } else {
            /* insert space single space then word */
            if (offset == par_end ||
                (col + 1 + word_size > PARAGRAPH_WIDTH)) {
                buf[0] = '\n';
                eb_write(s->b, chunk_start, buf, 1);
                chunk_start++;
                if (offset < par_end) {
                    /* indent */
                    buf[0] = ' ';
                    for (n = indent_size; n > 0; n--)
                        eb_insert(s->b, chunk_start, buf, 1);
                    chunk_start += indent_size;

                    word_start += indent_size;
                    offset += indent_size;
                    par_end += indent_size;
                }
                col = word_size + indent_size;
            } else {
                buf[0] = ' ';
                eb_write(s->b, chunk_start, buf, 1);
                chunk_start++;
                col += 1 + word_size;
            }

            /* remove all other spaces if needed */
            n = word_start - chunk_start;
            if (n > 0) {
                eb_delete(s->b, chunk_start, n);
                offset -= n;
                par_end -= n;
            }
        }
        word_count++;
    }
}

/* Upper / lower / capital case functions. Update offset, return isword */
/* arg: -1=lower-case, +1=upper-case, +2=capital-case */
/* (XXX: use generic unicode function). */
static int eb_changecase(EditBuffer *b, int *offsetp, int arg)
{
    int offset0, ch, ch1, len;
    char buf[MAX_CHAR_BYTES];

    offset0 = *offsetp;
    ch = eb_nextc(b, offset0, offsetp);
    if (!qe_isword(ch))
        return 0;

    if (arg > 0)
        ch1 = qe_toupper(ch);
    else
        ch1 = qe_tolower(ch);

    if (ch != ch1) {
        len = unicode_to_charset(buf, ch1, b->charset);
        eb_replace(b, offset0, *offsetp - offset0, buf, len);
    }
    return 1;
}

void do_changecase_word(EditState *s, int arg)
{
    int offset;

    word_right(s, 1);
    for (offset = s->offset;;) {
        if (offset >= s->b->total_size)
            break;
        if (!eb_changecase(s->b, &offset, arg))
            break;
        s->offset = offset;
        if (arg == 2)
            arg = -2;
    }
}

void do_changecase_region(EditState *s, int arg)
{
    int offset;

    /* WARNING: during case change, the region offsets can change, so
       it is not so simple ! */
    offset = min(s->offset, s->b->mark);
    for (;;) {
        if (offset >= max(s->offset, s->b->mark))
              break;
        if (eb_changecase(s->b, &offset, arg)) {
            if (arg == 2)
                arg = -arg;
        } else {
            if (arg == -2)
                arg = -arg;
        }
    }
}

void do_delete_char(EditState *s, int argval)
{
    int endpos, i, offset1;

    if (s->b->flags & BF_READONLY)
        return;

    if (argval == NO_ARG) {
        if (s->qe_state->last_cmd_func != (CmdFunc)do_append_next_kill) {
            eb_nextc(s->b, s->offset, &offset1);
            eb_delete(s->b, s->offset, offset1 - s->offset);
            return;
        }
        argval = 1;
    }

    /* save kill if universal argument given */
    endpos = s->offset;
    for (i = argval; i > 0 && endpos < s->b->total_size; i--) {
        eb_nextc(s->b, endpos, &endpos);
    }
    for (i = argval; i < 0 && endpos > 0; i++) {
        eb_prevc(s->b, endpos, &endpos);
    }
    do_kill(s, s->offset, endpos, argval);
}

void do_backspace(EditState *s, int argval)
{
    int offset1;

    if (s->b->flags & BF_READONLY) {
        /* CG: could scroll down */
        return;
    }

    /* XXX: Should delete hilighted region */

    /* deactivate region hilite */
    s->region_style = 0;

    if (argval == NO_ARG) {
        if (s->qe_state->last_cmd_func != (CmdFunc)do_append_next_kill) {
            eb_prevc(s->b, s->offset, &offset1);
            if (offset1 < s->offset) {
                s->offset = eb_delete_range(s->b, offset1, s->offset);
                /* special case for composing */
                if (s->compose_len > 0)
                    s->compose_len--;
            }
            return;
        }
        argval = 1;
    }
    /* save kill if universal argument given */
    do_delete_char(s, -argval);
}

/* return the cursor position relative to the screen. Note that xc is
   given in pixel coordinates */
typedef struct {
    int linec;
    int yc;
    int xc;
    int offsetc;
    DirType basec; /* direction of the line */
    DirType dirc; /* direction of the char under the cursor */
    int cursor_width; /* can be negative depending on char orientation */
    int cursor_height;
} CursorContext;

int cursor_func(DisplayState *ds,
                int offset1, int offset2, int line_num,
                int x, int y, int w, int h, __unused__ int hex_mode)
{
    CursorContext *m = ds->cursor_opaque;

    if (m->offsetc >= offset1 &&
        m->offsetc < offset2) {
        m->xc = x;
        m->yc = y;
        m->basec = ds->base;
        m->dirc = ds->base; /* XXX: do it */
        m->cursor_width = w;
        m->cursor_height = h;
        m->linec = line_num;
#if 0
        printf("cursor_func: xc=%d yc=%d linec=%d offset: %d<=%d<%d\n",
               m->xc, m->yc, m->linec, offset1, m->offsetc, offset2);
#endif
        return -1;
    } else {
        return 0;
    }
}

static void get_cursor_pos(EditState *s, CursorContext *m)
{
    DisplayState ds1, *ds = &ds1;

    display_init(ds, s, DISP_CURSOR);
    ds->cursor_opaque = m;
    ds->cursor_func = cursor_func;
    memset(m, 0, sizeof(*m));
    m->offsetc = s->offset;
    m->xc = m->yc = NO_CURSOR;
    display1(ds);
}

typedef struct {
    int yd;
    int xd;
    int xdmin;
    int offsetd;
} MoveContext;

/* called each time the cursor could be displayed */
static int down_cursor_func(DisplayState *ds,
                            int offset1, __unused__ int offset2, int line_num,
                            int x, __unused__ int y,
                            __unused__ int w, __unused__ int h,
                            __unused__ int hex_mode)
{
    int d;
    MoveContext *m = ds->cursor_opaque;

    if (line_num == m->yd) {
        /* find the closest char */
        d = abs(x - m->xd);
        if (d < m->xdmin) {
            m->xdmin = d;
            m->offsetd = offset1;
        }
        return 0;
    } else if (line_num > m->yd) {
        /* no need to explore more chars */
        return -1;
    } else {
        return 0;
    }
}

void do_up_down(EditState *s, int dir)
{
    if (s->mode->move_up_down)
        s->mode->move_up_down(s, dir);
}

void do_left_right(EditState *s, int dir)
{
    if (s->mode->move_left_right)
        s->mode->move_left_right(s, dir);
}

/* CG: Should move this to EditState */
static int up_down_last_x = -1;

void text_move_up_down(EditState *s, int dir)
{
    MoveContext m1, *m = &m1;
    DisplayState ds1, *ds = &ds1;
    CursorContext cm;

    if (s->qe_state->last_cmd_func != (CmdFunc)do_up_down)
        up_down_last_x = -1;

    get_cursor_pos(s, &cm);
    if (cm.xc == NO_CURSOR)
        return;

    if (up_down_last_x == -1)
        up_down_last_x = cm.xc;

    if (dir < 0) {
        /* difficult case: we need to go backward on displayed text */
        while (cm.linec <= 0) {
            if (s->offset_top <= 0)
                return;
            s->offset_top = s->mode->text_backward_offset(s, s->offset_top - 1);

            /* adjust y_disp so that the cursor is at the same position */
            s->y_disp += cm.yc;
            get_cursor_pos(s, &cm);
            s->y_disp -= cm.yc;
        }
    }

    /* find cursor offset */
    m->yd = cm.linec + dir;
    m->xd = up_down_last_x;
    m->xdmin = 0x7fffffff;
    /* if no cursor position is found, we go to bof or eof according
       to dir */
    if (dir > 0)
        m->offsetd = s->b->total_size;
    else
        m->offsetd = 0;
    display_init(ds, s, DISP_CURSOR);
    ds->cursor_opaque = m;
    ds->cursor_func = down_cursor_func;
    display1(ds);
    s->offset = m->offsetd;
}

typedef struct {
    int y_found;
    int offset_found;
    int dir;
    int offsetc;
} ScrollContext;

/* called each time the cursor could be displayed */
static int scroll_cursor_func(DisplayState *ds,
                              int offset1, int offset2,
                              __unused__ int line_num,
                              __unused__ int x, int y,
                              __unused__ int w, int h,
                              __unused__ int hex_mode)
{
    ScrollContext *m = ds->cursor_opaque;
    int y1;

    y1 = y + h;
    /* XXX: add bidir handling : position cursor on left / right */
    if (m->dir < 0) {
        if (y >= 0 && y < m->y_found) {
            m->y_found = y;
            m->offset_found = offset1;
        }
    } else {
        if (y1 <= ds->height && y1 > m->y_found) {
            m->y_found = y1;
            m->offset_found = offset1;
        }
    }
    if (m->offsetc >= offset1 && m->offsetc < offset2 &&
        y >= 0 && y1 <= ds->height) {
        m->offset_found = m->offsetc;
        m->y_found = 0x7fffffff * m->dir; /* ensure that no other
                                             position will be found */
        return -1;
    }
    return 0;
}

void do_scroll_up_down(EditState *s, int dir)
{
    if (s->mode->scroll_up_down)
        s->mode->scroll_up_down(s, dir);
}

void perform_scroll_up_down(EditState *s, int h)
{
    ScrollContext m1, *m = &m1;
    DisplayState ds1, *ds = &ds1;
    int dir;

    if (h < 0)
        dir = -1;
    else
        dir = 1;

    /* move display up/down */
    s->y_disp -= h;

    /* y_disp should not be > 0. So we update offset_top until we have
       it negative */
    if (s->y_disp > 0) {
        display_init(ds, s, DISP_CURSOR_SCREEN);
        do {
            if (s->offset_top <= 0) {
                /* cannot go back: we stay at the top of the screen and
                   exit loop */
                s->y_disp = 0;
            } else {
                s->offset_top = s->mode->text_backward_offset(s, s->offset_top - 1);
                ds->y = 0;
                s->mode->text_display(s, ds, s->offset_top);
                s->y_disp -= ds->y;
            }
        } while (s->y_disp > 0);
    }

    /* now update cursor position so that it is on screen */
    m->offsetc = s->offset;
    m->dir = -dir;
    m->y_found = 0x7fffffff * dir;
    m->offset_found = s->offset; /* default offset */
    display_init(ds, s, DISP_CURSOR_SCREEN);
    ds->cursor_opaque = m;
    ds->cursor_func = scroll_cursor_func;
    display1(ds);

    s->offset = m->offset_found;
}

void text_scroll_up_down(EditState *s, int dir)
{
    int h, line_height;

    /* try to round to a line height */
    line_height = get_line_height(s->screen, s->default_style);
    h = 1;
    if (abs(dir) == 2) {
        /* one page at a time: C-v / M-v */
        dir /= 2;
        h = (s->height / line_height) - 1;
        if (h < 1)
            h = 1;
    }
    h = h * line_height;

    perform_scroll_up_down(s, dir * h);
}

/* center the cursor in the window */
/* XXX: make it generic to all modes */
void do_center_cursor(EditState *s)
{
    CursorContext cm;

    /* only apply to text modes */
    if (!s->mode->text_display)
        return;

    get_cursor_pos(s, &cm);
    if (cm.xc == NO_CURSOR)
        return;

    /* try to center display */
    perform_scroll_up_down(s, -((s->height / 2) - cm.yc));
}

/* called each time the cursor could be displayed */
typedef struct {
    int yd;
    int xd;
    int xdmin;
    int offsetd;
    int dir;
    int after_found;
} LeftRightMoveContext;

static int left_right_cursor_func(DisplayState *ds,
                                  int offset1, __unused__ int offset2,
                                  int line_num,
                                  int x, __unused__ int y,
                                  __unused__ int w, __unused__ int h,
                                  __unused__ int hex_mode)
{
    int d;
    LeftRightMoveContext *m = ds->cursor_opaque;

    if (line_num == m->yd &&
        ((m->dir < 0 && x < m->xd) ||
         (m->dir > 0 && x > m->xd))) {
        /* find the closest char in the correct direction */
        d = abs(x - m->xd);
        if (d < m->xdmin) {
            m->xdmin = d;
            m->offsetd = offset1;
        }
        return 0;
    } else if (line_num > m->yd) {
        m->after_found = 1;
        /* no need to explore more chars */
        return -1;
    } else {
        return 0;
    }
}

/* go to left or right in visual order */
void text_move_left_right_visual(EditState *s, int dir)
{
    LeftRightMoveContext m1, *m = &m1;
    DisplayState ds1, *ds = &ds1;
    int xc, yc, nextline;
    CursorContext cm;

    get_cursor_pos(s, &cm);
    xc = cm.xc;
    yc = cm.linec;

    nextline = 0;
    for (;;) {
        /* find cursor offset */
        m->yd = yc;
        if (!nextline) {
            m->xd = xc;
        } else {
            m->xd = -dir * 0x3fffffff;  /* not too big to avoid overflow */
        }
        m->xdmin = 0x7fffffff;
        m->offsetd = -1;
        m->dir = dir;
        m->after_found = 0;
        display_init(ds, s, DISP_CURSOR);
        ds->cursor_opaque = m;
        ds->cursor_func = left_right_cursor_func;
        display1(ds);
        if (m->offsetd >= 0) {
            /* position found : update and exit */
            s->offset = m->offsetd;
            break;
        } else {
            if (dir > 0) {
                /* no suitable position found: go to next line */
                /* if no char after, no need to continue */
                if (!m->after_found)
                   break;
            } else {
                /* no suitable position found: go to previous line */
                if (yc <= 0) {
                    if (s->offset_top <= 0)
                        break;
                    s->offset_top = s->mode->text_backward_offset(s, s->offset_top - 1);
                    /* adjust y_disp so that the cursor is at the same position */
                    s->y_disp += cm.yc;
                    get_cursor_pos(s, &cm);
                    s->y_disp -= cm.yc;
                    yc = cm.linec;
                }
            }
            yc += dir;
            nextline = 1;
        }
    }
}

/* mouse get cursor func */
#ifndef CONFIG_TINY

/* called each time the cursor could be displayed */
typedef struct {
    int yd;
    int xd;
    int dy_min;
    int dx_min;
    int offset_found;
    int hex_mode;
} MouseGotoContext;

/* distance from x to segment [x1,x2-1] */
static int seg_dist(int x, int x1, int x2)
{
    if (x <= x1)
        return x1 - x;
    else if (x >= x2)
        return x - x2 + 1;
    else
        return 0;
}

/* XXX: would need two passes in the general case (first search line,
   then colunm */
static int mouse_goto_func(DisplayState *ds,
                           int offset1, __unused__ int offset2,
                           __unused__ int line_num,
                           int x, int y, int w, int h, int hex_mode)
{
    MouseGotoContext *m = ds->cursor_opaque;
    int dy, dx;

    dy = seg_dist(m->yd, y, y + h);
    if (dy < m->dy_min) {
        m->dy_min = dy;
        m->dx_min = 0x3fffffff;
    }
    if (dy == m->dy_min) {
        dx = seg_dist(m->xd, x, x + w);
        if (dx < m->dx_min) {
            m->dx_min = dx;
            m->offset_found = offset1;
            m->hex_mode = hex_mode;
            /* fast exit test */
            if (dy == 0 && dx == 0)
                return -1;
        }
    }
    return 0;
}

/* go to left or right in visual order. In hex mode, a side effect is
   to select the right column. */
void text_mouse_goto(EditState *s, int x, int y)
{
    QEmacsState *qs = s->qe_state;
    MouseGotoContext m1, *m = &m1;
    DisplayState ds1, *ds = &ds1;

    m->dx_min = 0x3fffffff;
    m->dy_min = 0x3fffffff;
    m->xd = x;
    m->yd = y;
    m->offset_found = s->offset; /* fail safe */
    m->hex_mode = s->hex_mode;

    display_init(ds, s, DISP_CURSOR_SCREEN);
    ds->hex_mode = -1; /* we select both hex chars and normal chars */
    ds->cursor_opaque = m;
    ds->cursor_func = mouse_goto_func;
    display1(ds);

    s->offset = m->offset_found;
    s->hex_mode = m->hex_mode;

    /* activate window (need more ideas for popups) */
    if (!(s->flags & WF_POPUP))
        qs->active_window = s;
    if (s->mouse_force_highlight)
        s->force_highlight = 1;
}
#else
void text_mouse_goto(EditState *s, int x, int y)
{
}
#endif

void do_char(EditState *s, int key, int argval)
{
    if (s->b->flags & BF_READONLY)
        return;

    /* XXX: Should delete hilighted region */

    /* deactivate region hilite */
    s->region_style = 0;

    for (;;) {
        if (s->mode->write_char)
            s->mode->write_char(s, key);
        if (argval-- <= 1)
            break;
    }
}

void text_write_char(EditState *s, int key)
{
    int cur_ch, len, cur_len, offset1, ret, insert;
    char buf[MAX_CHAR_BYTES];

    if (check_read_only(s))
        return;

    /* XXX: Should delete hilighted region */

    /* deactivate region hilite */
    s->region_style = 0;

    cur_ch = eb_nextc(s->b, s->offset, &offset1);
    cur_len = offset1 - s->offset;
    len = unicode_to_charset(buf, key, s->b->charset);
    insert = (s->insert || cur_ch == '\n');

    if (insert) {
        const InputMethod *m;
        int match_buf[20], match_len, offset, i;

        /* use compose system only if insert mode */
        if (s->compose_len == 0)
            s->compose_start_offset = s->offset;

        /* insert char */
        eb_insert(s->b, s->offset, buf, len);
        s->offset += len;

        s->compose_buf[s->compose_len++] = key;
        m = s->input_method;
        for (;;) {
            if (!m) {
                s->compose_len = 0;
                break;
            }
            ret = m->input_match(match_buf, countof(match_buf),
                                 &match_len, m->data, s->compose_buf,
                                 s->compose_len);
            if (ret == INPUTMETHOD_NOMATCH) {
                /* no match : reset compose state */
                s->compose_len = 0;
                break;
            } else
            if (ret == INPUTMETHOD_MORECHARS) {
                /* more chars expected: do nothing and insert current key */
                break;
            } else {
                /* match: delete matched chars */
                offset = s->compose_start_offset;
                for (i = 0; i < match_len; i++)
                    eb_nextc(s->b, offset, &offset);
                eb_delete_range(s->b, s->compose_start_offset, offset);
                s->compose_len -= match_len;
                umemmove(s->compose_buf, s->compose_buf + match_len,
                         s->compose_len);
                /* then insert match */
                for (i = 0; i < ret; i++) {
                    key = match_buf[i];
                    len = unicode_to_charset(buf, key, s->b->charset);
                    eb_insert(s->b, s->compose_start_offset, buf, len);
                    s->compose_start_offset += len;
                    /* should only bump s->offset if at insert point */
                    s->offset += len;
                }
                /* if some compose chars are left, we iterate */
                if (s->compose_len == 0)
                    break;
            }
        }
    } else {
        eb_replace(s->b, s->offset, cur_len, buf, len);
        /* adjust offset because in inserting at point */
        s->offset += len;
    }
}

struct QuoteKeyArgument {
    EditState *s;
    int argval;
};

/* XXX: may be better to move it into qe_key_process() */
static void quote_key(void *opaque, int key)
{
    struct QuoteKeyArgument *qa = opaque;
    EditState *s = qa->s;

    put_status(s, "");

    if (!s)
        return;

    /* CG: why not insert special keys as well? */
    if (!KEY_SPECIAL(key) || (key >= 0 && key <= 31)) {
        do_char(s, key, qa->argval);
        edit_display(s->qe_state);
        dpy_flush(&global_screen);
    }
    qe_ungrab_keys();
}

void do_quote(EditState *s, int argval)
{
    struct QuoteKeyArgument *qa = qe_mallocz(struct QuoteKeyArgument);

    qa->s = s;
    qa->argval = argval;

    qe_grab_keys(quote_key, qa);
    put_status(s, "Quote: ");
}

void do_insert(EditState *s)
{
    s->insert = !s->insert;
}

void do_tab(EditState *s, int argval)
{
    /* CG: should do smart complete, smart indent, insert tab */
    do_char(s, 9, argval);
}

void do_return(EditState *s, int move)
{
    if (s->b->flags & BF_READONLY)
        return;

    eb_insert(s->b, s->offset, "\n", 1);
    s->offset += move;
}

#if 0
void do_space(EditState *s, int key, int argval)
{
    if (s->b->flags & BF_READONLY) {
        do_scroll_up_down(s, 1, argval);
        return;
    }
    do_char(s, key, argval);
}
#endif

void do_break(EditState *s)
{
    /* deactivate region hilite */
    s->region_style = 0;

    /* well, currently nothing needs to be aborted in global context */
    /* CG: Should remove popups, sidepanes, helppanes... */
    put_status(s, "Quit");
}

/* block functions */
void do_set_mark(EditState *s)
{
    /* CG: Should have local and global mark rings */
    s->b->mark = s->offset;

    /* activate region hilite */
    if (s->qe_state->hilite_region)
        s->region_style = QE_STYLE_REGION_HILITE;

    put_status(s, "Mark set");
}

void do_mark_whole_buffer(EditState *s)
{
    s->b->mark = s->b->total_size;
    s->offset = 0;
}

EditBuffer *new_yank_buffer(QEmacsState *qs)
{
    char bufname[32];
    EditBuffer *b;

    if (qs->yank_buffers[qs->yank_current]) {
        if (++qs->yank_current == NB_YANK_BUFFERS)
            qs->yank_current = 0;
        b = qs->yank_buffers[qs->yank_current];
        if (b) {
            /* problem if buffer is displayed in window, should instead
             * just clear the buffer */
            eb_free(b);
        }
    }
    snprintf(bufname, sizeof(bufname), "*kill-%d*", qs->yank_current + 1);
    b = eb_new(bufname, 0);
    qs->yank_buffers[qs->yank_current] = b;
    return b;
}

void do_append_next_kill(__unused__ EditState *s)
{
    /* do nothing! */
}

void do_kill(EditState *s, int p1, int p2, int dir)
{
    QEmacsState *qs = s->qe_state;
    int len, tmp;
    EditBuffer *b;

    /* deactivate region hilite */
    s->region_style = 0;

    if (s->b->flags & BF_READONLY)
        return;

    if (p1 > p2) {
        tmp = p1;
        p1 = p2;
        p2 = tmp;
    }
    len = p2 - p1;
    b = qs->yank_buffers[qs->yank_current];
    if (!b || !dir || qs->last_cmd_func != (CmdFunc)do_append_next_kill) {
        /* append kill if last command was kill already */
        b = new_yank_buffer(qs);
    }
    /* insert at beginning or end depending on kill direction */
    eb_insert_buffer(b, dir < 0 ? 0 : b->total_size, s->b, p1, len);
    if (dir) {
        eb_delete(s->b, p1, len);
        s->offset = p1;
        qs->this_cmd_func = (CmdFunc)do_append_next_kill;
    }
    selection_activate(qs->screen);
}

void do_kill_region(EditState *s, int killtype)
{
    do_kill(s, s->b->mark, s->offset, killtype);
}

void do_kill_line(EditState *s, int dir)
{
    int p1, p2, offset1;

    if (s->b->flags & BF_READONLY)
        return;

    p1 = s->offset;
    if (dir < 0) {
        /* kill beginning of line */
        do_bol(s);
        p2 = s->offset;
    } else {
        /* kill line */
        if (eb_nextc(s->b, p1, &offset1) == '\n') {
            p2 = offset1;
        } else {
            p2 = offset1;
            while (eb_nextc(s->b, p2, &offset1) != '\n') {
                p2 = offset1;
            }
        }
    }
    do_kill(s, p1, p2, dir);
}

void do_kill_word(EditState *s, int dir)
{
    int start = s->offset;

    if (s->b->flags & BF_READONLY)
        return;

    do_word_right(s, dir);
    do_kill(s, start, s->offset, dir);
}

void do_yank(EditState *s)
{
    int size;
    QEmacsState *qs = s->qe_state;
    EditBuffer *b;

    if (s->b->flags & BF_READONLY)
        return;

    /* if the GUI selection is used, it will be handled in the GUI code */
    selection_request(qs->screen);

    s->b->mark = s->offset;
    b = qs->yank_buffers[qs->yank_current];
    if (b) {
        size = b->total_size;
        if (size > 0) {
            eb_insert_buffer(s->b, s->offset, b, 0, size);
            s->offset += size;
        }
    }
    qs->this_cmd_func = (CmdFunc)do_yank;
}

void do_yank_pop(EditState *s)
{
    QEmacsState *qs = s->qe_state;

    if (qs->last_cmd_func != (CmdFunc)do_yank) {
        put_status(s, "Previous command was not a yank");
        return;
    }

    eb_delete_range(s->b, s->b->mark, s->offset);

    if (--qs->yank_current < 0) {
        /* get last yank buffer, yank ring may not be full */
        qs->yank_current = NB_YANK_BUFFERS;
        while (--qs->yank_current && !qs->yank_buffers[qs->yank_current])
            continue;
    }
    do_yank(s);
}

void do_exchange_point_and_mark(EditState *s)
{
    int tmp;

    tmp = s->b->mark;
    s->b->mark = s->offset;
    s->offset = tmp;
}

static int reload_buffer(EditState *s, EditBuffer *b, FILE *f1)
{
    FILE *f;
    int ret, saved;

    /* if no file associated, cannot do anything */
    if (b->filename[0] == '\0')
        return 0;

    if (!f1) {
        struct stat st;

        if (stat(b->filename, &st) < 0 || !S_ISREG(st.st_mode))
            return -1;

        f = fopen(b->filename, "r");
        if (!f) {
            goto fail;
        }
    } else {
        f = f1;
    }
    saved = b->save_log;
    b->save_log = 0;
    ret = b->data_type->buffer_load(b, f);
    b->modified = 0;
    b->save_log = saved;
    if (!f1)
        fclose(f);

    if (ret < 0) {
      fail:
        if (!f1) {
            put_status(s, "Could not load '%s'", b->filename);
        } else {
            put_status(s, "Error while reloading '%s'", b->filename);
        }
        return -1;
    } else {
        return 0;
    }
}

static void edit_set_mode_file(EditState *s, ModeDef *m,
                               ModeSavedData *saved_data, FILE *f1)
{
    int size, data_count;
    int saved_data_allocated = 0;
    EditState *e;
    EditBuffer *b;

    b = s->b;

    /* if a mode is already defined, try to close it */
    if (s->mode) {
        /* save mode data if necessary */
        if (!saved_data) {
            saved_data = s->mode->mode_save_data(s);
            if (saved_data)
                saved_data_allocated = 1;
        }
        s->mode->mode_close(s);
        qe_free(&s->mode_data);
        s->mode = NULL;
        set_colorize_func(s, NULL);

        /* try to remove the raw or mode specific data if it is no
           longer used. */
        data_count = 0;
        for (e = s->qe_state->first_window; e != NULL; e = e->next_window) {
            if (e != s && e->b == b) {
                if (e->mode->data_type != &raw_data_type)
                    data_count++;
            }
        }
        /* we try to remove mode specific data if it is redundant with
           the buffer raw data */
        if (data_count == 0 && !b->modified) {
            /* close mode specific buffer representation because it is
               always redundant if it was not modified */
            if (b->data_type != &raw_data_type) {
                b->data_type->buffer_close(b);
                b->data = NULL;
                b->data_type = &raw_data_type;
            }
        }
    }
    /* if a new mode is wanted, open it */
    if (m) {
        size = m->instance_size;
        s->mode_data = NULL;
        if (m->data_type != &raw_data_type) {
            /* if a non raw data type is requested, we see if we can use it */
            if (b->data_type == &raw_data_type) {
                /* non raw data type: we must call a mode specific
                   load method */
                b->data_type = m->data_type;
                if (reload_buffer(s, b, f1) < 0) {
                    /* error: reset to text mode */
                    m = &text_mode;
                    b->data_type = &raw_data_type;
                }
            } else
            if (b->data_type != m->data_type) {
                /* non raw data type requested, but the the buffer has
                   a different type: we cannot switch mode, so we fall
                   back to text */
                m = &text_mode;
            } else {
                /* same data type: nothing more to do */
            }
        } else {
            /* if raw data and nothing loaded, we try to load */
            if (b->total_size == 0 && !b->modified)
                reload_buffer(s, b, f1);
        }
        if (size > 0) {
            s->mode_data = qe_mallocz_array(u8, size);
            /* safe fall back: use text mode */
            if (!s->mode_data)
                m = &text_mode;
        }
        s->mode = m;

        /* init mode */
        m->mode_init(s, saved_data);
        /* modify offset_top so that its value is correct */
        if (s->mode->text_backward_offset)
            s->offset_top = s->mode->text_backward_offset(s, s->offset_top);
    }
    if (saved_data_allocated)
        qe_free(&saved_data);
}

void edit_set_mode(EditState *s, ModeDef *m, ModeSavedData *saved_data)
{
    edit_set_mode_file(s, m, saved_data, NULL);
}

void do_set_mode(EditState *s, const char *name)
{
    ModeDef *m;

    m = find_mode(name);
    if (m)
        edit_set_mode(s, m, NULL);
    else
        put_status(s, "No mode %s", name);
}

/* CG: should have commands to cycle modes and charsets */
#if 0
/* cycle modes appropriate for buffer */
void do_next_mode(EditState *s)
{
    QEmacsState *qs = s->qe_state;
    char fname[MAX_FILENAME_SIZE];
    u8 buf[1024];
    ModeProbeData probe_data;
    int size;
    ModeDef *m, *m0;

    size = eb_read(s->b, 0, buf, sizeof(buf));
    probe_data.buf = buf;
    probe_data.buf_size = size;
    probe_data.real_filename = s->b->filename;
    probe_data.total_size = s->b->total_size;
    probe_data.filename = reduce_filename(fname, sizeof(fname),
                                          get_basename(s->b->filename));
    /* CG: should pass EditState? QEmacsState ? */

    m = m0 = s->mode;
    for (;;) {
        m = m->next;
        if (!m)
            m = qs->first_mode;
        if (m == m0)
            break;
        if (!m->mode_probe
        ||  m->mode_probe(&probe_data) > 0) {
            edit_set_mode(s, m, 0);
            break;
        }
    }
}

void do_cycle_charset(EditState *s)
{
    if (++s->b->charset == CHARSET_NB)
        s->b->charset = 0;
}
#endif

QECharset *read_charset(EditState *s, const char *charset_str)
{
    QECharset *charset;

    charset = find_charset(charset_str);
    if (!charset) {
        put_status(s, "Unknown charset '%s'", charset_str);
        return NULL;
    }
    return charset;
}

void do_set_buffer_file_coding_system(EditState *s, const char *charset_str)
{
    QECharset *charset;

    charset = read_charset(s, charset_str);
    if (!charset)
        return;
    eb_set_charset(s->b, charset);
}

/* convert the charset of a buffer to another charset */
void do_convert_buffer_file_coding_system(EditState *s,
                                          const char *charset_str)
{
    QECharset *charset;
    EditBuffer *b1, *b;
    int offset, c, len;
    char buf[MAX_CHAR_BYTES];

    charset = read_charset(s, charset_str);
    if (!charset)
        return;

    b1 = eb_new("*tmp*", 0);
    eb_set_charset(b1, charset);

    /* well, not very fast, but simple */
    b = s->b;
    for (offset = 0; offset < b->total_size;) {
        c = eb_nextc(b, offset, &offset);
        len = unicode_to_charset(buf, c, charset);
        eb_write(b1, b1->total_size, buf, len);
    }

    /* replace current buffer with conversion */
    eb_delete(b, 0, b->total_size);
    eb_set_charset(b, charset);
    eb_insert_buffer(b, 0, b1, 0, b1->total_size);

    eb_free(b1);
}

void do_toggle_bidir(EditState *s)
{
    s->bidir = !s->bidir;
}

void do_toggle_line_numbers(EditState *s)
{
    s->line_numbers = !s->line_numbers;
}

void do_toggle_truncate_lines(EditState *s)
{
    if (s->wrap == WRAP_TRUNCATE)
        s->wrap = WRAP_LINE;
    else
        s->wrap = WRAP_TRUNCATE;
}

void do_word_wrap(EditState *s)
{
    if (s->wrap == WRAP_WORD)
        s->wrap = WRAP_LINE;
    else
        s->wrap = WRAP_WORD;
}

/* do_goto: move point to a specified position.
 * take string and default unit,
 * string is parsed as an integer with an optional unit and suffix
 * units: (b)yte, (c)har, (w)ord, (l)line, (%)percentage
 * optional suffix :col or .col for column number in goto_line
 */

void do_goto(EditState *s, const char *str, int unit)
{
    const char *p;
    int pos, line, col, rel;

    rel = (*str == '+' || *str == '-');
    pos = strtol(str, (char**)&p, 0);

    if (memchr("bcwl%", *p, 5))
        unit = *p++;

    switch (unit) {
    case 'b':
        if (*p)
            goto error;
        if (rel)
            pos += s->offset;
        s->offset = clamp(pos, 0, s->b->total_size);
        return;
    case 'c':
        if (*p)
            goto error;
        if (rel)
            pos += eb_get_char_offset(s->b, s->offset);
        s->offset = eb_goto_char(s->b, max(0, pos));
        return;
    case '%':
        /* CG: should not require long long for this */
        pos = pos * (long long)s->b->total_size / 100;
        if (rel)
            pos += s->offset;
        eb_get_pos(s->b, &line, &col, max(pos, 0));
        line += (col > 0);
        goto getcol;

    case 'l':
        line = pos - 1;
        if (rel || pos == 0) {
            eb_get_pos(s->b, &line, &col, s->offset);
            line += pos;
        }
    getcol:
        col = 0;
        if (*p == ':' || *p == '.') {
            col = strtol(p + 1, (char**)&p, 0);
        }
        if (*p)
            goto error;
        s->offset = eb_goto_pos(s->b, max(0, line), col);
        return;
    }
error:
    put_status(s, "invalid position: %s", str);
}

void do_goto_line(EditState *s, int line)
{
    if (line >= 1)
        s->offset = eb_goto_pos(s->b, line - 1, 0);
}

void do_count_lines(EditState *s)
{
    int total_lines, line_num, mark_line, col_num;

    eb_get_pos(s->b, &total_lines, &col_num, s->b->total_size);
    eb_get_pos(s->b, &mark_line, &col_num, s->b->mark);
    eb_get_pos(s->b, &line_num, &col_num, s->offset);

    put_status(s, "%d lines, point on line %d, %d lines in block",
               total_lines, line_num + 1, abs(line_num - mark_line));
}

void do_what_cursor_position(EditState *s)
{
    char buf[256];
    buf_t out;
    unsigned char cc;
    int line_num, col_num;
    int c, offset1, off;

    buf_init(&out, buf, sizeof(buf));
    if (s->offset < s->b->total_size) {
        c = eb_nextc(s->b, s->offset, &offset1);
        buf_puts(&out, "char: ");
        if (c < 32 || c == 127) {
            buf_printf(&out, "^%c ", (c + '@') & 127);
        } else
        if (c < 127 || c >= 160) {
            buf_put_byte(&out, '\'');
            buf_putc_utf8(&out, c);
            buf_put_byte(&out, '\'');
            buf_put_byte(&out, ' ');
        }
        buf_printf(&out, "\\%03o %d 0x%02x ", c, c, c);

        /* Display buffer bytes if char is encoded */
        off = s->offset;
        eb_read(s->b, off++, &cc, 1);
        if (cc != c || off != offset1) {
            buf_printf(&out, "[%02X", cc);
            while (off < offset1) {
                eb_read(s->b, off++, &cc, 1);
                buf_printf(&out, " %02X", cc);
            }
            buf_put_byte(&out, ']');
            buf_put_byte(&out, ' ');
        }
        buf_put_byte(&out, ' ');
    }
    eb_get_pos(s->b, &line_num, &col_num, s->offset);
    put_status(s, "%spoint=%d column=%d mark=%d size=%d region=%d",
               out.buf, s->offset, col_num, s->b->mark, s->b->total_size,
               abs(s->offset - s->b->mark));
}

void do_set_tab_width(EditState *s, int tab_width)
{
    if (tab_width > 1)
        s->tab_size = tab_width;
}

void do_set_indent_width(EditState *s, int indent_width)
{
    if (indent_width > 1)
        s->indent_size = indent_width;
}

void do_set_indent_tabs_mode(EditState *s, int mode)
{
    s->indent_tabs_mode = (mode != 0);
}

/* compute string for the first part of the mode line (flags,
   filename, modename) */
int basic_mode_line(EditState *s, char *buf, int buf_size, int c1)
{
    int mod, state, pos;

    pos = 0;
    mod = s->b->modified ? '*' : '-';
    if (s->b->flags & BF_LOADING)
        state = 'L';
    else if (s->b->flags & BF_SAVING)
        state = 'S';
    else if (s->busy)
        state = 'B';
    else
        state = '-';

    pos += snprintf(buf + pos, buf_size - pos, "%c%c:%c%c  %-20s  (%s",
                    c1,
                    state,
                    s->b->flags & BF_READONLY ? '%' : mod,
                    mod,
                    s->b->name,
                    s->mode->name);
    if (!s->insert)
        pos += snprintf(buf + pos, buf_size - pos, " Ovwrt");
    if (s->interactive)
        pos += snprintf(buf + pos, buf_size - pos, " Interactive");
    pos += snprintf(buf + pos, buf_size - pos, ")--");

    return pos;
}

int text_mode_line(EditState *s, char *buf, int buf_size)
{
    int line_num, col_num, wrap_mode;
    int percent, pos;

    wrap_mode = '-';
    if (!s->hex_mode) {
        if (s->wrap == WRAP_TRUNCATE)
            wrap_mode = 'T';
        else if (s->wrap == WRAP_WORD)
            wrap_mode = 'W';
    }
    pos = basic_mode_line(s, buf, buf_size, wrap_mode);

    eb_get_pos(s->b, &line_num, &col_num, s->offset);
    pos += snprintf(buf + pos, buf_size - pos, "L%d--C%d--%s",
                    line_num + 1, col_num, s->b->charset->name);
    if (s->bidir) {
        pos += snprintf(buf + pos, buf_size - pos, "--%s",
                        s->cur_rtl ? "RTL" : "LTR");
    }
    if (s->input_method) {
        pos += snprintf(buf + pos, buf_size - pos, "--%s",
                        s->input_method->name);
    }
#if 0
    pos += snprintf(buf + pos, buf_size - pos, "--[%d,%d]-[%d]",
                    s->x_disp[0], s->x_disp[1], s->y_disp);
#endif
    percent = 0;
    if (s->b->total_size > 0)
        percent = (s->offset * 100) / s->b->total_size;
    pos += snprintf(buf + pos, buf_size - pos, "--%d%%", percent);
    return pos;
}

void display_mode_line(EditState *s)
{
    char buf[MAX_SCREEN_WIDTH];
    int y = s->ytop + s->height;

    if (s->flags & WF_MODELINE) {
        s->mode->get_mode_line(s, buf, sizeof(buf));
        if (!strequal(buf, s->modeline_shadow)) {
            print_at_byte(s->screen,
                          s->xleft,
                          y,
                          s->width,
                          s->qe_state->mode_line_height,
                          buf, QE_STYLE_MODE_LINE);
            pstrcpy(s->modeline_shadow, sizeof(s->modeline_shadow), buf);
        }
    }
}

void display_window_borders(EditState *e)
{
    QEmacsState *qs = e->qe_state;

    if (e->borders_invalid) {
        if (e->flags & (WF_POPUP | WF_RSEPARATOR)) {
            CSSRect rect;
            QEColor color;

            rect.x1 = 0;
            rect.y1 = 0;
            rect.x2 = qs->width;
            rect.y2 = qs->height;
            set_clip_rectangle(qs->screen, &rect);
            color = qe_styles[QE_STYLE_WINDOW_BORDER].bg_color;
            if (e->flags & WF_POPUP) {
                fill_rectangle(qs->screen,
                               e->x1, e->y1,
                               qs->border_width, e->y2 - e->y1, color);
                fill_rectangle(qs->screen,
                               e->x2 - qs->border_width, e->y1,
                               qs->border_width, e->y2 - e->y1, color);
                fill_rectangle(qs->screen,
                               e->x1, e->y1,
                               e->x2 - e->x1, qs->border_width, color);
                fill_rectangle(qs->screen,
                               e->x1, e->y2 - qs->border_width,
                               e->x2 - e->x1, qs->border_width, color);
            }
            if (e->flags & WF_RSEPARATOR) {
                fill_rectangle(qs->screen,
                               e->x2 - qs->separator_width, e->y1,
                               qs->separator_width, e->y2 - e->y1, color);
            }
        }
        e->borders_invalid = 0;
    }
}

#if 1
/* Should move all this to display.c */

/* compute style */
static void apply_style(QEStyleDef *style, int style_index)
{
    QEStyleDef *s;

#ifndef CONFIG_WIN32
    if (style_index & QE_STYLE_TTY) {
        style->fg_color = tty_fg_colors[TTY_GET_FG(style_index)];
        style->bg_color = tty_bg_colors[TTY_GET_BG(style_index)];
    } else
#endif
    {
        s = &qe_styles[style_index & ~QE_STYLE_SEL];
        if (s->fg_color != COLOR_TRANSPARENT)
            style->fg_color = s->fg_color;
        if (s->bg_color != COLOR_TRANSPARENT)
            style->bg_color = s->bg_color;
        if (s->font_style != 0)
            style->font_style = s->font_style;
        if (s->font_size != 0)
            style->font_size = s->font_size;
    }
    /* for selection, we need a special handling because only color is
           changed */
    if (style_index & QE_STYLE_SEL) {
        s = &qe_styles[QE_STYLE_SELECTION];
        style->fg_color = s->fg_color;
        style->bg_color = s->bg_color;
    }
}

void get_style(EditState *e, QEStyleDef *style, int style_index)
{
    /* get root default style */
    *style = qe_styles[0];

    /* apply window default style */
    if (e && e->default_style != 0)
        apply_style(style, e->default_style);

    /* apply specific style */
    if (style_index != 0)
        apply_style(style, style_index);
}

void style_completion(CompleteState *cp)
{
    int i;
    QEStyleDef *style;

    style = qe_styles;
    for (i = 0; i < QE_STYLE_NB; i++, style++) {
        complete_test(cp, style->name);
    }
}

QEStyleDef *find_style(const char *name)
{
    int i;
    QEStyleDef *style;

    style = qe_styles;
    for (i = 0; i < QE_STYLE_NB; i++, style++) {
        if (strequal(style->name, name))
            return style;
    }
    return NULL;
}

const char * const qe_style_properties[] = {
#define CSS_PROP_COLOR  0
    "color",            /* color */
#define CSS_PROP_BACKGROUND_COLOR  1
    "background-color", /* color */
#define CSS_PROP_FONT_FAMILY  2
    "font-family",      /* font_family: serif|times|sans|arial|helvetica| */
                        /*              fixed|monospace|courier */
#define CSS_PROP_FONT_STYLE  3
    "font-style",       /* font_style: italic / normal */
#define CSS_PROP_FONT_WEIGHT  4
    "font-weight",      /* font_weight: bold / normal */
#define CSS_PROP_FONT_SIZE  5
    "font-size",        /* font_size: inherit / size */
#define CSS_PROP_TEXT_DECORATION  6
    "text-decoration",  /* text_decoration: none / underline */
};

void style_property_completion(CompleteState *cp)
{
    int i;

    for (i = 0; i < countof(qe_style_properties); i++) {
        complete_test(cp, qe_style_properties[i]);
    }
}

int find_style_property(const char *name)
{
    int i;

    for (i = 0; i < countof(qe_style_properties); i++) {
        if (strequal(qe_style_properties[i], name))
            return i;
    }
    return -1;
}

/* Note: we use the same syntax as CSS styles to ease merging */
void do_set_style(EditState *e, const char *stylestr,
                  const char *propstr, const char *value)
{
    QEStyleDef *style;
    int v, prop_index;

    style = find_style(stylestr);
    if (!style) {
        put_status(e, "Unknown style '%s'", stylestr);
        return;
    }

    prop_index = find_style_property(propstr);
    if (prop_index < 0) {
        put_status(e, "Unknown property '%s'", propstr);
        return;
    }

    switch (prop_index) {
    case CSS_PROP_COLOR:
        if (css_get_color(&style->fg_color, value))
            goto bad_color;
        break;
    case CSS_PROP_BACKGROUND_COLOR:
        if (css_get_color(&style->bg_color, value))
            goto bad_color;
        break;
    bad_color:
        put_status(e, "Unknown color '%s'", value);
        return;
    case CSS_PROP_FONT_FAMILY:
        v = css_get_font_family(value);
        style->font_style = (style->font_style & ~QE_FAMILY_MASK) | v;
        break;
    case CSS_PROP_FONT_STYLE:
        /* XXX: cannot handle inherit correctly */
        v = style->font_style;
        if (strequal(value, "italic")) {
            v |= QE_STYLE_ITALIC;
        } else
        if (strequal(value, "normal")) {
            v &= ~QE_STYLE_ITALIC;
        }
        style->font_style = v;
        break;
    case CSS_PROP_FONT_WEIGHT:
        /* XXX: cannot handle inherit correctly */
        v = style->font_style;
        if (strequal(value, "bold")) {
            v |= QE_STYLE_BOLD;
        } else
        if (strequal(value, "normal")) {
            v &= ~QE_STYLE_BOLD;
        }
        style->font_style = v;
        break;
    case CSS_PROP_FONT_SIZE:
        if (strequal(value, "inherit")) {
            style->font_size = 0;
        } else {
            style->font_size = strtol(value, NULL, 0);
        }
        break;
    case CSS_PROP_TEXT_DECORATION:
        /* XXX: cannot handle inherit correctly */
        if (strequal(value, "none")) {
            style->font_style &= ~QE_STYLE_UNDERLINE;
        } else
        if (strequal(value, "underline")) {
            style->font_style |= QE_STYLE_UNDERLINE;
        }
        break;
    }
}

void do_define_color(EditState *e, const char *name, const char *value)
{
    if (css_define_color(name, value))
        put_status(e, "Invalid color '%s'", value);
}
#endif

void do_set_display_size(__unused__ EditState *s, int w, int h)
{
    if (w != NO_ARG && h != NO_ARG) {
        screen_width = w;
        screen_height = h;
    }
}

/* NOTE: toggle-full-screen also hide the modeline of the current
   window and the status line */
void do_toggle_full_screen(EditState *s)
{
    QEmacsState *qs = s->qe_state;
    QEditScreen *screen = s->screen;

    qs->is_full_screen = !qs->is_full_screen;
    if (screen->dpy.dpy_full_screen)
        screen->dpy.dpy_full_screen(screen, qs->is_full_screen);
    if (qs->is_full_screen)
        s->flags &= ~WF_MODELINE;
    else
        s->flags |= WF_MODELINE;
    qs->hide_status = qs->is_full_screen;
}

void do_toggle_mode_line(EditState *s)
{
    s->flags ^= WF_MODELINE;
    do_refresh(s);
}

void do_set_system_font(EditState *s, const char *qe_font_name,
                        const char *system_fonts)
{
    int font_type;

    font_type = css_get_enum(qe_font_name, "fixed,serif,sans");
    if (font_type < 0) {
        put_status(s, "Invalid qemacs font");
        return;
    }
    pstrcpy(s->qe_state->system_fonts[font_type],
            sizeof(s->qe_state->system_fonts[0]),
            system_fonts);
}


void display_init(DisplayState *s, EditState *e, enum DisplayType do_disp)
{
    QEFont *font;
    QEStyleDef style;

    s->do_disp = do_disp;
    s->wrap = e->wrap;
    s->edit_state = e;
    /* select default values */
    get_style(e, &style, e->default_style);
    font = select_font(e->screen, style.font_style, style.font_size);
    s->eol_width = max(glyph_width(e->screen, font, '/'),
                       glyph_width(e->screen, font, '\\'));
    s->eol_width = max(s->eol_width, glyph_width(e->screen, font, '$'));
    s->default_line_height = font->ascent + font->descent;
    s->space_width = glyph_width(e->screen, font, ' ');
    s->tab_width = s->space_width * e->tab_size;
    s->width = e->width - s->eol_width;
    s->height = e->height;
    s->hex_mode = e->hex_mode;
    s->cur_hex_mode = 0;
    s->y = e->y_disp;
    s->line_num = 0;
    s->eol_reached = 0;
    s->cursor_func = NULL;
    s->eod = 0;
    release_font(e->screen, font);
}

static void display_bol_bidir(DisplayState *s, DirType base,
                              int embedding_level_max)
{
    s->base = base;
    s->x_disp = s->edit_state->x_disp[base];
    s->x = s->x_disp;
    s->style = 0;
    s->last_style = 0;
    s->fragment_index = 0;
    s->line_index = 0;
    s->nb_fragments = 0;
    s->word_index = 0;
    s->embedding_level_max = embedding_level_max;
    s->last_word_space = 0;
}

void display_bol(DisplayState *s)
{
    display_bol_bidir(s, DIR_LTR, 0);
}

static void reverse_fragments(TextFragment *str, int len)
{
    int i, len2 = len / 2;

    for (i = 0; i < len2; i++) {
        TextFragment tmp = str[i];
        str[i] = str[len - 1 - i];
        str[len - 1 - i] = tmp;
    }
}

#define LINE_SHADOW_INCR 10

/* CRC to optimize redraw. */
/* XXX: is it safe enough ? */
static unsigned int compute_crc(unsigned char *data, int size, unsigned int sum)
{
    while (size >= 4) {
        sum += ((data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3]);
        data += 4;
        size -= 4;
    }
    while (size > 0) {
        sum += data[0] << (size * 8);
        data++;
        size--;
    }
    return sum;
}

static void flush_line(DisplayState *s,
                       TextFragment *fragments, int nb_fragments,
                       int offset1, int offset2, int last)
{
    EditState *e = s->edit_state;
    QEditScreen *screen = e->screen;
    int level, pos, p, i, x_start, x, x1, y, baseline, line_height, max_descent;
    TextFragment *frag;
    QEFont *font;

    /* compute baseline and lineheight */
    baseline = 0;
    max_descent = 0;
    for (i = 0; i < nb_fragments; i++) {
        if (fragments[i].ascent > baseline)
            baseline = fragments[i].ascent;
        if (fragments[i].descent > max_descent)
            max_descent = fragments[i].descent;
    }
    if (nb_fragments == 0) {
        /* if empty line, still needs a non zero line height */
        line_height = s->default_line_height;
    } else {
        line_height = baseline + max_descent;
    }

    /* swap according to embedding level */
    for (level = s->embedding_level_max; level > 0; level--) {
        pos = 0;
        while (pos < nb_fragments) {
            if (fragments[pos].embedding_level >= level) {
                /* find all chars >= level */
                for (p = pos + 1; p < nb_fragments && fragments[p].embedding_level >= level; p++);
                reverse_fragments(fragments + pos, p - pos);
                pos = p + 1;
            } else {
                pos++;
            }
        }
    }

    if (s->base == DIR_RTL) {
        x_start = e->width - s->x;
    } else {
        x_start = s->x_disp;
    }

    /* draw everything */
    if (s->do_disp == DISP_PRINT) {
        QEStyleDef style, default_style;
        QELineShadow *ls;
        unsigned int crc;

        /* test if display needed */
        crc = compute_crc((unsigned char *)fragments,
                          sizeof(TextFragment) * nb_fragments, 0);
        crc = compute_crc((unsigned char *)s->line_chars,
                          s->line_index * sizeof(int), crc);
        if (s->line_num >= e->shadow_nb_lines) {
            /* realloc shadow */
            int n = e->shadow_nb_lines;
            e->shadow_nb_lines = n + LINE_SHADOW_INCR;
            qe_realloc(&e->line_shadow,
                       e->shadow_nb_lines * sizeof(QELineShadow));
            /* put an impossible value so that we redraw */
            memset(&e->line_shadow[n], 0xff,
                   LINE_SHADOW_INCR * sizeof(QELineShadow));
        }
        ls = &e->line_shadow[s->line_num];
        if (ls->y == s->y &&
            ls->x_start == x_start &&
            ls->height == line_height &&
            ls->crc == crc) {
            /* no display needed */
        } else {
#if 0
            printf("old=%d %d %d %d\n",
                   ls->y, ls->x_start, ls->height, ls->crc);
            printf("cur=%d %d %d %d\n",
                   s->y, x_start, line_height, crc);
#endif
            /* init line shadow */
            ls->y = s->y;
            ls->x_start = x_start;
            ls->height = line_height;
            ls->crc = crc;

            /* display ! */

            get_style(e, &default_style, 0);
            x = e->xleft;
            y = e->ytop + s->y;

            /* first display background rectangles */
            if (x_start > 0) {
                fill_rectangle(screen, x, y,
                               x_start, line_height,
                               default_style.bg_color);
            }
            x += x_start;
            for (i = 0; i < nb_fragments; i++) {
                frag = &fragments[i];
                get_style(e, &style, frag->style);
                fill_rectangle(screen, x, y, frag->width, line_height,
                               style.bg_color);
                x += frag->width;
            }
            x1 = e->xleft + s->width + s->eol_width;
            if (x < x1) {
                fill_rectangle(screen, x, y, x1 - x, line_height,
                               default_style.bg_color);
            }

            /* then display text */
            x = e->xleft;
            if (x_start > 0) {
                /* RTL eol mark */
                if (!last && s->base == DIR_RTL) {
                    /* XXX: optimize that ! */
                    unsigned int markbuf[1];

                    font = select_font(screen,
                                       default_style.font_style,
                                       default_style.font_size);
                    markbuf[0] = '/';
                    draw_text(screen, font, x, y + font->ascent,
                              markbuf, 1, default_style.fg_color);
                    release_font(screen, font);
                }
            }
            x += x_start;
            for (i = 0; i < nb_fragments; i++) {
                frag = &fragments[i];
                get_style(e, &style, frag->style);
                font = select_font(screen,
                                   style.font_style, style.font_size);
                draw_text(screen, font, x, y + baseline,
                          s->line_chars + frag->line_index,
                          frag->len, style.fg_color);
                x += frag->width;
                release_font(screen, font);
            }
            x1 = e->xleft + s->width + s->eol_width;
            if (x < x1) {
                /* LTR eol mark */
                if (!last && s->base == DIR_LTR) {
                    /* XXX: optimize that ! */
                    unsigned int markbuf[1];

                    font = select_font(screen,
                                       default_style.font_style,
                                       default_style.font_size);
                    markbuf[0] = '\\';
                    draw_text(screen, font,
                              e->xleft + s->width, y + font->ascent,
                              markbuf, 1, default_style.fg_color);
                    release_font(screen, font);
                }
            }
        }
    }

    /* call cursor callback */
    if (s->cursor_func) {

        x = x_start;
        /* mark eol */
        if (offset1 >= 0 && offset2 >= 0 &&
            s->base == DIR_RTL &&
            s->cursor_func(s, offset1, offset2, s->line_num,
                           x, s->y, -s->eol_width, line_height, e->hex_mode)) {
            s->eod = 1;
        }

        for (i = 0; i < nb_fragments; i++) {
            int w, k, j, _offset1, _offset2;

            frag = &fragments[i];

            j = frag->line_index;
            for (k = 0; k < frag->len; k++) {
                int hex_mode;
                _offset1 = s->line_offsets[j][0];
                _offset2 = s->line_offsets[j][1];
                hex_mode = s->line_hex_mode[j];
                w = s->line_char_widths[j];
                if (hex_mode == s->hex_mode || s->hex_mode == -1) {
                    if (s->base == DIR_RTL) {
                        if (_offset1 >= 0 && _offset2 >= 0 &&
                            s->cursor_func(s, _offset1, _offset2, s->line_num,
                                           x + w, s->y, -w, line_height,
                                           hex_mode))
                            s->eod = 1;
                    } else {
                        if (_offset1 >= 0 && _offset2 >= 0 &&
                            s->cursor_func(s, _offset1, _offset2, s->line_num,
                                           x, s->y, w, line_height,
                                           hex_mode))
                            s->eod = 1;
                    }
                }
                x += w;
                j++;
            }
        }
        /* mark eol */
        if (offset1 >= 0 && offset2 >= 0 &&
            s->base == DIR_LTR &&
            s->cursor_func(s, offset1, offset2, s->line_num,
                           x, s->y, s->eol_width, line_height, e->hex_mode)) {
            s->eod = 1;
        }
    }
#if 0
    printf("y=%d line_num=%d line_height=%d baseline=%d\n",
           s->y, s->line_num, line_height, baseline);
#endif
    s->y += line_height;
    s->line_num++;
}

/* keep 'n' line chars at the start of the line */
static void keep_line_chars(DisplayState *s, int n)
{
    int index;

    index = s->line_index - n;
    memmove(s->line_chars, s->line_chars + index, n * sizeof(unsigned int));
    memmove(s->line_offsets, s->line_offsets + index, n * 2 * sizeof(unsigned int));
    memmove(s->line_char_widths, s->line_char_widths + index, n * sizeof(short));
    s->line_index = n;
}

#ifndef CONFIG_UNICODE_JOIN

/* fallback unicode functions */

int unicode_to_glyphs(unsigned int *dst, unsigned int *char_to_glyph_pos,
                      int dst_size, unsigned int *src, int src_size, int reverse)
{
    int len, i;

    len = src_size;
    if (len > dst_size)
        len = dst_size;
    memcpy(dst, src, len * sizeof(unsigned int));
    if (char_to_glyph_pos) {
        for (i = 0; i < len; i++)
            char_to_glyph_pos[i] = i;
    }
    return len;
}

#endif

/* layout of a word fragment */
static void flush_fragment(DisplayState *s)
{
    int w, len, style_index, i, j;
    QEditScreen *screen = s->edit_state->screen;
    TextFragment *frag;
    QEStyleDef style;
    QEFont *font;
    unsigned int char_to_glyph_pos[MAX_WORD_SIZE];
    int nb_glyphs, dst_max_size, ascent, descent;

    if (s->fragment_index == 0)
        return;
    if (s->nb_fragments >= MAX_SCREEN_WIDTH)
        goto the_end;

    /* update word start index if needed */
    if (s->nb_fragments >= 1 && s->last_word_space != s->last_space) {
        s->last_word_space = s->last_space;
        s->word_index = s->nb_fragments;
    }

    /* convert fragment to glyphs (currently font independent, but may
       change) */
    //dst_max_size = MAX_SCREEN_WIDTH - s->line_index;
    //if (dst_max_size <= 0)
    //    goto the_end;
    dst_max_size = MAX_WORD_SIZE; // assuming s->fragment_index MAX_WORD_SIZE
    nb_glyphs = unicode_to_glyphs(s->line_chars + s->line_index,
                                  char_to_glyph_pos, dst_max_size,
                                  s->fragment_chars, s->fragment_index,
                                  s->last_embedding_level & 1);

    /* compute new offsets */
    j = s->line_index;
    for (i = 0; i < nb_glyphs; i++) {
        s->line_offsets[j][0] = -1;
        s->line_offsets[j][1] = -1;
        j++;
    }
    for (i = 0; i < s->fragment_index; i++) {
        int offset1, offset2;
        j = s->line_index + char_to_glyph_pos[i];
        offset1 = s->fragment_offsets[i][0];
        offset2 = s->fragment_offsets[i][1];
        s->line_hex_mode[j] = s->fragment_hex_mode[i];
        /* we suppose the the chars are contiguous */
        if (s->line_offsets[j][0] == -1 ||
            s->line_offsets[j][0] > offset1)
            s->line_offsets[j][0] = offset1;
        if (s->line_offsets[j][1] == -1 ||
            s->line_offsets[j][1] < offset2)
            s->line_offsets[j][1] = offset2;
    }

    style_index = s->last_style;
    if (style_index == QE_STYLE_DEFAULT)
        style_index = s->edit_state->default_style;
    get_style(s->edit_state, &style, style_index);
    /* select font according to current style */
    font = select_font(screen, style.font_style, style.font_size);
    j = s->line_index;
    ascent = font->ascent;
    descent = font->descent;
    if (s->line_chars[j] == '\t') {
        int x1;
        /* special case for TAB */
        x1 = (s->x - s->x_disp) % s->tab_width;
        w = s->tab_width - x1;
        /* display a single space */
        s->line_chars[j] = ' ';
        s->line_char_widths[j] = w;
    } else {
        /* XXX: use text metrics for full fragment */
        w = 0;
        for (i = 0; i < nb_glyphs; i++) {
            QECharMetrics metrics;
            text_metrics(screen, font, &metrics, &s->line_chars[j], 1);
            if (metrics.font_ascent > ascent)
                ascent = metrics.font_ascent;
            if (metrics.font_descent > descent)
                descent = metrics.font_descent;
            s->line_char_widths[j] = metrics.width;
            w += s->line_char_widths[j];
            j++;
        }
    }
    release_font(screen, font);

    /* add the fragment */
    frag = &s->fragments[s->nb_fragments++];
    frag->width = w;
    frag->line_index = s->line_index;
    frag->len = nb_glyphs;
    frag->embedding_level = s->last_embedding_level;
    frag->style = style_index;
    frag->ascent = ascent;
    frag->descent = descent;
    frag->dummy = 0;

    s->line_index += nb_glyphs;
    s->x += frag->width;

    switch (s->wrap) {
    case WRAP_TRUNCATE:
        break;
    case WRAP_LINE:
        while (s->x > s->width) {
            int len1, w1, ww, n;
            //printf("x=%d maxw=%d len=%d\n", s->x, s->width, frag->len);
            frag = &s->fragments[s->nb_fragments - 1];
            /* find fragment truncation to fit the line */
            len = len1 = frag->len;
            w1 = s->x;
            while (s->x > s->width) {
                len--;
                ww = s->line_char_widths[frag->line_index + len];
                s->x -= ww;
                if (len == 0 && s->x == 0) {
                    /* avoid looping by putting at least one char per line */
                    len = 1;
                    s->x += ww;
                    break;
                }
            }
            len1 -= len;
            w1 -= s->x;
            frag->len = len;
            frag->width -= w1;
            //printf("after: x=%d w1=%d\n", s->x, w1);
            n = s->nb_fragments;
            if (len == 0)
                n--;
            flush_line(s, s->fragments, n, -1, -1, 0);

            /* move the remaining fragment to next line */
            s->nb_fragments = 0;
            s->x = 0;
            if (s->edit_state->line_numbers) {
                /* should skip line number column if present */
                //s->x = s->space_width * 8;
            }
            if (len1 > 0) {
                memmove(s->fragments, frag, sizeof(TextFragment));
                frag = s->fragments;
                frag->width = w1;
                frag->line_index = 0;
                frag->len = len1;
                s->nb_fragments = 1;
                s->x += w1;
            }
            keep_line_chars(s, len1);
        }
        break;
    case WRAP_WORD:
        if (s->x > s->width) {
            int index;

            flush_line(s, s->fragments, s->word_index, -1, -1, 0);

            /* put words on next line */
            index = s->fragments[s->word_index].line_index;
            memmove(s->fragments, s->fragments + s->word_index,
                    (s->nb_fragments - s->word_index) * sizeof(TextFragment));
            s->nb_fragments -= s->word_index;
            s->x = 0;
            if (s->edit_state->line_numbers) {
                /* skip line number column if present */
                //s->x = s->space_width * 8;
            }
            for (i = 0; i < s->nb_fragments; i++) {
                s->fragments[i].line_index -= index;
                s->x += s->fragments[i].width;
            }
            keep_line_chars(s, s->line_index - index);
            s->word_index = 0;
        }
        break;
    }
 the_end:
    s->fragment_index = 0;
}

int display_char_bidir(DisplayState *s, int offset1, int offset2,
                       int embedding_level, int ch)
{
    int space, style, istab;
    EditState *e;

    style = s->style;

    /* special code to colorize block */
    e = s->edit_state;
    if (e->show_selection) {
        int mark = e->b->mark;
        int offset = e->offset;

        if ((offset1 >= offset && offset1 < mark) ||
            (offset1 >= mark && offset1 < offset))
            style |= QE_STYLE_SEL;
    }
    /* special patch for selection in hex mode */
    if (offset1 == offset2) {
        offset1 = -1;
        offset2 = -1;
    }

    space = (ch == ' ');
    istab = (ch == '\t');
    /* a fragment is a part of word where style/embedding_level do not
       change. For TAB, only one fragment containing it is sent */
    if ((s->fragment_index >= MAX_WORD_SIZE) ||
        istab ||
        (s->fragment_index >= 1 &&
         (space != s->last_space ||
          style != s->last_style ||
          embedding_level != s->last_embedding_level))) {
        /* flush the current fragment if needed */
        flush_fragment(s);
    }

    /* store the char and its embedding level */
    s->fragment_chars[s->fragment_index] = ch;
    s->fragment_offsets[s->fragment_index][0] = offset1;
    s->fragment_offsets[s->fragment_index][1] = offset2;
    s->fragment_hex_mode[s->fragment_index] = s->cur_hex_mode;
    s->fragment_index++;

    s->last_space = space;
    s->last_style = style;
    s->last_embedding_level = embedding_level;

    if (istab) {
        flush_fragment(s);
    }
    return 0;
}

void display_printhex(DisplayState *s, int offset1, int offset2,
                      unsigned int h, int n)
{
    int i, v;
    EditState *e = s->edit_state;

    s->cur_hex_mode = 1;
    for (i = 0; i < n; i++) {
        v = (h >> ((n - i - 1) * 4)) & 0xf;
        if (v >= 10)
            v += 'a' - 10;
        else
            v += '0';
        /* XXX: simplistic */
        if (e->hex_nibble == i) {
            display_char(s, offset1, offset2, v);
        } else {
            display_char(s, offset1, offset1, v);
        }
    }
    s->cur_hex_mode = 0;
}

void display_printf(DisplayState *ds, int offset1, int offset2,
                    const char *fmt, ...)
{
    char buf[256], *p;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    p = buf;
    if (*p) {
        display_char(ds, offset1, offset2, *p++);
        while (*p) {
            display_char(ds, -1, -1, *p++);
        }
    }
}

/* end of line */
void display_eol(DisplayState *s, int offset1, int offset2)
{
    flush_fragment(s);

    /* note: the line may be empty */
    flush_line(s, s->fragments, s->nb_fragments, offset1, offset2, 1);
}

/* temporary function for backward compatibility */
static void display1(DisplayState *s)
{
    EditState *e = s->edit_state;
    int offset;

    s->eod = 0;
    offset = e->offset_top;
    for (;;) {
        offset = e->mode->text_display(e, s, offset);
        /* EOF reached ? */
        if (offset < 0)
            break;

        switch (s->do_disp) {
        case DISP_CURSOR:
            if (s->eod)
                return;
            break;
        default:
        case DISP_PRINT:
            if (s->y >= s->height)
                return; /* end of screen */
            break;
        case DISP_CURSOR_SCREEN:
            if (s->eod || s->y >= s->height)
                return;
            break;
        }
    }
}

/******************************************************/
int text_backward_offset(EditState *s, int offset)
{
    int line, col;

    eb_get_pos(s->b, &line, &col, offset);
    return eb_goto_pos(s->b, line, 0);
}

#ifdef CONFIG_UNICODE_JOIN
/* max_size should be >= 2 */
static int bidir_compute_attributes(TypeLink *list_tab, int max_size,
                                    EditBuffer *b, int offset)
{
    TypeLink *p;
    FriBidiCharType type, ltype;
    int left, offset1;
    unsigned int c;

    p = list_tab;
    /* Add the starting link */
    p->type = FRIBIDI_TYPE_SOT;
    p->len = 0;
    p->pos = 0;
    p++;
    left = max_size - 2;

    ltype = FRIBIDI_TYPE_SOT;

    for (;;) {
        offset1 = offset;
        c = eb_nextc(b, offset, &offset);
        if (c == '\n')
            break;
        type = fribidi_get_type(c);
        /* if not enough room, increment last link */
        if (type != ltype && left > 0) {
            p->type = type;
            p->pos = offset1;
            p->len = 1;
            p++;
            left--;
            ltype = type;
        } else {
            p[-1].len++;
        }
    }

    /* Add the ending link */
    p->type = FRIBIDI_TYPE_EOT;
    p->len = 0;
    p->pos = offset1;
    p++;

    return p - list_tab;
}
#endif

#ifndef CONFIG_TINY

/************************************************************/
/* colorization handling */
/* NOTE: only one colorization mode can be selected at a time for a
   buffer */

/* Gets the colorized line beginning at 'offset'. Its length
   excluding '\n' is returned */

#define COLORIZED_LINE_PREALLOC_SIZE 64

int generic_get_colorized_line(EditState *s, unsigned int *buf, int buf_size,
                               int *offsetp, int line_num)
{
    int len, l, line, col, offset;
    int colorize_state;

    /* invalidate cache if needed */
    if (s->colorize_max_valid_offset != INT_MAX) {
        eb_get_pos(s->b, &line, &col, s->colorize_max_valid_offset);
        line++;
        if (line < s->colorize_nb_valid_lines)
            s->colorize_nb_valid_lines = line;
        s->colorize_max_valid_offset = INT_MAX;
    }

    /* realloc line buffer if needed */
    if ((line_num + 2) > s->colorize_nb_lines) {
        s->colorize_nb_lines = line_num + 2 + COLORIZED_LINE_PREALLOC_SIZE;
        if (!qe_realloc(&s->colorize_states, s->colorize_nb_lines))
            return 0;
    }

    /* propagate state if needed */
    if (line_num >= s->colorize_nb_valid_lines) {
        if (s->colorize_nb_valid_lines == 0) {
            s->colorize_states[0] = 0; /* initial state : zero */
            s->colorize_nb_valid_lines = 1;
        }
        offset = eb_goto_pos(s->b, s->colorize_nb_valid_lines - 1, 0);
        colorize_state = s->colorize_states[s->colorize_nb_valid_lines - 1];

        for (l = s->colorize_nb_valid_lines; l <= line_num; l++) {
            len = eb_get_line(s->b, buf, buf_size, &offset);
            // XXX: should force \0 instead of \n
            buf[len] = '\n';

            s->colorize_func(buf, len, &colorize_state, 1);
            s->colorize_states[l] = colorize_state;
        }
    }

    /* compute line color */
    len = eb_get_line(s->b, buf, buf_size, offsetp);
    // XXX: should force \0 instead of \n
    buf[len] = '\n';

    colorize_state = s->colorize_states[line_num];
    s->colorize_func(buf, len, &colorize_state, 0);

    /* XXX: if state is same as previous, minimize invalid region? */
    s->colorize_states[line_num + 1] = colorize_state;

    s->colorize_nb_valid_lines = line_num + 2;
    return len;
}

/* invalidate the colorize data */
static void colorize_callback(__unused__ EditBuffer *b,
                              void *opaque,
                              __unused__ enum LogOperation op,
                              int offset,
                              __unused__ int size)
{
    EditState *e = opaque;

    if (offset < e->colorize_max_valid_offset)
        e->colorize_max_valid_offset = offset;
}

void set_colorize_func(EditState *s, ColorizeFunc colorize_func)
{
    /* invalidate the previous states & free previous colorizer */
    eb_free_callback(s->b, colorize_callback, s);
    qe_free(&s->colorize_states);
    s->colorize_nb_lines = 0;
    s->colorize_nb_valid_lines = 0;
    s->colorize_max_valid_offset = INT_MAX;
    s->get_colorized_line = get_non_colorized_line;
    s->colorize_func = NULL;

    if (colorize_func) {
        eb_add_callback(s->b, colorize_callback, s);
        s->get_colorized_line = generic_get_colorized_line;
        s->colorize_func = colorize_func;
    }
}

#else /* CONFIG_TINY */

void set_colorize_func(EditState *s, ColorizeFunc colorize_func)
{
    s->get_colorized_line = get_non_colorized_line;
}

#endif /* CONFIG_TINY */

int get_non_colorized_line(EditState *s, unsigned int *buf, int buf_size,
                           int *offsetp, int line_num)
{
    /* compute line color */
    int len = eb_get_line(s->b, buf, buf_size, offsetp);
    // XXX: should force \0 instead of \n
    buf[len] = '\n';

    return len;
}

#define RLE_EMBEDDINGS_SIZE    128
#define COLORED_MAX_LINE_SIZE  1024

int text_display(EditState *s, DisplayState *ds, int offset)
{
    int c;
    int offset0, offset1, line_num, col_num;
    TypeLink embeds[RLE_EMBEDDINGS_SIZE], *bd;
    int embedding_level, embedding_max_level;
    FriBidiCharType base;
    unsigned int colored_chars[COLORED_MAX_LINE_SIZE];
    int char_index, colored_nb_chars;

    line_num = 0; /* avoid warning */
    if (s->line_numbers || s->get_colorized_line != get_non_colorized_line) {
        eb_get_pos(s->b, &line_num, &col_num, offset);
    }

    offset1 = offset;

#ifdef CONFIG_UNICODE_JOIN
    /* compute the embedding levels and rle encode them */
    if (s->bidir
    &&  bidir_compute_attributes(embeds, RLE_EMBEDDINGS_SIZE,
                                 s->b, offset) > 2)
    {
        base = FRIBIDI_TYPE_WL;
        fribidi_analyse_string(embeds, &base, &embedding_max_level);
        /* assure that base has only two possible values */
        if (base != FRIBIDI_TYPE_RTL)
            base = FRIBIDI_TYPE_LTR;
    } else
#endif
    {
        /* all line is at embedding level 0 */
        embedding_max_level = 0;
        embeds[1].level = 0;
        embeds[2].pos = 0x7fffffff;
        base = FRIBIDI_TYPE_LTR;
    }

    display_bol_bidir(ds, base, embedding_max_level);

    /* line numbers */
    if (s->line_numbers) {
        ds->style = QE_STYLE_COMMENT;
        display_printf(ds, -1, -1, "%6d  ", line_num + 1);
        ds->style = 0;
    }

    /* prompt display */
    if (s->prompt && offset1 == 0) {
        const char *p;
        p = s->prompt;
        while (*p) {
            display_char(ds, -1, -1, *p++);
        }
    }

    /* colorize */
    colored_nb_chars = 0;
    offset0 = offset;
    if (s->get_colorized_line != get_non_colorized_line) {
        colored_nb_chars = s->get_colorized_line(s, colored_chars,
                                                 countof(colored_chars),
                                                 &offset0, line_num);
    }

#if 1
    /* colorize regions */
    if (s->curline_style || s->region_style) {
        if (s->get_colorized_line == get_non_colorized_line) {
            offset0 = offset;
            colored_nb_chars = eb_get_line(s->b, colored_chars,
                countof(colored_chars), &offset0);
        }
        /* CG: Should combine styles instead of replacing */
        if (s->region_style) {
            int line, start, stop;

            if (s->b->mark < s->offset) {
                start = max(offset, s->b->mark);
                stop = min(offset0, s->offset);
            } else {
                start = max(offset, s->offset);
                stop = min(offset0, s->b->mark);
            }
            if (start < stop) {
                /* Compute character positions */
                eb_get_pos(s->b, &line, &start, start);
                if (stop >= offset0)
                    stop = colored_nb_chars;
                else
                    eb_get_pos(s->b, &line, &stop, stop);
                clear_color(colored_chars + start, stop - start);
                set_color(colored_chars + start, colored_chars + stop,
                          s->region_style);
            }
        } else
        if (s->curline_style && s->offset >= offset && s->offset <= offset0) {
            clear_color(colored_chars, colored_nb_chars);
            set_color(colored_chars, colored_chars + colored_nb_chars,
                      s->curline_style);
        }
    }
#endif

    bd = embeds + 1;
    char_index = 0;
    for (;;) {
        offset0 = offset;
        if (offset >= s->b->total_size) {
            display_eol(ds, offset0, offset0 + 1);
            offset = -1; /* signal end of text */
            break;
        } else {
            /* Should simplify this if colored line was computed */
            ds->style = 0;
            if (char_index < colored_nb_chars) {
                /* colored_chars should just be a style array */
                c = colored_chars[char_index];
                ds->style = c >> STYLE_SHIFT;
            }
            c = eb_nextc(s->b, offset, &offset);
            if (c == '\n') {
                display_eol(ds, offset0, offset);
                break;
            }
            /* compute embedding from RLE embedding list */
            if (offset0 >= bd[1].pos)
                bd++;
            embedding_level = bd[0].level;
            /* XXX: use embedding level for all cases ? */
            /* CG: should query screen or window about display methods */
            if ((c < ' ' && c != '\t') || c == 127) {
                display_printf(ds, offset0, offset, "^%c", ('@' + c) & 127);
            } else
            if (c >= 0x10000) {
                /* currently, we cannot display these chars */
                display_printf(ds, offset0, offset, "\\U%08x", c);
            } else
            if (c >= 256 && s->qe_state->show_unicode == 1) {
                display_printf(ds, offset0, offset, "\\u%04x", c);
            } else {
                display_char_bidir(ds, offset0, offset, embedding_level, c);
            }
            char_index++;
        }
    }
    return offset;
}

/* Generic display algorithm with automatic fit */
static void generic_text_display(EditState *s)
{
    CursorContext m1, *m = &m1;
    DisplayState ds1, *ds = &ds1;
    int x1, xc, yc, offset;

    /* if the cursor is before the top of the display zone, we must
       resync backward */
    if (s->offset < s->offset_top) {
        s->offset_top = s->mode->text_backward_offset(s, s->offset);
    }

    if (s->display_invalid) {
        /* invalidate the line shadow buffer */
        qe_free(&s->line_shadow);
        s->shadow_nb_lines = 0;
        s->display_invalid = 0;
    }

    /* find cursor position with the current x_disp & y_disp and
       update y_disp so that we display only the needed lines */
    display_init(ds, s, DISP_CURSOR_SCREEN);
    ds->cursor_opaque = m;
    ds->cursor_func = cursor_func;
    memset(m, 0, sizeof(*m));
    m->offsetc = s->offset;
    m->xc = m->yc = NO_CURSOR;
    offset = s->offset_top;
    for (;;) {
        if (ds->y <= 0) {
            s->offset_top = offset;
            s->y_disp = ds->y;
        }
        offset = s->mode->text_display(s, ds, offset);
        if (offset < 0 || ds->y >= s->height || m->xc != NO_CURSOR)
            break;
    }
    //printf("cursor: xc=%d yc=%d linec=%d\n", m->xc, m->yc, m->linec);
    if (m->xc == NO_CURSOR) {
        /* if no cursor found then we compute offset_top so that we
           have a chance to find the cursor in a small amount of time */
        display_init(ds, s, DISP_CURSOR_SCREEN);
        ds->cursor_opaque = m;
        ds->cursor_func = cursor_func;
        ds->y = 0;
        offset = s->mode->text_backward_offset(s, s->offset);
        s->mode->text_display(s, ds, offset);
        if (m->xc == NO_CURSOR) {
            /* XXX: should not happen */
            printf("ERROR: cursor not found\n");
            ds->y = 0;
        } else {
            ds->y = m->yc + m->cursor_height;
        }

        while (ds->y < s->height && offset > 0) {
            offset = s->mode->text_backward_offset(s, offset - 1);
            s->mode->text_display(s, ds, offset);
        }
        s->offset_top = offset;
        /* adjust y_disp so that the cursor is at the bottom of the
           screen */
        s->y_disp = s->height - ds->y;
    } else {
        yc = m->yc;
        if (yc < 0) {
            s->y_disp -= yc;
        } else if ((yc + m->cursor_height) >= s->height) {
            s->y_disp += s->height - (yc + m->cursor_height);
        }
    }

    /* update x cursor position if needed. Note that we distinguish
       between rtl and ltr margins. We try to have x_disp == 0 as much
       as possible */
    if (s->wrap == WRAP_TRUNCATE) {
        xc = m->xc;
        x1 = xc - s->x_disp[m->basec];
        if (x1 >= 0 && x1 < ds->width - ds->eol_width) {
            s->x_disp[m->basec] = 0;
        } else if (xc < 0) {
            s->x_disp[m->basec] -= xc;
        } else if (xc >= ds->width) {
            s->x_disp[m->basec] += ds->width - xc - ds->eol_width;
        }
    } else {
        s->x_disp[0] = 0;
        s->x_disp[1] = 0;
    }

    /* now we can display the text and get the real cursor position !  */

    display_init(ds, s, DISP_PRINT);
    ds->cursor_opaque = m;
    ds->cursor_func = cursor_func;
    m->offsetc = s->offset;
    m->xc = m->yc = NO_CURSOR;
    display1(ds);
    /* display the remaining region */
    if (ds->y < s->height) {
        QEStyleDef default_style;
        get_style(s, &default_style, 0);
        fill_rectangle(s->screen, s->xleft, s->ytop + ds->y,
                       s->width, s->height - ds->y,
                       default_style.bg_color);
        /* do not forget to erase the line shadow  */
        memset(&s->line_shadow[ds->line_num], 0xff,
               (s->shadow_nb_lines - ds->line_num) * sizeof(QELineShadow));
    }
    xc = m->xc;
    yc = m->yc;

    if (s->qe_state->active_window == s) {
        int x, y, w, h;
        x = s->xleft + xc;
        y = s->ytop + yc;
        w = m->cursor_width;
        h = m->cursor_height;
        if (s->screen->dpy.dpy_cursor_at) {
            /* hardware cursor */
            s->screen->dpy.dpy_cursor_at(s->screen, x, y, w, h);
        } else {
            /* software cursor */
            if (w < 0) {
                x += w;
                w = -w;
            }
            fill_rectangle(s->screen, x, y, w, h, QECOLOR_XOR);
            /* invalidate line so that the cursor will be erased next time */
            memset(&s->line_shadow[m->linec], 0xff,
                   sizeof(QELineShadow));
        }
    }
    s->cur_rtl = (m->dirc == DIR_RTL);
#if 0
    printf("cursor1: xc=%d yc=%d w=%d h=%d linec=%d\n",
           m->xc, m->yc, m->cursor_width, m->cursor_height, m->linec);
#endif
}

typedef struct ExecCmdState {
    EditState *s;
    CmdDef *d;
    int nb_args;
    int argval;
    int key;
    const char *ptype;
    CmdArg args[MAX_CMD_ARGS];
    unsigned char args_type[MAX_CMD_ARGS];
    char default_input[512]; /* default input if none given */
} ExecCmdState;

/* Signature based dispatcher.
   So far 144 qemacs commands have these signatures:
   - void (*)(EditState *); (68)
   - void (*)(EditState *, int); (35)
   - void (*)(EditState *, const char *); (19)
   - void (*)(EditState *, int, int); (2)
   - void (*)(EditState *, const char *, int); (2)
   - void (*)(EditState *, const char *, const char *); (6)
   - void (*)(EditState *, const char *, const char *, const char *); (2)
   - void (*)(EditState *, const char *, const char *, int); (2)
*/
void call_func(CmdSig sig, CmdProto func, __unused__ int nb_args,
               CmdArg *args, __unused__ unsigned char *args_type)
{
    switch (sig) {
    case CMD_void:
        (*func.func)();
        break;
    case CMD_ES:     /* ES, no other arguments */
        (*func.ES)(args[0].s);
        break;
    case CMD_ESi:    /* ES + integer */
        (*func.ESi)(args[0].s, args[1].n);
        break;
    case CMD_ESs:    /* ES + string */
        (*func.ESs)(args[0].s, args[1].p);
        break;
    case CMD_ESss:   /* ES + string + string */
        (*func.ESss)(args[0].s, args[1].p, args[2].p);
        break;
    case CMD_ESsi:   /* ES + string + integer */
        (*func.ESsi)(args[0].s, args[1].p, args[2].n);
        break;
    case CMD_ESii:   /* ES + integer + integer */
        (*func.ESii)(args[0].s, args[1].n, args[2].n);
        break;
    case CMD_ESssi:  /* ES + string + string + integer */
        (*func.ESssi)(args[0].s, args[1].p, args[2].p, args[3].n);
        break;
    case CMD_ESsss:  /* ES + string + string + string */
        (*func.ESsss)(args[0].s, args[1].p, args[2].p, args[3].p);
        break;
    }
}

static void get_param(const char **pp, char *param, int param_size, int osep, int sep)
{
    const char *p;
    char *q;

    param_size--;
    p = *pp;
    if (*p == osep) {
        p++;
        if (param) {
            q = param;
            while (*p != sep && *p != '\0') {
                if ((q - param) < param_size)
                    *q++ = *p;
                p++;
            }
            *q = '\0';
        } else {
            while (*p != sep && *p != '\0')
                p++;
        }
        if (*p == sep)
            p++;
    } else {
        if (param)
            param[0] = '\0';
    }
    *pp = p;
}

/* return -1 if error, 0 if no more args, 1 if one arg parsed */
static int parse_arg(const char **pp, unsigned char *argtype,
                     char *prompt, int prompt_size,
                     char *completion, int completion_size,
                     char *history, int history_size)
{
    int tc, type;
    const char *p;

    p = *pp;
    type = 0;
    if (*p == 'k') {
        p++;
        type = CMD_ARG_USE_KEY;
    }
    if (*p == 'u') {
        p++;
        type = CMD_ARG_USE_ARGVAL;
    }
    if (*p == '\0')
        return 0;
    tc = *p++;
    get_param(&p, prompt, prompt_size, '{', '}');
    get_param(&p, completion, completion_size, '[', ']');
    get_param(&p, history, history_size, '|', '|');
    switch (tc) {
    case 'i':
        type |= CMD_ARG_INT;
        break;
    case 'v':
        type |= CMD_ARG_INTVAL;
        break;
    case 's':
        type |= CMD_ARG_STRING;
        break;
    case 'S':   /* used in define_kbd_macro, and mode selection */
        type |= CMD_ARG_STRINGVAL;
        break;
    default:
        return -1;
    }
    *pp = p;
    *argtype = type;
    return 1;
}

static void arg_edit_cb(void *opaque, char *str);
static void parse_args(ExecCmdState *es);
static void free_cmd(ExecCmdState *es);

void exec_command(EditState *s, CmdDef *d, int argval, int key)
{
    ExecCmdState *es;
    const char *argdesc;

    argdesc = d->name + strlen(d->name) + 1;
    if (*argdesc == '*') {
        argdesc++;
        if (s->b->flags & BF_READONLY) {
            put_status(s, "Buffer is read only");
            return;
        }
    }

    es = qe_malloc(ExecCmdState);
    if (!es)
        return;

    es->s = s;
    es->d = d;
    es->argval = argval;
    es->key = key;
    es->nb_args = 0;

    /* first argument is always the window */
    es->args[0].s = s;
    es->args_type[0] = CMD_ARG_WINDOW;
    es->nb_args++;
    es->ptype = argdesc;

    parse_args(es);
}

/* parse as much arguments as possible. ask value to user if possible */
static void parse_args(ExecCmdState *es)
{
    EditState *s = es->s;
    QEmacsState *qs = s->qe_state;
    QErrorContext ec;
    CmdDef *d = es->d;
    char prompt[256];
    char completion_name[64];
    char history[32];
    unsigned char arg_type;
    int ret, rep_count, get_arg, type, use_argval, use_key;

    for (;;) {
        ret = parse_arg(&es->ptype, &arg_type,
                        prompt, sizeof(prompt),
                        completion_name, sizeof(completion_name),
                        history, sizeof(history));
        if (ret < 0)
            goto fail;
        if (ret == 0)
            break;
        if (es->nb_args >= MAX_CMD_ARGS)
            goto fail;
        use_argval = arg_type & CMD_ARG_USE_ARGVAL;
        use_key = arg_type & CMD_ARG_USE_KEY;
        type = arg_type & CMD_ARG_TYPE_MASK;
        es->args_type[es->nb_args] = type;
        get_arg = 0;
        switch (type) {
        case CMD_ARG_INTVAL:
            es->args[es->nb_args].n = d->val;
            break;
        case CMD_ARG_STRINGVAL:
            es->args[es->nb_args].p = prompt;
            break;
        case CMD_ARG_INT:
            if (use_key) {
                es->args[es->nb_args].n = es->key;
            } else
            if (use_argval && es->argval != NO_ARG) {
                es->args[es->nb_args].n = es->argval;
                es->argval = NO_ARG;
            } else {
                /* CG: Should add syntax for default value if no prompt */
                es->args[es->nb_args].n = NO_ARG;
                get_arg = 1;
            }
            break;
        case CMD_ARG_STRING:
            if (use_argval && es->argval != NO_ARG) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%d", es->argval);
                es->args[es->nb_args].p = qe_strdup(buf);
                es->argval = NO_ARG;
            } else {
                es->args[es->nb_args].p = NULL;
                get_arg = 1;
                break;
            }
        }
        es->nb_args++;
        /* if no argument specified, try to ask it to the user */
        if (get_arg && prompt[0] != '\0') {
            char def_input[1024];

            /* XXX: currently, default input is handled non generically */
            def_input[0] = '\0';
            es->default_input[0] = '\0';
            if (strequal(completion_name, "file")) {
                get_default_path(s, def_input, sizeof(def_input));
            } else
            if (strequal(completion_name, "buffer")) {
                EditBuffer *b;
                if (d->action.ESs == do_switch_to_buffer)
                    b = predict_switch_to_buffer(s);
                else
                    b = s->b;
                pstrcpy(es->default_input, sizeof(es->default_input), b->name);
            }
            if (es->default_input[0] != '\0') {
                pstrcat(prompt, sizeof(prompt), "(default ");
                pstrcat(prompt, sizeof(prompt), es->default_input);
                pstrcat(prompt, sizeof(prompt), ") ");
            }
            minibuffer_edit(def_input, prompt,
                            get_history(history),
                            find_completion(completion_name),
                            arg_edit_cb, es);
            return;
        }
    }

    /* all arguments are parsed: we can now execute the command */
    /* argval is handled as repetition count if not taken as argument */
    if (es->argval != NO_ARG && es->argval > 1) {
        rep_count = es->argval;
    } else {
        rep_count = 1;
    }

    qs->this_cmd_func = d->action.func;

    do {
        /* special case for hex mode */
        if (d->action.ESii != do_char) {
            s->hex_nibble = 0;
            /* special case for character composing */
            if (d->action.ESi != do_backspace)
                s->compose_len = 0;
        }
#ifndef CONFIG_TINY
        save_selection();
#endif
        /* Save and restore ec context */
        ec = qs->ec;
        qs->ec.function = d->name;
        call_func(d->sig, d->action, es->nb_args, es->args, es->args_type);
        qs->ec = ec;
        /* CG: This doesn't work if the function needs input */
        /* CG: Should test for abort condition */
        /* CG: Should follow qs->active_window ? */
    } while (--rep_count > 0);

    qs->last_cmd_func = qs->this_cmd_func;
 fail:
    free_cmd(es);
}

static void free_cmd(ExecCmdState *es)
{
    int i;

    /* free allocated parameters */
    for (i = 0; i < es->nb_args; i++) {
        switch (es->args_type[i]) {
        case CMD_ARG_STRING:
            qe_free(&es->args[i].p);
            break;
        }
    }
    qe_free(&es);
}

/* when the argument has been typed by the user, this callback is
   called */
static void arg_edit_cb(void *opaque, char *str)
{
    ExecCmdState *es = opaque;
    int index, val;
    char *p;

    if (!str) {
        /* command aborted */
    fail:
        qe_free(&str);
        free_cmd(es);
        return;
    }
    index = es->nb_args - 1;
    switch (es->args_type[index]) {
    case CMD_ARG_INT:
        val = strtol(str, &p, 0);
        if (*p != '\0') {
            put_status(NULL, "Invalid number");
            goto fail;
        }
        es->args[index].n = val;
        break;
    case CMD_ARG_STRING:
        if (str[0] == '\0' && es->default_input[0] != '\0') {
            qe_free(&str);
            str = qe_strdup(es->default_input);
        }
        es->args[index].p = str; /* will be freed at the of the command */
        break;
    }
    /* now we can parse the following arguments */
    parse_args(es);
}

int check_read_only(EditState *s)
{
    if (s->b->flags & BF_READONLY) {
        put_status(s, "Buffer is read-only");
        return 1;
    } else {
        return 0;
    }
}

void do_execute_command(EditState *s, const char *cmd, int argval)
{
    CmdDef *d;

    d = qe_find_cmd(cmd);
    if (d) {
        exec_command(s, d, argval, 0);
    } else {
        put_status(s, "No match");
    }
}

void window_display(EditState *s)
{
    CSSRect rect;

    /* set the clipping rectangle to the whole window */
    rect.x1 = s->xleft;
    rect.y1 = s->ytop;
    rect.x2 = rect.x1 + s->width;
    rect.y2 = rect.y1 + s->height;
    set_clip_rectangle(s->screen, &rect);

    s->mode->display(s);

    display_mode_line(s);
    display_window_borders(s);
}

/* display all windows */
/* XXX: should use correct clipping to avoid popups display hacks */
void edit_display(QEmacsState *qs)
{
    EditState *s;
    int has_popups;

    /* first call hooks for mode specific fixups */
    for (s = qs->first_window; s != NULL; s = s->next_window) {
        if (s->mode->display_hook)
            s->mode->display_hook(s);
    }

    /* count popups */
    /* CG: maybe a separate list for popups? */
    has_popups = 0;
    for (s = qs->first_window; s != NULL; s = s->next_window) {
        if (s->flags & WF_POPUP) {
            has_popups = 1;
        }
    }

    /* refresh normal windows and minibuf with popup kludge */
    for (s = qs->first_window; s != NULL; s = s->next_window) {
        if (!(s->flags & WF_POPUP) &&
            (s->minibuf || !has_popups || qs->complete_refresh)) {
            window_display(s);
        }
    }
    /* refresh popups if any */
    if (has_popups) {
        for (s = qs->first_window; s != NULL; s = s->next_window) {
            if (s->flags & WF_POPUP) {
                //if (qs->complete_refresh)
                //    /* refresh frame */;
                window_display(s);
            }
        }
    }

    qs->complete_refresh = 0;
}

/* macros */

void do_start_macro(EditState *s)
{
    QEmacsState *qs = s->qe_state;

    if (qs->defining_macro) {
        qs->defining_macro = 0;
        put_status(s, "Already defining kbd macro");
        return;
    }
    qs->defining_macro = 1;
    qe_free(&qs->macro_keys);
    qs->nb_macro_keys = 0;
    qs->macro_keys_size = 0;
    put_status(s, "Defining kbd macro...");
}

void do_end_macro(EditState *s)
{
    QEmacsState *qs = s->qe_state;

    /* if called inside a macro, it is last recorded keys, so ignore
       it */
    if (qs->macro_key_index != -1)
        return;

    if (!qs->defining_macro) {
        put_status(s, "Not defining kbd macro");
        return;
    }
    qs->defining_macro = 0;
    put_status(s, "Keyboard macro defined");
}

void do_call_macro(EditState *s)
{
    QEmacsState *qs = s->qe_state;
    int key;

    if (qs->defining_macro) {
        qs->defining_macro = 0;
        put_status(s, "Can't execute macro while defining one");
        return;
    }

    if (qs->nb_macro_keys > 0) {
        /* CG: should share code with do_execute_macro */
        for (qs->macro_key_index = 0;
             qs->macro_key_index < qs->nb_macro_keys;
             qs->macro_key_index++) {
            key = qs->macro_keys[qs->macro_key_index];
            qe_key_process(key);
        }
        qs->macro_key_index = -1;
    }
}

void do_execute_macro_keys(__unused__ EditState *s, const char *keys)
{
    QEmacsState *qs = s->qe_state;
    const char *p;
    int key;

    qs->executing_macro++;

    /* Interactive commands get their input from the macro, unless some
     * suspend mechanism is added to create interactive macros.
     */

    p = keys;
    for (;;) {
        skip_spaces(&p);
        if (*p == '\0')
            break;
        key = strtokey(&p);
        qe_key_process(key);
    }
    qs->executing_macro--;
}

void do_define_kbd_macro(EditState *s, const char *name, const char *keys,
                         const char *key_bind)
{
    CmdDef *def;
    int size;
    char *buf;

    size = strlen(name) + 1 + 2 + strlen(keys) + 2;
    buf = qe_malloc_array(char, size);

    /* CG: should parse macro keys to an array and pass index 
     * to do_execute_macro.
     */
    snprintf(buf, size, "%s%cS{%s}", name, 0, keys);

    def = qe_mallocz_array(CmdDef, 2);
    def->key = def->alt_key = KEY_NONE;
    def->name = buf;
    def->sig = CMD_ESs;
    def->val = 0;
    def->action.ESs = do_execute_macro_keys;

    qe_register_cmd_table(def, NULL);
    do_set_key(s, key_bind, name, 0);
}

#define MACRO_KEY_INCR 64

static void macro_add_key(int key)
{
    QEmacsState *qs = &qe_state;
    int new_size;

    if (qs->nb_macro_keys >= qs->macro_keys_size) {
        new_size = qs->macro_keys_size + MACRO_KEY_INCR;
        if (!qe_realloc(&qs->macro_keys, new_size * sizeof(unsigned short)))
            return;
        qs->macro_keys_size = new_size;
    }
    qs->macro_keys[qs->nb_macro_keys++] = key;
}

const char *keys_to_str(char *buf, int buf_size,
                        unsigned int *keys, int nb_keys)
{
    char buf1[64];
    int i;

    buf[0] = '\0';
    for (i = 0; i < nb_keys; i++) {
        keytostr(buf1, sizeof(buf1), keys[i]);
        if (i != 0)
            pstrcat(buf, buf_size, " ");
        pstrcat(buf, buf_size, buf1);
    }
    return buf;
}

void do_universal_argument(__unused__ EditState *s)
{
    /* nothing is done there (see qe_key_process()) */
}

typedef struct QEKeyContext {
    int argval;
    int noargval;
    int sign;
    int is_universal_arg;
    int is_escape;
    int nb_keys;
    int describe_key; /* if true, the following command is only displayed */
    void (*grab_key_cb)(void *opaque, int key);
    void *grab_key_opaque;
    unsigned int keys[MAX_KEYS];
    char buf[128];
} QEKeyContext;

static QEKeyContext key_ctx;

/*
 * All typed keys are sent to the callback. Previous grab is aborted
 */
void qe_grab_keys(void (*cb)(void *opaque, int key), void *opaque)
{
    QEKeyContext *c = &key_ctx;

    /* CG: Should free previous grab? */
    /* CG: Should grabing be window dependent ? */
    c->grab_key_cb = cb;
    c->grab_key_opaque = opaque;
}

/*
 * Abort key grabing
 */
void qe_ungrab_keys(void)
{
    QEKeyContext *c = &key_ctx;

    /* CG: Should free previous grab? */
    c->grab_key_cb = NULL;
    c->grab_key_opaque = NULL;
}

/* init qe key handling context */
static void qe_key_init(QEKeyContext *c)
{
    c->is_universal_arg = 0;
    c->is_escape = 0;
    c->noargval = 1;
    c->argval = NO_ARG;
    c->sign = 1;
    c->nb_keys = 0;
    c->buf[0] = '\0';
}

static KeyDef *find_binding(unsigned int *keys, int nb_keys, int nroots, ...)
{
    KeyDef *kd = NULL;
    va_list ap;

    va_start(ap, nroots);
    while (nroots--) {
        for (kd = va_arg(ap, KeyDef *); kd != NULL; kd = kd->next) {
            if (kd->nb_keys >= nb_keys
            &&  !memcmp(kd->keys, keys, nb_keys * sizeof(keys[0])))
            {
                goto found;
            }
        }
    }
  found:
    va_end(ap);
    return kd;
}

static KeyDef *find_binding1(unsigned int key, int nroots, ...)
{
    KeyDef *kd = NULL;
    va_list ap;

    va_start(ap, nroots);
    while (nroots--) {
        for (kd = va_arg(ap, KeyDef *); kd != NULL; kd = kd->next) {
            if (kd->nb_keys == 1 && kd->keys[0] == key)
                goto found;
        }
    }
  found:
    va_end(ap);
    return kd;
}

static void qe_key_process(int key)
{
    QEmacsState *qs = &qe_state;
    QEKeyContext *c = &key_ctx;
    EditState *s;
    KeyDef *kd;
    CmdDef *d;
    char buf1[128];
    int len;

    if (qs->defining_macro && !qs->executing_macro) {
        macro_add_key(key);
    }

  again:
    if (c->grab_key_cb) {
        /* grabber should return codes for quit / fall thru / ungrab */
        c->grab_key_cb(c->grab_key_opaque, key);
        /* allow key_grabber to quit and unget last key */
        if (c->grab_key_cb || qs->ungot_key == -1)
            return;
        key = qs->ungot_key;
        qs->ungot_key = -1;
    }

    /* safety check */
    if (c->nb_keys >= MAX_KEYS) {
        qe_key_init(c);
        c->describe_key = 0;
        return;
    }

    c->keys[c->nb_keys++] = key;
    s = qs->active_window;
    if (!s->minibuf) {
        put_status(s, " ");
        dpy_flush(&global_screen);
    }

    /* Special case for escape: we transform it as meta so
       that unix users are happy ! */
    if (key == KEY_ESC) {
        c->is_escape = 1;
        goto next;
    } else
    if (c->is_escape) {
        compose_keys(c->keys, &c->nb_keys);
        c->is_escape = 0;
        key = c->keys[c->nb_keys - 1];
    }

    /* see if one command is found */
    if (!(kd = find_binding(c->keys, c->nb_keys, 2,
                            s->mode->first_key, qs->first_key)))
    {
        /* no key found */
        if (c->nb_keys == 1) {
            if (!KEY_SPECIAL(key)) {
                if (c->is_universal_arg) {
                    if (qe_isdigit(key)) {
                        if (c->argval == NO_ARG)
                            c->argval = 0;
                        c->argval = c->argval * 10 + (key - '0');
                        c->nb_keys = 0;
                        goto next;
                    } else
                    if (key == '-') {
                        c->sign = -c->sign;
                        c->nb_keys = 0;
                        goto next;
                    }
                }
                kd = find_binding1(KEY_DEFAULT, 2,
                                   s->mode->first_key, qs->first_key);
                if (kd) {
                    /* horrible kludge to pass key as intrinsic argument */
                    /* CG: should have an argument type for key */
                    /* CG: should be no longer necessary */
                    kd->cmd->val = key;
                    goto exec_cmd;
                }
            }
        }
        if (!c->describe_key) {
            /* CG: should beep */;
        }

        put_status(s, "No command on %s",
                   keys_to_str(buf1, sizeof(buf1), c->keys, c->nb_keys));
        c->describe_key = 0;
        qe_key_init(c);
        dpy_flush(&global_screen);
        return;
    } else
    if (c->nb_keys == kd->nb_keys) {
    exec_cmd:
        d = kd->cmd;
        if (d->action.ES == do_universal_argument && !c->describe_key) {
            /* special handling for universal argument */
            c->is_universal_arg = 1;
            if (key == KEY_META('-')) {
                c->sign = -c->sign;
                if (c->noargval == 1)
                    c->noargval = 4;
            } else {
                c->noargval = c->noargval * 4;
            }
            c->nb_keys = 0;
        } else {
            if (c->is_universal_arg) {
                if (c->argval == NO_ARG)
                    c->argval = c->noargval;
                c->argval *= c->sign;
            }
            if (c->describe_key) {
                put_status(s, "%s runs the command %s",
                           keys_to_str(buf1, sizeof(buf1), c->keys, c->nb_keys),
                           d->name);
                c->describe_key = 0;
            } else {
                int argval = c->argval;

                /* To allow recursive calls to qe_key_process, especially
                 * from macros, we reset the QEKeyContext before
                 * dispatching the command
                 */
                qe_key_init(c);
                exec_command(s, d, argval, key);
            }
            qe_key_init(c);
            edit_display(qs);
            dpy_flush(&global_screen);
            /* CG: should move ungot key handling to generic event dispatch */
            if (qs->ungot_key != -1) {
                key = qs->ungot_key;
                qs->ungot_key = -1;
                goto again;
            }
            return;
        }
    }
 next:
    /* display key pressed */
    if (!s->minibuf) {
        /* Should print argument if any in a more readable way */
        keytostr(buf1, sizeof(buf1), key);
        len = strlen(c->buf);
        if (len >= 1)
            c->buf[len-1] = ' ';
        pstrcat(c->buf, sizeof(c->buf), buf1);
        pstrcat(c->buf, sizeof(c->buf), "-");
        put_status(s, "~%s", c->buf);
        dpy_flush(&global_screen);
    }
}

/* Print a utf-8 encoded buffer as unicode */
void print_at_byte(QEditScreen *screen,
                   int x, int y, int width, int height,
                   const char *str, int style_index)
{
    unsigned int ubuf[MAX_SCREEN_WIDTH];
    int len;
    QEStyleDef style;
    QEFont *font;
    CSSRect rect;

    len = utf8_to_unicode(ubuf, countof(ubuf), str);
    get_style(NULL, &style, style_index);

    /* clip rectangle */
    rect.x1 = x;
    rect.y1 = y;
    rect.x2 = rect.x1 + width;
    rect.y2 = rect.y1 + height;
    set_clip_rectangle(screen, &rect);

    /* start rectangle */
    fill_rectangle(screen, x, y, width, height, style.bg_color);
    font = select_font(screen, style.font_style, style.font_size);
    draw_text(screen, font, x, y + font->ascent, ubuf, len, style.fg_color);
    release_font(screen, font);
}

static void eb_format_message(QEmacsState *qs, const char *bufname,
                              const char *message)
{
    char header[128];
    int len;
    EditBuffer *eb;

    header[len = 0] = '\0';
    if (qs->ec.filename) {
        snprintf(header, sizeof(header), "%s:%d: ",
                 qs->ec.filename, qs->ec.lineno);
        len = strlen(header);
    }
    if (qs->ec.function) {
        snprintf(header + len, sizeof(header) - len, "%s: ",
                 qs->ec.function);
        len = strlen(header);
    }
    eb = eb_find_new(bufname, BF_UTF8);
    if (eb) {
        eb_printf(eb, "%s%s\n", header, message);
    } else {
        fprintf(stderr, "%s%s\n", header, message);
    }
}

void put_error(__unused__ EditState *s, const char *fmt, ...)
{
    /* CG: s is not used and may be NULL! */
    QEmacsState *qs = &qe_state;
    char buf[MAX_SCREEN_WIDTH];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    eb_format_message(qs, "*errors*", buf);
}

void put_status(__unused__ EditState *s, const char *fmt, ...)
{
    /* CG: s is not used and may be NULL! */
    QEmacsState *qs = &qe_state;
    char buf[MAX_SCREEN_WIDTH];
    const char *p;
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    p = buf;
    if (*p == '~')
        p++;

    if (!qs->screen->dpy.dpy_probe) {
        eb_format_message(qs, "*errors*", p);
    } else {
        if (!strequal(p, qs->status_shadow)) {
            print_at_byte(qs->screen,
                          0, qs->screen->height - qs->status_height,
                          qs->screen->width, qs->status_height,
                          p, QE_STYLE_STATUS);
            pstrcpy(qs->status_shadow, sizeof(qs->status_shadow), p);
            skip_spaces(&p);
            if (*p && *buf != '~')
                eb_format_message(qs, "*messages*", buf);
        }
    }
}

#if 0
EditState *find_file_window(const char *filename)
{
    QEmacsState *qs = &qe_state;
    EditState *s;

    for (s = qs->first_window; s; s = s->next_window) {
        if (strequal(s->b->filename, filename))
            return s;
    }
    return NULL;
}
#endif

void switch_to_buffer(EditState *s, EditBuffer *b)
{
    QEmacsState *qs = s->qe_state;
    EditBuffer *b1;
    EditState *e;
    ModeSavedData *saved_data, **psaved_data;
    ModeDef *mode;

    b1 = s->b;
    if (b1) {
        /* save old buffer data if no other window uses the buffer */
        for (e = qs->first_window; e != NULL; e = e->next_window) {
            if (e != s && e->b == b1)
                break;
        }
        if (!e) {
            /* if no more window uses the buffer, then save the data
               in the buffer */
            /* CG: Should free previous such data ? */
            b1->saved_data = s->mode->mode_save_data(s);
        }
        /* now we can close the mode */
        edit_set_mode(s, NULL, NULL);
    }

    /* now we can switch ! */
    s->b = b;

    if (b) {
        /* try to restore saved data from another window or from the
           buffer saved data */
        for (e = qs->first_window; e != NULL; e = e->next_window) {
            if (e != s && e->b == b)
                break;
        }
        if (!e) {
            psaved_data = &b->saved_data;
            saved_data = *psaved_data;
        } else {
            psaved_data = NULL;
            saved_data = e->mode->mode_save_data(e);
        }

        /* find the mode */
        if (saved_data)
            mode = saved_data->mode;
        else
            mode = &text_mode; /* default mode */

        /* open it ! */
        edit_set_mode(s, mode, saved_data);
    }
}

/* compute the client area from the window position */
static void compute_client_area(EditState *s)
{
    QEmacsState *qs = s->qe_state;
    int x1, y1, x2, y2;

    x1 = s->x1;
    y1 = s->y1;
    x2 = s->x2;
    y2 = s->y2;
    if (s->flags & WF_MODELINE)
        y2 -= qs->mode_line_height;
    if (s->flags & WF_POPUP) {
        x1 += qs->border_width;
        x2 -= qs->border_width;
        y1 += qs->border_width;
        y2 -= qs->border_width;
    }
    if (s->flags & WF_RSEPARATOR)
        x2 -= qs->separator_width;

    s->xleft = x1;
    s->ytop = y1;
    s->width = x2 - x1;
    s->height = y2 - y1;
}

/* Create a new edit window, add it in the window list and sets it
 * active if none are active. The coordinates include the window
 * borders.
 */
EditState *edit_new(EditBuffer *b,
                    int x1, int y1, int width, int height, int flags)
{
    /* b may be NULL ??? */
    QEmacsState *qs = &qe_state;
    EditState *s;

    s = qe_mallocz(EditState);
    if (!s)
        return NULL;
    s->qe_state = qs;
    s->screen = qs->screen;
    s->x1 = x1;
    s->y1 = y1;
    s->x2 = x1 + width;
    s->y2 = y1 + height;
    s->flags = flags;
    compute_client_area(s);
    /* link window in window list */
    s->next_window = qs->first_window;
    qs->first_window = s;
    if (!qs->active_window)
        qs->active_window = s;
    /* restore saved window settings, set mode */
    switch_to_buffer(s, b);
    return s;
}

/* find a window with a given buffer if any. */
EditState *edit_find(EditBuffer *b)
{
    QEmacsState *qs = &qe_state;
    EditState *e;

    for (e = qs->first_window; e != NULL; e = e->next_window) {
        if (e->b == b)
            break;
    }
    return e;
}

/* detach the window from the window tree. */
void edit_detach(EditState *s)
{
    QEmacsState *qs = s->qe_state;
    EditState **ep;

    for (ep = &qs->first_window; *ep; ep = &(*ep)->next_window) {
        if (*ep == s) {
            *ep = s->next_window;
            s->next_window = NULL;
            break;
        }
    }
    if (qs->active_window == s)
        qs->active_window = qs->first_window;
}

/* attach the window to the window list. */
void edit_attach(EditState *s, EditState **ep)
{
    s->next_window = *ep;
    *ep = s;
}

/* close the edit window. If it is active, find another active
   window. If the buffer is only referenced by this window, then save
   in the buffer all the window state so that it can be recovered. */
void edit_close(EditState *s)
{
    QEmacsState *qs = s->qe_state;
    EditState **ps;

    /* save current state for later window reattachment */
    switch_to_buffer(s, NULL);

    /* free from window list */
    ps = &qs->first_window;
    while (*ps != NULL) {
        if (*ps == s)
            break;
        ps = &(*ps)->next_window;
    }
    *ps = (*ps)->next_window;

    /* if active window, select another active window */
    if (qs->active_window == s)
        qs->active_window = qs->first_window;

    qe_free(&s->line_shadow);
    qe_free(&s);
}

static const char *file_completion_ignore_extensions =
    "|bak|bin|dll|exe|o|so|obj|a|gz|tgz";

void file_completion(CompleteState *cp)
{
    char path[MAX_FILENAME_SIZE];
    char file[MAX_FILENAME_SIZE];
    char filename[MAX_FILENAME_SIZE];
    char *current;
    FindFileState *ffst;
    const char *base;
    int len;

    current = cp->current;
    if (*current == '~') {
        canonicalize_absolute_path(filename, sizeof(filename), cp->current);
        current = filename;
    }

    splitpath(path, sizeof(path), file, sizeof(file), current);
    pstrcat(file, sizeof(file), "*");

    ffst = find_file_open(*path ? path : ".", file);
    while (find_file_next(ffst, filename, sizeof(filename)) == 0) {
        struct stat sb;

        base = get_basename(filename);
        /* ignore . and .. to force direct match if
         * single entry in directory */
        if (strequal(base, ".") || strequal(base, ".."))
            continue;
        /* ignore known backup files */
        len = strlen(base);
        if (!len || base[len - 1] == '~')
            continue;
        /* ignore known output file extensions */
        if (match_extension(base, file_completion_ignore_extensions))
            continue;

        /* stat the file to find out if it's a directory.
         * In that case add a slash to speed up typing long paths
         */
        if (!stat(filename, &sb) && S_ISDIR(sb.st_mode))
            pstrcat(filename, sizeof(filename), "/");
        add_string(&cp->cs, filename);
    }

    find_file_close(ffst);
}

void buffer_completion(CompleteState *cp)
{
    QEmacsState *qs = cp->s->qe_state;
    EditBuffer *b;

    for (b = qs->first_buffer; b != NULL; b = b->next) {
        if (!(b->flags & BF_SYSTEM))
            complete_test(cp, b->name);
    }
}

/* register a new completion method */
void register_completion(const char *name, CompletionFunc completion_func)
{
    QEmacsState *qs = &qe_state;
    CompletionEntry **lp, *p;

    p = qe_malloc(CompletionEntry);
    if (!p)
        return;

    p->name = name;
    p->completion_func = completion_func;
    p->next = NULL;

    lp = &qs->first_completion;
    while (*lp != NULL)
        lp = &(*lp)->next;
    *lp = p;
}

static CompletionFunc find_completion(const char *name)
{
    CompletionEntry *p;

    if (name[0] != '\0') {
        for (p = qe_state.first_completion; p != NULL; p = p->next) {
            if (strequal(p->name, name))
                return p->completion_func;
        }
    }
    return NULL;
}

static void complete_start(EditState *s, CompleteState *cp)
{
    memset(cp, 0, sizeof(*cp));
    cp->s = s;
    cp->len = eb_get_contents(s->b, cp->current, sizeof(cp->current) - 1);
}

void complete_test(CompleteState *cp, const char *str)
{
    if (!memcmp(str, cp->current, cp->len))
        add_string(&cp->cs, str);
}

static int completion_sort_func(const void *p1, const void *p2)
{
    StringItem *item1 = *(StringItem **)p1;
    StringItem *item2 = *(StringItem **)p2;

    /* Use natural sort: keep numbers in order */
    return qe_strcollate(item1->str, item2->str);
}

static void complete_end(CompleteState *cp)
{
    free_strings(&cp->cs);
}

/* mini buffer stuff */

static ModeDef minibuffer_mode;

static void (*minibuffer_cb)(void *opaque, char *buf);
static void *minibuffer_opaque;
static EditState *minibuffer_saved_active;

static EditState *completion_popup_window;
static CompletionFunc completion_function;

static StringArray *minibuffer_history;
static int minibuffer_history_index;
static int minibuffer_history_saved_offset;

/* XXX: utf8 ? */
void do_completion(EditState *s)
{
    QEmacsState *qs = s->qe_state;
    int count, i, match_len, c;
    CompleteState cs;
    StringItem **outputs;
    EditState *e;
    int w, h, h1, w1;

    if (!completion_function)
        return;

    complete_start(s, &cs);
    (*completion_function)(&cs);
    count = cs.cs.nb_items;
    outputs = cs.cs.items;
#if 0
    printf("count=%d\n", count);
    for (i = 0; i < count; i++)
        printf("out[%d]=%s\n", i, outputs[i]->str);
#endif
    /* no completion ? */
    if (count == 0)
        goto the_end;
    /* compute the longest match len */
    match_len = cs.len;
    for (;;) {
        /* Potential UTF-8 issue: should use utility function */
        c = outputs[0]->str[match_len];
        if (c == '\0')
            break;
        for (i = 1; i < count; i++)
            if (outputs[i]->str[match_len] != c)
                goto no_match;
        match_len++;
    }
 no_match:
    if (match_len > cs.len) {
        /* add the possible chars */
        eb_write(s->b, 0, outputs[0]->str, match_len);
        s->offset = match_len;
    } else {
        if (count > 1) {
            /* if more than one match, then display them in a new popup
               buffer */
            if (!completion_popup_window) {
                EditBuffer *b;
                b = eb_new("*completion*", BF_SYSTEM);
                w1 = qs->screen->width;
                h1 = qs->screen->height - qs->status_height;
                w = (w1 * 3) / 4;
                h = (h1 * 3) / 4;
                e = edit_new(b, (w1 - w) / 2, (h1 - h) / 2, w, h, WF_POPUP);
                /* set list mode */
                edit_set_mode(e, &list_mode, NULL);
                do_refresh(e);
                completion_popup_window = e;
            }
        }
        if (completion_popup_window) {
            EditBuffer *b = completion_popup_window->b;
            /* modify the list with the current matches */
            qsort(outputs, count, sizeof(StringItem *), completion_sort_func);
            b->flags &= ~BF_READONLY;
            eb_delete(b, 0, b->total_size);
            for (i = 0; i < count; i++) {
                eb_printf(b, " %s", outputs[i]->str);
                if (i != count - 1)
                    eb_printf(b, "\n");
            }
            b->flags |= BF_READONLY;
            completion_popup_window->mouse_force_highlight = 1;
            completion_popup_window->force_highlight = 0;
            completion_popup_window->offset = 0;
        }
    }
 the_end:
    complete_end(&cs);
}

void do_electric_filename(EditState *s, int key)
{
    int c, offset;

    if (completion_function == file_completion) {
        c = eb_prevc(s->b, s->offset, &offset);
        if (c == '/')
            eb_delete(s->b, 0, s->offset);
    }
    do_char(s, key, 1);
}

/* space does completion only if a completion method is defined */
void do_completion_space(EditState *s)
{
    if (!completion_function) {
        do_char(s, ' ', 1);
    } else {
        do_completion(s);
    }
}

/* scroll in completion popup */
void minibuf_complete_scroll_up_down(__unused__ EditState *s, int dir)
{
    if (completion_popup_window) {
        completion_popup_window->force_highlight = 1;
        do_scroll_up_down(completion_popup_window, dir);
    }
}

static void set_minibuffer_str(EditState *s, const char *str)
{
    int len;

    eb_delete(s->b, 0, s->b->total_size);
    len = strlen(str);
    eb_write(s->b, 0, str, len);
    s->offset = len;
}

static StringArray *get_history(const char *name)
{
    QEmacsState *qs = &qe_state;
    HistoryEntry *p;

    if (name[0] == '\0')
        return NULL;
    for (p = qs->first_history; p != NULL; p = p->next) {
        if (strequal(p->name, name))
            return &p->history;
    }
    /* not found: allocate history list */
    p = qe_mallocz(HistoryEntry);
    if (!p)
        return NULL;
    pstrcpy(p->name, sizeof(p->name), name);
    p->next = qs->first_history;
    qs->first_history = p;
    return &p->history;
}

void do_history(EditState *s, int dir)
{
    QEmacsState *qs = s->qe_state;
    StringArray *hist = minibuffer_history;
    int index;
    char *str;
    char buf[1024];

    /* if completion visible, move in it */
    if (completion_popup_window) {
        completion_popup_window->force_highlight = 1;
        do_up_down(completion_popup_window, dir);
        return;
    }

    if (!hist)
        return;
    index = minibuffer_history_index + dir;
    if (index < 0 || index >= hist->nb_items)
        return;
    if (qs->last_cmd_func != (CmdFunc)do_history) {
        /* save currently edited line */
        eb_get_contents(s->b, buf, sizeof(buf));
        set_string(hist, hist->nb_items - 1, buf);
        minibuffer_history_saved_offset = s->offset;
    }
    /* insert history text */
    minibuffer_history_index = index;
    str = hist->items[index]->str;
    set_minibuffer_str(s, str);
    if (index == hist->nb_items - 1) {
        s->offset = minibuffer_history_saved_offset;
    }
}

void do_minibuffer_get_binary(EditState *s)
{
    unsigned long offset;

    if (minibuffer_saved_active) {
        eb_read(minibuffer_saved_active->b,
                minibuffer_saved_active->offset,
                &offset, sizeof(offset));
        eb_printf(s->b, "%lu", offset);
    }
}

void do_minibuffer_exit(EditState *s, int do_abort)
{
    QEmacsState *qs = s->qe_state;
    EditBuffer *b = s->b;
    StringArray *hist = minibuffer_history;
    static void (*cb)(void *opaque, char *buf);
    static void *opaque;
    char buf[4096], *retstr;
    int len;

    /* if completion is activated, then select current file only if
       the selection is highlighted */
    if (completion_popup_window &&
        completion_popup_window->force_highlight) {
        int offset;

        offset = list_get_offset(completion_popup_window);
        eb_get_strline(completion_popup_window->b, buf, sizeof(buf), &offset);
        if (buf[0] != '\0')
            set_minibuffer_str(s, buf + 1);
    }

    /* remove completion popup if present */
    /* CG: assuming completion_popup_window != s */
    if (completion_popup_window) {
        EditBuffer *b1 = completion_popup_window->b;
        edit_close(completion_popup_window);
        eb_free(b1);
        do_refresh(s);
    }

    len = eb_get_contents(s->b, buf, sizeof(buf));
    if (hist && hist->nb_items > 0) {
        /* if null string, do not insert in history */
        hist->nb_items--;
        qe_free(&hist->items[hist->nb_items]);
        if (buf[0] != '\0')
            add_string(hist, buf);
    }

    /* free prompt */
    qe_free(&s->prompt);

    edit_close(s);
    eb_free(b);
    /* restore active window */
    qs->active_window = minibuffer_saved_active;

    /* force status update */
    //pstrcpy(qs->status_shadow, sizeof(qs->status_shadow), " ");
    if (do_abort)
        put_status(NULL, "Canceled.");
    else
        put_status(NULL, "");

    /* call the callback */
    cb = minibuffer_cb;
    opaque = minibuffer_opaque;
    minibuffer_cb = NULL;
    minibuffer_opaque = NULL;

    if (do_abort) {
        cb(opaque, NULL);
    } else {
        retstr = qe_strdup(buf);
        cb(opaque, retstr);
    }
}

/* Start minibuffer editing. When editing is finished, the callback is
   called with an allocated string. If the string is null, it means
   editing was aborted. */
void minibuffer_edit(const char *input, const char *prompt,
                     StringArray *hist, CompletionFunc completion_func,
                     void (*cb)(void *opaque, char *buf), void *opaque)
{

    EditState *s;
    QEmacsState *qs = &qe_state;
    EditBuffer *b;
    int len;

    /* check if already in minibuffer editing */
    if (minibuffer_cb) {
        put_status(NULL, "Already editing in minibuffer");
        cb(opaque, NULL);
        return;
    }

    minibuffer_cb = cb;
    minibuffer_opaque = opaque;

    b = eb_new("*minibuf*", BF_SYSTEM | BF_SAVELOG | BF_UTF8);

    s = edit_new(b, 0, qs->screen->height - qs->status_height,
                 qs->screen->width, qs->status_height, 0);
    /* Should insert at end of window list */
    edit_set_mode(s, &minibuffer_mode, NULL);
    s->prompt = qe_strdup(prompt);
    s->minibuf = 1;
    s->bidir = 0;
    s->default_style = QE_STYLE_MINIBUF;
    s->wrap = WRAP_TRUNCATE;

    /* add default input */
    if (input) {
        len = strlen(input);
        eb_write(b, 0, (u8 *)input, len);
        s->offset = len;
    }

    minibuffer_saved_active = qs->active_window;
    qs->active_window = s;

    completion_popup_window = NULL;
    completion_function = completion_func;
    minibuffer_history = hist;
    minibuffer_history_saved_offset = 0;
    if (hist) {
        minibuffer_history_index = hist->nb_items;
        add_string(hist, "");
    }
}

void minibuffer_init(void)
{
    /* minibuf mode inherits from text mode */
    memcpy(&minibuffer_mode, &text_mode, sizeof(ModeDef));
    minibuffer_mode.name = "minibuffer";
    minibuffer_mode.scroll_up_down = minibuf_complete_scroll_up_down;
    qe_register_mode(&minibuffer_mode);
    qe_register_cmd_table(minibuffer_commands, &minibuffer_mode);
}

/* less mode */

static ModeDef less_mode;

/* XXX: incorrect to save it. Should use a safer method */
static EditState *popup_saved_active;

/* less like mode */
void do_less_exit(EditState *s)
{
    QEmacsState *qs = s->qe_state;
    EditBuffer *b;

    /* CG: should verify that popup_saved_active still exists */
    /* CG: This command crashes if not invoked from less popup mode */
    if (popup_saved_active) {
        qs->active_window = popup_saved_active;
        b = s->b;
        edit_close(s);
        eb_free(b);
        do_refresh(qs->active_window);
    }
}

/* show a popup on a readonly buffer */
void show_popup(EditBuffer *b)
{
    EditState *s;
    QEmacsState *qs = &qe_state;
    int w, h, w1, h1;

    /* XXX: generic function to open popup ? */
    w1 = qs->screen->width;
    h1 = qs->screen->height - qs->status_height;
    w = (w1 * 4) / 5;
    h = (h1 * 3) / 4;

    s = edit_new(b, (w1 - w) / 2, (h1 - h) / 2, w, h, WF_POPUP);
    edit_set_mode(s, &less_mode, NULL);
    s->wrap = WRAP_TRUNCATE;

    popup_saved_active = qs->active_window;
    qs->active_window = s;
    do_refresh(s);
}

void less_mode_init(void)
{
    /* less mode inherits from text mode */
    memcpy(&less_mode, &text_mode, sizeof(ModeDef));
    less_mode.name = "less";
    qe_register_mode(&less_mode);
    qe_register_cmd_table(less_commands, &less_mode);
}

#ifndef CONFIG_TINY
/* insert a window to the left. Close all windows which are totally
   under it (XXX: should try to move them first */
EditState *insert_window_left(EditBuffer *b, int width, int flags)
{
    QEmacsState *qs = &qe_state;
    EditState *e, *e_next, *e_new;

    for (e = qs->first_window; e != NULL; e = e_next) {
        e_next = e->next_window;
        if (e->minibuf)
            continue;
        if (e->x2 <= width) {
            edit_close(e);
        } else if (e->x1 < width) {
            e->x1 = width;
        }
    }

    e_new = edit_new(b, 0, 0, width, qs->height - qs->status_height,
                     flags | WF_RSEPARATOR);
    do_refresh(qs->first_window);
    return e_new;
}

/* return a window on the side of window 's' */
EditState *find_window(EditState *s, int key)
{
    QEmacsState *qs = s->qe_state;
    EditState *e;

    /* CG: Should compute cursor position to disambiguate
     * non regular window layouts
     */
    for (e = qs->first_window; e != NULL; e = e->next_window) {
        if (e->minibuf)
            continue;
        if (e->y1 < s->y2 && e->y2 > s->y1) {
            /* horizontal overlap */
            if (key == KEY_RIGHT && e->x1 == s->x2)
                return e;
            if (key == KEY_LEFT && e->x2 == s->x1)
                return e;
        }
        if (e->x1 < s->x2 && e->x2 > s->x1) {
            /* vertical overlap */
            if (key == KEY_UP && e->y2 == s->y1)
                return e;
            if (key == KEY_DOWN && e->y1 == s->y2)
                return e;
        }
    }
    return NULL;
}

void do_find_window(EditState *s, int key)
{
    QEmacsState *qs = s->qe_state;
    EditState *e;

    e = find_window(s, key);
    if (e)
        qs->active_window = e;
}
#endif

/* give a good guess to the user for the next buffer */
static EditBuffer *predict_switch_to_buffer(EditState *s)
{
    QEmacsState *qs = s->qe_state;
    EditState *e;
    EditBuffer *b;

    for (b = qs->first_buffer; b != NULL; b = b->next) {
        if (!(b->flags & BF_SYSTEM)) {
            for (e = qs->first_window; e != NULL; e = e->next_window) {
                if (e->b == b)
                    break;
            }
            if (!e)
                goto found;
        }
    }
    /* select current buffer if none found */
    b = s->b;
 found:
    return b;
}

void do_switch_to_buffer(EditState *s, const char *bufname)
{
    EditBuffer *b;

    b = eb_find_new(bufname, BF_SAVELOG);
    if (b)
        switch_to_buffer(s, b);
}

void do_toggle_read_only(EditState *s)
{
    s->b->flags ^= BF_READONLY;
}

void do_not_modified(EditState *s, int argval)
{
    s->b->modified = (argval != NO_ARG);
}

static void kill_buffer_confirm_cb(void *opaque, char *reply);
static void kill_buffer_noconfirm(EditBuffer *b);

void do_kill_buffer(EditState *s, const char *bufname)
{
    char buf[1024];
    EditBuffer *b;

    b = eb_find(bufname);
    if (!b) {
        put_status(s, "No buffer %s", bufname);
    } else {
        /* if modified and associated to a filename, then ask */
        if (b->modified && b->filename[0] != '\0') {
            snprintf(buf, sizeof(buf),
                     "Buffer %s modified; kill anyway? (yes or no) ", bufname);
            minibuffer_edit(NULL, buf, NULL, NULL,
                            kill_buffer_confirm_cb, b);
        } else {
            kill_buffer_noconfirm(b);
        }
    }
}

static void kill_buffer_confirm_cb(void *opaque, char *reply)
{
    int yes_replied;

    if (!reply)
        return;
    yes_replied = strequal(reply, "yes");
    qe_free(&reply);
    if (!yes_replied)
        return;
    kill_buffer_noconfirm(opaque);
}

static void kill_buffer_noconfirm(EditBuffer *b)
{
    QEmacsState *qs = &qe_state;
    EditState *e;
    EditBuffer *b1;

    // FIXME: used to delete windows containing the buffer ???

    /* find a new buffer to switch to */
    for (b1 = qs->first_buffer; b1 != NULL; b1 = b1->next) {
        if (b1 != b && !(b1->flags & BF_SYSTEM))
            break;
    }
    if (!b1)
        b1 = eb_new("*scratch*", BF_SAVELOG);

    /* if the buffer remains because we cannot delete the main
       window, then switch to the scratch buffer */
    for (e = qs->first_window; e != NULL; e = e->next_window) {
        if (e->b == b) {
            switch_to_buffer(e, b1);
        }
    }

    /* now we can safely delete buffer */
    eb_free(b);

    do_refresh(qs->first_window);
}

/* compute default path for find/save buffer */
static void get_default_path(EditState *s, char *buf, int buf_size)
{
    EditBuffer *b = s->b;
    char buf1[MAX_FILENAME_SIZE];
    const char *filename;

    /* CG: should have more specific code for dired/shell buffer... */
    if (b->flags & BF_DIRED) {
        makepath(buf, buf_size, b->filename, "");
        return;
    }

    if ((b->flags & BF_SYSTEM)
    ||  b->name[0] == '*'
    ||  b->filename[0] == '\0') {
        filename = "a";
    } else {
        filename = s->b->filename;
    }
    canonicalize_absolute_path(buf1, sizeof(buf1), filename);
    splitpath(buf, buf_size, NULL, 0, buf1);
}

static ModeDef *probe_mode(EditState *s, int mode, const uint8_t *buf,
                           int len, long total_size)
{
    QEmacsState *qs = s->qe_state;
    char fname[MAX_FILENAME_SIZE];
    EditBuffer *b;
    ModeDef *m, *selected_mode;
    ModeProbeData probe_data;
    int best_probe_percent, percent;
    const uint8_t *p;

    b = s->b;

    selected_mode = NULL;
    best_probe_percent = 0;
    probe_data.buf = buf;
    probe_data.buf_size = len;
    p = memchr(buf, '\n', len);
    probe_data.line_len = p ? p - buf : len;
    probe_data.real_filename = b->filename;
    probe_data.mode = mode;
    probe_data.total_size = total_size;
    probe_data.filename = reduce_filename(fname, sizeof(fname),
                                          get_basename(b->filename));
    /* CG: should pass EditState? QEmacsState ? */

    m = qs->first_mode;
    while (m != NULL) {
        if (m->mode_probe) {
            percent = m->mode_probe(&probe_data);
            if (percent > best_probe_percent) {
                selected_mode = m;
                best_probe_percent = percent;
            }
        }
        m = m->next;
    }
    return selected_mode;
}

/* Should take bits from enumeration instead of booleans */
static void do_load1(EditState *s, const char *filename1,
                     int kill_buffer, int load_resource)
{
    u8 buf[1025];
    char filename[MAX_FILENAME_SIZE];
    int mode, buf_size;
    ModeDef *selected_mode;
    EditBuffer *b;
    EditBufferDataType *bdt;
    FILE *f;
    struct stat st;

    if (load_resource) {
        if (find_resource_file(filename, sizeof(filename), filename1))
            return;
    } else {
        /* compute full name */
        canonicalize_absolute_path(filename, sizeof(filename), filename1);
    }

    ///* If file already shown in window, switch to that */
    //s1 = find_file_window(filename);
    //if (s1 != NULL) {
    //    qs->active_window = s1;
    //    return;
    //}

    ///* Split window if window large enough and not empty */
    //if (other_window && s->height > 10 && s->b->total_size > 0) {
    //    do_split_window(s, 0, 0);
    //    s = qs->active_window;
    //}

    if (kill_buffer) {
        /* CG: this behaviour is not correct */
        /* CG: should have a direct primitive */
        do_kill_buffer(s, s->b->name);
    }

    /* If file already loaded in existing buffer, switch to that */
    b = eb_find_file(filename);
    if (b != NULL) {
        switch_to_buffer(s, b);
        return;
    }

    /* Create new buffer with unique name from filename */
    b = eb_new("", BF_SAVELOG);
    eb_set_filename(b, filename);

    /* Switch to the newly created buffer */
    switch_to_buffer(s, b);

    s->offset = 0;
    /* CG: need a default setting for this */
    s->wrap = WRAP_LINE;

    /* First we try to read the first block to determine the data type */
    if (stat(filename, &st) < 0) {
        /* CG: should check for wildcards and do dired */
        //if (strchr(filename, '*') || strchr(filename, '?'))
        //    goto dired;
        put_status(s, "(New file)");
        /* Try to determine the desired mode based on the filename.
         * This avoids having to set c-mode for each new .c or .h file. */
        buf[0] = '\0';
        selected_mode = probe_mode(s, S_IFREG, buf, 0, 0);
        /* XXX: avoid loading file */
        if (selected_mode)
            edit_set_mode(s, selected_mode, NULL);
        return;
    } else {
        mode = st.st_mode;
        buf_size = 0;
        f = NULL;
        /* CG: should check for ISDIR and do dired */
        if (S_ISREG(mode)) {
            f = fopen(filename, "r");
            if (!f)
                goto fail;
            buf_size = fread(buf, 1, sizeof(buf) - 1, f);
            if (buf_size <= 0 && ferror(f)) {
              fail1:
                fclose(f);
                f = NULL;
                goto fail;
            }
        }
    }
    buf[buf_size] = '\0';
    selected_mode = probe_mode(s, mode, buf, buf_size, st.st_size);
    if (!selected_mode)
        goto fail1;
    bdt = selected_mode->data_type;

    /* autodetect buffer charset (could move it to raw buffer loader) */
    if (bdt == &raw_data_type)
        eb_set_charset(b, detect_charset(buf, buf_size));

    /* now we can set the mode */
    edit_set_mode_file(s, selected_mode, NULL, f);
    do_load_qerc(s, s->b->filename);

    if (access(b->filename, W_OK)) {
        b->flags |= BF_READONLY;
    }

    if (f) {
        fclose(f);
    }

    /* XXX: invalid place */
    edit_invalidate(s);
    return;

 fail:
    put_status(s, "Could not open '%s'", filename);
}

#if 0

static void load_progress_cb(void *opaque, int size)
{
    EditState *s = opaque;
    EditBuffer *b = s->b;
    if (size >= 1024 && !b->probed)
        probe_mode(s, S_IFREG);
}

static void load_completion_cb(void *opaque, int err)
{
    EditState *s = opaque;
    int mode;

    mode = S_IFREG;
    if (err == -ENOENT || err == -ENOTDIR) {
        put_status(s, "(New file)");
    } else if (err == -EISDIR) {
        mode = S_IFDIR;
    } else if (err < 0) {
        put_status(s, "Could not read file");
    }
    if (!s->b->probed)
        probe_mode(s, mode);
    edit_display(s->qe_state);
    dpy_flush(&global_screen);
}
#endif

void do_find_file(EditState *s, const char *filename)
{
    do_load1(s, filename, 0, 0);
}

void do_find_file_other_window(EditState *s, const char *filename)
{
    QEmacsState *qs = s->qe_state;

    do_split_window(s, 0);
    do_load1(qs->active_window, filename, 0, 0);
}

void do_find_alternate_file(EditState *s, const char *filename)
{
    do_load1(s, filename, 1, 0);
}

void do_load_file_from_path(EditState *s, const char *filename)
{
    do_load1(s, filename, 0, 1);
}

void do_insert_file(EditState *s, const char *filename)
{
    FILE *f;
    int size, lastsize = s->b->total_size;

    f = fopen(filename, "r");
    if (!f) {
        put_status(s, "Could not open file '%s'", filename);
        return;
    }
    /* CG: file charset will not be converted to buffer charset */
    size = raw_load_buffer1(s->b, f, s->offset);
    fclose(f);

    /* mark the insert chunk */
    s->b->mark = s->offset;
    s->offset += s->b->total_size - lastsize;

    if (size < 0) {
        put_status(s, "Error reading '%s'", filename);
        return;
    }
}

void do_set_visited_file_name(EditState *s, const char *filename,
                              const char *renamefile)
{
    char path[MAX_FILENAME_SIZE];

    canonicalize_absolute_path(path, sizeof(path), filename);
    if (*renamefile == 'y' && s->b->filename) {
        if (rename(s->b->filename, path))
            put_status(s, "Cannot rename file to %s", path);
    }
    eb_set_filename(s->b, path);
}

static void put_save_message(EditState *s, const char *filename, int nb)
{
    if (nb >= 0) {
        put_status(s, "Wrote %d bytes to %s", nb, filename);
    } else {
        put_status(s, "Could not write %s", filename);
    }
}

void do_save_buffer(EditState *s)
{
    if (!s->b->modified) {
        /* CG: This behaviour bugs me! */
        put_status(s, "(No changes need to be saved)");
        return;
    }
    put_save_message(s, s->b->filename, eb_save_buffer(s->b));
}

void do_write_file(EditState *s, const char *filename)
{
    do_set_visited_file_name(s, filename, "n");
    do_save_buffer(s);
}

void do_write_region(EditState *s, const char *filename)
{
    char absname[MAX_FILENAME_SIZE];

    canonicalize_absolute_path(absname, sizeof(absname), filename);
    put_save_message(s, filename,
                     eb_write_buffer(s->b, s->b->mark, s->offset, filename));
}

typedef struct QuitState {
    enum {
        QS_ASK,
        QS_NOSAVE,
        QS_SAVE,
    } state;
    int modified;
    EditBuffer *b;
} QuitState;

static void quit_examine_buffers(QuitState *is);
static void quit_key(void *opaque, int ch);
static void quit_confirm_cb(void *opaque, char *reply);

void do_exit_qemacs(EditState *s, int argval)
{
    QEmacsState *qs = s->qe_state;
    QuitState *is;

    if (argval != NO_ARG) {
        url_exit();
        return;
    }

    is = qe_malloc(QuitState);
    if (!is)
        return;

    /* scan each buffer and ask to save it if it was modified */
    is->modified = 0;
    is->state = QS_ASK;
    is->b = qs->first_buffer;

    qe_grab_keys(quit_key, is);
    quit_examine_buffers(is);
}

/* analyse next buffer and ask question if needed */
static void quit_examine_buffers(QuitState *is)
{
    EditBuffer *b;

    while (is->b != NULL) {
        b = is->b;
        if (!(b->flags & BF_SYSTEM) && b->filename[0] != '\0' && b->modified) {
            switch (is->state) {
            case QS_ASK:
                /* XXX: display cursor */
                put_status(NULL, "Save file %s? (y, n, !, ., q) ",
                           b->filename);
                dpy_flush(&global_screen);
                /* will wait for a key */
                return;
            case QS_NOSAVE:
                is->modified = 1;
                break;
            case QS_SAVE:
                eb_save_buffer(b);
                break;
            }
        }
        is->b = is->b->next;
    }
    qe_ungrab_keys();

    /* now asks for confirmation or exit directly */
    if (is->modified) {
        minibuffer_edit(NULL, "Modified buffers exist; exit anyway? (yes or no) ",
                        NULL, NULL,
                        quit_confirm_cb, NULL);
        edit_display(&qe_state);
        dpy_flush(&global_screen);
    } else {
        url_exit();
    }
}

static void quit_key(void *opaque, int ch)
{
    QuitState *is = opaque;
    EditBuffer *b;

    switch (ch) {
    case 'y':
    case ' ':
        /* save buffer */
        goto do_save;
    case 'n':
    case KEY_DELETE:
        is->modified = 1;
        break;
    case 'q':
    case KEY_RET:
        is->state = QS_NOSAVE;
        is->modified = 1;
        break;
    case '!':
        /* save all buffers */
        is->state = QS_SAVE;
        goto do_save;
    case '.':
        /* save current and exit */
        is->state = QS_NOSAVE;
    do_save:
        b = is->b;
        eb_save_buffer(b);
        break;
    case KEY_CTRL('g'):
        /* abort */
        put_status(NULL, "Quit");
        dpy_flush(&global_screen);
        qe_ungrab_keys();
        return;
    default:
        /* get another key */
        return;
    }
    is->b = is->b->next;
    quit_examine_buffers(is);
}

static void quit_confirm_cb(__unused__ void *opaque, char *reply)
{
    if (!reply)
        return;
    if (reply[0] == 'y' || reply[0] == 'Y')
        url_exit();
    qe_free(&reply);
}

/* Search stuff */

#define SEARCH_FLAG_IGNORECASE 0x0001
#define SEARCH_FLAG_SMARTCASE  0x0002 /* case sensitive if upper case present */
#define SEARCH_FLAG_WORD       0x0004

/* XXX: OPTIMIZE ! */
/* XXX: use UTF8 for words/chars ? */
int eb_search(EditBuffer *b, int offset, int dir, int flags,
              const u8 *buf, int size,
              CSSAbortFunc *abort_func, void *abort_opaque)
{
    int total_size = b->total_size;
    int i, c, lower_count, upper_count;
    u8 ch;
    u8 buf1[1024];

    if (size == 0 || size >= (int)sizeof(buf1))
        return -1;

    /* analyze buffer if smart case */
    if (flags & SEARCH_FLAG_SMARTCASE) {
        upper_count = 0;
        lower_count = 0;
        for (i = 0; i < size; i++) {
            c = buf[i];
            lower_count += qe_islower(c);
            upper_count += qe_isupper(c);
        }
        if (lower_count > 0 && upper_count == 0)
            flags |= SEARCH_FLAG_IGNORECASE;
    }

    /* copy buffer */
    for (i = 0; i < size; i++) {
        c = buf[i];
        if (flags & SEARCH_FLAG_IGNORECASE)
            buf1[i] = qe_toupper(c);
        else
            buf1[i] = c;
    }

    if (dir < 0) {
        if (offset > (total_size - size))
            offset = total_size - size;
    } else {
        offset--;
    }

    for (;;) {
        offset += dir;
        if (offset < 0)
            return -1;
        if (offset > (total_size - size))
            return -1;

        /* search abort */
        if ((offset & 0xfff) == 0) {
            if (abort_func && abort_func(abort_opaque))
                return -1;
        }

        /* search start of word */
        if (flags & SEARCH_FLAG_WORD) {
            ch = eb_prevc(b, offset, NULL);
            if (qe_isword(ch))
                continue;
        }

        i = 0;
        for (;;) {
            /* CG: Should bufferize a bit ? */
            eb_read(b, offset + i, &ch, 1);
            if (flags & SEARCH_FLAG_IGNORECASE)
                ch = qe_toupper(ch);
            if (ch != buf1[i])
                break;
            i++;
            if (i == size) {
                /* check end of word */
                if (flags & SEARCH_FLAG_WORD) {
                    ch = eb_nextc(b, offset + size, NULL);
                    if (qe_isword(ch))
                        break;
                }
                return offset;
            }
        }
    }
}

/* should separate search string length and number of match positions */
#define SEARCH_LENGTH  256
#define FOUND_TAG      0x80000000

/* store last searched string */
static unsigned int last_search_string[SEARCH_LENGTH];
static int last_search_string_len = 0;

int search_abort_func(__unused__ void *opaque)
{
    return is_user_input_pending();
}

typedef struct ISearchState {
    EditState *s;
    int start_offset;
    int dir;
    int pos;
    int stack_ptr;
    int search_flags;
    int found_offset;
    unsigned int search_string[SEARCH_LENGTH];
} ISearchState;

static void isearch_display(ISearchState *is)
{
    EditState *s = is->s;
    char ubuf[256];
    buf_t out;
    u8 buf[2*SEARCH_LENGTH], *q; /* XXX: incorrect size */
    int i, len, hex_nibble, h;
    unsigned int v;
    int search_offset;
    int flags;

    /* prepare the search bytes */
    q = buf;
    search_offset = is->start_offset;
    hex_nibble = 0;
    for (i = 0; i < is->pos; i++) {
        v = is->search_string[i];
        if (!(v & FOUND_TAG)) {
            if ((q - buf) < ((int)sizeof(buf) - 10)) {
                if (s->hex_mode) {
                    h = to_hex(v);
                    if (h >= 0) {
                        if (hex_nibble == 0)
                            *q = h << 4;
                        else
                            *q++ |= h;
                        hex_nibble ^= 1;
                    }
                    /* CG: should handle unihex mode */
                } else {
                    q += unicode_to_charset((char *)q, v, s->b->charset);
                }
            }
        } else {
            search_offset = (v & ~FOUND_TAG) + is->dir;
        }
    }
    len = q - buf;
    if (len == 0) {
        s->offset = is->start_offset;
        is->found_offset = -1;
    } else {
        flags = is->search_flags;
        if (s->hex_mode)
            flags = 0;
        is->found_offset = eb_search(s->b, search_offset, is->dir, flags,
                                     buf, len, search_abort_func, NULL);
        if (is->found_offset >= 0)
            s->offset = is->found_offset + len;
    }

    /* display search string */
    buf_init(&out, ubuf, sizeof(ubuf));
    if (is->found_offset < 0 && len > 0)
        buf_printf(&out, "Failing ");
    if (s->hex_mode) {
        buf_printf(&out, "hex ");
    } else {
        if (is->search_flags & SEARCH_FLAG_WORD)
            buf_printf(&out, "word ");
        if (is->search_flags & SEARCH_FLAG_IGNORECASE)
            buf_printf(&out, "case-insensitive ");
        else if (!(is->search_flags & SEARCH_FLAG_SMARTCASE))
            buf_printf(&out, "case-sensitive ");
    }
    buf_printf(&out, "I-search");
    if (is->dir < 0)
        buf_printf(&out, " backward");
    buf_printf(&out, ": ");
    for (i = 0; i < is->pos; i++) {
        v = is->search_string[i];
        if (!(v & FOUND_TAG)) {
            if (!buf_putc_utf8(&out, v))
                break;
        }
    }

    /* display text */
    do_center_cursor(s);
    edit_display(s->qe_state);

    put_status(NULL, "%s", out.buf);

    dpy_flush(s->screen);
}

static void isearch_key(void *opaque, int ch)
{
    ISearchState *is = opaque;
    EditState *s = is->s;
    int i, j;

    switch (ch) {
    case KEY_DEL:
    case KEY_BS:        /* Should test actual binding of KEY_BS */
        if (is->pos > 0)
            is->pos--;
        break;
    case KEY_CTRL('g'):
        s->offset = is->start_offset;
        put_status(s, "Quit");
    the_end:
        /* save current searched string */
        if (is->pos > 0) {
            j = 0;
            for (i = 0; i < is->pos; i++) {
                if (!(is->search_string[i] & FOUND_TAG))
                    last_search_string[j++] = is->search_string[i];
            }
            last_search_string_len = j;
        }
        qe_ungrab_keys();
        qe_free(&is);
        return;
    case KEY_CTRL('s'):
        is->dir = 1;
        goto addpos;
    case KEY_CTRL('r'):
        is->dir = -1;
    addpos:
        /* use last seached string if no input */
        if (is->pos == 0) {
            memcpy(is->search_string, last_search_string,
                   last_search_string_len * sizeof(unsigned int));
            is->pos = last_search_string_len;
        } else {
            /* add the match position, if any */
            if (is->pos < SEARCH_LENGTH && is->found_offset >= 0)
                is->search_string[is->pos++] = FOUND_TAG | is->found_offset;
        }
        break;
#if 0
    case KEY_CTRL('q'):
        ch = get_key(s->screen);
        goto addch;
    case KEY_CTRL('w'):
    case KEY_CTRL('y'):
        /* emacs compatibility: get word / line */
        /* CG: should yank into search string */
        break;
#endif
        /* case / word */
    case KEY_CTRL('w'):
        is->search_flags ^= SEARCH_FLAG_WORD;
        break;
    case KEY_CTRL('c'):
        is->search_flags ^= SEARCH_FLAG_IGNORECASE;
        is->search_flags &= ~SEARCH_FLAG_SMARTCASE;
        break;
    default:
        if (KEY_SPECIAL(ch)) {
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
            put_status(s, "Mark saved where search started");
            /* repost key */
            if (ch != KEY_RET)
                unget_key(ch);
            goto the_end;
        } else {
            //addch:
            if (is->pos < SEARCH_LENGTH) {
                is->search_string[is->pos++] = ch;
            }
        }
        break;
    }
    isearch_display(is);
}

/* XXX: handle busy */
void do_isearch(EditState *s, int dir)
{
    ISearchState *is;

    is = qe_malloc(ISearchState);
    if (!is)
        return;
    is->s = s;
    is->start_offset = s->offset;
    is->dir = dir;
    is->pos = 0;
    is->stack_ptr = 0;
    is->search_flags = SEARCH_FLAG_SMARTCASE;

    qe_grab_keys(isearch_key, is);
    isearch_display(is);
}

static int to_bytes(EditState *s1, u8 *dst, int dst_size, const char *str)
{
    const char *s;
    int c, len, hex_nibble, h;
    u8 *d;

    d = dst;
    if (s1->hex_mode) {
        s = str;
        h = 0;
        hex_nibble = 0;
        for (;;) {
            c = *s++;
            if (c == '\0')
                break;
            c = to_hex(c);
            if (c >= 0) {
                h = (h << 4) | c;
                if (hex_nibble) {
                    if (d < dst + dst_size)
                        *d++ = h;
                    h = 0;
                }
                hex_nibble ^= 1;
            }
        }
        return d - dst;
    } else {
        /* XXX: potentially incorrect if charset change */
        len = strlen(str);
        if (len > dst_size)
            len = dst_size;
        memcpy(dst, str, len);
        return len;
    }
}

typedef struct QueryReplaceState {
    EditState *s;
    int nb_reps;
    int search_bytes_len, replace_bytes_len, found_offset;
    int replace_all;
    int flags;
    char search_str[SEARCH_LENGTH];
    char replace_str[SEARCH_LENGTH];
    u8 search_bytes[SEARCH_LENGTH];
    u8 replace_bytes[SEARCH_LENGTH];
} QueryReplaceState;

static void query_replace_abort(QueryReplaceState *is)
{
    EditState *s = is->s;

    qe_ungrab_keys();
    put_status(NULL, "Replaced %d occurrences", is->nb_reps);
    qe_free(&is);
    edit_display(s->qe_state);
    dpy_flush(&global_screen);
}

static void query_replace_replace(QueryReplaceState *is)
{
    EditState *s = is->s;

    eb_delete(s->b, is->found_offset, is->search_bytes_len);
    eb_insert(s->b, is->found_offset, is->replace_bytes, is->replace_bytes_len);
    is->found_offset += is->replace_bytes_len;
    is->nb_reps++;
}

static void query_replace_display(QueryReplaceState *is)
{
    EditState *s = is->s;

 redo:
    is->found_offset = eb_search(s->b, is->found_offset, 1, is->flags,
                                 is->search_bytes, is->search_bytes_len,
                                 NULL, NULL);
    if (is->found_offset < 0) {
        query_replace_abort(is);
        return;
    }

    if (is->replace_all) {
        query_replace_replace(is);
        goto redo;
    }

    /* display text */
    s->offset = is->found_offset;
    do_center_cursor(s);
    edit_display(s->qe_state);

    put_status(NULL, "Query replace %s with %s: ",
               is->search_str, is->replace_str);
    dpy_flush(&global_screen);
}

static void query_replace_key(void *opaque, int ch)
{
    QueryReplaceState *is = opaque;

    switch (ch) {
    case 'Y':
    case 'y':
    case KEY_SPC:
        query_replace_replace(is);
        break;
    case '!':
        is->replace_all = 1;
        break;
    case 'N':
    case 'n':
    case KEY_DELETE:
        break;
    case '.':
        query_replace_replace(is);
        /* FALL THRU */
    default:
        query_replace_abort(is);
        return;
    }
    query_replace_display(is);
}

static void query_replace(EditState *s,
                          const char *search_str,
                          const char *replace_str, int all, int flags)
{
    QueryReplaceState *is;

    if (s->b->flags & BF_READONLY)
        return;

    is = qe_mallocz(QueryReplaceState);
    if (!is)
        return;
    is->s = s;
    pstrcpy(is->search_str, sizeof(is->search_str), search_str);
    pstrcpy(is->replace_str, sizeof(is->replace_str), replace_str);

    is->search_bytes_len = to_bytes(s, is->search_bytes, sizeof(is->search_bytes),
                                    search_str);
    is->replace_bytes_len = to_bytes(s, is->replace_bytes, sizeof(is->replace_bytes),
                                     replace_str);
    is->nb_reps = 0;
    is->replace_all = all;
    is->found_offset = s->offset;
    is->flags = flags;

    qe_grab_keys(query_replace_key, is);
    query_replace_display(is);
}

void do_query_replace(EditState *s, const char *search_str,
                      const char *replace_str)
{
    query_replace(s, search_str, replace_str, 0, 0);
}

void do_replace_string(EditState *s, const char *search_str,
                       const char *replace_str, int argval)
{
    query_replace(s, search_str, replace_str, 1,
                  argval == NO_ARG ? 0 : SEARCH_FLAG_WORD);
}

void do_search_string(EditState *s, const char *search_str, int dir)
{
    u8 search_bytes[SEARCH_LENGTH];
    int search_bytes_len;
    int found_offset;

    search_bytes_len = to_bytes(s, search_bytes, sizeof(search_bytes),
                                search_str);

    found_offset = eb_search(s->b, s->offset, dir, 0,
                             search_bytes, search_bytes_len, NULL, NULL);
    if (found_offset >= 0) {
        s->offset = found_offset;
        do_center_cursor(s);
    }
}

/*----------------*/

void do_doctor(EditState *s)
{
    /* Should show keys? */
    put_status(s, "Hello, how are you ?");
}

static int get_line_height(QEditScreen *screen, int style_index)
{
    QEFont *font;
    QEStyleDef style;
    int height;

    get_style(NULL, &style, style_index);
    font = select_font(screen, style.font_style, style.font_size);
    height = font->ascent + font->descent;
    release_font(screen, font);
    return height;
}

void edit_invalidate(EditState *s)
{
    /* invalidate the modeline buffer */
    s->modeline_shadow[0] = '\0';
    s->display_invalid = 1;
}

/* refresh the screen, s1 can be any edit window */
void do_refresh(__unused__ EditState *s1)
{
    /* CG: s1 may be NULL */
    QEmacsState *qs = &qe_state;
    EditState *e;
    int new_status_height, new_mode_line_height, content_height;
    int width, height, resized;

    if (qs->complete_refresh) {
        dpy_invalidate(qs->screen);
    }

    /* recompute various dimensions */
    if (qs->screen->media & CSS_MEDIA_TTY) {
        qs->separator_width = 1;
    } else {
        qs->separator_width = 4;
    }
    qs->border_width = 1; /* XXX: adapt to display type */

    width = qs->screen->width;
    height = qs->screen->height;
    new_status_height = get_line_height(qs->screen, QE_STYLE_STATUS);
    new_mode_line_height = get_line_height(qs->screen, QE_STYLE_MODE_LINE);
    content_height = height;
    if (!qs->hide_status)
        content_height -= new_status_height;

    resized = 0;

    /* see if resize is necessary */
    if (qs->width != width ||
        qs->height != height ||
        qs->status_height != new_status_height ||
        qs->mode_line_height != new_mode_line_height ||
        qs->content_height != content_height) {

        /* do the resize */
        resized = 1;
        for (e = qs->first_window; e != NULL; e = e->next_window) {
            if (e->minibuf) {
                /* first resize minibuffer if present */
                e->x1 = 0;
                e->y1 = content_height;
                e->x2 = width;
                e->y2 = height;
            } else if (qs->height == 0) {
                /* needed only to init the window size for the first time */
                e->x1 = 0;
                e->y1 = 0;
                e->y2 = content_height;
                e->x2 = width;
            } else {
                /* NOTE: to ensure that no rounding errors are made,
                   we resize the absolute coordinates */
                e->x1 = (e->x1 * width) / qs->width;
                e->x2 = (e->x2 * width) / qs->width;
                e->y1 = (e->y1 * content_height) / qs->content_height;
                e->y2 = (e->y2 * content_height) / qs->content_height;
            }
        }

        qs->width = width;
        qs->height = height;
        qs->status_height = new_status_height;
        qs->mode_line_height = new_mode_line_height;
        qs->content_height = content_height;
    }
    /* compute client area */
    for (e = qs->first_window; e != NULL; e = e->next_window)
        compute_client_area(e);

    /* invalidate all the edit windows and draw borders */
    for (e = qs->first_window; e != NULL; e = e->next_window) {
        edit_invalidate(e);
        e->borders_invalid = 1;
    }
    /* invalidate status line */
    qs->status_shadow[0] = '\0';

    if (resized) {
        /* CG: should compute column count w/ default count */
        put_status(NULL, "Screen is now %d by %d (%d rows)",
                   width, height, height / new_status_height);
    }
}

void do_refresh_complete(EditState *s)
{
    QEmacsState *qs = s->qe_state;

    qs->complete_refresh = 1;
    do_refresh(s);
}

void do_other_window(EditState *s)
{
    QEmacsState *qs = s->qe_state;
    EditState *e;

    e = s->next_window;
    if (!e)
        e = qs->first_window;
    qs->active_window = e;
}

void do_previous_window(EditState *s)
{
    QEmacsState *qs = s->qe_state;
    EditState *e;

    for (e = qs->first_window; e->next_window != NULL; e = e->next_window) {
        if (e->next_window == s)
            break;
    }
    qs->active_window = e;
}

/* Delete a window and try to resize other windows so that it gets
   covered. If force is not true, do not accept to kill window if it
   is the only window or if it is the minibuffer window. */
void do_delete_window(EditState *s, int force)
{
    QEmacsState *qs = s->qe_state;
    EditState *e;
    int count, x1, y1, x2, y2;
    int ex1, ey1, ex2, ey2;

    count = 0;
    for (e = qs->first_window; e != NULL; e = e->next_window) {
        if (!e->minibuf && !(e->flags & WF_POPUP))
            count++;
    }
    /* cannot close minibuf or if single window */
    if ((s->minibuf || count <= 1) && !force)
        return;

    if (!(s->flags & WF_POPUP)) {
        /* try to merge the window with one adjacent window. If none
           found, just leave a hole */
        x1 = s->x1;
        x2 = s->x2;
        y1 = s->y1;
        y2 = s->y2;

        for (e = qs->first_window; e != NULL; e = e->next_window) {
            if (e->minibuf || e == s)
                continue;
            ex1 = e->x1;
            ex2 = e->x2;
            ey1 = e->y1;
            ey2 = e->y2;

            if (x1 == ex2 && y1 == ey1 && y2 == ey2) {
                /* left border */
                e->x2 = x2;
                break;
            } else
            if (x2 == ex1 && y1 == ey1 && y2 == ey2) {
                /* right border */
                e->x1 = x1;
                break;
            } else
            if (y1 == ey2 && x1 == ex1 && x2 == ex2) {
                /* top border */
                e->y2 = y2;
                break;
            } else
            if (y2 == ey1 && x1 == ex1 && x2 == ex2) {
                /* bottom border */
                e->y1 = y1;
                break;
            }
        }
        if (e)
            compute_client_area(e);
    }
    if (qs->active_window == s)
        qs->active_window = e ? e : qs->first_window;
    edit_close(s);
    if (qs->first_window)
        do_refresh(qs->first_window);
}

void do_delete_other_windows(EditState *s)
{
    QEmacsState *qs = s->qe_state;
    EditState *e, *e1;

    for (e = qs->first_window; e != NULL; e = e1) {
        e1 = e->next_window;
        if (!e->minibuf && e != s)
            edit_close(e);
    }
    /* resize to whole screen */
    s->y1 = 0;
    s->x1 = 0;
    s->x2 = qs->width;
    s->y2 = qs->height - qs->status_height;
    s->flags &= ~WF_RSEPARATOR;
    compute_client_area(s);
    do_refresh(s);
}

/* XXX: add minimum size test and refuse to split if reached */
void do_split_window(EditState *s, int horiz)
{
    QEmacsState *qs = s->qe_state;
    EditState *e;
    int x, y;

    /* cannot split minibuf or popup */
    if (s->minibuf || (s->flags & WF_POPUP))
        return;

    if (horiz) {
        x = (s->x2 + s->x1) / 2;
        e = edit_new(s->b, x, s->y1,
                     s->x2 - x, s->y2 - s->y1, WF_MODELINE);
        if (!e)
            return;
        s->x2 = x;
        s->flags |= WF_RSEPARATOR;
        s->wrap = e->wrap = WRAP_TRUNCATE;
    } else {
        y = (s->y2 + s->y1) / 2;
        e = edit_new(s->b, s->x1, y,
                     s->x2 - s->x1, s->y2 - y,
                     WF_MODELINE | (s->flags & WF_RSEPARATOR));
        if (!e)
            return;
        s->y2 = y;
    }
    /* insert in the window list after current window */
    edit_detach(e);
    edit_attach(e, &s->next_window);

    if (qs->flag_split_window_change_focus)
        qs->active_window = e;

    compute_client_area(s);
    do_refresh(s);
}

/* help */

static void print_bindings(EditBuffer *b, const char *title,
                           __unused__ int type, ModeDef *mode)
{
    CmdDef *d;
    KeyDef *kd;
    char buf[64];
    int found, gfound, pos;

    d = qe_state.first_cmd;
    gfound = 0;
    while (d != NULL) {
        while (d->name != NULL) {
            /* find each key mapping pointing to this command */
            found = pos = 0;
            kd = mode ? mode->first_key : qe_state.first_key;
            for (; kd != NULL; kd = kd->next) {
                if (kd->cmd == d) {
                    if (!gfound)
                        eb_printf(b, "%s:\n\n", title);
                    if (found)
                        pos += eb_printf(b, ",");
                    if (pos > 50) {
                        eb_printf(b, "\n");
                        pos = 0;
                    }
                    pos += eb_printf(b, " %s",
                                     keys_to_str(buf, sizeof(buf),
                                                 kd->keys, kd->nb_keys));
                    found = 1;
                    gfound = 1;
                }
            }
            if (found) {
                /* print associated command name */
                if (pos > 25) {
                    eb_printf(b, "\n");
                    pos = 0;
                }
                eb_line_pad(b, 25);
                eb_printf(b, ": %s\n", d->name);
            }
            d++;
        }
        d = d->action.next;
    }
}

static EditBuffer *new_help_buffer(int *show_ptr)
{
    EditBuffer *b;

    *show_ptr = 0;
    b = eb_find("*Help*");
    if (b) {
        eb_delete(b, 0, b->total_size);
    } else {
        b = eb_new("*Help*", BF_UTF8);
        *show_ptr = 1;
    }
    return b;
}

void do_describe_bindings(EditState *s)
{
    EditBuffer *b;
    char buf[64];
    int show;

    b = new_help_buffer(&show);
    if (!b)
        return;
    snprintf(buf, sizeof(buf), "%s mode bindings", s->mode->name);
    print_bindings(b, buf, 0, s->mode);

    print_bindings(b, "\nGlobal bindings", 0, NULL);

    b->flags |= BF_READONLY;
    if (show) {
        show_popup(b);
        //eb_free(b);
    }
}

void do_help_for_help(__unused__ EditState *s)
{
    EditBuffer *b;
    int show;

    b = new_help_buffer(&show);
    if (!b)
        return;
    eb_printf(b,
              "QEmacs help for help - Press q to quit:\n"
              "\n"
              "C-h C-h   Show this help\n"
              "C-h b     Display table of all key bindings\n"
              "C-h c     Describe key briefly\n"
              );
    b->flags |= BF_READONLY;
    if (show) {
        show_popup(b);
    }
}

void do_describe_key_briefly(EditState *s)
{
    put_status(s, "Describe key: ");
    key_ctx.describe_key = 1;
}

#ifdef CONFIG_WIN32

void qe_event_init(void)
{
}

#else

/* we install a signal handler to set poll_flag to one so that we can
   avoid polling too often in some cases */

int __fast_test_event_poll_flag = 0;

static void poll_action(__unused__ int sig)
{
    __fast_test_event_poll_flag = 1;
}

/* init event system */
void qe_event_init(void)
{
    struct sigaction sigact;
    struct itimerval itimer;

    sigact.sa_flags = SA_RESTART;
    sigact.sa_handler = poll_action;
    sigemptyset(&sigact.sa_mask);
    sigaction(SIGVTALRM, &sigact, NULL);

    itimer.it_interval.tv_sec = 0;
    itimer.it_interval.tv_usec = 20 * 1000; /* 50 times per second */
    itimer.it_value = itimer.it_interval;
    setitimer(ITIMER_VIRTUAL, &itimer, NULL);
}

/* see also qe_fast_test_event() */
int __is_user_input_pending(void)
{
    QEditScreen *s = &global_screen;
    return s->dpy.dpy_is_user_input_pending(s);
}

#endif

#ifndef CONFIG_TINY

void window_get_min_size(EditState *s, int *w_ptr, int *h_ptr)
{
    QEmacsState *qs = s->qe_state;
    int w, h;

    /* XXX: currently, fixed height */
    w = 8;
    h = 8;
    if (s->flags & WF_MODELINE)
        h += qs->mode_line_height;
    *w_ptr = w;
    *h_ptr = h;
}

/* resize a window on bottom right edge */
void window_resize(EditState *s, int target_w, int target_h)
{
    QEmacsState *qs = s->qe_state;
    EditState *e;
    int delta_y, delta_x, min_w, min_h, new_h, new_w;

    delta_x = target_w - (s->x2 - s->x1);
    delta_y = target_h - (s->y2 - s->y1);

    /* then see if we can resize without having too small windows */
    window_get_min_size(s, &min_w, &min_h);
    if (target_w < min_w ||
        target_h < min_h)
        return;
    /* check if moving would uncover empty regions */
    if ((s->x2 >= qs->screen->width && delta_x != 0) ||
        (s->y2 >= qs->screen->height - qs->status_height && delta_y != 0))
        return;

    for (e = qs->first_window; e != NULL; e = e->next_window) {
        if (e->minibuf || e == s)
            continue;
        window_get_min_size(e, &min_w, &min_h);
        if (e->y1 == s->y2) {
            new_h = e->y2 - e->y1 - delta_y;
            goto test_h;
        } else if (e->y2 == s->y2) {
            new_h = e->y2 - e->y1 + delta_y;
        test_h:
            if (new_h < min_h)
                return;
        }
        if (e->x1 == s->x2) {
            new_w = e->x2 - e->x1 - delta_x;
            goto test_w;
        } else if (e->x2 == s->x2) {
            new_w = e->x2 - e->x1 + delta_x;
        test_w:
            if (new_w < min_w)
                return;
        }
    }

    /* now everything is OK, we can resize all windows */
    for (e = qs->first_window; e != NULL; e = e->next_window) {
        if (e->minibuf || e == s)
            continue;
        if (e->y1 == s->y2)
            e->y1 += delta_y;
        else if (e->y2 == s->y2)
            e->y2 += delta_y;
        if (e->x1 == s->x2)
            e->x1 += delta_x;
        else if (e->x2 == s->x2)
            e->x2 += delta_x;
        compute_client_area(e);
    }
    s->x2 = s->x1 + target_w;
    s->y2 = s->y1 + target_h;
    compute_client_area(s);
}

/* mouse handling */

#define MOTION_NONE       0
#define MOTION_MODELINE   1
#define MOTION_RSEPARATOR 2
#define MOTION_TEXT       3

static int motion_type = MOTION_NONE;
static EditState *motion_target;
static int motion_x, motion_y;

static int check_motion_target(__unused__ EditState *s)
{
    QEmacsState *qs = &qe_state;
    EditState *e;
    /* first verify that window is always valid */
    for (e = qs->first_window; e != NULL; e = e->next_window) {
        if (e == motion_target)
            break;
    }
    return e != NULL;
}

/* remove temporary selection colorization and selection area */
static void save_selection(void)
{
    QEmacsState *qs = &qe_state;
    EditState *e;
    int selection_showed;

    selection_showed = 0;
    for (e = qs->first_window; e != NULL; e = e->next_window) {
        selection_showed |= e->show_selection;
        e->show_selection = 0;
    }
    if (selection_showed && motion_type == MOTION_TEXT) {
        motion_type = MOTION_NONE;
        e = motion_target;
        if (!check_motion_target(e))
            return;
        do_kill_region(e, 0);
    }
}

/* XXX: need a more general scheme for other modes such as HTML/image */
/* CG: remove this */
void wheel_scroll_up_down(EditState *s, int dir)
{
    int line_height;

    /* only apply to text modes */
    if (!s->mode->text_display)
        return;

    line_height = get_line_height(s->screen, s->default_style);
    perform_scroll_up_down(s, dir * WHEEL_SCROLL_STEP * line_height);
}

void qe_mouse_event(QEEvent *ev)
{
    QEmacsState *qs = &qe_state;
    EditState *e;
    int mouse_x, mouse_y;
    mouse_x = ev->button_event.x;
    mouse_y = ev->button_event.y;

    switch (ev->type) {
    case QE_BUTTON_RELEASE_EVENT:
        save_selection();
        motion_type = MOTION_NONE;
        break;

    case QE_BUTTON_PRESS_EVENT:
        for (e = qs->first_window; e != NULL; e = e->next_window) {
            /* test if mouse is inside the text area */
            if (mouse_x >= e->xleft && mouse_x < e->xleft + e->width &&
                mouse_y >= e->ytop && mouse_y < e->ytop + e->height) {
                if (e->mode->mouse_goto) {
                    switch (ev->button_event.button) {
                    case QE_BUTTON_LEFT:
                        save_selection();
                        e->mode->mouse_goto(e, mouse_x - e->xleft,
                                            mouse_y - e->ytop);
                        motion_type = MOTION_TEXT;
                        motion_x = 0; /* indicate first move */
                        motion_target = e;
                        break;
                    case QE_BUTTON_MIDDLE:
                        save_selection();
                        e->mode->mouse_goto(e, mouse_x - e->xleft,
                                            mouse_y - e->ytop);
                        do_yank(e);
                        break;
                    case QE_WHEEL_UP:
                        wheel_scroll_up_down(e, -1);
                        break;
                    case QE_WHEEL_DOWN:
                        wheel_scroll_up_down(e, 1);
                        break;
                    }
                    edit_display(qs);
                    dpy_flush(qs->screen);
                }
                break;
            }
            /* test if inside modeline */
            if ((e->flags & WF_MODELINE) &&
                mouse_x >= e->xleft && mouse_x < e->xleft + e->width &&
                mouse_y >= e->ytop + e->height &&
                mouse_y < e->ytop + e->height + qs->mode_line_height) {
                /* mark that motion can occur */
                motion_type = MOTION_MODELINE;
                motion_target = e;
                motion_y = e->ytop + e->height;
                break;
            }
            /* test if inside right window separator */
            if ((e->flags & WF_RSEPARATOR) &&
                mouse_x >= e->x2 - qs->separator_width && mouse_x < e->x2 &&
                mouse_y >= e->ytop && mouse_y < e->ytop + e->height) {
                /* mark that motion can occur */
                motion_type = MOTION_RSEPARATOR;
                motion_target = e;
                motion_x = e->x2 - qs->separator_width;
                break;
            }
        }
        break;
    case QE_MOTION_EVENT:
        switch (motion_type) {
        case MOTION_NONE:
        default:
            break;
        case MOTION_TEXT:
            {
                e = motion_target;
                if (!check_motion_target(e)) {
                    e->show_selection = 0;
                    motion_type = MOTION_NONE;
                } else {
                    /* put a mark if first move */
                    if (!motion_x) {
                        /* test needed for list mode */
                        if (e->b)
                            e->b->mark = e->offset;
                        motion_x = 1;
                    }
                    /* highlight selection */
                    e->show_selection = 1;
                    if (mouse_x >= e->xleft && mouse_x < e->xleft + e->width &&
                        mouse_y >= e->ytop && mouse_y < e->ytop + e->height) {
                            /* if inside the buffer, then update cursor
                            position */
                            e->mode->mouse_goto(e, mouse_x - e->xleft,
                                                mouse_y - e->ytop);
                            edit_display(qs);
                            dpy_flush(qs->screen);
                     }
                }
            }
            break;
        case MOTION_MODELINE:
            if ((mouse_y / 8) != (motion_y / 8)) {
                if (!check_motion_target(motion_target)) {
                    motion_type = MOTION_NONE;
                } else {
                    motion_y = mouse_y;
                    window_resize(motion_target,
                                  motion_target->x2 - motion_target->x1,
                                  motion_y - motion_target->y1);
                    do_refresh(qs->first_window);
                    edit_display(qs);
                    dpy_flush(qs->screen);
                }
            }
            break;
        case MOTION_RSEPARATOR:
            if ((mouse_x / 8) != (motion_x / 8)) {
                if (!check_motion_target(motion_target)) {
                    motion_type = MOTION_NONE;
                } else {
                    motion_x = mouse_x;
                    window_resize(motion_target,
                                  motion_x - motion_target->x1,
                                  motion_target->y2 - motion_target->y1);
                    do_refresh(qs->first_window);
                    edit_display(qs);
                    dpy_flush(qs->screen);
                }
            }
            break;
        }
        break;
    default:
        break;
    }
}
#endif

/* put key in the unget buffer so that get_key() will return it */
void unget_key(int key)
{
    QEmacsState *qs = &qe_state;
    qs->ungot_key = key;
}

/* handle an event sent by the GUI */
void qe_handle_event(QEEvent *ev)
{
    QEmacsState *qs = &qe_state;

    switch (ev->type) {
    case QE_KEY_EVENT:
        qe_key_process(ev->key_event.key);
        break;
    case QE_EXPOSE_EVENT:
        do_refresh(qs->first_window);
        goto redraw;
    case QE_UPDATE_EVENT:
    redraw:
        edit_display(qs);
        dpy_flush(qs->screen);
        break;
#ifndef CONFIG_TINY
    case QE_BUTTON_PRESS_EVENT:
    case QE_BUTTON_RELEASE_EVENT:
    case QE_MOTION_EVENT:
        qe_mouse_event(ev);
        break;
    case QE_SELECTION_CLEAR_EVENT:
        save_selection();
        goto redraw;
#endif
    default:
        break;
    }
}

/* text mode */

#if 0
int detect_binary(const u8 *buf, int size)
{
    int i, c;

    for (i = 0; i < size; i++) {
        c = buf[i];
        if (c < 32 && (c != '\r' && c != '\n' && c != '\t' && c != '\e'))
            return 1;
    }
    /* Treat very long sequences of identical characters as binary */
    for (i = 0; i < size; i++) {
        if (buf[i] != buf[0])
            break;
    }
    if (i == size && size >= 2048 && buf[0] != '\n')
        return 1;

    return 0;
}
#endif

static int text_mode_probe(__unused__ ModeProbeData *p)
{
#if 0
    /* text mode inappropriate for huge binary files */
    if (detect_binary(p->buf, p->buf_size) && p->total_size > 1000000)
        return 0;
    else
#endif
        return 20;
}

int text_mode_init(EditState *s, ModeSavedData *saved_data)
{
    eb_add_callback(s->b, eb_offset_callback, &s->offset);
    eb_add_callback(s->b, eb_offset_callback, &s->offset_top);
    if (!saved_data) {
        memset(s, 0, SAVED_DATA_SIZE);
        s->insert = 1;
        s->tab_size = 8;
        s->indent_size = 4;
        s->default_style = QE_STYLE_DEFAULT;
        s->wrap = WRAP_LINE;
    } else {
        memcpy(s, saved_data->generic_data, SAVED_DATA_SIZE);
    }
    s->hex_mode = 0;
    set_colorize_func(s, NULL);
    return 0;
}

/* generic save mode data (saves text presentation information) */
ModeSavedData *generic_mode_save_data(EditState *s)
{
    ModeSavedData *saved_data;

    saved_data = qe_malloc(ModeSavedData);
    if (!saved_data)
        return NULL;
    saved_data->mode = s->mode;
    memcpy(saved_data->generic_data, s, SAVED_DATA_SIZE);
    return saved_data;
}

void text_mode_close(EditState *s)
{
    /* free all callbacks or associated buffer data */
    set_colorize_func(s, NULL);
    eb_free_callback(s->b, eb_offset_callback, &s->offset);
    eb_free_callback(s->b, eb_offset_callback, &s->offset_top);
}

ModeDef text_mode = {
    "text",
    .instance_size = 0,
    .mode_probe = text_mode_probe,
    .mode_init = text_mode_init,
    .mode_close = text_mode_close,

    .text_display = text_display,
    .text_backward_offset = text_backward_offset,

    .move_up_down = text_move_up_down,
    .move_left_right = text_move_left_right_visual,
    .move_bol = text_move_bol,
    .move_eol = text_move_eol,
    .move_word_left_right = text_move_word_left_right,
    .scroll_up_down = text_scroll_up_down,
    .write_char = text_write_char,
    .mouse_goto = text_mouse_goto,
};

/* find a resource file */
int find_resource_file(char *path, int path_size, const char *pattern)
{
    QEmacsState *qs = &qe_state;
    FindFileState *ffst;
    int ret;

    ffst = find_file_open(qs->res_path, pattern);
    if (!ffst)
        return -1;
    ret = find_file_next(ffst, path, path_size);

    find_file_close(ffst);

    return ret;
}

/******************************************************/
/* config file parsing */

/* CG: error messages should go to the *error* buffer.
 * displayed as a popup upon start.
 */

static int expect_token(const char **pp, int tok)
{
    skip_spaces(pp);
    if (**pp == tok) {
        ++*pp;
        skip_spaces(pp);
        return 1;
    } else {
        put_status(NULL, "'%c' expected", tok);
        return 0;
    }
}

static int qe_cfg_parse_string(EditState *s, const char **pp,
                               char *dest, int size)
{
    const char *p = *pp;
    int c, delim = *p++;
    int res = 0;
    int pos = 0;

    for (;;) {
        c = *p;
        if (c == '\0') {
            put_status(s, "Unterminated string");
            res = -1;
            break;
        }
        p++;
        if (c == delim)
            break;
        if (c == '\\') {
            c = *p++;
            switch (c) {
            case 'n':
                c = '\n';
                break;
            case 'r':
                c = '\r';
                break;
            case 't':
                c = '\t';
                break;
            }
        }
        if (pos < size - 1)
            dest[pos++] = c;
    }
    if (pos < size)
        dest[pos] = '\0';
    *pp = p;
    return res;
}

int parse_config_file(EditState *s, const char *filename)
{
    QEmacsState *qs = s->qe_state;
    QErrorContext ec;
    FILE *f;
    char line[1024], str[1024];
    char prompt[64], cmd[128], *q, *strp;
    const char *p, *r;
    int err, line_num;
    CmdDef *d;
    int nb_args, c, sep, i, skip;
    CmdArg args[MAX_CMD_ARGS];
    unsigned char args_type[MAX_CMD_ARGS];

    f = fopen(filename, "r");
    if (!f)
        return -1;
    ec = qs->ec;
    skip = 0;
    err = 0;
    line_num = 0;
    /* Should parse whole config file in a single read, or load it via
     * a buffer */
    for (;;) {
        if (fgets(line, sizeof(line), f) == NULL)
            break;
        line_num++;
        qs->ec.filename = filename;
        qs->ec.function = NULL;
        qs->ec.lineno = line_num;

        p = line;
        skip_spaces(&p);
        if (p[0] == '}') {
            /* simplistic 1 level if block skip feature */
            p++;
            skip_spaces(&p);
            skip = 0;
        }
        if (skip)
            continue;

        /* skip comments */
        while (p[0] == '/' && p[1] == '*') {
            for (p += 2; *p; p++) {
                if (*p == '*' && p[1] == '/') {
                    p += 2;
                    break;
                }
            }
            skip_spaces(&p);
            /* CG: unfinished comments silently unsupported */
        }
        if (p[0] == '/' && p[1] == '/')
            continue;
        if (p[0] == '\0')
            continue;

        get_str(&p, cmd, sizeof(cmd), "(");
        if (*cmd == '\0') {
            put_status(s, "Syntax error");
            continue;
        }
        /* transform '_' to '-' */
        q = cmd;
        while (*q) {
            if (*q == '_')
                *q = '-';
            q++;
        }
        /* simplistic 1 level if block skip feature */
        if (strequal(cmd, "if")) {
            if (!expect_token(&p, '('))
                goto fail;
            skip = !strtol(p, (char**)&p, 0);
            if (!expect_token(&p, ')') || !expect_token(&p, '{'))
                goto fail;
            continue;
        }
#ifndef CONFIG_TINY
        {
            /* search for variable */
            struct VarDef *vp;

            vp = qe_find_variable(cmd);
            if (vp) {
                if (!expect_token(&p, '='))
                    goto fail;
                skip_spaces(&p);
                if (*p == '\"' || *p == '\'') {
                    if (qe_cfg_parse_string(s, &p, str, countof(str)))
                        goto fail;
                    qe_set_variable(s, cmd, str, 0);
                } else {
                    qe_set_variable(s, cmd, NULL, strtol(p, (char**)&p, 0));
                }
                skip_spaces(&p);
                if (*p != ';' && *p != '\n')
                    put_status(s, "Syntax error '%s'", cmd);
                continue;
            }
        }
#endif
        /* search for command */
        d = qe_find_cmd(cmd);
        if (!d) {
            err = -1;
            put_status(s, "Unknown command '%s'", cmd);
            continue;
        }
        nb_args = 0;

        /* first argument is always the window */
        args_type[nb_args++] = CMD_ARG_WINDOW;

        /* construct argument type list */
        r = d->name + strlen(d->name) + 1;
        if (*r == '*') {
            r++;
            if (s->b->flags & BF_READONLY) {
                put_status(s, "Buffer is read only");
                continue;
            }
        }

        for (;;) {
            unsigned char arg_type;
            int ret;

            ret = parse_arg(&r, &arg_type, prompt, countof(prompt),
                            NULL, 0, NULL, 0);
            if (ret < 0)
                goto badcmd;
            if (ret == 0)
                break;
            if (nb_args >= MAX_CMD_ARGS) {
            badcmd:
                put_status(s, "Badly defined command '%s'", cmd);
                goto fail;
            }
            args[nb_args].p = NULL;
            args_type[nb_args++] = arg_type & CMD_ARG_TYPE_MASK;
        }

        if (!expect_token(&p, '('))
            goto fail;

        sep = '\0';
        strp = str;

        for (i = 0; i < nb_args; i++) {
            /* pseudo arguments: skip them */
            switch (args_type[i]) {
            case CMD_ARG_WINDOW:
                args[i].s = s;
                continue;
            case CMD_ARG_INTVAL:
                args[i].n = (int)(intptr_t)d->val;
                continue;
            case CMD_ARG_STRINGVAL:
                /* CG: kludge for xxx-mode functions and named kbd macros */
                args[i].p = prompt;
                continue;
            }

            skip_spaces(&p);
            if (sep) {
                /* CG: Should test for arg list too short. */
                /* CG: Could supply default arguments. */
                if (!expect_token(&p, sep))
                    goto fail;
            }
            sep = ',';

            switch (args_type[i]) {
            case CMD_ARG_INT:
                r = p;
                args[i].n = strtol(p, (char**)&p, 0);
                if (p == r) {
                    put_status(s, "Number expected for arg %d", i);
                    goto fail;
                }
                break;
            case CMD_ARG_STRING:
                if (*p != '\"' && *p != '\'') {
                    /* XXX: should convert number to string */
                    put_status(s, "String expected for arg %d", i);
                    goto fail;
                }
                if (qe_cfg_parse_string(s, &p, strp,
                                        str + countof(str) - strp) < 0)
                {
                    goto fail;
                }
                args[i].p = strp;
                strp += strlen(strp) + 1;
                break;
            }
        }
        skip_spaces(&p);
        c = ')';
        if (*p != c) {
            put_status(s, "Too many arguments for %s", d->name);
            goto fail;
        }

        qs->this_cmd_func = d->action.func;
        qs->ec.function = d->name;
        call_func(d->sig, d->action, nb_args, args, args_type);
        qs->last_cmd_func = qs->this_cmd_func;
        if (qs->active_window)
            s = qs->active_window;
        continue;

    fail:
        ;
    }
    fclose(f);
    qs->ec = ec;

    return 0;
}

void do_load_config_file(EditState *e, const char *file)
{
    QEmacsState *qs = e->qe_state;
    FindFileState *ffst;
    char filename[MAX_FILENAME_SIZE];

    if (file && *file) {
        parse_config_file(e, file);
        return;
    }

    ffst = find_file_open(qs->res_path, "config");
    if (!ffst)
        return;
    while (find_file_next(ffst, filename, sizeof(filename)) == 0) {
        parse_config_file(e, filename);
    }
    find_file_close(ffst);
    if (file)
        do_refresh(e);
}

/* Load .qerc files in all parent directories of filename */
/* CG: should keep a cache of failed attempts */
void do_load_qerc(EditState *e, const char *filename)
{
    char buf[MAX_FILENAME_SIZE];
    char *p = buf;

    for (;;) {
        pstrcpy(buf, sizeof(buf), filename);
        p = strchr(p, '/');
        if (!p)
            break;
        p += 1;
        pstrcpy(p, buf + sizeof(buf) - p, ".qerc");
        parse_config_file(e, buf);
    }
}

/******************************************************/
/* command line option handling */
static CmdOptionDef *first_cmd_options;

void qe_register_cmd_line_options(CmdOptionDef *table)
{
    CmdOptionDef **pp, *p;

    /* link command line options table at end of list */
    pp = &first_cmd_options;
    while (*pp != NULL) {
        p = *pp;
        while (p->name != NULL)
            p++;
        pp = &p->u.next;
    }
    *pp = table;
}

/******************************************************/

const char str_version[] = "QEmacs version " QE_VERSION;
const char str_credits[] = "Copyright (c) 2000-2003 Fabrice Bellard\n"
                           "Copyright (c) 2000-2008 Charlie Gordon\n";

static void show_version(void)
{
    printf("%s\n%s\n"
           "QEmacs comes with ABSOLUTELY NO WARRANTY.\n"
           "You may redistribute copies of QEmacs\n"
           "under the terms of the GNU Lesser General Public License.\n",
           str_version, str_credits);
    exit(1);
}

static void show_usage(void)
{
    CmdOptionDef *p;
    int pos;

    printf("Usage: qe [OPTIONS] [filename ...]\n"
           "\n"
           "Options:\n"
           "\n");

    /* print all registered command line options */
    p = first_cmd_options;
    while (p != NULL) {
        while (p->name != NULL) {
            pos = printf("--%s", p->name);
            if (p->shortname)
                pos += printf(", -%s", p->shortname);
            if (p->flags & CMD_OPT_ARG)
                pos += printf(" %s", p->argname);
            if (pos < 24)
                printf("%*s", pos - 24, "");
            printf("%s\n", p->help);
            p++;
        }
        p = p->u.next;
    }
    printf("\n"
           "Report bugs to bug@qemacs.org.  First, please see the Bugs\n"
           "section of the QEmacs manual or the file BUGS.\n");
    exit(1);
}

int parse_command_line(int argc, char **argv)
{
    int _optind;

    _optind = 1;
    for (;;) {
        const char *r, *r1, *r2, *_optarg;
        CmdOptionDef *p;

        if (_optind >= argc)
            break;
        r = argv[_optind];
        /* stop before first non option */
        if (r[0] != '-')
            break;
        _optind++;

        r2 = r1 = r + 1;
        if (r2[0] == '-') {
            r2++;
            /* stop after `--' marker */
            if (r2[0] == '\0')
                break;
        }

        p = first_cmd_options;
        while (p != NULL) {
            while (p->name != NULL) {
                if (strequal(p->name, r2) ||
                    (p->shortname && strequal(p->shortname, r1))) {
                    if (p->flags & CMD_OPT_ARG) {
                        if (_optind >= argc) {
                            put_status(NULL,
                                       "cmdline argument expected -- %s", r);
                            goto next_cmd;
                        }
                        _optarg = argv[_optind++];
                    } else {
                        _optarg = NULL;
                    }
                    if (p->flags & CMD_OPT_BOOL) {
                        *p->u.int_ptr = 1;
                    } else if (p->flags & CMD_OPT_STRING) {
                        *p->u.string_ptr = _optarg;
                    } else if (p->flags & CMD_OPT_INT) {
                        *p->u.int_ptr = strtol(_optarg, NULL, 0);
                    } else if (p->flags & CMD_OPT_ARG) {
                        p->u.func_arg(_optarg);
                    } else {
                        p->u.func_noarg();
                    }
                    goto next_cmd;
                }
                p++;
            }
            p = p->u.next;
        }
        put_status(NULL, "unknown cmdline option '%s'", r);
    next_cmd: ;
    }
    return _optind;
}

void set_user_option(const char *user)
{
    QEmacsState *qs = &qe_state;
    char path[MAX_FILENAME_SIZE];
    const char *home_path;

    user_option = user;

    /* compute resources path */
    qs->res_path[0] = '\0';

    /* put source directory first if qe invoked as ./qe */
    // should use actual directory
    if (stristart(qs->argv[0], "./qe", NULL)) {
        pstrcat(qs->res_path, sizeof(qs->res_path), ".:");
    }

    /* put user directory before standard list */
    if (user) {
        /* use ~USER/.qe instead of ~/.qe */
        /* CG: should get user homedir */
        snprintf(path, sizeof(path), "/home/%s", user);
        home_path = path;
    } else {
        home_path = getenv("HOME");
    }
    if (home_path) {
        pstrcat(qs->res_path, sizeof(qs->res_path), home_path);
        pstrcat(qs->res_path, sizeof(qs->res_path), "/.qe:");
    }

    pstrcat(qs->res_path, sizeof(qs->res_path),
            CONFIG_QE_PREFIX "/share/qe:"
            CONFIG_QE_PREFIX "/lib/qe:"
            "/usr/share/qe:"
            "/usr/lib/qe");
}

void set_tty_charset(const char *name)
{
    qe_free(&qe_state.tty_charset);
    qe_state.tty_charset = qe_strdup(name);
}

static CmdOptionDef cmd_options[] = {
    { "help", "h", NULL, 0, "display this help message and exit",
      { .func_noarg = show_usage }},
    { "no-init-file", "q", NULL, CMD_OPT_BOOL, "do not load config files",
      { .int_ptr = &no_init_file }},
    { "ttycharset", "c", "CHARSET", CMD_OPT_ARG, "specify tty charset",
      { .func_arg = set_tty_charset }},
    { "user", "u", "USER", CMD_OPT_ARG, "load ~USER/.qe/config instead of your own",
      { .func_arg = set_user_option }},
    { "version", "V", NULL, 0, "display version information and exit",
      { .func_noarg = show_version }},
    { NULL, NULL, NULL, 0, NULL, { NULL }},
};

/* default key bindings */

#include "qeconfig.h"

#if (defined(__GNUC__) || defined(__TINYC__)) && defined(CONFIG_INIT_CALLS)

static void init_all_modules(void)
{
    int (*initcall)(void);
    int (**ptr)(void);

    ptr = (int (**)(void))(void *)&__initcall_first;
    for (;;) {
        /* NOTE: if bound checking is on, a '\0' is inserted between
           each initialized 'void *' */
#if defined(__BOUNDS_CHECKING_ON)
        ptr = (void **)((long)ptr + (2 * sizeof(void *)));
#else
        ptr++;
#endif
        initcall = *ptr;
        if (initcall == NULL)
            break;
        (*initcall)();
    }
}

#if 0
static void exit_all_modules(void)
{
    /* CG: Should call in reverse order! */
    int (*exitcall)(void);
    int (**ptr)(void);

    ptr = (int (**)(void))(void *)&__exitcall_first;
    for (;;) {
#if defined(__BOUNDS_CHECKING_ON)
        ptr = (void **)((long)ptr + (2 * sizeof(void *)));
#else
        ptr++;
#endif
        exitcall = *ptr;
        if (exitcall == NULL)
            break;
        (*exitcall)();
    }
}
#endif

#else

#ifdef CONFIG_TINY
#define MODULE_LIST  "basemodules.txt"
#else
#define MODULE_LIST  "allmodules.txt"
#endif

#undef qe_module_init
#define qe_module_init(fn)  extern int module_ ## fn(void)
#include MODULE_LIST
#undef qe_module_init

static void init_all_modules(void)
{
#define qe_module_init(fn)  module_ ## fn()
#include MODULE_LIST
#undef qe_module_init
}
#endif

#ifdef CONFIG_DLL

static void load_all_modules(QEmacsState *qs)
{
    QErrorContext ec;
    FindFileState *ffst;
    char filename[MAX_FILENAME_SIZE];
    void *h;
    void *sym;
    int (*init_func)(void);

    ec = qs->ec;
    qs->ec.function = "load-all-modules";

    ffst = find_file_open(qs->res_path, "*.so");
    if (!ffst)
        goto done;

    while (!find_file_next(ffst, filename, sizeof(filename))) {
        h = dlopen(filename, RTLD_LAZY);
        if (!h) {
            char *error = dlerror();
            put_status(NULL, "Could not open module '%s': %s",
                       filename, error);
            continue;
        }
#if 0
        /* Writing: init_func = (int (*)(void))dlsym(handle, "xxx");
         * would seem more natural, but the C99 standard leaves
         * casting from "void *" to a function pointer undefined.
         * The assignment used below is the POSIX.1-2003 (Technical
         * Corrigendum 1) workaround; see the Rationale for the
         * POSIX specification of dlsym().
         */
        *(void **)(&init_func) = dlsym(h, "__qe_module_init");
        //init_func = (int (*)(void))dlsym(h, "__qe_module_init");
#else
        sym = dlsym(h, "__qe_module_init");
        memcpy(&init_func, &sym, sizeof(sym));
#endif
        if (!init_func) {
            dlclose(h);
            put_status(NULL,
                       "Could not find qemacs initializer in module '%s'",
                       filename);
            continue;
        }

        /* all is OK: we can init the module now */
        (*init_func)();
    }
    find_file_close(ffst);

  done:
    qs->ec = ec;
}

#endif

typedef struct QEArgs {
    int argc;
    char **argv;
} QEArgs;

/* init function */
static void qe_init(void *opaque)
{
    QEmacsState *qs = &qe_state;
    QEArgs *args = opaque;
    int argc = args->argc;
    char **argv = args->argv;
    EditState *s;
    EditBuffer *b;
    QEDisplay *dpy;
    int i, _optind, is_player;

    qs->ec.function = "qe-init";
    qs->macro_key_index = -1; /* no macro executing */
    qs->ungot_key = -1; /* no unget key */

    qs->argc = argc;
    qs->argv = argv;

    qs->hilite_region = 1;
    qs->mmap_threshold = MIN_MMAP_SIZE;
    qs->max_load_size = MAX_LOAD_SIZE;

    /* setup resource path */
    set_user_option(NULL);

    eb_init();
    charset_init();
    init_input_methods();
#ifdef CONFIG_ALL_KMAPS
    load_input_methods();
#endif
#ifdef CONFIG_UNICODE_JOIN
    load_ligatures();
#endif

    /* init basic modules */
    qe_register_mode(&text_mode);
    qe_register_cmd_table(basic_commands, NULL);
    qe_register_cmd_line_options(cmd_options);

    register_completion("command", command_completion);
    register_completion("charset", charset_completion);
    register_completion("mode", mode_completion);
    register_completion("style", style_completion);
    register_completion("style-property", style_property_completion);
    register_completion("file", file_completion);
    register_completion("buffer", buffer_completion);
    register_completion("color", color_completion);

    minibuffer_init();
    less_mode_init();

    /* init all external modules in link order */
    init_all_modules();

#ifdef CONFIG_DLL
    /* load all dynamic modules */
    load_all_modules(qs);
#endif

#if 0
    /* see if invoked as player */
    {
        const char *p;

        p = get_basename(argv[0]);
        if (strequal(p, "ffplay"))
            is_player = 1;
        else
            is_player = 0;
    }
#else
    /* Start in dired mode when invoked with no arguments */
    is_player = 1;
#endif

    /* init of the editor state */
    qs->screen = &global_screen;

    /* create first buffer */
    b = eb_new("*scratch*", BF_SAVELOG);

    /* will be positionned by do_refresh() */
    s = edit_new(b, 0, 0, 0, 0, WF_MODELINE);

    /* at this stage, no screen is defined. Initialize a
     * null display driver to have a consistent state
     * else many commands such as put_status would crash.
     */
    dpy_init(&global_screen, NULL, screen_width, screen_height);

    /* handle options */
    _optind = parse_command_line(argc, argv);

    /* load config file unless command line option given */
    if (!no_init_file)
        do_load_config_file(s, NULL);

    qe_key_init(&key_ctx);

    /* select the suitable display manager */
    for (;;) {
        dpy = probe_display();
        if (!dpy) {
            fprintf(stderr, "No suitable display found, exiting\n");
            exit(1);
        }
        if (dpy_init(&global_screen, dpy, screen_width, screen_height) < 0) {
            /* Just disable the display and try another */
            //fprintf(stderr, "Could not initialize display '%s', exiting\n",
            //        dpy->name);
            dpy->dpy_probe = NULL;
        } else {
            break;
        }
    }

    put_status(NULL, "%s display %dx%d",
               dpy->name, qs->screen->width, qs->screen->height);

    qe_event_init();

    do_refresh(s);

    /* load file(s) */
    for (i = _optind; i < argc; i++) {
        do_find_file(s, argv[i]);
        /* CG: handle +linenumber */
    }

#if !defined(CONFIG_TINY) && !defined(CONFIG_WIN32)
    if (is_player && _optind >= argc) {
        /* if player, go to directory mode by default if no file selected */
        do_dired(s);
    }
#endif

    put_status(s, "QEmacs %s - Press F1 for help", QE_VERSION);

    edit_display(qs);
    dpy_flush(&global_screen);

    b = eb_find("*errors*");
    if (b != NULL) {
        show_popup(b);
        edit_display(qs);
        dpy_flush(&global_screen);
    }
    qs->ec.function = NULL;
}


#ifdef CONFIG_WIN32
int main1(int argc, char **argv)
#else
int main(int argc, char **argv)
#endif
{
    QEArgs args;

    args.argc = argc;
    args.argv = argv;

    url_main_loop(qe_init, &args);

#ifdef CONFIG_ALL_KMAPS
    unload_input_methods();
#endif

    dpy_close(&global_screen);
    return 0;
}
