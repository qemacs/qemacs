/*
 * Basic Charset functions for QEmacs
 *
 * Copyright (c) 2000-2002 Fabrice Bellard.
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

#include "charset.h"

/* XXX: Should move this to QEmacsState, and find a way for html2png */
struct QECharset *first_charset;

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

/********************************************************/
/* raw */

static u8 *encode_raw(qe__unused__ QECharset *charset, u8 *p, char32_t c) {
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

static u8 *encode_8859_1(qe__unused__ QECharset *charset, u8 *p, char32_t c)
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

static u8 *encode_vt100(qe__unused__ QECharset *charset, u8 *p, char32_t c)
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

static u8 *encode_7bit(qe__unused__ QECharset *charset, u8 *p, char32_t c)
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
/* UTF-8 */

static unsigned short const table_utf8[256] = {
    RUN128(0),              /* [0x00...0x7F] are self-encoding ASCII bytes */
    REP64(INVALID_CHAR),    /* [0x80...0xBF] are invalid prefix bytes */
    /* 0xC0 and 0xC1 are invalid prefix bytes because they encode 7-bit codes */
    REP32(ESCAPE_CHAR),     /* [0xC0...0xDF] leading bytes of 2 byte sequences */
    REP16(ESCAPE_CHAR),     /* [0xE0...0xEF] leading bytes of 3 byte sequences */
    REP8(ESCAPE_CHAR),      /* [0xF0...0xF7] leading bytes of 4 byte sequences */
    REP4(ESCAPE_CHAR),      /* [0xF8...0xFB] leading bytes of 5 byte sequences */
    REP2(ESCAPE_CHAR),      /* [0xFC...0xFD] leading bytes of 6 byte sequences */
    /* allow for utf8x encoding */
    ESCAPE_CHAR,//INVALID_CHAR,           /* 0xFE is invalid in UTF-8 encoding */
    ESCAPE_CHAR,//INVALID_CHAR,           /* 0xFF is invalid in UTF-8 encoding */
};

static int probe_utf8(qe__unused__ QECharset *charset, const u8 *buf, int size)
{
    /* probe initial file block for UTF-8 encoding:
       scan buffer contents for valid ASCII and UTF-8 contents */
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
        c = *p++;
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
        if (c < 0x7F) { /* ASCII subset */
            continue;
        } else
        if (c < 0xc2) { /* UTF-8 trailing bytes and invalid prefix bytes */
            return 0;
        } else
        if (c < 0xe0) { /* 2 byte UTF-8 encoding for U+0080..U+07FF */
            if ((p[0] ^ 0x80) > 0x3f)
                return 0;
            count_utf8++;
            p += 1;
        } else
        if (c < 0xf0) { /* 3 byte UTF-8 encoding for U+0800..U+FFFF */
            if ((p[0] ^ 0x80) > 0x3f || (p[1] ^ 0x80) > 0x3f)
                return 0;
            count_utf8++;
            p += 2;
        } else
        if (c < 0xf8) { /* 4 byte UTF-8 encoding for U+10000..U+10FFFF */
            if ((p[0] ^ 0x80) > 0x3f || (p[1] ^ 0x80) > 0x3f
            ||  (p[2] ^ 0x80) > 0x3f)
                return 0;
            count_utf8++;
            p += 3;
        } else { /* overlong encodings and invalid lead bytes */
            return 0;
        }
    }
    if (count_spaces | count_lines | count_utf8)
        return 1;
    else
        return 0;
}

static char32_t decode_utf8_func(CharsetDecodeState *s)
{
    return utf8_decode((const char **)(void *)&s->p);
}

static u8 *encode_utf8(qe__unused__ QECharset *charset, u8 *q, char32_t c)
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
     * block, but works for correctly encoded UTF-8 contents. */
    // XXX: trailing bytes are counted at the start of the block.
    //      this is inconsistent with charset_goto_char_utf8().
    //      should instead count the number of single and leading bytes
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
        nb_chars += ((c ^ 0x80) > 0x3f);
    }
    /* CG: nb_chars is the number of character boundaries, trailing
     * UTF-8 sequence at start of buffer is ignored in count while
     * incomplete UTF-8 sequence at end of buffer is counted.  This may
     * cause problems when counting characters with eb_get_pos with an
     * offset falling inside a UTF-8 sequence, and will produce
     * incorrect counts on broken UTF-8 sequences spanning page
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
        if ((c ^ 0x80) <= 0x3f) {
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
    "utf8",
    "utf-8|al32utf8",
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

/* Extended UTF-8 encoding: accept any 32-bit value from 0 to 0xffffffff
   All combinations are accepted.
 */
// XXX: should also have CESU-8 where non-BMP1 glyphs are encoded as
//      surrogate pairs, themselves encoded in UTF-8.
static struct QECharset charset_utf8x = {
    "utf8x",
    "utf-8x",
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

static char32_t decode_ucs2le(CharsetDecodeState *s) {
    /* XXX: should handle surrogates */
    const u8 *p;

    p = s->p;
    s->p += 2;
    return p[0] + ((char32_t)p[1] << 8);
}

static u8 *encode_ucs2le(qe__unused__ QECharset *charset, u8 *p, char32_t c)
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

static char32_t decode_ucs2be(CharsetDecodeState *s) {
    /* XXX: should handle surrogates */
    const u8 *p;

    p = s->p;
    s->p += 2;
    return ((char32_t)p[0] << 8) + p[1];
}

static u8 *encode_ucs2be(qe__unused__ QECharset *charset, u8 *p, char32_t c)
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
        return min_offset(pos << 1, size);

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

static char32_t decode_ucs4le(CharsetDecodeState *s) {
    const u8 *p;

    p = s->p;
    s->p += 4;
    return (((char32_t)p[0] <<  0) + ((char32_t)p[1] <<  8) +
            ((char32_t)p[2] << 16) + ((char32_t)p[3] << 24));
}

static u8 *encode_ucs4le(qe__unused__ QECharset *charset, u8 *p, char32_t c)
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

static char32_t decode_ucs4be(CharsetDecodeState *s) {
    const u8 *p;

    p = s->p;
    s->p += 4;
    return (((char32_t)p[0] << 24) + ((char32_t)p[1] << 16) +
            ((char32_t)p[2] <<  8) + ((char32_t)p[3] <<  0));
}

static u8 *encode_ucs4be(qe__unused__ QECharset *charset, u8 *p, char32_t c)
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
        return min_offset(pos << 2, size);

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

void qe_register_charset(struct QEmacsState *qs, struct QECharset *charset)
{
    struct QECharset **pp;

    pp = &first_charset;
    while (*pp != NULL) {
        if (*pp == charset)
            return;
        pp = &(*pp)->next;
    }
    *pp = charset;
}

void charset_complete(CompleteState *cp, CompleteFunc enumerate) {
    QECharset *charset;
    char name[32];
    const char *p, *q;

    for (charset = first_charset; charset != NULL; charset = charset->next) {
        enumerate(cp, charset->name, CT_STRX);
        if (charset->aliases) {
            for (q = p = charset->aliases;; q++) {
                if (*q == '\0' || *q == '|') {
                    if (q > p) {
                        pstrncpy(name, sizeof(name), p, q - p);
                        enumerate(cp, name, CT_STRX);
                    }
                    if (*q == '\0')
                        break;
                    p = q + 1;
                }
            }
        }
    }
}

QECharset *qe_find_charset(struct QEmacsState *qs, const char *name)
{
    QECharset *charset;

    if (!name)
        return NULL;

    for (charset = first_charset; charset != NULL; charset = charset->next) {
        if (!strxcmp(charset->name, name)
        ||  (charset->aliases && strxfind(charset->aliases, name))) {
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
        qe_free(unconst(unsigned short **)&s->table);
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
    uint16_t c, cr, lf;
    union { uint16_t n; char c[2]; } u;
    int eol_bits;
    EOLType eol_type;

    if (!eol_typep)
        return;

    u.n = 0;
    u.c[charset == &charset_ucs2be] = '\r';
    cr = u.n;
    u.c[charset == &charset_ucs2be] = '\n';
    lf = u.n;

    p = (const uint16_t *)(const void *)buf;
    p1 = p + (size >> 1) - 1;

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
        eol_type = *eol_typep;
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
    uint32_t c, cr, lf;
    union { uint32_t n; char c[4]; } u;
    int eol_bits;
    EOLType eol_type;

    if (!eol_typep)
        return;

    u.n = 0;
    u.c[(charset == &charset_ucs4be) * 3] = '\r';
    cr = u.n;
    u.c[(charset == &charset_ucs4be) * 3] = '\n';
    lf = u.n;

    p = (const uint32_t *)(const void *)buf;
    p1 = p + (size >> 2) - 1;

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
        eol_type = *eol_typep;
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
    /* Detect the charset:
       UTF-8, UTF-16, UTF-32, Latin1, binary are detected */
    int i, j, trail, bits;
    char32_t c, min_code;
    enum {
        HAS_BINARY = 1, HAS_UTF8 = 2, TRAILING_BYTES = 4,
        INVALID_PREFIX = 8, INVALID_SEQUENCE = 16, OVERLONG_ENCODING = 32,
        HAS_SURROGATE = 64, INVALID_CODE = 128,
        UTF8_MASK = 255,
    };
    const uint32_t magic = ((1U << '\b') | (1U << '\t') | (1U << '\f') |
                            (1U << '\n') | (1U << '\r') | (1U << '\033') |
                            (1U << 0x0e) | (1U << 0x0f) | (1U << 0x1a) |
                            (1U << 0x1f));

    bits = 0;

    for (i = 0; i < size;) {
        c = buf[i++];
        if (c < 32) {
            if (!(magic & (1U << c)))
                bits |= HAS_BINARY;
        } else
        if (c < 0x80) {
            /* 0x7F should trigger HAS_BINARY? */
            continue;
        } else
        if (c < 0xC0) {
            bits |= TRAILING_BYTES;
        } else {
            bits |= HAS_UTF8;
            if (c < 0xE0) {
                trail = 1;
                min_code = 0x80;
                c &= 0x1f;
            } else
            if (c < 0xF0) {
                trail = 2;
                min_code = 0x800;
                c &= 0x0f;
            } else
            if (c < 0xF8) {
                trail = 3;
                min_code = 0x10000;
                c &= 0x07;
            } else
            if (c < 0xFC) {
                trail = 4;
                min_code = 0x00200000;
                c &= 0x03;
            } else {
                trail = 5;
                min_code = 0x04000000;
                c &= 0x03;
            }
            if (i + trail > size)
                break;
            for (j = 0; j < trail; j++) {
                char32_t c1 = buf[i + j] ^ 0x80;
                if (c1 > 0x3f)
                    break;
                c = (c << 6) + c1;
            }
            i += j;
            if (j < trail) {
                bits |= INVALID_SEQUENCE;
            } else {
                if (c < min_code)
                    bits |= OVERLONG_ENCODING;
                if (c >= 0xd800 && c <= 0xdfff) {
                    // XXX: should detect CESU-8 encoding
                    bits |= HAS_SURROGATE;
                }
                if (c == 0xfffe || c == 0xffff || c > 0x10ffff)
                    bits |= INVALID_CODE;
            }
        }
    }
    if ((bits & (HAS_UTF8 | HAS_BINARY | TRAILING_BYTES | INVALID_SEQUENCE)) == HAS_UTF8) {
        if (bits == HAS_UTF8) {
            /* strict UTF-8 encoding */
            return detect_eol_type(buf, size, &charset_utf8, eol_typep);
        } else {
            /* relax extended UTF-8 encoding */
            return detect_eol_type(buf, size, &charset_utf8x, eol_typep);
        }
    }

    /* Check for ZWNBSP as BOM: files starting with zero-width
     * no-break space as a byte-order mark (BOM) will be detected
     * as ucs2/UTF-16 or ucs4/UTF-32 encoded.
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

    if (bits & HAS_BINARY) {
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
    if (*eol_typep == EOL_DOS
    ||  (bits & (TRAILING_BYTES | INVALID_PREFIX | INVALID_SEQUENCE))) {
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

    table = unconst(unsigned short *)s->table;     /* remove const qualifier */
    for (i = 0; i < charset->min_char; i++)
        *table++ = i;
    n = charset->max_char - charset->min_char + 1;
    for (i = 0; i < n; i++)
        *table++ = charset->private_table[i];
    for (i = charset->max_char + 1; i < 256; i++)
        *table++ = i;
}

char32_t decode_8bit(CharsetDecodeState *s)
{
    return s->table[*(s->p)++];
}

/* not very fast, but not critical yet */
u8 *encode_8bit(QECharset *charset, u8 *q, char32_t c)
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
        return min_offset(pos, size);

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

void charset_init(struct QEmacsState *qs) {
    qe_register_charset(qs, &charset_raw);
    qe_register_charset(qs, &charset_8859_1);
    qe_register_charset(qs, &charset_vt100);
    qe_register_charset(qs, &charset_7bit);
    qe_register_charset(qs, &charset_utf8);
    qe_register_charset(qs, &charset_utf8x);
    qe_register_charset(qs, &charset_ucs2le);
    qe_register_charset(qs, &charset_ucs2be);
    qe_register_charset(qs, &charset_ucs4le);
    qe_register_charset(qs, &charset_ucs4be);
}
