/*
 * fbfrender - FBF font cache and renderer
 * 
 * Copyright (c) 2001, 2002 Fabrice Bellard
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

#include "fbfrender.h"
#include "libfbf.h"

//#define CONFIG_FILE_FONTS

static UniFontData *first_font;
static UniFontData *fallback_font;

/**********************************************/
/* glyph cache handling */
#define HASH_SIZE      263
#define MAX_CACHE_SIZE (256 * 1024)

static GlyphCache *hash_table[HASH_SIZE];
static int cache_size = 0;
static GlyphCache first_cache_entry;

void glyph_cache_init(void)
{
    first_cache_entry.next = &first_cache_entry;
    first_cache_entry.prev = &first_cache_entry;
}

static inline int glyph_hash(int index, int size, int style)
{
    unsigned int h;

    h = index + (style << 24) + (size << 16);
    return h % HASH_SIZE;
}


static GlyphCache *get_cached_glyph(QEFont *font, int index)
{
    GlyphCache *p;
    int h;

    h = glyph_hash(index, font->size, font->style);
    p = hash_table[h];
    while (p != NULL) {
        if (p->index == index &&
            p->size == font->size &&
            p->style == font->style)
            goto found;
        p = p->hash_next;
    }
    return NULL;
 found:
    /* suppress in linked list */
    p->next->prev = p->prev;
    p->prev->next = p->next;
    /* move to front */
    first_cache_entry.next->prev = p;
    p->next = first_cache_entry.next;
    first_cache_entry.next = p;
    p->prev = &first_cache_entry;

    return p;
}

static GlyphCache *add_cached_glyph(QEFont *font, int index, int data_size)
{
    GlyphCache **pp, *p;
    int h;

    cache_size += sizeof(GlyphCache) + data_size + 4;

    while (cache_size > MAX_CACHE_SIZE) {
        /* supress oldest entry */
        p = first_cache_entry.prev;
        cache_size -= sizeof(GlyphCache) + p->data_size + 4;
        p->next->prev = p->prev;
        p->prev->next = p->next;
        /* suppress from hash table (should use double linked list ?) */
        h = glyph_hash(p->index, p->size, p->style);
        pp = &hash_table[h];
        while (*pp != p) pp = &(*pp)->hash_next;
        *pp = p->hash_next;

        free(p);
    }

    p = malloc(sizeof(GlyphCache) + data_size);
    if (!p)
        return NULL;

    h = glyph_hash(index, font->size, font->style);
    pp = &hash_table[h];
    p->hash_next = *pp;
    *pp = p;
    p->index = index;
    p->size = font->size;
    p->style = font->style;
    p->data_size = data_size;
    p->private = NULL;
    first_cache_entry.next->prev = p;
    p->next = first_cache_entry.next;
    first_cache_entry.next = p;
    p->prev = &first_cache_entry;

    return p;
}

GlyphCache *fbf_decode_glyph1(QEFont *font, int code)
{
    UniFontData *uf = font->private;
    int glyph_index, size, src_width, src_height;
    GlyphCache *glyph_cache;
    GlyphEntry *fbf_glyph_entry;

    glyph_index = fbf_unicode_to_glyph(uf, code);
    if (glyph_index < 0)
        return NULL;

    /* decompress a glyph if needed */
    if (fbf_decode_glyph(uf, 
                         &fbf_glyph_entry,
                         glyph_index) < 0)
        return NULL;
    src_width = fbf_glyph_entry->w;
    src_height = fbf_glyph_entry->h;
    
    size = src_width * src_height;
    glyph_cache = add_cached_glyph(font, code, size);
    if (!glyph_cache) 
            return NULL;
    /* convert to bitmap */
    {
        int x, y, bit, pitch;
        unsigned char *bitmap;
        
        bitmap = fbf_glyph_entry->bitmap;
        pitch = (src_width + 7) >> 3;
        for(y=0;y<src_height;y++) {
            for(x=0;x<src_width;x++) {
                bit = (bitmap[pitch * y + (x >> 3)] >> 
                           (7 - (x & 7))) & 1;
                glyph_cache->data[src_width * y + x] = -bit;
                }
        }
    }
    
    glyph_cache->w = src_width;
    glyph_cache->h = src_height;
    glyph_cache->x = fbf_glyph_entry->x;
    glyph_cache->y = fbf_glyph_entry->y;
    glyph_cache->xincr = fbf_glyph_entry->xincr;
    return glyph_cache;
}


/* 
 * main function : get one glyph. Return -1 if no glyph found, 0 if OK.
 */
GlyphCache *decode_cached_glyph(QEditScreen *s, QEFont *font, int code)
{
    GlyphCache *g;
    QEFont *font1;

    g = get_cached_glyph(font, code);
    if (!g) {
        g = fbf_decode_glyph1(font, code);
        if (!g) {
            /* try with fallback font */
            font1 = select_font(s, font->style | (1 << QE_FAMILY_FALLBACK_SHIFT),
                                font->size);
            g = fbf_decode_glyph1(font1, code);
            if (!g) 
                return NULL;
            /* indicates that it is a fallback glyph so that the
               correct font height can be computed */
            g->is_fallback = 1;
        } else {
            g->is_fallback = 0;
        }
    }
    return g;
}

void fbf_text_metrics(QEditScreen *s, QEFont *font, 
                      QECharMetrics *metrics,
                      const unsigned int *str, int len)
{
    GlyphCache *g;
    int i, x;
    unsigned int cc;

    metrics->font_ascent = font->ascent;
    metrics->font_descent = font->descent;
    x = 0;
    for (i = 0;i < len; i++) {
        cc = str[i];
        g = decode_cached_glyph(s, font, cc);
        if (g) {
            x += g->xincr;
            if (g->is_fallback) {
                /* if alternate font used, modify metrics */
                metrics->font_ascent = max(metrics->font_ascent, 
                                           fallback_font->ascent);
                metrics->font_descent = max(metrics->font_descent, 
                                            fallback_font->descent);
            }
        }
    }
    metrics->width = x;
}

#define MAX_MATCHES 32

QEFont *fbf_open_font(QEditScreen *s, int style, int size)
{
    QEFont *font;
    UniFontData *uf, *uf_found;
    int d, dmin;
    UniFontData *fonts[MAX_MATCHES];
    int nb_fonts, i;

    font = malloc(sizeof(QEFont));
    if (!font)
        return NULL;

    if ((style & QE_FAMILY_FALLBACK_MASK) != 0) {
        uf_found = fallback_font;
    } else {
        /* convert to unifont family types */
        style = ((style & QE_FAMILY_MASK) >> QE_FAMILY_SHIFT) - 1;
        
        /* first match style */
        nb_fonts = 0;
        for(uf = first_font; uf != NULL; uf = uf->next_font) {
            if (uf->family_type == style) {
                fonts[nb_fonts++] = uf;
            }
        }
        if (!nb_fonts) {
            uf_found = first_font;
        } else {
            /* select closest size */
            uf_found = NULL;
            dmin = MAXINT;
            for(i = 0;i < nb_fonts;i++) {
                uf = fonts[i];
                d = abs(uf->pt_size - size);
                if (d < dmin) {
                    uf_found = uf;
                    dmin = d;
                }
            }
        }
    }
    font->private = uf_found;
    font->ascent = uf_found->ascent;
    font->descent = uf_found->descent;
    return font;
}

void fbf_close_font(QEditScreen *s, QEFont *font)
{
    free(font);
}

void *my_malloc(void *opaque, int size)
{
    return malloc(size);
}

void my_free(void *opaque, void *ptr)
{
    free(ptr);
}


#ifdef CONFIG_FILE_FONTS

/* disk font opening */

int my_fbf_seek(void *infile, long pos)
{
    return fseek((FILE *)infile, pos, SEEK_SET);
}

int my_fbf_read(void *infile, unsigned char *buf, int len)
{
    return fread(buf, 1, len, (FILE *)infile);
}

int my_fbf_getc(void *infile)
{
    return fgetc((FILE *)infile);
}

static int fbf_load_font_file(const char *filename)
{
    UniFontData *uf;
    FILE *f;

    printf("loading %s\n", filename);

    f = fopen(filename, "r");
    if (!f) 
        return -1;

    uf = malloc(sizeof(UniFontData));
    if (!uf) {
        fclose(f);
        return -1;
    }
    memset(uf, 0, sizeof(*uf));

    /* init memory */
    uf->mem_opaque = NULL;
    uf->fbf_malloc = my_malloc;
    uf->fbf_free = my_free;

    /* init file I/O */
    uf->infile = f;
    uf->fbf_seek = my_fbf_seek;
    uf->fbf_read = my_fbf_read;
    uf->fbf_getc = my_fbf_getc;

    if (fbf_load_font(uf) < 0) {
        free(uf);
        fclose(f);
        return -1;
    }

    /* add font */
    uf->next_font = first_font;
    first_font = uf;
    return 0;
}

int fbf_render_init(const char *font_path)
{
    FindFileState *ffs;
    char filename[1024];

    glyph_cache_init();
    first_font = NULL;

    ffs = find_file_open(font_path, "*.fbf");
    if (!ffs)
        return -1;

    for(;;) {
        if (find_file_next(ffs, filename, sizeof(filename)))
            break;
        if (fbf_load_font_file(filename) < 0) {
            fprintf(stderr, "Could not load font '%s'\n", filename);
        }
    }
    return 0;
}

void fbf_render_cleanup(void)
{
    UniFontData *uf, *uf1;

    for(uf = first_font; uf != NULL; uf = uf1) {
        uf1 = uf->next_font;
        /* close font data structures */
        fbf_free_font(uf);
        /* close font file */
        fclose(uf->infile);
    }
    first_font = NULL;
}

#else

typedef struct MemoryFile {
    const unsigned char *base;
    int offset;
    int size;
} MemoryFile;

int my_fbf_seek(void *infile, long pos)
{
    MemoryFile *f = infile;
    f->offset = pos;
    return pos;
}

int my_fbf_read(void *infile, unsigned char *buf, int len)
{
    MemoryFile *f = infile;
    int len1;
    len1 = f->size - f->offset;
    if (len > len1)
        len = len1;
    memcpy(buf, f->base + f->offset, len);
    f->offset += len;
    return len;
}

int my_fbf_getc(void *infile)
{
    MemoryFile *f = infile;
    if (f->offset < f->size)
        return f->base[f->offset++];
    else
        return EOF;
}

static int fbf_load_font_memory(const unsigned char *data,
                                int data_size)
{
    UniFontData *uf;
    MemoryFile *f;

    f = malloc(sizeof(MemoryFile));
    if (!f)
        return -1;
    f->base = data;
    f->size = data_size;
    f->offset = 0;
    
    uf = malloc(sizeof(UniFontData));
    if (!uf) {
        free(f);
        return -1;
    }
    memset(uf, 0, sizeof(*uf));

    /* init memory */
    uf->mem_opaque = NULL;
    uf->fbf_malloc = my_malloc;
    uf->fbf_free = my_free;

    /* init file I/O */
    uf->infile = f;
    uf->fbf_seek = my_fbf_seek;
    uf->fbf_read = my_fbf_read;
    uf->fbf_getc = my_fbf_getc;

    if (fbf_load_font(uf) < 0) {
        free(uf);
        free(f);
        return -1;
    }

    /* add font */
    uf->next_font = first_font;
    first_font = uf;

    /* we consider that unifont is the fallback font */
    /* XXX: use generic system as in x11.c */
    if (!strcasecmp(uf->family_name, "unifont"))
        fallback_font = uf;
    return 0;
}

extern const void *fbf_fonts[];

int fbf_render_init(const char *font_path)
{
    const void **pp;

    glyph_cache_init();
    first_font = NULL;
    for(pp = fbf_fonts; *pp != NULL; pp += 2) {
        fbf_load_font_memory(pp[0], (int)pp[1]);
    }
    if (!fallback_font)
        fallback_font = first_font;
    return 0;
}

void fbf_render_cleanup(void)
{
    UniFontData *uf, *uf1;

    for(uf = first_font; uf != NULL; uf = uf1) {
        uf1 = uf->next_font;
        /* close font data structures */
        fbf_free_font(uf);
        /* close font file */
        free(uf->infile);
        free(uf);
    }
    first_font = NULL;
}

#endif
