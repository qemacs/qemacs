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

#ifndef CUTILS_H
#define CUTILS_H

#include <stdlib.h>
#include <stdint.h>

/* this cast prevents compiler warnings when removing the const qualifier */
#define unconst(t)  (t)(uintptr_t)

/* These definitions prevent a clash with ffmpeg's cutil module. */

#define strstart(str, val, ptr)    qe_strstart(str, val, ptr)
#define strend(str, val, ptr)      qe_strend(str, val, ptr)
#define pstrcpy(buf, sz, str)      qe_pstrcpy(buf, sz, str)
#define pstrcat(buf, sz, str)      qe_pstrcat(buf, sz, str)
#define pstrncpy(buf, sz, str, n)  qe_pstrncpy(buf, sz, str, n)
#define pstrncat(buf, sz, str, n)  qe_pstrncat(buf, sz, str, n)

/* make sure neither strncpy not strtok are used */
#undef strncpy
#define strncpy(d,s)      do_not_use_strncpy!!(d,s)
#undef strtok
#define strtok(str,sep)   do_not_use_strtok!!(str,sep)

int strstart(const char *str, const char *val, const char **ptr);
int strend(const char *str, const char *val, const char **ptr);
char *pstrcpy(char *buf, int buf_size, const char *str);
char *pstrcat(char *buf, int buf_size, const char *s);
char *pstrncpy(char *buf, int buf_size, const char *s, int len);
char *pstrncat(char *buf, int buf_size, const char *s, int len);
size_t get_basename_offset(const char *filename);
static inline const char *get_basename(const char *filename) {
    /*@API utils
       Get the filename portion of a path.
       Return a pointer to the first character of the filename part of
       the path pointed to by string argument `path`.
       @note call this function for a constant string.
     */
    return filename + get_basename_offset(filename);
}
static inline char *get_basename_nc(char *filename) {
    /*@API utils
       Get the filename portion of a path.
       Return a pointer to the first character of the filename part of
       the path pointed to by string argument `path`.
       @note call this function for a modifiable string.
     */
    return filename + get_basename_offset(filename);
}
size_t get_extension_offset(const char *filename);
static inline const char *get_extension(const char *filename) {
    /*@API utils
       Get the filename extension portion of a path.
       Return a pointer to the first character of the last extension of
       the filename part of the path pointed to by string argument `path`.
       If there is no extension, return a pointer to the null terminator
       and the end of path.
       Leading dots are skipped, they are not considered part of an extension.
       @note call this function for a constant string.
     */
    return filename + get_extension_offset(filename);
}
static inline char *get_extension_nc(char *filename) {
    /*@API utils
       Get the filename extension portion of a path.
       Return a pointer to the first character of the last extension of
       the filename part of the path pointed to by string argument `path`.
       If there is no extension, return a pointer to the null terminator
       and the end of path.
       Leading dots are skipped, they are not considered part of an extension.
       @note call this function for a modifiable string.
     */
    return filename + get_extension_offset(filename);
}
static inline void strip_extension(char *filename) {
    /*@API utils
       Strip the filename extension portion of a path.
       Leading dots are skipped, they are not considered part of an extension.
     */
    filename[get_extension_offset(filename)] = '\0';
}
char *get_dirname(char *dest, int size, const char *file);
static inline long strtol_c(const char *str, const char **endptr, int base) {
    /*@API utils
       Convert the number in the string pointed to by `str` as a `long`.
       Call this function with a constant string and the address of a `const char *`.
     */
    return strtol(str, unconst(char **)endptr, base);
}
static inline long strtoll_c(const char *str, const char **endptr, int base) {
    /*@API utils
       Convert the number in the string pointed to by `str` as a `long long`.
       Call this function with a constant string and the address of a `const char *`.
     */
    return strtoll(str, unconst(char **)endptr, base);
}
static inline double strtod_c(const char *str, const char **endptr) {
    /*@API utils
       Convert the number in the string pointed to by `str` as a `double`.
       Call this function with a constant string and the address of a `const char *`.
     */
    return strtold(str, unconst(char **)endptr);
}
static inline long double strtold_c(const char *str, const char **endptr) {
    /*@API utils
       Convert the number in the string pointed to by `str` as a `long double`.
       Call this function with a constant string and the address of a `const char *`.
     */
    return strtold(str, unconst(char **)endptr);
}

/* Double linked lists. Same API as the linux kernel */

struct list_head {
    struct list_head *next, *prev;
};

static inline int list_empty(struct list_head *head)
{
    return head->next == head;
}

static inline void qe__list_add(struct list_head *elem,
                                struct list_head *prev, struct list_head *next)
{
    next->prev = elem;
    elem->next = next;
    prev->next = elem;
    elem->prev = prev;
}

static inline void qe__list_del(struct list_head *prev, struct list_head *next)
{
    prev->next = next;
    next->prev = prev;
}

#define LIST_HEAD(name) struct list_head name = { &name, &name }

/* add at the head */
#define list_add(elem, head) \
   qe__list_add((struct list_head *)elem, head, (head)->next);

/* add at tail */
#define list_add_tail(elem, head) \
   qe__list_add((struct list_head *)elem, (head)->prev, head)

/* delete */
#define list_del(elem)  qe__list_del(((struct list_head *)elem)->prev,  \
                                     ((struct list_head *)elem)->next)

#define list_for_each(elem, head) \
   for (elem = (void *)(head)->next; elem != (void *)(head); elem = elem->next)

#define list_for_each_safe(elem, elem1, head) \
   for (elem = (void *)(head)->next, elem1 = elem->next; elem != (void *)(head); \
                elem = elem1, elem1 = elem->next)

#define list_for_each_prev(elem, head) \
   for (elem = (void *)(head)->prev; elem != (void *)(head); elem = elem->prev)

#endif
