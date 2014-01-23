/*
 * Various simple C utilities
 *
 * Copyright (c) 2000-2002 Fabrice Bellard.
 * Copyright (c) 2000-2014 Charlie Gordon.
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

/* These definitions prevent a clash with ffmpeg's cutil module. */

#define strstart(str, val, ptr)    qe_strstart(str, val, ptr)
#define pstrcpy(buf, sz, str)      qe_pstrcpy(buf, sz, str)
#define pstrcat(buf, sz, str)      qe_pstrcat(buf, sz, str)
#define pstrncpy(buf, sz, str, n)  qe_pstrncpy(buf, sz, str, n)

#undef strncpy
#define strncpy(d,s)      do_not_use_strncpy!!(d,s)
#undef strtok
#define strtok(str,sep)   do_not_use_strtok!!(str,sep)

int strstart(const char *str, const char *val, const char **ptr);
char *pstrcpy(char *buf, int buf_size, const char *str);
char *pstrcat(char *buf, int buf_size, const char *s);
char *pstrncpy(char *buf, int buf_size, const char *s, int len);
const char *get_basename(const char *filename);
static inline char *get_basename_nc(char *filename) {
    return (char *)get_basename(filename);
}
static inline int get_basename_offset(const char *filename) {
    return get_basename(filename) - filename;
}
const char *get_extension(const char *filename);
static inline char *get_extension_nc(char *filename) {
    return (char *)get_extension(filename);
}
static inline int get_extension_offset(const char *filename) {
    return get_extension(filename) - filename;
}
static inline void strip_extension(char *filename) {
    filename[get_extension(filename) - filename] = '\0';
}
char *get_dirname(char *dest, int size, const char *file);

/* Double linked lists. Same API as the linux kernel */

struct list_head {
    struct list_head *next, *prev;
};

static inline int list_empty(struct list_head *head)
{
    return head->next == head;
}

static inline void __list_add(struct list_head *elem,
                              struct list_head *prev, struct list_head *next)
{
    next->prev = elem;
    elem->next = next;
    prev->next = elem;
    elem->prev = prev;
}

static inline void __list_del(struct list_head *prev, struct list_head *next)
{
    prev->next = next;
    next->prev = prev;
}

#define LIST_HEAD(name) struct list_head name = { &name, &name }

/* add at the head */
#define list_add(elem, head) \
   __list_add((struct list_head *)elem, head, (head)->next);

/* add at tail */
#define list_add_tail(elem, head) \
   __list_add((struct list_head *)elem, (head)->prev, head)

/* delete */
#define list_del(elem) __list_del(((struct list_head *)elem)->prev,  \
                                  ((struct list_head *)elem)->next)

#define list_for_each(elem, head) \
   for (elem = (void *)(head)->next; elem != (void *)(head); elem = elem->next)

#define list_for_each_safe(elem, elem1, head) \
   for (elem = (void *)(head)->next, elem1 = elem->next; elem != (void *)(head); \
                elem = elem1, elem1 = elem->next)

#define list_for_each_prev(elem, head) \
   for (elem = (void *)(head)->prev; elem != (void *)(head); elem = elem->prev)

#endif
