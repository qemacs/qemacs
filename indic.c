/*
 * Indic algorithms for QEmacs.
 *
 * Copyright (c) 2000 Fabrice Bellard.
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

#include "cutils.h"
#include "unicode_join.h"

#define VIRAMA  0x94d
#define RA      0x930
#define RRA     0x931
#define ZERO_WIDTH_JOINER 0x200d

/* private unicode extensions */
#define DEAD_CONSONANT_OFFSET 0x10000
#define HALF_OFFSET 0xe000

#define RA_SUP  0xe97e
#define RA_SUB  0xe97f
#define RRA_HALF 0xe97d
#define RA_DEAD (RA + DEAD_CONSONANT_OFFSET)

#if 0
static int devanagari_is_vowel_sign(char32_t i) {
    return (i >= 0x93E && i <= 0x94c) || (i >= 0x962 && i <= 0x963);
}
#endif

static int devanagari_is_consonant(char32_t i) {
    return (i >= 0x915 && i <= 0x939) || (i >= 0x958 && i <= 0x95f);
}

static int devanagari_is_vowel(char32_t i) {
    return (i >= 0x905 && i <= 0x914);
}

static int devanagari_is_dead_consonant(char32_t i) {
    return (i >= DEAD_CONSONANT_OFFSET && i <= DEAD_CONSONANT_OFFSET + 0x7f);
}

/* always returns a smaller buffer */
int devanagari_log2vis(char32_t *str, unsigned int *ctog, int len) {
    char32_t cc, c;
    int i, len1, j, k;
    /* CG: C99 variable-length arrays may be too large */
    char32_t *q, buf[len];

    /* Rule 1: dead consonant rule */
    q = buf;
    len1 = len - 1;
    for (i = 0; i < len; i++) {
        cc = str[i];
        if (devanagari_is_consonant(cc) && i < len1 && str[i+1] == VIRAMA) {
            *q++ = cc + DEAD_CONSONANT_OFFSET;
            i++;
        } else {
            *q++ = cc;
        }
    }

    /************ RA rules */
    /* XXX: rule 3, 4, 7 should be handled as ligatures */
    for (i = 0; i < len1; i++) {
        /* Rule 2 */
        if (buf[i] == RA_DEAD &&
            (devanagari_is_vowel(buf[i+1]) || devanagari_is_consonant(buf[i+1]))) {
            buf[i] = buf[i+1];
            buf[i+1] = RA_SUP;
        } else
        /* Rule 5 */
        if (buf[i] == RRA &&
            buf[i+1] == VIRAMA) {
            buf[i] = RRA_HALF;
            buf[i+1] = 0;
        } else
        /* Rule 5a */
        if (buf[i] == RA_DEAD &&
            buf[i+1] == ZERO_WIDTH_JOINER) {
            buf[i] = RRA_HALF;
            buf[i+1] = 0;
        } else
        /* Rule 6 */
        if (devanagari_is_dead_consonant(buf[i]) && buf[i+1] == RA) {
            buf[i] -= DEAD_CONSONANT_OFFSET;
            buf[i+1] = RA_SUB;
        } else
        /* Rule 8 */
        if (devanagari_is_dead_consonant(buf[i]) &&
            buf[i+1] == RA_DEAD) {
            buf[i] -= DEAD_CONSONANT_OFFSET;
            buf[i+1] = RA_SUB;
            buf[i+2] = VIRAMA;
        }
    }

    /* convert dead consonant to half consonants */
    for (i = 0; i < len1; i++) {
        if (devanagari_is_dead_consonant(buf[i]) &&
            (i == (len1 - 1) ||
             buf[i+1] == ZERO_WIDTH_JOINER ||
             devanagari_is_consonant(buf[i+1]) ||
             devanagari_is_dead_consonant(buf[i+1]))) {
            buf[i] -= DEAD_CONSONANT_OFFSET + HALF_OFFSET;
        }
    }

    /* output result and update ctog */
    j = 0;
    for (i = 0; i < len; i++) {
        c = buf[i];
        if (c != 0) {
            ctog[i] = j;
            str[j++] = c;
        } else {
            /* zero: associate it to previous char */
            /* XXX: is it always a good guess ? */
            k = j - 1;
            if (k < 0)
                k = 0;
            ctog[i] = k;
        }
    }
    return j;
}
