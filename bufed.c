/*
 * Buffer editor mode for QEmacs.
 *
 * Copyright (c) 2001-2002 Fabrice Bellard.
 * Copyright (c) 2002-2017 Charlie Gordon.
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

static void build_bufed_list(BufedState *bs, EditState *s)
{
    QEmacsState *qs = s->qe_state;
    EditBuffer *b;
    StringItem *item;
    int last_index = list_get_pos(s);
    int i;

    free_strings(&bs->items);
    for (b = qs->first_buffer; b != NULL; b = b->next) {
        if (!(b->flags & BF_SYSTEM) || (bs->flags & BUFED_ALL_VISIBLE)) {
            item = add_string(&bs->items, b->name, 0);
            item->opaque = b;
        }
    }
    bs->last_index = -1;

    /* build buffer */
    b = s->b;
    eb_clear(b);

    for (i = 0; i < bs->items.nb_items; i++) {
        EditBuffer *b1;
        char flags[4];
        char *flagp = flags;
        int len, style0;

        item = bs->items.items[i];
        b1 = check_buffer((EditBuffer**)&item->opaque);
        style0 = (b1->flags & BF_SYSTEM) ? BUFED_STYLE_SYSTEM : 0;

        if (i == last_index) {
            s->offset = b->total_size;
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
            } else
            if (b1->syntax_mode) {
                mode_name = b1->syntax_mode->name;
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
            if (b1->flags & BF_DIRED)
                b->cur_style = BUFED_STYLE_DIRECTORY;
            else
                b->cur_style = BUFED_STYLE_FILENAME;
            eb_printf(b, "%s",
                      make_user_path(path, sizeof(path), b1->filename));
            b->cur_style = style0;
        }
        eb_printf(b, "\n");
    }
    b->modified = 0;
    b->flags |= BF_READONLY;
}

static EditBuffer *bufed_get_buffer(BufedState *bs, EditState *s)
{
    int index;

    index = list_get_pos(s);
    if (index < 0 || index >= bs->items.nb_items)
        return NULL;

    return check_buffer((EditBuffer**)&bs->items.items[index]->opaque);
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
        b = check_buffer(&bs->cur_buffer);
        last_buffer = check_buffer(&bs->last_buffer);
    } else {
        index = list_get_pos(s);
        if (index < 0 || index >= bs->items.nb_items)
            return;

        if (temp > 0 && index == bs->last_index)
            return;

        b = check_buffer((EditBuffer**)&bs->items.items[index]->opaque);
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
        bs->last_index = index;
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
    EditBuffer *b = check_buffer((EditBuffer**)&item->opaque);

    /* XXX: avoid killing buffer list by mistake */
    if (b && b != s->b) {
        /* Give the user a chance to confirm if buffer is modified */
        do_kill_buffer(s, item->str, 0);
    }
}

static void bufed_kill_buffer(EditState *s)
{
    BufedState *bs;

    if (!(bs = bufed_get_state(s, 1)))
        return;

    string_selection_iterate(&bs->items, list_get_pos(s),
                             bufed_kill_item, s);
    build_bufed_list(bs, s);
}

/* show a list of buffers */
static void do_list_buffers(EditState *s, int argval)
{
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
        s->qe_state->active_window = s;
    }

    b = eb_scratch("*bufed*", BF_READONLY | BF_SYSTEM | BF_UTF8 | BF_STYLE1);
    if (!b)
        return;

    e = show_popup(s, b);
    if (!e)
        return;

    edit_set_mode(e, &bufed_mode);

    if (!(bs = bufed_get_state(e, 1)))
        return;

    bs->last_index = -1;
    bs->cur_window = s;
    bs->cur_buffer = s->b;
    bs->last_buffer = s->last_buffer;

    if (argval == NO_ARG) {
        bs->flags &= ~BUFED_ALL_VISIBLE;
    } else {
        bs->flags |= BUFED_ALL_VISIBLE;
    }
    build_bufed_list(bs, e);

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

    b = bufed_get_buffer(bs, s);
    if (!b)
        return;

    b->modified = 0;
    build_bufed_list(bs, s);
}

static void bufed_toggle_read_only(EditState *s)
{
    BufedState *bs;
    EditBuffer *b;

    if (!(bs = bufed_get_state(s, 1)))
        return;

    b = bufed_get_buffer(bs, s);
    if (!b)
        return;

    b->flags ^= BF_READONLY;
    build_bufed_list(bs, s);
}

static void bufed_refresh(EditState *s, int toggle)
{
    BufedState *bs;

    if (!(bs = bufed_get_state(s, 1)))
        return;

    if (toggle)
        bs->flags ^= BUFED_ALL_VISIBLE;

    build_bufed_list(bs, s);
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
    bufed_mode.buffer_instance_size = sizeof(BufedState);
    bufed_mode.mode_init = bufed_mode_init;
    bufed_mode.mode_free = bufed_mode_free;
    bufed_mode.display_hook = bufed_display_hook;

    qe_register_mode(&bufed_mode, MODEF_VIEW);
    qe_register_cmd_table(bufed_commands, &bufed_mode);
    qe_register_cmd_table(bufed_global_commands, NULL);

    return 0;
}

qe_module_init(bufed_init);
