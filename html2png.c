/* 
 * HTML to PPM converter using the qHTML library 
 * Copyright (c) 2002 Fabrice Bellard.
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

CSSFile *css_open(CSSContext *s, const char *filename)
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

int css_read(CSSFile *f1, unsigned char *buf, int size)
{
    FILE *f = (FILE *)f1;

    return fread(buf, 1, size, f);
}

void css_close(CSSFile *f1)
{
    FILE *f = (FILE *)f1;
    fclose(f);
}

/* error display */

void css_error(const char *filename, int line_num, 
               const char *msg)
{
    fprintf(stderr, "%s:%d: %s\n", filename, line_num, msg);
}

/* dummy functions */
int eb_nextc(EditBuffer *b, int offset, int *next_ptr)
{
    return 0;
}

/* find a resource file */
/* XXX: suppress that */
int find_resource_file(char *path, int path_size, const char *pattern)
{
    return -1;
}

/* display driver based on cfb driver */

static int ppm_init(QEditScreen *s, int w, int h);
static void ppm_close(QEditScreen *s);

static QEDisplay ppm_dpy = {
    "ppm",
    NULL,
    ppm_init,
    ppm_close,
};

/* realloc ppm bitmap */
int ppm_resize(QEditScreen *s, int w, int h)
{
    CFBContext *cfb = s->private;
    unsigned char *data;
    
    /* alloc bitmap */
    data = realloc(cfb->base, w * h * sizeof(int));
    if (!data) {
        return -1;
    }
    cfb->base = data;
    cfb->wrap = w * sizeof(int);
    s->width = w;
    s->height = h;

    s->clip_x1 = 0;
    s->clip_y1 = 0;
    s->clip_x2 = s->width;
    s->clip_y2 = s->height;
    return 0;
}

static int ppm_init(QEditScreen *s, int w, int h)
{
    CFBContext *cfb;

    memcpy(&s->dpy, &ppm_dpy, sizeof(QEDisplay));

    cfb = malloc(sizeof(CFBContext));
    if (!cfb)
        return -1;

    s->private = cfb;
    s->media = CSS_MEDIA_SCREEN;

    if (cfb_init(s, NULL, w * sizeof(int), 32, ".") < 0)
        goto fail;

    if (ppm_resize(s, w, h) < 0) {
    fail:
        free(cfb);
        return -1;
    }
    return 0;
}

static void ppm_close(QEditScreen *s)
{
    CFBContext *cfb = s->private;
    
    free(cfb->base);
    free(cfb);
}

int ppm_save(QEditScreen *s, const char *filename)
{
    CFBContext *cfb = s->private;
    int w, h, x, y;
    unsigned int r, g, b, v;
    unsigned int *data;
    FILE *f;

    f = fopen(filename, "w");
    if (!f)
        return -1;
    data = (unsigned int *)cfb->base;
    w = s->width;
    h = s->height;

    fprintf(f, "P6\n%d %d\n%d\n", w, h, 255);
    for(y=0;y<h;y++) {
        for(x=0;x<w;x++) {
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
extern void png_write_init();

int png_save(QEditScreen *s, const char *filename)
{
    CFBContext *cfb = s->private;
    png_struct * volatile png_ptr = NULL;
    png_info * volatile info_ptr = NULL;
    png_byte *row_ptr, *row_pointers[1], *row = NULL;
    int w, h, x, y;
    unsigned int r, g, b, v;
    unsigned int *data;
    FILE * volatile f = NULL;

    row = malloc(3 * s->width);
    if (!row)
        goto fail;
    png_ptr = malloc(sizeof (png_struct));
    if (!png_ptr)
        goto fail;
    info_ptr = malloc(sizeof (png_info));
    if (!info_ptr)
        goto fail;
    
    f = fopen(filename, "w");
    if (!f) 
        goto fail;

    if (setjmp(png_ptr->jmpbuf)) {
        png_write_destroy(png_ptr);
    fail:
        /* free pointers before returning.  Make sure you clean up
           anything else you've done. */
        free(png_ptr);
        free(info_ptr);
        free(row);
        if (f)
            fclose(f);
        return -1;
    }

    png_info_init(info_ptr);
    png_write_init(png_ptr);
    png_init_io(png_ptr, f);
    
    data = (unsigned int *)cfb->base;
    w = s->width;
    h = s->height;

    info_ptr->width = w;
    info_ptr->height = h;
    info_ptr->bit_depth = 8;
    info_ptr->color_type = PNG_COLOR_TYPE_RGB;

    png_write_info(png_ptr, info_ptr);

    row_pointers[0] = row;

    for(y=0;y<h;y++) {
        row_ptr = row;
        for(x=0;x<w;x++) {
            v = data[x];
            r = (v >> 16) & 0xff;
            g = (v >> 8) & 0xff;
            b = (v) & 0xff;
            row_ptr[0] = r;
            row_ptr[1] = g;
            row_ptr[2] = b;
            row_ptr += 3;
        }
        png_write_rows(png_ptr, row_pointers, 1);
        data = (void *)((char *)data + cfb->wrap);
    }
    png_write_end(png_ptr, info_ptr);
    png_write_destroy(png_ptr);

    free(png_ptr);
    free(info_ptr);
    free(row);
    fclose(f);
    return 0;
}

#endif

#if 0
void test_display(QEditScreen *screen)
{
    QEFont *font;
    unsigned int buf[256];
    int len;
    
    fill_rectangle(screen, 0, 0, screen->width, screen->height, 
                   QERGB(0xff, 0x00, 0x00));
    len = utf8_to_unicode(buf, sizeof(buf), "Hello World !");
    
    font = select_font(screen, QE_FAMILY_FIXED | QE_STYLE_NORM, 12);
    
    draw_text(screen, font, screen->width / 2, screen->height / 2, 
              buf, len, QERGB(0x00, 0x00, 0x00));
}
#endif

extern const char html_style[];

static int html_test_abort(void *opaque)
{
    return 0;
}

#define IO_BUF_SIZE 4096

int draw_html(QEditScreen *scr, 
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

    css_parse_style_sheet_str(s->style_sheet, html_style, 
                              flags);
    
    /* default colors */
    s->selection_bgcolor = QERGB(0x00, 0x00, 0xff);
    s->selection_fgcolor = QERGB(0x00, 0x00, 0x00);
    s->default_bgcolor = QERGB(0xbb, 0xbb, 0xbb);

    /* parse HTML file */

    f = css_open(s, filename);
    if (!f)
        goto fail;

    xml = xml_begin(s->style_sheet, flags, html_test_abort, NULL, filename, charset);
    
    for(;;) {
        len = css_read(f, buf, IO_BUF_SIZE);
        if (len <= 0)
            break;
        xml_parse(xml, buf, len);
    }
    
    css_close(f);

    top_box = xml_end(xml);
    
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
    
    css_delete_box(top_box);
    css_delete_document(s);
    return 0;
 fail:
    if (f)
        css_close(f);
    if (top_box)
        css_delete_box(top_box);
    if (s)
        css_delete_document(s);
    return -1;
}

void help(void)
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

/* XXX: use module system */
extern int charset_more_init(void);

int main(int argc, char **argv)
{
    QEDisplay *dpy;
    QEditScreen screen1, *screen = &screen1;
    int page_width, c, strict_xml, flags;
    char *outfilename, *infilename;
    QECharset *charset;

    charset_init();
    charset_more_init();
    css_init();

    page_width = DEFAULT_WIDTH; 
    outfilename = DEFAULT_OUTFILENAME;
    charset = &charset_8859_1;
    strict_xml = 0;
    
    for(;;) {
        c = getopt(argc, argv, "h?w:o:f:x");
        if (c == -1)
            break;
        switch(c) {
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
            charset = find_charset(optarg);
            if (!charset) {
                QECharset *p;
                fprintf(stderr, "Unknown charset '%s'\n", optarg);
                fprintf(stderr, "Supported charsets are:");
                for(p = first_charset; p != NULL; p = p->next)
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
    dpy = &ppm_dpy;
    if (dpy->dpy_init(screen, page_width, 1) < 0) {
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
    screen->dpy.dpy_close(screen);
    return 0;
}
