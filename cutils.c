/*
 * Various simple C utilities
 *
 * Copyright (c) 2000-2002 Fabrice Bellard.
 * Copyright (c) 2000-2022 Charlie Gordon.
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

#include <string.h>

#include "config.h"     /* for CONFIG_WIN32 */
#include "cutils.h"

/* these functions are duplicated from ffmpeg/libavformat/cutils.c
 * conflict is resolved by redefining the symbols in cutils.h
 */

int strstart(const char *str, const char *val, const char **ptr) {
    /*@API utils
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
    /*@API utils
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

char *pstrcpy(char *buf, int size, const char *str) {
    /*@API utils
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
    /*@API utils
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
    /*@API utils
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
    /*@API utils
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

size_t get_basename_offset(const char *path) {
    /*@API utils
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
    /*@API utils
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
    /*@API utils
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
