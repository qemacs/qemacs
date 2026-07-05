/*
 * Frame buffer low level functions for QEmacs
 *
 * Copyright (c) 2001, 2002 Fabrice Bellard.
 * Copyright (c) 2002-2026 Charlie Gordon.
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
#include "cfb.h"
#include "fbfrender.h"

static unsigned int cfb15_get_color(unsigned int color)
{
    unsigned int r, g, b;

    r = (color >> 16) & 0xff;
    g = (color >> 8) & 0xff;
    b = (color) & 0xff;
    return ((((r) >> 3) << 10) | (((g) >> 3) << 5) | ((b) >> 3));
}

static unsigned int cfb16_get_color(unsigned int color)
{
    unsigned int r, g, b;

    r = (color >> 16) & 0xff;
    g = (color >> 8) & 0xff;
    b = (color) & 0xff;
    return ((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3));
}

static unsigned int cfb24_get_color(unsigned int color)
{
    return color & 0xffffff;
}

static void cfb16_fill_rectangle(QEditScreen *s,
                                 int x1, int y1, int w, int h, QEColor color)
{
    CFBContext *cfb = s->priv_data;
    unsigned char *dest, *d;
    int y, n;
    unsigned int col;

    col = cfb->get_color(color);
    col = (col << 16) | col;

    dest = cfb->base + y1 * cfb->wrap + x1 * 2;
    for (y = 0; y < h; y++) {
        d = dest;
        n = w;

        if (((intptr_t)d & 3) != 0) {
            ((uint16_t *)(void *)d)[0] = col;
            d += 2;
            n--;
        }

        while (n >= 8) {
            ((uint32_t *)(void *)d)[0] = col;
            ((uint32_t *)(void *)d)[1] = col;
            ((uint32_t *)(void *)d)[2] = col;
            ((uint32_t *)(void *)d)[3] = col;
            d += 16;
            n -= 8;
        }
        while (n > 0) {
            ((uint16_t *)(void *)d)[0] = col;
            d += 2;
            n--;
        }
        dest += cfb->wrap;
    }
}

static void cfb16_xor_rectangle(QEditScreen *s,
                                int x1, int y1, int w, int h, QEColor color)
{
    CFBContext *cfb = s->priv_data;
    unsigned char *dest, *d;
    int y, n;
    unsigned int col;

    col = cfb->get_color(color);
    col = (col << 16) | col;

    dest = cfb->base + y1 * cfb->wrap + x1 * 2;
    for (y = 0; y < h; y++) {
        d = dest;
        for (n = w; n != 0; n--) {
            ((uint16_t *)(void *)d)[0] ^= 0xffff;
            d += 2;
        }
        dest += cfb->wrap;
    }
}

static void cfb32_fill_rectangle(QEditScreen *s,
                                 int x1, int y1, int w, int h, QEColor color)
{
    CFBContext *cfb = s->priv_data;
    unsigned char *dest, *d;
    int y, n;
    unsigned int col;

    col = cfb->get_color(color);

    dest = cfb->base + y1 * cfb->wrap + x1 * 4;
    for (y = 0; y < h; y++) {
        d = dest;
        n = w;
        while (n >= 4) {
            ((uint32_t *)(void *)d)[0] = col;
            ((uint32_t *)(void *)d)[1] = col;
            ((uint32_t *)(void *)d)[2] = col;
            ((uint32_t *)(void *)d)[3] = col;
            d += 16;
            n -= 4;
        }
        while (n > 0) {
            ((uint32_t *)(void *)d)[0] = col;
            d += 4;
            n--;
        }
        dest += cfb->wrap;
    }
}

static void cfb32_xor_rectangle(QEditScreen *s,
                                int x1, int y1, int w, int h, QEColor color)
{
    CFBContext *cfb = s->priv_data;
    unsigned char *dest, *d;
    int y, n;
    unsigned int col = 0x00ffffff;

    dest = cfb->base + y1 * cfb->wrap + x1 * 4;
    for (y = 0; y < h; y++) {
        d = dest;
        for (n = w; n != 0; n--) {
            ((uint32_t *)(void *)d)[0] ^= col;
            d += 4;
        }
        dest += cfb->wrap;
    }
}

static void cfb16_draw_glyph(QEditScreen *s1,
                             int x1, int y1, int w, int h, QEColor color,
                             unsigned char *glyph, int glyph_wrap)
{
    CFBContext *cfb = s1->priv_data;
    unsigned char *dest, *d, *s, *src;
    int n;
    unsigned int col;

    col = cfb->get_color(color);
    dest = cfb->base + y1 * cfb->wrap + x1 * 2;
    src = glyph;

    while (h > 0) {
        n = w;
        s = src;
        d = dest;
        while (n >= 4) {
            if (s[0] >= 0x80)
                ((uint16_t *)(void *)d)[0] = col;
            if (s[1] >= 0x80)
                ((uint16_t *)(void *)d)[1] = col;
            if (s[2] >= 0x80)
                ((uint16_t *)(void *)d)[2] = col;
            if (s[3] >= 0x80)
                ((uint16_t *)(void *)d)[3] = col;
            s += 4;
            d += 4 * 2;
            n -= 4;
        }
        while (n > 0) {
            if (s[0] >= 0x80)
                ((uint16_t *)(void *)d)[0] = col;
            s++;
            d += 2;
            n--;
        }

        h--;
        src += glyph_wrap;
        dest += cfb->wrap;
    }
}

static void cfb32_draw_glyph(QEditScreen *s1,
                             int x1, int y1, int w, int h, QEColor color,
                             unsigned char *glyph, int glyph_wrap)
{
    CFBContext *cfb = s1->priv_data;
    unsigned char *dest, *d, *s, *src;
    int n;
    unsigned int col;

    col = cfb->get_color(color);
    dest = cfb->base + y1 * cfb->wrap + x1 * 4;
    src = glyph;

    while (h > 0) {
        n = w;
        s = src;
        d = dest;
        while (n >= 4) {
            if (s[0] >= 0x80)
                ((uint32_t *)(void *)d)[0] = col;
            if (s[1] >= 0x80)
                ((uint32_t *)(void *)d)[1] = col;
            if (s[2] >= 0x80)
                ((uint32_t *)(void *)d)[2] = col;
            if (s[3] >= 0x80)
                ((uint32_t *)(void *)d)[3] = col;
            s += 4;
            d += 4 * 4;
            n -= 4;
        }
        while (n > 0) {
            if (s[0] >= 0x80)
                ((uint32_t *)(void *)d)[0] = col;
            s++;
            d += 4;
            n--;
        }

        h--;
        src += glyph_wrap;
        dest += cfb->wrap;
    }
}

static void cfb_draw_text(QEditScreen *s, QEFont *font,
                          int x0, int y_base, const unsigned int *str, int len,
                          QEColor color)
{
    CFBContext *cfb = s->priv_data;
    int i, x;

    x = x0;
    for (i = 0; i < len; i++) {
        int x1, y1, x2, y2, wrap;
        unsigned char *glyph_ptr;
        unsigned int cc;
        GlyphCache *g;

        cc = str[i];
        g = decode_cached_glyph(s, font, cc);
        if (!g)
            continue;

        x1 = x + g->x;
        x2 = x1 + g->w;
        y2 = y_base - g->y;
        y1 = y2 - g->h;
        glyph_ptr = g->data;
        wrap = g->w;

        if (x1 >= s->clip_x1 && y1 >= s->clip_y1 &&
            x2 <= s->clip_x2 && y2 <= s->clip_y2)
            goto draw;

        if (x2 <= s->clip_x1 || y2 <= s->clip_y1 ||
            x1 >= s->clip_x2 || y1 >= s->clip_y2)
            goto nodraw;

        if (x1 < s->clip_x1) {
            glyph_ptr += s->clip_x1 - x1;
            x1 = s->clip_x1;
        }
        if (x2 > s->clip_x2)
            x2 = s->clip_x2;
        if (y1 < s->clip_y1) {
            glyph_ptr += (s->clip_y1 - y1) * wrap;
            y1 = s->clip_y1;
        }
        if (y2 > s->clip_y2)
            y2 = s->clip_y2;
    draw:
        cfb->draw_glyph(s, x1, y1,
                        x2 - x1, y2 - y1,
                        color, glyph_ptr, wrap);
    nodraw:
        x += g->xincr;
    }

    /* text decoration synthesis */
    if (font->style & QE_FONT_DECORATION_MASK) {
        int y0 = y_base - font->ascent;
        int x1 = x;
        int y1 = y_base + font->descent;
        int w = x1 - x0;
        int h = y1 - y0;
        int dh = max_int((font->descent + 2) / 4, 1);
        int dw = dh;    // assume 1.0 aspect ratio
        if (font->style & QE_FONT_STYLE_UNDERLINE) {
            int y = y_base + (font->descent + 1) / 3;
            fill_rectangle(s, x0, y, w, dh, color);
        }
        if (font->style & QE_FONT_STYLE_LINE_THROUGH) {
            int y = y_base - (font->ascent / 2 - 1);
            fill_rectangle(s, x0, y, w, dh, color);
        }
        if (font->style & (QE_FONT_STYLE_OVERLINE | QE_FONT_STYLE_BOX)) {
            fill_rectangle(s, x0, y0 - dh, w, dh, color);
        }
        if (font->style & QE_FONT_STYLE_BOX) {
            fill_rectangle(s, x0 - dw, y0 - dh, dw, h + dh + dh, color);
            fill_rectangle(s, x1, y0 - dh, dw, h + dh + dh, color);
            fill_rectangle(s, x0, y1, w, dh, color);
        }
    }
}

static void cfb_set_clip(qe__unused__ QEditScreen *s,
                         qe__unused__ int x, qe__unused__ int y,
                         qe__unused__ int w, qe__unused__ int h)
{
}

int cfb_init(QEditScreen *s,
             void *base, int wrap, int depth, const char *font_path)
{
    CFBContext *cfb = s->priv_data;

    cfb->base = base;
    cfb->wrap = wrap;
    cfb->depth = depth;
    cfb->bpp = (depth + 7) / 8;

    switch (depth) {
    case 15:
        cfb->get_color = cfb15_get_color;
        break;
    case 16:
        cfb->get_color = cfb16_get_color;
        break;
    case 24:
    default:
        cfb->get_color = cfb24_get_color;
        break;
    }

    switch (cfb->bpp) {
    case 2:
        s->dpy.dpy_fill_rectangle = cfb16_fill_rectangle;
        s->dpy.dpy_xor_rectangle = cfb16_xor_rectangle;
        cfb->draw_glyph = cfb16_draw_glyph;
       break;
    default:
    case 4:
        s->dpy.dpy_fill_rectangle = cfb32_fill_rectangle;
        s->dpy.dpy_xor_rectangle = cfb32_xor_rectangle;
        cfb->draw_glyph = cfb32_draw_glyph;
        break;
    }

    s->dpy.dpy_set_clip = cfb_set_clip;
    s->dpy.dpy_draw_text = cfb_draw_text;

    /* fbf font handling init */
    s->dpy.dpy_text_metrics = fbf_text_metrics;
    s->dpy.dpy_open_font = fbf_open_font;
    s->dpy.dpy_close_font = fbf_close_font;

    return fbf_render_init(font_path);
}
