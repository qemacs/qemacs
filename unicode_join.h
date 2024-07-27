/*
 * Unicode joining algorithms for QEmacs.
 *
 * Copyright (c) 2000 Fabrice Bellard.
 * Copyright (c) 2000-2024 Charlie Gordon.
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

#ifndef UNICODE_JOIN_H
#define UNICODE_JOIN_H

int load_ligatures(const char *filename);
void unload_ligatures(void);
int combine_accent(char32_t *buf, char32_t c, char32_t accent);
int expand_ligature(char32_t *buf, char32_t c);

int unicode_to_glyphs(char32_t *dst, unsigned int *char_to_glyph_pos,
                      int dst_size, char32_t *src, int src_size,
                      int reverse);

char32_t get_mirror_char(char32_t c);

// XXX: should rewrite this part
typedef enum {
    BIDIR_TYPE_LTR,   /* Strong Left-to-Right */
    BIDIR_TYPE_RTL,   /* Right-to-left characters */
    BIDIR_TYPE_WL,    /* Weak left to right */
    BIDIR_TYPE_WR,    /* Weak right to left */
    BIDIR_TYPE_EN,    /* European Numeral */
    BIDIR_TYPE_ES,    /* European number Separator */
    BIDIR_TYPE_ET,    /* European number Terminator */
    BIDIR_TYPE_AN,    /* Arabic Numeral */
    BIDIR_TYPE_CS,    /* Common Separator */
    BIDIR_TYPE_BS,    /* Block Separator */
    BIDIR_TYPE_SS,    /* Segment Separator */
    BIDIR_TYPE_WS,    /* Whitespace */
    BIDIR_TYPE_AL,    /* Arabic Letter */
    BIDIR_TYPE_NSM,   /* Non Spacing Mark */
    BIDIR_TYPE_BN,    /* Boundary Neutral */
    BIDIR_TYPE_ON,    /* Other Neutral */
    BIDIR_TYPE_LRE,   /* Left-to-Right Embedding */
    BIDIR_TYPE_RLE,   /* Right-to-Left Embedding */
    BIDIR_TYPE_PDF,   /* Pop Directional Flag */
    BIDIR_TYPE_LRO,   /* Left-to-Right Override */
    BIDIR_TYPE_RLO,   /* Right-to-Left Override */

    /* The following are only used internally */
    BIDIR_TYPE_SOT,
    BIDIR_TYPE_EOT,
    BIDIR_TYPE_N,
    BIDIR_TYPE_E,
    BIDIR_TYPE_CTL,   /* Control units */
    BIDIR_TYPE_EO,    /* Control units */
    BIDIR_TYPE_DEL,  /* type record is to be deleted */
    BIDIR_TYPE_L = BIDIR_TYPE_LTR,
    BIDIR_TYPE_R = BIDIR_TYPE_RTL,
    BIDIR_TYPE_CM = BIDIR_TYPE_ON + 2,
} BidirCharType;

typedef struct BidirTypeLink {
    BidirCharType type;
    int pos;
    int len;
    int level;
} BidirTypeLink;

BidirCharType bidir_get_type(char32_t ch);
BidirCharType bidir_get_type_test(char32_t ch);

void bidir_analyze_string(BidirTypeLink *type_rl_list,
                          BidirCharType *pbase_dir,
                          int *pmax_level);

/* arabic.c */
int arabic_join(char32_t *line, unsigned int *ctog, int len);

/* indic.c */
int devanagari_log2vis(char32_t *str, unsigned int *ctog, int len);

#endif /* UNICODE_JOIN_H */
