/*
 * Buffer editor mode for QEmacs.
 *
 * Copyright (c) 2001, 2002 Fabrice Bellard.
 * Copyright (c) 2002-2008 Charlie Gordon.
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
    BUFED_ALL_VISIBLE = 1
};

typedef struct BufedState {
    StringArray items;
    int flags;
    int last_index;
} BufedState;

static ModeDef bufed_mode;

static void build_bufed_list(EditState *s)
{
    QEmacsState *qs = s->qe_state;
    EditBuffer *b, *b1;
    BufedState *hs;
    int last_index = list_get_pos(s);
    int i, flags;

    hs = s->mode_data;

    free_strings(&hs->items);
    for (b = qs->first_buffer; b != NULL; b = b->next) {
        if (!(b->flags & BF_SYSTEM) || (hs->flags & BUFED_ALL_VISIBLE))
            add_string(&hs->items, b->name);
    }

    /* build buffer */
    b = s->b;
    flags = b->flags;
    b->flags &= ~BF_READONLY;
    eb_delete(b, 0, b->total_size);
    for (i = 0; i < hs->items.nb_items; i++) {
        eb_printf(b, " %-20s", hs->items.items[i]->str);
        b1 = eb_find(hs->items.items[i]->str);
        if (b1) {
            /* CG: should also display mode */
            eb_printf(b, " %10d  %s", b1->total_size, b1->filename);
        }
        eb_printf(b, "\n");
    }
    b->flags = flags;
    s->offset = eb_goto_pos(s->b, last_index, 0);
}

static EditBuffer *bufed_get_buffer(EditState *s)
{
    BufedState *bs = s->mode_data;
    int index;

    index = list_get_pos(s);
    if (index < 0 || index >= bs->items.nb_items)
        return NULL;

    return eb_find(bs->items.items[index]->str);
}

static void bufed_select(EditState *s, int temp)
{
    BufedState *bs = s->mode_data;
    StringItem *item;
    EditBuffer *b;
    EditState *e;
    int index;

    index = list_get_pos(s);
    if (index < 0 || index >= bs->items.nb_items)
        return;

    if (temp && index == bs->last_index)
        return;

    item = bs->items.items[index];
    b = eb_find(item->str);
    if (!b)
        return;
    e = find_window(s, KEY_RIGHT);
    if (temp) {
        if (e) {
            bs->last_index = index;
            switch_to_buffer(e, b);
        }
        return;
    }
    if (e) {
        /* delete dired window */
        do_delete_window(s, 1);
        switch_to_buffer(e, b);
    } else {
        switch_to_buffer(s, b);
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
    do_kill_buffer(s, item->str);
}

static void bufed_kill_buffer(EditState *s)
{
    BufedState *hs = s->mode_data;
    string_selection_iterate(&hs->items, list_get_pos(s),
                             bufed_kill_item, s);
    build_bufed_list(s);
}

/* show a list of buffers */
static void do_list_buffers(EditState *s, int argval)
{
    QEmacsState *qs = s->qe_state;
    BufedState *bs;
    EditBuffer *b, *b0;
    EditState *e, *e1;
    int width, i;

    /* remember current buffer for target positioning, because
     * s may be destroyed by insert_window_left
     * CG: remove this kludge once we have delayed EditState free
     */
    b0 = s->b;

    /* XXX: must close this buffer when destroying window: add a
       special buffer flag to tell this */
    b = eb_scratch("*bufed*", BF_READONLY | BF_SYSTEM | BF_UTF8);

    width = qs->width / 5;
    e = insert_window_left(b, width, WF_MODELINE);
    edit_set_mode(e, &bufed_mode, NULL);

    bs = e->mode_data;
    if (argval != NO_ARG) {
        bs->flags |= BUFED_ALL_VISIBLE;
        build_bufed_list(e);
    }

    e1 = find_window(e, KEY_RIGHT);
    if (e1)
        b0 = e1->b;

    /* if active buffer is found, go directly on it */
    for (i = 0; i < bs->items.nb_items; i++) {
        if (strequal(bs->items.items[i]->str, b0->name)) {
            e->offset = eb_goto_pos(e->b, i, 0);
            break;
        }
    }

    /* modify active window */
    qs->active_window = e;
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
    BufedState *bs = s->mode_data;

    if (toggle)
        bs->flags ^= BUFED_ALL_VISIBLE;

    build_bufed_list(s);
}

static void bufed_display_hook(EditState *s)
{
    /* Prevent point from going beyond list */
    if (s->offset && s->offset == s->b->total_size)
        do_up_down(s, -1);

    bufed_select(s, 1);
}

static int bufed_mode_init(EditState *s, ModeSavedData *saved_data)
{
    list_mode.mode_init(s, saved_data);

    build_bufed_list(s);

    return 0;
}

static void bufed_mode_close(EditState *s)
{
    BufedState *bs = s->mode_data;

    free_strings(&bs->items);

    list_mode.mode_close(s);
}

/* specific bufed commands */
static CmdDef bufed_commands[] = {
    CMD1( KEY_RET, KEY_RIGHT,
          "bufed-select", bufed_select, 0)
    /* bufed-abort should restore previous buffer in right-window */
    CMD1( KEY_CTRL('g'), KEY_NONE,
          "bufed-abort", do_delete_window, 0)
    CMD0( ' ', KEY_CTRL('t'),
          "bufed-toggle_selection", list_toggle_selection)
    /* BS should go back to previous item and unmark it */
    //CMD1( 'u', KEY_NONE, "bufed-unmark", bufed_mark, ' ')
    CMD0( '~', KEY_NONE,
          "bufed-clear-modified", bufed_clear_modified)
    CMD0( '%', KEY_NONE,
          "bufed-toggle-read-only", bufed_toggle_read_only)
    CMD1( 'a', KEY_NONE,
          "bufed-toggle-all-visible", bufed_refresh, 1)
    CMD1( 'n', KEY_NONE,
          "next-line", do_up_down, 1)
    CMD1( 'p', KEY_NONE,
          "previous-line", do_up_down, -1)
    CMD1( 'r', 'g',
          "bufed-refresh", bufed_refresh, 0)
    CMD0( 'k', KEY_F8,
          "bufed-kill-buffer", bufed_kill_buffer)
    CMD_DEF_END,
};

static CmdDef bufed_global_commands[] = {
    CMD_( KEY_CTRLX(KEY_CTRL('b')), KEY_NONE,
          "list-buffers", do_list_buffers, ESi, "ui")
    CMD_DEF_END,
};

static int bufed_init(void)
{
    /* inherit from list mode */
    /* CG: assuming list_mode already initialized ? */
    memcpy(&bufed_mode, &list_mode, sizeof(ModeDef));
    bufed_mode.name = "bufed";
    bufed_mode.instance_size = sizeof(BufedState);
    bufed_mode.mode_init = bufed_mode_init;
    bufed_mode.mode_close = bufed_mode_close;
    /* CG: not a good idea, display hook has side effect on layout */
    bufed_mode.display_hook = bufed_display_hook;

    /* first register mode */
    qe_register_mode(&bufed_mode);

    qe_register_cmd_table(bufed_commands, &bufed_mode);
    qe_register_cmd_table(bufed_global_commands, NULL);

    return 0;
}

qe_module_init(bufed_init);
