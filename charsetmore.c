/*
 * More Charsets for QEmacs
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

/********************************************************/
/* 8 bit charsets */

extern QECharset charset_8859_2;
extern QECharset charset_cp1125;
extern QECharset charset_cp737;
extern QECharset charset_koi8_r;
extern QECharset charset_8859_4;
extern QECharset charset_cp1250;
extern QECharset charset_cp850;
extern QECharset charset_koi8_u;
extern QECharset charset_viscii;
extern QECharset charset_8859_13;
extern QECharset charset_8859_5;
extern QECharset charset_cp1251;
extern QECharset charset_cp852;
extern QECharset charset_mac_lat2;
extern QECharset charset_8859_15;
extern QECharset charset_8859_7;
extern QECharset charset_cp1257;
extern QECharset charset_cp866;
extern QECharset charset_macroman;
extern QECharset charset_8859_16;
extern QECharset charset_8859_9;
extern QECharset charset_cp437;
extern QECharset charset_kamen;
extern QECharset charset_tcvn5712;

void decode_8bit_init(CharsetDecodeState *s)
{
    QECharset *charset = s->charset;
    unsigned short *table;
    int i, n;

    table = s->table;
    for(i=0;i<charset->min_char;i++)
        *table++ = i;
    n = charset->max_char - charset->min_char + 1;
    for(i=0;i<n;i++)
        *table++ = charset->private_table[i];
    for(i=charset->max_char + 1;i<256;i++)
        *table++ = i;
}

/* not very fast, but not critical yet */
unsigned char *encode_8bit(QECharset *charset, unsigned char *q, int c)
{
    int i, n;
    const unsigned short *table;

    if (c < charset->min_char) {
        /* nothing to do */
    } else if (c > charset->max_char && c <= 0xff) {
        /* nothing to do */
    } else {
        n = charset->max_char - charset->min_char + 1;
        table = charset->private_table;
        for(i=0;i<n;i++) {
            if (table[i] == c)
                goto found;
        }
        return NULL;
    found:
        c = charset->min_char + i;
    }
    *q++ = c;
    return q;
}

/********************************************************/
/* JIS */

extern const unsigned short table_jis208[];
extern const unsigned short table_jis212[];

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
    switch(b1) {
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
    for(i=0;i<256;i++)
        table[i] = i;
    table[0x8e] = ESCAPE_CHAR;
    table[0x8f] = ESCAPE_CHAR;
    for(i=0xa1;i<=0xfe;i++)
        table[i] = ESCAPE_CHAR;
}

/* XXX: add state */
static int decode_euc_jp_func(CharsetDecodeState *s, const unsigned char **pp)
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

static unsigned char *encode_euc_jp(QECharset *s, unsigned char *q, int c)
{
    if (c <= 0x7f) {
        *q++ = c;
    } else if (c >= 0xff61 && c <= 0xFF9F) {
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
    table_alloc : 1,
};


static void decode_sjis_init(CharsetDecodeState *s)
{
    unsigned short *table = s->table;
    int i;

    for(i=0;i<256;i++)
        table[i] = i;
    table['\\'] = 0x00a5;
    table[0x80] = '\\';
    for(i=0x81;i<=0x9f;i++)
        table[i] = ESCAPE_CHAR;
    for(i=0xa1;i<=0xdf;i++)
        table[i] = i - 0xa1 + 0xff61;
    for(i=0xe0;i<=0xef;i++)
        table[i] = ESCAPE_CHAR;
    for(i=0xf0;i<=0xfc;i++)
        table[i] = ESCAPE_CHAR;
    table[0xfd] = 0xa9;
    table[0xfe] = 0x2122;
    table[0xff] = 0x2026;
}

/* XXX: add state */
static int decode_sjis_func(CharsetDecodeState *s, const unsigned char **pp)
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

static unsigned char *encode_sjis(QECharset *s, unsigned char *q, int c)
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
    table_alloc : 1,
};

int charset_more_init(void)
{
    qe_register_charset(&charset_8859_2);
    qe_register_charset(&charset_cp1125);
    qe_register_charset(&charset_cp737);
    qe_register_charset(&charset_koi8_r);
    qe_register_charset(&charset_8859_4);
    qe_register_charset(&charset_cp1250);
    qe_register_charset(&charset_cp850);
    qe_register_charset(&charset_koi8_u);
    qe_register_charset(&charset_viscii);
    qe_register_charset(&charset_8859_13);
    qe_register_charset(&charset_8859_5);
    qe_register_charset(&charset_cp1251);
    qe_register_charset(&charset_cp852);
    qe_register_charset(&charset_mac_lat2);
    qe_register_charset(&charset_8859_15);
    qe_register_charset(&charset_8859_7);
    qe_register_charset(&charset_cp1257);
    qe_register_charset(&charset_cp866);
    qe_register_charset(&charset_macroman);
    qe_register_charset(&charset_8859_16);
    qe_register_charset(&charset_8859_9);
    qe_register_charset(&charset_cp437);
    qe_register_charset(&charset_kamen);
    qe_register_charset(&charset_tcvn5712);

    qe_register_charset(&charset_sjis);
    qe_register_charset(&charset_euc_jp);
    return 0;
}

qe_module_init(charset_more_init);


