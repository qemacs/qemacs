/*
 * QEmacs graphics mode for image files
 *
 * Copyright (c) 2017 Charlie Gordon.
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

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ASSERT(x)
#undef malloc
#undef realloc
#undef free
#include "stb_image.h"

/*----------------------------------------------------------------*/

static ModeDef stb_mode;

typedef struct ImageState {
    QEModeData base;

    QEPicture pic;

    void *stb_image;
    int stb_x, stb_y, stb_channels;

} ImageState;

/*----------------*/

static inline ImageState *image_get_state(EditState *e, int status)
{
    return qe_get_buffer_mode_data(e->b, &stb_mode, status ? e : NULL);
}

static void image_display(EditState *s) {
    ImageState *ms = image_get_state(s, 0);
    QEColor col = qe_styles[QE_STYLE_GUTTER].bg_color;

    if (s->display_invalid) {
        if (ms && ms->stb_image) {
#if 0
            int x0, y0, w, h;

            /* No scaling */
            w = min(s->width, ms->pic.width);
            h = min(s->height, ms->pic.height / s->screen->dpy.yfactor);
            x0 = (s->width - w) / 2;
            y0 = (s->height - h) / 2;
            qe_draw_picture(s->screen, s->xleft + x0, s->ytop + y0, w, h,
                            &ms->pic, 0, 0, w, h * s->screen->dpy.yfactor,
                            0, QERGB(128, 128, 128));
            fill_border(s, x0, y0, w, h, col);
            put_status(s, "%dx%dx%d",
                       ms->pic.width, ms->pic.height, ms->stb_channels * 8);
#else
            int x0 = 0;
            int y0 = 0;
            int w = ms->pic.width;
            int h = (ms->pic.height + s->screen->dpy.yfactor - 1) / s->screen->dpy.yfactor;
            int factor = 1024;

            if (w > 0 && h > 0) {
                /* compute scaling factor for scale up or down */
                factor = min(4 * 1024, min(s->width * 1024 / w, s->height * 1024 / h));
                /* Do not scale up on text display */
                if (factor < 1024 || s->width != s->cols) {
                    w = ((w * factor) + 512) / 1024;
                    h = ((h * factor) + 512) / 1024;
                }
                x0 = (s->width - w) / 2;
                y0 = (s->height - h) / 2;
                qe_draw_picture(s->screen, s->xleft + x0, s->ytop + y0, w, h,
                                &ms->pic, 0, 0, ms->pic.width, ms->pic.height,
                                0, QERGB(128, 128, 128));
            }
            fill_border(s, x0, y0, w, h, col);
            put_status(s, "%dx%dx%d",
                       ms->pic.width, ms->pic.height, ms->stb_channels * 8);
#endif
        } else {
            fill_rectangle(s->screen, s->xleft, s->ytop, s->width, s->height, col);
        }
        s->display_invalid = 0;
    }
    if (s->qe_state->active_window == s) {
        /* Update cursor */
        int xc = s->xleft;
        int yc = s->ytop;
        int w = s->char_width;
        int h = s->line_height;
        if (s->screen->dpy.dpy_cursor_at) {
            /* hardware cursor */
            s->screen->dpy.dpy_cursor_at(s->screen, xc, yc, w, h);
        } else {
            xor_rectangle(s->screen, xc, yc, w, h, QERGB(0xFF, 0xFF, 0xFF));
        }
    }
}

static void image_display_hook(EditState *s) {
    ImageState *ms = image_get_state(s, 0);

    if (ms && !ms->stb_image) {
        ms->stb_image = stbi_load(s->b->filename,
                                  &ms->stb_x, &ms->stb_y, &ms->stb_channels, 4);
        if (ms->stb_image) {
            ms->pic.width = ms->stb_x;
            ms->pic.height = ms->stb_y;
            ms->pic.format = QEBITMAP_FORMAT_BGRA32;
            ms->pic.data[0] = ms->stb_image;
            ms->pic.linesize[0] = ms->stb_x * 4;
        } else {
            put_status(s, "stbi_load error");
        }
    }
    edit_invalidate(s, 1);
}

static void image_mode_free(EditBuffer *b, void *state) {
    ImageState *ms = state;

    if (ms->stb_image) {
        stbi_image_free(ms->stb_image);
        ms->stb_image = NULL;
        ms->pic.data[0] = NULL;
    }
}

static ModeDef stb_mode = {
    .name = "Image file",
    .alt_name = "stb",
    .extensions = "bmp|jpg|jpeg|png|tga|psd|gif|hdr|pic|pnm|ppm|pgm",
    .buffer_instance_size = sizeof(ImageState),
    .mode_free = image_mode_free,
    .display_hook = image_display_hook,
    .display = image_display,
};

static int stb_init(void)
{
    qe_register_mode(&stb_mode, MODEF_VIEW);
    return 0;
}

qe_module_init(stb_init);
