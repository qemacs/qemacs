#ifndef QE_H
#define QE_H

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>
#include <inttypes.h>

#ifdef HAVE_QE_CONFIG_H
#include "config.h"
#endif

/* OS specific defines */

#ifdef WIN32
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#endif

/************************/

#include "cutils.h"

/************************/

/* low level I/O events */
void set_read_handler(int fd, void (*cb)(void *opaque), void *opaque);
void set_write_handler(int fd, void (*cb)(void *opaque), void *opaque);
int set_pid_handler(int pid, 
                    void (*cb)(void *opaque, int status), void *opaque);
void url_exit(void);
void register_bottom_half(void (*cb)(void *opaque), void *opaque);
void unregister_bottom_half(void (*cb)(void *opaque), void *opaque);

struct QETimer;
typedef struct QETimer QETimer;
QETimer *qe_add_timer(int delay, void *opaque, void (*cb)(void *opaque));
void qe_kill_timer(QETimer *ti);

/* main loop for Unix programs using liburlio */
void url_main_loop(void (*init)(void *opaque), void *opaque);

typedef unsigned char u8;
struct EditState;

#define MAXINT 0x7fffffff
#define MAX_FILENAME_SIZE 1024
#define NO_ARG MAXINT

/* util.c */

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

struct FindFileState;
typedef struct FindFileState FindFileState;

FindFileState *find_file_open(const char *path, const char *pattern);
int find_file_next(FindFileState *s, char *filename, int filename_size_max);
void find_file_close(FindFileState *s);
void canonize_path(char *buf, int buf_size, const char *path);
void canonize_absolute_path(char *buf, int buf_size, const char *path1);
const char *basename(const char *filename);
const char *pathname(char *buf, int buf_size, const char *filename);

int find_resource_file(char *path, int path_size, const char *pattern);
char *pstrncpy(char *buf, int buf_size, const char *s, int len);
void umemmove(unsigned int *dest, unsigned int *src, int len);
void skip_spaces(const char **pp);
int ustristart(const unsigned int *str, const char *val, const unsigned int **ptr);
void css_strtolower(char *buf, int buf_size);

void get_str(const char **pp, char *buf, int buf_size, const char *stop);
int strtokey(const char **pp);
void keytostr(char *buf, int buf_size, int key);
int css_get_color(int *color_ptr, const char *p);
int css_get_font_family(const char *str);
void css_union_rect(CSSRect *a, CSSRect *b);
static inline int css_is_null_rect(CSSRect *a)
{
    return (a->x2 <= a->x1 ||
            a->y2 <= a->y1);
}
static inline void css_set_rect(CSSRect *a, int x1, int y1, int x2, int y2)
{
    a->x1 = x1;
    a->y1 = y1;
    a->x2 = x2;
    a->y2 = y2;
}
/* return true if a and b intersect */
static inline int css_is_inter_rect(CSSRect *a, CSSRect *b)
{
    return (!(a->x2 <= b->x1 ||
              a->x1 >= b->x2 ||
              a->y2 <= b->y1 ||
              a->y1 >= b->y2));
}


static inline int css_is_space(int ch)
{
    return (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t');
}

int css_get_enum(const char *str, const char *enum_str);

int get_clock_ms(void);

typedef int (CSSAbortFunc)(void *);

static inline int max(int a, int b)
{
    if (a > b)
        return a;
    else
        return b;
}

static inline int min(int a, int b)
{
    if (a < b)
        return a;
    else
        return b;
}

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

StringItem *set_string(StringArray *cs, int index, const char *str);
StringItem *add_string(StringArray *cs, const char *str);
void free_strings(StringArray *cs);

/* command line option */
#define CMD_OPT_ARG      0x0001 /* argument */
#define CMD_OPT_STRING   0x0002 /* string */
#define CMD_OPT_BOOL     0x0004 /* boolean */
#define CMD_OPT_INT      0x0008 /* int */

typedef struct CmdOptionDef {
    const char *name;
    const char *argname;
    int flags;
    const char *help;
    union {
        const char **string_ptr;
        int *int_ptr;
        void (*func_noarg)();
        void (*func_arg)(const char *);
        struct CmdOptionDef *next;
    } u;
} CmdOptionDef;

void qe_register_cmd_line_options(CmdOptionDef *table);

/* charset.c */

/* maximum number of bytes for a character in all the supported charsets */
#define MAX_CHAR_BYTES 6

struct CharsetDecodeState;

typedef struct QECharset {
    const char *name;
    const char **aliases;
    void (*decode_init)(struct CharsetDecodeState *);
    int (*decode_func)(struct CharsetDecodeState *,
                       const unsigned char **);
    /* return NULL if cannot encode. Currently no state since speed is
       not critical yet */
    unsigned char *(*encode_func)(struct QECharset *, unsigned char *, int); 
    u8 table_alloc; /* true if CharsetDecodeState.table must be malloced */
    /* private data for some charsets */
    u8 min_char, max_char;
    const unsigned short *private_table;
    struct QECharset *next;
} QECharset;

extern QECharset charset_utf8, charset_8859_1; /* predefined charsets */
extern QECharset *first_charset;

typedef struct CharsetDecodeState {
    /* 256 ushort table for hyper fast decoding */
    unsigned short *table; 
    /* slower decode function for complicated cases */
    int (*decode_func)(struct CharsetDecodeState *,
                       const unsigned char **); 
    QECharset *charset;
} CharsetDecodeState;

#define INVALID_CHAR 0xfffd
#define ESCAPE_CHAR  0xffff

void charset_init(void);

void qe_register_charset(QECharset *charset);

char *utf8_encode(char *q, int c);
int utf8_decode(const char **pp);
extern unsigned char utf8_length[256];

int utf8_to_unicode(unsigned int *dest, int dest_length, 
                    const char *str);

QECharset *find_charset(const char *str);
void charset_decode_init(CharsetDecodeState *s, QECharset *charset);
void charset_decode_close(CharsetDecodeState *s);

static inline int charset_decode(CharsetDecodeState *s, const char **pp)
{
    const unsigned char *p;
    int c;
    p = *pp;
    c = *p;
    c = s->table[c];
    if (c == ESCAPE_CHAR) {
        c = s->decode_func(s, (const unsigned char **)pp);
    } else {
        p++;
        *pp = p;
    }
    return c;
}

QECharset *detect_charset (const unsigned char *buf, int size);

void decode_8bit_init(CharsetDecodeState *s);
unsigned char *encode_8bit(QECharset *charset, unsigned char *q, int c);

int unicode_to_charset(char *buf, unsigned int c, QECharset *charset);

/* arabic.c */
int arab_join(unsigned int *line, unsigned int *ctog, int len);

/* indic.c */
int devanagari_log2vis(unsigned int *str, unsigned int *ctog, int len);

/* unicode_join.c */
int unicode_to_glyphs(unsigned int *dst, unsigned int *char_to_glyph_pos,
                      int dst_size, unsigned int *src, int src_size, 
                      int reverse);
void load_ligatures(void);

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

#define KEY_META(c) ((c) | 0xe000)
#define KEY_ESC1(c) ((c) | 0xe100)
#define KEY_CTRLX(c) ((c) | 0xe200)
#define KEY_CTRLXRET(c) ((c) | 0xe300)
#define KEY_CTRLH(c) ((c) | 0xe500)
#define KEY_CTRL(c) ((c) & 0x1f)
#define KEY_UP    KEY_ESC1('A')
#define KEY_DOWN  KEY_ESC1('B')
#define KEY_RIGHT KEY_ESC1('C')
#define KEY_LEFT  KEY_ESC1('D')
#define KEY_CTRL_UP    KEY_ESC1('a')
#define KEY_CTRL_DOWN  KEY_ESC1('b')
#define KEY_CTRL_RIGHT KEY_ESC1('c')
#define KEY_CTRL_LEFT  KEY_ESC1('d')
#define KEY_CTRL_HOME  0xe402
#define KEY_CTRL_END   0xe403
#define KEY_TAB        KEY_CTRL('i')
#define KEY_SHIFT_TAB  0xe404
#define KEY_F1         KEY_ESC1(11)
#define KEY_F2         KEY_ESC1(12)
#define KEY_F3         KEY_ESC1(13)
#define KEY_F4         KEY_ESC1(14)
#define KEY_F5         KEY_ESC1(15)
#define KEY_F6         KEY_ESC1(16)
#define KEY_F7         KEY_ESC1(17)
#define KEY_F8         KEY_ESC1(18)
#define KEY_F9         KEY_ESC1(19)
#define KEY_F10        KEY_ESC1(20)
#define KEY_F11        KEY_ESC1(21)
#define KEY_F12        KEY_ESC1(22)
#define KEY_BACKSPACE 127
#define KEY_INSERT     KEY_ESC1(2)
#define KEY_DELETE     KEY_ESC1(3)
#define KEY_PAGEUP     KEY_ESC1(5)
#define KEY_PAGEDOWN   KEY_ESC1(6)
#define KEY_HOME       KEY_ESC1(7)
#define KEY_END        KEY_ESC1(8)
#define KEY_REFRESH    KEY_CTRL('l')
#define KEY_RET        0x000d
#define KEY_ESC        0x001b
#define KEY_SPC        0x0020
#define KEY_NONE       0xffff
#define KEY_DEFAULT    0xe401 /* to handle all non special keys */
#define KEY_SPECIAL(c) (((c) >= 0xe000 && (c) < 0xf000) || ((c) >= 0 && (c) < 32))

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
void qe_grab_keys(void (*cb)(void *opaque, int key), void *opaque);
void qe_ungrab_keys(void);

/* buffer.c */

/* begin to mmap files from this size */
#define MIN_MMAP_SIZE (1024*1024)

#define MAX_PAGE_SIZE 4096
//#define MAX_PAGE_SIZE 16

#define NB_LOGS_MAX 50

#define PG_READ_ONLY    0x0001 /* the page is read only */
#define PG_VALID_POS    0x0002 /* set if the nb_lines / col fields are up to date */
#define PG_VALID_CHAR   0x0004 /* nb_chars is valid */
#define PG_VALID_COLORS 0x0008 /* color state is valid */

typedef struct Page {
    int size; /* data size */ 
    u8 *data;
    int flags;
    /* the following are needed to handle line / column computation */
    int nb_lines; /* Number of '\n' in data */
    int col;      /* Number of chars since the last '\n' */
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
    LOGOP_DELETE
};

struct EditBuffer;

/* each buffer modification can be catched with this callback */
typedef void (*EditBufferCallback)(struct EditBuffer *,
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

typedef struct EditBuffer {
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
    int save_log;    /* if true, each buffer operation is loged */
    int log_new_index, log_current;
    struct EditBuffer *log_buffer;
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
    void (*close)(struct EditBuffer *);

    /* saved data from the last opened mode, needed to restore mode */
    struct ModeSavedData *saved_data; 

    struct EditBuffer *next; /* next editbuffer in qe_state buffer list */
    char name[256];     /* buffer name */
    char filename[MAX_FILENAME_SIZE]; /* file name */
} EditBuffer;

struct ModeProbeData;

/* high level buffer type handling */
typedef struct EditBufferDataType {
    const char *name; /* name of buffer data type (text, image, ...) */
    int (*buffer_load)(EditBuffer *b, FILE *f);
    int (*buffer_save)(EditBuffer *b, const char *filename);
    void (*buffer_close)(EditBuffer *b);
    struct EditBufferDataType *next;
} EditBufferDataType;

/* the log buffer is used for the undo operation */
/* header of log operation */
typedef struct LogBuffer {
    u8 op;
    u8 was_modified;
    int offset;
    int size;
} LogBuffer;

void eb_init(void);
int eb_read(EditBuffer *b, int offset, u8 *buf, int size);
void eb_write(EditBuffer *b, int offset, u8 *buf, int size);
void eb_insert_buffer(EditBuffer *dest, int dest_offset, 
                      EditBuffer *src, int src_offset, 
                      int size);
void eb_insert(EditBuffer *b, int offset, u8 *buf, int size);
void eb_delete(EditBuffer *b, int offset, int size);
void log_reset(EditBuffer *b);
EditBuffer *eb_new(const char *name, int flags);
void eb_free(EditBuffer *b);
EditBuffer *eb_find(const char *name);
EditBuffer *eb_find_file(const char *filename);

void eb_set_charset(EditBuffer *b, QECharset *charset);
int eb_nextc(EditBuffer *b, int offset, int *next_ptr);
int eb_prevc(EditBuffer *b, int offset, int *prev_ptr);
int eb_goto_pos(EditBuffer *b, int line1, int col1);
int eb_get_pos(EditBuffer *b, int *line_ptr, int *col_ptr, int offset);
int eb_goto_char(EditBuffer *b, int pos);
int eb_get_char_offset(EditBuffer *b, int offset);
void do_undo(struct EditState *s);

int raw_load_buffer1(EditBuffer *b, FILE *f, int offset);
int save_buffer(EditBuffer *b);
void set_buffer_name(EditBuffer *b, const char *name1);
void set_filename(EditBuffer *b, const char *filename);
int eb_add_callback(EditBuffer *b, EditBufferCallback cb,
                    void *opaque);
void eb_free_callback(EditBuffer *b, EditBufferCallback cb,
                      void *opaque);
void eb_offset_callback(EditBuffer *b,
                        void *opaque,
                        enum LogOperation op,
                        int offset,
                        int size);
void eb_printf(EditBuffer *b, const char *fmt, ...);
void eb_line_pad(EditBuffer *b, int n);
int eb_get_str(EditBuffer *b, char *buf, int buf_size);
int eb_get_line(EditBuffer *b, unsigned int *buf, int buf_size,
                int *offset_ptr);
int eb_get_strline(EditBuffer *b, char *buf, int buf_size,
                   int *offset_ptr);
int eb_goto_bol(EditBuffer *b, int offset);
int eb_is_empty_line(EditBuffer *b, int offset);
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

#if defined(__GNUC__) || defined(__TINYC__)

/* make sure that the keyword is not disabled by glibc (TINYC case) */
#undef __attribute__ 

/* same method as the linux kernel... */
#define __init_call	__attribute__ ((unused,__section__ (".initcall.init")))
#define __exit_call	__attribute__ ((unused,__section__ (".exitcall.exit")))

#define qe_module_init(fn) \
	static int (*__initcall_##fn)(void) __init_call = fn

#define qe_module_exit(fn) \
	static void (*__exitcall_##fn)(void) __exit_call = fn
#else

#define __init_call
#define __exit_call

#define qe_module_init(fn) \
	int module_ ## fn (void) { fn(); }

#define qe_module_exit(fn)

#endif

#endif /* QE_MODULE */

/* display.c */

#include "display.h"

/* qe.c */

/* colorize & transform a line, lower level then ColorizeFunc */
typedef int (*GetColorizedLineFunc)(struct EditState *s, 
                                    unsigned int *buf, int buf_size,
                                    int offset1, int line_num);

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

typedef struct EditState {
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
    int insert;
    int bidir;
    int cur_rtl;     /* TRUE if the cursor on over RTL chars */
    enum WrapType wrap;
    int line_numbers;
    int tab_size;
    int indent_size;
    int indent_tabs_mode; /* if true, use tabs to indent */
    int interactive; /* true if interaction is done instead of editing
                        (e.g. for shell mode or HTML) */
    int force_highlight;  /* if true, force showing of cursor even if
                             window not focused (list mode only) */
    int mouse_force_highlight; /* if true, mouse can force highlight
                                  (list mode only) */
    /* low level colorization function */
    GetColorizedLineFunc get_colorized_line_func;
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
    /* maximum valid offset, MAXINT if not modified. Needed to invalide
       'colorize_states' */
    int colorize_max_valid_offset; 

    int busy; /* true if editing cannot be done if the window
                 (e.g. the parser HTML is parsing the buffer to
                 produce the display */
    int display_invalid; /* true if the display was invalidate. Full
                            redraw should be done */
    int show_selection;  /* if true, the selection is displayed */
    /* display area info */
    int width, height;
    int ytop, xleft;
    /* full window size, including borders */
    int x1, y1, x2, y2;
    int flags; /* display flags */
#define WF_POPUP      0x0001 /* popup window (with borders) */
#define WF_MODELINE   0x0002 /* mode line must be displayed */
#define WF_RSEPARATOR 0x0004 /* right window separator */

    char *prompt;
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
    struct EditState *next_window;
} EditState;

#define SAVED_DATA_SIZE ((int)&((EditState *)0)->end_of_saved_data)

int to_hex(int key);

struct DisplayState;

typedef struct ModeProbeData {
    unsigned char *filename;
    unsigned char *buf;
    int buf_size;
    int mode;     /* unix mode */
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
    int instance_size; /* size of malloced instance */
    int (*mode_probe)(ModeProbeData *); /* return the percentage of confidence */
    int (*mode_init)(EditState *, ModeSavedData *);
    void (*mode_close)(EditState *);
    /* save the internal state of the mode so that it can be opened
       again in the same state */
    ModeSavedData *(*mode_save_data)(EditState *s);

    /* low level display functions (must be NULL to use text relatedx
       functions)*/
    void (*display)(EditState *);

    /* text related functions */
    int (*text_display)(EditState *, struct DisplayState *, int);
    int (*text_backward_offset)(EditState *, int);

    /* common functions are defined here */
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
    void (*mode_line)(EditState *s, char *buf, int buf_size); /* return mode line */
    struct ModeDef *next;
} ModeDef;

/* special bit to indicate tty styles (for shell mode) */
#define QE_STYLE_TTY     0x800
#define TTY_GET_COLOR(fg, bg) (((fg) << 3) | (bg))
#define TTY_GET_FG(color) (((color) >> 3) & 7)
#define TTY_GET_BG(color) ((color) & 7)

extern unsigned int tty_colors[]; /* from tty.c */

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

extern QEStyleDef qe_styles[QE_STYLE_NB];
    
#define NB_YANK_BUFFERS 10

typedef struct QEmacsState {
    QEditScreen *screen;
    struct EditState *first_window;
    struct EditState *active_window; /* window in which we edit */
    struct EditBuffer *first_buffer;
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
    int is_full_screen;
    /* commands */
    void *last_cmd_func; /* last executed command function call */
    /* keyboard macros */
    int defining_macro;
    unsigned short *macro_keys;
    int nb_macro_keys;
    int macro_keys_size;
    int macro_key_index; /* -1 means no macro is being executed */
    int ungot_key;
    /* yank buffers */
    EditBuffer *yank_buffers[NB_YANK_BUFFERS];
    int yank_current;
    char res_path[1024];
    char status_shadow[MAX_SCREEN_WIDTH];
    char system_fonts[NB_FONT_FAMILIES][256];
} QEmacsState;

extern QEmacsState qe_state;

/* key bindings definitions */

/* dynamic key binding storage */

typedef struct KeyDef {
    int nb_keys;
    struct CmdDef *cmd;
    ModeDef *mode; /* if non NULL, key is only active in this mode */
    struct KeyDef *next;
    unsigned int keys[1];
} KeyDef;

void unget_key(int key);

/* command definitions */

typedef struct CmdDef {
    unsigned short key;       /* normal key */
    unsigned short alt_key;   /* alternate key */
    const char *name;
    union {
        void *func;
        struct CmdDef *next;
    } action;
    void *val;
} CmdDef;

/* new command macros */
#define CMD(key, key_alt, name, func) { key, key_alt, name, { (void *)func } },
#define CMDV(key, key_alt, name, func, val) { key, key_alt, name, { (void *)func }, val },

/* old macros for compatibility */
#define CMD0(key, key_alt, name, func) { key, key_alt, name "\0", { (void *)func } },
#define CMD1(key, key_alt, name, func, val) { key, key_alt, name "\0v", { (void *)func }, (void*)(val) },
#define CMDi(key, key_alt, name, func) { key, key_alt, name "\0i", { (void *)func } },
#define CMDss(key, key_alt, name, func) { key, key_alt, name "\0ss", { (void *)func } },
#define CMD_DEF_END { 0, 0, NULL, }

void qe_register_mode(ModeDef *m);
void qe_register_cmd_table(CmdDef *cmds, const char *mode);
void qe_register_binding(int key, const char *cmd_name, 
                         const char *mode_names);

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

typedef struct DisplayState {
    int do_disp; /* true if real display */
    int width;   /* display window width */
    int height;  /* display window height */
    int eol_width; /* width of eol char */
    int default_line_height;  /* line height if no chars */
    int tab_width;            /* width of tabulation */
    int x_disp;  /* starting x display */
    int x; /* current x position */
    int y; /* current y position */
    int line_num; /* current text line number */
    int cur_hex_mode; /* true if current char is in hex mode */
    int hex_mode; /* hex mode from edit_state, -1 if all chars wanted */
    void *cursor_opaque;
    int (*cursor_func)(struct DisplayState *, 
                       int offset1, int offset2, int line_num,
                       int x, int y, int w, int h, int hex_mode);
    int eod; /* end of display requested */
    /* if base == RTL, then all x are equivalent to width - x */
    DirType base; 
    int embedding_level_max;
    int wrap;
    int eol_reached;
    EditState *edit_state;
    
    /* fragment buffers */
    TextFragment fragments[MAX_SCREEN_WIDTH];
    int nb_fragments;
    int last_word_space; /* true if last word was a space */
    int word_index;      /* fragment index of the start of the current
                            word */
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
} DisplayState;

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
                    const char *fmt, ...);
void display_printhex(DisplayState *s, int offset1, int offset2, 
                      unsigned int h, int n);

static inline int display_char(DisplayState *s, int offset1, int offset2,
                               int ch)
{
    return display_char_bidir(s, offset1, offset2, 0, ch);
}

/* input.c */

#define INPUTMETHOD_NOMATCH   (-1)
#define INPUTMETHOD_MORECHARS (-2)

typedef struct InputMethod {
    const char *name;
    /* input match returns: 
       ch >= 0 if a character ch of len '*match_len_ptr' in buf was found, 
       INPUTMETHOD_NOMATCH if no match was found 
       INPUTMETHOD_MORECHARS if more chars need to be typed to find
       a suitable completion 
     */
    int (*input_match)(int *match_len_ptr, 
                       const u8 *data, const unsigned int *buf, int len);
    const u8 *data;
    struct InputMethod *next;
} InputMethod;

extern InputMethod *input_methods;

void do_set_input_method(EditState *s, const char *input_str);
void do_switch_input_method(EditState *s);
void init_input_methods(void);
void close_input_methods(void);

/* the following will be suppressed */
#define LINE_MAX_SIZE 256

static inline int align(int a, int n)
{
    return (a/n)*n;
}

/* minibuffer & status */

extern ModeDef minibuffer_mode;

typedef void (*CompletionFunc)(StringArray *cs, const char *input);

typedef struct CompletionEntry {
    const char *name;
    CompletionFunc completion_func;
    struct CompletionEntry *next;
} CompletionEntry;

void register_completion(const char *name, CompletionFunc completion_func);
void put_status(EditState *s, const char *fmt, ...);
void minibuffer_edit(const char *input, const char *prompt, 
                     StringArray *hist, CompletionFunc completion_func,
                     void (*cb)(void *opaque, char *buf), void *opaque);
void command_completion(StringArray *cs, const char *input);
void file_completion(StringArray *cs, const char *input);
void buffer_completion(StringArray *cs, const char *input);

#ifdef WIN32
static inline int is_user_input_pending(void)
{
    return 0;
}
#else
extern int __fast_test_event_poll_flag;
int __is_user_input_pending(void);

static inline int is_user_input_pending(void)
{
    if (__fast_test_event_poll_flag) {
        __fast_test_event_poll_flag = 0;
        return __is_user_input_pending();
    } else {
        return 0;
    }
}
#endif

/* popup / low level window handling */
void show_popup(EditBuffer *b);
EditState *insert_window_left(EditBuffer *b, int width, int flags);
EditState *find_window_right(EditState *s);

/* window handling */
void edit_close(EditState *s);
EditState *edit_new(EditBuffer *b,
                    int x1, int y1, int width, int height, int flags);
void do_refresh(EditState *s);
void do_delete_window(EditState *s, int force);
void edit_display(QEmacsState *qs);
void edit_invalidate(EditState *s);

/* text mode */
extern ModeDef text_mode;
int text_mode_init(EditState *s, ModeSavedData *saved_data);
void text_mode_close(EditState *s);
int text_backward_offset(EditState *s, int offset);
int text_display(EditState *s, DisplayState *ds, int offset);

void set_colorize_func(EditState *s, ColorizeFunc colorize_func);
int get_colorized_line(EditState *s, unsigned int *buf, int buf_size,
                       int offset1, int line_num);
void set_color(unsigned int *buf, int len, int style);

void do_char(EditState *s, int key);
void do_switch_to_buffer(EditState *s, const char *bufname);;
void do_set_mode(EditState *s, ModeDef *m, ModeSavedData *saved_data);
void text_move_left_right_visual(EditState *s, int dir);
void text_move_up_down(EditState *s, int dir);
void text_scroll_up_down(EditState *s, int dir);
void text_write_char(EditState *s, int key);
void do_return(EditState *s);
void do_backspace(EditState *s);
void do_delete_char(EditState *s);
void do_tab(EditState *s);
void do_kill_region(EditState *s, int kill);
void do_kill_buffer(EditState *s, const char *bufname1);
void text_move_bol(EditState *s);
void text_move_eol(EditState *s);
void do_load(EditState *s, const char *filename);
void do_goto_line(EditState *s, int line);
void switch_to_buffer(EditState *s, EditBuffer *b);
void do_up_down(EditState *s, int dir);
void display_mode_line(EditState *s);
void text_mouse_goto(EditState *s, int x, int y);
EditBuffer *new_yank_buffer(void);
void basic_mode_line(EditState *s, char *buf, int buf_size, int c1);
void text_mode_line(EditState *s, char *buf, int buf_size);
void do_toggle_full_screen(EditState *s);

/* hex.c */
void hex_write_char(EditState *s, int key);

/* list.c */

extern ModeDef list_mode;

void list_toggle_selection(EditState *s);
int list_get_pos(EditState *s);
int list_get_offset(EditState *s);

void get_style(EditState *e, QEStyleDef *style, int style_index);

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

#endif
