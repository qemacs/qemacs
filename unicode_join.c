/*
 * Unicode joining algorithms for QEmacs.  
 *
 * Copyright (c) 2000 Fabrice Bellard.
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
#include "qfribidi.h"
#include "qe.h"

#ifdef CONFIG_UNICODE_JOIN

/* ligature tables */
static unsigned short *subst1;
static unsigned short *ligature2;
static unsigned short *ligature_long;

static int subst1_count = 0;
static int ligature2_count = 0;

/* XXX: eof ? */
static int uni_get_be16(FILE *f)
{
    int v;
    v = fgetc(f) << 8;
    v |= fgetc(f);
    return v;
}

static unsigned short *read_array_be16(FILE *f, int n)
{
    unsigned short *tab;
    int i;

    tab = malloc(n * sizeof(unsigned short));
    if (!tab) 
        return NULL;
    for(i=0;i<n;i++) {
        tab[i] = uni_get_be16(f);
    }
    return tab;
}

void load_ligatures(void)
{
    FILE *f;
    unsigned char buf[1024];
    int long_count;

    if (find_resource_file(buf, sizeof(buf), "ligatures") < 0)
        return;

    f = fopen(buf, "r");
    if (!f)
        return;
    if (fread(buf, 1, 4, f) != 4 ||
        memcmp(buf, "liga", 4) != 0) 
        goto fail;

    subst1_count = uni_get_be16(f);
    ligature2_count = uni_get_be16(f);
    long_count = uni_get_be16(f);

    subst1 = read_array_be16(f, subst1_count * 2);
    if (!subst1) 
        goto fail;
    ligature2 = read_array_be16(f, ligature2_count * 3);
    if (!ligature2) 
        goto fail;
    ligature_long = read_array_be16(f, long_count);
    if (!ligature_long)
        goto fail;
    fclose(f);
    return;
 fail:
    free(subst1);
    free(ligature2);
    free(ligature_long);
    subst1_count = 0;
    ligature2_count = 0;
    fclose(f);
}

static int find_ligature(int l1, int l2)
{
    int a, b, m, v1, v2;

    a = 0;
    b = ligature2_count - 1;
    while (a <= b) {
        m = (a + b) >> 1;
        v1 = ligature2[3 * m];
        v2 = ligature2[3 * m + 1];
        if (v1 == l1 && v2 == l2)
            return ligature2[3 * m + 2];
        else if (v1 > l1 || (v1 == l1 && v2 > l2)) {
            b = m - 1;
        } else {
            a = m + 1;
        }
    }
    return -1;
}

/* apply all the ligature rules in logical order. Always return a
   smaller buffer */
static int unicode_ligature(unsigned int *buf_out, 
                            unsigned int *pos_L_to_V,
                            int len)
{
    int l, l1, l2, len1, len2, i, j;
    unsigned int *q;
    const unsigned short *lig;
    unsigned int buf[len];
    
    memcpy(buf, buf_out, len * sizeof(int));

    q = buf_out;
    for(i=0;i<len;) {
        l1 = buf[i];
        /* eliminate invisible chars */
        if (l1 >= 0x202a && l1 <= 0x202e) {
            /* LRE, RLE, PDF, RLO, LRO */
            pos_L_to_V[i] = q - buf_out;
            i++;
            goto found;
        }
        /* fast test to eliminate common cases */
        if (i == (len - 1))
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
            *q++ = l;
            i += 2;
        } else {
            /* generic case : use ligature_long[] table */
            lig = ligature_long;
            for(;;) {
                len1 = *lig++;
                if (len1 == 0)
                    break;
                len2 = *lig++;
                if (i + len1 <= len) {
                    for(j=0;j<len1;j++) {
                        if (buf[i+j] != lig[j])
                            goto notfound;
                    }
                    for(j=0;j<len1;j++)
                        pos_L_to_V[i + j] = q - buf_out;
                    for(j=0;j<len2;j++) {
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
        }
    }
    return q - buf_out;
}

/* fast classification of unicode chars to optimize the algorithms */
#define UNICODE_ARABIC   0x00000001
#define UNICODE_INDIC    0x00000002
#define UNICODE_NONASCII 0x00000004

static int unicode_classify(unsigned int *buf, int len)
{
    int i, mask, c;

    mask = 0;
    for(i=0;i<len;i++) {
        c = buf[i];
        if (c <= 0x7f) /* latin1 fast handling */
            continue;
        mask |= UNICODE_NONASCII;
        if (c >= 0x2000) /* fast test for non handled scripts */
            continue; 
        if (c >= 0x600 && c <= 0x6ff) 
            mask |= UNICODE_ARABIC;
        else if (c >= 0x900 && c <= 0x97f)
            mask |= UNICODE_INDIC;
    }
    return mask;
}

static void compose_char_to_glyph(unsigned int *ctog, int len, unsigned *ctog1)
{
    int i;
    for(i=0;i<len;i++)
        ctog[i] = ctog1[ctog[i]];
}

static void bidi_reverse_buf(unsigned int *str, int len)
{
    int i, len2 = len / 2;
    
    for (i = 0; i < len2; i++) {
	unsigned int tmp = str[i];
	str[i] = fribidi_get_mirror_char(str[len - 1 - i]);
	str[len - 1 - i] = fribidi_get_mirror_char(tmp);
    }
    /* do not forget central char ! */
    if (len & 1) {
        str[len2] = fribidi_get_mirror_char(str[len2]);
    }
}

/* Convert a string of unicode characters to a string of glyphs. We
   suppose that the font implements a minimum number of standard
   ligatures chars. The string is reversed if 'reversed' is set to
   deal with the bidir case. 'char_to_glyph_pos' gives the index of
   the first glyph associated to a given character of the source
   buffer. */
int unicode_to_glyphs(unsigned int *dst, unsigned int *char_to_glyph_pos,
                      int dst_size, unsigned int *src, int src_size, int reverse)
{
    int len, i;
    unsigned int ctog[src_size];
    unsigned int ctog1[src_size];
    unsigned int buf[src_size];
    int unicode_class;

    unicode_class = unicode_classify(src, src_size);
    if (unicode_class == 0 && !reverse) {
        /* fast case : no special treatment */
        len = src_size;
        if (len > dst_size)
            len = dst_size;
        memcpy(dst, src, len * sizeof(unsigned int));
        if (char_to_glyph_pos) {
            for(i=0;i<len;i++)
                char_to_glyph_pos[i] = i;
        }
        return len;
    } else {
        /* generic case */

        /* init current buffer */
        len = src_size;
        for(i=0;i<len;i++)
            ctog[i] = i;
        memcpy(buf, src, len * sizeof(int));
        
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
            for(i=0;i<src_size;i++) {
                ctog[i] = len - 1 - ctog[i];
            }
        }

        if (len > dst_size)
            len = dst_size;
        memcpy(dst, buf, len * sizeof(unsigned int));
        
        if (char_to_glyph_pos) {
            memcpy(char_to_glyph_pos, ctog, src_size * sizeof(unsigned int));
        }
    }
    return len;
}

#else /* CONFIG_UNICODE_JOIN */


/* fallback unicode functions */

void load_ligatures(void)
{
}

int unicode_to_glyphs(unsigned int *dst, unsigned int *char_to_glyph_pos,
                      int dst_size, unsigned int *src, int src_size, int reverse)
{
    int len, i;

    len = src_size;
    if (len > dst_size)
        len = dst_size;
    memcpy(dst, src, len * sizeof(unsigned int));
    if (char_to_glyph_pos) {
        for(i=0;i<len;i++)
            char_to_glyph_pos[i] = i;
    }
    return len;
}


#endif /* CONFIG_UNICODE_JOIN */
