/*
 * Indic algorithms for QEmacs.  
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
#include "qe.h"
#include "qfribidi.h"

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
static int is_vowel_sign (unsigned int i)
{
  return (i >= 0x93E && i <= 0x94c) || (i >= 0x962 && i <= 0x963);
}
#endif

static int is_consonant (unsigned int i)
{
  return (i >= 0x915 && i <= 0x939) || (i >= 0x958 && i <= 0x95f);
}

static int is_ind_vowel (unsigned int i)
{
  return (i >= 0x905 && i <= 0x914);
}

static int is_dead_consonant(unsigned int i)
{
    return (i >= DEAD_CONSONANT_OFFSET && i <= DEAD_CONSONANT_OFFSET + 0x7f);
}

/* always returns a smaller buffer */
int devanagari_log2vis(unsigned int *str, unsigned int *ctog, int len)
{
    int i, len1, cc, j, k, c;
    unsigned int *q, buf[len];
    
    /* Rule 1 : dead consonant rule */
    q = buf;
    len1 = len - 1;
    for(i=0;i<len;i++) {
        cc = str[i];
        if (is_consonant(cc) && i < len1 && str[i+1] == VIRAMA) {
            *q++ = cc + DEAD_CONSONANT_OFFSET;
            i++;
        } else {
            *q++ = cc;
        }
    }

    /************ RA rules */
    /* XXX: rule 3, 4, 7 should be handled as ligatures */
    for(i=0;i<len1;i++) {
        /* Rule 2 */
        if (buf[i] == RA_DEAD && 
            (is_ind_vowel(buf[i+1]) || is_consonant(buf[i+1]))) {
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
        if (is_dead_consonant(buf[i]) && buf[i+1] == RA) {
            buf[i] -= DEAD_CONSONANT_OFFSET;
            buf[i+1] = RA_SUB;
        } else
        /* Rule 8 */
        if (is_dead_consonant(buf[i]) && 
            buf[i+1] == RA_DEAD) {
            buf[i] -= DEAD_CONSONANT_OFFSET;
            buf[i+1] = RA_SUB;
            buf[i+2] = VIRAMA;
        }
    }

    /* convert dead consonant to half consonants */
    for(i=0;i<len1;i++) {
        if (is_dead_consonant(buf[i]) && 
            (i == (len1 - 1) ||
             buf[i+1] == ZERO_WIDTH_JOINER || 
             is_consonant(buf[i+1]) ||
             is_dead_consonant(buf[i+1]))) {
            buf[i] -= DEAD_CONSONANT_OFFSET + HALF_OFFSET;
        }
    }

    /* output result and update ctog */
    j = 0;
    for(i=0;i<len;i++) {
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
