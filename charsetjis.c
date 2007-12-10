/*
 * JIS Charset handling for QEmacs
 *
 * Copyright (c) 2002 Fabrice Bellard.
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

#include "charsetjis.def"

static int jis0208_decode(int b1, int b2)
{
    b1 -= 0x21;
    b2 -= 0x21;
    if (b1 > 83)
        return 0;
    if (b1 < 8) {
        /* do nothing */
    } else if (b1 <= 14) {
        return 0;
    } else {
        b1 -= 7;
    }
    return table_jis208[b1 * 94 + b2];
}

static int jis0212_decode(int b1, int b2)
{
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
    unsigned short *table = s->table;
    int i;

    for (i = 0; i < 256; i++)
        table[i] = i;
    table[0x8e] = ESCAPE_CHAR;
    table[0x8f] = ESCAPE_CHAR;
    for (i = 0xa1; i <= 0xfe; i++)
        table[i] = ESCAPE_CHAR;
}

/* XXX: add state */
static int decode_euc_jp_func(__unused__ CharsetDecodeState *s,
                              const unsigned char **pp)
{
    const unsigned char *p;
    int c, c2;
    
    p = *pp;
    c = *p++;
    if (c == 0x8e) {
        c = *p;
        if (c >= 0xa1 && c <= 0xdf) {
            c = c - 0xa1 + 0xff61;
            p++;
        }
    } else if (c >= 0xa1) {
        c2 = *p;
        if (c2 >= 0xa1 && c2 <= 0xfe) {
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
            c2 = jis0212_decode(p[0] & 0x7f, p[1] & 0x7f);
            if (c2) {
                c = c2;
                p += 2;
            }
        }
    }
    *pp = p;
    return c;
}

static unsigned char *encode_euc_jp(__unused__ QECharset *s,
                                    unsigned char *q, int c)
{
    if (c <= 0x7f) {
        *q++ = c;
    } else if (c >= 0xff61 && c <= 0xff9f) {
        *q++ = 0x8e;
        *q++ = c - 0xff61 + 0xa1;
    } else {
        /* XXX: do it */
        return NULL;
    }
    return q;
}

static QECharset charset_euc_jp = {
    "euc-jp",
    NULL,
    decode_euc_jp_init,
    decode_euc_jp_func,
    encode_euc_jp,
    .table_alloc = 1,
    .eol_char = 10,
};


static void decode_sjis_init(CharsetDecodeState *s)
{
    unsigned short *table = s->table;
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
static int decode_sjis_func(__unused__ CharsetDecodeState *s,
                            const unsigned char **pp)
{
    const unsigned char *p;
    int c, c1, c2, adjust, row, col;
    
    p = *pp;
    c = *p++;
    if (c >= 0xf0) {
        /* user data */
    } else {
        c1 = *p;
        if ((c1 >= 0x40 && c1 <= 0x7e) ||
            (c1 >= 0x80 && c1 <= 0xfc)) {
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
    *pp = p;
    return c;
}

static unsigned char *encode_sjis(__unused__ QECharset *s,
                                  unsigned char *q, int c)
{
    if (c <= 0x7f) {
        *q++ = c;
    } else {
        /* XXX: do it */
        return NULL;
    }
    return q;
}

static QECharset charset_sjis = {
    "sjis",
    NULL,
    decode_sjis_init,
    decode_sjis_func,
    encode_sjis,
    .table_alloc = 1,
    .eol_char = 10,
};

int charset_jis_init(void)
{
    qe_register_charset(&charset_sjis);
    qe_register_charset(&charset_euc_jp);

    return 0;
}

qe_module_init(charset_jis_init);
