/*
 * Buffer editor mode for QEmacs.
 * Copyright (c) 2001, 2002 Fabrice Bellard.
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

typedef struct BufedState {
    StringArray items;
} BufedState;

ModeDef bufed_mode;

static void build_bufed_list(EditState *s)
{
    QEmacsState *qs = s->qe_state;
    EditBuffer *b;
    BufedState *hs;
    int i;

    hs = s->mode_data;

    free_strings(&hs->items);
    for(b = qs->first_buffer; b != NULL; b = b->next) {
        if (!(b->flags & BF_SYSTEM))
            add_string(&hs->items, b->name);
    }
    
    /* build buffer */
    b = s->b;
    eb_delete(b, 0, b->total_size);
    for(i=0;i<hs->items.nb_items;i++) {
        eb_printf(b, " %s", hs->items.items[i]->str);
        if (i != hs->items.nb_items - 1)
            eb_printf(b, "\n");
    }
}

static void bufed_select(EditState *s)
{
    BufedState *bs = s->mode_data;
    StringItem *item;
    EditBuffer *b;
    EditState *e;
    int index;

    index = list_get_pos(s);
    if (index < 0 || index >= bs->items.nb_items)
        return;
    item = bs->items.items[index];
    b = eb_find(item->str);
    if (!b)
        return;
    e = find_window_right(s);
    if (!e) 
        return;
    /* delete dired window */
    do_delete_window(s, 1);
    switch_to_buffer(e, b);
}

/* iterate 'func_item' to selected items. If no selected items, then
   use current item */
void string_selection_iterate(StringArray *cs, 
                              int current_index,
                              void (*func_item)(void *, StringItem *),
                              void *opaque)
{
    StringItem *item;
    int count, i;

    count = 0;
    for(i=0;i<cs->nb_items;i++) {
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
static void do_list_buffers(EditState *s)
{
    QEmacsState *qs = s->qe_state;
    BufedState *bs;
    EditBuffer *b;
    EditState *e;
    int width, i;

    /* XXX: must close this buffer when destroying window: add a
       special buffer flag to tell this */
    b = eb_new("*bufed*", BF_READONLY | BF_SYSTEM);

    width = qs->width / 4;
    e = insert_window_left(b, width, WF_MODELINE);
    do_set_mode(e, &bufed_mode, NULL);

    bs = e->mode_data;

    /* if active buffer is found, go directly on it */
    for(i=0;i<bs->items.nb_items;i++) {
        if (!strcmp(bs->items.items[i]->str, s->b->name)) {
            e->offset = eb_goto_pos(e->b, i, 0);
            break;
        }
    }

    /* modify active window */
    qs->active_window = e;
}

static int bufed_mode_init(EditState *s, ModeSavedData *saved_data)
{
    list_mode.mode_init(s, saved_data);

    build_bufed_list(s);

    return 0;
}

static void bufed_mode_close(EditState *s)
{
    BufedState *hs;
    hs = s->mode_data;
    free_strings(&hs->items);

    list_mode.mode_close(s);
}

/* specific bufed commands */
static CmdDef bufed_commands[] = {
    CMD0( KEY_RET, KEY_RIGHT, "bufed-select", bufed_select)
    CMD1( KEY_CTRL('g'), KEY_NONE, "delete-window", do_delete_window, 0)
    CMD0( ' ', KEY_CTRL('t'), "bufed-toggle_selection", list_toggle_selection)
    CMD0( KEY_F8, KEY_NONE, "bufed-kill-buffer", bufed_kill_buffer)
    CMD_DEF_END,
};

static CmdDef bufed_global_commands[] = {
    CMD0( KEY_CTRLX(KEY_CTRL('b')), KEY_NONE, "list-buffers", do_list_buffers)
    CMD_DEF_END,
};

static int bufed_init(void)
{
    /* inherit from list mode */
    memcpy(&bufed_mode, &list_mode, sizeof(ModeDef));
    bufed_mode.name = "bufed";
    bufed_mode.instance_size = sizeof(BufedState);
    bufed_mode.mode_init = bufed_mode_init;
    bufed_mode.mode_close = bufed_mode_close;
    
    /* first register mode */
    qe_register_mode(&bufed_mode);

    qe_register_cmd_table(bufed_commands, "bufed");
    qe_register_cmd_table(bufed_global_commands, NULL);

    return 0;
}

qe_module_init(bufed_init);
