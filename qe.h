/*
 * QEmacs, tiny but powerful multimode editor
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

#ifndef QE_H
#define QE_H

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <time.h>

#if 1 //ifdef HAVE_QE_CONFIG_H
#include "config.h"
#endif

#ifndef DEFAULT_TAB_WIDTH
#define DEFAULT_TAB_WIDTH  4 /* used to be 8 */
#endif
#ifndef DEFAULT_INDENT_WIDTH
#define DEFAULT_INDENT_WIDTH  4 /* used to be 8 */
#endif
#ifndef DEFAULT_FILL_COLUMN
#define DEFAULT_FILL_COLUMN  70
#endif

/************************/

#include "cutils.h"
#include "util.h"

/************************/

typedef struct EditState EditState;
typedef struct EditBuffer EditBuffer;
typedef struct QEmacsState QEmacsState;
typedef struct DisplayState DisplayState;
typedef struct ModeProbeData ModeProbeData;
typedef struct QEModeData QEModeData;
typedef struct ModeDef ModeDef;
typedef struct QETimer QETimer;
typedef struct QEColorizeContext QEColorizeContext;
typedef struct KeyDef KeyDef;
typedef struct InputMethod InputMethod;
typedef struct ISearchState ISearchState;
typedef struct QEProperty QEProperty;

#ifndef INT_MAX
#define INT_MAX  0x7fffffff
#endif
#ifndef INT_MIN
#define INT_MIN  (-0x7fffffff-1)
#endif

#define NO_ARG  INT_MIN
#define HAS_ARG_NUMERIC  0x100
#define HAS_ARG_SIGN     0x200
#define HAS_ARG_NEGATIVE 0x400

#define MAX_FILENAME_SIZE    1024       /* Size for a filename buffer */
#define MAX_BUFFERNAME_SIZE  256        /* Size for a buffer name buffer */
#define MAX_CMDNAME_SIZE     32         /* Size for a command name buffer */

extern const char str_version[];
extern const char str_credits[];

/* low level I/O events */
void set_read_handler(int fd, void (*cb)(void *opaque), void *opaque);
void set_write_handler(int fd, void (*cb)(void *opaque), void *opaque);
int set_pid_handler(int pid,
                    void (*cb)(void *opaque, int status), void *opaque);
void url_exit(void);
void url_redisplay(void);
void register_bottom_half(void (*cb)(void *opaque), void *opaque);
void unregister_bottom_half(void (*cb)(void *opaque), void *opaque);

QETimer *qe_add_timer(int delay, void *opaque, void (*cb)(void *opaque));
void qe_kill_timer(QETimer **tip);

/* main loop for Unix programs using liburlio */
void url_main_loop(void (*init)(void *opaque), void *opaque);

int get_clock_ms(void);
int get_clock_usec(void);

/* extendable completion system */
typedef struct CompleteState {
    StringArray cs;
    struct EditState *s;
    struct EditState *target;
    struct CompletionDef *completion;
    int start, end, len, fuzzy;
    char current[MAX_FILENAME_SIZE];
} CompleteState;

void canonicalize_absolute_path(EditState *s, char *buf, int buf_size, const char *path1);
void canonicalize_absolute_buffer_path(EditBuffer *b, int offset,
                                       char *buf, int buf_size,
                                       const char *path1);

#include "color.h"

/* Command line options */
enum CmdLineOptionType {
    CMD_LINE_TYPE_NONE   = 0,  /* nothing */
    CMD_LINE_TYPE_BOOL   = 1,  /* boolean ptr */
    CMD_LINE_TYPE_INT    = 2,  /* int ptr */
    CMD_LINE_TYPE_STRING = 3,  /* string ptr */
    CMD_LINE_TYPE_FVOID  = 4,  /* function() */
    CMD_LINE_TYPE_FARG   = 5,  /* function(string) */
    CMD_LINE_TYPE_NEXT   = 6,  /* next pointer */
};

typedef struct CmdLineOptionDef {
    const char *desc;
    enum CmdLineOptionType type;
    union {
        int *int_ptr;
        const char **string_ptr;
        void (*func_noarg)(void);
        void (*func_arg)(const char *);
        struct CmdLineOptionDef *next;
    } u;
} CmdLineOptionDef;

#define CMD_LINE_NONE()          { NULL, CMD_LINE_TYPE_NONE, { NULL }}
#define CMD_LINE_BOOL(s,n,p,h)   { s "|" n "||" h, CMD_LINE_TYPE_BOOL, { .int_ptr = p }}
#define CMD_LINE_INT(s,n,a,p,h)  { s "|" n "|" a "|" h, CMD_LINE_TYPE_INT, { .int_ptr = p }}
#define CMD_LINE_STRING(s,n,a,p,h) { s "|" n "|" a "|" h, CMD_LINE_TYPE_STRING, { .string_ptr = p }}
#define CMD_LINE_FVOID(s,n,p,h)  { s "|" n "||" h, CMD_LINE_TYPE_FVOID, { .func_noarg = p }}
#define CMD_LINE_FARG(s,n,a,p,h) { s "|" n "|" a "|" h, CMD_LINE_TYPE_FARG, { .func_arg = p }}
#define CMD_LINE_LINK()          { NULL, CMD_LINE_TYPE_NEXT, { NULL }}

void qe_register_cmd_line_options(CmdLineOptionDef *table);

int find_resource_file(char *path, int path_size, const char *pattern);

/* charset.c */

/* maximum number of bytes for a character in all the supported charsets */
#define MAX_CHAR_BYTES 6

typedef struct CharsetDecodeState CharsetDecodeState;
#if defined(__cplusplus)
typedef struct QECharset QECharset;
#else
typedef const struct QECharset QECharset;
#endif

struct QECharset {
    const char *name;
    const char *aliases;
    int (*probe_func)(QECharset *charset, const u8 *buf, int size);
    void (*decode_init)(CharsetDecodeState *s);
    int (*decode_func)(CharsetDecodeState *s);
    /* return NULL if cannot encode. Currently no state since speed is
       not critical yet */
    u8 *(*encode_func)(QECharset *charset, u8 *buf, int size);
    void (*get_pos_func)(CharsetDecodeState *s, const u8 *buf, int size,
                         int *line_ptr, int *col_ptr);
    int (*get_chars_func)(CharsetDecodeState *s, const u8 *buf, int size);
    int (*goto_char_func)(CharsetDecodeState *s, const u8 *buf, int size, int pos);
    int (*goto_line_func)(CharsetDecodeState *s, const u8 *buf, int size, int lines);
    unsigned int char_size : 3;
    unsigned int variable_size : 1;
    unsigned int table_alloc : 1; /* true if CharsetDecodeState.table must be malloced */
    /* private data for some charsets */
    u8 eol_char; /* 0x0A for ASCII, 0x25 for EBCDIC */
    u8 min_char, max_char;
    const unsigned short *encode_table;
    const unsigned short *private_table;
    struct QECharset *next;
};

extern struct QECharset *first_charset;
/* predefined charsets */
extern struct QECharset charset_raw;
extern struct QECharset charset_8859_1;
extern struct QECharset charset_utf8;
extern struct QECharset charset_vt100; /* used for the tty output */
extern struct QECharset charset_mac_roman;
extern struct QECharset charset_ucs2le, charset_ucs2be;
extern struct QECharset charset_ucs4le, charset_ucs4be;

typedef enum EOLType {
    EOL_UNIX = 0,
    EOL_DOS,
    EOL_MAC,
} EOLType;

struct CharsetDecodeState {
    /* 256 ushort table for hyper fast decoding */
    const unsigned short *table;
    int char_size;
    EOLType eol_type;
    int eol_char;
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

void qe_register_charset(struct QECharset *charset);

void charset_complete(CompleteState *cp);
QECharset *find_charset(const char *str);
void charset_decode_init(CharsetDecodeState *s, QECharset *charset,
                         EOLType eol_type);
void charset_decode_close(CharsetDecodeState *s);
void charset_get_pos_8bit(CharsetDecodeState *s, const u8 *buf, int size,
                          int *line_ptr, int *col_ptr);
int charset_get_chars_8bit(CharsetDecodeState *s, const u8 *buf, int size);
int charset_goto_char_8bit(CharsetDecodeState *s, const u8 *buf, int size, int pos);
int charset_goto_line_8bit(CharsetDecodeState *s, const u8 *buf, int size, int nlines);

QECharset *detect_charset(const u8 *buf, int size, EOLType *eol_typep);

void decode_8bit_init(CharsetDecodeState *s);
int decode_8bit(CharsetDecodeState *s);
u8 *encode_8bit(QECharset *charset, u8 *q, int c);

/* arabic.c */
int arab_join(unsigned int *line, unsigned int *ctog, int len);

/* indic.c */
int devanagari_log2vis(unsigned int *str, unsigned int *ctog, int len);

/* unicode_join.c */
int unicode_to_glyphs(unsigned int *dst, unsigned int *char_to_glyph_pos,
                      int dst_size, unsigned int *src, int src_size,
                      int reverse);
int combine_accent(unsigned int *buf, int c, int accent);
int expand_ligature(unsigned int *buf, int c);
int load_ligatures(const char *filename);
void unload_ligatures(void);

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
KeyDef *qe_find_binding(unsigned int *keys, int nb_keys, KeyDef *kd, int exact);
KeyDef *qe_find_current_binding(unsigned int *keys, int nb_keys, ModeDef *m, int exact);

#define COLORED_MAX_LINE_SIZE  4096

/* colorize & transform a line, lower level then ColorizeFunc */
/* XXX: should return `len`, the number of valid codepoints copied to
 * destination excluding the null terminator and newline if present.
 * Truncation can be detected by testing if a newline is present
 * at this offset.
 */
typedef int (*GetColorizedLineFunc)(EditState *s,
                                    unsigned int *buf, int buf_size,
                                    QETermStyle *sbuf,
                                    int offset, int *offsetp, int line_num);

struct QEColorizeContext {
    EditState *s;
    EditBuffer *b;
    int offset;
    int colorize_state;
    int state_only;
    int combine_start, combine_stop; /* region for combine_static_colorized_line() */
    int cur_pos;   /* position of cursor in line or -1 if outside line */
};

/* colorize a line: this function modifies buf to set the char
 * styles. 'buf' is guaranted to have one more '\0' char after its len.
 */
typedef void (*ColorizeFunc)(QEColorizeContext *cp,
                             unsigned int *buf, int n, ModeDef *syn);

/* buffer.c */

/* begin to mmap files from this size */
#define MIN_MMAP_SIZE  (2*1024*1024)
#define MAX_LOAD_SIZE  (512*1024*1024)

#define MAX_PAGE_SIZE  4096
//#define MAX_PAGE_SIZE 16

#define NB_LOGS_MAX     100000  /* need better way to limit undo information */

#define PG_READ_ONLY    0x0001 /* the page is read only */
#define PG_VALID_POS    0x0002 /* set if the nb_lines / col fields are up to date */
#define PG_VALID_CHAR   0x0004 /* nb_chars is valid */
#define PG_VALID_COLORS 0x0008 /* color state is valid (unused) */

typedef struct Page {   /* should pack this */
    int size;     /* data size */
    int flags;
    u8 *data;
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
typedef void (*EditBufferCallback)(EditBuffer *b, void *opaque, int arg,
                                   enum LogOperation op, int offset, int size);

typedef struct EditBufferCallbackList {
    void *opaque;
    int arg;
    EditBufferCallback callback;
    struct EditBufferCallbackList *next;
} EditBufferCallbackList;

/* high level buffer type handling */
typedef struct EditBufferDataType {
    const char *name; /* name of buffer data type (text, image, ...) */
    int (*buffer_load)(EditBuffer *b, FILE *f);
    int (*buffer_save)(EditBuffer *b, int start, int end, const char *filename);
    void (*buffer_close)(EditBuffer *b);
    struct EditBufferDataType *next;
} EditBufferDataType;

/* buffer flags */
#define BF_SAVELOG   0x0001  /* activate buffer logging */
#define BF_SYSTEM    0x0002  /* buffer system, cannot be seen by the user */
#define BF_READONLY  0x0004  /* read only buffer */
#define BF_PREVIEW   0x0008  /* used in dired mode to mark previewed files */
#define BF_LOADING   0x0010  /* buffer is being loaded */
#define BF_SAVING    0x0020  /* buffer is being saved */
#define BF_DIRED     0x0100  /* buffer is interactive dired */
#define BF_UTF8      0x0200  /* buffer charset is utf-8 */
#define BF_RAW       0x0400  /* buffer charset is raw (no charset translation) */
#define BF_TRANSIENT 0x0800  /* buffer is deleted upon window close */
#define BF_STYLES    0x7000  /* buffer has styles */
#define BF_STYLE1    0x1000  /* buffer has 1 byte styles */
#define BF_STYLE2    0x2000  /* buffer has 2 byte styles */
#define BF_STYLE4    0x3000  /* buffer has 4 byte styles */
#define BF_STYLE8    0x4000  /* buffer has 8 byte styles */
#define BF_IS_STYLE  0x8000  /* buffer is a styles buffer */
#define BF_IS_LOG    0x10000  /* buffer is a log buffer */
#define BF_SHELL     0x20000  /* buffer is a shell buffer */

struct EditBuffer {
    OWNED Page *page_table;
    int nb_pages;
    int mark;       /* current mark (moved with text) */
    int total_size; /* total size of the buffer */
    int modified;
    int linum_mode;   /* display line numbers in left gutter */
    int linum_mode_set;   /* linum_mode was set, ignore global_linum_mode */

    /* page cache */
    Page *cur_page;
    int cur_offset;
    int flags;

    /* mmap data, including file handle if kept open */
    void *map_address;
    int map_length;
    int map_handle;

    /* buffer data type (default is raw) */
    ModeDef *data_mode;
    const char *data_type_name;
    EditBufferDataType *data_type;
    void *data_data;    /* associated buffer data, used if data_type != raw_data */

    /* buffer syntax or major mode */
    ModeDef *syntax_mode;
    ColorizeFunc colorize_func; /* line colorization function */
    unsigned short *colorize_states; /* state before line n, one per line */
    int colorize_nb_lines;
    int colorize_nb_valid_lines;
    /* maximum valid offset, INT_MAX if not modified. Needed to
     * invalidate 'colorize_states' */
    int colorize_max_valid_offset;

    /* charset handling */
    CharsetDecodeState charset_state;
    QECharset *charset;
    int char_bytes, char_shift;

    /* undo system */
    int save_log;    /* if true, each buffer operation is logged */
    int log_new_index, log_current;
    enum LogOperation last_log;
    int last_log_char;
    int nb_logs;
    EditBuffer *log_buffer;

    /* style system */
    EditBuffer *b_styles;
    QETermStyle cur_style;  /* current style for buffer writing APIs */
    int style_bytes;  /* 0, 1, 2, 4 or 8 bytes per char */
    int style_shift;  /* 0, 0, 1, 2 or 3 */

    /* modification callbacks */
    OWNED EditBufferCallbackList *first_callback;
    OWNED QEProperty *property_list;

#if 0
    /* asynchronous loading/saving support */
    struct BufferIOState *io_state;
    /* used during loading */
    int probed;
#endif

    ModeDef *default_mode;

    /* Saved window data from the last closed window attached to this buffer.
     * Used to restore mode and position when buffer gets re-attached
     * to the same window.
     */
    ModeDef *saved_mode;
    OWNED u8 *saved_data; /* SAVED_DATA_SIZE bytes */

    /* list of mode data associated with buffer */
    OWNED QEModeData *mode_data_list;

    /* default mode stuff when buffer is detached from window */
    int offset;

    int tab_width;
    int fill_column;
    EOLType eol_type;

    OWNED EditBuffer *next; /* next editbuffer in qe_state buffer list */

    time_t mtime;                       /* buffer last modification time */
    int st_mode;                        /* unix file mode */
    const char name[MAX_BUFFERNAME_SIZE];     /* buffer name */
    const char filename[MAX_FILENAME_SIZE];   /* file name */

    /* Should keep a stat buffer to check for file type and
     * asynchronous modifications
     */
};

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
int eb_read_one_byte(EditBuffer *b, int offset);
int eb_read(EditBuffer *b, int offset, void *buf, int size);
int eb_write(EditBuffer *b, int offset, const void *buf, int size);
int eb_insert_buffer(EditBuffer *dest, int dest_offset,
                     EditBuffer *src, int src_offset,
                     int size);
int eb_insert(EditBuffer *b, int offset, const void *buf, int size);
int eb_delete(EditBuffer *b, int offset, int size);
int eb_replace(EditBuffer *b, int offset, int size, const void *buf, int size1);
void eb_free_log_buffer(EditBuffer *b);
EditBuffer *eb_new(const char *name, int flags);
EditBuffer *eb_scratch(const char *name, int flags);
void eb_clear(EditBuffer *b);
void eb_free(EditBuffer **ep);
EditBuffer *eb_find(const char *name);
EditBuffer *eb_find_new(const char *name, int flags);
EditBuffer *eb_find_file(const char *filename);
EditState *eb_find_window(EditBuffer *b, EditState *e);

void eb_set_charset(EditBuffer *b, QECharset *charset, EOLType eol_type);
qe__attr_nonnull((3))
int eb_nextc(EditBuffer *b, int offset, int *next_ptr);
qe__attr_nonnull((3))
int eb_prevc(EditBuffer *b, int offset, int *prev_ptr);
qe__attr_nonnull((3))
int eb_next_glyph(EditBuffer *b, int offset, int *next_ptr);
qe__attr_nonnull((3))
int eb_prev_glyph(EditBuffer *b, int offset, int *prev_ptr);
int eb_skip_accents(EditBuffer *b, int offset);
int eb_skip_glyphs(EditBuffer *b, int offset, int n);
int eb_skip_chars(EditBuffer *b, int offset, int n);
int eb_delete_chars(EditBuffer *b, int offset, int n);
int eb_delete_glyphs(EditBuffer *b, int offset, int n);
int eb_goto_pos(EditBuffer *b, int line1, int col1);
int eb_get_pos(EditBuffer *b, int *line_ptr, int *col_ptr, int offset);
int eb_goto_char(EditBuffer *b, int pos);
int eb_get_char_offset(EditBuffer *b, int offset);
int eb_delete_range(EditBuffer *b, int p1, int p2);
static inline int eb_at_bol(EditBuffer *b, int offset) {
    return eb_prevc(b, offset, &offset) == '\n';
}
static inline int eb_next(EditBuffer *b, int offset) {
    eb_nextc(b, offset, &offset);
    return offset;
}
static inline int eb_prev(EditBuffer *b, int offset) {
    eb_prevc(b, offset, &offset);
    return offset;
}

//int eb_clip_offset(EditBuffer *b, int offset);
void do_undo(EditState *s);
void do_redo(EditState *s);

int eb_raw_buffer_load1(EditBuffer *b, FILE *f, int offset);
int eb_mmap_buffer(EditBuffer *b, const char *filename);
void eb_munmap_buffer(EditBuffer *b);
int eb_write_buffer(EditBuffer *b, int start, int end, const char *filename);
int eb_save_buffer(EditBuffer *b);

int eb_set_buffer_name(EditBuffer *b, const char *name1);
void eb_set_filename(EditBuffer *b, const char *filename);

int eb_add_callback(EditBuffer *b, EditBufferCallback cb, void *opaque, int arg);
void eb_free_callback(EditBuffer *b, EditBufferCallback cb, void *opaque);
void eb_offset_callback(EditBuffer *b, void *opaque, int edge,
                        enum LogOperation op, int offset, int size);
int eb_create_style_buffer(EditBuffer *b, int flags);
void eb_free_style_buffer(EditBuffer *b);
QETermStyle eb_get_style(EditBuffer *b, int offset);
void eb_set_style(EditBuffer *b, QETermStyle style, enum LogOperation op,
                  int offset, int size);
void eb_style_callback(EditBuffer *b, void *opaque, int arg,
                       enum LogOperation op, int offset, int size);
int eb_delete_uchar(EditBuffer *b, int offset);
int eb_encode_uchar(EditBuffer *b, char *buf, unsigned int c);
int eb_insert_uchar(EditBuffer *b, int offset, int c);
int eb_replace_uchar(EditBuffer *b, int offset, int c);
int eb_insert_uchars(EditBuffer *b, int offset, int c, int n);
static inline int eb_insert_spaces(EditBuffer *b, int offset, int n) {
    return eb_insert_uchars(b, offset, ' ', n);
}

int eb_insert_utf8_buf(EditBuffer *b, int offset, const char *buf, int len);
int eb_insert_u32_buf(EditBuffer *b, int offset, const unsigned int *buf, int len);
int eb_insert_str(EditBuffer *b, int offset, const char *str);
int eb_match_uchar(EditBuffer *b, int offset, int c, int *offsetp);
int eb_match_str(EditBuffer *b, int offset, const char *str, int *offsetp);
int eb_match_istr(EditBuffer *b, int offset, const char *str, int *offsetp);
/* These functions insert contents at b->offset */
int eb_vprintf(EditBuffer *b, const char *fmt, va_list ap) qe__attr_printf(2,0);
int eb_printf(EditBuffer *b, const char *fmt, ...) qe__attr_printf(2,3);
int eb_puts(EditBuffer *b, const char *s);
int eb_putc(EditBuffer *b, int c);

void eb_line_pad(EditBuffer *b, int offset, int n);
int eb_get_region_content_size(EditBuffer *b, int start, int stop);
static inline int eb_get_content_size(EditBuffer *b) {
    return eb_get_region_content_size(b, 0, b->total_size);
}
int eb_get_region_contents(EditBuffer *b, int start, int stop,
                           char *buf, int buf_size);
static inline int eb_get_contents(EditBuffer *b, char *buf, int buf_size) {
    return eb_get_region_contents(b, 0, b->total_size, buf, buf_size);
}
int eb_insert_buffer_convert(EditBuffer *dest, int dest_offset,
                             EditBuffer *src, int src_offset,
                             int size);
int eb_get_line(EditBuffer *b, unsigned int *buf, int buf_size,
                int offset, int *offset_ptr);
int eb_fgets(EditBuffer *b, char *buf, int buf_size,
             int offset, int *offset_ptr);
int eb_prev_line(EditBuffer *b, int offset);
int eb_goto_bol(EditBuffer *b, int offset);
int eb_goto_bol2(EditBuffer *b, int offset, int *countp);
int eb_is_blank_line(EditBuffer *b, int offset, int *offset1);
int eb_is_in_indentation(EditBuffer *b, int offset);
int eb_goto_eol(EditBuffer *b, int offset);
int eb_next_line(EditBuffer *b, int offset);

void eb_register_data_type(EditBufferDataType *bdt);
EditBufferDataType *eb_probe_data_type(const char *filename, int st_mode,
                                       uint8_t *buf, int buf_size);
void eb_set_data_type(EditBuffer *b, EditBufferDataType *bdt);
void eb_invalidate_raw_data(EditBuffer *b);
extern EditBufferDataType raw_data_type;

struct QEProperty {
    int offset;
#define QE_PROP_FREE  1
#define QE_PROP_TAG   3
    int type;
    void *data;
    QEProperty *next;
};

void eb_add_property(EditBuffer *b, int offset, int type, void *data);
QEProperty *eb_find_property(EditBuffer *b, int offset, int offset2, int type);
void eb_delete_properties(EditBuffer *b, int offset, int offset2);

/* qe module handling */

#ifdef QE_MODULE

/* dynamic module case */

#define qe_module_init(fn) \
        int qe__module_init(void) { return fn(); }

#define qe_module_exit(fn) \
        void qe__module_exit(void) { fn(); }

#else /* QE_MODULE */

void init_all_modules(void);

#define qe_module_init(fn) \
        extern int module_##fn(void); \
        int module_##fn(void) { return fn(); }

#define qe_module_exit(fn)

#endif /* QE_MODULE */

/* display.c */

#include "display.h"

/* qe.c */

extern int disable_crc;      /* Prevent CRC based display cacheing */

/* contains all the information necessary to uniquely identify a line,
   to avoid displaying it */
typedef struct QELineShadow {
    uint64_t crc;
    int x;
    short y;
    short height;
} QELineShadow;

enum WrapType {
    WRAP_AUTO = 0,
    WRAP_TRUNCATE,
    WRAP_LINE,
    WRAP_TERM,
    WRAP_WORD,
};

#define DIR_LTR 0
#define DIR_RTL 1

struct EditState {
    int offset;     /* offset of the cursor */
    /* text display state */
    int offset_top; /* offset of first character displayed in window */
    int offset_bottom; /* offset of first character beyond window or -1
                        * if end of file displayed */
    int y_disp;    /* virtual position of the displayed text */
    int x_disp[2]; /* position for LTR and RTL text resp. */
    int dump_width;  /* width in binary, hex and unihex modes */
    int hex_mode;    /* true if we are currently editing hexa */
    int unihex_mode; /* true if unihex editing (width of hex char dump) */
    int hex_nibble;  /* current hexa nibble */
    int bidir;
    int cur_rtl;     /* TRUE if the cursor on over RTL chars */
    enum WrapType wrap;
    /* XXX: these should be buffer specific rather than window specific */
    int wrap_cols;   /* number of columns in terminal emulator */
    int overwrite;   /* insert/overtype mode */
    int indent_size;    /* amount of whitespace for block indentation */
    int indent_tabs_mode; /* if true, use tabs to indent */
    int interactive; /* true if interaction is done instead of editing
                        (e.g. for shell mode or HTML) */
    int force_highlight;  /* if true, force showing of cursor even if
                             window not focused (list mode only) */
    int mouse_force_highlight; /* if true, mouse can force highlight
                                  (list mode only) */
    int up_down_last_x; /* last x offset for vertical movement */

    /* low level colorization function */
    ColorizeFunc colorize_func; /* colorization function and mode */
    ModeDef *colorize_mode;

    QETermStyle default_style;  /* default text style */

    /* after this limit, the fields are not saved into the buffer */
    int end_of_saved_data;

    EditBuffer *b;

    EditBuffer *last_buffer;    /* for predict_switch_to_buffer */
    ISearchState *isearch_state;  /* active search to colorize matches */
    EditState *target_window;   /* for minibuf, popleft and popup windows */

    /* mode specific info */
    ModeDef *mode;
    OWNED QEModeData *mode_data; /* mode private window based data */

    /* state before line n, one short per line */
    /* XXX: move this to buffer based mode_data */
    unsigned short *colorize_states;
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
    int xleft, ytop;
    int width, height;
    int char_width, line_height;
    int cols, rows;
    /* full window size, including borders */
    int x1, y1, x2, y2;         /* window coordinates in device units */
    //int xx1, yy1, xx2, yy2;     /* window coordinates in 1/1000 */

    int flags; /* display flags */
#define WF_POPUP      0x0001 /* popup window (with borders) */
#define WF_MODELINE   0x0002 /* mode line must be displayed */
#define WF_RSEPARATOR 0x0004 /* right window separator */
#define WF_POPLEFT    0x0008 /* left side window */
#define WF_HIDDEN     0x0010 /* hidden window, used for temporary changes */
#define WF_MINIBUF    0x0020 /* true if single line editing */
#define WF_FILELIST   0x1000 /* window is interactive file list */

    OWNED char *prompt;  /* optional window prompt, utf8 */
    OWNED char *caption;  /* optional window caption or title, utf8 */
    //const char *mode_line;
    //const char *title;
    struct QEmacsState *qe_state;
    struct QEditScreen *screen; /* copy of qe_state->screen */
    /* display shadow to optimize redraw */
    char modeline_shadow[MAX_SCREEN_WIDTH];
    OWNED QELineShadow *line_shadow; /* per window shadow CRC data */
    int shadow_nb_lines;
    /* compose state for input method */
    InputMethod *input_method; /* current input method */
    InputMethod *selected_input_method; /* selected input method (used to switch) */
    int compose_len;
    int compose_start_offset;
    unsigned int compose_buf[20];
    OWNED EditState *next_window;
};

/* Ugly patch for saving/restoring window data upon switching buffer */
#define SAVED_DATA_SIZE  offsetof(EditState, end_of_saved_data)

struct ModeProbeData {
    const char *real_filename;
    const char *filename;  /* reduced filename for mode matching purposes */
    const u8 *buf;
    int buf_size;
    int line_len;
    int st_errno;    /* errno from the stat system call */
    int st_mode;     /* unix file mode */
    long total_size;
    EOLType eol_type;
    CharsetDecodeState charset_state;
    QECharset *charset;
    EditBuffer *b;
};

struct QEModeData {
    QEModeData *next;
    ModeDef *mode;
    EditState *s;
    EditBuffer *b;
    /* Other mode specific data follows */
};

struct ModeDef {
    const char *name;           /* pretty name for the mode */
    const char *alt_name;       /* alternate name, for the mode setting cmd */
    const char *desc;           /* description of the mode */
    const char *extensions;
    const char *shell_handlers;
    const char *keywords;       /* list of keywords for a language mode */
    const char *types;          /* list of types for a language mode */

    int flags;
#define MODEF_NOCMD        0x8000 /* do not register xxx-mode command automatically */
#define MODEF_VIEW         0x01
#define MODEF_SYNTAX       0x02
#define MODEF_MAJOR        0x04
#define MODEF_DATATYPE     0x10
#define MODEF_SHELLPROC    0x20
#define MODEF_NEWINSTANCE  0x100
#define MODEF_NO_TRAILING_BLANKS  0x200
    int buffer_instance_size;   /* size of malloced buffer state  */
    int window_instance_size;   /* size of malloced window state */

    /* return the percentage of confidence */
    int (*mode_probe)(ModeDef *m, ModeProbeData *pd);
    int (*mode_init)(EditState *s, EditBuffer *b, int flags);
    void (*mode_close)(EditState *s);
    void (*mode_free)(EditBuffer *b, void *state);

    /* low level display functions (must be NULL to use text related
       functions)*/
    void (*display_hook)(EditState *);
    void (*display)(EditState *);

    /* text related functions */
    int (*display_line)(EditState *, DisplayState *, int);
    int (*backward_offset)(EditState *, int);

    ColorizeFunc colorize_func;
    int colorize_flags;
    int auto_indent;
    int default_wrap;

    /* common functions are defined here */
    /* TODO: Should have single move function with move type and argument */
    void (*move_up_down)(EditState *s, int dir);
    void (*move_left_right)(EditState *s, int dir);
    void (*move_bol)(EditState *s);
    void (*move_eol)(EditState *s);
    void (*move_bof)(EditState *s);
    void (*move_eof)(EditState *s);
    void (*move_word_left_right)(EditState *s, int dir);
    void (*scroll_up_down)(EditState *s, int dir);
    void (*scroll_line_up_down)(EditState *s, int dir);
    void (*mouse_goto)(EditState *s, int x, int y);

    /* Functions to insert and delete contents: */
    void (*write_char)(EditState *s, int c);
    void (*delete_bytes)(EditState *s, int offset, int size);

    EditBufferDataType *data_type; /* native buffer data type (NULL = raw) */
    void (*get_mode_line)(EditState *s, buf_t *out);
    void (*indent_func)(EditState *s, int offset);
    /* Get the current directory for the window, return NULL if none */
    char *(*get_default_path)(EditBuffer *s, int offset,
                              char *buf, int buf_size);

    /* mode specific key bindings */
    const char * const *bindings;
    // XXX: should also have local and global command definitions

    ModeDef *fallback;  /* use bindings from fallback mode */

    // XXX: should have a separate list to allow for contant data
    struct KeyDef *first_key;
    ModeDef *next;
};

QEModeData *qe_create_buffer_mode_data(EditBuffer *b, ModeDef *m);
void *qe_get_buffer_mode_data(EditBuffer *b, ModeDef *m, EditState *e);
QEModeData *qe_create_window_mode_data(EditState *s, ModeDef *m);
void *qe_get_window_mode_data(EditState *e, ModeDef *m, int status);
void *check_mode_data(void **pp);
int qe_free_mode_data(QEModeData *md);

/* from tty.c */
/* set from command line option to prevent GUI such as X11 */
extern int force_tty;

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

struct CmdDefArray {
    const struct CmdDef *array;
    int count;
    int allocated;
};

struct QEmacsState {
    QEditScreen *screen;
    //struct QEDisplay *first_dpy;
    struct ModeDef *first_mode;
    struct KeyDef *first_key;
    struct CmdDefArray *cmd_array;
    int cmd_array_count;
    int cmd_array_size;
    struct CompletionDef *first_completion;
    struct HistoryEntry *first_history;
    //struct QECharset *first_charset;
    //struct QETimer *first_timer;
    struct VarDef *first_variable;
    InputMethod *input_methods;
    EditState *first_window;
    EditState *active_window; /* window in which we edit */
    EditBuffer *first_buffer;
    EditBufferDataType *first_buffer_data_type;
    //EditBuffer *message_buffer;
#ifndef CONFIG_TINY
    EditBuffer **buffer_cache;
    int buffer_cache_size;
    int buffer_cache_len;
#endif
    EditBuffer *trace_buffer;
    int trace_flags;
    int trace_buffer_state;
#define EB_TRACE_TTY      0x01
#define EB_TRACE_KEY      0x02
#define EB_TRACE_COMMAND  0x04
#define EB_TRACE_SHELL    0x08
#define EB_TRACE_PTY      0x10
#define EB_TRACE_EMULATE  0x20
#define EB_TRACE_DEBUG    0x30
#define EB_TRACE_ALL      0x7F
#define EB_TRACE_FLUSH    0x100

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
    /* XXX: move these to ec */
    CmdFunc last_cmd_func; /* last executed command function call */
    CmdFunc this_cmd_func; /* current executing command */
    int cmd_start_time;
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
    char diag_shadow[MAX_SCREEN_WIDTH];
    QErrorContext ec;
    char system_fonts[NB_FONT_FAMILIES][256];

    ///* global variables */
    int it;             /* last result from expression evaluator */
    //int force_tty;      /* prevent graphics display (X11...) */
    //int no_config;      /* prevent config file eval */
    //int force_refresh;  /* force a complete screen refresh */
    int line_number_mode;   /* display line number in mode line */
    int column_number_mode;   /* display column number in mode line */
    int global_linum_mode;   /* display line numbers in left gutter */
    int ignore_spaces;  /* ignore spaces when comparing windows */
    int ignore_comments;  /* ignore comments when comparing windows */
    int ignore_case;    /* ignore case when comparing windows */
    int hilite_region;  /* hilite the current region when selecting */
    int mmap_threshold; /* minimum file size for mmap */
    int max_load_size;  /* maximum file size for loading in memory */
    int default_tab_width;      /* DEFAULT_TAB_WIDTH */
    int default_fill_column;    /* DEFAULT_FILL_COLUMN */
    EOLType default_eol_type;  /* EOL_UNIX */
    int flag_split_window_change_focus;
    int emulation_flags;
    int backspace_is_control_h;
    int backup_inhibited;  /* prevent qemacs from backing up files */
    //int fuzzy_search;    /* use fuzzy search for completion matcher */
    int c_label_indent;
    const char *user_option;
    int input_len;
    u8 input_buf[32];
};

extern QEmacsState qe_state;

/* key bindings definitions */

/* dynamic key binding storage */

#define MAX_KEYS 10

struct KeyDef {
    struct KeyDef *next;
    const struct CmdDef *cmd;
    int nb_keys;
    unsigned int keys[1];
};

void unget_key(int key);

/* command definitions */

enum CmdArgType {
    CMD_ARG_INT = 0,
    CMD_ARG_INTVAL,
    CMD_ARG_STRING,
    CMD_ARG_STRINGVAL,
    CMD_ARG_WINDOW,
    CMD_ARG_OPAQUE,
    CMD_ARG_TYPE_MASK  = 0x0f,
    CMD_ARG_RAW_ARGVAL = 0x10,
    CMD_ARG_NUM_ARGVAL = 0x20,
    CMD_ARG_NEG_ARGVAL = 0x30,
    CMD_ARG_USE_KEY    = 0x40,
    CMD_ARG_USE_MARK   = 0x50,
    CMD_ARG_USE_POINT  = 0x60,
    CMD_ARG_USE_ZERO   = 0x70,
    CMD_ARG_USE_BSIZE  = 0x80,
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
    void *vp;
    const char *p;
    int n;
} CmdArg;

typedef struct CmdArgSpec {
    int arg_type;
    int code_letter;
    char completion[32];
    char history[32];
    char prompt[1024];   /* used for keyboard macros */
} CmdArgSpec;

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
} CmdProto;

typedef struct CmdDef {
    const char *name;
    const char *spec;
    CmdSig sig : 8;
    signed int val : 24;
    CmdProto action;
} CmdDef;

#ifdef CONFIG_TINY
/* omit command descriptions in Tiny build */
#define CMD(name, bindings, desc, func, sig, val, spec) \
    { name "\0" bindings, spec "\0", CMD_ ## sig, val, { .sig = func } },
#else
#define CMD(name, bindings, desc, func, sig, val, spec) \
    { name "\0" bindings, spec "\0" desc, CMD_ ## sig, val, { .sig = func } },
#endif
/* command without arguments, no buffer modification */
#define CMD0(name, bindings, desc, func) \
    CMD(name, bindings, desc, func, ES, 0, "")
/* command with a single implicit int argument */
#define CMD1(name, bindings, desc, func, val) \
    CMD(name, bindings, desc, func, ESi, val, "v")
/* command with a signature and an argument description string */
#define CMD2(name, bindings, desc, func, sig, spec) \
    CMD(name, bindings, desc, func, sig, 0, spec)
/* command with a an argument description string and an int argument */
#define CMD3(name, bindings, desc, func, sig, spec, val) \
    CMD(name, bindings, desc, func, sig, val, spec)
/* command not implemented yet */
#define CMDx(name, bindings, desc, func, ...)

ModeDef *qe_find_mode(const char *name, int flags);
ModeDef *qe_find_mode_filename(const char *filename, int flags);
void qe_register_mode(ModeDef *m, int flags);
void mode_complete(CompleteState *cp);
int qe_register_commands(ModeDef *m, const CmdDef *cmds, int len);
int qe_register_bindings(ModeDef *m, const char *cmd_name, const char *keys);
const CmdDef *qe_find_cmd(const char *cmd_name);
int qe_get_prototype(const CmdDef *d, char *buf, int size);
int qe_list_bindings(const CmdDef *d, ModeDef *mode, int inherit, char *buf, int size);

/* text display system */

typedef struct TextFragment {
    unsigned short embedding_level;
    short width;       /* fragment width */
    short ascent;
    short descent;
    QETermStyle style; /* composite style */
    short line_index;  /* index into line_buf */
    short len;         /* number of glyphs */
#if QE_TERM_STYLE_BITS == 16
    short dummy;       /* alignment, must be set for CRC */
#endif
} TextFragment;

#ifdef CONFIG_TINY
#define MAX_UNICODE_DISPLAY  0xFFFF
#else
#define MAX_UNICODE_DISPLAY  0x10FFFF
#endif

#define MAX_WORD_SIZE  128
#define NO_CURSOR      0x7fffffff

#define STYLE_BITS     8
#define STYLE_SHIFT    (32 - STYLE_BITS)
#define CHAR_MASK      ((1 << STYLE_SHIFT) - 1)

struct DisplayState {
    int do_disp;        /* true if real display */
    int width;          /* display window width */
    int height;         /* display window height */
    int eol_width;      /* width of eol char */
    int default_line_height;  /* line height if no chars */
    int space_width;    /* width of space character */
    int tab_width;      /* width of tabulation */
    int x_disp;         /* starting x display */
    int x_start;        /* start_x adjusted for RTL */
    int x_line;         /* updated x position for line */
    int left_gutter;    /* width of the gutter at the left of output */
    int x;              /* current x position */
    int y;              /* current y position */
    int line_num;       /* current text line number */
    int cur_hex_mode;   /* true if current char is in hex mode */
    int hex_mode;       /* hex mode from edit_state, -1 if all chars wanted */
    int line_numbers;   /* display line numbers if enough space */
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
    QETermStyle style;   /* current style for display_printf... */

#if 0
    QEFont *font;
    QEStyleDef style_cache;
#endif

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
    int last_embedding_level;
    QETermStyle last_style;
};

enum DisplayType {
    DISP_NONE,
    DISP_CURSOR,
    DISP_PRINT,
    DISP_CURSOR_SCREEN,
};

void display_init(DisplayState *s, EditState *e, enum DisplayType do_disp,
                  int (*cursor_func)(DisplayState *,
                                     int offset1, int offset2, int line_num,
                                     int x, int y, int w, int h, int hex_mode),
                  void *cursor_opaque);
void display_close(DisplayState *s);
void display_bol(DisplayState *s);
void display_setcursor(DisplayState *s, DirType dir);
int display_char_bidir(DisplayState *s, int offset1, int offset2,
                       int embedding_level, int ch);
void display_eol(DisplayState *s, int offset1, int offset2);

void display_printf(DisplayState *ds, int offset1, int offset2,
                    const char *fmt, ...) qe__attr_printf(4,5);
void display_printhex(DisplayState *s, int offset1, int offset2,
                      unsigned int h, int n);

static inline int display_char(DisplayState *s, int offset1, int offset2,
                               int ch)
{
    return display_char_bidir(s, offset1, offset2, 0, ch);
}

#define SET_COLOR(str,a,b,style)  set_color((str) + (a), (str) + (b), style)

static inline void set_color(unsigned int *p, unsigned int *to, int style) {
    unsigned int bits = (unsigned int)style << STYLE_SHIFT;
    while (p < to)
        *p++ |= bits;
}

#define SET_COLOR1(str,a,style)  set_color1((str) + (a), style)

static inline void set_color1(unsigned int *p, int style) {
    *p |= (unsigned int)style << STYLE_SHIFT;
}

/* input.c */

#define INPUTMETHOD_NOMATCH   (-1)
#define INPUTMETHOD_MORECHARS (-2)

struct InputMethod {
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
    InputMethod *next;
};

void register_input_method(InputMethod *m);
void do_set_input_method(EditState *s, const char *method);
void do_switch_input_method(EditState *s);
void init_input_methods(void);
int load_input_methods(const char *filename);
void unload_input_methods(void);

/* the following will be suppressed */
#define LINE_MAX_SIZE 256

/* minibuffer & status */

void minibuffer_init(void);

typedef struct CompletionDef {
    const char *name;
    void (*enumerate)(CompleteState *cp);
    int (*print_entry)(CompleteState *cp, EditState *s, const char *name);
    int (*get_entry)(EditState *s, char *dest, int size, int offset);
#define CF_FILENAME        1
#define CF_NO_FUZZY        2
#define CF_SPACE_OK        4
#define CF_NO_AUTO_SUBMIT  8
#define CF_DIRNAME         16
#define CF_RESOURCE        32
    int flags;
    struct CompletionDef *next;
} CompletionDef;

void qe_register_completion(CompletionDef *cp);

void complete_test(CompleteState *cp, const char *str);

void put_status(EditState *s, const char *fmt, ...) qe__attr_printf(2,3);
void put_error(EditState *s, const char *fmt, ...) qe__attr_printf(2,3);
void minibuffer_edit(EditState *e, const char *input, const char *prompt,
                     StringArray *hist, const char *completion_name,
                     void (*cb)(void *opaque, char *buf), void *opaque);
void command_complete(CompleteState *cp);
int eb_command_print_entry(EditBuffer *b, const CmdDef *d, EditState *s);
int command_print_entry(CompleteState *cp, EditState *s, const char *name);
int command_get_entry(EditState *s, char *dest, int size, int offset);
void file_complete(CompleteState *cp);
int file_print_entry(CompleteState *cp, EditState *s, const char *name);
void buffer_complete(CompleteState *cp);

#ifdef CONFIG_WIN32
static inline int is_user_input_pending(void) {
    return 0;
}
#else
extern int qe__fast_test_event_poll_flag;
int qe__is_user_input_pending(void);

static inline int is_user_input_pending(void) {
    if (qe__fast_test_event_poll_flag) {
        qe__fast_test_event_poll_flag = 0;
        return qe__is_user_input_pending();
    } else {
        return 0;
    }
}
#endif

/* file loading */
#define LF_KILL_BUFFER    0x01
#define LF_LOAD_RESOURCE  0x02
#define LF_CWD_RELATIVE   0x04
#define LF_SPLIT_WINDOW   0x08
#define LF_NOSELECT       0x10
#define LF_NOWILDCARD     0x20
int qe_load_file(EditState *s, const char *filename, int lflags, int bflags);

/* config file support */
void do_load_config_file(EditState *e, const char *file);
void do_load_qerc(EditState *e, const char *file);
void do_add_resource_path(EditState *s, const char *path);

/* popup / low level window handling */
EditState *show_popup(EditState *s, EditBuffer *b, const char *caption);
int check_read_only(EditState *s);
EditState *insert_window_left(EditBuffer *b, int width, int flags);
EditState *find_window(EditState *s, int key, EditState *def);
EditState *qe_find_target_window(EditState *s, int activate);
void do_find_window(EditState *s, int key);

/* window handling */
void edit_close(EditState **sp);
EditState *edit_new(EditBuffer *b,
                    int x1, int y1, int width, int height, int flags);
EditBuffer *check_buffer(EditBuffer **sp);
EditState *check_window(EditState **sp);
int get_glyph_width(QEditScreen *screen, EditState *s, QETermStyle style, int c);
int get_line_height(QEditScreen *screen, EditState *s, QETermStyle style);
void do_refresh(EditState *s);
// should take direction argument
void do_other_window(EditState *s);
void do_previous_window(EditState *s);
void do_delete_window(EditState *s, int force);
#define SW_STACKED       0
#define SW_SIDE_BY_SIDE  1
EditState *qe_split_window(EditState *s, int side_by_side, int prop);
void do_split_window(EditState *s, int prop, int side_by_side);
void do_create_window(EditState *s, const char *filename, const char *layout);
void qe_save_window_layout(EditState *s, EditBuffer *b);

void edit_display(QEmacsState *qs);
void edit_invalidate(EditState *s, int all);
void display_mode_line(EditState *s);
int edit_set_mode(EditState *s, ModeDef *m);
void qe_set_next_mode(EditState *s, int n, int status);
void do_set_next_mode(EditState *s, int n);

/* loading files */
void do_exit_qemacs(EditState *s, int argval);
char *get_default_path(EditBuffer *b, int offset, char *buf, int buf_size);
void do_find_file(EditState *s, const char *filename, int bflags);
void do_load_from_path(EditState *s, const char *filename, int bflags);
void do_find_file_other_window(EditState *s, const char *filename, int bflags);
void do_switch_to_buffer(EditState *s, const char *bufname);
void do_preview_mode(EditState *s, int set);
void do_keyboard_quit(EditState *s);
void do_insert_file(EditState *s, const char *filename);
// should take argument?
void do_save_buffer(EditState *s);
void do_write_file(EditState *s, const char *filename);
void do_write_region(EditState *s, const char *filename);
void isearch_colorize_matches(EditState *s, unsigned int *buf, int len,
                              QETermStyle *sbuf, int offset);
void do_isearch(EditState *s, int argval, int dir);
void do_query_replace(EditState *s, const char *search_str,
                      const char *replace_str, int argval);
void do_replace_string(EditState *s, const char *search_str,
                       const char *replace_str, int argval);
void do_search_string(EditState *s, const char *search_str, int dir);
void do_refresh_complete(EditState *s);
void do_kill_buffer(EditState *s, const char *bufname, int force);
void switch_to_buffer(EditState *s, EditBuffer *b);
void qe_kill_buffer(EditBuffer *b);

/* text mode */

extern ModeDef text_mode;

int text_backward_offset(EditState *s, int offset);
int text_display_line(EditState *s, DisplayState *ds, int offset);

void set_colorize_func(EditState *s, ColorizeFunc colorize_func, ModeDef *mode);
int get_colorized_line(EditState *s, unsigned int *buf, int buf_size,
                       QETermStyle *sbuf,
                       int offset, int *offsetp, int line_num);

int do_delete_selection(EditState *s);
void do_char(EditState *s, int key, int argval);
void do_combine_accent(EditState *s, int accent);
void do_set_mode(EditState *s, const char *name);
void text_move_left_right_visual(EditState *s, int dir);
void text_move_word_left_right(EditState *s, int dir);
void text_move_up_down(EditState *s, int dir);
void text_scroll_up_down(EditState *s, int dir);
int text_screen_width(EditBuffer *b, int start, int stop, int tw);
void text_write_char(EditState *s, int key);
void do_newline(EditState *s);
void do_open_line(EditState *s);
void do_backspace(EditState *s, int argval);
void do_delete_char(EditState *s, int argval);
void do_tab(EditState *s, int argval);
EditBuffer *new_yank_buffer(QEmacsState *qs, EditBuffer *base);
void do_append_next_kill(EditState *s);
void do_kill(EditState *s, int p1, int p2, int dir, int keep);
void do_kill_region(EditState *s, int keep);
void do_kill_line(EditState *s, int argval);
void do_kill_beginning_of_line(EditState *s, int argval);
void do_kill_whole_line(EditState *s, int n);
void do_kill_word(EditState *s, int n);
void text_move_bol(EditState *s);
void text_move_eol(EditState *s);
void text_move_bof(EditState *s);
void text_move_eof(EditState *s);
int word_right(EditState *s, int w);
int word_left(EditState *s, int w);
int qe_get_word(EditState *s, char *buf, int buf_size,
                int offset, int *offset_ptr);
void do_goto(EditState *s, const char *str, int unit);
void do_goto_line(EditState *s, int line, int column);
void do_up_down(EditState *s, int n);
void do_left_right(EditState *s, int n);
void text_mouse_goto(EditState *s, int x, int y);
void basic_mode_line(EditState *s, buf_t *out, int c1);
void text_mode_line(EditState *s, buf_t *out);
void do_toggle_full_screen(EditState *s);
void do_toggle_control_h(EditState *s, int set);

/* misc */

void do_set_emulation(EditState *s, const char *name);
void do_set_trace_flags(EditState *s, int flags);
void do_toggle_trace_mode(EditState *s, int argval);
void do_set_trace_options(EditState *s, const char *options);
void do_cd(EditState *s, const char *name);
int qe_register_command_binding(ModeDef *m, const CmdDef *d, const char *keystr);
void do_set_key(EditState *s, const char *keystr, const char *cmd_name, int local);
void do_unset_key(EditState *s, const char *keystr, int local);
void do_bof(EditState *s);
void do_eof(EditState *s);
void do_bol(EditState *s);
void do_eol(EditState *s);
void do_word_left_right(EditState *s, int n);
void do_mark_region(EditState *s, int mark, int offset);
int eb_next_paragraph(EditBuffer *b, int offset);
int eb_prev_paragraph(EditBuffer *b, int offset);
void do_mark_paragraph(EditState *s, int n);
void do_forward_paragraph(EditState *s, int n);
void do_kill_paragraph(EditState *s, int n);
void do_fill_paragraph(EditState *s);
void do_changecase_word(EditState *s, int up);
void do_changecase_region(EditState *s, int up);
void do_delete_word(EditState *s, int dir);
int cursor_func(DisplayState *ds,
                int offset1, int offset2, int line_num,
                int x, int y, int w, int h, int hex_mode);
// should take argval
void do_scroll_left_right(EditState *s, int n);
void do_scroll_up_down(EditState *s, int dir);
void perform_scroll_up_down(EditState *s, int h);
void do_center_cursor(EditState *s, int force);
void do_quoted_insert(EditState *s, int argval);
void do_overwrite_mode(EditState *s, int argval);
// should take argval
void do_set_mark(EditState *s);
void do_mark_whole_buffer(EditState *s);
void do_yank(EditState *s);
void do_yank_pop(EditState *s);
void do_exchange_point_and_mark(EditState *s);
QECharset *read_charset(EditState *s, const char *charset_str,
                        EOLType *eol_typep);
void do_show_coding_system(EditState *s);
void do_set_auto_coding(EditState *s, int verbose);
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
void do_set_indent_tabs_mode(EditState *s, int val);
void display_window_borders(EditState *e);
int find_style_index(const char *name);
QEStyleDef *find_style(const char *name);
void style_complete(CompleteState *cp);
void get_style(EditState *e, QEStyleDef *stp, QETermStyle style);
void style_property_complete(CompleteState *cp);
int find_style_property(const char *name);
void do_define_color(EditState *e, const char *name, const char *value);
void do_set_style(EditState *e, const char *stylestr,
                  const char *propstr, const char *value);
void do_set_display_size(EditState *s, int w, int h);
void do_toggle_mode_line(EditState *s);
void do_set_system_font(EditState *s, const char *qe_font_name,
                        const char *system_fonts);
void do_set_window_style(EditState *s, const char *stylestr);
void call_func(CmdSig sig, CmdProto func, int nb_args, CmdArg *args,
               unsigned char *args_type);
int parse_arg(const char **pp, CmdArgSpec *ap);
void exec_command(EditState *s, const CmdDef *d, int argval, int key);
void do_execute_command(EditState *s, const char *cmd, int argval);
void window_display(EditState *s);
void do_prefix_argument(EditState *s, int key);
void do_start_kbd_macro(EditState *s);
void do_end_kbd_macro(EditState *s);
void do_call_last_kbd_macro(EditState *s);
void do_execute_macro_keys(EditState *s, const char *keys);
void do_define_kbd_macro(EditState *s, const char *name, const char *keys,
                         const char *key_bind);
void qe_save_macros(EditState *s, EditBuffer *b);

#define COMPLETION_TAB    0
#define COMPLETION_SPACE  1
#define COMPLETION_OTHER  2
void do_minibuffer_complete(EditState *s, int type);
void do_minibuffer_complete_space(EditState *s);
void do_minibuffer_scroll_up_down(EditState *s, int dir);
void do_minibuffer_history(EditState *s, int n);
void do_minibuffer_get_binary(EditState *s);
void do_minibuffer_exit(EditState *s, int fabort);

void do_popup_exit(EditState *s);
void do_toggle_read_only(EditState *s);
void do_not_modified(EditState *s, int argval);
void do_find_alternate_file(EditState *s, const char *filename, int bflags);
void do_find_file_noselect(EditState *s, const char *filename, int bflags);
void do_load_file_from_path(EditState *s, const char *filename, int bflags);
void do_set_visited_file_name(EditState *s, const char *filename,
                              const char *renamefile);
void qe_save_open_files(EditState *s, EditBuffer *b);

void do_delete_other_windows(EditState *s, int all);
void do_hide_window(EditState *s, int set);
void do_delete_hidden_windows(EditState *s);
void do_describe_key_briefly(EditState *s, const char *keystr, int argval);
void do_show_bindings(EditState *s, const char *cmd_name);
void do_apropos(EditState *s, const char *str);
EditBuffer *new_help_buffer(void);
void do_describe_bindings(EditState *s, int argval);
void do_help_for_help(EditState *s);
void qe_event_init(void);
void window_get_min_size(EditState *s, int *w_ptr, int *h_ptr);
void window_resize(EditState *s, int target_w, int target_h);
void wheel_scroll_up_down(EditState *s, int dir);
void qe_mouse_event(QEEvent *ev);
void set_user_option(const char *user);
void set_tty_charset(const char *name);

/* values */

typedef struct QEValue {
    unsigned int type : 16; // value type: TOK_VOID, TOK_NUMBER, TOK_CHAR, TOK_STRING
                            // should also have TOK_WINDOW, TOK_BUFFER, TOK_MODE...
    unsigned int alloc : 8; // u.str should be freed
    unsigned int flags : 8;
    // XXX: could have flags for owning allocated block pointed to by `str`
    // XXX: should have token precedence for operator tokens
    int len;                // string length
    union {
        long long value;    // number value
        char *str;          // string value
        // XXX: should have other object pointer types
        // XXX: could have floating point values with type double
        // XXX: should have short inline strings (7 bytes)
    } u;
} QEValue;

enum {
    TOK_VOID = 0, TOK_NUMBER = 128, TOK_STRING, TOK_CHAR, TOK_ID,
};

static inline void qe_cfg_set_void(QEValue *sp) {
    if (sp->alloc) {
        qe_free(&sp->u.str);
        sp->alloc = 0;
    }
    sp->type = TOK_VOID;
}

static inline void qe_cfg_set_num(QEValue *sp, long long value) {
    if (sp->alloc) {
        qe_free(&sp->u.str);
        sp->alloc = 0;
    }
    sp->u.value = value;
    sp->type = TOK_NUMBER;
}

static inline void qe_cfg_set_char(QEValue *sp, int c) {
    if (sp->alloc) {
        qe_free(&sp->u.str);
        sp->alloc = 0;
    }
    sp->u.value = c;
    sp->type = TOK_CHAR;
}

static inline void qe_cfg_set_str(QEValue *sp, const char *str, int len) {
    if (sp->alloc)
        qe_free(&sp->u.str);
    sp->u.str = qe_malloc_array(char, len + 1);
    memcpy(sp->u.str, str, len);
    sp->u.str[len] = '\0';
    sp->len = len;
    sp->type = TOK_STRING;      // TOK_ALLOC??
    sp->alloc = 1;
}

static inline void qe_cfg_set_pstr(QEValue *sp, char *str, int len) {
    if (sp->alloc) {
        qe_free(&sp->u.str);
        sp->alloc = 0;
    }
    sp->u.str = str;
    sp->len = len;
    sp->type = TOK_STRING;
}

static inline void qe_cfg_move(QEValue *sp, QEValue *sp1) {
    if (sp != sp1) {
        if (sp->alloc)
            qe_free(&sp->u.str);
        *sp = *sp1;
        sp1->alloc = 0;
        sp1->type = TOK_VOID;
    }
}

static inline void qe_cfg_swap(QEValue *sp, QEValue *sp1) {
    QEValue tmp = *sp;
    *sp = *sp1;
    *sp1 = tmp;
}

/* qescript.c */

int parse_config_file(EditState *s, const char *filename);
void do_eval_expression(EditState *s, const char *expression, int argval);
void do_eval_region(EditState *s); /* should pass actual offsets */
void do_eval_buffer(EditState *s);
extern int use_session_file;
int qe_load_session(EditState *s);
void do_save_session(EditState *s, int popup);

/* extras.c */

void do_compare_windows(EditState *s, int argval);
void do_compare_files(EditState *s, const char *filename, int bflags);
void do_delete_horizontal_space(EditState *s);
void do_show_date_and_time(EditState *s, int argval);

enum {
    CMD_TRANSPOSE_CHARS = 1,
    CMD_TRANSPOSE_WORDS,
    CMD_TRANSPOSE_LINES,
};
void do_transpose(EditState *s, int cmd);

/* hex.c */

void hex_write_char(EditState *s, int key);

/* erlang.c / elixir.c */

int erlang_match_char(unsigned int *str, int i);

/* lisp.c */

extern ModeDef lisp_mode;  /* used for org_mode */

/* list.c */

extern ModeDef list_mode;

void list_toggle_selection(EditState *s, int dir);
int list_get_pos(EditState *s);
int list_get_offset(EditState *s);

/* dired.c */

void do_dired(EditState *s, int argval);
void do_filelist(EditState *s, int argval);

/* syntax colorizers */

extern ModeDef c_mode;
extern ModeDef cpp_mode;
extern ModeDef js_mode;
extern ModeDef php_mode;
extern ModeDef csharp_mode;
extern ModeDef css_mode;
extern ModeDef xml_mode;  /* used in docbook_mode */
extern ModeDef htmlsrc_mode;

/* html.c */

extern ModeDef html_mode;  /* used in docbook_mode */
extern int use_html;
extern int is_player;

/* flags from libqhtml/css.h */
int gxml_mode_init(EditBuffer *b, int flags, const char *default_stylesheet);

/* image.c */

void fill_border(EditState *s, int x, int y, int w, int h, int color);
int qe_bitmap_format_to_pix_fmt(int format);

/* shell.c */

const char *get_shell(void);
void shell_colorize_line(QEColorizeContext *cp,
                         unsigned int *str, int n, ModeDef *syn);

#define SF_INTERACTIVE   0x01
#define SF_COLOR         0x02
#define SF_INFINITE      0x04
#define SF_AUTO_CODING   0x08
#define SF_AUTO_MODE     0x10
#define SF_BUFED_MODE    0x20
EditBuffer *new_shell_buffer(EditBuffer *b0, EditState *e,
                             const char *bufname, const char *caption,
                             const char *path,
                             const char *cmd, int shell_flags);

#define QASSERT(e)      do { if (!(e)) fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__, __LINE__, #e); } while (0)

#endif
