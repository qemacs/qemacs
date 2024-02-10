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

#ifndef CUTILS_H
#define CUTILS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if (defined(__GNUC__) || defined(__TINYC__))
/* make sure that the keyword is not disabled by glibc (TINYC case) */
#define qe__attr_printf(a, b)  __attribute__((format(printf, a, b)))
#else
#define qe__attr_printf(a, b)
#endif

#if defined(__GNUC__) && __GNUC__ > 2
#define qe__attr_nonnull(l)   __attribute__((nonnull l))
#define qe__unused__          __attribute__((unused))
#define likely(x)             __builtin_expect(!!(x), 1)
#define unlikely(x)           __builtin_expect(!!(x), 0)
#define __maybe_unused        __attribute__((unused))
#define fallthrough           __attribute__((__fallthrough__))
#else
#define qe__attr_nonnull(l)
#define qe__unused__
#define likely(x)       (x)
#define unlikely(x)     (x)
#define __maybe_unused
#define fallthrough           do {} while (0)
#endif

#ifndef offsetof
#define offsetof(s,m)  ((size_t)(&((s *)0)->m))
#endif
#ifndef countof
#define countof(a)  ((int)(sizeof(a) / sizeof((a)[0])))
#endif
#ifndef ssizeof
#define ssizeof(a)  ((int)(sizeof(a)))
#endif

typedef int BOOL;

#ifndef FALSE
enum {
    FALSE = 0,
    TRUE = 1,
};
#endif

typedef unsigned int char32_t;  // possible conflict with <uchar.h>

#ifndef U8_DEFINED
typedef unsigned char u8;
#define U8_DEFINED  1
#endif

static inline char *s8(u8 *p) { return (char*)p; }
static inline const char *cs8(const u8 *p) { return (const char*)p; }

/* this cast prevents compiler warnings when removing the const qualifier */
#define unconst(t)  (t)(uintptr_t)

/* These definitions prevent a clash with ffmpeg's cutil module. */

#define pstrcpy(buf, sz, str)      qe_pstrcpy(buf, sz, str)
#define pstrcat(buf, sz, str)      qe_pstrcat(buf, sz, str)
#define pstrncpy(buf, sz, str, n)  qe_pstrncpy(buf, sz, str, n)
#define pstrncat(buf, sz, str, n)  qe_pstrncat(buf, sz, str, n)
#define strstart(str, val, ptr)    qe_strstart(str, val, ptr)
#define strend(str, val, ptr)      qe_strend(str, val, ptr)

/* make sure neither strncpy not strtok are used */
#undef strncpy
#define strncpy(d,s)      do_not_use_strncpy!!(d,s)
#undef strtok
#define strtok(str,sep)   do_not_use_strtok!!(str,sep)

char *pstrcpy(char *buf, int buf_size, const char *str);
char *pstrcat(char *buf, int buf_size, const char *s);
char *pstrncpy(char *buf, int buf_size, const char *s, int len);
char *pstrncat(char *buf, int buf_size, const char *s, int len);
int strstart(const char *str, const char *val, const char **ptr);
int strend(const char *str, const char *val, const char **ptr);

static inline int strequal(const char *s1, const char *s2) {
    return !strcmp(s1, s2);
}

/* Use these macros to avoid stupid size mistakes:
 * n it a number of items
 * p1 and p2 should have the same type
 * the macro checks that the types pointed to by p1 and p2 have the same size
 */
#define blockcmp(p1, p2, n)   memcmp(p1, p2, (n) * sizeof *(p1) / (sizeof *(p1) == sizeof *(p2)))
#define blockcpy(p1, p2, n)   memcpy(p1, p2, (n) * sizeof *(p1) / (sizeof *(p1) == sizeof *(p2)))
#define blockmove(p1, p2, n) memmove(p1, p2, (n) * sizeof *(p1) / (sizeof *(p1) == sizeof *(p2)))

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

/* various arithmetic functions */
static inline int clamp_int(int a, int b, int c) {
    if (a < b)
        return b;
    else
    if (a > c)
        return c;
    else
        return a;
}

static inline int compute_percent(int a, int b) {
    return b <= 0 ? 0 : (int)((long long)a * 100 / b);
}

static inline int align(int a, int n) {
    return (a / n) * n;
}

static inline int scale(int a, int b, int c) {
    return (a * b + c / 2) / c;
}

static inline int max_int(int a, int b) {
    if (a > b)
        return a;
    else
        return b;
}

static inline int min_int(int a, int b) {
    if (a < b)
        return a;
    else
        return b;
}

static inline int max3_int(int a, int b, int c) {
    return max_int(max_int(a, b), c);
}

static inline int min3_int(int a, int b, int c) {
    return min_int(min_int(a, b), c);
}

static inline int max_uint(unsigned int a, unsigned int b) {
    if (a > b)
        return a;
    else
        return b;
}

static inline int min_uint(unsigned int a, unsigned int b) {
    if (a < b)
        return a;
    else
        return b;
}

static inline uint32_t max_uint32(uint32_t a, uint32_t b) {
    if (a > b)
        return a;
    else
        return b;
}

static inline uint32_t min_uint32(uint32_t a, uint32_t b) {
    if (a < b)
        return a;
    else
        return b;
}

static inline int64_t max_int64(int64_t a, int64_t b) {
    if (a > b)
        return a;
    else
        return b;
}

static inline int64_t min_int64(int64_t a, int64_t b) {
    if (a < b)
        return a;
    else
        return b;
}

/* WARNING: undefined if a = 0 */
static inline int clz32(unsigned int a)
{
    return __builtin_clz(a);
}

/* WARNING: undefined if a = 0 */
static inline int clz64(uint64_t a)
{
    return __builtin_clzll(a);
}

/* WARNING: undefined if a = 0 */
static inline int ctz32(unsigned int a)
{
    return __builtin_ctz(a);
}

/* WARNING: undefined if a = 0 */
static inline int ctz64(uint64_t a)
{
    return __builtin_ctzll(a);
}

struct __attribute__((packed)) packed_u64 {
    uint64_t v;
};

struct __attribute__((packed)) packed_u32 {
    uint32_t v;
};

struct __attribute__((packed)) packed_u16 {
    uint16_t v;
};

static inline uint64_t get_u64(const uint8_t *tab)
{
    return ((const struct packed_u64 *)tab)->v;
}

static inline int64_t get_i64(const uint8_t *tab)
{
    return (int64_t)((const struct packed_u64 *)tab)->v;
}

static inline void put_u64(uint8_t *tab, uint64_t val)
{
    ((struct packed_u64 *)tab)->v = val;
}

static inline uint32_t get_u32(const uint8_t *tab)
{
    return ((const struct packed_u32 *)tab)->v;
}

static inline int32_t get_i32(const uint8_t *tab)
{
    return (int32_t)((const struct packed_u32 *)tab)->v;
}

static inline void put_u32(uint8_t *tab, uint32_t val)
{
    ((struct packed_u32 *)tab)->v = val;
}

static inline uint32_t get_u16(const uint8_t *tab)
{
    return ((const struct packed_u16 *)tab)->v;
}

static inline int32_t get_i16(const uint8_t *tab)
{
    return (int16_t)((const struct packed_u16 *)tab)->v;
}

static inline void put_u16(uint8_t *tab, uint16_t val)
{
    ((struct packed_u16 *)tab)->v = val;
}

static inline uint32_t get_u8(const uint8_t *tab)
{
    return *tab;
}

static inline int32_t get_i8(const uint8_t *tab)
{
    return (int8_t)*tab;
}

static inline void put_u8(uint8_t *tab, uint8_t val)
{
    *tab = val;
}

static inline uint16_t bswap16(uint16_t x)
{
    return (x >> 8) | (x << 8);
}

static inline uint32_t bswap32(uint32_t v)
{
    return ((v & 0xff000000) >> 24) | ((v & 0x00ff0000) >>  8) |
        ((v & 0x0000ff00) <<  8) | ((v & 0x000000ff) << 24);
}

static inline uint64_t bswap64(uint64_t v)
{
    return ((v & ((uint64_t)0xff << (7 * 8))) >> (7 * 8)) |
        ((v & ((uint64_t)0xff << (6 * 8))) >> (5 * 8)) |
        ((v & ((uint64_t)0xff << (5 * 8))) >> (3 * 8)) |
        ((v & ((uint64_t)0xff << (4 * 8))) >> (1 * 8)) |
        ((v & ((uint64_t)0xff << (3 * 8))) << (1 * 8)) |
        ((v & ((uint64_t)0xff << (2 * 8))) << (3 * 8)) |
        ((v & ((uint64_t)0xff << (1 * 8))) << (5 * 8)) |
        ((v & ((uint64_t)0xff << (0 * 8))) << (7 * 8));
}

/* XXX: should take an extra argument to pass slack information to the caller */
typedef void *DynBufReallocFunc(void *opaque, void *ptr, size_t size);

typedef struct DynBuf {
    uint8_t *buf;
    size_t size;
    size_t allocated_size;
    BOOL error; /* true if a memory allocation error occurred */
    DynBufReallocFunc *realloc_func;
    void *opaque; /* for realloc_func */
} DynBuf;

void dbuf_init(DynBuf *s);
void dbuf_init2(DynBuf *s, void *opaque, DynBufReallocFunc *realloc_func);
int dbuf_realloc(DynBuf *s, size_t new_size);
int dbuf_write(DynBuf *s, size_t offset, const uint8_t *data, size_t len);
int dbuf_put(DynBuf *s, const uint8_t *data, size_t len);
int dbuf_put_self(DynBuf *s, size_t offset, size_t len);
int dbuf_putc(DynBuf *s, uint8_t c);
int dbuf_putstr(DynBuf *s, const char *str);
static inline int dbuf_put_u16(DynBuf *s, uint16_t val) {
    return dbuf_put(s, (uint8_t *)&val, 2);
}
static inline int dbuf_put_u32(DynBuf *s, uint32_t val) {
    return dbuf_put(s, (uint8_t *)&val, 4);
}
static inline int dbuf_put_u64(DynBuf *s, uint64_t val) {
    return dbuf_put(s, (uint8_t *)&val, 8);
}
int __attribute__((format(printf, 2, 3))) dbuf_printf(DynBuf *s,
                                                      const char *fmt, ...);
void dbuf_free(DynBuf *s);
static inline BOOL dbuf_error(DynBuf *s) {
    return s->error;
}
static inline void dbuf_set_error(DynBuf *s) {
    s->error = TRUE;
}

#define UTF8_CHAR_LEN_MAX 6

int unicode_to_utf8(uint8_t *buf, unsigned int c);
int unicode_from_utf8(const uint8_t *p, int max_len, const uint8_t **pp);

static inline int from_hex(int c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    else if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    else if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    else
        return -1;
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
   qe__list_add((struct list_head *)elem, head, (head)->next)

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
