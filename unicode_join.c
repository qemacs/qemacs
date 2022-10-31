/*
 * Unicode joining algorithms for QEmacs.
 *
 * Copyright (c) 2000 Fabrice Bellard.
 * Copyright (c) 2000-2022 Charlie Gordon.
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

#include "util.h"
#include "unicode_join.h"

/*---- ligature tables ----*/

static unsigned short *subst1;
static unsigned short *ligature2;
static unsigned short *ligature_long;

static unsigned short subst1_count;
static unsigned short ligature2_count;

static int uni_get_be16(FILE *f, unsigned short *pv) {
    /* read a big-endiann unsigned 16-bit value from `f` into `*pv` */
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

static char32_t find_ligature(char32_t l1, char32_t l2) {
    int a, b, m;
    char32_t v1, v2;

    a = 0;
    b = ligature2_count - 1;
    while (a <= b) {
        m = (a + b) >> 1;
        v1 = ligature2[3 * m];
        v2 = ligature2[3 * m + 1];
        if (v1 == l1 && v2 == l2)
            return ligature2[3 * m + 2];
        else
        if (v1 > l1 || (v1 == l1 && v2 > l2)) {
            b = m - 1;
        } else {
            a = m + 1;
        }
    }
    return 0xffffffff;
}

int combine_accent(char32_t *buf, char32_t c, char32_t accent) {
    char32_t lig = find_ligature(c, accent);
    if (lig != 0xffffffff) {
        *buf = lig;
        return 1;
    } else {
        return 0;
    }
}

/* No need for efficiency */
int expand_ligature(char32_t *buf, char32_t c) {
    unsigned short *a, *b;

    if (c > 0x7f) {
        for (a = ligature2, b = a + 3 * ligature2_count; a < b; a++) {
            if (a[2] == c) {
                buf[0] = a[0];
                buf[1] = a[1];
                return 1;
            }
        }
    }
    return 0;
}

char32_t qe_unaccent(char32_t c) {
    char32_t buf[2];
    if (expand_ligature(buf, c) && qe_isaccent(buf[1]))
        return buf[0];
    else
        return c;
}

char32_t wctoupper(char32_t c) {
    char32_t buf[2];
    if (expand_ligature(buf, c) && qe_isaccent(buf[1])){
        char32_t c2 = find_ligature(qe_toupper(buf[0]), buf[1]);
        if (c2 != 0xffffffff)
            return c2;
    }
    return c;
}

char32_t wctolower(char32_t c) {
    char32_t buf[2];
    if (expand_ligature(buf, c) && qe_isaccent(buf[1])) {
        char32_t c2 = find_ligature(qe_tolower(buf[0]), buf[1]);
        if (c2 != 0xffffffff)
            return c2;
    }
    return c;
}

/* apply all the ligature rules in logical order. Always return a
   smaller or equal buffer */
static int unicode_ligature(char32_t *buf_out,
                            unsigned int *pos_L_to_V,
                            int len)
{
    int len1, len2, i, j;
    char32_t l, l1, l2;
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
        if (l == 0xffffffff)
            goto nolig;
        if (l > 0) {
            /* ligature of length 2 found */
            pos_L_to_V[i] = q - buf_out;
            pos_L_to_V[i+1] = q - buf_out;
            *q++ = l;
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

static int unicode_classify(char32_t *buf, int len) {
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

static void compose_char_to_glyph(unsigned int *ctog, int len, unsigned int *ctog1)
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
        str[i] = fribidi_get_mirror_char(str[len - 1 - i]);
        str[len - 1 - i] = fribidi_get_mirror_char(tmp);
    }
    /* do not forget central char! */
    if (len & 1) {
        str[len2] = fribidi_get_mirror_char(str[len2]);
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
        len = src_size;
        if (len > dst_size)
            len = dst_size;
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
            len = arab_join(buf, ctog1, len);
            /* not needed for arabjoin */
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
