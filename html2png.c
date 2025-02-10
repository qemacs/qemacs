/*
 * HTML to PPM converter using the qHTML library
 *
 * Copyright (c) 2002 Fabrice Bellard.
 * Copyright (c) 2002-2025 Charlie Gordon.
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
#include "css.h"
#include "cfb.h"

#ifdef CONFIG_PNG_OUTPUT
#include <png.h>
#endif

#define DEFAULT_WIDTH 640
#ifdef CONFIG_PNG_OUTPUT
#define DEFAULT_OUTFILENAME "a.png"
#else
#define DEFAULT_OUTFILENAME "a.ppm"
#endif

/* file I/O for the qHTML library */

CSSFile *css_open(qe__unused__ CSSContext *s, const char *filename)
{
    FILE *f;
    f = fopen(filename, "rb");
    return (CSSFile *)f;
}

int css_filesize(CSSFile *f1)
{
    FILE *f = (FILE *)f1;
    int pos, size;

    pos = ftell(f);
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, pos, SEEK_SET);
    return size;
}

int css_read(CSSFile *f1, char *buf, int size)
{
    FILE *f = (FILE *)f1;

    return fread(buf, 1, size, f);
}

void css_close(CSSFile **f2)
{
    FILE *f = (FILE *)*f2;
    *f2 = NULL;
    if (f)
        fclose(f);
}

/* error display */

void css_error(void *error_opaque, const char *filename, int line_num,
               const char *msg)
{
    fprintf(stderr, "%s:%d: %s\n", filename, line_num, msg);
}

static void ppm_error(QEditScreen *s, const char *fmt, ...) qe__attr_printf(2,3);
static void ppm_error(qe__unused__ QEditScreen *s, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    putc('\n', stderr);
}

/* dummy functions */
char32_t eb_nextc(qe__unused__ EditBuffer *b,
                  qe__unused__ int offset, qe__unused__ int *next_ptr)
{
    return 0;
}

/* display driver based on cfb driver */

static int ppm_init(QEditScreen *s, QEmacsState *qs, int w, int h);
static void ppm_close(QEditScreen *s);
static void ppm_flush(QEditScreen *s) {}

static QEDisplay ppm_dpy = {
    "ppm", 1, 1,
    NULL,
    ppm_init,
    ppm_close,
    ppm_flush, /* dpy_flush */
    NULL, /* dpy_is_user_input_pending */
    NULL, /* dpy_fill_rectangle */
    NULL, /* dpy_xor_rectangle */
    NULL, /* dpy_open_font */
    NULL, /* dpy_close_font */
    NULL, /* dpy_text_metrics */
    NULL, /* dpy_draw_text */
    NULL, /* dpy_set_clip */
    NULL, /* dpy_selection_activate */
    NULL, /* dpy_selection_request */
    NULL, /* dpy_invalidate */
    NULL, /* dpy_cursor_at */
    NULL, /* dpy_bmp_alloc */
    NULL, /* dpy_bmp_free */
    NULL, /* dpy_bmp_draw */
    NULL, /* dpy_bmp_lock */
    NULL, /* dpy_bmp_unlock */
    NULL, /* dpy_draw_picture */
    NULL, /* dpy_full_screen */
    NULL, /* dpy_describe */
    NULL, /* dpy_sound_bell */
    NULL, /* dpy_suspend */
    ppm_error, /* dpy_error */
    NULL, /* next */
};

/* realloc ppm bitmap */
static int ppm_resize(QEditScreen *s, int w, int h)
{
    CFBContext *cfb = s->priv_data;

    /* alloc bitmap */
    // XXX: Achtung: cfb->base is an `unsigned char *`
    if (!qe_realloc_bytes(&cfb->base, w * h * sizeof(int))) {
        return -1;
    }
    cfb->wrap = w * sizeof(int);
    s->width = w;
    s->height = h;

    s->clip_x1 = 0;
    s->clip_y1 = 0;
    s->clip_x2 = s->width;
    s->clip_y2 = s->height;
    return 0;
}

static int ppm_init(QEditScreen *s, QEmacsState *qs, int w, int h)
{
    CFBContext *cfb;

    cfb = qe_mallocz(CFBContext);
    if (!cfb)
        return -1;

    s->qs = qs;
    s->priv_data = cfb;
    s->media = CSS_MEDIA_SCREEN;

    if (cfb_init(s, NULL, w * sizeof(int), 32, ".") < 0)
        goto fail;

    if (ppm_resize(s, w, h) < 0) {
    fail:
        qe_free(&cfb->base);
        qe_free(&s->priv_data);
        return -1;
    }
    return 0;
}

static void ppm_close(QEditScreen *s)
{
    CFBContext *cfb = s->priv_data;

    qe_free(&cfb->base);
    qe_free(&s->priv_data);
}

static int ppm_save(QEditScreen *s, const char *filename)
{
    CFBContext *cfb = s->priv_data;
    int w, h, x, y;
    unsigned int r, g, b, v;
    unsigned int *data;
    FILE *f;

    f = fopen(filename, "w");
    if (!f)
        return -1;
    data = (unsigned int *)(void *)cfb->base;
    w = s->width;
    h = s->height;

    fprintf(f, "P6\n%d %d\n%d\n", w, h, 255);
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            v = data[x];
            r = (v >> 16) & 0xff;
            g = (v >> 8) & 0xff;
            b = (v) & 0xff;
            fputc(r, f);
            fputc(g, f);
            fputc(b, f);
        }
        data = (void *)((char *)data + cfb->wrap);
    }
    fclose(f);
    return 0;
}

#ifdef CONFIG_PNG_OUTPUT

static int png_save(QEditScreen *s, const char *filename)
{
    CFBContext *cfb = s->priv_data;
    struct png_save_data {
        FILE *f;
        png_structp png_ptr;
        png_infop info_ptr;
        png_byte *row_buf;
    } d;

    d.f = fopen(filename, "wb");
    if (!d.f)
        return -1;

    d.png_ptr = NULL;
    d.row_buf = NULL;
    d.info_ptr = NULL;

    d.png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                        NULL, NULL, NULL);
    if (!d.png_ptr)
        goto fail;

    d.info_ptr = png_create_info_struct(d.png_ptr);
    if (!d.info_ptr)
        goto fail;

    d.row_buf = qe_malloc_array(u8, 3 * s->width);
    if (!d.row_buf)
        goto fail;

    if (!setjmp(png_jmpbuf(d.png_ptr))) {
        int w, h, x, y;
        unsigned int r, g, b, v;
        unsigned int *data;
        png_byte *row_ptr, *row_pointers[1];

        png_init_io(d.png_ptr, d.f);

        data = (unsigned int *)(void *)cfb->base;
        w = s->width;
        h = s->height;

        png_set_IHDR(d.png_ptr, d.info_ptr, w, h, 8, PNG_COLOR_TYPE_RGB,
                     PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                     PNG_FILTER_TYPE_DEFAULT);
        png_write_info(d.png_ptr, d.info_ptr);

        row_pointers[0] = d.row_buf;

        for (y = 0; y < h; y++) {
            row_ptr = d.row_buf;
            for (x = 0; x < w; x++) {
                v = data[x];
                r = (v >> 16) & 0xff;
                g = (v >> 8) & 0xff;
                b = (v) & 0xff;
                row_ptr[0] = r;
                row_ptr[1] = g;
                row_ptr[2] = b;
                row_ptr += 3;
            }
            png_write_rows(d.png_ptr, row_pointers, 1);
            data = (void *)((char *)data + cfb->wrap);
        }
        png_write_end(d.png_ptr, d.info_ptr);
        png_destroy_write_struct(&d.png_ptr, &d.info_ptr);
        fclose(d.f);
        return 0;
    } else {
    fail:
        /* free pointers before returning.  Make sure you clean up
           anything else you've done. */
        if (d.png_ptr) {
            png_destroy_write_struct(&d.png_ptr,
                                     d.info_ptr ? &d.info_ptr : NULL);
        }
        qe_free(&d.row_buf);
        fclose(d.f);
        return -1;
    }
}

#endif

#if 0
void test_display(QEditScreen *screen)
{
    QEFont *font;
    char32_t buf[256];
    int len;

    fill_rectangle(screen, 0, 0, screen->width, screen->height,
                   QERGB(0xff, 0x00, 0x00));
    len = utf8_to_char32(buf, countof(buf), "Hello World !");

    font = select_font(screen, QE_FONT_FAMILY_FIXED | QE_FONT_STYLE_NORM, 12);

    draw_text(screen, font, screen->width / 2, screen->height / 2,
              buf, len, QERGB(0x00, 0x00, 0x00));

    release_font(screen, font);
}
#endif

static int html_test_abort(qe__unused__ void *opaque)
{
    return 0;
}

#define IO_BUF_SIZE 4096

static int draw_html(QEditScreen *scr,
                     const char *filename, QECharset *charset, int flags)
{
    CSSContext *s = NULL;
    CSSBox *top_box = NULL;
    XMLState *xml;
    CSSFile *f = NULL;
    int len;
    char buf[IO_BUF_SIZE];
    CSSRect rect;
    int page_height;

    s = css_new_document(scr, NULL);
    if (!s)
        return -1;

    /* prepare default style sheet */
    s->style_sheet = css_new_style_sheet();

    css_parse_style_sheet_str(s->style_sheet, scr->qs, html_style, flags);

    /* default colors */
    s->selection_bgcolor = QERGB(0x00, 0x00, 0xff);
    s->selection_fgcolor = QERGB(0x00, 0x00, 0x00);
    s->default_bgcolor = QERGB(0xbb, 0xbb, 0xbb);

    /* parse HTML file */

    f = css_open(s, filename);
    if (!f)
        goto fail;

    xml = xml_begin(s->style_sheet, flags, html_test_abort, NULL, scr->qs, filename, charset);

    for (;;) {
        len = css_read(f, buf, IO_BUF_SIZE);
        if (len <= 0)
            break;
        xml_parse(xml, buf, len);
    }

    css_close(&f);

    top_box = xml_end(&xml);

    /* CSS computation */
    css_compute(s, top_box);

    /* CSS layout */
    css_layout(s, top_box, scr->width, html_test_abort, NULL);

    /* now we know the total size, so we allocate the ppm */
    page_height = top_box->bbox.y2;

    if (ppm_resize(scr, scr->width, page_height) < 0)
        goto fail;

    /* CSS display */
    rect.x1 = 0;
    rect.y1 = 0;
    rect.x2 = scr->width;
    rect.y2 = scr->height;

    css_display(s, top_box, &rect, 0, 0);

    css_delete_box(&top_box);
    css_delete_document(&s);
    return 0;
 fail:
    css_close(&f);
    css_delete_box(&top_box);
    css_delete_document(&s);
    return -1;
}

static void help(void)
{
    printf("html2png version %s (c) 2002 Fabrice Bellard\n"
           "\n"
           "usage: html2png [-h] [-x] [-w width] [-o outfile] [-f charset] infile\n"
           "Convert the HTML page 'infile' into the png/ppm image file 'outfile'\n"
           "\n"
           "-h         : display this help\n"
           "-x         : use strict XML parser (xhtml type parsing)\n"
           "-w width   : set the image width (default=%d)\n"
           "-f charset : set the default charset (default='%s')\n"
           "             use -f ? to list supported charsets\n"
           "-o outfile : set the output filename (default='%s')\n",
           QE_VERSION,
           DEFAULT_WIDTH,
           "8859-1",
           DEFAULT_OUTFILENAME);
}

int main(int argc, char **argv)
{
    QEmacsState state, *qs;
    QEditScreen screen1, *screen = &screen1;
    int page_width, c, strict_xml, flags;
    const char *outfilename, *infilename;
    QECharset *charset;

    qs = memset(&state, 0, sizeof state);

    charset_init(qs);
    qe_charset_more_init(qs);
    qe_charset_jis_init(qs);
    css_init();

    page_width = DEFAULT_WIDTH;
    outfilename = DEFAULT_OUTFILENAME;
    charset = &charset_8859_1;
    strict_xml = 0;

    for (;;) {
        c = getopt(argc, argv, "h?w:o:f:x");
        if (c == -1)
            break;
        switch (c) {
        case 'h':
        case '?':
            help();
            exit(1);
        case 'w':
            page_width = atoi(optarg);
            break;
        case 'o':
            outfilename = optarg;
            break;
        case 'f':
            charset = qe_find_charset(qs, optarg);
            if (!charset) {
                QECharset *p;
                fprintf(stderr, "Unknown charset '%s'\n", optarg);
                fprintf(stderr, "Supported charsets are:");
                for (p = first_charset; p != NULL; p = p->next)
                    fprintf(stderr, " %s", p->name);
                fprintf(stderr, "\n");
                exit(1);
            }
            break;
        case 'x':
            strict_xml = 1;
            break;
        }
    }
    if (optind >= argc) {
        help();
        exit(1);
    }
    infilename = argv[optind];

    /* init display driver with dummy height */
    if (qe_screen_init(qs, screen, &ppm_dpy, page_width, 1) < 0) {
        fprintf(stderr, "Could not init display driver\n");
        exit(1);
    }

    flags = XML_HTML;
    if (!strict_xml)
        flags |= XML_IGNORE_CASE | XML_HTML_SYNTAX;

    draw_html(screen, infilename, charset, flags);

    /* save ppm file */
#ifdef CONFIG_PNG_OUTPUT
    if (strstr(outfilename, ".ppm"))
        ppm_save(screen, outfilename);
    else
        png_save(screen, outfilename);
#else
    ppm_save(screen, outfilename);
#endif

    /* close screen */
    dpy_close(screen);
    css_exit();
    return 0;
}
