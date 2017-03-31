/*
 * QEmacs, tiny but powerful multimode editor
 *
 * Copyright (c) 2000-2002 Fabrice Bellard.
 * Copyright (c) 2000-2017 Charlie Gordon.
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

int debug_flags;

#ifdef CONFIG_INIT_CALLS
static int (*qe__initcall_first)(void) qe__init_call = NULL;
static void (*qe__exitcall_first)(void) qe__exit_call = NULL;
#endif

void print_at_byte(QEditScreen *screen,
                   int x, int y, int width, int height,
                   const char *str, int style_index);
static EditBuffer *predict_switch_to_buffer(EditState *s);
static StringArray *get_history(const char *name);
static void qe_key_process(int key);

static int generic_save_window_data(EditState *s);
static int generic_mode_init(EditState *s);
static void generic_mode_close(EditState *s);
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
int force_tty;
int single_window;
int use_session_file;
#ifndef CONFIG_TINY
static int free_everything;
#endif
static const char *user_option;

/* mode handling */

static int default_mode_init(EditState *s, EditBuffer *b, int flags) { return 0; }

static int generic_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (match_extension(p->filename, mode->extensions)
    ||  match_shell_handler(cs8(p->buf), mode->shell_handlers)) {
        return 80;
    }
    return 1;
}

ModeDef *qe_find_mode(const char *name, int flags)
{
    QEmacsState *qs = &qe_state;
    ModeDef *m;

    for (m = qs->first_mode; m; m = m->next) {
        if ((m->flags & flags) == flags) {
            if ((m->name && !strcasecmp(m->name, name))
            ||  (m->mode_name && !strcasecmp(m->mode_name, name)))
                break;
        }
    }
    return m;
}

ModeDef *qe_find_mode_filename(const char *filename, int flags)
{
    QEmacsState *qs = &qe_state;
    ModeDef *m;

    for (m = qs->first_mode; m; m = m->next) {
        if ((m->flags & flags) == flags
        &&  match_extension(filename, m->extensions)) {
            break;
        }
    }
    return m;
}

void qe_register_mode(ModeDef *m, int flags)
{
    QEmacsState *qs = &qe_state;
    ModeDef **p;

    m->flags |= flags;

    if (!m->mode_name)
        m->mode_name = m->name;

    /* register mode in mode list (at end) */
    for (p = &qs->first_mode;; p = &(*p)->next) {
        if (*p == m)
            return;
        if (*p == NULL) {
            m->next = NULL;
            *p = m;
            break;
        }
    }

    if (m->flags & MODEF_SYNTAX) {
        /* if no syntax probing function, use extension matcher */
        if (!m->mode_probe && m->extensions)
            m->mode_probe = generic_mode_probe;

        /* default to text handling */
        if (!m->display_line)
            m->display_line = text_display_line;
        if (!m->backward_offset)
            m->backward_offset = text_backward_offset;
        if (!m->move_up_down)
            m->move_up_down = text_move_up_down;
        if (!m->move_left_right)
            m->move_left_right = text_move_left_right_visual;
        if (!m->move_bol)
            m->move_bol = text_move_bol;
        if (!m->move_eol)
            m->move_eol = text_move_eol;
        if (!m->move_bof)
            m->move_bof = text_move_bof;
        if (!m->move_eof)
            m->move_eof = text_move_eof;
        if (!m->move_word_left_right)
            m->move_word_left_right = text_move_word_left_right;
        if (!m->scroll_up_down)
            m->scroll_up_down = text_scroll_up_down;
        if (!m->mouse_goto)
            m->mouse_goto = text_mouse_goto;
        if (!m->write_char)
            m->write_char = text_write_char;
    }

    /* add missing functions */
    if (!m->mode_init)
        m->mode_init = default_mode_init;
    if (!m->display)
        m->display = generic_text_display;
    if (!m->data_type)
        m->data_type = &raw_data_type;
    if (!m->get_mode_line)
        m->get_mode_line = text_mode_line;

    /* add a new command to switch to that mode */
    if (!(m->flags & MODEF_NOCMD)) {
        char buf[64];
        int size;
        CmdDef *def;

        /* lower case convert for C mode, Perl... */
        qe_strtolower(buf, sizeof(buf) - 10, m->mode_name);
        pstrcat(buf, sizeof(buf), "-mode");
        size = strlen(buf) + 1;
        /* constant immediate string parameter */
        size += snprintf(buf + size, sizeof(buf) - size,
                         "S{%s}", m->mode_name) + 1;
        def = qe_mallocz_array(CmdDef, 2);
        def->name = qe_malloc_dup(buf, size);
        def->key = def->alt_key = KEY_NONE;
        def->sig = CMD_ESs;
        def->val = 0;
        def->action.ESs = do_set_mode;
        def[1].val = 1;  /* flag as allocated for free-all */
        qe_register_cmd_table(def, NULL);
    }
}

void mode_completion(CompleteState *cp)
{
    QEmacsState *qs = cp->s->qe_state;
    ModeDef *m;

    for (m = qs->first_mode; m != NULL; m = m->next) {
        complete_test(cp, m->mode_name);
    }
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
    } else
    if (key >= KEY_CTRLC(0) && key <= KEY_CTRLC(0xff)) {
        keys[nb_keys++] = KEY_CTRL('c');
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
        if (d->key == KEY_CTRL('x') || d->key == KEY_CTRL('c')
        ||  d->key == KEY_ESC) {
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

int qe_mode_set_key(ModeDef *m, const char *keystr, const char *cmd_name)
{
    unsigned int keys[MAX_KEYS];
    int nb_keys;

    nb_keys = strtokeys(keystr, keys, MAX_KEYS);
    if (!nb_keys)
        return -2;

    return qe_register_binding1(keys, nb_keys, qe_find_cmd(cmd_name), m);
}

void do_set_key(EditState *s, const char *keystr,
                const char *cmd_name, int local)
{
    int res = qe_mode_set_key(local ? s->mode : NULL, keystr, cmd_name);

    if (res == -2)
        put_status(s, "Invalid keys: %s", keystr);
    if (res == -1)
        put_status(s, "Invalid command: %s", cmd_name);
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
                    kd->keys[i] = set ? KEY_META('h') : KEY_DEL;
                    break;
                case KEY_DEL:
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
        qs->emulation_flags = 1;
        qs->flag_split_window_change_focus = 1;
    } else
    if (strequal(name, "emacs") || strequal(name, "xemacs")) {
        qs->emulation_flags = 0;
        qs->flag_split_window_change_focus = 0;
    } else
    if (strequal(name, "vi") || strequal(name, "vim")) {
        put_status(s, "Emulation '%s' not available yet", name);
    } else {
        put_status(s, "Unknown emulation '%s'", name);
    }
}

void do_set_trace(EditState *s)
{
    do_split_window(s, 0);
    do_switch_to_buffer(s, "*trace*");
    do_previous_window(s);
}

void do_cd(EditState *s, const char *path)
{
    char buf[MAX_FILENAME_SIZE];

    canonicalize_absolute_path(s, buf, sizeof(buf), path);

    if (chdir(buf)) {
        put_status(s, "Cannot change directory to '%s'", buf);
    } else {
        getcwd(buf, sizeof(buf));
        put_status(s, "Current directory: %s", buf);
    }
}

/* basic editing functions */

void do_bof(EditState *s)
{
    if (s->mode->move_bof)
        s->mode->move_bof(s);
}

void do_eof(EditState *s)
{
    if (s->mode->move_eof)
        s->mode->move_eof(s);
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

void text_move_bof(EditState *s)
{
    s->offset = 0;
}

void text_move_eof(EditState *s)
{
    s->offset = s->b->total_size;
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

int qe_get_word(EditState *s, char *buf, int buf_size,
                int offset, int *offset_ptr)
{
    EditBuffer *b = s->b;
    buf_t outbuf, *out;
    int offset1;
    int c;

    out = buf_init(&outbuf, buf, buf_size);

    /* XXX: the qe_isword pattern should depend on the current mode */
    if (qe_isword(eb_nextc(b, offset, &offset1))) {
        while (qe_isword(eb_prevc(b, offset, &offset1))) {
            offset = offset1;
        }
    } else {
        while ((offset = offset1) < b->total_size) {
            if (!qe_isword(eb_nextc(b, offset, &offset1)))
                break;
        }
    }
    while (offset < b->total_size) {
        if (!qe_isword(c = eb_nextc(b, offset, &offset1)))
            break;
        buf_putc_utf8(out, c);
        offset = offset1;
    }
    if (offset_ptr) {
        *offset_ptr = offset;
    }
    return out->len;
}

void do_mark_region(EditState *s, int mark, int offset)
{
    /* CG: Should have local and global mark rings */
    s->b->mark = clamp(mark, 0, s->b->total_size);
    s->offset = clamp(offset, 0, s->b->total_size);
    /* activate region hilite */
    if (s->qe_state->hilite_region)
        s->region_style = QE_STYLE_REGION_HILITE;
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

int eb_start_paragraph(EditBuffer *b, int offset)
{
    for (;;) {
        offset = eb_goto_bol(b, offset);
        if (offset <= 0)
            break;
        /* check if only spaces */
        if (eb_is_blank_line(b, offset, &offset)) {
            break;
        }
        offset = eb_prev(b, offset);
    }
    return offset;
}

void do_mark_paragraph(EditState *s)
{
    int start = eb_start_paragraph(s->b, s->offset);
    int end = eb_next_paragraph(s->b, s->offset);

    do_mark_region(s, start, end);
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
        if (!eb_is_blank_line(s->b, offset, NULL))
            break;
        /* line just before */
        offset = eb_prev(s->b, offset);
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

    if (dir < 0)
        do_backward_paragraph(s);
    else
        do_forward_paragraph(s);

    do_kill(s, start, s->offset, dir, 0);
}

void do_fill_paragraph(EditState *s)
{
    /* buffer offsets, byte counts */
    int par_start, par_end, offset, offset1, chunk_start, word_start;
    /* number of characters */
    int col, indent_size, word_size, space_size;
    /* other counts */
    int n, word_count;
    /* character */
    int c;

    /* find start & end of paragraph */
    par_start = eb_start_paragraph(s->b, s->offset);
    par_end = eb_next_paragraph(s->b, par_start);

    /* compute indent size */
    indent_size = 0;
    offset = eb_next_line(s->b, par_start);
    if (!eb_is_blank_line(s->b, offset, NULL)) {
        while (offset < par_end) {
            c = eb_nextc(s->b, offset, &offset);
            if (!qe_isblank(c))
                break;
            indent_size++;
        }
    }

    /* suppress any spaces in between */
    col = 0;
    offset = par_start;
    word_count = 0;
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
            /* insert space single space the word */
            if (offset == par_end
            ||  (col + 1 + word_size > s->b->fill_column)) {
                eb_delete_uchar(s->b, chunk_start);
                chunk_start += eb_insert_uchar(s->b, chunk_start, '\n');
                if (offset < par_end) {
                    /* indent */
                    int nb = eb_insert_spaces(s->b, offset, indent_size);
                    chunk_start += nb;
                    word_start += nb;
                    offset += nb;
                    par_end += nb;
                }
                col = word_size + indent_size;
            } else {
                eb_delete_uchar(s->b, chunk_start);
                chunk_start += eb_insert_uchar(s->b, chunk_start, ' ');
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
        len = eb_encode_uchar(b, buf, ch1);
        /* replaced char may have a different encoding len from
         * original char, such as dotless i in Turkish. */
        eb_replace(b, offset0, *offsetp - offset0, buf, len);
        *offsetp = offset0 + len;
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

    /* deactivate region hilite */
    s->region_style = 0;

    /* WARNING: during case change, the region offsets can change, so
       it is not so simple ! */
    /* XXX: if last char of region changes width, offset will move */
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
    int endpos;

    if (s->b->flags & BF_READONLY)
        return;

    /* Delete hilighted region, if any.
     * do_append_next_kill silently ignored.
     */
    if (do_delete_selection(s))
        return;

    if (argval == NO_ARG) {
        if (s->qe_state->last_cmd_func != (CmdFunc)do_append_next_kill) {
            eb_delete_uchar(s->b, s->offset);
            return;
        }
        argval = 1;
    }

    /* save kill if numeric argument given */
    endpos = eb_skip_chars(s->b, s->offset, argval);
    do_kill(s, s->offset, endpos, argval, 0);
}

void do_backspace(EditState *s, int argval)
{
    int offset1;

#ifndef CONFIG_TINY
    if (s->b->flags & BF_PREVIEW) {
        do_scroll_up_down(s, -2);
        return;
    }
#endif

    if (s->b->flags & BF_READONLY) {
        /* CG: could scroll down */
        return;
    }

    /* Delete hilighted region, if any.
     * do_append_next_kill silently ignored.
     */
    if (do_delete_selection(s))
        return;

    if (argval == NO_ARG) {
        if (s->qe_state->last_cmd_func == (CmdFunc)do_tab
        &&  !s->indent_tabs_mode) {
            /* Delete tab or indentation? */
            do_undo(s);
            return;
        }
        if (s->qe_state->last_cmd_func != (CmdFunc)do_append_next_kill) {
            offset1 = eb_prev(s->b, s->offset);
            if (offset1 < s->offset) {
                eb_delete_range(s->b, offset1, s->offset);
                /* special case for composing */
                if (s->compose_len > 0)
                    s->compose_len--;
            }
            return;
        }
        argval = 1;
    }
    /* save kill if numeric argument given */
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
                int x, int y, int w, int h, qe__unused__ int hex_mode)
{
    CursorContext *m = ds->cursor_opaque;

    if (m->offsetc >= offset1 && m->offsetc < offset2) {
        if (w < 0) {  /* for RTL glyphs */
            x += w;
            w = -w;
        }
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

    memset(m, 0, sizeof(*m));
    m->offsetc = s->offset;
    m->xc = m->yc = NO_CURSOR;
    display_init(ds, s, DISP_CURSOR, cursor_func, m);
    display1(ds);
    display_close(ds);
}

typedef struct {
    int yd;
    int xd;
    int xdmin;
    int offsetd;
} MoveContext;

/* called each time the cursor could be displayed */
static int down_cursor_func(DisplayState *ds,
                            int offset1, qe__unused__ int offset2, int line_num,
                            int x, qe__unused__ int y,
                            qe__unused__ int w, qe__unused__ int h,
                            qe__unused__ int hex_mode)
{
    int d;
    MoveContext *m = ds->cursor_opaque;

    if (line_num == m->yd) {
        if (offset1 >= 0) {
            if (w < 0) {  /* for RTL glyphs */
                x += w;
                w = -w;
            }
            /* find the closest char */
            d = abs(x - m->xd);
            if (d < m->xdmin) {
                m->xdmin = d;
                m->offsetd = offset1;
            }
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
#ifndef CONFIG_TINY
    if (s->b->flags & BF_PREVIEW) {
        if (s->mode->scroll_up_down
        &&  (dir > 0 || s->offset_top > 0)
        &&  eb_at_bol(s->b, s->offset)) {
            s->mode->scroll_up_down(s, dir);
            return;
        }
    }
#endif
    if (s->mode->move_up_down)
        s->mode->move_up_down(s, dir);
}

void do_left_right(EditState *s, int dir)
{
#ifndef CONFIG_TINY
    if (s->b->flags & BF_PREVIEW) {
        EditState *e = find_window(s, KEY_LEFT, NULL);
        if (e && (e->flags & WF_FILELIST)
        &&  dir < 0 && eb_at_bol(s->b, s->offset)) {
            s->qe_state->active_window = e;
            return;
        }
    }
#endif
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
            int offset_top = s->offset_top;

            if (offset_top <= 0)
                return;

            offset_top = eb_prev(s->b, offset_top);
            s->offset_top = s->mode->backward_offset(s, offset_top);

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
    display_init(ds, s, DISP_CURSOR, down_cursor_func, m);
    display1(ds);
    display_close(ds);
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
                              qe__unused__ int line_num,
                              qe__unused__ int x, int y,
                              qe__unused__ int w, int h,
                              qe__unused__ int hex_mode)
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
    /* XXX: should also track horizontal position? */
    if (m->offsetc >= offset1 && m->offsetc < offset2 &&
        y >= 0 && y1 <= ds->height) {
        m->offset_found = m->offsetc;
        m->y_found = 0x7fffffff * m->dir; /* ensure that no other
                                             position will be found */
        return -1;
    }
    return 0;
}

void do_scroll_left_right(EditState *s, int dir)
{
    DisplayState ds1, *ds = &ds1;
    int adjust;

    /* compute space_width */
    display_init(ds, s, DISP_NONE, NULL, NULL);
    adjust = dir * ds->space_width;
    display_close(ds);

    if (dir > 0) {
        if (s->wrap == WRAP_TRUNCATE) {
            if (s->x_disp[0] == 0) {
                s->wrap = WRAP_LINE;
            } else {
                s->x_disp[0] = min(s->x_disp[0] + adjust, 0);
            }
        } else
        if (s->wrap == WRAP_LINE) {
            s->wrap = WRAP_WORD;
        }
    } else {
        if (s->wrap == WRAP_WORD) {
            s->wrap = WRAP_LINE;
        } else
        if (s->wrap == WRAP_LINE) {
            s->wrap = WRAP_TRUNCATE;
        } else {
            s->x_disp[0] = min(s->x_disp[0] + adjust, 0);
        }
    }
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
        display_init(ds, s, DISP_CURSOR_SCREEN, NULL, NULL);
        while (s->y_disp > 0) {
            if (s->offset_top <= 0) {
                /* cannot go back: we stay at the top of the screen and
                   exit loop */
                s->y_disp = 0;
            } else {
                int offset = eb_prev(s->b, s->offset_top);
                s->offset_top = s->mode->backward_offset(s, offset);
                ds->y = 0;
                s->mode->display_line(s, ds, s->offset_top);
                s->y_disp -= ds->y;
            }
        }
        display_close(ds);
    }

    /* now update cursor position so that it is on screen */
    m->offsetc = s->offset;
    m->dir = -dir;
    m->y_found = 0x7fffffff * dir;
    m->offset_found = s->offset; /* default offset */
    display_init(ds, s, DISP_CURSOR_SCREEN, scroll_cursor_func, m);
    display1(ds);
    display_close(ds);

    s->offset = m->offset_found;
}

void text_scroll_up_down(EditState *s, int dir)
{
    int h, line_height;

    /* try to round to a line height */
    line_height = get_line_height(s->screen, s, QE_STYLE_DEFAULT);
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
void do_center_cursor(EditState *s, int force)
{
    CursorContext cm;

    /* only apply to text modes */
    if (!s->mode->display_line)
        return;

    if (s->offset < s->offset_top
    ||  (s->offset_bottom >= 0 && s->offset >= s->offset_bottom)) {
        /* if point is outside the current window, first move the
         * window to start at the line with point.  This significantly
         * speeds up get_cursor_pos() on large files, except for the
         * pathological case of huge lines.
         */
        int offset = eb_prev(s->b, s->offset);
        s->offset_top = s->mode->backward_offset(s, offset);
    } else {
        if (!force)
            return;
    }

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
                                  int offset1, qe__unused__ int offset2,
                                  int line_num,
                                  int x, qe__unused__ int y,
                                  qe__unused__ int w, qe__unused__ int h,
                                  qe__unused__ int hex_mode)
{
    int d;
    LeftRightMoveContext *m = ds->cursor_opaque;

    if (w < 0) {  /* for RTL glyphs */
        x += w;
        w = -w;
    }
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
        display_init(ds, s, DISP_CURSOR, left_right_cursor_func, m);
        display1(ds);
        display_close(ds);
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
                    int offset = s->offset_top;

                    if (offset <= 0)
                        break;
                    offset = eb_prev(s->b, offset);
                    s->offset_top = s->mode->backward_offset(s, offset);
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
                           int offset1, qe__unused__ int offset2,
                           qe__unused__ int line_num,
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

    display_init(ds, s, DISP_CURSOR_SCREEN, mouse_goto_func, m);
    ds->hex_mode = -1; /* we select both hex chars and normal chars */
    display1(ds);
    display_close(ds);

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

int do_delete_selection(EditState *s)
{
    int res = 0;

    if (s->region_style && s->b->mark != s->offset) {
        /* Delete hilighted region */
        // XXX: make it optional?
        res = eb_delete_range(s->b, s->b->mark, s->offset);
    }
    /* deactivate region hilite */
    s->region_style = 0;

    return res;
}

void do_char(EditState *s, int key, int argval)
{
#ifndef CONFIG_TINY
    if (s->b->flags & BF_PREVIEW) {
        if (key == KEY_SPC) {
            do_scroll_up_down(s, 2);
            return;
        }
        do_preview_mode(s, 0);
        return;
    }
#endif

    if (s->b->flags & BF_READONLY)
        return;

    /* Delete hilighted region */
    do_delete_selection(s);

    for (;;) {
        if (s->mode->write_char)
            s->mode->write_char(s, key);
        if (argval-- <= 1)
            break;
    }
}

#ifdef CONFIG_UNICODE_JOIN
void do_combine_char(EditState *s, int accent)
{
    int offset0, len, c;
    unsigned int g[2];
    char buf[MAX_CHAR_BYTES];

    if (s->b->flags & BF_READONLY)
        return;

    c = eb_prevc(s->b, s->offset, &offset0);
    if (c == accent) {
        eb_delete(s->b, offset0, s->offset - offset0);
    } else
    if (((expand_ligature(g, c) && g[1] == (unsigned int)accent)
    ||   (c != '\n' && combine_accent(g, c, accent)))
    &&  (len = eb_encode_uchar(s->b, buf, g[0])) > 0) {
        /* XXX: should bypass eb_encode_uchar to detect encoding failure */
        eb_replace(s->b, offset0, s->offset - offset0, buf, len);
        s->offset = offset0 + len;
    } else {
        do_char(s, accent, 1);
    }
}
#endif

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
    len = eb_encode_uchar(s->b, buf, key);
    insert = (s->insert || cur_ch == '\n');

    if (insert) {
        const InputMethod *m;
        int match_buf[20], match_len, offset, i;

        /* use compose system only if insert mode */
        if (s->compose_len == 0)
            s->compose_start_offset = s->offset;

        /* break sequence of insertions */
        if (key == '\n' || (key != ' ' && s->b->last_log_char == ' ')) {
            s->b->last_log = LOGOP_FREE;
        }
        s->b->last_log_char = key;

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
                offset = eb_skip_chars(s->b, s->compose_start_offset, match_len);
                eb_delete_range(s->b, s->compose_start_offset, offset);
                s->compose_len -= match_len;
                umemmove(s->compose_buf, s->compose_buf + match_len,
                         s->compose_len);
                /* then insert match */
                for (i = 0; i < ret; i++) {
                    key = match_buf[i];
                    len = eb_encode_uchar(s->b, buf, key);
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
    if (!KEY_IS_SPECIAL(key)) {
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

void do_overwrite_mode(EditState *s, int argval)
{
    if (argval == NO_ARG)
        s->insert = !s->insert;
    else
        s->insert = !argval;
}

void do_tab(EditState *s, int argval)
{
    /* CG: should do smart complete, smart indent, insert tab */
    if (s->indent_tabs_mode) {
        do_char(s, 9, argval);
    } else {
        int offset = s->offset;
        int offset0 = eb_goto_bol(s->b, offset);
        int col = 0;
        int tw = s->b->tab_width > 0 ? s->b->tab_width : 8;
        int indent = s->indent_size > 0 ? s->indent_size : tw;

        while (offset0 < offset) {
            int c = eb_nextc(s->b, offset0, &offset0);
            if (c == '\t') {
                col += tw - col % tw;
            } else {
                col += unicode_tty_glyph_width(c);
            }
        }
        if (argval == NO_ARG)
            argval = 1;

        s->offset += eb_insert_spaces(s->b, s->offset,
                                      indent * argval - (col % indent));
    }
}

#ifndef CONFIG_TINY
void do_preview_mode(EditState *s, int set)
{
    const char *state = NULL;

    if (set < 0 && (s->b->flags & BF_PREVIEW)) {
        s->b->flags &= ~BF_PREVIEW;
        state = "exited";
    } else
    if (set > 0 && !(s->b->flags & BF_PREVIEW)) {
        s->b->flags |= BF_PREVIEW;
        state = "started";
    } else
    if (set == 0) {
        state = (s->b->flags & BF_PREVIEW) ? "active" : "inactive";
    }
    if (state)
        put_status(s, "Preview mode %s", state);
}
#endif

void do_return(EditState *s, int move)
{
    int len;

#ifndef CONFIG_TINY
    if (s->b->flags & BF_PREVIEW) {
        do_preview_mode(s, -1);
        return;
    }
#endif
    if (s->b->flags & BF_READONLY)
        return;

    /* CG: in overwrite mode, should just go to beginning of next line */
    len = eb_insert_uchar(s->b, s->offset, '\n');
    if (move)
        s->offset += len;
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
#ifndef CONFIG_TINY
    if (s->b->flags & BF_PREVIEW) {
        do_preview_mode(s, -1);
        return;
    }
#endif
    /* deactivate region hilite */
    s->region_style = 0;
    /* deactivate search hilite */
    s->isearch_state = NULL;

    /* well, currently nothing needs to be aborted in global context */
    /* CG: Should remove popups, sidepanes, helppanes... */
    put_status(s, "Quit");
}

/* block functions */
void do_set_mark(EditState *s)
{
    do_mark_region(s, s->offset, s->offset);
    put_status(s, "Mark set");
}

void do_mark_whole_buffer(EditState *s)
{
    do_mark_region(s, s->b->total_size, 0);
}

EditBuffer *new_yank_buffer(QEmacsState *qs, EditBuffer *base)
{
    char bufname[32];
    EditBuffer *b;
    int cur = qs->yank_current;

    if (qs->yank_buffers[cur]) {
        cur = (cur + 1) % NB_YANK_BUFFERS;
        qs->yank_current = cur;
        /* Maybe should instead just clear the buffer and reset styles */
        qe_kill_buffer(qs->yank_buffers[cur]);
        qs->yank_buffers[cur] = NULL;
    }
    snprintf(bufname, sizeof(bufname), "*kill-%d*", cur + 1);
    if (base) {
        b = eb_new(bufname, BF_SYSTEM | (base->flags & BF_STYLES));
        eb_set_charset(b, base->charset, base->eol_type);
    } else {
        b = eb_new(bufname, 0);
    }
    qs->yank_buffers[cur] = b;
    return b;
}

void do_append_next_kill(qe__unused__ EditState *s)
{
    /* do nothing! */
}

void do_kill(EditState *s, int p1, int p2, int dir, int keep)
{
    QEmacsState *qs = s->qe_state;
    int len, tmp;
    EditBuffer *b;

    /* deactivate region hilite */
    s->region_style = 0;

    if (p1 > p2) {
        tmp = p1;
        p1 = p2;
        p2 = tmp;
    }
    len = p2 - p1;
    b = qs->yank_buffers[qs->yank_current];
    if (!b || !dir || qs->last_cmd_func != (CmdFunc)do_append_next_kill) {
        /* append kill if last command was kill already */
        b = new_yank_buffer(qs, s->b);
    }
    /* insert at beginning or end depending on kill direction */
    eb_insert_buffer_convert(b, dir < 0 ? 0 : b->total_size, s->b, p1, len);
    if (keep) {
        /* no message */
    } else
    if (!(s->b->flags & BF_READONLY)) {
        if (s->mode->delete_bytes) {
            s->mode->delete_bytes(s, p1, len);
        } else {
            eb_delete(s->b, p1, len);
        }
        s->offset = p1;
    } else {
        put_status(s, "Region copied");
    }
    if (dir) {
        qs->this_cmd_func = (CmdFunc)do_append_next_kill;
    }
    selection_activate(qs->screen);
}

void do_kill_region(EditState *s, int keep)
{
    do_kill(s, s->b->mark, s->offset, 0, keep);
}

void do_kill_line(EditState *s, int argval)
{
    int p1, p2, offset1, dir = 1;

    p1 = s->offset;
    if (argval == NO_ARG) {
        if (s->region_style && s->b->mark != s->offset) {
            /* kill highlighted region */
            p1 = s->b->mark;
            p2 = s->offset;
        } else
        if (eb_nextc(s->b, p1, &offset1) == '\n') {
            /* kill end of line marker */
            p2 = s->offset = offset1;
        } else {
            /* kill to end of line */
            do_eol(s);
            p2 = s->offset;
        }
    } else
    if (argval <= 0) {
        /* kill backwards */
        dir = -1;
        for (;;) {
            do_bol(s);
            p2 = s->offset;
            if (p2 <= 0 || argval == 0)
                break;
            p2 = eb_prev(s->b, p2);
            s->offset = p2;
            argval += 1;
        }
    } else {
        for (;;) {
            do_eol(s);
            p2 = s->offset;
            if (p2 >= s->b->total_size || argval == 0)
                break;
            p2 = eb_next(s->b, p2);
            s->offset = p2;
            argval -= 1;
        }
    }
    do_kill(s, p1, p2, dir, 0);
}

void do_kill_beginning_of_line(EditState *s, int argval)
{
    do_kill_line(s, argval == NO_ARG ? 0 : -argval);
}

void do_kill_word(EditState *s, int dir)
{
    int start = s->offset;

    do_word_right(s, dir);
    do_kill(s, start, s->offset, dir, 0);
}

void do_yank(EditState *s)
{
    int size;
    QEmacsState *qs = s->qe_state;
    EditBuffer *b;

    if (s->b->flags & BF_READONLY)
        return;

    /* First delete any highlighted range */
    do_delete_selection(s);

    /* if the GUI selection is used, it will be handled in the GUI code */
    selection_request(qs->screen);

    s->b->mark = s->offset;
    b = qs->yank_buffers[qs->yank_current];
    if (b) {
        size = b->total_size;
        if (size > 0) {
            s->b->last_log = LOGOP_FREE;
            s->offset += eb_insert_buffer_convert(s->b, s->offset, b, 0, size);
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

static int reload_buffer(EditState *s, EditBuffer *b)
{
    FILE *f, *f1 = NULL;
    int ret, saved;

    /* if no file associated, cannot do anything */
    if (b->filename[0] == '\0')
        return 0;

    if (!f1 && b->data_type == &raw_data_type) {
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
    /* XXX: the log buffer is inappropriate if the file was modified on
     * disk. If this is a reload operation, should create a log for
     * clearing the buffer and another one for loading it. So the
     * operation can be undone.
     */
    saved = b->save_log;
    b->save_log = 0;
    if (b->data_type->buffer_load)
        ret = b->data_type->buffer_load(b, f);
    else
        ret = -1;

    b->modified = 0;
    b->save_log = saved;

    if (!f1 && f)
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

QEModeData *qe_create_buffer_mode_data(EditBuffer *b, ModeDef *m)
{
    QEModeData *md = NULL;
    int size = m->buffer_instance_size - sizeof(QEModeData);

    if (size >= 0) {
        md = qe_mallocz_hack(QEModeData, size);
        if (md) {
            md->mode = m;
            md->b = b;
            md->next = b->mode_data_list;
            b->mode_data_list = md;
        }
        if (!b->default_mode) {
            b->default_mode = m;
        }
    }
    return md;
}

void *qe_get_buffer_mode_data(EditBuffer *b, ModeDef *m, EditState *e)
{
    if (b) {
        QEModeData *md;
        for (md = b->mode_data_list; md; md = md->next) {
            if (md->mode == m)
                return md;
        }
    }
    if (e)
        put_status(e, "Not a %s buffer", m->name);

    return NULL;
}

QEModeData *qe_create_window_mode_data(EditState *s, ModeDef *m)
{
    QEModeData *md = NULL;
    int size = m->window_instance_size - sizeof(QEModeData);

    if (!s->mode_data && size >= 0) {
        md = qe_mallocz_hack(QEModeData, size);
        if (md) {
            md->mode = m;
            md->s = s;
            s->mode_data = md;
        }
    }
    return md;
}

void *qe_get_window_mode_data(EditState *e, ModeDef *m, int status)
{
    if (e) {
        QEModeData *md = e->mode_data;
        if (md && md->mode == m)
            return md;
    }
    if (status)
        put_status(e, "Not a %s buffer", m->name);

    return NULL;
}

int qe_free_mode_data(QEModeData *md)
{
    int rc = -1;

    if (!md)
        return 0;

    if (check_buffer(&md->b)) {
        EditBuffer *b = md->b;
        QEModeData **mdp;
        for (mdp = &b->mode_data_list; *mdp; mdp = &(*mdp)->next) {
            if (*mdp == md) {
                /* unlink before calling destructor */
                *mdp = md->next;
                if (md->mode->mode_free)
                    md->mode->mode_free(b, md);
                rc = 0;
                break;
            }
        }
    }
    if (check_window(&md->s)) {
        if (md->s->mode_data == md) {
            md->s->mode_data = NULL;
            rc = 0;
        }
    }
    qe_free(&md);
    return rc;
}

int edit_set_mode(EditState *s, ModeDef *m)
{
    int mode_flags = 0;
    EditBuffer *b = s->b;
    const char *errstr = NULL;
    int rc = 0;

    /* if a mode is already defined, try to close it */
    if (s->mode) {
        /* save mode data if necessary */
        s->interactive = 0;
        if (s->mode->mode_close)
            s->mode->mode_close(s);
        generic_mode_close(s);
        qe_free_mode_data(s->mode_data);
        s->mode = NULL;  /* XXX: should instead use fundamental_mode */
        set_colorize_func(s, NULL);

        /* XXX: this code makes no sense, if must be reworked! */
#if 0
        int data_count;
        EditState *e;

        /* try to remove the raw or mode specific data if it is no
           longer used. */
        data_count = 0;
        for (e = s->qe_state->first_window; e != NULL; e = e->next_window) {
            if (e != s && e->b == b) {
                if (e->mode && e->mode->data_type != &raw_data_type)
                    data_count++;
            }
        }
        /* we try to remove mode specific data if it is redundant with
           the buffer raw data */
        if (data_count == 0 && !b->modified) {
            /* close mode specific buffer representation because it is
               always redundant if it was not modified */
            /* XXX: move this to reset buffer data: eb_free or changing
             * data_type */
            if (b->data_type != &raw_data_type) {
                b->data_type->buffer_close(b);
                b->data_data = NULL;
                b->data_type = &raw_data_type;
                eb_delete(b, 0, b->total_size);
                b->modified = 0;
            }
        }
#endif
    }

    /* if a new mode is wanted, open it */
    if (m) {
        s->mode_data = NULL;
        if (m->buffer_instance_size > 0) {
            if (!qe_get_buffer_mode_data(b, m, NULL)) {
                if (qe_create_buffer_mode_data(b, m)) {
                    mode_flags = MODEF_NEWINSTANCE;
                } else {
                    errstr = "Cannot allocate buffer mode data";
                }
            }
        }
        if (m->window_instance_size > 0) {
            if (!qe_create_window_mode_data(s, m)) {
                /* safe fall back: use text mode */
                errstr = "Cannot allocate window mode data";
            }
        }
        if (m->data_type != &raw_data_type) {
            /* if a non raw data type is requested, we see if we can use it */
            if (b->data_type == &raw_data_type) {
                /* non raw data type: we must call a mode specific
                   load method */
                s->mode = m;
                b->data_type = m->data_type;
                b->data_type_name = m->data_type->name;
                if (reload_buffer(s, b) < 0) {
                    b->data_type = &raw_data_type;
                    b->data_type_name = NULL;
                    errstr = "Cannot reload buffer";
                }
            } else
            if (b->data_type != m->data_type) {
                /* non raw data type requested, but the the buffer has
                   a different type: we cannot switch mode, so we fall
                   back to text */
                errstr = "incompatible data type";
            } else {
                /* same data type: nothing more to do */
            }
        } else {
            /* if raw data and nothing loaded, we try to load */
            if (b->total_size == 0 && !b->modified)
                reload_buffer(s, b);
        }
        if (errstr) {
            put_status(s, "Cannot set mode %s: %s", m->name, errstr);
            m = &text_mode;
            rc = -1;
        }

        s->mode = m;

        /* init mode */
        generic_mode_init(s);
        m->mode_init(s, s->b, MODEF_VIEW | mode_flags);
        if (m->colorize_func)
            set_colorize_func(s, m->colorize_func);
        /* modify offset_top so that its value is correct */
        if (s->mode->backward_offset)
            s->offset_top = s->mode->backward_offset(s, s->offset_top);
        /* keep saved data in sync with last mode used for buffer */
        generic_save_window_data(s);
    }
    return rc;
}

void do_set_mode(EditState *s, const char *name)
{
    ModeDef *m;

    /* XXX: should check if mode is appropriate */
    m = qe_find_mode(name, 0);
    if (m)
        edit_set_mode(s, m);
    else
        put_status(s, "No mode %s", name);
}

QECharset *read_charset(EditState *s, const char *charset_str,
                        EOLType *eol_typep)
{
    char buf[64];
    const char *p;
    QECharset *charset;
    EOLType eol_type = *eol_typep;

    p = NULL;

    if (strend(charset_str, "-mac", &p))
        eol_type = EOL_MAC;
    else
    if (strend(charset_str, "-dos", &p))
        eol_type = EOL_DOS;
    else
    if (strend(charset_str, "-unix", &p))
        eol_type = EOL_UNIX;

    if (p) {
        pstrncpy(buf, sizeof(buf), charset_str, p - charset_str);
        charset_str = buf;
    }

    charset = find_charset(charset_str);
    if (!charset) {
        put_status(s, "Unknown charset '%s'", charset_str);
        return NULL;
    }
    *eol_typep = eol_type;
    return charset;
}

void do_show_coding_system(EditState *s)
{
    put_status(s, "Buffer charset is now %s%s", s->b->charset->name,
               s->b->eol_type == EOL_DOS ? "-dos" :
               s->b->eol_type == EOL_MAC ? "-mac" : "-unix");
}

void do_set_auto_coding(EditState *s, int verbose)
{
    u8 buf[4096];
    int buf_size;
    EditBuffer *b = s->b;
    EOLType eol_type = b->eol_type;
    QECharset *charset;

    buf_size = eb_read(b, 0, buf, sizeof(buf));
    eol_type = b->eol_type;
    /* XXX: detect_charset returns a default charset */
    /* XXX: should enforce 32 bit alignment of buf */
    charset = detect_charset(buf, buf_size, &eol_type);
    eb_set_charset(b, charset, eol_type);
    if (verbose) {
        do_show_coding_system(s);
    }
}

void do_set_buffer_file_coding_system(EditState *s, const char *charset_str)
{
    QECharset *charset;
    EOLType eol_type;

    eol_type = s->b->eol_type;
    charset = read_charset(s, charset_str, &eol_type);
    if (!charset)
        return;
    eb_set_charset(s->b, charset, eol_type);
    do_show_coding_system(s);
}

/* convert the charset of a buffer to another charset */
void do_convert_buffer_file_coding_system(EditState *s,
                                          const char *charset_str)
{
    QECharset *charset;
    EOLType eol_type;
    EditBuffer *b1, *b;
    int offset, len, i;
    EditBufferCallbackList *cb;
    int pos[32];
    char buf[MAX_CHAR_BYTES];

    eol_type = s->b->eol_type;
    charset = read_charset(s, charset_str, &eol_type);
    if (!charset)
        return;

    b = s->b;

    b1 = eb_new("*tmp*", b->flags & BF_STYLES);
    eb_set_charset(b1, charset, eol_type);

    /* preserve positions */
    cb = b->first_callback;
    for (i = 0; i < countof(pos) && cb; cb = cb->next) {
        if (cb->callback == eb_offset_callback) {
            pos[i] = eb_get_char_offset(b, *(int*)cb->opaque);
            i++;
        }
    }

    /* slow, but simple iterative method */
    for (offset = 0; offset < b->total_size;) {
        int style = eb_get_style(b, offset);
        int c = eb_nextc(b, offset, &offset);
        b1->cur_style = style;
        len = eb_encode_uchar(b1, buf, c);
        eb_insert(b1, b1->total_size, buf, len);
    }

    /* replace current buffer with conversion */
    /* quick hack to transfer styles from tmp buffer to b */
    eb_free(&b->b_styles);
    eb_delete(b, 0, b->total_size);
    eb_set_charset(b, charset, eol_type);
    eb_insert_buffer(b, 0, b1, 0, b1->total_size);
    b->b_styles = b1->b_styles;
    b1->b_styles = NULL;

    /* restore positions */
    cb = b->first_callback;
    for (i = 0; i < countof(pos) && cb; cb = cb->next) {
        if (cb->callback == eb_offset_callback) {
            *(int*)cb->opaque = eb_goto_char(b, pos[i]);
            i++;
        }
    }

    eb_free(&b1);

    put_status(s, "Buffer charset is now %s, %d bytes",
               s->b->charset->name, b->total_size);
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
    if (s->wrap == WRAP_TRUNCATE) {
        s->wrap = WRAP_LINE;
        s->x_disp[0] = s->x_disp[1] = 0;
    } else {
        s->wrap = WRAP_TRUNCATE;
    }
}

void do_word_wrap(EditState *s)
{
    if (s->wrap == WRAP_WORD) {
        s->wrap = WRAP_LINE;
    } else {
        s->wrap = WRAP_WORD;
        s->x_disp[0] = s->x_disp[1] = 0;
    }
}

/* do_goto: move point to a specified position.
 * take string and default unit,
 * string is parsed as an integer with an optional sign, unit and suffix
 * units: (b)yte, (c)har, (w)ord, (l)line, (%)percentage
 * optional suffix :col or .col for column number in goto_line
 */

void do_goto(EditState *s, const char *str, int unit)
{
    const char *p;
    int pos, line, col, rel;

    /* Update s->offset from str specification:
     * optional +- for relative moves
     * distance in decimal, octal or hex
     * optional space
     * optional unit suffix:
     *    b(bytes) c(chars) w(words) l(lines) %(percent)
     *
     * CG: XXX: resulting offset may fall inside a character.
     */
    rel = (*str == '+' || *str == '-');
    pos = strtol(str, (char**)&p, 0);

    /* skip space required to separate hex offset from b or c suffix */
    if (*p == ' ')
        p++;

    /* handle an optional multiple suffix */
    switch (*p) {
    case 'g':
        pos *= 1000;
        /* fall thru */
    case 'm':
        pos *= 1000;
        /* fall thru */
    case 'k':
        pos *= 1000;
        p++;
        break;
    case 'G':
        pos *= 1024;
        /* fall thru */
    case 'M':
        pos *= 1024;
        /* fall thru */
    case 'K':
        pos *= 1024;
        p++;
        break;
    }

    if (memchr("bcwl%", *p, 5))
        unit = *p++;

    switch (unit) {
    case 'b':
        if (*p)
            goto error;
        if (rel)
            pos += s->offset;
        /* XXX: should realign on character boundary?
         *      realignment probably better addressed in display module
         */
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
        eb_get_pos(s->b, &line, &col, clamp(pos, 0, s->b->total_size));
        line += (col > 0);
        goto getcol;

    case 'l':
        line = pos - 1;
        if (rel || pos <= 0) {
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
    put_status(s, "Invalid position: %s", str);
}

void do_goto_line(EditState *s, int line, int column)
{
    if (line >= 1)
        s->offset = eb_goto_pos(s->b, line - 1, column > 0 ? column - 1 : 0);
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
    int accents[6];
    buf_t outbuf, *out;
    int line_num, col_num;
    int offset1, off;
    int c, cc;
    int i, n;

    out = buf_init(&outbuf, buf, sizeof(buf));
    if (s->offset < s->b->total_size) {
        c = eb_nextc(s->b, s->offset, &offset1);
        n = 0;
        if (c != '\n' && !s->unihex_mode) {
            while (n < countof(accents)
               &&  qe_isaccent(cc = eb_nextc(s->b, offset1, &off))) {
                   accents[n++] = cc;
                   offset1 = off;
               }
        }
        if (s->b->eol_type == EOL_MAC) {
            /* CR and LF are swapped in old style Mac buffers */
            if (c == '\r' || c == '\n')
                c ^= '\r' ^ '\n';
        }
        buf_puts(out, "char:");
        if (c < 32 || c == 127) {
            buf_printf(out, " ^%c", (c + '@') & 127);
        } else
        if (c < 127 || c >= 160) {
            buf_put_byte(out, ' ');
            buf_put_byte(out, '\'');
            if (c == '\\' || c == '\'') {
                buf_put_byte(out, '\\');
            }
            if (qe_isaccent(c)) {
                buf_putc_utf8(out, ' ');
            }
            buf_putc_utf8(out, c);
            for (i = 0; i < n; i++) {
                buf_put_byte(out, ' ');
                buf_putc_utf8(out, accents[i]);
            }
            buf_put_byte(out, '\'');
        }
        if (n == 0) {
            if (c < 0x100) {
                buf_printf(out, " \\%03o", c);
            }
            buf_printf(out, " %d", c);
        }
        buf_printf(out, " 0x%02x", c);
        for (i = 0; i < n; i++) {
            buf_printf(out, "/0x%02x", accents[i]);
        }
        /* Display buffer bytes if char is encoded */
        if (offset1 != s->offset + 1) {
            int sep = '[';
            buf_put_byte(out, ' ');
            for (off = s->offset; off < offset1; off++) {
                cc = eb_read_one_byte(s->b, off);
                buf_printf(out, "%c%02X", sep, cc);
                sep = ' ';
            }
            buf_put_byte(out, ']');
        }
    }
    eb_get_pos(s->b, &line_num, &col_num, s->offset);
    put_status(s, "%s  point=%d mark=%d size=%d region=%d col=%d",
               out->buf, s->offset, s->b->mark, s->b->total_size,
               abs(s->offset - s->b->mark), col_num);
}

void do_set_tab_width(EditState *s, int tab_width)
{
    if (tab_width > 1)
        s->b->tab_width = tab_width;
}

void do_set_indent_width(EditState *s, int indent_width)
{
    if (indent_width > 1)
        s->indent_size = indent_width;
}

void do_set_indent_tabs_mode(EditState *s, int val)
{
    s->indent_tabs_mode = (val != 0);
}

static void do_set_fill_column(EditState *s, int fill_column)
{
    if (fill_column > 1)
        s->b->fill_column = fill_column;
}

/* compute string for the first part of the mode line (flags,
   filename, modename) */
void basic_mode_line(EditState *s, buf_t *out, int c1)
{
    int mod, state;

    mod = s->b->modified ? '*' : '-';
    if (s->b->flags & BF_LOADING)
        state = 'L';
    else if (s->b->flags & BF_SAVING)
        state = 'S';
    else if (s->busy)
        state = 'B';
    else
        state = '-';

    buf_printf(out, "%c%c:%c%c  %-20s  (",
               c1, state, s->b->flags & BF_READONLY ? '%' : mod,
               mod, s->b->name);
    if (s->b->data_type_name)
        buf_printf(out, "%s+", s->b->data_type_name);
    buf_puts(out, s->mode ? s->mode->name : "raw");
    if (!s->insert)
        buf_puts(out, " Ovwrt");
    if (s->interactive)
        buf_puts(out, " Interactive");
    if (s->b->flags & BF_PREVIEW)
        buf_puts(out, " Preview");
    buf_puts(out, ")--");
}

void text_mode_line(EditState *s, buf_t *out)
{
    int line_num, col_num, wrap_mode;
    const QEProperty *tag;

    wrap_mode = '-';
    if (!s->hex_mode) {
        if (s->wrap == WRAP_TRUNCATE)
            wrap_mode = 'T';
        else if (s->wrap == WRAP_WORD)
            wrap_mode = 'W';
    }
    basic_mode_line(s, out, wrap_mode);

    eb_get_pos(s->b, &line_num, &col_num, s->offset);
    buf_printf(out, "L%d--C%d--%s",
               line_num + 1, col_num, s->b->charset->name);
    if (s->b->eol_type == EOL_DOS)
        buf_puts(out, "-dos");
    if (s->b->eol_type == EOL_MAC)
        buf_puts(out, "-mac");
    if (s->bidir)
        buf_printf(out, "--%s", s->cur_rtl ? "RTL" : "LTR");

    if (s->input_method)
        buf_printf(out, "--%s", s->input_method->name);
    buf_printf(out, "--%d%%", compute_percent(s->offset, s->b->total_size));
    if (s->x_disp[0])
        buf_printf(out, "--<%d", -s->x_disp[0]);
    if (s->x_disp[1])
        buf_printf(out, "-->%d", -s->x_disp[1]);
    tag = eb_find_property(s->b, 0, s->offset + 1, QE_PROP_TAG);
    if (tag)
        buf_printf(out, "--%s", tag->data);
#if 0
    buf_printf(out, "--[%d]", s->y_disp);
#endif
}

void display_mode_line(EditState *s)
{
    char buf[MAX_SCREEN_WIDTH];
    buf_t outbuf, *out;
    int y = s->ytop + s->height;

    if (s->flags & WF_MODELINE) {
        out = buf_init(&outbuf, buf, sizeof(buf));
        s->mode->get_mode_line(s, out);
        if (!strequal(buf, s->modeline_shadow)) {
            print_at_byte(s->screen, s->xleft, y, s->width,
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
            int x = e->x1;
            int y = e->y1;
            int width = e->x2 - e->x1;
            int height = e->y2 - e->y1;
            int bw = qs->border_width;
            int bh = qs->border_width;

            rect.x1 = 0;
            rect.y1 = 0;
            rect.x2 = qs->width;
            rect.y2 = qs->height;
            set_clip_rectangle(qs->screen, &rect);
            color = qe_styles[QE_STYLE_WINDOW_BORDER].bg_color;
            if (e->flags & WF_POPUP) {
                fill_rectangle(qs->screen, x, y, width, bh, color);
                fill_rectangle(qs->screen,
                               x, y + bh, bw, height - bh - bh, color);
                fill_rectangle(qs->screen,
                               x + width - bw, y + bh, bw, height - bh - bh, color);
                fill_rectangle(qs->screen,
                               x, y + height - bh, width, bh, color);
            }
            if (e->flags & WF_RSEPARATOR) {
                fill_rectangle(qs->screen,
                               x + width - qs->separator_width, y,
                               qs->separator_width, height, color);
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

int find_style_index(const char *name)
{
    int i;
    QEStyleDef *style;

    style = qe_styles;
    for (i = 0; i < QE_STYLE_NB; i++, style++) {
        if (strequal(style->name, name))
            return i;
    }
    if (qe_isdigit(*name)) {
        i = strtol(name, NULL, 0);
        if (i < QE_STYLE_NB)
            return i;
    }
    return -1;
}

QEStyleDef *find_style(const char *name)
{
    int i = find_style_index(name);

    if (i >= 0 && i < QE_STYLE_NB)
        return qe_styles + i;
    else
        return NULL;
}

static const char * const qe_style_properties[] = {
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

void do_set_display_size(qe__unused__ EditState *s, int w, int h)
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

void do_set_window_style(EditState *s, const char *stylestr)
{
    int style = find_style_index(stylestr);

    if (style < 0) {
        put_status(s, "Unknown style '%s'", stylestr);
        return;
    }
    s->default_style = style;
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

static void display_bol_bidir(DisplayState *s, DirType base,
                              int embedding_level_max)
{
    s->base = base;
    s->x = s->x_disp = s->edit_state->x_disp[base];
    if (s->base == DIR_RTL) {
        /* XXX: probably broken. bidir handling needs fixing */
        s->x_start = s->edit_state->width - s->x;
    } else {
        s->x_start = s->x;
    }
    s->left_gutter = 0;
    s->x_line = s->x_start;
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

void display_close(DisplayState *s)
{
}

void display_init(DisplayState *s, EditState *e, enum DisplayType do_disp,
                  int (*cursor_func)(DisplayState *ds,
                                     int offset1, int offset2, int line_num,
                                     int x, int y, int w, int h, int hex_mode),
                  void *cursor_opaque)
{
    QEFont *font;
    QEStyleDef style;

    s->edit_state = e;
    s->do_disp = do_disp;
    s->cursor_func = cursor_func;
    s->cursor_opaque = cursor_opaque;
    s->wrap = e->wrap;
    /* select default values */
    get_style(e, &style, QE_STYLE_DEFAULT);
    font = select_font(e->screen, style.font_style, style.font_size);
    s->default_line_height = font->ascent + font->descent;
    s->eol_width = max3(glyph_width(e->screen, font, '/'),
                        glyph_width(e->screen, font, '\\'),
                        glyph_width(e->screen, font, '$'));
    s->space_width = glyph_width(e->screen, font, ' ');
    s->tab_width = s->space_width * e->b->tab_width;
    s->width = e->width - s->eol_width;
    s->height = e->height;
    s->hex_mode = e->hex_mode;
    s->cur_hex_mode = 0;
    s->y = e->y_disp;
    s->line_num = 0;
    s->line_numbers = e->line_numbers * s->space_width * 8;
    if (s->line_numbers > s->width / 2)
        s->line_numbers = 0;
    s->eol_reached = 0;
    s->eod = 0;
    display_bol(s);
    release_font(e->screen, font);
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
static unsigned int compute_crc(const void *p, int size, unsigned int sum)
{
    const u8 *data = (const u8 *)p;

    /* Rotating sum necessary to prevent trivial collisions on
     * line_chars because it is an array of code points stored as u32.
     */
    /* XXX: We still have a bug when transposing two 31 byte words as in
     * B123456789012345678901234567890 A123456789012345678901234567890
     */
    while (((uintptr_t)data & 3) && size > 0) {
        sum += ((sum >> 31) & 1) + sum + *data;
        data++;
        size--;
    }
    while (size >= 4) {
        sum += ((sum >> 31) & 1) + sum + *(const uint32_t *)(const void *)data;
        data += 4;
        size -= 4;
    }
    while (size > 0) {
        sum += ((sum >> 31) & 1) + sum + *data;
        data++;
        size--;
    }
    return sum;
}

/* flush the line fragments to the screen.
   `offset1..offset2` is the range of offsets for cursor management
   `last` is 0 for a line wrap, 1 for end of line, -1 for continuation
*/
static void flush_line(DisplayState *s,
                       TextFragment *fragments, int nb_fragments,
                       int offset1, int offset2, int last)
{
    EditState *e = s->edit_state;
    QEditScreen *screen = e->screen;
    int level, pos, p, i, x, x1, y, baseline, line_height, max_descent;
    TextFragment *frag;
    QEFont *font;

    /* compute baseline and lineheight (incorrect for very long lines) */
    baseline = 0;
    max_descent = 0;
    for (i = 0; i < nb_fragments; i++) {
        frag = &fragments[i];
        if (frag->ascent > baseline)
            baseline = frag->ascent;
        if (frag->descent > max_descent)
            max_descent = frag->descent;
    }
    if (nb_fragments == 0) {
        /* if empty line, still needs a non zero line height */
        line_height = s->default_line_height;
    } else {
        line_height = baseline + max_descent;
    }

    /* swap according to embedding level (incorrect for very long lines) */
    for (level = s->embedding_level_max; level > 0; level--) {
        pos = 0;
        while (pos < nb_fragments) {
            if (fragments[pos].embedding_level >= level) {
                /* find all chars >= level */
                for (p = pos + 1; p < nb_fragments && fragments[p].embedding_level >= level; p++)
                    continue;
                reverse_fragments(fragments + pos, p - pos);
                pos = p + 1;
            } else {
                pos++;
            }
        }
    }

    /* draw everything if line is visible in window */
    if (s->do_disp == DISP_PRINT 
    &&  s->y + line_height >= 0
    &&  s->y < e->ytop + e->height) {
        QEStyleDef style, default_style;
        QELineShadow *ls;
        unsigned int crc;

        /* test if display needed */
        crc = compute_crc(fragments, sizeof(*fragments) * nb_fragments, 0);
        crc = compute_crc(s->line_chars, sizeof(*s->line_chars) * s->line_index, crc);
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
        /* Use checksum based line cache to improve speed in graphics mode.
         * XXX: overlong lines will fail the cache test
         */
        ls = &e->line_shadow[s->line_num];
        if (ls->y != s->y || ls->x != s->x_line
        ||  ls->height != line_height || ls->crc != crc) {
            /* update values for the line cache */
            ls->y = s->y;
            ls->x = s->x_line;
            ls->height = line_height;
            ls->crc = crc;

            /* display */
            get_style(e, &default_style, QE_STYLE_DEFAULT);
            x = s->x_start;
            y = s->y;

            /* first display background rectangles */
            /* XXX: should coalesce rectangles with identical style */
            if (s->left_gutter > 0) {
                /* erase space before the line display, aka left gutter */
                fill_rectangle(screen, e->xleft + x, e->ytop + y,
                               s->left_gutter, line_height,
                               default_style.bg_color);
            }
            x = s->x_line;
            x1 = s->width + s->eol_width;
            for (i = 0; i < nb_fragments && x < x1; i++) {
                frag = &fragments[i];
                get_style(e, &style, frag->style);
                fill_rectangle(screen, e->xleft + x, e->ytop + y, 
                               frag->width, line_height, style.bg_color);
                x += frag->width;
            }
            if (x < x1 && last != -1) {
                fill_rectangle(screen, e->xleft + x, e->ytop + y,
                               x1 - x, line_height, default_style.bg_color);
            }

            /* then display text */
            x = s->x_line;
            y += baseline;

            for (i = 0; i < nb_fragments && x < x1; i++) {
                frag = &fragments[i];
                x += frag->width;
                if (x > 0) {
                    get_style(e, &style, frag->style);
                    font = select_font(screen,
                                       style.font_style, style.font_size);
                    draw_text(screen, font, 
                              e->xleft + x - frag->width, e->ytop + y,
                              s->line_chars + frag->line_index,
                              frag->len, style.fg_color);
                    release_font(screen, font);
                }
            }

            if (last == 0) {
                /* draw eol mark */
                unsigned int markbuf[1];

                markbuf[0] = '/';        /* RTL eol mark */
                x = 0;                   /* displayed at the left border */
                if (s->base == DIR_LTR) {
                    markbuf[0] = '\\';   /* LTR eol mark */
                    x = s->width;        /* displayed at the right border */
                }
                font = select_font(screen,
                                   default_style.font_style,
                                   default_style.font_size);
                draw_text(screen, font, e->xleft + x, e->ytop + y,
                          markbuf, 1, default_style.fg_color);
                release_font(screen, font);
            }
        }
    }

    /* call cursor callback */
    if (s->cursor_func) {
        x = s->x_line;
        y = s->y;

        /* RTL eol cursor check (probably incorrect) */
        if (offset1 >= 0 && offset2 >= 0 &&
            s->base == DIR_RTL &&
            s->cursor_func(s, offset1, offset2, s->line_num,
                           x, y, -s->eol_width, line_height, e->hex_mode)) {
            s->eod = 1;
        }

        for (i = 0; i < nb_fragments; i++) {
            int j, k;

            frag = &fragments[i];

            for (j = frag->line_index, k = 0; k < frag->len; k++, j++) {
                int _offset1 = s->line_offsets[j][0];
                int _offset2 = s->line_offsets[j][1];
                int hex_mode = s->line_hex_mode[j];
                int w = s->line_char_widths[j];
                x += w;
                if ((hex_mode == s->hex_mode || s->hex_mode == -1) &&
                    _offset1 >= 0 && _offset2 >= 0) {
#if 0
                    /* probably broken, bidir needs rework */
                    if (s->base == DIR_RTL) {
                        if (s->cursor_func(s, _offset1, _offset2, s->line_num,
                                           x, y, -w, line_height, hex_mode))
                            s->eod = 1;
                    } else 
#endif
                    {
                        if (s->cursor_func(s, _offset1, _offset2, s->line_num,
                                           x - w, y, w, line_height, hex_mode))
                            s->eod = 1;
                    }
                }
            }
        }

        /* LTR eol cursor check */
        if (offset1 >= 0 && offset2 >= 0 &&
            s->base == DIR_LTR &&
            s->cursor_func(s, offset1, offset2, s->line_num,
                           x, y, s->eol_width, line_height, e->hex_mode)) {
            s->eod = 1;
        }
        s->x_line = x;
    }
#if 0
    printf("y=%d line_num=%d line_height=%d baseline=%d\n",
           s->y, s->line_num, line_height, baseline);
#endif
    if (last != -1) {
        /* bump to next line */
        s->x_line = s->x_start;
        s->y += line_height;
        s->line_num++;
    }
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

    if (s->nb_fragments >= MAX_SCREEN_WIDTH ||
        s->line_index + s->fragment_index > MAX_SCREEN_WIDTH) {
        /* too many fragments on the same line, flush and stay on the line */
        flush_line(s, s->fragments, s->nb_fragments, -1, -1, -1);
        s->nb_fragments = 0;
        s->line_index = 0;
        s->word_index = 0;
    }

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
        /* XXX: is the width negative for a RTL fragment? */
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

            /* flush fragments with a line continuation mark */
            flush_line(s, s->fragments, n, -1, -1, 0);

            /* skip line number column if present */
            s->left_gutter = s->line_numbers;
            s->x = s->x_line += s->left_gutter;

            /* move the remaining fragment to next line */
            s->nb_fragments = 0;
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

            /* flush fragments with a line continuation mark */
            flush_line(s, s->fragments, s->word_index, -1, -1, 0);

            /* skip line number column if present */
            s->left_gutter = s->line_numbers;
            s->x = s->x_line += s->left_gutter;

            /* put words on next line */
            index = s->fragments[s->word_index].line_index;
            memmove(s->fragments, s->fragments + s->word_index,
                    (s->nb_fragments - s->word_index) * sizeof(TextFragment));
            s->nb_fragments -= s->word_index;

            for (i = 0; i < s->nb_fragments; i++) {
                s->fragments[i].line_index -= index;
                s->x += s->fragments[i].width;
            }
            keep_line_chars(s, s->line_index - index);
            s->word_index = 0;
        }
        break;
    }
    s->fragment_index = 0;
}

int display_char_bidir(DisplayState *s, int offset1, int offset2,
                       int embedding_level, int ch)
{
    int space, style, istab, isaccent;
    EditState *e;

    style = s->style;

    /* special code to colorize block */
    e = s->edit_state;
    if (e->show_selection || e->region_style) {
        int mark = e->b->mark;
        int offset = e->offset;

        if ((offset1 >= offset && offset1 < mark) ||
            (offset1 >= mark && offset1 < offset)) {
            if (e->show_selection)
                style |= QE_STYLE_SEL;
            else
                style = e->region_style;
        }
    }
    /* special patch for selection in hex mode */
    if (offset1 == offset2) {
        offset1 = -1;
        offset2 = -1;
    }

    space = (ch == ' ');
    istab = (ch == '\t');
    isaccent = qe_isaccent(ch);
    /* a fragment is a part of word where style/embedding_level do not
       change. For TAB, only one fragment containing it is sent */
    if (s->fragment_index >= 1) {
        if (s->fragment_index >= MAX_WORD_SIZE ||
            istab ||
            space != s->last_space ||
            style != s->last_style ||
            embedding_level != s->last_embedding_level) {
            /* flush the current fragment if needed */
            if (isaccent && s->fragment_chars[s->fragment_index - 1] == ' ') {
                /* separate last space to make it part of the next word */
                int off1, off2, cur_hex;
                --s->fragment_index;
                off1 = s->fragment_offsets[s->fragment_index][0];
                off2 = s->fragment_offsets[s->fragment_index][1];
                cur_hex = s->fragment_hex_mode[s->fragment_index];
                flush_fragment(s);
                s->fragment_chars[s->fragment_index] = ' ';
                s->fragment_offsets[s->fragment_index][0] = off1;
                s->fragment_offsets[s->fragment_index][1] = off2;
                s->fragment_hex_mode[s->fragment_index] = cur_hex;
                s->fragment_index++;
            } else {
                flush_fragment(s);
            }
        }
    }

    if (isaccent && s->fragment_index == 0) {
        /* prepend a space if fragment starts with an accent */
        s->fragment_chars[s->fragment_index] = ' ';
        s->fragment_offsets[s->fragment_index][0] = offset1;
        s->fragment_offsets[s->fragment_index][1] = offset2;
        s->fragment_hex_mode[s->fragment_index] = s->cur_hex_mode;
        s->fragment_index++;
        offset1 = offset2 = -1;
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
        /* XXX: utf-8 unsupported, not needed at this point */
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
        /* XXX: need early bailout from display_line if WRAP_TRUNCATE
           and far beyond the right border after cursor found.
        */
        offset = e->mode->display_line(e, s, offset);
        e->offset_bottom = offset;

        /* EOF reached ? */
        if (offset < 0)
            break;

        switch (s->do_disp) {
        case DISP_NONE:
            return;
        case DISP_CURSOR:
            if (s->eod)
                return;
            break;
        case DISP_CURSOR_SCREEN:
            if (s->eod || s->y >= s->height)
                return;
            break;
        case DISP_PRINT:
        default:
            if (s->y >= s->height)
                return; /* end of screen */
            break;
        }
    }
}

/******************************************************/
int text_backward_offset(EditState *s, int offset)
{
    int line, col;

    /* CG: beware: offset may fall inside a character */
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

/************************************************************/
/* colorization handling */
/* NOTE: only one colorization mode can be selected at a time for a
   buffer */

static int get_staticly_colorized_line(EditState *s, unsigned int *buf, int buf_size,
                                       int offset, int *offset_ptr, int line_num)
{
    EditBuffer *b = s->b;
    unsigned int *buf_ptr, *buf_end;

    buf_ptr = buf;
    buf_end = buf + buf_size - 1;
    for (;;) {
        int style = eb_get_style(b, offset);
        int c = eb_nextc(b, offset, &offset);
        if (c == '\n')
            break;
        if (buf_ptr < buf_end) {
            c = (c & CHAR_MASK) | (style << STYLE_SHIFT);
            *buf_ptr++ = c;
        }
    }
    *buf_ptr = '\0';
    *offset_ptr = offset;
    return buf_ptr - buf;
}

#ifndef CONFIG_TINY

/* Gets the colorized line beginning at 'offset'. Its length
   excluding '\n' is returned */

#define COLORIZED_LINE_PREALLOC_SIZE 64

static int syntax_get_colorized_line(EditState *s, 
                                     unsigned int *buf, int buf_size, 
                                     int offset, int *offsetp, int line_num)
{
    QEColorizeContext cctx;
    EditBuffer *b = s->b;
    int len, line, n, col, bom;

    /* invalidate cache if needed */
    if (s->colorize_max_valid_offset != INT_MAX) {
        eb_get_pos(b, &line, &col, s->colorize_max_valid_offset);
        line++;
        if (line < s->colorize_nb_valid_lines)
            s->colorize_nb_valid_lines = line;
        eb_delete_properties(b, s->colorize_max_valid_offset, INT_MAX);
        s->colorize_max_valid_offset = INT_MAX;
    }

    /* realloc state array if needed */
    if ((line_num + 2) > s->colorize_nb_lines) {
        /* Reallocate colorization state buffer with pseudo-Fibonacci
         * geometric progression (ratio of 1.625)
         */
        n = max(s->colorize_nb_lines, COLORIZED_LINE_PREALLOC_SIZE);
        while (n < (line_num + 2))
            n += (n >> 1) + (n >> 3);
        if (!qe_realloc(&s->colorize_states,
                        n * sizeof(*s->colorize_states))) {
            return 0;
        }
        s->colorize_nb_lines = n;
    }

    memset(&cctx, 0, sizeof(cctx));
    cctx.s = s;
    cctx.b = b;

    /* propagate state if needed */
    if (line_num >= s->colorize_nb_valid_lines) {
        if (s->colorize_nb_valid_lines == 0) {
            s->colorize_states[0] = 0; /* initial state : zero */
            s->colorize_nb_valid_lines = 1;
        }
        offset = eb_goto_pos(b, s->colorize_nb_valid_lines - 1, 0);
        cctx.colorize_state = s->colorize_states[s->colorize_nb_valid_lines - 1];
        cctx.state_only = 1;

        for (line = s->colorize_nb_valid_lines; line <= line_num; line++) {
            cctx.offset = offset;
            len = eb_get_line(b, buf, buf_size - 1, offset, &offset);
            if (buf[len] != '\n') {
                /* line was truncated */
                /* XXX: should use reallocatable buffer */
                offset = eb_goto_pos(b, line, 0);
            }
            buf[len] = '\0';

            /* skip byte order mark if present */
            bom = (buf[0] == 0xFEFF);
            if (bom) {
                cctx.offset = eb_next(b, cctx.offset);
            }
            s->colorize_func(&cctx, buf + bom, len - bom, s->mode);
            s->colorize_states[line] = cctx.colorize_state;
        }
    }

    /* compute line color */
    cctx.colorize_state = s->colorize_states[line_num];
    cctx.state_only = 0;
    cctx.offset = offset;
    len = eb_get_line(b, buf, buf_size - 1, offset, offsetp);
    if (buf[len] != '\n') {
        /* line was truncated */
        /* XXX: should use reallocatable buffer */
        *offsetp = eb_goto_pos(b, line_num + 1, 0);
    }
    buf[len] = '\0';

    bom = (buf[0] == 0xFEFF);
    if (bom) {
        SET_COLOR1(buf, 0, QE_STYLE_PREPROCESS);
        cctx.offset = eb_next(b, cctx.offset);
    }
    s->colorize_func(&cctx, buf + bom, len - bom, s->mode);
    /* buf[len] has char '\0' but may hold style, force buf ending */
    buf[len + 1] = 0;

    /* XXX: if state is same as previous, minimize invalid region? */
    s->colorize_states[line_num + 1] = cctx.colorize_state;

    /* Extend valid area */
    if (s->colorize_nb_valid_lines < line_num + 2)
        s->colorize_nb_valid_lines = line_num + 2;

    return len;
}

/* invalidate the colorize data */
static void colorize_callback(qe__unused__ EditBuffer *b,
                              void *opaque, qe__unused__ int arg,
                              qe__unused__ enum LogOperation op,
                              int offset,
                              qe__unused__ int size)
{
    EditState *e = opaque;

    if (offset < e->colorize_max_valid_offset)
        e->colorize_max_valid_offset = offset;
}

int combine_static_colorized_line(EditState *s, unsigned int *buf, 
                                  int len, int offset)
{
    EditBuffer *b = s->b;
    int i;

    if (b->b_styles) {
        for (i = 0; i < len; i++) {
            int style = eb_get_style(b, offset);
            if (style) {
                buf[i] = (buf[i] & CHAR_MASK) | (style << STYLE_SHIFT);
            }
            offset = eb_next(b, offset);
        }
    }
    return len;
}

#endif /* CONFIG_TINY */

void set_colorize_func(EditState *s, ColorizeFunc colorize_func)
{
    s->get_colorized_line = generic_get_colorized_line;
    s->colorize_func = NULL;

#ifndef CONFIG_TINY
    /* invalidate the previous states & free previous colorizer */
    eb_free_callback(s->b, colorize_callback, s);
    qe_free(&s->colorize_states);
    s->colorize_nb_lines = 0;
    s->colorize_nb_valid_lines = 0;
    s->colorize_max_valid_offset = INT_MAX;
    s->colorize_func = colorize_func;
    if (colorize_func)
        eb_add_callback(s->b, colorize_callback, s, 0);
#endif
}

int generic_get_colorized_line(EditState *s, unsigned int *buf, int buf_size,
                               int offset, int *offsetp, int line_num)
{
    int len;

#ifndef CONFIG_TINY
    if (s->colorize_func) {
        len = syntax_get_colorized_line(s, buf, buf_size, offset, offsetp, line_num);
        if (s->b->b_styles && s->colorize_func != shell_colorize_line) {
            /* XXX: shell mode should have its own get_colorized_line handler */
            combine_static_colorized_line(s, buf, buf_size, len);
        }
    } else
#endif
    if (s->b->b_styles) {
        len = get_staticly_colorized_line(s, buf, buf_size, offset, offsetp, line_num);
    } else {
        len = eb_get_line(s->b, buf, buf_size, offset, offsetp);
        if (buf[len] != '\n') {
            /* line was truncated */
            /* XXX: should use reallocatable buffer */
            *offsetp = eb_goto_pos(s->b, line_num + 1, 0);
        }
        buf[len] = '\0';
    }
    return len;
}

#define RLE_EMBEDDINGS_SIZE    128

/* Display one line in the window */
int text_display_line(EditState *s, DisplayState *ds, int offset)
{
    int c;
    int offset0, offset1, line_num, col_num;
    TypeLink embeds[RLE_EMBEDDINGS_SIZE], *bd;
    int embedding_level, embedding_max_level;
    FriBidiCharType base;
    unsigned int colored_chars[COLORED_MAX_LINE_SIZE];
    int char_index, colored_nb_chars;

    line_num = 0;
    /* XXX: should test a flag, to avoid this call in hex/binary */
    if (s->line_numbers || s->colorize_func) {
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
    if (ds->line_numbers) {
        ds->style = QE_STYLE_COMMENT;
        display_printf(ds, -1, -1, "%6d  ", line_num + 1);
        ds->style = 0;
    }

    /* prompt display */
    if (s->prompt && offset1 == 0) {
        const char *p = s->prompt;

        while (*p) {
            display_char(ds, -1, -1, utf8_decode(&p));
        }
    }

    /* colorize */
    colored_nb_chars = 0;
    offset0 = offset;
    if (s->colorize_func
    ||  s->curline_style || s->region_style
    ||  s->b->b_styles
    ||  s->isearch_state) {
        /* XXX: deal with truncation */
        colored_nb_chars = s->get_colorized_line(s, colored_chars,
                                                 countof(colored_chars),
                                                 offset, &offset0, line_num);
        if (s->isearch_state) {
            isearch_colorize_matches(s, colored_chars, colored_nb_chars,
                                     offset);
        }
    }

#if 1
    /* colorize regions */
    if (s->curline_style || s->region_style) {
        /* CG: Should combine styles instead of replacing */
        if (s->region_style && !s->curline_style) {
            int line, start_offset, end_offset;
            int start_char, end_char;

            if (s->b->mark < s->offset) {
                start_offset = max(offset, s->b->mark);
                end_offset = min(offset0, s->offset);
            } else {
                start_offset = max(offset, s->offset);
                end_offset = min(offset0, s->b->mark);
            }
            if (start_offset < end_offset) {
                /* Compute character positions */
                eb_get_pos(s->b, &line, &start_char, start_offset);
                if (end_offset >= offset0)
                    end_char = colored_nb_chars;
                else
                    eb_get_pos(s->b, &line, &end_char, end_offset);
                clear_color(colored_chars + start_char, end_char - start_char);
                set_color(colored_chars + start_char, colored_chars + end_char,
                          s->region_style);
            }
        } else
        if (s->curline_style && s->offset >= offset && s->offset <= offset0) {
            /* XXX: only if qs->active_window == s ? */
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
            /* the offset passed here is for cursor positioning 
               when s->offset == s->b->total_size.
            */
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
            if (c == '\n' && !s->minibuf) {
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
                if (c == '\r' && s->b->eol_type == EOL_MAC)
                    c = '\n';
                display_printf(ds, offset0, offset, "^%c", ('@' + c) & 127);
            } else
            if (c >= 128
            &&  (s->qe_state->show_unicode == 1 ||
                 c == 0xfeff ||   /* Display BOM as \uFEFF to make it explicit */
                 c > MAX_UNICODE_DISPLAY)) {
                /* display unsupported unicode code points as hex */
                if (c > 0xffff) {
                    display_printf(ds, offset0, offset, "\\U%08x", c);
                } else {
                    display_printf(ds, offset0, offset, "\\u%04x", c);
                }
            } else {
                display_char_bidir(ds, offset0, offset, embedding_level, c);
            }
            char_index++;
            //if (ds->y >= s->height && ds->eod)  //@@@ causes bug
            //    break;
        }
    }
    return offset;
}

/* Generic display algorithm with automatic fit */
static void generic_text_display(EditState *s)
{
    CursorContext m1, *m = &m1;
    DisplayState ds1, *ds = &ds1;
    int x1, xc, yc, offset, bottom = -1;

    if (s->offset == 0) {
        s->offset_top = s->y_disp = s->x_disp[0] = s->x_disp[1] = 0;
    }

    /* if the cursor is before the top of the display zone, we must
       resync backward */
    if (s->offset < s->offset_top) {
        s->offset_top = s->mode->backward_offset(s, s->offset);
        ///XXXX probably too strong, should keep cursor close to top
        //s->y_disp = 0;  //@@@?
    }

    if (s->display_invalid) {
        /* invalidate the line shadow buffer */
        qe_free(&s->line_shadow);
        s->shadow_nb_lines = 0;
        s->display_invalid = 0;
    }

    /* find cursor position with the current x_disp & y_disp and
       update y_disp so that we display only the needed lines */
    /* XXX: should update x_disp, y_disp to bring the cursor closest
       to the center, top or bottom of the screen depending on
       window movement */
    memset(m, 0, sizeof(*m));
    m->offsetc = s->offset;
    m->xc = m->yc = NO_CURSOR;
    display_init(ds, s, DISP_CURSOR_SCREEN, cursor_func, m);
    offset = s->offset_top;
    for (;;) {
        if (ds->y <= 0) {
            s->offset_top = offset;
            s->y_disp = ds->y;
        }
        offset = s->mode->display_line(s, ds, offset);
        s->offset_bottom = offset;
        if (offset < 0 || ds->y >= s->height || m->xc != NO_CURSOR)
            break;
    }
    display_close(ds);

    //printf("cursor: xc=%d yc=%d linec=%d\n", m->xc, m->yc, m->linec);
    if (m->xc == NO_CURSOR) {
        /* if no cursor found then we compute offset_top so that we
           have a chance to find the cursor in a small amount of time */
        display_init(ds, s, DISP_CURSOR_SCREEN, cursor_func, m);
        ds->y = 0;
        offset = s->mode->backward_offset(s, s->offset);
        bottom = s->mode->display_line(s, ds, offset);
        if (m->xc == NO_CURSOR) {
            /* XXX: should not happen */
            put_error(NULL, "ERROR: cursor not found");
            ds->y = 0;
        } else {
            ds->y = m->yc + m->cursor_height;
        }

        while (ds->y < s->height && offset > 0) {
            offset = eb_prev(s->b, offset);
            offset = s->mode->backward_offset(s, offset);
            bottom = s->mode->display_line(s, ds, offset);
        }
        s->offset_top = offset;
        s->offset_bottom = bottom;
        /* adjust y_disp so that the cursor is at the bottom of the screen */
        s->y_disp = min(s->height - ds->y, 0);
        display_close(ds);
    } else {
        yc = m->yc;
        if (yc < 0) {
            s->y_disp += -yc;
        } else
        if ((yc + m->cursor_height) > s->height) {
            s->y_disp += s->height - (yc + m->cursor_height);
        }
    }

    /* update x cursor position if needed. Note that we distinguish
       between rtl and ltr margins. We try to have x_disp == 0 as much
       as possible */
    if (s->wrap == WRAP_TRUNCATE) {
#if 1  //@@@?
        //put_status(s, "|xc=%d x_disp+%d", m->xc, s->x_disp[m->basec]);
        if (m->xc != NO_CURSOR) {
            xc = m->xc;
            x1 = xc - s->x_disp[m->basec];
            // Do not snap x_disp to 0 to allow scroll_left()
            //if (x1 >= 0 && x1 < ds->width) {
            //    s->x_disp[m->basec] = 0;
            //} else
            if (xc < 0) {
                /* XXX: refering to ds after display_close(ds) */
                if (x1 >= 0 && x1 < ds->width) {
                    /* snap back to left margin */
                    s->x_disp[m->basec] = 0;
                } else {
                    /* XXX: should center screen horizontally? */
                    /* XXX: maybe scroll horizontally by a quarter screen? */
                    s->x_disp[m->basec] += -xc;
                }
            } else
            if (xc + m->cursor_width >= ds->width) {
                /* XXX: maybe scroll horizontally by a quarter screen? */
                s->x_disp[m->basec] += ds->width - (xc + m->cursor_width);
            }
        }
#else
        xc = m->xc;
        x1 = xc - s->x_disp[m->basec];
        if (x1 >= 0 && x1 < ds->width - ds->eol_width) {
            s->x_disp[m->basec] = 0;
        } else
        if (xc < 0) {
            s->x_disp[m->basec] -= xc;
        } else
        if (xc >= ds->width) {
            s->x_disp[m->basec] += ds->width - xc - ds->eol_width;
        }
#endif
    } else {
        s->x_disp[0] = 0;
        s->x_disp[1] = 0;
    }

    /* now we can display the text and get the real cursor position !  */

    m->offsetc = s->offset;
    m->xc = m->yc = NO_CURSOR;
    display_init(ds, s, DISP_PRINT, cursor_func, m);
    display1(ds);
    /* display the remaining region */
    if (ds->y < s->height) {
        QEStyleDef default_style;
        get_style(s, &default_style, QE_STYLE_DEFAULT);
        fill_rectangle(s->screen, s->xleft, s->ytop + ds->y,
                       s->width, s->height - ds->y,
                       default_style.bg_color);
        /* do not forget to erase the line shadow  */
        memset(&s->line_shadow[ds->line_num], 0xff,
               (s->shadow_nb_lines - ds->line_num) * sizeof(QELineShadow));
    }
    display_close(ds);

    xc = m->xc;
    yc = m->yc;

    if (xc != NO_CURSOR && yc != NO_CURSOR
    &&  s->qe_state->active_window == s) {
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
void call_func(CmdSig sig, CmdProto func, qe__unused__ int nb_args,
               CmdArg *args, qe__unused__ unsigned char *args_type)
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
int parse_arg(const char **pp, unsigned char *argtype,
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

int qe_get_prototype(CmdDef *d, char *buf, int size)
{
    buf_t outbuf, *out;
    const char *r;
    const char *sep = "";

    out = buf_init(&outbuf, buf, size);

    /* construct argument type list */
    r = d->name + strlen(d->name) + 1;
    if (*r == '*') {
        r++;    /* buffer modification indicator */
    }

    for (;;) {
        unsigned char arg_type;
        char completion[32];
        char history[32];
        int ret;

        ret = parse_arg(&r, &arg_type, NULL, 0,
                        completion, sizeof(completion),
                        history, sizeof(history));
        if (ret <= 0)
            break;

        /* pseudo arguments: skip them */
        switch (arg_type & CMD_ARG_TYPE_MASK) {
        case CMD_ARG_INT:
            buf_printf(out, "%sint ", sep);
            break;
        case CMD_ARG_STRING:
            buf_printf(out, "%sstring ", sep);
            break;
        case CMD_ARG_WINDOW:
        case CMD_ARG_INTVAL:
        case CMD_ARG_STRINGVAL:
        default:
            continue;
        }
        sep = ", ";
        if (arg_type & CMD_ARG_USE_KEY) {
            buf_puts(out, "key");
        } else
        if (arg_type & CMD_ARG_USE_ARGVAL) {
            buf_puts(out, "argval");
        } else {
            buf_puts(out, *history ? history : completion);
        }
    }
    return out->len;
}

static void arg_edit_cb(void *opaque, char *str);
static void parse_args(ExecCmdState *es);
static void free_cmd(ExecCmdState **esp);

void exec_command(EditState *s, CmdDef *d, int argval, int key)
{
    ExecCmdState *es;
    const char *argdesc;

    if (qe_state.trace_buffer && qe_state.trace_buffer != s->b)
        eb_trace_bytes(d->name, -1, EB_TRACE_COMMAND);

    argdesc = d->name + strlen(d->name) + 1;
    if (*argdesc == '*') {
        argdesc++;
        if (s->b->flags & BF_READONLY) {
            put_status(s, "Buffer is read only");
            return;
        }
    }

    es = qe_mallocz(ExecCmdState);
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
    int elapsed_time;

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
                get_default_path(s->b, s->offset, def_input, sizeof(def_input));
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

    qs->cmd_start_time = get_clock_ms();

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

    elapsed_time = get_clock_ms() - qs->cmd_start_time;
    qs->cmd_start_time += elapsed_time;
    if (elapsed_time >= 100)
        put_status(s, "|%s: %dms", d->name, elapsed_time);

    qs->last_cmd_func = qs->this_cmd_func;
 fail:
    free_cmd(&es);
}

static void free_cmd(ExecCmdState **esp)
{
    if (*esp) {
        ExecCmdState *es = *esp;
        int i;

        /* free allocated parameters */
        for (i = 0; i < es->nb_args; i++) {
            switch (es->args_type[i]) {
            case CMD_ARG_STRING:
                qe_free((char **)&es->args[i].p);
                break;
            }
        }
        qe_free(esp);
    }
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
        free_cmd(&es);
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
        put_status(s, "No command %s", cmd);
    }
}

void window_display(EditState *s)
{
    CSSRect rect;

    /* set the clipping rectangle to the whole window */
    /* XXX: should clip out popup windows */
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
    int start_time, elapsed_time;

    start_time = get_clock_ms();

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

    elapsed_time = get_clock_ms() - start_time;
    if (elapsed_time >= 100)
        put_status(s, "|edit_display: %dms", elapsed_time);

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
        put_status(s, "Cannot execute macro while defining one");
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

void do_execute_macro_keys(qe__unused__ EditState *s, const char *keys)
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
    int namelen, size;
    char *buf;

    namelen = strlen(name);
    size = namelen + 1 + 2 + strlen(keys) + 2;
    buf = qe_malloc_array(char, size);

    // XXX: should special case "last-kbd-macro"

    /* CG: should parse macro keys to an array and pass index
     * to do_execute_macro.
     */
    snprintf(buf, size, "%s%cS{%s}", name, 0, keys);

    def = qe_find_cmd(name);
    if (def && def->action.ESs == do_execute_macro_keys) {
        /* redefininig a macro */
        /* XXX: freeing the current macro definition may cause a crash if it
         * is currently executing.
         */
        qe_free((char **)&def->name);
        def->name = buf;
    } else {
        def = qe_mallocz_array(CmdDef, 2);
        def->key = def->alt_key = KEY_NONE;
        def->name = buf;
        def->sig = CMD_ESs;
        def->val = 0;
        def->action.ESs = do_execute_macro_keys;
        def[1].val = 1;  /* flag as allocated for free-all */
        qe_register_cmd_table(def, NULL);
    }
    if (key_bind && *key_bind) {
        do_set_key(s, key_bind, name, 0);
    }
}

static void qe_save_macro(EditState *s, CmdDef *def, EditBuffer *b)
{
    QEmacsState *qs = s->qe_state;
    char buf[32];
    buf_t outbuf, *out;
    int i;
    const char *name = "last-kbd-macro";

    if (def)
        name = def->name;

    eb_printf(b, "define_kbd_macro(\"%s\", \"", name);

    if (def) {
        const char *keys = def->name;
        keys += strlen(keys) + 1 + 2;
        while (keys[1]) {
            eb_putc(b, utf8_decode(&keys));
        }
    } else {
        for (i = 0; i < qs->nb_macro_keys; i++) {
            out = buf_init(&outbuf, buf, sizeof(buf));
            buf_put_key(out, qs->macro_keys[i]);
            eb_puts(b, out->buf);
        }
    }
    eb_puts(b, "\", \"\");\n");
}

void qe_save_macros(EditState *s, EditBuffer *b)
{
    QEmacsState *qs = &qe_state;
    CmdDef *d;

    eb_puts(b, "// macros:\n");
    qe_save_macro(s, NULL, b);

    /* Enumerate defined macros */
    for (d = qs->first_cmd; d != NULL; d = d->action.next) {
        for (; d->name != NULL; d++) {
            if (d->action.ESs == do_execute_macro_keys)
                qe_save_macro(s, d, b);
        }
    }
    eb_putc(b, '\n');
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

void do_numeric_argument(qe__unused__ EditState *s)
{
    /* nothing is done there (see qe_key_process()) */
}

typedef struct QEKeyContext {
    int argval;
    int noargval;
    int sign;
    int is_numeric_arg;
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
    c->is_numeric_arg = 0;
    c->is_escape = 0;
    c->noargval = 1;
    c->argval = NO_ARG;
    c->sign = 1;
    c->nb_keys = 0;
    c->buf[0] = '\0';
}

KeyDef *qe_find_binding(unsigned int *keys, int nb_keys, KeyDef *kd)
{
    for (; kd != NULL; kd = kd->next) {
        if (kd->nb_keys >= nb_keys
        &&  !memcmp(kd->keys, keys, nb_keys * sizeof(keys[0]))) {
            break;
        }
    }
    return kd;
}

KeyDef *qe_find_current_binding(unsigned int *keys, int nb_keys, ModeDef *m)
{
    QEmacsState *qs = &qe_state;

    for (; m; m = m->fallback) {
        KeyDef *kd = qe_find_binding(keys, nb_keys, m->first_key);
        if (kd != NULL)
            return kd;
    }
    return qe_find_binding(keys, nb_keys, qs->first_key);
}

static void qe_key_process(int key)
{
    QEmacsState *qs = &qe_state;
    QEKeyContext *c = &key_ctx;
    EditState *s;
    KeyDef *kd;
    CmdDef *d;
    char buf1[128];
    buf_t outbuf, *out;

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
    kd = qe_find_current_binding(c->keys, c->nb_keys, s->mode);
    if (!kd) {
        /* no key found */
        unsigned int key_default = KEY_DEFAULT;

        if (c->nb_keys == 1) {
            if (!KEY_IS_SPECIAL(key) && !KEY_IS_CONTROL(key)) {
                if (c->is_numeric_arg) {
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
                kd = qe_find_current_binding(&key_default, 1, s->mode);
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

        out = buf_init(&outbuf, buf1, sizeof(buf1));
        buf_put_keys(out, c->keys, c->nb_keys);
        put_status(s, "No command on %s", buf1);
        c->describe_key = 0;
        qe_key_init(c);
        dpy_flush(&global_screen);
        return;
    } else
    if (c->nb_keys == kd->nb_keys) {
    exec_cmd:
        d = kd->cmd;
        if (d->action.ES == do_numeric_argument && !c->describe_key) {
            /* special handling for numeric argument */
            /* CG: XXX: should display value of numeric argument */
            c->is_numeric_arg = 1;
            if (key == KEY_META('-')) {
                c->sign = -c->sign;
                if (c->noargval == 1)
                    c->noargval = 4;
            } else {
                c->noargval = c->noargval * 4;
            }
            c->nb_keys = 0;
        } else {
            if (c->is_numeric_arg) {
                if (c->argval == NO_ARG)
                    c->argval = c->noargval;
                c->argval *= c->sign;
            }
            if (c->describe_key) {
                out = buf_init(&outbuf, buf1, sizeof(buf1));
                buf_put_keys(out, c->keys, c->nb_keys);
                put_status(s, "%s runs the command %s", buf1, d->name);
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
        int len;

        len = strlen(c->buf);
        if (len >= 1)
            c->buf[len-1] = ' ';
        /* Should print argument if any in a more readable way */
        out = buf_attach(&outbuf, c->buf, sizeof(c->buf), len);
        buf_put_key(out, key);
        buf_put_byte(out, '-');
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

/* XXX: should take va_list */
static void eb_format_message(QEmacsState *qs, const char *bufname,
                              const char *message)
{
    char header[128];
    EditBuffer *eb;
    buf_t outbuf, *out;

    out = buf_init(&outbuf, header, sizeof(header));

    if (qs->ec.filename)
        buf_printf(out, "%s:%d: ", qs->ec.filename, qs->ec.lineno);

    if (qs->ec.function)
        buf_printf(out, "%s: ", qs->ec.function);

    eb = eb_find_new(bufname, BF_UTF8);
    if (eb) {
        eb_printf(eb, "%s%s\n", header, message);
    } else {
        fprintf(stderr, "%s%s\n", header, message);
    }
}

void put_error(qe__unused__ EditState *s, const char *fmt, ...)
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

void put_status(qe__unused__ EditState *s, const char *fmt, ...)
{
    /* CG: s is not used and may be NULL! */
    QEmacsState *qs = &qe_state;
    char buf[MAX_SCREEN_WIDTH];
    const char *p;
    va_list ap;
    int silent = 0;
    int diag = 0;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    for (p = buf;; p++) {
        if (*p == '|') {
            diag = 1;
        } else
        if (*p == '~') {
            silent = 1;
        } else {
            break;
        }
    }

    if (!qs->screen->dpy.dpy_probe) {
        eb_format_message(qs, "*errors*", p);
    } else {
        if (diag) {
            if (!strequal(p, qs->diag_shadow)) {
                /* right align display and overwrite last diag message */
                int w = strlen(qs->diag_shadow);
                w = snprintf(qs->diag_shadow, sizeof(qs->diag_shadow),
                             "%*s", w, p) + 1;
                w *= get_glyph_width(qs->screen, NULL, QE_STYLE_STATUS, '0');
                print_at_byte(qs->screen,
                              qs->screen->width - w,
                              qs->screen->height - qs->status_height,
                              qs->screen->width - w, qs->status_height,
                              qs->diag_shadow, QE_STYLE_STATUS);
                pstrcpy(qs->diag_shadow, sizeof(qs->diag_shadow), p);
            }
        } else {
            if (!strequal(p, qs->status_shadow)) {
                print_at_byte(qs->screen,
                              0, qs->screen->height - qs->status_height,
                              qs->screen->width, qs->status_height,
                              p, QE_STYLE_STATUS);
                pstrcpy(qs->status_shadow, sizeof(qs->status_shadow), p);
            }
        }
        skip_spaces(&p);
        if (!silent && *buf)
            eb_format_message(qs, "*messages*", buf);
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
    EditBuffer *b0 = s->b;
    EditState *e;
    ModeDef *mode;

    /* remove region hilite */
    s->region_style = 0;

    if (b == b0)
        return;

    if (b0) {
        /* Save generic mode data to the buffer */
        generic_save_window_data(s);

        /* Close the mode */
        edit_set_mode(s, NULL);
    }

    /* now we can switch ! */
    s->b = b;

    /* Delete transient buffer if no other window displays it */
    if (b0) {
        if ((b0->flags & BF_TRANSIENT) && !eb_find_window(b0, NULL)) {
            eb_free(&b0);
        } else {
            /* save buffer for predict_switch_to_buffer */
            s->last_buffer = b0;
        }
    }

    if (b) {
        if (b->saved_data) {
            /* Restore window mode and data from buffer saved data */
            memcpy(s, b->saved_data, SAVED_DATA_SIZE);
            s->offset = min(s->offset, b->total_size);
            s->offset_top = min(s->offset_top, b->total_size);
            mode = b->saved_mode;
        } else {
            /* Try to get window mode and data from another window */
            e = eb_find_window(b, s);
            if (e) {
                memcpy(s, e, SAVED_DATA_SIZE);
                mode = e->mode;
            } else {
                memset(s, 0, SAVED_DATA_SIZE);
                mode = b->default_mode;
                /* <default> default values */
                s->insert = 1;
                s->indent_size = s->qe_state->default_tab_width;
                s->default_style = QE_STYLE_DEFAULT;
                s->wrap = WRAP_LINE;
            }
        }
        /* validate the mode */
        if (!mode)
            mode = b->default_mode;
        if (!mode) {
            /* default mode */
            mode = &text_mode;
        }
        /* initialize the mode */
        edit_set_mode(s, mode);
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
    EditState *s, *e;

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
    for (e = qs->first_window; e != NULL; e = e->next_window) {
        if (e->y1 > s->y1 || (e->y1 == s->y1 && e->x1 > s->x1))
            break;
    }
    edit_attach(s, e);

    /* restore saved window settings, set mode */
    switch_to_buffer(s, b);
    return s;
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

/* move a window before another one */
void edit_attach(EditState *s, EditState *e)
{
    QEmacsState *qs = s->qe_state;
    EditState *active_window = qs->active_window;
    EditState **ep;

    if (s != e) {
        edit_detach(s);

        for (ep = &qs->first_window; *ep; ep = &(*ep)->next_window) {
            if (*ep == e)
                break;
        }
        s->next_window = *ep;
        *ep = s;
        qs->active_window = active_window ? active_window : s;
    }
}

/* Close the edit window.
 * Save the window state to the buffer for later retrieval.
 * If it is active, find another window to activate.
 */
void edit_close(EditState **sp)
{
    if (*sp) {
        EditState *s = *sp;

        /* save current state for later window reattachment */
        switch_to_buffer(s, NULL);
        edit_detach(s);
        qe_free_mode_data(s->mode_data);
        qe_free(&s->prompt);
        qe_free(&s->line_shadow);
        qe_free(sp);
    }
}

static const char *file_completion_ignore_extensions = {
    "|bak|bin|obj|dll|exe|o|so|a|gz|tgz|bz2|bzip2|xz"
    "|cma|cmi|cmo|cmt|cmti|cmx"
    "|"
};

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
        canonicalize_absolute_path(cp->s, filename, sizeof(filename), cp->current);
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
        add_string(&cp->cs, filename, 0);
    }

    find_file_close(&ffst);
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

    p = qe_mallocz(CompletionEntry);
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
    cp->len = eb_get_contents(s->b, cp->current, sizeof(cp->current));
}

/* XXX: should have a globbing option */
void complete_test(CompleteState *cp, const char *str)
{
    QEmacsState *qs = &qe_state;
    int fuzzy = 0;

    if (memcmp(str, cp->current, cp->len)) {
        if (!qe_memicmp(str, cp->current, cp->len))
            fuzzy = 1;
        else
        if (qs->fuzzy_search && strmem(str, cp->current, cp->len))
            fuzzy = 2;
        else
            return;
    }
    add_string(&cp->cs, str, fuzzy);
}

static int completion_sort_func(const void *p1, const void *p2)
{
    StringItem *item1 = *(StringItem **)p1;
    StringItem *item2 = *(StringItem **)p2;

    /* Group items by group order */
    if (item1->group != item2->group)
        return item1->group - item2->group;
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

void do_completion(EditState *s, int type)
{
    QEmacsState *qs = s->qe_state;
    int count, i, match_len, c;
    CompleteState cs;
    StringItem **outputs;
    EditState *e;
    EditBuffer *b;
    int w, h, h1, w1;

    if (!completion_function)
        return;

    /* Remove highlighted selection. */
    // XXX: Should complete based on point position,
    //      not necessarily full minibuffer contents
    do_delete_selection(s);

    /* check completion window */
    check_window(&completion_popup_window);
    if (completion_popup_window
    &&  type == COMPLETION_TAB
    &&  qs->last_cmd_func == qs->this_cmd_func) {
        /* toggle cpmpletion popup on TAB */
        edit_close(&completion_popup_window);
        do_refresh(s);
        return;
    }

    complete_start(s, &cs);
    (*completion_function)(&cs);
    count = cs.cs.nb_items;
    outputs = cs.cs.items;
#if 0
    printf("count=%d\n", count);
    for (i = 0; i < count; i++)
        printf("out[%d]=%s\n", i, outputs[i]->str);
#endif
    /* compute the longest match len */
    match_len = cs.len;

    if (count > 0) {
        for (; (c = outputs[0]->str[match_len]) != '\0'; match_len++) {
            for (i = 1; i < count; i++) {
                if (outputs[i]->str[match_len] != c)
                    break;
            }
            if (i < count)
                break;
        }
        /* Clip incomplete UTF-8 character before match_len */
        for (i = cs.len; i < match_len; ) {
            int clen = utf8_length[(unsigned char)outputs[0]->str[i]];
            if (i + clen > match_len)
                match_len = i;
            else
                i += clen;
        }
    }
    if (match_len > cs.len) {
        /* add the possible chars */
        // XXX: potential utf-8 issue?
        eb_write(s->b, 0, outputs[0]->str, match_len);
        s->offset = match_len;
        if (type == COMPLETION_OTHER) {
            do_mark_region(s, match_len, cs.len);
        }
    } else {
        if (count > 1) {
            /* if more than one match, then display them in a new popup
               buffer */
            if (!completion_popup_window) {
                b = eb_new("*completion*", 
                           BF_SYSTEM | BF_UTF8 | BF_TRANSIENT | BF_STYLE1);
                b->default_mode = &list_mode;
                w1 = qs->screen->width;
                h1 = qs->screen->height - qs->status_height;
                w = (w1 * 3) / 4;
                h = (h1 * 3) / 4;
                e = edit_new(b, (w1 - w) / 2, (h1 - h) / 2, w, h, WF_POPUP);
                do_refresh(e);
                completion_popup_window = e;
            }
        } else
        if (count == 0 || type != COMPLETION_OTHER) {
            /* close the popup when minibuf contents matches nothing */
            edit_close(&completion_popup_window);
            do_refresh(s);
        }
    }
    if (completion_popup_window) {
        /* modify the list with the current matches */
        e = completion_popup_window;
        b = e->b;
        qsort(outputs, count, sizeof(StringItem *), completion_sort_func);
        b->flags &= ~BF_READONLY;
        eb_delete(b, 0, b->total_size);
        for (i = 0; i < count; i++) {
            eb_printf(b, " %s", outputs[i]->str);
            if (i != count - 1)
                eb_printf(b, "\n");
        }
        b->flags |= BF_READONLY;
        e->mouse_force_highlight = 1;
        e->force_highlight = 1;
        e->offset = 0;
    }
    complete_end(&cs);
}

static int eb_match_string_reverse(EditBuffer *b, int offset, const char *str,
                                   int *offsetp)
{
    int len = strlen(str);

    while (len > 0) {
        if (offset <= 0 || eb_prevc(b, offset, &offset) != str[--len])
            return 0;
    }
    *offsetp = offset;
    return 1;
}

void do_electric_filename(EditState *s, int key)
{
    int c, offset, stop;

    if (completion_function == file_completion) {
        stop = s->offset;
        c = eb_prevc(s->b, s->offset, &offset);
        if (c == '/') {
            if (eb_match_string_reverse(s->b, offset, "http:", &stop)
            ||  eb_match_string_reverse(s->b, offset, "https:", &stop)
            ||  eb_match_string_reverse(s->b, offset, "ftp:", &stop)) {
		    /* nothing, stop already updated */
	    }
            eb_delete(s->b, 0, stop);
        }
    }
    do_char(s, key, 1);
}

/* space does completion only if a completion method is defined */
void do_completion_space(EditState *s)
{
    QEmacsState *qs = s->qe_state;

    if (!completion_function) {
        do_char(s, ' ', 1);
    } else
    if (completion_popup_window && qs->last_cmd_func == qs->this_cmd_func) {
        /* page through the list */
        // XXX: should close the popup at the bottom of the list
        do_scroll_up_down(completion_popup_window, 2);
    } else {
        do_completion(s, COMPLETION_SPACE);
    }
}

static void do_minibuffer_char(EditState *s, int key, int argval)
{
    do_char(s, key, argval);
    if (completion_popup_window) {
        /* automatic filtering of completion list */
        // XXX: should prevent auto-completion
        do_completion(s, COMPLETION_OTHER);
    }
}

/* scroll in completion popup */
void minibuf_complete_scroll_up_down(qe__unused__ EditState *s, int dir)
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
    /* XXX: should insert utf-8? */
    len = strlen(str);
    eb_write(s->b, 0, str, len);
    s->offset = len;
}

/* CG: should use buffer of responses */
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
        set_string(hist, hist->nb_items - 1, buf, 0);
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
    StringArray *hist = minibuffer_history;
    static void (*cb)(void *opaque, char *buf);
    static void *opaque;
    char buf[4096], *retstr;
    EditState *cw = completion_popup_window;

    /* if completion is activated, then select current file only if
       the selection is highlighted */
    if (cw && cw->force_highlight) {
        int offset, len;

        len = eb_fgets(cw->b, buf, sizeof(buf), list_get_offset(cw), &offset);
        buf[len] = '\0';   /* strip the trailing newline if any */
        if (len > 0) {
            set_minibuffer_str(s, buf + 1);
        }
    }

    /* remove completion popup if present */
    /* CG: assuming completion_popup_window != s */
    if (cw) {
        edit_close(&completion_popup_window);
        cw = NULL;
        do_refresh(s);
    }

    eb_get_contents(s->b, buf, sizeof(buf));
    if (hist && hist->nb_items > 0) {
        /* if null string, do not insert in history */
        hist->nb_items--;
        qe_free(&hist->items[hist->nb_items]);
        if (buf[0] != '\0')
            add_string(hist, buf, 0);
    }

    s->b->flags |= BF_TRANSIENT;
    /* Close the minibuffer window */
    edit_close(&s);

    /* restore active window */
    qs->active_window = check_window(&minibuffer_saved_active);
    minibuffer_saved_active = NULL;

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
    b->default_mode = &minibuffer_mode;

    s = edit_new(b, 0, qs->screen->height - qs->status_height,
                 qs->screen->width, qs->status_height, 0);
    /* Should insert at end of window list */
    /* XXX: should qe_free previous value? */
    s->prompt = qe_strdup(prompt);
    s->minibuf = 1;
    s->bidir = 0;
    s->default_style = QE_STYLE_MINIBUF;
    s->wrap = WRAP_TRUNCATE;

    /* add default input */
    if (input) {
        /* XXX: should insert utf-8? */
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
        add_string(hist, "", 0);
    }
}

void minibuffer_init(void)
{
    /* minibuf mode inherits from text mode */
    memcpy(&minibuffer_mode, &text_mode, sizeof(ModeDef));
    minibuffer_mode.name = "minibuffer";
    minibuffer_mode.mode_name = NULL;
    minibuffer_mode.mode_probe = NULL;
    minibuffer_mode.scroll_up_down = minibuf_complete_scroll_up_down;
    qe_register_mode(&minibuffer_mode, MODEF_NOCMD | MODEF_VIEW);
    qe_register_cmd_table(minibuffer_commands, &minibuffer_mode);
}

/* popup paging mode */

static ModeDef popup_mode;

/* XXX: incorrect to save it. Should use window target member */
static EditState *popup_saved_active;

/* Verify that window still exists, return argument or NULL,
 * update handle if window is invalid.
 */
EditState *check_window(EditState **sp)
{
    QEmacsState *qs = &qe_state;
    EditState *e;

    for (e = qs->first_window; e != NULL; e = e->next_window) {
        if (e == *sp)
            return e;
    }
    return *sp = NULL;
}

void do_popup_exit(EditState *s)
{
    QEmacsState *qs = s->qe_state;

    /* XXX: only do this for a popup? */
    s->b->flags |= BF_TRANSIENT;
    edit_close(&s);

    qs->active_window = check_window(&popup_saved_active);
    popup_saved_active = NULL;

    do_refresh(qs->active_window);
}

/* show a popup on a readonly buffer */
EditState *show_popup(EditState *s, EditBuffer *b)
{
    QEmacsState *qs = &qe_state;
    EditState *e;
    int w, h, w1, h1;

    /* Prevent recursion */
    if (s && s->b == b)
        return s;

    /* XXX: generic function to open popup ? */
    w1 = qs->screen->width;
    h1 = qs->screen->height - qs->status_height;
    w = (w1 * 4) / 5;
    h = (h1 * 3) / 4;

    e = edit_new(b, (w1 - w) / 2, (h1 - h) / 2, w, h, WF_POPUP);
    edit_set_mode(e, &popup_mode);
    e->wrap = WRAP_TRUNCATE;

    popup_saved_active = qs->active_window;
    qs->active_window = e;
    do_refresh(e);
    return e;
}

static void popup_init(void)
{
    /* popup mode inherits from text mode */
    memcpy(&popup_mode, &text_mode, sizeof(ModeDef));
    popup_mode.name = "popup";
    popup_mode.mode_name = NULL;
    popup_mode.mode_probe = NULL;
    qe_register_mode(&popup_mode, MODEF_VIEW);
    qe_register_cmd_table(popup_commands, &popup_mode);
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
            edit_close(&e);
        } else
        if (e->x1 < width) {
            e->x1 = width;
        }
    }

    e_new = edit_new(b, 0, 0, width, qs->height - qs->status_height,
                     flags | WF_POPLEFT | WF_RSEPARATOR);
    e_new->wrap = WRAP_TRUNCATE;
    do_refresh(e_new);
    return e_new;
}

/* return a window on the side of window 's' */
EditState *find_window(EditState *s, int key, EditState *def)
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
    return def;
}

void do_find_window(EditState *s, int key)
{
    s->qe_state->active_window = find_window(s, key, s);
}
#endif

/* Give a good guess to the user for the next buffer */
static EditBuffer *predict_switch_to_buffer(EditState *s)
{
    QEmacsState *qs = s->qe_state;
    EditBuffer *b;

    /* try and switch to the last buffer attached to the window */
    b = check_buffer(&s->last_buffer);
    if (b)
        return b;

    /* else try and switch to a buffer not shown in any window */
    for (b = qs->first_buffer; b != NULL; b = b->next) {
        if (!(b->flags & BF_SYSTEM)) {
            if (!eb_find_window(b, NULL))
                return b;
        }
    }
    /* otherwise select current buffer. */
    return s->b;
}

void do_switch_to_buffer(EditState *s, const char *bufname)
{
    EditBuffer *b;

    /* XXX: Default buffer charset should be selectable */
    b = eb_find_new(bufname, BF_SAVELOG | BF_UTF8);
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

static void kill_buffer_confirm_cb(void *opaque, char *reply)
{
    int yes_replied;

    if (!reply)
        return;
    yes_replied = strequal(reply, "yes");
    qe_free(&reply);
    if (!yes_replied)
        return;
    qe_kill_buffer(opaque);
}

void do_kill_buffer(EditState *s, const char *bufname, int force)
{
    char buf[1024];
    EditBuffer *b;

    b = eb_find(bufname);
    if (!b) {
        put_status(s, "No buffer %s", bufname);
    } else {
        /* if modified and associated to a filename, then ask */
        if (!force && b->modified && b->filename[0] != '\0') {
            snprintf(buf, sizeof(buf),
                     "Buffer %s modified; kill anyway? (yes or no) ", bufname);
            minibuffer_edit(NULL, buf, NULL, NULL,
                            kill_buffer_confirm_cb, b);
        } else {
            qe_kill_buffer(b);
        }
    }
}

void qe_kill_buffer(EditBuffer *b)
{
    QEmacsState *qs = &qe_state;
    EditState *e;
    EditBuffer *b1 = NULL;

    if (!b)
        return;

    /* Check for windows showing the buffer:
     * - Emacs makes any window showing the killed buffer switch to
     *   another buffer.
     * - An alternative is to delete windows showing the buffer, but we
     *   cannot delete the main window, so switch to the scratch buffer.
     */
    for (e = qs->first_window; e != NULL; e = e->next_window) {
        if (e->last_buffer == b) {
            e->last_buffer = NULL;
        }
        if (e->b == b) {
            if (!b1) {
                /* find a new buffer to switch to */
                for (b1 = qs->first_buffer; b1 != NULL; b1 = b1->next) {
                    if (b1 != b && !(b1->flags & BF_SYSTEM))
                        break;
                }
                if (!b1) {
                    b1 = eb_new("*scratch*", BF_SAVELOG | BF_UTF8);
                }
            }
            switch_to_buffer(e, b1);
        }
    }

    if (b->flags & BF_SYSTEM) {
        int i;
        for (i = 0; i < NB_YANK_BUFFERS; i++) {
            if (qs->yank_buffers[i] == b)
                qs->yank_buffers[i] = NULL;
        }
    }

    /* now we can safely delete buffer */
    eb_free(&b);

    do_refresh(qs->first_window);
}

/* return TRUE if absolute path. works for files and URLs */
static int is_abs_path(const char *path)
{
    size_t prefix;

    if (*path == '/')
        return 1;

    /* Accept as absolute a drive or protocol followed by `/` */
    prefix = strcspn(path, "/:");
    if (path[prefix] == ':' && path[prefix + 1] == '/')
        return 1;
    
    return 0;
}

/* canonicalize the path for a given window and make it absolute */
void canonicalize_absolute_path(EditState *s, char *buf, int buf_size, const char *path1)
{
    return canonicalize_absolute_buffer_path(s ? s->b : NULL, s ? s->offset : 0, buf, buf_size, path1);
}

void canonicalize_absolute_buffer_path(EditBuffer *b, int offset, char *buf, int buf_size, const char *path1)
{
    char cwd[MAX_FILENAME_SIZE];
    char path[MAX_FILENAME_SIZE];
    char *homedir;

    if (!is_abs_path(path1)) {
        if (*path1 == '~') {
            if (path1[1] == '\0' || path1[1] == '/') {
                homedir = getenv("HOME");
                if (homedir) {
                    pstrcpy(path, sizeof(path), homedir);
#ifdef CONFIG_WIN32
                    path_win_to_unix(path);
#endif
                    remove_slash(path);
                    pstrcat(path, sizeof(path), path1 + 1);
                    path1 = path;
                }
            } else {
                /* CG: should get info from getpwnam */
#ifdef CONFIG_DARWIN
                pstrcpy(path, sizeof(path), "/Users/");
#else
                pstrcpy(path, sizeof(path), "/home/");
#endif
                pstrcat(path, sizeof(path), path1 + 1);
                path1 = path;
            }
        } else {
            /* CG: not sufficient for windows drives */
            if (!b || !get_default_path(b, offset, cwd, sizeof(cwd))) {
                getcwd(cwd, sizeof(cwd));
#ifdef CONFIG_WIN32
                path_win_to_unix(cwd);
#endif
            }
            makepath(path, sizeof(path), cwd, path1);
            path1 = path;
        }
    }
    canonicalize_path(buf, buf_size, path1);
}

/* compute default path for find/save buffer */
char *get_default_path(EditBuffer *b, int offset, char *buf, int buf_size)
{
    char buf1[MAX_FILENAME_SIZE];
    const char *filename;

    /* dispatch to mode specific handler if any */
    if (b->default_mode
    &&  b->default_mode->get_default_path
    &&  b->default_mode->get_default_path(b, offset, buf, buf_size)) {
        return buf;
    }

    if ((b->flags & BF_SYSTEM)
    ||  b->name[0] == '*'
    ||  b->filename[0] == '\0') {
        filename = "a";
    } else {
        filename = b->filename;
    }
    /* XXX: should just retrieve the current directory */
    canonicalize_absolute_path(NULL, buf1, sizeof(buf1), filename);
    splitpath(buf, buf_size, NULL, 0, buf1);
    return buf;
}

/* should have: rawbuf[len] == '\0' */
static int probe_mode(EditState *s, EditBuffer *b,
                      ModeDef **modes, int nb_modes,
                      int *scores, int min_score,
                      const char *filename, int st_mode, long total_size,
                      const uint8_t *rawbuf, int len,
                      QECharset *charset, EOLType eol_type)
{
    u8 buf[4097];
    QEmacsState *qs = s->qe_state;
    char fname[MAX_FILENAME_SIZE];
    ModeDef *m;
    ModeProbeData probe_data;
    int found_modes;
    const uint8_t *p;

    if (!modes || !scores || nb_modes < 1)
        return 0;

    found_modes = 0;
    *modes = NULL;
    *scores = 0;

    probe_data.b = b;
    probe_data.buf = buf;
    probe_data.buf_size = len;
    probe_data.real_filename = filename;
    probe_data.st_mode = st_mode;
    probe_data.total_size = total_size;
    probe_data.filename = reduce_filename(fname, sizeof(fname),
                                          get_basename(filename));
    /* CG: should pass EditState? QEmacsState ? */

    /* XXX: Should use eb_get_range_contents to deal with charset and
     * eol_type instead of hand coding this conversion */
    probe_data.eol_type = eol_type;
    probe_data.charset = charset;
    charset_decode_init(&probe_data.charset_state, charset, eol_type);

    /* XXX: Should handle eol_type and transcode \r and \r\n */
    if (charset == &charset_utf8
    ||  charset == &charset_raw
    ||  charset == &charset_8859_1) {
        probe_data.buf = rawbuf;
        probe_data.buf_size = len;
    } else {
        int offset = 0;
        u8 *bufp = buf;

        while (offset < len) {
            int ch = probe_data.charset_state.table[rawbuf[offset]];
            offset++;
            if (ch == ESCAPE_CHAR) {
                probe_data.charset_state.p = rawbuf + offset - 1;
                ch = probe_data.charset_state.decode_func(&probe_data.charset_state);
                offset = probe_data.charset_state.p - rawbuf;
            }
            bufp += utf8_encode((char *)bufp, ch);
            if (bufp > buf + sizeof(buf) - MAX_CHAR_BYTES - 1)
                break;
        }
        probe_data.buf = buf;
        probe_data.buf_size = bufp - buf;
        *bufp = '\0';
    }

    /* Skip the BOM if present */
    if (probe_data.buf_size >= 3
    &&  probe_data.buf[0] == 0xEF
    &&  probe_data.buf[1] == 0xBB
    &&  probe_data.buf[2] == 0xBF) {
        probe_data.buf += 3;
        probe_data.buf_size -= 3;
    }

    charset_decode_close(&probe_data.charset_state);

    p = memchr(probe_data.buf, '\n', probe_data.buf_size);
    probe_data.line_len = p ? p - probe_data.buf : probe_data.buf_size;

    for (m = qs->first_mode; m != NULL; m = m->next) {
        if (m->mode_probe) {
            int score = m->mode_probe(m, &probe_data);
            if (score > min_score) {
                int i;
                /* sort appropriate modes by insertion in modes array */
                for (i = 0; i < found_modes; i++) {
                    if (scores[i] < score)
                        break;
                }
                if (i < nb_modes) {
                    if (found_modes >= nb_modes)
                        found_modes = nb_modes - 1;
                    if (i < found_modes) {
                        memmove(modes + i + 1, modes + i,
                                (found_modes - i) * sizeof(*modes));
                        memmove(scores + i + 1, scores + i,
                                (found_modes - i) * sizeof(*scores));
                    }
                    modes[i] = m;
                    scores[i] = score;
                    found_modes++;
                }
            }
        }
    }
    return found_modes;
}

static EditState *qe_find_target_window(EditState *s, int activate) {
    /* Find the target window for some commands run from the dired window */
#ifndef CONFIG_TINY
    if ((s->flags & WF_POPLEFT) && s->x1 == 0) {
        EditState *e = find_window(s, KEY_RIGHT, NULL);
        if (e) {
            if (activate && s->qe_state->active_window == s)
                s->qe_state->active_window = e;
            s = e;
        }
    }
#endif
    return s;
}

/* Select appropriate mode for buffer:
 * iff dir == 0, select best mode
 * iff dir > 0, select next mode
 * iff dir < 0, select previous mode
 */
void do_set_next_mode(EditState *s, int dir)
{
    /* next-mode from the dired window applies to the target window */
    s = qe_find_target_window(s, 0);
    qe_set_next_mode(s, dir, 1);
}

void qe_set_next_mode(EditState *s, int dir, int status)
{
    u8 buf[4097];
    int size;
    ModeDef *modes[32];
    int scores[32];
    int i, nb, found;
    EditBuffer *b = s->b;

    size = eb_read(b, 0, buf, sizeof(buf) - 1);
    buf[size] = '\0';

    nb = probe_mode(s, b, modes, countof(modes), scores, 2,
                    b->filename, b->st_mode, b->total_size,
                    buf, size, b->charset, b->eol_type);
    found = 0;
    if (dir && nb > 0) {
        for (i = 0; i < nb; i++) {
            if (s->mode == modes[i]) {
                found = (i + nb + dir) % nb;
                break;
            }
        }
    }
    edit_set_mode(s, modes[found]);
    if (status) {
        put_status(s, "Mode is now %s, score=%d",
                   modes[found]->name, scores[found]);
    }
}

/* Load a file and attach buffer to window `s`.
 * Return -1 if loading failed.
 * Return 0 if file or resource was already loaded,
 * Return 1 if file or resource was newly loaded,
 * Return 2 if buffer was created for a new file.
 * Should take bits from enumeration instead of booleans.
 */
int qe_load_file(EditState *s, const char *filename1, int lflags, int bflags)
{
    u8 buf[4097];
    char filename[MAX_FILENAME_SIZE];
    int st_mode, buf_size, mode_score;
    ModeDef *selected_mode;
    EditBuffer *b;
    EditBufferDataType *bdt;
    FILE *f;
    struct stat st;
    EOLType eol_type = EOL_UNIX;
    QECharset *charset = &charset_utf8;

    if (lflags & LF_SPLIT_WINDOW) {
        /* Split window if window large enough and not empty */
        /* XXX: should check s->height units */
        if (s->height > 10 && s->b->total_size > 0) {
            do_split_window(s, 0);
            s = s->qe_state->active_window;
        }
    }

    if (lflags & LF_LOAD_RESOURCE) {
        if (find_resource_file(filename, sizeof(filename), filename1)) {
            put_status(s, "Cannot find resource file '%s'", filename1);
            return -1;
        }
    } else {
        /* compute full name */
        canonicalize_absolute_path((lflags & LF_CWD_RELATIVE) ? NULL : s,
                                   filename, sizeof(filename), filename1);
    }

#ifndef CONFIG_TINY
    /* when exploring from a popleft dired buffer, load a directory or
     * file pattern into the same pane, but load a regular file into the view pane
     */
    if (!is_directory(filename) && !is_filepattern(filename)) {
        s = qe_find_target_window(s, 1);
    }
#endif

    /* If file already loaded in existing buffer, switch to that */
    b = eb_find_file(filename);
    if (b != NULL) {
        switch_to_buffer(s, b);
        return 0;
    }

    /* We are going to try and load a new file: potentially delete the
     * current buffer if requested.
     */
    if ((lflags & LF_KILL_BUFFER) && s->b && !s->b->modified) {
        s->b->flags |= BF_TRANSIENT;
    }

    /* Create new buffer with unique name from filename */
    b = eb_new(get_basename(filename), BF_SAVELOG | bflags);
    eb_set_filename(b, filename);

    /* XXX: should actually initialize SAVED_DATA area in new buffer */
    s->offset = 0;
    /* XXX: Should test for full width and WRAP_TRUNCATE if not */
    s->wrap = WRAP_LINE;        /* default mode may override this */

    /* First we try to read the first block to determine the data type */
    if (stat(filename, &st) < 0) {
        /* XXX: default charset should be selectable.  Should have auto
         * charset transparent support for both utf8 and latin1.
         * Use utf8 for now */
        eb_set_charset(b, &charset_utf8, b->eol_type);
        /* XXX: dired_mode_probe will check for wildcards in real_filename */
        /* Try to determine the desired mode based on the filename. */
        b->st_mode = st_mode = S_IFREG;
        buf[0] = '\0';
        buf_size = 0;
        probe_mode(s, b, &selected_mode, 1, &mode_score, 2,
                   b->filename, b->st_mode, b->total_size,
                   buf, buf_size, b->charset, b->eol_type);

        /* Attach buffer to window, will set default_mode
         * XXX: this will also load the file, incorrect for non raw modes
         */
        b->default_mode = selected_mode;
        switch_to_buffer(s, b);
        if (b->data_type == &raw_data_type)
            put_status(s, "(New file)");
        do_load_qerc(s, s->b->filename);
        return 2;
    } else {
        b->st_mode = st_mode = st.st_mode;
        buf_size = 0;
        f = NULL;

        if (S_ISREG(st_mode)) {
            f = fopen(filename, "r");
            if (!f)
                goto fail;
            buf_size = fread(buf, 1, sizeof(buf) - 1, f);
            if (buf_size <= 0 && ferror(f)) {
                fclose(f);
                f = NULL;
                goto fail;
            }
            /* autodetect buffer charset */
            /* XXX: should enforce 32 bit alignment of buf */
            charset = detect_charset(buf, buf_size, &eol_type);
        }
        buf[buf_size] = '\0';
        if (!probe_mode(s, b, &selected_mode, 1, &mode_score, 2,
                        filename, b->st_mode, st.st_size,
                        buf, buf_size, charset, eol_type)) {
            fclose(f);
            f = NULL;
            goto fail;
        }
        bdt = selected_mode->data_type;
        if (bdt == &raw_data_type)
            eb_set_charset(b, charset, eol_type);

        if (f) {
            /* XXX: should use f to load buffer if raw_data_type */
            fclose(f);
            f = NULL;
        }

        b->default_mode = selected_mode;
        /* attaching the buffer to the window will set the default_mode
         * which in turn will load the data.
         * XXX: This is an ugly side effect, ineffective for
         * asynchronous shell buffers.
         * XXX: should instead instantiate the mode on the buffer and
         * test the resulting data_mode, loading the file if raw_mode
         * selected.
         */
        switch_to_buffer(s, b);
        if (access(b->filename, W_OK)) {
            b->flags |= BF_READONLY;
        }
        do_load_qerc(s, s->b->filename);

        /* XXX: invalid place */
        edit_invalidate(s);
        return 1;
    }

 fail:
    eb_free(&b);

    put_status(s, "Could not open '%s': %s",
               filename, strerror(errno));
    return -1;
}

void qe_save_open_files(EditState *s, EditBuffer *b)
{
    QEmacsState *qs = &qe_state;
    EditBuffer *b1;

    eb_puts(b, "// open files:\n");
    for (b1 = qs->first_buffer; b1 != NULL; b1 = b1->next) {
        if (!(b1->flags & BF_SYSTEM) && *b1->filename)
            eb_printf(b, "find_file(\"%s\");\n", b1->filename);
    }
    eb_putc(b, '\n');
}

#if 0
static void load_progress_cb(void *opaque, int size)
{
    EditState *s = opaque;
    EditBuffer *b = s->b;

    if (size >= 1024 && !b->probed) {
        qe_set_next_mode(s, 0, 0);
    }
}

static void load_completion_cb(void *opaque, int err)
{
    EditState *s = opaque;

    /* CG: potential problem: EXXX may be negative, as in Haiku */
    if (err == -ENOENT || err == -ENOTDIR) {
        put_status(s, "(New file)");
    } else
    if (err == -EISDIR) {
        s->b->st_mode = S_IFDIR;
    } else
    if (err < 0) {
        put_status(s, "Could not read file");
    }
    if (!s->b->probed) {
        qe_set_next_mode(s, 0, 0);
    }
    edit_display(s->qe_state);
    dpy_flush(&global_screen);
}
#endif

void do_find_file(EditState *s, const char *filename, int bflags)
{
    qe_load_file(s, filename, 0, bflags);
}

void do_find_file_other_window(EditState *s, const char *filename, int bflags)
{
    qe_load_file(s, filename, LF_SPLIT_WINDOW, bflags);
}

void do_find_alternate_file(EditState *s, const char *filename, int bflags)
{
    qe_load_file(s, filename, LF_KILL_BUFFER, bflags);
}

void do_load_file_from_path(EditState *s, const char *filename, int bflags)
{
    qe_load_file(s, filename, LF_LOAD_RESOURCE, bflags);
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
    /* CG: should load in a separate buffer, auto-detect charset and
     * copy buffer contents with charset translation
     */
    size = eb_raw_buffer_load1(s->b, f, s->offset);
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

    canonicalize_absolute_path(s, path, sizeof(path), filename);
    if (*renamefile == 'y' && *s->b->filename) {
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
    /* CG: Override bogus behaviour on unmodified buffers */
    s->b->modified = 1;
    do_save_buffer(s);
}

void do_write_region(EditState *s, const char *filename)
{
    char absname[MAX_FILENAME_SIZE];

    /* deactivate region hilite */
    s->region_style = 0;

    canonicalize_absolute_path(s, absname, sizeof(absname), filename);
    put_save_message(s, filename,
                     eb_write_buffer(s->b, s->b->mark, s->offset, filename));
}

enum QSState {
    QS_ASK,
    QS_NOSAVE,
    QS_SAVE,
};

typedef struct QuitState {
    enum QSState state;
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

    is = qe_mallocz(QuitState);
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
#ifndef CONFIG_TINY
        if (use_session_file)
            do_save_session(qe_state.active_window, 0);
#endif
        qe_free(&is);
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

static void quit_confirm_cb(qe__unused__ void *opaque, char *reply)
{
    if (!reply)
        return;
    if (reply[0] == 'y' || reply[0] == 'Y')
        url_exit();
    qe_free(&reply);
}

/*----------------*/

void do_doctor(EditState *s)
{
    /* Should show keys? */
    put_status(s, "Hello, how are you?");
}

int get_glyph_width(QEditScreen *screen, 
                    EditState *s, int style_index, int c)
{
    QEFont *font;
    QEStyleDef style;
    int width;

    get_style(s, &style, style_index);
    font = select_font(screen, style.font_style, style.font_size);
    width = glyph_width(screen, font, c);
    release_font(screen, font);
    return width;
}

int get_line_height(QEditScreen *screen, 
                    EditState *s, int style_index)
{
    QEFont *font;
    QEStyleDef style;
    int height;

    get_style(s, &style, style_index);
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
void do_refresh(qe__unused__ EditState *s1)
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
    new_status_height = get_line_height(qs->screen, NULL, QE_STYLE_STATUS);
    new_mode_line_height = get_line_height(qs->screen, NULL, QE_STYLE_MODE_LINE);
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

    if (s->qe_state->last_cmd_func == (CmdFunc)do_refresh_complete) {
        do_center_cursor(s, 1);
    } else {
        do_refresh(s);
    }
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
    EditState *e, *e1 = NULL;
    int count, pass, x1, y1, x2, y2;

    count = 0;
    for (e = qs->first_window; e != NULL; e = e->next_window) {
        if (!e->minibuf && !(e->flags & WF_POPUP))
            count++;
    }
    /* cannot close minibuf or if single window */
    if ((s->minibuf || count <= 1) && !force)
        return;

    if (!(s->flags & WF_POPUP)) {
        /* Try to merge the window with adjacent windows.
         * If this cannot be done, just leave a hole and force full
         * redisplay.
         */
        x1 = s->x1;
        x2 = s->x2;
        y1 = s->y1;
        y2 = s->y2;

        for (pass = 0; pass < 2; pass++) {
            for (e = qs->first_window; e != NULL; e = e->next_window) {
                if (e->minibuf || e == s || (e->flags & WF_POPUP))
                    continue;

                if (x1 == e->x2 && y1 == e->y1 && y2 >= e->y2) {
                    /* partial vertical split along the left border */
                    e->x2 = x2;
                    e->flags &= ~WF_RSEPARATOR;
                    e->flags |= s->flags & WF_RSEPARATOR;
                    y1 = e->y2;
                } else
                if (x2 == e->x1 && y1 == e->y1 && y2 >= e->y2) {
                    /* partial vertical split along the right border */
                    e->x1 = x1;
                    y1 = e->y2;
                } else
                if (y1 == e->y2 && x1 == e->x1 && x2 >= e->x2) {
                    /* partial horizontal split along the top border */
                    e->y2 = y2;
                    x1 = e->x2;
                } else
                if (y2 == e->y1 && x1 == e->x1 && x2 >= e->x2) {
                    /* partial horizontal split along bottom border */
                    e->y1 = y1;
                    x1 = e->x2;
                } else {
                    continue;
                }
                compute_client_area(e1 = e);
            }
            if (x1 == x2 || y1 == y2)
                break;
        }
        if (x1 != x2 && y1 != y2)
            qs->complete_refresh = 1;
    }
    if (qs->active_window == s)
        qs->active_window = e1 ? e1 : qs->first_window;

    edit_close(&s);
    if (qs->first_window)
        do_refresh(qs->first_window);
}

void do_delete_other_windows(EditState *s, int all)
{
    QEmacsState *qs = s->qe_state;
    EditState *e, *e1;

    if (s->minibuf || (s->flags & WF_POPUP))
        return;

    for (e = qs->first_window; e != NULL; e = e1) {
        e1 = e->next_window;
        if (!e->minibuf && e != s)
            edit_close(&e);
    }
    if (all) {
        edit_close(&s);
    } else {
        /* resize to whole screen */
        s->y1 = 0;
        s->x1 = 0;
        s->x2 = qs->width;
        s->y2 = qs->height - qs->status_height;
        s->flags &= ~WF_RSEPARATOR;
        compute_client_area(s);
        do_refresh(s);
    }
}

void do_hide_window(EditState *s, int set)
{
    if (set)
        s->flags |= WF_HIDDEN;
    else
        s->flags &= ~WF_HIDDEN;
}

void do_delete_hidden_windows(EditState *s)
{
    QEmacsState *qs = s->qe_state;
    EditState *e, *e1;

    for (e = qs->first_window; e != NULL; e = e1) {
        e1 = e->next_window;
        if (e->flags & WF_HIDDEN)
            edit_close(&e);
    }
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

    /* This will clone mode and mode data to the newly created window */
    generic_save_window_data(s);
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
    edit_attach(e, s->next_window);

    if (qs->flag_split_window_change_focus)
        qs->active_window = e;

    compute_client_area(s);
    do_refresh(s);
}

void do_create_window(EditState *s, const char *filename, const char *layout)
{
    QEmacsState *qs = s->qe_state;
    static const char * const names[] = {
        "x1:", "y1:", "x2:", "y2:", "flags:", "wrap:",
        "offset:", "offset.col:", "mark:", "mark.col:", "top:", "top.col:",
        "active:",
    };
    int args[] = { 0, 0, 0, 0, WF_MODELINE, WRAP_LINE, 0, 0, 0, 0, 0, 0, 0  };
    ModeDef *m = NULL;
    int i, n, x1, y1, x2, y2, flags;
    enum WrapType wrap;
    const char *p = layout;
    EditBuffer *b1;

    b1 = eb_find_file(filename);
    if (!b1) {
        put_status(s, "create_window: no such file loaded: %s", filename);
        return;
    }

    for (n = 0; *p; n++) {
        while (qe_isblank(*p))
            p++;
        for (i = 0; i < countof(names); i++) {
            if (strstart(p, names[i], &p)) {
                n = i;
                break;
            }
        }
        if (strstart(p, "mode:", &p)) {
            m = qe_find_mode(p, 0);
            break;
        }
        if (n >= countof(args))
            break;

        args[n] = strtol(p, (char **)&p, 0);
        while (qe_isblank(*p))
            p++;
        if (*p == ',')
            p++;
    }
    x1 = scale(args[0], qs->width, 1000);
    y1 = scale(args[1], qs->height - qs->status_height, 1000);
    x2 = scale(args[2], qs->width, 1000);
    y2 = scale(args[3], qs->height - qs->status_height, 1000);
    flags = args[4];
    wrap = (enum WrapType)args[5];

    s = edit_new(b1, x1, y1, x2 - x1, y2 - y1, flags);
    if (m)
        edit_set_mode(s, m);
    s->wrap = wrap;
    s->offset = clamp(eb_goto_pos(b1, args[6], args[7]), 0, b1->total_size);
    s->b->mark = clamp(eb_goto_pos(b1, args[8], args[9]), 0, b1->total_size);
    s->offset_top = clamp(eb_goto_pos(b1, args[10], args[11]), 0, b1->total_size);
    if (args[12])
        qs->active_window = s;

    do_refresh(s);
}

void qe_save_window_layout(EditState *s, EditBuffer *b)
{
    QEmacsState *qs = s->qe_state;
    const EditState *e;
    int offset_row, offset_col;
    int mark_row, mark_col;
    int top_row, top_col;

    eb_puts(b, "// window layout:\n");
    /* Get rid of default window */
    // XXX: should simplify layout management
    // XXX: should save mark, offset, offset_top
    eb_puts(b, "delete_other_windows();\n");
    eb_puts(b, "hide_window();\n");
    for (e = qs->first_window; e != NULL; e = e->next_window) {
        if (*e->b->filename) {
            eb_get_pos(e->b, &offset_row, &offset_col, e->offset);
            eb_get_pos(e->b, &mark_row, &mark_col, e->b->mark);
            eb_get_pos(e->b, &top_row, &top_col, e->offset_top);
            eb_printf(b, "create_window(\"%s\", "
                      "\"%d,%d,%d,%d flags:%d wrap:%d",
                      e->b->filename,
                      scale(e->x1, 1000, qs->width),
                      scale(e->y1, 1000, qs->height - qs->status_height),
                      scale(e->x2, 1000, qs->width),
                      scale(e->y2, 1000, qs->height - qs->status_height),
                      e->flags, e->wrap);
            if (e->offset)
                eb_printf(b, " offset:%d,%d", offset_row, offset_col);
            if (e->b->mark)
                eb_printf(b, " mark:%d,%d", mark_row, mark_col);
            if (e->offset_top)
                eb_printf(b, " top:%d,%d", top_row, top_col);
            if (e == qs->active_window)
                eb_printf(b, " active:1");
            eb_printf(b, " mode:%s\");\n", e->mode->name);
        }
    }
    eb_puts(b, "delete_hidden_windows();\n");
    eb_putc(b, '\n');
}

/* help */

void do_describe_key_briefly(EditState *s)
{
    put_status(s, "Describe key: ");
    key_ctx.describe_key = 1;
}

EditBuffer *new_help_buffer(void)
{
    EditBuffer *b;

    b = eb_find("*Help*");
    if (b) {
        eb_clear(b);
    } else {
        b = eb_new("*Help*", BF_UTF8);
    }
    return b;
}

void do_help_for_help(EditState *s)
{
    EditBuffer *b;

    b = new_help_buffer();
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
    show_popup(s, b);
}

#ifdef CONFIG_WIN32

void qe_event_init(void)
{
}

#else

/* we install a signal handler to set poll_flag to one so that we can
   avoid polling too often in some cases */

int qe__fast_test_event_poll_flag = 0;

static void poll_action(qe__unused__ int sig)
{
    qe__fast_test_event_poll_flag = 1;
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
int qe__is_user_input_pending(void)
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

static int check_motion_target(qe__unused__ EditState *s)
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
        do_kill_region(e, 1);
    }
}

/* XXX: need a more general scheme for other modes such as HTML/image */
/* CG: remove this */
void wheel_scroll_up_down(EditState *s, int dir)
{
    int line_height;

    /* only apply to text modes */
    if (!s->mode->display_line)
        return;

    line_height = get_line_height(s->screen, s, QE_STYLE_DEFAULT);
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

static int text_mode_probe(qe__unused__ ModeDef *mode,
                           qe__unused__ ModeProbeData *p)
{
    if (mode->extensions) {
        if (match_extension(p->filename, mode->extensions))
            return 80;
        else
            return 1;
    }
#if 0
    /* text mode inappropriate for huge binary files */
    if (detect_binary(p->buf, p->buf_size) && p->total_size > 1000000)
        return 0;
    else
#endif
        return 20;
}

static int generic_mode_init(EditState *s)
{
    s->offset = min(s->offset, s->b->total_size);
    s->offset_top = min(s->offset_top, s->b->total_size);
    eb_add_callback(s->b, eb_offset_callback, &s->offset, 0);
    eb_add_callback(s->b, eb_offset_callback, &s->offset_top, 0);
    set_colorize_func(s, NULL);
    return 0;
}

/* Save window generic window data and mode */
static int generic_save_window_data(EditState *s)
{
    EditBuffer *b = s->b;

    if (!b->saved_data
    &&  !(b->saved_data = qe_mallocz_array(u8, SAVED_DATA_SIZE))) {
        return -1;
    }
    memcpy(b->saved_data, s, SAVED_DATA_SIZE);
    b->saved_mode = s->mode;
    return 0;
}

static void generic_mode_close(EditState *s)
{
    s->hex_mode = 0;
    s->hex_nibble = 0;
    s->unihex_mode = 0;
    s->insert = 1;
    s->wrap = WRAP_LINE;

    /* free all callbacks or associated buffer data */
    set_colorize_func(s, NULL);
    eb_free_callback(s->b, eb_offset_callback, &s->offset);
    eb_free_callback(s->b, eb_offset_callback, &s->offset_top);
}

ModeDef text_mode = {
    .name = "text",
    .mode_probe = text_mode_probe,

    .display_line = text_display_line,
    .backward_offset = text_backward_offset,

    .move_up_down = text_move_up_down,
    .move_left_right = text_move_left_right_visual,
    .move_bol = text_move_bol,
    .move_eol = text_move_eol,
    .move_bof = text_move_bof,
    .move_eof = text_move_eof,
    .move_word_left_right = text_move_word_left_right,
    .scroll_up_down = text_scroll_up_down,
    .mouse_goto = text_mouse_goto,
    .write_char = text_write_char,
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

    find_file_close(&ffst);

    return ret;
}

/******************************************************/

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
    find_file_close(&ffst);
    if (file)
        do_refresh(e);
}

/* Load .qerc files in all parent directories of filename */
/* CG: should keep a cache of failed attempts */
void do_load_qerc(EditState *e, const char *filename)
{
    char buf[MAX_FILENAME_SIZE];
    char *p = buf;
    QEmacsState *qs = e->qe_state;
    EditState *saved = qs->active_window;

    for (;;) {
        pstrcpy(buf, sizeof(buf), filename);
        p = strchr(p, '/');
        if (!p)
            break;
        p += 1;
        pstrcpy(p, buf + sizeof(buf) - p, ".qerc");
        qs->active_window = e;
        parse_config_file(e, buf);
    }
    qs->active_window = saved;
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
	if (p == table)
	    return;  /* already registered */
        while (p->name != NULL)
            p++;
        pp = &p->u.next;
    }
    *pp = table;
}

/******************************************************/

const char str_version[] = "QEmacs version " QE_VERSION;
const char str_credits[] = "Copyright (c) 2000-2003 Fabrice Bellard\n"
                           "Copyright (c) 2000-2017 Charlie Gordon\n";

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

static int parse_command_line(int argc, char **argv)
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

void do_add_resource_path(EditState *s, const char *path)
{
    QEmacsState *qs = s->qe_state;
    pstrcat(qs->res_path, sizeof(qs->res_path), ":");
    pstrcat(qs->res_path, sizeof(qs->res_path), path);
}

void set_user_option(const char *user)
{
    QEmacsState *qs = &qe_state;
    char path[MAX_FILENAME_SIZE];
    const char *home_path;

    user_option = user;

    /* compute resources path */
    qs->res_path[0] = '\0';

    /* put current directory first if qe invoked as ./qe */
    if (stristart(qs->argv[0], "./qe", NULL)) {
        if (!getcwd(path, sizeof(path)))
            strcpy(path, ".");
        pstrcat(qs->res_path, sizeof(qs->res_path), path);
        pstrcat(qs->res_path, sizeof(qs->res_path), ":");
    }

    /* put user directory before standard list */
    if (user) {
        /* use ~USER/.qe instead of ~/.qe */
        /* CG: should get user homedir */
#ifdef CONFIG_DARWIN
        snprintf(path, sizeof(path), "/Users/%s", user);
#else
        snprintf(path, sizeof(path), "/home/%s", user);
#endif
        home_path = path;
    } else {
        home_path = getenv("HOME");
    }
    if (home_path) {
        pstrcat(qs->res_path, sizeof(qs->res_path), home_path);
        pstrcat(qs->res_path, sizeof(qs->res_path), "/.qe:");
    }

    pstrcat(qs->res_path, sizeof(qs->res_path),
            CONFIG_QE_DATADIR ":"
            CONFIG_QE_PREFIX "/share/qe" ":"
            CONFIG_QE_PREFIX "/lib/qe" ":"
            "/usr/share/qe" ":"
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
    { "single-window", "1", NULL, CMD_OPT_BOOL, "keep a single window when loading multiple files",
       { .int_ptr = &single_window }},
    { "no-windows", "nw", NULL, CMD_OPT_BOOL, "force tty terminal usage",
       { .int_ptr = &force_tty }},
    { "ttycharset", "c", "CHARSET", CMD_OPT_ARG, "specify tty charset",
      { .func_arg = set_tty_charset }},
    { "use-session", "s", NULL, CMD_OPT_BOOL, "load and save session files",
      { .int_ptr = &use_session_file }},
    { "user", "u", "USER", CMD_OPT_ARG, "load ~USER/.qe/config instead of your own",
      { .func_arg = set_user_option }},
    { "version", "V", NULL, 0, "display version information and exit",
      { .func_noarg = show_version }},
#ifndef CONFIG_TINY
    { "free-all", NULL, NULL, CMD_OPT_BOOL, "free all structures upon exit",
      { .int_ptr = &free_everything }},
#endif
    { NULL, NULL, NULL, 0, NULL, { NULL }},
};

/* default key bindings */

#include "qeconfig.h"

#if (defined(__GNUC__) || defined(__TINYC__)) && defined(CONFIG_INIT_CALLS)

static void init_all_modules(void)
{
    int (*initcall)(void);
    int (**ptr)(void);

    ptr = (int (**)(void))(void *)&qe__initcall_first;
    for (;;) {
#if defined(__BOUNDS_CHECKING_ON)
        /* NOTE: if bound checking is on, a NULL is inserted between
           each initialized 'void *' */
        ptr++;
#endif
        ptr++;
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

    ptr = (int (**)(void))(void *)&qe__exitcall_first;
    for (;;) {
#if defined(__BOUNDS_CHECKING_ON)
        /* NOTE: if bound checking is on, a NULL is inserted between
           each initialized 'void *' */
        ptr++;
#endif
        ptr++;
        exitcall = *ptr;
        if (exitcall == NULL)
            break;
        (*exitcall)();
    }
}
#endif

#else

#ifndef SPLINT
#define qe_module_declare(fn)  extern int module_ ## fn(void)
#include "modules.txt"
#undef qe_module_declare
#endif

static void init_all_modules(void)
{
#ifndef SPLINT
#define qe_module_declare(fn)  module_ ## fn()
#include "modules.txt"
#undef qe_module_declare
#endif
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
        *(void **)&init_func = dlsym(h, "__qe_module_init");
        //init_func = (int (*)(void))dlsym(h, "__qe_module_init");
#else
        /* This kludge gets rid of compile and lint warnings */
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
    find_file_close(&ffst);

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
    int i, _optind;
#if !defined(CONFIG_TINY) && !defined(CONFIG_WIN32)
    int is_player, session_loaded = 0;
#endif

    qs->ec.function = "qe-init";
    qs->macro_key_index = -1; /* no macro executing */
    qs->ungot_key = -1; /* no unget key */

    qs->argc = argc;
    qs->argv = argv;

    qs->hilite_region = 1;
    qs->default_tab_width = 8;
    qs->default_fill_column = 70;
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
    qe_register_mode(&text_mode, MODEF_VIEW);
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
    popup_init();

    /* init all external modules in link order */
    init_all_modules();

#ifdef CONFIG_DLL
    /* load all dynamic modules */
    load_all_modules(qs);
#endif

#if !defined(CONFIG_TINY) && !defined(CONFIG_WIN32)
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
#endif

    /* init of the editor state */
    qs->screen = &global_screen;

    /* create first buffer */
    b = eb_new("*scratch*", BF_SAVELOG | BF_UTF8);

    /* will be positionned by do_refresh() */
    s = edit_new(b, 0, 0, 0, 0, WF_MODELINE);

    /* at this stage, no screen is defined. Initialize a
     * null display driver to have a consistent state
     * else many commands such as put_status would crash.
     */
    screen_init(&global_screen, NULL, screen_width, screen_height);

    /* handle options */
    _optind = parse_command_line(argc, argv);

    /* load config file unless command line option given */
    if (!no_init_file) {
        do_load_config_file(s, NULL);
        s = qs->active_window;
    }

    qe_key_init(&key_ctx);

    /* select the suitable display manager */
    for (;;) {
        dpy = probe_display();
        if (!dpy) {
            fprintf(stderr, "No suitable display found, exiting\n");
            exit(1);
        }
        if (screen_init(&global_screen, dpy, screen_width, screen_height) < 0) {
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

#ifndef CONFIG_TINY
    if (use_session_file) {
        session_loaded = !qe_load_session(s);
        s = qs->active_window;
    }
#endif
    do_refresh(s);

    /* load file(s) */
    for (i = _optind; i < argc; ) {
        int line_num = 0, col_num = 0;
        char *arg, *p;

        arg = argv[i++];

        if (*arg == '+' && i < argc) {
            if (strequal(arg, "+eval")) {
                do_eval_expression(s, argv[i++]);
                s = qs->active_window;
                continue;
            }
            if (strequal(arg, "+load")) {
                /* load script file */
                parse_config_file(s, argv[i++]);
                s = qs->active_window;
                continue;
            }
            /* Handle +linenumber[,column] before file */
            line_num = strtol(arg + 1, &p, 10);
            if (*p == ',')
                col_num = strtol(p + 1, NULL, 10);
            arg = argv[i++];
        }
        /* load filename relative to qe current directory */
        /* XXX: should split windows evenly */
        qe_load_file(s, arg,
                     single_window ? LF_CWD_RELATIVE :
                     LF_CWD_RELATIVE | LF_SPLIT_WINDOW,
                     0);
        s = qs->active_window;
        if (line_num)
            do_goto_line(s, line_num, col_num);
    }

#if !defined(CONFIG_TINY) && !defined(CONFIG_WIN32)
    if (is_player && !session_loaded && (_optind >= argc || S_ISDIR(s->b->st_mode))) {
        /* if player, go to directory mode by default if no file selected */
        do_dired(s, NO_ARG);
        s = qs->active_window;
    }
#endif

    put_status(s, "QEmacs %s - Press F1 for help", QE_VERSION);

    edit_display(qs);
    dpy_flush(&global_screen);

    b = eb_find("*errors*");
    if (b != NULL) {
        show_popup(s, b);
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
#ifdef CONFIG_UNICODE_JOIN
    unload_ligatures();
#endif
#ifndef CONFIG_TINY
    if (free_everything) {
        /* free all structures for valgrind */
        QEmacsState *qs = &qe_state;

        while (qs->input_methods) {
            InputMethod *p = qs->input_methods;
            qs->input_methods = p->next;
            if (p->data)
                qe_free(&p);
        }
        while (qs->first_window) {
            EditState *e = qs->first_window;
            edit_close(&e);
        }
        while (qs->first_buffer) {
            EditBuffer *b = qs->first_buffer;
            eb_free(&b);
        }
        while (qs->first_cmd) {
            CmdDef *d = qs->first_cmd;
            CmdDef *d1 = d;
            while (d1->name)
                d1++;
            qs->first_cmd = d1->action.next;
            /* free xxx-mode commands and macros */
            if (d->name && !d[1].name && d[1].val) {
                qe_free((char **)&d->name);
                qe_free(&d);
            }
        }
        while (qs->first_key) {
            KeyDef *p = qs->first_key;
            qs->first_key = p->next;
            qe_free(&p);
        }
        while (qs->first_mode) {
            ModeDef *m = qs->first_mode;
            qs->first_mode = m->next;

            while (m->first_key) {
                KeyDef *p = m->first_key;
                m->first_key = p->next;
                qe_free(&p);
            }
        }
        while (qs->first_completion) {
            CompletionEntry *cp = qs->first_completion;
            qs->first_completion = cp->next;
            qe_free(&cp);
        }
        css_free_colors();
        free_font_cache(&global_screen);
        qe_free(&qs->buffer_cache);
        qs->buffer_cache_size = qs->buffer_cache_len = 0;
    }
#endif
    dpy_close(&global_screen);

    return 0;
}
