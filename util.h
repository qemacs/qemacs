/*
 * Utilities for qemacs.
 *
 * Copyright (c) 2000-2001 Fabrice Bellard.
 * Copyright (c) 2000-2022 Charlie Gordon.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef UTIL_H
#define UTIL_H

#include <stdlib.h>
#include <string.h>

/* OS specific defines */

#ifdef CONFIG_WIN32
#define snprintf   _snprintf
#define vsnprintf  _vsnprintf
#endif

#if (defined(__GNUC__) || defined(__TINYC__))
/* make sure that the keyword is not disabled by glibc (TINYC case) */
#define qe__attr_printf(a, b)  __attribute__((format(printf, a, b)))
#else
#define qe__attr_printf(a, b)
#endif

#if defined(__GNUC__) && __GNUC__ > 2
#define qe__attr_nonnull(l)   __attribute__((nonnull l))
#define qe__unused__          __attribute__((unused))
#else
#define qe__attr_nonnull(l)
#define qe__unused__
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

#define OWNED     /* ptr attribute for allocated data owned by a structure */

/* prevent gcc warning about shadowing a global declaration */
#define index  index__

typedef unsigned char u8;

static inline char *s8(u8 *p) { return (char*)p; }
static inline const char *cs8(const u8 *p) { return (const char*)p; }

/* string arrays */
typedef struct StringItem {
    void *opaque;  /* opaque data that the user can use */
    char selected; /* true if selected */
    char group;    /* used to group sorted items */
    char str[1];
} StringItem;

typedef struct StringArray {
    int nb_allocated;
    int nb_items;
    StringItem **items;
} StringArray;
#define NULL_STRINGARRAY  { 0, 0, NULL }

typedef struct FindFileState FindFileState;

#define FF_PATH     0x010  /* enumerate path argument */
#define FF_NODIR    0x020  /* only match regular files */
#define FF_NOXXDIR  0x040  /* do not match . or .. */
#define FF_ONLYDIR  0x080  /* do not match non directories */
#define FF_DEPTH    0x00f  /* max recursion depth */

FindFileState *find_file_open(const char *path, const char *pattern, int flags);
int find_file_next(FindFileState *s, char *filename, int filename_size_max);
void find_file_close(FindFileState **sp);
int is_directory(const char *path);
int is_filepattern(const char *filespec);
void canonicalize_path(char *buf, int buf_size, const char *path);
char *make_user_path(char *buf, int buf_size, const char *path);
char *reduce_filename(char *dest, int size, const char *filename);
char *file_load(const char *filename, int max_size, int *sizep);
int match_extension(const char *filename, const char *extlist);
int match_shell_handler(const char *p, const char *list);
int remove_slash(char *buf);
int append_slash(char *buf, int buf_size);
char *makepath(char *buf, int buf_size, const char *path, const char *filename);
void splitpath(char *dirname, int dirname_size,
               char *filename, int filename_size, const char *pathname);

extern unsigned char const qe_digit_value__[128];
static inline int qe_digit_value(int c) {
    return (unsigned int)c < 128 ? qe_digit_value__[c] : 255;
}
static inline int qe_inrange(int c, int a, int b) {
    //return c >= a && c <= b;
    //CG: assuming a <= b and wrap around semantics for (c - a) and (b - a)
    return (unsigned int)(c - a) <= (unsigned int)(b - a);
}
/* character classification tests assume ASCII superset */
static inline int qe_isspace(int c) {
    /* CG: what about \v and \f */
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 160);
}
static inline int qe_isblank(int c) {
    return (c == ' ' || c == '\t' || c == 160);
}
static inline int qe_isdigit(int c) {
    return qe_inrange(c, '0', '9');
}
static inline int qe_isdigit_(int c) {
    return (qe_inrange(c, '0', '9') || c == '_');
}
static inline int qe_isupper(int c) {
    return qe_inrange(c, 'A', 'Z');
}
static inline int qe_isupper_(int c) {
    return (qe_inrange(c, 'A', 'Z') || c == '_');
}
static inline int qe_islower(int c) {
    return qe_inrange(c, 'a', 'z');
}
static inline int qe_islower_(int c) {
    return (qe_inrange(c, 'a', 'z') || (c == '_'));
}
static inline int qe_isalpha(int c) {
    return qe_inrange(c | ('a' - 'A'), 'a', 'z');
}
static inline int qe_isalpha_(int c) {
    return (qe_inrange(c | ('a' - 'A'), 'a', 'z') || c == '_');
}
static inline int qe_isoctdigit(int c) {
    return qe_inrange(c, '0', '7');
}
static inline int qe_isoctdigit_(int c) {
    return qe_inrange(c, '0', '7') || (c == '_');
}
static inline int qe_isbindigit(int c) {
    return qe_inrange(c, '0', '1');
}
static inline int qe_isbindigit_(int c) {
    return qe_inrange(c, '0', '1') || (c == '_');
}
static inline int qe_isxdigit(int c) {
    return qe_digit_value(c) < 16;
}
static inline int qe_isxdigit_(int c) {
    return (qe_digit_value(c) < 16) || (c == '_');
}
static inline int qe_isalnum(int c) {
    return qe_digit_value(c) < 36;
}
static inline int qe_isalnum_(int c) {
    return (qe_digit_value(c) < 36) || (c == '_');
}
static inline int qe_isword(int c) {
    /* XXX: any unicode char >= 128 is considered as word. */
    return qe_isalnum_(c) || (c >= 128);
}
static inline int qe_toupper(int c) {
    return (qe_inrange(c, 'a', 'z') ? c + 'A' - 'a' : c);
}
static inline int qe_tolower(int c) {
    return (qe_inrange(c, 'A', 'Z') ? c + 'a' - 'A' : c);
}
static inline int qe_findchar(const char *str, int c) {
    return qe_inrange(c, 1, 255) && strchr(str, c) != NULL;
}
static inline int qe_indexof(const char *str, int c) {
    if (qe_inrange(c, 1, 255)) {
        const char *p = strchr(str, c);
        if (p) return (int)(p - str);
    }
    return -1;
}

static inline int qe_match2(int c, int c1, int c2) {
    return c == c1 || c == c2;
}

static inline int check_fcall(const unsigned int *str, int i) {
    while (str[i] == ' ')
        i++;
    return str[i] == '(';
}

int ustr_get_identifier(char *buf, int buf_size, int c,
                        const unsigned int *str, int i, int n);
int ustr_get_identifier_lc(char *buf, int buf_size, int c,
                           const unsigned int *str, int i, int n);
int ustr_get_word(char *buf, int buf_size, int c,
                  const unsigned int *str, int i, int n);

int qe_strcollate(const char *s1, const char *s2);
int qe_strtobool(const char *s, int def);
void qe_strtolower(char *buf, int buf_size, const char *str);
int qe_skip_spaces(const char **pp);

static inline int strequal(const char *s1, const char *s2) {
    return !strcmp(s1, s2);
}

int memfind(const char *list, const char *p, int len);
int strfind(const char *list, const char *s);
int strxfind(const char *list, const char *s);
const char *strmem(const char *str, const void *mem, int size);
const void *memstr(const void *buf, int size, const char *str);

#define stristart(str, val, ptr)   qe_stristart(str, val, ptr)
int stristart(const char *str, const char *val, const char **ptr);
int strxstart(const char *str, const char *val, const char **ptr);
int strxcmp(const char *str1, const char *str2);
int strmatchword(const char *str, const char *val, const char **ptr);
int ustrstart(const unsigned int *str, const char *val, int *lenp);
int ustristart(const unsigned int *str, const char *val, int *lenp);
const unsigned int *ustrstr(const unsigned int *str, const char *val);
const unsigned int *ustristr(const unsigned int *str, const char *val);
static inline unsigned int *umemmove(unsigned int *dest,
                                     const unsigned int *src, size_t count) {
    return (unsigned int *)memmove(dest, src, count * sizeof(unsigned int));
}
static inline unsigned int *umemcpy(unsigned int *dest,
                                    const unsigned int *src, size_t count) {
    return (unsigned int *)memcpy(dest, src, count * sizeof(unsigned int));
}
int umemcmp(const unsigned int *s1, const unsigned int *s2, size_t count);
int qe_memicmp(const void *p1, const void *p2, size_t count);
const char *qe_stristr(const char *s1, const char *s2);

int strsubst(char *buf, int buf_size, const char *from,
             const char *s1, const char *s2);
int byte_quote(char *dest, int size, unsigned char c);
int strquote(char *dest, int size, const char *str, int len);
int strunquote(char *dest, int size, const char *str, int len);
int get_str(const char **pp, char *buf, int buf_size, const char *stop);

/* allocation wrappers and utilities */
void *qe_malloc_bytes(size_t size);
void *qe_mallocz_bytes(size_t size);
void *qe_malloc_dup(const void *src, size_t size);
char *qe_strdup(const char *str);
void *qe_realloc(void *pp, size_t size);

#if 0
/* Documentation prototypes */
T *qe_malloc(type T);
/*@API memory
   Allocate memory for an object of type `T`.
   @argument `T` the type of the object to allocate.
   @note this function is implemented as a macro.
 */
T *qe_mallocz(type T);
/*@API memory
   Allocate memory for an object of type `T`.
   The object is initialized to all bits zero.
   @argument `T` the type of the object to allocate.
   @note this function is implemented as a macro.
 */
T *qe_malloc_array(type T, size_t n);
/*@API memory
   Allocate memory for an array of objects of type `T`.
   @argument `T` the type of the object to allocate.
   @argument `n` the number of elements to allocate.
   @note this function is implemented as a macro.
 */
T *qe_mallocz_array(type T, size_t n);
/*@API memory
   Allocate memory for an array of objects of type `T`.
   The objects are initialized to all bits zero.
   @argument `T` the type of the object to allocate.
   @argument `n` the number of elements to allocate.
   @note this function is implemented as a macro.
 */
T *qe_malloc_hack(type T, size_t n);
/*@API memory
   Allocate memory for an object of type `T` with `n` extra bytes.
   @argument `T` the type of the object to allocate.
   @argument `n` the number of bytes to allocate in addition to the size of type `T`.
   @note this function is implemented as a macro.
 */
T *qe_mallocz_array(type T, size_t n);
/*@API memory
   Allocate memory for an object of type `T` with `n` extra bytes.
   The object and the extra space is initialized to all bits zero.
   @argument `T` the type of the object to allocate.
   @argument `n` the number of bytes to allocate in addition to the size of type `T`.
   @note this function is implemented as a macro.
 */
void qe_free(T **pp);
/*@API memory
   Free the allocated memory pointed to by a pointer whose address is passed.
   @argument `pp` the address of a possibly null pointer. This pointer is set
   to `NULL` after freeing the memory. If the pointer memory is null,
   nothing happens.
   @argument `n` the number of bytes to allocate in addition to the size of type `T`.
   @note this function is implemented as a macro.
 */
#endif
#define qe_malloc(t)            ((t *)qe_malloc_bytes(sizeof(t)))
#define qe_mallocz(t)           ((t *)qe_mallocz_bytes(sizeof(t)))
#define qe_malloc_array(t, n)   ((t *)qe_malloc_bytes((n) * sizeof(t)))
#define qe_mallocz_array(t, n)  ((t *)qe_mallocz_bytes((n) * sizeof(t)))
#define qe_malloc_hack(t, n)    ((t *)qe_malloc_bytes(sizeof(t) + (n)))
#define qe_mallocz_hack(t, n)   ((t *)qe_mallocz_bytes(sizeof(t) + (n)))

#if 1  // to test clang -Weverything
#define qe_free(pp)    do  { void *_ = (pp); (free)(*(void **)_); *(void **)_ = NULL; } while (0)
#elif defined CONFIG_HAS_TYPEOF
#define qe_free(pp)    do { typeof(**(pp)) **__ = (pp); (free)(*__); *__ = NULL; } while (0)
#else
#define qe_free(pp)    do if (sizeof(**(pp)) >= 0) { void *_ = (pp); (free)(*(void **)_); *(void **)_ = NULL; } while (0)
#endif

/* prevent the use of regular malloc and free functions */
#ifndef free
#define free(p)       do_not_use_free!!(p)
#endif
#ifndef malloc
#define malloc(s)     do_not_use_malloc!!(s)
#endif
#ifndef realloc
#define realloc(p,s)  do_not_use_realloc!!(p,s)
#endif

/*---- Various string packages: should unify these but keep API simple ----*/

StringItem *set_string(StringArray *cs, int index, const char *str, int group);
StringItem *add_string(StringArray *cs, const char *str, int group);
void free_strings(StringArray *cs);

/*---- Dynamic buffers with static allocation ----*/

typedef struct buf_t buf_t;
struct buf_t {
    char *buf;
    int size, len, pos;
};

static inline buf_t *buf_init(buf_t *bp, char *buf, int size) {
    if (size > 0) {
        bp->buf = buf;
        bp->size = size;
        *buf = '\0';
    } else {
        bp->buf = NULL;
        bp->size = 0;
    }
    bp->len = bp->pos = 0;
    return bp;
}

static inline buf_t *buf_attach(buf_t *bp, char *buf, int size, int pos) {
    /* assuming 0 <= pos < size */
    // XXX: Does not set a null byte?
    bp->buf = buf;
    bp->size = size;
    bp->len = bp->pos = pos;
    return bp;
}
static inline int buf_avail(buf_t *bp) {
    return bp->size - bp->pos - 1;
}
static inline int buf_put_byte(buf_t *bp, int c) {
    if (bp->len < bp->size - 1) {
        bp->buf[bp->len++] = c;
        bp->buf[bp->len] = '\0';
    }
    return bp->pos++;
}
int buf_write(buf_t *bp, const void *src, int size);
static inline int buf_puts(buf_t *bp, const char *str) {
    return buf_write(bp, str, strlen(str));
}

int buf_printf(buf_t *bp, const char *fmt, ...) qe__attr_printf(2,3);
int buf_putc_utf8(buf_t *bp, int c);

/*---- Bounded constant strings used in various parse functions ----*/
typedef struct bstr_t {
    const char *s;
    int len;
} bstr_t;

static inline bstr_t bstr_make(const char *s) {
    bstr_t bs = { s, s ? strlen(s) : 0 };
    return bs;
}

bstr_t bstr_token(const char *s, int sep, const char **pp);
bstr_t bstr_get_nth(const char *s, int n);

static inline int bstr_equal(bstr_t s1, bstr_t s2) {
    /* NULL and empty strings are equivalent */
    return s1.len == s2.len && !memcmp(s1.s, s2.s, s1.len);
}

/* our own implementation of qsort_r() */
void qe_qsort_r(void *base, size_t nmemb, size_t size, void *thunk,
                int (*compar)(void *, const void *, const void *));

/* various arithmetic functions */
static inline int max(int a, int b) {
    if (a > b)
        return a;
    else
        return b;
}

static inline int maxp(int *pa, int b) {
    int a = *pa;
    if (a > b)
        return a;
    else
        return *pa = b;
}

static inline int max3(int a, int b, int c) {
    return max(max(a, b), c);
}

static inline int min(int a, int b) {
    if (a < b)
        return a;
    else
        return b;
}

static inline int minp(int *pa, int b) {
    int a = *pa;
    if (a < b)
        return a;
    else
        return *pa = b;
}

static inline int min3(int a, int b, int c) {
    return min(min(a, b), c);
}

static inline int clamp(int a, int b, int c) {
    if (a < b)
        return b;
    else
    if (a > c)
        return c;
    else
        return a;
}

static inline int clampp(int *pa, int b, int c) {
    int a = *pa;
    if (a < b)
        return *pa = b;
    else
    if (a > c)
        return *pa = c;
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

/*---------------- key definitions ----------------*/

int compose_keys(unsigned int *keys, int *nb_keys);
int strtokey(const char **pp);
int strtokeys(const char *keystr, unsigned int *keys, int max_keys, const char **endp);
int buf_put_key(buf_t *out, int key);
int buf_put_keys(buf_t *out, unsigned int *keys, int nb_keys);
int buf_encode_byte(buf_t *out, unsigned char ch);

#define KEY_CTRL(c)     ((c) & 0x001f)
/* allow combinations such as KEY_META(KEY_LEFT) */
#define KEY_META(c)     ((c) | 0xe100)
#define KEY_ESC1(c)     ((c) | 0xe200)
#define KEY_IS_ESC1(c)     ((c) >= KEY_ESC1(0) && (c) <= KEY_ESC1(0xff))
#define KEY_IS_SPECIAL(c)  ((c) >= 0xe000 && (c) < 0xf000)
#define KEY_IS_CONTROL(c)  (((c) >= 0 && (c) < 32) || (c) == 127)

#define KEY_NONE        0xE000
#define KEY_DEFAULT     0xE001 /* to handle all non special keys */
#define KEY_UNKNOWN     0xE002

#define KEY_TAB         KEY_CTRL('i')
#define KEY_LF          KEY_CTRL('j')
#define KEY_RET         KEY_CTRL('m')
#define KEY_ESC         KEY_CTRL('[')
#define KEY_SPC         0x0020
#define KEY_DEL         127             // kbs
#define KEY_BS          KEY_CTRL('h')   // kbs

#define KEY_UP          KEY_ESC1('A')   // kcuu1
#define KEY_DOWN        KEY_ESC1('B')   // kcud1
#define KEY_RIGHT       KEY_ESC1('C')   // kcuf1
#define KEY_LEFT        KEY_ESC1('D')   // kcub1
#define KEY_CTRL_UP     KEY_ESC1('a')
#define KEY_CTRL_DOWN   KEY_ESC1('b')
#define KEY_CTRL_RIGHT  KEY_ESC1('c')
#define KEY_CTRL_LEFT   KEY_ESC1('d')
#define KEY_CTRL_END    KEY_ESC1('f')
#define KEY_CTRL_HOME   KEY_ESC1('h')
#define KEY_CTRL_PAGEUP KEY_ESC1('i')
#define KEY_CTRL_PAGEDOWN KEY_ESC1('j')
#define KEY_SHIFT_UP     KEY_ESC1('a'+128)
#define KEY_SHIFT_DOWN   KEY_ESC1('b'+128)
#define KEY_SHIFT_RIGHT  KEY_ESC1('c'+128)
#define KEY_SHIFT_LEFT   KEY_ESC1('d'+128)
#define KEY_SHIFT_END    KEY_ESC1('f'+128)
#define KEY_SHIFT_HOME   KEY_ESC1('h'+128)
#define KEY_SHIFT_PAGEUP KEY_ESC1('i'+128)
#define KEY_SHIFT_PAGEDOWN KEY_ESC1('j'+128)
#define KEY_CTRL_SHIFT_UP     KEY_ESC1('a'+64)
#define KEY_CTRL_SHIFT_DOWN   KEY_ESC1('b'+64)
#define KEY_CTRL_SHIFT_RIGHT  KEY_ESC1('c'+64)
#define KEY_CTRL_SHIFT_LEFT   KEY_ESC1('d'+64)
#define KEY_CTRL_SHIFT_END    KEY_ESC1('f'+64)
#define KEY_CTRL_SHIFT_HOME   KEY_ESC1('h'+64)
#define KEY_CTRL_SHIFT_PAGEUP KEY_ESC1('i'+64)
#define KEY_CTRL_SHIFT_PAGEDOWN KEY_ESC1('j'+64)
#define KEY_SHIFT_TAB   KEY_ESC1('Z')   // kcbt
#define KEY_HOME        KEY_ESC1(1)     // khome
#define KEY_INSERT      KEY_ESC1(2)     // kich1
#define KEY_DELETE      KEY_ESC1(3)     // kdch1
#define KEY_END         KEY_ESC1(4)     // kend
#define KEY_PAGEUP      KEY_ESC1(5)     // kpp
#define KEY_PAGEDOWN    KEY_ESC1(6)     // knp
#define KEY_F1          KEY_ESC1(11)
#define KEY_F2          KEY_ESC1(12)
#define KEY_F3          KEY_ESC1(13)
#define KEY_F4          KEY_ESC1(14)
#define KEY_F5          KEY_ESC1(15)
#define KEY_F6          KEY_ESC1(17)
#define KEY_F7          KEY_ESC1(18)
#define KEY_F8          KEY_ESC1(19)
#define KEY_F9          KEY_ESC1(20)
#define KEY_F10         KEY_ESC1(21)
#define KEY_F11         KEY_ESC1(23)
#define KEY_F12         KEY_ESC1(24)
#define KEY_F13         KEY_ESC1(25)
#define KEY_F14         KEY_ESC1(26)
#define KEY_F15         KEY_ESC1(28)
#define KEY_F16         KEY_ESC1(29)
#define KEY_F17         KEY_ESC1(31)
#define KEY_F18         KEY_ESC1(32)
#define KEY_F19         KEY_ESC1(33)
#define KEY_F20         KEY_ESC1(34)

extern unsigned char const utf8_length[256];
static inline int utf8_is_trailing_byte(int c) { return (c & 0xC0) == 0x80; }
int utf8_encode(char *q, int c);
int utf8_decode(const char **pp);
char *utf8_char_to_string(char *buf, int c);
int utf8_to_unicode(unsigned int *dest, int dest_length, const char *str);

int unicode_tty_glyph_width(unsigned int ucs);

static inline int qe_isaccent(int c) {
    return c >= 0x300 && unicode_tty_glyph_width(c) == 0;
}

static inline int qe_iswide(int c) {
    return c >= 0x01000 && unicode_tty_glyph_width(c) > 1;
}

#endif  /* UTIL_H */
