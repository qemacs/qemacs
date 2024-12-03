/*
 * Image mode for QEmacs.
 *
 * Copyright (c) 2002-2003 Fabrice Bellard.
 * Copyright (c) 2003-2024 Charlie Gordon.
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

#include "qe.h"
#include "avformat.h"

#define SCROLL_MHEIGHT     10

typedef struct ImageBuffer ImageBuffer;
typedef struct ImageState ImageState;
typedef struct ImageBufferState ImageBufferState;

struct ImageBufferState {
    QEModeData base;
    ImageBuffer *ib;
};

struct ImageBuffer {
    int pix_fmt;
    int width;
    int height;
    int interleaved; /* just an info to tell if the image is interleaved */
    int alpha_info;  /* see FF_ALPHA_xxx constants */
    AVPicture pict; /* NOTE: data[] fields are temporary */
};

struct ImageState {
    QEModeData base;
    ImageBufferState *ibs;
    QEBitmap *disp_bmp;
    int x, y; /* position of the center of the image in window */
    int w, h; /* displayed size */
    int xfactor_num, xfactor_den;
    int yfactor_num, yfactor_den;
    /* XXX: should also have a current zone with mark(x/y) and cur(x/y) */
    QEColor background_color; /* transparent to display tiles */
};

int qe_bitmap_format_to_pix_fmt(int format)
{
    int dst_pix_fmt;

    switch (format) {
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

static void image_callback(EditBuffer *b, void *opaque, int arg,
                           enum LogOperation op, int offset, int size);

void draw_alpha_grid(EditState *s, int x1, int y1, int w, int h)
{
    int state, x, y;
    unsigned int color;

    for (y = 0; y < h; y += 16) {
        for (x = 0; x < w; x += 16) {
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

static ImageState *image_get_state(EditState *e, int status)
{
    return qe_get_window_mode_data(e, &image_mode, status);
}

/* transp: 0x94 and 0x64, 16x16 grid */

static void image_display(EditState *s)
{
    ImageState *is = image_get_state(s, 0);
    int x, y;

    if (!is)
        return;

    if (s->display_invalid) {
        if (is->disp_bmp) {
            x = is->x + (s->width - is->w) / 2;
            y = is->y + (s->height - is->h) / 2;

            fill_window_slack(s, x, y, is->w, is->h, QERGB(0x00, 0x00, 0x00));

            bmp_draw(s->screen, is->disp_bmp,
                     s->xleft + x, s->ytop + y,
                     is->w, is->h, 0, 0, 0);
        }
        s->display_invalid = 0;
    }
}

#if 1

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
    ImageState *is = image_get_state(s, 1);
    ImageBuffer *ib;
    int d, w, h;

    if (!is)
        return;

    ib = is->ibs->ib;

    /* simplify factors */
    d = gcd(is->xfactor_num, is->xfactor_den);
    is->xfactor_num /= d;
    is->xfactor_den /= d;

    d = gcd(is->yfactor_num, is->yfactor_den);
    is->yfactor_num /= d;
    is->yfactor_den /= d;

    w = ((long long)ib->width * (long long)is->xfactor_num) /
        is->xfactor_den;
    h = ((long long)ib->height * (long long)is->yfactor_num) /
        is->yfactor_den;

    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;

    /* if no resize needed, exit */
    if (w == is->w && h == is->h)
        return;

    edit_invalidate(s, 0);
}

static void image_normal_size(EditState *s)
{
    ImageState *is = image_get_state(s, 1);

    if (!is)
        return;

    is->xfactor_num = 1;
    is->xfactor_den = 1;
    is->yfactor_num = 1;
    is->yfactor_den = 1;

    image_resize(s);
}


/* increase or decrease image size by percent */
static void image_mult_size(EditState *s, int percent)
{
    ImageState *is = image_get_state(s, 1);

    if (!is)
        return;

    is->xfactor_num *= (100 + percent);
    is->xfactor_den *= 100;
    is->yfactor_num *= (100 + percent);
    is->yfactor_den *= 100;

    image_resize(s);
}

static void image_set_size(EditState *s, int w, int h)
{
    ImageState *is = image_get_state(s, 1);
    ImageBuffer *ib;

    if (!is)
        return;

    ib = is->ibs->ib;

    if (w < 1 || h < 1) {
        put_error(s, "Invalid image size");
        return;
    }

    is->xfactor_num = w;
    is->xfactor_den = ib->width;
    is->yfactor_num = h;
    is->yfactor_den = ib->height;

    image_resize(s);
}
#endif

static int image_mode_probe(ModeDef *mode, ModeProbeData *pd)
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

static void image_mode_free(EditBuffer *b, void *state)
{
    ImageBufferState *ibs = state;

    image_free(&ibs->ib);
}

/* allocate a new image at the end of the buffer */
static ImageBuffer *image_allocate(int pix_fmt, int width, int height)
{
    unsigned char *ptr;
    int size;
    ImageBuffer *ib;

    ib = qe_mallocz(ImageBuffer);
    if (!ib)
        return NULL;

    size = avpicture_get_size(pix_fmt, width, height);
    if (size < 0)
        goto fail;
    ptr = qe_malloc_array(u8, size);
    if (!ptr) {
    fail:
        qe_free(&ib);
        return NULL;
    }

    avpicture_fill(&ib->pict, ptr, pix_fmt, width, height);
    ib->pix_fmt = pix_fmt;
    ib->width = width;
    ib->height = height;
    return ib;
}

static void image_free(ImageBuffer **ibp)
{
    if (*ibp) {
        qe_free(&(*ibp)->pict.data[0]);
        qe_free(ibp);
    }
}

static int read_image_cb(void *opaque, AVImageInfo *info)
{
    ImageBufferState *ibs = opaque;
    ImageBuffer *ib;
    int i;

    ib = image_allocate(info->pix_fmt, info->width, info->height);
    if (!ib)
        return AVERROR_NOMEM;

    ibs->ib = ib;
    ib->interleaved = info->interleaved;
    for (i = 0; i < 4; i++) {
        info->pict.linesize[i] = ib->pict.linesize[i];
        info->pict.data[i] = ib->pict.data[i];
    }
    return 0;
}

static int image_buffer_load(EditBuffer *b, FILE *f)
{
    ByteIOContext pb1, *pb = &pb1;
    ImageBufferState *ibs;
    int ret;

    ibs = qe_get_buffer_mode_data(b, &image_mode, NULL);
    if (!ibs)
        return;

    /* start loading the image */
    ret = url_fopen(pb, b->filename, URL_RDONLY);
    if (ret < 0)
        return -1;

    ret = av_read_image(pb, b->filename, NULL, read_image_cb, ibs);
    url_fclose(pb);
    if (ret) {
        return -1;
    } else {
        ImageBuffer *ib = ibs->ib;
        ib->alpha_info = img_get_alpha_info(&ib->pict, ib->pix_fmt,
                                            ib->width, ib->height);
        return 0;
    }
}

static int set_new_image(EditBuffer *b, ImageBuffer *ib)
{
    ImageBufferState *ibs = qe_get_buffer_mode_data(b, &image_mode, NULL);

    if (!ibs)
        return -1;

    image_free(&ibs->ib);
    ibs->ib = ib;
    /* XXX: should signal all windows that image changed? */
    eb_invalidate_raw_data(b);
    b->modified = 1;  /* not really */
    return 0;
}

static int image_buffer_save(EditBuffer *b, int start, int end,
                             const char *filename)
{
    ByteIOContext pb1, *pb = &pb1;
    ImageBufferState *ibs = qe_get_buffer_mode_data(b, &image_mode, NULL);
    ImageBuffer *ib;
    ImageBuffer *ib1 = NULL;
    int ret, dst_pix_fmt, loss;
    AVImageFormat *fmt;
    AVImageInfo info;

    if (!ibs || !ibs->ib)
        return -1;

    ib = ibs->ib;

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
                        &ib->pict, ib->pix_fmt, ib->width, ib->height) < 0) {
            image_free(&ib1);
            return -1;
        }
        set_new_image(b, ib1);
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
    ImageBufferState *ibs = qe_get_buffer_mode_data(b, &image_mode, NULL);

    if (ibs) {
        image_free(&ibs->ib);
    }
}

static void update_bmp(EditState *s)
{
    ImageState *is = image_get_state(s, 1);
    ImageBuffer *ib;
    QEPicture pict;
    AVPicture avpict;
    ImageBuffer *ib1;
    int dst_pix_fmt;
    int i;

    if (!is)
        return;

    ib = is->ibs->ib;

    bmp_free(s->screen, &is->disp_bmp);

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
        for (y = 0; y < is->h; y++) {
            d = d1;
            for (x = 0; x < is->w; x++) {
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

    for (i = 0; i < 4; i++) {
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
    edit_invalidate(s, 0);
}

static int image_mode_init(EditState *s, EditBuffer *b, int flags)
{
    if (s) {
        if (flags & MODEF_NEWINSTANCE) {
            ImageState *is = qe_get_window_mode_data(s, &image_mode, 0);
            ImageBufferState *ibs = qe_get_buffer_mode_data(b, &image_mode, NULL);
            ImageBuffer *ib;

            if (!ibs || !is)
                return -1;

            ib = qe_mallocz(ImageBuffer);
            if (!ib)
                return -1;

            is->ibs = ibs;
            ibs->ib = ib;
            is->w = ib->width;
            is->h = ib->height;
            is->xfactor_num = 1;
            is->xfactor_den = 1;
            is->yfactor_num = 1;
            is->yfactor_den = 1;
            is->background_color = 0; /* display tiles */
        }
        update_bmp(s);

        eb_add_callback(s->b, image_callback, s, 1);
    }
    return 0;
}

static void update_pos(EditState *s, int dx, int dy)
{
    ImageState *is = image_get_state(s, 1);
    int delta;

    if (!is)
        return;

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
    edit_invalidate(s, 0);
}

static void image_move_left_right(EditState *s, int disp)
{
    int d;

    /* move 10% */
    d = s->width / 10;
    if (d < 1)
        d = 1;
    update_pos(s, -disp * d, 0);
}

static void image_move_up_down(EditState *s, int disp)
{
    int d;

    /* move 10% */
    d = s->height / 10;
    if (d < 1)
        d = 1;
    update_pos(s, 0, -disp * d);
}

static void image_scroll_up_down(EditState *s, int dir)
{
    int d;

    d = SCROLL_MHEIGHT;
    if (abs(dir) == 2) {
        /* move 50% */
        d = s->height / 2;
        dir /= 2;
    }
    if (d < 1)
        d = 1;
    update_pos(s, 0, -dir * d);
}

static void image_mode_close(EditState *s)
{
    ImageState *is = image_get_state(s, 0);

    if (!is)
        return;

    bmp_free(s->screen, &is->disp_bmp);
    eb_free_callback(s->b, image_callback, s);
}

/* when the image is modified, reparse it */
static void image_callback(EditBuffer *b, void *opaque, int arg,
                           enum LogOperation op, int offset, int size)
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

    switch (pix_fmt) {
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

    for (y = 0; y < h; y++) {
        s = s1;
        d = d1;

        switch (pix_fmt) {
        case PIX_FMT_PAL8:
        case PIX_FMT_GRAY8:
            for (x = 0; x < w; x++) {
                d[0] = s[0];
                s++;
                d += dlinesize;
            }
            break;
        case PIX_FMT_RGB24:
        case PIX_FMT_BGR24:
            for (x = 0; x < w; x++) {
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
                s += 3;
                d += dlinesize;
            }
            break;
        case PIX_FMT_RGBA32:
            for (x = 0; x < w; x++) {
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
    ImageState *is = image_get_state(e, 1);
    ImageBuffer *ib;
    int ret, w, h, pix_fmt;
    ImageBuffer *ib1;

    if (!is)
        return;

    ib = is->ibs->ib;

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
        put_error(e, "Format '%s' not supported yet in rotate",
                  avcodec_get_pix_fmt_name(pix_fmt));
        return;
    }
    ib1->alpha_info = ib->alpha_info;
    set_new_image(e->b, ib1);
    /* temporary */
    is->w = h;
    is->h = w;
    /* suppress that and use callback */
    update_bmp(e);
}

static void image_set_background_color(EditState *e, const char *color_str)
{
    ImageState *is = image_get_state(s, 0);

    if (!is)
        return;

    css_get_color(&is->background_color, color_str);
    update_bmp(e);
}

static void image_convert(EditState *e, const char *pix_fmt_str)
{
    ImageState *is = image_get_state(s, 0);
    ImageBuffer *ib;
    int ret, new_pix_fmt, i, loss;
    ImageBuffer *ib1;
    const char *name;

    if (!is)
        return;

    ib = is->ibs->ib;

    for (i = 0; i < PIX_FMT_NB; i++) {
        name = avcodec_get_pix_fmt_name(i);
        if (strequal(name, pix_fmt_str))
            goto found;
    }
    put_error(e, "Unknown pixel format");
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
        put_error(e, "Conversion from '%s' to '%s' not supported yet",
                  avcodec_get_pix_fmt_name(ib->pix_fmt),
                  avcodec_get_pix_fmt_name(new_pix_fmt));
        return;
    } else {
        char buf[128];
        loss = avcodec_get_pix_fmt_loss(new_pix_fmt, ib->pix_fmt, ib->alpha_info);
        if (loss != 0) {
            buf[0] = '\0';
            if (loss & FF_LOSS_RESOLUTION)
                pstrcat(buf, sizeof(buf), " res");
            if (loss & FF_LOSS_DEPTH)
                pstrcat(buf, sizeof(buf), " depth");
            if (loss & FF_LOSS_COLORSPACE)
                pstrcat(buf, sizeof(buf), " colorspace");
            if (loss & FF_LOSS_ALPHA)
                pstrcat(buf, sizeof(buf), " alpha");
            if (loss & FF_LOSS_COLORQUANT)
                pstrcat(buf, sizeof(buf), " colorquant");
            if (loss & FF_LOSS_CHROMA)
                pstrcat(buf, sizeof(buf), " chroma");
            put_status(e, "Warning: data loss:%s", buf);
        }
    }
    ib1->alpha_info = img_get_alpha_info(&ib1->pict, ib1->pix_fmt,
                                         ib1->width, ib1->height);
    set_new_image(e->b, ib1);
    /* suppress that and use callback */
    update_bmp(e);
}

void image_mode_line(EditState *s, buf_t *out)
{
    EditBuffer *b = s->b;
    ImageState *is = image_get_state(s, 0);
    ImageBuffer *ib;
    char alpha_mode;

    if (!is)
        return;

    ib = is->ib;

    basic_mode_line(s, out, '-');

    if (ib->alpha_info & FF_ALPHA_SEMI_TRANSP)
        alpha_mode = 'A';
    else if (ib->alpha_info & FF_ALPHA_TRANSP)
        alpha_mode = 'T';
    else
        alpha_mode = ' ';

    buf_printf(out, "--%dx%d %s %c%c",
               ib->width, ib->height,
               avcodec_get_pix_fmt_name(ib->pix_fmt),
               alpha_mode,
               ib->interleaved ? 'I' : ' ');
}

static void pixel_format_complete(CompleteState *cp, CompleteFunc enumerate) {
    int i;
    const char *name;

    for (i = 0; i < PIX_FMT_NB; i++) {
        name = avcodec_get_pix_fmt_name(i);
        enumerate(cp, name, CT_IGLOB);
    }
}

/* specific image commands */
static const CmdDef image_commands[] = {
    CMD0( "image-rotate", "t", "",
          image_rotate)
    CMD2( "image-convert", "c", "",
          image_convert, ESs,
          "s{New pixel format: }[pixel-format]|pixel-format|")
    CMD2( "image-set-background-color", "b", "",
          image_set_background_color, ESs,
          "s{Background color (use 'transparent' for tiling): }")
#if 1
    CMD0( "image-normal-size", "n", "",
          image_normal_size)
    CMD1( "image-double-size", ">", "",
          image_mult_size, 100)
    CMD1( "image-halve-size", "<", "",
          image_mult_size, -50)
    CMD1( "image-larger-10", ".", "",
          image_mult_size, 10)
    CMD1( "image-smaller-10", ",", "",
          image_mult_size, -10)
    CMD2( "image-set-display-size", "S", "",
          image_set_size, ESii,
          "n{Displayed width: }"
          "n{Displayed height: }")
#endif
};

static EditBufferDataType image_data_type = {
    "image",
    image_buffer_load,
    image_buffer_save,
    image_buffer_close,
};

/* additional mode specific bindings */
static const char * const image_bindings[] = {
    "f", "toggle-full-screen",
    NULL
};

static ModeDef image_mode = {
    .name = "image",
    .buffer_instance_size = sizeof(ImageBufferState),
    .window_instance_size = sizeof(ImageState),
    .mode_probe = image_mode_probe,
    .mode_init = image_mode_init,
    .mode_close = image_mode_close,
    .mode_free = image_mode_free,
    .display = image_display,
    .move_up_down = image_move_up_down,
    .move_left_right = image_move_left_right,
    .scroll_up_down = image_scroll_up_down,
    .data_type = &image_data_type,
    .get_mode_line = image_mode_line,
    .bindings = image_bindings,
};

static CompletionDef pixel_format_completion = {
    "pixel-format", pixel_format_complete
};

static int image_init(QEmacsState *qs) {
    av_register_all();
    qe_register_data_type(qs, &image_data_type);
    qe_register_mode(qs, &image_mode, MODEF_DATATYPE | MODEF_VIEW);
    qe_register_commands(qs, &image_mode, image_commands, countof(image_commands));
    qe_register_completion(qs, &pixel_format_completion);
    return 0;
}

qe_module_init(image_init);
