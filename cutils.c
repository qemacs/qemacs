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

/**
 * Return TRUE if val is a prefix of str. If it returns TRUE, ptr is
 * set to the next character in 'str' after the prefix.
 *
 * @param str input string
 * @param val prefix to test
 * @param ptr updated after the prefix in str in there is a match
 * @return TRUE if there is a match
 */
int strstart(const char *str, const char *val, const char **ptr) {
    size_t i;
    for (i = 0; val[i] != '\0'; i++) {
        if (str[i] != val[i])
            return 0;
    }
    if (ptr)
        *ptr = &str[i];
    return 1;
}

/**
 * Return TRUE if val is a suffix of str. If it returns TRUE, ptr is
 * set to the first character of the suffix in 'str'.
 *
 * @param str input string
 * @param val suffix to test
 * @param ptr updated to the suffix in str in there is a match
 * @return TRUE if there is a match
 */
int strend(const char *str, const char *val, const char **ptr) {
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

/**
 * Copy the string str to buf. If str length is bigger than buf_size -
 * 1 then it is clamped to buf_size - 1.
 * NOTE: this function does what strncpy should have done to be
 * useful. NEVER use strncpy.
 *
 * @param buf destination buffer
 * @param buf_size size of destination buffer
 * @param str source string
 */
char *pstrcpy(char *buf, int size, const char *str) {
    if (size > 0) {
        int i;
        for (i = 0; i < size - 1 && (buf[i] = str[i]) != '\0'; i++)
            continue;
        buf[i] = '\0';
    }
    return buf;
}

/* strcat and truncate. */
char *pstrcat(char *buf, int size, const char *s) {
    int len = strlen(buf);
    if (size > len)
        pstrcpy(buf + len, size - len, s);
    return buf;
}

/* copy the n first char of a string and truncate it. */
char *pstrncpy(char *buf, int size, const char *s, int len) {
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

/* strcat and truncate. */
char *pstrncat(char *buf, int size, const char *s, int slen) {
    int len = strlen(buf);
    if (size > len)
        pstrncpy(buf + len, size - len, s, slen);
    return buf;
}

/* Get the filename portion of a path */
size_t get_basename_offset(const char *p) {
    size_t i, base = 0;

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

/* Return the last extension in a path, ignoring leading dots */
size_t get_extension_offset(const char *p) {
    size_t ext = 0;

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

/* Extract the directory portion of a path:
 * This leaves out the trailing slash if any.  The complete path is
 * obtained by catenating dirname + '/' + basename.
 * if the original path doesn't contain anything dirname is just "."
 */
char *get_dirname(char *dest, int size, const char *file) {
    if (dest && size > 0) {
        size_t i = 0;
        if (file) {
            pstrcpy(dest, size, file);
            i = get_basename_offset(dest);
            /* remove the trailing slash unless root dir or
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
