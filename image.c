/*
 * Image mode for QEmacs.
 * Copyright (c) 2002, 2003 Fabrice Bellard.
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
#include "avformat.h"

typedef struct ImageBuffer {
    int pix_fmt;
    int width;
    int height;
    int interleaved; /* just an info to tell if the image is interleaved */
    int alpha_info;  /* see FF_ALPHA_xxx constants */
    AVPicture pict; /* NOTE: data[] fields are temporary */
} ImageBuffer;

typedef struct ImageState {
    QEBitmap *disp_bmp;
    int x, y; /* position of the center of the image in window */
    int w, h; /* displayed size */
    int xfactor_num, xfactor_den;
    int yfactor_num, yfactor_den;
    QEColor background_color; /* transparent to display tiles */
} ImageState;

extern EditBufferDataType image_data_type;

int qe_bitmap_format_to_pix_fmt(int format)
{
    int dst_pix_fmt;

    switch(format) {
    case QEBITMAP_FORMAT_YUV420P:
        dst_pix_fmt = PIX_FMT_YUV420P;
        break;
    case QEBITMAP_FORMAT_RGB555:
        dst_pix_fmt = PIX_FMT_RGB555;
        break;
    default:
    case QEBITMAP_FORMAT_RGB565:
        dst_pix_fmt = PIX_FMT_RGB565;
        break;
    case QEBITMAP_FORMAT_RGB24:
        dst_pix_fmt = PIX_FMT_RGB24;
        break;
    case QEBITMAP_FORMAT_RGBA32:
        dst_pix_fmt = PIX_FMT_RGBA32;
        break;
    }
    return dst_pix_fmt;
}

static void image_callback(EditBuffer *b,
                           void *opaque,
                           enum LogOperation op,
                           int offset,
                           int size);

/* draw only the border of a rectangle */
void fill_border(EditState *s, int x, int y, int w, int h, int color)
{
    int w1, w2, h1, h2;

    /* fill the background */
    w1 = x;
    if (w1 < 0)
        w1 = 0;
    w2 = s->width - (x + w);
    if (w2 < 0)
        w2 = 0;
    h1 = y;
    if (h1 < 0)
        h1 = 0;
    h2 = s->height - (y + h);
    if (h2 < 0)
        h2 = 0;
    fill_rectangle(s->screen, 
                   s->xleft, s->ytop, 
                   w1, s->height, 
                   color);
    fill_rectangle(s->screen, 
                   s->xleft + s->width - w2, s->ytop, 
                   w2, s->height, 
                   color);
    fill_rectangle(s->screen, 
                   s->xleft + w1, s->ytop, 
                   s->width - w1 - w2, h1, 
                   color);
    fill_rectangle(s->screen, 
                   s->xleft + w1, s->ytop + s->height - h2,
                   s->width - w1 - w2, h2,
                   color);
}

void draw_alpha_grid(EditState *s, int x1, int y1, int w, int h)
{
    int state, x, y;
    unsigned int color;
    
    for(y = 0; y < h; y += 16) {
        for(x = 0; x < w; x += 16) {
            state = (x ^ y) & 16;
            if (state)
                color = QERGB(0x94, 0x94, 0x94);
            else
                color = QERGB(0x64, 0x64, 0x64);
            fill_rectangle(s->screen, 
                           x1 + x, y1 + y, 16, 16, color);
        }
    }
}

/* transp: 0x94 and 0x64, 16x16 grid */

static void image_display(EditState *s)
{
    ImageState *is = s->mode_data;
    int x, y;
    
    if (s->display_invalid) {
        if (is->disp_bmp) {
            x = is->x + (s->width - is->w) / 2;
            y = is->y + (s->height - is->h) / 2;

            fill_border(s, x, y, is->w, is->h, QERGB(0x00, 0x00, 0x00));

            bmp_draw(s->screen, is->disp_bmp, 
                     s->xleft + x, s->ytop + y,
                     is->w, is->h, 0, 0, 0);
        }
        s->display_invalid = 0;
    }
}

#if 0

static int gcd(int a, int b)
{
    int c;

    while (1) {
        c = a % b;
        if (c == 0)
            return b;
        a = b;
        b = c;
    }
}


/* resize current image using the current factors */
static void image_resize(EditState *s)
{
    ImageState *is = s->mode_data;
    int d, w, h;
    
    /* simplify factors */
    d = gcd(is->xfactor_num, is->xfactor_den);
    is->xfactor_num /= d;
    is->xfactor_den /= d;

    d = gcd(is->yfactor_num, is->yfactor_den);
    is->yfactor_num /= d;
    is->yfactor_den /= d;

    w = ((long long)is->img.width * (long long)is->xfactor_num) / 
        is->xfactor_den;
    h = ((long long)is->img.height * (long long)is->yfactor_num) / 
        is->yfactor_den;

    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;

    /* if no resize needed, exit */
    if (w == is->w &&
        h == is->h)
        return;
}


static void image_normal_size(EditState *s)
{
    ImageState *is = s->mode_data;

    is->xfactor_num = 1;
    is->xfactor_den = 1;
    is->yfactor_num = 1;
    is->yfactor_den = 1;

    image_resize(s);
}


/* increase or decrease image size by percent */
static void image_mult_size(EditState *s, int percent)
{
    ImageState *is = s->mode_data;

    is->xfactor_num *= (100 + percent);
    is->xfactor_den *= 100;
    is->yfactor_num *= (100 + percent);
    is->yfactor_den *= 100;
    image_resize(s);
}

static void image_set_size(EditState *s, int w, int h)
{
    ImageState *is = s->mode_data;

    if (w < 1 || h < 1) {
        put_status(s, "Invalid image size");
        return;
    }
    
    is->xfactor_num = w;
    is->xfactor_den = is->img.width;
    is->yfactor_num = h;
    is->yfactor_den = is->img.height;
    
    image_resize(s);
}
#endif

static int image_mode_probe(ModeProbeData *pd)
{
    AVProbeData avpd;
    AVImageFormat *fmt;
    
    avpd.filename = pd->filename;
    avpd.buf = pd->buf;
    avpd.buf_size = pd->buf_size;
    fmt = av_probe_image_format(&avpd);
    if (!fmt)
        return 0;
    else
        return 100;
}

/* allocate a new image at the end of the buffer */
static ImageBuffer *image_allocate(int pix_fmt, int width, int height)
{
    unsigned char *ptr;
    int size;
    ImageBuffer *ib;

    ib = malloc(sizeof(ImageBuffer));
    if (!ib)
        return NULL;
    memset(ib, 0, sizeof(ImageBuffer));

    size = avpicture_get_size(pix_fmt, width, height);
    if (size < 0)
        goto fail;
    ptr = malloc(size);
    if (!ptr) {
    fail:
        free(ib);
        return NULL;
    }

    avpicture_fill(&ib->pict, ptr, pix_fmt, width, height);
    ib->pix_fmt = pix_fmt;
    ib->width = width;
    ib->height = height;
    return ib;
}

static void image_free(ImageBuffer *ib)
{
    free(ib->pict.data[0]);
    free(ib);
}


static int read_image_cb(void *opaque, AVImageInfo *info)
{
    EditBuffer *b = opaque;
    ImageBuffer *ib;
    int i;

    ib = image_allocate(info->pix_fmt, info->width, info->height);
    if (!ib)
        return AVERROR_NOMEM;
    ib->interleaved = info->interleaved;
    b->data = ib;
    for(i=0;i<4;i++) {
        info->pict.linesize[i] = ib->pict.linesize[i];
        info->pict.data[i] = ib->pict.data[i];
    }
    return 0;
}

static int image_buffer_load(EditBuffer *b, FILE *f)
{
    ByteIOContext pb1, *pb = &pb1;
    int ret;

    /* start loading the image */
    ret = url_fopen(pb, b->filename, URL_RDONLY);
    if (ret < 0)
        return -1;
    
    ret = av_read_image(pb, b->filename, NULL, read_image_cb, b);
    url_fclose(pb);
    if (ret) {
        return -1;
    } else {
        ImageBuffer *ib = b->data;
        ib->alpha_info = img_get_alpha_info(&ib->pict, ib->pix_fmt, 
                                            ib->width, ib->height);
        return 0;
    }
}

static void set_new_image(EditBuffer *b, ImageBuffer *ib)
{
    b->data = ib;
    eb_invalidate_raw_data(b);
    b->modified = 1;
}

static int image_buffer_save(EditBuffer *b, const char *filename)
{
    ByteIOContext pb1, *pb = &pb1;
    ImageBuffer *ib = b->data;
    ImageBuffer *ib1 = NULL;
    int ret, dst_pix_fmt, loss;
    AVImageFormat *fmt;
    AVImageInfo info;
    
    /* find image format */
    fmt = guess_image_format(filename);
    if (!fmt)
        return -1;

    /* find the best image format */
    dst_pix_fmt = avcodec_find_best_pix_fmt(fmt->supported_pixel_formats, 
                                            ib->pix_fmt, ib->alpha_info, &loss);
    if (dst_pix_fmt < 0)
        return -1;
    /* convert to new format if needed */
    if (dst_pix_fmt != ib->pix_fmt) {
        ib1 = image_allocate(dst_pix_fmt, ib->width, ib->height);
        if (!ib1)
            return -1;
        if (img_convert(&ib1->pict, ib1->pix_fmt,
                        &ib->pict, ib->pix_fmt, ib->width, ib->height) < 0)
            return -1;

        set_new_image(b, ib1);
        image_free(ib);
        ib = ib1;
    }

    /* start saving the image */
    ret = url_fopen(pb, filename, URL_WRONLY);
    if (ret < 0)
        return -1;
    memset(&info, 0, sizeof(AVImageInfo));
    info.pix_fmt = ib->pix_fmt;
    info.width = ib->width;
    info.height = ib->height;
    info.pict = ib->pict;
    
    av_write_image(pb, fmt, &info);
    url_fclose(pb);
    
    return 0;
}

static void image_buffer_close(EditBuffer *b)
{
    ImageBuffer *ib = b->data;
    image_free(ib);
}


static void update_bmp(EditState *s)
{
    ImageState *is = s->mode_data;
    ImageBuffer *ib = s->b->data;
    QEPicture pict;
    AVPicture avpict;
    ImageBuffer *ib1;
    int dst_pix_fmt;
    int i;

    if (is->disp_bmp) {
        bmp_free(s->screen, is->disp_bmp);
        is->disp_bmp = NULL;
    }

    /* combine with the appropriate background if alpha is present */
    ib1 = NULL;
    if (ib->alpha_info) {
        int x, y, state, linesize;
        unsigned int r, g, b, a, bg_r, bg_g, bg_b, v;
        uint8_t *d, *d1;

        ib1 = image_allocate(PIX_FMT_RGBA32, is->w, is->h);
        if (!ib1)
            goto next;
        img_convert(&ib1->pict, ib1->pix_fmt,
                    &ib->pict, ib->pix_fmt, ib->width, ib->height);
        linesize = ib1->pict.linesize[0];
        d1 = ib1->pict.data[0];
        bg_r = (is->background_color >> 16) & 0xff;
        bg_g = (is->background_color >> 8) & 0xff;
        bg_b = (is->background_color) & 0xff;
        for(y = 0; y < is->h; y++) {
            d = d1;
            for(x = 0; x < is->w; x++) {
                if (is->background_color == 0) {
                    state = (x ^ y) & 16;
                    if (state) {
                        bg_r = bg_g = bg_b = 0x94;
                    } else {
                        bg_r = bg_g = bg_b = 0x64;
                    }
                }
                v = ((uint32_t *)d)[0];
                a = (v >> 24) & 0xff;
                r = (v >> 16) & 0xff;
                g = (v >> 8) & 0xff;
                b = (v) & 0xff;
                r = (bg_r * (256 - a) + r * a) >> 8;
                g = (bg_g * (256 - a) + g * a) >> 8;
                b = (bg_b * (256 - a) + b * a) >> 8;
                ((uint32_t *)d)[0] = (0xff << 24) | (r << 16) | (g << 8) | b;
                d += 4;
            }
            d1 += linesize;
        }
        ib = ib1;
    next: ;
    }

    /* create the displayed bitmap and put the image in it */
    is->disp_bmp = bmp_alloc(s->screen, is->w, is->h, 0);

    bmp_lock(s->screen, is->disp_bmp, &pict, 
             0, 0, is->w, is->h);
    
    for(i=0;i<4;i++) {
        avpict.data[i] = pict.data[i];
        avpict.linesize[i] = pict.linesize[i];
    }
    dst_pix_fmt = qe_bitmap_format_to_pix_fmt(is->disp_bmp->format);
#if 0
    printf("dst=%s src=%s\n", 
           avcodec_get_pix_fmt_name(dst_pix_fmt),
           avcodec_get_pix_fmt_name(ib->pix_fmt));
#endif
    if (img_convert(&avpict, dst_pix_fmt,
                    &ib->pict, ib->pix_fmt, ib->width, ib->height) < 0) {
        printf("Cannot convert from %s to %s\n",
               avcodec_get_pix_fmt_name(ib->pix_fmt),
               avcodec_get_pix_fmt_name(dst_pix_fmt));
    }
    bmp_unlock(s->screen, is->disp_bmp);
    if (ib1)
        image_free(ib1);
    edit_invalidate(s);
}

static int image_mode_init(EditState *s, ModeSavedData *saved_data)
{
    ImageState *is = s->mode_data;
    ImageBuffer *ib = s->b->data;

    is->w = ib->width;
    is->h = ib->height;
    is->xfactor_num = 1;
    is->xfactor_den = 1;
    is->yfactor_num = 1;
    is->yfactor_den = 1;
    is->background_color = 0; /* display tiles */

    update_bmp(s);

    eb_add_callback(s->b, image_callback, s);
    return 0;
}

static void update_pos(EditState *s, int dx, int dy)
{
    ImageState *is = s->mode_data;
    int delta;

    is->x += dx;
    delta = (s->width - is->w) / 2;
    if (delta < 0) {
        if (is->x + delta > 0)
            is->x = -delta;
        else if (is->x + delta + is->w < s->width)
            is->x = s->width - is->w - delta;
    } else {
        is->x = 0;
    }

    is->y += dy;
    delta = (s->height - is->h) / 2;
    if (delta < 0) {
        if (is->y + delta > 0)
            is->y = -delta;
        else if (is->y + delta + is->h < s->height)
            is->y = s->height - is->h - delta;
    } else {
        is->y = 0;
    }
    edit_invalidate(s);
}

static void image_move_left_right(EditState *s, int dir)
{
    int d;

    /* move 10% */
    d = s->width / 10;
    if (d < 1)
        d = 1;
    update_pos(s, -dir * d, 0);
}

static void image_move_up_down(EditState *s, int dir)
{
    int d;

    /* move 10% */
    d = s->height / 10;
    if (d < 1)
        d = 1;
    update_pos(s, 0, -dir * d);
}

static void image_scroll_up_down(EditState *s, int dir)
{
    int d;

    /* move 50% */
    d = s->height / 2;
    if (d < 1)
        d = 1;
    update_pos(s, 0, -dir * d);
}

static void image_mode_close(EditState *s)
{
    ImageState *is = s->mode_data;

    if (is->disp_bmp) {
        bmp_free(s->screen, is->disp_bmp);
        is->disp_bmp = NULL;
    }
    eb_free_callback(s->b, image_callback, s);
}

/* when the image is modified, reparse it */
static void image_callback(EditBuffer *b,
                          void *opaque,
                          enum LogOperation op,
                          int offset,
                          int size)
{
    //    EditState *s = opaque;
    
    //    update_bmp(s);
}

static int img_rotate(AVPicture *dst,
                      AVPicture *src, int pix_fmt, 
                      int w, int h)
{
    int x, y, dlinesize, bpp;
    uint8_t *d, *d1;
    const uint8_t *s, *s1;

    switch(pix_fmt) {
    case PIX_FMT_GRAY8:
    case PIX_FMT_PAL8:
        bpp = 1;
        break;
    case PIX_FMT_RGB24:
    case PIX_FMT_BGR24:
        bpp = 3;
        break;
    case PIX_FMT_RGBA32:
        bpp = 4;
        break;
    default:
        return -1;
    }

    s1 = src->data[0];
    dlinesize = dst->linesize[0];
    d1 = dst->data[0] + (h - 1) * bpp;
    
    for(y=0;y<h;y++) {
        s = s1;
        d = d1;
        
        switch(pix_fmt) {
        case PIX_FMT_PAL8:
        case PIX_FMT_GRAY8:
            for(x=0;x<w;x++) {
                d[0] = s[0];
                s++;
                d += dlinesize;
            }
            break;
        case PIX_FMT_RGB24:
        case PIX_FMT_BGR24:
            for(x=0;x<w;x++) {
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
                s += 3;
                d += dlinesize;
            }
            break;
        case PIX_FMT_RGBA32:
            for(x=0;x<w;x++) {
                ((uint32_t *)d)[0] = ((uint32_t *)s)[0];
                s += 4;
                d += dlinesize;
            }
            break;
        default:
            break;
        }

        s1 += src->linesize[0];
        d1 -= bpp;
    }

    if (pix_fmt == PIX_FMT_PAL8) {
        /* copy palette */
        memcpy(dst->data[1], src->data[1], 256 * sizeof(uint32_t));
    }
    return 0;
}

static void image_rotate(EditState *e)
{
    ImageState *is = e->mode_data;
    EditBuffer *b = e->b;
    ImageBuffer *ib = b->data;
    int ret, w, h, pix_fmt;
    ImageBuffer *ib1;

    pix_fmt = ib->pix_fmt;
    w = ib->width;
    h = ib->height;
    ib1 = image_allocate(pix_fmt, h, w);
    if (!ib1)
        return;

    /* need to reget image because buffer address may have changed */
    ret = img_rotate(&ib1->pict, &ib->pict, pix_fmt, w, h);
    if (ret < 0) {
        /* remove temporary image */
        image_free(ib1);
        put_status(e, "Format '%s' not supported yet in rotate", 
                   avcodec_get_pix_fmt_name(pix_fmt));
        return;
    }
    ib1->alpha_info = ib->alpha_info;
    set_new_image(b, ib1);
    image_free(ib);
    /* temporary */
    is->w = h;
    is->h = w;
    /* suppress that and use callback */
    update_bmp(e);
}

static void image_set_background_color(EditState *e, const char *color_str)
{
    ImageState *is = e->mode_data;

    css_get_color(&is->background_color, color_str);
    update_bmp(e);
}

static void image_convert(EditState *e, const char *pix_fmt_str)
{
    EditBuffer *b = e->b;
    ImageBuffer *ib = b->data;
    int ret, new_pix_fmt, i, loss;
    ImageBuffer *ib1;
    const char *name;
    
    for(i = 0; i < PIX_FMT_NB; i++) {
        name = avcodec_get_pix_fmt_name(i);
        if (!strcmp(pix_fmt_str, name))
            goto found;
    }
    put_status(e, "Unknown pixel format");
    return;
 found:
    new_pix_fmt = i;
    ib1 = image_allocate(new_pix_fmt, ib->width, ib->height);
    if (!ib1)
        return;
    ret = img_convert(&ib1->pict, ib1->pix_fmt, 
                      &ib->pict, ib->pix_fmt, ib->width, ib->height);
    if (ret < 0) {
        /* remove temporary image */
        image_free(ib1);
        put_status(e, "Convertion from '%s' to '%s' not supported yet", 
                   avcodec_get_pix_fmt_name(ib->pix_fmt),
                   avcodec_get_pix_fmt_name(new_pix_fmt));
        return;
    } else {
        char buf[128];
        loss = avcodec_get_pix_fmt_loss(new_pix_fmt, ib->pix_fmt, ib->alpha_info);
        if (loss != 0) {
            buf[0] = '\0';
            if (loss & FF_LOSS_RESOLUTION)
                strcat(buf, " res");
            if (loss & FF_LOSS_DEPTH)
                strcat(buf, " depth");
            if (loss & FF_LOSS_COLORSPACE)
                strcat(buf, " colorspace");
            if (loss & FF_LOSS_ALPHA)
                strcat(buf, " alpha");
            if (loss & FF_LOSS_COLORQUANT)
                strcat(buf, " colorquant");
            if (loss & FF_LOSS_CHROMA)
                strcat(buf, " chroma");
            put_status(e, "Warning: data loss:%s", buf);
        }
    }
    ib1->alpha_info = img_get_alpha_info(&ib1->pict, ib1->pix_fmt, 
                                        ib1->width, ib1->height);
    set_new_image(b, ib1);
    image_free(ib);
    /* suppress that and use callback */
    update_bmp(e);
}

void image_mode_line(EditState *s, char *buf, int buf_size)
{
    char *q;
    EditBuffer *b = s->b;
    ImageBuffer *ib = b->data;
    char alpha_mode;
    
    basic_mode_line(s, buf, buf_size, '-');
    q = buf + strlen(buf);
    if (ib->alpha_info & FF_ALPHA_SEMI_TRANSP)
        alpha_mode = 'A';
    else if (ib->alpha_info & FF_ALPHA_TRANSP)
        alpha_mode = 'T';
    else
        alpha_mode = ' ';

    q += sprintf(q, "%dx%d %s %c%c", 
                 ib->width, ib->height, 
                 avcodec_get_pix_fmt_name(ib->pix_fmt),
                 alpha_mode,
                 ib->interleaved ? 'I' : ' ');
}

static void pixel_format_completion(StringArray *cs, const char *str)
{
    int len, i;
    const char *name;
    
    len = strlen(str);
    for(i = 0; i < PIX_FMT_NB; i++) {
        name = avcodec_get_pix_fmt_name(i);
        if (!strncmp(name, str, len))
            add_string(cs, name);
    }
}

/* specific image commands */
static CmdDef image_commands[] = {
    CMD0( 't', KEY_NONE, "image-rotate", image_rotate)
    CMD( 'c', KEY_NONE, "image-convert\0s{New pixel format: }[pixel_format]|pixel_format|", image_convert)
    CMD( 'b', KEY_NONE, "image-set-background-color\0s{Background color (use 'transparent' for tiling): }", 
         image_set_background_color)
#if 0
    CMD0( 'n', KEY_NONE, "image-normal-size", image_normal_size)
    CMD1( '>', KEY_NONE, "image-double-size", image_mult_size, 100)
    CMD1( '<', KEY_NONE, "image-halve-size", image_mult_size, -50)
    CMD1( '.', KEY_NONE, "image-larger-10", image_mult_size, 10)
    CMD1( ',', KEY_NONE, "image-smaller-10", image_mult_size, -10)
    CMD( 'S', KEY_NONE, "image-set-display-size\0i{Displayed width: }i{Displayed height: }", image_set_size)
#endif
    CMD_DEF_END,
};

ModeDef image_mode = {
    "image", 
    instance_size: sizeof(ImageState),
    mode_probe: image_mode_probe,
    mode_init: image_mode_init,
    mode_close: image_mode_close,
    display: image_display,
    move_up_down: image_move_up_down,
    move_left_right: image_move_left_right,
    scroll_up_down: image_scroll_up_down,
    data_type: &image_data_type,
    mode_line: image_mode_line,
};

static EditBufferDataType image_data_type = {
    "image",
    image_buffer_load,
    image_buffer_save,
    image_buffer_close,
};

static int image_init(void)
{
    av_register_all();
    eb_register_data_type(&image_data_type);
    qe_register_mode(&image_mode);
    qe_register_cmd_table(image_commands, "image");
    register_completion("pixel_format", pixel_format_completion);
    /* additionnal mode specific keys */
    qe_register_binding('f', "toggle-full-screen", "image");
    return 0;
}

qe_module_init(image_init);
