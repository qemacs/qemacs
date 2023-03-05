/*
 * Frame buffer low level functions for QEmacs
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

typedef struct CFBContext {
    unsigned char *base;
    int bpp;   /* number of bytes per pixel */
    int depth; /* number of color bits per pixel */
    int wrap;
    unsigned int (*get_color)(unsigned int);
    void (*draw_glyph)(QEditScreen *s1,
                       int x1, int y1, int w, int h, QEColor color,
                       unsigned char *glyph, int glyph_wrap);
} CFBContext;

int cfb_init(QEditScreen *s,
             void *base, int wrap, int depth, const char *font_path);
