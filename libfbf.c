/*
 * libfbf.c - FBF font decoder
 * 
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
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
#include <stdlib.h>

#include "libfbf.h"

//#define DEBUG

#define free(ptr)    do_not_use_free
#define malloc(size) do_not_use_malloc

static void init_context_tables(void);
static int unicode_to_johab(int code);
static int decode_hangul_glyph(UniFontData *uf,
                               GlyphEntry **glyph_entry_ptr,
                               int code);

static inline void *uf_malloc(UniFontData *uf, int size)
{
    return uf->fbf_malloc(uf->mem_opaque, size);
}

static inline void uf_free(UniFontData *uf, void *ptr)
{
    return uf->fbf_free(uf->mem_opaque, ptr);
}

/* totally unoptimised get bit functions */
static void get_bit_init(UniFontData *uf)
{
    uf->bitcnt = 0;
}

static int get_bit(UniFontData *uf)
{
    if (uf->bitcnt == 0) {
        uf->bitbuf = uf->fbf_getc(uf->infile);
        uf->bitcnt = 7;
    } else {
        uf->bitcnt--;
    }
    return (uf->bitbuf >> uf->bitcnt) & 1;
}

static int get_bits(UniFontData *uf, int n)
{
    int val, i;

    val = 0;
    for(i=0;i<n;i++) {
        val = (val << 1) | get_bit(uf);
    }
    return val;
}

/* read log encoded number */
static int read_num(UniFontData *uf)
{
    int l, n;

    l = 0;
    while (get_bit(uf) != 0)
        l++;
    if (l <= 1)
        return l;
    n = get_bits(uf, l - 1) | (1 << (l -1));
    return n;
}

static void read_segments(UniFontData *uf)
{
    int n, code, delta, size, seg;

    get_bit_init(uf);
    seg = 0;
    n = 0;
    code = 0;
    while (n < uf->nb_glyphs && seg < uf->nb_segs) {
        /* read one segment */
        delta = read_num(uf);
        size = read_num(uf) + 1;
        code += delta;
        uf->seg_table[seg].glyph = n;
        uf->seg_table[seg].start = code;
        uf->seg_table[seg].size = size;
        //printf("%05x: %d\n", code, size);
        code += size + 1;
        n += size;
        seg++;
    }
}

static int get_be16(UniFontData *uf)
{
    int val;
    val = uf->fbf_getc(uf->infile) << 8;
    val |= uf->fbf_getc(uf->infile);
    return val;
}

static int get_be32(UniFontData *uf)
{
    int val;
    val = uf->fbf_getc(uf->infile) << 24;
    val |= uf->fbf_getc(uf->infile) << 16;
    val |= uf->fbf_getc(uf->infile) << 8;
    val |= uf->fbf_getc(uf->infile);
    return val;
}

static void get_str(UniFontData *uf, char *buf, int buf_size)
{
    int i, len, c;
    char *q;

    len = uf->fbf_getc(uf->infile);  
    q = buf;
    for(i=0;i<len;i++) {
        c = uf->fbf_getc(uf->infile);
        if ((q - buf) < (buf_size - 1))
            *q++ = c;
    }
    *q = '\0';
}

int fbf_load_font(UniFontData *uf)
{
    UnifontHeader h;
    int i;
    static int init_done = 0;

    if (!init_done) {
        init_context_tables();
        init_done = 1;
    }

    h.magic = get_be32(uf);
    if (h.magic != UNIFONT_MAGIC)
        return -1;
    h.version = get_be32(uf);
    if (h.version != 1)
        return -1;
    h.nb_glyphs = get_be32(uf);
    h.compressed_segment_size = get_be32(uf);
    h.flags = get_be32(uf);
    h.max_width = get_be16(uf);
    h.max_height = get_be16(uf);
    uf->x_res = get_be16(uf);
    uf->y_res = get_be16(uf);
    uf->pt_size = get_be16(uf);
    uf->ascent = get_be16(uf);
    uf->descent = get_be16(uf);
    uf->line_height = get_be16(uf);
    uf->underline_position = get_be16(uf);
    uf->underline_thickness = get_be16(uf);
    h.nb_segments = get_be16(uf);
    uf->family_type = uf->fbf_getc(uf->infile);
    uf->fbf_getc(uf->infile);

    get_str(uf, uf->family_name, sizeof(uf->family_name));

    uf->nb_glyphs = h.nb_glyphs;
    uf->compressed_segment_size = h.compressed_segment_size;
    uf->max_width = h.max_width;
    uf->max_height = h.max_height;
    uf->flags = h.flags;
    uf->nb_segs = h.nb_segments;

    /* compressed segments offsets */

    uf->nb_csegs = (uf->nb_glyphs + uf->compressed_segment_size - 1) / 
        uf->compressed_segment_size;
    uf->csegs_offsets = uf_malloc(uf, uf->nb_csegs * 2 * sizeof(int));
    if (!uf->csegs_offsets)
        goto fail;
    uf->msegs_offsets = uf->csegs_offsets + uf->nb_csegs;
    for(i=0;i<uf->nb_csegs;i++) {
        uf->msegs_offsets[i] = get_be32(uf);
    }

    for(i=0;i<uf->nb_csegs;i++) {
        uf->csegs_offsets[i] = get_be32(uf);
    }

    /* unicode to glyph index conversion table */

    uf->seg_table = uf_malloc(uf, uf->nb_segs * sizeof(SegData));
    if (!uf->seg_table)
        goto fail;
    read_segments(uf);
    
    /* decoding context */
    if (uf->fbf_read(uf->infile, uf->ctx1, sizeof(uf->ctx1)) != sizeof(uf->ctx1))
        goto fail;

    uf->nb_glyphs_total = uf->nb_glyphs;
    /* if hangul composite glyphs, increase virtual number of glyphs */
    if (uf->flags & UF_FLAG_HANGUL) {
        uf->nb_glyphs_total += 11172;
    }
    return 0;

 fail:
    uf_free(uf, uf->seg_table);
    if (uf) {
        if (uf->csegs_offsets)
            uf_free(uf, uf->csegs_offsets);
    }
    return -1;
}

void fbf_free_font(UniFontData *uf)
{
    GlyphSegment *gseg;
    int i;

    for(i=0;i<CSEG_CACHE_SIZE;i++) {
        gseg = uf->cseg_cache[i];
        if (gseg) {
            uf_free(uf, gseg->bitmap_table);
            uf_free(uf, gseg);
        }
    }
    uf_free(uf, uf->csegs_offsets);
    uf_free(uf, uf->seg_table);
}

/* arithmetic font decoder */

static unsigned char ctx_incr[2][256];
static unsigned char ctx_shift[256];

static void init_context_tables(void)
{
    int ctxval, val, bit, freq0, freq1, shift;

    /* table for context frequency update */
    for(bit=0;bit<2;bit++) {
        for(ctxval=0;ctxval<256;ctxval++) {
            freq0 = 2*(ctxval >> 4) + 1;
            freq1 = 2*(ctxval & 0xf) + 1;
            if (!bit)
                freq0 += 2;
            else
                freq1 += 2;
            if (freq0 > 31 || freq1 > 31) { 
                freq0 = (freq0 + 1) >> 1;
                freq1 = (freq1 + 1) >> 1;
            }
            val = (((freq0 - 1) >> 1) << 4) | 
                ((freq1 - 1) >> 1);
            ctx_incr[bit][ctxval] = val;
            //            printf("%d %02x %02x\n", bit, ctxval, val);
        }
    }

    /* table for arith coding */
    for(ctxval=0;ctxval<256;ctxval++) {
        unsigned int sum, m, s;

        freq0 = 2*(ctxval >> 4) + 1;
        freq1 = 2*(ctxval & 0xf) + 1;
        val = 0;

        sum = freq0 + freq1;
        /* invert bit */
        if (freq0 > freq1) {
            val = 0x80;
            freq0 = freq1;
        }

        shift = 0;
        m=freq0*8;
        s=sum*5;
        while (m<s) {
            shift++;
            m<<=1;
        }
        if (m<sum*7) {
            shift += 2;
            val |= 0x40;
        }
        ctx_shift[ctxval] = val | shift;
        //        printf("%02x %02x\n", ctxval, ctx_shift[ctxval]);
    }
}

static void arith_init(UniFontData *uf)
{
    uf->arange = 0x1000000;
    /* data buffer */
    uf->alow = uf->fbf_getc(uf->infile) << 16;
    uf->alow |= uf->fbf_getc(uf->infile) << 8;
    uf->alow |= uf->fbf_getc(uf->infile);
}

static inline int decode_ctx(UniFontData *uf, unsigned char *ctx)
{
    int ctxval, range, shift, b;
    unsigned int alow, arange;

    alow = uf->alow;
    arange = uf->arange;

    ctxval = ctx[0];
#ifdef DEBUG
    printf("low=%x range=%x ctx=%x ", alow, arange, ctxval);
#endif

    /* compute decision level */
    shift = ctx_shift[ctxval];
    range = arange;
    if (shift & 0x40)
        range = range * 3;
    range = range >> (shift & 0x3f);
    
    /* which bit is was encoded ? */
    b = (alow >= range);
    if (b) {
        alow -= range;
        arange -= range;
    } else {
        arange = range;
    }
    
    /* increment context */
    b = b ^ (shift >> 7);
    ctx[0] = ctx_incr[b][ctxval];

    /* renormalize arith state */
    if (arange < RANGE_MIN) {
        alow = ((alow << 8) | uf->fbf_getc(uf->infile)) & 0xffffff;
        arange <<= 8;
    }

    uf->arange = arange;
    uf->alow = alow;
#ifdef DEBUG
    printf("b=%d\n", b);
#endif
    return b;
}


/* glyph decoder */

static inline int get_ctx(int x, int y, unsigned char *p)
{
    int v;

    v = 0;

            v = v * 3;
            if (y >= 4) {
                v += p[-3 * WRAP] << 0;  // M
            } else {
                v += 2;
            }
            v = v  * 3;
            if (x >= 4) {
                v += (p[-3]) << 0;  // T
            } else {
                v += 2;
            }

            /* distance of 2 */
            v = v << 2;
            v += p[-2 * WRAP + 2] << 0; // K
            v += p[-2 * WRAP - 2] << 1;     // G

            v = v << 1;
            v += p[-2] << 0;            // E

            v = v << 3;
            v += p[-2 * WRAP - 1] << 0;  // H
            v += p[-2 * WRAP] << 1;      // I
            v += p[-2 * WRAP + 1] << 2;  // J
            
            v = v << 4;
            v += p[-WRAP - 1] << 0;     // B
            v += p[-WRAP + 1] << 1;      // D
            v += p[-WRAP + 2] << 2;     // L
            v += p[-WRAP - 2] << 3;     // F

            v = v << 2;
            v += p[-1] << 0;                    // A
            v += p[-WRAP] << 1;          // C

            return v;
}

static void decode_glyph(UniFontData *uf, 
                         unsigned char *ctx1,
                         unsigned char *ctx_adapt, 
                         unsigned char *outbuf,
                         int w, int h)
{
    char *p;
    char bitmap[WRAP * (MAXHEIGHT + MAXDIST)];
    int v, v1, x, y, b, i;
    unsigned int lbuf, w1;

    memset(bitmap, 1, sizeof(bitmap));
    p = bitmap + MAXDIST + WRAP * MAXDIST;
    w1 = (w + 7) >> 3;
    for(y=0;y<h;y++) {
        lbuf = 0;
        for(x=0;x<w;x++) {
            v = get_ctx(x, y, p);
            if (ctx_adapt[v] == 0x00 ||
                ctx_adapt[v] == 0x10 ||
                ctx_adapt[v] == 0x01) {
                v1 = v & (NB_CTX1 - 1);
                b = decode_ctx(uf, &ctx1[v1]);
                ctx_adapt[v] = ctx_incr[b][ctx_adapt[v]];
            } else {
                b = decode_ctx(uf, &ctx_adapt[v]);
            }
            *p++ = b;
            lbuf = (lbuf << 1) | b;
            if ((x & 7) == 7) {
                *outbuf++ = lbuf;
            }
        }
        p += WRAP - w;
        /* output remaning bits */
        i = (w & 7);
        if (i != 0) {
            *outbuf++ = lbuf << (8 - i);
        }
    }
}

typedef struct EncodeLogContext {
    unsigned char log_ctx[16];
    unsigned char sign_ctx;
} EncodeLogContext;

static int read_num1(UniFontData *uf, EncodeLogContext *c, int is_signed)
{
    int l, n, i;
    unsigned char ctx;

    l = 0;
    while (decode_ctx(uf, &c->log_ctx[l]) != 0)
        l++;
    if (l == 0) {
        return 0;
    }
    n = 1 << (l - 1);
    for(i=l-2;i>=0;i--) {
        ctx = 0;
        n |= decode_ctx(uf, &ctx) << i;
    }
    if (is_signed) {
        if (decode_ctx(uf, &c->sign_ctx))
            n = -n;
    }
    return n;
}

typedef struct {
    EncodeLogContext metric_ctx[6];
    int last_w, last_h, last_x, last_y, last_xincr;
} MetricContext;

static void decode_glyph_metric(UniFontData *uf, MetricContext *m,
                                GlyphEntry *g)
{
    g->w = read_num1(uf, &m->metric_ctx[1], 1) + m->last_w;
    g->h = read_num1(uf, &m->metric_ctx[2],  1) + m->last_h;
    g->x = read_num1(uf, &m->metric_ctx[3], 1) + m->last_x;
    g->y = read_num1(uf, &m->metric_ctx[4], 1) + m->last_y;
    g->xincr = read_num1(uf, &m->metric_ctx[5], 1) + m->last_xincr;

    m->last_w = g->w;
    m->last_h = g->h;
    m->last_x = g->x;
    m->last_y = g->y;
    m->last_xincr = g->xincr;
}

/* decode one segment of 'compressed_segment_size' glyph metrics */
static GlyphSegment *decode_metrics_segment(UniFontData *uf, int segment)
{
    int i, glyph_start, glyph_end, nb_glyphs;
    GlyphSegment *g;
    GlyphEntry *m;
    MetricContext metric_ctx;

    glyph_start = segment * uf->compressed_segment_size;
    if (glyph_start >= uf->nb_glyphs)
        return NULL;
    glyph_end = glyph_start + uf->compressed_segment_size;
    if (glyph_end > uf->nb_glyphs)
        glyph_end = uf->nb_glyphs;
    nb_glyphs = glyph_end - glyph_start;

    g = uf_malloc(uf, sizeof(GlyphSegment) + 
                  (nb_glyphs - 1) * sizeof(GlyphEntry));
    if (!g)
        return NULL;
    
    g->first_glyph = glyph_start;
    g->nb_glyphs = nb_glyphs;
    g->bitmap_table = NULL;

    uf->fbf_seek(uf->infile, uf->msegs_offsets[segment]);

    arith_init(uf);
    
    memset(&metric_ctx, 0, sizeof(metric_ctx));
    for(i=0;i<nb_glyphs;i++) {
        m = &g->metrics[i];
        decode_glyph_metric(uf, &metric_ctx, m);
    }
    return g;
}

/* decode one segment of 'compressed_segment_size' glyphs */
static int decode_glyphs_segment(UniFontData *uf, GlyphSegment *g, int segment)
{
    int glyph_start, glyph_end, size, nb_glyphs, bitmap_size, i;
    unsigned char *ctx, ctx1[NB_CTX1], *data;
    GlyphEntry *m;

    glyph_start = segment * uf->compressed_segment_size;
    if (glyph_start >= uf->nb_glyphs)
        return -1;
    glyph_end = glyph_start + uf->compressed_segment_size;
    if (glyph_end > uf->nb_glyphs)
        glyph_end = uf->nb_glyphs;
    nb_glyphs = glyph_end - glyph_start;

    uf->fbf_seek(uf->infile, uf->csegs_offsets[segment]);
    
    /* allocate bitmap */
    bitmap_size = 0;
    for(i=0;i<nb_glyphs;i++) {
        m = &g->metrics[i];
        size = ((m->w + 7) >> 3) * m->h;
        bitmap_size += size;
    }
    //    printf("segment=%d bitmap_size=%d\n", segment, bitmap_size);
    g->bitmap_table = uf_malloc(uf, bitmap_size);
    if (!g->bitmap_table)
        return -1;

    /* allocate compression context */
    ctx = uf_malloc(uf, NB_CTX);
    if (!ctx)
        return -1;
    memset(ctx, 0, NB_CTX);
    memcpy(ctx1, uf->ctx1, NB_CTX1);

    arith_init(uf);
    data = g->bitmap_table;
    for(i=0;i<nb_glyphs;i++) {
        m = &g->metrics[i];
        m->bitmap = data;
        decode_glyph(uf, ctx1, ctx, data, m->w, m->h);
        size = ((m->w + 7) >> 3) * m->h;
        data += size;
    }

    uf_free(uf, ctx);
    return 0;
}

int fbf_decode_glyph(UniFontData *uf, 
                     GlyphEntry **glyph_entry_ptr,
                     int index)
{
    GlyphSegment *gseg, **pgseg;
    int i, segment, use_count_min, cseg_min;

    if (index >= uf->nb_glyphs) {
        /* special case for algorithmically generated hangul glyphs */
        return decode_hangul_glyph(uf, glyph_entry_ptr, index - uf->nb_glyphs);
    }

 redo:
    for(i=0;i<CSEG_CACHE_SIZE;i++) {
        gseg = uf->cseg_cache[i];
        if (gseg && 
            index >= gseg->first_glyph && 
            index < gseg->first_glyph + gseg->nb_glyphs) {
            gseg->use_count++;
            *glyph_entry_ptr = &gseg->metrics[index - gseg->first_glyph];
            return 0;
        }
    }
    /* no glyph found : decode a new segment */

    /* free least used segment selected segment */
    cseg_min = 0;
    use_count_min = 0x7fffffff;
    for(i=0;i<CSEG_CACHE_SIZE;i++) {
        if (uf->cseg_cache[i] == NULL) {
            cseg_min = i;
            break;
        }
        if (uf->cseg_cache[i]->use_count < use_count_min) {
            use_count_min = uf->cseg_cache[i]->use_count;
            cseg_min = i;
        }
    }

    pgseg = &uf->cseg_cache[cseg_min];
    gseg = *pgseg;
    if (gseg) {
        uf_free(uf, gseg->bitmap_table);
        uf_free(uf, gseg);
        *pgseg = NULL;
    }
    
    /* create a glyph segment and decode only its metrics */
    segment = index / uf->compressed_segment_size;
    gseg = decode_metrics_segment(uf, segment);
    if (!gseg)
        return -1;
    /* decode the glyphs */
    if (decode_glyphs_segment(uf, gseg, segment) < 0)
        return -1;
    gseg->use_count = 0;
    *pgseg = gseg;
    goto redo;
}

/* XXX: could be faster with a table for the high order bits of 'code' */
int fbf_unicode_to_glyph(UniFontData *uf, int code)
{
    int start, end, mid, size, k;

    /* special hangul case */
    if (code >= 0x1100 && code <= 0x11ff) {
        code = unicode_to_johab(code);
    } else if (code >= 0xAC00 && code < (0xAC00 + 11172)) {
        /* map to composite glyph area */
        return code - 0xAC00 + uf->nb_glyphs;
    }
               
    start = 0;
    end = uf->nb_segs - 1;
    while (end >= start) {
        mid = (start + end) >> 1;
        k = uf->seg_table[mid].start;
        size = uf->seg_table[mid].size;
        if (code >= k && code < (k + size)) {
            return uf->seg_table[mid].glyph + code - k;
        } else if (code < k)
            end = mid - 1;
        else {
            start = mid + 1;
        }
    }
    return -1;
}

/************************************************************/
/* HANGUL composite glyph handling */

// The base font index for leading consonants
static const unsigned char lconBase[] = {
        1, 11, 21, 31, 41, 51,
        61, 71, 81, 91, 101, 111,
        121, 131, 141, 151, 161, 171,
        181
};

// The base font index for vowels

static const unsigned short vowBase[] = {
    0,311,314,317,320,323,   //  (Fill), A, AE, YA, YAE, EO
    326,329,332,335,339,343, //  E, YEO, YE, O, WA, WAE
    347,351,355,358,361,364, //  OI, YO, U, WEO, WE, WI
    367,370,374,378          //  YU, EU, UI, I
};

  // The base font index for trailing consonants

static const unsigned short tconBase[] = {
    // modern trailing consonants (filler + 27)
    0,
    405, 409, 413, 417, 421,
    425, 429, 433, 437, 441,
    445, 449, 453, 457, 461,
    465, 469, 473, 477, 481,
    485, 489, 493, 497, 501,
    505, 509
};

// The mapping from vowels to leading consonant type
// in absence of trailing consonant

static const unsigned char lconMap1[] = {
    0,0,0,0,0,0,     // (Fill), A, AE, YA, YAE, EO
    0,0,0,1,3,3,     // E, YEO, YE, O, WA, WAE
    3,1,2,4,4,4,     // OI, YO, U, WEO, WE, WI
    2,1,3,0          // YU, EU, UI, I
};

// The mapping from vowels to leading consonant type
// in presence of trailing consonant

static const unsigned char lconMap2[] = {
    5,5,5,5,5,5,     //  (Fill), A, AE, YA, YAE, EO
    5,5,5,6,8,8,     //  E, YEO, YE, O, WA, WAE
    8,6,7,9,9,9,     //  OI, YO, U, WEO, WE, WI
    7,6,8,5          //  YU, EU, UI, I
};

//  vowel type ; 1 = o and its alikes, 0 = others

static const unsigned char vowType[] = {
    0,0,0,0,0,0,
    0,0,0,1,1,1,
    1,1,0,0,0,0,
    0,1,1,0
};

//  The mapping from trailing consonants to vowel type

static const unsigned char tconType[] = {
    0, 1, 1, 1, 2, 1,
    1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1,
    1, 1, 1, 1
};

//  The mapping from vowels to trailing consonant type

static const unsigned char tconMap[] = {
    0, 0, 2, 0, 2, 1,  //  (Fill), A, AE, YA, YAE, EO
    2, 1, 2, 3, 0, 0,  //  E, YEO, YE, O, WA, WAE
    0, 3, 3, 1, 1, 1,  //  OI, YO, U, WEO, WE, WI
    3, 3, 0, 1         //  YU, EU, UI, I
};

/* put src at position (x,y) in dst. src has a size of (w, h) */
static inline void bitmap_or(unsigned char *dst, int dst_wrap,
                             unsigned char *src, int src_wrap,
                             int x, int y,
                             int w, int h)
{
    int i, j, bit, x1;
    dst += dst_wrap * y;
    for(i=0;i<h;i++) {
        for(j=0;j<w;j++) {
            bit = (src[j >> 3] >> (7 - (j & 7))) & 1;
            x1 = x + j;
            dst[x1 >> 3] |= bit << (7 - (x1 & 7));
        }
        dst += dst_wrap;
        src += src_wrap;
    }
}

/* XXX: suppress that by modifying unicode to glyph mapping in
   ufencode */
static int unicode_to_johab(int code)
{
    int j;

    if (code >= 0x1100 && code <= 0x1100 + 18) {
        j = code - 0x1100;
        code = JOHAB_BASE + lconBase[j] + 9;
    } else if (code >= 0x1161 && code < 0x1161 + 21) {
        j = code - 0x1161 + 1;
        code = JOHAB_BASE + vowBase[j]+1+vowType[j];
    } else if (code >= 0x11A8 && code < 0x11A8 + 27) {
        j = code - 0x11A8 + 1;
        code = JOHAB_BASE + tconBase[j] + 3;
    }
    return code;
}

/* 0 <= code < 11172 */
static int decode_hangul_glyph(UniFontData *uf,
                               GlyphEntry **glyph_entry_ptr,
                               int code)
{
    int i, index, l, m, f, ind[3], wrap;
    GlyphEntry *glyph_entry, *glyph1;

    glyph_entry = &uf->tmp_glyph_entry;
    glyph_entry->bitmap = uf->tmp_buffer;
    /* XXX: size is hardcoded for unifont */
    glyph_entry->w = 16;
    glyph_entry->h = 16;
    glyph_entry->x = 0;
    glyph_entry->y = 0;
    glyph_entry->xincr = 16;
    wrap = (glyph_entry->w + 7) >> 3;
    
    l = code / (21 * 28);
    m = ((code / 28) % 21) + 1;
    f = code % 28;
    
    /* first glyph */
    ind[0] = lconBase[l] + ((f > 0) ? lconMap2[m] : lconMap1[m]);
    
    /* second glyph */
    ind[1] = vowBase[m];
    if (vowType[m] == 1) {
        ind[1] += ((l == 0 || l == 15) ? 0 : 1) + (f > 0 ? 2 : 0);
    } else {
        ind[1] += tconType[f];
    }

    /* third glyph */
    ind[2] = f ? tconBase[f] + tconMap[m] : 0;

    /* render the three glyphs & supperpose them */
    memset(glyph_entry->bitmap, 0, glyph_entry->h * wrap);
    for(i=0;i<3;i++) {
        index = fbf_unicode_to_glyph(uf, JOHAB_BASE + ind[i]);
        if (index < 0)
            continue;
        if (fbf_decode_glyph(uf, &glyph1, index) < 0)
            continue;
        bitmap_or(glyph_entry->bitmap, wrap,
                  glyph1->bitmap, (glyph1->w + 7) >> 3,
                  glyph1->x, glyph1->y,
                  glyph1->w, glyph1->h);
    }
    *glyph_entry_ptr = glyph_entry;
    return 0;
}

