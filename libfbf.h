#ifndef LIBFBF_H
#define LIBFBF_Hx

typedef struct {
    unsigned int magic;
    unsigned int version;
    unsigned int nb_glyphs;
    unsigned int compressed_segment_size;
    unsigned int flags;
    unsigned short max_width;
    unsigned short max_height;
    unsigned short x_res;
    unsigned short y_res;
    unsigned short pt_size;
    unsigned short ascent;
    unsigned short descent;
    unsigned short line_height;
    short underline_position;
    unsigned short underline_thickness;
    unsigned short nb_segments;
    unsigned char family_type;
    unsigned char dummy; /* align */
} UnifontHeader;

#define UNIFONT_MAGIC (('U' << 24) | ('N' << 16) | ('F' << 8) | ('T'))

/* interval compression model parameters */

#define NB_CTX ((1 << 12) * 3 * 3)
#define NB_CTX1 (1 << 10)

#define MAXDIST   4

#define MAXWIDTH  128
#define MAXHEIGHT 128 
#define MAXWRAP ((MAXWIDTH+7)/8)

#define WRAP (MAXWIDTH + 2 * MAXDIST)

/* arith coder parameters */
#define RANGE_MIN (1 << 10)

/* flags */
#define UF_FLAG_HANGUL 0x0001 /* use ad hoc hangul compression */

/* family type */
#define UF_FAMILY_FIXED 0
#define UF_FAMILY_SERIF 1
#define UF_FAMILY_SANS  2

#define JOHAB_BASE 0x20000

/* segment cache */
typedef struct {
    short w, h;
    short x, y, xincr;
    unsigned char *bitmap;
} GlyphEntry;

struct UniFontData;

typedef struct UniFontData UniFontData;

#define CSEG_CACHE_SIZE 8

typedef struct SegData {
    unsigned int start;
    unsigned short glyph;
    unsigned short size;
} SegData;

struct UniFontData {
    struct UniFontData *next_font; /* can be used by the user to link
                                      to next font */
    int nb_glyphs; /* real number of glyphs */
    int nb_glyphs_total; /* total number, including algorithmically
                            generated ones */
    unsigned int flags; 
    int compressed_segment_size;
    int max_width, max_height; /* maximum dimensions of bitmaps */
    int x_res, y_res, pt_size;
    int ascent, descent, line_height;
    int underline_position;  /* in 64th pixel */
    int underline_thickness; /* in 64th pixel */
    char family_name[64];
    int family_type;
    /* compressed segments offsets */
    int nb_csegs;
    int *csegs_offsets;
    int *msegs_offsets;
    /* compressed segment cache */
    struct GlyphSegment *cseg_cache[CSEG_CACHE_SIZE];

    /* segments */
    /* XXX: allocation */
    int nb_segs;
    SegData *seg_table;

    /* memory functions */
    void *mem_opaque;
    void *(*fbf_malloc)(void *mem_opaque, int size);
    void (*fbf_free)(void *mem_opaque, void *ptr);
    
    /* file input handlings */
    void *infile;
    int (*fbf_seek)(void *infile, long pos);
    int (*fbf_read)(void *infile, unsigned char *buf, int len);
    int (*fbf_getc)(void *infile);

    /* bit buffer handling */
    int bitbuf;
    int bitcnt;
    unsigned char ctx1[NB_CTX1];
    /* arithmetic decoder */
    unsigned int alow, arange, adata;

    /* temporary bitmap for composite glyph decoding */
    unsigned char tmp_buffer[MAXWRAP * MAXHEIGHT];
    GlyphEntry tmp_glyph_entry;
};

typedef struct GlyphSegment {
    int first_glyph;
    int nb_glyphs;
    int use_count;
    unsigned char *bitmap_table;
    GlyphEntry metrics[1];
} GlyphSegment;

int fbf_load_font(UniFontData *uf);
void fbf_free_font(UniFontData *uf);

int fbf_unicode_to_glyph(UniFontData *uf, int code);
int fbf_decode_glyph(UniFontData *uf, 
                     GlyphEntry **glyph_entry_ptr,
                     int index);

#endif
