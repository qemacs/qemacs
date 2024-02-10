/*
 * Utilities for qemacs.
 *
 * Copyright (c) 2000-2001 Fabrice Bellard.
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

#ifndef UTIL_H
#define UTIL_H

#include "cutils.h"

/* OS specific defines */

// XXX: should include config.h for CONFIG_WIN32 and CONFIG_HAS_TYPEOF

#ifdef CONFIG_WIN32
#define snprintf   _snprintf
#define vsnprintf  _vsnprintf
#endif

#define OWNED     /* ptr attribute for allocated data owned by a structure */

#define QASSERT(e)   do { if (!(e)) fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__, __LINE__, #e); } while (0)

/* prevent gcc warning about shadowing a global declaration */
#define index  index__

#define MAX_WORD_SIZE  128
#define MAX_FILENAME_SIZE 1024

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
// XXX: should move these to cutils?
int remove_slash(char *buf);
int append_slash(char *buf, int buf_size);
char *makepath(char *buf, int buf_size, const char *path, const char *filename);
void splitpath(char *dirname, int dirname_size,
               char *filename, int filename_size, const char *pathname);

/*---- Buffer offset functions ----*/

/* buffer offsets are 32-bit signed integers */
#define min_offset(a, b)  min_int(a, b)
#define max_offset(a, b)  max_int(a, b)
#define clamp_offset(a, b, c)  clamp_int(a, b, c)

/*---- Character classification functions ----*/

/* Character classification tests work for ASCII, Latin1 and Unicode */

extern unsigned char const qe_digit_value__[128];

static inline int qe_digit_value(char32_t c) {
    return c < 128 ? qe_digit_value__[c] : 255;
}

static inline int qe_inrange(char32_t c, char32_t a, char32_t b) {
    //return c >= a && c <= b;
    //CG: assuming a <= b and wrap around semantics for (c - a) and (b - a)
    return (unsigned int)(c - a) <= (unsigned int)(b - a);
}

static inline int qe_isspace(char32_t c) {
    /* CG: what about \v and \f */
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 160);
}

static inline int qe_isblank(char32_t c) {
    return (c == ' ' || c == '\t' || c == 160);
}

static inline int qe_isdigit(char32_t c) {
    return qe_inrange(c, '0', '9');
}

static inline int qe_isdigit_(char32_t c) {
    return (qe_inrange(c, '0', '9') || c == '_');
}

static inline int qe_isupper(char32_t c) {
    return qe_inrange(c, 'A', 'Z');
}

static inline int qe_isupper_(char32_t c) {
    return (qe_inrange(c, 'A', 'Z') || c == '_');
}

static inline int qe_islower(char32_t c) {
    return qe_inrange(c, 'a', 'z');
}

static inline int qe_islower_(char32_t c) {
    return (qe_inrange(c, 'a', 'z') || (c == '_'));
}

static inline int qe_isalpha(char32_t c) {
    return qe_inrange(c | ('a' - 'A'), 'a', 'z');
}

static inline int qe_isalpha_(char32_t c) {
    return (qe_inrange(c | ('a' - 'A'), 'a', 'z') || c == '_');
}

static inline int qe_isoctdigit(char32_t c) {
    return qe_inrange(c, '0', '7');
}

static inline int qe_isoctdigit_(char32_t c) {
    return qe_inrange(c, '0', '7') || (c == '_');
}

static inline int qe_isbindigit(char32_t c) {
    return qe_inrange(c, '0', '1');
}

static inline int qe_isbindigit_(char32_t c) {
    return qe_inrange(c, '0', '1') || (c == '_');
}

static inline int qe_isxdigit(char32_t c) {
    return qe_digit_value(c) < 16;
}

static inline int qe_isxdigit_(char32_t c) {
    return (qe_digit_value(c) < 16) || (c == '_');
}

static inline int qe_isalnum(char32_t c) {
    return qe_digit_value(c) < 36;
}

static inline int qe_isalnum_(char32_t c) {
    return (qe_digit_value(c) < 36) || (c == '_');
}

static inline int qe_isword(char32_t c) {
    /* XXX: any unicode char >= 128 is considered as word. */
    // XXX: should use wc version with tables
    return qe_isalnum_(c) || (c >= 128);
}

static inline char32_t qe_tolower(char32_t c) {
    return (qe_inrange(c, 'A', 'Z') ? c + 'a' - 'A' : c);
}

static inline char32_t qe_toupper(char32_t c) {
    return (qe_inrange(c, 'a', 'z') ? c + 'A' - 'a' : c);
}

static inline int qe_findchar(const char *str, char32_t c) {
    return qe_inrange(c, 1, 127) && strchr(str, c) != NULL;
}

static inline int qe_indexof(const char *str, char32_t c) {
    if (qe_inrange(c, 1, 127)) {
        const char *p = strchr(str, c);
        if (p) return (int)(p - str);
    }
    return -1;
}

static inline int qe_match2(char32_t c, char32_t c1, char32_t c2) {
    return c == c1 || c == c2;
}

int qe_skip_spaces(const char **pp);
int qe_strcollate(const char *s1, const char *s2);
int qe_strtobool(const char *s, int def);
void qe_strtolower(char *buf, int buf_size, const char *str);
int memfind(const char *list, const char *p, int len);
int strfind(const char *list, const char *s);
int strxfind(const char *list, const char *s);
const char *strmem(const char *str, const void *mem, int size);
const void *memstr(const void *buf, int size, const char *str);
int qe_memicmp(const void *p1, const void *p2, size_t count);
const char *qe_stristr(const char *s1, const char *s2);
#define stristart(str, val, ptr)   qe_stristart(str, val, ptr)
int stristart(const char *str, const char *val, const char **ptr);
int strxstart(const char *str, const char *val, const char **ptr);
int strxcmp(const char *str1, const char *str2);
int strmatchword(const char *str, const char *val, const char **ptr);
int strmatch_pat(const char *str, const char *pat, int start);
int utf8_strimatch_pat(const char *str, const char *pat, int start);
int get_str(const char **pp, char *buf, int buf_size, const char *stop);

int strsubst(char *buf, int buf_size, const char *from,
             const char *s1, const char *s2);
int byte_quote(char *dest, int size, unsigned char c);
int strquote(char *dest, int size, const char *str, int len);
int strunquote(char *dest, int size, const char *str, int len);

/*---- Unicode string functions ----*/

int ustrstart(const char32_t *str, const char *val, int *lenp);
const char32_t *ustrstr(const char32_t *str, const char *val);
int ustristart(const char32_t *str, const char *val, int *lenp);
const char32_t *ustristr(const char32_t *str, const char *val);
int umemcmp(const char32_t *s1, const char32_t *s2, size_t count);

static inline char32_t *umemcpy(char32_t *dest, const char32_t *src, size_t count) {
    return blockcpy(dest, src, count);
}

static inline char32_t *umemmove(char32_t *dest, const char32_t *src, size_t count) {
    return blockmove(dest, src, count);
}

int ustr_get_identifier(char *buf, int buf_size, char32_t c,
                        const char32_t *str, int i, int n);
int ustr_get_identifier_lc(char *buf, int buf_size, char32_t c,
                           const char32_t *str, int i, int n);
int ustr_match_keyword(const char32_t *buf, const char *str, int *lenp);
int utf8_get_word(char *buf, int buf_size, char32_t c,
                  const char32_t *str, int i, int n);

static inline int check_fcall(const char32_t *str, int i) {
    while (str[i] == ' ')
        i++;
    return str[i] == '(';
}

/*---- Allocation wrappers and utilities ----*/

void *qe_malloc_bytes(size_t size);
void *qe_mallocz_bytes(size_t size);
void *qe_malloc_dup(const void *src, size_t size);
char *qe_strdup(const char *str);
void *qe_realloc(void *pp, size_t size);

#if 0  /* Documentation prototypes */

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
#define qe_free(pp)    do { void *_1 = (pp), **_2 = _1; (free)(*_2); *_2 = NULL; } while (0)
#elif defined CONFIG_HAS_TYPEOF
#define qe_free(pp)    do { typeof(**(pp)) **_2 = (pp); (free)(*_2); *_2 = NULL; } while (0)
#else
#define qe_free(pp)    do if (sizeof(**(pp)) >= 0) { void *_1 = (pp), **_2 = _1; (free)(*_2); *_2 = NULL; } while (0)
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

/*---- StringArray functions ----*/

// XXX: Should unify these string packages but keep API simple

StringItem *set_string(StringArray *cs, int index, const char *str, int group);
StringItem *add_string(StringArray *cs, const char *str, int group);
int remove_string(StringArray *cs, const char *str);
void free_strings(StringArray *cs);

/*---- Dynamic buffers with static allocation ----*/

typedef struct buf_t buf_t;
struct buf_t {
    char *buf;  /* pointer to the array holding the dynamic string */
    int size;   /* size of the array pointed to by buf */
    int len;    /* length of output in buf. buf is null terminated */
    int pos;    /* output position into or beyond the end of buf */
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

static inline int buf_put_byte(buf_t *bp, unsigned char ch) {
    if (bp->len < bp->size - 1) {
        bp->buf[bp->len++] = ch;
        bp->buf[bp->len] = '\0';
    }
    return bp->pos++;
}

int buf_write(buf_t *bp, const void *src, int size);

static inline int buf_puts(buf_t *bp, const char *str) {
    return buf_write(bp, str, strlen(str));
}

int buf_printf(buf_t *bp, const char *fmt, ...) qe__attr_printf(2,3);
int buf_putc_utf8(buf_t *bp, char32_t c);

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

/*---- our own implementation of qsort_r() ----*/

void qe_qsort_r(void *base, size_t nmemb, size_t size, void *thunk,
                int (*compar)(void *, const void *, const void *));

/*---- key definitions and functions ----*/

int compose_keys(unsigned int *keys, int *nb_keys);
int strtokey(const char **pp);
int strtokeys(const char *keystr, unsigned int *keys, int max_keys, const char **endp);
int buf_put_key(buf_t *out, int key);
int buf_put_keys(buf_t *out, unsigned int *keys, int nb_keys);
int buf_encode_byte(buf_t *out, unsigned char ch);

/* XXX: should use a more regular key mapping scheme:
   - 0000..001F: standard control keys: KEY_CTRL('@') to KEY_CTRL('@') to KEY_CTRL('_')
   - 0020: SPC
   - 0021..007E: ASCII characters (self insert)
   - 007F: DEL
   - 0080..DFFF: Unicode code points
   - E000..EFFF: surrogates used for function and modified keys
   - E000..E0FF: special keys: KEY_NONE, KEY_DEFAULT, KEY_INVALID
   - E100..E1FF: function keys: KEY_F1, KEY_UP ...
   - E200: M- modifier
   - E400: S- modifier (S- modified non function keys unavailable from terminal)
   - E800: C- modifier (C- modified non function keys unavailable from terminal)
   - F000..1FFFF: Unicode code points
   If using 32-bit key codes, could use higher modifier bits:
   Make these bits consistent X11 modifier keys.
   - 010000: Shift (S-)     - ShiftMask      (1<<0)
   - 020000: Lock (L-)      - LockMask       (1<<1)
   - 040000: Ctrl (C-)      - ControlMask    (1<<2)
   - 080000: Meta (M-)      - Mod1Mask       (1<<3)
   - 100000: Alt (A-)       - Mod2Mask       (1<<4)
   - 200000: Super (s-)     - Mod3Mask       (1<<5)
   - 400000: Hyper (H-)     - Mod4Mask       (1<<6)
   - 800000: Extra (E-)     - Mod5Mask       (1<<7)

   X11 function keys are mapped in the range FF00..FFFF, including prefix keys

   - XK_Shift_L     0xffe1  // Left shift
   - XK_Shift_R     0xffe2  // Right shift
   - XK_Control_L   0xffe3  // Left control
   - XK_Control_R   0xffe4  // Right control
   - XK_Caps_Lock   0xffe5  // Caps lock
   - XK_Shift_Lock  0xffe6  // Shift lock

   - XK_Meta_L      0xffe7  // Left meta
   - XK_Meta_R      0xffe8  // Right meta
   - XK_Alt_L       0xffe9  // Left alt
   - XK_Alt_R       0xffea  // Right alt
   - XK_Super_L     0xffeb  // Left super
   - XK_Super_R     0xffec  // Right super
   - XK_Hyper_L     0xffed  // Left hyper
   - XK_Hyper_R     0xffee  // Right hyper
 */

#define KEY_CTRL(c)     ((c) & 0x001f)
/* allow combinations such as KEY_META(KEY_LEFT) */
#define KEY_META(c)     ((c) | 0xe100)
#define KEY_ESC1(c)     ((c) | 0xe200)
#define KEY_IS_ESC1(c)     ((c) >= KEY_ESC1(0) && (c) <= KEY_ESC1(0xff))
#define KEY_IS_SPECIAL(c)  ((c) >= 0xe000 && (c) < 0xf000)
#define KEY_IS_CONTROL(c)  ((unsigned int)(c) < 32 || (c) == 127)

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

/*---- Unicode and UTF-8 support ----*/

#include "wcwidth.h"

extern char32_t qe_wctoupper(char32_t c);
extern char32_t qe_wctolower(char32_t c);

extern unsigned char const utf8_length[256];

static inline int utf8_is_trailing_byte(unsigned char c) {
    return (c & 0xC0) == 0x80;
}

int utf8_encode(char *q, char32_t c);
char32_t utf8_decode_strict(const char **pp);
char32_t utf8_decode(const char **pp);
int utf8_to_char32(char32_t *dest, int dest_length, const char *str);
int char32_to_utf8(char *dest, int dest_length, const char32_t *src, int src_length);

char32_t qe_unaccent(char32_t c);

static inline int qe_isaccent(char32_t c) {
    return c >= 0x300 && qe_wcwidth(c) == 0;
}

static inline int qe_iswide(char32_t c) {
    return c >= 0x01000 && qe_wcwidth(c) > 1;
}

static inline int qe_iswalpha(char32_t c) {
    return qe_isalpha(qe_unaccent(c));
}

static inline char32_t qe_iswlower(char32_t c) {
    return qe_islower(qe_unaccent(c));
}

static inline char32_t qe_iswupper(char32_t c) {
    return qe_isupper(qe_unaccent(c));
}

static inline char32_t qe_wtolower(char32_t c) {
    return (qe_inrange(c, 'A', 'Z') ? c + 'a' - 'A' :
            c >= 0x80 ? qe_wctolower(c) : c);
}

static inline char32_t qe_wtoupper(char32_t c) {
    return (qe_inrange(c, 'a', 'z') ? c + 'A' - 'a' :
            c >= 0x80 ? qe_wctoupper(c) : c);
}

/*---- Completion types used for enumerations ----*/

typedef struct CompleteState CompleteState;
typedef void (*CompleteFunc)(CompleteState *cp, const char *str, int mode);
/* mode values for default CompleteFunc passed to complete_xxx() functions */
enum { CT_TEST, CT_GLOB, CT_IGLOB, CT_STRX, CT_SET };

#endif  /* UTIL_H */
