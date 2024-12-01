/*
 * MPEG mode for QEmacs.
 *
 * Copyright (c) 2001 Fabrice Bellard.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qe.h"

#define SEQ_END_CODE            0x000001b7
#define SEQ_START_CODE          0x000001b3
#define GOP_START_CODE          0x000001b8
#define PICTURE_START_CODE      0x00000100
#define SLICE_MIN_START_CODE    0x00000101
#define SLICE_MAX_START_CODE    0x000001af
#define EXT_START_CODE          0x000001b5
#define USER_START_CODE         0x000001b2

#define PACK_START_CODE             0x000001ba
#define SYSTEM_HEADER_START_CODE    0x000001bb
#define ISO_11172_END_CODE          0x000001b9

static int mpeg_display_line(EditState *s, DisplayState *ds, int offset)
{
    unsigned int startcode;
    int ret, badchars, offset_start;
    unsigned char buf[4];

    /* search start code */

    badchars = 0;

    display_bol(ds);
    display_printf(ds, -1, -1, "%08x:", offset);
    for (;;) {
        ret = eb_read(s->b, offset, buf, 4);
        if (ret == 0) {
            if (badchars)
                display_eol(ds, -1, -1);
            goto the_end;
        }

        if (ret == 4) {
            startcode = (buf[0] << 24) | (buf[1] << 16) | (buf[2]  << 8) | buf[3];
            if ((startcode & 0xffffff00) == 0x00000100) {
                if (badchars) {
                    display_eol(ds, -1, -1);
                    display_bol(ds);
                    display_printf(ds, -1, -1, "%08x:", offset);
                }
                break;
            }
        }
        /* display unknown chars */
        display_printf(ds, -1, -1, " [");
        display_printhex(ds, offset, offset + 1, buf[0], 2);
        display_printf(ds, -1, -1, "]");
        offset++;
        if (++badchars == 8) {
            badchars = 0;
            display_eol(ds, -1, -1);
            goto the_end;
        }
    }
    offset_start = offset;
    offset += 4;
    display_printf(ds, offset_start, offset, " [%08x] ", startcode);

    switch (startcode) {
    case SEQ_END_CODE:
        display_printf(ds, -1, -1, "SEQ_END");
        break;
    case SEQ_START_CODE:
        display_printf(ds, -1, -1, "SEQUENCE");
        break;
    case PICTURE_START_CODE:
        display_printf(ds, -1, -1, "PICTURE");
        break;
    case GOP_START_CODE:
        display_printf(ds, -1, -1, "GOP");
        break;
    case EXT_START_CODE:
        display_printf(ds, -1, -1, "EXT");
        break;
    case PACK_START_CODE:
        display_printf(ds, -1, -1, "PACK");
        break;
    case SYSTEM_HEADER_START_CODE:
        display_printf(ds, -1, -1, "SYSTEM");
        break;
    default:
        if (startcode >= SLICE_MIN_START_CODE &&
            startcode <= SLICE_MAX_START_CODE) {
            display_printf(ds, -1, -1, "SLICE %d", startcode & 0xff);
        } else {
            display_printf(ds, -1, -1, "UNKNOWN", startcode);
        }
        break;
    }

    display_eol(ds, -1, -1);
 the_end:
    return offset;
}

/* go to previous synchronization point */
static int mpeg_backward_offset(EditState *s, int offset)
{
    unsigned char buf[4];
    unsigned int startcode;
    int ret;

    for (;;) {
        if (offset <= 0)
            break;
        ret = eb_read(s->b, offset, buf, 4);
        if (ret != 4)
            break;
        startcode = (buf[0] << 24) | (buf[1] << 16) | (buf[2]  << 8) | buf[3];
        if ((startcode & 0xffffff00) == 0x00000100) {
            break;
        }
        offset--;
    }
    return offset;
}

static int mpeg_mode_init(EditState *s, EditBuffer *b, int flags)
{
    if (s) {
        s->hex_mode = 1;
        s->hex_nibble = 0;
        /* XXX: should come from mode.default_wrap */
        s->wrap = WRAP_TRUNCATE;
    }
    return 0;
}

static int mpeg_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (p->buf_size >= 4 &&
        p->buf[0] == 0x00 &&
        p->buf[1] == 0x00 &&
        p->buf[2] == 0x01 &&
        p->buf[3] >= 0xa0)
        return 100;
    else
        return 0;
}

static ModeDef mpeg_mode = {
    .name = "mpeg",
    .mode_probe = mpeg_mode_probe,
    .mode_init = mpeg_mode_init,
    .display_line = mpeg_display_line,
    .backward_offset = mpeg_backward_offset,
    .write_char = hex_write_char,
};

static int mpeg_init(QEmacsState *qs)
{
    qe_register_mode(qs, &mpeg_mode, MODEF_MAJOR | MODEF_VIEW);
    return 0;
}

qe_module_init(mpeg_init);
