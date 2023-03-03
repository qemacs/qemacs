/*
 * Unicode joining algorithms for QEmacs.
 *
 * Copyright (c) 2000 Fabrice Bellard.
 * Copyright (c) 2000-2023 Charlie Gordon.
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

#ifndef UNICODE_JOIN_H
#define UNICODE_JOIN_H

int load_ligatures(const char *filename);
void unload_ligatures(void);
int combine_accent(char32_t *buf, char32_t c, char32_t accent);
int expand_ligature(char32_t *buf, char32_t c);

int unicode_to_glyphs(char32_t *dst, unsigned int *char_to_glyph_pos,
                      int dst_size, char32_t *src, int src_size,
                      int reverse);

/* arabic.c */
int arabic_join(char32_t *line, unsigned int *ctog, int len);

/* indic.c */
int devanagari_log2vis(char32_t *str, unsigned int *ctog, int len);

// XXX: should rewrite this part
#include "qfribidi.h"

#endif /* UNICODE_JOIN_H */
