/*
 * fbfrender - FBF font cache and renderer
 *
 * Copyright (c) 2001, 2002 Fabrice Bellard.
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

#ifndef FBFRENDER_H
#define FBFRENDER_H

/* glyph cache */
typedef struct GlyphCache {
    struct GlyphCache *hash_next;
    struct GlyphCache *prev, *next;
    void *private; /* private data available for the driver, initialized to NULL */
    /* font info */
    short size; /* font size */
    unsigned short style; /* font style */
    short w, h;   /* glyph bitmap size */
    short x, y;     /* glyph bitmap offset */
    unsigned short index; /* glyph index */
    unsigned short data_size;
    short xincr;  /* glyph x increment */
    unsigned char is_fallback; /* true if fallback glyph */
    unsigned char data[0];  /* CG: C99 flexible array */
} GlyphCache;

void fbf_text_metrics(QEditScreen *s, QEFont *font,
                      QECharMetrics *metrics,
                      const char32_t *str, int len);
GlyphCache *decode_cached_glyph(QEditScreen *s, QEFont *font, int code);
QEFont *fbf_open_font(QEditScreen *s, int style, int size);
void fbf_close_font(QEditScreen *s, QEFont **fontp);

int fbf_render_init(const char *font_path);
void fbf_render_cleanup(void);

struct fbf_font {
    const unsigned char *data;
    unsigned int size;
};
extern const struct fbf_font fbf_fonts[];

#endif
