/*
 * Hexadecimal modes for QEmacs.
 * Copyright (c) 2000, 2001 Fabrice Bellard.
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

extern ModeDef hex_mode;

static int to_disp(int c)
{
    if (c < ' ' || c >= 127)
        c = '.';
    return c;
}

static int hex_backward_offset(EditState *s, int offset)
{
    return align(offset, s->disp_width);
}

static int hex_display(EditState *s, DisplayState *ds, int offset)
{
    int j, len, eof;
    int offset1;
    unsigned char b;

    display_bol(ds);

    display_printf(ds, -1, -1, "%08x ", offset);
    eof = 0;
    len = s->b->total_size - offset;
    if (len > s->disp_width)
        len = s->disp_width;
    if (s->mode == &hex_mode) {
        for(j=0;j<s->disp_width;j++) {
            display_char(ds, -1, -1, ' ');
            offset1 = offset + j;
            if (j < len) {
                eb_read(s->b, offset + j, &b, 1);
                display_printhex(ds, offset1, offset1 + 1, b, 2);
            } else {
                if (!eof) {
                    eof = 1;
                } else {
                    offset1 = -2;
                }
                display_printf(ds, offset1, offset1 + 1, "  ");
            }
            if ((j & 7)== 7)
                display_char(ds, -1, -1, ' ');
        }
        display_char(ds, -1, -1, ' ');
        display_char(ds, -1, -1, ' ');
    }
    eof = 0;
    for(j=0;j<s->disp_width;j++) {
        offset1 = offset + j;
        if (j < len) {
            eb_read(s->b, offset + j, &b, 1);
        } else {
            b = ' ';
            if (!eof) {
                eof = 1;
            } else {
                offset1 = -2;
            }
        }
        display_char(ds, offset1, offset1 + 1, to_disp(b));
    }
    offset += len;
    display_eol(ds, -1, -1);

    if (len >= s->disp_width)
        return offset;
    else
        return -1;
}

void do_goto_byte(EditState *s, int offset)
{
    if (offset < 0 || offset >= s->b->total_size)
        return;
    s->offset = offset;
}

void do_set_width(EditState *s, int w)
{
    if (w >= 1) {
        s->disp_width = w;
        s->offset_top = s->mode->text_backward_offset(s, s->offset_top);
    }
}

void do_incr_width(EditState *s, int incr)
{
    int w;
    w = s->disp_width + incr;
    if (w >= 1)
        do_set_width(s, w);
}


void do_toggle_hex(EditState *s)
{
    s->hex_mode = !s->hex_mode;
}

/* specific hex commands */
static CmdDef hex_commands[] = {
    CMD1( KEY_NONE, KEY_NONE, "decrease-width", do_incr_width, -1)
    CMD1( KEY_NONE, KEY_NONE, "increase-width", do_incr_width, 1)
    CMD( KEY_NONE, KEY_NONE, "set-width\0i{Width: }", do_set_width)
    CMD( KEY_NONE, KEY_NONE, "goto-byte\0i{Goto byte: }", do_goto_byte)
    CMD0( KEY_NONE, KEY_NONE, "toggle-hex", do_toggle_hex)
    CMD_DEF_END,
};

static int ascii_mode_init(EditState *s, ModeSavedData *saved_data)
{
    QEFont *font;
    QEStyleDef style;
    int num_width;
    int ret;

    ret = text_mode_init(s, saved_data);
    if (ret)
        return ret;

    /* get typical number width */
    get_style(s, &style, s->default_style);
    font = select_font(s->screen, style.font_style, style.font_size);
    num_width = glyph_width(s->screen, font, '0');

    s->disp_width = (s->screen->width / num_width) - 10;
    s->hex_mode = 0;
    s->wrap = WRAP_TRUNCATE;
    return 0;
}

static int hex_mode_init(EditState *s, ModeSavedData *saved_data)
{
    int ret;

    ret = text_mode_init(s, saved_data);
    if (ret)
        return ret;
    s->disp_width = 16;
    s->hex_mode = 1;
    s->unihex_mode = 0;
    s->hex_nibble = 0;
    s->wrap = WRAP_TRUNCATE;
    return 0;
}

int detect_binary(const unsigned char *buf, int size)
{
    int i, c;

    for (i = 0; i < size; i++) {
        c = buf[i];
	if (c < 32 && 
            (c != '\r' && c != '\n' && c != '\t' && c != '\e' && c!= '\b'))
	    return 1;
    }
    return 0;
}

static int hex_mode_probe(ModeProbeData *p)
{
    if (detect_binary(p->buf, p->buf_size))
        return 50;
    else
        return 0;
}

void hex_move_bol(EditState *s)
{
    s->offset = align(s->offset, s->disp_width);
}

void hex_move_eol(EditState *s)
{
    s->offset = align(s->offset, s->disp_width) + s->disp_width - 1;
    if (s->offset >= s->b->total_size)
        s->offset = s->b->total_size;
}

void hex_move_left_right(EditState *s, int dir)
{
    s->offset += dir;
    if (s->offset < 0)
        s->offset = 0;
    else if (s->offset > s->b->total_size)
        s->offset = s->b->total_size;
}

void hex_move_up_down(EditState *s, int dir)
{
    s->offset += dir * s->disp_width;
    if (s->offset < 0)
        s->offset = 0;
    else if (s->offset > s->b->total_size)
        s->offset = s->b->total_size;
}

void hex_write_char(EditState *s, int key)
{
    unsigned int cur_ch, ch;
    int hsize, shift, cur_len, len, h;
    char buf[10];
    
    if (s->hex_mode) {
        if (s->unihex_mode)
            hsize = 4;
        else
            hsize = 2;
        h = to_hex(key);
        if (h < 0)
            return;
        if (s->insert && s->hex_nibble == 0) {
            ch = h << ((hsize - 1) * 4);
            if (s->unihex_mode) {
                len = unicode_to_charset(buf, ch, s->b->charset);
            } else {
                len = 1;
                buf[0] = ch;
            }
            eb_insert(s->b, s->offset, buf, len);
        } else {
            if (s->unihex_mode) {
                cur_ch = eb_nextc(s->b, s->offset, &cur_len);
                cur_len -= s->offset;
            } else {
                eb_read(s->b, s->offset, buf, 1);
                cur_ch = buf[0];
                cur_len = 1;
            }

            shift = (hsize - s->hex_nibble - 1) * 4;
            ch = (cur_ch & ~(0xf << shift)) | (h << shift);

            if (s->unihex_mode) {
                len = unicode_to_charset(buf, ch, s->b->charset);
            } else {
                len = 1;
                buf[0] = ch;
            }

            if (cur_len == len) {
                eb_write(s->b, s->offset, buf, len);
            } else {
                eb_delete(s->b, s->offset, cur_len);
                eb_insert(s->b, s->offset, buf, len);
            }
        }
        if (++s->hex_nibble == hsize) {
            s->hex_nibble = 0;
            if (s->offset < s->b->total_size)
                s->offset += len;
        }
    } else {
        text_write_char(s, key);
    }
}

void hex_mode_line(EditState *s, char *buf, int buf_size)
{
    char *q;
    int percent;

    basic_mode_line(s, buf, buf_size, '-');
    q = buf + strlen(buf);
    q += sprintf(q, "0x%x--0x%x", 
                 s->offset, s->b->total_size);
    percent = 0;
    if (s->b->total_size > 0)
        percent = (s->offset * 100) / s->b->total_size;
    q += sprintf(q, "--%d%%", percent);
}

ModeDef ascii_mode = { 
    "ascii", 
    instance_size: 0,
    mode_probe: NULL,
    mode_init: ascii_mode_init, 
    mode_close: text_mode_close,
    text_display: hex_display, 
    text_backward_offset: hex_backward_offset,
    
    move_up_down: hex_move_up_down,
    move_left_right: hex_move_left_right,
    move_bol: hex_move_bol,
    move_eol: hex_move_eol,
    scroll_up_down: text_scroll_up_down,
    write_char: text_write_char,
    mouse_goto: text_mouse_goto,
    mode_line: hex_mode_line,
};

ModeDef hex_mode = {
    "hex", 
    instance_size: 0,
    mode_probe: hex_mode_probe,
    mode_init: hex_mode_init, 
    mode_close: text_mode_close,
    text_display: hex_display, 
    text_backward_offset: hex_backward_offset,

    move_up_down: hex_move_up_down,
    move_left_right: hex_move_left_right,
    move_bol: hex_move_bol,
    move_eol: hex_move_eol,
    scroll_up_down: text_scroll_up_down,
    write_char: hex_write_char,
    mouse_goto: text_mouse_goto,
    mode_line: hex_mode_line,
};

static int hex_init(void)
{
    /* first register mode(s) */
    qe_register_mode(&ascii_mode);
    qe_register_mode(&hex_mode);

    /* commands and default keys */
    qe_register_cmd_table(hex_commands, NULL);

    /* additionnal mode specific keys */
    qe_register_binding(KEY_CTRL_LEFT, "decrease-width", "ascii|hex");
    qe_register_binding(KEY_CTRL_RIGHT, "increase-width", "ascii|hex");
    qe_register_binding(KEY_TAB, "toggle-hex", "hex");
    qe_register_binding(KEY_SHIFT_TAB, "toggle-hex", "hex");
    
    return 0;
}

qe_module_init(hex_init);
