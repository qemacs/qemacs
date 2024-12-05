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

#include "util.h"
#include "unicode_join.h"

/*---- ligature tables ----*/

static unsigned short *subst1;
static unsigned short *ligature2;
static unsigned short *ligature_long;

static unsigned short subst1_count;
static unsigned short ligature2_count;

static int uni_get_be16(FILE *f, unsigned short *pv) {
    /* read a big-endian unsigned 16-bit value from `f` into `*pv` */
    /* return -1 if end of file */
    int c1, c2;
    if ((c1 = fgetc(f)) != EOF && (c2 = fgetc(f)) != EOF) {
        *pv = (c1 << 8) | c2;
        return 0;
    } else {
        return 1;
    }
}

static unsigned short *read_array_be16(FILE *f, int n) {
    unsigned short *tab;
    int i;

    tab = qe_malloc_array(unsigned short, n);
    if (!tab)
        return NULL;
    for (i = 0; i < n; i++) {
        if (uni_get_be16(f, &tab[i])) {
            qe_free(&tab);
            return NULL;
        }
    }
    return tab;
}

int load_ligatures(const char *filename) {
    FILE *f;
    unsigned char sig[4];
    unsigned short long_count;

    f = fopen(filename, "r");
    if (!f)
        return -1;

    if (fread(sig, 1, 4, f) != 4
    ||  memcmp(sig, "liga", 4) != 0
    ||  uni_get_be16(f, &subst1_count)
    ||  uni_get_be16(f, &ligature2_count)
    ||  uni_get_be16(f, &long_count)
    ||  !(subst1 = read_array_be16(f, subst1_count * 2))
    ||  !(ligature2 = read_array_be16(f, ligature2_count * 3))
    ||  !(ligature_long = read_array_be16(f, long_count))) {
        unload_ligatures();
        fclose(f);
        return -1;
    } else {
        fclose(f);
        return 0;
    }
}

void unload_ligatures(void) {
    qe_free(&subst1);
    qe_free(&ligature2);
    qe_free(&ligature_long);
    subst1_count = 0;
    ligature2_count = 0;
}

/* ligature2 table is sorted by increasing v1 and v2.
 * a return value of `0` indicates a placeholder for a
 * complex ligature handled in `ligature_long`.
 */
static int find_ligature(char32_t l1, char32_t l2) {
    const unsigned short *p = ligature2;
    size_t a = 0;
    size_t b = ligature2_count;
    while (a < b) {
        size_t m = (a + b) >> 1;
        char32_t v1 = p[3 * m];
        char32_t v2 = p[3 * m + 1];
        if (v1 == l1 && v2 == l2)
            return p[3 * m + 2];
        else
        if (v1 > l1 || (v1 == l1 && v2 > l2)) {
            b = m;
        } else {
            a = m + 1;
        }
    }
    return -1;
}

int combine_accent(char32_t *buf, char32_t c, char32_t accent) {
    int lig = find_ligature(c, accent);
    if (lig > 0) {
        *buf = (char32_t)lig;
        return 1;
    } else {
        return 0;
    }
}

/* No need for efficiency */
int expand_ligature(char32_t *buf, char32_t c) {
    const unsigned short *a, *b;

    if (c > 0x7f && c <= 0xffff) {
        for (a = ligature2, b = a + 3 * ligature2_count; a < b; a += 3) {
            if (a[2] == c) {
                buf[0] = a[0];
                buf[1] = a[1];
                return 1;
            }
        }
    }
    return 0;
}

char32_t qe_wcunaccent(char32_t c) {
    char32_t buf[2];
    if (expand_ligature(buf, c) && qe_isaccent(buf[1]))
        return buf[0];
    else
        return c;
}

/* simplistic case change for non ASCII glyphs: only support accents */
char32_t qe_wctoupper(char32_t c) {
    char32_t buf[2];
    if (expand_ligature(buf, c) && qe_isaccent(buf[1])) {
        int c2 = find_ligature(qe_wtoupper(buf[0]), buf[1]);
        if (c2 > 0)
            return (char32_t)c2;
    }
    return c;
}

char32_t qe_wctolower(char32_t c) {
    char32_t buf[2];
    if (expand_ligature(buf, c) && qe_isaccent(buf[1])) {
        int c2 = find_ligature(qe_wtolower(buf[0]), buf[1]);
        if (c2 > 0)
            return (char32_t)c2;
    }
    return c;
}

/* apply all the ligature rules in logical order. Always return a
   smaller or equal buffer */
static int unicode_ligature(char32_t *buf_out,
                            unsigned int *pos_L_to_V,
                            int len)
{
    int len1, len2, i, j, l;
    char32_t l1, l2;
    char32_t *q;
    const unsigned short *lig;
    /* CG: C99 variable-length arrays may be too large */
    // XXX: why do we need a local copy?
    char32_t buf[len];

    blockcpy(buf, buf_out, len);

    q = buf_out;
    for (i = 0; i < len;) {
        l1 = buf[i];
        /* eliminate invisible chars */
        if (l1 >= 0x202a && l1 <= 0x202e) {
            /* LRE, RLE, PDF, RLO, LRO */
            pos_L_to_V[i] = q - buf_out;
            i++;
            goto found;
        }
        /* fast test to eliminate common cases */
        if (i == len - 1)
            goto nolig;
        l2 = buf[i + 1];
        if (l1 <= 0x7f && l2 <= 0x7f)
            goto nolig;
        l = find_ligature(l1, l2);
        if (l < 0)
            goto nolig;
        if (l > 0) {
            /* ligature of length 2 found */
            pos_L_to_V[i] = q - buf_out;
            pos_L_to_V[i+1] = q - buf_out;
            *q++ = (char32_t)l;
            i += 2;
        } else {
            /* generic case: use ligature_long[] table */
            lig = ligature_long;
            for (;;) {
                len1 = *lig++;
                if (len1 == 0)
                    break;
                len2 = *lig++;
                if (i + len1 <= len) {
                    for (j = 0; j < len1; j++) {
                        if (buf[i+j] != lig[j])
                            goto notfound;
                    }
                    for (j = 0; j < len1; j++)
                        pos_L_to_V[i + j] = q - buf_out;
                    for (j = 0; j < len2; j++) {
                        *q++ = lig[len1 + j];
                    }
                    i += len1;
                    goto found;
                }
            notfound:
                lig += len1 + len2;
            }
            /* nothing found */
        nolig:
            pos_L_to_V[i] = q - buf_out;
            *q++ = l1;
            i++;
        found:
            ;
        }
    }
    return q - buf_out;
}

/* fast classification of unicode chars to optimize the algorithms */
#define UNICODE_ARABIC   0x00000001
#define UNICODE_INDIC    0x00000002
#define UNICODE_NONASCII 0x00000004

static int unicode_classify(const char32_t *buf, int len) {
    int i, mask;

    mask = 0;
    for (i = 0; i < len; i++) {
        char32_t c = buf[i];
        if (c <= 0x7f) /* latin1 fast handling */
            continue;
        mask |= UNICODE_NONASCII;
        if (c < 0xA00) {
            if ((c & ~0xff) == 0x600)   /* 0600..06FF */
                mask |= UNICODE_ARABIC;
            else
            if ((c & ~0x7f) == 0x900)   /* 0900..097F */
                mask |= UNICODE_INDIC;
        }
    }
    return mask;
}

static void compose_char_to_glyph(unsigned int *ctog, int len,
                                  const unsigned int *ctog1)
{
    int i;
    for (i = 0; i < len; i++)
        ctog[i] = ctog1[ctog[i]];
}

static void bidi_reverse_buf(char32_t *str, int len)
{
    int i, len2 = len / 2;

    for (i = 0; i < len2; i++) {
        char32_t tmp = str[i];
        str[i] = get_mirror_char(str[len - 1 - i]);
        str[len - 1 - i] = get_mirror_char(tmp);
    }
    /* do not forget central char! */
    if (len & 1) {
        str[len2] = get_mirror_char(str[len2]);
    }
}

/* Convert a string of unicode characters to a string of glyphs. We
   assume that the font implements a minimum number of standard
   ligature chars. The string is reversed if 'reversed' is set to
   deal with the bidir case. 'char_to_glyph_pos' gives the index of
   the first glyph associated to a given character of the source
   buffer. */
int unicode_to_glyphs(char32_t *dst, unsigned int *char_to_glyph_pos,
                      int dst_size, char32_t *src, int src_size,
                      int reverse)
{
    int len, i;
    /* CG: C99 variable-length arrays may be too large */
    unsigned int ctog[src_size];
    unsigned int ctog1[src_size];
    char32_t buf[src_size];
    int unicode_class;

    unicode_class = unicode_classify(src, src_size);
    if (unicode_class == 0 && !reverse) {
        /* fast case: no special treatment */
        len = min_int(src_size, dst_size);
        blockcpy(dst, src, len);
        if (char_to_glyph_pos) {
            for (i = 0; i < len; i++)
                char_to_glyph_pos[i] = i;
        }
        return len;
    } else {
        /* generic case */

        /* init current buffer */
        len = src_size;
        for (i = 0; i < len; i++)
            ctog[i] = i;
        blockcpy(buf, src, len);

        /* apply each filter */

        if (unicode_class & UNICODE_ARABIC) {
            len = arabic_join(buf, ctog1, len);
            /* not needed for arabic_join */
            //compose_char_to_glyph(ctog, src_size, ctog1);
        }

        if (unicode_class & UNICODE_INDIC) {
            len = devanagari_log2vis(buf, ctog1, len);
            compose_char_to_glyph(ctog, src_size, ctog1);
        }

        len = unicode_ligature(buf, ctog1, len);
        compose_char_to_glyph(ctog, src_size, ctog1);

        if (reverse) {
            bidi_reverse_buf(buf, len);
            for (i = 0; i < src_size; i++) {
                ctog[i] = len - 1 - ctog[i];
            }
        }

        if (len > dst_size)
            len = dst_size;
        blockcpy(dst, buf, len);

        if (char_to_glyph_pos) {
            blockcpy(char_to_glyph_pos, ctog, src_size);
        }
    }
    return len;
}

/* from BidiMirroring-15.0.0.txt
 *  Date: 2022-05-03, 18:47:00 GMT [KW, RP]
 */
static const unsigned short mirror_pairs[][2] = {
    { 0x0028, 0x0029 }, // LEFT PARENTHESIS
    { 0x0029, 0x0028 }, // RIGHT PARENTHESIS
    { 0x003C, 0x003E }, // LESS-THAN SIGN
    { 0x003E, 0x003C }, // GREATER-THAN SIGN
    { 0x005B, 0x005D }, // LEFT SQUARE BRACKET
    { 0x005D, 0x005B }, // RIGHT SQUARE BRACKET
    { 0x007B, 0x007D }, // LEFT CURLY BRACKET
    { 0x007D, 0x007B }, // RIGHT CURLY BRACKET
    { 0x00AB, 0x00BB }, // LEFT-POINTING DOUBLE ANGLE QUOTATION MARK
    { 0x00BB, 0x00AB }, // RIGHT-POINTING DOUBLE ANGLE QUOTATION MARK
    { 0x0F3A, 0x0F3B }, // TIBETAN MARK GUG RTAGS GYON
    { 0x0F3B, 0x0F3A }, // TIBETAN MARK GUG RTAGS GYAS
    { 0x0F3C, 0x0F3D }, // TIBETAN MARK ANG KHANG GYON
    { 0x0F3D, 0x0F3C }, // TIBETAN MARK ANG KHANG GYAS
    { 0x169B, 0x169C }, // OGHAM FEATHER MARK
    { 0x169C, 0x169B }, // OGHAM REVERSED FEATHER MARK
    { 0x2039, 0x203A }, // SINGLE LEFT-POINTING ANGLE QUOTATION MARK
    { 0x203A, 0x2039 }, // SINGLE RIGHT-POINTING ANGLE QUOTATION MARK
    { 0x2045, 0x2046 }, // LEFT SQUARE BRACKET WITH QUILL
    { 0x2046, 0x2045 }, // RIGHT SQUARE BRACKET WITH QUILL
    { 0x207D, 0x207E }, // SUPERSCRIPT LEFT PARENTHESIS
    { 0x207E, 0x207D }, // SUPERSCRIPT RIGHT PARENTHESIS
    { 0x208D, 0x208E }, // SUBSCRIPT LEFT PARENTHESIS
    { 0x208E, 0x208D }, // SUBSCRIPT RIGHT PARENTHESIS
    { 0x2208, 0x220B }, // ELEMENT OF
    { 0x2209, 0x220C }, // [BEST FIT] NOT AN ELEMENT OF
    { 0x220A, 0x220D }, // SMALL ELEMENT OF
    { 0x220B, 0x2208 }, // CONTAINS AS MEMBER
    { 0x220C, 0x2209 }, // [BEST FIT] DOES NOT CONTAIN AS MEMBER
    { 0x220D, 0x220A }, // SMALL CONTAINS AS MEMBER
    { 0x2215, 0x29F5 }, // DIVISION SLASH
    { 0x221F, 0x2BFE }, // RIGHT ANGLE
    { 0x2220, 0x29A3 }, // ANGLE
    { 0x2221, 0x299B }, // MEASURED ANGLE
    { 0x2222, 0x29A0 }, // SPHERICAL ANGLE
    { 0x2224, 0x2AEE }, // DOES NOT DIVIDE
    { 0x223C, 0x223D }, // TILDE OPERATOR
    { 0x223D, 0x223C }, // REVERSED TILDE
    { 0x2243, 0x22CD }, // ASYMPTOTICALLY EQUAL TO
    { 0x2245, 0x224C }, // APPROXIMATELY EQUAL TO
    { 0x224C, 0x2245 }, // ALL EQUAL TO
    { 0x2252, 0x2253 }, // APPROXIMATELY EQUAL TO OR THE IMAGE OF
    { 0x2253, 0x2252 }, // IMAGE OF OR APPROXIMATELY EQUAL TO
    { 0x2254, 0x2255 }, // COLON EQUALS
    { 0x2255, 0x2254 }, // EQUALS COLON
    { 0x2264, 0x2265 }, // LESS-THAN OR EQUAL TO
    { 0x2265, 0x2264 }, // GREATER-THAN OR EQUAL TO
    { 0x2266, 0x2267 }, // LESS-THAN OVER EQUAL TO
    { 0x2267, 0x2266 }, // GREATER-THAN OVER EQUAL TO
    { 0x2268, 0x2269 }, // [BEST FIT] LESS-THAN BUT NOT EQUAL TO
    { 0x2269, 0x2268 }, // [BEST FIT] GREATER-THAN BUT NOT EQUAL TO
    { 0x226A, 0x226B }, // MUCH LESS-THAN
    { 0x226B, 0x226A }, // MUCH GREATER-THAN
    { 0x226E, 0x226F }, // [BEST FIT] NOT LESS-THAN
    { 0x226F, 0x226E }, // [BEST FIT] NOT GREATER-THAN
    { 0x2270, 0x2271 }, // [BEST FIT] NEITHER LESS-THAN NOR EQUAL TO
    { 0x2271, 0x2270 }, // [BEST FIT] NEITHER GREATER-THAN NOR EQUAL TO
    { 0x2272, 0x2273 }, // [BEST FIT] LESS-THAN OR EQUIVALENT TO
    { 0x2273, 0x2272 }, // [BEST FIT] GREATER-THAN OR EQUIVALENT TO
    { 0x2274, 0x2275 }, // [BEST FIT] NEITHER LESS-THAN NOR EQUIVALENT TO
    { 0x2275, 0x2274 }, // [BEST FIT] NEITHER GREATER-THAN NOR EQUIVALENT TO
    { 0x2276, 0x2277 }, // LESS-THAN OR GREATER-THAN
    { 0x2277, 0x2276 }, // GREATER-THAN OR LESS-THAN
    { 0x2278, 0x2279 }, // [BEST FIT] NEITHER LESS-THAN NOR GREATER-THAN
    { 0x2279, 0x2278 }, // [BEST FIT] NEITHER GREATER-THAN NOR LESS-THAN
    { 0x227A, 0x227B }, // PRECEDES
    { 0x227B, 0x227A }, // SUCCEEDS
    { 0x227C, 0x227D }, // PRECEDES OR EQUAL TO
    { 0x227D, 0x227C }, // SUCCEEDS OR EQUAL TO
    { 0x227E, 0x227F }, // [BEST FIT] PRECEDES OR EQUIVALENT TO
    { 0x227F, 0x227E }, // [BEST FIT] SUCCEEDS OR EQUIVALENT TO
    { 0x2280, 0x2281 }, // [BEST FIT] DOES NOT PRECEDE
    { 0x2281, 0x2280 }, // [BEST FIT] DOES NOT SUCCEED
    { 0x2282, 0x2283 }, // SUBSET OF
    { 0x2283, 0x2282 }, // SUPERSET OF
    { 0x2284, 0x2285 }, // [BEST FIT] NOT A SUBSET OF
    { 0x2285, 0x2284 }, // [BEST FIT] NOT A SUPERSET OF
    { 0x2286, 0x2287 }, // SUBSET OF OR EQUAL TO
    { 0x2287, 0x2286 }, // SUPERSET OF OR EQUAL TO
    { 0x2288, 0x2289 }, // [BEST FIT] NEITHER A SUBSET OF NOR EQUAL TO
    { 0x2289, 0x2288 }, // [BEST FIT] NEITHER A SUPERSET OF NOR EQUAL TO
    { 0x228A, 0x228B }, // [BEST FIT] SUBSET OF WITH NOT EQUAL TO
    { 0x228B, 0x228A }, // [BEST FIT] SUPERSET OF WITH NOT EQUAL TO
    { 0x228F, 0x2290 }, // SQUARE IMAGE OF
    { 0x2290, 0x228F }, // SQUARE ORIGINAL OF
    { 0x2291, 0x2292 }, // SQUARE IMAGE OF OR EQUAL TO
    { 0x2292, 0x2291 }, // SQUARE ORIGINAL OF OR EQUAL TO
    { 0x2298, 0x29B8 }, // CIRCLED DIVISION SLASH
    { 0x22A2, 0x22A3 }, // RIGHT TACK
    { 0x22A3, 0x22A2 }, // LEFT TACK
    { 0x22A6, 0x2ADE }, // ASSERTION
    { 0x22A8, 0x2AE4 }, // TRUE
    { 0x22A9, 0x2AE3 }, // FORCES
    { 0x22AB, 0x2AE5 }, // DOUBLE VERTICAL BAR DOUBLE RIGHT TURNSTILE
    { 0x22B0, 0x22B1 }, // PRECEDES UNDER RELATION
    { 0x22B1, 0x22B0 }, // SUCCEEDS UNDER RELATION
    { 0x22B2, 0x22B3 }, // NORMAL SUBGROUP OF
    { 0x22B3, 0x22B2 }, // CONTAINS AS NORMAL SUBGROUP
    { 0x22B4, 0x22B5 }, // NORMAL SUBGROUP OF OR EQUAL TO
    { 0x22B5, 0x22B4 }, // CONTAINS AS NORMAL SUBGROUP OR EQUAL TO
    { 0x22B6, 0x22B7 }, // ORIGINAL OF
    { 0x22B7, 0x22B6 }, // IMAGE OF
    { 0x22B8, 0x27DC }, // MULTIMAP
    { 0x22C9, 0x22CA }, // LEFT NORMAL FACTOR SEMIDIRECT PRODUCT
    { 0x22CA, 0x22C9 }, // RIGHT NORMAL FACTOR SEMIDIRECT PRODUCT
    { 0x22CB, 0x22CC }, // LEFT SEMIDIRECT PRODUCT
    { 0x22CC, 0x22CB }, // RIGHT SEMIDIRECT PRODUCT
    { 0x22CD, 0x2243 }, // REVERSED TILDE EQUALS
    { 0x22D0, 0x22D1 }, // DOUBLE SUBSET
    { 0x22D1, 0x22D0 }, // DOUBLE SUPERSET
    { 0x22D6, 0x22D7 }, // LESS-THAN WITH DOT
    { 0x22D7, 0x22D6 }, // GREATER-THAN WITH DOT
    { 0x22D8, 0x22D9 }, // VERY MUCH LESS-THAN
    { 0x22D9, 0x22D8 }, // VERY MUCH GREATER-THAN
    { 0x22DA, 0x22DB }, // LESS-THAN EQUAL TO OR GREATER-THAN
    { 0x22DB, 0x22DA }, // GREATER-THAN EQUAL TO OR LESS-THAN
    { 0x22DC, 0x22DD }, // EQUAL TO OR LESS-THAN
    { 0x22DD, 0x22DC }, // EQUAL TO OR GREATER-THAN
    { 0x22DE, 0x22DF }, // EQUAL TO OR PRECEDES
    { 0x22DF, 0x22DE }, // EQUAL TO OR SUCCEEDS
    { 0x22E0, 0x22E1 }, // [BEST FIT] DOES NOT PRECEDE OR EQUAL
    { 0x22E1, 0x22E0 }, // [BEST FIT] DOES NOT SUCCEED OR EQUAL
    { 0x22E2, 0x22E3 }, // [BEST FIT] NOT SQUARE IMAGE OF OR EQUAL TO
    { 0x22E3, 0x22E2 }, // [BEST FIT] NOT SQUARE ORIGINAL OF OR EQUAL TO
    { 0x22E4, 0x22E5 }, // [BEST FIT] SQUARE IMAGE OF OR NOT EQUAL TO
    { 0x22E5, 0x22E4 }, // [BEST FIT] SQUARE ORIGINAL OF OR NOT EQUAL TO
    { 0x22E6, 0x22E7 }, // [BEST FIT] LESS-THAN BUT NOT EQUIVALENT TO
    { 0x22E7, 0x22E6 }, // [BEST FIT] GREATER-THAN BUT NOT EQUIVALENT TO
    { 0x22E8, 0x22E9 }, // [BEST FIT] PRECEDES BUT NOT EQUIVALENT TO
    { 0x22E9, 0x22E8 }, // [BEST FIT] SUCCEEDS BUT NOT EQUIVALENT TO
    { 0x22EA, 0x22EB }, // [BEST FIT] NOT NORMAL SUBGROUP OF
    { 0x22EB, 0x22EA }, // [BEST FIT] DOES NOT CONTAIN AS NORMAL SUBGROUP
    { 0x22EC, 0x22ED }, // [BEST FIT] NOT NORMAL SUBGROUP OF OR EQUAL TO
    { 0x22ED, 0x22EC }, // [BEST FIT] DOES NOT CONTAIN AS NORMAL SUBGROUP OR EQUAL
    { 0x22F0, 0x22F1 }, // UP RIGHT DIAGONAL ELLIPSIS
    { 0x22F1, 0x22F0 }, // DOWN RIGHT DIAGONAL ELLIPSIS
    { 0x22F2, 0x22FA }, // ELEMENT OF WITH LONG HORIZONTAL STROKE
    { 0x22F3, 0x22FB }, // ELEMENT OF WITH VERTICAL BAR AT END OF HORIZONTAL STROKE
    { 0x22F4, 0x22FC }, // SMALL ELEMENT OF WITH VERTICAL BAR AT END OF HORIZONTAL STROKE
    { 0x22F6, 0x22FD }, // ELEMENT OF WITH OVERBAR
    { 0x22F7, 0x22FE }, // SMALL ELEMENT OF WITH OVERBAR
    { 0x22FA, 0x22F2 }, // CONTAINS WITH LONG HORIZONTAL STROKE
    { 0x22FB, 0x22F3 }, // CONTAINS WITH VERTICAL BAR AT END OF HORIZONTAL STROKE
    { 0x22FC, 0x22F4 }, // SMALL CONTAINS WITH VERTICAL BAR AT END OF HORIZONTAL STROKE
    { 0x22FD, 0x22F6 }, // CONTAINS WITH OVERBAR
    { 0x22FE, 0x22F7 }, // SMALL CONTAINS WITH OVERBAR
    { 0x2308, 0x2309 }, // LEFT CEILING
    { 0x2309, 0x2308 }, // RIGHT CEILING
    { 0x230A, 0x230B }, // LEFT FLOOR
    { 0x230B, 0x230A }, // RIGHT FLOOR
    { 0x2329, 0x232A }, // LEFT-POINTING ANGLE BRACKET
    { 0x232A, 0x2329 }, // RIGHT-POINTING ANGLE BRACKET
    { 0x2768, 0x2769 }, // MEDIUM LEFT PARENTHESIS ORNAMENT
    { 0x2769, 0x2768 }, // MEDIUM RIGHT PARENTHESIS ORNAMENT
    { 0x276A, 0x276B }, // MEDIUM FLATTENED LEFT PARENTHESIS ORNAMENT
    { 0x276B, 0x276A }, // MEDIUM FLATTENED RIGHT PARENTHESIS ORNAMENT
    { 0x276C, 0x276D }, // MEDIUM LEFT-POINTING ANGLE BRACKET ORNAMENT
    { 0x276D, 0x276C }, // MEDIUM RIGHT-POINTING ANGLE BRACKET ORNAMENT
    { 0x276E, 0x276F }, // HEAVY LEFT-POINTING ANGLE QUOTATION MARK ORNAMENT
    { 0x276F, 0x276E }, // HEAVY RIGHT-POINTING ANGLE QUOTATION MARK ORNAMENT
    { 0x2770, 0x2771 }, // HEAVY LEFT-POINTING ANGLE BRACKET ORNAMENT
    { 0x2771, 0x2770 }, // HEAVY RIGHT-POINTING ANGLE BRACKET ORNAMENT
    { 0x2772, 0x2773 }, // LIGHT LEFT TORTOISE SHELL BRACKET ORNAMENT
    { 0x2773, 0x2772 }, // LIGHT RIGHT TORTOISE SHELL BRACKET ORNAMENT
    { 0x2774, 0x2775 }, // MEDIUM LEFT CURLY BRACKET ORNAMENT
    { 0x2775, 0x2774 }, // MEDIUM RIGHT CURLY BRACKET ORNAMENT
    { 0x27C3, 0x27C4 }, // OPEN SUBSET
    { 0x27C4, 0x27C3 }, // OPEN SUPERSET
    { 0x27C5, 0x27C6 }, // LEFT S-SHAPED BAG DELIMITER
    { 0x27C6, 0x27C5 }, // RIGHT S-SHAPED BAG DELIMITER
    { 0x27C8, 0x27C9 }, // REVERSE SOLIDUS PRECEDING SUBSET
    { 0x27C9, 0x27C8 }, // SUPERSET PRECEDING SOLIDUS
    { 0x27CB, 0x27CD }, // MATHEMATICAL RISING DIAGONAL
    { 0x27CD, 0x27CB }, // MATHEMATICAL FALLING DIAGONAL
    { 0x27D5, 0x27D6 }, // LEFT OUTER JOIN
    { 0x27D6, 0x27D5 }, // RIGHT OUTER JOIN
    { 0x27DC, 0x22B8 }, // LEFT MULTIMAP
    { 0x27DD, 0x27DE }, // LONG RIGHT TACK
    { 0x27DE, 0x27DD }, // LONG LEFT TACK
    { 0x27E2, 0x27E3 }, // WHITE CONCAVE-SIDED DIAMOND WITH LEFTWARDS TICK
    { 0x27E3, 0x27E2 }, // WHITE CONCAVE-SIDED DIAMOND WITH RIGHTWARDS TICK
    { 0x27E4, 0x27E5 }, // WHITE SQUARE WITH LEFTWARDS TICK
    { 0x27E5, 0x27E4 }, // WHITE SQUARE WITH RIGHTWARDS TICK
    { 0x27E6, 0x27E7 }, // MATHEMATICAL LEFT WHITE SQUARE BRACKET
    { 0x27E7, 0x27E6 }, // MATHEMATICAL RIGHT WHITE SQUARE BRACKET
    { 0x27E8, 0x27E9 }, // MATHEMATICAL LEFT ANGLE BRACKET
    { 0x27E9, 0x27E8 }, // MATHEMATICAL RIGHT ANGLE BRACKET
    { 0x27EA, 0x27EB }, // MATHEMATICAL LEFT DOUBLE ANGLE BRACKET
    { 0x27EB, 0x27EA }, // MATHEMATICAL RIGHT DOUBLE ANGLE BRACKET
    { 0x27EC, 0x27ED }, // MATHEMATICAL LEFT WHITE TORTOISE SHELL BRACKET
    { 0x27ED, 0x27EC }, // MATHEMATICAL RIGHT WHITE TORTOISE SHELL BRACKET
    { 0x27EE, 0x27EF }, // MATHEMATICAL LEFT FLATTENED PARENTHESIS
    { 0x27EF, 0x27EE }, // MATHEMATICAL RIGHT FLATTENED PARENTHESIS
    { 0x2983, 0x2984 }, // LEFT WHITE CURLY BRACKET
    { 0x2984, 0x2983 }, // RIGHT WHITE CURLY BRACKET
    { 0x2985, 0x2986 }, // LEFT WHITE PARENTHESIS
    { 0x2986, 0x2985 }, // RIGHT WHITE PARENTHESIS
    { 0x2987, 0x2988 }, // Z NOTATION LEFT IMAGE BRACKET
    { 0x2988, 0x2987 }, // Z NOTATION RIGHT IMAGE BRACKET
    { 0x2989, 0x298A }, // Z NOTATION LEFT BINDING BRACKET
    { 0x298A, 0x2989 }, // Z NOTATION RIGHT BINDING BRACKET
    { 0x298B, 0x298C }, // LEFT SQUARE BRACKET WITH UNDERBAR
    { 0x298C, 0x298B }, // RIGHT SQUARE BRACKET WITH UNDERBAR
    { 0x298D, 0x2990 }, // LEFT SQUARE BRACKET WITH TICK IN TOP CORNER
    { 0x298E, 0x298F }, // RIGHT SQUARE BRACKET WITH TICK IN BOTTOM CORNER
    { 0x298F, 0x298E }, // LEFT SQUARE BRACKET WITH TICK IN BOTTOM CORNER
    { 0x2990, 0x298D }, // RIGHT SQUARE BRACKET WITH TICK IN TOP CORNER
    { 0x2991, 0x2992 }, // LEFT ANGLE BRACKET WITH DOT
    { 0x2992, 0x2991 }, // RIGHT ANGLE BRACKET WITH DOT
    { 0x2993, 0x2994 }, // LEFT ARC LESS-THAN BRACKET
    { 0x2994, 0x2993 }, // RIGHT ARC GREATER-THAN BRACKET
    { 0x2995, 0x2996 }, // DOUBLE LEFT ARC GREATER-THAN BRACKET
    { 0x2996, 0x2995 }, // DOUBLE RIGHT ARC LESS-THAN BRACKET
    { 0x2997, 0x2998 }, // LEFT BLACK TORTOISE SHELL BRACKET
    { 0x2998, 0x2997 }, // RIGHT BLACK TORTOISE SHELL BRACKET
    { 0x299B, 0x2221 }, // MEASURED ANGLE OPENING LEFT
    { 0x29A0, 0x2222 }, // SPHERICAL ANGLE OPENING LEFT
    { 0x29A3, 0x2220 }, // REVERSED ANGLE
    { 0x29A4, 0x29A5 }, // ANGLE WITH UNDERBAR
    { 0x29A5, 0x29A4 }, // REVERSED ANGLE WITH UNDERBAR
    { 0x29A8, 0x29A9 }, // MEASURED ANGLE WITH OPEN ARM ENDING IN ARROW POINTING UP AND RIGHT
    { 0x29A9, 0x29A8 }, // MEASURED ANGLE WITH OPEN ARM ENDING IN ARROW POINTING UP AND LEFT
    { 0x29AA, 0x29AB }, // MEASURED ANGLE WITH OPEN ARM ENDING IN ARROW POINTING DOWN AND RIGHT
    { 0x29AB, 0x29AA }, // MEASURED ANGLE WITH OPEN ARM ENDING IN ARROW POINTING DOWN AND LEFT
    { 0x29AC, 0x29AD }, // MEASURED ANGLE WITH OPEN ARM ENDING IN ARROW POINTING RIGHT AND UP
    { 0x29AD, 0x29AC }, // MEASURED ANGLE WITH OPEN ARM ENDING IN ARROW POINTING LEFT AND UP
    { 0x29AE, 0x29AF }, // MEASURED ANGLE WITH OPEN ARM ENDING IN ARROW POINTING RIGHT AND DOWN
    { 0x29AF, 0x29AE }, // MEASURED ANGLE WITH OPEN ARM ENDING IN ARROW POINTING LEFT AND DOWN
    { 0x29B8, 0x2298 }, // CIRCLED REVERSE SOLIDUS
    { 0x29C0, 0x29C1 }, // CIRCLED LESS-THAN
    { 0x29C1, 0x29C0 }, // CIRCLED GREATER-THAN
    { 0x29C4, 0x29C5 }, // SQUARED RISING DIAGONAL SLASH
    { 0x29C5, 0x29C4 }, // SQUARED FALLING DIAGONAL SLASH
    { 0x29CF, 0x29D0 }, // LEFT TRIANGLE BESIDE VERTICAL BAR
    { 0x29D0, 0x29CF }, // VERTICAL BAR BESIDE RIGHT TRIANGLE
    { 0x29D1, 0x29D2 }, // BOWTIE WITH LEFT HALF BLACK
    { 0x29D2, 0x29D1 }, // BOWTIE WITH RIGHT HALF BLACK
    { 0x29D4, 0x29D5 }, // TIMES WITH LEFT HALF BLACK
    { 0x29D5, 0x29D4 }, // TIMES WITH RIGHT HALF BLACK
    { 0x29D8, 0x29D9 }, // LEFT WIGGLY FENCE
    { 0x29D9, 0x29D8 }, // RIGHT WIGGLY FENCE
    { 0x29DA, 0x29DB }, // LEFT DOUBLE WIGGLY FENCE
    { 0x29DB, 0x29DA }, // RIGHT DOUBLE WIGGLY FENCE
    { 0x29E8, 0x29E9 }, // DOWN-POINTING TRIANGLE WITH LEFT HALF BLACK
    { 0x29E9, 0x29E8 }, // DOWN-POINTING TRIANGLE WITH RIGHT HALF BLACK
    { 0x29F5, 0x2215 }, // REVERSE SOLIDUS OPERATOR
    { 0x29F8, 0x29F9 }, // BIG SOLIDUS
    { 0x29F9, 0x29F8 }, // BIG REVERSE SOLIDUS
    { 0x29FC, 0x29FD }, // LEFT-POINTING CURVED ANGLE BRACKET
    { 0x29FD, 0x29FC }, // RIGHT-POINTING CURVED ANGLE BRACKET
    { 0x2A2B, 0x2A2C }, // MINUS SIGN WITH FALLING DOTS
    { 0x2A2C, 0x2A2B }, // MINUS SIGN WITH RISING DOTS
    { 0x2A2D, 0x2A2E }, // PLUS SIGN IN LEFT HALF CIRCLE
    { 0x2A2E, 0x2A2D }, // PLUS SIGN IN RIGHT HALF CIRCLE
    { 0x2A34, 0x2A35 }, // MULTIPLICATION SIGN IN LEFT HALF CIRCLE
    { 0x2A35, 0x2A34 }, // MULTIPLICATION SIGN IN RIGHT HALF CIRCLE
    { 0x2A3C, 0x2A3D }, // INTERIOR PRODUCT
    { 0x2A3D, 0x2A3C }, // RIGHTHAND INTERIOR PRODUCT
    { 0x2A64, 0x2A65 }, // Z NOTATION DOMAIN ANTIRESTRICTION
    { 0x2A65, 0x2A64 }, // Z NOTATION RANGE ANTIRESTRICTION
    { 0x2A79, 0x2A7A }, // LESS-THAN WITH CIRCLE INSIDE
    { 0x2A7A, 0x2A79 }, // GREATER-THAN WITH CIRCLE INSIDE
    { 0x2A7B, 0x2A7C }, // [BEST FIT] LESS-THAN WITH QUESTION MARK ABOVE
    { 0x2A7C, 0x2A7B }, // [BEST FIT] GREATER-THAN WITH QUESTION MARK ABOVE
    { 0x2A7D, 0x2A7E }, // LESS-THAN OR SLANTED EQUAL TO
    { 0x2A7E, 0x2A7D }, // GREATER-THAN OR SLANTED EQUAL TO
    { 0x2A7F, 0x2A80 }, // LESS-THAN OR SLANTED EQUAL TO WITH DOT INSIDE
    { 0x2A80, 0x2A7F }, // GREATER-THAN OR SLANTED EQUAL TO WITH DOT INSIDE
    { 0x2A81, 0x2A82 }, // LESS-THAN OR SLANTED EQUAL TO WITH DOT ABOVE
    { 0x2A82, 0x2A81 }, // GREATER-THAN OR SLANTED EQUAL TO WITH DOT ABOVE
    { 0x2A83, 0x2A84 }, // LESS-THAN OR SLANTED EQUAL TO WITH DOT ABOVE RIGHT
    { 0x2A84, 0x2A83 }, // GREATER-THAN OR SLANTED EQUAL TO WITH DOT ABOVE LEFT
    { 0x2A85, 0x2A86 }, // [BEST FIT] LESS-THAN OR APPROXIMATE
    { 0x2A86, 0x2A85 }, // [BEST FIT] GREATER-THAN OR APPROXIMATE
    { 0x2A87, 0x2A88 }, // [BEST FIT] LESS-THAN AND SINGLE-LINE NOT EQUAL TO
    { 0x2A88, 0x2A87 }, // [BEST FIT] GREATER-THAN AND SINGLE-LINE NOT EQUAL TO
    { 0x2A89, 0x2A8A }, // [BEST FIT] LESS-THAN AND NOT APPROXIMATE
    { 0x2A8A, 0x2A89 }, // [BEST FIT] GREATER-THAN AND NOT APPROXIMATE
    { 0x2A8B, 0x2A8C }, // LESS-THAN ABOVE DOUBLE-LINE EQUAL ABOVE GREATER-THAN
    { 0x2A8C, 0x2A8B }, // GREATER-THAN ABOVE DOUBLE-LINE EQUAL ABOVE LESS-THAN
    { 0x2A8D, 0x2A8E }, // [BEST FIT] LESS-THAN ABOVE SIMILAR OR EQUAL
    { 0x2A8E, 0x2A8D }, // [BEST FIT] GREATER-THAN ABOVE SIMILAR OR EQUAL
    { 0x2A8F, 0x2A90 }, // [BEST FIT] LESS-THAN ABOVE SIMILAR ABOVE GREATER-THAN
    { 0x2A90, 0x2A8F }, // [BEST FIT] GREATER-THAN ABOVE SIMILAR ABOVE LESS-THAN
    { 0x2A91, 0x2A92 }, // LESS-THAN ABOVE GREATER-THAN ABOVE DOUBLE-LINE EQUAL
    { 0x2A92, 0x2A91 }, // GREATER-THAN ABOVE LESS-THAN ABOVE DOUBLE-LINE EQUAL
    { 0x2A93, 0x2A94 }, // LESS-THAN ABOVE SLANTED EQUAL ABOVE GREATER-THAN ABOVE SLANTED EQUAL
    { 0x2A94, 0x2A93 }, // GREATER-THAN ABOVE SLANTED EQUAL ABOVE LESS-THAN ABOVE SLANTED EQUAL
    { 0x2A95, 0x2A96 }, // SLANTED EQUAL TO OR LESS-THAN
    { 0x2A96, 0x2A95 }, // SLANTED EQUAL TO OR GREATER-THAN
    { 0x2A97, 0x2A98 }, // SLANTED EQUAL TO OR LESS-THAN WITH DOT INSIDE
    { 0x2A98, 0x2A97 }, // SLANTED EQUAL TO OR GREATER-THAN WITH DOT INSIDE
    { 0x2A99, 0x2A9A }, // DOUBLE-LINE EQUAL TO OR LESS-THAN
    { 0x2A9A, 0x2A99 }, // DOUBLE-LINE EQUAL TO OR GREATER-THAN
    { 0x2A9B, 0x2A9C }, // DOUBLE-LINE SLANTED EQUAL TO OR LESS-THAN
    { 0x2A9C, 0x2A9B }, // DOUBLE-LINE SLANTED EQUAL TO OR GREATER-THAN
    { 0x2A9D, 0x2A9E }, // [BEST FIT] SIMILAR OR LESS-THAN
    { 0x2A9E, 0x2A9D }, // [BEST FIT] SIMILAR OR GREATER-THAN
    { 0x2A9F, 0x2AA0 }, // [BEST FIT] SIMILAR ABOVE LESS-THAN ABOVE EQUALS SIGN
    { 0x2AA0, 0x2A9F }, // [BEST FIT] SIMILAR ABOVE GREATER-THAN ABOVE EQUALS SIGN
    { 0x2AA1, 0x2AA2 }, // DOUBLE NESTED LESS-THAN
    { 0x2AA2, 0x2AA1 }, // DOUBLE NESTED GREATER-THAN
    { 0x2AA6, 0x2AA7 }, // LESS-THAN CLOSED BY CURVE
    { 0x2AA7, 0x2AA6 }, // GREATER-THAN CLOSED BY CURVE
    { 0x2AA8, 0x2AA9 }, // LESS-THAN CLOSED BY CURVE ABOVE SLANTED EQUAL
    { 0x2AA9, 0x2AA8 }, // GREATER-THAN CLOSED BY CURVE ABOVE SLANTED EQUAL
    { 0x2AAA, 0x2AAB }, // SMALLER THAN
    { 0x2AAB, 0x2AAA }, // LARGER THAN
    { 0x2AAC, 0x2AAD }, // SMALLER THAN OR EQUAL TO
    { 0x2AAD, 0x2AAC }, // LARGER THAN OR EQUAL TO
    { 0x2AAF, 0x2AB0 }, // PRECEDES ABOVE SINGLE-LINE EQUALS SIGN
    { 0x2AB0, 0x2AAF }, // SUCCEEDS ABOVE SINGLE-LINE EQUALS SIGN
    { 0x2AB1, 0x2AB2 }, // [BEST FIT] PRECEDES ABOVE SINGLE-LINE NOT EQUAL TO
    { 0x2AB2, 0x2AB1 }, // [BEST FIT] SUCCEEDS ABOVE SINGLE-LINE NOT EQUAL TO
    { 0x2AB3, 0x2AB4 }, // PRECEDES ABOVE EQUALS SIGN
    { 0x2AB4, 0x2AB3 }, // SUCCEEDS ABOVE EQUALS SIGN
    { 0x2AB5, 0x2AB6 }, // [BEST FIT] PRECEDES ABOVE NOT EQUAL TO
    { 0x2AB6, 0x2AB5 }, // [BEST FIT] SUCCEEDS ABOVE NOT EQUAL TO
    { 0x2AB7, 0x2AB8 }, // [BEST FIT] PRECEDES ABOVE ALMOST EQUAL TO
    { 0x2AB8, 0x2AB7 }, // [BEST FIT] SUCCEEDS ABOVE ALMOST EQUAL TO
    { 0x2AB9, 0x2ABA }, // [BEST FIT] PRECEDES ABOVE NOT ALMOST EQUAL TO
    { 0x2ABA, 0x2AB9 }, // [BEST FIT] SUCCEEDS ABOVE NOT ALMOST EQUAL TO
    { 0x2ABB, 0x2ABC }, // DOUBLE PRECEDES
    { 0x2ABC, 0x2ABB }, // DOUBLE SUCCEEDS
    { 0x2ABD, 0x2ABE }, // SUBSET WITH DOT
    { 0x2ABE, 0x2ABD }, // SUPERSET WITH DOT
    { 0x2ABF, 0x2AC0 }, // SUBSET WITH PLUS SIGN BELOW
    { 0x2AC0, 0x2ABF }, // SUPERSET WITH PLUS SIGN BELOW
    { 0x2AC1, 0x2AC2 }, // SUBSET WITH MULTIPLICATION SIGN BELOW
    { 0x2AC2, 0x2AC1 }, // SUPERSET WITH MULTIPLICATION SIGN BELOW
    { 0x2AC3, 0x2AC4 }, // SUBSET OF OR EQUAL TO WITH DOT ABOVE
    { 0x2AC4, 0x2AC3 }, // SUPERSET OF OR EQUAL TO WITH DOT ABOVE
    { 0x2AC5, 0x2AC6 }, // SUBSET OF ABOVE EQUALS SIGN
    { 0x2AC6, 0x2AC5 }, // SUPERSET OF ABOVE EQUALS SIGN
    { 0x2AC7, 0x2AC8 }, // [BEST FIT] SUBSET OF ABOVE TILDE OPERATOR
    { 0x2AC8, 0x2AC7 }, // [BEST FIT] SUPERSET OF ABOVE TILDE OPERATOR
    { 0x2AC9, 0x2ACA }, // [BEST FIT] SUBSET OF ABOVE ALMOST EQUAL TO
    { 0x2ACA, 0x2AC9 }, // [BEST FIT] SUPERSET OF ABOVE ALMOST EQUAL TO
    { 0x2ACB, 0x2ACC }, // [BEST FIT] SUBSET OF ABOVE NOT EQUAL TO
    { 0x2ACC, 0x2ACB }, // [BEST FIT] SUPERSET OF ABOVE NOT EQUAL TO
    { 0x2ACD, 0x2ACE }, // SQUARE LEFT OPEN BOX OPERATOR
    { 0x2ACE, 0x2ACD }, // SQUARE RIGHT OPEN BOX OPERATOR
    { 0x2ACF, 0x2AD0 }, // CLOSED SUBSET
    { 0x2AD0, 0x2ACF }, // CLOSED SUPERSET
    { 0x2AD1, 0x2AD2 }, // CLOSED SUBSET OR EQUAL TO
    { 0x2AD2, 0x2AD1 }, // CLOSED SUPERSET OR EQUAL TO
    { 0x2AD3, 0x2AD4 }, // SUBSET ABOVE SUPERSET
    { 0x2AD4, 0x2AD3 }, // SUPERSET ABOVE SUBSET
    { 0x2AD5, 0x2AD6 }, // SUBSET ABOVE SUBSET
    { 0x2AD6, 0x2AD5 }, // SUPERSET ABOVE SUPERSET
    { 0x2ADE, 0x22A6 }, // SHORT LEFT TACK
    { 0x2AE3, 0x22A9 }, // DOUBLE VERTICAL BAR LEFT TURNSTILE
    { 0x2AE4, 0x22A8 }, // VERTICAL BAR DOUBLE LEFT TURNSTILE
    { 0x2AE5, 0x22AB }, // DOUBLE VERTICAL BAR DOUBLE LEFT TURNSTILE
    { 0x2AEC, 0x2AED }, // DOUBLE STROKE NOT SIGN
    { 0x2AED, 0x2AEC }, // REVERSED DOUBLE STROKE NOT SIGN
    { 0x2AEE, 0x2224 }, // DOES NOT DIVIDE WITH REVERSED NEGATION SLASH
    { 0x2AF7, 0x2AF8 }, // TRIPLE NESTED LESS-THAN
    { 0x2AF8, 0x2AF7 }, // TRIPLE NESTED GREATER-THAN
    { 0x2AF9, 0x2AFA }, // DOUBLE-LINE SLANTED LESS-THAN OR EQUAL TO
    { 0x2AFA, 0x2AF9 }, // DOUBLE-LINE SLANTED GREATER-THAN OR EQUAL TO
    { 0x2BFE, 0x221F }, // REVERSED RIGHT ANGLE
    { 0x2E02, 0x2E03 }, // LEFT SUBSTITUTION BRACKET
    { 0x2E03, 0x2E02 }, // RIGHT SUBSTITUTION BRACKET
    { 0x2E04, 0x2E05 }, // LEFT DOTTED SUBSTITUTION BRACKET
    { 0x2E05, 0x2E04 }, // RIGHT DOTTED SUBSTITUTION BRACKET
    { 0x2E09, 0x2E0A }, // LEFT TRANSPOSITION BRACKET
    { 0x2E0A, 0x2E09 }, // RIGHT TRANSPOSITION BRACKET
    { 0x2E0C, 0x2E0D }, // LEFT RAISED OMISSION BRACKET
    { 0x2E0D, 0x2E0C }, // RIGHT RAISED OMISSION BRACKET
    { 0x2E1C, 0x2E1D }, // LEFT LOW PARAPHRASE BRACKET
    { 0x2E1D, 0x2E1C }, // RIGHT LOW PARAPHRASE BRACKET
    { 0x2E20, 0x2E21 }, // LEFT VERTICAL BAR WITH QUILL
    { 0x2E21, 0x2E20 }, // RIGHT VERTICAL BAR WITH QUILL
    { 0x2E22, 0x2E23 }, // TOP LEFT HALF BRACKET
    { 0x2E23, 0x2E22 }, // TOP RIGHT HALF BRACKET
    { 0x2E24, 0x2E25 }, // BOTTOM LEFT HALF BRACKET
    { 0x2E25, 0x2E24 }, // BOTTOM RIGHT HALF BRACKET
    { 0x2E26, 0x2E27 }, // LEFT SIDEWAYS U BRACKET
    { 0x2E27, 0x2E26 }, // RIGHT SIDEWAYS U BRACKET
    { 0x2E28, 0x2E29 }, // LEFT DOUBLE PARENTHESIS
    { 0x2E29, 0x2E28 }, // RIGHT DOUBLE PARENTHESIS
    { 0x2E55, 0x2E56 }, // LEFT SQUARE BRACKET WITH STROKE
    { 0x2E56, 0x2E55 }, // RIGHT SQUARE BRACKET WITH STROKE
    { 0x2E57, 0x2E58 }, // LEFT SQUARE BRACKET WITH DOUBLE STROKE
    { 0x2E58, 0x2E57 }, // RIGHT SQUARE BRACKET WITH DOUBLE STROKE
    { 0x2E59, 0x2E5A }, // TOP HALF LEFT PARENTHESIS
    { 0x2E5A, 0x2E59 }, // TOP HALF RIGHT PARENTHESIS
    { 0x2E5B, 0x2E5C }, // BOTTOM HALF LEFT PARENTHESIS
    { 0x2E5C, 0x2E5B }, // BOTTOM HALF RIGHT PARENTHESIS
    { 0x3008, 0x3009 }, // LEFT ANGLE BRACKET
    { 0x3009, 0x3008 }, // RIGHT ANGLE BRACKET
    { 0x300A, 0x300B }, // LEFT DOUBLE ANGLE BRACKET
    { 0x300B, 0x300A }, // RIGHT DOUBLE ANGLE BRACKET
    { 0x300C, 0x300D }, // [BEST FIT] LEFT CORNER BRACKET
    { 0x300D, 0x300C }, // [BEST FIT] RIGHT CORNER BRACKET
    { 0x300E, 0x300F }, // [BEST FIT] LEFT WHITE CORNER BRACKET
    { 0x300F, 0x300E }, // [BEST FIT] RIGHT WHITE CORNER BRACKET
    { 0x3010, 0x3011 }, // LEFT BLACK LENTICULAR BRACKET
    { 0x3011, 0x3010 }, // RIGHT BLACK LENTICULAR BRACKET
    { 0x3014, 0x3015 }, // LEFT TORTOISE SHELL BRACKET
    { 0x3015, 0x3014 }, // RIGHT TORTOISE SHELL BRACKET
    { 0x3016, 0x3017 }, // LEFT WHITE LENTICULAR BRACKET
    { 0x3017, 0x3016 }, // RIGHT WHITE LENTICULAR BRACKET
    { 0x3018, 0x3019 }, // LEFT WHITE TORTOISE SHELL BRACKET
    { 0x3019, 0x3018 }, // RIGHT WHITE TORTOISE SHELL BRACKET
    { 0x301A, 0x301B }, // LEFT WHITE SQUARE BRACKET
    { 0x301B, 0x301A }, // RIGHT WHITE SQUARE BRACKET
    { 0xFE59, 0xFE5A }, // SMALL LEFT PARENTHESIS
    { 0xFE5A, 0xFE59 }, // SMALL RIGHT PARENTHESIS
    { 0xFE5B, 0xFE5C }, // SMALL LEFT CURLY BRACKET
    { 0xFE5C, 0xFE5B }, // SMALL RIGHT CURLY BRACKET
    { 0xFE5D, 0xFE5E }, // SMALL LEFT TORTOISE SHELL BRACKET
    { 0xFE5E, 0xFE5D }, // SMALL RIGHT TORTOISE SHELL BRACKET
    { 0xFE64, 0xFE65 }, // SMALL LESS-THAN SIGN
    { 0xFE65, 0xFE64 }, // SMALL GREATER-THAN SIGN
    { 0xFF08, 0xFF09 }, // FULLWIDTH LEFT PARENTHESIS
    { 0xFF09, 0xFF08 }, // FULLWIDTH RIGHT PARENTHESIS
    { 0xFF1C, 0xFF1E }, // FULLWIDTH LESS-THAN SIGN
    { 0xFF1E, 0xFF1C }, // FULLWIDTH GREATER-THAN SIGN
    { 0xFF3B, 0xFF3D }, // FULLWIDTH LEFT SQUARE BRACKET
    { 0xFF3D, 0xFF3B }, // FULLWIDTH RIGHT SQUARE BRACKET
    { 0xFF5B, 0xFF5D }, // FULLWIDTH LEFT CURLY BRACKET
    { 0xFF5D, 0xFF5B }, // FULLWIDTH RIGHT CURLY BRACKET
    { 0xFF5F, 0xFF60 }, // FULLWIDTH LEFT WHITE PARENTHESIS
    { 0xFF60, 0xFF5F }, // FULLWIDTH RIGHT WHITE PARENTHESIS
    { 0xFF62, 0xFF63 }, // [BEST FIT] HALFWIDTH LEFT CORNER BRACKET
    { 0xFF63, 0xFF62 }, // [BEST FIT] HALFWIDTH RIGHT CORNER BRACKET
    { 0xFFFF, 0xFFFF }, // Boundary
};

char32_t get_mirror_char(char32_t c) {
    const unsigned short (*pp)[2] = mirror_pairs;
    size_t a = 0, b = sizeof(mirror_pairs) / sizeof(mirror_pairs[0]);

    //while (a + 1 < b) {
    // generate branchless code
    int i;
    for (i = 0; i < 9; i++) {
        size_t m = (a + b) / 2;
        if (pp[m][0] > c)
            b = m;
        else
            a = m;
    }
    return (c == pp[a][0]) ? pp[a][1] : c;
}

/* Bidirectional algorithm should be rewritten.
   Heavily modified version from early code by Dov Grobgeld
 */

// XXX: these tables are obsolete, complete rewrite is needed
static const unsigned short bidir_char_type_start[366] = {
    0x0000, 0x0009, 0x000a, 0x000b, 0x000c, 0x000d, 0x000e, 0x001c,
    0x001f, 0x0020, 0x0021, 0x0023, 0x0026, 0x002b, 0x002c, 0x002d,
    0x002e, 0x002f, 0x0030, 0x003a, 0x003b, 0x0041, 0x005b, 0x0061,
    0x007b, 0x007f, 0x0085, 0x0086, 0x00a0, 0x00a1, 0x00a2, 0x00a6,
    0x00aa, 0x00ab, 0x00b0, 0x00b2, 0x00b4, 0x00b5, 0x00b6, 0x00b9,
    0x00ba, 0x00bb, 0x00c0, 0x00d7, 0x00d8, 0x00f7, 0x00f8, 0x02b9,
    0x02bb, 0x02c2, 0x02d0, 0x02d2, 0x02e0, 0x02e5, 0x02ee, 0x0300,
    0x0374, 0x037a, 0x037e, 0x0386, 0x0387, 0x0388, 0x0483, 0x048c,
    0x058a, 0x0591, 0x05be, 0x05bf, 0x05c0, 0x05c1, 0x05c3, 0x05c4,
    0x05d0, 0x0600, 0x061b, 0x064b, 0x0660, 0x066a, 0x066b, 0x066d,
    0x0670, 0x0671, 0x06d6, 0x06e5, 0x06e7, 0x06e9, 0x06ea, 0x06f0,
    0x06fa, 0x070f, 0x0710, 0x0711, 0x0712, 0x0730, 0x0780, 0x07a6,
    0x0800, 0x0900, 0x0903, 0x093c, 0x093d, 0x0941, 0x0949, 0x094d,
    0x0950, 0x0951, 0x0958, 0x0962, 0x0964, 0x0981, 0x0982, 0x09bc,
    0x09be, 0x09c1, 0x09c7, 0x09cd, 0x09d7, 0x09e2, 0x09e6, 0x09f2,
    0x09f4, 0x0a00, 0x0a05, 0x0a3c, 0x0a3e, 0x0a41, 0x0a59, 0x0a70,
    0x0a72, 0x0a81, 0x0a83, 0x0abc, 0x0abd, 0x0ac1, 0x0ac9, 0x0acd,
    0x0ad0, 0x0b00, 0x0b02, 0x0b3c, 0x0b3d, 0x0b3f, 0x0b40, 0x0b41,
    0x0b47, 0x0b4d, 0x0b57, 0x0b82, 0x0b83, 0x0bc0, 0x0bc1, 0x0bcd,
    0x0bd7, 0x0c3e, 0x0c41, 0x0c46, 0x0c60, 0x0cbf, 0x0cc0, 0x0cc6,
    0x0cc7, 0x0ccc, 0x0cd5, 0x0d41, 0x0d46, 0x0d4d, 0x0d57, 0x0dca,
    0x0dcf, 0x0dd2, 0x0dd8, 0x0e31, 0x0e32, 0x0e34, 0x0e3f, 0x0e40,
    0x0e47, 0x0e4f, 0x0eb1, 0x0eb2, 0x0eb4, 0x0ebd, 0x0ec8, 0x0ed0,
    0x0f18, 0x0f1a, 0x0f35, 0x0f36, 0x0f37, 0x0f38, 0x0f39, 0x0f3a,
    0x0f3e, 0x0f71, 0x0f7f, 0x0f80, 0x0f85, 0x0f86, 0x0f88, 0x0f90,
    0x0fbe, 0x0fc6, 0x0fc7, 0x102d, 0x1031, 0x1032, 0x1038, 0x1039,
    0x1040, 0x1058, 0x10a0, 0x1680, 0x1681, 0x169b, 0x16a0, 0x17b7,
    0x17be, 0x17c6, 0x17c7, 0x17c9, 0x17d4, 0x17db, 0x17dc, 0x1800,
    0x180b, 0x1810, 0x18a9, 0x1900, 0x1fbd, 0x1fbe, 0x1fbf, 0x1fc2,
    0x1fcd, 0x1fd0, 0x1fdd, 0x1fe0, 0x1fed, 0x1ff2, 0x1ffd, 0x2000,
    0x200b, 0x200e, 0x200f, 0x2010, 0x2028, 0x2029, 0x202a, 0x202b,
    0x202c, 0x202d, 0x202e, 0x202f, 0x2030, 0x2035, 0x206a, 0x2070,
    0x207a, 0x207c, 0x207f, 0x2080, 0x208a, 0x208c, 0x20a0, 0x20d0,
    0x2100, 0x2102, 0x2103, 0x2107, 0x2108, 0x210a, 0x2114, 0x2115,
    0x2116, 0x2119, 0x211e, 0x2124, 0x2125, 0x2126, 0x2127, 0x2128,
    0x2129, 0x212a, 0x212e, 0x212f, 0x2132, 0x2133, 0x213a, 0x2160,
    0x2190, 0x2212, 0x2214, 0x2336, 0x237b, 0x2395, 0x2396, 0x2460,
    0x249c, 0x24ea, 0x2500, 0x2900, 0x2e00, 0x3000, 0x3001, 0x3005,
    0x3008, 0x3021, 0x302a, 0x3030, 0x3031, 0x3036, 0x3038, 0x303e,
    0x3041, 0x3099, 0x309b, 0x309d, 0x30fb, 0x30fc, 0xa490, 0xa500,
    0xfb1d, 0xfb1e, 0xfb1f, 0xfb29, 0xfb2a, 0xfb50, 0xfd3e, 0xfd50,
    0xfe00, 0xfe30, 0xfe50, 0xfe51, 0xfe52, 0xfe54, 0xfe55, 0xfe56,
    0xfe5f, 0xfe60, 0xfe62, 0xfe64, 0xfe69, 0xfe6b, 0xfe70, 0xfeff,
    0xff00, 0xff03, 0xff06, 0xff0b, 0xff0c, 0xff0d, 0xff0e, 0xff0f,
    0xff10, 0xff1a, 0xff1b, 0xff21, 0xff3b, 0xff41, 0xff5b, 0xff66,
    0xffe0, 0xffe2, 0xffe5, 0xffe8, 0xfff9, 0xfffc,
};

#define LTR  BIDIR_TYPE_LTR   /* Strong Left-to-Right */
#define RTL  BIDIR_TYPE_RTL   /* Right-to-left characters */
#define WL   BIDIR_TYPE_WL    /* Weak left to right */
#define WR   BIDIR_TYPE_WR    /* Weak right to left */
#define EN   BIDIR_TYPE_EN    /* European Numeral */
#define ES   BIDIR_TYPE_ES    /* European number Separator */
#define ET   BIDIR_TYPE_ET    /* European number Terminator */
#define AN   BIDIR_TYPE_AN    /* Arabic Numeral */
#define CS   BIDIR_TYPE_CS    /* Common Separator */
#define BS   BIDIR_TYPE_BS    /* Block Separator */
#define SS   BIDIR_TYPE_SS    /* Segment Separator */
#define WS   BIDIR_TYPE_WS    /* Whitespace */
#define AL   BIDIR_TYPE_AL    /* Arabic Letter */
#define NSM  BIDIR_TYPE_NSM   /* Non Spacing Mark */
#define BN   BIDIR_TYPE_BN    /* Boundary Neutral */
#define ON   BIDIR_TYPE_ON    /* Other Neutral */
#define LRE  BIDIR_TYPE_LRE   /* Left-to-Right Embedding */
#define RLE  BIDIR_TYPE_RLE   /* Right-to-Left Embedding */
#define PDF  BIDIR_TYPE_PDF   /* Pop Directional Flag */
#define LRO  BIDIR_TYPE_LRO   /* Left-to-Right Override */
#define RLO  BIDIR_TYPE_RLO   /* Right-to-Left Override */

static const unsigned char bidir_char_type_val[366] = {
    BN, SS, BS, SS, WS, BS, BN, BS, SS, WS, ON, ET, ON, ET, CS, ET,
    CS, ES, EN, CS, ON, LTR, ON, LTR, ON, BN, BS, BN, CS, ON, ET, ON,
    LTR, ON, ET, EN, ON, LTR, ON, EN, LTR, ON, LTR, ON, LTR, ON, LTR, ON,
    LTR, ON, LTR, ON, LTR, ON, LTR, NSM, ON, LTR, ON, LTR, ON, LTR, NSM, LTR,
    ON, NSM, RTL, NSM, RTL, NSM, RTL, NSM, RTL, CS, AL, NSM, AN, ET, AN, AL,
    NSM, AL, NSM, AL, NSM, ON, NSM, EN, AL, BN, AL, NSM, AL, NSM, AL, NSM,
    LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM,
    LTR, NSM, LTR, NSM, LTR, NSM, LTR, ET, LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM,
    LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM,
    LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM,
    LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM, ET, LTR,
    NSM, LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM, ON,
    LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM, LTR, NSM,
    LTR, NSM, LTR, WS, LTR, ON, LTR, NSM, LTR, NSM, LTR, NSM, LTR, ET, LTR, ON,
    BN, LTR, NSM, LTR, ON, LTR, ON, LTR, ON, LTR, ON, LTR, ON, LTR, ON, WS,
    BN, LTR, RTL, ON, WS, BS, LRE, RLE, PDF, LRO, RLO, WS, ET, ON, BN, EN,
    ET, ON, LTR, EN, ET, ON, ET, NSM, ON, LTR, ON, LTR, ON, LTR, ON, LTR,
    ON, LTR, ON, LTR, ON, LTR, ON, LTR, ON, LTR, ET, LTR, ON, LTR, ON, LTR,
    ON, ET, ON, LTR, ON, LTR, ON, EN, LTR, EN, ON, LTR, ON, WS, ON, LTR,
    ON, LTR, NSM, ON, LTR, ON, LTR, ON, LTR, NSM, ON, LTR, ON, LTR, ON, LTR,
    RTL, NSM, RTL, ET, RTL, AL, ON, AL, NSM, ON, CS, ON, CS, ON, CS, ON,
    ET, ON, ET, ON, ET, ON, AL, BN, ON, ET, ON, ET, CS, ET, CS, ES,
    EN, CS, ON, LTR, ON, LTR, ON, LTR, ET, ON, ET, ON, BN, ON,
};

BidirCharType bidir_get_type(char32_t ch) {
    /* use a binary lookup loop */
    size_t a = 0;
    size_t b = countof(bidir_char_type_start);
    //while (b - a > 1) {
    int i;
    for (i = 0; i < 9; i++) {
        size_t m = (a + b) >> 1;
        if (ch < bidir_char_type_start[m])
            b = m;
        else
            a = m;
    }
    return bidir_char_type_val[a];
}

/* version for test with ASCII chars */
BidirCharType bidir_get_type_test(char32_t ch) {
    if (ch >= 'A' && ch <= 'Z')
        return BIDIR_TYPE_RTL;
    else
        return bidir_get_type(ch);
}

/* Some convenience macros */
#define RL_TYPE(list)   (list)->type
#define RL_LEN(list)    (list)->len
#define RL_POS(list)    (list)->pos
#define RL_LEVEL(list)  (list)->level

static void compact_list(BidirTypeLink *list_tab) {
    BidirTypeLink *p = list_tab;
    BidirTypeLink *q = list_tab;
    for (;;) {
        BidirCharType type = p->type;
        *q = *p++;
        if (type == BIDIR_TYPE_EOT)
            break;
        if (type != BIDIR_TYPE_DEL) {
            while (p->type == type) {
                q->len += p->len;
                p++;
            }
            q++;
        }
    }
}

/* Define a rule macro */

/* Rules for overriding current type */
#define TYPE_RULE1(old_this, new_this)          \
     if (this_type == TYPE_ ## old_this)        \
         RL_TYPE(pp) = BIDIR_TYPE_ ## new_this; \

/* Rules for current and previous type */
#define TYPE_RULE2(old_prev, old_this,             \
                   new_prev, new_this)             \
     if (prev_type == BIDIR_TYPE_ ## old_prev      \
     &&  this_type == BIDIR_TYPE_ ## old_this) {   \
         RL_TYPE(pp-1) = BIDIR_TYPE_ ## new_prev;  \
         RL_TYPE(pp)   = BIDIR_TYPE_ ## new_this;  \
         continue;                                 \
     }

/* A full rule that assigns all three types */
#define TYPE_RULE(old_prev, old_this, old_next,   \
                  new_prev, new_this, new_next)   \
     if (prev_type == BIDIR_TYPE_ ## old_prev     \
     &&  this_type == BIDIR_TYPE_ ## old_this     \
     &&  next_type == BIDIR_TYPE_ ## old_next) {  \
         RL_TYPE(pp-1) = BIDIR_TYPE_ ## new_prev; \
         RL_TYPE(pp)   = BIDIR_TYPE_ ## new_this; \
         RL_TYPE(pp+2) = BIDIR_TYPE_ ## new_next; \
         continue;                                \
     }

/* For optimization the following macro only assigns the center type */
#define TYPE_RULE_C(old_prev, old_this, old_next, \
                    new_this)                     \
     if (prev_type == BIDIR_TYPE_ ## old_prev     \
     &&  this_type == BIDIR_TYPE_ ## old_this     \
     &&  next_type == BIDIR_TYPE_ ## old_next) {  \
         RL_TYPE(pp) = BIDIR_TYPE_ ## new_this;   \
         continue;                                \
     }

#ifdef DEBUG

/*======================================================================
//  For debugging, define some macros for printing the types and the
//  levels.
//----------------------------------------------------------------------*/
static int type_to_char(int type) {
    switch (type) {
    case BIDIR_TYPE_R:  return 'R';
    case BIDIR_TYPE_L:  return 'L';
    case BIDIR_TYPE_E:  return 'E';
    case BIDIR_TYPE_EN: return 'n';
    case BIDIR_TYPE_N:  return 'N';
    default:            return '?';
    }
}

static void print_types_re(const BidirTypeLink *pp, int level) {
    for (pp++; pp->type != BIDIR_TYPE_EOT; pp++) {
        printf("%d:%c(%d)[%d] ", RL_POS(pp), type_to_char(RL_TYPE(pp)), RL_LEN(pp), level ? RL_LEVEL(pp) : 0);
    }
    printf("\n");
}

#endif

#define STACK_SIZE 64

#define ONE_OF_2(t,a,b)    ((1L << (t)) & ((1L << (a)) | (1L << (b))))
#define ONE_OF_3(t,a,b,c)    \
        ((1L << (t)) & ((1L << (a)) | (1L << (b)) | (1L << (c))))
#define ONE_OF_4(t,a,b,c,d)  \
        ((1L << (t)) & ((1L << (a)) | (1L << (b)) | (1L << (c)) | (1L << (d))))
#define ONE_OF_6(t,a,b,c,d,e,f)  \
        ((1L << (t)) & ((1L << (a)) | (1L << (b)) | (1L << (c)) | \
                        (1L << (d)) | (1L << (e)) | (1L << (f))))

/*======================================================================
//  This function should follow the Unicode specification closely!
//
//  It is still lacking the support for <RLO> and <LRO>.
//----------------------------------------------------------------------*/
void bidir_analyze_string(BidirTypeLink *type_rl_list,
                          BidirCharType *pbase_dir,
                          int *pmax_level)
{
    int base_level, base_dir;
    int max_level, level, override;
    int last_strong;
    BidirTypeLink *pp;
    unsigned char stack_level[STACK_SIZE];
    unsigned char stack_override[STACK_SIZE];
    int stack_index;

#ifdef DEBUG
    print_types_re(type_rl_list, 0);
#endif

    /* Find the base level */
    if (*pbase_dir == BIDIR_TYPE_L) {
        base_dir = BIDIR_TYPE_L;
        base_level = 0;
    } else
    if (*pbase_dir == BIDIR_TYPE_R) {
        base_dir = BIDIR_TYPE_R;
        base_level = 1;
    } else {
        /* Search for first strong character and use its direction as base
           direction */
        base_level = 0;         /* Default */
        base_dir = BIDIR_TYPE_N;
        for (pp = type_rl_list; pp->type != BIDIR_TYPE_EOT; pp++) {
            int type;
            type = RL_TYPE(pp);
            if (ONE_OF_3(type, BIDIR_TYPE_R, BIDIR_TYPE_RLE, BIDIR_TYPE_RLO)) {
                base_level = 1;
                base_dir = BIDIR_TYPE_R;
                break;
            } else
            if (ONE_OF_3(type, BIDIR_TYPE_L, BIDIR_TYPE_LRE, BIDIR_TYPE_LRO)) {
                base_level = 0;
                base_dir = BIDIR_TYPE_L;
                break;
            }
        }

        /* If no strong base_dir was found, resort to the weak direction
         * that was passed on input.
         */
        if (base_dir == BIDIR_TYPE_N) {
            if (*pbase_dir == BIDIR_TYPE_WR) {
                base_dir = BIDIR_TYPE_RTL;
                base_level = 1;
            } else
            if (*pbase_dir == BIDIR_TYPE_WL) {
                base_dir = BIDIR_TYPE_LTR;
                base_level = 0;
            }
        }
    }

    /* hack for RLE/LRE/RLO/LRO/PDF. It is not complete in case of
       errors or with neutrals. Need more work. */
    level = base_level;
    override = BIDIR_TYPE_N;
    stack_index = 0;
    for (pp = type_rl_list + 1; pp->type != BIDIR_TYPE_EOT; pp++) {
        int type, i;
        type = RL_TYPE(pp);
        if (ONE_OF_4(type, BIDIR_TYPE_LRE, BIDIR_TYPE_RLE, BIDIR_TYPE_LRO, BIDIR_TYPE_RLO)) {
            for (i = 0; i < RL_LEN(pp); i++) {
                if (stack_index < STACK_SIZE) {
                    /* push level & override */
                    stack_level[stack_index] = level;
                    stack_override[stack_index] = override;
                    stack_index++;
                    /* compute new level */
                    if (ONE_OF_2(type, BIDIR_TYPE_LRE, BIDIR_TYPE_LRO)) {
                        level = (level + 2) & ~1; /* least greater even */
                    } else {
                        level = (level + 1) | 1; /* least greater odd */
                    }
                    /* compute new override */
                    if (type == BIDIR_TYPE_LRO)
                        override = BIDIR_TYPE_L;
                    else
                    if (type == BIDIR_TYPE_RLO)
                        override = BIDIR_TYPE_R;
                    else
                        override = BIDIR_TYPE_N;
                }
            }
            RL_TYPE(pp) = BIDIR_TYPE_DEL;
        } else if (type == BIDIR_TYPE_PDF) {
            for (i = 0; i < RL_LEN(pp); i++) {
                if (stack_index > 0) {
                    stack_index--;
                    /* pop level & override */
                    level = stack_level[stack_index];
                    override = stack_override[stack_index];
                }
            }
            RL_TYPE(pp) = BIDIR_TYPE_DEL;
        } else {
            RL_LEVEL(pp) = level;
            /* if override defined, then modify type accordingly */
            if (override != BIDIR_TYPE_N)
                RL_TYPE(pp) = override;
        }
    }

    compact_list(type_rl_list);

    /* 4. Resolving weak types */
    last_strong = base_dir;
    for (pp = type_rl_list + 1; pp->type != BIDIR_TYPE_EOT; pp++) {
        int prev_type = RL_TYPE(pp-1);
        int this_type = RL_TYPE(pp);
        int next_type = RL_TYPE(pp+1);

        /* Remember the last strong character */
        if (ONE_OF_3(prev_type, BIDIR_TYPE_AL, BIDIR_TYPE_R, BIDIR_TYPE_L))
            last_strong = prev_type;

        /* W1. NSM */
        if (this_type == BIDIR_TYPE_NSM) {
            if (prev_type == BIDIR_TYPE_SOT)
                RL_TYPE(pp) = BIDIR_TYPE_N;   /* Will be resolved to base dir */
            else
                RL_TYPE(pp) = prev_type;
        }
        /* W2: European numbers */
        if (this_type == BIDIR_TYPE_N && last_strong == BIDIR_TYPE_AL)
            RL_TYPE(pp) = BIDIR_TYPE_AN;

        /* W3: Change ALs to R
           We have to do this for prev character as we would otherwise
           interfer with the next last_strong which is BIDIR_TYPE_AL.
         */
        if (prev_type == BIDIR_TYPE_AL)
            RL_TYPE(pp-1) = BIDIR_TYPE_R;

        /* W4. A single european separator changes to a european number.
           A single common separator between two numbers of the same type
           changes to that type.
         */
        if (RL_LEN(pp) == 1 && prev_type == next_type) {
            TYPE_RULE_C(EN, ES, EN, EN)
            TYPE_RULE_C(EN, CS, EN, EN)
            TYPE_RULE_C(AN, CS, AN, AN)
        }
        /* W5. A sequence of European terminators adjacent to European
           numbers changes to All European numbers.
         */
        if (this_type == BIDIR_TYPE_ET) {
            if (next_type == BIDIR_TYPE_EN || prev_type == BIDIR_TYPE_EN) {
                RL_TYPE(pp) = BIDIR_TYPE_EN;
            }
        }
        /* This type may have been overriden */
        this_type = RL_TYPE(pp);

        /* W6. Otherwise change separators and terminators to other neutral */
        if (ONE_OF_3(this_type, BIDIR_TYPE_ET, BIDIR_TYPE_CS, BIDIR_TYPE_ES))
            RL_TYPE(pp) = BIDIR_TYPE_ON;

        /* W7. Change european numbers to L. */
        if (prev_type == BIDIR_TYPE_EN && last_strong == BIDIR_TYPE_L)
            RL_TYPE(pp-1) = BIDIR_TYPE_L;
    }

    compact_list(type_rl_list);

    /* 5. Resolving Neutral Types */

    /* We can now collapse all separators and other neutral types to
       plain neutrals */
    for (pp = type_rl_list + 1; pp->type != BIDIR_TYPE_EOT; pp++) {
        int this_type = RL_TYPE(pp);
        if (ONE_OF_6(this_type, BIDIR_TYPE_WS, BIDIR_TYPE_ON, BIDIR_TYPE_ES,
                     BIDIR_TYPE_ET, BIDIR_TYPE_CS, BIDIR_TYPE_BN)) {
            RL_TYPE(pp) = BIDIR_TYPE_N;
        }
    }

    compact_list(type_rl_list);

    for (pp = type_rl_list + 1; pp->type != BIDIR_TYPE_EOT; pp++) {
        if (RL_TYPE(pp) == BIDIR_TYPE_N) {      /* optimization! */
            int prev_type = RL_TYPE(pp-1);
            int next_type = RL_TYPE(pp+1);

            /* "European and arabic numbers are treated
               as though they were R" */

            /* N1. */
            if (prev_type == BIDIR_TYPE_L && next_type == BIDIR_TYPE_L)
                RL_TYPE(pp) = BIDIR_TYPE_L;
            else
            if (ONE_OF_3(prev_type, BIDIR_TYPE_EN, BIDIR_TYPE_AN, BIDIR_TYPE_R)
            &&  ONE_OF_3(next_type, BIDIR_TYPE_EN, BIDIR_TYPE_AN, BIDIR_TYPE_R))
                RL_TYPE(pp) = BIDIR_TYPE_R;
            else
            /* N2. Any remaining neutrals takes the embedding direction */
            if (RL_TYPE(pp) == BIDIR_TYPE_N)
                RL_TYPE(pp) = BIDIR_TYPE_E;
        }
    }

    compact_list(type_rl_list);

    /* 6. Resolving Implicit levels */
    max_level = base_level;

    for (pp = type_rl_list + 1; pp->type != BIDIR_TYPE_EOT; pp++) {
        int this_type = RL_TYPE(pp);

        level = RL_LEVEL(pp);
        if ((level & 1) == 0) {
            /* Even */
            if (this_type == BIDIR_TYPE_R)
                RL_LEVEL(pp) = level + 1;
            else if (this_type == BIDIR_TYPE_AN)
                RL_LEVEL(pp) = level + 2;
            else if (RL_TYPE(pp-1) != BIDIR_TYPE_L && this_type == BIDIR_TYPE_EN)
                RL_LEVEL(pp) = level + 2;
            else
                RL_LEVEL(pp) = level;
        } else {
            /* Odd */
            if (ONE_OF_3(this_type, BIDIR_TYPE_L, BIDIR_TYPE_AN, BIDIR_TYPE_EN))
                RL_LEVEL(pp) = level + 1;
            else
                RL_LEVEL(pp) = level;
        }

        if (RL_LEVEL(pp) > max_level)
            max_level = RL_LEVEL(pp);
    }

    compact_list(type_rl_list);

    *pmax_level = max_level;
    *pbase_dir = base_dir;
}
