/*
 * Buffer editor mode for QEmacs.
 *
 * Copyright (c) 2001-2002 Fabrice Bellard.
 * Copyright (c) 2002-2024 Charlie Gordon.
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
    BUFED_SORT_MODIFIED = 1 << 0,
    BUFED_SORT_TIME     = 1 << 2,
    BUFED_SORT_NAME     = 1 << 4,
    BUFED_SORT_FILENAME = 1 << 6,
    BUFED_SORT_SIZE     = 1 << 8,
    BUFED_SORT_DESCENDING = 0xAAAA,
};

static int bufed_sort_order;  // XXX: should be a variable

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
    int sort_mode;
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
    BufedState *bs = opaque;
    int sort_mode = bs->sort_mode, res;

    if ((res = (b1->flags & BF_SYSTEM) - (b2->flags & BF_SYSTEM)) != 0)
        return res;

    if ((res = (b1->flags & BF_IS_LOG) - (b2->flags & BF_IS_LOG)) != 0)
        return res;

    if ((res = (b1->flags & BF_IS_STYLE) - (b2->flags & BF_IS_STYLE)) != 0)
        return res;

    if (sort_mode & BUFED_SORT_MODIFIED) {
        if ((res = (b2->modified - b1->modified)) != 0)
            return res;
    }
    for (;;) {
        if (sort_mode & BUFED_SORT_TIME) {
            if (b1->mtime != b2->mtime) {
                res = (b1->mtime < b2->mtime) ? -1 : 1;
                break;
            }
        }
        if (sort_mode & BUFED_SORT_SIZE) {
            if (b1->total_size != b2->total_size) {
                res = (b1->total_size < b2->total_size) ? -1 : 1;
                break;
            }
        }
        if (sort_mode & BUFED_SORT_FILENAME) {
            /* sort by buffer filename, no filename last */
            if ((res = (*b2->filename - *b1->filename)) != 0) {
                break;
            } else {
                res = qe_strcollate(b1->filename, b2->filename);
                if (res)
                    break;
            }
        }
        /* sort by buffer name, system buffers last */
        if ((res = (*b1->name == '*') - (*b2->name == '*')) != 0) {
            break;
        } else {
            res = qe_strcollate(b1->name, b2->name);
        }
        break;
    }
    return (sort_mode & BUFED_SORT_DESCENDING) ? -res : res;
}

static void build_bufed_list(EditState *s, BufedState *bs)
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
    bs->sort_mode = bufed_sort_order;

    if (bufed_sort_order) {
        qe_qsort_r(bs->items.items, bs->items.nb_items,
                   sizeof(StringItem *), bs, bufed_sort_func);
    }

    /* build buffer */
    b = s->b;
    vpos = -1;
    if (b->total_size > 0) {
        /* try and preserve current line in window */
        eb_get_pos(b, &line, &col, s->offset);
        eb_get_pos(b, &topline, &col, s->offset_top);
        vpos = line - topline;
    }
    eb_clear(b);

    line = 0;
    for (i = 0; i < bs->items.nb_items; i++) {
        char flags[4];
        char *flagp = flags;
        int len, style0;

        item = bs->items.items[i];
        b1 = qe_check_buffer(qs, (EditBuffer**)(void *)&item->opaque);
        style0 = (b1->flags & BF_SYSTEM) ? BUFED_STYLE_SYSTEM : 0;

        if ((bs->last_index == -1 && b1 == bs->cur_buffer)
        ||  bs->last_index >= i) {
            line = i;
            s->offset = b->offset;
        }
        if (b1) {
            if (b1->flags & BF_SYSTEM)
                *flagp++ = 'S';
            else
            if (b1->modified)
                *flagp++ = '*';
            else
            if (b1->flags & BF_READONLY)
                *flagp++ = '%';
        }
        *flagp = '\0';

        b->cur_style = style0;
        eb_printf(b, " %-2s", flags);
        b->cur_style = BUFED_STYLE_BUFNAME;
        len = strlen(item->str);
        /* simplistic column fitting, does not work for wide characters */
#define COLWIDTH  20
        if (len > COLWIDTH) {
            eb_printf(b, "%.*s...%s",
                      COLWIDTH - 5 - 3, item->str, item->str + len - 5);
        } else {
            eb_printf(b, "%-*s", COLWIDTH, item->str);
        }
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

            b->cur_style = style0;
            eb_printf(b, " %10d %1.0d %-8.8s %-11s ",
                      b1->total_size, b1->style_bytes & 7,
                      b1->charset->name, mode_buf);
            if (b1->flags & (BF_DIRED | BF_SHELL))
                b->cur_style = BUFED_STYLE_DIRECTORY;
            else
                b->cur_style = BUFED_STYLE_FILENAME;
            if (b1->flags & BF_SHELL) {
                get_default_path(b1, b1->offset, path, sizeof(path));
                make_user_path(path, sizeof(path), path);
                get_dirname(path, sizeof(path), path);
            } else {
                make_user_path(path, sizeof(path), b1->filename);
            }
            eb_puts(b, path);
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
    int index = -1;

    if (!(bs = bufed_get_state(s, 1)))
        return;

    if (temp < 0) {
        b = qe_check_buffer(s->qs, &bs->cur_buffer);
        last_buffer = qe_check_buffer(s->qs, &bs->last_buffer);
    } else {
        index = list_get_pos(s);
        if (index < 0 || index >= bs->items.nb_items)
            return;

        if (temp > 0 && index == bs->last_index)
            return;

        b = qe_check_buffer(s->qs, (EditBuffer**)(void *)&bs->items.items[index]->opaque);
        last_buffer = bs->cur_buffer;
    }
    e = qe_check_window(s->qs, &bs->cur_window);
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
            if (e)
                e->qs->active_window = e;
        }
    } else {
        bs->last_index = index;
        do_refresh_complete(s);
    }
}

/* iterate 'func_item' to selected items. If no selected items, then
   use current item */
static void string_selection_iterate(StringArray *cs,
                                     int current_index,
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

    /* if no item selected, then act on selected item */
    if (count == 0 &&
        current_index >=0 && current_index < cs->nb_items) {
        item = cs->items[current_index];
        func_item(opaque, item, current_index);
    }
}

static void bufed_kill_item(void *opaque, StringItem *item, int index)
{
    BufedState *bs;
    EditState *s = opaque;
    EditBuffer *b = qe_check_buffer(s->qs, (EditBuffer**)(void *)&item->opaque);

    if (!(bs = bufed_get_state(s, 1)))
        return;

    /* XXX: avoid killing buffer list by mistake */
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

    /* XXX: should just kill current line */
    string_selection_iterate(&bs->items, list_get_pos(s),
                             bufed_kill_item, s);
    bufed_select(s, 1);
    build_bufed_list(s, bs);
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

    b = qe_new_buffer(qs, "*bufed*", BC_REUSE | BC_CLEAR | BF_SYSTEM | BF_UTF8 | BF_STYLE1);
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
    build_bufed_list(e, bs);

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
    build_bufed_list(s, bs);
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
    build_bufed_list(s, bs);
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

    build_bufed_list(s, bs);
}

static void bufed_set_sort(EditState *s, int order)
{
    BufedState *bs;

    if (!(bs = bufed_get_state(s, 1)))
        return;

    if (bufed_sort_order == order)
        bufed_sort_order = order * 3;
    else
        bufed_sort_order = order;

    bs->last_index = -1;
    build_bufed_list(s, bs);
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
    //CMD0( "bufed-help", "?", "Show help window about the bufed mode", bufed_help)
    //CMD0( "bufed-save-buffer", "s", "Save the buffer to its associated file", bufed_save_buffer)
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
          "Refreh buffer list",
          bufed_refresh, 0)
    CMD0( "bufed-kill-buffer", "k, d, DEL, delete",
          "Kill buffer at current line in bufed window",
          bufed_kill_buffer)
    CMD1( "bufed-unsorted", "u",
          "Sort the buffer list by creation time",
          bufed_set_sort, 0)
    CMD1( "bufed-sort-name", "b",
          "Sort the buffer list by buffer name",
          bufed_set_sort, BUFED_SORT_NAME)
    CMD1( "bufed-sort-filename", "f",
          "Sort the buffer list by buffer file name",
          bufed_set_sort, BUFED_SORT_FILENAME)
    CMD1( "bufed-sort-size", "z",
          "Sort the buffer list by buffer size",
          bufed_set_sort, BUFED_SORT_SIZE)
    CMD1( "bufed-sort-time", "t",
          "Sort the buffer list by buffer modification time",
          bufed_set_sort, BUFED_SORT_TIME)
    CMD1( "bufed-sort-modified", "m",
          "Sort the buffer list with modified buffers first",
          bufed_set_sort, BUFED_SORT_MODIFIED)
    CMD2( "bufed-summary", "?",
          "Display a summary of bufed commands",
          do_apropos, ESs, "@{bufed}")
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

qe_module_init(bufed_init);
