#ifndef QE_DISPLAY_H
#define QE_DISPLAY_H

#define MAX_SCREEN_WIDTH 256  /* in chars */
#define MAX_SCREEN_LINES 256  /* in text lines */

typedef unsigned int QEColor;
#define QEARGB(a,r,g,b) (((a) << 24) | ((r) << 16) | ((g) << 8) | (b))
#define QERGB(r,g,b) QEARGB(0xff, r, g, b)
#define COLOR_TRANSPARENT 0
#define QECOLOR_XOR       1

/* XXX: use different name prefix to avoid conflict */
#define QE_STYLE_NORM         0x0001
#define QE_STYLE_BOLD         0x0002
#define QE_STYLE_ITALIC       0x0004
#define QE_STYLE_UNDERLINE    0x0008
#define QE_STYLE_LINE_THROUGH 0x0010
#define QE_STYLE_MASK         0x00ff

#define NB_FONT_FAMILIES 3
#define QE_FAMILY_SHIFT       8
#define QE_FAMILY_MASK        0xff00
#define QE_FAMILY_FIXED       0x0100
#define QE_FAMILY_SERIF       0x0200
#define QE_FAMILY_SANS        0x0300 /* sans serif */

/* fallback font handling */
#define QE_FAMILY_FALLBACK_SHIFT  16
#define QE_FAMILY_FALLBACK_MASK   0xff0000

typedef struct QEFont {
    int ascent;
    int descent;
    void *private;
    int system_font; /* TRUE if system font */
    /* cache data */
    int style;
    int size;
    int timestamp;
} QEFont;

typedef struct QECharMetrics {
    int font_ascent;    /* maximum font->ascent */
    int font_descent;   /* maximum font->descent */
    int width;          /* sum of glyph widths */
} QECharMetrics;

typedef enum QEBitmapFormat {
    QEBITMAP_FORMAT_RGB565 = 0,
    QEBITMAP_FORMAT_RGB555,
    QEBITMAP_FORMAT_RGB24,
    QEBITMAP_FORMAT_RGBA32,
    QEBITMAP_FORMAT_YUV420P,
} QEBitmapFormat;

#define QEBITMAP_FLAG_VIDEO 0x0001 /* bitmap used to display video */

/* opaque bitmap structure */
typedef struct QEBitmap {
    int width;
    int height;
    QEBitmapFormat format;
    int flags;
    void *priv_data; /* driver data */
} QEBitmap;

/* draw options */
#define QEBITMAP_DRAW_HWZOOM 0x0001

/* user visible picture data (to modify content) */
typedef struct QEPicture {
    int width;
    int height;
    QEBitmapFormat format;
    unsigned char *data[4];
    int linesize[4];
} QEPicture;

struct QEditScreen;
typedef struct QEditScreen QEditScreen;

typedef struct QEDisplay {
    const char *name;
    int (*dpy_probe)(void);
    int (*dpy_init)(QEditScreen *s, int w, int h);
    void (*dpy_close)(QEditScreen *s);
    void (*dpy_cursor_at)(QEditScreen *s, int x1, int y1, int w, int h);
    void (*dpy_flush)(QEditScreen *s);
    int (*dpy_is_user_input_pending)(QEditScreen *s);
    void (*dpy_fill_rectangle)(QEditScreen *s,
                               int x, int y, int w, int h, QEColor color);
    QEFont *(*dpy_open_font)(QEditScreen *s, int style, int size);
    void (*dpy_close_font)(QEditScreen *s, QEFont *font);
    void (*dpy_text_metrics)(QEditScreen *s, QEFont *font, 
                             QECharMetrics *metrics,
                             const unsigned int *str, int len);
    void (*dpy_draw_text)(QEditScreen *s, QEFont *font,
                          int x, int y, const unsigned int *str, int len,
                          QEColor color);
    void (*dpy_set_clip)(QEditScreen *s,
                         int x, int y, int w, int h);
    void (*dpy_selection_activate)(QEditScreen *s);
    void (*dpy_selection_request)(QEditScreen *s);
    /* bitmap support */
    int (*dpy_bmp_alloc)(QEditScreen *s, QEBitmap *b);
    void (*dpy_bmp_free)(QEditScreen *s, QEBitmap *b);
    void (*dpy_bmp_draw)(QEditScreen *s, QEBitmap *b, 
                         int dst_x, int dst_y, int dst_w, int dst_h, 
                         int offset_x, int offset_y, int flags);
    void (*dpy_bmp_lock)(QEditScreen *s, QEBitmap *bitmap, QEPicture *pict,
                         int x1, int y1, int w1, int h1);
    void (*dpy_bmp_unlock)(QEditScreen *s, QEBitmap *b);
    /* full screen support */
    void (*dpy_full_screen)(QEditScreen *s, int full_screen);
    struct QEDisplay *next;
} QEDisplay;

struct QEditScreen {
    struct QEDisplay dpy;
    int width, height;
    QECharset *charset; /* the charset of the TTY, XXX: suppress that,
                          use a system in fonts instead */
    int media; /* media type (see CSS_MEDIA_xxx) */
    int bitmap_format; /* supported bitmap format */
    int video_format; /* supported video format */
    /* clip region handling */
    int clip_x1, clip_y1;
    int clip_x2, clip_y2;
    void *private;
};

static inline void draw_text(QEditScreen *s, QEFont *font,
                             int x, int y, const unsigned int *str, int len,
                             QEColor color)
{
    s->dpy.dpy_draw_text(s, font, x, y, str, len, color);
}

static inline QEFont *open_font(QEditScreen *s, 
                                int style, int size)
{
    return s->dpy.dpy_open_font(s, style, size);
}

static inline void close_font(QEditScreen *s, QEFont *font)
{
    s->dpy.dpy_close_font(s, font);
}

static inline void text_metrics(QEditScreen *s, QEFont *font, 
                                QECharMetrics *metrics,
                                const unsigned int *str, int len)
{
    s->dpy.dpy_text_metrics(s, font, metrics, str, len);
}

/* XXX: only needed for backward compatibility */
static inline int glyph_width(QEditScreen *s, QEFont *font, int ch)
{
    int buf[1];
    QECharMetrics metrics;
    buf[0] = ch;
    text_metrics(s, font, &metrics, buf, 1);
    return metrics.width;
}

static inline void dpy_flush(QEditScreen *s)
{
     s->dpy.dpy_flush(s);
}

static inline void dpy_close(QEditScreen *s)
{
     s->dpy.dpy_close(s);
}

void fill_rectangle(QEditScreen *s,
                    int x1, int y1, int w, int h, QEColor color);
void set_clip_rectangle(QEditScreen *s, CSSRect *r);
void push_clip_rectangle(QEditScreen *s, CSSRect *or, CSSRect *r);

int qe_register_display(QEDisplay *dpy);
QEDisplay *probe_display(void);
QEFont *select_font(QEditScreen *s, int style, int size);

void selection_activate(QEditScreen *s);
void selection_request(QEditScreen *s);

QEBitmap *bmp_alloc(QEditScreen *s, int width, int height, int flags);
void bmp_free(QEditScreen *s, QEBitmap *b);
void bmp_draw(QEditScreen *s, QEBitmap *b, 
              int dst_x, int dst_y, int dst_w, int dst_h, 
              int offset_x, int offset_y, int flags);
void bmp_lock(QEditScreen *s, QEBitmap *bitmap, QEPicture *pict,
              int x1, int y1, int w1, int h1);
void bmp_unlock(QEditScreen *s, QEBitmap *bitmap);

#endif
