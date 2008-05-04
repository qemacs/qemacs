/*
 * Basic Charset functions for QEmacs
 *
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard.
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

QECharset *first_charset;

/* specific tables */
static unsigned short table_idem[256];
static unsigned short table_utf8[256];
static unsigned short table_none[256];

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

static u8 *encode_8859_1(__unused__ QECharset *charset, u8 *p, int c)
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
    decode_8bit,
    encode_8859_1,
    charset_get_pos_8bit,
    charset_get_chars_8bit,
    charset_goto_char_8bit,
    charset_goto_line_8bit,
    1, 0, 0, 10, 0, 0, NULL, NULL,
};

/********************************************************/
/* vt100 */

static void decode_vt100_init(CharsetDecodeState *s)
{
    s->table = table_idem;
}

static u8 *encode_vt100(__unused__ QECharset *charset, u8 *p, int c)
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
    decode_8bit,
    encode_vt100,
    charset_get_pos_8bit,
    charset_get_chars_8bit,
    charset_goto_char_8bit,
    charset_goto_line_8bit,
    1, 0, 0, 10, 0, 0, NULL, NULL,
};

/********************************************************/
/* 7 bit */

static u8 *encode_7bit(__unused__ QECharset *charset, u8 *p, int c)
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
    decode_8bit,
    encode_7bit,
    charset_get_pos_8bit,
    charset_get_chars_8bit,
    charset_goto_char_8bit,
    charset_goto_line_8bit,
    1, 0, 0, 10, 0, 0, NULL, NULL,
};

/********************************************************/
/* UTF8 */

/* return the utf8 char and increment 'p' of at least one char. strict
   decoding is done (refuse non canonical UTF8) */
int utf8_decode(const char **pp)
{
    unsigned int c, c1;
    const u8 *p;
    int i, l;

    p = *(const u8 **)pp;
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
    *(const u8 **)pp = p;
    return c;
 fail:
    *(const u8 **)pp = p;
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

int utf8_to_unicode(unsigned int *dest, int dest_length, const char *str)
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

static int decode_utf8_func(CharsetDecodeState *s)
{
    return utf8_decode((const char **)(void *)&s->p);
}

static u8 *encode_utf8(__unused__ QECharset *charset, u8 *q, int c)
{
    return q + utf8_encode((char*)q, c);
}

/* return the number of lines and column position for a buffer */
static void charset_get_pos_utf8(CharsetDecodeState *s, const u8 *buf, int size,
                                 int *line_ptr, int *col_ptr)
{
    const u8 *p, *p1, *lp;
    int nl, line, col;

    QASSERT(size >= 0);

    line = 0;
    lp = p = buf;
    p1 = p + size;
    nl = s->charset->eol_char;

    for (;;) {
        p = memchr(p, nl, p1 - p);
        if (!p)
            break;
        p++;
        lp = p;
        line++;
    }
    /* now compute number of chars (XXX: potential problem if out of
     * block, but for UTF8 it works) */
    col = 0;
    while (lp < p1) {
        col++;
        lp += utf8_length[*lp];
    }
    *line_ptr = line;
    *col_ptr = col;
}

static int charset_get_chars_utf8(QECharset *charset, const u8 *buf, int size)
{
    int nb_chars, c;
    const u8 *buf_end, *buf_ptr;

    nb_chars = 0;
    buf_ptr = buf;
    buf_end = buf + size;
    while (buf_ptr < buf_end) {
        c = *buf_ptr++;
        if (c < 0x80 || c >= 0xc0)
            nb_chars++;
    }
    return nb_chars;
}

static int charset_goto_char_utf8(QECharset *charset, const u8 *buf, int size, int pos)
{
    int nb_chars, c;
    const u8 *buf_ptr, *buf_end;

    nb_chars = 0;
    buf_ptr = buf;
    buf_end = buf + size;
    while (buf_ptr < buf_end) {
        c = *buf_ptr;
        if (c < 0x80 || c >= 0xc0) {
            /* Test done here to skip initial trailing bytes if any */
            if (nb_chars >= pos)
                break;
            nb_chars++;
        }
        buf_ptr++;
    }
    return buf_ptr - buf;
}

QECharset charset_utf8 = {
    "utf-8",
    "utf8",
    decode_utf8_init,
    decode_utf8_func,
    encode_utf8,
    charset_get_pos_utf8,
    charset_get_chars_utf8,
    charset_goto_char_utf8,
    charset_goto_line_8bit,
    1, 1, 0, 10, 0, 0, NULL, NULL,
};

/********************************************************/
/* UCS2/UCS4 */

static void decode_ucs_init(CharsetDecodeState *s)
{
    s->table = table_none;
}

static int decode_ucs2le(CharsetDecodeState *s)
{
    const u8 *p;

    p = s->p;
    s->p += 2;
    return p[0] + (p[1] << 8);
}

static u8 *encode_ucs2le(__unused__ QECharset *charset, u8 *p, int c)
{
    p[0] = c;
    p[1] = c >> 8;
    return p + 2;
}

/* return the number of lines and column position for a buffer */
static void charset_get_pos_ucs2(CharsetDecodeState *s, const u8 *buf, int size,
                                 int *line_ptr, int *col_ptr)
{
    const uint16_t *p, *p1, *lp;
    uint16_t nl;
    union { uint16_t n; char c[2]; } u;
    int line, col;

    line = 0;
    lp = p = (const uint16_t *)buf;
    p1 = p + (size >> 1);
    u.n = 0;
    u.c[s->charset == &charset_ucs2be] = s->charset->eol_char;
    nl = u.n;

    for (; p < p1; p++) {
        if (*p == nl) {
            lp = p;
            line++;
        }
    }
    col = p1 - lp;
    *line_ptr = line;
    *col_ptr = col;
}

static int charset_goto_line_ucs2(QECharset *charset, const u8 *buf, int size,
                                  int nlines)
{
    const uint16_t *p, *p1, *lp;
    uint16_t nl;
    union { uint16_t n; char c[2]; } u;

    lp = p = (const uint16_t *)buf;
    p1 = p + (size >> 1);
    u.n = 0;
    u.c[charset == &charset_ucs2be] = charset->eol_char;
    nl = u.n;

    while (nlines > 0) {
        while (p < p1) {
            if (*p++ == nl) {
                lp = p;
                nlines--;
                break;
            }
        }
    }
    return (u8 *)lp - buf;
}

static int decode_ucs2be(CharsetDecodeState *s)
{
    const u8 *p;

    p = s->p;
    s->p += 2;
    return p[1] + (p[0] << 8);
}

static u8 *encode_ucs2be(__unused__ QECharset *charset, u8 *p, int c)
{
    p[0] = c >> 8;
    p[1] = c;
    return p + 2;
}

static int charset_get_chars_ucs2(__unused__ QECharset *charset, const u8 *buf, int size)
{
    return size >> 1;
}

static int charset_goto_char_ucs2(__unused__ QECharset *charset, const u8 *buf, int size, int pos)
{
    return min(pos << 1, size);
}

QECharset charset_ucs2le = {
    "ucs2le",
    "ucs2|utf16|utf16le|utf-16|utf-16le",
    decode_ucs_init,
    decode_ucs2le,
    encode_ucs2le,
    charset_get_pos_ucs2,
    charset_get_chars_ucs2,
    charset_goto_char_ucs2,
    charset_goto_line_ucs2,
    2, 0, 0, 10, 0, 0, NULL, NULL,
};

QECharset charset_ucs2be = {
    "ucs2be",
    "utf16be|utf-16be",
    decode_ucs_init,
    decode_ucs2be,
    encode_ucs2be,
    charset_get_pos_ucs2,
    charset_get_chars_ucs2,
    charset_goto_char_ucs2,
    charset_goto_line_ucs2,
    2, 0, 0, 10, 0, 0, NULL, NULL,
};

static int decode_ucs4le(CharsetDecodeState *s)
{
    const u8 *p;

    p = s->p;
    s->p += 4;
    return p[0] + (p[1] << 8) + (p[2] << 16) + (p[3] << 24);
}

static u8 *encode_ucs4le(__unused__ QECharset *charset, u8 *p, int c)
{
    p[0] = c;
    p[1] = c >> 8;
    p[2] = c >> 16;
    p[3] = c >> 24;
    return p + 4;
}

/* return the number of lines and column position for a buffer */
static void charset_get_pos_ucs4(CharsetDecodeState *s, const u8 *buf, int size,
                                 int *line_ptr, int *col_ptr)
{
    const uint32_t *p, *p1, *lp;
    uint32_t nl;
    union { uint32_t n; char c[4]; } u;
    int line, col;

    line = 0;
    lp = p = (const uint32_t *)buf;
    p1 = p + (size >> 2);
    u.n = 0;
    u.c[(s->charset == &charset_ucs4be) * 3] = s->charset->eol_char;
    nl = u.n;

    for (; p < p1; p++) {
        if (*p == nl) {
            lp = p;
            line++;
        }
    }
    col = p1 - lp;
    *line_ptr = line;
    *col_ptr = col;
}

static int charset_goto_line_ucs4(QECharset *charset, const u8 *buf, int size,
                                  int nlines)
{
    uint32_t *p, *p1, *lp;
    uint32_t nl;
    union { uint32_t n; char c[2]; } u;

    lp = p = (uint32_t *)buf;
    p1 = p + (size >> 2);
    u.n = 0;
    u.c[(charset == &charset_ucs2be) * 3] = charset->eol_char;
    nl = u.n;

    while (nlines > 0) {
        while (p < p1) {
            if (*p++ == nl) {
                lp = p;
                nlines--;
                break;
            }
        }
    }
    return (u8 *)lp - buf;
}

static int decode_ucs4be(CharsetDecodeState *s)
{
    const u8 *p;

    p = s->p;
    s->p += 4;
    return (p[0] << 24) + (p[1] << 16) + (p[2] << 8) + p[3];
}

static u8 *encode_ucs4be(__unused__ QECharset *charset, u8 *p, int c)
{
    p[0] = c >> 24;
    p[1] = c >> 16;
    p[2] = c >> 8;
    p[3] = c;
    return p + 4;
}

static int charset_get_chars_ucs4(__unused__ QECharset *charset, const u8 *buf, int size)
{
    return size >> 2;
}

static int charset_goto_char_ucs4(__unused__ QECharset *charset, const u8 *buf, int size, int pos)
{
    return min(pos << 2, size);
}

QECharset charset_ucs4le = {
    "ucs4le",
    "ucs4|utf32|utf32le|utf-32|utf-32le",
    decode_ucs_init,
    decode_ucs4le,
    encode_ucs4le,
    charset_get_pos_ucs4,
    charset_get_chars_ucs4,
    charset_goto_char_ucs4,
    charset_goto_line_ucs4,
    4, 0, 0, 10, 0, 0, NULL, NULL,
};

QECharset charset_ucs4be = {
    "ucs4be",
    "utf32be|utf-32be",
    decode_ucs_init,
    decode_ucs4be,
    encode_ucs4be,
    charset_get_pos_ucs4,
    charset_get_chars_ucs4,
    charset_goto_char_ucs4,
    charset_goto_line_ucs4,
    4, 0, 0, 10, 0, 0, NULL, NULL,
};

/********************************************************/
/* generic charset functions */

void qe_register_charset(QECharset *charset)
{
    QECharset **pp;

    if (!charset->aliases)
        charset->aliases = "";

    pp = &first_charset;
    while (*pp != NULL) {
        if (*pp == charset)
            return;
        pp = &(*pp)->next;
    }
    *pp = charset;
}

void charset_completion(CompleteState *cp)
{
    QECharset *charset;
    char name[32];
    const char *p, *q;

    for (charset = first_charset; charset != NULL; charset = charset->next) {
        if (strxstart(charset->name, cp->current, NULL))
            add_string(&cp->cs, charset->name);
        if (charset->aliases) {
            for (q = p = charset->aliases;; q++) {
                if (*q == '\0' || *q == '|') {
                    if (q > p) {
                        pstrncpy(name, sizeof(name), p, q - p);
                        if (strxstart(name, cp->current, NULL))
                            add_string(&cp->cs, name);
                    }
                    if (*q == '\0')
                        break;
                    p = q + 1;
                }
            }
        }
    }
}

QECharset *find_charset(const char *name)
{
    QECharset *charset;

    if (!name)
        return NULL;

    for (charset = first_charset; charset != NULL; charset = charset->next) {
        if (!strxcmp(charset->name, name)
        ||  strxfind(charset->aliases, name)) {
            return charset;
        }
    }
    return NULL;
}

void charset_decode_init(CharsetDecodeState *s, QECharset *charset)
{
    s->table = NULL; /* fail safe */
    if (charset->table_alloc) {
        s->table = qe_malloc_array(unsigned short, 256);
        if (!s->table) {
            charset = &charset_8859_1;
        }
    }
    s->charset = charset;
    s->char_size = charset->char_size;
    s->decode_func = charset->decode_func;
    s->get_pos_func = charset->get_pos_func;
    if (charset->decode_init)
        charset->decode_init(s);
}

void charset_decode_close(CharsetDecodeState *s)
{
    if (s->charset->table_alloc)
        qe_free(&s->table);
    /* safety */
    memset(s, 0, sizeof(CharsetDecodeState));
}

/* detect the charset. Actually only UTF8 is detected */
QECharset *detect_charset(const u8 *buf, int size)
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

    /* Check for zwnbsp BOM: files starting with zero-width
     * non-breaking space as a byte-order mark (BOM) will be detected
     * as ucs2 or ucs4 encoded.
     */
    if (size >= 2 && buf[0] == 0xff && buf[1] == 0xfe) {
        if (size >= 4 && buf[2] == 0 && buf[3] == 0)
            return &charset_ucs4le;
        return &charset_ucs2le;
    }

    if (size >= 2 && buf[0] == 0xfe && buf[1] == 0xff)
        return &charset_ucs2be;

    if (size >= 4
    &&  buf[0] == 0 && buf[1] == 0 && buf[2] == 0xfe && buf[3] == 0xff) {
        return &charset_ucs4be;
    }

#if 0
    {
        /* Need a more reliable generic ucs2/ucs4 detection */
        int maxc[4];
        
        maxc[0] = maxc[1] = maxc[2] = maxc[3] = 0;
        for (i = 0; i < size; i += 2) {
            if (buf[i] > maxc[i & 3])
                maxc[i & 3] = buf[i];
        }
        if (maxc[0] > 'a' && maxc[1] < 0x2f && maxc[2] > 'a' && maxc[3] < 0x2f)
            return &charset_ucs2le;

        if (maxc[1] > 'a' && maxc[0] < 0x2f && maxc[3] > 'a' && maxc[2] < 0x2f)
            return &charset_ucs2be;
    }
#endif
    /* CG: should use a state variable for default charset */
    return &charset_8859_1;
}

/* the function uses '?' to indicate that no match could be found in
   current charset */
int unicode_to_charset(char *buf, unsigned int c, QECharset *charset)
{
    char *q;

    q = (char *)charset->encode_func(charset, (u8 *)buf, c);
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

int decode_8bit(CharsetDecodeState *s)
{
    return s->table[*(s->p)++];
}

/* not very fast, but not critical yet */
u8 *encode_8bit(QECharset *charset, u8 *q, int c)
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

/* return the number of lines and column position for a buffer */
void charset_get_pos_8bit(CharsetDecodeState *s, const u8 *buf, int size,
                          int *line_ptr, int *col_ptr)
{
    const u8 *p, *p1, *lp;
    int nl, line, col;

    QASSERT(size >= 0);

    line = 0;
    lp = p = buf;
    p1 = p + size;
    nl = s->charset->eol_char;

    for (;;) {
        p = memchr(p, nl, p1 - p);
        if (!p)
            break;
        p++;
        lp = p;
        line++;
    }
    col = p1 - lp;
    *line_ptr = line;
    *col_ptr = col;
}

int charset_goto_line_8bit(QECharset *charset, const u8 *buf, int size, int nlines)
{
    const u8 *p, *p1, *lp;
    int nl;

    lp = p = buf;
    p1 = p + size;
    nl = charset->eol_char;

    while (nlines > 0) {
        p = memchr(p, nl, p1 - p);
        if (!p)
            break;
        p++;
        lp = p;
        nlines--;
    }
    return lp - buf;
}

int charset_get_chars_8bit(QECharset *charset, const u8 *buf, int size)
{
    return size;
}

int charset_goto_char_8bit(QECharset *charset, const u8 *buf, int size, int pos)
{
    return min(pos, size);
}

/********************************************************/

void charset_init(void)
{
    int l, i, n;

    for (i = 0; i < 256; i++) {
        table_idem[i] = i;
        table_none[i] = ESCAPE_CHAR;
    }

    /* utf8 tables */

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
        table_utf8[i] = INVALID_CHAR;
    for (i = 0; i < 0x80; i++)
        table_utf8[i] = i;
    for (i = 0xc0; i < 0xfe; i++)
        table_utf8[i] = ESCAPE_CHAR;

    qe_register_charset(&charset_8859_1);
    qe_register_charset(&charset_vt100);
    qe_register_charset(&charset_utf8);
    qe_register_charset(&charset_7bit);
    qe_register_charset(&charset_ucs2le);
    qe_register_charset(&charset_ucs2be);
}
