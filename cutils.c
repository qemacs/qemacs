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
#include <stdarg.h>
#include <ctype.h>

#include "cutils.h"

#if !defined(CONFIG_NOCUTILS)
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
 * Return TRUE if val is a prefix of str (case independent). If it
 * returns TRUE, ptr is set to the next character in 'str' after the
 * prefix.
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
        if (toupper(*(unsigned char *)p) != toupper(*(unsigned char *)q))
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
void pstrcpy(char *buf, int buf_size, const char *str)
{
    int c;
    char *q = buf;

    if (buf_size <= 0)
        return;

    for(;;) {
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

#endif

/**
 * Add a memory region to a dynamic string. In case of allocation
 * failure, the data is not added. The dynamic string is guaranted to
 * be 0 terminated, although it can be longer if it contains zeros.
 *
 * @return 0 if OK, -1 if allocation error.  
 */
int qmemcat(QString *q, const unsigned char *data1, int len1)
{
    int new_len, len, alloc_size;
    unsigned char *data;

    data = q->data;
    len = q->len;
    new_len = len + len1;
    /* see if we got a new power of two */
    /* NOTE: we got this trick from the excellent 'links' browser */
    if ((len ^ new_len) >= len) {
        /* find immediately bigger 2^n - 1 */
        alloc_size = new_len;
        alloc_size |= (alloc_size >> 1);
        alloc_size |= (alloc_size >> 2);
        alloc_size |= (alloc_size >> 4);
        alloc_size |= (alloc_size >> 8);
        alloc_size |= (alloc_size >> 16);
        /* allocate one more byte for end of string marker */
        data = realloc(data, alloc_size + 1);
        if (!data)
            return -1;
        q->data = data;
    }
    memcpy(data + len, data1, len1);
    data[new_len] = '\0'; /* we force a trailing '\0' */
    q->len = new_len;
    return 0;
}

/*
 * add a string to a dynamic string
 */
int qstrcat(QString *q, const char *str)
{
    return qmemcat(q, str, strlen(str));
}

/* XXX: we use a fixed size buffer */
int qprintf(QString *q, const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    int len, ret;

    va_start(ap, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, ap);
    /* avoid problems for non C99 snprintf() which can return -1 if overflow */
    if (len < 0)
        len = strlen(buf);
    ret = qmemcat(q, buf, len);
    va_end(ap);
    return ret;
}

