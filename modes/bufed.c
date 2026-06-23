/*
 * Buffer editor mode for QEmacs.
 *
 * Copyright (c) 2001-2002 Fabrice Bellard.
 * Copyright (c) 2002-2026 Charlie Gordon.
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

static int bufed_pf_flags;
// (c) creation time (b) buffer name (f) filename (z) size (t) modification time
// (a) access time (m) modified first
static char bufed_sort_default[] = "mtbfpzac";
#define BUFED_SORT_LEN  sizeof(bufed_sort_default)
static char bufed_sort_order[BUFED_SORT_LEN] = "mtbfpzac";

enum {
    BUFED_HIDE_SYSTEM = 0,
    BUFED_SYSTEM_VISIBLE = 1,
    BUFED_ALL_VISIBLE = 2,
};

enum {
    BUFED_STYLE_NORMAL = QE_STYLE_DEFAULT,
    BUFED_STYLE_HEADER = QE_STYLE_STRING,
    BUFED_STYLE_BUFNAME = QE_STYLE_KEYWORD,
    BUFED_STYLE_FILENAME = QE_STYLE_FUNCTION,
    BUFED_STYLE_DIRECTORY = QE_STYLE_COMMENT,
    BUFED_STYLE_SYSTEM = QE_STYLE_ERROR,
};

typedef struct BufedState {
    QEModeData base;
    int flags;
    int last_index;
    int pf_flags;
    EditState *cur_window;
    EditBuffer *cur_buffer;
    EditBuffer *last_buffer;
    StringArray items;
} BufedState;

static ModeDef bufed_mode;

static inline BufedState *bufed_get_state(EditState *e, int status)
{
    return qe_get_buffer_mode_data(e->b, &bufed_mode, status ? e : NULL);
}

static int bufed_sort_func(void *opaque, const void *p1, const void *p2)
{
    const StringItem * const *pp1 = (const StringItem * const *)p1;
    const StringItem * const *pp2 = (const StringItem * const *)p2;
    const StringItem *item1 = *pp1;
    const StringItem *item2 = *pp2;
    const EditBuffer *b1 = item1->opaque;
    const EditBuffer *b2 = item2->opaque;
    int res;
    const char *p;

    // sort system buffers last in name order regardless of `sort_order`
    if ((res = (b1->flags & BF_SYSTEM) - (b2->flags & BF_SYSTEM)) != 0)
        return res;

    if ((res = (b1->flags & BF_IS_LOG) - (b2->flags & BF_IS_LOG)) != 0)
        return res;

    if ((res = (b1->flags & BF_IS_STYLE) - (b2->flags & BF_IS_STYLE)) != 0)
        return res;

    if ((res = (*b1->name == '*') - (*b2->name == '*')) != 0)
        return res;

    if ((b1->flags & BF_SYSTEM) || (*b1->name == '*'))
        return qe_strcollate(b1->name, b2->name);

    for (p = bufed_sort_order; *p; p++) {
        switch (qe_tolower(*p)) {
        case 'm':   // modified first
            res = (b2->modified - b1->modified);
            break;
        case 'a':   // access time, most recent first
            res = (b1->atime < b2->atime) - (b1->atime > b2->atime);
            break;
        case 'c':   // creation time, most recent first
            res = (b1->ctime < b2->ctime) - (b1->ctime > b2->ctime);
            break;
        case 't':   // modification time, most recent first
            res = (b1->mtime < b2->mtime) - (b1->mtime > b2->mtime);
            break;
        case 'z':   // buffer size, smallest first
            res = (b1->total_size > b2->total_size) - (b1->total_size < b2->total_size);
            break;
        case 'f':   // filename, no filename last
            if (*b1->filename && *b2->filename)
                res = qe_strcollate(get_basename(b1->filename), get_basename(b2->filename));
            else
                res = (*b2->filename - *b1->filename);
            break;
        case 'p':   // pathname, no filename last
            if (*b1->filename && *b2->filename)
                res = qe_strcollate(b1->filename, b2->filename);
            else
                res = (*b2->filename - *b1->filename);
            break;
        case 'b':   // buffer name, system buffers last
            res = qe_strcollate(b1->name, b2->name);
            break;
        }
        if (qe_isupper(*p))
            res = -res;
        if (res)
            break;
    }
    return res;
}

static void bufed_build_list(EditState *s, BufedState *bs)
{
    QEmacsState *qs = s->qs;
    EditBuffer *b, *b1;
    StringItem *item;
    int i, line, topline, col, vpos;

    free_strings(&bs->items);
    for (b1 = qs->first_buffer; b1 != NULL; b1 = b1->next) {
        if (!(b1->flags & BF_SYSTEM)
        ||  (bs->flags & BUFED_ALL_VISIBLE)
        ||  (!(b1->flags & (BF_IS_LOG | BF_IS_STYLE)) &&
             (bs->flags & BUFED_SYSTEM_VISIBLE)))
        {
            item = add_string(&bs->items, b1->name, 0);
            item->opaque = b1;
        }
    }
    bs->pf_flags = bufed_pf_flags;

    if (*bufed_sort_order) {
        qe_qsort_r(bs->items.items, bs->items.nb_items,
                   sizeof(StringItem *), bs, bufed_sort_func);
    }

    /* build buffer */
    b = s->b;
    vpos = -1;
    if (b->total_size > 0 && bs->items.nb_items > s->rows) {
        /* try and preserve current line in window */
        eb_get_pos(b, &line, &col, s->offset);
        eb_get_pos(b, &topline, &col, s->offset_top);
        vpos = line - topline;
    }
    eb_clear(b);

    line = 0;
    b->tab_width = 20;
    for (i = 0; i < bs->items.nb_items; i++) {
        char flags[4];
        char *flagp = flags;
        int len, style0 = 0;

        item = bs->items.items[i];
        b1 = qe_check_buffer(qs, (EditBuffer**)(void *)&item->opaque);

        if ((bs->last_index == -1 && b1 == bs->cur_buffer)
        ||  bs->last_index >= i) {
            line = i;
            s->offset = b->offset;
        }
        if (b1) {
            if (b1->flags & BF_SYSTEM) {
                style0 = BUFED_STYLE_SYSTEM;
                *flagp++ = 'S';
            } else
            if (b1->modified) {
                *flagp++ = '*';
            } else
            if (b1->flags & BF_READONLY) {
                *flagp++ = '%';
            }
        }
        if (flagp < flags + 3)
            *flagp++ = ' ';
        *flagp = '\0';

        eb_print_style(b, style0, "%3s", flags);
        b->cur_style = BUFED_STYLE_BUFNAME;
        len = eb_put_filename(b, item->str, bs->pf_flags);
        b->tab_width = max_int(3 + len + 2, b->tab_width);
        eb_putc(b, '\t');
        if (b1) {
            char path[MAX_FILENAME_SIZE];
            char mode_buf[64];
            const char *mode_name;
            buf_t outbuf, *out;
            QEModeData *md;

            if (b1->flags & BF_IS_LOG) {
                mode_name = "log";
            } else
            if (b1->flags & BF_IS_STYLE) {
                mode_name = "style";
            } else
            if (b1->saved_mode) {
                mode_name = b1->saved_mode->name;
            } else
            if (b1->default_mode) {
                mode_name = b1->default_mode->name;
            } else {
                mode_name = "none";
            }
            out = buf_init(&outbuf, mode_buf, sizeof(mode_buf));
            if (b1->data_type_name) {
                buf_printf(out, "%s+", b1->data_type_name);
            }
            buf_puts(out, mode_name);
            for (md = b1->mode_data_list; md; md = md->next) {
                if (md->mode && md->mode != b1->saved_mode)
                    buf_printf(out, ",%s", md->mode->name);
            }

            eb_print_style(b, style0, " %10d %1.0d %-8.8s %-11s ",
                           b1->total_size, b1->style_bytes & 7,
                           b1->charset->name, mode_buf);
            if (b1->flags & (BF_DIRED | BF_SHELL))
                b->cur_style = BUFED_STYLE_DIRECTORY;
            else
                b->cur_style = BUFED_STYLE_FILENAME;
            make_user_path(path, sizeof(path), b1->filename);
            eb_put_filename(b, path, bs->pf_flags);
            b->cur_style = style0;
        }
        eb_putc(b, '\n');
    }
    bs->last_index = -1;
    b->modified = 0;
    b->flags |= BF_READONLY;
    if (vpos >= 0 && line > vpos) {
        /* scroll window contents to preserve current line position */
        s->offset_top = eb_goto_pos(b, line - vpos, 0);
    }
}

static EditBuffer *bufed_get_buffer(EditState *s, BufedState *bs)
{
    int index;

    index = list_get_pos(s);
    if (index < 0 || index >= bs->items.nb_items)
        return NULL;

    return qe_check_buffer(s->qs, (EditBuffer**)(void *)&bs->items.items[index]->opaque);
}

static void bufed_select(EditState *s, int temp)
{
    BufedState *bs;
    EditBuffer *b, *last_buffer;
    EditState *e;
    QEmacsState *qs = s->qs;
    int index = -1;

    if (!(bs = bufed_get_state(s, 1)))
        return;

    if (temp < 0) {
        b = qe_check_buffer(qs, &bs->cur_buffer);
        last_buffer = qe_check_buffer(qs, &bs->last_buffer);
    } else {
        index = list_get_pos(s);
        if (index < 0 || index >= bs->items.nb_items)
            return;

        if (temp > 0 && index == bs->last_index)
            return;

        b = qe_check_buffer(qs, (EditBuffer**)(void *)&bs->items.items[index]->opaque);
        last_buffer = bs->cur_buffer;
    }
    e = qe_check_window(qs, &bs->cur_window);
    if (e && b) {
        switch_to_buffer(e, b);
        e->last_buffer = last_buffer;
    } else {
        if (e == NULL) {
            switch_to_buffer(s, b);
        }
    }
    if (temp <= 0) {
        if (s->flags & WF_POPUP) {
            /* delete bufed window */
            do_delete_window(s, 1);
            if (e) {
                qs->active_window = e;
            }
        }
    } else {
        bs->last_index = index;
        do_refresh_complete(s);
    }
}

/* iterate 'func_item' to selected items. If no selected items, then
   use current item */
static int string_selection_iterate(StringArray *cs,
                                    int pos,
                                    void (*func_item)(void *, StringItem *, int),
                                    void *opaque)
{
    StringItem *item;
    int count, i;

    count = 0;
    for (i = 0; i < cs->nb_items; i++) {
        item = cs->items[i];
        if (item->selected) {
            func_item(opaque, item, i);
            count++;
        }
    }

    /* if no item selected, then act on current item */
    if (count == 0 && pos >= 0 && pos < cs->nb_items) {
        item = cs->items[pos];
        func_item(opaque, item, pos);
        count++;
    }
    return count;
}

static void bufed_kill_item(void *opaque, StringItem *item, int index)
{
    BufedState *bs;
    EditState *s = opaque;
    EditBuffer *b = qe_check_buffer(s->qs, (EditBuffer**)(void *)&item->opaque);

    if (!(bs = bufed_get_state(s, 1)))
        return;

    /* Avoid killing buffer list by mistake */
    if (b && b != s->b) {
        /* Give the user a chance to confirm if buffer is modified */
        do_kill_buffer(s, item->str, 0);
        item->opaque = NULL;
        if (bs->cur_buffer == b)
            bs->cur_buffer = NULL;
    }
}

static void bufed_kill_buffer(EditState *s)
{
    BufedState *bs;

    if (!(bs = bufed_get_state(s, 1)))
        return;

    /* XXX: should just kill current item? */
    string_selection_iterate(&bs->items, list_get_pos(s),
                             bufed_kill_item, s);
    bufed_select(s, 1);
    bufed_build_list(s, bs);
}

/* show a list of buffers */
static void do_buffer_list(EditState *s, int argval)
{
    QEmacsState *qs = s->qs;
    BufedState *bs;
    EditBuffer *b;
    EditState *e;
    int i;

    /* ignore command from the minibuffer and popups */
    if (s->flags & (WF_POPUP | WF_MINIBUF))
        return;

    if (s->flags & WF_POPLEFT) {
        /* avoid messing with the dired pane */
        s = find_window(s, KEY_RIGHT, s);
        qs->active_window = s;
    }

    b = qe_new_buffer(qs, "*bufed*", BC_CLEAR | BF_SYSTEM | BF_UTF8 | BF_STYLE1);
    if (!b)
        return;

    /* XXX: header should have column captions */
    e = show_popup(s, b, "Buffer list");
    if (!e)
        return;

    edit_set_mode(e, &bufed_mode);

    if (!(bs = bufed_get_state(e, 1)))
        return;

    bs->last_index = -1;
    bs->cur_window = s;
    bs->cur_buffer = s->b;
    bs->last_buffer = s->last_buffer;
    if (argval > 0) {
        bs->flags |= BUFED_SYSTEM_VISIBLE;
        if (argval > 4)
            bs->flags |= BUFED_ALL_VISIBLE;
    }
    bufed_build_list(e, bs);

    /* if active buffer is found, go directly on it */
    for (i = 0; i < bs->items.nb_items; i++) {
        if (strequal(bs->items.items[i]->str, s->b->name)) {
            e->offset = eb_goto_pos(e->b, i, 0);
            break;
        }
    }
}

static void bufed_clear_modified(EditState *s)
{
    BufedState *bs;
    EditBuffer *b;

    if (!(bs = bufed_get_state(s, 1)))
        return;

    b = bufed_get_buffer(s, bs);
    if (!b)
        return;

    b->modified = 0;
    bufed_build_list(s, bs);
}

static void bufed_toggle_read_only(EditState *s)
{
    BufedState *bs;
    EditBuffer *b;

    if (!(bs = bufed_get_state(s, 1)))
        return;

    b = bufed_get_buffer(s, bs);
    if (!b)
        return;

    b->flags ^= BF_READONLY;
    bufed_build_list(s, bs);
}

static void bufed_refresh(EditState *s, int toggle)
{
    BufedState *bs;

    if (!(bs = bufed_get_state(s, 1)))
        return;

    if (toggle) {
        if (bs->flags & BUFED_ALL_VISIBLE)
            bs->flags &= ~(BUFED_SYSTEM_VISIBLE | BUFED_ALL_VISIBLE);
        else
        if (bs->flags & BUFED_SYSTEM_VISIBLE)
            bs->flags |= BUFED_ALL_VISIBLE;
        else
            bs->flags |= BUFED_SYSTEM_VISIBLE;
    }

    bufed_build_list(s, bs);
}

static void bufed_set_sort_order(EditState *s, const char *str)
{
    BufedState *bs;
    char tmp[BUFED_SORT_LEN];
    int i;

    if (!*str) str = bufed_sort_default;

    for (i = 0; str[i]; i++) {
        int o = qe_tolower(str[i]);
        if (!strchr(bufed_sort_default, o)) {
            put_error(s, "Invalid sort order '%c'", str[i]);
            return;
        }
        if (memchr(tmp, o, i)) {
            put_error(s, "Duplicate sort order '%c'", str[i]);
            return;
        }
        tmp[i] = (char)o;
    }
    pstrcpy(bufed_sort_order, sizeof(bufed_sort_order), str);

    if (!(bs = bufed_get_state(s, 1)))
        return;

    bs->last_index = -1;
    bufed_build_list(s, bs);
    put_status(s, "Sort order: %s", bufed_sort_order);
}

static void bufed_toggle_sort(EditState *s, int arg)
{
    BufedState *bs;

    size_t len = strlen(bufed_sort_order);
    size_t i;
    char o = (char)arg;
    char upper = (char)qe_toupper(o);
    if (*bufed_sort_order == o) {
        *bufed_sort_order = upper;
    } else
    if (*bufed_sort_order == upper) {
        memmove(bufed_sort_order, bufed_sort_order + 1, --len);
        bufed_sort_order[len++] = o;
        bufed_sort_order[len] = '\0';
    } else {
        for (i = 0; i < len; i++) {
            if (bufed_sort_order[i] == o || bufed_sort_order[i] == upper)
                break;
        }
        memmove(bufed_sort_order + 1, bufed_sort_order, i);
        bufed_sort_order[0] = o;
        if (i == len)
            bufed_sort_order[i + 1] = '\0';
    }

    if (!(bs = bufed_get_state(s, 1)))
        return;

    bs->last_index = -1;
    bufed_build_list(s, bs);
    put_status(s, "Sort order: %s", bufed_sort_order);
}

static void bufed_cycle_encoding(EditState *s)
{
    BufedState *bs;

    int encoding = bufed_pf_flags & PF_ENCODING;
    bufed_pf_flags &= ~PF_ENCODING;
    bufed_pf_flags |= (encoding + 1) & PF_ENCODING;

    if (!(bs = bufed_get_state(s, 1)))
        return;

    bufed_build_list(s, bs);
}

static void bufed_toggle_unicode(EditState *s)
{
    BufedState *bs;

    bufed_pf_flags ^= PF_NO_UNICODE;

    if (!(bs = bufed_get_state(s, 1)))
        return;

    bufed_build_list(s, bs);
}

static void bufed_display_hook(EditState *s)
{
    /* Prevent point from going beyond list */
    if (s->offset && s->offset == s->b->total_size)
        do_up_down(s, -1);

    if (s->flags & WF_POPUP)
        bufed_select(s, 1);
}

static int bufed_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (qe_get_buffer_mode_data(p->b, &bufed_mode, NULL))
        return 95;

    return 0;
}

static int bufed_mode_init(EditState *s, EditBuffer *b, int flags)
{
    BufedState *bs = qe_get_buffer_mode_data(b, &bufed_mode, NULL);

    if (!bs)
        return -1;

    return list_mode.mode_init(s, b, flags);
}

static void bufed_mode_free(EditBuffer *b, void *state)
{
    BufedState *bs = state;

    free_strings(&bs->items);
}

/* specific bufed commands */
static const CmdDef bufed_commands[] = {
    CMD1( "bufed-select", "RET, LF, SPC, e, q",
          "Select buffer from current line and close bufed popup window",
          bufed_select, 0)
    CMD1( "bufed-abort", "C-g, C-x C-g",
          "Abort and close bufed popup window",
          bufed_select, -1)
    CMDx( "bufed-save-buffer", "s",
          "Save the buffer to its associated file",
          bufed_save_buffer)
    CMD0( "bufed-clear-modified", "~",
          "Clear buffer modified indicator",
          bufed_clear_modified)
    CMD0( "bufed-toggle-read-only", "%",
          "Toggle buffer read-only flag",
          bufed_toggle_read_only)
    CMD1( "bufed-toggle-all-visible", "a, .",
          "Show all buffers including system buffers",
          bufed_refresh, 1)
    CMD1( "bufed-refresh", "r, g",
          "Refresh buffer list",
          bufed_refresh, 0)
    CMD0( "bufed-kill-buffer", "k, d, DEL, delete",
          "Kill buffer at current line in bufed window",
          bufed_kill_buffer)
    CMD2( "bufed-set-sort-order", "S",
          "Set the buffer list sort criteria",
          bufed_set_sort_order, ESs,
          "s{Bufed sort order [mtbfpzac]: }")
    CMD1( "bufed-unsorted", "c, u",
          "Sort the buffer list by creation time",
          bufed_toggle_sort, 'c')
    CMD1( "bufed-sort-name", "b",
          "Sort the buffer list by buffer name",
          bufed_toggle_sort, 'b')
    CMD1( "bufed-sort-filename", "f",
          "Sort the buffer list by buffer file name",
          bufed_toggle_sort, 'f')
    CMD1( "bufed-sort-pathname", "p",
          "Sort the buffer list by buffer path name",
          bufed_toggle_sort, 'p')
    CMD1( "bufed-sort-size", "z",
          "Sort the buffer list by buffer size",
          bufed_toggle_sort, 'z')
    CMD1( "bufed-sort-time", "t",
          "Sort the buffer list by buffer modification time",
          bufed_toggle_sort, 't')
    CMD1( "bufed-sort-atime", "a",
          "Sort the buffer list by buffer access time",
          bufed_toggle_sort, 'a')
    CMD1( "bufed-sort-modified", "m",
          "Sort the buffer list with modified buffers first",
          bufed_toggle_sort, 'm')
    CMD0( "bufed-cycle-encoding", "\\",
          "Cycle display of non printing characters in filenames",
          bufed_cycle_encoding)
    CMD0( "bufed-toggle-unicode", "U",
          "Toggle display of Unicode characters in filenames",
          bufed_toggle_unicode)
    CMD2( "bufed-summary", "?",
          "Display a summary of bufed commands",
          do_apropos, ESs, "@{bufed}")
    CMDx( "bufed-help", "h",
          "Show help window about the bufed mode",
         bufed_help)
};

static const CmdDef bufed_global_commands[] = {
    CMD2( "buffer-list", "C-x C-b",
          "Show the buffer list in a popup window",
          do_buffer_list, ESi, "P")
};

/* additional mode specific bindings */
static const char * const bufed_bindings[] = {
    "n", "next-line",
    "p", "previous-line",
    NULL
};

static int bufed_init(QEmacsState *qs)
{
    /* inherit from list mode */
    /* CG: assuming list_mode already initialized ? */
    // XXX: remove this mess
    memcpy(&bufed_mode, &list_mode, offsetof(ModeDef, first_key));
    bufed_mode.name = "bufed";
    bufed_mode.mode_probe = bufed_mode_probe;
    bufed_mode.buffer_instance_size = sizeof(BufedState);
    bufed_mode.mode_init = bufed_mode_init;
    bufed_mode.mode_free = bufed_mode_free;
    bufed_mode.display_hook = bufed_display_hook;
    bufed_mode.bindings = bufed_bindings;

    qe_register_mode(qs, &bufed_mode, MODEF_VIEW);
    qe_register_commands(qs, &bufed_mode, bufed_commands, countof(bufed_commands));
    qe_register_commands(qs, NULL, bufed_global_commands, countof(bufed_global_commands));

    return 0;
}

// Used by buffer_complete to construct buffer name popup.
// Uses bufed global state: bufed_pf_flags
int buffer_print_entry(CompleteState *cp, EditState *s, const char *name)
{
    EditBuffer *b = s->b;
    QEmacsState *qs = s->qs;
    EditBuffer *b1 = qe_find_buffer_name(qs, name);
    int len;

    if (b1) {
        b->cur_style = QE_STYLE_KEYWORD;
        len = eb_put_filename(b, b1->name, bufed_pf_flags);
        b->tab_width = max3_int(16, len + 3, b->tab_width);
        len += eb_putc(b, '\t');
        if (*b1->filename) {
            b->cur_style = QE_STYLE_COMMENT;
            len += eb_put_filename(b, b1->filename, bufed_pf_flags);
        }
        b->cur_style = QE_STYLE_DEFAULT;
    } else {
        return eb_put_filename(b, name, bufed_pf_flags);
    }
    return len;
}

qe_module_init(bufed_init);
