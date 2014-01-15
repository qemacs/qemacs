/*
 * List mode for QEmacs.
 *
 * Copyright (c) 2001, 2002 Fabrice Bellard.
 * Copyright (c) 2002-2013 Charlie Gordon.
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

static int list_get_colorized_line(EditState *s,
                                   unsigned int *buf, int buf_size,
                                   int *offsetp, __unused__ int line_num)
{
    QEmacsState *qs = s->qe_state;
    int offset, len;

    offset = *offsetp;
    len = eb_get_line(s->b, buf, buf_size, offsetp);

    if (((qs->active_window == s) || s->force_highlight) &&
          s->offset >= offset && s->offset < *offsetp)
    {
        /* highlight the line if the cursor is inside */
        set_color(buf, buf + len, QE_STYLE_HIGHLIGHT);
    } else
    if (buf[0] == '*') {
        /* selection */
        set_color(buf, buf + len, QE_STYLE_SELECTION);
    }
    return len;
}

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
    int line, col;
    eb_get_pos(s->b, &line, &col, s->offset);
    return eb_goto_pos(s->b, line, 0);
}

void list_toggle_selection(EditState *s)
{
    int offset;
    unsigned char ch;

    offset = list_get_offset(s);

    eb_read(s->b, offset , &ch, 1);
    if (ch == ' ')
        ch = '*';
    else
        ch = ' ';
    eb_write(s->b, offset , &ch, 1);

    text_move_up_down(s, 1);
}

static int list_mode_init(EditState *s, __unused__ ModeSavedData *saved_data)
{
    text_mode_init(s, saved_data);

    s->wrap = WRAP_TRUNCATE;
    s->get_colorized_line = list_get_colorized_line;
    return 0;
}

static int list_init(void)
{
    memcpy(&list_mode, &text_mode, sizeof(ModeDef));
    list_mode.name = "list";
    list_mode.mode_probe = NULL;
    list_mode.mode_init = list_mode_init;
    list_mode.mode_flags = MODEF_NOCMD;

    qe_register_mode(&list_mode);

    return 0;
}

qe_module_init(list_init);
