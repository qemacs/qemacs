#ifndef CUTILS_H
#define CUTILS_H

#include <stdlib.h>

int strstart(const char *str, const char *val, const char **ptr);
int stristart(const char *str, const char *val, const char **ptr);
void pstrcpy(char *buf, int buf_size, const char *str);
char *pstrcat(char *buf, int buf_size, const char *s);

/* simple dynamic strings wrappers. The strings are always terminated
   by zero except if they are empty. */

typedef struct QString {
    unsigned char *data;
    int len; /* string length excluding trailing '\0' */
} QString;

static inline void qstrinit(QString *q)
{
    q->data = NULL;
    q->len = 0;
}

static inline void qstrfree(QString *q)
{
    free(q->data);
}

int qmemcat(QString *q, const unsigned char *data1, int len1);
int qstrcat(QString *q, const char *str);
int qprintf(QString *q, const char *fmt, ...);

/* Double linked lists. Same api as the linux kernel */

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
