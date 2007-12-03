/*
 * Frame buffer low level functions for QEmacs
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
