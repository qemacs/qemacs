/*
 * JIS Charset handling for QEmacs
 *
 * Copyright (c) 2002 Fabrice Bellard.
 * Copyright (c) 2002-2024 Charlie Gordon.
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

#include "charsetjis.def"

static char32_t jis0208_decode(int b1, int b2) {
    b1 -= 0x21;
    b2 -= 0x21;
    if (b1 > 83)
        return 0;
    if (b1 < 8) {
        /* do nothing */
    } else
    if (b1 <= 14) {
        return 0;
    } else {
        b1 -= 7;
    }
    return table_jis208[b1 * 94 + b2];
}

static char32_t jis0212_decode(int b1, int b2) {
    b1 -= 0x21;
    b2 -= 0x21;

    if (b1 > 76)
        return 0;

    switch (b1) {
    case 0:
    case 2:
    case 3:
    case 4:
    case 7:
    case 11:
    case 12:
    case 13:
    case 14:
        return 0;
    case 1:
        b1 = 0;
        break;
    case 5:
    case 6:
        b1 = b1 - 5 + 1;
        break;
    case 8:
    case 9:
    case 10:
        b1 = b1 - 8 + 3;
        break;
    default:
        b1 = b1 - 15 + 6;
        break;
    }
    return table_jis212[b1 * 94 + b2];
}

static void decode_euc_jp_init(CharsetDecodeState *s)
{
    /* XXX: should use static table instead of removing const qualifier */
    unsigned short *table = unconst(unsigned short *)s->table;
    int i;

    for (i = 0; i < 256; i++)
        table[i] = i;
    table[0x8e] = ESCAPE_CHAR;
    table[0x8f] = ESCAPE_CHAR;
    for (i = 0xa1; i <= 0xfe; i++)
        table[i] = ESCAPE_CHAR;
}

/* XXX: add state */
static char32_t decode_euc_jp_func(CharsetDecodeState *s) {
    const unsigned char *p;
    char32_t c, c2;

    p = s->p;
    c = *p++;
    if (c == 0x8e) {
        c = *p;
        if (c >= 0xa1 && c <= 0xdf) {
            /* 2 byte sequence for HALFWIDTH KANA FF61..FF9F */
            c = c - 0xa1 + 0xff61;
            p++;
        }
    } else
    if (c >= 0xa1) {
        c2 = *p;
        if (c2 >= 0xa1 && c2 <= 0xfe) {
            /* 2 byte sequence for 77x94 KANJI block */
            c2 = jis0208_decode(c & 0x7f, c2 & 0x7f);
            if (c2) {
                c = c2;
                p++;
            }
        }
    } else {
        /* 8f case */
        if (p[0] >= 0xa1 && p[0] <= 0xfe &&
            p[1] >= 0xa1 && p[1] <= 0xfe) {
            /* 3 byte sequence for 68x94 KANJI block */
            c2 = jis0212_decode(p[0] & 0x7f, p[1] & 0x7f);
            if (c2) {
                c = c2;
                p += 2;
            }
        }
    }
    s->p = p;
    return c;
}

static unsigned char *encode_euc_jp(qe__unused__ QECharset *s, u8 *q, char32_t c) {
    if (c <= 0x7f) {
        *q++ = c;
    } else
    if (c >= 0xff61 && c <= 0xff9f) {
        *q++ = 0x8e;
        *q++ = c - 0xff61 + 0xa1;
    } else {
        /* XXX: do it */
        return NULL;
    }
    return q;
}

static struct QECharset charset_euc_jp = {
    "euc-jp",
    NULL,
    NULL,
    decode_euc_jp_init,
    decode_euc_jp_func,
    encode_euc_jp,
    charset_get_pos_8bit,
    charset_get_chars_8bit,
    charset_goto_char_8bit,
    charset_goto_line_8bit,
    .char_size = 1,
    .variable_size = 1,
    .table_alloc = 1,
    .eol_char = 10,
};


static void decode_sjis_init(CharsetDecodeState *s)
{
    /* XXX: should use static table instead of removing const qualifier */
    unsigned short *table = unconst(unsigned short *)s->table;
    int i;

    for (i = 0; i < 256; i++)
        table[i] = i;
    table['\\'] = 0x00a5;
    table[0x80] = '\\';
    for (i = 0x81; i <= 0x9f; i++)
        table[i] = ESCAPE_CHAR;
    for (i = 0xa1; i <= 0xdf; i++)
        table[i] = i - 0xa1 + 0xff61;
    for (i = 0xe0; i <= 0xef; i++)
        table[i] = ESCAPE_CHAR;
    for (i = 0xf0; i <= 0xfc; i++)
        table[i] = ESCAPE_CHAR;
    table[0xfd] = 0xa9;
    table[0xfe] = 0x2122;
    table[0xff] = 0x2026;
}

/* XXX: add state */
static char32_t decode_sjis_func(CharsetDecodeState *s) {
    const unsigned char *p;
    char32_t c, c1, c2, adjust, row, col;

    p = s->p;
    c = *p++;
    if (c >= 0xf0) {
        /* user data */
    } else {
        c1 = *p;
        if ((c1 >= 0x40 && c1 <= 0x7e) || (c1 >= 0x80 && c1 <= 0xfc)) {
            row = c < 0xa0 ? 0x70 : 0xb0;
            adjust = (c1 < 0x9f);
            col = adjust ? (c1 >= 0x80 ? 32 : 31) : 0x7e;
            c2 = jis0208_decode(((c - row) << 1) - adjust, c1 - col);
            if (c2) {
                c = c2;
                p++;
            }
        }
    }
    s->p = p;
    return c;
}

static unsigned char *encode_sjis(qe__unused__ QECharset *s, u8 *q, char32_t c) {
    if (c <= 0x7f) {
        *q++ = c;
    } else {
        /* XXX: do it */
        return NULL;
    }
    return q;
}

static struct QECharset charset_sjis = {
    "sjis",
    NULL,
    NULL,
    decode_sjis_init,
    decode_sjis_func,
    encode_sjis,
    charset_get_pos_8bit,
    charset_get_chars_8bit,
    charset_goto_char_8bit,
    charset_goto_line_8bit,
    .char_size = 1,
    .variable_size = 1,
    .table_alloc = 1,
    .eol_char = 10,
};

int qe_charset_jis_init(QEmacsState *qs)
{
    qe_register_charset(qs, &charset_sjis);
    qe_register_charset(qs, &charset_euc_jp);

    return 0;
}

qe_module_init(qe_charset_jis_init);
