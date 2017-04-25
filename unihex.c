/*
 * Unicode Hexadecimal mode for QEmacs.
 *
 * Copyright (c) 2000-2001 Fabrice Bellard.
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
    UNIHEX_STYLE_OFFSET = QE_STYLE_COMMENT,
    UNIHEX_STYLE_DUMP   = QE_STYLE_FUNCTION,
};

static int unihex_mode_init(EditState *s, EditBuffer *b, int flags)
{
    if (s) {
        int c, maxc, offset, max_offset, w;

        /* unihex mode is incompatible with EOL_DOS eol type */
        eb_set_charset(s->b, s->b->charset, EOL_UNIX);

        /* Compute max width of character in hex dump (limit to first 64K) */
        maxc = 0xFFFF;
        max_offset = min(65536, s->b->total_size);
        for (offset = 0; offset < max_offset;) {
            c = eb_nextc(s->b, offset, &offset);
            maxc = max(maxc, c);
        }

        s->hex_mode = 1;
        s->hex_nibble = 0;
        s->unihex_mode = w = snprintf(NULL, 0, "%x", maxc);
        s->dump_width = clamp((s->width - 8 - 2 - 2 - 1) / (w + 3), 8, 16);
        s->insert = 0;
        /* XXX: should come from mode.default_wrap */
        s->wrap = WRAP_TRUNCATE;
    }
    return 0;
}

static int unihex_to_disp(int c)
{
    /* Prevent display of C0 and C1 control codes and invalid code points */
    if (c < ' ' || c == 127 || (c >= 128 && c < 160)
    ||  (c >= 0xD800 && c <= 0xDFFF) || c > 0x10FFFF)
        c = '.';
    return c;
}

static int unihex_backward_offset(EditState *s, int offset)
{
    int pos;

    /* CG: beware: offset may fall inside a character */
    pos = eb_get_char_offset(s->b, offset);
    pos = align(pos, s->dump_width);
    return eb_goto_char(s->b, pos);
}

static int unihex_display_line(EditState *s, DisplayState *ds, int offset)
{
    int j, len, ateof, dump_width;
    int offset1, offset2;
    int c, maxc;
    unsigned int b;
    /* CG: array size is incorrect, should be smaller */
    unsigned int buf[LINE_MAX_SIZE];
    unsigned int pos[LINE_MAX_SIZE];

    display_bol(ds);

    ds->style = UNIHEX_STYLE_OFFSET;
    display_printf(ds, -1, -1, "%08x ", offset);
    //int charpos = eb_get_char_offset(s->b, offset);
    //display_printf(ds, -1, -1, "%08x ", charpos);
    //display_printf(ds, -1, -1, "%08x %08x ", charpos, offset);

    dump_width = min(LINE_MAX_SIZE - 1, s->dump_width);
    ateof = 0;
    len = 0;
    maxc = 0;
    for (j = 0; j < dump_width && offset < s->b->total_size; j++) {
        pos[len] = offset;
        buf[len] = c = eb_nextc(s->b, offset, &offset);
        if (maxc < c)
            maxc = c;
        len++;
    }
    pos[len] = offset;

    /* adjust s->unihex_mode if a larger character has been found */
    while (s->unihex_mode < 8 && maxc >> (s->unihex_mode * 4) != 0)
        s->unihex_mode++;

    ds->style = UNIHEX_STYLE_DUMP;

    for (j = 0; j < dump_width; j++) {
        display_char(ds, -1, -1, ' ');
        offset1 = pos[j];
        offset2 = pos[j + 1];
        if (j < len) {
            display_printhex(ds, offset1, offset2, buf[j], s->unihex_mode);
            //ds->cur_hex_mode = 1;
            //display_printf(ds, offset1, offset2, "%0*x", s->unihex_mode, buf[j]);
            //ds->cur_hex_mode = 0;
        } else {
            if (!ateof) {
                ateof = 1;
                offset2 = offset1 + 1;
            } else {
                offset2 = offset1 = -1;
            }
            ds->cur_hex_mode = s->hex_mode;
            display_printf(ds, offset1, offset2, "%*c", s->unihex_mode, ' ');
            ds->cur_hex_mode = 0;
        }
        if ((j & 7) == 7)
            display_char(ds, -1, -1, ' ');
    }
    display_char(ds, -1, -1, ' ');

    ds->style = 0;

    display_char(ds, -1, -1, ' ');

    ateof = 0;
    for (j = 0; j < dump_width; j++) {
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
        if (qe_isaccent(b)) {
            /* insert space to make accent stand on its own */
            display_char(ds, offset1, offset2, ' ');
            display_char(ds, -1, -1, b);
        } else {
            display_char(ds, offset1, offset2, b);
        }
        /* spacing out single width glyphs may be less readable */
        if (unicode_tty_glyph_width(b) < 2) {
            display_char(ds, -1, -1, ' ');
        }
    }
    display_eol(ds, -1, -1);

    if (len >= dump_width)
        return offset;
    else
        return -1;
}

static void unihex_move_bol(EditState *s)
{
    int pos;

    pos = eb_get_char_offset(s->b, s->offset);
    pos = align(pos, s->dump_width);
    s->offset = eb_goto_char(s->b, pos);
}

static void unihex_move_eol(EditState *s)
{
    int pos;

    pos = eb_get_char_offset(s->b, s->offset);

    /* CG: should include the last character! */
    pos = align(pos, s->dump_width) + s->dump_width - 1;

    s->offset = eb_goto_char(s->b, pos);
}

static void unihex_move_left_right(EditState *s, int dir)
{
    if (dir > 0) {
        s->offset = eb_next(s->b, s->offset);
    } else {
        s->offset = eb_prev(s->b, s->offset);
    }
}

static void unihex_move_up_down(EditState *s, int dir)
{
    int pos;

    pos = eb_get_char_offset(s->b, s->offset);

    pos += dir * s->dump_width;

    s->offset = eb_goto_char(s->b, pos);
}

static void unihex_mode_line(EditState *s, buf_t *out)
{
    basic_mode_line(s, out, '-');
    buf_printf(out, "0x%x--0x%x--%s",
               eb_get_char_offset(s->b, s->offset),
               s->offset, s->b->charset->name);
    buf_printf(out, "--%d%%", compute_percent(s->offset, s->b->total_size));
}

static int unihex_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    /* XXX: should check for non 8 bit characters */
    /* XXX: should auto-detect if content has non ASCII utf8 contents */
    return 1;
}

static ModeDef unihex_mode = {
    .name = "unihex",
    .mode_probe = unihex_mode_probe,
    .mode_init = unihex_mode_init,
    .display_line = unihex_display_line,
    .backward_offset = unihex_backward_offset,
    .move_up_down = unihex_move_up_down,
    .move_left_right = unihex_move_left_right,
    .move_bol = unihex_move_bol,
    .move_eol = unihex_move_eol,
    .move_bof = text_move_bof,
    .move_eof = text_move_eof,
    .scroll_up_down = text_scroll_up_down,
    .mouse_goto = text_mouse_goto,
    .write_char = hex_write_char,
    .get_mode_line = unihex_mode_line,
};


static int unihex_init(void)
{
    /* first register mode(s) */
    qe_register_mode(&unihex_mode, MODEF_VIEW);

    /* additional mode specific keys */
    qe_register_binding(KEY_CTRL_LEFT, "decrease-width", &unihex_mode);
    qe_register_binding(KEY_CTRL_RIGHT, "increase-width", &unihex_mode);
    qe_register_binding(KEY_TAB, "toggle-hex", &unihex_mode);
    qe_register_binding(KEY_SHIFT_TAB, "toggle-hex", &unihex_mode);
    return 0;
}

qe_module_init(unihex_init);
