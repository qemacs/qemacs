/*
 * X11 handling for QEmacs
 * Copyright (c) 2000, 2001, 2002, 2003 Fabrice Bellard.
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

#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>
#ifdef CONFIG_XFT
#include <X11/extensions/Xrender.h>
#include <X11/Xft/Xft.h>
#endif
#ifdef CONFIG_XV
#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvlib.h>
#endif

//#define CONFIG_DOUBLE_BUFFER

/* NOTE: XFT code is currently broken */

static QEFont *term_open_font(QEditScreen *s, int style, int size);
static void term_close_font(QEditScreen *s, QEFont *font);
static void xv_init(QEditScreen *s);
static void x11_handle_event(void *opaque);

static Display *display;
static int xscreen;
static Window window;
static GC gc, gc_pixmap;
static XWindowAttributes attr;
static int event_mask;
static int screen_width, screen_height;
static int last_window_width, last_window_height, last_window_x, last_window_y;
static XIM xim; /* X11 input method */
static XIC xic; /* X11 input context */
static Pixmap dbuffer;
static int shm_use;
#ifdef CONFIG_XFT
static XftDraw		*renderDraw;
#endif
#ifdef CONFIG_XV
static unsigned int xv_nb_formats, xv_nb_adaptors, xv_port, xv_format, xv_open_count;
static XvAdaptorInfo *xv_ai;
static XvImageFormatValues *xv_fo;
#endif

#ifdef CONFIG_DOUBLE_BUFFER
/* update region to speed up double buffer flush */
#define UPDATE_MAX_REGIONS 3
#define UPDATE_DMIN 64
static int update_nb;
static CSSRect update_rects[UPDATE_MAX_REGIONS];
#endif

extern QEDisplay x11_dpy;
static int visual_depth;

static int force_tty = 0;
static const char *display_str = "";
static const char *geometry_str = "80x25";

const char *default_x11_fonts[NB_FONT_FAMILIES] = {
#ifdef CONFIG_XFT
    "mono",
#else
    "fixed,unifont",
#endif
    "times,unifont",
    "helvetica,unifont",
};
    
static int font_ptsize = 0;

#ifdef CONFIG_DOUBLE_BUFFER
static void update_reset(void)
{
    int i;
    for(i=0;i<UPDATE_MAX_REGIONS;i++)
        css_set_rect(update_rects + i, 0, 0, 0, 0);
    update_nb = 0;
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

static void update_rect(int x1, int y1, int x2, int y2)
{
    CSSRect r, *r1, *r2;
    int i, d, dmin;

    css_set_rect(&r, x1, y1, x2, y2);
    if (css_is_null_rect(&r))
        return;

    /* find closest rectangle */
    dmin = MAXINT;
    r2 = update_rects;
    r1 = NULL;
    for(i=0;i<update_nb;i++) {
        d = rect_dist(r2, &r);
        if (d < dmin) {
            dmin = d;
            r1 = r2;
        }
        r2++;
    }
    if (dmin < UPDATE_DMIN || update_nb == UPDATE_MAX_REGIONS) {
        css_union_rect(r1, &r);
    } else {
        *r2 = r;
        update_nb++;
    }
}

#else
static inline void update_rect(int x1, int y1, int x2, int y2)
{
}
#endif

static int term_probe(void)
{
    char *dpy;

    if (force_tty)
        return 0;

    /* if no env variable DISPLAY, we do not use x11 */
    dpy = getenv("DISPLAY");
    if (dpy == NULL ||
        dpy[0] == '\0')
        return 0;
    return 1;
}

static int term_init(QEditScreen *s, int w, int h)
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

    memcpy(&s->dpy, &x11_dpy, sizeof(QEDisplay));

    s->private = NULL;
    s->media = CSS_MEDIA_SCREEN;

    display = XOpenDisplay(display_str);
    if (display == NULL) {
        fprintf(stderr, "Could not open X11 display - exiting.\n");
        return -1;
    }
    xscreen = DefaultScreen(display);

    bg = BlackPixel(display, xscreen);
    fg = WhitePixel(display, xscreen);
    screen_width = DisplayWidth(display, xscreen);
    screen_height = DisplayHeight(display, xscreen);

    /* At this point, we should be able to ask for metrics */
    if (font_ptsize)
        qe_styles[0].font_size = font_ptsize;
    get_style(NULL, &default_style, 0);
    font = term_open_font(s, default_style.font_style, 
                            default_style.font_size);
    if (!font) {
        fprintf(stderr, "Could not open default font\n");
        exit(1);
    }
    font_ysize = font->ascent + font->descent;
    font_xsize = glyph_width(s, font, 'x');
    term_close_font(s, font);
    
    p = geometry_str;
    xsize = strtol(p, (char **)&p, 0);
    if (*p == 'x')
        p++;
    ysize = strtol(p, (char **)&p, 0);
    if (w > 0 && h > 0) {
        xsize = w;
        ysize = h;
    }

    if (xsize <= 0 || ysize <=0) {
        fprintf(stderr, "Invalid geometry '%s'\n", geometry_str);
        exit(1);
    }

    xsize *= font_xsize;
    ysize *= font_ysize;
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
    window = XCreateSimpleWindow(display,
                                 DefaultRootWindow(display),
                                 hint.x, hint.y,
                                 hint.width, hint.height,
                                 4, fg, bg);
    /* Enable backing store */
    xwa.backing_store = Always;
    XChangeWindowAttributes(display, window, CWBackingStore, &xwa);

    XSelectInput(display, window, StructureNotifyMask);

    /* Tell other applications about this window */

    XSetStandardProperties(display, window,
			   "qemacs", "qemacs",
			   None, NULL, 0, &hint);

    /* Map window. */

    XMapWindow(display, window);

    /* Wait for map. */
    while (1) {
	XEvent xev;
	XNextEvent(display, &xev);
	if (xev.type == MapNotify && xev.xmap.event == window)
	    break;
    }
    event_mask = KeyPressMask | ButtonPressMask | ButtonReleaseMask | 
        ButtonMotionMask | ExposureMask | StructureNotifyMask;
    XSelectInput(display, window, event_mask);

    XGetWindowAttributes(display, window, &attr);

    /* see if we can bypass the X color functions */
    visual_depth = 0;
    if (attr.visual->class == TrueColor) {
        XVisualInfo *vinfo, templ;
        int n;
        templ.visualid = attr.visual->visualid;
        vinfo = XGetVisualInfo(display, VisualIDMask, &templ, &n);
        if (vinfo) {
            visual_depth = vinfo->depth;
        }
    }

    xim = XOpenIM(display, NULL, NULL, NULL);
    xic = XCreateIC(xim, XNInputStyle,  XIMPreeditNothing |
                    XIMStatusNothing,
                    XNClientWindow, window,
                    XNFocusWindow, window,
                    NULL);

    gc = XCreateGC(display, window, 0, NULL);
#ifdef CONFIG_XFT
    renderDraw = XftDrawCreate (display, window, attr.visual, 
                                DefaultColormap (display, xscreen));
#endif
    /* double buffer handling */
    gc_pixmap = XCreateGC(display, window, 0, NULL);
    gc_val.graphics_exposures = 0;
    XChangeGC(display, gc_pixmap, GCGraphicsExposures, &gc_val);
    XSetForeground(display, gc, BlackPixel(display, xscreen));
#ifdef CONFIG_DOUBLE_BUFFER
    dbuffer = XCreatePixmap(display, window, xsize, ysize, attr.depth);
    /* erase pixmap */
    XFillRectangle(display, dbuffer, gc, 
                   0, 0, xsize, ysize);
    update_reset();
#else
    dbuffer = window;
#endif

    /* shm extension usable ? */
    {
        const char *p;
        int is_local;
        
        p = XDisplayName(display_str);
        strstart(p, "unix:", &p);
        strstart(p, "localhost:", &p);
        is_local = (*p == ':');
        shm_use = 0;
        if (is_local && XShmQueryExtension(display))
            shm_use = 1;
    }
    /* compute bitmap format */
    switch(visual_depth) {
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
    fd = ConnectionNumber(display);
    set_read_handler(fd, x11_handle_event, s);
    return 0;
}

#ifdef CONFIG_XV
static void xv_init(QEditScreen *s)
{
    int i;
    XvPortID xv_p;

    xv_port = 0; /* zero means no XV port found */
    if (XvQueryAdaptors(display,
                        DefaultRootWindow(display), 
                        &xv_nb_adaptors, &xv_ai) != Success)
        return;

    for (i = 0; i < xv_nb_adaptors; i++) {
        if ((xv_ai[i].type & XvInputMask) && 
            (xv_ai[i].type & XvImageMask)) {
            for (xv_p = xv_ai[i].base_id; 
                 xv_p < xv_ai[i].base_id + xv_ai[i].num_ports; 
                 xv_p++) {
                if (!XvGrabPort(display, xv_p, CurrentTime)) {
                    xv_port = xv_p;
                    goto found;
                }
            }
        }
    }
    return;
 found:
    
    xv_fo = XvListImageFormats(display, xv_port, &xv_nb_formats);
    for(i = 0; i < xv_nb_formats; i++) {
        XvImageFormatValues *fo = &xv_fo[i];
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
            xv_format = fo->id;
            break;
        }
    }
    /* if no format found, then release port */
    if (i == xv_nb_formats) {
        XvUngrabPort(display, xv_p, CurrentTime);
        xv_port = 0;
    } else {
        s->video_format = QEBITMAP_FORMAT_YUV420P;
    }
}
#endif

static void term_close(QEditScreen *s)
{
#ifdef CONFIG_DOUBLE_BUFFER
    XFreePixmap(display, dbuffer);
#endif
    XCloseDisplay(display);
}

static void term_resize(QEditScreen *s, int w, int h)
{
    s->width = w;
    s->height = h;
#ifdef CONFIG_DOUBLE_BUFFER
    /* resize double buffer */
    XFreePixmap(display, dbuffer);
    dbuffer = XCreatePixmap(display, window, w, h, attr.depth);
#endif
}
                
static unsigned long get_x11_color(QEColor color)
{
    unsigned int r, g, b;
    XColor col;

    r = (color >> 16) & 0xff;
    g = (color >> 8) & 0xff;
    b = (color) & 0xff;
    switch(visual_depth) {
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
        XAllocColor(display, attr.colormap, &col);
        return col.pixel;
    }
}

static void xor_rectangle(QEditScreen *s, 
                          int x, int y, int w, int h)
{
    int fg;

    fg = WhitePixel(display, xscreen);
    XSetForeground(display, gc, fg);
    XSetFunction(display, gc, GXxor);

    XFillRectangle(display, dbuffer, gc, x, y, w, h);

    XSetFunction(display, gc, GXcopy);
}


/* Print the unicode string 'str' with baseline at position (x,y). The
   higher bits of each char may contain attributes. */
#ifdef CONFIG_XFT

static void term_fill_rectangle(QEditScreen *s,
                                int x1, int y1, int w, int h, QEColor color)
{
    XftColor col;
    int r, g, b, a;

    if (color == QECOLOR_XOR) {
        xor_rectangle(s, x1, y1, w, h);
        return;
    }
    
    a = (color >> 24) & 0xff;
    r = (color >> 16) & 0xff;
    g = (color >> 8) & 0xff;
    b = (color) & 0xff;
    /* not exact, but faster */
    col.color.red = r << 8;
    col.color.green = g << 8;
    col.color.blue = b << 8;
    col.color.alpha = a << 8;
    col.pixel = get_x11_color(color);
    XftDrawRect (renderDraw, 
                 &col,
                 x1, y1, 
                 w, h);
}

static QEFont *term_open_font(QEditScreen *s, int style, int size)
{
    const char *family;
    int weight, slant;
    XftFont *renderFont;
    QEFont *font;

    font = malloc(sizeof(QEFont));
    if (!font)
        return NULL;


    switch(style & QE_FAMILY_MASK) {
    default:
    case QE_FAMILY_FIXED:
        family = font_family_str;
        break;
    case QE_FAMILY_SANS:
        family = "sans";
        break;
    case QE_FAMILY_SERIF:
        family = "serif";
        break;
    }
    weight = XFT_WEIGHT_MEDIUM;
    if (style & QE_STYLE_BOLD)
        weight = XFT_WEIGHT_BOLD;
    slant = XFT_SLANT_ROMAN;
    if (style & QE_STYLE_ITALIC)
        slant = XFT_SLANT_ITALIC;
    renderFont = XftFontOpen(display, xscreen, 
                             XFT_FAMILY, XftTypeString, family,
                             XFT_SIZE, XftTypeInteger, size,
                             XFT_WEIGHT, XftTypeInteger, weight,
                             XFT_SLANT, XftTypeInteger, slant,
                             0);
    font->ascent = renderFont->ascent;
    font->descent = renderFont->descent;
    font->private = renderFont;
    return font;
}

static void term_close_font(QEditScreen *s, QEFont *font)
{
    XftFont *renderFont = font->private;

    XftFontClose(display, renderFont);
    free(font);
}

static int term_glyph_width(QEditScreen *s, QEFont *font, unsigned int cc)
{
    XftFont *renderFont = font->private;
    XGlyphInfo gi;

    XftTextExtents32 (display, renderFont, &cc, 1, &gi);
    return gi.xOff;
}

static void term_draw_text(QEditScreen *s, QEFont *font,
                           int x, int y, const unsigned int *str, int len,
                           QEColor color)
{
    XftFont *renderFont = font->private;
    XftColor col;
    int r, g, b, a;
    
    a = (color >> 24) & 0xff;
    r = (color >> 16) & 0xff;
    g = (color >> 8) & 0xff;
    b = (color) & 0xff;
    /* not exact, but faster */
    col.color.red = r << 8;
    col.color.green = g << 8;
    col.color.blue = b << 8;
    col.color.alpha = a << 8;
    col.pixel = get_x11_color(color);
    
    XftDrawString32(renderDraw, &col, renderFont, x, y, 
                    (XftChar32 *)str, len);
}

#else

static void term_fill_rectangle(QEditScreen *s,
                                int x1, int y1, int w, int h, QEColor color)
{
    unsigned long xcolor;

    update_rect(x1, y1, x1 + w, y1 + h);

    if (color == QECOLOR_XOR) {
        xor_rectangle(s, x1, y1, w, h);
        return;
    }

    xcolor = get_x11_color(color);
    XSetForeground(display, gc, xcolor);
    XFillRectangle(display, dbuffer, gc, 
                   x1, y1, w, h);
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


static QEFont *term_open_font(QEditScreen *s, int style, int size)
{
    char family[128];
    const char *family_list, *p1;
    XFontStruct *xfont;
    QEFont *font;
    char buf[512];
    int count, found, dist, dist_min, i, size1, font_index, font_fallback;
    char **list;
    const char *p;

    font = malloc(sizeof(QEFont));
    if (!font)
        return NULL;
    
    /* get font name */
    font_index = ((style & QE_FAMILY_MASK) >> QE_FAMILY_SHIFT) - 1;
    if ((unsigned)font_index >= NB_FONT_FAMILIES)
        font_index = 0; /* fixed font is default */
    family_list = qe_state.system_fonts[font_index];
    if (family_list[0] == '\0')
        family_list = default_x11_fonts[font_index];

    /* take the nth font number in family list */
    font_fallback = (style & QE_FAMILY_FALLBACK_MASK) >> QE_FAMILY_FALLBACK_SHIFT;
    p = family_list;
    for(i=0;i<font_fallback;i++) {
        p = strchr(p, ',');
        if (!p) {
            /* no font found */
            free(font);
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
    for(i=0;i<2;i++) {
        char buf1[32];
        if (i == 0)
            snprintf(buf1, sizeof(buf1), "%d", size * 10);
        else
            strcpy(buf1, "*");
        snprintf(buf, sizeof(buf),
                 "-*-%s-*-*-*-*-*-%s-*-*-*-*-*-*", 
                 family, buf1);
        list = XListFonts(display, buf, 256, &count);
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
    list = XListFonts(display, buf, 256, &count);
    if (count == 0)
        goto fail;
#endif
    /* iterate thru each font and select closer one */
    found = 0;
    dist_min = MAXINT;
    for(i=0;i<count;i++) {
        dist = 0;
        p = list[i] + 1;
        get_entry(NULL, 0, &p);
        get_entry(NULL, 0, &p); /* family */
        get_entry(buf, sizeof(buf), &p); /* weight */
        if (!((!strcmp(buf, "bold") && (style & QE_STYLE_BOLD)) ||
              (!strcmp(buf, "medium") && !(style & QE_STYLE_BOLD))))
            dist += 3;
        get_entry(buf, sizeof(buf), &p); /* slant */
        if (!((!strcmp(buf, "o") && (style & QE_STYLE_ITALIC)) ||
              (!strcmp(buf, "i") && (style & QE_STYLE_ITALIC)) ||
              (!strcmp(buf, "r") && !(style & QE_STYLE_ITALIC))))
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
        if (strcmp(p, "iso10646-1") != 0)
            dist += 3;

        if (dist < dist_min) {
            found = i;
            dist_min = dist;
        }
    }

    xfont = XLoadQueryFont(display, list[found]);
    if (!xfont)
        goto fail;
    XFreeFontNames(list);

    font->ascent = xfont->ascent;
    font->descent = xfont->descent;
    font->private = xfont;
    return font;
 fail:
    XFreeFontNames(list);
    free(font);
    return NULL;
}

static void term_close_font(QEditScreen *s, QEFont *font)
{
    XFontStruct *xfont = font->private;

    XFreeFont(display, xfont);
    free(font);
}

/* get a char struct associated to a char. Return NULL if no glyph
   associated. */
static XCharStruct *get_char_struct(QEFont *font, int cc)
{
    XFontStruct *xfont = font->private;
    int b1, b2;
    XCharStruct *cs;

    if (xfont->min_byte1 == 0 && xfont->max_byte1 == 0) {
        if (cc > xfont->max_char_or_byte2)
            return NULL;
        cc -= xfont->min_char_or_byte2;
        if (cc < 0)
            return NULL;
    } else {
        b1 = (cc >> 8) & 0xff;
        b2 = cc & 0xff;
        if (b1 > xfont->max_byte1)
            return NULL;
        b1 -= xfont->min_byte1;
        if (b1 < 0)
            return NULL;
        if (b2 > xfont->max_char_or_byte2)
            return NULL;
        b2 -= xfont->min_char_or_byte2;
        if (b2 < 0)
            return NULL;
        cc = b1 * (xfont->max_char_or_byte2 - 
                   xfont->min_char_or_byte2 + 1) + b2;
    }
    cs = xfont->per_char;
    if (!cs)
        return &xfont->min_bounds; /* all char have same metrics */
    cs += cc;
    /* fast test for non existant char */
    if (cs->width == 0 &&
        (cs->ascent | cs->descent | cs->rbearing | cs->lbearing) == 0) {
        return NULL;
    } else {
        return cs;
    }
}

static XCharStruct *handle_fallback(QEditScreen *s, QEFont **out_font, 
                                    QEFont *font, unsigned int cc)
{
    XFontStruct *xfont = font->private;
    XCharStruct *cs;
    int fallback_count;
    QEFont *font1;


    /* fallback case */
    fallback_count = 1;
    for(;;) {
        font1 = select_font(s, font->style | 
                            (fallback_count << QE_FAMILY_FALLBACK_SHIFT), font->size);
        if (!font1)
            break;
        cs = get_char_struct(font1, cc);
        if (cs) {
            *out_font = font1;
            return cs;
        }
        fallback_count++;
    }
    
    /* really no glyph : use default char in current font */
    xfont = font->private;
    cs = get_char_struct(font, xfont->default_char);
    *out_font = font;
    return cs;
}

static void term_text_metrics(QEditScreen *s, QEFont *font, 
                              QECharMetrics *metrics,
                              const unsigned int *str, int len)
{
    QEFont *font1;
    XCharStruct *cs;
    int i, x;
    unsigned int cc;

    metrics->font_ascent = font->ascent;
    metrics->font_descent = font->descent;
    x = 0;
    for(i=0;i<len;i++) {
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
                metrics->font_ascent = max(metrics->font_ascent, font1->ascent);
                metrics->font_descent = max(metrics->font_descent, font1->descent);
            }
        }
    }
    metrics->width = x;
}

static void term_draw_text(QEditScreen *s, QEFont *font,
                           int x1, int y, const unsigned int *str, int len,
                           QEColor color)
{
    XFontStruct *xfont;
    QEFont *font1, *last_font;
    XCharStruct *cs;
#ifdef __GNUC__
    XChar2b x11_str[len];
#else
    XChar2b x11_str[LINE_MAX_SIZE];
#endif
    XChar2b *q;
    int i, l, x, x_start;
    unsigned int cc;
    unsigned long xcolor;
    
    xcolor = get_x11_color(color);
    XSetForeground(display, gc, xcolor);
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
                xfont = font->private;
                cc = xfont->default_char;
            }
        }
        /* flush previous chars if font change needed */
        if (font1 != last_font && q > x11_str) {
            xfont = last_font->private;
            l = q - x11_str;
            XSetFont(display, gc, xfont->fid);
            XDrawString16(display, dbuffer, gc,
                          x_start, y, x11_str, l);
            update_rect(x_start, y - last_font->ascent, x, y + last_font->descent);
            x_start = x;
            q = x11_str;
        }
        last_font = font1;
        q->byte1 = (cc >> 8) & 0xff;
        q->byte2 = (cc) & 0xff;
        q++;
        x += cs->width;
    }
    if (q > x11_str) {
        /* flush remaining chars (more common case) */
        xfont = last_font->private;
        l = q - x11_str;
        XSetFont(display, gc, xfont->fid);
        XDrawString16(display, dbuffer, gc,
                      x_start, y, x11_str, l);
        update_rect(x_start, y - last_font->ascent, x, y + last_font->descent);
    }
    /* underline synthesis */
    if (font->style & (QE_STYLE_UNDERLINE | QE_STYLE_LINE_THROUGH)) {
        int dy, h, w;
        h = (font->descent + 2) / 4;
        if (h < 1)
            h = 1;
        w = x - x1;
        if (font->style & QE_STYLE_UNDERLINE) {
            dy = (font->descent + 1) / 3;
            XFillRectangle(display, dbuffer, gc, 
                           x1, y + dy, w, h);
        }
        if (font->style & QE_STYLE_LINE_THROUGH) {
            dy = -(font->ascent / 2 - 1);
            XFillRectangle(display, dbuffer, gc, 
                           x1, y + dy, w, h);
        }
    }
}
#endif

static void term_set_clip(QEditScreen *s,
                          int x, int y, int w, int h)
{
    XRectangle rect;

    rect.x = x;
    rect.y = y;
    rect.width = w;
    rect.height = h;
    XSetClipRectangles(display, gc, 0, 0, &rect, 1, YXSorted);
}

static void term_flush(QEditScreen *s)
{
#ifdef CONFIG_DOUBLE_BUFFER
    CSSRect *r;
    int i, w, h;

    r = update_rects;
    for(i=0;i<update_nb;i++) {
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
            XCopyArea(display, dbuffer, window, gc_pixmap,
                      r->x1, r->y1, w, h, r->x1, r->y1);
#if 0
            XSetForeground(display, gc_pixmap, 0xffff);
            XDrawRectangle(display, window, gc_pixmap, r->x1, r->y1,
                           w - 1, h - 1);
#endif
        }
        r++;
    }
#endif
    XFlush(display);
#ifdef CONFIG_DOUBLE_BUFFER
    update_reset();
#endif
}

static void x11_full_screen(QEditScreen *s, int full_screen)
{
    XWindowAttributes attr;
    Window win;

    XGetWindowAttributes(display, window, &attr);
    if (full_screen) {
        if ((attr.width != screen_width || attr.height != screen_height)) {
            /* store current window position and size */
	    XTranslateCoordinates(display, window, attr.root, 0, 0,
                                  &last_window_x, &last_window_y, &win);
            last_window_width = attr.width;
            last_window_height = attr.height;
            XMoveResizeWindow(display, window, 
                              0, 0, screen_width, screen_height);
        }
    } else if (!full_screen) {
        if (attr.width == screen_width && attr.height == screen_height) {
            XMoveResizeWindow(display, window, 
                              last_window_x, last_window_y, 
                              last_window_width, last_window_height);
        }
    }
}

static void term_selection_activate(QEditScreen *s)
{
    /* own selection from now */
    XSetSelectionOwner(display, XA_PRIMARY, window, CurrentTime);
}

static Bool test_event(Display *dpy, XEvent *ev, char *arg)
{
    return (ev->type == SelectionNotify);
}

/* request the selection from the GUI and put it in a new yank buffer
   if needed */
static void term_selection_request(QEditScreen *s)
{
    Window w;
    Atom prop;
    long nread, bytes_after;
    unsigned char *data;
    Atom actual_type;
    int actual_fmt;
    long nitems;
    EditBuffer *b;
    XEvent xev;

    w = XGetSelectionOwner (display, XA_PRIMARY);
    if (w == None || w == window)
        return; /* qemacs can use its own selection */

    /* use X11 selection (Will be pasted when receiving
       SelectionNotify event) */
    prop = XInternAtom (display, "VT_SELECTION", False);
    XConvertSelection (display, XA_PRIMARY, XA_STRING,
                       prop, window, CurrentTime);

    /* XXX: add timeout too if the target application is not well
       educated */
    XIfEvent(display, &xev, test_event, NULL);
    
    w = xev.xselection.requestor;
    prop = xev.xselection.property;
    
    /* copy GUI selection a new yank buffer */
    b = new_yank_buffer();
    
    nread = 0;
    for(;;) {
        if ((XGetWindowProperty (display, w, prop,
                                 nread/4, 4096, True,
                                 AnyPropertyType, &actual_type, &actual_fmt,
                                 &nitems, &bytes_after,
                                 &data) != Success) ||
            (actual_type != XA_STRING)) {
            XFree (data);
            break;
        }
        
       eb_write(b, nread, data, nitems);
       
       nread += nitems;
       XFree (data);
       
       if (bytes_after == 0)
           break;
   }
}

/* send qemacs selection to requestor */
static void selection_send(XSelectionRequestEvent *rq)
{
    static Atom xa_targets = None;
    QEmacsState *qs = &qe_state;
    unsigned char *buf;
    XEvent ev;
    EditBuffer *b;

    if (xa_targets == None)
        xa_targets = XInternAtom (display, "TARGETS", False);
   
    ev.xselection.type      = SelectionNotify;
    ev.xselection.property  = None;
    ev.xselection.display   = rq->display;
    ev.xselection.requestor = rq->requestor;
    ev.xselection.selection = rq->selection;
    ev.xselection.target	   = rq->target;
    ev.xselection.time      = rq->time;

    if (rq->target == xa_targets) {
	unsigned int target_list[2];

        /* indicate which are supported types */
 	target_list[0] = xa_targets;
 	target_list[1] = XA_STRING;

 	XChangeProperty (display, rq->requestor, rq->property,
 			 xa_targets, 8*sizeof(target_list[0]), PropModeReplace,
 			 (char *)target_list,
			 sizeof(target_list)/sizeof(target_list[0]));
    } else if (rq->target == XA_STRING) {
        /* get qemacs yank buffer */
       
        b = qs->yank_buffers[qs->yank_current];
        if (!b) 
            return;
        buf = malloc(b->total_size);
        if (!buf)
            return;
        eb_read(b, 0, buf, b->total_size);
       
        XChangeProperty (display, rq->requestor, rq->property,
                         XA_STRING, 8, PropModeReplace,
                         buf, b->total_size);
        free(buf);
    }
    ev.xselection.property = rq->property;
    XSendEvent (display, rq->requestor, False, 0, &ev);
}

/* fast test to see if the user pressed a key or a mouse button */
static int x11_is_user_input_pending(QEditScreen *s)
{
    XEvent xev;

    if (XCheckMaskEvent(display, KeyPressMask | ButtonPressMask, &xev)) {
        XPutBackEvent(display, &xev);
        return 1;
    } else {
        return 0;
    }
}

/* called when an X event happens. dispatch events to qe_handle_event() */
static void x11_handle_event(void *opaque)
{
    QEditScreen *s = opaque;
    unsigned char buf[16];
    XEvent xev;
    KeySym keysym;
    int shift, ctrl, meta, len, key;
    QEEvent ev1, *ev = &ev1;

    for(;;) {
        if (!XPending(display))
            return;
        XNextEvent(display, &xev);
        switch(xev.type) {
        case ConfigureNotify:
            {
                int w, h;
                w = xev.xconfigure.width;
                h = xev.xconfigure.height;
                term_resize(s, w, h);
                goto expose_event;
            }
        case Expose:
        expose_event:
        ev->expose_event.type = QE_EXPOSE_EVENT;
        qe_handle_event(ev);
        break;
        
        case ButtonPress:
        case ButtonRelease:
            {
                XButtonEvent *xe = (XButtonEvent *)&xev;
                if (xev.type == ButtonPress)
                    ev->button_event.type = QE_BUTTON_PRESS_EVENT;
                else
                    ev->button_event.type = QE_BUTTON_RELEASE_EVENT;
                ev->button_event.x = xe->x;
                ev->button_event.y = xe->y;
                switch(xe->button) {
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
                    return;
                }
            }
            qe_handle_event(ev);
            break;
        case MotionNotify:
            {
                XMotionEvent *xe = (XMotionEvent *)&xev;
                ev->button_event.type = QE_MOTION_EVENT;
                ev->button_event.x = xe->x;
                ev->button_event.y = xe->y;
                qe_handle_event(ev);
            }
            break;
            /* selection handling */
        case SelectionClear:
            {
                /* ask qemacs to stop visual notification of selection */
                ev->type = QE_SELECTION_CLEAR_EVENT;
                qe_handle_event(ev);
            }
            break;
        case SelectionRequest:
            selection_send((XSelectionRequestEvent *)&xev);
            break;
        case KeyPress:
#ifdef X_HAVE_UTF8_STRING
            /* only present since XFree 4.0.2 */
            {
                Status status;
                len = Xutf8LookupString(xic, (XKeyEvent *)&xev, buf,
                                        sizeof(buf),
                                        &keysym, &status);
            }
#else
            {
                static XComposeStatus status;
                len = XLookupString((XKeyEvent *)&xev, buf,
                                    sizeof(buf),
                                    &keysym, &status);
            }
#endif
            shift = (xev.xkey.state & ShiftMask);
            ctrl = (xev.xkey.state & ControlMask);
            meta = (xev.xkey.state & Mod1Mask);

            if (ctrl) {
                switch(keysym) {
                case XK_Right:
                    key = KEY_CTRL_RIGHT;
                    goto got_key;
                case XK_Left:
                    key = KEY_CTRL_LEFT;
                    goto got_key;
                case XK_Home:
                    key = KEY_CTRL_HOME;
                    goto got_key;
                case XK_End:
                    key = KEY_CTRL_END;
                    goto got_key;
                default:
                    if (len > 0) {
                        key = buf[0];
                        goto got_key;
                    }
                    break;
                }
            } else if (meta) {
                switch(keysym) {
                case XK_BackSpace:
                    key = KEY_META(KEY_BACKSPACE);
                    goto got_key;
                default:
                    if (keysym >= ' ' && keysym <= '~') {
                        key = KEY_META(' ') + keysym - ' ';
                        goto got_key;
                    }
                    break;
                }
            } else {
                switch(keysym) {
                case XK_F1:
                case XK_F2:
                case XK_F3:
                case XK_F4:
                case XK_F5:
                case XK_F6:
                case XK_F7:
                case XK_F8:
                case XK_F9:
                case XK_F10:
                case XK_F11:
                case XK_F12:
                    key = KEY_F1 + keysym - XK_F1;
                    goto got_key;
                case XK_Up:
                    key = KEY_UP;
                    goto got_key;
                case XK_Down:
                    key = KEY_DOWN;
                    goto got_key;
                case XK_Right:
                    key = KEY_RIGHT;
                    goto got_key;
                case XK_Left:
                    key = KEY_LEFT;
                    goto got_key;
                case XK_BackSpace:
                    key = KEY_BACKSPACE;
                    goto got_key;
                case XK_Insert:
                    key = KEY_INSERT;
                    goto got_key;
                case XK_Delete:
                    key = KEY_DELETE;
                    goto got_key;
                case XK_Home:
                    key = KEY_HOME;
                    goto got_key;
                case XK_End:
                    key = KEY_END;
                    goto got_key;
                case XK_Prior:
                    key = KEY_PAGEUP;
                    goto got_key;
                case XK_Next:
                    key = KEY_PAGEDOWN;
                    goto got_key;
                case XK_ISO_Left_Tab:
                    key = KEY_SHIFT_TAB;
                    goto got_key;
                default:
                    if (len > 0) {
#ifdef X_HAVE_UTF8_STRING
                        {
                            const char *p = buf;
                            buf[len] = '\0';
                            key = utf8_decode(&p);
                        }
#else
                        key = buf[0];
#endif
                    got_key:
                        ev->key_event.type = QE_KEY_EVENT;
                        ev->key_event.key = key;
                        qe_handle_event(ev);
                    }
                    break;
                }
            }
        }
    }
}

/* bitmap handling */

enum X11BitmapType {
    BMP_PIXMAP,
    BMP_XIMAGE,
    BMP_XSHMIMAGE,
    BMP_XVIMAGE,
    BMP_XVSHMIMAGE,
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
    XShmSegmentInfo *shm_info;
    int x_lock, y_lock; /* destination for locking */
    XImage *ximage_lock;
} X11Bitmap;

static int x11_bmp_alloc(QEditScreen *s, QEBitmap *b)
{
    X11Bitmap *xb;

    xb = malloc(sizeof(X11Bitmap));
    if (!xb)
        return -1;
    b->priv_data = xb;
    /* choose bitmap type to optimize communication with x server and
       performances */
    if (b->flags & QEBITMAP_FLAG_VIDEO) {
#if defined(CONFIG_XV)
        if (xv_port != 0 && xv_open_count == 0) {
            if (shm_use)
                xb->type = BMP_XVSHMIMAGE;
            else
                xb->type = BMP_XVIMAGE;
            b->format = s->video_format;
            xv_open_count++;
        } else
#endif
        {
            if (shm_use) 
                xb->type = BMP_XSHMIMAGE;
            else
                xb->type = BMP_XIMAGE;
            b->format = s->bitmap_format;
        }
    } else {
        xb->type = BMP_PIXMAP;
        b->format = s->bitmap_format;
    }

    switch(xb->type) {
    default:
    case BMP_PIXMAP:
        xb->u.pixmap = XCreatePixmap(display, window, 
                                     b->width, b->height, attr.depth);
        if (!xb->u.pixmap)
            goto fail;
        break;
    case BMP_XIMAGE:
        {
            XImage *ximage;
            ximage = XCreateImage(display, None, attr.depth, ZPixmap, 0, 
                                  NULL, b->width, b->height, 8, 0);
            ximage->data = malloc(b->height * ximage->bytes_per_line);
            xb->u.ximage = ximage;
        }
        break;
    case BMP_XSHMIMAGE:
        {
            XImage *ximage;
            XShmSegmentInfo *shm_info;
            
            /* XXX: error testing */
            shm_info = malloc(sizeof(XShmSegmentInfo));
            ximage=XShmCreateImage(display, None, attr.depth, ZPixmap, NULL,
                                   shm_info, b->width, b->height);
            shm_info->shmid=shmget(IPC_PRIVATE,
                                   b->height * ximage->bytes_per_line,
                                   IPC_CREAT | 0777);
            ximage->data = shmat(shm_info->shmid, 0, 0);
            shm_info->shmaddr = ximage->data;
            shm_info->readOnly = False;
            
            XShmAttach(display, shm_info);
            XSync(display, False);
            
            /* the shared memory will be automatically deleted */
            shmctl(shm_info->shmid, IPC_RMID, 0);
            xb->shm_info = shm_info;
            xb->u.ximage = ximage;
        }
        break;
#ifdef CONFIG_XV
    case BMP_XVIMAGE:
        {
            XvImage *xvimage;
            xvimage = XvCreateImage(display, xv_port, xv_format, 0, 
                                    b->width, b->height);
            xvimage->data = malloc(xvimage->data_size);
            xb->u.xvimage = xvimage;
        }
        break;
    case BMP_XVSHMIMAGE:
        {
            XvImage *xvimage;
            XShmSegmentInfo *shm_info;

            shm_info = malloc(sizeof(XShmSegmentInfo));
            xvimage = XvShmCreateImage(display, xv_port, xv_format, 0, 
                                       b->width, b->height, shm_info);
            shm_info->shmid = shmget(IPC_PRIVATE,
                                     xvimage->data_size,
                                     IPC_CREAT | 0777);
            xvimage->data = shmat(shm_info->shmid, 0, 0);
            shm_info->shmaddr = xvimage->data;
            shm_info->readOnly = False;
            
            XShmAttach(display, shm_info);
            XSync(display, False);
            
            /* the shared memory will be automatically deleted */
            shmctl(shm_info->shmid, IPC_RMID, 0);
            xb->shm_info = shm_info;
            xb->u.xvimage = xvimage;
        }
        break;
#endif
    }
    return 0;
 fail:
    free(xb);
    return -1;
}

static void x11_bmp_free(QEditScreen *s, QEBitmap *b)
{
    X11Bitmap *xb = b->priv_data;

    switch(xb->type) {
    case BMP_PIXMAP:
        XFreePixmap(display, xb->u.pixmap);
        break;
    case BMP_XIMAGE:
        /* NOTE: also frees the ximage data */
        XDestroyImage(xb->u.ximage);
        break;
    case BMP_XSHMIMAGE:
        XShmDetach(display, xb->shm_info);
        XDestroyImage(xb->u.ximage);
        shmdt(xb->shm_info->shmaddr);
        free(xb->shm_info);
        break;
#ifdef CONFIG_XV
    case BMP_XVIMAGE:
        free(xb->u.xvimage->data);
        XFree(xb->u.xvimage);
        xv_open_count--;
        break;
    case BMP_XVSHMIMAGE:
        XShmDetach(display, xb->shm_info);
        XFree(xb->u.xvimage);
        shmdt(xb->shm_info->shmaddr);
        free(xb->shm_info);
        xv_open_count--;
        break;
#endif
    }
    free(xb);
}

static void x11_bmp_draw(QEditScreen *s, QEBitmap *b, 
                         int dst_x, int dst_y, int dst_w, int dst_h, 
                         int offset_x, int offset_y, int flags)
{
    X11Bitmap *xb = b->priv_data;

    /* XXX: handle clipping ? */
    update_rect(dst_x, dst_y, dst_x + dst_w, dst_y + dst_h);

    switch(xb->type) {
    case BMP_PIXMAP:
        XCopyArea(display, xb->u.pixmap, dbuffer, gc, 
                  0, 0, b->width, b->height, dst_x, dst_y);
        break;
    case BMP_XIMAGE:
        XPutImage(display, dbuffer, gc, 
                  xb->u.ximage, 0, 0, dst_x, dst_y, 
                  b->width, b->height);
        break;

    case BMP_XSHMIMAGE:
        XShmPutImage(display, dbuffer, gc, 
                     xb->u.ximage, 0, 0, dst_x, dst_y, 
                     b->width, b->height, False);
        break;
#ifdef CONFIG_XV
    case BMP_XVIMAGE:
        XvPutImage(display, xv_port, window, gc, xb->u.xvimage,
                   0, 0, b->width, b->height,
                   dst_x, dst_y, dst_w, dst_h);
        break;
    case BMP_XVSHMIMAGE:
        XvShmPutImage(display, xv_port, window, gc, xb->u.xvimage,
                      0, 0, b->width, b->height,
                      dst_x, dst_y, dst_w, dst_h, False);
        break;
#endif
    }
}

static void x11_bmp_lock(QEditScreen *s, QEBitmap *b, QEPicture *pict,
                         int x1, int y1, int w1, int h1)
{
    X11Bitmap *xb = b->priv_data;
    int bpp;

    pict->width = w1;
    pict->height = h1;
    pict->format = b->format;
    switch(xb->type) {
    case BMP_PIXMAP:
        {
            XImage *ximage;
            ximage = XCreateImage(display, None, attr.depth, ZPixmap, 0, 
                                  NULL, w1, h1, 8, 0);
            ximage->data = malloc(h1 * ximage->bytes_per_line);
            pict->data[0] = ximage->data;
            pict->linesize[0] = ximage->bytes_per_line;
            xb->ximage_lock = ximage;
            xb->x_lock = x1;
            xb->y_lock = y1;
        }
        break;
    case BMP_XIMAGE:
    case BMP_XSHMIMAGE:
        bpp = (xb->u.ximage->bits_per_pixel + 7) >> 3;
        pict->data[0] = xb->u.ximage->data + 
            y1 * xb->u.ximage->bytes_per_line + x1 * bpp;
        pict->linesize[0] = xb->u.ximage->bytes_per_line;
        break;
#ifdef CONFIG_XV
    case BMP_XVIMAGE:
    case BMP_XVSHMIMAGE:
        /* XXX: only YUV420P is handled yet */
        {
            XvImage *xvimage = xb->u.xvimage;
            int i, xx, yy, j;
            for(i=0;i<3;i++) {
                xx = x1;
                yy = y1;
                j = i;
                if (i >= 1) {
                    xx >>= 1;
                    yy >>= 1;
                    j = 3 - i; /* don't know why they inverted Cb and Cr! */
                }
                pict->data[j] = xvimage->data + xvimage->offsets[i] + 
                    yy * xvimage->pitches[i] + xx;
                pict->linesize[j] = xvimage->pitches[i];
            }
        }
        break;
#endif
    }
}

static void x11_bmp_unlock(QEditScreen *s, QEBitmap *b)
{
    X11Bitmap *xb = b->priv_data;
    int ret;
    switch(xb->type) {
    case BMP_PIXMAP:
        ret = XPutImage(display, xb->u.pixmap, gc_pixmap, xb->ximage_lock, 
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

static QEDisplay x11_dpy = {
    "x11",
    term_probe,
    term_init,
    term_close,
    NULL,
    term_flush,
    x11_is_user_input_pending,
    term_fill_rectangle,
    term_open_font,
    term_close_font,
    term_text_metrics,
    term_draw_text,
    term_set_clip,
    term_selection_activate,
    term_selection_request,
    x11_bmp_alloc,
    x11_bmp_free,
    x11_bmp_draw,
    x11_bmp_lock,
    x11_bmp_unlock,
    x11_full_screen,
};

static CmdOptionDef cmd_options[] = {
    { "nw", NULL, CMD_OPT_BOOL, "force tty terminal usage", 
      {int_ptr: &force_tty} },
    { "display", "display", CMD_OPT_STRING | CMD_OPT_ARG, "set X11 display", 
      {string_ptr: &display_str} },
    { "geometry", "WxH", CMD_OPT_STRING | CMD_OPT_ARG, "set X11 display size", 
      {string_ptr: &geometry_str} },
    { "fs", "ptsize", CMD_OPT_INT | CMD_OPT_ARG, "set default font size", 
      {int_ptr: &font_ptsize} },
    { NULL },
};

int x11_init(void)
{
    qe_register_cmd_line_options(cmd_options);
    return qe_register_display(&x11_dpy);
}

qe_module_init(x11_init);
