/*
 * QEmacs, tiny but powerful multimode editor
 *
 * Copyright (c) 2000-2002 Fabrice Bellard.
 * Copyright (c) 2000-2025 Charlie Gordon.
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
#include "unicode_join.h"
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

void print_at_byte(QEditScreen *screen,
                   int x, int y, int width, int height,
                   const char *str, QETermStyle style);
static EditBuffer *predict_switch_to_buffer(EditState *s);
static void qe_key_process(QEmacsState *qs, int key);

static int generic_save_window_data(EditState *s);
static int generic_mode_init(EditState *s);
static void generic_mode_close(EditState *s);
static void generic_text_display(EditState *s);
static void display1(DisplayState *ds);
#ifndef CONFIG_TINY
static void qe_save_selection(QEmacsState *qs, int copy);
#endif

static QEmacsState qe_state;
/* should handle multiple screens, and multiple sessions */
static QEditScreen global_screen;
static int screen_width = 0;
static int screen_height = 0;
static int no_init_file;
static int single_window;
int force_tty;
int tty_mk = -1;
int tty_mouse = -1;
int tty_clipboard = -1;
int disable_crc;
#ifdef CONFIG_SESSION
int use_session_file;
#endif
int use_html = 1;
int is_player = 1;    /* Start in dired mode when invoked with no arguments */
#ifndef CONFIG_TINY
static int free_everything;
#endif

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

ModeDef *qe_find_mode(QEmacsState *qs, const char *name, int flags)
{
    ModeDef *m;

    strstart(name, "lang-", &name);
    for (m = qs->first_mode; m; m = m->next) {
        if ((m->flags & flags) == flags) {
            if ((m->name && !strcasecmp(m->name, name))
            ||  (m->alt_name && !strcasecmp(m->alt_name, name))
            ||  (m->extensions && strfind(m->extensions, name)))  // XXX: really?
                break;
        }
    }
    return m;
}

ModeDef *qe_find_mode_filename(QEmacsState *qs, const char *filename, int flags)
{
    ModeDef *m;

    for (m = qs->first_mode; m; m = m->next) {
        // XXX: should have a filenames field to match basenames
        if ((m->flags & flags) == flags
        &&  match_extension(filename, m->extensions)) {
            break;
        }
    }
    return m;
}

void qe_register_mode(QEmacsState *qs, ModeDef *m, int flags)
{
    ModeDef **p;

    /* register mode in mode list (at end) */
    for (p = &qs->first_mode;; p = &(*p)->next) {
        if (*p == m) {
            /* mode is already registered, do nothing */
            return;
        }
        if (*p == NULL) {
            m->next = NULL;
            *p = m;
            break;
        }
    }

    m->flags |= flags;

    if (m->flags & MODEF_SYNTAX) {
        /* default to text handling */
        /* should follow the fallback chain */
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
    /* if no syntax probing function, use extension matcher */
    if (!m->mode_probe && m->extensions)
        m->mode_probe = generic_mode_probe;
    if (!m->display)
        m->display = generic_text_display;
    if (!m->data_type)
        m->data_type = &raw_data_type;
    if (!m->get_mode_line)
        m->get_mode_line = text_mode_line;

    /* add a new command to switch to that mode */
    if (!(m->flags & MODEF_NOCMD)) {
        char name[64];
        char spec[64];
        int name_len, spec_len;
        CmdDef *def;
        const char *mode_name = m->alt_name ? m->alt_name : m->name;

        /* constuct command name and specification */
        /* lower case convert for C mode, Perl... */
        qe_strtolower(name, sizeof(name) - 10, mode_name);
        pstrcat(name, sizeof(name), "-mode");
        name_len = strlen(name);
        name[name_len + 1] = '\0'; /* empty default bindings string */

        /* Achtung: embedded null bytes */
        spec_len = snprintf(spec, sizeof(spec),
                            "@{%s}%cselect the %s mode",
                            mode_name, 0, mode_name);
        def = qe_mallocz(CmdDef);
        /* allocate space for name and spec with embedded null bytes */
        def->name = qe_malloc_dup_bytes(name, name_len + 2);
        def->spec = qe_malloc_dup_bytes(spec, spec_len + 1);
        def->sig = CMD_ESs;
        def->val = 0;
        def->action.ESs = do_set_mode;
        /* register allocated command */
        qe_register_commands(qs, NULL, def, -1);
    }
    if (m->bindings) {
        int i;
        for (i = 0; m->bindings[i]; i += 2) {
            qe_register_bindings(qs, &m->first_key, m->bindings[i + 1], m->bindings[i]);
        }
    }
}

void mode_complete(CompleteState *cp, CompleteFunc enumerate) {
    QEmacsState *qs = cp->s->qs;
    ModeDef *m;

    for (m = qs->first_mode; m != NULL; m = m->next) {
        enumerate(cp, m->name, CT_GLOB);
        if (m->alt_name && !strequal(m->name, m->alt_name))
            enumerate(cp, m->alt_name, CT_GLOB);
    }
}

static CompletionDef mode_completion = {
    .name = "mode",
    .enumerate = mode_complete,
};

/* commands handling */

const CmdDef *qe_find_cmd(QEmacsState *qs, const char *cmd_name)
{
    const CmdDef *d;
    int i, j;

    for (i = 0; i < qs->cmd_array_count; i++) {
        for (j = qs->cmd_array[i].count, d = qs->cmd_array[i].array; j-- > 0; d++) {
            if (strequal(cmd_name, d->name))
                return d;
        }
    }
    return NULL;
}

void command_complete(CompleteState *cp, CompleteFunc enumerate) {
    QEmacsState *qs = cp->s->qs;
    const CmdDef *d;
    int i, j;

    for (i = 0; i < qs->cmd_array_count; i++) {
        for (j = qs->cmd_array[i].count, d = qs->cmd_array[i].array; j-- > 0; d++) {
            enumerate(cp, d->name, CT_GLOB);
        }
    }
}

int eb_command_print_entry(EditBuffer *b, const CmdDef *d, EditState *s) {
    char buf[256];
    int len = 0;

    if (d) {
        b->cur_style = QE_STYLE_FUNCTION;
        len = eb_puts(b, d->name);
        b->cur_style = QE_STYLE_DEFAULT;
        qe_get_prototype(d, buf, sizeof buf);
        len += eb_puts(b, buf);
#ifndef CONFIG_TINY
        if (qe_list_bindings(b->qs, d, s->mode, 1, buf, sizeof buf)) {
            b->cur_style = QE_STYLE_COMMENT;
            if (len + 1 < 40) {
                b->tab_width = max_int(len + 1, b->tab_width);
                len += eb_putc(b, '\t');
            } else {
                b->tab_width = 40;
            }
            len += eb_printf(b, "  bound to %s", buf);
            b->cur_style = QE_STYLE_DEFAULT;
        }
#endif
    }
    return len;
}

int command_print_entry(CompleteState *cp, EditState *s, const char *name) {
    const CmdDef *d = qe_find_cmd(s->qs, name);
    if (d) {
        // XXX: should pass the target window
        return eb_command_print_entry(s->b, d, s);
    } else {
        return eb_puts(s->b, name);
    }
}

int command_get_entry(EditState *s, char *dest, int size, int offset)
{
    int len;
    eb_fgets(s->b, dest, size, offset, &offset);
    len = strcspn(dest, " \t\n(");
    dest[len] = '\0';   /* strip the TAB or trailing newline if any */
    return len;
}

static CompletionDef command_completion = {
    .name = "command",
    .enumerate = command_complete,
    .print_entry = command_print_entry,
    .get_entry = command_get_entry,
};

/* key binding handling */

static void qe_free_bindings(KeyDef **lp) {
    while (*lp) {
        KeyDef *p = *lp;
        *lp = p->next;
        qe_free(&p);
    }
}

static int qe_register_binding(KeyDef **lp, const CmdDef *d, const unsigned int *keys, int nb_keys)
{
    KeyDef *p;
    int i;

    if (!nb_keys)
        return -2;
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
    /* Bindings must be prepended to override previous bindings
     * skip bindings to the same command for consistency */
    while (*lp != NULL && (*lp)->cmd == d)
        lp = &(*lp)->next;
    p->next = *lp;
    *lp = p;
    return 0;
}

/* remove a key binding from mode or globally */
static int qe_unregister_binding(KeyDef **lp, unsigned int *keys, int nb_keys) {
    KeyDef *p;

    if (!nb_keys)
        return -2;

    while (*lp) {
        if ((*lp)->nb_keys == nb_keys && !blockcmp((*lp)->keys, keys, nb_keys)) {
            p = *lp;
            *lp = p->next;
            qe_free(&p);
            return 1;
        }
        lp = &(*lp)->next;
    }
    return 0;
}

/* if mode is non NULL, the defined keys are only active in this mode */
static int qe_register_command_bindings(QEmacsState *qs, KeyDef **lp, const CmdDef *d, const char *keystr)
{
    unsigned int keys[MAX_KEYS];
    int nb_keys, res = -2;
    const char *p = keystr;

    while (p && *p) {
        nb_keys = strtokeys(p, keys, MAX_KEYS, &p);
        res = qe_register_binding(lp, d, keys, nb_keys);
    }
    return res;
}

int qe_register_bindings(QEmacsState *qs, KeyDef **lp, const char *cmd_name, const char *keys) {
    return qe_register_command_bindings(qs, lp, qe_find_cmd(qs, cmd_name), keys);
}

int qe_register_transient_binding(QEmacsState *qs, const char *cmd_name, const char *keys) {
    return qe_register_command_bindings(qs, &qs->first_transient_key, qe_find_cmd(qs, cmd_name), keys);
}

static void qe_unregister_bindings(KeyDef **lp, const char *keystr) {
    unsigned int keys[MAX_KEYS];
    int nb_keys;
    const char *p = keystr;

    while (p && *p) {
        nb_keys = strtokeys(p, keys, MAX_KEYS, &p);
        qe_unregister_binding(lp, keys, nb_keys);
    }
}

/* if mode is non NULL, the defined keys are only active in this mode */
int qe_register_commands(QEmacsState *qs, ModeDef *m, const CmdDef *cmds, int len)
{
    KeyDef **lp = m ? &m->first_key : &qs->first_key;
    const CmdDef *d;
    int i, allocated = 0;

    if (len < 0) {
        allocated = 1;
        len = -len;
    }

    for (i = 0; i < qs->cmd_array_count; i++) {
        if (qs->cmd_array[i].array == cmds) {
            /* Command table already registered, still do the binding
             * phase to allow multiple mode bindings.
             */
            break;
        }
    }
    if (i >= qs->cmd_array_count) {
        if (i >= qs->cmd_array_size) {
            int n = max_int(i + 16, 32);
            if (!qe_realloc_array(&qs->cmd_array, n)) {
                qe_put_error(qs, "Out of memory");
                return -1;
            }
            qs->cmd_array_size = n;
        }
        qs->cmd_array[i].array = cmds;
        qs->cmd_array[i].count = len;
        qs->cmd_array[i].allocated = allocated;
        qs->cmd_array_count++;
    }
    /* register default bindings */
    for (d = cmds, i = len; i-- > 0; d++) {
        const char *p = d->name + strlen(d->name) + 1;
        if (*p)
            qe_register_command_bindings(qs, lp, d, p);
    }
    return 0;
}

void do_set_key(EditState *s, const char *keystr,
                const char *cmd_name, int local)
{
    QEmacsState *qs = s->qs;
    KeyDef **lp = local ? &s->mode->first_key : &qs->first_key;
    int res = qe_register_bindings(qs, lp, cmd_name, keystr);
    if (res == -2)
        put_error(s, "Invalid keys: %s", keystr);
    if (res == -1)
        put_error(s, "Invalid command: %s", cmd_name);
}

void do_unset_key(EditState *s, const char *keystr, int local) {
    KeyDef **lp = local ? &s->mode->first_key : &s->qs->first_key;
    qe_unregister_bindings(lp, keystr);
}

void qe_toggle_control_h(QEmacsState *qs, int set)
{
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

void do_toggle_control_h(EditState *s, int set) {
    qe_toggle_control_h(s->qs, set);
}

static const char * const epsilon_bindings[] = {
    "C-w", "isearch-toggle-word-match", "isearch",
    "M-w", "isearch-yank-word", "isearch",
    "C-y", "isearch-yank-kill", "isearch",
    "M-y", "isearch-yank-line", "isearch",
    "C-\\", "call-last-kbd-macro", NULL,
    "C-x C-l", "compare-windows", NULL,
    "C-x RET", "shell", NULL,
    "C-x d", "delete-window", NULL,
    "M-SPC", "set-mark-command", NULL,
    "M-[", "backward-paragraph", NULL,
    "M-]", "forward-paragraph", NULL,
    "M-j", "fill-paragraph", NULL,
    "M-k", "kill-beginning-of-line", NULL,
    "M-q", "query-replace", NULL,
    "M-{", "scroll-left", NULL,
    "M-}", "scroll-right", NULL,
    NULL
};

static const char * const emacs_bindings[] = {
    "C-w", "isearch-yank-word", "isearch",
    "M-w", "isearch-toggle-word-match", "isearch",
    "C-y", "isearch-yank-line", "isearch",
    "M-y", "isearch-yank-kill", "isearch",
    "C-\\", "toggle-input-method", NULL,
    "C-x C-l", "downcase-region", NULL,
    "C-x RET", NULL, NULL,
    "C-x d", "dired", NULL,
    "M-SPC", "just-one-space", NULL,
    "M-[", NULL, NULL,
    "M-]", NULL, NULL,
    "M-j", "indent-new-comment-line", NULL,
    "M-k", "kill-sentence", NULL,
    "M-q", "fill-paragraph", NULL,
    "M-{", "backward-paragraph", NULL,
    "M-}", "forward-paragraph", NULL,
    NULL
};

static const char * const gosmacs_bindings[] = {
    NULL
};

static void qe_register_emulation_bindings(QEmacsState *qs, const char * const *pp) {
    int i;
    for (i = 0; pp[i]; i += 3) {
        KeyDef **lp = &qs->first_key;
        if (pp[i + 2]) {
            ModeDef *mode = qe_find_mode(qs, pp[i + 2], 0);
            if (!mode)
                continue;
            lp = &mode->first_key;
        }
        qe_unregister_bindings(lp, pp[i]);
        if (pp[i])
            qe_register_bindings(qs, lp, pp[i + 1], pp[i]);
    }
}

static void do_qemacs_version(EditState *s) {
    put_status(s, "%s", str_version);
}

void do_set_emulation(EditState *s, const char *name) {
    QEmacsState *qs = s->qs;

    if (strequal(name, "epsilon")) {
        qe_register_emulation_bindings(qs, epsilon_bindings);
        qs->emulation_flags = 1;
        qs->flag_split_window_change_focus = 1;
    } else
    if (strequal(name, "emacs") || strequal(name, "xemacs")) {
        qe_register_emulation_bindings(qs, emacs_bindings);
        qs->emulation_flags = 0;
        qs->flag_split_window_change_focus = 0;
    } else
    if (strequal(name, "gosmacs")) {
        qe_register_emulation_bindings(qs, gosmacs_bindings);
        qs->emulation_flags = 2;
    } else
    if (strequal(name, "vi") || strequal(name, "vim")) {
        put_error(s, "Emulation '%s' not available yet", name);
    } else {
        put_error(s, "Unknown emulation '%s'", name);
    }
}

struct QETraceDef const qe_trace_defs[] = {
    { EB_TRACE_TTY, "tty" },
    { EB_TRACE_KEY, "key" },
    { EB_TRACE_MOUSE, "mouse" },
    { EB_TRACE_COMMAND, "command" },
    { EB_TRACE_SHELL, "shell" },
    { EB_TRACE_PTY, "pty" },
    { EB_TRACE_EMULATE, "emulate" },
    { EB_TRACE_DEBUG, "debug" },
    { EB_TRACE_CLIPBOARD, "clipboard" },
    { EB_TRACE_ALL, "all" },
    { EB_TRACE_ALL, "on" },
    { 0, "off" },
    { 0, "none" },
};
size_t const qe_trace_defs_count = sizeof(qe_trace_defs) / sizeof (qe_trace_defs[0]);

void do_set_trace_flags(EditState *s, int flags) {
    QEmacsState *qs = s->qs;

    qs->trace_flags = flags;
    if (qs->trace_flags) {
        char buf[80];
        buf_t out[1];
        size_t i;

        if (!qs->trace_buffer) {
            qs->trace_buffer = qe_new_buffer(qs, "*trace*", BF_SYSTEM | BF_UTF8);
            if (!qs->trace_buffer)
                return;
        }
        if (!eb_find_window(qs->trace_buffer, NULL)) {
            EditState *e = qe_split_window(s, SW_STACKED, 75);
            if (e) {
                do_switch_to_buffer(e, "*trace*");
                e->offset = e->b->total_size;
            }
        }
        buf_init(out, buf, sizeof buf);
        for (i = 0; flags && i < qe_trace_defs_count; i++) {
            int bits = qe_trace_defs[i].flags;
            if (bits && (flags & bits) == bits) {
                buf_printf(out, ", %s", qe_trace_defs[i].name);
                flags ^= bits;
            }
        }
        put_status(s, "Tracing enabled for %s", buf + 2);
    } else {
        put_status(s, "Tracing disabled");
    }
}

void do_toggle_trace_mode(EditState *s, int argval) {
    QEmacsState *qs = s->qs;
    if (argval == NO_ARG) {
        /* toggle trace mode */
        do_set_trace_flags(s, qs->trace_flags ? 0 : EB_TRACE_ALL);
    } else {
        do_set_trace_flags(s, argval);
    }
}

void do_set_trace_options(EditState *s, const char *options) {
    const char *p = options;
    QEmacsState *qs = s->qs;
    int flags = qs->trace_flags;

    for (;;) {
        int found = 0;
        size_t i;

        p += strspn(p, " \t,");
        if (!*p)
            break;
        for (i = 0; i < qe_trace_defs_count; i++) {
            if (strmatchword(p, qe_trace_defs[i].name, &p)) {
                if (!qe_trace_defs[i].flags)
                    flags = 0;
                else
                    flags |= qe_trace_defs[i].flags;
                found = 1;
                break;
            }
        }
        if (!found) {
            put_error(s, "Unsupported flag: %s", p);
            return;
        }
    }
    do_set_trace_flags(s, flags);
}

void do_cd(EditState *s, const char *path)
{
    char buf[MAX_FILENAME_SIZE];

    canonicalize_absolute_path(s, buf, sizeof(buf), path);

    if (chdir(buf)) {
        put_error(s, "Cannot change directory to '%s'", buf);
    } else {
        if (!getcwd(buf, sizeof(buf)))
            put_error(s, "Cannot get current directory");
        else
            put_status(s, "Current directory: %s", buf);
    }
}

static void color_complete(CompleteState *cp, CompleteFunc enumerate) {
    const char *name = cp->current;
    char buf[32];
    int i, len;

    if (*name == '#') {
        for (len = 0; qe_isxdigit(name[1 + len]); len++)
            continue;
        if (len > 2 && len <= 6) {
            QEColor rgb = strtol_c(name + 1, NULL, 16);
            int shift = (6 - len) * 4;
            rgb <<= shift;
            for (i = 0; i < (0x1 << shift); i++) {
                snprintf(buf, sizeof buf, "#%06x", rgb + i);
                enumerate(cp, buf, CT_GLOB);
            }
        } else {
            for (i = 0; i < 8192; i++) {
                QEColor rgb = qe_unmap_color(i, 8192);
                snprintf(buf, sizeof buf, "#%06x", rgb & 0xFFFFFF);
                enumerate(cp, buf, CT_GLOB);
            }
        }
    } else
#if 0
    if (name[0] == 'g' && name[1] == 'r' && (name[2] == 'a' || name[2] == 'e') && name[3] == 'y') {
        snprintf(buf, sizeof buf, "%.4s", name);
        enumerate(cp, buf, CT_GLOB);
        for (i = 0; i < 100; i++) {
            snprintf(buf + 4, sizeof(buf) - 4, "%d", i);
            enumerate(cp, buf, CT_GLOB);
        }
    } else
#endif
    {
        ColorDef const *def = qe_colors;
        int count = nb_qe_colors;
        while (count > 0) {
            enumerate(cp, def->name, CT_STRX);
            def++;
            count--;
        }
        if (name[0] == 'p' && !qe_isalpha(name[1])) {
            for (i = 0; i < 8192; i++) {
                snprintf(buf, sizeof buf, "p%d", i);
                enumerate(cp, buf, CT_GLOB);
            }
        }
    }
}

static int color_sort_func(const void *p1, const void *p2)
{
    const StringItem * const *pp1 = (const StringItem * const *)p1;
    const StringItem * const *pp2 = (const StringItem * const *)p2;
    const StringItem *item1 = *pp1;
    const StringItem *item2 = *pp2;

    /* Group items by group order */
    if (item1->group != item2->group)
        return item1->group - item2->group;
    /* Use natural sort */
    return qe_strcollate(item1->str, item2->str);
}

static CompletionDef color_completion = {
    "color", color_complete,
#ifndef CONFIG_TINY
    color_print_entry,
#endif
    .sort_func = color_sort_func,
};

/* basic editing functions */

void do_bof(EditState *s)
{
    do_maybe_set_mark(s);

    if (s->mode->move_bof)
        s->mode->move_bof(s);
}

void do_eof(EditState *s)
{
    do_maybe_set_mark(s);

    if (s->mode->move_eof)
        s->mode->move_eof(s);
}

void do_bol(EditState *s)
{
    do_maybe_set_mark(s);

    if (s->mode->move_bol)
        s->mode->move_bol(s);
}

void do_eol(EditState *s)
{
    do_maybe_set_mark(s);

    if (s->mode->move_eol)
        s->mode->move_eol(s);
}

void do_word_left_right(EditState *s, int n)
{
    int dir = n < 0 ? -1 : 1;

    do_maybe_set_mark(s);

    for (; n != 0; n -= dir) {
        if (s->mode->move_word_left_right)
            s->mode->move_word_left_right(s, dir);
    }
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

static int eb_word_right(EditBuffer *b, int w, int offset) {
    int offset1;

    while (offset < b->total_size) {
        char32_t c = eb_nextc(b, offset, &offset1);
        if (qe_isword(c) == w)
            break;
        offset = offset1;
    }
    return offset;
}

static int eb_word_left(EditBuffer *b, int w, int offset) {
    int offset1;

    while (offset > 0) {
        char32_t c = eb_prevc(b, offset, &offset1);
        if (qe_isword(c) == w)
            break;
        offset = offset1;
    }
    return offset;
}

int word_right(EditState *s, int w) {
    return s->offset = eb_word_right(s->b, w, s->offset);
}

int word_left(EditState *s, int w) {
    return s->offset = eb_word_left(s->b, w, s->offset);
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
    char32_t c;

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
    s->b->mark = clamp_offset(mark, 0, s->b->total_size);
    s->offset = clamp_offset(offset, 0, s->b->total_size);
    /* activate region hilite */
    if (s->qs->hilite_region)
        s->region_style = QE_STYLE_REGION_HILITE;
}

/*---------------- Case handling ----------------*/

/* Upper / lower / capital case functions. Update offset, return isword */
/* arg: -1=lower-case, +1=upper-case, +2=capital-case */
static int eb_changecase(EditBuffer *b, int offset, int *offsetp, int arg)
{
    char buf[MAX_CHAR_BYTES];
    int len;
    char32_t ch, ch1;

    ch = eb_nextc(b, offset, offsetp);
    if (!qe_isword(ch))
        return 0;

    if (arg > 0)
        ch1 = qe_wtoupper(ch);
    else
        ch1 = qe_wtolower(ch);

    if (ch != ch1) {
        len = eb_encode_char32(b, buf, ch1);
        /* replaced char may have a different encoding len from
         * original char, such as dotless i in Turkish. */
        // XXX: make special case for Turkish locale
        offset += eb_replace(b, offset, *offsetp - offset, buf, len);
        *offsetp = offset;
    }
    return 1;
}

void do_changecase_word(EditState *s, int arg)
{
    int offset, offset1;

    offset = word_right(s, 1);
    while (offset < s->b->total_size) {
        if (!eb_changecase(s->b, offset, &offset1, arg))
            break;
        offset = offset1;
        if (arg == 2)
            arg = -2;
    }
    s->offset = offset;
}

void do_changecase_region(EditState *s, int arg)
{
    int offset;

    /* deactivate region hilite */
    s->region_style = 0;

    /* WARNING: during case change, the region offsets can change, so
       it is not so simple ! */
    /* XXX: if last char of region changes width, offset will move */
    offset = min_offset(s->offset, s->b->mark);
    for (;;) {
        if (offset >= max_offset(s->offset, s->b->mark))
              break;
        if (eb_changecase(s->b, offset, &offset, arg)) {
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
        if (s->qs->last_cmd_func != (CmdFunc)do_append_next_kill) {
            /* delete character with its combining glyphs */
            eb_delete_glyphs(s->b, s->offset, 1);
            return;
        }
        argval = 1;
    }

    /* save kill if numeric argument given */
    endpos = eb_skip_glyphs(s->b, s->offset, argval);
    do_kill(s, s->offset, endpos, argval, 0);
}

static void do_backward_delete_tab(EditState *s, int backspace) {
    /* Delete white space backward after TAB:
     * attempt to delete whitespace up to the previous tab stop.
     * If no whitespace is present, delete a character if `backspace` is non zero.
     */
    int offset = s->offset;
    int tw = s->b->tab_width > 0 ? s->b->tab_width : DEFAULT_TAB_WIDTH;
    int indent = s->indent_width > 0 ? s->indent_width : tw;
    int col = text_screen_width(s->b, eb_goto_bol(s->b, offset), offset, tw);
    if (col > 0) {
        int delta = 1 + (col - 1) % indent;
        int offset1;
        while (delta --> 0 && eb_prevc(s->b, offset, &offset1) == ' ') {
            offset = offset1;
        }
    }
    if (offset == s->offset) {
        if (!backspace)
            return;
        eb_prevc(s->b, offset, &offset);
    }
    eb_delete_range(s->b, offset, s->offset);
}

void do_backspace(EditState *s, int argval)
{
    int endpos;

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

    if (s->overwrite) {
        /* In overwrite mode, backspace overwrites the previous glyphs
           with spaces and deletes TABs and newlines. if argument is
           provided, prepend the ARG characters to the kill buffer and,
           if this block spans a line boundary, just remove it.
           Characters at the end of a line are removed, not replaced
           with spaces.
         */
        int offset1;
        int spaces = 0;
        int newlines = 0;
        int count = (argval == NO_ARG) ? 1 : argval;
        char32_t c1 = eb_nextc(s->b, s->offset, &offset1);
        endpos = s->offset;
        while (count > 0) {
            char32_t c = eb_prev_glyph(s->b, endpos, &endpos);
            if (c == '\n') {
                newlines++;
            } else
            if (c >= ' ') {
                spaces += qe_wcwidth(c);
            }
            count--;
        }
        if (newlines || c1 == '\n') {
            /* if removing the span or at end of line do not insert spaces */
            spaces = 0;
        } else
        if (c1 == '\t') {
            /* unfill the TAB: only insert spaces to preserve layout */
            int tw = s->b->tab_width > 0 ? s->b->tab_width : 8;
            int col = text_screen_width(s->b, eb_goto_bol(s->b, s->offset), s->offset, tw);
            spaces -= min_int(spaces, col % tw);
        }
        if (argval > 0) {
            do_kill(s, s->offset, endpos, -argval, 0);
        } else {
            int len;
            char buf[MAX_CHAR_BYTES];
            len = eb_encode_char32(s->b, buf, ' ');
            if (spaces == 1 && endpos + len == s->offset) {
                eb_write(s->b, endpos, buf, len);
                spaces = 0;
            } else {
                eb_delete_range(s->b, endpos, s->offset);
            }
        }
        eb_insert_spaces(s->b, endpos, spaces);
        s->offset = endpos;
        return;
    }
    if (argval == NO_ARG) {
        if ((s->qs->last_cmd_func == (CmdFunc)do_tabulate ||
             s->qs->last_cmd_func == (CmdFunc)do_backward_delete_tab)
        &&  !s->indent_tabs_mode && !s->multi_cursor_active) {
            do_backward_delete_tab(s, 1);
            s->qs->this_cmd_func = (CmdFunc)do_backward_delete_tab;
            return;
        }
        if (s->qs->last_cmd_func != (CmdFunc)do_append_next_kill) {
            /* backspace without prefix argument deletes combining accents */
            if (eb_delete_chars(s->b, s->offset, -1)) {
                /* special case for composing */
                if (s->compose_len > 0)
                    s->compose_len--;
            }
            return;
        }
        argval = 1;
    }
    /* save kill if numeric argument given,
       delete full glyphs (including combining accents) */
    endpos = eb_skip_glyphs(s->b, s->offset, -argval);
    do_kill(s, s->offset, endpos, -argval, 0);
}

/* return the cursor position relative to the screen. Note that xc is
   given in pixel coordinates */
typedef struct CursorContext {
    int linec;
    int yc;
    int xc;
    int offsetc;
    DirType basec; /* direction of the line */
    DirType dirc; /* direction of the char under the cursor */
    int cursor_width;
    int cursor_height;
} CursorContext;

static int cursor_func(DisplayState *ds,
                       int offset1, int offset2, int line_num,
                       int x, int y, int w, int h, qe__unused__ int hex_mode)
{
    CursorContext *m = ds->cursor_opaque;

    if (m->offsetc >= offset1 && m->offsetc < offset2) {
        if (w <= 0) {  /* for RTL glyphs */
            x += w;
            w = -w;
            if (w == 0) {
                /* for end of line */
                w = ds->space_width;
            }
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

typedef struct MoveContext {
    int yd;
    int xd;
    int xdmin;
    int offsetd;
} MoveContext;

/* called each time the cursor could be displayed */
static int down_cursor_func(DisplayState *ds,
                            int offset1, qe__unused__ int offset2, int line_num,
                            int x, qe__unused__ int y,
                            int w, qe__unused__ int h,
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

void do_up_down(EditState *s, int n)
{
    int dir = n < 0 ? -1 : 1;

    do_maybe_set_mark(s);

    for (; n != 0; n -= dir) {
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
}

void do_left_right(EditState *s, int n)
{
    int dir = n < 0 ? -1 : 1;

    do_maybe_set_mark(s);

    for (; n != 0; n -= dir) {
#ifndef CONFIG_TINY
        if (s->b->flags & BF_PREVIEW) {
            EditState *e = find_window(s, KEY_LEFT, NULL);
            if (e && (e->flags & WF_FILELIST)
            &&  s->qs->active_window == s
            &&  dir < 0 && eb_at_bol(s->b, s->offset)) {
                s->qs->active_window = e;
                return;
            }
        }
#endif
        if (s->mode->move_left_right)
            s->mode->move_left_right(s, dir);
    }
}

void text_move_up_down(EditState *s, int dir)
{
    MoveContext m1, *m = &m1;
    DisplayState ds1, *ds = &ds1;
    CursorContext cm;

    if (s->qs->last_cmd_func != (CmdFunc)do_up_down)
        s->up_down_last_x = -1;

    get_cursor_pos(s, &cm);
    if (cm.xc == NO_CURSOR)
        return;

    if (s->up_down_last_x == -1)
        s->up_down_last_x = cm.xc;

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
    m->xd = s->up_down_last_x;
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

void do_scroll_left_right(EditState *s, int n)
{
    DisplayState ds1, *ds = &ds1;
    int adjust;

    if (s->wrap == WRAP_TERM)
        return;

    /* compute space_width */
    display_init(ds, s, DISP_NONE, NULL, NULL);
    adjust = n * ds->space_width;
    display_close(ds);

    if (n > 0) {
        if (s->wrap == WRAP_TRUNCATE) {
            if (s->x_disp[0] == 0) {
                s->wrap = WRAP_LINE;
            } else {
                s->x_disp[0] = min_int(s->x_disp[0] + adjust, 0);
            }
        } else
        if (s->wrap == WRAP_LINE || s->wrap == WRAP_AUTO) {
            s->wrap = WRAP_WORD;
        }
    } else {
        if (s->wrap == WRAP_WORD) {
            s->wrap = WRAP_LINE;
        } else
        if (s->wrap == WRAP_LINE || s->wrap == WRAP_AUTO) {
            s->wrap = WRAP_TRUNCATE;
        } else {
            s->x_disp[0] = min_int(s->x_disp[0] + adjust, 0);
        }
    }
}

void do_scroll_up_down(EditState *s, int dir)
{
    do_maybe_set_mark(s);

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
                                  int w, qe__unused__ int h,
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
            /* adjust for accents */
            int offset = m->offsetd;
            int offset1, offset2;
            while (qe_isaccent(eb_nextc(s->b, offset, &offset1))
            &&     eb_prevc(s->b, offset, &offset2) != '\n') {
                offset = offset1;
            }
            s->offset = offset;
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
void text_mouse_goto(EditState *s, int x, int y, QEEvent *ev)
{
    QEmacsState *qs = s->qs;
    EditState *curw = qs->active_window;
    MouseGotoContext m1, *m = &m1;
    DisplayState ds1, *ds = &ds1;
    int found, start, stop;

    // TODO: check process buffer active state
    // TODO: dispatch event to window mode to handle graphics, html, shell, dired...
    // TODO: handle drag and drop

    /* disable mouse goto in incremental search */
    if (curw && curw->isearch_state) {
        // TODO: should actually end search at current spot
        return;
    }
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

    found = m->offset_found;
    if ((ev->type == QE_BUTTON_PRESS_EVENT)
    &&  !(ev->button_event.shift & KEY_STATE_SHIFT)) {
        s->mouse_down_offset = found;
    }
    start = min_offset(s->mouse_down_offset, found);
    stop = max_offset(s->mouse_down_offset, found);

    if (qs->mouse_clicks > 1) {
        if (qs->mouse_clicks == 2) {
            /* always include the character under the mouse pointer,
             * extend to the end of the word it is a word character
             */
            if (qe_isword(eb_peekc(s->b, start)))
                start = eb_word_left(s->b, 0, start);
            if (qe_isword(eb_peekc(s->b, stop)))
                stop = eb_word_right(s->b, 0, stop);
            else
                stop = eb_next(s->b, stop);
        } else
        if (qs->mouse_clicks == 3) {
            /* by line or paragraph? */
            start = eb_goto_bol(s->b, start);
            stop = eb_next_line(s->b, stop);
        } else {
            /* whole buffer */
            start = 0;
            stop = s->b->total_size;
        }
    }

    if (start == stop) {
        s->offset = found;
        s->hex_mode = m->hex_mode;
    } else {
        if (found >= s->mouse_down_offset) {
            s->b->mark = start;
            s->offset = stop;
        } else {
            s->b->mark = stop;
            s->offset = start;
        }
        s->show_selection = 1;
    }

    /* activate window unless curw is modal */
    if (!(curw && (curw->flags & (WF_POPUP | WF_MINIBUF)))
    &&  !qs->key_ctx.grab_key_cb) {
        qs->active_window = s;
    }
    if (s->mouse_force_highlight)
        s->force_highlight = 1;
}
#else
void text_mouse_goto(EditState *s, int x, int y, QEEvent *ev)
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

void do_char(EditState *s, int key, int argval) {
    int repeat = (argval == NO_ARG) ? 1 : max_int(0, argval);

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

    if (s->region_style && s->b->mark != s->offset) {
        const char *pairs = "<>[](){}''``\"\"";
        const char *p;
        if (key < 255 && (p = strchr(pairs, key)) != NULL) {
            while (repeat --> 0) {
                int index = (p - pairs) & ~1;
                const char *p1 = &pairs[index + (s->b->mark > s->offset)];
                const char *p2 = &pairs[index + (s->b->mark < s->offset)];
                eb_insert(s->b, s->b->mark, p1, 1);
                s->offset += eb_insert(s->b, s->offset, p2, 1);
            }
            return;
        }
    }

    /* Delete hilighted region */
    do_delete_selection(s);

    if (s->mode->write_char) {
        while (repeat --> 0)
            s->mode->write_char(s, key);
    }
}

#ifdef CONFIG_UNICODE_JOIN
void do_combine_accent(EditState *s, int accent_arg) {
    int offset0, len;
    char32_t g[2];
    char buf[MAX_CHAR_BYTES];
    char32_t c, accent = accent_arg;

    if (s->b->flags & BF_READONLY)
        return;

    c = eb_prevc(s->b, s->offset, &offset0);
    if (c == accent) {
        /* inserting twice the same accent removes it */
        eb_delete_range(s->b, offset0, s->offset);
    } else
    if (c != '\n'
    &&  ((expand_ligature(g, c) && g[1] == accent)
     ||  (combine_accent(g, c, accent)))
    &&  (len = eb_encode_char32(s->b, buf, g[0])) > 0) {
        /* if accent can be removed from previous ligature
           or if previous character can be combined with accent as a
           ligature, encode the single character and replace the
           previous character.
           XXX: should bypass eb_encode_char32 to detect encoding failure
         */
        offset0 += eb_replace(s->b, offset0, s->offset - offset0, buf, len);
        s->offset = offset0;
    } else {
        do_char(s, accent, 1);
    }
}
#endif

/* compute the number of screen positions between start and stop
   assuming a TAB width of tw and a fixed fitch font with single or
   double width glyphs and zero width accents.
 */
int text_screen_width(EditBuffer *b, int start, int stop, int tw) {
    int col = 0, offset = start;

    while (offset < stop) {
        char32_t c = eb_nextc(b, offset, &offset);
        if (c == '\r' || c == '\n') {
            col = 0;
        } else
        if (c == '\t') {
            col += tw - col % tw;
        } else {
            col += qe_wcwidth(c);
        }
    }
    return col;
}

void text_write_char(EditState *s, int key)
{
    int len, endpos, ret, insert;
    char buf[MAX_CHAR_BYTES];
    char32_t cur_ch, c2;

    if (check_read_only(s))
        return;

    /* Highlighted region was deleted by caller */
    /* deactivate region hilite */
    s->region_style = 0;

    cur_ch = eb_nextc(s->b, s->offset, &endpos);
    len = eb_encode_char32(s->b, buf, key);
    insert = (!s->overwrite || cur_ch == '\n' ||
              key == '\t' ||  key == '\n' || qe_isaccent(key));

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
        s->offset += eb_insert(s->b, s->offset, buf, len);

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
                    len = eb_encode_char32(s->b, buf, key);
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
        int w, w1, offset2;

        w = qe_wcwidth(key);
        if (cur_ch == '\t') {
            int tw = s->b->tab_width > 0 ? s->b->tab_width : 8;
            int col = text_screen_width(s->b, eb_goto_bol(s->b, s->offset), s->offset, tw);
            w1 = tw - col % tw;
            if (w < w1) {
                s->offset += eb_insert(s->b, s->offset, buf, len);
                return;
            }
        } else {
            w1 = qe_wcwidth(cur_ch);
            endpos = eb_skip_accents(s->b, endpos);
        }
        if (w > w1) {
            c2 = eb_next_glyph(s->b, endpos, &offset2);
            // XXX: potential issue if c2 is a TAB
            if (c2 >= ' ') {
                endpos = offset2;
                w1 += qe_wcwidth(c2);
            }
        }
        s->offset += eb_replace(s->b, s->offset, endpos - s->offset, buf, len);
        if (w1 > w) {
            c2 = eb_nextc(s->b, s->offset, &offset2);
            if (c2 >= ' ')
                eb_insert_spaces(s->b, s->offset, w1 - w);
        }
    }
}

struct QuoteKeyArgument {
    EditState *s;
    int has_arg;
    int argval;
};

/* XXX: may be better to move it into qe_key_process() */
static void quote_key(void *opaque, int key)
{
    // XXX: emacs supports octal input followed by RET
    //      and f1 for context sensitive help
    //      qemacs supports special keys and inserts the keyboard sequence
    struct QuoteKeyArgument *qa = opaque;
    EditState *s = qa->s;
    QEmacsState *qs = s->qs;
    int repeat = qa->argval;

    put_status(s, "");  /* erase "Quote: " message */
    /* Achtung! this should free the grab data */
    qe_ungrab_keys(qs);

    if (!s)
        return;

    if (s->b->flags & BF_READONLY)
        return;

    /* Delete hilighted region */
    do_delete_selection(s);

    if (s->mode->write_char) {
        int save_overwrite = s->overwrite;
        /* quoted-insert always inserts characters */
        s->overwrite = 0;
        while (repeat --> 0) {
            if (KEY_IS_SPECIAL(key)) {
                /* Insert the byte sequence received from the terminal */
                int i;
                for (i = 0; i < qs->input_len; i++)
                    s->mode->write_char(s, qs->input_buf[i]);
            } else {
                s->mode->write_char(s, key);
            }
        }
        s->overwrite = save_overwrite;
        qe_display(qs);
    }
}

void do_quoted_insert(EditState *s, int argval) {
    QEmacsState *qs = s->qs;
    struct QuoteKeyArgument *qa = qe_mallocz(struct QuoteKeyArgument);

    if (qa == NULL) // FIXME: error message?
        return;

    qa->s = s;
    qa->has_arg = (argval != NO_ARG);
    qa->argval = qa->has_arg ? argval : 1;

    qe_grab_keys(qs, quote_key, qa);
    put_status(s, "Quote: ");
}

void do_overwrite_mode(EditState *s, int argval) {
    /*@CMD overwrite-mode
       ### `overwrite-mode(argval)`

       Toggle overwrite mode.

       With a prefix argument, turn overwrite mode on if the argument
       is positive, otherwise select insert mode.
       In overwrite mode, characters entered into a buffer replace
       existing text without moving the rest of the line, rather than
       shifting it to the right.  Characters typed before a newline
       extend the line.  Space created by TAB characters is filled until
       the tabulation stop is reached.
       Backspace erases the previous character and sets point to it.
       C-q still inserts characters in overwrite mode as a convenient way
       to insert characters when necessary.
     */

    if (argval == NO_ARG)
        s->overwrite = !s->overwrite;
    else
        s->overwrite = (argval > 0);

    put_status(s, "Overwrite mode is %s", s->overwrite ? "on" : "off");
}

/*---------------- indentation ----------------*/

/* get the indentation width at a given offset */
int find_indent(EditState *s, int offset, int pos, int *offsetp) {
    int tw = s->b->tab_width > 0 ? s->b->tab_width : 8;
    int offset1;
    for (;; offset = offset1) {
        char32_t c = eb_nextc(s->b, offset, &offset1);
        if (c == '\t')
            pos += tw - (pos % tw);
        else if (c == ' ')
            pos++;
        else
            break;
    }
    *offsetp = offset;
    return pos;
}

/* replace characters in region with specified TABs and spaces */
static int replace_indent(EditState *s, int offset, int offset2,
                          int ntabs, int nspaces)
{
    int offset1;

    while (offset < offset2) {
        char32_t c = eb_nextc(s->b, offset, &offset1);
        if (c == '\t' && ntabs) {
            ntabs--;
        } else
        if (c == ' ' && !ntabs && nspaces) {
            nspaces--;
        } else {
            break;
        }
        offset = offset1;
    }
    if (offset2 > offset)
        eb_delete_range(s->b, offset, offset2);
    if (ntabs)
        offset += eb_insert_char32_n(s->b, offset, '\t', ntabs);
    if (nspaces)
        offset += eb_insert_spaces(s->b, offset, nspaces);
    return offset;
}

int make_indent(EditState *s, int offset, int offset2, int pos, int target) {
    int tabs = 0, spaces = 0;
    if (target > pos) {
        spaces = target - pos;
        if (s->indent_tabs_mode) {
            int tw = s->b->tab_width > 0 ? s->b->tab_width : 8;
            int incr = tw - pos % tw;
            if (spaces >= incr) {
                tabs++;
                spaces -= incr;
                tabs += spaces / tw;
                spaces = spaces % tw;
            }
        }
    }
    return replace_indent(s, offset, offset2, tabs, spaces);
}

void do_indent_rigidly_by(EditState *s, int start, int end, int argval)
{
    int col_num, line, line1, line2;

    /* Swap point and mark so mark <= point */
    if (end < start) {
        int tmp = start;
        start = end;
        end = tmp;
    }
    /* We do it with lines to avoid offset variations during indenting */
    eb_get_pos(s->b, &line1, &col_num, start);
    if (start == end) {
        line2 = line1;
    } else {
        if (col_num > 0)
            line1++;
        eb_get_pos(s->b, &line2, &col_num, end);
        if (col_num == 0)
            line2--;
    }
    /* Iterate over all lines inside block */
    for (line = line1; line <= line2; line++) {
        int offset = eb_goto_pos(s->b, line, 0);
        int off1, off2;
        int indent = find_indent(s, offset, 0, &off1);
        int new_indent = max_int(0, indent + argval);
        /* if line is empty, remove indentation,
         * otherwise adjust it by argval columns.
         */
        if (eb_nextc(s->b, off1, &off2) == '\n')
            new_indent = 0;
        make_indent(s, offset, off1, 0, new_indent);
    }
}

void do_indent_rigidly_to_tab_stop(EditState *s, int start, int end, int dir) {
    int tw = s->b->tab_width > 0 ? s->b->tab_width : DEFAULT_TAB_WIDTH;
    int indent = s->indent_width > 0 ? s->indent_width : tw;

    do_indent_rigidly_by(s, start, end, indent * dir);
}

void do_tabulate(EditState *s, int argval)
{
    int tw = s->b->tab_width > 0 ? s->b->tab_width : DEFAULT_TAB_WIDTH;
    int indent = s->indent_width > 0 ? s->indent_width : tw;
    int offset = s->offset;

#ifndef CONFIG_TINY
    if (s->b->flags & BF_PREVIEW) {
        EditState *e = find_window(s, KEY_LEFT, NULL);
        if (e && (e->flags & WF_FILELIST)
        &&  s->qs->active_window == s) {
            s->qs->active_window = e;
            return;
        }
    }
#endif
    if (s->b->flags & BF_READONLY)
        return;

    if (s->region_style) {
        do_indent_rigidly_by(s, s->b->mark, offset, indent * argval);
        return;
    }
    if (argval < 0) {
        do_indent_rigidly_by(s, offset, offset, indent * argval);
        return;
    }

    // TODO: should do smart complete, smart indent, insert tab
    if (s->indent_tabs_mode) {
        s->offset += eb_insert_char32_n(s->b, offset, '\t', argval);
    } else {
        int col = text_screen_width(s->b, eb_goto_bol(s->b, offset), offset, tw);
        s->offset += eb_insert_spaces(s->b, offset, indent * argval - (col % indent));
    }
}

static void do_untabulate(EditState *s) {
    if (s->region_style) {
        /* unindent the whole highlighted region */
        do_indent_rigidly_to_tab_stop(s, s->b->mark, s->offset, -1);
        return;
    }
    /* decrease indentation at s->offset */
    do_backward_delete_tab(s, 0);
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

void do_newline(EditState *s)
{
#ifndef CONFIG_TINY
    if (s->b->flags & BF_PREVIEW) {
        do_preview_mode(s, -1);
        return;
    }
#endif
    if (s->b->flags & BF_READONLY)
        return;

    s->offset += eb_insert_char32(s->b, s->offset, '\n');
}

void do_open_line(EditState *s)
{
    if (s->b->flags & (BF_PREVIEW | BF_READONLY))
        return;

    /* preserve s->offset */
    eb_insert_char32(s->b, s->offset, '\n');
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

static void do_unknown_key(EditState *s) {
    QEmacsState *qs = s->qs;
    char buf[80];
    buf_t out[1];
    int i;

    buf_init(out, buf, sizeof buf);
    for (i = 0; i < qs->input_len; i++)
        buf_quote_byte(out, qs->input_buf[i]);
    put_error(s, "Unknown key: %s", buf);
}

void do_keyboard_quit(EditState *s)
{
    if (s->flags & WF_POPUP) {
        do_popup_exit(s);
        return;
    }
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
    s->multi_cursor_active = 0;

    /* well, currently nothing needs to be aborted in global context */
    /* CG: Should remove sidepanes, helppanes... */
    put_status(s, "|");
    put_error(s, "Quit");
}

/* block functions */
void do_set_mark(EditState *s)
{
    do_mark_region(s, s->offset, s->offset);
    put_status(s, "Mark set");
}

void do_maybe_set_mark(EditState *s)
{
    if (!s->region_style && is_shift_key(s->qs->last_key)) {
        do_set_mark(s);
    }
}

void do_mark_whole_buffer(EditState *s)
{
    do_mark_region(s, s->b->total_size, 0);
}

/* base buffer may be null */
EditBuffer *qe_new_yank_buffer(QEmacsState *qs, EditBuffer *base)
{
    char bufname[32];
    EditBuffer *b;
    int cur = qs->yank_current;

    if (qs->yank_buffers[cur]) {
        cur = (cur + 1) % NB_YANK_BUFFERS;
        qs->yank_current = cur;
        /* Maybe should instead just clear the buffer and reset styles */
        qe_kill_buffer(qs, qs->yank_buffers[cur]);
        // XXX: qe_kill_buffer should have cleared this already
        qs->yank_buffers[cur] = NULL;
    }
    snprintf(bufname, sizeof(bufname), "*kill-%d*", cur + 1);
    b = qe_new_buffer(qs, bufname, BF_SYSTEM);
    if (!b)
        return NULL;
    if (base) {
        eb_set_charset(b, base->charset, base->eol_type);
        if (base->flags & BF_STYLES)
            eb_create_style_buffer(b, base->flags);
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
    QEmacsState *qs = s->qs;
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
        b = qe_new_yank_buffer(qs, s->b);
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

void do_kill_region(EditState *s) {
    do_kill(s, s->b->mark, s->offset, 1, 0);
}

void do_copy_region(EditState *s) {
    do_kill(s, s->b->mark, s->offset, 0, 1);
}

void do_kill_line(EditState *s, int argval)
{
    int p1, p2, offset1, dir = 1;

    // XXX: should handle kill_whole_line variable
    // XXX: can there be a variable and a function with the same name?
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

void do_kill_whole_line(EditState *s, int n)
{
    // XXX: should not modify s->offset
    // XXX: should fix behavior for binary and hex modes
    int p1 = 0, p2 = 0, dir = n;
    if (n < 0) {
        do_eol(s);
        p1 = s->offset;
        while (n++ < 0 && s->offset > 0) {
            do_bol(s);
            s->offset = eb_prev(s->b, s->offset);
        }
        p2 = s->offset;
    } else
    if (n > 0) {
        do_bol(s);
        p1 = s->offset;
        while (n-- > 0 && s->offset < s->b->total_size) {
            do_eol(s);
            s->offset = eb_next(s->b, s->offset);
        }
        p2 = s->offset;
    }
    if (p1 != p2)
        do_kill(s, p1, p2, dir, 0);
}

void do_kill_word(EditState *s, int n)
{
    int start = s->offset;

    if (n != 0) {
        do_word_left_right(s, n);
        do_kill(s, start, s->offset, n, 0);
    }
}

void do_yank(EditState *s) {
    /* The behavior is different from emacs:
       emacs: with a C-u prefix, set mark at the end and point at the
         beginning of the yanked block. With a numeric prefix, yank
         the n-th element of the kill-ring
       qemacs: with a C-u prefix, yank n copies of the last killed block
     */
    int size;
    QEmacsState *qs = s->qs;
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
    QEmacsState *qs = s->qs;

    if (qs->last_cmd_func != (CmdFunc)do_yank) {
        put_error(s, "Previous command was not a yank");
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
            put_error(s, "Could not load '%s'", b->filename);
        } else {
            put_error(s, "Error while reloading '%s'", b->filename);
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
            md->qs = b->qs;
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
        put_error(e, "Not a %s buffer", m->name);

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
            md->qs = s->qs;
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
        put_error(e, "Not a %s buffer", m->name);

    return NULL;
}

void *check_mode_data(void **pp) {
    QEModeData *md = *pp;
    QEmacsState *qs;
    EditBuffer *b;
    EditState *e;

    if (md == NULL)
        return NULL;

    qs = md->qs;
    for (b = qs->first_buffer; b != NULL; b = b->next) {
        QEModeData **mdp;
        for (mdp = &b->mode_data_list; *mdp; mdp = &(*mdp)->next) {
            if (*mdp == md)
                return md;
        }
    }
    for (e = qs->first_window; e != NULL; e = e->next_window) {
        if (e->mode_data == md)
            return md;
    }
    return NULL;
}

int qe_free_mode_data(QEModeData *md)
{
    int rc = -1;

    if (!md)
        return 0;

    if (qe_check_buffer(md->qs, &md->b)) {
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
    if (qe_check_window(md->qs, &md->s)) {
        if (md->s->mode_data == md) {
            md->s->mode_data = NULL;
            rc = 0;
        }
    }
    if (rc == 0) {
        /* mode data was found, OK to free */
        qe_free(&md);
    }
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
        set_colorize_mode(s, NULL);

        /* XXX: this code makes no sense, if must be reworked! */
#if 0
        int data_count;
        EditState *e;

        /* try to remove the raw or mode specific data if it is no
           longer used. */
        data_count = 0;
        for (e = s->qs->first_window; e != NULL; e = e->next_window) {
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
            put_error(s, "Cannot set mode %s: %s", m->name, errstr);
            m = &text_mode;
            rc = -1;
        }

        s->mode = m;

        /* init mode */
        generic_mode_init(s);
        s->wrap = m->default_wrap;
        m->mode_init(s, s->b, MODEF_VIEW | mode_flags);
        if (m->colorize_func)
            set_colorize_mode(s, m);
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

    /* set-mode from the dired window applies to the target window */
    s = qe_find_target_window(s, 0);

    /* XXX: should check if mode is appropriate */
    m = qe_find_mode(s->qs, name, 0);
    if (m)
        edit_set_mode(s, m);
    else
        put_error(s, "No mode %s", name);
}

QECharset *qe_parse_charset(EditState *s, const char *charset_str, EOLType *eol_typep)
{
    char buf[64];
    const char *p;
    QECharset *charset;
    EOLType eol_type = *eol_typep;

    pstrcpy(buf, sizeof(buf), charset_str);

    p = NULL;
    if (strend(buf, "-mac", &p))
        eol_type = EOL_MAC;
    else
    if (strend(buf, "-dos", &p))
        eol_type = EOL_DOS;
    else
    if (strend(buf, "-unix", &p))
        eol_type = EOL_UNIX;

    if (p) {
        /* strip suffix */
        buf[p - buf] = '\0';
    }
    charset = qe_find_charset(s->qs, buf);
    if (!charset) {
        put_error(s, "Unknown charset '%s'", charset_str);
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
    charset = qe_parse_charset(s, charset_str, &eol_type);
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
    charset = qe_parse_charset(s, charset_str, &eol_type);
    if (!charset)
        return;

    b = s->b;

    b1 = qe_new_buffer(s->qs, "*tmp*", b->flags & BF_STYLES);
    if (!b1)
        return;
    eb_set_charset(b1, charset, eol_type);

    /* preserve positions */
    cb = b->first_callback;
    for (i = 0; i < countof(pos) && cb; cb = cb->next) {
        if (cb->callback == eb_offset_callback) {
            int *offsetp = (int *)cb->opaque;
            pos[i] = eb_get_char_offset(b, *offsetp);
            i++;
        }
    }

    // XXX: should use eb_insert_buffer_convert()
    /* slow, but simple iterative method */
    for (offset = 0; offset < b->total_size;) {
        QETermStyle style = eb_get_style(b, offset);
        char32_t c = eb_nextc(b, offset, &offset);
        b1->cur_style = style;
        len = eb_encode_char32(b1, buf, c);
        eb_insert(b1, b1->total_size, buf, len);
    }

    /* replace current buffer with conversion */
    /* quick hack to transfer styles from tmp buffer to b */
    eb_free(&b->b_styles);
    eb_delete(b, 0, b->total_size);
    eb_set_charset(b, charset, eol_type);
    // XXX: this does not transfer styles
    //      should use eb_insert_buffer_convert()
    eb_insert_buffer(b, 0, b1, 0, b1->total_size);
    b->b_styles = b1->b_styles;
    b1->b_styles = NULL;

    /* restore positions */
    cb = b->first_callback;
    for (i = 0; i < countof(pos) && cb; cb = cb->next) {
        if (cb->callback == eb_offset_callback) {
            int *offsetp = (int *)cb->opaque;
            *offsetp = eb_goto_char(b, pos[i]);
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

static void update_setting(EditState *s, const char *name, int *pval, int argval) {
    *pval = (argval == NO_ARG) ? !*pval : (argval > 0);
    s->qs->complete_refresh = 1;
    put_status(s, "%s %s", name, *pval ? "enabled" : "disabled");
}

static void do_line_number_mode(EditState *s, int argval) {
    update_setting(s, "line-number-mode", &s->qs->line_number_mode, argval);
}

static void do_column_number_mode(EditState *s, int argval) {
    update_setting(s, "column-number-mode", &s->qs->column_number_mode, argval);
}

static void do_global_linum_mode(EditState *s, int argval) {
    update_setting(s, "global-linum-mode", &s->qs->global_linum_mode, argval);
}

static int has_linum_mode(EditState *s) {
    return (s->b->linum_mode_set ? s->b->linum_mode :
            (s->qs->global_linum_mode &&
             !(s->b->flags & (BF_DIRED | BF_SHELL)) &&
             !(s->flags & (WF_POPUP | WF_MINIBUF))));
}

static void do_linum_mode(EditState *s, int argval) {
    s->b->linum_mode = has_linum_mode(s);
    s->b->linum_mode_set = 1;
    update_setting(s, "linum-mode", &s->b->linum_mode, argval);
}

void do_toggle_truncate_lines(EditState *s)
{
    if (s->wrap == WRAP_TERM)
        return;

    if (s->wrap == WRAP_TRUNCATE) {
        s->wrap = WRAP_LINE;
        s->x_disp[0] = s->x_disp[1] = 0;
    } else {
        s->wrap = WRAP_TRUNCATE;
    }
}

void do_word_wrap(EditState *s)
{
    if (s->wrap == WRAP_TERM)
        return;

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
    pos = strtol_c(str, &p, 0);

    /* skip space required to separate hex offset from b or c suffix */
    if (*p == ' ')
        p++;

    /* handle an optional multiple suffix */
    switch (*p) {
    case 'g':
        pos *= 1000;
        fallthrough;
    case 'm':
        pos *= 1000;
        fallthrough;
    case 'k':
        pos *= 1000;
        p++;
        break;
    case 'G':
        pos *= 1024;
        fallthrough;
    case 'M':
        pos *= 1024;
        fallthrough;
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
        s->offset = clamp_offset(pos, 0, s->b->total_size);
        return;
    case 'c':
        if (*p)
            goto error;
        if (rel)
            pos += eb_get_char_offset(s->b, s->offset);
        s->offset = eb_goto_char(s->b, max_offset(0, pos));
        return;
    case '%':
        /* CG: should not require long long for this */
        pos = pos * (long long)s->b->total_size / 100;
        if (rel)
            pos += s->offset;
        eb_get_pos(s->b, &line, &col, clamp_offset(pos, 0, s->b->total_size));
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
            col = strtol_c(p + 1, &p, 0);
            col -= (col > 0);  // user column numbers are 1-based
        }
        if (*p)
            goto error;
        // XXX: col should be a display column, not a character number
        s->offset = eb_goto_pos(s->b, max_offset(0, line), col);
        return;
    }
error:
    put_error(s, "Invalid position: %s", str);
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
    char32_t accents[6];
    buf_t outbuf, *out;
    int line_num, col_num;
    int offset1, off;
    int w, v;
    int i, n;
    char32_t c, cc;

    out = buf_init(&outbuf, buf, sizeof(buf));
    if (s->offset < s->b->total_size) {
        c = eb_nextc(s->b, s->offset, &offset1);
        n = 0;
        if (c != '\n' && !s->unihex_mode) {
            while (n < countof(accents) && qe_isaccent(cc = eb_nextc(s->b, offset1, &off))) {
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
        if (c < 127 || (c >= 160 && c <= MAX_UNICODE_DISPLAY)) {
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
            buf_printf(out, " %u", c);
        }
        buf_printf(out, " 0x%02x", c);
        for (i = 0; i < n; i++) {
            buf_printf(out, "/0x%02x", accents[i]);
        }
        /* Display buffer bytes if char is encoded */
        if (offset1 != s->offset + 1 || c != (u8)eb_read_one_byte(s->b, s->offset)) {
            int sep = '[';
            buf_put_byte(out, ' ');
            for (off = s->offset; off < offset1; off++) {
                cc = eb_read_one_byte(s->b, off);
                buf_printf(out, "%c%02X", sep, cc);
                sep = ' ';
            }
            buf_put_byte(out, ']');
        }
        w = qe_wcwidth(c);
        if (w != 1)
            buf_printf(out, " w=%d", w);
        v = qe_wcwidth_variant(c);
        if (v)
            buf_printf(out, " v=%d", v);
        if (s->b->style_bytes) {
            int bits = s->b->style_bytes * 8;
            unsigned long long style = eb_get_style(s->b, s->offset);
            buf_printf(out, " style={%0*llX", bits / 8, style);
            if (bits == 8 || bits == QE_TERM_STYLE_BITS) {
                buf_put_byte(out, ':');
                if (style & QE_TERM_UNDERLINE)
                    buf_puts(out, " underline");
                if (style & QE_TERM_BOLD)
                    buf_puts(out, " bold");
                if (style & QE_TERM_ITALIC)
                    buf_puts(out, " italic");
                if (style & QE_TERM_BLINK)
                    buf_puts(out, " blink");
                if (style & QE_TERM_COMPOSITE) {
                    buf_printf(out, " %d/%d", QE_TERM_GET_FG(style),
                               QE_TERM_GET_BG(style));
                } else {
                    buf_printf(out, " %s", qe_styles[style].name);
                }
            }
            buf_put_byte(out, '}');
        }
    }
    eb_get_pos(s->b, &line_num, &col_num, s->offset);
    put_status(s, "%s  point=%d mark=%d size=%d region=%d col=%d",
               out->buf, s->offset, s->b->mark, s->b->total_size,
               abs(s->offset - s->b->mark), col_num + 1);
}

void do_set_tab_width(EditState *s, int tab_width)
{
    if (tab_width > 1)
        s->b->tab_width = tab_width;
}

void do_set_indent_width(EditState *s, int indent_width)
{
    if (indent_width > 1)
        s->indent_width = indent_width;
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

static char *qe_get_mode_name(EditState *s, char *buf, int size, int full)
{
    buf_t outbuf, *out;

    out = buf_init(&outbuf, buf, size);

    if (s->b->data_type_name)
        buf_printf(out, "%s+", s->b->data_type_name);
    buf_puts(out, s->mode ? s->mode->name : "raw");

    if (full) {
        if (s->overwrite)
            buf_puts(out, " Ovwrt");
        if (s->interactive)
            buf_puts(out, " Interactive");
        if (s->b->flags & BF_PREVIEW)
            buf_puts(out, " Preview");
    }
    return buf;
}

/* compute string for the first part of the mode line (flags,
   filename, modename) */
void basic_mode_line(EditState *s, buf_t *out, int c1)
{
    char buf[128];
    const char *mode_name;
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

    mode_name = qe_get_mode_name(s, buf, sizeof(buf), 1);
    /* Strip text mode name if another mode is also active */
    strstart(mode_name, "text ", &mode_name);

    buf_printf(out, "%c%c:%c%c  %-20s  (%s)",
               c1, state, s->b->flags & BF_READONLY ? '%' : mod,
               mod, s->b->name, mode_name);
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
    if (s->qs->line_number_mode)
        buf_printf(out, "--L%d", line_num + 1);
    if (s->qs->column_number_mode)
        buf_printf(out, "--C%d", col_num + 1);
    buf_printf(out, "--%s", s->b->charset->name);
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
        buf_printf(out, "--%s", (char*)tag->data);
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
                          s->qs->mode_line_height,
                          buf, QE_STYLE_MODE_LINE);
            pstrcpy(s->modeline_shadow, sizeof(s->modeline_shadow), buf);
        }
    }
}

void display_window_borders(EditState *e)
{
    QEmacsState *qs = e->qs;

    if (e->borders_invalid) {
        if (e->flags & (WF_POPUP | WF_RSEPARATOR)) {
            CSSRect rect;
            QEColor color;
            int x = e->x1;
            int y = e->y1;
            int width = e->x2 - e->x1;
            int height = e->y2 - e->y1;

            rect.x1 = 0;
            rect.y1 = 0;
            rect.x2 = qs->width;
            rect.y2 = qs->height;
            /* XXX: should use popup size? */
            set_clip_rectangle(qs->screen, &rect);
            color = qe_styles[QE_STYLE_WINDOW_BORDER].bg_color;
            if (e->flags & WF_POPUP) {
                /* XXX: should use client area instead of recomputing it */
                int top_h = e->caption ? qs->mode_line_height : qs->border_width;
                int bottom_h = qs->border_width;
                int left_w = qs->border_width;
                int right_w = qs->border_width;

                fill_rectangle(qs->screen, x, y, width, top_h, color);
                fill_rectangle(qs->screen, x, y + bottom_h,
                               left_w, height - top_h - bottom_h, color);
                fill_rectangle(qs->screen, x + width - right_w, y + top_h,
                               right_w, height - top_h - bottom_h, color);
                fill_rectangle(qs->screen, x, y + height - bottom_h,
                               width, bottom_h, color);
                /* display caption */
                if (e->caption) {
                    QEStyleDef styledef;
                    QECharMetrics metrics;
                    QEFont *font;
                    char32_t buf[256];
                    int len;

                    /* XXX: Should convert from UTF-8? */
                    for (len = 0; len < 256 && e->caption[len]; len++) {
                        buf[len] = e->caption[len];
                    }
                    get_style(e, &styledef, QE_STYLE_WINDOW_BORDER);
                    font = select_font(qs->screen,
                                       styledef.font_style, styledef.font_size);
                    text_metrics(qs->screen, font, &metrics, buf, len);
                    draw_text(qs->screen, font,
                              x + width / 2 - metrics.width / 2, y + metrics.font_ascent,
                              buf, len, styledef.fg_color);
                    release_font(qs->screen, font);
                }
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

void fill_window_slack(EditState *s, int x, int y, int w, int h, int color)
{
    /* fill the window space outside a given rectangle */
    int x0, y0, w0, h0, w1, w2, h1, h2;

    /* fill the background */
    x0 = s->xleft;
    y0 = s->ytop;
    w0 = s->width;
    h0 = s->height;
    w1 = max_int(0, x);
    w2 = max_int(0, w0 - (x + w));
    h1 = max_int(0, y);
    h2 = max_int(0, h0 - (y + h));

    if (w1) fill_rectangle(s->screen, x0, y0, w1, h0, color);
    if (w2) fill_rectangle(s->screen, x0 + w0 - w2, y0, w2, h0, color);
    if (h1) fill_rectangle(s->screen, x0 + w1, y0, w0 - w1 - w2, h1, color);
    if (h2) fill_rectangle(s->screen, x0 + w1, y0 + h0 - h2, w0 - w1 - w2, h2, color);
}

#if 1
/* Should move all this to display.c */

/* compute style */
static void apply_style(QEStyleDef *stp, QETermStyle style)
{
    QEStyleDef *s;

    if (style & QE_TERM_COMPOSITE) {
        int fg = QE_TERM_GET_FG(style);
        int bg = QE_TERM_GET_BG(style);
        if (style & QE_TERM_BOLD) {
            stp->font_style |= QE_FONT_STYLE_BOLD;
            if (fg < 8) {
                // FIXME: this is a hack for 16 color mode
                fg |= 8;
            }
        }
        if (style & QE_TERM_UNDERLINE)
            stp->font_style |= QE_FONT_STYLE_UNDERLINE;
        if (style & QE_TERM_ITALIC)
            stp->font_style |= QE_FONT_STYLE_ITALIC;
        if (style & QE_TERM_BLINK)
            stp->font_style |= QE_FONT_STYLE_BLINK;
        // FIXME: should support transparent background
        stp->fg_color = qe_unmap_color(fg, QE_TERM_FG_COLORS);
        stp->bg_color = qe_unmap_color(bg, QE_TERM_BG_COLORS);
    } else {
        s = &qe_styles[style & QE_STYLE_NUM];
        if (s->fg_color != COLOR_TRANSPARENT)
            stp->fg_color = s->fg_color;
        if (s->bg_color != COLOR_TRANSPARENT)
            stp->bg_color = s->bg_color;
        if (s->font_style != 0)
            stp->font_style = s->font_style;
        if (s->font_size != 0)
            stp->font_size = s->font_size;
    }
    /* for selection, we need a special handling because only color is
           changed */
    if (style & QE_STYLE_SEL) {
        s = &qe_styles[QE_STYLE_SELECTION];
        stp->fg_color = s->fg_color;
        stp->bg_color = s->bg_color;
    }
}

void get_style(EditState *e, QEStyleDef *stp, QETermStyle style)
{
    /* get root default style */
    *stp = qe_styles[0];

    /* apply window default style */
    if (e && e->default_style != 0)
        apply_style(stp, e->default_style);

    /* apply specific style */
    if (style != 0)
        apply_style(stp, style);
}

void style_complete(CompleteState *cp, CompleteFunc enumerate) {
    int i;
    QEStyleDef *stp;

    stp = qe_styles;
    for (i = 0; i < QE_STYLE_NB; i++, stp++) {
        enumerate(cp, stp->name, CT_GLOB);
    }
}

int find_style_index(const char *name)
{
    int i;
    QEStyleDef *stp;

    stp = qe_styles;
    for (i = 0; i < QE_STYLE_NB; i++, stp++) {
        if (strequal(stp->name, name))
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

static CompletionDef style_completion = {
    .name = "style",
    .enumerate = style_complete,
#ifndef CONFIG_TINY
    .print_entry = style_print_entry,
#endif
};

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

void style_property_complete(CompleteState *cp, CompleteFunc enumerate) {
    int i;

    for (i = 0; i < countof(qe_style_properties); i++) {
        enumerate(cp, qe_style_properties[i], CT_STRX);
    }
}

static CompletionDef style_property_completion = {
    .name = "style-property",
    .enumerate = style_property_complete,
};

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
    QEStyleDef *stp;
    int v, prop_index;

    stp = find_style(stylestr);
    if (!stp) {
        put_error(e, "Unknown style '%s'", stylestr);
        return;
    }

    prop_index = find_style_property(propstr);
    if (prop_index < 0) {
        put_error(e, "Unknown property '%s'", propstr);
        return;
    }

    switch (prop_index) {
    case CSS_PROP_COLOR:
        if (css_get_color(&stp->fg_color, value))
            goto bad_color;
        break;
    case CSS_PROP_BACKGROUND_COLOR:
        if (css_get_color(&stp->bg_color, value))
            goto bad_color;
        break;
    bad_color:
        put_error(e, "Unknown color '%s'", value);
        return;
    case CSS_PROP_FONT_FAMILY:
        v = css_get_font_family(value);
        stp->font_style = (stp->font_style & ~QE_FONT_FAMILY_MASK) | v;
        break;
    case CSS_PROP_FONT_STYLE:
        /* XXX: cannot handle inherit correctly */
        v = stp->font_style;
        if (strequal(value, "italic")) {
            v |= QE_FONT_STYLE_ITALIC;
        } else
        if (strequal(value, "normal")) {
            v &= ~QE_FONT_STYLE_ITALIC;
        }
        stp->font_style = v;
        break;
    case CSS_PROP_FONT_WEIGHT:
        /* XXX: cannot handle inherit correctly */
        v = stp->font_style;
        if (strequal(value, "bold")) {
            v |= QE_FONT_STYLE_BOLD;
        } else
        if (strequal(value, "normal")) {
            v &= ~QE_FONT_STYLE_BOLD;
        }
        stp->font_style = v;
        break;
    case CSS_PROP_FONT_SIZE:
        if (strequal(value, "inherit")) {
            stp->font_size = 0;
        } else {
            stp->font_size = strtol(value, NULL, 0);
        }
        break;
    case CSS_PROP_TEXT_DECORATION:
        /* XXX: cannot handle inherit correctly */
        if (strequal(value, "none")) {
            stp->font_style &= ~QE_FONT_STYLE_UNDERLINE;
        } else
        if (strequal(value, "underline")) {
            stp->font_style |= QE_FONT_STYLE_UNDERLINE;
        }
        break;
    }
    e->qs->complete_refresh = 1;
}

void do_define_color(EditState *e, const char *name, const char *value)
{
    if (css_define_color(name, value))
        put_error(e, "Invalid color '%s'", value);
}
#endif

void do_set_display_size(qe__unused__ EditState *s, int w, int h)
{
    if (w != NO_ARG && h != NO_ARG) {
        screen_width = w;
        screen_height = h;
    }
}

/* NOTE: toggle-full-screen zooms the current pane to the whole screen if
   possible. It does not hide the modeline not the status line */
void do_toggle_full_screen(EditState *s)
{
    QEmacsState *qs = s->qs;
    QEditScreen *screen = s->screen;

    if (screen->dpy.dpy_full_screen) {
        qs->is_full_screen = !qs->is_full_screen;
        screen->dpy.dpy_full_screen(screen, qs->is_full_screen);
        do_refresh(s);
    } else {
        put_error(s, "Full screen unsupported on this device");
    }
}

void do_toggle_mode_line(EditState *s)
{
    s->flags ^= WF_MODELINE;
    do_refresh(s);
}

void do_set_window_style(EditState *s, const char *stylestr)
{
    int style_index = find_style_index(stylestr);

    if (style_index < 0) {
        put_error(s, "Unknown style '%s'", stylestr);
        return;
    }
    s->default_style = style_index;
}

void do_set_system_font(EditState *s, const char *qe_font_name,
                        const char *system_fonts)
{
    QEmacsState *qs = s->qs;
    int font_type;

    font_type = css_get_enum(qe_font_name, "fixed,serif,sans");
    if (font_type < 0) {
        put_error(s, "Invalid qemacs font");
        return;
    }
    pstrcpy(qs->system_fonts[font_type], sizeof(qs->system_fonts[0]),
            system_fonts);
    //qs->complete_refresh = 1;
}

static void display_bol_bidir(DisplayState *ds, DirType base,
                              int embedding_level_max)
{
    ds->base = base;
    ds->x = ds->x_disp = ds->edit_state->x_disp[base];
    if (ds->base == DIR_RTL) {
        /* XXX: probably broken. bidir handling needs fixing */
        ds->x_start = ds->edit_state->width - ds->x;
    } else {
        ds->x_start = ds->x;
    }
    ds->left_gutter = 0;
    ds->x_line = ds->x_start;
    ds->style = 0;
    ds->last_style = 0;
    ds->fragment_index = 0;
    ds->line_index = 0;
    ds->nb_fragments = 0;
    ds->word_index = 0;
    ds->embedding_level_max = embedding_level_max;
    ds->last_word_space = 0;
}

void display_bol(DisplayState *ds)
{
    display_bol_bidir(ds, DIR_LTR, 0);
}

void display_close(DisplayState *ds)
{
}

void display_init(DisplayState *ds, EditState *e, enum DisplayType do_disp,
                  int (*cursor_func)(DisplayState *ds,
                                     int offset1, int offset2, int line_num,
                                     int x, int y, int w, int h, int hex_mode),
                  void *cursor_opaque)
{
    QEFont *font;
    QEStyleDef styledef;

    memset(ds, 0, sizeof(*ds));
    ds->edit_state = e;
    ds->do_disp = do_disp;
    ds->cursor_func = cursor_func;
    ds->cursor_opaque = cursor_opaque;
    ds->wrap = e->wrap;
    if (ds->wrap == WRAP_AUTO) {
        /* XXX: check e->mode->default_wrap */
        /* Behave as WRAP_LINE if window is not at least 75% of full width.
         * This allows the same default behavior for full width window and
         * the dired view pane but behaves as WRAP_TRUNCATE on split screens
         */
        if (e->width >= e->screen->width * 3 / 4) {
            ds->wrap = WRAP_LINE;
        }
    }
    /* select default values */
    get_style(e, &styledef, QE_STYLE_DEFAULT);
    font = select_font(e->screen, styledef.font_style, styledef.font_size);
    ds->default_line_height = font->ascent + font->descent;
    ds->space_width = glyph_width(e->screen, font, ' ');
    ds->tab_width = ds->space_width * e->b->tab_width;
    ds->height = e->height;
    ds->hex_mode = e->hex_mode;
    ds->y = e->y_disp;
    /* display line numbers if linum-mode was selected explicitly
       or if global-linum-mode was selected and buffer is not special
     */
    if (has_linum_mode(e)) {
        // XXX: gutter width should depend on number of digits in line numbers
        ds->line_numbers = ds->space_width * 8;
        if (ds->line_numbers > e->width / 2)
            ds->line_numbers = 0;
    }
    if (ds->wrap == WRAP_TERM) {
        ds->width = ds->line_numbers +
            e->wrap_cols * glyph_width(e->screen, font, '0');
    } else {
        ds->eol_width = max3_int(glyph_width(e->screen, font, '/'),
                                 glyph_width(e->screen, font, '\\'),
                                 glyph_width(e->screen, font, '$'));
        ds->width = e->width - ds->eol_width;
    }
    display_bol(ds);
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

static uint32_t get_uint32(const void *p) {
    const uint32_t *p32 = p;
    return *p32;
}

/* CRC to optimize redraw. */
/* XXX: is it safe enough ? */
static uint64_t compute_crc(const void *p, int size, uint64_t sum)
{
    const u8 *data = (const u8 *)p;

    /* Rotating sum necessary to prevent trivial collisions on
     * line_chars because it is an array of code points stored as char32_t.
     */
    /* XXX: We still have a bug when transposing two 31 byte words as in
     * B123456789012345678901234567890 A123456789012345678901234567890
     * Here is another trivial collision:
                    s->esc_params[s->nb_esc_params] = 0;
                    s->has_params[s->nb_esc_params] = 1;
     */
    while (((uintptr_t)data & 3) && size > 0) {
        //sum += ((sum >> 31) & 1) + sum + *data;
        //sum += sum + *data + (sum >> 32);
        sum = (sum << 3) + *data + (sum >> 32);
        data++;
        size--;
    }
    while (size >= 4) {
        //sum += ((sum >> 31) & 1) + sum + get_uint32(data);
        //sum += sum + get_uint32(data) + (sum >> 32);
        sum = (sum << 3) + get_uint32(data) + (sum >> 32);
        data += 4;
        size -= 4;
    }
    while (size > 0) {
        //sum += ((sum >> 31) & 1) + sum + *data;
        //sum += sum + *data + (sum >> 32);
        sum = (sum << 2) + *data + (sum >> 32);
        data++;
        size--;
    }
    return sum;
}

/* flush the line fragments to the screen.
   `offset1..offset2` is the range of offsets for cursor management
   `last` is 0 for a line wrap, 1 for end of line, -1 for continuation
*/
static void flush_line(DisplayState *ds,
                       TextFragment *fragments, int nb_fragments,
                       int offset1, int offset2, int last)
{
    EditState *e = ds->edit_state;
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
        line_height = ds->default_line_height;
    } else {
        line_height = baseline + max_descent;
    }

    /* swap according to embedding level (incorrect for very long lines) */
    for (level = ds->embedding_level_max; level > 0; level--) {
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
    if (ds->do_disp == DISP_PRINT
    &&  ds->y + line_height >= 0
    &&  ds->y < e->ytop + e->height) {
        QEStyleDef styledef, default_style;
        int no_display = 0;

        /* test if display needed */
        if (ds->line_num >= 0 && ds->line_num < 2048) {
            /* paranoid: prevent cache growing too large */
            /* Use checksum based line cache to improve speed in graphics mode.
             * XXX: overlong lines will fail the cache test
             */
            if (ds->line_num >= e->shadow_nb_lines) {
                /* reallocate shadow */
                int n = ds->line_num + LINE_SHADOW_INCR;
                if (qe_realloc_array(&e->line_shadow, n)) {
                    /* put an impossible value so that we redraw */
                    memset(&e->line_shadow[e->shadow_nb_lines], 0xff,
                           (n - e->shadow_nb_lines) * sizeof(QELineShadow));
                    e->shadow_nb_lines = n;
                }
            }
            if (ds->line_num < e->shadow_nb_lines && !disable_crc) {
                QELineShadow *ls;
                uint64_t crc;

                crc = compute_crc(fragments, sizeof(*fragments) * nb_fragments, 0);
                crc = compute_crc(ds->line_chars, sizeof(*ds->line_chars) * ds->line_index, crc);
                ls = &e->line_shadow[ds->line_num];
                if (ls->y != ds->y || ls->x != ds->x_line
                ||  ls->height != line_height || ls->crc != crc) {
                    /* update values for the line cache */
                    ls->y = ds->y;
                    ls->x = ds->x_line;
                    ls->height = line_height;
                    ls->crc = crc;
                } else {
                    no_display = 1;
                }
            }
        }
        if (!no_display) {
            /* display */
            get_style(e, &default_style, QE_STYLE_DEFAULT);
            x = ds->x_start;
            y = ds->y;

            /* first display background rectangles */
            /* XXX: should coalesce rectangles with identical style */
            if (ds->left_gutter > 0) {
                /* erase space before the line display, aka left gutter */
                get_style(e, &styledef, QE_STYLE_GUTTER);
                fill_rectangle(screen, e->xleft + x, e->ytop + y,
                               ds->left_gutter, line_height, styledef.bg_color);
            }
            x = ds->x_line;
            x1 = ds->width + ds->eol_width;
            for (i = 0; i < nb_fragments && x < x1; i++) {
                frag = &fragments[i];
                get_style(e, &styledef, frag->style);
                fill_rectangle(screen, e->xleft + x, e->ytop + y,
                               frag->width, line_height, styledef.bg_color);
                x += frag->width;
            }
            if (x < x1 && last != -1) {
                /* XXX: color may be inappropriate for terminal mode */
                fill_rectangle(screen, e->xleft + x, e->ytop + y,
                               x1 - x, line_height, default_style.bg_color);
            }
            if (x1 < e->width) {
                /* right gutter like space beyond terminal right margin */
                get_style(e, &styledef, QE_STYLE_GUTTER);
                fill_rectangle(screen, e->xleft + x1, e->ytop + y,
                               e->width - x1, line_height, styledef.bg_color);
            }

            /* then display text */
            x = ds->x_line;
            y += baseline;

            for (i = 0; i < nb_fragments && x < x1; i++) {
                frag = &fragments[i];
                x += frag->width;
                if (x > 0) {
                    get_style(e, &styledef, frag->style);
                    font = select_font(screen,
                                       styledef.font_style, styledef.font_size);
                    draw_text(screen, font,
                              e->xleft + x - frag->width, e->ytop + y,
                              ds->line_chars + frag->line_index,
                              frag->len, styledef.fg_color);
                    release_font(screen, font);
                }
            }

            if (last == 0 && ds->eol_width != 0) {
                /* draw eol mark */
                char32_t markbuf[1];

                markbuf[0] = '/';        /* RTL eol mark */
                x = 0;                   /* displayed at the left border */
                if (ds->base == DIR_LTR) {
                    markbuf[0] = '\\';   /* LTR eol mark */
                    x = ds->width;        /* displayed at the right border */
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
    if (ds->cursor_func) {
        x = ds->x_line;
        y = ds->y;

        /* RTL eol cursor check (probably incorrect) */
        if (offset1 >= 0 && offset2 >= 0 &&
            ds->base == DIR_RTL &&
            ds->cursor_func(ds, offset1, offset2, ds->line_num,
                           x, y, -ds->eol_width, line_height, e->hex_mode)) {
            ds->eod = 1;
        }

        for (i = 0; i < nb_fragments; i++) {
            int j, k;

            frag = &fragments[i];

            for (j = frag->line_index, k = 0; k < frag->len; k++, j++) {
                int _offset1 = ds->line_offsets[j][0];
                int _offset2 = ds->line_offsets[j][1];
                int hex_mode = ds->line_hex_mode[j];
                int w = ds->line_char_widths[j];
                x += w;
                if ((hex_mode == ds->hex_mode || ds->hex_mode == -1) &&
                    _offset1 >= 0 && _offset2 >= 0) {
#if 0
                    /* probably broken, bidir needs rework */
                    if (ds->base == DIR_RTL) {
                        if (ds->cursor_func(ds, _offset1, _offset2, ds->line_num,
                                           x, y, -w, line_height, hex_mode))
                            ds->eod = 1;
                    } else
#endif
                    {
                        if (ds->cursor_func(ds, _offset1, _offset2, ds->line_num,
                                           x - w, y, w, line_height, hex_mode))
                            ds->eod = 1;
                    }
                }
            }
        }

        /* LTR eol cursor check */
        if (offset1 >= 0 && offset2 >= 0 &&
            ds->base == DIR_LTR &&
            ds->cursor_func(ds, offset1, offset2, ds->line_num,
                           x, y, ds->eol_width, line_height, e->hex_mode)) {
            ds->eod = 1;
        }
        ds->x_line = x;
    }
#if 0
    printf("y=%d line_num=%d line_height=%d baseline=%d\n",
           ds->y, ds->line_num, line_height, baseline);
#endif
    if (last != -1) {
        /* bump to next line */
        ds->x_line = ds->x_start;
        ds->y += line_height;
        ds->line_num++;
    }
}

/* keep 'n' line chars at the start of the line */
static void keep_line_chars(DisplayState *ds, int n)
{
    int index;

    index = ds->line_index - n;
    blockmove(ds->line_chars, ds->line_chars + index, n);
    blockmove(ds->line_offsets, ds->line_offsets + index, n);
    blockmove(ds->line_char_widths, ds->line_char_widths + index, n);
    ds->line_index = n;
}

#ifndef CONFIG_UNICODE_JOIN

/* fallback unicode functions */

int unicode_to_glyphs(char32_t *dst, unsigned int *char_to_glyph_pos,
                      int dst_size, char32_t *src, int src_size, int reverse)
{
    int len, i;

    len = src_size;
    if (len > dst_size)
        len = dst_size;
    blockcpy(dst, src, len);
    if (char_to_glyph_pos) {
        for (i = 0; i < len; i++)
            char_to_glyph_pos[i] = i;
    }
    return len;
}

#endif

/* layout of a word fragment */
static void flush_fragment(DisplayState *ds)
{
    int w, len, i, j;
    QETermStyle style;
    QEditScreen *screen = ds->edit_state->screen;
    TextFragment *frag;
    QEStyleDef styledef;
    QEFont *font;
    unsigned int char_to_glyph_pos[MAX_WORD_SIZE];
    int nb_glyphs, dst_max_size, ascent, descent;

    if (ds->fragment_index == 0)
        return;

    if (ds->nb_fragments >= MAX_SCREEN_WIDTH ||
        ds->line_index + ds->fragment_index > MAX_SCREEN_WIDTH) {
        /* too many fragments on the same line, flush and stay on the line */
        flush_line(ds, ds->fragments, ds->nb_fragments, -1, -1, -1);
        ds->nb_fragments = 0;
        ds->line_index = 0;
        ds->word_index = 0;
    }

    /* update word start index if needed */
    if (ds->nb_fragments >= 1 && ds->last_word_space != ds->last_space) {
        ds->last_word_space = ds->last_space;
        ds->word_index = ds->nb_fragments;
    }

    /* convert fragment to glyphs (currently font independent, but may
       change) */
    //dst_max_size = MAX_SCREEN_WIDTH - ds->line_index;
    //if (dst_max_size <= 0)
    //    goto the_end;
    dst_max_size = MAX_WORD_SIZE; // assuming ds->fragment_index MAX_WORD_SIZE
    nb_glyphs = unicode_to_glyphs(ds->line_chars + ds->line_index,
                                  char_to_glyph_pos, dst_max_size,
                                  ds->fragment_chars, ds->fragment_index,
                                  ds->last_embedding_level & 1);

    /* compute new offsets */
    j = ds->line_index;
    for (i = 0; i < nb_glyphs; i++) {
        ds->line_offsets[j][0] = -1;
        ds->line_offsets[j][1] = -1;
        j++;
    }
    for (i = 0; i < ds->fragment_index; i++) {
        int offset1, offset2;
        j = ds->line_index + char_to_glyph_pos[i];
        offset1 = ds->fragment_offsets[i][0];
        offset2 = ds->fragment_offsets[i][1];
        ds->line_hex_mode[j] = ds->fragment_hex_mode[i];
        /* we suppose the the chars are contiguous */
        if (ds->line_offsets[j][0] == -1 ||
            ds->line_offsets[j][0] > offset1)
            ds->line_offsets[j][0] = offset1;
        if (ds->line_offsets[j][1] == -1 ||
            ds->line_offsets[j][1] < offset2)
            ds->line_offsets[j][1] = offset2;
    }

    style = ds->last_style;
    get_style(ds->edit_state, &styledef, style);
    /* select font according to current style */
    font = select_font(screen, styledef.font_style, styledef.font_size);
    j = ds->line_index;
    ascent = font->ascent;
    descent = font->descent;
    if (ds->line_chars[j] == '\t') {
        int x1;
        /* special case for TAB */
        x1 = (ds->x - ds->x_disp) % ds->tab_width;
        w = ds->tab_width - x1;
        /* display a single space */
        ds->line_chars[j] = ' ';
        ds->line_char_widths[j] = w;
    } else {
        /* XXX: use text metrics for full fragment */
        w = 0;
        /* XXX: is the width negative for a RTL fragment? */
        for (i = 0; i < nb_glyphs; i++) {
            QECharMetrics metrics;
            text_metrics(screen, font, &metrics, &ds->line_chars[j], 1);
            if (metrics.font_ascent > ascent)
                ascent = metrics.font_ascent;
            if (metrics.font_descent > descent)
                descent = metrics.font_descent;
            ds->line_char_widths[j] = metrics.width;
            w += ds->line_char_widths[j];
            j++;
        }
    }
    release_font(screen, font);

    /* add the fragment */
    frag = &ds->fragments[ds->nb_fragments++];
    frag->width = w;
    frag->line_index = ds->line_index;
    frag->len = nb_glyphs;
    frag->embedding_level = ds->last_embedding_level;
    frag->style = style;
    frag->ascent = ascent;
    frag->descent = descent;
#if QE_TERM_STYLE_BITS == 16
    frag->dummy = 0;  /* initialize padding for checksum consistency */
#endif

    ds->line_index += nb_glyphs;
    ds->x += frag->width;

    switch (ds->wrap) {
    case WRAP_TRUNCATE:
    case WRAP_AUTO:
        break;
    case WRAP_LINE:
    case WRAP_TERM:
        while (ds->x > ds->width) {
            int len1, w1, ww, n;
            //printf("x=%d maxw=%d len=%d\n", ds->x, ds->width, frag->len);
            frag = &ds->fragments[ds->nb_fragments - 1];
            /* find fragment truncation to fit the line */
            len = len1 = frag->len;
            w1 = ds->x;
            while (ds->x > ds->width) {
                len--;
                ww = ds->line_char_widths[frag->line_index + len];
                ds->x -= ww;
                if (len == 0 && ds->x == 0) {
                    /* avoid looping by putting at least one char per line */
                    len = 1;
                    ds->x += ww;
                    break;
                }
            }
            len1 -= len;
            w1 -= ds->x;
            frag->len = len;
            frag->width -= w1;
            //printf("after: x=%d w1=%d\n", ds->x, w1);
            n = ds->nb_fragments;
            if (len == 0)
                n--;

            /* flush fragments with a line continuation mark */
            flush_line(ds, ds->fragments, n, -1, -1, 0);

            /* skip line number column if present */
            ds->left_gutter = ds->line_numbers;
            ds->x = ds->x_line += ds->left_gutter;

            /* move the remaining fragment to next line */
            ds->nb_fragments = 0;
            if (len1 > 0) {
                blockmove(ds->fragments, frag, 1);
                frag = ds->fragments;
                frag->width = w1;
                frag->line_index = 0;
                frag->len = len1;
                ds->nb_fragments = 1;
                ds->x += w1;
            }
            keep_line_chars(ds, len1);
        }
        break;
    case WRAP_WORD:
        if (ds->x > ds->width) {
            int index;

            /* flush fragments with a line continuation mark */
            flush_line(ds, ds->fragments, ds->word_index, -1, -1, 0);

            /* skip line number column if present */
            ds->left_gutter = ds->line_numbers;
            ds->x = ds->x_line += ds->left_gutter;

            /* put words on next line */
            index = ds->fragments[ds->word_index].line_index;
            blockmove(ds->fragments, ds->fragments + ds->word_index,
                      (ds->nb_fragments - ds->word_index));
            ds->nb_fragments -= ds->word_index;

            for (i = 0; i < ds->nb_fragments; i++) {
                ds->fragments[i].line_index -= index;
                ds->x += ds->fragments[i].width;
            }
            keep_line_chars(ds, ds->line_index - index);
            ds->word_index = 0;
        }
        break;
    }
    ds->fragment_index = 0;
}

int display_char_bidir(DisplayState *ds, int offset1, int offset2,
                       int embedding_level, char32_t ch)
{
    int space, istab, isaccent;
    QETermStyle style;
    EditState *e;

    style = ds->style;

    /* special code to colorize block */
    e = ds->edit_state;
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
    if (ds->fragment_index >= 1) {
        if (ds->fragment_index >= MAX_WORD_SIZE ||
            istab ||
            space != ds->last_space ||
            style != ds->last_style ||
            embedding_level != ds->last_embedding_level) {
            /* flush the current fragment if needed */
            if (isaccent && ds->fragment_chars[ds->fragment_index - 1] == ' ') {
                /* separate last space to make it part of the next word */
                int off1, off2, cur_hex;
                --ds->fragment_index;
                off1 = ds->fragment_offsets[ds->fragment_index][0];
                off2 = ds->fragment_offsets[ds->fragment_index][1];
                cur_hex = ds->fragment_hex_mode[ds->fragment_index];
                flush_fragment(ds);
                ds->fragment_chars[ds->fragment_index] = ' ';
                ds->fragment_offsets[ds->fragment_index][0] = off1;
                ds->fragment_offsets[ds->fragment_index][1] = off2;
                ds->fragment_hex_mode[ds->fragment_index] = cur_hex;
                ds->fragment_index++;
            } else {
                flush_fragment(ds);
            }
        }
    }

    if (isaccent && ds->fragment_index == 0) {
        /* prepend a space if fragment starts with an accent */
        ds->fragment_chars[ds->fragment_index] = ' ';
        ds->fragment_offsets[ds->fragment_index][0] = offset1;
        ds->fragment_offsets[ds->fragment_index][1] = offset2;
        ds->fragment_hex_mode[ds->fragment_index] = ds->cur_hex_mode;
        ds->fragment_index++;
        offset1 = offset2 = -1;
    }
    /* store the char and its embedding level */
    ds->fragment_chars[ds->fragment_index] = ch;
    ds->fragment_offsets[ds->fragment_index][0] = offset1;
    ds->fragment_offsets[ds->fragment_index][1] = offset2;
    ds->fragment_hex_mode[ds->fragment_index] = ds->cur_hex_mode;
    ds->fragment_index++;

    ds->last_space = space;
    ds->last_style = style;
    ds->last_embedding_level = embedding_level;

    if (istab) {
        flush_fragment(ds);
    }
    return 0;
}

void display_printhex(DisplayState *ds, int offset1, int offset2,
                      char32_t h, int n)
{
    int i, v;
    EditState *e = ds->edit_state;

    ds->cur_hex_mode = 1;
    for (i = 0; i < n; i++) {
        v = (h >> ((n - i - 1) * 4)) & 0xf;
        if (v >= 10)
            v += 'a' - 10;
        else
            v += '0';
        /* XXX: simplistic */
        if (e->hex_nibble == i) {
            display_char(ds, offset1, offset2, v);
        } else {
            display_char(ds, offset1, offset1, v);
        }
    }
    ds->cur_hex_mode = 0;
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
        /* XXX: UTF-8 unsupported, not needed at this point */
        display_char(ds, offset1, offset2, *p++);
        while (*p) {
            /* XXX: Should make these display character mouse selectable */
            display_char(ds, -1, -1, *p++);
        }
    }
}

/* end of line */
void display_eol(DisplayState *ds, int offset1, int offset2)
{
    flush_fragment(ds);

    /* note: the line may be empty */
    flush_line(ds, ds->fragments, ds->nb_fragments, offset1, offset2, 1);
}

/* temporary function for backward compatibility */
static void display1(DisplayState *ds)
{
    EditState *e = ds->edit_state;
    int offset;

    ds->eod = 0;
    offset = e->offset_top;
    for (;;) {
        /* XXX: need early bailout from display_line if WRAP_TRUNCATE
           and far beyond the right border after cursor found.
        */
        offset = e->mode->display_line(e, ds, offset);
        e->offset_bottom = offset;

        /* EOF reached ? */
        if (offset < 0)
            break;

        switch (ds->do_disp) {
        case DISP_NONE:
            return;
        case DISP_CURSOR:
            if (ds->eod)
                return;
            break;
        case DISP_CURSOR_SCREEN:
            if (ds->eod || ds->y >= ds->height)
                return;
            break;
        case DISP_PRINT:
        default:
            if (ds->y >= ds->height)
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
static int bidir_compute_attributes(BidirTypeLink *list_tab, int max_size,
                                    EditBuffer *b, int offset)
{
    BidirTypeLink *p;
    BidirCharType type, ltype;
    int left, offset1;
    char32_t c;

    p = list_tab;
    /* Add the starting link */
    p->type = BIDIR_TYPE_SOT;
    p->len = 0;
    p->pos = 0;
    p++;
    left = max_size - 2;

    ltype = BIDIR_TYPE_SOT;

    for (;;) {
        offset1 = offset;
        c = eb_nextc(b, offset, &offset);
        if (c == '\n')
            break;
        type = bidir_get_type(c);
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
    p->type = BIDIR_TYPE_EOT;
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

static int get_staticly_colorized_line(QEColorizeContext *cp,
                                       int offset, int *offset_ptr, int line_num)
{
    EditBuffer *b = cp->b;
    int start_offset = offset, end_offset;
    int len = 0;

    for (;;) {
        int next;
        QETermStyle style = eb_get_style(b, offset);
        char32_t c = eb_nextc(b, offset, &next);
        if (len + 1 >= cp->buf_size) {
            int new_size = min_int(eb_get_line_length(b, start_offset, &end_offset) + 1,
                                   MAX_COLORED_LINE_SIZE);
            if (cp->buf_size == new_size || !cp_reallocate(cp, new_size)) {
                offset = end_offset;
                cp->sbuf[len] = style;
                cp->buf[len] = '\0';
                break;
            }
        }
        cp->sbuf[len] = style;
        cp->buf[len++] = c;
        offset = next;
        if (c == '\n') {
            /* end of line: offset points to the beginning of the next line */
            /* adjust return value for easy stripping and truncation test */
            cp->buf[len--] = '\0';
            break;
        }
    }
    if (offset_ptr)
        *offset_ptr = offset;
    return len;
}

void cp_colorize_line(QEColorizeContext *cp,
                      const char32_t *buf, int i, int n,
                      QETermStyle *sbuf, ModeDef *syn)
{
    if (syn && syn->colorize_func) {
        buf += i;
        sbuf += i;
        n -= i;
        if (buf[n] != '\0') {
            /* ensure buf is null terminated, clone if needed */
            char32_t *buf1 = qe_malloc_dup_array(buf + i, n + 1);
            if (buf1) {
                buf1[n] = '\0';
                syn->colorize_func(cp, buf1, n, sbuf, syn);
                sbuf[n] = 0;  /* reset the end of line style */
                qe_free(&buf1);
                return;
            }
        }
        syn->colorize_func(cp, buf, n, sbuf, syn);
    }
}

QEColorizeContext *cp_initialize(QEColorizeContext *cp, EditState *s) {
    memset(cp, 0, sizeof(*cp));
    cp->s = s;
    cp->b = s->b;
    cp->buf_size = countof(cp->buf0);
    cp->buf = cp->buf0;
    cp->sbuf = cp->sbuf0;
    return cp;
}

void cp_destroy(QEColorizeContext *cp) {
    if (cp->buf != cp->buf0)
        qe_free(&cp->buf);
    if (cp->sbuf != cp->sbuf0)
        qe_free(&cp->sbuf);
    memset(cp, 0, sizeof(*cp));
}

int cp_reallocate(QEColorizeContext *cp, int new_size) {
    if (cp->buf == cp->buf0) {
        char32_t *new_buf = qe_malloc_array(char32_t, new_size);
        if (!new_buf)
            return 0;
        blockcpy(new_buf, cp->buf, cp->buf_size);
        cp->buf = new_buf;
    } else {
        if (!qe_realloc_array(&cp->buf, new_size))
            return 0;
    }
    if (cp->sbuf == cp->sbuf0) {
        QETermStyle *new_sbuf = qe_malloc_array(QETermStyle, new_size);
        if (!new_sbuf)
            return 0;
        blockcpy(new_sbuf, cp->sbuf, cp->buf_size);
        cp->sbuf = new_sbuf;
    } else {
        if (!qe_realloc_array(&cp->sbuf, new_size))
            return 0;
    }
    cp->buf_size = new_size;
    return 1;
}

#ifndef CONFIG_TINY

/* Gets the colorized line beginning at 'offset'. Its length
   excluding '\n' is returned */

#define COLORIZED_LINE_PREALLOC_SIZE 64

// XXX: s->colorize_xxx fields should be mode data, potentially shared by
//      multiple EditState upon splitting windows
static int syntax_get_colorized_line(QEColorizeContext *cp,
                                     int offset, int *offsetp, int line_num)
{
    EditState *s = cp->s;
    EditBuffer *b = cp->b;
    int i, len, line, n, col, bom;

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
        n = max_int(s->colorize_nb_lines, COLORIZED_LINE_PREALLOC_SIZE);
        while (n < (line_num + 2))
            n += (n >> 1) + (n >> 3);
        if (!qe_realloc_array(&s->colorize_states, n))
            return 0;
        s->colorize_nb_lines = n;
    }

    /* propagate state if needed */
    if (line_num >= s->colorize_nb_valid_lines) {
        if (s->colorize_nb_valid_lines == 0) {
            s->colorize_states[0] = 0; /* initial state : zero */
            s->colorize_nb_valid_lines = 1;
        }
        offset = eb_goto_pos(b, s->colorize_nb_valid_lines - 1, 0);
        cp->colorize_state = s->colorize_states[s->colorize_nb_valid_lines - 1];
        cp->state_only = 1;

        for (line = s->colorize_nb_valid_lines; line <= line_num; line++) {
            cp->offset = offset;
            len = eb_get_line(b, cp->buf, cp->buf_size, cp->offset, &offset);
            if (cp->buf[len] != '\n') {
                /* line was truncated */
                int new_size = min_int(eb_get_line_length(b, cp->offset, &offset) + 1,
                                       MAX_COLORED_LINE_SIZE);
                if (cp_reallocate(cp, new_size)) {
                    len = eb_get_line(s->b, cp->buf, cp->buf_size, cp->offset, NULL);
                }
            }
            cp->buf[len] = '\0';

            /* skip byte order mark if present */
            bom = (cp->buf[0] == 0xFEFF);
            if (bom) {
                cp->offset = eb_next(b, cp->offset);
            }
            cp_colorize_line(cp, cp->buf, bom, len, cp->sbuf, s->colorize_mode);
            s->colorize_states[line] = cp->colorize_state;
        }
    }

    /* compute line color */
    cp->colorize_state = s->colorize_states[line_num];
    cp->state_only = 0;
    cp->offset = offset;
    len = eb_get_line(b, cp->buf, cp->buf_size, offset, offsetp);
    if (cp->buf[len] != '\n') {
        /* line was truncated */
        int new_size = min_int(eb_get_line_length(b, offset, offsetp) + 1,
                               MAX_COLORED_LINE_SIZE);
        if (cp_reallocate(cp, new_size)) {
            len = eb_get_line(s->b, cp->buf, cp->buf_size, offset, NULL);
        }
    }
    cp->buf[len] = '\0';
    if (s->offset >= offset && s->offset < *offsetp + (s->offset == s->b->total_size)) {
        /* compute position of first codepoint before the cursor */
        int offset1 = offset;
        for (cp->cur_pos = 0; offset1 < s->offset; cp->cur_pos++)
            offset1 = eb_next(b, offset1);
    }

    memset(cp->sbuf, 0, (len + 1) * sizeof(*cp->sbuf));
    bom = (cp->buf[0] == 0xFEFF);
    if (bom) {
        SET_STYLE1(cp->sbuf, 0, QE_STYLE_PREPROCESS);
        cp->offset = eb_next(b, cp->offset);
    }
    cp->combine_stop = len - bom;
    cp->cur_pos -= bom;
    cp_colorize_line(cp, cp->buf, bom, len, cp->sbuf, s->colorize_mode);
    cp->cur_pos += bom;
    /* buf[len] has char '\0' but may hold style, force buf ending */
    //cp->buf[len + 1] = 0;

    /* XXX: if state is same as previous, minimize invalid region? */
    s->colorize_states[line_num + 1] = cp->colorize_state;

    /* Extend valid area */
    if (s->colorize_nb_valid_lines < line_num + 2)
        s->colorize_nb_valid_lines = line_num + 2;

    /* Combine with buffer styles on restricted range */
    if (s->b->b_styles) {
        int start = bom + cp->combine_start, stop = bom + cp->combine_stop;
        offset = cp->offset;
        for (i = bom; i < stop; i++) {
            QETermStyle style = eb_get_style(b, offset);
            if (style && i >= start) {
                cp->sbuf[i] = style;
            }
            offset = eb_next(b, offset);
        }
    }
    if (!(s->colorize_mode->flags & MODEF_NO_TRAILING_BLANKS)) {
        /* Mark trailing blanks as errors if cursor is not at end of line */
        for (i = len; i > 0 && qe_isblank(cp->buf[i - 1]) && i != cp->cur_pos; i--) {
            cp->sbuf[i - 1] = QE_STYLE_BLANK_HILITE;
        }
    }
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

#endif /* CONFIG_TINY */

void set_colorize_mode(EditState *s, ModeDef *colorize_mode)
{
    s->colorize_mode = NULL;

#ifndef CONFIG_TINY
    /* invalidate the previous states & free previous colorizer */
    eb_free_callback(s->b, colorize_callback, s);
    qe_free(&s->colorize_states);
    s->colorize_nb_lines = 0;
    s->colorize_nb_valid_lines = 0;
    s->colorize_max_valid_offset = INT_MAX;
    s->colorize_mode = colorize_mode;
    if (colorize_mode)
        eb_add_callback(s->b, colorize_callback, s, 0);
#endif
}

int get_colorized_line(QEColorizeContext *cp,
                       int offset, int *offsetp, int line_num)
{
    EditState *s = cp->s;
    int len;

#ifndef CONFIG_TINY
    if (s->colorize_mode) {
        len = syntax_get_colorized_line(cp, offset, offsetp, line_num);
    } else
#endif
    if (s->b->b_styles) {
        len = get_staticly_colorized_line(cp, offset, offsetp, line_num);
    } else {
        len = eb_get_line(s->b, cp->buf, cp->buf_size, offset, offsetp);
        if (cp->buf[len] != '\n') {
            /* line was truncated */
            int new_size = min_int(eb_get_line_length(s->b, offset, offsetp) + 1,
                                   MAX_COLORED_LINE_SIZE);
            if (cp_reallocate(cp, new_size)) {
                len = eb_get_line(s->b, cp->buf, cp->buf_size, offset, NULL);
            }
        }
        cp->buf[len] = '\0';
        if (cp->sbuf) {
            memset(cp->sbuf, 0, (len + 1) * sizeof(*cp->sbuf));
        }
    }
    // XXX: should test if TABs should be colorized too
    // XXX: should colorize trailing blanks
    return len;
}

#define RLE_EMBEDDINGS_SIZE    128

/* Display one line in the window */
int text_display_line(EditState *s, DisplayState *ds, int offset)
{
    char32_t c;
    int offset0, offset1, line_num, col_num;
    BidirTypeLink embeds[RLE_EMBEDDINGS_SIZE], *bd;
    int embedding_level, embedding_max_level;
    BidirCharType base;
    QEColorizeContext cp[1];
    int char_index, colored_nb_chars;

    cp_initialize(cp, s);

    line_num = 0;
    /* XXX: should test a flag, to avoid this call in hex/binary */
    if (ds->line_numbers || s->colorize_mode) {
        eb_get_pos(s->b, &line_num, &col_num, offset);
    }

    offset1 = offset;

#ifdef CONFIG_UNICODE_JOIN
    /* compute the embedding levels and rle encode them */
    if (s->bidir
    &&  bidir_compute_attributes(embeds, RLE_EMBEDDINGS_SIZE,
                                 s->b, offset) > 2)
    {
        base = BIDIR_TYPE_WL;
        bidir_analyze_string(embeds, &base, &embedding_max_level);
        /* assure that base has only two possible values */
        if (base != BIDIR_TYPE_RTL)
            base = BIDIR_TYPE_LTR;
    } else
#endif
    {
        /* all line is at embedding level 0 */
        embedding_max_level = 0;
        embeds[1].level = 0;
        embeds[2].pos = 0x7fffffff;
        base = BIDIR_TYPE_LTR;
    }

    display_bol_bidir(ds, base, embedding_max_level);

    /* line numbers */
    if (ds->line_numbers) {
        // XXX: line_numbers should be the number of characters to use
        ds->style = QE_STYLE_GUTTER;
        display_printf(ds, -1, -1, "%6d  ", line_num + 1);
        ds->style = 0;
    }

    /* prompt display, only on first line */
    if (s->prompt && offset1 == 0) {
        /* used for the minibuffer prompt */
        const char *p = s->prompt;

        while (*p) {
            display_char(ds, -1, -1, utf8_decode(&p));
        }
    }

    /* colorize */
    colored_nb_chars = 0;
    offset0 = offset;
    if (s->colorize_mode || s->b->b_styles
    ||  s->curline_style || s->region_style
    ||  s->isearch_state) {
        /* XXX: deal with truncation */
        colored_nb_chars = get_colorized_line(cp, offset, &offset0, line_num);
        if (s->mode == &list_mode) {
            // TODO: should use a `colorize_line_hook` for this
            QEmacsState *qs = s->qs;
            int i;

            if ((qs->active_window == s || s->force_highlight) &&
                s->offset >= offset && s->offset < offset0)
            {
                int level = 0;
                /* highlight the current line (except bracketed areas) */
                for (i = 0; i <= colored_nb_chars; i++) {
                    if (cp->buf[i] == '[') {
                        level++;
                    } else
                    if (cp->buf[i] == ']') {
                        level--;
                    } else
                    if (level == 0 || !(cp->sbuf[i] & ~QE_STYLE_NUM)) {
                        cp->sbuf[i] = QE_STYLE_HIGHLIGHT;
                    }
                }
            } else
            if (cp->buf[0] == '*') {
                /* selection */
                for (i = 0; i <= colored_nb_chars; i++) {
                    cp->sbuf[i] |= QE_STYLE_SEL;
                }
            }
        }
        if (s->isearch_state) {
            isearch_colorize_matches(s, cp->buf, colored_nb_chars, cp->sbuf, offset);
        }
        // TODO: should handle multi_cursor position and selection
    }

#if 1
    /* colorize regions */
    if (s->curline_style || s->region_style) {
        /* CG: Should combine styles instead of replacing */
        if (s->region_style && !s->curline_style) {
            int line, start_offset, end_offset;
            int i, start_char, end_char;

            if (s->b->mark < s->offset) {
                start_offset = max_offset(offset, s->b->mark);
                end_offset = min_offset(offset0, s->offset);
            } else {
                start_offset = max_offset(offset, s->offset);
                end_offset = min_offset(offset0, s->b->mark);
            }
            if (start_offset < end_offset) {
                /* Compute character positions */
                eb_get_pos(s->b, &line, &start_char, start_offset);
                if (end_offset >= offset0)
                    end_char = colored_nb_chars;
                else
                    eb_get_pos(s->b, &line, &end_char, end_offset);

                for (i = start_char; i < end_char; i++) {
                    cp->sbuf[i] = s->region_style;
                }
            }
        } else
        if (s->curline_style && s->offset >= offset && s->offset <= offset0) {
            /* XXX: only if qs->active_window == s ? */
            int i;
            for (i = 0; i < colored_nb_chars; i++)
                cp->sbuf[i] = s->curline_style;
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
            ds->style = 0;
            if (char_index < colored_nb_chars) {
                ds->style = cp->sbuf[char_index];
            }
            c = eb_nextc(s->b, offset, &offset);
            if (c == '\n' && !(s->flags & WF_MINIBUF)) {
                display_eol(ds, offset0, offset);
                break;
            }
            /* compute embedding from RLE embedding list */
            if (offset0 >= bd[1].pos)
                bd++;
            embedding_level = bd[0].level;
            /* XXX: use embedding level for all cases ? */
            /* CG: should query screen or window about display methods */
            if ((c < ' ' && (c != '\t' || (s->flags & WF_MINIBUF))) || c == 127) {
                /* EOL_MAC encoding swaps \r and \n to simplify end of line
                   handling in many places. We must handle '\r' explicitly for
                   it to be displayed as ^J
                 */
                if (c == '\r' && s->b->eol_type == EOL_MAC)
                    c = '\n';
                display_printf(ds, offset0, offset, "^%c", ('@' + c) & 127);
            } else
            if (c >= 128
            &&  (s->qs->show_unicode == 1 ||
                 c == 0xfeff ||   /* Display BOM as \uFEFF to make it explicit */
                 c > MAX_UNICODE_DISPLAY ||
                 (c < 160 && s->b->charset == &charset_raw))) {
                /* display unsupported unicode code points as hex */
                if (c > 0xffff) {
                    display_printf(ds, offset0, offset, "\\U%08x", c);
                } else
                if (c > 0xff) {
                    display_printf(ds, offset0, offset, "\\u%04x", c);
                } else {
                    display_printf(ds, offset0, offset, "\\x%02x", c);
                }
            } else {
                display_char_bidir(ds, offset0, offset, embedding_level, c);
            }
            char_index++;
            //if (ds->y >= s->height && ds->eod)  //@@@ causes bug
            //    break;
        }
    }
    cp_destroy(cp);
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
            put_error(s, "ERROR: cursor not found");
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
        s->y_disp = min_int(s->height - ds->y, 0);
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
    if (ds->wrap == WRAP_TRUNCATE) {
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
        if (ds->line_num >= 0 && ds->line_num < s->shadow_nb_lines) {
            /* erase the line shadow for the rest of the window */
            memset(&s->line_shadow[ds->line_num], 0xff,
                   (s->shadow_nb_lines - ds->line_num) * sizeof(QELineShadow));
        }
    }
    display_close(ds);

    xc = m->xc;
    yc = m->yc;

    if (xc != NO_CURSOR && yc != NO_CURSOR
    &&  s->qs->active_window == s) {
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
            xor_rectangle(s->screen, x, y, w, h, QERGB(0xFF, 0xFF, 0xFF));
            if (m->linec >= 0 && m->linec < s->shadow_nb_lines) {
                /* invalidate line so that the cursor will be erased next time */
                memset(&s->line_shadow[m->linec], 0xff, sizeof(QELineShadow));
            }
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
    const CmdDef *d;
    int nb_args;
    int has_arg;
    int argval;
    int key;
    const char *ptype;
    unsigned char args_type[MAX_CMD_ARGS];
    CmdArg args[MAX_CMD_ARGS];
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
   - void (*)(EditState *, int, int, int); (1)
   - void (*)(EditState *, const char *, int, int); (0)
   - void (*)(EditState *, const char *, const char *, int); (2)
   - void (*)(EditState *, const char *, const char *, const char *); (2)
   - void (*)(ISearchState *); (?)
   - void (*)(ISearchState *, int); (?)
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
    case CMD_ESii:   /* ES + integer + integer */
        (*func.ESii)(args[0].s, args[1].n, args[2].n);
        break;
    case CMD_ESsi:   /* ES + string + integer */
        (*func.ESsi)(args[0].s, args[1].p, args[2].n);
        break;
    case CMD_ESss:   /* ES + string + string */
        (*func.ESss)(args[0].s, args[1].p, args[2].p);
        break;
    case CMD_ESiii:  /* ES + integer + integer + integer */
        (*func.ESiii)(args[0].s, args[1].n, args[2].n, args[3].n);
        break;
    case CMD_ESsii:  /* ES + string + integer + integer */
        (*func.ESsii)(args[0].s, args[1].p, args[2].n, args[3].n);
        break;
    case CMD_ESssi:  /* ES + string + string + integer */
        (*func.ESssi)(args[0].s, args[1].p, args[2].p, args[3].n);
        break;
    case CMD_ESsss:  /* ES + string + string + string */
        (*func.ESsss)(args[0].s, args[1].p, args[2].p, args[3].p);
        break;
    }
}

static void get_param(const char **pp, int osep, int sep, char *param, int param_size)
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
int parse_arg(const char **pp, CmdArgSpec *ap)
{
    int tc, type;
    const char *p;

    p = *pp;
    if (*p == '\0')
        return 0;
    tc = *p++;
    get_param(&p, '{', '}', ap->prompt, sizeof(ap->prompt));
    get_param(&p, '[', ']', ap->completion, sizeof(ap->completion));
    get_param(&p, '|', '|', ap->history, sizeof(ap->history));
    type = 0;
    /* code letters modeled after emacs (interactive) function code letters */
    switch (ap->code_letter = tc) {
    case 'd':  /* point as a number */
        type = CMD_ARG_INT | CMD_ARG_USE_POINT;
        break;
    case 'e':  /* the buffer size, used to select full buffer contents */
        type = CMD_ARG_INT | CMD_ARG_USE_BSIZE;
        break;
    case 'k':  /* last key typed: should pass string with encoded keys */
        type = CMD_ARG_INT | CMD_ARG_USE_KEY;
        break;
    case 'm':  /* buffer mark as a number */
        type = CMD_ARG_INT | CMD_ARG_USE_MARK;
        break;
    case 'n':  /* number read from minibuffer */
        type = CMD_ARG_INT;
        break;
    case 'N':  /* numeric prefix argument else get from minibuffer */
        type = CMD_ARG_INT | CMD_ARG_RAW_ARGVAL;
        break;
    case 'p':  /* number: converted prefix argument */
        type = CMD_ARG_INT | CMD_ARG_NUM_ARGVAL;
        break;
    case 'P':  /* raw prefix argument */
        type = CMD_ARG_INT | CMD_ARG_RAW_ARGVAL;  /* kludge! */
        break;
    case 'q':  /* number: negated converted prefix argument */
        type = CMD_ARG_INT | CMD_ARG_NEG_ARGVAL;
        break;
    case 's':  /* string read from minibuffer */
        type = CMD_ARG_STRING;
        break;
    case '@':  /* immediate string from CmdDef prompt string */
        /* used in define_kbd_macro, and mode selection */
        /* must be the last argument */
        type = CMD_ARG_STRINGVAL;
        break;
    case 'v':  /* the immediate value from CmdDef val field */
        type = CMD_ARG_INTVAL;
        break;
    case 'z':  /* the number 0, used to select full buffer contents */
        type = CMD_ARG_INT | CMD_ARG_USE_ZERO;
        break;
    default:
        return -1;
    }
    *pp = p;
    ap->arg_type = type;
    return 1;
}

int qe_get_prototype(const CmdDef *d, char *buf, int size) {
    buf_t outbuf, *out;
    const char *r;
    const char *sep = "";
    CmdArgSpec cas;

    out = buf_init(&outbuf, buf, size);

    buf_put_byte(out, '(');

    /* construct argument type list */
    r = d->spec;
    if (*r == '*') {
        r++;    /* buffer modification indicator */
    }

    while (parse_arg(&r, &cas) > 0) {
        switch (cas.arg_type & CMD_ARG_TYPE_MASK) {
        case CMD_ARG_INT:
            buf_printf(out, "%sint ", sep);
            break;
        case CMD_ARG_STRING:
            buf_printf(out, "%sstring ", sep);
            break;
        case CMD_ARG_WINDOW:
        case CMD_ARG_OPAQUE:
        case CMD_ARG_INTVAL:
        case CMD_ARG_STRINGVAL:
        default:
            continue;
        }
        sep = ", ";
        switch (cas.code_letter) {
        case 'd':
            buf_puts(out, "= point");
            break;
        case 'e':
            buf_puts(out, "= bufsize");
            break;
        case 'k':
            buf_puts(out, "= key");
            break;
        case 'm':
            buf_puts(out, "= mark");
            break;
        case 'N':
        case 'p':
        case 'P':
        case 'q':
            buf_puts(out, "= argval");
            break;
        case 'z':
            buf_puts(out, "= 0");
            break;
        default:
            buf_puts(out, *cas.history ? cas.history : cas.completion);
            break;
        }
    }
    buf_put_byte(out, ')');
    return out->len;
}

static void arg_edit_cb(void *opaque, char *str, CompletionDef *completion);
static void parse_arguments(ExecCmdState *es);
static void free_cmd(ExecCmdState **esp);

void exec_command(EditState *s, const CmdDef *d, int argval, int key)
{
    QEmacsState *qs = s->qs;
    ExecCmdState *es;
    const char *argdesc;

    if (qs->trace_buffer)
        qe_trace_bytes(qs, d->name, -1, EB_TRACE_COMMAND);

    argdesc = d->spec;
    if (*argdesc == '*') {
        argdesc++;
        if (s->b->flags & BF_READONLY) {
            put_error(s, "Buffer is read only");
            return;
        }
    }

    es = qe_mallocz(ExecCmdState);
    if (!es)
        return;

    es->s = s;
    es->d = d;
    if (argval == NO_ARG) {
        es->has_arg = 0;
        es->argval = 1;
    } else {
        es->has_arg = 1;
        es->argval = argval;
    }
    es->key = key;
    es->nb_args = 0;

    /* first argument is always the window */
    es->args[0].s = s;
    es->args_type[0] = CMD_ARG_WINDOW;
    es->nb_args++;
    es->ptype = argdesc;

    parse_arguments(es);
}

/* parse as much arguments as possible. ask value to user if possible */
static void parse_arguments(ExecCmdState *es)
{
    EditState *s = es->s;
    QEmacsState *qs = s->qs;
    QErrorContext ec;
    const CmdDef *d = es->d;
    CmdArg *argp;
    CmdArgSpec cas;
    int ret, rep_count, get_arg, type;
    int elapsed_time;

    while ((ret = parse_arg(&es->ptype, &cas)) != 0) {
        if (ret < 0 || es->nb_args >= MAX_CMD_ARGS)
            goto fail;
        type = cas.arg_type & CMD_ARG_TYPE_MASK;
        argp = &es->args[es->nb_args];
        es->args_type[es->nb_args] = type;
        get_arg = 0;
        switch (type) {
        case CMD_ARG_INTVAL:
            argp->n = d->val;
            break;
        case CMD_ARG_STRINGVAL:
            /* CG: kludge for xxx-mode functions and named kbd macros,
               must be last argument */
            argp->p = cas.prompt;
            break;
        case CMD_ARG_INT:
            switch (cas.code_letter) {
            case 'd':   argp->n = s->offset;    break;
            case 'e':   argp->n = s->b->total_size; break;
            case 'k':   argp->n = es->key;      break;
            case 'm':   argp->n = s->b->mark;   break;
            case 'n':   argp->n = 0; get_arg = 1; break;
            case 'N':   argp->n = es->argval; get_arg = !es->has_arg; goto consume_arg;
            case 'p':   argp->n = es->argval;   goto consume_arg;
            case 'P':   argp->n = es->has_arg ? es->argval : NO_ARG; goto consume_arg;
            case 'q':   argp->n = -es->argval;  goto consume_arg;
            case 'z':
                /* CG: Should add syntax for default value if no prompt */
            default:    argp->n = 0;            break; /* invalid */
            consume_arg:
                es->has_arg = 0;
                es->argval = 1;
                break;
            }
            break;
        case CMD_ARG_STRING:
            {
                argp->p = NULL;
                get_arg = 1;
                break;
            }
        }
        es->nb_args++;
        /* if no argument specified, try to ask it to the user */
        if (get_arg && cas.prompt[0] != '\0') {
            char def_input[1024];
            StringArray *hist = qe_get_history(qs, cas.history);
            /* XXX: currently, default input is handled non generically */
            /* XXX: should use completion function for default input? */
            def_input[0] = '\0';
            es->default_input[0] = '\0';
            if (strequal(cas.completion, "file") || strequal(cas.completion, "dir")) {
                get_default_path(s->b, s->offset, def_input, sizeof(def_input));
            } else
            if (strequal(cas.completion, "buffer")) {
                EditBuffer *b;
                if (d->action.ESs == do_switch_to_buffer)
                    b = predict_switch_to_buffer(s);
                else
                    b = s->b;
                pstrcpy(es->default_input, sizeof(es->default_input), b->name);
            } else
            if (strequal(cas.history, "macrokeys")) {
                if (hist && hist->nb_items)
                    pstrcpy(def_input, sizeof(def_input), hist->items[hist->nb_items - 1]->str);
            }
            if (es->default_input[0] != '\0') {
                pstrcat(cas.prompt, sizeof(cas.prompt), "(default ");
                pstrcat(cas.prompt, sizeof(cas.prompt), es->default_input);
                pstrcat(cas.prompt, sizeof(cas.prompt), ") ");
            }
            minibuffer_edit(s, def_input, cas.prompt, hist, cas.completion, arg_edit_cb, es);
            return;
        }
    }

    /* all arguments are parsed: we can now execute the command */
    /* if not taken as argument, argval is handled as repetition count.
       A negative or zero count prevents executing the command, unless it
       takes the prefix argument explicitly */
    if (es->has_arg && es->argval >= 0) {
        rep_count = es->argval;
    } else {
        rep_count = 1;
    }
    // XXX: reset es->argval?

    qs->this_cmd_func = d->action.func;
    qs->cmd_start_time = get_clock_ms();

    while (rep_count --> 0) {
        /* special case for hex mode */
        if (d->action.ESii != do_char) {
            s->hex_nibble = 0;
            /* special case for character composing */
            if (d->action.ESi != do_backspace)
                s->compose_len = 0;
        }
#ifndef CONFIG_TINY
        qe_save_selection(qs, FALSE);
#endif
        /* Save and restore ec context */
        ec = qs->ec;
        qs->ec.function = d->name;
        call_func(d->sig, d->action, es->nb_args, es->args, es->args_type);
        qs->ec = ec;
        /* CG: This doesn't work if the function needs input */
        /* CG: Should test for abort condition */
        /* CG: Should follow qs->active_window ? */
    }

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
                qe_free(unconst(char **)&es->args[i].p);
                break;
            }
        }
        qe_free(esp);
    }
}

/* when the argument has been typed by the user, this callback is
   called */
static void arg_edit_cb(void *opaque, char *str, CompletionDef *completion)
{
    ExecCmdState *es = opaque;
    int index, val;
    const char *p;

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
        if (completion && completion->convert_entry) {
            val = completion->convert_entry(es->s, str, &p);
        } else {
            val = strtol_c(str, &p, 0);
        }
        if (*p != '\0') {
            put_error(es->s, "Invalid number: %s", str);
            goto fail;
        }
        es->args[index].n = val;
        break;
    case CMD_ARG_STRING:
        if (str[0] == '\0' && es->default_input[0] != '\0') {
            qe_free(&str);
            str = qe_strdup(es->default_input);
        }
        es->args[index].p = str; /* will be freed at end of the command */
        break;
    }
    /* now we can parse the following arguments */
    parse_arguments(es);
}

int check_read_only(EditState *s)
{
    if (s->b->flags & BF_READONLY) {
        put_error(s, "Buffer is read-only");
        return 1;
    } else {
        return 0;
    }
}

void do_execute_command(EditState *s, const char *cmd, int argval)
{
    const CmdDef *d;

    /* XXX: should test for '(' and '=' and evaluate script instead */
    d = qe_find_cmd(s->qs, cmd);
    if (d) {
        exec_command(s, d, argval, 0);
    } else {
        put_error(s, "No command %s", cmd);
    }
}

void window_display(EditState *s)
{
    QEmacsState *qs = s->qs;
    CSSRect rect;

    /* set the clipping rectangle to the whole window */
    /* XXX: should clip out popup windows */
    rect.x1 = s->xleft;
    rect.y1 = s->ytop;
    rect.x2 = rect.x1 + s->width;
    rect.y2 = rect.y1 + s->height;
    set_clip_rectangle(s->screen, &rect);

    if (qs->complete_refresh) {
        edit_invalidate(s, 0);
        s->borders_invalid = 1;
    }

    s->mode->display(s);

    display_mode_line(s);
    display_window_borders(s);
}

/* display all windows */
/* XXX: should use correct clipping to avoid popups display hacks */
void qe_display(QEmacsState *qs)
{
    EditState *s;
    int has_popups, has_minibuf;
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
    has_minibuf = 0;
    for (s = qs->first_window; s != NULL; s = s->next_window) {
        if (s->flags & WF_POPUP) {
            has_popups++;
        }
        if (s->flags & WF_MINIBUF) {
            has_minibuf++;
        }
    }

    /* refresh normal windows and minibuf with popup kludge */
    for (s = qs->first_window; s != NULL; s = s->next_window) {
        if (!(s->flags & WF_POPUP) &&
            ((s->flags & WF_MINIBUF) || !has_popups || qs->complete_refresh)) {
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

    /* Redraw status and diag messages */
    if (*qs->status_shadow || *qs->diag_shadow) {
        int width = qs->screen->width;
        int height = qs->status_height;
        int x = 0, y = qs->screen->height - height;

        if (*qs->status_shadow && !has_minibuf) {
            print_at_byte(qs->screen, x, y, width, height,
                          qs->status_shadow, QE_STYLE_STATUS);
        }
        if (*qs->diag_shadow) {
            int w = strlen(qs->diag_shadow) + 1;
            w *= get_glyph_width(qs->screen, NULL, QE_STYLE_STATUS, '0');
            print_at_byte(qs->screen, x + width - w, y, w, height,
                          qs->diag_shadow, QE_STYLE_STATUS);
        }
    }

    elapsed_time = get_clock_ms() - start_time;
    if (elapsed_time >= 100)
        put_status(qs->active_window, "|qe_display: %dms", elapsed_time);

    qs->complete_refresh = 0;
    dpy_flush(qs->screen);
}

/*---------------- Keyboard macros ----------------*/

/* XXX: missing macro commands:
   `macro-edit-lossage`           C-x C-k l
   Edit most recent 300 keystrokes as a keyboard macro.
   `kbd-macro-query`              C-x q
   Query user during kbd macro execution.
   `show-macro`, `dump-macro` to ease macro debugging and timing
 */

static void qe_clear_macro(QEmacsState *qs) {
    qe_free(&qs->macro_keys);
    qs->macro_keys_size = 0;
    qs->nb_macro_keys = 0;
    qs->nb_macro_keys_run = 0;
}

static void qe_stop_macro(QEmacsState *qs) {
    // XXX: should output a message
    if (qs->defining_macro) {
        qs->defining_macro = 0;
        qe_clear_macro(qs);
    }
    qs->executing_macro = 0;
    qs->macro_key_index = -1;
}

void do_start_kbd_macro(EditState *s)
{
    /*@CMD start-kbd-macro
       ### `start-kbd-macro()`

       Record subsequent keyboard input, defining a keyboard macro.
       The commands are recorded even as they are executed.
       Use `end-kbd-macro` (bound to `C-x )`) to finish recording and
       make the macro available.
       Use `name-last-kbd-macro` to give it a permanent name.
       Use `call-last-kbd-macro` (bound to `C-x e` or `C-\`) to replay
       the keystrokes.
     */
    QEmacsState *qs = s->qs;

    if (qs->defining_macro) {
        put_status(s, "Already defining kbd macro: restarting");
    } else {
        put_status(s, "Defining kbd macro...");
    }
    qe_clear_macro(qs);
    qs->defining_macro = 1;
    qs->macro_counter = 0;
}

#ifdef CONFIG_TINY
#define save_last_kbd_macro(s)
#else
static void save_last_kbd_macro(EditState *s) {
    QEmacsState *qs = s->qs;
    char buf[32];
    buf_t out[1];
    DynBuf db[1];
    int i, len, haskey = 1;
    const char *p;
    StringArray *hist;

    if (qs->defining_macro || !qs->nb_macro_keys)
        return;

    dbuf_init(db);
    for (i = 0; i < qs->nb_macro_keys; i++) {
        buf_init(out, buf, sizeof(buf));
        len = buf_put_key(out, qs->macro_keys[i]);
        if (len != 1
        ||  haskey
        ||  find_key_suffix(dbuf_str(db), out->buf[0]) != -1) {
            if (i > 0)
                dbuf_putc(db, ' ');
        }
        dbuf_putstr(db, out->buf);
        haskey = (len != 1);
    }
    p = dbuf_str(db);
    hist = qe_get_history(qs, "macrokeys");
    remove_string(hist, p);
    add_string(hist, p, 0);
    dbuf_free(db);
}

static void qe_macro_add_key(QEmacsState *qs, int key);

static void do_edit_last_kbd_macro(EditState *s, const char *keys) {
    QEmacsState *qs = s->qs;
    const char *p;

    if (!keys || !*keys)
        return;

    qe_clear_macro(qs);
    qs->macro_counter = 0;

    p = keys;
    while (qe_skip_spaces(&p)) {
        int key = strtokey(&p);
        qe_macro_add_key(qs, key);
    }
    save_last_kbd_macro(s);
    put_status(s, "Keyboard macro redefined");
}

static void do_name_last_kbd_macro(EditState *s, const char *name)
{
    StringArray *hist = qe_get_history(s->qs, "macrokeys");
    if (hist && hist->nb_items) {
        do_define_kbd_macro(s, name, hist->items[hist->nb_items - 1]->str, NULL);
    }
}

static void do_insert_kbd_macro(EditState *s, const char *name)
{
    QEmacsState *qs = s->qs;
    EditBuffer *b = s->b;

    if (name && *name) {
        const CmdDef *d = qe_find_cmd(qs, name);
        if (d && d->action.ESs == do_execute_macro_keys) {
            const char *keys = d->spec + 2;   /* skip @{ */
            b->offset = s->offset;
            eb_printf(b, "define_kbd_macro(\"%s\", \"", name);
            while (keys[1]) {                   /* stop at } */
                char32_t c = utf8_decode(&keys);
                if (c == '\\' || c == '"')
                    eb_putc(b, '\\');
                eb_putc(b, c);
            }
            eb_puts(b, "\", \"\");\n");
            s->offset = b->offset;
        }
    } else {
        StringArray *hist = qe_get_history(qs, "macrokeys");
        if (hist && hist->nb_items) {
            const char *keys = hist->items[hist->nb_items - 1]->str;
            b->offset = s->offset;
            eb_printf(b, "edit_last_kbd_macro(\"");
            while (keys[0]) {
                char32_t c = utf8_decode(&keys);
                if (c == '\\' || c == '"')
                    eb_putc(b, '\\');
                eb_putc(b, c);
            }
            eb_puts(b, "\");\n");
            s->offset = b->offset;
        }
    }
}

static void do_read_kbd_macro(EditState *s, int mark, int offset) {
    char buf[1024];
    int start = min_offset(mark, offset);
    int stop = max_offset(mark, offset);
    eb_get_region_contents(s->b, start, stop, buf, sizeof buf, 0);
    do_edit_last_kbd_macro(s, buf);
}

static void show_macro_counter(EditState *s) {
    QEmacsState *qs = s->qs;
    put_status(s, "New macro counter: %d", qs->macro_counter);
}

static void do_macro_add_counter(EditState *s, int arg) {
    QEmacsState *qs = s->qs;
    qs->macro_counter += arg;
    show_macro_counter(s);
}

static void do_macro_set_counter(EditState *s, int arg) {
    QEmacsState *qs = s->qs;
    qs->macro_counter = arg;
    show_macro_counter(s);
}

static int check_format_string(const char *fmt1, const char *fmt2, int max_width) {
    /*@API utils
       Check that a format string is compatible with a set of parameters.
       @argument `fmt1` a valid pointer to a C format string.
       @argument `fmt2` a valid pointer to a C format string with a minimal
       set of conversion specifiers without flags, width, precision.
       @return the number of conversions matched or `-1` if there is a
       type mismatch or too many conversions in the `fmt` string.
     */
    const char *p;
    const char *q;
    int found = 0;
    for (p = fmt1, q = fmt2; (p = strchr(p, '%')) != NULL; p++, q++) {
        p++;
        if (*p == '%')
            continue;
        if (*q++ != '%')
            return -1;
        p += strspn(p, "+- #0123456789");
        if (*p == '.')
            p += 1 + strspn(p + 1, "0123456789");
        switch (*p) {
        case 'h':
            p += 1 + (p[1] == 'h');
            break;
        case 'l':
            if (*q++ != 'l')
                return -1;
            p++;
            if (*p == 'l') {
                p++;
                if (*q++ != 'l')
                    return -1;
            } else
            if (*p == 'c' && *q != *p)
                return -1;
            break;
        case 'L':
        case 'j':
        case 't':
        case 'z':
            if (*p++ != *q++)
                return -1;
            break;
        }
        if (*p == '\0' || *q == '\0')
            return -1;
        if (*p != *q) {
            if ((memchr("bBcdiouxX", *p, 9) && memchr("bBcdiouxX", *q, 9))
            ||  (memchr("aAeEfFgG", *p, 8) && memchr("aAeEfFgG", *q, 8)))
                continue;
            return -1;
        }
        found++;
    }
    return found;
}

static void do_macro_insert_counter(EditState *s, int arg) {
    QEmacsState *qs = s->qs;
    int (*eb_printf_fun)(EditBuffer *b, const char *fmt, ...) = eb_printf;
    const char *fmt = qs->macro_format;
    int n = qs->macro_counter;
    if (!fmt || !*fmt)
        fmt = "%d";
    if (check_format_string(fmt, "%d%d%d%d", 1024) < 0) {
        put_error(s, "Invalid macro format: %s", fmt);
        return;
    }
    s->b->offset = s->offset;
    eb_printf_fun(s->b, fmt, n, n, n, n);
    s->offset = s->b->offset;
    qs->macro_counter += arg;
    show_macro_counter(s);
}

static void do_macro_set_format(EditState *s, const char *fmt) {
    QEmacsState *qs = s->qs;
    qe_free(&qs->macro_format);
    if (fmt)
        qs->macro_format = qe_strdup(fmt);
}
#endif

void do_end_kbd_macro(EditState *s)
{
    /*@CMD end-kbd-macro
       ### `end-kbd-macro()`

       Stop recording the keyboard macro. The last keyboard macro can
       be run using `call-last-kbd-macro`.
     */
    QEmacsState *qs = s->qs;

    /* if called inside a macro, it is last recorded keys, so ignore
       it */
    if (qs->macro_key_index != -1)
        return;

    if (!qs->defining_macro) {
        put_error(s, "Not defining kbd macro");
        return;
    }
    qs->defining_macro = 0;
    /* remove the last key(s) that invoked this function */
    qs->nb_macro_keys = qs->nb_macro_keys_run;
    save_last_kbd_macro(s);
    put_status(s, "Keyboard macro defined");
}

void do_call_last_kbd_macro(EditState *s, int argval)
{
    /*@CMD call-last-kbd-macro
       ### `call-last-kbd-macro(argval)`

       Run the last keyboard macro recorded by `start-kbd-macro`.
       Repeat `argval` times.
     */
    QEmacsState *qs = s->qs;
    int set_repeat = (qs->last_key == 'e');

    if (qs->defining_macro) {
        // XXX: should allow recursive definition
        qs->defining_macro = 0;
        put_error(s, "Cannot execute macro while defining one");
        return;
    }

    if (qs->nb_macro_keys > 0) {
        while (argval-- > 0) {
            /* CG: should share code with do_execute_macro */
            for (qs->macro_key_index = 0;
                 qs->macro_key_index < qs->nb_macro_keys;
                 qs->macro_key_index++)
            {
                int key = qs->macro_keys[qs->macro_key_index];
                qe_key_process(qs, key);
                if (qs->macro_key_index < 0) {
                    // After 0 kbd macro iterations: Keyboard macro terminated by a command ringing the bell
                    argval = 0;
                    break;
                }
            }
        }
        qs->macro_key_index = -1;
        qe_free_bindings(&qs->first_transient_key);
        if (set_repeat)
            qe_register_transient_binding(qs, "call-last-kbd-macro", "e");
    }
}

void do_execute_macro_keys(EditState *s, const char *keys)
{
    QEmacsState *qs = s->qs;
    const char *p;
    int key;

    qs->executing_macro++;

    /* Interactive commands get their input from the macro, unless some
     * suspend mechanism is added to create interactive macros.
     */

    p = keys;
    while (qe_skip_spaces(&p)) {
        key = strtokey(&p);
        qe_key_process(qs, key);
        if (!qs->executing_macro) {
            // After 0 kbd macro iterations: Keyboard macro terminated by a command ringing the bell
        }
    }
    if (qs->executing_macro)
        qs->executing_macro--;
}

void do_define_kbd_macro(EditState *s, const char *name, const char *keys,
                         const char *key_bind)
{
    const CmdDef *d;
    CmdDef *def;
    int size, name_len;
    char *buf;

    size = 2 + strlen(keys) + 3;
    buf = qe_malloc_array(char, size);

    // XXX: should special case "last-kbd-macro"

    /* CG: should parse macro keys to an array and pass index
     * to do_execute_macro.
     */
    snprintf(buf, size, "@{%s}%c", keys, 0);

    d = qe_find_cmd(s->qs, name);
    if (d && d->action.ESs == do_execute_macro_keys) {
        /* redefininig a macro */
        /* XXX: freeing the current macro definition may cause a crash if it
         * is currently executing.
         */
        def = unconst(CmdDef *)d;
        qe_free(unconst(char **)&def->spec);
        def->spec = buf;
    } else {
        def = qe_mallocz(CmdDef);
        name_len = strlen(name);
        /* allocate space for name and extra NUL for no bindings */
        def->name = memcpy(qe_mallocz_bytes(name_len + 2), name, name_len);
        def->spec = buf;
        def->sig = CMD_ESs;
        def->val = 0;
        def->action.ESs = do_execute_macro_keys;
        /* register allocated command */
        qe_register_commands(s->qs, NULL, def, -1);
    }
    if (key_bind && *key_bind) {
        do_set_key(s, key_bind, name, 0);
    }
}

#ifndef CONFIG_TINY
static void qe_save_macro(EditState *s, const CmdDef *def, EditBuffer *b)
{
    QEmacsState *qs = s->qs;
    char buf[32];
    buf_t outbuf, *out;
    int i;
    const char *name = "last-kbd-macro";

    if (def)
        name = def->name;

    eb_printf(b, "define_kbd_macro(\"%s\", \"", name);

    if (def) {
        const char *keys = def->spec + 2;   /* skip @{ */
        while (keys[1]) {                   /* stop at } */
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
    QEmacsState *qs = s->qs;
    const CmdDef *d;
    int i, j;

    eb_puts(b, "// macros:\n");
    qe_save_macro(s, NULL, b);

    /* Enumerate defined macros */
    for (i = 0; i < qs->cmd_array_count; i++) {
        for (j = qs->cmd_array[i].count, d = qs->cmd_array[i].array; j-- > 0; d++) {
            if (d->action.ESs == do_execute_macro_keys)
                qe_save_macro(s, d, b);
        }
    }
    eb_putc(b, '\n');
}
#endif

#define MACRO_KEY_INCR 64

static void qe_macro_add_key(QEmacsState *qs, int key)
{
    int new_size;

    if (qs->nb_macro_keys >= qs->macro_keys_size) {
        new_size = qs->macro_keys_size + MACRO_KEY_INCR;
        if (!qe_realloc_array(&qs->macro_keys, new_size))
            return;
        qs->macro_keys_size = new_size;
    }
    qs->macro_keys[qs->nb_macro_keys++] = key;
}

/*---------------- multi-cursor handling ----------------*/

#ifdef CONFIG_TINY
#define qe_free_multi_cursor(s)
#else
static int qe_add_multi_cursor_position(EditState *s, int offset) {
    /* First unregister the callbacks because the array may be reallocated */
    struct QECursor *cp;
    if (s->multi_cursor_len >= s->multi_cursor_size) {
        /* reallocate array of QECursor */
        int new_size = s->multi_cursor_len + s->multi_cursor_len / 2 + 16;
        int reallocated = FALSE;
        int i;
        /* unregister callbacks because array may move */
        for (i = 0; i < s->multi_cursor_len; i++) {
            eb_free_callback(s->b, eb_offset_callback, &s->multi_cursor[i].mark);
            eb_free_callback(s->b, eb_offset_callback, &s->multi_cursor[i].offset);
        }
        if (qe_realloc_array(&s->multi_cursor, new_size)) {
            reallocated = TRUE;
            s->multi_cursor_size = new_size;
        }
        for (i = 0; i < s->multi_cursor_len; i++) {
            eb_add_callback(s->b, eb_offset_callback, &s->multi_cursor[i].mark, 0);
            eb_add_callback(s->b, eb_offset_callback, &s->multi_cursor[i].offset, 0);
        }
        if (!reallocated)
            return -1;
    }
    cp = &s->multi_cursor[s->multi_cursor_len++];
    cp->kill_buf = NULL;
    cp->kill_len = 0;
    cp->kill_size = 0;
    cp->mark = offset;
    cp->offset = offset;
    eb_add_callback(s->b, eb_offset_callback, &cp->mark, 0);
    eb_add_callback(s->b, eb_offset_callback, &cp->offset, 0);
    return 0;
}

static void qe_free_multi_cursor(EditState *s) {
    while (s->multi_cursor_len > 0) {
        struct QECursor *cp = &s->multi_cursor[--s->multi_cursor_len];
        eb_free_callback(s->b, eb_offset_callback, &cp->mark);
        eb_free_callback(s->b, eb_offset_callback, &cp->offset);
        qe_free(&cp->kill_buf);
        cp->kill_len = 0;
        cp->kill_size = 0;
    }
    qe_free(&s->multi_cursor);
    s->multi_cursor_size = 0;
    s->multi_cursor_len = 0;
    s->multi_cursor_cur = 0;
    s->multi_cursor_active = 0;
}

static void do_activate_multi_cursor(EditState *s) {
    if (s->multi_cursor_active)
        return;
    if (s->region_style) {
        int start_line, start_col, end_line, end_col, line;
        int start = min_int(s->b->mark, s->offset);
        int end = max_int(s->b->mark, s->offset);
        eb_get_pos(s->b, &start_line, &start_col, start);
        eb_get_pos(s->b, &end_line, &end_col, end);
        qe_free_multi_cursor(s);
        for (line = start_line; line < end_line; line++) {
            // TODO: should we pad line if too short?
            int pos = eb_goto_pos(s->b, line, start_col);
            qe_add_multi_cursor_position(s, pos);
        }
        // TODO: need some way of rendering multi-line cursor
        s->region_style = 0;
        s->offset = start;
    }
    if (s->multi_cursor_len) {
        swap_int(&s->b->mark, &s->multi_cursor[0].mark);
        swap_int(&s->offset, &s->multi_cursor[0].offset);
        s->multi_cursor_cur = 0;
        s->multi_cursor_active = 1;
    } else {
        put_error(s, "No multi-cursor defined");
    }
}

static void do_start_multi_cursor(EditState *s) {
    qe_free_multi_cursor(s);
    // TODO: need some way of rendering multi-line cursor
    do_set_mark(s);
}

static void do_end_multi_cursor(EditState *s) {
    // XXX: not sure what UX to use
    do_activate_multi_cursor(s);
}

static void do_add_multi_cursor(EditState *s) {
    qe_add_multi_cursor_position(s, s->offset);
}
#endif

/*---------------- key dispatcher ----------------*/

void do_prefix_argument(EditState *s, int key) {
    /* Behavior of prefix-argument keys:
       C-u:
       increment c->has_arg
       if !c->is_numeric_arg: multiply the c->argval by 4
       '-':
       if !c->is_numeric_arg: negate c->sign and set c->has_arg
       else: normal key
       A--: negate c->sign and set c->has_arg
       0-9 and A-0 to A-9:
       if !c->is_numeric_arg: set c->arval and c->is_numeric_arg
       else: multiply c->argval and add digit
     */
    QEmacsState *qs = s->qs;
    QEKeyContext *c = &qs->key_ctx;

    if (key == KEY_CTRL('u')) {
        /* increment has_arg and multiply argval unless already numeric */
        if (!(c->has_arg & HAS_ARG_NUMERIC))
            c->argval *= 4;
        c->has_arg++;
        c->nb_keys = 0;
    } else
    if ((key >= '0' && key <= '9')
    ||  (key >= KEY_META('0') && key <= KEY_META('9'))) {
        if (!(c->has_arg & HAS_ARG_NUMERIC)) {
            c->has_arg |= HAS_ARG_NUMERIC;
            c->argval = 0;
        }
        c->argval = c->argval * 10 + (key & 15);
        c->nb_keys = 0;
    } else
    if ((key == '-' && !(c->has_arg & HAS_ARG_NUMERIC))
    ||  (key == KEY_META('-'))) {
        /* negate argument sign and set has_arg */
        c->has_arg ^= HAS_ARG_NEGATIVE;
        c->has_arg |= HAS_ARG_SIGN;
        c->nb_keys = 0;
    }
}

/*
 * All typed keys are sent to the callback. Previous grab is aborted
 */
void qe_grab_keys(QEmacsState *qs, void (*cb)(void *opaque, int key), void *opaque)
{
    QEKeyContext *c = &qs->key_ctx;

    /* CG: Should free previous grab? */
    /* CG: Should grabing be window dependent ? */
    c->grab_key_cb = cb;
    c->grab_key_opaque = opaque;
}

/*
 * Abort key grabing
 */
void qe_ungrab_keys(QEmacsState *qs)
{
    QEKeyContext *c = &qs->key_ctx;

    /* CG: Should have an indicator to free previous grab */
    c->grab_key_cb = NULL;
    c->grab_key_opaque = NULL;
    if (qs->defining_macro) {
        qs->nb_macro_keys_run = qs->nb_macro_keys;
    }
}

/* init qe key handling context */
static void qe_key_init(QEKeyContext *c) {
    c->has_arg = 0;
    c->argval = 1;
    c->is_escape = 0;
    c->nb_keys = 0;
    c->buf[0] = '\0';
}

KeyDef *qe_find_binding(unsigned int *keys, int nb_keys, KeyDef *kd, int exact)
{
    for (; kd != NULL; kd = kd->next) {
        if (kd->nb_keys >= nb_keys
        &&  !blockcmp(kd->keys, keys, nb_keys)
        &&  (!exact || kd->nb_keys == nb_keys)) {
            break;
        }
    }
    return kd;
}

KeyDef *qe_find_current_binding(QEmacsState *qs, unsigned int *keys, int nb_keys, ModeDef *m, int exact)
{
    KeyDef *kd;

    if (qs->first_transient_key) {
        /* first look up transient repeat mode */
        kd = qe_find_binding(keys, nb_keys, qs->first_transient_key, exact);
        if (kd)
            return kd;
        qe_free_bindings(&qs->first_transient_key);
    }

    for (; m; m = m->fallback) {
        kd = qe_find_binding(keys, nb_keys, m->first_key, exact);
        if (kd != NULL)
            return kd;
    }
    return qe_find_binding(keys, nb_keys, qs->first_key, exact);
}

static void qe_key_process(QEmacsState *qs, int key)
{
    QEKeyContext *c = &qs->key_ctx;
    EditState *s;
    KeyDef *kd;
    const CmdDef *d;
    char buf1[128];
    buf_t outbuf, *out;
    int len;
    unsigned int key_redirect = KEY_NONE;

    if (qs->defining_macro && !qs->executing_macro) {
        qe_macro_add_key(qs, key);
    }

  again:
    // XXX: shound test for help-popup
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
    if (s == NULL) {
        s = qs->active_window = qs->first_window;
        if (s == NULL)
            return;
    }
    put_status(s, "&");     /* Erase pending keystrokes and message */

    /* Special case for escape: we transform it as meta so
       that unix users are happy ! */
    if (key == KEY_ESC && c->nb_keys == 1) {
        c->is_escape = 1;
        goto next;
    } else
    if (c->is_escape) {
        compose_keys(c->keys, &c->nb_keys);
        c->is_escape = 0;
        key = c->keys[c->nb_keys - 1];
    }

    /* see if one command is found */
    kd = qe_find_current_binding(qs, c->keys, c->nb_keys, s->mode, 0);
    if (!kd) {
        /* no key found */
        unsigned int key_default = KEY_DEFAULT;

        if (c->nb_keys == 1) {
            if (!KEY_IS_SPECIAL(key) && !KEY_IS_CONTROL(key)) {
                if (c->has_arg && !c->describe_key) {
                    do_prefix_argument(s, key);
                    /* check if key was consumed by do_prefix_argument */
                    if (!c->nb_keys)
                        goto next;
                }
                kd = qe_find_current_binding(qs, &key_default, 1, s->mode, 1);
                if (kd)
                    goto exec_cmd;
            }
            if (key == KEY_SHIFT(KEY_DEL)
            ||  key == KEY_SHIFT(KEY_RET)
            ||  key == KEY_CONTROL(KEY_RET)) {
                /* redirect to unmodified version */
                // TODO: need more redirections
                key_redirect = key & 0xff;
                kd = qe_find_current_binding(qs, &key_redirect, 1, s->mode, 1);
                if (kd)
                    goto exec_cmd;
            }
        }
        out = buf_init(&outbuf, buf1, sizeof(buf1));
        buf_puts(out, "No command on ");
        buf_put_keys(out, c->keys, c->nb_keys);
        if (qs->trace_buffer)
            qe_trace_bytes(qs, buf1, -1, EB_TRACE_COMMAND);
        put_status(s, "&%s%s", buf1, c->describe_key ? "" : "\007");
        c->describe_key = 0;
        qe_key_init(c);
        if (qs->trace_buffer)
            qe_display(qs);
        return;
    } else
    if (c->nb_keys == kd->nb_keys) {
    exec_cmd:
        d = kd->cmd;
        if (c->describe_key) {
            out = buf_init(&outbuf, buf1, sizeof(buf1));
            buf_put_keys(out, c->keys, c->nb_keys);
            if (key_redirect != KEY_NONE) {
                buf_puts(out, " redirected to ");
                buf_put_key(out, key_redirect);
            }
            if (c->describe_key > 1) {
                int save_offset = s->b->offset;
                s->b->offset = s->offset;
                s->offset += eb_printf(s->b, "%s runs the command %s", buf1, d->name);
                s->b->offset = save_offset;
            } else {
                put_status(s, "%s runs the command %s", buf1, d->name);
            }
            c->describe_key = 0;
        } else
        if (d->action.ESsi == do_describe_key_briefly) {
            c->describe_key = 1 + (c->has_arg != 0);
            qe_key_init(c);
            strcpy(c->buf, "Describe key: ");
            key = -1;
            goto next;
        } else
        if (d->action.ESi == do_prefix_argument) {
            do_prefix_argument(s, key);
            /* always consume the key */
            c->nb_keys = 0;
            goto next;
        } else {
            int argval = c->argval;
            int multi_cursor_active = s->multi_cursor_active;
            if (c->has_arg & HAS_ARG_NEGATIVE)
                argval = -argval;
            else
            if (!c->has_arg) {
                // XXX: temporary hack
                argval = NO_ARG;
            }
            /* To allow recursive calls to qe_key_process, especially
             * from macros, we reset the QEKeyContext before
             * dispatching the command
             */
            qe_key_init(c);
            if (d->action.ESi != do_repeat) {
                qs->last_cmd = d;
                qs->last_argval = argval;
                qs->last_key = key;
            }
            exec_command(s, d, argval, key);
            if (multi_cursor_active && qe_check_window(qs, &s)) {
                int i;
                if (s != qs->active_window) {
                    /* end multi-cursor session upto window change */
                    s->multi_cursor_active = 0;
                }
                s->multi_cursor[0].offset = s->offset;
                for (i = 1; s->multi_cursor_active && i < s->multi_cursor_len; i++) {
                    swap_int(&s->b->mark, &s->multi_cursor[i].mark);
                    swap_int(&s->offset, &s->multi_cursor[i].offset);
                    s->multi_cursor_cur = i;
                    /* prevent append-next-kill */
                    if (s->qs->last_cmd_func == (CmdFunc)do_append_next_kill)
                        qs->last_cmd_func = NULL;
                    exec_command(s, d, argval, key);
                    if (!qe_check_window(qs, &s))
                        break;
                    s->multi_cursor_cur = 0;
                    swap_int(&s->b->mark, &s->multi_cursor[i].mark);
                    swap_int(&s->offset, &s->multi_cursor[i].offset);
                    if (s != qs->active_window) {
                        // TODO: notify user?
                        s->multi_cursor_active = 0;
                    }
                }
            }
        }
        if (qs->defining_macro) {
            qs->nb_macro_keys_run = qs->nb_macro_keys;
        }
        qe_key_init(c);
        // XXX: should delay until after macro execution
        qe_display(qs);
        /* CG: should move ungot key handling to generic event dispatch */
        if (qs->ungot_key != -1) {
            key = qs->ungot_key;
            qs->ungot_key = -1;
            goto again;
        }
        return;
    }
 next:
    /* display prefix key pressed */
    if (key >= 0) {
        len = strlen(c->buf);
        if (len > 0 && c->buf[len-1] == '-')
            c->buf[len-1] = ' ';
        /* Should print argument if any in a more readable way */
        out = buf_attach(&outbuf, c->buf, sizeof(c->buf), len);
        buf_put_key(out, key);
        buf_put_byte(out, '-');
    }
    put_status(s, "&~%s", c->buf);
    if (qs->trace_buffer)
        qe_display(qs);
}

/* Print a UTF-8 encoded buffer as unicode */
void print_at_byte(QEditScreen *screen,
                   int x, int y, int width, int height,
                   const char *str, QETermStyle style)
{
    char32_t ubuf[MAX_SCREEN_WIDTH];
    int len;
    QEStyleDef styledef;
    QEFont *font;
    CSSRect rect;

    len = utf8_to_char32(ubuf, countof(ubuf), str);
    get_style(NULL, &styledef, style);

    /* clip rectangle */
    rect.x1 = x;
    rect.y1 = y;
    rect.x2 = rect.x1 + width;
    rect.y2 = rect.y1 + height;
    set_clip_rectangle(screen, &rect);

    /* start rectangle */
    fill_rectangle(screen, x, y, width, height, styledef.bg_color);
    font = select_font(screen, styledef.font_style, styledef.font_size);
    draw_text(screen, font, x, y + font->ascent, ubuf, len, styledef.fg_color);
    release_font(screen, font);
}

/* XXX: should take va_list */
static void qe_format_message(QEmacsState *qs, const char *bufname,
                              const char *message)
{
    char header[128];
    EditBuffer *eb = NULL;
    buf_t outbuf, *out;

    out = buf_init(&outbuf, header, sizeof(header));

    if (qs->ec.filename)
        buf_printf(out, "%s:%d: ", qs->ec.filename, qs->ec.lineno);

    if (qs->ec.function)
        buf_printf(out, "%s: ", qs->ec.function);

    if (bufname)
        eb = qe_new_buffer(qs, bufname, BC_REUSE | BF_UTF8);
    if (eb) {
        eb_printf(eb, "%s%s\n", header, message);
    } else {
        fprintf(stderr, "%s%s\n", header, message);
    }
}

/* Produce an error message from internal sources */
void qe_put_error(QEmacsState *qs, const char *fmt, ...)
{
    char buf[MAX_SCREEN_WIDTH];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    put_status(qs ? qs->active_window : NULL, "!\007\006%s", buf);
}

void qe_dpy_error(QEditScreen *s, const char *fmt, ...)
{
    char buf[MAX_SCREEN_WIDTH];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    put_status(s->qs->active_window, "!\007\006%s", buf);
}

/* Display an error message in the status area.
   `s` may be NULL in extreme circumstances
 */
void put_error(EditState *s, const char *fmt, ...)
{
    char buf[MAX_SCREEN_WIDTH];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    put_status(s, "!\007\006%s", buf);
}

/* Display a message in the status area.
   `s` may be NULL in extreme circumstances
 */
void put_status(EditState *s, const char *fmt, ...)
{
    char buf[MAX_SCREEN_WIDTH];
    const char *p;
    va_list ap;
    int silent = 0;
    int diag = 0;
    int force = 0;
    int beep = 0;
    int error = 0;
    int flush = 0;
    QEmacsState *qs;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    for (p = buf;; p++) {
        if (*p == '\007') {
            beep = 1;
        } else
        if (*p == '\006') {
            error = 1;
        } else
        if (*p == '|') {
            diag = 1;
        } else
        if (*p == '~') {
            silent = 1;
        } else
        if (*p == '!') {
            force = 1;
        } else
        if (*p == '&') {
            flush = 1;
        } else {
            break;
        }
    }

    if (!s) {
        if (qe_skip_spaces(&p))
            fprintf(stderr, "qe: %s\n", p);
        return;
    }

    qs = s->qs;
    if (qs->screen->dpy.dpy_probe) {
        /* Display message on screen if initialized */
        int width = qs->screen->width;
        int height = qs->status_height;
        int x = 0, y = qs->screen->height - height;

        if (qs->active_window && (qs->active_window->flags & WF_MINIBUF)) {
            /* display status messages in diag area if minibuffer is active */
            diag = 1;
        }
        if (diag) {
            if (force || !strequal(p, qs->diag_shadow)) {
                /* right align display and overwrite last diag message */
                int w = strlen(qs->diag_shadow);
                w = snprintf(qs->diag_shadow, sizeof(qs->diag_shadow),
                             "%*s", w, p) + 1;
                w *= get_glyph_width(qs->screen, NULL, QE_STYLE_STATUS, '0');
                print_at_byte(qs->screen, x + width - w, y, w, height,
                              qs->diag_shadow, QE_STYLE_STATUS);
                pstrcpy(qs->diag_shadow, sizeof(qs->diag_shadow), p);
            }
        } else {
            if (force || !strequal(p, qs->status_shadow)) {
                print_at_byte(qs->screen, x, y, width, height,
                              p, QE_STYLE_STATUS);
                pstrcpy(qs->status_shadow, sizeof(qs->status_shadow), p);
            }
        }
    }
    if (!silent && qe_skip_spaces(&p)) {
        if (error)
            qe_format_message(qs, "*errors*", p);
        qe_format_message(qs, "*messages*", p);
    }
    if (beep) {
        qe_stop_macro(qs);
        dpy_sound_bell(s->screen);
    }
    if (flush)
        dpy_flush(s->screen);
}

#if 0
EditState *qe_find_file_window(QEmacsState *qs, const char *filename)
{
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
        b0->ref_count--;
        /* Save generic mode data to the buffer */
        generic_save_window_data(s);
        qe_free_multi_cursor(s);

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
        b->ref_count++;
        if (b->saved_data) {
            /* Restore window mode and data from buffer saved data */
            memcpy(s, b->saved_data, SAVED_DATA_SIZE);
            s->offset = min_offset(s->offset, b->total_size);
            s->offset_top = min_offset(s->offset_top, b->total_size);
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
                s->indent_width = s->qs->default_tab_width;
                s->default_style = QE_STYLE_DEFAULT;
                s->wrap = mode ? mode->default_wrap : WRAP_AUTO;
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

static int edit_detach_list(EditState **ep, EditState *s) {
    int found = FALSE;
    while (*ep) {
        if ((*ep)->target_window == s) {
            (*ep)->target_window = NULL;
        }
        if (*ep == s) {
            found = TRUE;
            *ep = s->next_window;
            s->next_window = NULL;
        } else {
            ep = &(*ep)->next_window;
        }
    }
    return found;
}

/* detach the window from the window tree. */
static void edit_detach(EditState *s)
{
    QEmacsState *qs = s->qs;

    /* unlink the window from the frame */
    edit_detach_list(&qs->first_window, s);
    edit_detach_list(&qs->first_hidden_window, s);
    /* if window was active, activate target window or default window */
    if (qs->active_window == s) {
        if (s->target_window)
            qs->active_window = s->target_window;
        else
            qs->active_window = qs->first_window;
    }
}

/* move a window before another one */
static void edit_attach(EditState *s, EditState *e)
{
    QEmacsState *qs = s->qs;
    EditState **ep;

    if (s != e) {
        /* Detach the window from the frame */
        for (ep = &qs->first_window; *ep; ep = &(*ep)->next_window) {
            if (*ep == s) {
                *ep = s->next_window;
                s->next_window = NULL;
                break;
            }
        }
        /* Re-attach the window before `e` */
        for (ep = &qs->first_window; *ep; ep = &(*ep)->next_window) {
            if (*ep == e)
                break;
        }
        s->next_window = *ep;
        *ep = s;
        // FIXME: This side effect should not be necessary
        if (qs->active_window == NULL)
            qs->active_window = s;
    }
}

/* compute the client area from the window position */
void compute_client_area(EditState *s)
{
    QEmacsState *qs = s->qs;
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
        y1 += s->caption ? qs->mode_line_height : qs->border_width;
        y2 -= qs->border_width;
    }
    if (s->flags & WF_RSEPARATOR)
        x2 -= qs->separator_width;

    s->xleft = x1;
    s->ytop = y1;
    s->width = x2 - x1;
    s->height = y2 - y1;

    s->line_height = s->char_width = 1;
    if (s->screen && s->screen->dpy.dpy_probe) {
        /* use window default style font except for dummy display */
        s->line_height = max_int(1, get_line_height(s->screen, s, QE_STYLE_DEFAULT));
        s->char_width = max_int(1, get_glyph_width(s->screen, s, QE_STYLE_DEFAULT, '0'));
    }

    s->rows = max_int(1, s->height / s->line_height);
    s->cols = max_int(1, s->width / s->char_width);
}

/* Create a new edit window, add it in the window list and sets it
 * active if none are active. The coordinates include the window
 * borders.
 */
EditState *qe_new_window(EditBuffer *b,
                         int x1, int y1, int width, int height, int flags)
{
    QEmacsState *qs = b->qs;
    EditState *s, *e;

    s = qe_mallocz(EditState);
    if (!s) {
        qe_put_error(qs, "Out of memory for window on '%s'", b->name);
        return NULL;
    }
    s->qs = qs;
    s->screen = qs->screen;
    s->x1 = x1;
    s->y1 = y1;
    s->x2 = x1 + width;
    s->y2 = y1 + height;
    s->flags = flags;
    compute_client_area(s);

    if (flags & WF_POPUP) {
        /* attach popups at the end of list */
        e = NULL;
    } else {
        /* link window in window list */
        for (e = qs->first_window; e != NULL; e = e->next_window) {
            if (e->flags & WF_POPUP) {
                /* keep popups at the end of list */
                break;
            }
            if (e->y1 > s->y1 || (e->y1 == s->y1 && e->x1 > s->x1)) {
                /* keep window list sorted by increasing y */
                break;
            }
        }
    }
    edit_attach(s, e);

    /* restore saved window settings, set mode */
    switch_to_buffer(s, b);
    return s;
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
        /* closing the window mode should have freed it already */
        qe_free_mode_data(s->mode_data);
        qe_free(&s->prompt);
        qe_free(&s->caption);
        qe_free(&s->line_shadow);
#ifndef CONFIG_TINY
        qe_free_multi_cursor(s);
#endif
        s->shadow_nb_lines = 0;
        qe_free(sp);
    }
}

static const char *file_completion_ignore_extensions = {
    "|bak"
    "|xls|xlsx|ppt|pptx|swf" /* binary formats */
    "|apk"
    "|bin|obj|dll|exe" /* DOS binaries */
    "|o|so|a" /* Unix binaries */
    "|dylib|dSYM" /* macOS */
    "|gz|tgz|taz|bz2|bzip2|xz|zip|rar|z|tar" /* archives */
    "|cma|cmi|cmo|cmt|cmti|cmx"
    "|class|jar" /* java */
    "|b"
    "|"
};

void file_complete(CompleteState *cp, CompleteFunc enumerate)
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

    if (cp->completion->flags & CF_RESOURCE) {
        QEmacsState *qs = cp->s->qs;
        ffst = find_file_open(qs->res_path, file, FF_PATH | FF_NOXXDIR);
    } else {
        int flags = FF_NOXXDIR;
        if (cp->completion->flags & CF_DIRNAME)
            flags |= FF_ONLYDIR;
        if (cp->fuzzy)
            flags |= 1;  // recursion level
        ffst = find_file_open(*path ? path : ".", file, flags);
    }
    while (find_file_next(ffst, filename, sizeof(filename)) == 0) {
        struct stat sb;

        base = get_basename(filename);
        /* ignore known backup files (hardcoded test for *~) */
        len = strlen(base);
        if (!len || base[len - 1] == '~')
            continue;
        /* ignore known binary file extensions */
        if (match_extension(base, file_completion_ignore_extensions))
            continue;
        if (*base == '.') {
            if (strequal(base, ".DS_Store"))
                continue;
        }
        /* stat the file to find out if it's a directory.
         * In this case add a slash to speed up typing long paths
         */
        if (!stat(filename, &sb) && S_ISDIR(sb.st_mode))
            pstrcat(filename, sizeof(filename), "/");
        if (sb.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) {
            /* XXX: if the file has no extension and is executable,
             * should check and ignore binary executable files.
             */
        }
        enumerate(cp, filename, CT_SET);
    }
    find_file_close(&ffst);
}

static CompletionDef file_completion = {
    .name = "file",
    .enumerate = file_complete,
#ifndef CONFIG_TINY
    .print_entry = file_print_entry,
#endif
    .flags = CF_FILENAME,
};

#ifndef CONFIG_TINY
static CompletionDef dir_completion = {
    .name = "dir",
    .enumerate = file_complete,
    .print_entry = file_print_entry,
    .flags = CF_DIRNAME | CF_NO_FUZZY,
};

static CompletionDef resource_completion = {
    .name = "resource",
    .enumerate = file_complete,
    .print_entry = file_print_entry,
    .flags = CF_RESOURCE | CF_NO_FUZZY,
};
#endif

void buffer_complete(CompleteState *cp, CompleteFunc enumerate) {
    QEmacsState *qs = cp->s->qs;
    EditBuffer *b;

    for (b = qs->first_buffer; b != NULL; b = b->next) {
        if (!(b->flags & BF_SYSTEM))
            enumerate(cp, b->name, CT_GLOB);
    }
}

static int buffer_print_entry(CompleteState *cp, EditState *s, const char *name)
{
    EditBuffer *b = s->b;
    QEmacsState *qs = s->qs;
    EditBuffer *b1 = qe_find_buffer_name(qs, name);
    int len;

    if (b1) {
        b->cur_style = QE_STYLE_KEYWORD;
        len = eb_puts(b, b1->name);
        b->tab_width = max3_int(16, 2 + len, b->tab_width);
        len += eb_putc(b, '\t');
        if (*b1->filename) {
            b->cur_style = QE_STYLE_COMMENT;
            len += eb_puts(b, b1->filename);
        }
        b->cur_style = QE_STYLE_DEFAULT;
    } else {
        return eb_puts(b, name);
    }
    return len;
}

static CompletionDef buffer_completion = {
    .name = "buffer",
    .enumerate = buffer_complete,
    .print_entry = buffer_print_entry,
};

static int default_completion_window_print_entry(CompleteState *cp, EditState *s, const char *name) {
    return eb_puts(s->b, name);
}

static int default_completion_window_get_entry(EditState *s, char *dest, int size, int offset) {
    int len = eb_fgets(s->b, dest, size, offset, &offset);
    char *p = strchr(dest, '\t');
    if (p != NULL)
        len = p - dest;
    dest[len] = '\0';   /* strip the TAB or trailing newline if any */
    return len;
}

static int completion_sort_func(const void *p1, const void *p2)
{
    const StringItem * const *pp1 = (const StringItem * const *)p1;
    const StringItem * const *pp2 = (const StringItem * const *)p2;
    const StringItem *item1 = *pp1;
    const StringItem *item2 = *pp2;

    /* Group items by group order */
    if (item1->group != item2->group)
        return item1->group - item2->group;
    /* Use natural sort: keep numbers in order */
    return qe_strcollate(item1->str, item2->str);
}

/* register a new completion method */
void qe_register_completion(QEmacsState *qs, CompletionDef *cp)
{
    CompletionDef **p;

    for (p = &qs->first_completion;; p = &(*p)->next) {
        if (*p == cp) {
            /* completion is already registered, do nothing */
            return;
        }
        if (*p == NULL) {
            cp->next = NULL;
            *p = cp;
            break;
        }
    }
    if (!cp->print_entry)
        cp->print_entry = default_completion_window_print_entry;
    if (!cp->get_entry)
        cp->get_entry = default_completion_window_get_entry;
    if (!cp->sort_func)
        cp->sort_func = completion_sort_func;
}

static CompletionDef *qe_find_completion(QEmacsState *qs, const char *name)
{
    CompletionDef *p;

    if (name[0] != '\0') {
        for (p = qs->first_completion; p != NULL; p = p->next) {
            if (strequal(p->name, name))
                return p;
        }
    }
    return NULL;
}

static void complete_start(CompleteState *cp, EditState *s, int start, int end,
                           EditState *target)
{
    memset(cp, 0, sizeof(*cp));
    cp->s = s;
    cp->target = target;
    cp->start = start;
    cp->end = end;
    cp->len = eb_get_region_contents(s->b, cp->start, cp->end,
                                     cp->current, sizeof(cp->current), 0);
}

static void complete_test(CompleteState *cp, const char *str, int mode) {
    int fuzzy = 0;

    switch (mode) {
    case CT_GLOB:
        if (!strmatch_pat(str, cp->current, 1))
            return;
        break;
    case CT_IGLOB:
        if (!utf8_strimatch_pat(str, cp->current, 1))
            return;
        break;
    case CT_STRX:
        if (!strxstart(str, cp->current, NULL))
            return;
        break;
    case CT_TEST:
        if (memcmp(str, cp->current, cp->len)) {
            if (!qe_memicmp(str, cp->current, cp->len))
                fuzzy = 1;
            else
            if (cp->fuzzy && strmem(str, cp->current, cp->len))
                fuzzy = 2;
            else
                return;
        }
        break;
    }
    add_string(&cp->cs, str, fuzzy);
}

static void complete_end(CompleteState *cp)
{
    free_strings(&cp->cs);
}

/* mini buffer stuff */

typedef struct MinibufState {
    QEModeData base;

    void (*cb)(void *opaque, char *buf, CompletionDef *completion);
    void *opaque;

    EditState *completion_popup_window;  /* XXX: should have a popup_window member */
    int completion_stage;
    int completion_flags;
    int completion_start;
    int completion_end;
    int completion_count;
    CompletionDef *completion;

    StringArray *history;
    int history_index;
    int history_saved_offset;
} MinibufState;

static ModeDef minibuffer_mode;

static inline MinibufState *minibuffer_get_state(EditState *e, int status) {
    return qe_get_buffer_mode_data(e->b, &minibuffer_mode, status ? e : NULL);
}

static int match_strings(const char *s1, const char *s2, int len) {
    /*@API utils
       Find the length of the common prefix, only count complete UTF-8
       sequences.
       @argument `s1` a valid string pointer
       @argument `s2` a valid string pointer
       @argument `len` the maximum number of bytes to compare. This count
       is assumed to only include complete UTF-8 sequences.
       @return the length of the common prefix, between `0` and `len`.
     */
    int pos, i;

    for (i = pos = 0; i < len; i++) {
        u8 c = s1[i];
        if (!utf8_is_trailing_byte(c))
            pos = i;
        if (c != s2[i])
            return pos;
    }
    return len;
}

void do_minibuffer_complete(EditState *s, int type, int key, int argval) {
    QEmacsState *qs = s->qs;
    int count, i, match_len, start, end;
    CompleteState cs;
    StringItem **outputs;
    EditState *e;
    EditBuffer *b;
    int w, h, h1, w1;
    MinibufState *mb;
    const char *p;

    if ((mb = minibuffer_get_state(s, 1)) == NULL)
        return;

    if (!mb->completion || !mb->completion->enumerate) {
        if (type != COMPLETION_OTHER)
            do_char(s, key, argval);
        return;
    }
    /* Remove highlighted selection. */
    // XXX: Should complete based on point position,
    //      not necessarily full minibuffer contents
    do_delete_selection(s);

    /* XXX: if completion_popup_window already displayed, should page
     * through the window, if at end, should remove focus from
     * completion_popup_window or close it.
     */

    if (type == COMPLETION_TAB && qs->last_cmd_func == qs->this_cmd_func) {
        mb->completion_stage++;
        if (mb->completion->flags & CF_NO_FUZZY)
            mb->completion_stage = 2;
    } else {
        mb->completion_stage = 0;
    }

    /* check completion window */
    qe_check_window(s->qs, &mb->completion_popup_window);
    if (mb->completion_popup_window && mb->completion_stage > 1) {
        /* toggle completion popup on TAB */
        mb->completion_stage = 0;
        qs->this_cmd_func = 0;
        edit_close(&mb->completion_popup_window);
        do_refresh(s);
        return;
    }

    start = 0;
    end = s->offset;
    if (mb->completion_flags) {
        /* XXX: completion select? */
        int offset = end;
        while ((start = offset) > 0) {
            char32_t c = eb_prevc(s->b, offset, &offset);
            if (!qe_isalnum_(c) && c != '-' && c != '#')
                break;
        }
    }
    mb->completion_start = start;
    mb->completion_end = end;
    complete_start(&cs, s, start, end, s->target_window);
    cs.completion = mb->completion;
    if (!(mb->completion->flags & CF_NO_FUZZY))
        cs.fuzzy = mb->completion_stage;
    (*mb->completion->enumerate)(&cs, complete_test);
    sort_strings(&cs.cs, mb->completion->sort_func);
    remove_duplicate_strings(&cs.cs);
    count = cs.cs.nb_items;
    outputs = cs.cs.items;
    mb->completion_count = count;
#if 0
    printf("count=%d\n", count);
    for (i = 0; i < count; i++)
        printf("out[%d]=%s\n", i, outputs[i]->str);
#endif
    /* compute the longest match len */
    match_len = cs.len;

    if (count > 0) {
        /* find the longest common prefix */
        match_len = strlen(outputs[0]->str);
        for (i = 1; i < count; i++) {
            match_len = match_strings(outputs[0]->str, outputs[i]->str,
                                      match_len);
        }
        /* strip extra data */
        p = memchr(outputs[0]->str, '\t', match_len);
        if (p)
            match_len = p - outputs[0]->str;
    }
    if (match_len > cs.len) {
        /* add the possible chars */
        // XXX: potential UTF-8 issue?
        // XXX: replace the completed part, not necessarily at the start (use mark?)
        // XXX: should delete region and insert as UTF-8
        // XXX: should not replace if fuzzy match?
        eb_replace(s->b, cs.start, cs.end - cs.start, outputs[0]->str, match_len);
        s->offset = cs.start + match_len;
        mb->completion_end = s->offset;
        if (type == COMPLETION_OTHER) {
            /* mark the region with extra common characters */
            do_mark_region(s, cs.start + match_len, cs.start + cs.len);
        }
    } else {
        if (count > 1) {
            /* if more than one match, then display them in a new popup
               buffer */
            if (!mb->completion_popup_window) {
                char buf[60];

                b = qe_new_buffer(qs, "*completion*",
                                  BF_SYSTEM | BF_UTF8 | BF_TRANSIENT | BF_STYLE_COMP);
                if (!b)
                    return;
                b->default_mode = &list_mode;
                w1 = qs->screen->width;
                h1 = qs->screen->height - qs->status_height;
                w = (w1 * 3) / 4;
                h = (h1 * 3) / 4;
                e = qe_new_window(b, (w1 - w) / 2, (h1 - h) / 2, w, h, WF_POPUP);
                if (!e)
                    return;
                snprintf(buf, sizeof buf, "Select a %s:", mb->completion->name);
                e->caption = qe_strdup(buf);
                e->target_window = s;
                mb->completion_popup_window = e;
                do_refresh(e);
            }
        } else
        if (count == 0 || type != COMPLETION_OTHER) {
            /* close the popup when minibuf contents matches nothing */
            edit_close(&mb->completion_popup_window);
            do_refresh(s);
        }
    }
    if (mb->completion_popup_window) {
        /* modify the list with the current matches */
        e = mb->completion_popup_window;
        b = e->b;
        b->flags &= ~BF_READONLY;
        eb_delete(b, 0, b->total_size);
        b->tab_width = 4;
        for (i = 0; i < count; i++) {
            eb_putc(b, ' ');    /* XXX: should use window margins */
            mb->completion->print_entry(&cs, e, outputs[i]->str);
            eb_putc(b, '\n');
        }
        b->flags |= BF_READONLY;
        e->mouse_force_highlight = 1;
        e->force_highlight = 1;
        e->offset = 0;
    }
    complete_end(&cs);
}

static void do_minibuffer_electric_key(EditState *s, int key, int argval) {
    char32_t c;
    int offset, stop;
    MinibufState *mb = minibuffer_get_state(s, 0);

    /* erase beginning of line if typing / or ~ in certain places */
    if (mb && mb->completion && (mb->completion->flags & CF_FILENAME)
    &&  ((c = eb_nextc(s->b, 0, &offset)) == '/' || c == '~')) {
        stop = s->offset;
        c = eb_prevc(s->b, s->offset, &offset);
        if (c == '/') {
            /* kill leading part if typing a URL */
            if (eb_match_str_utf8_reverse(s->b, offset, "http:", 5, &stop)
            ||  eb_match_str_utf8_reverse(s->b, offset, "https:", 6, &stop)
            ||  eb_match_str_utf8_reverse(s->b, offset, "ftp:", 4, &stop)) {
                /* nothing, stop already updated */
            }
            eb_delete(s->b, 0, stop);
        }
    }
    do_char(s, key, argval);
}

static void do_minibuffer_electric_yank(EditState *s) {
    MinibufState *mb = minibuffer_get_state(s, 0);
    int stop = s->b->total_size;
    int offset;
    char32_t c;

    do_yank(s);

    /* erase beginning of line if yanking absolute path after / */
    if (mb && mb->completion && (mb->completion->flags & CF_FILENAME)
    &&  ((c = eb_nextc(s->b, 0, &offset)) == '/' || c == '~')) {
        c = eb_prevc(s->b, stop, &offset);
        if (c == '/') {
            /* check for absolute path */
            if (eb_match_char32(s->b, stop, '/', NULL)
            ||  eb_match_char32(s->b, stop, '~', NULL)
            ||  eb_match_str_utf8(s->b, stop, "http://", NULL)
            ||  eb_match_str_utf8(s->b, stop, "https://", NULL)
            ||  eb_match_str_utf8(s->b, stop, "ftp://", NULL)) {
                eb_delete(s->b, 0, stop);
            }
        }
    }
    s->qs->this_cmd_func = (CmdFunc)do_yank;
}

/* space does completion only if a completion method is defined */
void do_minibuffer_complete_space(EditState *s, int key, int argval) {
    QEmacsState *qs = s->qs;
    MinibufState *mb = minibuffer_get_state(s, 0);

    if (!mb || !mb->completion || !mb->completion->enumerate
    ||  (mb->completion->flags & CF_SPACE_OK)) {
        do_char(s, key, argval);
    } else
    if (qe_check_window(s->qs, &mb->completion_popup_window)
    &&  qs->last_cmd_func == qs->this_cmd_func
    &&  mb->completion_count > 1) {
        /* page through the list */
        // XXX: should close the popup at the bottom of the list
        do_scroll_up_down(mb->completion_popup_window, 2);
    } else {
        do_minibuffer_complete(s, COMPLETION_SPACE, key, argval);
    }
}

static void do_minibuffer_char(EditState *s, int key, int argval)
{
    MinibufState *mb = minibuffer_get_state(s, 0);

    do_char(s, key, argval);
    // XXX: this should be triggered by any minibuffer modification
    if (mb && qe_check_window(s->qs, &mb->completion_popup_window)) {
        /* automatic filtering of completion list */
        // XXX: should prevent auto-completion
        do_minibuffer_complete(s, COMPLETION_OTHER, key, argval);
    }
}

/* scroll in completion popup */
static void do_minibuffer_move_bof(EditState *s) {
    MinibufState *mb = minibuffer_get_state(s, 0);

    if (mb && qe_check_window(s->qs, &mb->completion_popup_window)) {
        //mb->completion_popup_window->force_highlight = 1;
        do_bof(mb->completion_popup_window);
        return;
    }
    s->offset = 0;
}

static void do_minibuffer_move_eof(EditState *s) {
    MinibufState *mb = minibuffer_get_state(s, 0);

    if (mb && qe_check_window(s->qs, &mb->completion_popup_window)) {
        //mb->completion_popup_window->force_highlight = 1;
        do_eof(mb->completion_popup_window);
        return;
    }
    s->offset = s->b->total_size;
}

static void do_minibuffer_scroll_up_down(EditState *s, int dir)
{
    MinibufState *mb = minibuffer_get_state(s, 0);

    if (mb && qe_check_window(s->qs, &mb->completion_popup_window)) {
        //mb->completion_popup_window->force_highlight = 1;
        do_scroll_up_down(mb->completion_popup_window, dir);
    }
}

static void minibuffer_set_str(EditState *s, int start, int end, const char *str)
{
    /* Replace the completion trigger zone */
    /* XXX: should insert UTF-8? */
    start += eb_replace(s->b, start, end - start, str, strlen(str));
    s->offset = start;
}

/* CG: should use buffer of responses */
StringArray *qe_get_history(QEmacsState *qs, const char *name) {
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

#ifndef CONFIG_TINY
static void qe_free_history_list(QEmacsState *qs) {
    HistoryEntry *p;
    while ((p = qs->first_history) != NULL) {
        qs->first_history = p->next;
        free_strings(&p->history);
        qe_free(&p);
    }
}
#endif

void do_minibuffer_history(EditState *s, int n)
{
    QEmacsState *qs = s->qs;
    MinibufState *mb;
    StringArray *hist;
    int index;
    char *str;
    char buf[2048];

    if ((mb = minibuffer_get_state(s, 0)) == NULL)
        return;

    /* if completion visible, move in it */
    if (qe_check_window(s->qs, &mb->completion_popup_window)) {
        mb->completion_popup_window->force_highlight = 1;
        do_up_down(mb->completion_popup_window, n);
        return;
    }

    hist = mb->history;
    if (!hist)
        return;

    index = mb->history_index + n;
    if (index < 0 || index >= hist->nb_items)
        return;

    if (qs->last_cmd_func != (CmdFunc)do_minibuffer_history) {
        /* save currently edited line (including embedded null bytes) */
        eb_get_contents(s->b, buf, sizeof(buf), 1);
        set_string(hist, hist->nb_items - 1, buf, 0);
        mb->history_saved_offset = s->offset;
    }
    /* insert history text */
    mb->history_index = index;
    str = hist->items[index]->str;
    minibuffer_set_str(s, 0, s->b->total_size, str);
    if (index == hist->nb_items - 1) {
        s->offset = mb->history_saved_offset;
    }
}

static void do_minibuffer_get_binary(EditState *s)
{
    unsigned long offset;

    if (s->target_window) {
        eb_read(s->target_window->b, s->target_window->offset,
                &offset, sizeof(offset));
        s->b->offset = s->offset;
        eb_printf(s->b, "%lu", offset);
    }
}

void do_minibuffer_exit(EditState *s, int do_abort)
{
    char buf[4096], *retstr;
    MinibufState *mb;
    CompletionDef *completion;
    StringArray *hist;
    EditState *cw;
    EditState *target;
    void (*cb)(void *opaque, char *buf, CompletionDef *completion);
    void *opaque;

    if ((mb = minibuffer_get_state(s, 1)) == NULL)
        return;

    cw = qe_check_window(s->qs, &mb->completion_popup_window);

    if (!do_abort) {
        /* if completion is activated, then select current file only if
           the selection is highlighted */
        if (cw && cw->force_highlight) {
            int len;
            len = mb->completion->get_entry(cw, buf, sizeof(buf), list_get_offset(cw) + 1);
            if (len > 0) {
                // insert completion string (delete highlighted part)
                minibuffer_set_str(s, mb->completion_start, mb->completion_end, buf);
            }
            if (mb->completion->flags & CF_NO_AUTO_SUBMIT) {
                edit_close(&mb->completion_popup_window);
                return;
            }
        }

        eb_get_contents(s->b, buf, sizeof(buf), 1);

        /* Append response to history list */
        hist = mb->history;
        if (hist && hist->nb_items > 0) {
            /* if null string, do not insert in history */
            hist->nb_items--;
            qe_free(&hist->items[hist->nb_items]);
            if (buf[0] != '\0')
                add_string(hist, buf, 0);
        }
    }

    /* remove completion popup if present */
    if (cw) {
        edit_close(&mb->completion_popup_window);
        cw = NULL;
        do_refresh(s);
    }

    cb = mb->cb;
    completion = mb->completion;
    opaque = mb->opaque;
    target = s->target_window;
    mb->cb = NULL;
    mb->opaque = NULL;

    if (completion && completion->end_edit) {
        if (do_abort)
            completion->end_edit(s, NULL, 0);
        else
            completion->end_edit(s, buf, countof(buf));
    }

    /* Close the minibuffer window */
    s->b->flags |= BF_TRANSIENT;
    edit_close(&s);

    /* Force status update and call the callback */
    if (do_abort) {
        put_error(target, "Canceled.");
        (*cb)(opaque, NULL, NULL);
    } else {
        put_status(target, "!");
        retstr = qe_strdup(buf);
        (*cb)(opaque, retstr, completion);
    }
}

/* Start minibuffer editing. When editing is finished, the callback is
   called with an allocated string. If the string is null, it means
   editing was aborted. */
void minibuffer_edit(EditState *e, const char *input, const char *prompt,
                     StringArray *hist, const char *completion_name,
                     void (*cb)(void *opaque, char *buf, CompletionDef *completion),
                     void *opaque)
{
    QEmacsState *qs = e->qs;
    MinibufState *mb;
    EditState *s;
    EditBuffer *b;
    int len;

    /* check if already in minibuffer editing */
    if (e->flags & WF_MINIBUF) {
        put_status(e, "|Already editing in minibuffer");
        cb(opaque, NULL, NULL);
        return;
    }

    b = qe_new_buffer(qs, "*minibuf*", BF_SYSTEM | BF_SAVELOG | BF_UTF8);
    if (!b)
        return;
    b->default_mode = &minibuffer_mode;

    s = qe_new_window(b, 0, qs->screen->height - qs->status_height,
                      qs->screen->width, qs->status_height, WF_MINIBUF);
    if (!s)
        return;
    s->target_window = e;
    s->prompt = qe_strdup(prompt);
    s->bidir = 0;
    s->default_style = QE_STYLE_MINIBUF;
    /* XXX: should come from mode.default_wrap */
    s->wrap = WRAP_TRUNCATE;

    mb = minibuffer_get_state(s, 0);
    if (mb) {
        mb->completion_popup_window = NULL;
        mb->completion = NULL;
        if (completion_name) {
            if (*completion_name == '.') {
                mb->completion_flags = 1;
                completion_name++;
            }
            mb->completion = qe_find_completion(qs, completion_name);
        }
        mb->history = hist;
        mb->history_saved_offset = 0;
        if (hist) {
            mb->history_index = hist->nb_items;
            add_string(hist, "", 0);
        }
        mb->cb = cb;
        mb->opaque = opaque;
        qs->active_window = s;
    }
    /* add default input */
    if (input) {
        /* Default input should already be encoded as UTF-8 */
        len = strlen(input);
        eb_write(b, 0, (const u8 *)input, len);
        s->offset = len;
    }
    if (mb->completion && mb->completion->start_edit) {
        mb->completion->start_edit(s);
    }
}

static void minibuffer_mode_free(EditBuffer *b, void *state)
{
    /* If minibuffer is destroyed, call callback with NULL pointer */
    MinibufState *mb = state;
    void (*cb)(void *opaque, char *buf, CompletionDef *completion);
    void *opaque;

    if (!mb)
        return;

    if (qe_check_window(b->qs, &mb->completion_popup_window))
        edit_close(&mb->completion_popup_window);

    cb = mb->cb;
    opaque = mb->opaque;
    mb->cb = NULL;
    mb->opaque = NULL;

    if (cb) {
        put_status(b->qs->active_window, "!Abort.");
        (*cb)(opaque, NULL, NULL);
    }
}

static const CmdDef minibuffer_commands[] = {
    CMD2( "minibuffer-insert", "default",
          "Insert a character into the minibuffer",
          do_minibuffer_char, ESii,
          "*" "k" "p")
    CMD1( "minibuffer-exit", "RET, LF",
          "End the minibuffer input",
          do_minibuffer_exit, 0)
    CMD1( "minibuffer-abort", "C-g, C-x C-g, M-C-g",
          "Abort the minibuffer input",
          do_minibuffer_exit, 1)
    CMD3( "minibuffer-complete", "TAB",
          "Try and complete the minibuffer input",
          do_minibuffer_complete, ESiii,
          "*" "v" "k" "p", COMPLETION_TAB)
    /* should take numeric prefix to specify word size */
    CMD0( "minibuffer-get-binary", "M-=",
          "Insert the byte value at point in the current buffer into the minibuffer",
          do_minibuffer_get_binary)
    CMD2( "minibuffer-complete-space", "SPC",
          "Try and complete the minibuffer input",
          do_minibuffer_complete_space, ESii,
          "*" "k" "p")
    CMD2( "minibuffer-previous-history-element", "C-p, up, M-p",
          "Replace contents of the minibuffer with the previous historical entry",
          do_minibuffer_history, ESi, "q")
    CMD2( "minibuffer-next-history-element", "C-n, down, M-n",
          "Replace contents of the minibuffer with the next historical entry",
          do_minibuffer_history, ESi, "p")
    CMD2( "minibuffer-electric-key", "/, ~",
          "Insert a character into the minibuffer with side effects",
          do_minibuffer_electric_key, ESii,
          "*" "k" "p")
    CMD2( "minibuffer-electric-yank", "C-y",
          "Yank from kill buffer with side effects",
          do_minibuffer_electric_yank, ES, "*")
    /* commands used to configure search flags */
    CMD0( "minibuffer-toggle-case-fold", "M-c, C-c",
          "toggle search case-sensitivity",
          isearch_toggle_case_fold)
    CMD0( "minibuffer-toggle-hex", "M-h, M-C-b",
          "toggle normal/hex/unihex searching",
          isearch_toggle_hex)
#ifdef CONFIG_REGEX
    CMD0( "minibuffer-toggle-regexp", "M-r, C-t",
          "toggle regular-expression mode",
          isearch_toggle_regexp)
#endif
    CMD0( "minibuffer-toggle-word-match", "M-w",
          "toggle word match",
          isearch_toggle_word_match)
};

void qe_minibuffer_init(QEmacsState *qs)
{
    /* populate and register minibuffer mode and commands */
    // XXX: remove this mess: should just inherit with fallback
    memcpy(&minibuffer_mode, &text_mode, offsetof(ModeDef, first_key));
    minibuffer_mode.name = "minibuffer";
    minibuffer_mode.mode_probe = NULL;
    minibuffer_mode.buffer_instance_size = sizeof(MinibufState);
    minibuffer_mode.mode_free = minibuffer_mode_free;
    minibuffer_mode.move_bof = do_minibuffer_move_bof;
    minibuffer_mode.move_eof = do_minibuffer_move_eof;
    minibuffer_mode.scroll_up_down = do_minibuffer_scroll_up_down;
    qe_register_mode(qs, &minibuffer_mode, MODEF_NOCMD | MODEF_VIEW);
    qe_register_commands(qs, &minibuffer_mode, minibuffer_commands, countof(minibuffer_commands));
}

/* list paging mode */

ModeDef list_mode;

/* get current position (index) in list */
int list_get_pos(EditState *s)
{
    int line, col;
    eb_get_pos(s->b, &line, &col, s->offset);
    return line;
}

/* get current offset of the line in list */
int list_get_offset(EditState *s)
{
    return eb_goto_bol(s->b, s->offset);
}

void list_toggle_selection(EditState *s, int dir)
{
    int offset, offset1, flags;
    char32_t ch;

    if (dir < 0)
        text_move_up_down(s, -1);

    offset = list_get_offset(s);

    ch = eb_nextc(s->b, offset, &offset1);
    if (ch == ' ')
        ch = '*';
    else
        ch = ' ';
    flags = s->b->flags & BF_READONLY;
    s->b->flags ^= flags;
    eb_replace_char32(s->b, offset, ch);
    s->b->flags ^= flags;

    if (dir > 0)
        text_move_up_down(s, 1);
}

static int list_mode_init(EditState *s, EditBuffer *b, int flags)
{
    if (s) {
        /* XXX: should come from mode.default_wrap */
        s->wrap = WRAP_TRUNCATE;
    }
    return 0;
}

static void list_display_hook(EditState *s)
{
    /* Keep point at the beginning of a non empty line */
    if (s->offset && s->offset == s->b->total_size)
        s->offset = eb_prev(s->b, s->offset);
    s->offset = eb_goto_bol(s->b, s->offset);
}

static int list_init(QEmacsState *qs)
{
    // XXX: remove this mess: should just inherit with fallback
    memcpy(&list_mode, &text_mode, offsetof(ModeDef, first_key));
    list_mode.name = "list";
    list_mode.mode_probe = NULL;
    list_mode.mode_init = list_mode_init;
    list_mode.display_hook = list_display_hook;
    qe_register_mode(qs, &list_mode, MODEF_NOCMD | MODEF_VIEW);
    return 0;
}

/* popup paging mode */

static ModeDef popup_mode;

/* Verify that window still exists, return argument or NULL,
 * update handle if window is invalid.
 */
EditState *qe_check_window(QEmacsState *qs, EditState **sp)
{
    EditState *e0 = *sp;
    EditState *e;

    if (e0 == NULL)
        return NULL;

    for (e = qs->first_window; e != NULL; e = e->next_window) {
        if (e == e0)
            return e;
    }
    return *sp = NULL;
}

void do_popup_exit(EditState *s)
{
    QEmacsState *qs = s->qs;

    if (s->flags & WF_POPUP) {
        /* only do this for a popup? */
        // XXX: BF_TRANSIENT flag should be set at buffer creation time
        if (s->b->flags & BF_SYSTEM)
            s->b->flags |= BF_TRANSIENT;
        edit_close(&s);
        do_refresh(qs->active_window);
    }
}

/* show a popup on a readonly buffer */
EditState *show_popup(EditState *s, EditBuffer *b, const char *caption)
{
    QEmacsState *qs = s->qs;
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

    b->default_mode = &popup_mode;
    b->flags |= BF_READONLY;
    e = qe_new_window(b, (w1 - w) / 2, (h1 - h) / 2, w, h, WF_POPUP);
    if (e != NULL) {
        if (caption)
            e->caption = qe_strdup(caption);
        /* XXX: should come from mode.default_wrap */
        e->wrap = WRAP_TRUNCATE;
        e->target_window = s;
        qs->active_window = e;
        do_refresh(e);
    }
    return e;
}

// XXX: this should be a minor mode
static const CmdDef popup_commands[] = {
    CMD3( "popup-isearch", "/",
          "Search for contents",
          do_isearch, ESii, "p" "v", 1)
};

static void popup_init(QEmacsState *qs)
{
    /* popup mode inherits from text mode */
    // XXX: remove this mess: should just inherit with fallback
    memcpy(&popup_mode, &text_mode, offsetof(ModeDef, first_key));
    popup_mode.name = "popup";
    popup_mode.mode_probe = NULL;
    qe_register_mode(qs, &popup_mode, MODEF_VIEW);
    qe_register_commands(qs, &popup_mode, popup_commands, countof(popup_commands));
}

#ifndef CONFIG_TINY
/* insert a window to the left. Close all windows which are totally
   under it (XXX: should try to move them first */
EditState *insert_window_left(EditBuffer *b, int width, int flags)
{
    QEmacsState *qs = b->qs;
    EditState *e, *e_next, *e_new;

    for (e = qs->first_window; e != NULL; e = e_next) {
        e_next = e->next_window;
        if (e->flags & WF_MINIBUF)
            continue;
        if (e->x2 <= width) {
            edit_close(&e);
        } else
        if (e->x1 < width) {
            e->x1 = width;
        }
    }

    e_new = qe_new_window(b, 0, 0, width, qs->height - qs->status_height,
                          flags | WF_POPLEFT | WF_RSEPARATOR);
    if (e_new) {
        /* XXX: WRAP_AUTO is a better choice? */
        e_new->wrap = WRAP_TRUNCATE;
        // FIXME: should be useless
        do_refresh(e_new);
    }
    return e_new;
}

/* return a window on the side of window 's' */
EditState *find_window(EditState *s, int key, EditState *def)
{
    QEmacsState *qs = s->qs;
    EditState *e;

    /* CG: Should compute cursor position to disambiguate
     * non regular window layouts
     */
    for (e = qs->first_window; e != NULL; e = e->next_window) {
        if (e->flags & (WF_MINIBUF | WF_POPUP))
            continue;
        if (e->y1 <= s->y1 && e->y2 > s->y1) {
            /* horizontal overlap */
            if (key == KEY_RIGHT && e->x1 == s->x2)
                return e;
            if (key == KEY_LEFT && e->x2 == s->x1)
                return e;
        }
        if (e->x1 <= s->x1 && e->x2 > s->x1) {
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
    QEmacsState *qs = s->qs;

    if (!qs->first_transient_key) {
        put_status(s, "Window navigation, repeat with <up>, <down>, <left>, <right>");
        qe_register_transient_binding(qs, "find-window-down", "down");
        qe_register_transient_binding(qs, "find-window-left", "left");
        qe_register_transient_binding(qs, "find-window-right", "right");
        qe_register_transient_binding(qs, "find-window-up", "up");
    }
    qs->active_window = find_window(s, key, s);
}
#endif

/* Give a good guess to the user for the next buffer */
static EditBuffer *predict_switch_to_buffer(EditState *s)
{
    QEmacsState *qs = s->qs;
    EditBuffer *b;

    /* try and switch to the last buffer attached to the window */
    b = qe_check_buffer(qs, &s->last_buffer);
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

    if (s->flags & WF_MINIBUF)
        return;

    /* XXX: Default buffer charset should be selectable */
    b = qe_new_buffer(s->qs, bufname, BC_REUSE | BF_SAVELOG | BF_UTF8);
    if (b)
        switch_to_buffer(s, b);
}

int qe_count_buffers(QEmacsState *qs, EditBuffer *b0, int *countp, int mask, int val) {
    EditBuffer *b;
    int index = 0, count = 0;
    for (b = qs->first_buffer; b; b = b->next) {
        if (b == b0)
            index = count;
        if ((b->flags & mask) == val)
            count++;
    }
    if (countp)
        *countp = count;
    return index;
}

EditBuffer *qe_get_buffer_from_index(QEmacsState *qs, int index, int mask, int val) {
    EditBuffer *b;
    for (b = qs->first_buffer; b; b = b->next) {
        if ((b->flags & mask) == val) {
            if (index == 0)
                return b;
            index--;
        }
    }
    return NULL;
}

/* Find the n-th non-system buffer from the specified buffer in a given
   direction. Set up repeat map */
void do_buffer_navigation(EditState *s, int argval, int dir)
{
    QEmacsState *qs = s->qs;
    int buffer_index, buffer_count;
    EditBuffer *b;

    /* ignore command from the minibuffer and popups */
    if (s->flags & (WF_POPUP | WF_MINIBUF))
        return;

    buffer_index = qe_count_buffers(qs, s->b, &buffer_count, BF_SYSTEM, 0);
    /* no action if single non system buffer */
    if (buffer_count <= 1)
        return;
    if (!qs->first_transient_key) {
        put_status(s, "Buffer navigation, repeat with <left> and <right>");
        qe_register_transient_binding(qs, "next-buffer", "right, C-right");
        qe_register_transient_binding(qs, "previous-buffer", "left, C-left");
    }
    buffer_index = (buffer_index + argval * dir) % buffer_count;
    b = qe_get_buffer_from_index(qs, buffer_index, BF_SYSTEM, 0);
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

static void kill_buffer_confirm_cb(void *opaque, char *reply, CompletionDef *completion)
{
    EditBuffer *b = opaque;
    int yes_replied;

    if (!reply)
        return;
    yes_replied = strequal(reply, "yes");
    qe_free(&reply);
    if (!yes_replied)
        return;
    qe_kill_buffer(b->qs, b);
}

void do_kill_buffer(EditState *s, const char *bufname, int force)
{
    QEmacsState *qs = s->qs;
    char buf[1024];
    EditBuffer *b;

    b = qe_find_buffer_name(qs, bufname);
    if (!b) {
        put_error(s, "No buffer %s", bufname);
    } else {
        /* if modified and associated to a filename, then ask */
        if (!force && b->modified && b->filename[0] != '\0') {
            qe_stop_macro(qs);
            snprintf(buf, sizeof(buf),
                     "Buffer %s modified; kill anyway? (yes or no) ", bufname);
            minibuffer_edit(s, NULL, buf, NULL, NULL,
                            kill_buffer_confirm_cb, b);
        } else {
            qe_kill_buffer(qs, b);
        }
    }
}

void qe_kill_buffer(QEmacsState *qs, EditBuffer *b)
{
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
                    /* XXX: no regular buffer to show: we are killing the last
                     * regular buffer... Try and allocate a scratch buffer,
                     * but if this fails, all odds are off.
                     */
                    b1 = qe_new_buffer(qs, "*scratch*", BF_SAVELOG | BF_UTF8);
                    if (!b1)
                        return;
                }
            }
            switch_to_buffer(e, b1);
        }
    }

    /* now we can safely delete buffer */
    eb_free(&b);

    // XXX: should just return an update flag?
    // FIXME: useless as switching a window to another buffer should have refreshed it already
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

#ifdef CONFIG_WIN32
/* convert '\' to '/' */
static void path_win_to_unix(char *buf) {
    char *p;

    for (p = buf; *p; p++) {
        if (*p == '\\')
            *p = '/';
    }
}
#endif

/* canonicalize the path for a given window and make it absolute */
void canonicalize_absolute_path(EditState *s, char *buf, int buf_size, const char *path1)
{
    canonicalize_absolute_buffer_path(s ? s->b : NULL, s ? s->offset : 0, buf, buf_size, path1);
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
                if (!getcwd(cwd, sizeof(cwd)))
                    strcpy(cwd, ".");
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
                      const char *filename, int st_errno, int st_mode,
                      long total_size, const uint8_t *rawbuf, int len,
                      QECharset *charset, EOLType eol_type)
{
    u8 buf[4097];
    QEmacsState *qs = s->qs;
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
    probe_data.st_errno = st_errno;
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
            char32_t ch = probe_data.charset_state.table[rawbuf[offset]];
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
                        blockmove(modes + i + 1, modes + i, found_modes - i);
                        blockmove(scores + i + 1, scores + i, found_modes - i);
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

EditState *qe_find_target_window(EditState *s, int activate) {
    QEmacsState *qs = s->qs;
    EditState *e;

    /* Find the target window for some commands run from the dired window */
    if (s->flags & WF_POPUP) {
        e = qe_check_window(s->qs, &s->target_window);
        if (e) {
            if (activate && qs->active_window == s)
                qs->active_window = e;
        }
        s->b->flags |= BF_TRANSIENT;
        edit_close(&s);
        s = e;
        do_refresh(s);
    }
#ifndef CONFIG_TINY
    if (s && (s->flags & WF_POPLEFT) && s->x1 == 0) {
        e = find_window(s, KEY_RIGHT, NULL);
        if (e) {
            if (activate && qs->active_window == s)
                qs->active_window = e;
            s = e;
        }
    }
#endif
    return s;
}

/* Select appropriate mode for buffer:
 * if n == 0, select best mode
 * if n > 0, select n-th next mode
 * if n < 0, select n-th previous mode
 */
void do_set_next_mode(EditState *s, int n)
{
    if (s->flags & (WF_POPUP | WF_MINIBUF))
        return;

    /* next-mode from the dired window applies to the target window */
    s = qe_find_target_window(s, 0);
    qe_set_next_mode(s, n, 1);
}

void qe_set_next_mode(EditState *s, int n, int status)
{
    u8 buf[4097];
    int size;
    ModeDef *modes[32];
    int scores[32];
    int i, nb, found;
    EditBuffer *b;

    if (s->flags & WF_MINIBUF)
        return;

#ifndef CONFIG_TINY
    /* Find target window for POPLEFT pane */
    if ((s->flags & WF_POPLEFT) && s->x1 == 0) {
        EditState *e = find_window(s, KEY_RIGHT, NULL);
        if (e) {
            s = e;
        }
    }
#endif

    b = s->b;
    size = eb_read(b, 0, buf, sizeof(buf) - 1);
    buf[size] = '\0';

    nb = probe_mode(s, b, modes, countof(modes), scores, 2,
                    b->filename, 0, b->st_mode, b->total_size,
                    buf, size, b->charset, b->eol_type);
    found = 0;
    if (n && nb > 0) {
        for (i = 0; i < nb; i++) {
            if (s->mode == modes[i]) {
                found = (i + n) % nb;
                if (found < 0)
                    found += nb;
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
    QEmacsState *qs = s->qs;
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

    if ((lflags & LF_LOAD_RESOURCE) && !strchr(filename1, '/')) {
        if (qe_find_resource_file(qs, filename, sizeof(filename), filename1)) {
            put_error(s, "Cannot find resource file '%s'", filename1);
            return -1;
        }
    } else {
        /* compute full name */
        canonicalize_absolute_path((lflags & LF_CWD_RELATIVE) ? NULL :
                                   (s->b->flags & BF_DIRED) ? s :
                                   qe_find_target_window(s, 0),
                                   filename, sizeof(filename), filename1);
    }

#ifndef CONFIG_TINY
    /* when exploring from a popleft dired buffer, load a directory or
     * file pattern into the same pane, but load a regular file into the view pane
     */
    if ((s->flags & WF_POPUP)
    ||  (!is_directory(filename) &&
         ((lflags & LF_NOWILDCARD) || !is_filepattern(filename)))) {
        s = qe_find_target_window(s, 1);
    }
#endif

    if (s->flags & (WF_POPUP | WF_MINIBUF))
        return -1;

    if (lflags & LF_SPLIT_WINDOW) {
        /* Split window if window large enough and not empty */
        /* XXX: should check s->height units */
        if (s->height > 10 && s->b->total_size > 0) {
            EditState *e = qe_split_window(s, SW_STACKED, 50);
            if (e) {
                qs->active_window = s = e;
            }
        }
    }

#ifndef CONFIG_TINY
    if ((s->flags & WF_POPLEFT) && (s->b->flags & BF_DIRED) && is_directory(filename)) {
        do_dired_path(s, filename);
        return 0;
    }
#endif
    /* If file already loaded in existing buffer, switch to that */
    b = qe_find_buffer_filename(qs, filename);
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
    b = qe_new_buffer(qs, get_basename(filename), BF_SAVELOG | bflags);
    if (!b)
        return -1;

    eb_set_filename(b, filename);

    /* XXX: should actually initialize SAVED_DATA area in new buffer */
    s->offset = 0;
    s->wrap = WRAP_AUTO;  /* default mode may override this */

    /* First we try to read the first block to determine the data type */
    if (stat(filename, &st) < 0) {
        int st_errno = errno;
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
                   b->filename, st_errno, b->st_mode, b->total_size,
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
                        filename, 0, b->st_mode, st.st_size,
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
        if (!(lflags & LF_NOSELECT)) {
            /* XXX: bug: file loading will be delayed until buffer is
             * attached to a window.
             */
            switch_to_buffer(s, b);
        }
        if (access(b->filename, W_OK)) {
            b->flags |= BF_READONLY;
        }
        // XXX: problem if buffer is not attached to window
        do_load_qerc(s, s->b->filename);

        /* XXX: invalid place */
        edit_invalidate(s, 0);
        return 1;
    }

 fail:
    eb_free(&b);

    put_error(s, "Could not open '%s': %s", filename, strerror(errno));
    return -1;
}

#ifndef CONFIG_TINY
void qe_save_open_files(EditState *s, EditBuffer *b)
{
    QEmacsState *qs = s->qs;
    EditBuffer *b1;

    eb_puts(b, "// open files:\n");
    for (b1 = qs->first_buffer; b1 != NULL; b1 = b1->next) {
        if (!(b1->flags & BF_SYSTEM) && *b1->filename)
            eb_printf(b, "find_file(\"%s\");\n", b1->filename);
    }
    eb_putc(b, '\n');
}
#endif

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
        put_error(s, "Could not read file");
    }
    if (!s->b->probed) {
        qe_set_next_mode(s, 0, 0);
    }
    qe_display(s->qs);
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

void do_find_file_noselect(EditState *s, const char *filename, int bflags)
{
    qe_load_file(s, filename, LF_NOSELECT, bflags);
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
        put_error(s, "Could not open file '%s'", filename);
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
        put_error(s, "Error reading '%s'", filename);
        return;
    }
}

void do_set_visited_file_name(EditState *s, const char *filename,
                              const char *renamefile)
{
    /*@CMD set-visited-file-name
       ### `set-visited-file-name(string FILENAME, string RENAMEFILE)`

       Change the name of file visited in current buffer to FILENAME.
       This also renames the buffer to correspond to the new file. The
       next time the buffer is saved it will go in the newly specified
       file. If FILENAME is an empty string, mark the buffer as not
       visiting any file.

       If the RENAMEFILE argument is not null and starts with 'y', an
       attempt is made to rename the old visited file to the new name
       FILENAME.
     */
    char path[MAX_FILENAME_SIZE];

    *path = '\0';
    if (!filename || *filename) {
        canonicalize_absolute_path(s, path, sizeof(path), filename);
        // XXX: should search for another buffer with the same file
        if (renamefile && *renamefile == 'y' && *s->b->filename) {
            if (rename(s->b->filename, path))
                put_error(s, "Cannot rename file to %s", path);
        }
    }
    eb_set_filename(s->b, path);
}

static void put_save_message(EditState *s, const char *filename, int nb)
{
    if (nb >= 0) {
        put_status(s, "Wrote %d bytes to %s", nb, filename);
    } else {
        put_error(s, "Could not write %s", filename);
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
    QEmacsState *qs;
} QuitState;

static void quit_examine_buffers(QuitState *is);
static void quit_key(void *opaque, int ch);
static void quit_confirm_cb(void *opaque, char *reply, CompletionDef *completion);

static void do_suspend_qemacs(EditState *s, int argval)
{
    QEditScreen *sp = s->screen;
    if (sp->dpy.dpy_suspend)
        sp->dpy.dpy_suspend(sp);
}

void do_exit_qemacs(EditState *s, int argval)
{
    QEmacsState *qs = s->qs;
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
    is->qs = qs;

    qe_stop_macro(qs);
    qe_grab_keys(qs, quit_key, is);
    quit_examine_buffers(is);
}

/* analyse next buffer and ask question if needed */
static void quit_examine_buffers(QuitState *is)
{
    QEmacsState *qs = is->qs;
    EditBuffer *b;

    while (is->b != NULL) {
        b = is->b;
        if (!(b->flags & BF_SYSTEM) && b->filename[0] != '\0' && b->modified) {
            switch (is->state) {
            case QS_ASK:
                /* XXX: display cursor */
                put_status(qs->active_window,
                           "&Save file %s? (y, n, !, ., q) ", b->filename);
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
    qe_ungrab_keys(qs);

    /* now asks for confirmation or exit directly */
    if (is->modified) {
        qe_stop_macro(qs);
        minibuffer_edit(qs->active_window,
                        NULL, "Modified buffers exist; exit anyway? (yes or no) ",
                        NULL, NULL, quit_confirm_cb, NULL);
        qe_display(qs);
    } else {
#ifdef CONFIG_SESSION
        if (use_session_file)
            do_save_session(qs->active_window, 0);
#endif
        url_exit();
    }
    qe_free(&is);
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
    case KEY_LF:
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
        qe_ungrab_keys(is->qs);
        // FIXME: should use put_error()
        put_error(is->qs->active_window, "&Quit");
        return;
    default:
        /* get another key */
        return;
    }
    is->b = is->b->next;
    quit_examine_buffers(is);
}

static void quit_confirm_cb(qe__unused__ void *opaque,
                            char *reply,
                            qe__unused__ CompletionDef *completion)
{
    if (!reply)
        return;
    if (reply[0] == 'y' || reply[0] == 'Y')
        url_exit();
    qe_free(&reply);
}

/*----------------*/

int get_glyph_width(QEditScreen *screen, EditState *s, QETermStyle style, char32_t c)
{
    QEStyleDef styledef;
    QEFont *font;
    int width = 1;

    get_style(s, &styledef, style);
    font = select_font(screen, styledef.font_style, styledef.font_size);
    if (font) {
        width = glyph_width(screen, font, c);
        release_font(screen, font);
    }
    return width;
}

int get_line_height(QEditScreen *screen, EditState *s, QETermStyle style)
{
    QEStyleDef styledef;
    QEFont *font;
    int height = 1;

    get_style(s, &styledef, style);
    font = select_font(screen, styledef.font_style, styledef.font_size);
    if (font) {
        height = font->ascent + font->descent;
        release_font(screen, font);
    }
    return height;
}

void edit_invalidate(EditState *s, int all)
{
    /* invalidate the modeline buffer */
    s->modeline_shadow[0] = '\0';
    s->display_invalid = 1;
    if (all) {
        EditState *e;
        for (e = s->qs->first_window; e != NULL; e = e->next_window) {
            if (e->b == s->b) {
                s->modeline_shadow[0] = '\0';
                s->display_invalid = 1;
            }
        }
    }
}

/* refresh the screen, s1 can be any edit window */
void do_refresh(EditState *s1)
{
    QEmacsState *qs = s1->qs;
    EditState *e;
    int new_status_height, new_mode_line_height, content_height;
    int width, height, resized;

    if (qs->complete_refresh) {
        dpy_invalidate(qs->screen);
    }

    /* recompute various dimensions */
    if (qs->screen->media & CSS_MEDIA_TTY) {
        qs->separator_width = 1;
        qs->border_width = 1;
    } else {
        qs->separator_width = 4;
        qs->border_width = 3; /* XXX: adapt to display type */
    }

    width = qs->screen->width;
    height = qs->screen->height;
    new_status_height = get_line_height(qs->screen, NULL, QE_STYLE_STATUS);
    new_mode_line_height = get_line_height(qs->screen, NULL, QE_STYLE_MODE_LINE);
    content_height = height;
    if (!qs->hide_status)
        content_height -= new_status_height;

    /* Prevent potential division overflow */
    width = max_int(1, width);
    height = max_int(1, height);
    content_height = max_int(1, content_height);

    resized = 0;

    /* see if resize is necessary */
    if (qs->width != width ||
        qs->height != height ||
        qs->status_height != new_status_height ||
        qs->mode_line_height != new_mode_line_height ||
        qs->content_height != content_height) {

        /* do the resize */
        resized = 1;
        qs->complete_refresh = 1;
        for (e = qs->first_window; e != NULL; e = e->next_window) {
            if (e->flags & WF_MINIBUF) {
                /* first resize minibuffer if present */
                e->x1 = 0;
                e->y1 = content_height;
                e->x2 = width;
                e->y2 = height;
            } else
            if (qs->height == 0 || qs->width == 0 || qs->content_height == 0) {
                /* needed only to init the window size for the first time */
                e->x1 = 0;
                e->y1 = 0;
                e->y2 = content_height;
                e->x2 = width;
            } else {
                /* NOTE: to ensure that no rounding errors are made,
                   we resize the absolute coordinates */
                e->x1 = (e->x1 * width + qs->width / 2) / qs->width;
                e->x2 = (e->x2 * width + qs->width / 2) / qs->width;
                e->y1 = (e->y1 * content_height + qs->content_height / 2) / qs->content_height;
                e->y2 = (e->y2 * content_height + qs->content_height / 2) / qs->content_height;
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
        edit_invalidate(e, 0);
        e->borders_invalid = 1;
    }
    /* invalidate status line */
    qs->status_shadow[0] = '\0';

    if (resized) {
        /* CG: should compute column count w/ default count */
        put_status(s1, "Screen is now %d by %d (%d rows)",
                   width, height, height / new_status_height);
    }
}

void do_repeat(EditState *s, int argval)
{
    QEmacsState *qs = s->qs;
    int active = (s == qs->active_window);

    if (!qs->first_transient_key)
        qe_register_transient_binding(qs, "repeat", "z");

    while (argval --> 0) {
        exec_command(s, qs->last_cmd, qs->last_argval, qs->last_key);
        if (active)
            s = qs->active_window;
    }
}

void do_refresh_complete(EditState *s)
{
    QEmacsState *qs = s->qs;

    qs->complete_refresh = 1;

    if (s->qs->last_cmd_func == (CmdFunc)do_refresh_complete) {
        do_center_cursor(s, 1);
    } else {
        do_refresh(s);
    }
}

EditState *get_next_window(EditState *s, int mask, int val)
{
    QEmacsState *qs = s->qs;
    EditState *e, *s0 = s;

    for (;;) {
        if (s->next_window)
            e = s->next_window;
        else
            e = qs->first_window;
        if (e == s0)
            return NULL;
        if ((e->flags & mask) == val)
            return e;
        s = e;
    }
}

EditState *get_previous_window(EditState *s, int mask, int val)
{
    QEmacsState *qs = s->qs;
    EditState *e, *s0 = s;

    for (;;) {
        for (e = qs->first_window; e->next_window; e = e->next_window) {
            if (e->next_window == s)
                break;
        }
        if (e == s0)
            return NULL;
        if ((e->flags & mask) == val)
            return e;
        s = e;
    }
}

static EditState **get_window_link(EditState *s)
{
    QEmacsState *qs = s->qs;
    EditState **ep = &qs->first_window;
    for (;;) {
        if (*ep == s)
            return ep;
        if (*ep == NULL)
            break;
        ep = &(*ep)->next_window;
    }
    return NULL;
}

void do_other_window(EditState *s)
{
    EditState *e = get_next_window(s, 0, 0);
    if (e)
        s->qs->active_window = e;
}

void do_previous_window(EditState *s)
{
    EditState *e = get_previous_window(s, 0, 0);
    if (e)
        s->qs->active_window = e;
}

/* Delete a window and try to resize other windows so that it gets
   covered. If force is not true, do not accept to kill window if it
   is the only window or if it is the minibuffer window. */
void do_delete_window(EditState *s, int force)
{
    QEmacsState *qs = s->qs;
    EditState *e, *e1 = NULL;
    int count, pass, x1, y1, x2, y2;

    count = 0;
    for (e = qs->first_window; e != NULL; e = e->next_window) {
        if (!(e->flags & (WF_POPUP | WF_MINIBUF)))
            count++;
    }
    /* cannot close minibuf or if single window */
    if (((s->flags & WF_MINIBUF) || count <= 1) && !force)
        return;

    if (s->flags & WF_POPUP) {
        // XXX: this causes a crash on C-g from bufed
        //e1 = qe_check_window(s->qs, &s->target_window);
    } else {
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
                if (e == s || (e->flags & (WF_POPUP | WF_MINIBUF)))
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
    QEmacsState *qs = s->qs;
    EditState *e;

    if (s->flags & (WF_POPUP | WF_MINIBUF))
        return;

    s->flags &= ~WF_POPLEFT;

    for (;;) {
        for (e = qs->first_window; e != NULL; e = e->next_window) {
            if (e != s && !(e->flags & WF_MINIBUF))
                break;
        }
        if (e == NULL)
            break;
        /* rescan after closing a window because another window could
         * be closed as a side effect
         */
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
        s->flags &= ~(WF_RSEPARATOR);
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
    QEmacsState *qs = s->qs;
    EditState *e, *e1;

    for (e = qs->first_window; e != NULL; e = e1) {
        e1 = e->next_window;
        if (e->flags & WF_HIDDEN)
            edit_close(&e);
    }
}

/* XXX: add minimum size test and refuse to split if reached */
EditState *qe_split_window(EditState *s, int side_by_side, int prop)
{
    EditState *e;
    int w, h, w1, h1;

    /* cannot split minibuf or popup */
    if (s->flags & (WF_POPUP | WF_MINIBUF))
        return NULL;

    if (prop <= 0)
        return NULL;

    /* This will clone mode and mode data to the newly created window */
    generic_save_window_data(s);
    w = s->x2 - s->x1;
    h = s->y2 - s->y1;
    if (side_by_side) {
        w1 = (w * min_int(prop, 100) + 50) / 100;
        e = qe_new_window(s->b, s->x1 + w1, s->y1,
                          w - w1, h, WF_MODELINE | (s->flags & WF_RSEPARATOR));
        if (!e)
            return NULL;
        s->x2 = s->x1 + w1;
        s->flags |= WF_RSEPARATOR;
    } else {
        h1 = (h * min_int(prop, 100) + 50) / 100;
        e = qe_new_window(s->b, s->x1, s->y1 + h1,
                          w, h - h1, WF_MODELINE | (s->flags & WF_RSEPARATOR));
        if (!e)
            return NULL;
        s->y2 = s->y1 + h1;
    }
    compute_client_area(s);

    /* reposition new window in window list just after s */
    edit_attach(e, s->next_window);

    do_refresh(s);
    return e;
}

void do_split_window(EditState *s, int prop, int side_by_side)
{
    QEmacsState *qs = s->qs;
    EditState *e = qe_split_window(s, side_by_side, prop == NO_ARG ? 50 : prop);

    if (e && qs->flag_split_window_change_focus)
        qs->active_window = e;
}

void do_window_swap_states(EditState *s)
{
    QEmacsState *qs = s->qs;
    uint8_t buffer[offsetof(EditState, flags) - offsetof(EditState, xleft)];
    int mask = WF_POPUP | WF_MINIBUF | WF_HIDDEN | WF_POPLEFT | WF_FILELIST;
    int flags;
    EditState *e, *tmp, **elink, **slink;

    if (s->flags & mask)
        return;
    /* find another suitable window */
    e = get_previous_window(s, mask, 0);
    if (!e)
        return;
    /* swapping window positions and focus */
    memcpy(buffer, &s->xleft, sizeof buffer);
    memcpy(&s->xleft, &e->xleft, sizeof buffer);
    memcpy(&e->xleft, buffer, sizeof buffer);
    flags = (e->flags ^ s->flags) & (WF_RSEPARATOR | WF_MODELINE);
    e->flags ^= flags;
    s->flags ^= flags;
    elink = get_window_link(e);
    slink = get_window_link(s);
    if (elink && slink) {
        /* swap the windows in the list */
        *elink = s;
        *slink = e;
        tmp = e->next_window;
        e->next_window = s->next_window;
        s->next_window = tmp;
    }
    do_refresh(s);
    qs->active_window = e;
}

#ifndef CONFIG_TINY
void do_create_window(EditState *s, const char *filename, const char *layout)
{
    QEmacsState *qs = s->qs;
    static const char * const names[] = {
        "x1:", "y1:", "x2:", "y2:", "flags:", "wrap:",
        "offset:", "offset.col:", "mark:", "mark.col:", "top:", "top.col:",
        "active:",
    };
    int args[] = { 0, 0, 0, 0, WF_MODELINE, WRAP_AUTO, 0, 0, 0, 0, 0, 0, 0  };
    ModeDef *m = NULL;
    int i, n, x1, y1, x2, y2, flags;
    enum WrapType wrap;
    const char *p = layout;
    EditBuffer *b1;

    b1 = qe_find_buffer_filename(qs, filename);
    if (!b1) {
        put_error(s, "No such file loaded: %s", filename);
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
            m = qe_find_mode(qs, p, 0);
            break;
        }
        if (n >= countof(args))
            break;

        args[n] = strtol_c(p, &p, 0);
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

    s = qe_new_window(b1, x1, y1, x2 - x1, y2 - y1, flags);
    if (s == NULL)
        return;
    if (m)
        edit_set_mode(s, m);
    s->wrap = wrap;
    s->offset = clamp_offset(eb_goto_pos(b1, args[6], args[7]), 0, b1->total_size);
    s->b->mark = clamp_offset(eb_goto_pos(b1, args[8], args[9]), 0, b1->total_size);
    s->offset_top = clamp_offset(eb_goto_pos(b1, args[10], args[11]), 0, b1->total_size);
    if (args[12])
        qs->active_window = s;

    do_refresh(s);
}

void qe_save_window_layout(EditState *s, EditBuffer *b)
{
    QEmacsState *qs = s->qs;
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
#endif  /* !CONFIG_TINY */

#ifdef CONFIG_SESSION
int qe_load_session(EditState *s)
{
    return parse_config_file(s, ".qesession");
}

void do_save_session(EditState *s, int popup)
{
    EditBuffer *b;
    time_t now;

    b = qe_new_buffer(s->qs, "*session*", BC_REUSE | BC_CLEAR | BF_UTF8);
    if (!b)
        return;

    eb_printf(b, "// qemacs version: %s\n", QE_VERSION);
    now = time(NULL);
    eb_printf(b, "// session saved: %s\n", ctime(&now));

    qe_save_variables(s, b);
    qe_save_macros(s, b);
    qe_save_open_files(s, b);
    qe_save_window_layout(s, b);

    if (popup) {
        b->offset = 0;
        show_popup(s, b, "QEmacs session");
    } else {
        eb_write_buffer(b, 0, b->total_size, ".qesession");
        eb_free(&b);
    }
}
#endif /* CONFIG_SESSION */

/* help */

void do_describe_key_briefly(EditState *s, const char *keystr, int argval) {
#ifndef CONFIG_TINY
    /* This code is only called from a script.
     * The interactive implementation is handled in qe_key_process()
     */
    char buf[128];
    unsigned int keys[MAX_KEYS];
    unsigned int key_default = KEY_DEFAULT;
    int nb_keys, len;
    const char *p = keystr;
    KeyDef *kd;
    QEmacsState *qs = s->qs;

    nb_keys = strtokeys(p, keys, MAX_KEYS, &p);
    if (!nb_keys || *p) {
        put_error(s, "%s is not a valid key sequence", keystr);
        return;
    }
    kd = qe_find_current_binding(qs, keys, nb_keys, s->mode, 0);
    if (!kd && nb_keys == 1 && !KEY_IS_SPECIAL(keys[0]) && !KEY_IS_CONTROL(keys[0])) {
        kd = qe_find_current_binding(qs, &key_default, 1, s->mode, 1);
    }
    if (kd) {
        if (kd->nb_keys == nb_keys) {
            len = snprintf(buf, sizeof buf, "%s runs the command %s", keystr, kd->cmd->name);
        } else {
            len = snprintf(buf, sizeof buf, "%s is a prefix", keystr);
        }
    } else {
        len = snprintf(buf, sizeof buf, "%s is not bound to a command", keystr);
    }
    if (argval != NO_ARG) {
        if (!check_read_only(s))
            eb_insert_utf8_buf(s->b, s->offset, buf, len);
    } else {
        put_status(s, "%s", buf);
    }
#endif
}

EditBuffer *new_help_buffer(EditState *s)
{
    return qe_new_buffer(s->qs, "*Help*", BC_REUSE | BC_CLEAR | BF_SYSTEM | BF_UTF8 | BF_STYLE1);
}

void do_help_for_help(EditState *s)
{
    EditBuffer *b;

    b = new_help_buffer(s);
    if (!b)
        return;

    // FIXME: use minor mode for `q` to quit
    eb_puts(b,
            "QEmacs help for help - Press q to quit:\n"
            "\n"
            "C-h C-h   Show this help\n"
            "C-h b     Display table of all key bindings\n"
            "C-h c     Describe key briefly\n"
            );
    show_popup(s, b, "QEmacs help for help - Press q to quit:");
}

#ifdef CONFIG_WIN32

void qe_event_init(QEmacsState *qs)
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
void qe_event_init(QEmacsState *qs)
{
    struct sigaction sigact;
    struct itimerval itimer;

#ifdef SA_RESTART
    sigact.sa_flags = SA_RESTART;
#else
#warning SA_RESTART not defined
#endif
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
    QEmacsState *qs = s->qs;
    int w, h;

    /* XXX: currently, a small square */
    w = 5;
    h = 5;
    if (s->flags & WF_MODELINE)
        h += 1;
    *w_ptr = w * qs->mode_line_height;
    *h_ptr = h * qs->mode_line_height;
}

/* resize a window on bottom right edge */
int window_resize(EditState *s, int target_w, int target_h)
{
    QEmacsState *qs = s->qs;
    EditState *e;
    int delta_y, delta_x, min_w, min_h;

    if (s->flags & WF_MINIBUF)
        return 0;

    delta_x = target_w - (s->x2 - s->x1);
    delta_y = target_h - (s->y2 - s->y1);

    /* then see if we can resize without having too small windows */
    window_get_min_size(s, &min_w, &min_h);
    if (target_w < min_w || target_h < min_h)
        return 0;

    if (!(s->flags & WF_POPUP)) {
        /* check if moving would uncover empty regions */
        if ((s->x2 >= qs->screen->width && delta_x != 0) ||
            (s->y2 >= qs->screen->height - qs->status_height && delta_y != 0))
            return 0;

        /* check if retiling would make other windows too small */
        for (e = qs->first_window; e != NULL; e = e->next_window) {
            if ((e->flags & (WF_MINIBUF | WF_POPUP)) || e == s)
                continue;
            window_get_min_size(e, &min_w, &min_h);
            if (e->y1 == s->y2 && e->y2 - e->y1 - delta_y < min_h)
                return 0;
            else
            if (e->y2 == s->y2 && e->y2 - e->y1 + delta_y < min_h)
                return 0;

            if (e->x1 == s->x2 && e->x2 - e->x1 - delta_x < min_w)
                return 0;
            else
            if (e->x2 == s->x2 && e->x2 - e->x1 + delta_x < min_w)
                return 0;
        }

        /* now everything is OK, we can resize all windows */
        for (e = qs->first_window; e != NULL; e = e->next_window) {
            if ((e->flags & (WF_MINIBUF | WF_POPUP)) || e == s)
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
    }
    s->x2 += delta_x;
    s->y2 += delta_y;
    compute_client_area(s);
    return 1;
}

/* mouse handling */

enum {
    MOTION_NONE,
    MOTION_MODELINE,
    MOTION_RSEPARATOR,
    MOTION_CAPTION,
    MOTION_BORDER,
    MOTION_TEXT,
};

enum {
    MOTION_BORDER_LEFT   = 1 << 0,
    MOTION_BORDER_RIGHT  = 1 << 1,
    MOTION_BORDER_TOP    = 1 << 2,
    MOTION_BORDER_BOTTOM = 1 << 3,
    MOTION_BORDER_ALL    = 15 << 0,
};

/* remove temporary selection colorization and selection area */
// TODO: clean this mess
static void qe_save_selection(QEmacsState *qs, int copy)
{
    EditState *e;
    int selection_showed;

    selection_showed = 0;
    for (e = qs->first_window; e != NULL; e = e->next_window) {
        selection_showed |= e->show_selection;
        e->show_selection = 0;
    }
    if (selection_showed && qs->motion_type == MOTION_TEXT) {
        qs->motion_type = MOTION_NONE;
        e = qe_check_window(qs, &qs->motion_target);
        if (e != NULL && copy) {
            qe_trace_bytes(qs, "copy-region", -1, EB_TRACE_COMMAND);
            do_copy_region(e);
            /* activate region hilite */
            if (qs->hilite_region)
                e->region_style = QE_STYLE_REGION_HILITE;
        }
    }
}

/* XXX: need a more general scheme for other modes such as HTML/image */
void wheel_scroll_up_down(EditState *s, int dir)
{
    QEmacsState *qs = s->qs;
    int line_height;

    if (qs->trace_buffer && s->b != qs->trace_buffer) {
        qe_trace_bytes(qs, dir < 0 ? "wheel-scroll-up" : "wheel-scroll-down",
                       -1, EB_TRACE_COMMAND);
    }

    /* only apply to text modes */
    if (!s->mode->display_line)
        return;

    line_height = get_line_height(s->screen, s, QE_STYLE_DEFAULT);
    perform_scroll_up_down(s, dir * WHEEL_SCROLL_STEP * line_height);
}

static void call_mouse_goto(EditState *e, int x, int y, QEEvent *ev) {
    qe_trace_bytes(e->qs, "mouse-goto", -1, EB_TRACE_COMMAND);
    if (e->mode->mouse_goto)
        e->mode->mouse_goto(e, x, y, ev);
}

static void qe_reverse_window_list(QEmacsState *qs) {
    EditState *e = qs->first_window;
    EditState *last = NULL;
    while (e != NULL) {
        EditState *enext = e->next_window;
        e->next_window = last;
        last = e;
        e = enext;
    }
    qs->first_window = last;
}

static int check_mouse_event(EditState *e, QEEvent *ev) {
    QEmacsState *qs = e->qs;
    EditState *curw = qs->active_window;
    int mouse_x = ev->button_event.x;
    int mouse_y = ev->button_event.y;

    /* first check if mouse is inside window area */
    if (mouse_x < e->x1 || mouse_x >= e->x2 || mouse_y < e->y1 || mouse_y >= e->y2)
        return 0;

    /* test if mouse is inside the client area */
    if (mouse_x >= e->xleft && mouse_x < e->xleft + e->width &&
        mouse_y >= e->ytop && mouse_y < e->ytop + e->height)
    {
        switch (ev->button_event.button) {
        case QE_BUTTON_LEFT:
            // TODO: should cancel popup, minibuf?
            if (e->interactive) {
                /* TODO: should re-enable interactive shell behavior
                 * if mouse_goto sets the point to the end of buffer
                 */
                if (!qs->key_ctx.grab_key_cb) {
                    /* TODO: virtualize mouse events to shell process */
                    e->interactive = 0;
                }
            }
            if (curw && curw->isearch_state) {
                /* Cancel pending search */
                QEEvent ev1, *evp = qe_event_clear(&ev1);
                evp->key_event.type = QE_KEY_EVENT;
                evp->key_event.key = KEY_QUIT;
                qe_handle_event(qs, evp);
            }
            /* make popup windows modal for now */
            if (curw && e != curw && (curw->flags & WF_POPUP))
                return 0;
            if (!e->mode->mouse_goto)
                goto no_handler;
            qe_save_selection(qs, FALSE);
            e->show_selection = 0;
            e->region_style = 0;
            call_mouse_goto(e, mouse_x - e->xleft, mouse_y - e->ytop, ev);
            qs->motion_type = MOTION_TEXT;
            qs->motion_target = e;
            break;
        case QE_BUTTON_MIDDLE:
            // TODO: should cancel popup, minibuf and isearch_mode
            /* make popup windows modal for now */
            if (curw && e != curw && (curw->flags & WF_POPUP))
                return 0;
            if (!e->mode->mouse_goto) {
            no_handler:
                put_error(e, "No mouse handler for mode %s", e->mode->name);
                return 0;
            }
            qe_save_selection(qs, FALSE);
            call_mouse_goto(e, mouse_x - e->xleft, mouse_y - e->ytop, ev);
            do_yank(e);
            break;
        case QE_WHEEL_UP:
            wheel_scroll_up_down(e, -1);
            break;
        case QE_WHEEL_DOWN:
            wheel_scroll_up_down(e, 1);
            break;
        default:
            return 0;
        }
        qe_display(qs);
        return 1;
    }
    /* test for popup window caption and borders */
    if (e->flags & WF_POPUP) {
        int top_h = e->caption ? qs->mode_line_height : qs->border_width;
        int type, border;
        // TODO: check for top left close button
        if (mouse_y < e->y1 + top_h && mouse_x >= e->xleft && mouse_x < e->xleft + e->width) {
            type = MOTION_CAPTION;
            border = MOTION_BORDER_ALL;
        } else {
            type = MOTION_BORDER;
            border = 0;
            if (mouse_x < e->xleft)
                border |= MOTION_BORDER_LEFT;
            if (mouse_x >= e->xleft + e->width)
                border |= MOTION_BORDER_RIGHT;
            if (mouse_y < e->ytop)
                border |= MOTION_BORDER_TOP;
            if (mouse_y >= e->ytop + e->height)
                border |= MOTION_BORDER_BOTTOM;
        }
        if (border) {
            /* mark that motion can occur */
            qs->motion_type = type;
            qs->motion_target = e;
            qs->motion_border = border;
            qs->motion_x = mouse_x;
            qs->motion_y = mouse_y;
            return 1;
        }
    }
    if (curw && e != curw && (curw->flags & WF_POPUP))
        return 0;

    // TODO: check for top left close button and window split marks
    /* test if inside modeline */
    if ((e->flags & WF_MODELINE) &&
        mouse_x >= e->xleft && mouse_x < e->xleft + e->width &&
        mouse_y >= e->ytop + e->height &&
        mouse_y < e->ytop + e->height + qs->mode_line_height)
    {
        /* mark that motion can occur */
        qs->motion_type = MOTION_MODELINE;
        qs->motion_target = e;
        qs->motion_border = 0;
        qs->motion_x = mouse_x;
        qs->motion_y = mouse_y;
        return 1;
    }
    /* test if inside right window separator */
    if ((e->flags & WF_RSEPARATOR) &&
        mouse_x >= e->x2 - qs->separator_width && mouse_x < e->x2 &&
        mouse_y >= e->ytop && mouse_y < e->ytop + e->height)
    {
        /* mark that motion can occur */
        qs->motion_type = MOTION_RSEPARATOR;
        qs->motion_target = e;
        qs->motion_border = 0;
        qs->motion_x = mouse_x;
        qs->motion_y = mouse_y;
        return 1;
    }
    return 0;
}

static void handle_mouse_motion(EditState *e, QEEvent *ev) {
    QEmacsState *qs = e->qs;
    int mouse_x = ev->button_event.x;
    int mouse_y = ev->button_event.y;
    int scale = (qs->screen->media & CSS_MEDIA_TTY) ? 1 : 8;
    int new_y;

    switch (qs->motion_type) {
    case MOTION_NONE:
    default:
        break;
    case MOTION_TEXT:
        /* put a mark if first move */
        if (!e->show_selection) {
            // XXX: test on e->b seems unnecessary, even for list mode
            e->b->mark = e->offset;
        }
        /* if dragging mouse outside the window active area,
         * scroll the window contents in the opposite direction
         * before updating cursor position.
         */
        // TODO: drag window contents horizontally too.
        new_y = mouse_y;
        if (mouse_y < e->ytop)
            new_y = e->ytop;
        else
        if (mouse_y >= e->ytop + e->height)
            new_y = e->ytop + e->height - 1;

        if (mouse_y != new_y) {
            perform_scroll_up_down(e, mouse_y - new_y);
            mouse_y = new_y;
            // TODO: should register auto-repeat action if mouse does not move
        }
        /* highlight selection */
        e->show_selection = 1;
        if (mouse_x >= e->xleft && mouse_x < e->xleft + e->width) {
            /* if inside the buffer, then update cursor position */
            call_mouse_goto(e, mouse_x - e->xleft, mouse_y - e->ytop, ev);
            qe_display(qs);
        }
        break;
    case MOTION_CAPTION:
    case MOTION_BORDER:
        if ((mouse_x / scale) != (qs->motion_x / scale)
        ||  (mouse_y / scale) != (qs->motion_y / scale)) {
            int dx = mouse_x - qs->motion_x;
            int dy = mouse_y - qs->motion_y;
            int x1 = e->x1;
            int x2 = e->x2;
            int y1 = e->y1;
            int y2 = e->y2;
            int min_w, min_h;
            int changed = 0;
            window_get_min_size(e, &min_w, &min_h);
            /* move window borders, keep window inside screen with minimum size */
            if (qs->motion_border & MOTION_BORDER_LEFT)
                x1 += dx;
            if (qs->motion_border & MOTION_BORDER_RIGHT)
                x2 += dx;
            if (qs->motion_border & MOTION_BORDER_TOP)
                y1 += dy;
            if (qs->motion_border & MOTION_BORDER_BOTTOM)
                y2 += dy;

            if (x1 >= 0 && x2 <= qs->width && (x2 - x1) >= min_w) {
                changed |= (e->x1 - x1) | (e->x2 - x2);
                e->x1 = x1;
                e->x2 = x2;
                qs->motion_x = mouse_x;
            }
            if (y1 >= 0 && y2 <= qs->height - qs->status_height && (y2 - y1) >= min_h) {
                changed |= (e->y1 - y1) | (e->y2 - y2);
                e->y1 = y1;
                e->y2 = y2;
                qs->motion_y = mouse_y;
            }
            if (changed) {
                compute_client_area(e);
                //do_refresh(qs->first_window);
                qs->complete_refresh = 1;
                qe_display(qs);
            }
        }
        break;
    case MOTION_MODELINE:
        /* adjust window heighy and retile other windows */
        if ((mouse_y / scale) != (qs->motion_y / scale)) {
            qs->motion_y = mouse_y;
            window_resize(e, e->x2 - e->x1, qs->motion_y - e->y1);
            do_refresh(qs->first_window);
            qe_display(qs);
        }
        break;
    case MOTION_RSEPARATOR:
        /* adjust window width and retile other windows */
        if ((mouse_x / scale) != (qs->motion_x / scale)) {
            qs->motion_x = mouse_x;
            window_resize(e, qs->motion_x - e->x1, e->y2 - e->y1);
            do_refresh(qs->first_window);
            qe_display(qs);
        }
        break;
    }
}

void qe_mouse_event(QEmacsState *qs, QEEvent *ev)
{
    EditState *e;

    switch (ev->type) {
    case QE_BUTTON_RELEASE_EVENT:
        // TODO: should handle frame and buffer buttons
        // TODO: should handle double/triple click
        qe_save_selection(qs, TRUE);
        qs->motion_type = MOTION_NONE;
        qs->motion_target = NULL;
        break;

    case QE_BUTTON_PRESS_EVENT:
        // TODO: should use doubly linked list
        qe_reverse_window_list(qs);
        for (e = qs->first_window; e != NULL; e = e->next_window) {
            if (check_mouse_event(e, ev))
                break;
        }
        qe_reverse_window_list(qs);
        break;
    case QE_MOTION_EVENT:
        //qe_trace_bytes(qs, "mouse-move", -1, EB_TRACE_COMMAND);
        e = qe_check_window(qs, &qs->motion_target);
        if (e != NULL) {
            /* dispatch move drag event if mouse was captured */
            handle_mouse_motion(e, ev);
        } else {
            // TODO: dispatch move hover event */
            qs->motion_type = MOTION_NONE;
        }
        break;
    default:
        break;
    }
}
#endif

/* put key in the unget buffer so that get_key() will return it */
void qe_unget_key(QEmacsState *qs, int key)
{
    qs->ungot_key = key;
}

/* handle an event sent by the GUI */
void qe_handle_event(QEmacsState *qs, QEEvent *ev)
{
    switch (ev->type) {
    case QE_KEY_EVENT:
        if (qs->trace_buffer) {
            char buf[32];
            buf_t out[1];
            buf_init(out, buf, sizeof buf);
            buf_printf(out, "0x%04X ", ev->key_event.key);
            buf_put_key(out, ev->key_event.key);
            buf_put_byte(out, ' ');
            qe_trace_bytes(qs, buf, out->len, EB_TRACE_KEY);
        }
        qe_key_process(qs, ev->key_event.key);
        break;
    case QE_EXPOSE_EVENT:
        do_refresh(qs->first_window);
        goto redraw;
    case QE_UPDATE_EVENT:
    redraw:
        qe_display(qs);
        break;
#ifndef CONFIG_TINY
    case QE_BUTTON_PRESS_EVENT:
        qs->mouse_down_time[1] = qs->mouse_down_time[0];
        qs->mouse_down_time[0] = get_clock_ms();
        if (qs->mouse_down_time[0] - qs->mouse_down_time[1] < qs->double_click_threshold)
            qs->mouse_clicks++;
        else
            qs->mouse_clicks = 1;
        /* fallthrough */
    case QE_BUTTON_RELEASE_EVENT:
    case QE_MOTION_EVENT:
        if (qs->trace_buffer) {
            char buf[32];
            buf_t out[1];
            buf_init(out, buf, sizeof buf);
            buf_printf(out, "%d %d %d %d %d ", ev->button_event.type,
                       ev->button_event.shift, ev->button_event.button,
                       ev->button_event.x, ev->button_event.y);
            qe_trace_bytes(qs, buf, out->len, EB_TRACE_MOUSE);
        }
        qe_mouse_event(qs, ev);
        break;
    case QE_SELECTION_CLEAR_EVENT:
        qe_save_selection(qs, FALSE);
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

static int text_mode_probe(ModeDef *mode, ModeProbeData *p)
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
    s->offset = min_offset(s->offset, s->b->total_size);
    s->offset_top = min_offset(s->offset_top, s->b->total_size);
    // XXX: should track insertions at s->offset?
    eb_add_callback(s->b, eb_offset_callback, &s->offset, 0);
    eb_add_callback(s->b, eb_offset_callback, &s->offset_top, 0);
    set_colorize_mode(s, NULL);
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
    s->overwrite = 0;
    s->wrap = WRAP_AUTO;

    /* free all callbacks or associated buffer data */
    set_colorize_mode(s, NULL);
    eb_free_callback(s->b, eb_offset_callback, &s->offset);
    eb_free_callback(s->b, eb_offset_callback, &s->offset_top);

    /* Should free CRCs when switching display modes */
    qe_free(&s->line_shadow);
    s->shadow_nb_lines = 0;
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
int qe_find_resource_file(QEmacsState *qs, char *path, int path_size, const char *pattern)
{
    FindFileState *ffst;
    int ret;

    ffst = find_file_open(qs->res_path, pattern, FF_PATH);
    if (!ffst)
        return -1;
    ret = find_file_next(ffst, path, path_size);

    find_file_close(&ffst);

    return ret;
}

FILE *qe_open_resource_file(QEmacsState *qs, const char *name) {
    char filename[MAX_FILENAME_SIZE];
    if (qe_find_resource_file(qs, filename, sizeof(filename), name) >= 0)
        return fopen(filename, "r");
    else
        return NULL;
}

/******************************************************/

void do_load_config_file(EditState *e, const char *file)
{
    QEmacsState *qs = e->qs;
    FindFileState *ffst;
    char filename[MAX_FILENAME_SIZE];

    if (file && *file) {
        parse_config_file(e, file);
        do_refresh(e);
        return;
    }

    ffst = find_file_open(qs->res_path, "config", FF_PATH | FF_NODIR);
    if (!ffst)
        return;
    while (find_file_next(ffst, filename, sizeof(filename)) == 0) {
        parse_config_file(e, filename);
    }
    find_file_close(&ffst);
}

/* Load .qerc files in all parent directories of filename */
/* CG: should keep a cache of failed attempts */
void do_load_qerc(EditState *e, const char *filename)
{
    char buf[MAX_FILENAME_SIZE];
    char *p = buf;
    QEmacsState *qs = e->qs;
    /* FIXME: should run script in restricted mode:
       - protect against window and buffer deletion
       - prevent buffer changes?
       - prevent window changes?
       - only allow customisation variable updates?
     */
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
    if (qe_check_window(qs, &saved))
        qs->active_window = saved;
}

/******************************************************/
/* command line option handling */
static CmdLineOptionDef *first_cmd_options;

void qe_register_cmd_line_options(QEmacsState *qs, CmdLineOptionDef *table)
{
    CmdLineOptionDef **pp, *p;

    /* link command line options table at end of list */
    for (pp = &first_cmd_options; *pp != NULL; pp = &p->u.next) {
        p = *pp;
        if (p == table)
            return;  /* already registered */
        while (p->desc != NULL)
            p++;
    }
    *pp = table;
}

/******************************************************/

const char str_version[] = "QEmacs version " QE_VERSION;
const char str_credits[] = "Copyright (c) 2000-2003 Fabrice Bellard\n"
                           "Copyright (c) 2000-2025 Charlie Gordon\n";

static void show_version(void)
{
    printf("%s\n%s\n"
           "QEmacs comes with ABSOLUTELY NO WARRANTY.\n"
           "You may redistribute copies of QEmacs\n"
           "under the terms of the MIT license.\n",
           str_version, str_credits);
    exit(1);
}

static void show_usage(void)
{
    CmdLineOptionDef *p;

    printf("Usage: qe [OPTIONS] [filename ...]\n"
           "\n"
           "Options:\n"
           "\n");

    /* print all registered command line options */
    for (p = first_cmd_options; p != NULL; p = p->u.next) {
        while (p->desc != NULL) {
            const char *s = p->desc;
            bstr_t shortname = bstr_token(s, '|', &s);
            bstr_t name = bstr_token(s, '|', &s);
            bstr_t argname = bstr_token(s, '|', &s);
            bstr_t help = bstr_make(s);
            int pos = printf(" ");

            if (shortname.len)
                pos += printf(" -%.*s", shortname.len, shortname.s);
            if (name.len)
                pos += printf(" --%.*s", name.len, name.s);
            if (argname.len)
                pos += printf(" %.*s", argname.len, argname.s);
            if (pos < 22)
                printf("%*s", pos - 22, "");
            printf("  %.*s\n", help.len, help.s);
            p++;
        }
    }
    printf("\n"
           "Report bugs to bug@qemacs.org.  First, please see the Bugs\n"
           "section of the QEmacs manual or the file BUGS.\n");
    exit(1);
}

static int qe_parse_command_line(QEmacsState *qs, int argc, char **argv)
{
    int optind_;

    for (optind_ = 1; optind_ < argc;) {
        const char *arg, *r, *optarg_;
        CmdLineOptionDef *p;
        bstr_t opt1;
        bstr_t opt2;

        r = arg = argv[optind_];
        /* stop before first non option */
        if (r[0] != '-')
            break;
        optind_++;

        opt1.s = opt2.s = r + 1;
        if (r[1] == '-') {
            opt2.s++;
            /* stop after `--' marker */
            if (r[2] == '\0')
                break;
        }
        /* parse optional argument specified with opt=arg or opt:arg syntax */
        optarg_ = NULL;
        while (*r) {
            if (*r == ':' || *r == '=') {
                optarg_ = r + 1;
                break;
            }
            r++;
        }
        opt1.len = r - opt1.s;
        opt2.len = r - opt2.s;

        for (p = first_cmd_options; p != NULL; p = p->u.next) {
            while (p->desc != NULL) {
                const char *s = p->desc;
                bstr_t shortname = bstr_token(s, '|', &s);
                bstr_t name = bstr_token(s, '|', &s);
                bstr_t argname = bstr_token(s, '|', &s);
                if (bstr_equal(opt1, shortname) || bstr_equal(opt2, name)) {
                    if (p->need_arg && optarg_ == NULL) {
                        if (optind_ >= argc) {
                            qe_put_error(qs, "Argument %.*s expected for --%.*s",
                                         argname.len, argname.s, name.len, name.s);
                            goto next_cmd;
                        }
                        optarg_ = argv[optind_++];
                    }
                    switch (p->type) {
                    case CMD_LINE_TYPE_BOOL:
                        *p->u.int_ptr = optarg_ ? qe_strtobool(optarg_, 1) : TRUE;
                        break;
                    case CMD_LINE_TYPE_INT:
                        *p->u.int_ptr = optarg_ ? strtol(optarg_, NULL, 0) : *p->u.int_ptr + 1;
                        break;
                    case CMD_LINE_TYPE_STRING:
                        *p->u.string_ptr = optarg_;
                        break;
                    case CMD_LINE_TYPE_FVOID:
                        p->u.func_noarg();
                        break;
                    case CMD_LINE_TYPE_FARG:
                        p->u.func_arg(qs, optarg_);
                        break;
                    case CMD_LINE_TYPE_NONE:
                    case CMD_LINE_TYPE_NEXT:
                        break;
                    }
                    goto next_cmd;
                }
                p++;
            }
        }
        qe_put_error(qs, "Unknown command line option '%s'", arg);
    next_cmd: ;
    }

    return optind_;
}

void do_add_resource_path(EditState *s, const char *path)
{
    QEmacsState *qs = s->qs;
    pstrcat(qs->res_path, sizeof(qs->res_path), ":");
    pstrcat(qs->res_path, sizeof(qs->res_path), path);
}

static void qe_set_user_option(QEmacsState *qs, const char *user)
{
    char path[MAX_FILENAME_SIZE];
    const char *home_path;

    qs->user_option = user;

    /* compute resources path */
    qs->res_path[0] = '\0';

    /* put current directory first if qe invoked as ./qe */
    if (stristart(qs->argv[0], "./qe", NULL)) {
        if (!getcwd(path, sizeof(path)))
            strcpy(path, ".");
        pstrcat(qs->res_path, sizeof(qs->res_path), path);
        pstrcat(qs->res_path, sizeof(qs->res_path), ":");
        pstrcat(qs->res_path, sizeof(qs->res_path), path);
        pstrcat(qs->res_path, sizeof(qs->res_path), "/unidata:");
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

static void qe_set_tty_charset(QEmacsState *qs, const char *name)
{
    qe_free(&qs->tty_charset);
    qs->tty_charset = qe_strdup(name);
}

static CmdLineOptionDef cmd_options[] = {
    CMD_LINE_FVOID("h", "help", show_usage,
                   "display this help message and exit"),
    CMD_LINE_FVOID("?", "", show_usage, ""),
    CMD_LINE_BOOL("q", "no-init-file", &no_init_file,
                  "do not load config files"),
    CMD_LINE_BOOL("nc", "no-crc", &disable_crc,
                  "do not use crc based display cacheing"),
    CMD_LINE_BOOL("1", "single-window", &single_window,
                  "keep a single window when loading multiple files"),
    CMD_LINE_BOOL("nw", "no-windows", &force_tty,
                  "force tty terminal usage"),
    CMD_LINE_FARG("c", "charset", "CHARSET", qe_set_tty_charset,
                  "specify tty charset"),
#ifdef CONFIG_SESSION
    CMD_LINE_BOOL("s", "use-session", &use_session_file,
                  "load and save session files"),
#endif
    CMD_LINE_FARG("u", "user", "USER", qe_set_user_option,
                  "load ~USER/.qe/config instead of your own"),
    CMD_LINE_FVOID("V", "version", show_version,
                   "display version information and exit"),
#ifndef CONFIG_TINY
    CMD_LINE_BOOL("", "free-all", &free_everything,
                  "free all structures upon exit"),
#endif
    CMD_LINE_INT("mk", "modify-other-keys", "VAL", &tty_mk,
                 "set the modifyOtherKeys tty configuration (0,1,2)"),
    CMD_LINE_INT("", "clipboard", "VAL", &tty_clipboard,
                 "set the tty clipboard support method (0,1,2)"),
    CMD_LINE_INT("m", "mouse", "VAL", &tty_mouse,
                 "set the mouse emulation mode (0,1,2)"),
    CMD_LINE_LINK()
};

/* default key bindings */

static const CmdDef basic_commands[] = {

    /*---------------- Simple commands ----------------*/

    /* Character insertion */

    CMD2( "self-insert-command", "default",
          "Insert the character you type",
          do_char, ESii, "*" "k" "p")
    CMD2( "insert-char", "M-#, C-x 8 RET",
          "Insert the character with a specific code or Unicode name",
          do_char, ESii, "*"
          "n{Insert char: }[charname]|charvalue|"
          "p")
    /* do_tabulate will not change read only buffer */
    CMD2( "tabulate", "TAB",
          "Insert a TAB or spaces according to indent-tabs-mode",
          do_tabulate, ESi, "p")
    CMD2( "untabulate", "S-TAB",
          "Decrease indentation by one tab width",
          do_untabulate, ES, "*")
    //CMD2( "space", "SPC", "Insert a space", do_space, ESi, "*" "P")
    CMD2( "quoted-insert", "C-q",
          "Read next input character and insert it",
          do_quoted_insert, ESi, "*" "p")
    CMD2( "newline", "RET, LF",
          "Insert a newline, and move to left margin of the new line",
          do_newline, ES, "*")
    CMD2( "open-line", "C-o",
          "Insert a newline and leave point before it",
          do_open_line, ES, "*")

    CMD2( "overwrite-mode", "insert, C-c o",
          "Toggle between overwrite mode and insert mode",
          do_overwrite_mode, ESi, "P")
    CMD3( "insert-mode", "C-c i",
          "Select insert mode",
          do_overwrite_mode, ESi, "v", 0)

#ifdef CONFIG_UNICODE_JOIN
    /* Insert combining accent: combine with letter if possible */
    CMD3( "combine-grave-accent", "M-`",
          "Combine the previous letter with a grave accent",
          do_combine_accent, ESi, "*" "v", 0x300)
    CMD3( "combine-acute-accent", "M-'",
          "Combine the previous letter with an acute accent",
          do_combine_accent, ESi, "*" "v", 0x301)
    CMD3( "combine-circumflex-accent", "M-^",
          "Combine the previous letter with a circumflex accent",
          do_combine_accent, ESi, "*" "v", 0x302)
    CMD3( "combine-diaeresis", "M-\"",
          "Combine the previous letter with a diaeresis (aka trema or umlaut)",
          do_combine_accent, ESi, "*" "v", 0x308)
    CMD3( "combine-tilde", "M-~",  // binding conflicts with not-modified
          "Combine the previous letter with a tilde",
          do_combine_accent, ESi, "*" "v", 0x303)
#endif

    /* Moving around */

    CMD2( "previous-line", "C-p, up, S-up",
          "Move to previous line",
          do_up_down, ESi, "q")
    CMD2( "next-line", "C-n, down, S-down",
          "Move to next line",
          do_up_down, ESi, "p")
    CMD2( "backward-char", "C-b, left, S-left",
          "Move to the previous character",
          do_left_right, ESi, "q")
    CMD2( "forward-char", "C-f, right, S-right",
          "Move to the next character",
          do_left_right, ESi, "p")
    CMD2( "backward-word", "M-b, C-left, M-left, C-S-left, M-S-left",
          "Move to the beginning of the word on or before point",
          do_word_left_right, ESi, "q")
    CMD2( "forward-word", "M-f, C-right, M-right, C-S-right, M-S-right",
          "Move to the end of the word on or after point",
          do_word_left_right, ESi, "p")
    CMD1( "scroll-down", "M-v, pageup, S-pageup",
          "Display the previous page",
          do_scroll_up_down, -2) /* u? */
    CMD1( "scroll-up", "C-v, pagedown, S-pagedown",
          "Display the next page",
          do_scroll_up_down, 2) /* u? */
    CMD1( "scroll-down-one", "M-z",
          "Move the window contents down one line",
          do_scroll_up_down, -1) /* u? */
    CMD1( "scroll-up-one", "C-z",
          "Move the window contents up one line",
          do_scroll_up_down, 1) /* u? */
    CMD0( "beginning-of-line", "C-a, home, S-home",
          "Move point to the beginning of the line",
          do_bol)
    CMD0( "end-of-line", "C-e, end, S-end",
          "Move point to the end of the line",
          do_eol)
    CMD0( "beginning-of-buffer", "M-<, C-home, C-S-home",
          "Move point to the beginning of the buffer",
          do_bof)
    CMD0( "end-of-buffer", "M->, C-end, C-S-end",
          "Move point to the end of the buffer",
          do_eof)

    /*---------------- Region handling / Kill commands ----------------*/

    /* deletion commands should be allowed in read only buffers,
     * they should merely copy the data to the kill ring */
    CMD2( "delete-char", "C-d, delete",
          "Delete the character at point",
          do_delete_char, ESi, "*" "P")
    CMD2( "backward-delete-char", "DEL",
          "Delete the character before point",
          do_backspace, ESi, "*" "P")
    CMD0( "set-mark-command", "C-@",
          "Set the buffer mark",
          do_set_mark)
    CMD0( "exchange-point-and-mark", "C-x C-x",
          "Exchange point and the buffer mark",
          do_exchange_point_and_mark)
    CMD0( "mark-whole-buffer", "C-x h",
          "Set the mark at the beginning and point at the end of the buffer",
          do_mark_whole_buffer)
    CMD0( "append-next-kill", "M-C-w",
          "Append the next kill to the current entry in the kill ring",
          do_append_next_kill)
    CMD2( "kill-line", "C-k",
          "Kill to the end of line",
          do_kill_line, ESi, "P")
    CMD2( "kill-whole-line", "M-k", // should be C-S-Backspace
          "Kill the line at point",
          do_kill_whole_line, ESi, "p")
    CMD2( "kill-beginning-of-line", "",
          "Kill to the beginning of the line",
          do_kill_beginning_of_line, ESi, "P")
    CMD2( "backward-kill-word", "M-DEL, M-C-h",
          "Kill to the beginning of the word at or before point",
          do_kill_word, ESi, "q")
    CMD2( "kill-word", "M-d",
          "Kill to the end of the word at or after point",
          do_kill_word, ESi, "p")
    /* XXX: should take region as argument, implicit from keyboard */
    CMD0( "kill-region", "C-w",
          "Kill the current region",
          do_kill_region)
    CMD0( "copy-region", "M-w",
          "Copy the current region to the kill ring",
          do_copy_region)
    CMD2( "yank", "C-y",
          "Insert the contents of the current entry in the kill ring",
          do_yank, ES, "*")
    CMD2( "yank-pop", "M-y",
          "Replace the last yanked data with the contents of the previous entry in the kill ring",
          do_yank_pop, ES, "*")

    /*---------------- Buffer and file handling ----------------*/

    CMD3( "find-file", "C-x C-f",
          "Load a file into a new buffer and/or display it in the current window",
          do_find_file, ESsi,
          "s{Find file: }[file]|file|"
          "v", 0) /* u? */
    CMD3( "find-file-other-window", "C-x M-f",
          "Load a file into a new buffer and/or display it in a new window",
          do_find_file_other_window, ESsi,
          "s{Find file: }[file]|file|"
          "v", 0) /* u? */
    CMD3( "find-alternate-file", "C-x C-v",
          "Load a new file into the current buffer and/or display it in the current window",
          do_find_alternate_file, ESsi,
          "s{Find alternate file: }[file]|file|"
          "v", 0) /* u? */
    CMD3( "find-file-noselect", "",
          "Load a file into a new buffer",
          do_find_file_noselect, ESsi,
          "s{Find file: }[file]|file|"
          "v", 0) /* u? */
    CMD2( "insert-file", "C-x i",
          "Insert the contents of a file at point",
          do_insert_file, ESs, "*"
          "s{Insert file: }[file]|file|") /* u? */
    CMD0( "save-buffer", "C-x C-s",
          "Save the buffer contents to the associated file if modified",
          do_save_buffer) /* u? */
    CMD2( "write-file", "C-x C-w",
          "Write the buffer contents to a specified file and associate it to the buffer",
          do_write_file, ESs,
          "s{Write file: }[file]|file|") /* u? */
    /* XXX: should take region as argument, implicit from keyboard */
    CMD2( "write-region", "C-x w",
          "Write the contents of the current region to a specified file",
          do_write_region, ESs,
          "s{Write region to file: }[file]|file|") /* u? */
    CMD2( "switch-to-buffer", "C-x b",
          "Change the buffer attached to the current window",
          do_switch_to_buffer, ESs,
          "s{Switch to buffer: }[buffer]|buffer|")
    CMD3( "kill-buffer", "C-x k",
          "Remove a named buffer",
          do_kill_buffer, ESsi,
          "s{Kill buffer: }[buffer]|buffer|"
          "v", 0)
    CMD3( "next-buffer", "C-x C-right",
          "Switch to the next buffer",
          do_buffer_navigation, ESii, "p" "v", 1)
    CMD3( "previous-buffer", "C-x C-left",
          "Switch to the previous buffer",
          do_buffer_navigation, ESii, "p" "v", -1)
    CMD0( "toggle-read-only", "C-x C-q, C-c %",
          "Toggle the read-only flag of the current buffer",
          do_toggle_read_only)
    CMD2( "not-modified", "M-~, C-c ~",
          "Toggle the modified flag of the current buffer",
          do_not_modified, ESi, "P")
    CMD2( "set-visited-file-name", "",
          "Change the name of file visited in current buffer",
          do_set_visited_file_name, ESss,
          "s{Set visited file name: }[file]|file|"
          "s{Rename file? }|newname|")

    /*---------------- Case handling ----------------*/

    CMD3( "capitalize-word", "M-c",
          "Upcase the first letter and downcase the rest of the word",
          do_changecase_word, ESi, "*" "v", 2)
    CMD3( "downcase-word", "M-l",
          "Downcase the rest of the word",
          do_changecase_word, ESi, "*" "v", -1)
    CMD3( "upcase-word", "M-u",
          "Upcase the rest of the word",
          do_changecase_word, ESi, "*" "v", 1)
    /* XXX: should take region as argument, implicit from keyboard */
    CMD3( "capitalize-region", "M-C-c",
          "Apply capital case to all words in the region",
          do_changecase_region, ESi, "*" "v", 2)
    CMD3( "downcase-region", "C-x C-l",
          "Downcase all words in the region",
          do_changecase_region, ESi, "*" "v", -1)
    CMD3( "upcase-region", "C-x C-u",
          "Upcase all words in the region",
          do_changecase_region, ESi, "*" "v", 1)

    /*---------------- Command handling ----------------*/

    CMD2( "execute-command", "M-x, f2",
          "Run a named command",
          do_execute_command, ESsi,
          "s{Command: }[command]|command|"
          "P")
    CMD2( "universal-argument",
          "C-u",
          "Set or multiply the numeric argument by 4",
          do_prefix_argument, ESi, "k")
    CMD2( "negative-argument",
          "M--",
          "Negate or set the numeric argument to -1",
          do_prefix_argument, ESi, "k")
    CMD2( "digit-argument",
          "M-0, M-1, M-2, M-3, M-4, M-5, M-6, M-7, M-8, M-9",
          "Set the numeric prefix argument",
          do_prefix_argument, ESi, "k")
    CMD0( "keyboard-quit",
          "C-g, C-x C-g, C-c C-g, C-h C-g, M-C-g, ESC ESC ESC, QUIT",
          "Abort the current command",
          do_keyboard_quit)
    CMD0( "unknown-key",
          "none",
          "An unknown key was pressed",
          do_unknown_key)

    CMD0( "start-kbd-macro", "C-x (",
          "Start recording a keyboard macro",
          do_start_kbd_macro)
    CMD0( "end-kbd-macro", "C-x )",
          "End recording a keyboard macro",
          do_end_kbd_macro)
    CMD2( "call-last-kbd-macro", "C-x e, C-\\",
          "Run the last recorded keyboard macro",
          do_call_last_kbd_macro, ESi, "p")
    CMD2( "define-kbd-macro", "",
          "Define a named keyboard macro",
          do_define_kbd_macro, ESsss,
          "s{Macro name: }[command]"
          "s{Macro keys: }|macrokeys|"
          "s{Bind to key: }[key]")
#ifndef CONFIG_TINY
    CMD2( "edit-last-kbd-macro", "C-x *, C-x C-k C-e, C-x C-k e",
          "Edit the last keyboard macro",
          do_edit_last_kbd_macro, ESs,
          "s{Macro keys: }|macrokeys|")
    CMD2( "name-last-kbd-macro", "C-x C-k C-n, C-x C-k n",
          "Define a named command from the last keyboard macro",
          do_name_last_kbd_macro, ESs,
          "s{Macro name: }[command]")
    CMD2( "insert-kbd-macro", "C-x C-k i",
          "Insert in buffer the definition of kbd macro MACRONAME, as qescript code",
          do_insert_kbd_macro, ESs,
          "*s{Macro name: }[command]")
    CMD2( "read-kbd-macro", "C-x C-k r",
          "Read the region as a keyboard macro definition",
          do_read_kbd_macro, ESii, "m" "d")
    CMD2( "macro-add-counter", "C-x C-k C-a, C-x C-k a",
          "Add the value of numeric prefix arg (prompt if missing) to `macro-counter`",
          do_macro_add_counter, ESi,
          "N{Macro increment: }")
    CMD2( "macro-set-counter", "C-x C-k C-c, C-x C-k c",
          "Set the value of `macro-counter' to ARG, or prompt for value if no argument",
          do_macro_set_counter, ESi,
          "N{Macro counter: }")
    CMD2( "macro-insert-counter", "C-x C-k TAB, C-x C-k =",
          "Insert current value of `macro-counter`, then increment it by ARG",
          do_macro_insert_counter, ESi, "*p")
    CMD2( "macro-set-format", "C-x C-k C-f, C-x C-k f",
          "Set the printf-like format for `macro-insert-counter`",
          do_macro_set_format, ESs,
          "s{Format: }|macroformat|")

    CMD0( "start-multi-cursor", "C-x <",
          "Start recording a multi-line cursor",
          do_start_multi_cursor)
    CMD0( "end-multi-cursor", "C-x >",
          "End recording a multi-line cursor",
          do_end_multi_cursor)
    CMD0( "add-multi-cursor", "C-x #",
          "Add point to the current multi-line cursor",
          do_add_multi_cursor)
    CMD0( "activate-multi-cursor", "C-x \\",
          "Activate multi-line cursor editing (stop with C-g)",
          do_activate_multi_cursor)
#endif
    /* set/unset key? */
    CMD3( "global-set-key", "f4",
          "Register a global key binding",
          do_set_key, ESssi,
          "s{Set key globally: }[key]"
          "s{command: }[command]|command|"
          "v", 0)
    CMD3( "local-set-key", "",
          "Register a key binding a given mode",
          do_set_key, ESssi,
          "s{Set key locally: }[key]"
          "s{command: }[command]|command|"
          "v", 1)

    /*---------------- Window handling ----------------*/

    /* should merge these functions */
    CMD0( "other-window", "C-x o",
          "Move the focus to another window",
          do_other_window)
    CMD0( "next-window", "C-x n",
          "Move the focus to the next window",
          do_other_window)
    CMD0( "previous-window", "C-x p",
          "Move the focus to the previous window",
          do_previous_window)
    CMD0( "window-swap-states", "C-x /",
          "Swap the states of the current and next windows",
          do_window_swap_states)
#ifndef CONFIG_TINY
    CMD1( "center-cursor", "M-C-l",
          "Center the window contents at point",
          do_center_cursor, 1)
    CMD1( "find-window-up", "C-x up",
          "Move the focus to the window above the current one",
          do_find_window, KEY_UP)
    CMD1( "find-window-down", "C-x down",
          "Move the focus to the window below the current one",
          do_find_window, KEY_DOWN)
    CMD1( "find-window-left", "C-x left",
          "Move the focus to the window to the left of the current one",
          do_find_window, KEY_LEFT)
    CMD1( "find-window-right", "C-x right",
          "Move the focus to the window to the right of the current one",
          do_find_window, KEY_RIGHT)
    CMD2( "scroll-left", "M-(",
          "Shift the window contents to the left",
          do_scroll_left_right, ESi, "q")
    CMD2( "scroll-right", "M-)",
          "Shift the window contents to the right",
          do_scroll_left_right, ESi, "p")
    CMD1( "preview-mode", "",
          "Enter preview mode: cursor movement keys cause window scrolling",
          do_preview_mode, 1)
#endif
    CMD1( "delete-window", "C-x 0, CLOSE",
          "Delete the current window",
          do_delete_window, 0)
    CMD1( "delete-other-windows", "C-x 1",
          "Delete all other windows",
          do_delete_other_windows, 0)
    CMD1( "delete-all-windows", "",
          "Delete all windows",
          do_delete_other_windows, 1)
    CMD1( "hide-window", "",
          "Hide the current window",
          do_hide_window, 1)
    CMD0( "delete-hidden-windows", "",
          "Delete the hidden windows",
          do_delete_hidden_windows)
    CMD3( "split-window-vertically", "C-x 2",
          "Split the current window top and bottom",
          do_split_window, ESii, "P" "v", SW_STACKED)
    CMD3( "split-window-horizontally", "C-x 3",
          "Split the current window side by side",
          do_split_window, ESii, "P" "v", SW_SIDE_BY_SIDE)
    CMD0( "toggle-full-screen", "C-c f",
          "Toggle full screen display (on graphics displays)",
          do_toggle_full_screen)
    CMD0( "toggle-mode-line", "C-c m",
          "Toggle mode-line display",
          do_toggle_mode_line)
#ifdef CONFIG_SESSION
    CMD2( "create-window", "",
          "Create a new window with a specified layout",
          do_create_window, ESss,
          "s{Filename: }[file]|file|"
          "s{Layout: }|layout|")
    CMD1( "save-session", "",
          "Save the current session in a .qesession file",
          do_save_session, 1)
#endif

    /*---------------- Help ----------------*/

    CMD2( "toggle-trace-mode", "C-h d",
          "Enable or disable trace mode: show the *Trace* buffer with debugging info",
          do_toggle_trace_mode, ESi, "P")
    CMD2( "set-trace-options", "C-h t",
         "Select the trace options: all, none, command, debug, emulate, shell, tty, pty",
          do_set_trace_options, ESs,
          "s{Trace options: }|trace|")
    CMD2( "describe-key-briefly", "C-h c, C-h k, f6",
          "Describe a key binding",
          do_describe_key_briefly, ESsi,
          "s{Describe key: }|keys|"
          "P")
    CMD0( "help-for-help", "C-h C-h, f1",
          "Show the qemacs help window",
          do_help_for_help)

    /*---------------- International ----------------*/

    CMD2( "set-buffer-file-coding-system", "C-x RET f, C-c c",
          "Set the buffer charset and encoding system",
          do_set_buffer_file_coding_system, ESs,
          "s{Charset: }[charset]|charset|")
    CMD2( "convert-buffer-file-coding-system", "",
          "Convert the buffer contents to a new charset and encoding system",
          do_convert_buffer_file_coding_system, ESs, "*"
          "s{Charset: }[charset]|charset|")
    CMD0( "toggle-bidir", "C-x RET b, C-c b",
          "",
          do_toggle_bidir)
    CMD2( "set-input-method", "C-x RET C-\\, C-c C-\\",
          "",
          do_set_input_method, ESs,
          "s{Input method: }[input]")
    CMD0( "switch-input-method", "C-x C-\\",
          "",
          do_switch_input_method)

    /*---------------- Styles & display ----------------*/

    CMD2( "define-color", "",
          "Define a named color",
          do_define_color, ESss,
          "s{Color name: }[color]|color|"
          "s{Color value: }[color]|color|")
    CMD2( "set-style", "",
          "Set a property for a named style",
          do_set_style, ESsss,
          "s{Style: }[style]|style|"
          "s{CSS Property Name: }[style-property]|style-property|"
          "s{CSS Property Value: }|value|")
    CMD2( "set-display-size", "",
          "Set the dimensions of the graphics screen",
          do_set_display_size, ESii,
          "n{Width: }|width|"
          "n{Height: }|height|")
    CMD2( "set-system-font", "",
          "Set the system font",
          do_set_system_font, ESss,
          "s{Font family: }|fontfamily|"
          "s{System fonts: }|fontnames|")
    CMD2( "set-window-style", "",
          "",
          do_set_window_style, ESs,
          "s{Style: }[style]|style|")

    /*---------------- Miscellaneous ----------------*/

    CMD2( "suspend-qemacs", "", //"C-z",
          "Suspend Quick Emacs",
          do_suspend_qemacs, ESi, "P")
    CMD2( "exit-qemacs", "C-x C-c, EXIT",
          "Exit Quick Emacs",
          do_exit_qemacs, ESi, "P")
    CMD0( "refresh", "C-l",
          "Refresh the display, center the window contents at point",
          do_refresh_complete)
    CMD2( "repeat", "C-x z",
          "Repeat last command with same prefix argument",
          do_repeat, ESi, "p")
    CMD0( "undo", "C-x u, C-_, C-/, f9",
          "Undo the last change",
          do_undo)
    CMD0( "redo", "C-x r, C-x C-_, C-x C-/, f10",
          "Redo the last change undone",
          do_redo)

    CMD3( "goto-line", "M-g g, M-g M-g, C-x g",
          "Go to a line number",
          do_goto, ESsi,
          "s{Goto line: }"
          "v", 'l')
    CMD3( "goto-char", "M-g c",
          "Go to a character number",
          do_goto, ESsi,
          "s{Goto char: }"
          "v", 'c')
    CMD0( "count-lines", "C-x l",
          "Count the lines in the current buffer and region",
          do_count_lines)
    CMD0( "what-cursor-position", "C-x =",
          "Show the current point and mark positions",
          do_what_cursor_position)

    /* non standard mappings */
    CMD2( "line-number-mode", "",
          "Control the display of line numbers in mode lines",
          do_line_number_mode, ESi, "P")
    CMD2( "column-number-mode", "",
          "Control the display of column numbers in mode lines",
          do_column_number_mode, ESi, "P")
    CMD2( "global-linum-mode", "",
          "Control the display of line numbers in the left gutter for all buffers",
          do_global_linum_mode, ESi, "P")
    CMD2( "linum-mode", "C-x RET l, C-c l",
          "Control the display of line numbers in the left gutter for the current buffer",
          do_linum_mode, ESi, "P")
    CMD2( "toggle-line-numbers", "",    /* for compatibility with previous versions */
          "Toggle the line number display",
          do_linum_mode, ESi, "P")
    CMD0( "toggle-truncate-lines", "C-x RET t, C-c t",
          "Toggle displaying long lines on multiple screen rows",
          do_toggle_truncate_lines)
    CMD0( "word-wrap", "C-x RET w, C-c w",
          "Toggle wrapping on a character or word basis",
          do_word_wrap)
    CMD1( "toggle-control-h", "",
          "Toggle backspace / DEL handling",
          do_toggle_control_h, 0)
    CMD0( "qemacs-version", "",
          "Show the current version of qemacs",
          do_qemacs_version)

    CMD2( "set-emulation", "",
          "Select emacs flavor emulation",
          do_set_emulation, ESs,
          "s{Emulation mode: }|emulation|")
    CMD2( "cd", "f7",
          "Change the current directory of the qemacs process",
          do_cd, ESs,
          "s{Change default directory: }[dir]|file|")
    CMD2( "set-mode", "",
          "Set an editing mode",
          do_set_mode, ESs,
          "s{Set mode: }[mode]")
    CMD1( "set-auto-coding", "",
          "",
          do_set_auto_coding, 1)
    CMD1( "set-auto-mode", "",
          "Select the best mode",
          do_set_next_mode, 0)
    CMD2( "set-next-mode", "M-m",
          "Select the next mode appropriate for the current buffer",
          do_set_next_mode, ESi, "p")
    CMD2( "set-previous-mode", "",
          "Select the previous mode appropriate for the current buffer",
          do_set_next_mode, ESi, "q")

    /* tab & indent */
    CMD2( "set-tab-width", "",
          "Set the TAB width for the current buffer",
          do_set_tab_width, ESi,
          "N{Tab width: }")
    CMD2( "set-indent-width", "",
          "Set the indentation width for the current window",
          do_set_indent_width, ESi,
          "N{Indent width: }")
    CMD2( "set-indent-tabs-mode", "",
          "Select whether to use TABs or spaces for indentation",
          do_set_indent_tabs_mode, ESi,
          "N{Indent tabs mode (0 or 1): }")
    CMD2( "set-fill-column", "",
          "Set the width for paragraph filling",
          do_set_fill_column, ESi,
          "N{Fill column: }")

    /* other stuff */
    CMD3( "load-file-from-path", "C-c C-f",
          "Load a resource file from the QEPATH",
          do_load_file_from_path, ESsi,
          "s{Load file from path: }[resource]|file|"
          "v", 0)
    CMD2( "load-config-file", "",
          "Load a configuration file from the QEPATH",
          do_load_config_file, ESs,
          "s{Configuration file: }[resource]|file|")
    CMD2( "load-qerc", "",
          "Load a local .qerc settings file",
          do_load_qerc, ESs,
          "s{path: }[file]|file|")
    CMD2( "add-resource-path", "",
          "Add a path to the resource path list",
          do_add_resource_path, ESs,
          "s{resource path: }[dir]|file|")
};

QEStyleDef qe_styles[QE_STYLE_NB] = {

#define STYLE_DEF(constant, name, fg_color, bg_color, \
                  font_style, font_size) \
{ name, fg_color, bg_color, font_style, font_size },

#include "qestyles.h"

#undef STYLE_DEF
};

#ifdef CONFIG_DLL

static void qe_load_all_modules(QEmacsState *qs)
{
    QErrorContext ec;
    FindFileState *ffst;
    char filename[MAX_FILENAME_SIZE];
    void *h;
    void *sym;
    int (*init_func)(QEmacsState *);

    ec = qs->ec;
    qs->ec.function = "load-all-modules";

    ffst = find_file_open(qs->res_path, "*.so", FF_PATH | FF_NODIR);
    if (!ffst)
        goto done;

    while (!find_file_next(ffst, filename, sizeof(filename))) {
        h = dlopen(filename, RTLD_LAZY);
        if (!h) {
            char *error = dlerror();
            qe_put_error(qs, "Could not open module '%s': %s", filename, error);
            continue;
        }
#if 0
        /* Writing: init_func = (int (*)(void))dlsym(handle, "xxx");
         * would seem more natural, but the C99 standard leaves
         * casting from "void *" to a function pointer undefined.
         * The assignment used below is the POSIX.1-2003 (Technical
         * Corrigendum 1) workaround; see the Rationale for the
         * POSIX specification of dlsym().
         * XXX: this violates the strict aliasing rule. This syntax
         * is known to cause incorrect code generation in gcc 12.2
         * with optimizations enabled in other contexts.
         */
        *(void **)&init_func = dlsym(h, "__qe_module_init");
        //init_func = (int (*)(void))dlsym(h, "__qe_module_init");
#else
        /* This kludge gets rid of compile and lint warnings.
           The implicit assumption is that code and function pointers
           have the same size and representation, which is a requirement
           ofor POSIX targets */
        sym = dlsym(h, "__qe_module_init");
        memcpy(&init_func, &sym, sizeof(sym));
#endif
        if (!init_func) {
            dlclose(h);
            qe_put_error(qs, "Could not find qemacs initializer in module '%s'", filename);
            continue;
        }

        /* all is OK: we can init the module now */
        (*init_func)(qs);
    }
    find_file_close(&ffst);

  done:
    qs->ec = ec;
}

#endif

static CompletionDef charset_completion = {
    .name = "charset",
    .enumerate = charset_complete,
};

/* init function */
static int qe_init(void *opaque)
{
    QEArgs *args = opaque;
    QEmacsState *qs = args->qs;
    int argc = args->argc;
    char **argv = args->argv;
    EditState *s;
    EditBuffer *b;
    QEDisplay *dpy;
    int i, _optind;
#if !defined(CONFIG_TINY)
    int session_loaded = 0;
#endif
#if defined(CONFIG_ALL_KMAPS) || defined(CONFIG_UNICODE_JOIN)
    char filename[MAX_FILENAME_SIZE];
#endif

    qs->ec.function = "qe-init";
    qs->macro_key_index = -1; /* no macro executing */
    qs->ungot_key = -1; /* no unget key */

    qs->argc = argc;
    qs->argv = argv;

    qs->hilite_region = 1;
    qs->line_number_mode = 1;
    qs->column_number_mode = 1;

    qs->default_tab_width = DEFAULT_TAB_WIDTH;
    qs->default_fill_column = DEFAULT_FILL_COLUMN;
    qs->mmap_threshold = MIN_MMAP_SIZE;
    qs->max_load_size = MAX_LOAD_SIZE;
    qs->input_buf = qs->input_buf_def;
    qs->input_size = countof(qs->input_buf_def);
    qs->double_click_threshold = DEFAULT_DOUBLE_CLICK_THRESHOLD;
    qs->screen = &global_screen;

    // FIXME: Achtung Minen! qs->active_window is NULL until
    //        the first window is created for the "*scratch*" buffer

    /* setup resource path */
    qe_set_user_option(qs, NULL);

    qe_data_init(qs);
    charset_init(qs);
    qe_input_methods_init(qs);
    colors_init();

#ifdef CONFIG_ALL_KMAPS
    if (qe_find_resource_file(qs, filename, sizeof(filename), "kmaps") >= 0)
        qe_load_input_methods(qs, filename);
#endif
#ifdef CONFIG_UNICODE_JOIN
    if (qe_find_resource_file(qs, filename, sizeof(filename), "ligatures") >= 0)
        load_ligatures(filename);
#endif

    /* init basic modules */
    qe_register_mode(qs, &text_mode, MODEF_VIEW);
    qe_register_commands(qs, NULL, basic_commands, countof(basic_commands));
    qe_register_cmd_line_options(qs, cmd_options);

    qe_register_completion(qs, &buffer_completion);
    qe_register_completion(qs, &charset_completion);
    qe_register_completion(qs, &color_completion);
    qe_register_completion(qs, &command_completion);
    qe_register_completion(qs, &file_completion);
    qe_register_completion(qs, &mode_completion);
    qe_register_completion(qs, &style_completion);
    qe_register_completion(qs, &style_property_completion);
#ifndef CONFIG_TINY
    qe_register_completion(qs, &dir_completion);
    qe_register_completion(qs, &resource_completion);
#endif

    qe_minibuffer_init(qs);
    list_init(qs);
    popup_init(qs);

    /* init all external modules in link order */
    qe_init_all_modules(qs);

#ifdef CONFIG_DLL
    /* load all dynamic modules */
    qe_load_all_modules(qs);
#endif

    /* create first buffer */
    b = qe_new_buffer(qs, "*scratch*", BF_SAVELOG | BF_UTF8);
    if (!b)
        return 1;

    /* will be positionned by do_refresh() */
    s = qe_new_window(b, 0, 0, 0, 0, WF_MODELINE);
    if (!s)
        return 2;

    /* at this stage, no screen is defined. Initialize a
     * null display driver to have a consistent state
     * else many commands such as put_status would crash.
     */
    qe_screen_init(qs, qs->screen, NULL, screen_width, screen_height);

    /* handle options */
    _optind = qe_parse_command_line(qs, argc, argv);

    /* load config file unless command line option given */
    if (!no_init_file) {
        do_load_config_file(s, NULL);
        s = qs->active_window;
    }

    qe_key_init(&qs->key_ctx);

    /* select the suitable display manager */
    for (;;) {
        dpy = probe_display();
        if (!dpy) {
            fprintf(stderr, "No suitable display found, exiting\n");
            exit(1);
        }
        if (qe_screen_init(qs, qs->screen, dpy, screen_width, screen_height) < 0) {
            /* Just disable the display and try another */
            //fprintf(stderr, "Could not initialize display '%s', exiting\n",
            //        dpy->name);
            dpy->dpy_probe = NULL;
        } else {
            break;
        }
    }

    put_status(s, "%s display %dx%d",
               dpy->name, qs->screen->width, qs->screen->height);

    qe_event_init(qs);

#ifdef CONFIG_SESSION
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
                do_eval_expression(s, argv[i++], NO_ARG);
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
            if (*p == ',' || *p == ':') {
                col_num = strtol(p + 1, NULL, 10);
                col_num -= (col_num > 0);  // user column numbers are 1-based
            }
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

#if !defined(CONFIG_TINY)
#if defined(CONFIG_FFMPEG)
    /* Force is_player if invoked as ffplay */
    if (strequal(get_basename(argv[0]), "ffplay"))
        is_player = 1;
#endif
    if (is_player && !session_loaded && (_optind >= argc || S_ISDIR(s->b->st_mode))) {
        /* if player, go to directory mode by default if no file selected */
        do_dired(s, NO_ARG);
        s = qs->active_window;
    }
#endif
#ifdef CONFIG_TINY
    put_status(s, "Tiny QEmacs %s - Press F1 for help", QE_VERSION);
#else
    put_status(s, "QEmacs %s - Press F1 for help", QE_VERSION);
    b = qe_find_buffer_name(qs, "*errors*");
    if (b != NULL) {
        show_popup(s, b, "Errors");
    }
#endif
    qe_display(qs);
    qs->ec.function = NULL;
    return 0;
}

#ifdef CONFIG_WIN32
int main1(int argc, char **argv)
#else
int main(int argc, char **argv)
#endif
{
    QEmacsState *qs = &qe_state;
    QEArgs args;
    int status;

    args.qs = qs;
    args.argc = argc;
    args.argv = argv;

    status = url_main_loop(qe_init, &args);

#ifdef CONFIG_ALL_KMAPS
    /* unmap/free input methods file */
    qe_unload_input_methods(qs);
#endif
#ifdef CONFIG_UNICODE_JOIN
    /* free ligature arrays */
    unload_ligatures();
#endif

    /* restore TTY so console is clean for error messages */
    free_font_cache(qs->screen);
    dpy_close(qs->screen);

#ifndef CONFIG_TINY
    /* exit all external modules in link order */
    qe_exit_all_modules(qs);

    if (free_everything) {
        /* free all structures for valgrind */
        while (qs->first_window) {
            EditState *e = qs->first_window;
            edit_close(&e);
        }
        while (qs->first_buffer) {
            EditBuffer *b = qs->first_buffer;
            eb_free(&b);
        }
        while (qs->input_methods) {
            InputMethod *p = qs->input_methods;
            qs->input_methods = p->next;
            if (p->data)
                qe_free(&p);
        }
        if (qs->cmd_array) {
            int i, j;
            for (i = 0; i < qs->cmd_array_count; i++) {
                if (qs->cmd_array[i].allocated) {
                    const CmdDef *d = qs->cmd_array[i].array;
                    for (j = qs->cmd_array[i].count; j-- > 0; d++) {
                        /* free named macros and xxx-mode commands */
                        qe_free(unconst(char **)&d->name);
                        qe_free(unconst(char **)&d->spec);
                    }
                    qe_free(unconst(CmdDef **)&qs->cmd_array[i].array);
                }
            }
            qe_free(&qs->cmd_array);
        }
        qe_free_bindings(&qs->first_key);
        while (qs->first_mode) {
            ModeDef *m = qs->first_mode;
            qs->first_mode = m->next;

            qe_free_bindings(&m->first_key);
            // XXX: should free allocated ModeDef structures
        }
        while (qs->first_variable) {
            struct VarDef *vp = qs->first_variable;
            qs->first_variable = vp->next;
            if (vp->str_alloc)
                qe_free(&vp->value.str);
            if (vp->var_alloc) {
                qe_free(&vp->name);
                qe_free(&vp);
            }
        }
        css_free_colors();
        qe_free(&qs->buffer_cache);
        qs->buffer_cache_size = qs->buffer_cache_len = 0;
        qe_clear_macro(qs);
        qe_free(&qs->macro_format);
        qe_free_history_list(qs);
    }
#endif
    return status;
}
