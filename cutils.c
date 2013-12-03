/*
 * Various simple C utilities
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
int strstart(const char *str, const char *val, const char **ptr)
{
    const char *p, *q;
    p = str;
    q = val;
    while (*q != '\0') {
        if (*p != *q)
            return 0;
        p++;
        q++;
    }
    if (ptr)
        *ptr = p;
    return 1;
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
char *pstrcpy(char *buf, int buf_size, const char *str)
{
    int c;
    char *q = buf;

    if (buf_size <= 0)
        return buf;

    for (;;) {
        c = *str++;
        if (c == '\0' || q >= buf + buf_size - 1)
            break;
        *q++ = c;
    }
    *q = '\0';
    return buf;
}

/* strcat and truncate. */
char *pstrcat(char *buf, int buf_size, const char *s)
{
    int len = strlen(buf);

    if (len < buf_size)
        pstrcpy(buf + len, buf_size - len, s);
    return buf;
}

/* copy the n first char of a string and truncate it. */
char *pstrncpy(char *buf, int buf_size, const char *s, int len)
{
    char *q;
    int c;

    if (buf_size > 0) {
        q = buf;
        if (len >= buf_size)
            len = buf_size - 1;
        while (len > 0) {
            c = *s++;
            if (c == '\0')
                break;
            *q++ = c;
            len--;
        }
        *q = '\0';
    }
    return buf;
}

/* Get the filename portion of a path */
const char *get_basename(const char *filename)
{
    const char *p;
    const char *base;

    base = filename;
    if (base) {
        for (p = base; *p; p++) {
#ifdef CONFIG_WIN32
            /* Simplistic DOS/Windows filename support */
            if (*p == '/' || *p == '\\' || (*p == ':' && p == filename + 1))
                base = p + 1;
#else
            if (*p == '/')
                base = p + 1;
#endif
        }
    }
    return base;
}

/* Return the last extension in a path, ignoring leading dots */
const char *get_extension(const char *filename)
{
    const char *p, *ext;

    p = get_basename(filename);
    ext = NULL;
    if (p) {
        while (*p == '.')
            p++;
        for (; *p; p++) {
            if (*p == '.')
                ext = p;
        }
        if (!ext)
            ext = p;
    }
    return ext;
}

/* Extract the directory portion of a path:
 * This leaves out the trailing slash if any.  The complete path is
 * obtained by catenating dirname + '/' + basename.
 * if the original path doesn't contain anything dirname is just "."
 */
char *get_dirname(char *dest, int size, const char *file)
{
    char *p;

    if (dest) {
        p = dest;
        if (file) {
            pstrcpy(dest, size, file);
            p = get_basename_nc(dest);
            if (p > dest + 1 && p[-1] != ':' && p[-2] != ':')
                p--;

            if (p == dest)
                *p++ = '.';
        }
        *p = '\0';
    }
    return dest;
}
