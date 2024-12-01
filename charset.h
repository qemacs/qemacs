/*
 * Charset definitions and functions for QEmacs
 *
 * Copyright (c) 2000-2001 Fabrice Bellard.
 * Copyright (c) 2000-2024 Charlie Gordon.
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

#ifndef CHARSET_H
#define CHARSET_H

#include "util.h"   /* CompleteState */

/* maximum number of bytes for a character in all the supported charsets */
#define MAX_CHAR_BYTES 6

typedef struct CharsetDecodeState CharsetDecodeState;
#if defined(__cplusplus)
typedef struct QECharset QECharset;
#else
typedef const struct QECharset QECharset;
#endif

#ifndef U8_DEFINED
typedef unsigned char u8;
#define U8_DEFINED  1
#endif

struct QECharset {
    const char *name;
    const char *aliases;
    int (*probe_func)(QECharset *charset, const u8 *buf, int size);
    void (*decode_init)(CharsetDecodeState *s);
    char32_t (*decode_func)(CharsetDecodeState *s);
    /* return NULL if cannot encode. Currently no state since speed is
       not critical yet */
    u8 *(*encode_func)(QECharset *charset, u8 *buf, char32_t c);
    void (*get_pos_func)(CharsetDecodeState *s, const u8 *buf, int size,
                         int *line_ptr, int *col_ptr);
    int (*get_chars_func)(CharsetDecodeState *s, const u8 *buf, int size);
    int (*goto_char_func)(CharsetDecodeState *s, const u8 *buf, int size, int pos);
    int (*goto_line_func)(CharsetDecodeState *s, const u8 *buf, int size, int lines);
    unsigned int char_size : 3;
    unsigned int variable_size : 1;
    unsigned int table_alloc : 1; /* true if CharsetDecodeState.table must be malloced */
    /* private data for some charsets */
    u8 eol_char; /* 0x0A for ASCII, 0x25 for EBCDIC */
    u8 min_char, max_char;
    const unsigned short *encode_table;
    const unsigned short *private_table;
    struct QECharset *next;
};

extern struct QECharset *first_charset;
/* predefined charsets */
extern struct QECharset charset_raw;
extern struct QECharset charset_8859_1;
extern struct QECharset charset_utf8;
extern struct QECharset charset_vt100; /* used for the tty output */
extern struct QECharset charset_mac_roman;
extern struct QECharset charset_ucs2le, charset_ucs2be;
extern struct QECharset charset_ucs4le, charset_ucs4be;

typedef enum EOLType {
    EOL_UNIX = 0,
    EOL_DOS,
    EOL_MAC,
} EOLType;

struct CharsetDecodeState {
    /* 256 ushort table for hyper fast decoding */
    const unsigned short *table;
    int char_size;
    EOLType eol_type;
    int eol_char;
    const u8 *p;
    /* slower decode function for complicated cases */
    char32_t (*decode_func)(CharsetDecodeState *s);
    void (*get_pos_func)(CharsetDecodeState *s, const u8 *buf, int size,
                         int *line_ptr, int *col_ptr);
    QECharset *charset;
};

#define INVALID_CHAR 0xfffd
#define ESCAPE_CHAR  0xffff

struct QEmacsState;
void charset_init(struct QEmacsState *qs);
int qe_charset_more_init(struct QEmacsState *qs);
int qe_charset_jis_init(struct QEmacsState *qs);

void qe_register_charset(struct QEmacsState *qs, struct QECharset *charset);
void charset_complete(CompleteState *cp, CompleteFunc enumerate);

QECharset *qe_find_charset(struct QEmacsState *qs, const char *str);
void charset_decode_init(CharsetDecodeState *s, QECharset *charset,
                         EOLType eol_type);
void charset_decode_close(CharsetDecodeState *s);
void charset_get_pos_8bit(CharsetDecodeState *s, const u8 *buf, int size,
                          int *line_ptr, int *col_ptr);
int charset_get_chars_8bit(CharsetDecodeState *s, const u8 *buf, int size);
int charset_goto_char_8bit(CharsetDecodeState *s, const u8 *buf, int size, int pos);
int charset_goto_line_8bit(CharsetDecodeState *s, const u8 *buf, int size, int nlines);

QECharset *detect_charset(const u8 *buf, int size, EOLType *eol_typep);

void decode_8bit_init(CharsetDecodeState *s);
char32_t decode_8bit(CharsetDecodeState *s);
u8 *encode_8bit(QECharset *charset, u8 *q, char32_t c);

#endif /* CHARSET_H */
