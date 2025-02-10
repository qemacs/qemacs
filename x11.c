/*
 * X11 handling for QEmacs
 *
 * Copyright (c) 2000-2003 Fabrice Bellard.
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

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#ifdef CONFIG_XSHM
#include <X11/extensions/XShm.h>
#endif
#ifdef CONFIG_XFT
#include <X11/extensions/Xrender.h>
#include <X11/Xft/Xft.h>
#endif
#ifdef CONFIG_XV
#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvlib.h>
#endif


#define _NET_WM_STATE_REMOVE 0L
#define _NET_WM_STATE_ADD    1L

//#define CONFIG_DOUBLE_BUFFER  1

/* NOTE: XFT code is currently broken */

static QEFont *x11_dpy_open_font(QEditScreen *s, int style, int size);
static void x11_dpy_close_font(QEditScreen *s, QEFont **fontp);
#ifdef CONFIG_XV
static void xv_init(QEditScreen *s);
#endif
static void x11_handle_event(void *opaque);

typedef struct X11State {
    QEmacsState *qs;
    Display *display;
    int xscreen;
    Window root;
    Window window;
    Atom wm_delete_window;
    GC gc, gc_pixmap;
    XWindowAttributes attr;
    int event_mask;
    int screen_width, screen_height;
    int last_window_width, last_window_height, last_window_x, last_window_y;
    XIM xim; /* X11 input method */
    XIC xic; /* X11 input context */
    Pixmap dbuffer;
#ifdef CONFIG_XSHM
    int shm_use;
#endif
#ifdef CONFIG_XFT
    XftDraw *renderDraw;
#endif
#ifdef CONFIG_XV
    unsigned int xv_nb_adaptors;
    int xv_nb_formats, xv_port, xv_format, xv_open_count;
    XvAdaptorInfo *xv_ai;
    XvImageFormatValues *xv_fo;
#endif

#ifdef CONFIG_DOUBLE_BUFFER
/* update region to speed up double buffer flush */
#define UPDATE_MAX_REGIONS 3
#define UPDATE_DMIN 64
    int update_nb;
    CSSRect update_rects[UPDATE_MAX_REGIONS];
#endif

    int visual_depth;

} X11State;

/* global variables set from the command line */
static const char *display_str;
static const char *geometry_str;
static int font_ptsize;

static const char * const default_x11_fonts[NB_FONT_FAMILIES] = {
#ifdef CONFIG_XFT
    "mono",
#else
    "fixed,unifont",
#endif
    "times,unifont",
    "helvetica,unifont",
};

#ifdef CONFIG_DOUBLE_BUFFER
static void update_reset(X11State *xs)
{
    int i;
    for (i = 0; i < UPDATE_MAX_REGIONS; i++)
        css_set_rect(xs->update_rects + i, 0, 0, 0, 0);
    xs->update_nb = 0;
}

static inline int rect_dist(CSSRect *r1, CSSRect *r2)
{
    int dx, dy;

    if (r2->x2 < r1->x1)
        dx = r1->x1 - r2->x2;
    else if (r2->x1 > r1->x2)
        dx = r2->x1 - r1->x2;
    else
        dx = 0;

    if (r2->y2 < r1->y1)
        dy = r1->y1 - r2->y2;
    else if (r2->y1 > r1->y2)
        dy = r2->y1 - r1->y2;
    else
        dy = 0;
    if (dy > dx)
        dx = dy;
    return dx;
}

static void update_rect(X11State *xs, int x1, int y1, int x2, int y2)
{
    CSSRect r, *r1, *r2;
    int i, d, dmin;

    css_set_rect(&r, x1, y1, x2, y2);
    if (css_is_null_rect(&r))
        return;

    /* find closest rectangle */
    dmin = INT_MAX;
    r2 = xs->update_rects;
    r1 = NULL;
    for (i = 0; i < xs->update_nb; i++) {
        d = rect_dist(r2, &r);
        if (d < dmin) {
            dmin = d;
            r1 = r2;
        }
        r2++;
    }
    if (dmin < UPDATE_DMIN || xs->update_nb == UPDATE_MAX_REGIONS) {
        css_union_rect(r1, &r);
    } else {
        *r2 = r;
        xs->update_nb++;
    }
}

#else
static inline void update_rect(X11State *xs,
                               qe__unused__ int x1, qe__unused__ int y1,
                               qe__unused__ int x2, qe__unused__ int y2)
{
}
#endif

static int x11_dpy_probe(void)
{
    char *dpy;

    if (force_tty)
        return 0;

    /* if no env variable DISPLAY, we do not use x11 */
    dpy = getenv("DISPLAY");
    if (dpy == NULL || dpy[0] == '\0')
        return 0;
    return 1;
}

static int x11_dpy_init(QEditScreen *s, QEmacsState *qs, int w, int h)
{
    XSizeHints hint;
    int xsize, ysize;
    XSetWindowAttributes xwa;
    int font_xsize, font_ysize, fd;
    unsigned int fg, bg;
    const char *p;
    QEFont *font;
    QEStyleDef default_style;
    XGCValues gc_val;
    X11State *xs = qe_mallocz(X11State);

    if (xs == NULL) {
        fprintf(stderr, "Cannot allocate X11State.\n");
        return -1;
    }
    s->qs = xs->qs = qs;
    s->priv_data = xs;
    s->media = CSS_MEDIA_SCREEN;

    if (!display_str)
        display_str = "";

    xs->display = XOpenDisplay(display_str);
    if (xs->display == NULL) {
        fprintf(stderr, "Could not open X11 display - exiting.\n");
        return -1;
    }
    xs->xscreen = DefaultScreen(xs->display);
    xs->root = DefaultRootWindow(xs->display);

    bg = BlackPixel(xs->display, xs->xscreen);
    fg = WhitePixel(xs->display, xs->xscreen);
    xs->screen_width = DisplayWidth(xs->display, xs->xscreen);
    xs->screen_height = DisplayHeight(xs->display, xs->xscreen);

    /* At this point, we should be able to ask for metrics */
    if (font_ptsize)
        qe_styles[0].font_size = font_ptsize;
    get_style(NULL, &default_style, 0);
    font = x11_dpy_open_font(s, default_style.font_style,
                          default_style.font_size);
    if (!font) {
        fprintf(stderr, "Could not open default font\n");
        exit(1);
    }
    font_ysize = font->ascent + font->descent;
    font_xsize = glyph_width(s, font, 'x');
    x11_dpy_close_font(s, &font);

    if (w > 0 && h > 0) {
        xsize = w;
        ysize = h;
    } else {
        xsize = 128 * font_xsize;
        ysize = 50 * font_ysize;

        if (geometry_str) {
            p = geometry_str;
            xsize = strtol_c(p, &p, 0);
            if (*p == 'x')
                p++;
            ysize = strtol_c(p, &p, 0);

            if (xsize <= 0 || ysize <=0) {
                fprintf(stderr, "Invalid geometry '%s'\n", geometry_str);
                exit(1);
            }
        }
    }

    s->width = xsize;
    s->height = ysize;
    s->charset = &charset_utf8;

    s->clip_x1 = 0;
    s->clip_y1 = 0;
    s->clip_x2 = s->width;
    s->clip_y2 = s->height;
    /* Fill in hint structure */

    hint.x = 0;
    hint.y = 0;
    hint.width = xsize;
    hint.height = ysize;
    hint.flags = PPosition | PSize;

    /* Make the window */
    xs->window = XCreateSimpleWindow(xs->display,
                                     DefaultRootWindow(xs->display),
                                     hint.x, hint.y,
                                     hint.width, hint.height,
                                     4, fg, bg);
    /* Enable backing store */
    xwa.backing_store = Always;
    XChangeWindowAttributes(xs->display, xs->window, CWBackingStore, &xwa);

    XSelectInput(xs->display, xs->window, StructureNotifyMask);

    /* Tell other applications about this window */

    XSetStandardProperties(xs->display, xs->window,
                           "qemacs", "qemacs",
                           None, NULL, 0, &hint);

    /* Map window. */

    XMapWindow(xs->display, xs->window);

    /* Wait for map. */
    while (1) {
        XEvent xev;
        XNextEvent(xs->display, &xev);
        if (xev.type == MapNotify && xev.xmap.event == xs->window)
            break;
    }
    xs->event_mask = KeyPressMask | ButtonPressMask | ButtonReleaseMask |
                     ButtonMotionMask | ExposureMask | StructureNotifyMask;
    XSelectInput(xs->display, xs->window, xs->event_mask);

    XGetWindowAttributes(xs->display, xs->window, &xs->attr);

    /* see if we can bypass the X color functions */
    xs->visual_depth = 0;
    if (xs->attr.visual->class == TrueColor) {
        XVisualInfo *vinfo, templ;
        int n;
        templ.visualid = xs->attr.visual->visualid;
        vinfo = XGetVisualInfo(xs->display, VisualIDMask, &templ, &n);
        if (vinfo) {
            xs->visual_depth = vinfo->depth;
        }
        XFree(vinfo);
    }

    xs->xim = XOpenIM(xs->display, NULL, NULL, NULL);
    xs->xic = XCreateIC(xs->xim, XNInputStyle,
                        XIMPreeditNothing | XIMStatusNothing,
                        XNClientWindow, xs->window,
                        XNFocusWindow, xs->window,
                        NULL);

    xs->wm_delete_window = XInternAtom(xs->display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(xs->display, xs->window, &xs->wm_delete_window, 1);

    xs->gc = XCreateGC(xs->display, xs->window, 0, NULL);
#ifdef CONFIG_XFT
    xs->renderDraw = XftDrawCreate(xs->display, xs->window, xs->attr.visual,
                                   DefaultColormap(xs->display, xs->xscreen));
#endif
    /* double buffer handling */
    xs->gc_pixmap = XCreateGC(xs->display, xs->window, 0, NULL);
    gc_val.graphics_exposures = 0;
    XChangeGC(xs->display, xs->gc_pixmap, GCGraphicsExposures, &gc_val);
    XSetForeground(xs->display, xs->gc, BlackPixel(xs->display, xs->xscreen));
#ifdef CONFIG_DOUBLE_BUFFER
    xs->dbuffer = XCreatePixmap(xs->display, xs->window, xsize, ysize, xs->attr.depth);
    /* erase pixmap */
    XFillRectangle(xs->display, xs->dbuffer, xs->gc, 0, 0, xsize, ysize);
    update_reset(xs);
#else
    xs->dbuffer = xs->window;
#endif

#ifdef CONFIG_XSHM
    /* shm extension usable ? */
    p = XDisplayName(display_str);
    strstart(p, "unix:", &p);
    strstart(p, "localhost:", &p);
    xs->shm_use = 0;
    /* Check if display is local and XShm available */
    if ((*p == ':') && XShmQueryExtension(xs->display))
        xs->shm_use = 1;
#endif

    /* compute bitmap format */
    switch (xs->visual_depth) {
    case 15:
        s->bitmap_format = QEBITMAP_FORMAT_RGB555;
        break;
    default:
    case 16:
        s->bitmap_format = QEBITMAP_FORMAT_RGB565;
        break;
    case 24:
        s->bitmap_format = QEBITMAP_FORMAT_RGB24;
        break;
    case 32:
        s->bitmap_format = QEBITMAP_FORMAT_RGBA32;
        break;
    }
    s->video_format = s->bitmap_format;

#ifdef CONFIG_XV
    xv_init(s);
#endif
    fd = ConnectionNumber(xs->display);
    set_read_handler(fd, x11_handle_event, s);
    return 0;
}

#ifdef CONFIG_XV
static void xv_init(QEditScreen *s)
{
    X11State *xs = s->priv_data;
    int i;
    XvPortID xv_p;

    xs->xv_port = 0; /* zero means no XV port found */
    if (XvQueryAdaptors(xs->display,
                        DefaultRootWindow(xs->display),
                        &xs->xv_nb_adaptors, &xs->xv_ai) != Success)
        return;

    for (i = 0; i < (int)xs->xv_nb_adaptors; i++) {
        if ((xs->xv_ai[i].type & XvInputMask) &&
            (xs->xv_ai[i].type & XvImageMask)) {
            for (xv_p = xs->xv_ai[i].base_id;
                 xv_p < (XvPortID)(xs->xv_ai[i].base_id + xs->xv_ai[i].num_ports);
                 xv_p++) {
                if (!XvGrabPort(xs->display, xv_p, CurrentTime)) {
                    xs->xv_port = xv_p;
                    goto found;
                }
            }
        }
    }
    return;
 found:

    xs->xv_fo = XvListImageFormats(xs->display, xs->xv_port, &xs->xv_nb_formats);
    for (i = 0; i < xs->xv_nb_formats; i++) {
        XvImageFormatValues *fo = &xs->xv_fo[i];
#if 0
        printf("Xvideo image format: 0x%x (%c%c%c%c) %s\n",
               fo->id,
               (fo->id) & 0xff,
               (fo->id >> 8) & 0xff,
               (fo->id >> 16) & 0xff,
               (fo->id >> 24) & 0xff,
               (fo->format == XvPacked) ? "packed" : "planar");
#endif
        /* search YUV420P format */
        if (fo->id == 0x32315659 && fo->format == XvPlanar) {
            xs->xv_format = fo->id;
            break;
        }
    }
    /* if no format found, then release port */
    if (i == xs->xv_nb_formats) {
        XvUngrabPort(xs->display, xv_p, CurrentTime);
        xs->xv_port = 0;
    } else {
        s->video_format = QEBITMAP_FORMAT_YUV420P;
    }
}

static void xv_close(QEditScreen *s)
{
    X11State *xs = s->priv_data;

    XFree(xs->xv_fo);
    XvFreeAdaptorInfo(xs->xv_ai);
}
#endif

static void x11_dpy_close(QEditScreen *s)
{
    X11State *xs = s->priv_data;

#ifdef CONFIG_XV
    xv_close(s);
#endif

#ifdef CONFIG_DOUBLE_BUFFER
    XFreePixmap(xs->display, xs->dbuffer);
#endif

    XFreeGC(xs->display, xs->gc_pixmap);
    XFreeGC(xs->display, xs->gc);

    XCloseDisplay(xs->display);
    qe_free(&s->priv_data);
}

static int x11_term_resize(QEditScreen *s, int w, int h)
{
    if (s->width == w && s->height == h)
        return 0;

    s->width = w;
    s->height = h;

#ifdef CONFIG_DOUBLE_BUFFER
    {
        X11State *xs = s->priv_data;

        /* resize double buffer */
        XFreePixmap(xs->display, xs->dbuffer);
        xs->dbuffer = XCreatePixmap(xs->display, xs->window, w, h, xs->attr.depth);
    }
#endif
    return 1;
}

static unsigned long get_x11_color(X11State *xs, QEColor color)
{
    unsigned int r = (color >> 16) & 0xff;
    unsigned int g = (color >>  8) & 0xff;
    unsigned int b = (color >>  0) & 0xff;
    XColor col;

    switch (xs->visual_depth) {
    case 15:
        return ((((r) >> 3) << 10) | (((g) >> 3) << 5) | ((b) >> 3));
    case 16:
        return ((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3));
    case 24:
    case 32:
        return (r << 16) | (g << 8) | b;
    default:
        /* NOTE: the color is never freed */
        /* XXX: should do allocation ourself ? */
        col.red = r << 8;
        col.green = g << 8;
        col.blue = b << 8;
        XAllocColor(xs->display, xs->attr.colormap, &col);
        return col.pixel;
    }
}

/* Print the unicode string 'str' with baseline at position (x,y). The
   higher bits of each char may contain attributes. */
#ifdef CONFIG_XFT

static void x11_dpy_fill_rectangle(QEditScreen *s,
                                   int x1, int y1, int w, int h, QEColor color)
{
    X11State *xs = s->priv_data;
    XftColor col;
    int r, g, b, a;

    a = (color >> 24) & 0xff;
    r = (color >> 16) & 0xff;
    g = (color >>  8) & 0xff;
    b = (color >>  0) & 0xff;
    /* not exact, but faster */
    col.color.red = r << 8;
    col.color.green = g << 8;
    col.color.blue = b << 8;
    col.color.alpha = a << 8;
    col.pixel = get_x11_color(xs, color);
    XftDrawRect(xs->renderDraw, &col, x1, y1, w, h);
}

static void x11_dpy_xor_rectangle(QEditScreen *s,
                                  int x1, int y1, int w, int h, QEColor color)
{
    X11State *xs = s->priv_data;
    unsigned long fg;

    fg = get_x11_color(xs, QE_RGB(255, 255, 255));
    XSetForeground(xs->display, xs->gc, fg);
    XSetFunction(xs->display, xs->gc, GXxor);
    XFillRectangle(xs->display, xs->dbuffer, xs->gc, x1, y1, w, h);
    XSetFunction(xs->display, xs->gc, GXcopy);
}

static QEFont *x11_dpy_open_font(QEditScreen *s, int style, int size)
{
    X11State *xs = s->priv_data;
    const char *family;
    int weight, slant;
    XftFont *renderFont;
    QEFont *font;

    font = qe_mallocz(QEFont);
    if (!font)
        return NULL;

    switch (style & QE_FONT_FAMILY_MASK) {
    default:
    case QE_FONT_FAMILY_FIXED:
        family = font_family_str;
        break;
    case QE_FONT_FAMILY_SANS:
        family = "sans";
        break;
    case QE_FONT_FAMILY_SERIF:
        family = "serif";
        break;
    }
    weight = XFT_WEIGHT_MEDIUM;
    if (style & QE_FONT_STYLE_BOLD)
        weight = XFT_WEIGHT_BOLD;
    slant = XFT_SLANT_ROMAN;
    if (style & QE_FONT_STYLE_ITALIC)
        slant = XFT_SLANT_ITALIC;

    renderFont = XftFontOpen(xs->display, xs->xscreen,
                             XFT_FAMILY, XftTypeString, family,
                             XFT_SIZE, XftTypeInteger, size,
                             XFT_WEIGHT, XftTypeInteger, weight,
                             XFT_SLANT, XftTypeInteger, slant,
                             0);
    if (!renderFont) {
        /* CG: don't know if this can happen, should try fallback? */
        qe_free(&font);
        return NULL;
    }
    font->ascent = renderFont->ascent;
    font->descent = renderFont->descent;
    font->priv_data = renderFont;
    return font;
}

static void x11_dpy_close_font(QEditScreen *s, QEFont **fontp)
{
    X11State *xs = s->priv_data;
    QEFont *font = *fontp;

    if (font) {
        XftFont *renderFont = font->priv_data;

        XftFontClose(xs->display, renderFont);
        /* Clear structure to force crash if font is still used after
         * close_font.
         */
        memset(font, 0, sizeof(*font));
        qe_free(fontp);
    }
}

static int x11_term_glyph_width(QEditScreen *s, QEFont *font, char32_t cc) {
    X11State *xs = s->priv_data;
    XftFont *renderFont = font->priv_data;
    unsigned int uc = cc;
    XGlyphInfo gi;

    XftTextExtents32(xs->display, renderFont, &uc, 1, &gi);
    return gi.xOff;
}

static void x11_dpy_draw_text(QEditScreen *s, QEFont *font,
                              int x, int y, const char32_t *str, int len,
                              QEColor color)
{
    X11State *xs = s->priv_data;
    XftFont *renderFont = font->priv_data;
    XftColor col;
    int a = (color >> 24) & 0xff;
    int r = (color >> 16) & 0xff;
    int g = (color >>  8) & 0xff;
    int b = (color >>  0) & 0xff;

    /* not exact, but faster */
    col.color.red = r << 8;
    col.color.green = g << 8;
    col.color.blue = b << 8;
    col.color.alpha = a << 8;
    col.pixel = get_x11_color(xs, color);

    XftDrawString32(xs->renderDraw, &col, renderFont, x, y,
                    (XftChar32 *)str, len);
}

#else

static void x11_dpy_fill_rectangle(QEditScreen *s,
                                   int x1, int y1, int w, int h, QEColor color)
{
    X11State *xs = s->priv_data;
    unsigned long xcolor;

    update_rect(xs, x1, y1, x1 + w, y1 + h);

    xcolor = get_x11_color(xs, color);
    XSetForeground(xs->display, xs->gc, xcolor);
    XFillRectangle(xs->display, xs->dbuffer, xs->gc, x1, y1, w, h);
}

static void x11_dpy_xor_rectangle(QEditScreen *s,
                                  int x1, int y1, int w, int h, QEColor color)
{
    X11State *xs = s->priv_data;
    int fg;

    update_rect(xs, x1, y1, x1 + w, y1 + h);

    fg = WhitePixel(xs->display, xs->xscreen);
    XSetForeground(xs->display, xs->gc, fg);
    XSetFunction(xs->display, xs->gc, GXxor);
    XFillRectangle(xs->display, xs->dbuffer, xs->gc, x1, y1, w, h);
    XSetFunction(xs->display, xs->gc, GXcopy);
}

static void get_entry(char *buf, int buf_size, const char **pp)
{
    const char *p, *r;
    int len;

    r = *pp;
    p = strchr(r, '-');
    if (!p) {
        if (buf)
            buf[0] = '\0';
    } else {
        if (buf) {
            len = p - r;
            if (len >= buf_size)
                len = buf_size - 1;
            memcpy(buf, r, len);
            buf[len] = '\0';
        }
        *pp = p + 1;
    }
}


static QEFont *x11_dpy_open_font(QEditScreen *s, int style, int size)
{
    X11State *xs = s->priv_data;
    char family[128];
    const char *family_list, *p1;
    XFontStruct *xfont;
    QEFont *font;
    char buf[512];
    int count, found, dist, dist_min, i, size1, font_index, font_fallback;
    char **list;
    const char *p;

    font = qe_mallocz(QEFont);
    if (!font)
        return NULL;

    /* get font name */
    font_index = ((style & QE_FONT_FAMILY_MASK) >> QE_FONT_FAMILY_SHIFT) - 1;
    if ((unsigned)font_index >= NB_FONT_FAMILIES)
        font_index = 0; /* fixed font is default */
    family_list = s->qs->system_fonts[font_index];
    if (family_list[0] == '\0')
        family_list = default_x11_fonts[font_index];

    /* take the nth font number in family list */
    font_fallback = (style & QE_FONT_FAMILY_FALLBACK_MASK) >> QE_FONT_FAMILY_FALLBACK_SHIFT;
    p = family_list;
    for (i = 0; i < font_fallback; i++) {
        p = strchr(p, ',');
        if (!p) {
            /* no font found */
            qe_free(&font);
            return NULL;
        }
        p++;
    }
    p1 = strchr(p, ',');
    if (!p1)
        pstrcpy(family, sizeof(family), p);
    else
        pstrncpy(family, sizeof(family), p, p1 - p);
#if 0
    for (i = 0; i < 2; i++) {
        char buf1[32];
        if (i == 0)
            snprintf(buf1, sizeof(buf1), "%d", size * 10);
        else
            pstrcpy(buf1, sizeof(buf1), "*");
        snprintf(buf, sizeof(buf),
                 "-*-%s-*-*-*-*-*-%s-*-*-*-*-*-*",
                 family, buf1);
        list = XListFonts(xs->display, buf, 256, &count);
        if (count > 0)
            break;
    }
    if (i == 2)
        goto fail;
#else
    /* note: we do not want the X server to scale the font (usually
       ugly result), so we do not ask a given size */
    snprintf(buf, sizeof(buf),
             "-*-%s-*-*-*-*-*-%s-*-*-*-*-*-*",
             family, "*");
    list = XListFonts(xs->display, buf, 256, &count);
    if (count == 0)
        goto fail;
#endif
    /* iterate thru each font and select closer one */
    found = 0;
    dist_min = INT_MAX;
    for (i = 0; i < count; i++) {
        dist = 0;
        p = list[i] + 1;
        get_entry(NULL, 0, &p);
        get_entry(NULL, 0, &p); /* family */
        get_entry(buf, sizeof(buf), &p); /* weight */
        if (!((strequal(buf, "bold") && (style & QE_FONT_STYLE_BOLD)) ||
              (strequal(buf, "medium") && !(style & QE_FONT_STYLE_BOLD))))
            dist += 3;
        get_entry(buf, sizeof(buf), &p); /* slant */
        if (!((strequal(buf, "o") && (style & QE_FONT_STYLE_ITALIC)) ||
              (strequal(buf, "i") && (style & QE_FONT_STYLE_ITALIC)) ||
              (strequal(buf, "r") && !(style & QE_FONT_STYLE_ITALIC))))
            dist += 3;
        get_entry(NULL, 0, &p); /* swidth */
        get_entry(NULL, 0, &p); /* adstyle */
        get_entry(NULL, 0, &p); /* pixsize */
        get_entry(buf, sizeof(buf), &p); /* size */
        size1 = atoi(buf) / 10;
        dist += abs(size - size1) * 3;
        get_entry(NULL, 0, &p); /* pixsize */
        get_entry(NULL, 0, &p); /* pixsize */
        get_entry(NULL, 0, &p); /* pixsize */
        get_entry(NULL, 0, &p); /* pixsize */
        /* always favor unicode encoding */
        if (!strequal(p, "iso10646-1"))
            dist += 3;

        if (dist < dist_min) {
            found = i;
            dist_min = dist;
        }
    }

    xfont = XLoadQueryFont(xs->display, list[found]);
    if (!xfont)
        goto fail;
    XFreeFontNames(list);

    font->ascent = xfont->ascent;
    font->descent = xfont->descent;
    font->priv_data = xfont;
    return font;
 fail:
    XFreeFontNames(list);
    qe_free(&font);
    return NULL;
}

static void x11_dpy_close_font(QEditScreen *s, QEFont **fontp)
{
    X11State *xs = s->priv_data;

    if (*fontp) {
        QEFont *font = *fontp;
        XFontStruct *xfont = font->priv_data;

        XFreeFont(xs->display, xfont);
        /* Clear structure to force crash if font is still used after
         * close_font.
         */
        memset(font, 0, sizeof(*font));
        qe_free(fontp);
    }
}

/* get a char struct associated to a char. Return NULL if no glyph
   associated. */
static XCharStruct *get_char_struct(QEFont *font, char32_t cc) {
    XFontStruct *xfont = font->priv_data;
    unsigned int b1, b2;
    XCharStruct *cs;

    if (!xfont)
        return NULL;

    if (xfont->min_byte1 == 0 && xfont->max_byte1 == 0) {
        if (cc < xfont->min_char_or_byte2
        ||  cc > xfont->max_char_or_byte2)
            return NULL;
        cc -= xfont->min_char_or_byte2;
    } else {
        b1 = (cc >> 8) & 0xff;
        b2 = cc & 0xff;
        if (b1 < xfont->min_byte1
        ||  b1 > xfont->max_byte1
        ||  b2 < xfont->min_char_or_byte2
        ||  b2 > xfont->max_char_or_byte2)
            return NULL;
        b1 -= xfont->min_byte1;
        b2 -= xfont->min_char_or_byte2;
        cc = b1 * (xfont->max_char_or_byte2 -
                   xfont->min_char_or_byte2 + 1) + b2;
    }
    cs = xfont->per_char;
    if (!cs)
        return &xfont->min_bounds; /* all char have same metrics */
    cs += cc;
    /* fast test for non existent char */
    if (cs->width == 0 &&
        (cs->ascent | cs->descent | cs->rbearing | cs->lbearing) == 0) {
        return NULL;
    } else {
        return cs;
    }
}

static XCharStruct *handle_fallback(QEditScreen *s, QEFont **out_font,
                                    QEFont *font, char32_t cc)
{
    XFontStruct *xfont;
    XCharStruct *cs;
    int fallback_count;
    QEFont *font1;

    /* fallback case */
    for (fallback_count = 1; fallback_count < 5; fallback_count++) {
        font1 = select_font(s, font->style |
                            (fallback_count << QE_FONT_FAMILY_FALLBACK_SHIFT),
                            font->size);
        if (!font1)
            break;
        cs = get_char_struct(font1, cc);
        if (cs) {
            *out_font = font1;
            return cs;
        }
        release_font(s, font1);
    }

    /* really no glyph : use default char in current font */
    /* Should have half-width and full-width default char patterns */
    xfont = font->priv_data;
    cs = get_char_struct(font, xfont->default_char);
    *out_font = lock_font(s, font);
    return cs;
}

static void x11_dpy_text_metrics(QEditScreen *s, QEFont *font,
                                 QECharMetrics *metrics,
                                 const char32_t *str, int len)
{
    QEFont *font1;
    XCharStruct *cs;
    int i, x;
    char32_t cc;

    metrics->font_ascent = font->ascent;
    metrics->font_descent = font->descent;
    x = 0;
    for (i = 0; i < len; i++) {
        cc = str[i];
        cs = get_char_struct(font, cc);
        if (cs) {
            /* most common case */
            x += cs->width;
        } else {
            /* no glyph: use a fallback font */
            cs = handle_fallback(s, &font1, font, cc);
            if (cs) {
                x += cs->width;
                metrics->font_ascent = max_int(metrics->font_ascent, font1->ascent);
                metrics->font_descent = max_int(metrics->font_descent, font1->descent);
            }
            release_font(s, font1);
        }
    }
    metrics->width = x;
}

static void x11_dpy_draw_text(QEditScreen *s, QEFont *font,
                              int x1, int y, const char32_t *str, int len,
                              QEColor color)
{
    X11State *xs = s->priv_data;
    XFontStruct *xfont;
    QEFont *font1, *last_font;
    XCharStruct *cs;
#ifdef __GNUC__
    /* CG: C99 variable-length arrays may be too large */
    XChar2b x11_str[len];
#else
    XChar2b x11_str[LINE_MAX_SIZE];
#endif
    XChar2b *q;
    int i, l, x, x_start;
    char32_t cc;
    unsigned long xcolor;

    xcolor = get_x11_color(xs, color);
    XSetForeground(xs->display, xs->gc, xcolor);
    q = x11_str;
    i = 0;
    x = x1;
    x_start = x;
    last_font = font;
    while (i < len) {
        cc = str[i++];
        cs = get_char_struct(font, cc);
        if (cs) {
            font1 = font; /* most common case */
        } else {
            /* complicated case: fallback */
            cs = handle_fallback(s, &font1, font, cc);
            if (!cs) {
                /* still no char: use default glyph */
                xfont = font->priv_data;
                cc = xfont->default_char;
            }
        }
        /* flush previous chars if font change needed */
        if (font1 != last_font && q > x11_str) {
            xfont = last_font->priv_data;
            l = q - x11_str;
            XSetFont(xs->display, xs->gc, xfont->fid);
            XDrawString16(xs->display, xs->dbuffer, xs->gc, x_start, y, x11_str, l);
            update_rect(xs, x_start, y - last_font->ascent, x, y + last_font->descent);
            x_start = x;
            q = x11_str;
        }
        last_font = font1;
        /* XXX: invalid conversion from UCS4 to UCS2 */
        if (cc >= 0xFFFF)
            cc = 0xFFFD;
        q->byte1 = (cc >> 8) & 0xff;
        q->byte2 = (cc) & 0xff;
        q++;
        x += cs->width;
    }
    if (q > x11_str) {
        /* flush remaining chars (more common case) */
        xfont = last_font->priv_data;
        l = q - x11_str;
        XSetFont(xs->display, xs->gc, xfont->fid);
        XDrawString16(xs->display, xs->dbuffer, xs->gc, x_start, y, x11_str, l);
        update_rect(xs, x_start, y - last_font->ascent, x, y + last_font->descent);
    }
    /* underline synthesis */
    if (font->style & (QE_FONT_STYLE_UNDERLINE | QE_FONT_STYLE_LINE_THROUGH)) {
        int dy, h, w;
        h = (font->descent + 2) / 4;
        if (h < 1)
            h = 1;
        w = x - x1;
        if (font->style & QE_FONT_STYLE_UNDERLINE) {
            dy = (font->descent + 1) / 3;
            XFillRectangle(xs->display, xs->dbuffer, xs->gc, x1, y + dy, w, h);
        }
        if (font->style & QE_FONT_STYLE_LINE_THROUGH) {
            dy = -(font->ascent / 2 - 1);
            XFillRectangle(xs->display, xs->dbuffer, xs->gc, x1, y + dy, w, h);
        }
    }
}
#endif

static void x11_dpy_set_clip(QEditScreen *s, int x, int y, int w, int h)
{
    X11State *xs = s->priv_data;
    XRectangle rect;

    rect.x = x;
    rect.y = y;
    rect.width = w;
    rect.height = h;
    XSetClipRectangles(xs->display, xs->gc, 0, 0, &rect, 1, YXSorted);
}

static void x11_dpy_flush(QEditScreen *s)
{
    X11State *xs = s->priv_data;
#ifdef CONFIG_DOUBLE_BUFFER
    CSSRect *r;
    int i, w, h;

    r = xs->update_rects;
    for (i = 0; i < xs->update_nb; i++) {
        if (r->x1 < 0)
            r->x1 = 0;
        if (r->x2 > s->width)
            r->x2 = s->width;
        if (r->y1 < 0)
            r->y1 = 0;
        if (r->y2 > s->height)
            r->y2 = s->height;
        w = r->x2 - r->x1;
        h = r->y2 - r->y1;
        if (w > 0 && h > 0) {
            XCopyArea(xs->display, xs->dbuffer, xs->window, xs->gc_pixmap,
                      r->x1, r->y1, w, h, r->x1, r->y1);
#if 0
            XSetForeground(xs->display, xs->gc_pixmap, 0xffff);
            XDrawRectangle(xs->display, xs->window, xs->gc_pixmap, r->x1, r->y1,
                           w - 1, h - 1);
#endif
        }
        r++;
    }
#endif
    /* XXX: update cursor? */
    XFlush(xs->display);
#ifdef CONFIG_DOUBLE_BUFFER
    update_reset(xs);
#endif
}

static void x11_dpy_full_screen(QEditScreen *s, int full_screen)
{
    X11State *xs = s->priv_data;
    Atom wm_state;
    Atom wm_state_fullscreen;
    XEvent event;
    XWindowAttributes attribs;

    wm_state = XInternAtom(xs->display, "_NET_WM_STATE", False);
    wm_state_fullscreen = XInternAtom(xs->display, "_NET_WM_STATE_FULLSCREEN", False);

    memset(&event, 0, sizeof(event));
    event.type = ClientMessage;
    event.xclient.window = xs->window;
    event.xclient.format = 32;
    event.xclient.message_type = wm_state;
    event.xclient.data.l[0] = full_screen ? _NET_WM_STATE_ADD : _NET_WM_STATE_REMOVE;
    event.xclient.data.l[1] = wm_state_fullscreen;
    event.xclient.data.l[2] = 0;
    event.xclient.data.l[3] = 1;
    event.xclient.data.l[4] = 0;

    XSendEvent(xs->display, xs->root, False,
               SubstructureNotifyMask | SubstructureRedirectMask,
               &event);

    XFlush(xs->display);

    /*
      last_window_width, last_window_height, last_window_x, last_window_y
      not need since window manager doing geometric.
    */
    XGetWindowAttributes(xs->display, xs->window, &attribs);
    xs->screen_width = attribs.width;
    xs->screen_height = attribs.height;
}

static void x11_dpy_selection_activate(QEditScreen *s)
{
    X11State *xs = s->priv_data;

    /* own selection from now */
    XSetSelectionOwner(xs->display, XA_PRIMARY, xs->window, CurrentTime);
}

static Bool test_event(qe__unused__ Display *dpy, XEvent *ev,
                       qe__unused__ char *arg)
{
    return (ev->type == SelectionNotify);
}

/* request the selection from the GUI and put it in a new yank buffer
   if needed */
static void x11_dpy_selection_request(QEditScreen *s)
{
    X11State *xs = s->priv_data;
    Window w;
    Atom prop;
    Atom utf8;
    long nread;
    unsigned char *data;
    Atom actual_type;
    int actual_fmt;
    unsigned long nitems, bytes_after;
    EditBuffer *b;
    XEvent xev;

    w = XGetSelectionOwner(xs->display, XA_PRIMARY);
    if (w == None || w == xs->window)
        return; /* qemacs can use its own selection */

    /* use X11 selection (Will be pasted when receiving
       SelectionNotify event) */
    prop = XInternAtom(xs->display, "VT_SELECTION", False);
    utf8 = XInternAtom(xs->display, "UTF8_STRING", False);
    XConvertSelection(xs->display, XA_PRIMARY, utf8,
                      prop, xs->window, CurrentTime);

    /* XXX: add timeout too if the target application is not well
       educated */
    XIfEvent(xs->display, &xev, test_event, NULL);

    w = xev.xselection.requestor;
    prop = xev.xselection.property;

    /* copy GUI selection a new yank buffer */
    b = qe_new_yank_buffer(s->qs, NULL);
    eb_set_charset(b, &charset_utf8, EOL_UNIX);

    nread = 0;
    for (;;) {
        if ((XGetWindowProperty(xs->display, w, prop,
                                nread/4, 4096, True,
                                AnyPropertyType, &actual_type, &actual_fmt,
                                &nitems, &bytes_after,
                                &data) != Success) ||
            (actual_type != utf8)) {
            XFree(data);
            break;
        }

       eb_write(b, nread, data, nitems);

       nread += nitems;
       XFree(data);

       if (bytes_after == 0)
           break;
   }
}

/* send qemacs selection to requestor */
static void selection_send(X11State *xs, XSelectionRequestEvent *rq)
{
    static Atom xa_targets = None;
    static Atom xa_formats[] = { None, None, None, None };
    QEmacsState *qs = xs->qs;
    unsigned char *buf;
    XEvent ev;
    EditBuffer *b;

    if (xa_targets == None)
        xa_targets = XInternAtom(xs->display, "TARGETS", False);
    if (xa_formats[0] == None) {
        xa_formats[0] = XInternAtom(xs->display, "UTF8_STRING", False);
        xa_formats[1] = XInternAtom(xs->display, "text/plain;charset=UTF-8", False);
        xa_formats[2] = XInternAtom(xs->display, "text/plain;charset=utf-8", False);
        xa_formats[3] = XA_STRING;
    }

    ev.xselection.type      = SelectionNotify;
    ev.xselection.property  = None;
    ev.xselection.display   = rq->display;
    ev.xselection.requestor = rq->requestor;
    ev.xselection.selection = rq->selection;
    ev.xselection.target    = rq->target;
    ev.xselection.time      = rq->time;

    if (rq->target == xa_targets) {
        unsigned int target_list[1 + countof(xa_formats)];
        int i;

        /* indicate which are supported types */
        target_list[0] = xa_targets;
        for (i = 0; i < countof(xa_formats); i++)
            target_list[i + 1] = xa_formats[i];

        XChangeProperty(xs->display, rq->requestor, rq->property,
                        xa_targets, 8*sizeof(target_list[0]), PropModeReplace,
                        (unsigned char *)target_list,
                        countof(target_list));
    } else
    if (rq->target == XA_STRING) {
        /* XXX: charset is ignored! */

        /* get qemacs yank buffer */
        b = qs->yank_buffers[qs->yank_current];
        if (!b)
            return;
        buf = qe_malloc_array(unsigned char, b->total_size);
        if (!buf)
            return;
        eb_read(b, 0, buf, b->total_size);

        XChangeProperty(xs->display, rq->requestor, rq->property,
                        XA_STRING, 8, PropModeReplace,
                        buf, b->total_size);
        qe_free(&buf);
    } else
    if (rq->target == xa_formats[0]
    ||  rq->target == xa_formats[1]
    ||  rq->target == xa_formats[2]) {
        int len, size;

        /* get qemacs yank buffer */
        b = qs->yank_buffers[qs->yank_current];
        if (!b)
            return;

        /* Get buffer contents encoded in utf-8-unix */
        size = eb_get_content_size(b) + 1;
        buf = qe_malloc_array(unsigned char, size);
        if (!buf)
            return;
        len = eb_get_contents(b, (char *)buf, size, 0);

        XChangeProperty(xs->display, rq->requestor, rq->property,
                        rq->target, 8, PropModeReplace,
                        buf, len);
        qe_free(&buf);
    }
    ev.xselection.property = rq->property;
    XSendEvent(xs->display, rq->requestor, False, 0, &ev);
}

/* fast test to see if the user pressed a key or a mouse button */
static int x11_dpy_is_user_input_pending(QEditScreen *s)
{
    X11State *xs = s->priv_data;
    XEvent xev;

    if (XCheckMaskEvent(xs->display, KeyPressMask | ButtonPressMask, &xev)) {
        XPutBackEvent(xs->display, &xev);
        return 1;
    } else {
        return 0;
    }
}

typedef struct QExposeRegion QExposeRegion;
struct QExposeRegion {
    int pending;
    /* simplistic single rectangle region */
    int x0, y0, x1, y1;
};

static void qe_expose_reset(QEditScreen *s, QExposeRegion *rgn)
{
    rgn->pending = 0;
    rgn->x0 = rgn->y0 = INT_MAX;
    rgn->x1 = rgn->y1 = INT_MIN;
}

static void qe_expose_set(QEditScreen *s, QExposeRegion *rgn,
                          int x, int y, int width, int height)
{
    rgn->pending = 1;
    rgn->x0 = x;
    rgn->y0 = y;
    rgn->x1 = x + width;
    rgn->y1 = y + height;
}

static void qe_expose_add(QEditScreen *s, QExposeRegion *rgn,
                          int x, int y, int width, int height)
{
    rgn->pending++;
    if (rgn->x0 > x)
        rgn->x0 = x;
    if (rgn->y0 > y)
        rgn->y0 = y;
    if (rgn->x1 < x + width)
        rgn->x1 = x + width;
    if (rgn->y1 > y + height)
        rgn->y1 = y + height;
}

static void qe_expose_flush(QEditScreen *s, QExposeRegion *rgn)
{
    if (rgn->pending) {
        QEmacsState *qs = s->qs;
        QEEvent ev1, *ev = qe_event_clear(&ev1);

        /* Ignore expose region */
        ev->expose_event.type = QE_EXPOSE_EVENT;
        qe_handle_event(qs, ev);
        qe_expose_reset(s, rgn);
    }
}

/* called when an X event happens. dispatch events to qe_handle_event() */
static void x11_handle_event(void *opaque)
{
    QEditScreen *s = opaque;
    QEmacsState *qs = s->qs;
    X11State *xs = s->priv_data;
    char buf[16];
    XEvent xev;
    KeySym keysym;
    int len, key, key_state;
    QEEvent ev1, *ev = qe_event_clear(&ev1);
    QExposeRegion rgn1, *rgn = &rgn1;

    qe_expose_reset(s, rgn);

    while (XPending(xs->display)) {
        XNextEvent(xs->display, &xev);
        switch (xev.type) {
        case ClientMessage:
            if ((Atom)xev.xclient.data.l[0] == xs->wm_delete_window) {
                // cancel pending operation
                ev->key_event.type = QE_KEY_EVENT;
                ev->key_event.key = KEY_QUIT;       // C-g
                qe_handle_event(qs, ev);

                // exit qemacs
                ev->key_event.type = QE_KEY_EVENT;
                ev->key_event.key = KEY_EXIT;       // C-x C-c
                qe_handle_event(qs, ev);
            }
            break;
        case ConfigureNotify:
            if (x11_term_resize(s, xev.xconfigure.width, xev.xconfigure.height)) {
                qe_expose_set(s, rgn, 0, 0, s->width, s->height);
                /* FIXME: fullscreen may be set via window managers,
                 * record it to qs->is_full_screen the right way.
                 */
            }
            break;

        case Expose:
            {
                XExposeEvent *xe = &xev.xexpose;

                qe_expose_add(s, rgn, xe->x, xe->y, xe->width, xe->height);
            }
            break;

        case ButtonPress:
        case ButtonRelease:
            {
                XButtonEvent *xe = &xev.xbutton;

                if (xev.type == ButtonPress)
                    ev->button_event.type = QE_BUTTON_PRESS_EVENT;
                else
                    ev->button_event.type = QE_BUTTON_RELEASE_EVENT;

                // TODO: set shift state
                ev->button_event.x = xe->x;
                ev->button_event.y = xe->y;
                switch (xe->button) {
                case Button1:
                    ev->button_event.button = QE_BUTTON_LEFT;
                    break;
                case Button2:
                    ev->button_event.button = QE_BUTTON_MIDDLE;
                    break;
                case Button3:
                    ev->button_event.button = QE_BUTTON_RIGHT;
                    break;
                case Button4:
                    ev->button_event.button = QE_WHEEL_UP;
                    break;
                case Button5:
                    ev->button_event.button = QE_WHEEL_DOWN;
                    break;
                default:
                    continue;
                }
                qe_expose_flush(s, rgn);
                qe_handle_event(qs, ev);
            }
            break;
        case MotionNotify:
            {
                XMotionEvent *xe = &xev.xmotion;
                ev->button_event.type = QE_MOTION_EVENT;
                // TODO: set shift state
                ev->button_event.x = xe->x;
                ev->button_event.y = xe->y;
                qe_expose_flush(s, rgn);
                qe_handle_event(qs, ev);
            }
            break;
            /* selection handling */
        case SelectionClear:
            {
                /* ask qemacs to stop visual notification of selection */
                ev->type = QE_SELECTION_CLEAR_EVENT;
                qe_expose_flush(s, rgn);
                qe_handle_event(qs, ev);
            }
            break;
        case SelectionRequest:
            qe_expose_flush(s, rgn);
            selection_send(xs, &xev.xselectionrequest);
            break;
        case KeyPress:
#ifdef X_HAVE_UTF8_STRING
            /* only present since XFree 4.0.2 */
            {
                Status status;
                len = Xutf8LookupString(xs->xic, &xev.xkey, buf, sizeof(buf),
                                        &keysym, &status);
            }
#else
            {
                static XComposeStatus status;
                len = XLookupString(&xev.xkey, buf, sizeof(buf),
                                    &keysym, &status);
            }
#endif
            key_state = 0;
            if (xev.xkey.state & ShiftMask)
                key_state = KEY_STATE_SHIFT;
            if (xev.xkey.state & ControlMask)
                key_state = KEY_STATE_CONTROL;
            if (xev.xkey.state & Mod1Mask)
                key_state = KEY_STATE_META;
#ifdef CONFIG_DARWIN
            /* Also interpret Darwin's Command key as Meta */
            if (xev.xkey.state & Mod2Mask)
                key_state = KEY_STATE_COMMAND;
#endif
#if 0
            // XXX: should use qe_trace_bytes(qs, buf, out->len, EB_TRACE_KEY);
            fprintf(stderr, "keysym=%lx  state=%lx%s%s%s  len=%d  buf[0]='\\x%02x'\n",
                    (long)keysym, (long)xev.xkey.state,
                    (key_state & KEY_STATE_SHIFT) ? " shft" : "",
                    (key_state & KEY_STATE_CONTROL) ? " ctrl" : "",
                    (key_state & (KEY_STATE_META|KEY_STATE_COMMAND)) ? " meta" : "",
                    len, buf[0]);
#endif
            key = -1;
            switch (keysym) {
            case XK_F1:     key = KEY_F1;     goto got_key;
            case XK_F2:     key = KEY_F2;     goto got_key;
            case XK_F3:     key = KEY_F3;     goto got_key;
            case XK_F4:     key = KEY_F4;     goto got_key;
            case XK_F5:     key = KEY_F5;     goto got_key;
            case XK_F6:     key = KEY_F6;     goto got_key;
            case XK_F7:     key = KEY_F7;     goto got_key;
            case XK_F8:     key = KEY_F8;     goto got_key;
            case XK_F9:     key = KEY_F9;     goto got_key;
            case XK_F10:    key = KEY_F10;    goto got_key;
            case XK_F11:    key = KEY_F11;    goto got_key;
            case XK_F13:    key = KEY_F13;    goto got_key;
            case XK_F14:    key = KEY_F14;    goto got_key;
            case XK_F15:    key = KEY_F15;    goto got_key;
            case XK_F16:    key = KEY_F16;    goto got_key;
            case XK_F17:    key = KEY_F17;    goto got_key;
            case XK_F18:    key = KEY_F18;    goto got_key;
            case XK_F19:    key = KEY_F19;    goto got_key;
            case XK_F20:    key = KEY_F20;    goto got_key;
            case XK_Up:     key = KEY_UP;     goto got_key;
            case XK_Down:   key = KEY_DOWN;   goto got_key;
            case XK_Right:  key = KEY_RIGHT;  goto got_key;
            case XK_Left:   key = KEY_LEFT;   goto got_key;
            case XK_BackSpace: key = KEY_DEL; goto got_key;
            case XK_Insert: key = KEY_INSERT; goto got_key;
            case XK_Delete: key = KEY_DELETE; goto got_key;
            case XK_Home:   key = KEY_HOME;   goto got_key;
            case XK_End:    key = KEY_END;    goto got_key;
            case XK_Prior:  key = KEY_PAGEUP; goto got_key;
            case XK_Next:   key = KEY_PAGEDOWN; goto got_key;
            case XK_ISO_Left_Tab: key = KEY_TAB; goto got_key;
            default:
                key_state &= ~KEY_STATE_SHIFT;
                if (len > 0) {
#ifdef X_HAVE_UTF8_STRING
                    const char *p = buf;
                    buf[len] = '\0';
                    key = utf8_decode(&p);
#else
                    key = buf[0] & 0xff;
#endif
                    if (key < 32 || key == 127)
                        key_state &= ~KEY_STATE_CONTROL;
                }
                break;
            }
            if (key < 0)
                break;
        got_key:
            ev->key_event.type = QE_KEY_EVENT;
            ev->key_event.shift = key_state;
            ev->key_event.key = get_modified_key(key, key_state);
            qe_expose_flush(s, rgn);
            qe_handle_event(qs, ev);
            break;
        }
    }
    qe_expose_flush(s, rgn);
}

/* bitmap handling */

enum X11BitmapType {
    BMP_PIXMAP,
    BMP_XIMAGE,
#ifdef CONFIG_XSHM
    BMP_XSHMIMAGE,
#endif
#ifdef CONFIG_XV
    BMP_XVIMAGE,
#ifdef CONFIG_XSHM
    BMP_XVSHMIMAGE,
#endif
#endif
};

typedef struct X11Bitmap {
    enum X11BitmapType type;
    union {
        Pixmap pixmap;
        XImage *ximage;
#ifdef CONFIG_XV
        XvImage *xvimage;
#endif
    } u;
#ifdef CONFIG_XSHM
    XShmSegmentInfo *shm_info;
#endif
    int x_lock, y_lock; /* destination for locking */
    XImage *ximage_lock;
} X11Bitmap;

static int x11_dpy_bmp_alloc(QEditScreen *s, QEBitmap *b)
{
    X11State *xs = s->priv_data;
    X11Bitmap *xb;

    xb = qe_mallocz(X11Bitmap);
    if (!xb)
        return -1;
    b->priv_data = xb;
    /* choose bitmap type to optimize communication with x server and
       performances */
    if (b->flags & QEBITMAP_FLAG_VIDEO) {
#if defined(CONFIG_XV)
        if (xs->xv_port != 0 && xs->xv_open_count == 0) {
#ifdef CONFIG_XSHM
            if (xs->shm_use)
                xb->type = BMP_XVSHMIMAGE;
            else
#endif
                xb->type = BMP_XVIMAGE;
            b->format = s->video_format;
            xs->xv_open_count++;
        } else
#endif
        {
#ifdef CONFIG_XSHM
            if (xs->shm_use)
                xb->type = BMP_XSHMIMAGE;
            else
#endif
                xb->type = BMP_XIMAGE;
            b->format = s->bitmap_format;
        }
    } else {
        xb->type = BMP_PIXMAP;
        b->format = s->bitmap_format;
    }

    switch (xb->type) {
    default:
    case BMP_PIXMAP:
        xb->u.pixmap = XCreatePixmap(xs->display, xs->window,
                                     b->width, b->height, xs->attr.depth);
        if (!xb->u.pixmap)
            goto fail;
        break;
    case BMP_XIMAGE:
        {
            XImage *ximage;
            ximage = XCreateImage(xs->display, None, xs->attr.depth, ZPixmap, 0,
                                  NULL, b->width, b->height, 8, 0);
            ximage->data = qe_malloc_array(char, b->height * ximage->bytes_per_line);
            xb->u.ximage = ximage;
        }
        break;
#ifdef CONFIG_XSHM
    case BMP_XSHMIMAGE:
        {
            XImage *ximage;
            XShmSegmentInfo *shm_info;

            /* XXX: error testing */
            shm_info = qe_mallocz(XShmSegmentInfo);
            ximage = XShmCreateImage(xs->display, None, xs->attr.depth, ZPixmap, NULL,
                                     shm_info, b->width, b->height);
            shm_info->shmid = shmget(IPC_PRIVATE,
                                     b->height * ximage->bytes_per_line,
                                     IPC_CREAT | 0777);
            ximage->data = shmat(shm_info->shmid, 0, 0);
            shm_info->shmaddr = ximage->data;
            shm_info->readOnly = False;

            XShmAttach(xs->display, shm_info);
            XSync(xs->display, False);

            /* the shared memory will be automatically deleted */
            shmctl(shm_info->shmid, IPC_RMID, 0);
            xb->shm_info = shm_info;
            xb->u.ximage = ximage;
        }
        break;
#endif
#ifdef CONFIG_XV
    case BMP_XVIMAGE:
        {
            XvImage *xvimage;
            xvimage = XvCreateImage(xs->display, xs->xv_port, xs->xv_format, 0,
                                    b->width, b->height);
            xvimage->data = qe_malloc_array(char, xvimage->data_size);
            xb->u.xvimage = xvimage;
        }
        break;
#ifdef CONFIG_XSHM
    case BMP_XVSHMIMAGE:
        {
            XvImage *xvimage;
            XShmSegmentInfo *shm_info;

            shm_info = qe_mallocz(XShmSegmentInfo);
            xvimage = XvShmCreateImage(xs->display, xs->xv_port, xs->xv_format, 0,
                                       b->width, b->height, shm_info);
            shm_info->shmid = shmget(IPC_PRIVATE,
                                     xvimage->data_size,
                                     IPC_CREAT | 0777);
            xvimage->data = shmat(shm_info->shmid, 0, 0);
            shm_info->shmaddr = xvimage->data;
            shm_info->readOnly = False;

            XShmAttach(xs->display, shm_info);
            XSync(xs->display, False);

            /* the shared memory will be automatically deleted */
            shmctl(shm_info->shmid, IPC_RMID, 0);
            xb->shm_info = shm_info;
            xb->u.xvimage = xvimage;
        }
        break;
#endif
#endif
    }
    return 0;
 fail:
    qe_free(&xb);
    return -1;
}

static void x11_dpy_bmp_free(QEditScreen *s, QEBitmap *b)
{
    X11State *xs = s->priv_data;
    X11Bitmap *xb = b->priv_data;

    switch (xb->type) {
    case BMP_PIXMAP:
        XFreePixmap(xs->display, xb->u.pixmap);
        break;
    case BMP_XIMAGE:
        /* NOTE: also frees the ximage data */
        XDestroyImage(xb->u.ximage);
        break;
#ifdef CONFIG_XSHM
    case BMP_XSHMIMAGE:
        XShmDetach(xs->display, xb->shm_info);
        XDestroyImage(xb->u.ximage);
        shmdt(xb->shm_info->shmaddr);
        qe_free(&xb->shm_info);
        break;
#endif
#ifdef CONFIG_XV
    case BMP_XVIMAGE:
        qe_free(&xb->u.xvimage->data);
        XFree(xb->u.xvimage);
        xs->xv_open_count--;
        break;
#ifdef CONFIG_XSHM
    case BMP_XVSHMIMAGE:
        XShmDetach(xs->display, xb->shm_info);
        XFree(xb->u.xvimage);
        shmdt(xb->shm_info->shmaddr);
        qe_free(&xb->shm_info);
        xs->xv_open_count--;
        break;
#endif
#endif
    }
    qe_free(&b->priv_data);
}

static void x11_dpy_bmp_draw(QEditScreen *s, QEBitmap *b,
                             int dst_x, int dst_y, int dst_w, int dst_h,
                             qe__unused__ int offset_x, qe__unused__ int offset_y,
                             qe__unused__ int flags)
{
    X11State *xs = s->priv_data;
    X11Bitmap *xb = b->priv_data;

    /* XXX: handle clipping ? */
    update_rect(xs, dst_x, dst_y, dst_x + dst_w, dst_y + dst_h);

    switch (xb->type) {
    case BMP_PIXMAP:
        XCopyArea(xs->display, xb->u.pixmap, xs->dbuffer, xs->gc,
                  0, 0, b->width, b->height, dst_x, dst_y);
        break;
    case BMP_XIMAGE:
        XPutImage(xs->display, xs->dbuffer, xs->gc,
                  xb->u.ximage, 0, 0, dst_x, dst_y,
                  b->width, b->height);
        break;

#ifdef CONFIG_XSHM
    case BMP_XSHMIMAGE:
        XShmPutImage(xs->display, xs->dbuffer, xs->gc,
                     xb->u.ximage, 0, 0, dst_x, dst_y,
                     b->width, b->height, False);
        break;
#endif
#ifdef CONFIG_XV
    case BMP_XVIMAGE:
        XvPutImage(xs->display, xs->xv_port, xs->window, xs->gc, xb->u.xvimage,
                   0, 0, b->width, b->height,
                   dst_x, dst_y, dst_w, dst_h);
        break;
#ifdef CONFIG_XSHM
    case BMP_XVSHMIMAGE:
        XvShmPutImage(xs->display, xs->xv_port, xs->window, xs->gc, xb->u.xvimage,
                      0, 0, b->width, b->height,
                      dst_x, dst_y, dst_w, dst_h, False);
        break;
#endif
#endif
    }
}

static void x11_dpy_bmp_lock(QEditScreen *s, QEBitmap *b, QEPicture *pict,
                             int x1, int y1, int w1, int h1)
{
    X11State *xs = s->priv_data;
    X11Bitmap *xb = b->priv_data;

    pict->width = w1;
    pict->height = h1;
    pict->format = b->format;
    switch (xb->type) {
    case BMP_PIXMAP:
        {
            XImage *ximage;
            ximage = XCreateImage(xs->display, None, xs->attr.depth, ZPixmap, 0,
                                  NULL, w1, h1, 8, 0);
            ximage->data = qe_malloc_array(char, h1 * ximage->bytes_per_line);
            pict->data[0] = (unsigned char *)ximage->data;
            pict->linesize[0] = ximage->bytes_per_line;
            xb->ximage_lock = ximage;
            xb->x_lock = x1;
            xb->y_lock = y1;
            if (ximage->bits_per_pixel == 32) {
                /* adjust format from QEBITMAP_FORMAT_RGB24 to PIX_FMT_BGR0
                   on little endian architectures */
                pict->format = QEBITMAP_FORMAT_RGBA32;
            }
        }
        break;
    case BMP_XIMAGE:
#ifdef CONFIG_XSHM
    case BMP_XSHMIMAGE:
#endif
        {
            int bpp = (xb->u.ximage->bits_per_pixel + 7) >> 3;
            pict->data[0] = (unsigned char *)xb->u.ximage->data +
                y1 * xb->u.ximage->bytes_per_line + x1 * bpp;
            pict->linesize[0] = xb->u.ximage->bytes_per_line;
            if (bpp == 4) {
                /* adjust format from QEBITMAP_FORMAT_RGB24 to PIX_FMT_BGR0
                   on little endian architectures */
                pict->format = QEBITMAP_FORMAT_RGBA32;
            }
        }
        break;
#ifdef CONFIG_XV
    case BMP_XVIMAGE:
#ifdef CONFIG_XSHM
    case BMP_XVSHMIMAGE:
#endif
        /* XXX: only YUV420P is handled yet */
        {
            XvImage *xvimage = xb->u.xvimage;
            int i, xx, yy, j;
            for (i = 0; i < 3; i++) {
                xx = x1;
                yy = y1;
                j = i;
                if (i >= 1) {
                    xx >>= 1;
                    yy >>= 1;
                    j = 3 - i; /* don't know why they inverted Cb and Cr! */
                }
                pict->data[j] = (unsigned char *)xvimage->data +
                                xvimage->offsets[i] +
                                yy * xvimage->pitches[i] + xx;
                pict->linesize[j] = xvimage->pitches[i];
            }
        }
        break;
#endif
    }
}

static void x11_dpy_bmp_unlock(QEditScreen *s, QEBitmap *b)
{
    X11State *xs = s->priv_data;
    X11Bitmap *xb = b->priv_data;

    switch (xb->type) {
    case BMP_PIXMAP:
        XPutImage(xs->display, xb->u.pixmap, xs->gc_pixmap, xb->ximage_lock,
                  0, 0, xb->x_lock, xb->y_lock,
                  xb->ximage_lock->width, xb->ximage_lock->height);
        /* NOTE: also frees the ximage data */
        XDestroyImage(xb->ximage_lock);
        xb->ximage_lock = NULL;
        break;
    default:
        break;
    }
}

static int x11_dpy_draw_picture(QEditScreen *s,
                                int dst_x, int dst_y, int dst_w, int dst_h,
                                const QEPicture *ip0,
                                int src_x, int src_y, int src_w, int src_h,
                                int flags)
{
    X11State *xs = s->priv_data;
    XImage *im;
    int status = 0, depth;
    const QEPicture *ip = ip0;
    QEPicture *ip1 = NULL;

    /* Only support 32-bit true color X11 displays */
    /* XXX: should enumerate visuals? */
    depth = DefaultDepth(xs->display, xs->xscreen);
    if (depth != 24)
        return 1;

    if (ip->format != QEBITMAP_FORMAT_RGBA32
    ||  !(src_w == dst_w && src_h == dst_h)) {
        ip1 = qe_create_picture(dst_w, dst_h, QEBITMAP_FORMAT_RGBA32, 0);
        if (!ip1)
            return 2;
        /* Convert and/or resize picture */
        if (qe_picture_copy(ip1, 0, 0, dst_w, dst_h,
                            ip0, src_x, src_y, src_w, src_h, flags)) {
            qe_free_picture(&ip1);
            return 3;
        }
        ip = ip1;
        src_x = src_y = 0;
        src_w = dst_w;
        src_h = dst_h;
    }

    im = XCreateImage(xs->display, xs->attr.visual, depth, ZPixmap, 0,
                      NULL, dst_w, dst_h, 32, ip->linesize[0] / 4);
    if (im == NULL) {
        qe_free_picture(&ip1);
        return 4;
    }
    if (im->bits_per_pixel != 32) {
        XDestroyImage(im);
        qe_free_picture(&ip1);
        return 5;
    }

    if (ip->format == QEBITMAP_FORMAT_RGBA32) {
        im->data = (char *)(ip->data[0] + src_y * ip->linesize[0] + src_x * 4);
        im->bytes_per_line = ip->linesize[0];
        update_rect(xs, dst_x, dst_y, dst_x + dst_w, dst_y + dst_h);
        status = XPutImage(xs->display, xs->dbuffer, xs->gc, im,
                           0, 0, dst_x, dst_y, dst_w, dst_h);
        XFlush(xs->display);
    } else {
        status = 6;
    }

    im->data = NULL;
    XDestroyImage(im);
    qe_free_picture(&ip1);
    return status;
}

static QEDisplay x11_dpy = {
    "x11", 1, 1,
    x11_dpy_probe,
    x11_dpy_init,
    x11_dpy_close,
    x11_dpy_flush,
    x11_dpy_is_user_input_pending,
    x11_dpy_fill_rectangle,
    x11_dpy_xor_rectangle,
    x11_dpy_open_font,
    x11_dpy_close_font,
    x11_dpy_text_metrics,
    x11_dpy_draw_text,
    x11_dpy_set_clip,
    x11_dpy_selection_activate,
    x11_dpy_selection_request,
    NULL, /* dpy_invalidate */
    NULL, /* dpy_cursor_at */
    x11_dpy_bmp_alloc,
    x11_dpy_bmp_free,
    x11_dpy_bmp_draw,
    x11_dpy_bmp_lock,
    x11_dpy_bmp_unlock,
    x11_dpy_draw_picture,
    x11_dpy_full_screen,
    NULL, /* dpy_describe */
    NULL, /* dpy_sound_bell */
    NULL, /* dpy_suspend */
    qe_dpy_error, /* dpy_error */
    NULL, /* next */
};

static void x11_list_fonts(EditState *s, int argval)
{
    QEditScreen *screen = s->qs->screen;
    X11State *xs = screen->priv_data;
    char buf[80];
    EditBuffer *b;
    int i, count;
    char **list;

    b = new_help_buffer(s);
    if (!b)
        return;

    if (argval == NO_ARG) {
        snprintf(buf, sizeof(buf), "-*-*-*-*-*-*-*-*-*-*-*-*-iso10646-1");
    } else {
        snprintf(buf, sizeof(buf), "-*-*-*-*-*-*-*-%d-*-*-*-*-iso10646-1", argval);
    }
    list = XListFonts(xs->display, buf, 20000, &count);

    eb_printf(b, "\n%d entries\n\n", count);

    for (i = 0; i < count; i++) {
        eb_printf(b, "%d: %s\n", i, list[i]);
    }
    XFreeFontNames(list);

    show_popup(s, b, "X11 Font list");
}

static const CmdDef x11_commands[] = {
    CMD2( "x11-list-fonts", "C-h C-f",
          "List the X11 fonts in a popup window",
          x11_list_fonts, ESi, "P")
};

static CmdLineOptionDef cmd_options[] = {
    CMD_LINE_STRING("d", "display", "DISPLAY", &display_str,
                    "set X11 display"),
    CMD_LINE_STRING("g", "geometry", "WxH", &geometry_str,
                    "set X11 display size"),
    CMD_LINE_INT_ARG("fs", "font-size", "ptsize", &font_ptsize,
                     "set default font size"),
    CMD_LINE_LINK()
};

static int x11_init(QEmacsState *qs) {
    qe_register_cmd_line_options(qs, cmd_options);
    qe_register_commands(qs, NULL, x11_commands, countof(x11_commands));
    if (force_tty)
        return 0;
    return qe_register_display(qs, &x11_dpy);
}

qe_module_init(x11_init);
