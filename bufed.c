/*
 * Buffer editor mode for QEmacs.
 *
 * Copyright (c) 2001-2002 Fabrice Bellard.
 * Copyright (c) 2002-2014 Charlie Gordon.
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

enum {
    BUFED_HIDE_SYSTEM = 0,
    BUFED_ALL_VISIBLE = 1,
};

typedef struct BufedState {
    ModeDef *signature;
    int flags;
    EditState *cur_window;
    EditBuffer *cur_buffer;
    EditBuffer *last_buffer;
    StringArray items;
} BufedState;

static ModeDef bufed_mode;

static BufedState *bufed_get_state(EditState *s, int status)
{
    BufedState *bs = s->b->priv_data;

    if (bs && bs->signature == &bufed_mode)
        return bs;

    if (status)
        put_status(s, "Not a bufed buffer");

    return NULL;
}

static void build_bufed_list(EditState *s)
{
    QEmacsState *qs = s->qe_state;
    EditBuffer *b;
    BufedState *bs;
    int last_index = list_get_pos(s);
    int i;

    if (!(bs = bufed_get_state(s, 1)))
        return;

    free_strings(&bs->items);
    for (b = qs->first_buffer; b != NULL; b = b->next) {
        if (!(b->flags & BF_SYSTEM) || (bs->flags & BUFED_ALL_VISIBLE))
            add_string(&bs->items, b->name, 0);
    }

    /* build buffer */
    b = s->b;
    eb_clear(b);

    for (i = 0; i < bs->items.nb_items; i++) {
        EditBuffer *b1 = eb_find(bs->items.items[i]->str);
        char flags[4];
        char *flagp = flags;

        if (i == last_index) {
            s->offset = b->total_size;
        }
        if (b1) {
            if (b1->modified)
                *flagp++ = '*';
            if (b1->flags & BF_READONLY)
                *flagp++ = '%';
        }
        *flagp = '\0';
        eb_printf(b, " %-2s%-16s", flags, bs->items.items[i]->str);
        if (b1) {
            char path[MAX_FILENAME_SIZE];
            const char *mode_name;

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
            } else
            if (b1->syntax_mode) {
                mode_name = b1->syntax_mode->name;
            } else {
                mode_name = "none";
            }

            eb_printf(b, " %10d %1.0d %-8s %-8s %s",
                      b1->total_size, b1->style_bytes & 7,
                      b1->charset->name, mode_name,
                      make_user_path(path, sizeof(path), b1->filename));
        }
        eb_printf(b, "\n");
    }
    b->modified = 0;
    b->flags |= BF_READONLY;
}

static EditBuffer *bufed_get_buffer(EditState *s)
{
    BufedState *bs;
    int index;

    if (!(bs = bufed_get_state(s, 1)))
        return NULL;

    index = list_get_pos(s);
    if (index < 0 || index >= bs->items.nb_items)
        return NULL;

    return eb_find(bs->items.items[index]->str);
}

static void bufed_select(EditState *s, int temp)
{
    BufedState *bs;
    EditBuffer *b, *last_buffer;
    EditState *e;
    int index;

    if (!(bs = bufed_get_state(s, 1)))
        return;

    if (temp < 0) {
        b = check_buffer(&bs->cur_buffer);
        last_buffer = check_buffer(&bs->last_buffer);
    } else {
        index = list_get_pos(s);
        if (index < 0 || index >= bs->items.nb_items)
            return;

        b = eb_find(bs->items.items[index]->str);
        last_buffer = bs->cur_buffer;
    }
    e = check_window(&bs->cur_window);
    if (e && b) {
        switch_to_buffer(e, b);
        e->last_buffer = last_buffer;
    }
    if (temp <= 0) {
        /* delete bufed window */
        do_delete_window(s, 1);
        if (e)
            e->qe_state->active_window = e;
    } else {
        do_refresh_complete(s);
    }
}

/* iterate 'func_item' to selected items. If no selected items, then
   use current item */
static void string_selection_iterate(StringArray *cs,
                                     int current_index,
                                     void (*func_item)(void *, StringItem *),
                                     void *opaque)
{
    StringItem *item;
    int count, i;

    count = 0;
    for (i = 0; i < cs->nb_items; i++) {
        item = cs->items[i];
        if (item->selected) {
            func_item(opaque, item);
            count++;
        }
    }

    /* if no item selected, then act on selected item */
    if (count == 0 &&
        current_index >=0 && current_index < cs->nb_items) {
        item = cs->items[current_index];
        func_item(opaque, item);
    }
}

static void bufed_kill_item(void *opaque, StringItem *item)
{
    EditState *s = opaque;

    /* XXX: avoid killing buffer list by mistake */
    if (strcmp(s->b->name, item->str))
        do_kill_buffer(s, item->str);
}

static void bufed_kill_buffer(EditState *s)
{
    BufedState *bs;

    if (!(bs = bufed_get_state(s, 1)))
        return;

    string_selection_iterate(&bs->items, list_get_pos(s),
                             bufed_kill_item, s);
    build_bufed_list(s);
}

/* show a list of buffers */
static void do_list_buffers(EditState *s, int argval)
{
    BufedState *bs;
    EditBuffer *b;
    EditState *e;
    int i;

    /* ignore command from the minibuffer and popups */
    if (s->minibuf || (s->flags & WF_POPUP))
        return;

    if (s->flags & WF_POPLEFT) {
        /* avoid messing with the dired pane */
        s = find_window(s, KEY_RIGHT, s);
        s->qe_state->active_window = s;
    }

    b = eb_scratch("*bufed*", BF_READONLY | BF_SYSTEM | BF_UTF8);
    if (!b)
        return;

    e = show_popup(b);
    if (!e)
        return;

    edit_set_mode(e, &bufed_mode);

    if (!(bs = bufed_get_state(e, 1)))
        return;

    bs->cur_window = s;
    bs->cur_buffer = s->b;
    bs->last_buffer = s->last_buffer;

    if (argval == NO_ARG) {
        bs->flags &= ~BUFED_ALL_VISIBLE;
    } else {
        bs->flags |= BUFED_ALL_VISIBLE;
    }
    build_bufed_list(e);

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
    EditBuffer *b;

    b = bufed_get_buffer(s);
    if (!b)
        return;

    b->modified = 0;
    build_bufed_list(s);
}

static void bufed_toggle_read_only(EditState *s)
{
    EditBuffer *b;

    b = bufed_get_buffer(s);
    if (!b)
        return;

    b->flags ^= BF_READONLY;
    build_bufed_list(s);
}

static void bufed_refresh(EditState *s, int toggle)
{
    BufedState *bs;

    if (!(bs = bufed_get_state(s, 1)))
        return;

    if (toggle)
        bs->flags ^= BUFED_ALL_VISIBLE;

    build_bufed_list(s);
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
    if (p->b->priv_data) {
        BufedState *bs = p->b->priv_data;
        if (bs->signature == &bufed_mode)
            return 95;
        else
            return 0;
    }
    return 0;
}

static void bufed_close(EditBuffer *b)
{
    BufedState *bs = b->priv_data;

    if (bs && bs->signature == &bufed_mode) {
        free_strings(&bs->items);
    }

    qe_free(&b->priv_data);
    if (b->close == bufed_close)
        b->close = NULL;
}

static int bufed_mode_init(EditState *s, EditBuffer *b, int flags)
{
    BufedState *bs;

    list_mode.mode_init(s, b, flags);

    if (s) {
        if (s->b->priv_data) {
            bs = s->b->priv_data;
            if (bs->signature != &bufed_mode)
                return -1;
        } else {
            /* XXX: should be allocated by buffer_load API */
            bs = qe_mallocz(BufedState);
            if (!bs)
                return -1;

            bs->signature = &bufed_mode;
            s->b->priv_data = bs;
            s->b->close = bufed_close;
        }
    }
    return 0;
}

/* specific bufed commands */
static CmdDef bufed_commands[] = {
    CMD1( KEY_RET, KEY_NONE,
          "bufed-select", bufed_select, 0)
    /* bufed-abort should restore previous buffer in right-window */
    CMD1( KEY_CTRL('g'), KEY_CTRLX(KEY_CTRL('g')),
          "bufed-abort", bufed_select, -1)
    CMD1( ' ', KEY_CTRL('t'),
          "bufed-toggle-selection", list_toggle_selection, 1)
    CMD1( KEY_DEL, KEY_NONE,
          "bufed-unmark-backward", list_toggle_selection, -1)
    //CMD1( 'u', KEY_NONE, "bufed-unmark", bufed_mark, ' ')
    CMD0( '~', KEY_NONE,
          "bufed-clear-modified", bufed_clear_modified)
    CMD0( '%', KEY_NONE,
          "bufed-toggle-read-only", bufed_toggle_read_only)
    CMD1( 'a', '.',
          "bufed-toggle-all-visible", bufed_refresh, 1)
    CMD1( 'n', KEY_CTRL('n'), /* KEY_DOWN */
          "bufed-next-line", do_up_down, 1)
    CMD1( 'p', KEY_CTRL('p'), /* KEY_UP */
          "bufed-previous-line", do_up_down, -1)
    CMD1( 'r', 'g',
          "bufed-refresh", bufed_refresh, 0)
    CMD0( 'k', 'd',
          "bufed-kill-buffer", bufed_kill_buffer)
    CMD_DEF_END,
};

static CmdDef bufed_global_commands[] = {
    CMD2( KEY_CTRLX(KEY_CTRL('b')), KEY_NONE,
          "list-buffers", do_list_buffers, ESi, "ui")
    CMD_DEF_END,
};

static int bufed_init(void)
{
    /* inherit from list mode */
    /* CG: assuming list_mode already initialized ? */
    memcpy(&bufed_mode, &list_mode, sizeof(ModeDef));
    bufed_mode.name = "bufed";
    bufed_mode.mode_probe = bufed_mode_probe;
    bufed_mode.mode_init = bufed_mode_init;
    bufed_mode.display_hook = bufed_display_hook;

    /* first register mode */
    qe_register_mode(&bufed_mode, MODEF_VIEW);

    qe_register_cmd_table(bufed_commands, &bufed_mode);
    qe_register_cmd_table(bufed_global_commands, NULL);

    return 0;
}

qe_module_init(bufed_init);
