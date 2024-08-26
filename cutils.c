/*
 * Various simple C utilities
 *
 * Copyright (c) 2000-2002 Fabrice Bellard.
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

#include <stdarg.h>
#include <string.h>

#include "config.h"     /* for CONFIG_WIN32 */
#include "cutils.h"

char *pstrcpy(char *buf, int size, const char *str) {
    /*@API utils.string
       Copy the string pointed by `str` to the destination array `buf`,
       of length `size` bytes, truncating excess bytes.

       @param `buf` destination array, must be a valid pointer.
       @param `size` length of destination array in bytes.
       @param `str` pointer to a source string, must be a valid pointer.
       @return a pointer to the destination array.
       @note: truncation cannot be detected reliably.
       @note: this function does what many programmers wrongly expect
       `strncpy` to do. `strncpy` has different semantics and does not
       null terminate the destination array in case of excess bytes.
       **NEVER use `strncpy`**.
     */
    if (size > 0) {
        int i;
        for (i = 0; i < size - 1 && (buf[i] = str[i]) != '\0'; i++)
            continue;
        buf[i] = '\0';
    }
    return buf;
}

char *pstrcat(char *buf, int size, const char *s) {
    /*@API utils.string
       Copy the string pointed by `s` at the end of the string contained
       in the destination array `buf`, of length `size` bytes,
       truncating excess bytes.
       @return a pointer to the destination array.
       @note: truncation cannot be detected reliably.
       @note: `strncat` has different semantics and does not check
       for potential overflow of the destination array.
     */
    int len = strnlen(buf, size);
    if (len < size)
        pstrcpy(buf + len, size - len, s);
    return buf;
}

char *pstrncpy(char *buf, int size, const char *s, int len) {
    /*@API utils.string
       Copy at most `len` bytes from the string pointed by `s` to the
       destination array `buf`, of length `size` bytes, truncating
       excess bytes.
       @return a pointer to the destination array.
       @note truncation cannot be detected reliably.
     */
    if (size > 0) {
        int i;
        if (len >= size)
            len = size - 1;
        for (i = 0; i < len && (buf[i] = s[i]) != '\0'; i++)
            continue;
        buf[i] = '\0';
    }
    return buf;
}

char *pstrncat(char *buf, int size, const char *s, int slen) {
    /*@API utils.string
       Copy at most `len` bytes from the string pointed by `s` at the end
       of the string contained in the destination array `buf`, of length
       `size` bytes, truncating excess bytes.
       @return a pointer to the destination array.
       @note truncation cannot be detected reliably.
     */
    int len = strnlen(buf, size);
    if (len < size)
        pstrncpy(buf + len, size - len, s, slen);
    return buf;
}

/* these functions are duplicated from ffmpeg/libavformat/cutils.c
 * conflict is resolved by redefining the symbols in cutils.h
 */

int strstart(const char *str, const char *val, const char **ptr) {
    /*@API utils.string
       Test if `val` is a prefix of `str`.

       If `val` is a prefix of `str`, a pointer to the first character
       after the prefix in `str` is stored into `ptr` provided `ptr`
       is not a null pointer.

       If `val` is not a prefix of `str`, return `0` and leave `*ptr`
       unchanged.

       @param `str` input string, must be a valid pointer.
       @param `val` prefix string, must be a valid pointer.
       @param `ptr` updated with a pointer past the prefix if found.
       @return `true` if there is a match, `false` otherwise.
     */
    size_t i;
    for (i = 0; val[i] != '\0'; i++) {
        if (str[i] != val[i])
            return 0;
    }
    if (ptr)
        *ptr = &str[i];
    return 1;
}

int strend(const char *str, const char *val, const char **ptr) {
    /*@API utils.string
       Test if `val` is a suffix of `str`.

       if `val` is a suffix of `str`, a pointer to the first character
       of the suffix in `str` is stored into `ptr` provided `ptr` is
       not a null pointer.

       @param `str` input string, must be a valid pointer.
       @param `val` suffix string, must be a valid pointer.
       @param `ptr` updated to the suffix in `str` if there is a match.
       @return `true` if there is a match, `false` otherwise.
     */
    size_t len1 = strlen(str);
    size_t len2 = strlen(val);

    if (len1 >= len2 && !memcmp(str += len1 - len2, val, len2)) {
        if (ptr)
            *ptr = str;
        return 1;
    } else {
        return 0;
    }
}

size_t get_basename_offset(const char *path) {
    /*@API utils.string
       Get the offset of the filename component of a path.
       Return the offset to the first character of the filename part of
       the path pointed to by string argument `path`.
     */
    size_t i, base = 0;
    const char *p = path;

    if (p) {
        for (i = 0; p[i]; i++) {
#ifdef CONFIG_WIN32
            /* Simplistic DOS/Windows filename support */
            if (p[i] == '/' || p[i] == '\\' || (p[i] == ':' && i == 1))
                base = i + 1;
#else
            if (p[i] == '/')
                base = i + 1;
#endif
        }
    }
    return base;
}

size_t get_extension_offset(const char *path) {
    /*@API utils.string
       Get the filename extension portion of a path.
       Return the offset to the first character of the last extension of
       the filename part of the path pointed to by string argument `path`.
       If there is no extension, return a pointer to the null terminator
       and the end of path.
       Leading dots are skipped, they are not considered part of an extension.
     */
    size_t ext = 0;
    const char *p = path;

    if (p) {
        size_t i = get_basename_offset(p);
        while (p[i] == '.')
            i++;
        for (; p[i]; i++) {
            if (p[i] == '.')
                ext = i;
        }
        if (!ext)
            ext = i;
    }
    return ext;
}

char *get_dirname(char *dest, int size, const char *file) {
    /*@API utils.string
       Extract the directory portion of a path.
       This leaves out the trailing slash if any.  The complete path is
       obtained by concatenating `dirname` + `"/"` + `basename`.
       If the original path doesn't contain a directory name, `"."` is
       copied to `dest`.
       @return a pointer to the destination array.
       @note: truncation cannot be detected reliably.
       @note: the trailing slash is not removed if the directory is the
       root directory: this makes the behavior somewhat inconsistent,
       requiring more tests when reconstructing the full path.
     */
    if (dest && size > 0) {
        size_t i = 0;
        if (file) {
            pstrcpy(dest, size, file);
            // XXX: should try and detect truncation
            i = get_basename_offset(dest);
            /* remove the trailing slash (or backslash) unless root dir or
               preceded by a drive spec or protocol prefix (eg: http:) */
            if (i > 1 && dest[i - 1] != ':' && dest[i - 2] != ':')
                i--;
            if (i == 0)
                dest[i++] = '.';
        }
        dest[i] = '\0';
    }
    return dest;
}

const char *get_relativename(const char *filename, const char *dirname) {
    /*@API utils.string
       Get the offset to the filename porting that is relative to the
       directory name `dirname`
       @argument `dirname` the name of the directory.
       @argument `filename` the name of the file that is a descendent of directory.
       @return a pointer inside filename.
     */
    const char *ptr;
    if (strstart(filename, dirname, &ptr) && *ptr == '/')
        return ptr + 1;
    return filename;
}

/* Dynamic buffer package */

static void *dbuf_default_realloc(void *opaque, void *ptr, size_t size) {
    /*@API utils.dbuf
       Default memory allocation routine for dynamic buffers.
       @argument `opaque` is the opaque argument passed to `dbuf_init2`.
       @argument `ptr` is the pointer to the object that must be reallocated.
       It is `NULL` if the buffer has not been allocated yet.
       @argument `size` is new size requested for the buffer in bytes.
       An argument value of `0` for `size` specifies that the block should be
       freed and `NULL` will be returned.
       @return a pointer to the reallocated block or `NULL` if allocation
       failed or the requested size was `0` and the pointer was freed.
       @note: the C Standard specifies that if the size is zero, the behavior
       of `realloc` is undefined. In FreeBSD systems, if `size` is zero and
       `ptr` is not `NULL`, a new, minimum sized object is allocated and the
       original object is freed.
     */
    if (!size) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, size);
}

DynBuf *dbuf_init2(DynBuf *s, void *opaque, DynBufReallocFunc *realloc_func) {
    /*@API utils.dbuf
       Initialize a dynamic buffer with a specified reallocation function.
       @argument `s` a valid pointer to an uninitialized dynamic buffer object.
       @argument `opaque` is the opaque argument that will be passed to
       the reallocation function.
       @argument `realloc_func` the reallocation function to use for this dynamic buffer.
       @return the value of `s`.
     */
    memset(s, 0, sizeof(*s));
    if (!realloc_func)
        realloc_func = dbuf_default_realloc;
    s->opaque = opaque;
    s->realloc_func = realloc_func;
    return s;
}

DynBuf *dbuf_init(DynBuf *s) {
    /*@API utils.dbuf
       Initialize a dynamic buffer with the default reallocation function.
       @argument `s` a valid pointer to an uninitialized dynamic buffer object.
       @return the value of `s`.
     */
    return dbuf_init2(s, NULL, NULL);
}

/* return < 0 if error */
int dbuf_realloc(DynBuf *s, size_t new_size) {
    /*@API utils.dbuf
       Reallocate the buffer to a larger size.
       @argument `s` a valid pointer to an uninitialized dynamic buffer object.
       @argument `new_size` the new size for the buffer.  If `new_size` is
       smaller than the current buffer length, no reallocation is performed.
       @return an error indicator: `0` if buffer was successfully reallocated,
       `-1` otherwise and `error` member is set.
       @note: the new buffer length may be larger than `new_size` to minimize
       further reallocation requests.
     */
    size_t size = s->allocated_size;
    if (new_size > size) {
        uint8_t *new_buf;
        if (s->error)
            return -1;
        size += size / 2;
        if (new_size < size)
            new_size = size;
        new_buf = s->realloc_func(s->opaque, s->buf, new_size);
        if (!new_buf) {
            s->error = TRUE;
            return -1;
        }
        s->buf = new_buf;
        s->allocated_size = new_size;
    }
    return 0;
}

int dbuf_write(DynBuf *s, size_t offset, const uint8_t *data, size_t len) {
    /*@API utils.dbuf
       Write a block of data at a given offset in a dynamic buffer
       @argument `s` a valid pointer to an uninitialized dynamic buffer object.
       @argument `offset` the position where to write the block
       @argument `data` a valid pointer to a memory block
       @argument `len` the number of bytes to write
       @return an error indicator: `0` if data could be written, `-1` if
       buffer could not be reallocated.
     */
    size_t end;
    end = offset + len;
    if (dbuf_realloc(s, end))
        return -1;
    memcpy(s->buf + offset, data, len);
    if (end > s->size)
        s->size = end;
    return 0;
}

int dbuf_put(DynBuf *s, const uint8_t *data, size_t len) {
    /*@API utils.dbuf
       Write a block of data at the end of a dynamic buffer
       @argument `s` a valid pointer to an uninitialized dynamic buffer object.
       @argument `data` a valid pointer to a memory block
       @argument `len` the number of bytes to write
       @return an error indicator: `0` if data could be written, `-1` if
       buffer could not be reallocated.
     */
    if (unlikely((s->size + len) > s->allocated_size)) {
        if (dbuf_realloc(s, s->size + len))
            return -1;
    }
    memcpy(s->buf + s->size, data, len);
    s->size += len;
    return 0;
}

int dbuf_put_self(DynBuf *s, size_t offset, size_t len) {
    /*@API utils.dbuf
       Duplicate a block of data from the dynamic buffer at the end
       @argument `s` a valid pointer to an uninitialized dynamic buffer object.
       @argument `offset` the offset of the block to copy
       @argument `len` the number of bytes to copy
       @return an error indicator: `0` if data could be written, `-1` if
       buffer could not be reallocated.
     */
    if (unlikely((s->size + len) > s->allocated_size)) {
        if (dbuf_realloc(s, s->size + len))
            return -1;
    }
    memcpy(s->buf + s->size, s->buf + offset, len);
    s->size += len;
    return 0;
}

int dbuf_putc(DynBuf *s, uint8_t c) {
    /*@API utils.dbuf
       Write a byte at the end of a dynamic buffer
       @argument `s` a valid pointer to an uninitialized dynamic buffer object.
       @argument `c` the byte value
       @return an error indicator: `0` if data could be written, `-1` if
       buffer could not be reallocated.
     */
    return dbuf_put(s, &c, 1);
}

int dbuf_putstr(DynBuf *s, const char *str) {
    /*@API utils.dbuf
       Write a string at the end of a dynamic buffer
       @argument `s` a valid pointer to an uninitialized dynamic buffer object.
       @argument `str` a valid pointer to a string
       @return an error indicator: `0` if data could be written, `-1` if
       buffer could not be reallocated.
     */
    return dbuf_put(s, (const uint8_t *)str, strlen(str));
}

int __attribute__((format(printf, 2, 3))) dbuf_printf(DynBuf *s, const char *fmt, ...) {
    /*@API utils.dbuf
       Produce formatted output at the end of a dynamic buffer
       @argument `s` a valid pointer to an uninitialized dynamic buffer object.
       @argument `fmt` a valid pointer to a format string
       @return an error indicator: `0` if data could be written, `-1` if
       the buffer could not be reallocated or if there was a formatting error.
     */
    va_list ap;
    char buf[128];
    int len;

    va_start(ap, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len < 0)
        return len;
    if (len < (int)sizeof(buf)) {
        /* fast case */
        return dbuf_put(s, (uint8_t *)buf, len);
    } else {
        if (dbuf_realloc(s, s->size + len + 1))
            return -1;
        va_start(ap, fmt);
        vsnprintf((char *)(s->buf + s->size), s->allocated_size - s->size,
                  fmt, ap);
        va_end(ap);
        s->size += len;
    }
    return 0;
}

void dbuf_free(DynBuf *s) {
    /*@API utils.dbuf
       Free the allocated data in a dynamic buffer
       @argument `s` a valid pointer to an uninitialized dynamic buffer object.
     */
    if (s->buf && s->realloc_func) {
        s->realloc_func(s->opaque, s->buf, 0);
    }
    memset(s, 0, sizeof(*s));
}

int unicode_to_utf8(uint8_t *buf, unsigned int c) {
    /*@API utils
       Encode a codepoint as UTF-8
       @argument `buf` a valid pointer to an array of char at least 6 bytes long
       @argument `c` a codepoint
       @return the number of bytes produced in the array.
       @note: at most 31 bits are encoded, producing at most `UTF8_CHAR_LEN_MAX` bytes.
       @note: no null terminator byte is written to the destination array.
     */
    uint8_t *q = buf;

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
                    } else if (c < 0x80000000) {
                        *q++ = (c >> 30) | 0xfc;
                        *q++ = ((c >> 24) & 0x3f) | 0x80;
                    } else {
                        return 0;
                    }
                    *q++ = ((c >> 18) & 0x3f) | 0x80;
                }
                *q++ = ((c >> 12) & 0x3f) | 0x80;
            }
            *q++ = ((c >> 6) & 0x3f) | 0x80;
        }
        *q++ = (c & 0x3f) | 0x80;
    }
    return q - buf;
}

#if 0
static const int utf8_min_code[5] = {
    0x80, 0x800, 0x10000, 0x200000, 0x4000000,
    //0x80, 0x20, 0x400, 0x8000, 0x100000,
    //0x80, 0x20, 0x10, 0x200, 0x4000,
    //0x80, 0x20, 0x10, 0x8, 0x100,
    //0x80, 0x20, 0x10, 0x8, 0x4,
};
#endif

int unicode_from_utf8(const uint8_t *p, int max_len, const uint8_t **pp) {
    /*@API utils
       Decode a codepoint from a UTF-8 encoded array
       @argument `p` a valid pointer to the source array of char
       @argument `max_len` the maximum number of bytes to consume,
       must be at least `1`.
       @argument `pp` a pointer to store the updated value of `p`
       @return the codepoint decoded from the array, or `-1` in case of
       error. `*pp` is not updated in this case.
       @note: the maximum length for a UTF-8 byte sequence is 6 bytes.
     */
    // XXX: should return synthetic byte code for invalid UTF-8 encoding
    //      emacs uses 0x3fff80-0x3fffff for invalid bytes 0x80 to 0xff.
    int c, n, b;

    c = *p++;
    if (c < 0x80) {
        *pp = p;
        return c;
    }
    switch (c) {
    case 0xc0: case 0xc1:
        return -1;
    case 0xc2: case 0xc3:
    case 0xc4: case 0xc5: case 0xc6: case 0xc7:
    case 0xc8: case 0xc9: case 0xca: case 0xcb:
    case 0xcc: case 0xcd: case 0xce: case 0xcf:
    case 0xd0: case 0xd1: case 0xd2: case 0xd3:
    case 0xd4: case 0xd5: case 0xd6: case 0xd7:
    case 0xd8: case 0xd9: case 0xda: case 0xdb:
    case 0xdc: case 0xdd: case 0xde: case 0xdf:
        if (max_len < 2)
            return -1;
        b = *p++ ^ 0x80;
        if (b >= 0x40)
            return -1;
        c = ((c & 0x1f) << 6) | b;
        *pp = p;
        return c;
    case 0xe0: case 0xe1: case 0xe2: case 0xe3:
    case 0xe4: case 0xe5: case 0xe6: case 0xe7:
    case 0xe8: case 0xe9: case 0xea: case 0xeb:
    case 0xec: case 0xed: case 0xee: case 0xef:
        n = 1;
        break;
    case 0xf0: case 0xf1: case 0xf2: case 0xf3:
    case 0xf4: case 0xf5: case 0xf6: case 0xf7:
        n = 2;
        break;
    case 0xf8: case 0xf9: case 0xfa: case 0xfb:
        n = 3;
        break;
    case 0xfc: case 0xfd:
        n = 4;
        break;
    default:
        return -1;
    }
    if (max_len < n + 2)
        return -1;
    b = *p++ ^ 0x80;
    if (b >= 0x40)
        return -1;
    c &= 0x1f >> n;
    c = (c << 6) | b;
    if (c < (0x40 >> n))
        return -1;
    while (n --> 0) {
        b = *p++ ^ 0x80;
        if (b >= 0x40)
            return -1;
        c = (c << 6) | b;
    }
    *pp = p;
    return c;
}
