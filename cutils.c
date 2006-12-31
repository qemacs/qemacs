/*
 * Various simple C utilities
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#include "cutils.h"

#if !defined(CONFIG_NOCUTILS)
/* these functions are defined in ffmpeg/libavformat/cutils.c and
 * conflict with this module because of extra functions referenced
 * in the ffmpeg module.  This module should not be linked with
 * qemacs with ffmpeg support.
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

#if 0
/* need this for >= 256 */
static inline int utoupper(int c)
{
    if (c >= 'a' && c <= 'z')
        c += 'A' - 'a';
    return c;
}
#endif

/**
 * Return TRUE if val is a prefix of str (case independent).
 * If it returns TRUE, ptr is set to the next character in 'str' after
 * the prefix.
 * Spaces, dashes and underscores are also ignored in this comparison.
 *
 * @param str input string
 * @param val prefix to test
 * @param ptr updated after the prefix in str in there is a match
 * @return TRUE if there is a match */
int stristart(const char *str, const char *val, const char **ptr)
{
    const char *p, *q;
    p = str;
    q = val;
    while (*q != '\0') {
        if (toupper(*(const unsigned char *)p) !=
              toupper(*(const unsigned char *)q))
        {
            if (*p == '-' || *p == '_' || *p == ' ') {
                p++;
                continue;
            }
            if (*q == '-' || *q == '_' || *q == ' ') {
                q++;
                continue;
            }
            return 0;
        }
        p++;
        q++;
    }
    if (ptr)
        *ptr = p;
    return 1;
}

/**
 * Compare strings str1 and str2 case independently.
 * Spaces, dashes and underscores are also ignored in this comparison.
 *
 * @param str1 input string 1 (left operand)
 * @param str2 input string 2 (right operand)
 * @return -1, 0, +1 reflecting the sign of str1 <=> str2
 */
int stricmp(const char *str1, const char *str2)
{
    const char *p, *q;
    p = str1;
    q = str2;
    for (;;) {
        if (toupper(*(const unsigned char *)p) !=
              toupper(*(const unsigned char *)q))
        {
            if (*p == '-' || *p == '_' || *p == ' ') {
                p++;
                continue;
            }
            if (*q == '-' || *q == '_' || *q == ' ') {
                q++;
                continue;
            }
            return (toupper(*(const unsigned char *)p) <
                    toupper(*(const unsigned char *)q)) ? -1 : +1;
        }
        if (!*p)
            break;
        p++;
        q++;
    }
    return 0;
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
void pstrcpy(char *buf, int buf_size, const char *str)
{
    int c;
    char *q = buf;

    if (buf_size <= 0)
        return;

    for (;;) {
        c = *str++;
        if (c == 0 || q >= buf + buf_size - 1)
            break;
        *q++ = c;
    }
    *q = '\0';
}

/* strcat and truncate. */
char *pstrcat(char *buf, int buf_size, const char *s)
{
    int len;
    len = strlen(buf);
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

#endif
