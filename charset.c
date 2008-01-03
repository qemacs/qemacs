/*
 * Basic Charset functions for QEmacs
 *
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard.
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

QECharset *first_charset;

/* specific tables */
static unsigned short table_idem[256];
static unsigned short table_utf8[256];

unsigned char utf8_length[256];

static const unsigned int utf8_min_code[7] = {
    0, 0, 0x80, 0x800, 0x10000, 0x00200000, 0x04000000,
};

static const unsigned char utf8_first_code_mask[7] = {
    0, 0, 0x1f, 0xf, 0x7, 0x3, 0x1,
};

/********************************************************/
/* 8859-1 */

static void decode_8859_1_init(CharsetDecodeState *s)
{
    s->table = table_idem;
}

static unsigned char *encode_8859_1(__unused__ QECharset *charset,
                                    unsigned char *p, int c)
{
    if (c <= 0xff) {
        *p++ = c;
        return p;
    } else {
        return NULL;
    }
}

QECharset charset_8859_1 = {
    "8859-1",
    "ISO-8859-1|iso-ir-100|latin1|l1|819",
    decode_8859_1_init,
    NULL,
    encode_8859_1,
    0, 10, 0, 0, NULL, NULL,
};

/********************************************************/
/* vt100 */

static void decode_vt100_init(CharsetDecodeState *s)
{
    s->table = table_idem;
}

static unsigned char *encode_vt100(__unused__ QECharset *charset,
                                   unsigned char *p, int c)
{
    if (c <= 0xff) {
        *p++ = c;
        return p;
    } else {
        return NULL;
    }
}

QECharset charset_vt100 = {
    "vt100",
    NULL,
    decode_vt100_init,
    NULL,
    encode_vt100,
    0, 10, 0, 0, NULL, NULL,
};

/********************************************************/
/* 7 bit */

static unsigned char *encode_7bit(__unused__ QECharset *charset,
                                  unsigned char *p, int c)
{
    if (c <= 0x7f) {
        *p++ = c;
        return p;
    } else {
        return NULL;
    }
}

static QECharset charset_7bit = {
    "7bit",
    "us-ascii|ascii|7-bit|iso-ir-6|ANSI_X3.4|646",
    decode_8859_1_init,
    NULL,
    encode_7bit,
    0, 10, 0, 0, NULL, NULL,
};

/********************************************************/
/* UTF8 */

/* return the utf8 char and increment 'p' of at least one char. strict
   decoding is done (refuse non canonical UTF8) */
int utf8_decode(const char **pp)
{
    unsigned int c, c1;
    const unsigned char *p;
    int i, l;

    p = *(const unsigned char**)pp;
    c = *p++;
    if (c < 128) {
        /* fast case for ASCII */
    } else {
        l = utf8_length[c];
        if (l == 1)
            goto fail; /* can only be multi byte code here */
        c = c & utf8_first_code_mask[l];
        for (i = 1; i < l; i++) {
            c1 = *p;
            if (c1 < 0x80 || c1 >= 0xc0)
                goto fail;
            p++;
            c = (c << 6) | (c1 & 0x3f);
        }
        if (c < utf8_min_code[l])
            goto fail;
        /* exclude surrogate pairs and special codes */
        if ((c >= 0xd800 && c <= 0xdfff) ||
                c == 0xfffe || c == 0xffff)
            goto fail;
    }
    *(const unsigned char**)pp = p;
    return c;
 fail:
    *(const unsigned char**)pp = p;
    return INVALID_CHAR;
}

/* NOTE: the buffer must be at least 6 bytes long. Return number of
 * bytes copied. */
int utf8_encode(char *q0, int c)
{
    char *q = q0;

    if (c < 0x80) {
        *q++ = c;
    } else {
        if (c < 0x800) {
            *q++ = (c >> 6) | 0xc0;
        } else {
            if (c < 0x10000) {
                *q++ = (c >> 12) | 0xe0;
            } else {
                if (c < 0x00200000) {
                    *q++ = (c >> 18) | 0xf0;
                } else {
                    if (c < 0x04000000) {
                        *q++ = (c >> 24) | 0xf8;
                    } else {
                        *q++ = (c >> 30) | 0xfc;
                        *q++ = ((c >> 24) & 0x3f) | 0x80;
                    }
                    *q++ = ((c >> 18) & 0x3f) | 0x80;
                }
                *q++ = ((c >> 12) & 0x3f) | 0x80;
            }
            *q++ = ((c >> 6) & 0x3f) | 0x80;
        }
        *q++ = (c & 0x3f) | 0x80;
    }
    return q - q0;
}

int utf8_to_unicode(unsigned int *dest, int dest_length,
                    const char *str)
{
    const char *p;
    unsigned int *uq, *uq_end, c;

    if (dest_length <= 0)
        return 0;

    p = str;
    uq = dest;
    uq_end = dest + dest_length - 1;
    for (;;) {
        if (uq >= uq_end)
            break;
        c = utf8_decode(&p);
        if (c == '\0')
            break;
        *uq++ = c;
    }
    *uq = 0;
    return uq - dest;
}

static void decode_utf8_init(CharsetDecodeState *s)
{
    s->table = table_utf8;
}

static int decode_utf8_func(__unused__ CharsetDecodeState *s,
                            const unsigned char **pp)
{
    return utf8_decode((const char **)(void *)pp);
}

static unsigned char *encode_utf8(__unused__ QECharset *charset,
                                  unsigned char *q, int c)
{
    return q + utf8_encode((char*)q, c);
}

QECharset charset_utf8 = {
    "utf-8",
    "utf8",
    decode_utf8_init,
    decode_utf8_func,
    encode_utf8,
    0, 10, 0, 0, NULL, NULL,
};

/********************************************************/
/* generic charset functions */

void qe_register_charset(QECharset *charset)
{
    QECharset **pp;

    pp = &first_charset;
    while (*pp != NULL) {
        if (*pp == charset)
            return;
        pp = &(*pp)->next;
    }
    *pp = charset;
}

void charset_completion(StringArray *cs, const char *input)
{
    QECharset *charset;
    char name[32];
    const char *p, *q;

    for (charset = first_charset; charset != NULL; charset = charset->next) {
        if (strxstart(charset->name, input, NULL))
            add_string(cs, charset->name);
        for (q = p = charset->aliases;; q++) {
            if (*q == '\0' || *q == '|') {
                if (q > p) {
                    pstrncpy(name, sizeof(name), p, q - p);
                    if (strxstart(name, input, NULL))
                        add_string(cs, name);
                }
                if (*q == '\0')
                    break;
                p = ++q;
            }
        }
    }
}

QECharset *find_charset(const char *name)
{
    QECharset *charset;

    for (charset = first_charset; charset != NULL; charset = charset->next) {
        if (!strxcmp(charset->name, name)
        ||  strcasefind(charset->aliases, name)) {
            return charset;
        }
    }
    return NULL;
}

void charset_decode_init(CharsetDecodeState *s, QECharset *charset)
{
    unsigned short *table;

    s->table = NULL; /* fail safe */
    if (charset->table_alloc) {
        table = qe_malloc_array(unsigned short, 256);
        if (!table) {
            charset = &charset_8859_1;
        } else {
            s->table = table;
        }
    }
    s->charset = charset;
    s->decode_func = charset->decode_func;
    if (charset->decode_init)
        charset->decode_init(s);
}

void charset_decode_close(CharsetDecodeState *s)
{
    if (s->charset->table_alloc &&
        s->charset != &charset_8859_1)
        qe_free(&s->table);
    /* safety */
    memset(s, 0, sizeof(CharsetDecodeState));
}

/* detect the charset. Actually only UTF8 is detected */
QECharset *detect_charset(const unsigned char *buf, int size)
{
    int i, l, c, has_utf8;

    has_utf8 = 0;
    for (i = 0; i < size;) {
        c = buf[i++];
        if ((c >= 0x80 && c < 0xc0) || c >= 0xfe)
            goto no_utf8;
        l = utf8_length[c];
        while (l > 1) {
            has_utf8 = 1;
            if (i >= size)
                break;
            c = buf[i++];
            if (!(c >= 0x80 && c < 0xc0)) {
            no_utf8:
                has_utf8 = 0;
                break;
            }
            l--;
        }
    }
    if (has_utf8)
        return &charset_utf8;
    else
        return &charset_8859_1;
}

/* the function uses '?' to indicate that no match could be found in
   current charset */
int unicode_to_charset(char *buf, unsigned int c, QECharset *charset)
{
    char *q;

    q = (char *)charset->encode_func(charset, (unsigned char*)buf, c);
    if (!q) {
        q = buf;
        *q++ = '?';
    }
    *q = '\0';
    return q - buf;
}

/********************************************************/
/* 8 bit charsets */

void decode_8bit_init(CharsetDecodeState *s)
{
    QECharset *charset = s->charset;
    unsigned short *table;
    int i, n;

    table = s->table;
    for (i = 0; i < charset->min_char; i++)
        *table++ = i;
    n = charset->max_char - charset->min_char + 1;
    for (i = 0; i < n; i++)
        *table++ = charset->private_table[i];
    for (i = charset->max_char + 1; i < 256; i++)
        *table++ = i;
}

/* not very fast, but not critical yet */
unsigned char *encode_8bit(QECharset *charset, unsigned char *q, int c)
{
    int i, n;
    const unsigned short *table;

    if (c < charset->min_char) {
        /* nothing to do */
    } else
    if (c > charset->max_char && c <= 0xff) {
        /* nothing to do */
    } else {
        n = charset->max_char - charset->min_char + 1;
        table = charset->private_table;
        for (i = 0; i < n; i++) {
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

void charset_init(void)
{
    int l, i, n;

    // could set utf8_length[128...0xc0] to 0 as invalid bytes
    memset(utf8_length, 1, 256);

    i = 0xc0;
    l = 2;
    while (l <= 6) {
        n = utf8_first_code_mask[l] + 1;
        while (n > 0) {
            utf8_length[i++] = l;
            n--;
        }
        l++;
    }

    for (i = 0; i < 256; i++)
        table_idem[i] = i;

    /* utf8 table */
    for (i = 0; i < 256; i++)
        table_utf8[i] = INVALID_CHAR;
    for (i = 0; i < 0x80; i++)
        table_utf8[i] = i;
    for (i = 0xc0; i < 0xfe; i++)
        table_utf8[i] = ESCAPE_CHAR;

    qe_register_charset(&charset_8859_1);
    qe_register_charset(&charset_vt100);
    qe_register_charset(&charset_utf8);
    qe_register_charset(&charset_7bit);
}
