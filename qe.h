/*
 * QEmacs, tiny but powerful multimode editor
 *
 * Copyright (c) 2000,2001 Fabrice Bellard.
 * Copyright (c) 2000-2008 Charlie Gordon.
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

#ifndef QE_H
#define QE_H

#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>
#include <limits.h>
#include <inttypes.h>

#ifdef HAVE_QE_CONFIG_H
#include "config.h"
#endif

/* OS specific defines */

#ifdef CONFIG_WIN32
#define snprintf   _snprintf
#define vsnprintf  _vsnprintf
#endif

#if (defined(__GNUC__) || defined(__TINYC__))
/* make sure that the keyword is not disabled by glibc (TINYC case) */
#undef __attribute__
#define __attr_printf(a, b)  __attribute__((format(printf, a, b)))
#define __attr_nonnull(l)    __attribute__((nonnull l))
#define __unused__           __attribute__((unused))
#else
#ifndef __attribute__
#define __attribute__(l)
#endif
#define __attr_printf(a, b)
#define __attr_nonnull(l)
#define __unused__
#endif

#ifdef __SPARSE__
#define __bitwise__             __attribute__((bitwise))
#define force_cast(type, expr)  ((__attribute__((force))type)(expr))
#else
#define __bitwise__
#define force_cast(type, expr)  ((type)(expr))
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

/* prevent gcc warning about shadowing a global declaration */
#define index  index__

/************************/

#include "cutils.h"

/************************/

/* allocation wrappers and utilities */
void *qe_malloc_bytes(size_t size);
void *qe_mallocz_bytes(size_t size);
void *qe_malloc_dup(const void *src, size_t size);
char *qe_strdup(const char *str);
void *qe_realloc(void *pp, size_t size);
#define qe_malloc(t)            ((t *)qe_malloc_bytes(sizeof(t)))
#define qe_mallocz(t)           ((t *)qe_mallocz_bytes(sizeof(t)))
#define qe_malloc_array(t, n)   ((t *)qe_malloc_bytes((n) * sizeof(t)))
#define qe_mallocz_array(t, n)  ((t *)qe_mallocz_bytes((n) * sizeof(t)))
#define qe_malloc_hack(t, n)    ((t *)qe_malloc_bytes(sizeof(t) + (n)))
#define qe_mallocz_hack(t, n)   ((t *)qe_mallocz_bytes(sizeof(t) + (n)))
#define qe_free(pp)      \
    do { void *_ = (pp); free(*(void **)_); *(void **)_ = NULL; } while (0)

/************************/

typedef unsigned char u8;
typedef struct EditState EditState;
typedef struct EditBuffer EditBuffer;
typedef struct QEmacsState QEmacsState;

static inline char *s8(u8 *p) { return (char*)p; }
static inline const char *cs8(const u8 *p) { return (const char*)p; }

#ifndef INT_MAX
#define INT_MAX  0x7fffffff
#endif
#ifndef INT_MIN
#define INT_MIN  (-0x7fffffff-1)
#endif
#define NO_ARG  INT_MIN
/* Size for a filename buffer */
#define MAX_FILENAME_SIZE    1024
/* Size for a buffer name buffer */
#define MAX_BUFFERNAME_SIZE  256
/* Size for a command name buffer */
#define MAX_CMDNAME_SIZE     32

extern const char str_version[];
extern const char str_credits[];

/* low level I/O events */
void set_read_handler(int fd, void (*cb)(void *opaque), void *opaque);
void set_write_handler(int fd, void (*cb)(void *opaque), void *opaque);
int set_pid_handler(int pid,
                    void (*cb)(void *opaque, int status), void *opaque);
void url_exit(void);
void register_bottom_half(void (*cb)(void *opaque), void *opaque);
void unregister_bottom_half(void (*cb)(void *opaque), void *opaque);

typedef struct QETimer QETimer;
QETimer *qe_add_timer(int delay, void *opaque, void (*cb)(void *opaque));
void qe_kill_timer(QETimer *ti);

/* main loop for Unix programs using liburlio */
void url_main_loop(void (*init)(void *opaque), void *opaque);

/* util.c */

/* string arrays */
typedef struct StringItem {
    void *opaque; /* opaque data that the user can use */
    char selected; /* true if selected */
    char str[1];
} StringItem;

typedef struct StringArray {
    int nb_allocated;
    int nb_items;
    StringItem **items;
} StringArray;
#define NULL_STRINGARRAY  { 0, 0, NULL }

typedef struct CompleteState {
    StringArray cs;
    struct EditState *s;
    char current[MAX_FILENAME_SIZE];
    int len;
} CompleteState;

/* media definitions */
#define CSS_MEDIA_TTY     0x0001
#define CSS_MEDIA_SCREEN  0x0002
#define CSS_MEDIA_PRINT   0x0004
#define CSS_MEDIA_TV      0x0008
#define CSS_MEDIA_SPEECH  0x0010

#define CSS_MEDIA_ALL     0xffff

typedef struct CSSRect {
    int x1, y1, x2, y2;
} CSSRect;

typedef struct FindFileState FindFileState;

FindFileState *find_file_open(const char *path, const char *pattern);
int find_file_next(FindFileState *s, char *filename, int filename_size_max);
void find_file_close(FindFileState *s);
int is_directory(const char *path);
void canonicalize_path(char *buf, int buf_size, const char *path);
void canonicalize_absolute_path(char *buf, int buf_size, const char *path1);
char *reduce_filename(char *dest, int size, const char *filename);
int match_extension(const char *filename, const char *extlist);
int remove_slash(char *buf);
int append_slash(char *buf, int buf_size);
char *makepath(char *buf, int buf_size, const char *path, const char *filename);
void splitpath(char *dirname, int dirname_size,
               char *filename, int filename_size, const char *pathname);

static inline int qe_inrange(int c, int a, int b) {
    return c >= a && c <= b;
}
static inline int qe_isspace(int c) {
    /* CG: what about \v and \f */
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}
static inline int qe_isblank(int c) {
    return (c == ' ' || c == '\t');
}
static inline int qe_isdigit(int c) {
    return qe_inrange(c, '0', '9');
}
static inline int qe_isupper(int c) {
    return qe_inrange(c, 'A', 'Z');
}
static inline int qe_islower(int c) {
    return qe_inrange(c, 'a', 'z');
}
static inline int qe_isalpha(int c) {
    return qe_inrange(c | ('a' - 'A'), 'a', 'z');
}
static inline int qe_isalpha_(int c) {
    return (qe_inrange(c | ('a' - 'A'), 'a', 'z') || c == '_');
}
static inline int qe_isxdigit(int c) {
    return (qe_inrange(c, '0', '9') ||
            qe_inrange(c | ('a' - 'A'), 'a', 'f'));
}
static inline int qe_isalnum(int c) {
    return (qe_inrange(c, '0', '9') ||
            qe_inrange(c | ('a' - 'A'), 'a', 'z'));
}
static inline int qe_isalnum_(int c) {
    return (qe_inrange(c, '0', '9') ||
            qe_inrange(c | ('a' - 'A'), 'a', 'z') ||
            c == '_');
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

int qe_strcollate(const char *s1, const char *s2);
void qe_strtolower(char *buf, int buf_size, const char *str);
void skip_spaces(const char **pp);

static inline int strequal(const char *s1, const char *s2) {
    return !strcmp(s1, s2);
}

int strfind(const char *list, const char *s);
int strcasefind(const char *list, const char *s);
const void *memstr(const void *buf, int size, const char *str);

#define stristart(str, val, ptr)   qe_stristart(str, val, ptr)
int stristart(const char *str, const char *val, const char **ptr);
int strxstart(const char *str, const char *val, const char **ptr);
int strxcmp(const char *str1, const char *str2);
int ustristart(const unsigned int *str, const char *val, const unsigned int **ptr);
static inline unsigned int *umemmove(unsigned int *dest,
                                     unsigned int *src, int count) {
    return memmove(dest, src, count * sizeof(unsigned int));
}
static inline unsigned int *umemcpy(unsigned int *dest,
                                    unsigned int *src, int count) {
    return memcpy(dest, src, count * sizeof(unsigned int));
}
int umemcmp(const unsigned int *s1, const unsigned int *s2, int count);

int strsubst(char *buf, int buf_size, const char *from,
             const char *s1, const char *s2);
int strquote(char *dest, int size, const char *str, int len);
int strunquote(char *dest, int size, const char *str, int len);

void get_str(const char **pp, char *buf, int buf_size, const char *stop);
int css_get_enum(const char *str, const char *enum_str);
int compose_keys(unsigned int *keys, int *nb_keys);
int strtokey(const char **pp);
int strtokeys(const char *keystr, unsigned int *keys, int max_keys);
void keytostr(char *buf, int buf_size, int key);
const char *keys_to_str(char *buf, int buf_size,
                        unsigned int *keys, int nb_keys);
int to_hex(int key);
void color_completion(CompleteState *cp);
int css_define_color(const char *name, const char *value);
int css_get_color(unsigned int *color_ptr, const char *p);
int css_get_font_family(const char *str);
void css_union_rect(CSSRect *a, const CSSRect *b);
static inline int css_is_null_rect(const CSSRect *a) {
    return (a->x2 <= a->x1 || a->y2 <= a->y1);
}
static inline void css_set_rect(CSSRect *a, int x1, int y1, int x2, int y2) {
    a->x1 = x1;
    a->y1 = y1;
    a->x2 = x2;
    a->y2 = y2;
}
/* return true if a and b intersect */
static inline int css_is_inter_rect(const CSSRect *a, const CSSRect *b) {
    return (!(a->x2 <= b->x1 ||
              a->x1 >= b->x2 ||
              a->y2 <= b->y1 ||
              a->y1 >= b->y2));
}

int get_clock_ms(void);
int get_clock_usec(void);

/* Various string packages: should unify these but keep API simple */

StringItem *set_string(StringArray *cs, int index, const char *str);
StringItem *add_string(StringArray *cs, const char *str);
void free_strings(StringArray *cs);

/* simple dynamic strings wrappers. The strings are always terminated
   by zero except if they are empty. */
typedef struct QString {
    u8 *data;
    int len; /* string length excluding trailing '\0' */
} QString;

static inline void qstrinit(QString *q) {
    q->data = NULL;
    q->len = 0;
}

static inline void qstrfree(QString *q) {
    qe_free(&q->data);
}

int qmemcat(QString *q, const u8 *data1, int len1);
int qstrcat(QString *q, const char *str);
int qprintf(QString *q, const char *fmt, ...) __attr_printf(2,3);

/* Dynamic buffers with static allocation */
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

int buf_printf(buf_t *bp, const char *fmt, ...) __attr_printf(2,3);
int buf_putc_utf8(buf_t *bp, int c);

/* command line option */
#define CMD_OPT_ARG      0x0001 /* argument */
#define CMD_OPT_STRING   0x0002 /* string */
#define CMD_OPT_BOOL     0x0004 /* boolean */
#define CMD_OPT_INT      0x0008 /* int */

typedef struct CmdOptionDef {
    const char *name;
    const char *shortname;
    const char *argname;
    int flags;
    const char *help;
    union {
        const char **string_ptr;
        int *int_ptr;
        void (*func_noarg)(void);
        void (*func_arg)(const char *);
        struct CmdOptionDef *next;
    } u;
} CmdOptionDef;

void qe_register_cmd_line_options(CmdOptionDef *table);

int find_resource_file(char *path, int path_size, const char *pattern);

typedef int (CSSAbortFunc)(void *);

static inline int max(int a, int b) {
    if (a > b)
        return a;
    else
        return b;
}

static inline int min(int a, int b) {
    if (a < b)
        return a;
    else
        return b;
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

/* charset.c */

/* maximum number of bytes for a character in all the supported charsets */
#define MAX_CHAR_BYTES 6

typedef struct CharsetDecodeState CharsetDecodeState;
typedef struct QECharset QECharset;

struct QECharset {
    const char *name;
    const char *aliases;
    void (*decode_init)(CharsetDecodeState *s);
    int (*decode_func)(CharsetDecodeState *s);
    /* return NULL if cannot encode. Currently no state since speed is
       not critical yet */
    u8 *(*encode_func)(QECharset *charset, u8 *buf, int size);
    void (*get_pos_func)(CharsetDecodeState *s, const u8 *buf, int size,
                         int *line_ptr, int *col_ptr);
    int (*get_chars_func)(QECharset *charset, const u8 *buf, int size);
    int (*goto_char_func)(QECharset *charset, const u8 *buf, int size, int pos);
    int (*goto_line_func)(QECharset *charset, const u8 *buf, int size, int lines);
    unsigned int char_size : 3;
    unsigned int variable_size : 1;
    unsigned int table_alloc : 1; /* true if CharsetDecodeState.table must be malloced */
    /* private data for some charsets */
    u8 eol_char; /* 0x0A for ASCII, 0x25 for EBCDIC */
    u8 min_char, max_char;
    const unsigned short *private_table;
    struct QECharset *next;
};

extern QECharset *first_charset;
extern QECharset charset_utf8, charset_8859_1; /* predefined charsets */
extern QECharset charset_vt100; /* used for the tty output */
extern QECharset charset_ucs2le, charset_ucs2be;
extern QECharset charset_ucs4le, charset_ucs4be;

struct CharsetDecodeState {
    /* 256 ushort table for hyper fast decoding */
    unsigned short *table;
    int char_size;
    const u8 *p;
    /* slower decode function for complicated cases */
    int (*decode_func)(CharsetDecodeState *s);
    void (*get_pos_func)(CharsetDecodeState *s, const u8 *buf, int size,
                         int *line_ptr, int *col_ptr);
    QECharset *charset;
};

#define INVALID_CHAR 0xfffd
#define ESCAPE_CHAR  0xffff

void charset_init(void);
int charset_more_init(void);
int charset_jis_init(void);

void qe_register_charset(QECharset *charset);

extern unsigned char utf8_length[256];
int utf8_encode(char *q, int c);
int utf8_decode(const char **pp);
int utf8_to_unicode(unsigned int *dest, int dest_length, const char *str);

void charset_completion(CompleteState *cp);
QECharset *find_charset(const char *str);
void charset_decode_init(CharsetDecodeState *s, QECharset *charset);
void charset_decode_close(CharsetDecodeState *s);
void charset_get_pos_8bit(CharsetDecodeState *s, const u8 *buf, int size,
                          int *line_ptr, int *col_ptr);
int charset_get_chars_8bit(QECharset *charset, const u8 *buf, int size);
int charset_goto_char_8bit(QECharset *charset, const u8 *buf, int size, int pos);
int charset_goto_line_8bit(QECharset *charset, const u8 *buf, int size, int nlines);

QECharset *detect_charset(const u8 *buf, int size);

void decode_8bit_init(CharsetDecodeState *s);
int decode_8bit(CharsetDecodeState *s);
u8 *encode_8bit(QECharset *charset, u8 *q, int c);

int unicode_to_charset(char *buf, unsigned int c, QECharset *charset);

/* arabic.c */
int arab_join(unsigned int *line, unsigned int *ctog, int len);

/* indic.c */
int devanagari_log2vis(unsigned int *str, unsigned int *ctog, int len);

/* unicode_join.c */
int unicode_to_glyphs(unsigned int *dst, unsigned int *char_to_glyph_pos,
                      int dst_size, unsigned int *src, int src_size,
                      int reverse);
int load_ligatures(void);

/* qe event handling */

enum QEEventType {
    QE_KEY_EVENT,
    QE_EXPOSE_EVENT, /* full redraw */
    QE_UPDATE_EVENT, /* update content */
    QE_BUTTON_PRESS_EVENT, /* mouse button press event */
    QE_BUTTON_RELEASE_EVENT, /* mouse button release event */
    QE_MOTION_EVENT, /* mouse motion event */
    QE_SELECTION_CLEAR_EVENT, /* request selection clear (X11 type selection) */
};

#define KEY_CTRL(c)     ((c) & 0x001f)
#define KEY_META(c)     ((c) | 0xe000)
#define KEY_ESC1(c)     ((c) | 0xe100)
#define KEY_CTRLX(c)    ((c) | 0xe200)
#define KEY_CTRLXRET(c) ((c) | 0xe300)
#define KEY_CTRLH(c)    ((c) | 0xe500)
#define KEY_SPECIAL(c)  (((c) >= 0xe000 && (c) < 0xf000) || ((c) >= 0 && (c) < 32) || (c) == 127)

#define KEY_NONE        0xffff
#define KEY_DEFAULT     0xe401 /* to handle all non special keys */

#define KEY_TAB         KEY_CTRL('i')
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

typedef struct QEKeyEvent {
    enum QEEventType type;
    int key;
} QEKeyEvent;

typedef struct QEExposeEvent {
    enum QEEventType type;
    /* currently, no more info */
} QEExposeEvent;

#define QE_BUTTON_LEFT   0x0001
#define QE_BUTTON_MIDDLE 0x0002
#define QE_BUTTON_RIGHT  0x0004
#define QE_WHEEL_UP      0x0008
#define QE_WHEEL_DOWN    0x0010

/* should probably go somewhere else, or in the config file */
/* how many text lines to scroll when mouse wheel is used */
#define WHEEL_SCROLL_STEP 4

typedef struct QEButtonEvent {
    enum QEEventType type;
    int x;
    int y;
    int button;
} QEButtonEvent;

typedef struct QEMotionEvent {
    enum QEEventType type;
    int x;
    int y;
} QEMotionEvent;

typedef union QEEvent {
    enum QEEventType type;
    QEKeyEvent key_event;
    QEExposeEvent expose_event;
    QEButtonEvent button_event;
    QEMotionEvent motion_event;
} QEEvent;

void qe_handle_event(QEEvent *ev);
/* CG: Should optionally attach grab to a window */
/* CG: Should deal with opaque object life cycle */
void qe_grab_keys(void (*cb)(void *opaque, int key), void *opaque);
void qe_ungrab_keys(void);

/* buffer.c */

/* begin to mmap files from this size */
#define MIN_MMAP_SIZE  (1024*1024)
#define MAX_LOAD_SIZE  (512*1024*1024)

#define MAX_PAGE_SIZE 4096
//#define MAX_PAGE_SIZE 16

#define NB_LOGS_MAX     100000  /* need better way to limit undo information */

#define PG_READ_ONLY    0x0001 /* the page is read only */
#define PG_VALID_POS    0x0002 /* set if the nb_lines / col fields are up to date */
#define PG_VALID_CHAR   0x0004 /* nb_chars is valid */
#define PG_VALID_COLORS 0x0008 /* color state is valid */

typedef struct Page {
    int size; /* data size */
    u8 *data;
    int flags;
    /* the following are needed to handle line / column computation */
    int nb_lines; /* Number of EOL characters in data */
    int col;      /* Number of chars since the last EOL */
    /* the following is needed for char offset computation */
    int nb_chars;
} Page;

#define DIR_LTR 0
#define DIR_RTL 1

typedef int DirType;

enum LogOperation {
    LOGOP_FREE = 0,
    LOGOP_WRITE,
    LOGOP_INSERT,
    LOGOP_DELETE,
};

/* Each buffer modification can be caught with this callback */
typedef void (*EditBufferCallback)(EditBuffer *,
                                   void *opaque,
                                   enum LogOperation op,
                                   int offset,
                                   int size);

typedef struct EditBufferCallbackList {
    void *opaque;
    EditBufferCallback callback;
    struct EditBufferCallbackList *next;
} EditBufferCallbackList;

/* buffer flags */
#define BF_SAVELOG   0x0001  /* activate buffer logging */
#define BF_SYSTEM    0x0002  /* buffer system, cannot be seen by the user */
#define BF_READONLY  0x0004  /* read only buffer */
#define BF_PREVIEW   0x0008  /* used in dired mode to mark previewed files */
#define BF_LOADING   0x0010  /* buffer is being loaded */
#define BF_SAVING    0x0020  /* buffer is being saved */
#define BF_DIRED     0x0100  /* buffer is interactive dired */
#define BF_UTF8      0x0200  /* buffer charset is utf-8 */
#define BF_RAW       0x0400  /* buffer charset is raw (same as latin1) */

struct EditBuffer {
    Page *page_table;
    int nb_pages;
    int mark;       /* current mark (moved with text) */
    int total_size; /* total size of the buffer */
    int modified;

    /* page cache */
    Page *cur_page;
    int cur_offset;
    int file_handle; /* if the file is kept open because it is mapped,
                        its handle is there */
    int flags;

    /* buffer data type (default is raw) */
    struct EditBufferDataType *data_type;
    void *data; /* associated buffer data, used if data_type != raw_data */

    /* charset handling */
    CharsetDecodeState charset_state;
    QECharset *charset;

    /* undo system */
    int save_log;    /* if true, each buffer operation is logged */
    int log_new_index, log_current;
    EditBuffer *log_buffer;
    int nb_logs;

    /* modification callbacks */
    EditBufferCallbackList *first_callback;

    /* asynchronous loading/saving support */
    struct BufferIOState *io_state;

    /* used during loading */
    int probed;

    /* buffer polling & private data */
    void *priv_data;
    /* called when deleting the buffer */
    void (*close)(EditBuffer *);

    /* saved data from the last opened mode, needed to restore mode */
    /* CG: should instead keep a pointer to last window using this
     * buffer, even if no longer on screen
     */
    struct ModeSavedData *saved_data;

    /* default mode stuff when buffer is detached from window */
    int offset;         /* used in eval.c */
    int tab_size;

    EditBuffer *next; /* next editbuffer in qe_state buffer list */

    char name[MAX_BUFFERNAME_SIZE];     /* buffer name */
    char filename[MAX_FILENAME_SIZE];   /* file name */

    /* Should keep a stat buffer to check for file type and
     * asynchronous modifications
     */
};

/* high level buffer type handling */
typedef struct EditBufferDataType {
    const char *name; /* name of buffer data type (text, image, ...) */
    int (*buffer_load)(EditBuffer *b, FILE *f);
    int (*buffer_save)(EditBuffer *b, int start, int end, const char *filename);
    void (*buffer_close)(EditBuffer *b);
    struct EditBufferDataType *next;
} EditBufferDataType;

/* the log buffer is used for the undo operation */
/* header of log operation */
typedef struct LogBuffer {
    u8 pad1, pad2;    /* for Log buffer readability */
    u8 op;
    u8 was_modified;
    int offset;
    int size;
} LogBuffer;

void eb_trace_bytes(const void *buf, int size, int state);

void eb_init(void);
int eb_read(EditBuffer *b, int offset, void *buf, int size);
void eb_write(EditBuffer *b, int offset, const void *buf, int size);
void eb_insert_buffer(EditBuffer *dest, int dest_offset,
                      EditBuffer *src, int src_offset,
                      int size);
void eb_insert(EditBuffer *b, int offset, const void *buf, int size);
void eb_delete(EditBuffer *b, int offset, int size);
void eb_replace(EditBuffer *b, int offset, int size,
                const void *buf, int size1);
void log_reset(EditBuffer *b);
EditBuffer *eb_new(const char *name, int flags);
EditBuffer *eb_scratch(const char *name, int flags);
void eb_clear(EditBuffer *b);
void eb_free(EditBuffer *b);
EditBuffer *eb_find(const char *name);
EditBuffer *eb_find_new(const char *name, int flags);
EditBuffer *eb_find_file(const char *filename);
EditState *eb_find_window(EditBuffer *b, EditState *e);

void eb_set_charset(EditBuffer *b, QECharset *charset);
__attr_nonnull((3))
int eb_nextc(EditBuffer *b, int offset, int *next_ptr);
__attr_nonnull((3))
int eb_prevc(EditBuffer *b, int offset, int *prev_ptr);
int eb_goto_pos(EditBuffer *b, int line1, int col1);
int eb_get_pos(EditBuffer *b, int *line_ptr, int *col_ptr, int offset);
int eb_goto_char(EditBuffer *b, int pos);
int eb_get_char_offset(EditBuffer *b, int offset);
int eb_delete_range(EditBuffer *b, int p1, int p2);
//int eb_clip_offset(EditBuffer *b, int offset);
void do_undo(EditState *s);
//void do_redo(EditState *s);

int raw_load_buffer1(EditBuffer *b, FILE *f, int offset);
int mmap_buffer(EditBuffer *b, const char *filename);
int eb_write_buffer(EditBuffer *b, int start, int end, const char *filename);
int eb_save_buffer(EditBuffer *b);

void eb_set_buffer_name(EditBuffer *b, const char *name1);
void eb_set_filename(EditBuffer *b, const char *filename);

int eb_add_callback(EditBuffer *b, EditBufferCallback cb, void *opaque);
void eb_free_callback(EditBuffer *b, EditBufferCallback cb, void *opaque);
void eb_offset_callback(EditBuffer *b,
                        void *opaque,
                        enum LogOperation op,
                        int offset,
                        int size);
int eb_printf(EditBuffer *b, const char *fmt, ...) __attr_printf(2,3);
void eb_line_pad(EditBuffer *b, int n);
int eb_get_contents(EditBuffer *b, char *buf, int buf_size);
int eb_get_line(EditBuffer *b, unsigned int *buf, int buf_size,
                int *offset_ptr);
int eb_get_strline(EditBuffer *b, char *buf, int buf_size,
                   int *offset_ptr);
int eb_prev_line(EditBuffer *b, int offset);
int eb_goto_bol(EditBuffer *b, int offset);
int eb_goto_bol2(EditBuffer *b, int offset, int *countp);
int eb_is_empty_line(EditBuffer *b, int offset);
int eb_goto_eol(EditBuffer *b, int offset);
int eb_next_line(EditBuffer *b, int offset);

void eb_register_data_type(EditBufferDataType *bdt);
EditBufferDataType *eb_probe_data_type(const char *filename, int mode,
                                       uint8_t *buf, int buf_size);
void eb_set_data_type(EditBuffer *b, EditBufferDataType *bdt);
void eb_invalidate_raw_data(EditBuffer *b);
extern EditBufferDataType raw_data_type;

/* qe module handling */

#ifdef QE_MODULE

/* dynamic module case */

#define qe_module_init(fn) \
        int __qe_module_init(void) { return fn(); }

#define qe_module_exit(fn) \
        void __qe_module_exit(void) { fn(); }

#else /* QE_MODULE */

#if (defined(__GNUC__) || defined(__TINYC__)) && defined(CONFIG_INIT_CALLS)
#if __GNUC__ < 3 || (__GNUC__ == 3 && __GNUC_MINOR__ < 3)
/* same method as the linux kernel... */
#define __init_call     __attribute__((unused, __section__ (".initcall.init")))
#define __exit_call     __attribute__((unused, __section__ (".exitcall.exit")))
#else
#undef __attribute_used__
#define __attribute_used__      __attribute__((__used__))
#define __init_call     __attribute_used__ __attribute__((__section__ (".initcall.init")))
#define __exit_call     __attribute_used__ __attribute__((__section__ (".exitcall.exit")))
#endif

#define qe_module_init(fn) \
        static int (*__initcall_##fn)(void) __init_call = fn

#define qe_module_exit(fn) \
        static void (*__exitcall_##fn)(void) __exit_call = fn
#else

#define __init_call
#define __exit_call

#define qe_module_init(fn) \
        extern int module_ ## fn (void); \
        int module_ ## fn (void) { return fn(); }

#define qe_module_exit(fn)

#endif

#endif /* QE_MODULE */

/* display.c */

#include "display.h"

/* qe.c */

/* colorize & transform a line, lower level then ColorizeFunc */
typedef int (*GetColorizedLineFunc)(EditState *s,
                                    unsigned int *buf, int buf_size,
                                    int *offset1, int line_num);

/* colorize a line : this function modifies buf to set the char
   styles. 'buf' is guaranted to have one more char after its len
   (it is either '\n' or '\0') */
typedef void (*ColorizeFunc)(unsigned int *buf, int len,
                             int *colorize_state_ptr, int state_only);

/* contains all the information necessary to uniquely identify a line,
   to avoid displaying it */
typedef struct QELineShadow {
    short x_start;
    short y;
    short height;
    unsigned int crc;
} QELineShadow;

enum WrapType {
    WRAP_TRUNCATE = 0,
    WRAP_LINE,
    WRAP_WORD
};

#define DIR_LTR 0
#define DIR_RTL 1

struct EditState {
    int offset;     /* offset of the cursor */
    /* text display state */
    int offset_top;
    int y_disp;    /* virtual position of the displayed text */
    int x_disp[2]; /* position for LTR and RTL text resp. */
    int minibuf;   /* true if single line editing */
    int disp_width;  /* width in hex or ascii mode */
    int hex_mode;    /* true if we are currently editing hexa */
    int unihex_mode; /* true if unihex editing (hex_mode must be true too) */
    int hex_nibble;  /* current hexa nibble */
    int insert;      /* insert/overtype mode */
    int bidir;
    int cur_rtl;     /* TRUE if the cursor on over RTL chars */
    enum WrapType wrap;
    int line_numbers;
    int tab_size;    /* the tab width for the window in chars */
    int indent_size;
    int indent_tabs_mode; /* if true, use tabs to indent */
    int interactive; /* true if interaction is done instead of editing
                        (e.g. for shell mode or HTML) */
    int force_highlight;  /* if true, force showing of cursor even if
                             window not focused (list mode only) */
    int mouse_force_highlight; /* if true, mouse can force highlight
                                  (list mode only) */
    /* low level colorization function */
    GetColorizedLineFunc get_colorized_line;
    /* colorization function */
    ColorizeFunc colorize_func;
    /* default text style */
    int default_style;

    /* after this limit, the fields are not saved into the buffer */
    int end_of_saved_data;

    /* mode specific info */
    struct ModeDef *mode;
    void *mode_data; /* mode private data */

    EditBuffer *b;

    /* state before line n, one byte per line */
    unsigned char *colorize_states;
    int colorize_nb_lines;
    int colorize_nb_valid_lines;
    /* maximum valid offset, INT_MAX if not modified. Needed to invalide
       'colorize_states' */
    int colorize_max_valid_offset;

    int busy; /* true if editing cannot be done if the window
                 (e.g. the parser HTML is parsing the buffer to
                 produce the display */
    int display_invalid; /* true if the display was invalidated. Full
                            redraw should be done */
    int borders_invalid; /* true if window borders should be redrawn */
    int show_selection;  /* if true, the selection is displayed */

    int region_style;
    int curline_style;

    /* display area info */
    int width, height;
    int ytop, xleft;
    /* full window size, including borders */
    int x1, y1, x2, y2;         /* window coordinates in device units */
    //int xx1, yy1, xx2, yy2;     /* window coordinates in 1/1000 */

    int flags; /* display flags */
#define WF_POPUP      0x0001 /* popup window (with borders) */
#define WF_MODELINE   0x0002 /* mode line must be displayed */
#define WF_RSEPARATOR 0x0004 /* right window separator */

    char *prompt;
    //const char *mode_line;
    //const char *title;
    struct QEmacsState *qe_state;
    struct QEditScreen *screen; /* copy of qe_state->screen */
    /* display shadow to optimize redraw */
    char modeline_shadow[MAX_SCREEN_WIDTH];
    QELineShadow *line_shadow; /* per window shadow */
    int shadow_nb_lines;
    /* compose state for input method */
    struct InputMethod *input_method; /* current input method */
    struct InputMethod *selected_input_method; /* selected input method (used to switch) */
    int compose_len;
    int compose_start_offset;
    unsigned int compose_buf[20];
    EditState *next_window;
};

/* Ugly patch for saving/restoring window data upon switching buffer */
#define SAVED_DATA_SIZE  offsetof(EditState, end_of_saved_data)

typedef struct DisplayState DisplayState;

typedef struct ModeProbeData {
    const char *real_filename;
    const char *filename;  /* reduced filename for mode matching purposes */
    const u8 *buf;
    int buf_size;
    int line_len;
    int mode;     /* unix mode */
    long total_size;
} ModeProbeData;

/* private data saved by a mode so that it can be restored when the
   mode is started again on a buffer */
typedef struct ModeSavedData {
    struct ModeDef *mode; /* the mode is saved there */
    char generic_data[SAVED_DATA_SIZE]; /* generic text data */
    int data_size; /* mode specific saved data */
    char data[1];
} ModeSavedData;

typedef struct ModeDef {
    const char *name;
    //const char *mode_line;
    int instance_size; /* size of malloced instance */
    int (*mode_probe)(ModeProbeData *); /* return the percentage of confidence */
    int (*mode_init)(EditState *, ModeSavedData *);
    void (*mode_close)(EditState *);
    /* save the internal state of the mode so that it can be opened
       again in the same state */
    ModeSavedData *(*mode_save_data)(EditState *s);

    /* low level display functions (must be NULL to use text related
       functions)*/
    void (*display_hook)(EditState *);
    void (*display)(EditState *);

    /* text related functions */
    int (*text_display)(EditState *, DisplayState *, int);
    int (*text_backward_offset)(EditState *, int);

    /* common functions are defined here */
    /* TODO: Should have single move function with move type and argument */
    void (*move_up_down)(EditState *, int);
    void (*move_left_right)(EditState *, int);
    void (*move_bol)(EditState *);
    void (*move_eol)(EditState *);
    void (*move_word_left_right)(EditState *, int);
    void (*scroll_up_down)(EditState *, int);
    void (*scroll_line_up_down)(EditState *, int);
    void (*write_char)(EditState *, int);
    void (*mouse_goto)(EditState *, int x, int y);

    int mode_flags;
#define MODEF_NOCMD 0x0001 /* do not register xxx-mode command automatically */
    EditBufferDataType *data_type; /* native buffer data type (NULL = raw) */
    int (*get_mode_line)(EditState *s, char *buf, int buf_size); /* return mode line */

    /* mode specific key bindings */
    struct KeyDef *first_key;

    struct ModeDef *next;
} ModeDef;

/* special bit to indicate tty styles (for shell mode) */
#define QE_STYLE_TTY       0x800
#define TTY_UNDERLINE      0
#define TTY_BOLD           (1 << 7)
#define TTY_BLINK          (1 << 3)
#define TTY_DEFFG          7
#define TTY_DEFBG          0
#define TTY_MAKE_COLOR(fg, bg)  (((fg) << 4) | (bg))
#define TTY_SET_FG_COLOR(color, fg)   ((color) = ((color) & ~(15 << 4)) | ((fg) << 4))
#define TTY_SET_BG_COLOR(color, bg)   ((color) = ((color) & ~(15)) | ((bg)))
#define TTY_GET_FG(color)  (((color) >> 4) & 15)
#define TTY_GET_BG(color)  (((color) >> 0) & 15)

/* from tty.c */
extern unsigned int const *tty_bg_colors;
extern int tty_bg_colors_count;
extern unsigned int const *tty_fg_colors;
extern int tty_fg_colors_count;
int get_tty_color(QEColor color, unsigned int const *colors, int count);

/* special selection style (cumulative with another style) */
#define QE_STYLE_SEL     0x400

enum QEStyle {
#define STYLE_DEF(constant, name, fg_color, bg_color, \
                  font_style, font_size) \
                  constant,

#include "qestyles.h"

#undef STYLE_DEF
    QE_STYLE_NB,
};

typedef struct QEStyleDef {
    const char *name;
    /* if any style is 0, then default edit style applies */
    QEColor fg_color, bg_color;
    short font_style;
    short font_size;
} QEStyleDef;

/* CG: Should register styles as well */
extern QEStyleDef qe_styles[QE_STYLE_NB];

/* QEmacs state structure */

#define NB_YANK_BUFFERS 10

typedef struct QErrorContext {
    const char *function;
    const char *filename;
    int lineno;
} QErrorContext;

typedef void (*CmdFunc)(void);

struct QEmacsState {
    QEditScreen *screen;
    //struct QEDisplay *first_dpy;
    struct ModeDef *first_mode;
    struct KeyDef *first_key;
    struct CmdDef *first_cmd;
    struct CompletionEntry *first_completion;
    struct HistoryEntry *first_history;
    //struct QECharset *first_charset;
    //struct QETimer *first_timer;
    struct VarDef *first_variable;
    struct InputMethod *input_methods;
    EditState *first_window;
    EditState *active_window; /* window in which we edit */
    EditBuffer *first_buffer;
    //EditBuffer *message_buffer;
    EditBuffer *trace_buffer;
    int trace_buffer_state;
#define EB_TRACE_TTY    1
#define EB_TRACE_SHELL  2
#define EB_TRACE_PTY    4

    /* global layout info : DO NOT modify these directly. do_refresh
       does it */
    int status_height;
    int mode_line_height;
    int content_height; /* height excluding status line */
    int width, height;
    int border_width;
    int separator_width;
    /* full screen state */
    int hide_status; /* true if status should be hidden */
    int complete_refresh;
    int is_full_screen;
    /* select display aspect for non-latin1 characters:
     * 0 (auto) -> display as unicode on utf-8 capable ttys and x11
     * 1 (nc) -> display as ? or ?? non character symbols
     * 2 (escape) -> display as \uXXXX escape sequence
     */
    int show_unicode;

    /* commands */
    int flag_split_window_change_focus;
    int backspace_is_control_h;
    /* XXX: move these to ec */
    CmdFunc last_cmd_func; /* last executed command function call */
    CmdFunc this_cmd_func; /* current executing command */
    /* keyboard macros */
    int defining_macro;
    int executing_macro;
    unsigned short *macro_keys;
    int nb_macro_keys;
    int macro_keys_size;
    int macro_key_index; /* -1 means no macro is being executed */
    int ungot_key;
    /* yank buffers */
    EditBuffer *yank_buffers[NB_YANK_BUFFERS];
    int yank_current;
    int argc;  /* command line arguments */
    char **argv;
    char *tty_charset;
    char res_path[1024];        /* exported as QEPATH */
    char status_shadow[MAX_SCREEN_WIDTH];
    QErrorContext ec;
    char system_fonts[NB_FONT_FAMILIES][256];

    ///* global variables */
    int it;             /* last result from expression evaluator */
    //int force_tty;      /* prevent graphics display (X11...) */
    //int no_config;      /* prevent config file eval */
    //int force_refresh;  /* force a complete screen refresh */
    int ignore_spaces;  /* ignore spaces when comparing windows */
    int hilite_region;  /* hilite the current region when selecting */
    int mmap_threshold; /* minimum file size for mmap */
    int max_load_size;  /* maximum file size for loading in memory */
};

extern QEmacsState qe_state;

/* key bindings definitions */

/* dynamic key binding storage */

#define MAX_KEYS 10

typedef struct KeyDef {
    struct KeyDef *next;
    struct CmdDef *cmd;
    int nb_keys;
    unsigned int keys[1];
} KeyDef;

void unget_key(int key);

/* command definitions */

enum CmdArgType {
    CMD_ARG_INT = 0,
    CMD_ARG_INTVAL,
    CMD_ARG_STRING,
    CMD_ARG_STRINGVAL,
    CMD_ARG_WINDOW,
    CMD_ARG_TYPE_MASK = 0x3f,
    CMD_ARG_USE_KEY = 0x40,
    CMD_ARG_USE_ARGVAL = 0x80,
};

typedef enum CmdSig {
    CMD_void = 0,
    CMD_ES,     /* (ES*) -> void */
    CMD_ESi,    /* (ES*, int) -> void */
    CMD_ESs,    /* (ES*, string) -> void */
    CMD_ESii,   /* (ES*, int, int) -> void */
    CMD_ESsi,   /* (ES*, string, int) -> void */
    CMD_ESss,   /* (ES*, string, string) -> void */
    CMD_ESssi,  /* (ES*, string, string, int) -> void */
    CMD_ESsss,  /* (ES*, string, string, string) -> void */
} CmdSig;

#define MAX_CMD_ARGS 5

typedef union CmdArg {
    EditState *s;
    const char *p;
    int n;
} CmdArg;

typedef union CmdProto {
    void (*func)(void);
    void (*ES)(EditState *);
    void (*ESi)(EditState *, int);
    void (*ESs)(EditState *, const char *);
    void (*ESii)(EditState *, int, int);
    void (*ESsi)(EditState *, const char *, int);
    void (*ESss)(EditState *, const char *, const char *);
    void (*ESssi)(EditState *, const char *, const char *, int);
    void (*ESsss)(EditState *, const char *, const char *, const char *);
    struct CmdDef *next;
} CmdProto;

typedef struct CmdDef {
    unsigned short key;       /* normal key */
    unsigned short alt_key;   /* alternate key */
    const char *name;
    CmdProto action;
    CmdSig sig : 8;
    signed int val : 24;
} CmdDef;

/* new command macros */
#define CMD2(key, key_alt, name, func, sig, args) \
    { key, key_alt, name "\0" args, { .sig = func }, CMD_ ## sig, 0 },
#define CMD3(key, key_alt, name, func, sig, val, args) \
    { key, key_alt, name "\0" args, { .sig = func }, CMD_ ## sig, val },

/* old macros for compatibility */
#define CMD0(key, key_alt, name, func) \
    { key, key_alt, name "\0", { .ES = func }, CMD_ES, 0 },
#define CMD1(key, key_alt, name, func, val) \
    { key, key_alt, name "\0v", { .ESi = func }, CMD_ESi, val },
#define CMD_DEF_END \
    { 0, 0, NULL, { NULL }, CMD_void, 0 }

void qe_register_mode(ModeDef *m);
void mode_completion(CompleteState *cp);
void qe_register_cmd_table(CmdDef *cmds, ModeDef *m);
int qe_register_binding(int key, const char *cmd_name, ModeDef *m);
CmdDef *qe_find_cmd(const char *cmd_name);
void qe_get_prototype(CmdDef *d, char *buf, int size);

/* text display system */

typedef struct TextFragment {
    unsigned short embedding_level;
    short width; /* fragment width */
    short ascent;
    short descent;
    short style;      /* style index */
    short line_index; /* index in line_buf */
    short len;   /* number of glyphs */
    short dummy;  /* align, must be assigned for CRC */
} TextFragment;

#define MAX_WORD_SIZE 128
#define NO_CURSOR 0x7fffffff

#define STYLE_BITS       12
#define STYLE_SHIFT      (32 - STYLE_BITS)
#define STYLE_MASK       (((1 << STYLE_BITS) - 1) << STYLE_SHIFT)
#define CHAR_MASK        ((1 << STYLE_SHIFT) - 1)

struct DisplayState {
    int do_disp;        /* true if real display */
    int width;          /* display window width */
    int height;         /* display window height */
    int eol_width;      /* width of eol char */
    int default_line_height;  /* line height if no chars */
    int space_width;    /* width of space character */
    int tab_width;      /* width of tabulation */
    int x_disp;         /* starting x display */
    int x;              /* current x position */
    int y;              /* current y position */
    int line_num;       /* current text line number */
    int cur_hex_mode;   /* true if current char is in hex mode */
    int hex_mode;       /* hex mode from edit_state, -1 if all chars wanted */
    void *cursor_opaque;
    int (*cursor_func)(struct DisplayState *,
                       int offset1, int offset2, int line_num,
                       int x, int y, int w, int h, int hex_mode);
    int eod;            /* end of display requested */
    /* if base == RTL, then all x are equivalent to width - x */
    DirType base;
    int embedding_level_max;
    int wrap;
    int eol_reached;
    EditState *edit_state;
    int style;          /* css display style */

    /* fragment buffers */
    TextFragment fragments[MAX_SCREEN_WIDTH];
    int nb_fragments;
    int last_word_space; /* true if last word was a space */
    int word_index;     /* fragment index of the start of the current word */
    /* line char (in fact glyph) buffer */
    unsigned int line_chars[MAX_SCREEN_WIDTH];
    short line_char_widths[MAX_SCREEN_WIDTH];
    int line_offsets[MAX_SCREEN_WIDTH][2];
    unsigned char line_hex_mode[MAX_SCREEN_WIDTH];
    int line_index;

    /* fragment temporary buffer */
    unsigned int fragment_chars[MAX_WORD_SIZE];
    int fragment_offsets[MAX_WORD_SIZE][2];
    unsigned char fragment_hex_mode[MAX_WORD_SIZE];
    int fragment_index;
    int last_space;
    int last_style;
    int last_embedding_level;
};

enum DisplayType {
    DISP_CURSOR,
    DISP_PRINT,
    DISP_CURSOR_SCREEN,
};

void display_init(DisplayState *s, EditState *e, enum DisplayType do_disp);
void display_bol(DisplayState *s);
void display_setcursor(DisplayState *s, DirType dir);
int display_char_bidir(DisplayState *s, int offset1, int offset2,
                       int embedding_level, int ch);
void display_eol(DisplayState *s, int offset1, int offset2);

void display_printf(DisplayState *ds, int offset1, int offset2,
                    const char *fmt, ...) __attr_printf(4,5);
void display_printhex(DisplayState *s, int offset1, int offset2,
                      unsigned int h, int n);

static inline int display_char(DisplayState *s, int offset1, int offset2,
                               int ch)
{
    return display_char_bidir(s, offset1, offset2, 0, ch);
}

static inline void set_color(unsigned int *p, const unsigned int *to, int style) {
    style <<= STYLE_SHIFT;
    while (p < to)
        *p++ |= style;
}

static inline void set_color1(unsigned int *p, int style) {
    *p |= style << STYLE_SHIFT;
}

static inline void clear_color(unsigned int *p, int count) {
    int i;

    for (i = 0; i < count; i++)
        p[i] &= ~STYLE_MASK;
}

/* input.c */

#define INPUTMETHOD_NOMATCH   (-1)
#define INPUTMETHOD_MORECHARS (-2)

typedef struct InputMethod {
    const char *name;
    /* input match returns:
       n > 0: number of code points in replacement found for a sequence
       of keystrokes in buf.  number of keystrokes in match was stored
       in '*match_len_ptr'.
       INPUTMETHOD_NOMATCH if no match was found
       INPUTMETHOD_MORECHARS if more chars need to be typed to find
         a suitable completion
     */
    int (*input_match)(int *match_buf, int match_buf_size,
                       int *match_len_ptr, const u8 *data,
                       const unsigned int *buf, int len);
    const u8 *data;
    struct InputMethod *next;
} InputMethod;

void register_input_method(InputMethod *m);
void do_set_input_method(EditState *s, const char *method);
void do_switch_input_method(EditState *s);
void init_input_methods(void);
int load_input_methods(void);
void unload_input_methods(void);

/* the following will be suppressed */
#define LINE_MAX_SIZE 256

static inline int align(int a, int n) {
    return (a / n) * n;
}

static inline int scale(int a, int b, int c) {
    return (a * b + c / 2) / c;
}

/* minibuffer & status */

void less_mode_init(void);
void minibuffer_init(void);

extern CmdDef minibuffer_commands[];
extern CmdDef less_commands[];

typedef void (*CompletionFunc)(CompleteState *cp);

typedef struct CompletionEntry {
    const char *name;
    CompletionFunc completion_func;
    struct CompletionEntry *next;
} CompletionEntry;

void complete_test(CompleteState *cp, const char *str);

void register_completion(const char *name, CompletionFunc completion_func);
void put_status(EditState *s, const char *fmt, ...) __attr_printf(2,3);
void put_error(EditState *s, const char *fmt, ...) __attr_printf(2,3);
void minibuffer_edit(const char *input, const char *prompt,
                     StringArray *hist, CompletionFunc completion_func,
                     void (*cb)(void *opaque, char *buf), void *opaque);
void command_completion(CompleteState *cp);
void file_completion(CompleteState *cp);
void buffer_completion(CompleteState *cp);

#ifdef CONFIG_WIN32
static inline int is_user_input_pending(void) {
    return 0;
}
#else
extern int __fast_test_event_poll_flag;
int __is_user_input_pending(void);

static inline int is_user_input_pending(void) {
    if (__fast_test_event_poll_flag) {
        __fast_test_event_poll_flag = 0;
        return __is_user_input_pending();
    } else {
        return 0;
    }
}
#endif

/* config file support */
void do_load_config_file(EditState *e, const char *file);
void do_load_qerc(EditState *e, const char *file);

/* popup / low level window handling */
void show_popup(EditBuffer *b);
int check_read_only(EditState *s);
EditState *insert_window_left(EditBuffer *b, int width, int flags);
EditState *find_window(EditState *s, int key);
void do_find_window(EditState *s, int key);

/* window handling */
void edit_close(EditState *s);
EditState *edit_new(EditBuffer *b,
                    int x1, int y1, int width, int height, int flags);
void edit_detach(EditState *s);
void edit_append(EditState *s, EditState *e);
EditState *edit_find(EditBuffer *b);
void do_refresh(EditState *s);
// should take direction argument
void do_other_window(EditState *s);
void do_previous_window(EditState *s);
void do_delete_window(EditState *s, int force);
// should take argval
void do_split_window(EditState *s, int horiz);
void edit_display(QEmacsState *qs);
void edit_invalidate(EditState *s);
void display_mode_line(EditState *s);
void edit_set_mode(EditState *s, ModeDef *m, ModeSavedData *saved_data);

/* loading files */
void do_exit_qemacs(EditState *s, int argval);
void do_find_file(EditState *s, const char *filename);
void do_load_from_path(EditState *s, const char *filename);
void do_find_file_other_window(EditState *s, const char *filename);
void do_switch_to_buffer(EditState *s, const char *bufname);
void do_break(EditState *s);
void do_insert_file(EditState *s, const char *filename);
// should take argument?
void do_save_buffer(EditState *s);
void do_write_file(EditState *s, const char *filename);
void do_write_region(EditState *s, const char *filename);
// should take argument?
void do_isearch(EditState *s, int dir);
void do_query_replace(EditState *s, const char *search_str,
                      const char *replace_str);
void do_replace_string(EditState *s, const char *search_str,
                       const char *replace_str, int argval);
void do_search_string(EditState *s, const char *search_str, int dir);
void do_refresh_complete(EditState *s);
void do_kill_buffer(EditState *s, const char *bufname1);
void switch_to_buffer(EditState *s, EditBuffer *b);

/* text mode */

extern ModeDef text_mode;

int text_mode_init(EditState *s, ModeSavedData *saved_data);
void text_mode_close(EditState *s);
int text_backward_offset(EditState *s, int offset);
int text_display(EditState *s, DisplayState *ds, int offset);

void set_colorize_func(EditState *s, ColorizeFunc colorize_func);
int generic_get_colorized_line(EditState *s, unsigned int *buf, int buf_size,
                               int *offsetp, int line_num);
int get_non_colorized_line(EditState *s, unsigned int *buf, int buf_size,
                           int *offsetp, int line_num);

void do_char(EditState *s, int key, int argval);
void do_set_mode(EditState *s, const char *name);
void text_move_left_right_visual(EditState *s, int dir);
void text_move_word_left_right(EditState *s, int dir);
void text_move_up_down(EditState *s, int dir);
void text_scroll_up_down(EditState *s, int dir);
void text_write_char(EditState *s, int key);
void do_return(EditState *s, int move);
void do_backspace(EditState *s, int argval);
void do_delete_char(EditState *s, int argval);
void do_tab(EditState *s, int argval);
EditBuffer *new_yank_buffer(QEmacsState *qs);
void do_append_next_kill(EditState *s);
void do_kill(EditState *s, int p1, int p2, int dir);
void do_kill_region(EditState *s, int killtype);
void do_kill_line(EditState *s, int dir);
void do_kill_word(EditState *s, int dir);
void text_move_bol(EditState *s);
void text_move_eol(EditState *s);
void word_right(EditState *s, int w);
void word_left(EditState *s, int w);
void do_goto(EditState *s, const char *str, int unit);
void do_goto_line(EditState *s, int line);
void do_up_down(EditState *s, int dir);
void do_left_right(EditState *s, int dir);
void text_mouse_goto(EditState *s, int x, int y);
int basic_mode_line(EditState *s, char *buf, int buf_size, int c1);
int text_mode_line(EditState *s, char *buf, int buf_size);
void do_toggle_full_screen(EditState *s);
void do_toggle_control_h(EditState *s, int set);

/* misc */

void do_set_emulation(EditState *s, const char *name);
void do_set_trace(EditState *s);
void do_cd(EditState *s, const char *name);
void do_set_key(EditState *s, const char *keystr, const char *cmd_name, int local);
//void do_unset_key(EditState *s, const char *keystr, int local);
void do_bof(EditState *s);
void do_eof(EditState *s);
void do_bol(EditState *s);
void do_eol(EditState *s);
void do_word_right(EditState *s, int dir);
int eb_next_paragraph(EditBuffer *b, int offset);
int eb_start_paragraph(EditBuffer *b, int offset);
void do_backward_paragraph(EditState *s);
void do_forward_paragraph(EditState *s);
void do_kill_paragraph(EditState *s, int dir);
void do_fill_paragraph(EditState *s);
void do_changecase_word(EditState *s, int up);
void do_changecase_region(EditState *s, int up);
void do_delete_word(EditState *s, int dir);
int cursor_func(DisplayState *ds,
                int offset1, int offset2, int line_num,
                int x, int y, int w, int h, int hex_mode);
// should take argval
void do_scroll_up_down(EditState *s, int dir);
void perform_scroll_up_down(EditState *s, int h);
void do_center_cursor(EditState *s);
void do_quote(EditState *s, int argval);
void do_insert(EditState *s);
// should take argval
void do_set_mark(EditState *s);
void do_mark_whole_buffer(EditState *s);
void do_yank(EditState *s);
void do_yank_pop(EditState *s);
void do_exchange_point_and_mark(EditState *s);
QECharset *read_charset(EditState *s, const char *charset_str);
void do_set_buffer_file_coding_system(EditState *s, const char *charset_str);
void do_convert_buffer_file_coding_system(EditState *s,
    const char *charset_str);
void do_toggle_bidir(EditState *s);
void do_toggle_line_numbers(EditState *s);
void do_toggle_truncate_lines(EditState *s);
void do_word_wrap(EditState *s);
void do_count_lines(EditState *s);
void do_what_cursor_position(EditState *s);
void do_set_tab_width(EditState *s, int tab_width);
void do_set_indent_width(EditState *s, int indent_width);
void do_set_indent_tabs_mode(EditState *s, int mode);
void display_window_borders(EditState *e);
QEStyleDef *find_style(const char *name);
void style_completion(CompleteState *cp);
void get_style(EditState *e, QEStyleDef *style, int style_index);
void style_property_completion(CompleteState *cp);
int find_style_property(const char *name);
void do_define_color(EditState *e, const char *name, const char *value);
void do_set_style(EditState *e, const char *stylestr,
                  const char *propstr, const char *value);
void do_set_display_size(EditState *s, int w, int h);
void do_toggle_mode_line(EditState *s);
void do_set_system_font(EditState *s, const char *qe_font_name,
                        const char *system_fonts);
void call_func(CmdSig sig, CmdProto func, int nb_args, CmdArg *args,
               unsigned char *args_type);
void exec_command(EditState *s, CmdDef *d, int argval, int key);
void do_execute_command(EditState *s, const char *cmd, int argval);
void window_display(EditState *s);
void do_numeric_argument(EditState *s);
void do_start_macro(EditState *s);
void do_end_macro(EditState *s);
void do_call_macro(EditState *s);
void do_execute_macro_keys(EditState *s, const char *keys);
void do_define_kbd_macro(EditState *s, const char *name, const char *keys,
                         const char *key_bind);
void edit_attach(EditState *s, EditState **ep);
void do_completion(EditState *s);
void do_completion_space(EditState *s);
void do_electric_filename(EditState *s, int key);
void minibuf_complete_scroll_up_down(EditState *s, int dir);
void do_history(EditState *s, int dir);
void do_minibuffer_get_binary(EditState *s);
void do_minibuffer_exit(EditState *s, int fabort);
void do_less_exit(EditState *s);
void do_toggle_read_only(EditState *s);
void do_not_modified(EditState *s, int argval);
void do_find_alternate_file(EditState *s, const char *filename);
void do_load_file_from_path(EditState *s, const char *filename);
void do_set_visited_file_name(EditState *s, const char *filename,
                              const char *renamefile);
int eb_search(EditBuffer *b, int offset, int dir, int flags,
              const u8 *buf, int size,
              CSSAbortFunc *abort_func, void *abort_opaque);
int search_abort_func(void *opaque);
void do_doctor(EditState *s);
void do_delete_other_windows(EditState *s);
void do_describe_key_briefly(EditState *s);
void do_show_bindings(EditState *s, const char *cmd_name);
void do_apropos(EditState *s, const char *str);
EditBuffer *new_help_buffer(int *show_ptr);
void do_describe_bindings(EditState *s);
void do_help_for_help(EditState *s);
void qe_event_init(void);
void window_get_min_size(EditState *s, int *w_ptr, int *h_ptr);
void window_resize(EditState *s, int target_w, int target_h);
void wheel_scroll_up_down(EditState *s, int dir);
void qe_mouse_event(QEEvent *ev);
int parse_config_file(EditState *s, const char *filename);
int parse_command_line(int argc, char **argv);
void set_user_option(const char *user);
void set_tty_charset(const char *name);

/* extras.c */

void do_compare_windows(EditState *s, int argval);
void do_delete_horizontal_space(EditState *s);
void do_show_date_and_time(EditState *s, int argval);

/* hex.c */

void hex_write_char(EditState *s, int key);

/* list.c */

extern ModeDef list_mode;

void list_toggle_selection(EditState *s);
int list_get_pos(EditState *s);
int list_get_offset(EditState *s);

/* dired.c */
void do_dired(EditState *s);

/* c_mode.c */
void c_colorize_line(unsigned int *buf, int len,
                     int *colorize_state_ptr, int state_only);

/* xml.c */
int xml_mode_probe(ModeProbeData *p1);

/* html.c */

extern ModeDef html_mode;

int gxml_mode_init(EditState *s,
                   ModeSavedData *saved_data,
                   int is_html, const char *default_stylesheet);

/* image.c */
void fill_border(EditState *s, int x, int y, int w, int h, int color);
int qe_bitmap_format_to_pix_fmt(int format);

/* shell.c */
EditBuffer *new_shell_buffer(EditBuffer *b0, const char *name,
                             const char *path, const char **argv,
                             int is_shell);

#define QASSERT(e)      do { if (!(e)) fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__, __LINE__, #e); } while (0)

#endif
