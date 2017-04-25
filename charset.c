/*
 * Basic Charset functions for QEmacs
 *
 * Copyright (c) 2000-2002 Fabrice Bellard.
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

/* XXX: Should move this to QEmacsState, and find a way for html2png */
struct QECharset *first_charset;

/* Unicode utilities */

/* Compute tty width of unicode characters.  This is a modified
 * implementation of wcwidth() from Markus Kuhn. We handle most
 * non spacing and enclosing combining characters.
 */
static unsigned int const unicode_glyph_ranges[] = {
/* width table is produced by unitable.c */
#include "unicode_width.h"
};

static const unsigned int *unicode_glyph_range_index[0x20];

int unicode_tty_glyph_width(unsigned int ucs)
{
    /* Iterative lookup with fast initial jump, no boundary test needed */
    /* Very efficient for BMP and SMP code-points */
    unsigned int const *ip = unicode_glyph_range_index[(ucs >> 12) & 0x1F];

    while (ucs > ip[0]) {
        ip += 2;
    }
    return ip[1];
}

/* utf-8 specific tables */

#define REP2(x)    x, x
#define REP4(x)    x, x, x, x
#define REP8(x)    REP4(x), REP4(x)
#define REP16(x)   REP4(x), REP4(x), REP4(x), REP4(x)
#define REP32(x)   REP16(x), REP16(x)
#define REP64(x)   REP16(x), REP16(x), REP16(x), REP16(x)
#define REP128(x)  REP64(x), REP64(x)
#define REP256(x)  REP64(x), REP64(x), REP64(x), REP64(x)

#define RUN2(x)    (x)+0, (x)+1
#define RUN4(x)    (x)+0, (x)+1, (x)+2, (x)+3
#define RUN8(x)    RUN4(x), RUN4((x)+4)
#define RUN16(x)   RUN4(x), RUN4((x)+4), RUN4((x)+8), RUN4((x)+12)
#define RUN32(x)   RUN16(x), RUN16((x)+16)
#define RUN64(x)   RUN16(x), RUN16((x)+16), RUN16((x)+32), RUN16((x)+48)
#define RUN128(x)  RUN64(x), RUN64((x)+64)
#define RUN256(x)  RUN64(x), RUN64((x)+64), RUN64((x)+128), RUN64((x)+192)

static unsigned short const table_idem[256] = { RUN256(0) };
static unsigned short const table_none[256] = { REP256(ESCAPE_CHAR) };

static unsigned short const table_utf8[256] = {
    RUN128(0),              /* [0x00...0x80] are self-encoding ASCII bytes */
    REP64(INVALID_CHAR),    /* [0x80...0xC0] are invalid prefix bytes */
    REP32(ESCAPE_CHAR),     /* [0xC0...0xE0] leading bytes of 2 byte sequences */
    REP16(ESCAPE_CHAR),     /* [0xE0...0xF0] leading bytes of 3 byte sequences */
    REP8(ESCAPE_CHAR),      /* [0xF0...0xF8] leading bytes of 4 byte sequences */
    REP4(ESCAPE_CHAR),      /* [0xF8...0xFC] leading bytes of 5 byte sequences */
    REP2(ESCAPE_CHAR),      /* [0xFC...0xFE] leading bytes of  byte sequences */
    INVALID_CHAR,           /* 0xFE is invalid in UTF-8 encoding */
    INVALID_CHAR,           /* 0xFF is invalid in UTF-8 encoding */
};

unsigned char const utf8_length[256] = {
    REP128(1),  /* [0x00...0x80] are self-encoding ASCII bytes */
    REP64(1),   /* [0x80...0xC0] are invalid prefix bytes, could use 0 */
    REP32(2),   /* [0xC0...0xE0] leading bytes of 2 byte sequences */
    REP16(3),   /* [0xE0...0xF0] leading bytes of 3 byte sequences */
    REP8(4),    /* [0xF0...0xF8] leading bytes of 4 byte sequences */
    REP4(5),    /* [0xF8...0xFC] leading bytes of 5 byte sequences */
    REP2(6),    /* [0xFC...0xFE] leading bytes of  byte sequences */
    1,          /* 0xFE is invalid in UTF-8 encoding */
    1,          /* 0xFF is invalid in UTF-8 encoding */
};

static unsigned int const utf8_min_code[7] = {
    0, 0, 0x80, 0x800, 0x10000, 0x00200000, 0x04000000,
};

static unsigned char const utf8_first_code_mask[7] = {
    0, 0, 0x1f, 0xf, 0x7, 0x3, 0x1,
};

/********************************************************/
/* raw */

static u8 *encode_raw(qe__unused__ QECharset *charset, u8 *p, int c)
{
    if (c <= 0xff) {
        *p++ = c;
        return p;
    } else {
        return NULL;
    }
}

struct QECharset charset_raw = {
    "raw",
    "binary|none",
    NULL,
    NULL,
    decode_8bit,
    encode_raw,
    charset_get_pos_8bit,
    charset_get_chars_8bit,
    charset_goto_char_8bit,
    charset_goto_line_8bit,
    1, 0, 0, 10, 0, 0, table_idem, NULL, NULL,
};

/********************************************************/
/* 8859-1 */

static int probe_8859_1(qe__unused__ QECharset *charset, const u8 *buf, int size)
{
    const uint32_t magic = (1U << '\b') | (1U << '\t') | (1U << '\f') |
                           (1U << '\n') | (1U << '\r') | (1U << '\033') |
                           (1U << 0x0e) | (1U << 0x0f) | (1U << 0x1a) |
                           (1U << 0x1f);
    const u8 *p = buf;
    const u8 *p_end = p + size;
    uint32_t c;
    int count_spaces, count_lines, count_high;

    count_spaces = count_lines = count_high = 0;

    while (p < p_end) {
        c = p[0];
        p += 1;
        if (c <= 32) {
            if (c == ' ')
                count_spaces++;
            else
            if (c == '\n')
                count_lines++;
            else
            if (!(magic & (1U << c)))
                return 0;
        } else
        if (c < 0x7F) {
            continue;
        } else
        if (c < 0x80) {
            return 0;
        } else {
            count_high++;
        }
    }
    if (count_spaces | count_lines)
        return 1;
    else
        return 0;
}

static u8 *encode_8859_1(qe__unused__ QECharset *charset, u8 *p, int c)
{
    if (c <= 0xff) {
        *p++ = c;
        return p;
    } else {
        return NULL;
    }
}

struct QECharset charset_8859_1 = {
    "8859-1",
    "ISO-8859-1|iso-ir-100|latin1|l1|819",
    probe_8859_1,
    NULL,
    decode_8bit,
    encode_8859_1,
    charset_get_pos_8bit,
    charset_get_chars_8bit,
    charset_goto_char_8bit,
    charset_goto_line_8bit,
    1, 0, 0, 10, 0, 0, table_idem, NULL, NULL,
};

/********************************************************/
/* vt100 */

static u8 *encode_vt100(qe__unused__ QECharset *charset, u8 *p, int c)
{
    if (c <= 0xff) {
        *p++ = c;
        return p;
    } else {
        return NULL;
    }
}

struct QECharset charset_vt100 = {
    "vt100",
    NULL,
    NULL,
    NULL,
    decode_8bit,
    encode_vt100,
    charset_get_pos_8bit,
    charset_get_chars_8bit,
    charset_goto_char_8bit,
    charset_goto_line_8bit,
    1, 0, 0, 10, 0, 0, table_idem, NULL, NULL,
};

/********************************************************/
/* 7 bit */

static u8 *encode_7bit(qe__unused__ QECharset *charset, u8 *p, int c)
{
    if (c <= 0x7f) {
        *p++ = c;
        return p;
    } else {
        return NULL;
    }
}

static struct QECharset charset_7bit = {
    "7bit",
    "us-ascii|ascii|7-bit|iso-ir-6|ANSI_X3.4|646",
    NULL,
    NULL,
    decode_8bit,
    encode_7bit,
    charset_get_pos_8bit,
    charset_get_chars_8bit,
    charset_goto_char_8bit,
    charset_goto_line_8bit,
    1, 0, 0, 10, 0, 0, table_idem, NULL, NULL,
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

char *utf8_char_to_string(char *buf, int c) {
    char *p = buf;
    if (qe_isaccent(c))
        *p++ = ' ';
    p[utf8_encode(p, c)] = '\0';
    return buf;
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

static int probe_utf8(qe__unused__ QECharset *charset, const u8 *buf, int size)
{
    const uint32_t magic = (1U << '\b') | (1U << '\t') | (1U << '\f') |
                           (1U << '\n') | (1U << '\r') | (1U << '\033') |
                           (1U << 0x0e) | (1U << 0x0f) | (1U << 0x1a) |
                           (1U << 0x1f);
    const u8 *p = buf;
    const u8 *p_end = p + size;
    uint32_t c;
    int count_spaces, count_lines, count_utf8;

    count_spaces = count_lines = count_utf8 = 0;

    while (p < p_end) {
        c = p[0];
        p += 1;
        if (c <= 32) {
            if (c == ' ')
                count_spaces++;
            else
            if (c == '\n')
                count_lines++;
            else
            if (!(magic & (1U << c)))
                return 0;
        } else
        if (c < 0x7F) {
            continue;
        } else
        if (c < 0xc0) {
            return 0;
        } else
        if (c < 0xe0) {
            if (p[0] < 0x80 || p[0] > 0xbf)
                return 0;
            count_utf8++;
            p += 1;
        } else
        if (c < 0xf0) {
            if (p[0] < 0x80 || p[0] > 0xbf || p[1] < 0x80 || p[1] > 0xbf)
                return 0;
            count_utf8++;
            p += 2;
        } else
        if (c < 0xf8) {
            if (p[0] < 0x80 || p[0] > 0xbf || p[1] < 0x80 || p[1] > 0xbf
            ||  p[2] < 0x80 || p[2] > 0xbf)
                return 0;
            count_utf8++;
            p += 3;
        } else
        if (c < 0xfc) {
            if (p[0] < 0x80 || p[0] > 0xbf || p[1] < 0x80 || p[1] > 0xbf
            ||  p[2] < 0x80 || p[2] > 0xbf || p[3] < 0x80 || p[3] > 0xbf)
                return 0;
            count_utf8++;
            p += 4;
        } else
        if (c < 0xfe) {
            if (p[0] < 0x80 || p[0] > 0xbf || p[1] < 0x80 || p[1] > 0xbf
            ||  p[2] < 0x80 || p[2] > 0xbf || p[3] < 0x80 || p[3] > 0xbf
            ||  p[4] < 0x80 || p[4] > 0xbf)
                return 0;
            count_utf8++;
            p += 5;
        } else {
            return 0;
        }
    }
    if (count_spaces | count_lines | count_utf8)
        return 1;
    else
        return 0;
}

static int decode_utf8_func(CharsetDecodeState *s)
{
    return utf8_decode((const char **)(void *)&s->p);
}

static u8 *encode_utf8(qe__unused__ QECharset *charset, u8 *q, int c)
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
    nl = s->eol_char;

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

static int charset_get_chars_utf8(CharsetDecodeState *s,
                                  const u8 *buf, int size)
{
    int nb_chars, c;
    const u8 *buf_end, *buf_ptr;

    nb_chars = 0;
    buf_ptr = buf;
    buf_end = buf_ptr + size;
    while (buf_ptr < buf_end) {
        c = *buf_ptr++;
        if (c == '\n' && s->eol_type == EOL_DOS) {
            /* ignore \n in EOL_DOS scan, but count \r.
             * XXX: potentially incorrect if buffer contains
             * \n not preceded by \r and requires special state
             * data to handle \r\n sequence at page boundary.
             */
            continue;
        }
        /* ignoring trailing bytes: will produce incorrect
         * count on isolated and trailing bytes and overlong
         * sequences.
         */
        if (c < 0x80 || c >= 0xc0)
            nb_chars++;
    }
    /* CG: nb_chars is the number of character boundaries, trailing
     * utf-8 sequence at start of buffer is ignored in count while
     * incomplete utf-8 sequence at end of buffer is counted.  This may
     * cause problems when counting characters with eb_get_pos with an
     * offset falling inside a utf-8 sequence, and will produce
     * incorrect counts on broken utf-8 sequences spanning page
     * boundaries.
     */
    return nb_chars;
}

static int charset_goto_char_utf8(CharsetDecodeState *s,
                                  const u8 *buf, int size, int pos)
{
    int nb_chars, c;
    const u8 *buf_ptr, *buf_end;

    nb_chars = 0;
    buf_ptr = buf;
    buf_end = buf_ptr + size;
    for (; buf_ptr < buf_end; buf_ptr++) {
        c = *buf_ptr;
        if (c >= 0x80 && c < 0xc0) {
            /* Test done here to skip initial trailing bytes if any */
            continue;
        }
        if (c == '\n' && s->eol_type == EOL_DOS) {
            /* ignore \n in EOL_DOS scan, but count \r.
             * see comment above.
             */
            continue;
        }
        if (nb_chars >= pos)
            break;
        nb_chars++;
    }
    return buf_ptr - buf;
}

struct QECharset charset_utf8 = {
    "utf-8",
    "utf8|al32utf8",
    probe_utf8,
    NULL,
    decode_utf8_func,
    encode_utf8,
    charset_get_pos_utf8,
    charset_get_chars_utf8,
    charset_goto_char_utf8,
    charset_goto_line_8bit,
    1, 1, 0, 10, 0, 0, table_utf8, NULL, NULL,
};

/********************************************************/
/* UCS2/UCS4 */

static int probe_ucs2le(qe__unused__ QECharset *charset, const u8 *buf, int size)
{
    const uint32_t magic = (1U << '\b') | (1U << '\t') | (1U << '\f') |
                           (1U << '\n') | (1U << '\r') | (1U << '\033') |
                           (1U << 0x0e) | (1U << 0x0f) | (1U << 0x1a) |
                           (1U << 0x1f);
    const u8 *p = buf;
    const u8 *p_end = p + (size & ~1);
    uint32_t c;
    int count_spaces, count_lines;

    if (size & 1)
        return 0;

    count_spaces = count_lines = 0;

    while (p < p_end) {
        c = (p[0] << 0) | (p[1] << 8);
        p += 2;
        if (c <= 32) {
            if (c == ' ')
                count_spaces++;
            else
            if (c == '\n')
                count_lines++;
            else
            if (!(magic & (1U << c)))
                return 0;
        } else
        if (c >= 0x10000) {
            return 0;
        }
    }
    if (count_spaces + count_lines > size / (16 * 2))
        return 1;
    else
        return 0;
}

static int decode_ucs2le(CharsetDecodeState *s)
{
    /* XXX: should handle surrogates */
    const u8 *p;

    p = s->p;
    s->p += 2;
    return p[0] + (p[1] << 8);
}

static u8 *encode_ucs2le(qe__unused__ QECharset *charset, u8 *p, int c)
{
    /* XXX: should handle surrogates */
    p[0] = c;
    p[1] = c >> 8;
    return p + 2;
}

/* return the number of lines and column position for a buffer */
static void charset_get_pos_ucs2(CharsetDecodeState *s, const u8 *buf, int size,
                                 int *line_ptr, int *col_ptr)
{
    const uint16_t *p, *p1, *lp;
    uint16_t nl, lf;
    union { uint16_t n; char c[2]; } u;
    int line, col;

    line = 0;
    lp = p = (const uint16_t *)(const void *)buf;
    p1 = p + (size >> 1);
    u.n = 0;
    u.c[s->charset == &charset_ucs2be] = s->eol_char;
    nl = u.n;
    u.c[s->charset == &charset_ucs2be] = '\n';
    lf = u.n;

    if (s->eol_type == EOL_DOS && p < p1 && *p == lf) {
        /* Skip \n at start of buffer.
         * Should check for pending skip state */
        p++;
        lp++;
    }

    /* XXX: should handle surrogates */
    while (p < p1) {
        if (*p++ == nl) {
            if (s->eol_type == EOL_DOS && p < p1 && *p == lf)
                p++;
            lp = p;
            line++;
        }
    }
    col = p1 - lp;
    *line_ptr = line;
    *col_ptr = col;
}

static int charset_goto_line_ucs2(CharsetDecodeState *s,
                                  const u8 *buf, int size, int nlines)
{
    const uint16_t *p, *p1, *lp;
    uint16_t nl, lf;
    union { uint16_t n; char c[2]; } u;

    lp = p = (const uint16_t *)(const void *)buf;
    p1 = p + (size >> 1);
    u.n = 0;
    u.c[s->charset == &charset_ucs2be] = s->eol_char;
    nl = u.n;
    u.c[s->charset == &charset_ucs2be] = '\n';
    lf = u.n;

    if (s->eol_type == EOL_DOS && p < p1 && *p == lf) {
        /* Skip \n at start of buffer.
         * Should check for pending skip state */
        p++;
        lp++;
    }

    while (nlines > 0 && p < p1) {
        while (p < p1) {
            if (*p++ == nl) {
                if (s->eol_type == EOL_DOS && p < p1 && *p == lf)
                    p++;
                lp = p;
                nlines--;
                break;
            }
        }
    }
    return (const u8 *)lp - buf;
}

static int probe_ucs2be(qe__unused__ QECharset *charset, const u8 *buf, int size)
{
    const uint32_t magic = (1U << '\b') | (1U << '\t') | (1U << '\f') |
                           (1U << '\n') | (1U << '\r') | (1U << '\033') |
                           (1U << 0x0e) | (1U << 0x0f) | (1U << 0x1a) |
                           (1U << 0x1f);
    const u8 *p = buf;
    const u8 *p_end = p + (size & ~1);
    uint32_t c;
    int count_spaces, count_lines;

    if (size & 1)
        return 0;

    count_spaces = count_lines = 0;

    while (p < p_end) {
        c = (p[0] << 8) | (p[1] << 0);
        p += 2;
        if (c <= 32) {
            if (c == ' ')
                count_spaces++;
            else
            if (c == '\n')
                count_lines++;
            else
            if (!(magic & (1U << c)))
                return 0;
        } else
        if (c >= 0x10000) {
            return 0;
        }
    }
    if (count_spaces + count_lines > size / (16 * 2))
        return 1;
    else
        return 0;
}

static int decode_ucs2be(CharsetDecodeState *s)
{
    /* XXX: should handle surrogates */
    const u8 *p;

    p = s->p;
    s->p += 2;
    return (p[0] << 8) + p[1];
}

static u8 *encode_ucs2be(qe__unused__ QECharset *charset, u8 *p, int c)
{
    /* XXX: should handle surrogates */
    p[0] = c >> 8;
    p[1] = c;
    return p + 2;
}

static int charset_get_chars_ucs2(CharsetDecodeState *s,
                                  const u8 *buf, int size)
{
    /* XXX: should handle surrogates */
    int count = size >> 1;  /* convert byte count to char16 count */
    const uint16_t *buf_end, *buf_ptr;
    uint16_t nl;
    union { uint16_t n; char c[2]; } u;

    if (s->eol_type != EOL_DOS)
        return count;

    buf_ptr = (const uint16_t *)(const void *)buf;
    buf_end = buf_ptr + count;
    // XXX: undefined behavior
    u.n = 0;
    u.c[s->charset == &charset_ucs2be] = '\n';
    nl = u.n;

    while (buf_ptr < buf_end) {
        if (*buf_ptr++ == nl) {
            /* ignore \n in EOL_DOS scan, but count \r. (see above) */
            count--;
        }
    }
    return count;
}

static int charset_goto_char_ucs2(CharsetDecodeState *s,
                                  const u8 *buf, int size, int pos)
{
    /* XXX: should handle surrogates */
    int nb_chars;
    const uint16_t *buf_ptr, *buf_end;
    uint16_t nl;
    union { uint16_t n; char c[2]; } u;

    if (s->eol_type != EOL_DOS)
        return min(pos << 1, size);

    nb_chars = 0;
    buf_ptr = (const uint16_t *)(const void *)buf;
    buf_end = buf_ptr + (size >> 1);
    // XXX: undefined behavior
    u.n = 0;
    u.c[s->charset == &charset_ucs2be] = '\n';
    nl = u.n;

    for (; buf_ptr < buf_end; buf_ptr++) {
        if (*buf_ptr == nl) {
            /* ignore \n in EOL_DOS scan, but count \r. (see above) */
            continue;
        }
        if (nb_chars >= pos)
            break;
        nb_chars++;
    }
    return (const u8*)buf_ptr - buf;
}

struct QECharset charset_ucs2le = {
    "ucs2le",
    "utf16le|utf-16le",
    probe_ucs2le,
    NULL,
    decode_ucs2le,
    encode_ucs2le,
    charset_get_pos_ucs2,
    charset_get_chars_ucs2,
    charset_goto_char_ucs2,
    charset_goto_line_ucs2,
    2, 0, 0, 10, 0, 0, table_none, NULL, NULL,
};

struct QECharset charset_ucs2be = {
    "ucs2be",
    "ucs2|utf16|utf-16|utf16be|utf-16be",
    probe_ucs2be,
    NULL,
    decode_ucs2be,
    encode_ucs2be,
    charset_get_pos_ucs2,
    charset_get_chars_ucs2,
    charset_goto_char_ucs2,
    charset_goto_line_ucs2,
    2, 0, 0, 10, 0, 0, table_none, NULL, NULL,
};

static int probe_ucs4le(qe__unused__ QECharset *charset, const u8 *buf, int size)
{
    const uint32_t magic = (1U << '\b') | (1U << '\t') | (1U << '\f') |
                           (1U << '\n') | (1U << '\r') | (1U << '\033') |
                           (1U << 0x0e) | (1U << 0x0f) | (1U << 0x1a) |
                           (1U << 0x1f);
    const u8 *p = buf;
    const u8 *p_end = p + (size & ~3);
    uint32_t c;
    int count_spaces, count_lines;

    if (size & 3)
        return 0;

    count_spaces = count_lines = 0;

    while (p < p_end) {
        c = (p[0] << 0) | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
        p += 4;
        if (c <= 32) {
            if (c == ' ')
                count_spaces++;
            else
            if (c == '\n')
                count_lines++;
            else
            if (!(magic & (1U << c)))
                return 0;
        } else
        if (c > 0x10FFFF) {
            return 0;
        }
    }
    if (count_spaces + count_lines > size / (16 * 4))
        return 1;
    else
        return 0;
}

static int decode_ucs4le(CharsetDecodeState *s)
{
    const u8 *p;

    p = s->p;
    s->p += 4;
    return p[0] + (p[1] << 8) + (p[2] << 16) + (p[3] << 24);
}

static u8 *encode_ucs4le(qe__unused__ QECharset *charset, u8 *p, int c)
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
    uint32_t nl, lf;
    union { uint32_t n; char c[4]; } u;
    int line, col;

    line = 0;
    lp = p = (const uint32_t *)(const void *)buf;
    p1 = p + (size >> 2);
    u.n = 0;
    u.c[(s->charset == &charset_ucs4be) * 3] = s->eol_char;
    nl = u.n;
    u.c[(s->charset == &charset_ucs4be) * 3] = '\n';
    lf = u.n;

    if (s->eol_type == EOL_DOS && p < p1 && *p == lf) {
        /* Skip \n at start of buffer.
         * Should check for pending skip state */
        p++;
        lp++;
    }

    while (p < p1) {
        if (*p++ == nl) {
            if (s->eol_type == EOL_DOS && p < p1 && *p == lf)
                p++;
            lp = p;
            line++;
        }
    }
    col = p1 - lp;
    *line_ptr = line;
    *col_ptr = col;
}

static int charset_goto_line_ucs4(CharsetDecodeState *s,
                                  const u8 *buf, int size, int nlines)
{
    const uint32_t *p, *p1, *lp;
    uint32_t nl, lf;
    union { uint32_t n; char c[4]; } u;

    lp = p = (const uint32_t *)(const void *)buf;
    p1 = p + (size >> 2);
    u.n = 0;
    u.c[(s->charset == &charset_ucs4be) * 3] = s->eol_char;
    nl = u.n;
    u.c[(s->charset == &charset_ucs4be) * 3] = '\n';
    lf = u.n;

    if (s->eol_type == EOL_DOS && p < p1 && *p == lf) {
        /* Skip \n at start of buffer.
         * Should check for pending skip state */
        p++;
        lp++;
    }

    while (nlines > 0 && p < p1) {
        while (p < p1) {
            if (*p++ == nl) {
                if (s->eol_type == EOL_DOS && p < p1 && *p == lf)
                    p++;
                lp = p;
                nlines--;
                break;
            }
        }
    }
    return (const u8 *)lp - buf;
}

static int probe_ucs4be(qe__unused__ QECharset *charset, const u8 *buf, int size)
{
    const uint32_t magic = (1U << '\b') | (1U << '\t') | (1U << '\f') |
                           (1U << '\n') | (1U << '\r') | (1U << '\033') |
                           (1U << 0x0e) | (1U << 0x0f) | (1U << 0x1a) |
                           (1U << 0x1f);
    const u8 *p = buf;
    const u8 *p_end = p + (size & ~3);
    uint32_t c;
    int count_spaces, count_lines;

    if (size & 3)
        return 0;

    count_spaces = count_lines = 0;

    while (p < p_end) {
        c = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | (p[3] << 0);
        p += 4;
        if (c <= 32) {
            if (c == ' ')
                count_spaces++;
            else
            if (c == '\n')
                count_lines++;
            else
            if (!(magic & (1U << c)))
                return 0;
        } else
        if (c > 0x10FFFF) {
            return 0;
        }
    }
    if (count_spaces + count_lines > size / (16 * 4))
        return 1;
    else
        return 0;
}

static int decode_ucs4be(CharsetDecodeState *s)
{
    const u8 *p;

    p = s->p;
    s->p += 4;
    return (p[0] << 24) + (p[1] << 16) + (p[2] << 8) + p[3];
}

static u8 *encode_ucs4be(qe__unused__ QECharset *charset, u8 *p, int c)
{
    p[0] = c >> 24;
    p[1] = c >> 16;
    p[2] = c >> 8;
    p[3] = c;
    return p + 4;
}

static int charset_get_chars_ucs4(CharsetDecodeState *s,
                                  const u8 *buf, int size)
{
    int count = size >> 2;  /* convert byte count to char32 count */
    const uint32_t *buf_end, *buf_ptr;
    uint32_t nl;
    union { uint32_t n; char c[4]; } u;

    if (s->eol_type != EOL_DOS)
        return count;

    buf_ptr = (const uint32_t *)(const void *)buf;
    buf_end = buf_ptr + count;
    // XXX: undefined behavior
    u.n = 0;
    u.c[(s->charset == &charset_ucs4be) * 3] = '\n';
    nl = u.n;

    while (buf_ptr < buf_end) {
        if (*buf_ptr++ == nl) {
            /* ignore \n in EOL_DOS scan, but count \r. (see above) */
            count--;
        }
    }
    return count;
}

static int charset_goto_char_ucs4(CharsetDecodeState *s,
                                  const u8 *buf, int size, int pos)
{
    int nb_chars;
    const uint32_t *buf_ptr, *buf_end;
    uint32_t nl;
    union { uint32_t n; char c[4]; } u;

    if (s->eol_type != EOL_DOS)
        return min(pos << 2, size);

    nb_chars = 0;
    buf_ptr = (const uint32_t *)(const void *)buf;
    buf_end = buf_ptr + (size >> 2);
    // XXX: undefined behavior
    u.n = 0;
    u.c[(s->charset == &charset_ucs4be) * 3] = '\n';
    nl = u.n;

    for (; buf_ptr < buf_end; buf_ptr++) {
        if (*buf_ptr == nl) {
            /* ignore \n in EOL_DOS scan, but count \r. (see above) */
            continue;
        }
        if (nb_chars >= pos)
            break;
        nb_chars++;
    }
    return (const u8*)buf_ptr - buf;
}

struct QECharset charset_ucs4le = {
    "ucs4le",
    "utf32le|utf-32le",
    probe_ucs4le,
    NULL,
    decode_ucs4le,
    encode_ucs4le,
    charset_get_pos_ucs4,
    charset_get_chars_ucs4,
    charset_goto_char_ucs4,
    charset_goto_line_ucs4,
    4, 0, 0, 10, 0, 0, table_none, NULL, NULL,
};

struct QECharset charset_ucs4be = {
    "ucs4be",
    "ucs4|utf32|utf-32|utf32be|utf-32be",
    probe_ucs4be,
    NULL,
    decode_ucs4be,
    encode_ucs4be,
    charset_get_pos_ucs4,
    charset_get_chars_ucs4,
    charset_goto_char_ucs4,
    charset_goto_line_ucs4,
    4, 0, 0, 10, 0, 0, table_none, NULL, NULL,
};

/********************************************************/
/* generic charset functions */

void qe_register_charset(struct QECharset *charset)
{
    struct QECharset **pp;

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
            add_string(&cp->cs, charset->name, 0);
        if (charset->aliases) {
            for (q = p = charset->aliases;; q++) {
                if (*q == '\0' || *q == '|') {
                    if (q > p) {
                        pstrncpy(name, sizeof(name), p, q - p);
                        if (strxstart(name, cp->current, NULL))
                            add_string(&cp->cs, name, 0);
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

void charset_decode_init(CharsetDecodeState *s, QECharset *charset,
                         EOLType eol_type)
{
    s->table = charset->encode_table;  /* default encode table */
    if (charset->table_alloc) {
        s->table = qe_malloc_array(unsigned short, 256);
        if (!s->table) {
            charset = &charset_8859_1;
            s->table = charset->encode_table;
        }
    }
    s->charset = charset;
    s->char_size = charset->char_size;
    s->eol_type = eol_type;
    s->eol_char = charset->eol_char;
    if (s->eol_char == '\n' && (s->eol_type == EOL_MAC || s->eol_type == EOL_DOS))
        s->eol_char = '\r';
    s->decode_func = charset->decode_func;
    s->get_pos_func = charset->get_pos_func;
    if (charset->decode_init)
        charset->decode_init(s);
}

void charset_decode_close(CharsetDecodeState *s)
{
    if (s->charset->table_alloc) {
        /* remove the const qualifier */
        qe_free((unsigned short **)&s->table);
    }
    /* safety */
    memset(s, 0, sizeof(CharsetDecodeState));
}

/* detect the end of line type. */
static void detect_eol_type_8bit(const u8 *buf, int size,
                                 QECharset *charset, EOLType *eol_typep)
{
    const u8 *p, *p1;
    int c, eol_bits;
    EOLType eol_type;

    if (!eol_typep)
        return;

    eol_type = *eol_typep;

    p = buf;
    p1 = p + size - 1;

    eol_bits = 0;
    while (p < p1) {
        c = *p++;
        if (c == '\r') {
            if (*p == '\r') {
                /* possibly spurious extra ^M in DOS file: ignore it,
                 * next iteration will determine if encoding is MAC or DOS
                 */
            } else
            if (*p == '\n') {
                p++;
                eol_bits |= 1 << EOL_DOS;
            } else {
                eol_bits |= 1 << EOL_MAC;
            }
        } else
        if (c == '\n') {
            eol_bits |= 1 << EOL_UNIX;
        }
    }
    switch (eol_bits) {
        case 0:
            /* no change, keep default value */
            break;
        case 1 << EOL_UNIX:
            eol_type = EOL_UNIX;
            break;
        case 1 << EOL_DOS:
            eol_type = EOL_DOS;
            break;
        case 1 << EOL_MAC:
            eol_type = EOL_MAC;
            break;
        default:
            /* A mixture of different styles, binary / unix */
            eol_type = EOL_UNIX;
            break;
    }
    *eol_typep = eol_type;
}

static void detect_eol_type_16bit(const u8 *buf, int size,
                                  QECharset *charset, EOLType *eol_typep)
{
    const uint16_t *p, *p1;
    uint16_t cr, lf;
    union { uint16_t n; char c[2]; } u;
    int c, eol_bits;
    EOLType eol_type;

    if (!eol_typep)
        return;

    eol_type = *eol_typep;

    p = (const uint16_t *)(const void *)buf;
    p1 = p + (size >> 1) - 1;
    u.n = 0;
    u.c[charset == &charset_ucs2be] = '\r';
    cr = u.n;
    u.c[charset == &charset_ucs2be] = '\n';
    lf = u.n;

    eol_bits = 0;
    while (p < p1) {
        c = *p++;
        if (c == cr) {
            if (*p == lf) {
                p++;
                eol_bits |= 1 << EOL_DOS;
            } else {
                eol_bits |= 1 << EOL_MAC;
            }
        } else
        if (c == lf) {
            eol_bits |= 1 << EOL_UNIX;
        }
    }
    switch (eol_bits) {
        case 0:
            /* no change, keep default value */
            break;
        case 1 << EOL_UNIX:
            eol_type = EOL_UNIX;
            break;
        case 1 << EOL_DOS:
            eol_type = EOL_DOS;
            break;
        case 1 << EOL_MAC:
            eol_type = EOL_MAC;
            break;
        default:
            /* A mixture of different styles, binary / unix */
            eol_type = EOL_UNIX;
            break;
    }
    *eol_typep = eol_type;
}

static void detect_eol_type_32bit(const u8 *buf, int size,
                                  QECharset *charset, EOLType *eol_typep)
{
    const uint32_t *p, *p1;
    uint16_t cr, lf;
    union { uint32_t n; char c[4]; } u;
    int c, eol_bits;
    EOLType eol_type;

    if (!eol_typep)
        return;

    eol_type = *eol_typep;

    p = (const uint32_t *)(const void *)buf;
    p1 = p + (size >> 2) - 1;
    u.n = 0;
    u.c[(charset == &charset_ucs4be) * 3] = '\r';
    cr = u.n;
    u.c[(charset == &charset_ucs4be) * 3] = '\n';
    lf = u.n;

    eol_bits = 0;
    while (p < p1) {
        c = *p++;
        if (c == cr) {
            if (*p == lf) {
                p++;
                eol_bits |= 1 << EOL_DOS;
            } else {
                eol_bits |= 1 << EOL_MAC;
            }
        } else
        if (c == lf) {
            eol_bits |= 1 << EOL_UNIX;
        }
    }
    switch (eol_bits) {
        case 0:
            /* no change, keep default value */
            break;
        case 1 << EOL_UNIX:
            eol_type = EOL_UNIX;
            break;
        case 1 << EOL_DOS:
            eol_type = EOL_DOS;
            break;
        case 1 << EOL_MAC:
            eol_type = EOL_MAC;
            break;
        default:
            /* A mixture of different styles, binary / unix */
            eol_type = EOL_UNIX;
            break;
    }
    *eol_typep = eol_type;
}

static QECharset *detect_eol_type(const u8 *buf, int size,
                                  QECharset *charset, EOLType *eol_typep)
{
    if (charset->char_size == 4)
        detect_eol_type_32bit(buf, size, charset, eol_typep);
    else
    if (charset->char_size == 2)
        detect_eol_type_16bit(buf, size, charset, eol_typep);
    else
        detect_eol_type_8bit(buf, size, charset, eol_typep);

    return charset;
}

QECharset *detect_charset(const u8 *buf, int size, EOLType *eol_typep)
{
#if 0
    QECharset *charset;

    /* Try and determine charset */
    /* CG: should iterate over charsets with probe function and score */
    charset = &charset_utf8;
    if (size > 0) {
        if (charset_utf8.probe_func(&charset_utf8, buf, size))
            charset = &charset_utf8;
        else
        if (charset_ucs4le.probe_func(&charset_ucs4le, buf, size))
            charset = &charset_ucs4le;
        else
        if (charset_ucs4be.probe_func(&charset_ucs4be, buf, size))
            charset = &charset_ucs4be;
        else
        if (charset_ucs2le.probe_func(&charset_ucs2le, buf, size))
            charset = &charset_ucs2le;
        else
        if (charset_ucs2be.probe_func(&charset_ucs2be, buf, size))
            charset = &charset_ucs2be;
        else
            charset = &charset_8859_1;
        /* CG: should distinguish charset_8859_1, charset_raw and
         * charset_auto */
    }
    return charset;
#else
    /* detect the charset. Actually only UTF8 is detected */
    int i, l, c, has_utf8, has_binary;

    has_utf8 = 0;
    for (i = 0; i < size;) {
        c = buf[i++];
        if ((c >= 0x80 && c < 0xc0) || c >= 0xfe) {
            has_utf8 = -1;
            goto done_utf8;
        }
        l = utf8_length[c];
        while (l > 1) {
            has_utf8 = 1;
            if (i >= size)
                break;
            c = buf[i++];
            if (!(c >= 0x80 && c < 0xc0)) {
                has_utf8 = -1;
                goto done_utf8;
            }
            l--;
        }
    }
done_utf8:
    if (has_utf8 > 0) {
        return detect_eol_type(buf, size, &charset_utf8, eol_typep);
    }

    /* Check for zwnbsp BOM: files starting with zero-width
     * no-break space as a byte-order mark (BOM) will be detected
     * as ucs2 or ucs4 encoded.
     */
    if (size >= 2 && buf[0] == 0xff && buf[1] == 0xfe) {
        if (size >= 4 && buf[2] == 0 && buf[3] == 0) {
            return detect_eol_type(buf, size, &charset_ucs4le, eol_typep);
        } else {
            return detect_eol_type(buf, size, &charset_ucs2le, eol_typep);
        }
    }

    if (size >= 2 && buf[0] == 0xfe && buf[1] == 0xff) {
        return detect_eol_type(buf, size, &charset_ucs2be, eol_typep);
    }

    if (size >= 4
    &&  buf[0] == 0 && buf[1] == 0 && buf[2] == 0xfe && buf[3] == 0xff) {
        return detect_eol_type(buf, size, &charset_ucs4be, eol_typep);
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
        if (maxc[0] > 'a' && maxc[1] < 0x2f && maxc[2] > 'a' && maxc[3] < 0x2f) {
            detect_eol_type(buf, size, &charset_ucs2le, eol_typep);
            return &charset_ucs2le;
        }
        if (maxc[1] > 'a' && maxc[0] < 0x2f && maxc[3] > 'a' && maxc[2] < 0x2f) {
            detect_eol_type(buf, size, &charset_ucs2be, eol_typep);
            return &charset_ucs2be;
        }
    }
#else
    if (charset_ucs4le.probe_func(&charset_ucs4le, buf, size))
        return detect_eol_type(buf, size, &charset_ucs4le, eol_typep);
    else
    if (charset_ucs4be.probe_func(&charset_ucs4be, buf, size))
        return detect_eol_type(buf, size, &charset_ucs4be, eol_typep);
    else
    if (charset_ucs2le.probe_func(&charset_ucs2le, buf, size))
        return detect_eol_type(buf, size, &charset_ucs2le, eol_typep);
    else
    if (charset_ucs2be.probe_func(&charset_ucs2be, buf, size))
        return detect_eol_type(buf, size, &charset_ucs2be, eol_typep);
#endif

    /* Should detect iso-2220-jp upon \033$@ and \033$B, but jis
     * support is not selected in tiny build
     * XXX: should use charset probe functions.
     */

    has_binary = 0;
    {
        const uint32_t magic = (1U << '\b') | (1U << '\t') | (1U << '\f') |
                               (1U << '\n') | (1U << '\r') | (1U << '\033') |
                               (1U << 0x0e) | (1U << 0x0f) | (1U << 0x1a) |
                               (1U << 0x1f);

        for (i = 0; i < size; i++) {
            c = buf[i];
            if (c < 32 && !(magic & (1U << c)))
                has_binary += 1;
        }
    }
    if (has_binary) {
        *eol_typep = EOL_UNIX;
        return &charset_raw;
    }

    detect_eol_type(buf, size, &charset_raw, eol_typep);

#ifndef CONFIG_TINY
    if (*eol_typep == EOL_MAC) {
        /* XXX: default MAC files to Mac_roman, should be selectable */
        /* XXX: should use probe functions */
        return &charset_mac_roman;
    }
#endif
    if (*eol_typep == EOL_DOS || has_utf8 < 0) {
        /* XXX: default DOS files to Latin1, should be selectable */
        return &charset_8859_1;
    }
    /* XXX: should use a state variable for default charset */
    return &charset_utf8;
#endif
}

/********************************************************/
/* 8 bit charsets */

void decode_8bit_init(CharsetDecodeState *s)
{
    QECharset *charset = s->charset;
    unsigned short *table;
    int i, n;

    table = (unsigned short *)s->table;     /* remove const qualifier */
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
    nl = s->eol_char;

    if (s->eol_type == EOL_DOS && p < p1 && *p == '\n') {
        /* Skip \n at start of buffer.
         * Should check for pending skip state */
        p++;
        lp++;
    }

    for (;;) {
        p = memchr(p, nl, p1 - p);
        if (!p)
            break;
        p++;
        if (s->eol_type == EOL_DOS && p < p1 && *p == '\n')
            p++;
        lp = p;
        line++;
    }
    col = p1 - lp;
    *line_ptr = line;
    *col_ptr = col;
}

int charset_goto_line_8bit(CharsetDecodeState *s,
                           const u8 *buf, int size, int nlines)
{
    const u8 *p, *p1, *lp;
    int nl;

    lp = p = buf;
    p1 = p + size;
    nl = s->eol_char;

    if (s->eol_type == EOL_DOS && p < p1 && *p == '\n') {
        /* Skip \n at start of buffer.
         * Should check for pending skip state */
        p++;
        lp++;
    }

    while (nlines > 0) {
        p = memchr(p, nl, p1 - p);
        if (!p)
            break;
        p++;
        if (s->eol_type == EOL_DOS && p < p1 && *p == '\n')
            p++;
        lp = p;
        nlines--;
    }
    return lp - buf;
}

int charset_get_chars_8bit(CharsetDecodeState *s,
                           const u8 *buf, int size)
{
    int count = size;
    const u8 *buf_end, *buf_ptr;

    if (s->eol_type != EOL_DOS)
        return count;

    buf_ptr = buf;
    buf_end = buf_ptr + count;
    while (buf_ptr < buf_end) {
        if (*buf_ptr++ == '\n') {
            /* ignore \n in EOL_DOS scan, but count \r. (see above) */
            count--;
        }
    }
    return count;
}

int charset_goto_char_8bit(CharsetDecodeState *s,
                           const u8 *buf, int size, int pos)
{
    int nb_chars;
    const u8 *buf_ptr, *buf_end;

    if (s->eol_type != EOL_DOS)
        return min(pos, size);

    nb_chars = 0;
    buf_ptr = buf;
    buf_end = buf_ptr + size;
    for (; buf_ptr < buf_end; buf_ptr++) {
        if (*buf_ptr == '\n') {
            /* ignore \n in EOL_DOS scan, but count \r. */
            continue;
        }
        if (nb_chars >= pos)
            break;
        nb_chars++;
    }
    return buf_ptr - buf;
}

/********************************************************/

void charset_init(void)
{
    /* initialize unicode_glyph_range_index[] */
    unsigned int const *ip = unicode_glyph_ranges;
    unsigned int ucs;

    for (ucs = 0; ucs < 0x20000; ucs += 0x1000) {
        while (ucs > ip[0])
            ip += 2;
        unicode_glyph_range_index[ucs >> 12] = ip;
    }

    qe_register_charset(&charset_raw);
    qe_register_charset(&charset_8859_1);
    qe_register_charset(&charset_vt100);
    qe_register_charset(&charset_7bit);
    qe_register_charset(&charset_utf8);
    qe_register_charset(&charset_ucs2le);
    qe_register_charset(&charset_ucs2be);
    qe_register_charset(&charset_ucs4le);
    qe_register_charset(&charset_ucs4be);
}
