/*
 * Unicode Hexadecimal mode for QEmacs.
 *
 * Copyright (c) 2000-2001 Fabrice Bellard.
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

static int unihex_to_disp(int c)
{
    /* Do not allow characters in range 127-160 to show as graphics
     * nor characters beyond the BMP plane
     */
    if (c < ' ' || c == 127 || (c >= 128 && c < 160) || c > 0xFFFF)
        c = '.';
    return c;
}

static int unihex_backward_offset(EditState *s, int offset)
{
    int pos;

    /* CG: beware: offset may fall inside a character */
    pos = eb_get_char_offset(s->b, offset);
    pos = align(pos, s->disp_width);
    return eb_goto_char(s->b, pos);
}

static int unihex_display(EditState *s, DisplayState *ds, int offset)
{
    int j, len, ateof, disp_width;
    int offset1, offset2, charpos;
    unsigned int b;
    /* CG: array size is incorrect, should be smaller */
    unsigned int buf[LINE_MAX_SIZE];
    unsigned int pos[LINE_MAX_SIZE];

    display_bol(ds);

    ds->style = QE_STYLE_COMMENT;
    charpos = eb_get_char_offset(s->b, offset);
    display_printf(ds, -1, -1, "%08x ", charpos);
    //display_printf(ds, -1, -1, "%08x %08x ", charpos, offset);

    disp_width = min(LINE_MAX_SIZE - 1, s->disp_width);
    ateof = 0;
    len = 0;
    for (j = 0; j < disp_width && offset < s->b->total_size; j++) {
        pos[len] = offset;
        buf[len] = eb_nextc(s->b, offset, &offset);
        len++;
    }
    pos[len] = offset;

    ds->style = QE_STYLE_FUNCTION;

    for (j = 0; j < disp_width; j++) {
        display_char(ds, -1, -1, ' ');
        offset1 = pos[j];
        offset2 = pos[j + 1];
        if (j < len) {
            if (buf[j] < 0x10000) {
                display_printhex(ds, offset1, offset2, buf[j], 4);
            } else {
                ds->cur_hex_mode = 1;
                display_printf(ds, offset1, offset2, "%x", buf[j]);
                ds->cur_hex_mode = 0;
            }
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
    for (j = 0; j < disp_width; j++) {
        offset1 = pos[j];
        offset2 = pos[j + 1];
        if (j < len) {
            b = buf[j];
            b = unihex_to_disp(b);
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
#if 0
        /* CG: spacing out single width glyphs is less readable */
        if (unicode_glyph_tty_width(b) == 1)
            display_char(ds, -1, -1, ' ');
#endif
    }
    display_eol(ds, -1, -1);

    if (len >= disp_width)
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

static int unihex_mode_line(EditState *s, char *buf, int buf_size)
{
    int percent, pos, cpos;

    cpos = eb_get_char_offset(s->b, s->offset);

    pos = basic_mode_line(s, buf, buf_size, '-');
    pos += snprintf(buf + pos, buf_size - pos, "0x%x--0x%x--%s",
                    cpos, s->offset, s->b->charset->name);
    percent = 0;
    if (s->b->total_size > 0)
        percent = (s->offset * 100) / s->b->total_size;
    pos += snprintf(buf + pos, buf_size - pos, "--%d%%", percent);
    return pos;
}

static ModeDef unihex_mode = {
    .name = "unihex",
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
    .get_mode_line = unihex_mode_line,
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
