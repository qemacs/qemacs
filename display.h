/*
 * Display system for QEmacs
 *
 * Copyright (c) 2000 Fabrice Bellard.
 * Copyright (c) 2002-2024 Charlie Gordon.
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

#ifndef QE_DISPLAY_H
#define QE_DISPLAY_H

#include "cutils.h"   /* qe__unused__ */
#include "color.h"

#define MAX_SCREEN_WIDTH  1024  /* in chars */
#define MAX_SCREEN_LINES   256  /* in text lines */

typedef enum QEBitmapFormat {
    QEBITMAP_FORMAT_1BIT = 0,
    QEBITMAP_FORMAT_4BIT,
    QEBITMAP_FORMAT_8BIT,
    QEBITMAP_FORMAT_RGB565,
    QEBITMAP_FORMAT_RGB555,
    QEBITMAP_FORMAT_RGB24,
    QEBITMAP_FORMAT_BGR24,
    QEBITMAP_FORMAT_RGBA32,
    QEBITMAP_FORMAT_BGRA32,
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
    QEColor *palette;
    int palette_size;
    int tcolor;
} QEPicture;

struct QEmacsState;
typedef struct QEditScreen QEditScreen;
typedef struct QEDisplay QEDisplay;
struct EditBuffer;
struct QECharset;

struct QEDisplay {
    const char *name;
    int xfactor, yfactor;
    int (*dpy_probe)(void);
    int (*dpy_init)(QEditScreen *s, struct QEmacsState *qs, int w, int h);
    void (*dpy_close)(QEditScreen *s);
    void (*dpy_flush)(QEditScreen *s);
    int (*dpy_is_user_input_pending)(QEditScreen *s);
    void (*dpy_fill_rectangle)(QEditScreen *s,
                               int x, int y, int w, int h, QEColor color);
    void (*dpy_xor_rectangle)(QEditScreen *s,
                              int x, int y, int w, int h, QEColor color);
    QEFont *(*dpy_open_font)(QEditScreen *s, int style, int size);
    void (*dpy_close_font)(QEditScreen *s, QEFont **fontp);
    void (*dpy_text_metrics)(QEditScreen *s, QEFont *font,
                             QECharMetrics *metrics,
                             const char32_t *str, int len);
    void (*dpy_draw_text)(QEditScreen *s, QEFont *font,
                          int x, int y, const char32_t *str, int len,
                          QEColor color);
    void (*dpy_set_clip)(QEditScreen *s,
                         int x, int y, int w, int h);

    /* These are optional, may be NULL */
    void (*dpy_selection_activate)(QEditScreen *s);
    void (*dpy_selection_request)(QEditScreen *s);
    void (*dpy_invalidate)(QEditScreen *s);
    void (*dpy_cursor_at)(QEditScreen *s, int x1, int y1, int w, int h);

    /* bitmap support */
    int (*dpy_bmp_alloc)(QEditScreen *s, QEBitmap *b);
    void (*dpy_bmp_free)(QEditScreen *s, QEBitmap *b);
    void (*dpy_bmp_draw)(QEditScreen *s, QEBitmap *b,
                         int dst_x, int dst_y, int dst_w, int dst_h,
                         int offset_x, int offset_y, int flags);
    void (*dpy_bmp_lock)(QEditScreen *s, QEBitmap *bitmap, QEPicture *pict,
                         int x1, int y1, int w1, int h1);
    void (*dpy_bmp_unlock)(QEditScreen *s, QEBitmap *b);
    int (*dpy_draw_picture)(QEditScreen *s,
                            int dst_x, int dst_y, int dst_w, int dst_h,
                            const QEPicture *ip,
                            int src_x, int src_y, int src_w, int src_h,
                            int flags);
    void (*dpy_full_screen)(QEditScreen *s, int full_screen);
    void (*dpy_describe)(QEditScreen *s, struct EditBuffer *b);
    void (*dpy_sound_bell)(QEditScreen *s);
    void (*dpy_suspend)(QEditScreen *s);
    void (*dpy_error)(QEditScreen *s, const char *fmt, ...) qe__attr_printf(2,3);
    QEDisplay *next;
};

struct QEditScreen {
    QEDisplay dpy;
    struct QEmacsState *qs;
    FILE *STDIN, *STDOUT;
    int width, height;
    const struct QECharset *charset; /* the charset of the TTY, XXX: suppress that,
                                        use a system in fonts instead */
    int unicode_version;
    int media; /* media type (see CSS_MEDIA_xxx) */
    QEBitmapFormat bitmap_format; /* supported bitmap format */
    QEBitmapFormat video_format; /* supported video format */
    /* clip region handling */
    int clip_x1, clip_y1;
    int clip_x2, clip_y2;
    void *priv_data;
};

int qe_register_display(struct QEmacsState *qs, QEDisplay *dpy);
QEDisplay *probe_display(void);

int qe_screen_init(struct QEmacsState *qs, QEditScreen *s, QEDisplay *dpy, int w, int h);

static inline void dpy_close(QEditScreen *s)
{
    s->dpy.dpy_close(s);
}

static inline void dpy_flush(QEditScreen *s)
{
    s->dpy.dpy_flush(s);
}

static inline QEFont *open_font(QEditScreen *s,
                                int style, int size)
{
    if (s->dpy.dpy_open_font)
        return s->dpy.dpy_open_font(s, style, size);
    return NULL;
}

static inline void close_font(QEditScreen *s, QEFont **fontp)
{
    if (*fontp && !(*fontp)->system_font && s->dpy.dpy_close_font)
        s->dpy.dpy_close_font(s, fontp);
}

static inline void text_metrics(QEditScreen *s, QEFont *font,
                                QECharMetrics *metrics,
                                const char32_t *str, int len)
{
    s->dpy.dpy_text_metrics(s, font, metrics, str, len);
}

static inline void draw_text(QEditScreen *s, QEFont *font,
                             int x, int y, const char32_t *str, int len,
                             QEColor color)
{
    s->dpy.dpy_draw_text(s, font, x, y, str, len, color);
}

static inline void selection_activate(QEditScreen *s)
{
    if (s->dpy.dpy_selection_activate)
        s->dpy.dpy_selection_activate(s);
}

static inline void selection_request(QEditScreen *s)
{
    if (s->dpy.dpy_selection_request)
        s->dpy.dpy_selection_request(s);
}

static inline void dpy_invalidate(QEditScreen *s)
{
    if (s->dpy.dpy_invalidate)
        s->dpy.dpy_invalidate(s);
}

QEBitmap *bmp_alloc(QEditScreen *s, int width, int height, int flags);
void bmp_free(QEditScreen *s, QEBitmap **bp);

static inline void bmp_draw(QEditScreen *s, QEBitmap *b,
                            int dst_x, int dst_y, int dst_w, int dst_h,
                            int offset_x, int offset_y, int flags)
{
    if (s->dpy.dpy_bmp_draw) {
        s->dpy.dpy_bmp_draw(s, b, dst_x, dst_y, dst_w, dst_h,
                            offset_x, offset_y, flags);
    }
}

/* used to access the bitmap data. Return the necessary pointers to
   modify the image in 'pict'. */
static inline void bmp_lock(QEditScreen *s, QEBitmap *bitmap, QEPicture *pict,
                            int x1, int y1, int w1, int h1)
{
    if (s->dpy.dpy_bmp_lock)
        s->dpy.dpy_bmp_lock(s, bitmap, pict, x1, y1, w1, h1);
}

static inline void bmp_unlock(QEditScreen *s, QEBitmap *bitmap)
{
    if (s->dpy.dpy_bmp_unlock)
        s->dpy.dpy_bmp_unlock(s, bitmap);
}

static inline void dpy_describe(QEditScreen *s, struct EditBuffer *b)
{
    if (s->dpy.dpy_describe)
        s->dpy.dpy_describe(s, b);
}

static inline void dpy_sound_bell(QEditScreen *s)
{
    if (s->dpy.dpy_sound_bell)
        s->dpy.dpy_sound_bell(s);
}

#define dpy_error(ds, ...)   do { if ((ds)->dpy.dpy_error) ((ds)->dpy.dpy_error)(ds, __VA_ARGS__); } while (0)

/* XXX: only needed for backward compatibility */
static inline int glyph_width(QEditScreen *s, QEFont *font, char32_t ch) {
    char32_t buf[1];
    QECharMetrics metrics;
    buf[0] = ch;
    text_metrics(s, font, &metrics, buf, 1);
    return metrics.width;
}

void fill_rectangle(QEditScreen *s,
                    int x1, int y1, int w, int h, QEColor color);
void xor_rectangle(QEditScreen *s,
                   int x1, int y1, int w, int h, QEColor color);
void set_clip_rectangle(QEditScreen *s, CSSRect *r);
void push_clip_rectangle(QEditScreen *s, CSSRect *r0, CSSRect *r);

void free_font_cache(QEditScreen *s);
QEFont *select_font(QEditScreen *s, int style, int size);
static inline QEFont *lock_font(qe__unused__ QEditScreen *s, QEFont *font) {
    if (font && font->refcount)
        font->refcount++;
    return font;
}
static inline void release_font(qe__unused__ QEditScreen *s, QEFont *font) {
    if (font && font->refcount)
        font->refcount--;
}

QEPicture *qe_create_picture(int width, int height,
                             QEBitmapFormat format, int flags);
static inline int qe_picture_lock(QEPicture *ip) { return ip == NULL; }
static inline void qe_picture_unlock(QEPicture *ip) {}
void qe_free_picture(QEPicture **ipp);

#define QE_PAL_MODE(r, g, b, incr)  (((r) << 12) | ((g) << 8) | ((b) << 4) | (incr))
#define QE_PAL_RGB3     QE_PAL_MODE(0, 1, 2, 3)
#define QE_PAL_RGB4     QE_PAL_MODE(0, 1, 2, 4)
#define QE_PAL_BGR3     QE_PAL_MODE(2, 1, 0, 3)
#define QE_PAL_BGR4     QE_PAL_MODE(2, 1, 0, 4)
#define QE_PAL_QECOLOR  QE_PAL_MODE(2, 1, 0, 4)   /* XXX: depends on endianness */
int qe_picture_set_palette(QEPicture *ip, int mode,
                           unsigned char *p, int count, int tcolor);

int qe_picture_copy(QEPicture *dst, int dst_x, int dst_y, int dst_w, int dst_h,
                    const QEPicture *src, int src_x, int src_y, int src_w, int src_h,
                    int flags);

int qe_draw_picture(QEditScreen *s, int dst_x, int dst_y, int dst_w, int dst_h,
                    const QEPicture *ip,
                    int src_x, int src_y, int src_w, int src_h,
                    int flags, QEColor col);
#endif
