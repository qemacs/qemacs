/*
 * Unicode Hexadecimal mode for QEmacs.
 *
 * Copyright (c) 2000, 2001 Fabrice Bellard.
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

static int unihex_mode_init(EditState *s, ModeSavedData *saved_data)
{
    int ret;

    ret = text_mode_init(s, saved_data);
    if (ret)
        return ret;

    s->disp_width = 8;
    s->hex_mode = 1;
    s->unihex_mode = 1;
    s->hex_nibble = 0;
    s->insert = 0;
    s->wrap = WRAP_TRUNCATE;
    return 0;
}

static int to_disp(int c)
{
#if 1
    /* Do not allow characters in range 160-255 to show as graphics */
    if ((c & 127) < ' ' || c == 127)
        c = '.';
#else
    if (c < ' ' || c >= 127)
        c = '.';
#endif
    return c;
}

static int unihex_backward_offset(EditState *s, int offset)
{
    int pos;

    pos = eb_get_char_offset(s->b, offset);
    pos = align(pos, s->disp_width);
    return eb_goto_char(s->b, pos);
}

static int unihex_display(EditState *s, DisplayState *ds, int offset)
{
    int j, len, ateof;
    int offset1, offset2, charpos;
    unsigned int b;
    /* CG: array size is incorrect, should be smaller and should clip
     * disp_width too.
     */
    unsigned int buf[LINE_MAX_SIZE];
    unsigned int pos[LINE_MAX_SIZE];

    display_bol(ds);

    ds->style = QE_STYLE_COMMENT;
    charpos = eb_get_char_offset(s->b, offset);
    display_printf(ds, -1, -1, "%08x %08x ", charpos, offset);

    ateof = 0;
    len = 0;
    for (j = 0; j < s->disp_width; j++) {
        if (offset < s->b->total_size) {
            pos[len] = offset;
            buf[len] = eb_nextc(s->b, offset, &offset);
            len++;
        }
    }
    pos[len] = offset;

    ds->style = QE_STYLE_FUNCTION;

    for (j = 0; j < s->disp_width; j++) {
        display_char(ds, -1, -1, ' ');
        offset1 = pos[j];
        offset2 = pos[j + 1];
        if (j < len) {
            display_printhex(ds, offset1, offset2, buf[j], 4);
        } else {
            if (!ateof) {
                ateof = 1;
                offset2 = offset1 + 1;
            } else {
                offset2 = offset1 = -1;
            }
            ds->cur_hex_mode = s->hex_mode;
            display_printf(ds, offset1, offset2, "    ");
            ds->cur_hex_mode = 0;
        }
        if ((j & 7) == 7)
            display_char(ds, -1, -1, ' ');
    }
    display_char(ds, -1, -1, ' ');

    ds->style = 0;

    display_char(ds, -1, -1, ' ');

    ateof = 0;
    for (j = 0; j < s->disp_width; j++) {
        offset1 = pos[j];
        offset2 = pos[j + 1];
        if (j < len) {
            b = buf[j];
            /* CG: should handle double width glyphs */
            b = to_disp(b);
        } else {
            b = ' ';
            if (!ateof) {
                ateof = 1;
                offset2 = offset1 + 1;
            } else {
                offset2 = offset1 = -1;
            }
        }
        display_char(ds, offset1, offset2, b);
    }
    display_eol(ds, -1, -1);

    if (len >= s->disp_width)
        return offset;
    else
        return -1;
}

static void unihex_move_bol(EditState *s)
{
    int pos;

    pos = eb_get_char_offset(s->b, s->offset);
    pos = align(pos, s->disp_width);
    s->offset = eb_goto_char(s->b, pos);
}

static void unihex_move_eol(EditState *s)
{
    int pos;

    pos = eb_get_char_offset(s->b, s->offset);

    /* CG: should include the last character! */
    pos = align(pos, s->disp_width) + s->disp_width - 1;

    s->offset = eb_goto_char(s->b, pos);
}

static void unihex_move_left_right(EditState *s, int dir)
{
    if (dir > 0) {
        eb_nextc(s->b, s->offset, &s->offset);
    } else {
        eb_prevc(s->b, s->offset, &s->offset);
    }
}

static void unihex_move_up_down(EditState *s, int dir)
{
    int pos;

    pos = eb_get_char_offset(s->b, s->offset);

    pos += dir * s->disp_width;

    s->offset = eb_goto_char(s->b, pos);
}

static ModeDef unihex_mode = {
    "unihex",
    .instance_size = 0,
    .mode_probe = NULL,
    .mode_init = unihex_mode_init,
    .mode_close = text_mode_close,
    .text_display = unihex_display,
    .text_backward_offset = unihex_backward_offset,

    .move_up_down = unihex_move_up_down,
    .move_left_right = unihex_move_left_right,
    .move_bol = unihex_move_bol,
    .move_eol = unihex_move_eol,
    .scroll_up_down = text_scroll_up_down,
    .write_char = hex_write_char,
    .mouse_goto = text_mouse_goto,
};


static int unihex_init(void)
{
    /* first register mode(s) */
    qe_register_mode(&unihex_mode);

    /* additional mode specific keys */
    qe_register_binding(KEY_CTRL_LEFT, "decrease-width", &unihex_mode);
    qe_register_binding(KEY_CTRL_RIGHT, "increase-width", &unihex_mode);
    qe_register_binding(KEY_TAB, "toggle-hex", &unihex_mode);
    qe_register_binding(KEY_SHIFT_TAB, "toggle-hex", &unihex_mode);
    return 0;
}

qe_module_init(unihex_init);
