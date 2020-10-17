/*
 * List mode for QEmacs.
 *
 * Copyright (c) 2001-2002 Fabrice Bellard.
 * Copyright (c) 2002-2020 Charlie Gordon.
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
    int offset, offset1;
    int ch, flags;

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
    eb_replace_uchar(s->b, offset, ch);
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
        s->offset -= 1;
    s->offset = eb_goto_bol(s->b, s->offset);
}

static int list_init(void)
{
    memcpy(&list_mode, &text_mode, sizeof(ModeDef));
    list_mode.name = "list";
    list_mode.mode_probe = NULL;
    list_mode.mode_init = list_mode_init;
    list_mode.display_hook = list_display_hook;
    qe_register_mode(&list_mode, MODEF_NOCMD | MODEF_VIEW);
    return 0;
}

qe_module_init(list_init);
