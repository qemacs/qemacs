/*
 * Display system for QEmacs
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

QEDisplay *first_dpy = NULL;

void fill_rectangle(QEditScreen *s,
                    int x1, int y1, int w, int h, QEColor color)
{
    int x2, y2;
    x2 = x1 + w;
    y2 = y1 + h;
    
    /* quick clip rejection */
    if (x2 <= s->clip_x1 || y2 <= s->clip_y1 ||
        x1 >= s->clip_x2 || y1 >= s->clip_y2)
        return;
    
    /* region update */
    if (x2 > s->clip_x2)
        x2 = s->clip_x2;
    if (y2 > s->clip_y2)
        y2 = s->clip_y2;
    if (x1 < s->clip_x1)
        x1 = s->clip_x1;
    if (y1 < s->clip_y1)
        y1 = s->clip_y1;

    /* rejection if zero size */
    if (x1 >= x2 || y1 >= y2)
        return;

    s->dpy.dpy_fill_rectangle(s, x1, y1, x2 - x1, y2 - y1, color);
}

/* set the clip rectangle (and does not clip by the previous one) */
void set_clip_rectangle(QEditScreen *s, CSSRect *r)
{
    int x1, y1, x2, y2;
    x1 = r->x1;
    y1 = r->y1;
    x2 = r->x2;
    y2 = r->y2;
    
    if (x2 > s->width)
        x2 = s->width;
    if (y2 > s->height)
        y2 = s->height;
    if (x1 < 0)
        x1 = 0;
    if (y1 < 0)
        y1 = 0;
    
    s->clip_x1 = x1;
    s->clip_y1 = y1;
    s->clip_x2 = x2;
    s->clip_y2 = y2;

    s->dpy.dpy_set_clip(s, x1, y1, x2 - x1, y2 - y1);
}

void push_clip_rectangle(QEditScreen *s, CSSRect *or, CSSRect *r)
{
    int x1, y1, x2, y2;

    /* save old rectangle */
    or->x1 = s->clip_x1;
    or->y1 = s->clip_y1;
    or->x2 = s->clip_x2;
    or->y2 = s->clip_y2;

    /* load and clip new rectangle against the current one */
    x1 = r->x1;
    y1 = r->y1;
    x2 = r->x2;
    y2 = r->y2;
    
    if (x2 > s->clip_x2)
        x2 = s->clip_x2;
    if (y2 > s->clip_y2)
        y2 = s->clip_y2;
    if (x1 < s->clip_x1)
        x1 = s->clip_x1;
    if (y1 < s->clip_y1)
        y1 = s->clip_y1;
    
    /* set new rectangle */
    s->clip_x1 = x1;
    s->clip_y1 = y1;
    s->clip_x2 = x2;
    s->clip_y2 = y2;

    s->dpy.dpy_set_clip(s, x1, y1, x2 - x1, y2 - y1);
}

int qe_register_display(QEDisplay *dpy)
{
    QEDisplay **p;
    p = &first_dpy;
    while (*p != NULL)
        p = &(*p)->next;
    *p = dpy;
    dpy->next = NULL;
    return 0;
}

QEDisplay *probe_display(void)
{
    QEDisplay *p, *dpy;
    int probe_max, probe;

    p = first_dpy;
    dpy = NULL;
    probe_max = 0;
    while (p != NULL) {
        probe = p->dpy_probe();
        if (probe >= probe_max) {
            probe_max = probe;
            dpy = p;
        }
        p = p->next;
    }
    return dpy;
}

/* simple font cache */

#define FONT_CACHE_SIZE 32
static QEFont *font_cache[FONT_CACHE_SIZE];
static int font_cache_timestamp = 0;

QEFont *select_font(QEditScreen *s, int style, int size)
{
    QEFont *fc;
    int i, min_ts, min_index;

    min_ts = MAXINT;
    min_index = 0;
    for(i=0;i<FONT_CACHE_SIZE;i++) {
        fc = font_cache[i];
        if (fc) {
            if (fc->style == style && fc->size == size) {
                fc->timestamp = font_cache_timestamp;
                goto the_end;
            }
            if (fc->timestamp < min_ts) {
                min_ts = fc->timestamp;
                min_index = i;
            }
        } else {
            min_ts = 0;
            min_index = i;
        }
    }
    /* not found : open new font */
    if (font_cache[min_index])
        close_font(s, font_cache[min_index]);
    fc = open_font(s, style, size);
    if (!fc)
        return NULL;
    fc->style = style;
    fc->size = size;
    fc->timestamp = font_cache_timestamp;
    font_cache[min_index] = fc;
 the_end:
    font_cache_timestamp++;
    return fc;
}

void selection_activate(QEditScreen *s)
{
    if (s->dpy.dpy_selection_activate)
        s->dpy.dpy_selection_activate(s);
}

void selection_request(QEditScreen *s)
{
    if (s->dpy.dpy_selection_request)
        s->dpy.dpy_selection_request(s);
}

QEBitmap *bmp_alloc(QEditScreen *s, int width, int height, int flags)
{
    QEBitmap *b;

    if (!s->dpy.dpy_bmp_alloc)
        return NULL;
    b = malloc(sizeof(QEBitmap));
    if (!b)
        return NULL;
    b->width = width;
    b->height = height;
    b->flags = flags;
    if (s->dpy.dpy_bmp_alloc(s, b) < 0) {
        free(b);
        return NULL;
    }
    return b;
}

void bmp_free(QEditScreen *s, QEBitmap *b)
{
    s->dpy.dpy_bmp_free(s, b);
    free(b);
}

void bmp_draw(QEditScreen *s, QEBitmap *b, 
              int dst_x, int dst_y, int dst_w, int dst_h, 
              int offset_x, int offset_y, int flags)
{
    s->dpy.dpy_bmp_draw(s, b, dst_x, dst_y, dst_w, dst_h, 
                        offset_x, offset_y, flags);
}

/* used to access the bitmap data. Return the necessary pointers to
   modify the image in 'pict'. */
void bmp_lock(QEditScreen *s, QEBitmap *bitmap, QEPicture *pict,
              int x1, int y1, int w1, int h1)
{
    s->dpy.dpy_bmp_lock(s, bitmap, pict, x1, y1, w1, h1);
}

void bmp_unlock(QEditScreen *s, QEBitmap *bitmap)
{
    s->dpy.dpy_bmp_unlock(s, bitmap);
}

#if 0
/* bitmap cache */
typedef struct QECachedBitmap {
    QEBitmap *bitmap;
    char url[1];
    int refcount;
    struct QECachedBitmap *next;
} QECachedBitmap;

/* dst_w or dst_h can be zero if unspecified */
/* XXX: add scaling for printing */
QECachedBitmap *cbmp_open(QEditScreen *s, 
                          const char *url,
                          int dst_w, int dst_h)
{
}

/* return the DESTINATION size of the bitmap. return non zero if
   unknown size */
int cbmp_get_size(QECachedBitmap *bitmap, int *w_ptr, int *h_ptr)
{
}

void cbmp_draw(QEditScreen *s, QECachedBitmap *bitmap, 
               int dst_x, int dst_y, int dst_w, int dst_h, 
               int offset_x, int offset_y, int flags)
{
}

void cbmp_close(QEditScreen *cbmp)
{
}
#endif
