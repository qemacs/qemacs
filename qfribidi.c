/* FriBidi - Library of BiDi algorithm
 * Copyright (C) 1999 Dov Grobgeld
 * 
 * The optimizations to reduce the code size from 13 KB to 3 KB and
 * the embed/override hack handling are 
 * Copyright (C) 2000 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.  */
#include <stdlib.h>
#include <stdio.h>
#include "qfribidi.h"

//#define TEST
//#define DEBUG

/*======================================================================
//  Mirrored characters include all the characters in the Unicode list
//  that have been declared as being mirrored and that have a mirrored
//  equivalent.
//
//  There are lots of characters that are designed as being mirrored
//  but do not have any mirrored glyph, e.g. the sign for there exist.
//  Are these used in Arabic? That is are all the mathematical signs
//  that are assigned to be mirrorable actually mirrored in Arabic?
//  If that is the case, I'll change the below code to include also
//  characters that mirror to themself. It will then be the responsibility
//  of the display engine to actually mirror these.
//----------------------------------------------------------------------*/

/* The character (n & 0x7fff) is the mirror of (n & 0x7fff) +1 */
/* except for AB <-> BB, 3C <-> 3E, 5B <-> 5D, 7B <-> 7D which have
   their high order bit set */
static const unsigned short mirror_table[] = {
    0x0028, 0x803C, 0x805B, 0x807B, 0x80AB, 0x2039, 0x2045, 0x207D, 
    0x208D, 0x2264, 0x2266, 0x2268, 0x226A, 0x226E, 0x2270, 0x2272, 
    0x2274, 0x22A2, 0x22C9, 0x22CB, 0x22D6, 0x22D8, 0x22DC, 0x22E6, 
    0x22F0, 0x2308, 0x230A, 0x2329, 0x3008, 0x300A, 0x300C, 0x300E, 
    0x3010, 0x3014, 0x3016, 0x3018, 0x301A,
};

/*
 * Note: the first 256 bytes could be stored explicitely to optimize
 * the speed. The table 'property_val' could be stored in 4
 * bits/symbol to reduce its size.
 */
static const unsigned short property_start[366] = {
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

static const unsigned char property_val[366] = {
0x0e, 0x0a, 0x09, 0x0a, 0x0b, 0x09, 0x0e, 0x09,
0x0a, 0x0b, 0x0f, 0x06, 0x0f, 0x06, 0x08, 0x06,
0x08, 0x05, 0x04, 0x08, 0x0f, 0x00, 0x0f, 0x00,
0x0f, 0x0e, 0x09, 0x0e, 0x08, 0x0f, 0x06, 0x0f,
0x00, 0x0f, 0x06, 0x04, 0x0f, 0x00, 0x0f, 0x04,
0x00, 0x0f, 0x00, 0x0f, 0x00, 0x0f, 0x00, 0x0f,
0x00, 0x0f, 0x00, 0x0f, 0x00, 0x0f, 0x00, 0x0d,
0x0f, 0x00, 0x0f, 0x00, 0x0f, 0x00, 0x0d, 0x00,
0x0f, 0x0d, 0x01, 0x0d, 0x01, 0x0d, 0x01, 0x0d,
0x01, 0x08, 0x0c, 0x0d, 0x07, 0x06, 0x07, 0x0c,
0x0d, 0x0c, 0x0d, 0x0c, 0x0d, 0x0f, 0x0d, 0x04,
0x0c, 0x0e, 0x0c, 0x0d, 0x0c, 0x0d, 0x0c, 0x0d,
0x00, 0x0d, 0x00, 0x0d, 0x00, 0x0d, 0x00, 0x0d,
0x00, 0x0d, 0x00, 0x0d, 0x00, 0x0d, 0x00, 0x0d,
0x00, 0x0d, 0x00, 0x0d, 0x00, 0x0d, 0x00, 0x06,
0x00, 0x0d, 0x00, 0x0d, 0x00, 0x0d, 0x00, 0x0d,
0x00, 0x0d, 0x00, 0x0d, 0x00, 0x0d, 0x00, 0x0d,
0x00, 0x0d, 0x00, 0x0d, 0x00, 0x0d, 0x00, 0x0d,
0x00, 0x0d, 0x00, 0x0d, 0x00, 0x0d, 0x00, 0x0d,
0x00, 0x0d, 0x00, 0x0d, 0x00, 0x0d, 0x00, 0x0d,
0x00, 0x0d, 0x00, 0x0d, 0x00, 0x0d, 0x00, 0x0d,
0x00, 0x0d, 0x00, 0x0d, 0x00, 0x0d, 0x06, 0x00,
0x0d, 0x00, 0x0d, 0x00, 0x0d, 0x00, 0x0d, 0x00,
0x0d, 0x00, 0x0d, 0x00, 0x0d, 0x00, 0x0d, 0x0f,
0x00, 0x0d, 0x00, 0x0d, 0x00, 0x0d, 0x00, 0x0d,
0x00, 0x0d, 0x00, 0x0d, 0x00, 0x0d, 0x00, 0x0d,
0x00, 0x0d, 0x00, 0x0b, 0x00, 0x0f, 0x00, 0x0d,
0x00, 0x0d, 0x00, 0x0d, 0x00, 0x06, 0x00, 0x0f,
0x0e, 0x00, 0x0d, 0x00, 0x0f, 0x00, 0x0f, 0x00,
0x0f, 0x00, 0x0f, 0x00, 0x0f, 0x00, 0x0f, 0x0b,
0x0e, 0x00, 0x01, 0x0f, 0x0b, 0x09, 0x10, 0x11,
0x12, 0x13, 0x14, 0x0b, 0x06, 0x0f, 0x0e, 0x04,
0x06, 0x0f, 0x00, 0x04, 0x06, 0x0f, 0x06, 0x0d,
0x0f, 0x00, 0x0f, 0x00, 0x0f, 0x00, 0x0f, 0x00,
0x0f, 0x00, 0x0f, 0x00, 0x0f, 0x00, 0x0f, 0x00,
0x0f, 0x00, 0x06, 0x00, 0x0f, 0x00, 0x0f, 0x00,
0x0f, 0x06, 0x0f, 0x00, 0x0f, 0x00, 0x0f, 0x04,
0x00, 0x04, 0x0f, 0x00, 0x0f, 0x0b, 0x0f, 0x00,
0x0f, 0x00, 0x0d, 0x0f, 0x00, 0x0f, 0x00, 0x0f,
0x00, 0x0d, 0x0f, 0x00, 0x0f, 0x00, 0x0f, 0x00,
0x01, 0x0d, 0x01, 0x06, 0x01, 0x0c, 0x0f, 0x0c,
0x0d, 0x0f, 0x08, 0x0f, 0x08, 0x0f, 0x08, 0x0f,
0x06, 0x0f, 0x06, 0x0f, 0x06, 0x0f, 0x0c, 0x0e,
0x0f, 0x06, 0x0f, 0x06, 0x08, 0x06, 0x08, 0x05,
0x04, 0x08, 0x0f, 0x00, 0x0f, 0x00, 0x0f, 0x00,
0x06, 0x0f, 0x06, 0x0f, 0x0e, 0x0f, 
};

FriBidiCharType fribidi_get_type(FriBidiChar ch)
{
    int a, b, m, ch1;
    
#if defined(TEST) || 0
    /* for testing only */
    if (ch >= 'A' && ch <= 'Z')
        return FRIBIDI_TYPE_RTL;
#endif

    a = 0;
    b = (sizeof(property_start) / 2) - 1;
    while (a <= b) {
        m = (a + b) >> 1;
        ch1 = property_start[m];
        if (ch1 == ch)
            goto found;
        else if (ch1 > ch) {
            b = m - 1;
        } else {
            a = m + 1;
        }
    }
    m = a;
    if (m > 0)
        m--;
 found:
    return property_val[m];
}

/* version for test with ASCII chars */
FriBidiCharType fribidi_get_type_test(FriBidiChar ch)
{
    if (ch >= 'A' && ch <= 'Z')
        return FRIBIDI_TYPE_RTL;
    else
        return fribidi_get_type(ch);
}

FriBidiChar
fribidi_get_mirror_char(FriBidiChar ch)
{
    int a, b, m, ch1, d, e;
    
    if (ch >= 0x8000)
        return ch;

    a = 0;
    b = (sizeof(mirror_table) / 2) - 1;
    while (a <= b) {
        m = (a + b) >> 1;
        ch1 = mirror_table[m];
        if ((ch1 & 0x7fff) == ch)
            goto found;
        else if ((ch1 & 0x7fff) > ch) {
            b = m - 1;
        } else {
            a = m + 1;
        }
    }
    if (a > 0)
        a--;
    ch1 = mirror_table[a];
 found:
    e = 1 + (ch1 >> 15);
    ch1 = ch1 & 0x7fff;
    /* special case */
    if (ch1 == 0xab)
        e = 16;
    d = ch - ch1;
    if (d == 0)
        return ch + e;
    else if (d == e)
        return ch - e;
    else
        return ch;
}

/* Some convenience macros */
#define RL_TYPE(list) (list)->type
#define RL_LEN(list) (list)->len
#define RL_POS(list) (list)->pos
#define RL_LEVEL(list) (list)->level

static void compact_list(TypeLink * list_tab)
{
    TypeLink *p, *lp, *q;
    int type, len;

    p = list_tab;
    q = list_tab;
    for(;;) {
        lp = p;
        type = p->type;
        len = p->len;
        p++;
        if (type == FRIBIDI_TYPE_NULL) {
            /* nothing to do */
        } else if (type != FRIBIDI_TYPE_EOT) {
            while (p->type == type) {
                len += p->len;
                p++;
            }
            q->type = type;
            q->len = len;
            q->pos = lp->pos;
            q->level = lp->level;
            q++;
        } else {
            q->type = FRIBIDI_TYPE_EOT;
            q->pos = lp->pos;
            q++;
            break;
        }
    }
}

/* Define a rule macro */

/* Rules for overriding current type */
#define TYPE_RULE1(old_this,            \
		   new_this)             \
     if (this_type == TYPE_ ## old_this)      \
         RL_TYPE(pp) =       FRIBIDI_TYPE_ ## new_this; \

/* Rules for current and previous type */
#define TYPE_RULE2(old_prev, old_this,            \
		  new_prev, new_this)             \
     if (    prev_type == FRIBIDI_TYPE_ ## old_prev       \
	  && this_type == FRIBIDI_TYPE_ ## old_this)      \
       {                                          \
	   RL_TYPE(pp->prev) = FRIBIDI_TYPE_ ## new_prev; \
	   RL_TYPE(pp) =       FRIBIDI_TYPE_ ## new_this; \
           continue;                              \
       }

/* A full rule that assigns all three types */
#define TYPE_RULE(old_prev, old_this, old_next,   \
		  new_prev, new_this, new_next)   \
     if (    prev_type == FRIBIDI_TYPE_ ## old_prev       \
	  && this_type == FRIBIDI_TYPE_ ## old_this       \
	  && next_type == FRIBIDI_TYPE_ ## old_next)      \
       {                                          \
	   RL_TYPE(pp->prev) = FRIBIDI_TYPE_ ## new_prev; \
	   RL_TYPE(pp) =       FRIBIDI_TYPE_ ## new_this; \
	   RL_TYPE(pp->next) = FRIBIDI_TYPE_ ## new_next; \
           continue;                              \
       }

/* For optimization the following macro only assigns the center type */
#define TYPE_RULE_C(old_prev, old_this, old_next,   \
		    new_this)   \
     if (    prev_type == FRIBIDI_TYPE_ ## old_prev       \
	  && this_type == FRIBIDI_TYPE_ ## old_this       \
	  && next_type == FRIBIDI_TYPE_ ## old_next)      \
       {                                          \
	   RL_TYPE(pp) =       FRIBIDI_TYPE_ ## new_this; \
           continue;                              \
       }

#ifdef DEBUG

/*======================================================================
//  For debugging, define some macros for printing the types and the
//  levels.
//----------------------------------------------------------------------*/
static int type_to_char(int type)
{
    int ch;

    if (type == FRIBIDI_TYPE_R)
    ch = 'R';
    else if (type == FRIBIDI_TYPE_L)
    ch = 'L';
    else if (type == FRIBIDI_TYPE_E)
    ch = 'E';
    else if (type == FRIBIDI_TYPE_EN)
    ch = 'n';
    else if (type == FRIBIDI_TYPE_N)
    ch = 'N';
    else
    ch = '?';
    
    return ch;
}

static void print_types_re(TypeLink * pp_tab, int level)
{
    TypeLink *pp;

    for(pp = pp_tab + 1; pp->type != FRIBIDI_TYPE_EOT; pp++) {
	printf("%d:%c(%d)[%d] ", RL_POS(pp), type_to_char(RL_TYPE(pp)), RL_LEN(pp), level ? RL_LEVEL(pp) : 0);
    }
    printf("\n");
}

#endif

#define STACK_SIZE 64


/*======================================================================
//  This function should follow the Unicode specification closely!
//
//  It is still lacking the support for <RLO> and <LRO>.
//----------------------------------------------------------------------*/
void fribidi_analyse_string(TypeLink * type_rl_list,
                            FriBidiCharType * pbase_dir,
                            int *pmax_level)
{
    int base_level, base_dir;
    int max_level, level, override;
    int last_strong;
    TypeLink *pp;
    unsigned char stack_level[STACK_SIZE];
    unsigned char stack_override[STACK_SIZE];
    int stack_index;
    

#ifdef DEBUG
    print_types_re(type_rl_list, 0);
#endif

    /* Find the base level */
    if (*pbase_dir == FRIBIDI_TYPE_L) {
	base_dir = FRIBIDI_TYPE_L;
	base_level = 0;
    } else if (*pbase_dir == FRIBIDI_TYPE_R) {
	base_dir = FRIBIDI_TYPE_R;
	base_level = 1;
    }
    /* Search for first strong character and use its direction as base
       direction */
    else {
	base_level = 0;		/* Default */
	base_dir = FRIBIDI_TYPE_N;
	for (pp = type_rl_list; pp->type != FRIBIDI_TYPE_EOT; pp++) {
            int type;
            type = RL_TYPE(pp);
	    if (type == FRIBIDI_TYPE_R ||
                type == FRIBIDI_TYPE_RLE ||
                type == FRIBIDI_TYPE_RLO) {
		base_level = 1;
		base_dir = FRIBIDI_TYPE_R;
		break;
	    } else if (type == FRIBIDI_TYPE_L ||
                       type == FRIBIDI_TYPE_LRE ||
                       type == FRIBIDI_TYPE_LRO) {
		base_level = 0;
		base_dir = FRIBIDI_TYPE_L;
		break;
	    }
	}

	/* If no strong base_dir was found, resort to the weak direction
	 * that was passed on input.
	 */
	if (base_dir == FRIBIDI_TYPE_N) {
	    if (*pbase_dir == FRIBIDI_TYPE_WR) {
		base_dir = FRIBIDI_TYPE_RTL;
		base_level = 1;
	    } else if (*pbase_dir == FRIBIDI_TYPE_WL) {
		base_dir = FRIBIDI_TYPE_LTR;
		base_level = 0;
	    }
	}
    }

    /* hack for RLE/LRE/RLO/LRO/PDF. It is not complete in case of
       errors or with neutrals. Better take the latest version of
       fribidi ? */
    level = base_level;
    override = FRIBIDI_TYPE_N;
    stack_index = 0;
    for (pp = type_rl_list + 1; pp->type != FRIBIDI_TYPE_EOT; pp++) {
        int type, i;
        type = RL_TYPE(pp);
        if (type == FRIBIDI_TYPE_LRE || 
            type == FRIBIDI_TYPE_RLE || 
            type == FRIBIDI_TYPE_LRO || 
            type == FRIBIDI_TYPE_RLO) {
            for(i=0;i<RL_LEN(pp);i++) {
                if (stack_index < STACK_SIZE) {
                    /* push level & override */
                    stack_level[stack_index] = level;
                    stack_override[stack_index] = override;
                    stack_index++;
                    /* compute new level */
                    if (type == FRIBIDI_TYPE_LRE ||
                        type == FRIBIDI_TYPE_LRO) {
                        level = (level + 2) & ~1; /* least greater even */
                    } else {
                        level = (level + 1) | 1; /* least greater odd */
                    }
                    /* compute new override */
                    if (type == FRIBIDI_TYPE_LRO)
                        override = FRIBIDI_TYPE_L;
                    else if (type == FRIBIDI_TYPE_RLO)
                        override = FRIBIDI_TYPE_R;
                    else
                        override = FRIBIDI_TYPE_N;
                }
            }
            RL_TYPE(pp) = FRIBIDI_TYPE_NULL;
        } else if (type == FRIBIDI_TYPE_PDF) {
            for(i=0;i<RL_LEN(pp);i++) {
                if (stack_index > 0) {
                    stack_index--;
                    /* pop level & override */
                    level = stack_level[stack_index];
                    override = stack_override[stack_index];
                }
            }
            RL_TYPE(pp) = FRIBIDI_TYPE_NULL;
        } else {
            RL_LEVEL(pp) = level;
            /* if override defined, then modify type accordingly */
            if (override != FRIBIDI_TYPE_N)
                RL_TYPE(pp) = override;
        }
    }

    compact_list(type_rl_list);

    /* 4. Resolving weak types */
    last_strong = base_dir;
    for (pp = type_rl_list + 1; pp->type != FRIBIDI_TYPE_EOT; pp++) {
	int prev_type = RL_TYPE(pp-1);
	int this_type = RL_TYPE(pp);
	int next_type = RL_TYPE(pp+1);

	/* Remember the last strong character */
	if (prev_type == FRIBIDI_TYPE_AL
	    || prev_type == FRIBIDI_TYPE_R
	    || prev_type == FRIBIDI_TYPE_L)
	    last_strong = prev_type;

	/* W1. NSM */
	if (this_type == FRIBIDI_TYPE_NSM) {
	    if (prev_type == FRIBIDI_TYPE_SOT)
		RL_TYPE(pp) = FRIBIDI_TYPE_N;	/* Will be resolved to base dir */
	    else
		RL_TYPE(pp) = prev_type;
	}
	/* W2: European numbers */
	if (this_type == FRIBIDI_TYPE_N
	    && last_strong == FRIBIDI_TYPE_AL)
	    RL_TYPE(pp) = FRIBIDI_TYPE_AN;

	/* W3: Change ALs to R
	   We have to do this for prev character as we would otherwise
	   interfer with the next last_strong which is FRIBIDI_TYPE_AL.
	 */
	if (prev_type == FRIBIDI_TYPE_AL)
	    RL_TYPE(pp-1) = FRIBIDI_TYPE_R;

	/* W4. A single european separator changes to a european number.
	   A single common separator between two numbers of the same type
	   changes to that type.
	 */
	if (RL_LEN(pp) == 1) {
	    TYPE_RULE_C(EN, ES, EN, EN);
	    TYPE_RULE_C(EN, CS, EN, EN);
	    TYPE_RULE_C(AN, CS, AN, AN);
	}
	/* W5. A sequence of European terminators adjacent to European
	   numbers changes to All European numbers.
	 */
	if (this_type == FRIBIDI_TYPE_ET) {
	    if (next_type == FRIBIDI_TYPE_EN
		|| prev_type == FRIBIDI_TYPE_EN) {
		RL_TYPE(pp) = FRIBIDI_TYPE_EN;
	    }
	}
	/* This type may have been overriden */
	this_type = RL_TYPE(pp);

	/* W6. Otherwise change separators and terminators to other neutral */
	if (this_type == FRIBIDI_TYPE_ET
	    || this_type == FRIBIDI_TYPE_CS
	    || this_type == FRIBIDI_TYPE_ES)
	    RL_TYPE(pp) = FRIBIDI_TYPE_ON;

	/* W7. Change european numbers to L. */
	if (prev_type == FRIBIDI_TYPE_EN
	    && last_strong == FRIBIDI_TYPE_L)
	    RL_TYPE(pp-1) = FRIBIDI_TYPE_L;
    }

    compact_list(type_rl_list);

    /* 5. Resolving Neutral Types */

    /* We can now collapse all separators and other neutral types to
       plain neutrals */
    for (pp = type_rl_list + 1; pp->type != FRIBIDI_TYPE_EOT; pp++) {
	int this_type = RL_TYPE(pp);

	if (this_type == FRIBIDI_TYPE_WS
	    || this_type == FRIBIDI_TYPE_ON
	    || this_type == FRIBIDI_TYPE_ES
	    || this_type == FRIBIDI_TYPE_ET
	    || this_type == FRIBIDI_TYPE_CS
	    || this_type == FRIBIDI_TYPE_BN)
	    RL_TYPE(pp) = FRIBIDI_TYPE_N;
    }

    compact_list(type_rl_list);

    for (pp = type_rl_list + 1; pp->type != FRIBIDI_TYPE_EOT; pp++) {
	int prev_type = RL_TYPE(pp-1);
	int this_type = RL_TYPE(pp);
	int next_type = RL_TYPE(pp+1);

	if (this_type == FRIBIDI_TYPE_N) {	/* optimization! */
	    /* "European and arabic numbers are treated
	       as though they were R" */

	    if (prev_type == FRIBIDI_TYPE_EN || prev_type == FRIBIDI_TYPE_AN)
		prev_type = FRIBIDI_TYPE_R;

	    if (next_type == FRIBIDI_TYPE_EN || next_type == FRIBIDI_TYPE_AN)
		next_type = FRIBIDI_TYPE_R;

	    /* N1. */
	    TYPE_RULE_C(R, N, R, R);
	    TYPE_RULE_C(L, N, L, L);

	    /* N2. Any remaining neutrals takes the embedding direction */
	    if (RL_TYPE(pp) == FRIBIDI_TYPE_N)
		RL_TYPE(pp) = FRIBIDI_TYPE_E;
	}
    }

    compact_list(type_rl_list);

    /* 6. Resolving Implicit levels */
    max_level = base_level;
    
    for (pp = type_rl_list + 1; pp->type != FRIBIDI_TYPE_EOT; pp++) {
        int this_type = RL_TYPE(pp);
        
        level = RL_LEVEL(pp);
        if ((level & 1) == 0) {
            /* Even */
            if (this_type == FRIBIDI_TYPE_R)
                RL_LEVEL(pp) = level + 1;
            else if (this_type == FRIBIDI_TYPE_AN)
                RL_LEVEL(pp) = level + 2;
            else if (RL_TYPE(pp-1) != FRIBIDI_TYPE_L && this_type == FRIBIDI_TYPE_EN)
                RL_LEVEL(pp) = level + 2;
            else
                RL_LEVEL(pp) = level;
        } else {
        /* Odd */
            if (this_type == FRIBIDI_TYPE_L
                || this_type == FRIBIDI_TYPE_AN
                || this_type == FRIBIDI_TYPE_EN)
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


