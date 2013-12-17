/*
 * fbfrender - FBF font cache and renderer
 *
 * Copyright (c) 2001, 2002 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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
    unsigned char data[0];
} GlyphCache;

void fbf_text_metrics(QEditScreen *s, QEFont *font,
                      QECharMetrics *metrics,
                      const unsigned int *str, int len);
GlyphCache *decode_cached_glyph(QEditScreen *s, QEFont *font, int code);
QEFont *fbf_open_font(QEditScreen *s, int style, int size);
void fbf_close_font(QEditScreen *s, QEFont *font);

int fbf_render_init(const char *font_path);
void fbf_render_cleanup(void);

struct fbf_font {
    const unsigned char *data;
    unsigned int size;
};
extern const struct fbf_font fbf_fonts[];

#endif
