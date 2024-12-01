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

#include "qe.h"

static QEDisplay *first_dpy;

/* dummy display driver for initialization time */

static int dummy_dpy_init(QEditScreen *s, QEmacsState *qs, qe__unused__ int w, qe__unused__ int h)
{
    s->qs = qs;
    s->charset = &charset_8859_1;

    return 0;
}

static void dummy_dpy_close(qe__unused__ QEditScreen *s)
{
}

static void dummy_dpy_flush(qe__unused__ QEditScreen *s)
{
}

static int dummy_dpy_is_user_input_pending(qe__unused__ QEditScreen *s)
{
    return 0;
}

static void dummy_dpy_fill_rectangle(qe__unused__ QEditScreen *s,
                                     qe__unused__ int x1, qe__unused__ int y1,
                                     qe__unused__ int w, qe__unused__ int h,
                                     qe__unused__ QEColor color)
{
}

static void dummy_dpy_xor_rectangle(qe__unused__ QEditScreen *s,
                                    qe__unused__ int x1, qe__unused__ int y1,
                                    qe__unused__ int w, qe__unused__ int h,
                                    qe__unused__ QEColor color)
{
}

static QEFont *dummy_dpy_open_font(qe__unused__ QEditScreen *s,
                                   qe__unused__ int style, qe__unused__ int size)
{
    return NULL;
}

static void dummy_dpy_close_font(qe__unused__ QEditScreen *s,
                                 qe__unused__ QEFont **fontp)
{
}

static void dummy_dpy_text_metrics(qe__unused__ QEditScreen *s,
                                   qe__unused__ QEFont *font,
                                   QECharMetrics *metrics,
                                   qe__unused__ const char32_t *str,
                                   int len)
{
    metrics->font_ascent = 1;
    metrics->font_descent = 0;
    metrics->width = len;
}

static void dummy_dpy_draw_text(qe__unused__ QEditScreen *s,
                                qe__unused__ QEFont *font,
                                qe__unused__ int x, qe__unused__ int y,
                                qe__unused__ const char32_t *str,
                                qe__unused__ int len,
                                qe__unused__ QEColor color)
{
}

static void dummy_dpy_set_clip(qe__unused__ QEditScreen *s,
                               qe__unused__ int x, qe__unused__ int y,
                               qe__unused__ int w, qe__unused__ int h)
{
}

static QEDisplay const dummy_dpy = {
    "dummy", 1, 1,
    NULL, /* dpy_probe */
    dummy_dpy_init,
    dummy_dpy_close,
    dummy_dpy_flush,
    dummy_dpy_is_user_input_pending,
    dummy_dpy_fill_rectangle,
    dummy_dpy_xor_rectangle,
    dummy_dpy_open_font,
    dummy_dpy_close_font,
    dummy_dpy_text_metrics,
    dummy_dpy_draw_text,
    dummy_dpy_set_clip,

    NULL, /* dpy_selection_activate */
    NULL, /* dpy_selection_request */
    NULL, /* dpy_invalidate */
    NULL, /* dpy_cursor_at */
    NULL, /* dpy_bmp_alloc */
    NULL, /* dpy_bmp_free */
    NULL, /* dpy_bmp_draw */
    NULL, /* dpy_bmp_lock */
    NULL, /* dpy_bmp_unlock */
    NULL, /* dpy_draw_picture */
    NULL, /* dpy_full_screen */
    NULL, /* dpy_describe */
    NULL, /* dpy_sound_bell */
    NULL, /* dpy_suspend */
    NULL, /* dpy_error */
    NULL, /* next */
};

/*----------------*/

void fill_rectangle(QEditScreen *s,
                    int x1, int y1, int w, int h, QEColor color)
{
    int x2 = x1 + w;
    int y2 = y1 + h;

    /* quick clip rejection */
    if (x2 <= s->clip_x1 || y2 <= s->clip_y1 ||
        x1 >= s->clip_x2 || y1 >= s->clip_y2)
        return;

    /* region update */
    if (x2 > s->clip_x2)
        x2 = s->clip_x2;
    if (y2 > s->clip_y2)
        y2 = s->clip_y2;
    if (x1 < s->clip_x1)
        x1 = s->clip_x1;
    if (y1 < s->clip_y1)
        y1 = s->clip_y1;

    /* rejection if zero size */
    if (x1 >= x2 || y1 >= y2)
        return;

    s->dpy.dpy_fill_rectangle(s, x1, y1, x2 - x1, y2 - y1, color);
}

void xor_rectangle(QEditScreen *s,
                   int x, int y, int w, int h, QEColor color)
{
    /* intersect with clip region */
    int x1 = max_int(s->clip_x1, x);
    int y1 = max_int(s->clip_y1, y);
    int x2 = min_int(s->clip_x2, x1 + w);
    int y2 = min_int(s->clip_y2, y1 + h);

    if (x1 < x2 && y1 < y2) {
        s->dpy.dpy_xor_rectangle(s, x1, y1, x2 - x1, y2 - y1, color);
    }
}

/* set the clip rectangle (and does not clip by the previous one) */
void set_clip_rectangle(QEditScreen *s, CSSRect *r)
{
    int x1, y1, x2, y2;

    x1 = r->x1;
    y1 = r->y1;
    x2 = r->x2;
    y2 = r->y2;

    if (x2 > s->width)
        x2 = s->width;
    if (y2 > s->height)
        y2 = s->height;
    if (x1 < 0)
        x1 = 0;
    if (y1 < 0)
        y1 = 0;

    s->clip_x1 = x1;
    s->clip_y1 = y1;
    s->clip_x2 = x2;
    s->clip_y2 = y2;

    s->dpy.dpy_set_clip(s, x1, y1, x2 - x1, y2 - y1);
}

void push_clip_rectangle(QEditScreen *s, CSSRect *r0, CSSRect *r)
{
    int x1, y1, x2, y2;

    /* save old rectangle */
    r0->x1 = s->clip_x1;
    r0->y1 = s->clip_y1;
    r0->x2 = s->clip_x2;
    r0->y2 = s->clip_y2;

    /* load and clip new rectangle against the current one */
    x1 = r->x1;
    y1 = r->y1;
    x2 = r->x2;
    y2 = r->y2;

    if (x2 > s->clip_x2)
        x2 = s->clip_x2;
    if (y2 > s->clip_y2)
        y2 = s->clip_y2;
    if (x1 < s->clip_x1)
        x1 = s->clip_x1;
    if (y1 < s->clip_y1)
        y1 = s->clip_y1;

    /* set new rectangle */
    s->clip_x1 = x1;
    s->clip_y1 = y1;
    s->clip_x2 = x2;
    s->clip_y2 = y2;

    s->dpy.dpy_set_clip(s, x1, y1, x2 - x1, y2 - y1);
}

int qe_register_display(QEmacsState *qs, QEDisplay *dpy)
{
    QEDisplay **pp;

    pp = &first_dpy;
    while (*pp != NULL)
        pp = &(*pp)->next;
    *pp = dpy;
    dpy->next = NULL;
    return 0;
}

QEDisplay *probe_display(void)
{
    QEDisplay *p, *dpy;
    int probe_max, probe;

    p = first_dpy;
    dpy = NULL;
    probe_max = 0;
    while (p != NULL) {
        probe = p->dpy_probe ? p->dpy_probe() : 0;
        if (probe >= probe_max) {
            probe_max = probe;
            dpy = p;
        }
        p = p->next;
    }
    return dpy;
}

int qe_screen_init(QEmacsState *qs, QEditScreen *s, QEDisplay *dpy, int w, int h)
{
    s->dpy = dpy ? *dpy : dummy_dpy;
    return s->dpy.dpy_init(s, qs, w, h);
}

/* simple font cache */

#define FONT_CACHE_SIZE 32
static QEFont dummy_font;
static QEFont *font_cache[FONT_CACHE_SIZE];
static int font_cache_timestamp = 0;

void free_font_cache(QEditScreen *s)
{
    int i;
    for (i = 0; i < FONT_CACHE_SIZE; i++) {
        close_font(s, &font_cache[i]);
    }
}

QEFont *select_font(QEditScreen *s, int style, int size)
{
    QEFont *fc;
    int i, min_ts, min_index;

    min_ts = INT_MAX;
    min_index = -1;
    for (i = 0; i < FONT_CACHE_SIZE; i++) {
        fc = font_cache[i];
        if (fc) {
            if (fc->style == style && fc->size == size) {
                goto found;
            }
            if (fc->timestamp < min_ts && fc->refcount <= 0) {
                min_ts = fc->timestamp;
                min_index = i;
            }
        } else {
            min_ts = 0;
            min_index = i;
        }
    }
    /* not found : open new font */
    if (min_index < 0) {
        dpy_error(s, "Font cache full");
        goto fail;
    }
    if (font_cache[min_index]) {
        close_font(s, &font_cache[min_index]);
    }
    fc = open_font(s, style, size);
    if (!fc) {
        if (style & QE_FONT_FAMILY_FALLBACK_MASK)
            return NULL;

        dpy_error(s, "open_font: cannot open style=%X size=%d",
                  style, size);
        goto fail;
    }

    fc->style = style;
    fc->size = size;
    font_cache[min_index] = fc;
 found:
    fc->timestamp = font_cache_timestamp;
    font_cache_timestamp++;
    fc->refcount++;
    return fc;

 fail:
    /* select_font never returns NULL */
    /* CG: This is bogus, dummy font is not device compatible? */
    fc = &dummy_font;
    fc->system_font = 1;
    return fc;
}

QEBitmap *bmp_alloc(QEditScreen *s, int width, int height, int flags)
{
    QEBitmap *b;

    if (!s->dpy.dpy_bmp_alloc)
        return NULL;

    b = qe_mallocz(QEBitmap);
    if (!b)
        return NULL;
    b->width = width;
    b->height = height;
    b->flags = flags;
    if (s->dpy.dpy_bmp_alloc(s, b) < 0) {
        qe_free(&b);
        return NULL;
    }
    return b;
}

void bmp_free(QEditScreen *s, QEBitmap **bp)
{
    if (*bp) {
        s->dpy.dpy_bmp_free(s, *bp);
        qe_free(bp);
    }
}

#if 0
/* bitmap cache */
typedef struct QECachedBitmap {
    QEBitmap *bitmap;
    char url[1];
    int refcount;
    struct QECachedBitmap *next;
} QECachedBitmap;

/* dst_w or dst_h can be zero if unspecified */
/* XXX: add scaling for printing */
QECachedBitmap *cbmp_open(QEditScreen *s,
                          const char *url,
                          int dst_w, int dst_h)
{
}

/* return the DESTINATION size of the bitmap. return non zero if
   unknown size */
int cbmp_get_size(QECachedBitmap *bitmap, int *w_ptr, int *h_ptr)
{
}

void cbmp_draw(QEditScreen *s, QECachedBitmap *bitmap,
               int dst_x, int dst_y, int dst_w, int dst_h,
               int offset_x, int offset_y, int flags)
{
}

void cbmp_close(QEditScreen *cbmp)
{
}
#endif

/*---------------- QEPicture handling functions ----------------*/

int qe_draw_picture(QEditScreen *s, int dst_x, int dst_y, int dst_w, int dst_h,
                    const QEPicture *ip,
                    int src_x, int src_y, int src_w, int src_h,
                    int flags, QEColor col)
{
#ifndef CONFIG_TINY
    int x1 = dst_x;
    int y1 = dst_y;
    int x2 = x1 + dst_w;
    int y2 = y1 + dst_h;
    int w1, h1;

    /* quick clip rejection */
    if (x2 <= s->clip_x1 || y2 <= s->clip_y1 ||
        x1 >= s->clip_x2 || y1 >= s->clip_y2)
        return 1;

    /* region update */
    if (x2 > s->clip_x2)
        x2 = s->clip_x2;
    if (y2 > s->clip_y2)
        y2 = s->clip_y2;
    if (x1 < s->clip_x1)
        x1 = s->clip_x1;
    if (y1 < s->clip_y1)
        y1 = s->clip_y1;

    /* rejection if zero size */
    w1 = x2 - x1;
    h1 = y2 - y1;
    if (w1 < 0 || h1 < 0)
        return 1;

    if (w1 != dst_w) {
        int dx = min_int((x1 - dst_x) * src_w / dst_w, src_w - 1);
        src_x += dx;
        src_w = clamp_int(src_w * w1 / dst_w, 1, src_w - dx);
    }
    if (h1 != dst_h) {
        int dy = min_int((y1 - dst_y) * src_h / dst_h, src_h - 1);
        src_y += dy;
        src_h = clamp_int(src_h * h1 / dst_h, 1, src_h - dy);
    }

    if (s->dpy.dpy_draw_picture &&
        !s->dpy.dpy_draw_picture(s, x1, y1, w1, h1,
                                 ip, src_x, src_y, src_w, src_h, flags)) {
        return 0;
    } else {
        if (col != COLOR_TRANSPARENT) {
            /* if image cannot be displayed, just draw a rectangle */
            fill_rectangle(s, x1, y1, w1, h1, col);
        }
        return 2;
    }
#else
    return 1;
#endif
}

#ifndef CONFIG_TINY
static int qe_picture_format_bits(QEBitmapFormat format) {
    switch (format) {
    case QEBITMAP_FORMAT_1BIT:
        return 1;
    case QEBITMAP_FORMAT_4BIT:
        return 4;
    case QEBITMAP_FORMAT_8BIT:
        return 8;
    case QEBITMAP_FORMAT_RGB565:
    case QEBITMAP_FORMAT_RGB555:
        return 16;
    case QEBITMAP_FORMAT_RGB24:
    case QEBITMAP_FORMAT_BGR24:
        return 24;
    case QEBITMAP_FORMAT_RGBA32:
    case QEBITMAP_FORMAT_BGRA32:
        return 32;
    case QEBITMAP_FORMAT_YUV420P:
        /* unsupported for now */
    default:
        return 0;
    }
}

QEPicture *qe_create_picture(int width, int height,
                             QEBitmapFormat format, int flags)
{
    QEPicture *ip;
    unsigned int bits;

    bits = qe_picture_format_bits(format);
    if (bits <= 0)
        return NULL;

    ip = qe_mallocz(QEPicture);
    if (ip) {
        /* align pixmap lines on 64 bit boundaries */
        unsigned int wb = width * bits + 63 / 64 * 8;
        ip->width = width;
        ip->height = height;
        ip->format = format;
        ip->data[0] = qe_malloc_array(unsigned char, wb * height);
        ip->linesize[0] = wb;
    }
    return ip;
}

void qe_free_picture(QEPicture **ipp) {
    if (*ipp) {
        qe_free(&(*ipp)->data[0]);
        qe_free(ipp);
    }
}

int qe_picture_set_palette(QEPicture *ip, int mode,
                           unsigned char *p, int count, int tcolor)
{
    int i, r, g, b, incr;

    if (!ip)
        return 1;

    if (mode == 0)
        mode = QE_PAL_QECOLOR;

    incr = mode & 15;
    b = (mode >>  4) & 15;
    g = (mode >>  8) & 15;
    r = (mode >> 12) & 15;

    ip->tcolor = tcolor;
    if (ip->palette_size != 256) {
        qe_free(&ip->palette);
        ip->palette_size = 256;
        ip->palette = qe_malloc_array(QEColor, 256);
        if (ip->palette == NULL) {
            ip->palette_size = 0;
            return -1;
        }
        /* Default colors to standard palette */
        blockcpy(ip->palette, xterm_colors, 256);
    }
    count = min_int(count, 256);
    for (i = 0; i < count; i++) {
        ip->palette[i] = QERGB(p[r], p[g], p[b]);
        p += incr;
    }
    return 0;
}

static int qe_picture_scale(QEPicture *to, int dst_x, int dst_y, int dst_w, int dst_h,
                            const QEPicture *from,
                            int src_x, int src_y, int src_w, int src_h, int flags)
{
    if (from->format != to->format)
        return 1;

    if (from->format != QEBITMAP_FORMAT_RGBA32
    &&  from->format != QEBITMAP_FORMAT_BGRA32)
        return 1;
    {  // prevent -Wdeclaration-after-statement warning
#if 0
    /* Bilinear interpolation */
    uint32_t w = src_w, h = src_h, w2 = dst_w, h2 = dst_h;
    uint32_t x_ratio = ((w - 1) << 16) / w2;
    uint32_t y_ratio = ((h - 1) << 16) / h2;
    uint32_t pitch = from->linesize[0];
    uint32_t pitch2 = to->linesize[0];
    uint32_t y = 0;
    for (uint32_t i = 0; i < h2; i++, y += y_ratio) {
        uint32_t yr = y >> 16;
        uint32_t y_diff = y & 0xFFFF;
        const uint8_t *src = from->data[0] + (src_y + yr) * pitch + src_x * 4;
        uint8_t *dst = to->data[0] + (dst_y + i) * pitch2 + dst_x * 4;
        uint32_t x = 0;
        for (uint32_t j = 0; j < w2; j++, x += x_ratio) {
            uint32_t xr = x >> 16;
            uint32_t x_diff = x & 0xFFFF;
            uint32_t m0 = (0x10000 - x_diff) * (0x10000 - y_diff);
            uint32_t m1 = x_diff * (0x10000 - y_diff);
            uint32_t m2 = (0x10000 - x_diff) * y_diff;
            uint32_t m3 = x_diff * y_diff;

            for (int k = 0; k < 3; k++) {
                uint32_t v = (src[xr * 4 + k + 0] * (int64_t)m0 +
                              src[xr * 4 + k + 4] * (int64_t)m1 +
                              src[xr * 4 + k + pitch + 0] * (int64_t)m2 +
                              src[xr * 4 + k + pitch + 4] * (int64_t)m3) >> 32;
                dst[j * 4 + k] = (uint8_t)v;
            }
        }
    }
#else
    int x, y, sx, sy, sx0, sy0, dx, dy, width = dst_w, height = dst_h;

    /* Nearest-neighbor interpolation */
    dx = dy = sx0 = sy0 = 0;
    if (dst_w > 1) {
        if (dst_w > src_w) {
            dx = src_w * 0x10000 / dst_w;
        } else {
            dx = (src_w - 1) * 0x10000 / (dst_w - 1);
            sx0 = 0x8000;
        }
    }
    if (dst_h > 1) {
        if (dst_h > src_h) {
            dy = src_h * 0x10000 / dst_h;
        } else {
            dy = (src_h - 1) * 0x10000 / (dst_h - 1);
            sy0 = 0x8000;
        }
    }
    for (y = 0, sy = sy0; y < height; y++, sy += dy) {
        const uint32_t *src = (const uint32_t *)(void *)(from->data[0] +
                                                         (src_y + (sy >> 16)) * from->linesize[0]) + src_x;
        uint32_t *dst = (uint32_t *)(void *)(to->data[0] + (dst_y + y) * to->linesize[0]) + dst_x;

        for (x = 0, sx = sx0; x < width; x++, sx += dx) {
            /* XXX: No filtering */
            dst[x + 0] = src[sx >> 16];
        }
    }
#endif
    }
    return 0;
}

int qe_picture_copy(QEPicture *to, int dst_x, int dst_y, int dst_w, int dst_h,
                    const QEPicture *from,
                    int src_x, int src_y, int src_w, int src_h, int flags)
{
    const uint32_t *palette;
    int x, y, width = src_w, height = src_h;
    int res = 0;

    if (src_w != dst_w || src_h != dst_h) {
        /* Generic scaling */
        QEPicture *ip1 = NULL;

        if (from->format != QEBITMAP_FORMAT_RGBA32) {
            ip1 = qe_create_picture(src_w, src_h, QEBITMAP_FORMAT_RGBA32, 0);
            if (!ip1)
                return -1;
            res = qe_picture_copy(ip1, 0, 0, src_w, src_h,
                                  from, src_x, src_y, src_w, src_h, 0);
            from = ip1;
            src_x = src_y = 0;
        }
        if (!res) {
            res = qe_picture_scale(to, dst_x, dst_y, dst_w, dst_h,
                                   from, src_x, src_y, src_w, src_h, flags);
        }
        qe_free_picture(&ip1);
        return res;
    }

    if (from->format == QEBITMAP_FORMAT_8BIT && to->format == QEBITMAP_FORMAT_RGBA32) {
        palette = from->palette;
        if (palette == NULL) {
            palette = xterm_colors;
        }
        for (y = 0; y < height; y++) {
            const unsigned char *src = from->data[0] + (src_y + y) * from->linesize[0] + src_x;
            uint32_t *dst = (uint32_t *)(void *)(to->data[0] + (dst_y + y) * to->linesize[0]) + dst_x;
            int w4 = width & ~3;

            for (x = 0; x < w4; x += 4) {
                dst[x + 0] = palette[src[x + 0]];
                dst[x + 1] = palette[src[x + 1]];
                dst[x + 2] = palette[src[x + 2]];
                dst[x + 3] = palette[src[x + 3]];
            }
            for (; x < width; x++) {
                dst[x] = palette[src[x]];
            }
        }
        return 0;
    }
    if (from->format == QEBITMAP_FORMAT_4BIT && to->format == QEBITMAP_FORMAT_RGBA32) {
        palette = from->palette;
        if (palette == NULL) {
            palette = xterm_colors;
        }
        for (y = 0; y < height; y++) {
            const unsigned char *src = from->data[0] + (src_y + y) * from->linesize[0] + (src_x >> 1);
            uint32_t *dst = (uint32_t *)(void *)(to->data[0] + (dst_y + y) * to->linesize[0]) + dst_x;
            int shift, w4;

            x = 0;
            if (src_x & 1) {
                /* Convert incomplete left block */
                dst[x] = palette[*src++ & 15];
                x++;
            }
            /* Convert middle block */
            w4 = x + ((width - x) & ~3);
            for (x = 0; x < w4; x += 4, src += 2) {
                dst[x + 0] = palette[src[0] >> 4];
                dst[x + 1] = palette[src[0] & 15];
                dst[x + 2] = palette[src[1] >> 4];
                dst[x + 3] = palette[src[1] & 15];
            }
            /* Convert right block */
            shift = 4;
            for (; x < width; x++) {
                dst[x] = palette[(src[0] >> shift) & 15];
                src += (shift ^= 4) >> 2;
            }
        }
        return 0;
    }
    if (from->format == QEBITMAP_FORMAT_1BIT && to->format == QEBITMAP_FORMAT_RGBA32) {
        QEColor bw[2] = { QERGB(0, 0, 0), QERGB(0xff, 0xff, 0xff) };

        palette = from->palette;
        if (palette == NULL) {
            palette = bw;
        }
        for (y = 0; y < height; y++) {
            const unsigned char *src = from->data[0] + (src_y + y) * from->linesize[0] + (src_x >> 3);
            uint32_t *dst = (uint32_t *)(void *)(to->data[0] + (dst_y + y) * to->linesize[0]) + dst_x;
            int bits, shift, w8;

            x = 0;
            if (src_x & 7) {
                /* Convert incomplete left block */
                bits = *src++;
                shift = 8 - (src_x & 7);
                for (; x < width && shift != 0; x++) {
                    dst[x] = palette[(bits >> --shift) & 1];
                }
            }
            /* Convert middle block */
            w8 = x + ((width - x) & ~7);
            for (; x < w8; x += 8) {
                bits = *src++;
                dst[x + 0] = palette[(bits >> 7) & 1];
                dst[x + 1] = palette[(bits >> 6) & 1];
                dst[x + 2] = palette[(bits >> 5) & 1];
                dst[x + 3] = palette[(bits >> 4) & 1];
                dst[x + 4] = palette[(bits >> 3) & 1];
                dst[x + 5] = palette[(bits >> 2) & 1];
                dst[x + 6] = palette[(bits >> 1) & 1];
                dst[x + 7] = palette[(bits >> 0) & 1];
            }
            if (x < width) {
                /* Convert incomplete right block */
                bits = *src;
                shift = 8;
                for (; x < width; x++) {
                    dst[x] = palette[(bits >> --shift) & 1];
                }
            }
        }
        return 0;
    }
    if (from->format == QEBITMAP_FORMAT_RGB565 && to->format == QEBITMAP_FORMAT_RGBA32) {
        /* XXX: deal with endianness */
        for (y = 0; y < height; y++) {
            const uint16_t *src = (const uint16_t *)(void *)(from->data[0] + (src_y + y) * from->linesize[0]) + src_x;
            uint32_t *dst = (uint32_t *)(void *)(to->data[0] + (dst_y + y) * to->linesize[0]) + dst_x;
            for (x = 0; x < width; x++, src++) {
                unsigned int r = (src[0] >> 8) & 0xF8;
                unsigned int g = (src[0] >> 3) & 0xFC;
                unsigned int b = (src[0] << 3) & 0xF8;
                dst[x] = QERGB(r | (r >> 5), g | (g >> 6), b | (b >> 5));
            }
        }
        return 0;
    }
    if (from->format == QEBITMAP_FORMAT_RGB555 && to->format == QEBITMAP_FORMAT_RGBA32) {
        /* XXX: deal with endianness */
        for (y = 0; y < height; y++) {
            const uint16_t *src = (const uint16_t *)(void *)(from->data[0] + (src_y + y) * from->linesize[0]) + src_x;
            uint32_t *dst = (uint32_t *)(void *)(to->data[0] + (dst_y + y) * to->linesize[0]) + dst_x;
            for (x = 0; x < width; x++, src++) {
                unsigned int r = (src[0] >> 7) & 0xF8;
                unsigned int g = (src[0] >> 2) & 0xF8;
                unsigned int b = (src[0] << 3) & 0xF8;
                dst[x] = QERGB(r | (r >> 5), g | (g >> 5), b | (b >> 5));
            }
        }
        return 0;
    }
    if (from->format == QEBITMAP_FORMAT_RGB24 && to->format == QEBITMAP_FORMAT_RGBA32) {
        for (y = 0; y < height; y++) {
            const unsigned char *src = from->data[0] + (src_y + y) * from->linesize[0] + src_x * 3;
            uint32_t *dst = (uint32_t *)(void *)(to->data[0] + (dst_y + y) * to->linesize[0]) + dst_x;
            int w4 = width & ~3;

            for (x = 0; x < w4; x += 4, src += 12) {
                dst[x + 0] = QERGB(src[0], src[1], src[2]);
                dst[x + 1] = QERGB(src[3], src[4], src[5]);
                dst[x + 2] = QERGB(src[6], src[7], src[8]);
                dst[x + 3] = QERGB(src[9], src[10], src[11]);
            }
            for (; x < width; x++, src += 3) {
                dst[x] = QERGB(src[0], src[1], src[2]);
            }
        }
        return 0;
    }
    if (from->format == QEBITMAP_FORMAT_BGR24 && to->format == QEBITMAP_FORMAT_RGBA32) {
        for (y = 0; y < height; y++) {
            const unsigned char *src = from->data[0] + (src_y + y) * from->linesize[0] + src_x * 3;
            uint32_t *dst = (uint32_t *)(void *)(to->data[0] + (dst_y + y) * to->linesize[0]) + dst_x;
            int w4 = width & ~3;

            for (x = 0; x < w4; x += 4, src += 12) {
                dst[x + 0] = QERGB(src[2], src[1], src[0]);
                dst[x + 1] = QERGB(src[5], src[4], src[3]);
                dst[x + 2] = QERGB(src[8], src[7], src[6]);
                dst[x + 3] = QERGB(src[11], src[10], src[9]);
            }
            for (; x < width; x++, src += 3) {
                dst[x] = QERGB(src[2], src[1], src[0]);
            }
        }
        return 0;
    }
    if (from->format == QEBITMAP_FORMAT_BGRA32 && to->format == QEBITMAP_FORMAT_RGBA32) {
        for (y = 0; y < height; y++) {
            const unsigned char *src = from->data[0] + (src_y + y) * from->linesize[0] + src_x * 4;
            uint32_t *dst = (uint32_t *)(void *)(to->data[0] + (dst_y + y) * to->linesize[0]) + dst_x;
            int w4 = width & ~3;

            for (x = 0; x < w4; x += 4, src += 16) {
                dst[x + 0] = QERGB(src[0], src[1], src[2]);
                dst[x + 1] = QERGB(src[4], src[5], src[6]);
                dst[x + 2] = QERGB(src[8], src[9], src[10]);
                dst[x + 3] = QERGB(src[12], src[13], src[14]);
            }
            for (; x < width; x++, src += 4) {
                dst[x] = QERGB(src[0], src[1], src[2]);
            }
        }
        return 0;
    }
    return 1;
}
#endif  /* !CONFIG_TINY */
