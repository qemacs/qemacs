/*
 * Color / CSS Utilities for qemacs.
 *
 * Copyright (c) 2000-2001 Fabrice Bellard.
 * Copyright (c) 2000-2024 Charlie Gordon.
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

#ifndef COLOR_H
#define COLOR_H

#include <stdint.h>

typedef int (CSSAbortFunc)(void *);

/* media definitions */
#define CSS_MEDIA_TTY     0x0001
#define CSS_MEDIA_SCREEN  0x0002
#define CSS_MEDIA_PRINT   0x0004
#define CSS_MEDIA_TV      0x0008
#define CSS_MEDIA_SPEECH  0x0010

#define CSS_MEDIA_ALL     0xffff

typedef uint32_t QEColor;
#define QEARGB(a,r,g,b)    (((unsigned int)(a) << 24) | ((r) << 16) | ((g) << 8) | (b))
#define QERGB(r,g,b)       QEARGB(0xff, r, g, b)
#define QERGB25(r,g,b)     QEARGB(1, r, g, b)
#define COLOR_TRANSPARENT  0
#define QERGB_ALPHA(c)     (((c) >> 24) & 255)
#define QERGB_RED(c)       (((c) >> 16) & 255)
#define QERGB_GREEN(c)     (((c) >>  8) & 255)
#define QERGB_BLUE(c)      (((c) >>  0) & 255)

/* A qemacs style is a named set of attributes including:
 * - colors for foreground and background
 * - font style bits
 * - font style size
 *
 * Styles are applied to text in successive phases:
 * - the current syntax mode computes the composite style for each
 *   character displayed on the line. These styles are stored into the
 *   array `sbuf` passed to the `ColorizeFunc` of the mode structure.
 * - the styles from the optional style buffer are combined into these
 *   style values.
 * - search matches and selection styles are applied if relevant.
 * - the bidirectional algorithm is applied to compute the display order
 * - sequences of codepoints with the same composite style are formed into
 *   display units, ligatures are applied.
 * - the composite style is expanded and converted into display attributes
 *   to trace the display unit on the device surface
 */

/* Style numbers are limited to 8 bits, the default set has 27 entries */
/* Composite styles are 32-bit values that specify
 * - a style number
 * - display attributes for underline, bold, blink
 * - text and background colors as either palette numbers or 4096 rgb values
 */

#if 0   /* 25-bit color for FG and BG */
/* 64-bit style layout:
 * x: unused, b: background color, f: foreground color,
 * K: blink, I: italics, B: bold, U: underline, C: composite,
 * S: selection, N: no-selection
 * xxxxxxxb bbbbbbbb bbbbbbbb bbbbbbbb NKIBUCSf ffffffff ffffffff ffffffff
 */
#define QE_TERM_STYLE_BITS  64
typedef uint64_t QETermStyle;
#define QE_STYLE_NUM        0x00FF
#define QE_STYLE_SEL        0x02000000  /* special selection style (cumulative with another style) */
#define QE_TERM_COMPOSITE   0x04000000  /* style is composite, not just a style number */
/* XXX: reversed as attribute? */
/* XXX: faint? */
#define QE_TERM_UNDERLINE   0x08000000
#define QE_TERM_BOLD        0x10000000
#define QE_TERM_ITALIC      0x20000000
#define QE_TERM_BLINK       0x40000000
#define QE_TERM_BG_BITS     25
#define QE_TERM_BG_SHIFT    32
#define QE_TERM_FG_BITS     25
#define QE_TERM_FG_SHIFT    0

#elif 1   /* 8K colors for FG and BG */
/* 32-bit style layout:
 * x: unused, b: background color, f: foreground color,
 * K: blink, I: italics, B: bold, U: underline, C: composite, S: selection
 * bbbbbbbb bbbbbKIB UCSfffff ffffffff
 */
#define QE_TERM_STYLE_BITS  32
typedef uint32_t QETermStyle;
#define QE_STYLE_NUM        0x00FF
#define QE_STYLE_SEL        0x02000  /* special selection style (cumulative with another style) */
#define QE_TERM_COMPOSITE   0x04000  /* style is composite, not just a style number */
#define QE_TERM_UNDERLINE   0x08000
#define QE_TERM_BOLD        0x10000
#define QE_TERM_ITALIC      0x20000
#define QE_TERM_BLINK       0x40000
#define QE_TERM_BG_BITS     13
#define QE_TERM_BG_SHIFT    19
#define QE_TERM_FG_BITS     13
#define QE_TERM_FG_SHIFT    0

#elif 1   /* 256 colors for FG and BG */
/* 32-bit style layout for 256-color terminals:
 * x: unused, b: background color, f: foreground color,
 * K: blink, I: italics, B: bold, U: underline, C: composite, S: selection
 * xxxxxxxx bbbbbbbb xxKIBUCS ffffffff
 */
#define QE_TERM_STYLE_BITS  32
typedef uint32_t QETermStyle;
#define QE_STYLE_NUM        0x00FF
#define QE_STYLE_SEL        0x0100  /* special selection style (cumulative with another style) */
#define QE_TERM_COMPOSITE   0x0200  /* style is composite, not just a style number */
#define QE_TERM_UNDERLINE   0x0400
#define QE_TERM_BOLD        0x0800
#define QE_TERM_ITALIC      0x1000
#define QE_TERM_BLINK       0x2000
#define QE_TERM_BG_BITS     8
#define QE_TERM_BG_SHIFT    16
#define QE_TERM_FG_BITS     8
#define QE_TERM_FG_SHIFT    0

#else   /* 16 colors for FG and 16 color BG */
/* 16-bit style layout:
 * x: unused, b: background color, f: foreground color,
 * K: blink, I: italics, B: bold, U: underline, C: composite, S: selection
 * xxKIBUCS bbbbffff
 */
#define QE_TERM_STYLE_BITS  16
typedef uint16_t QETermStyle;
#define QE_STYLE_NUM        0x00FF
#define QE_STYLE_SEL        0x0100  /* special selection style (cumulative with another style) */
#define QE_TERM_COMPOSITE   0x0200  /* style is composite, not just a style number */
#define QE_TERM_UNDERLINE   0x0400
#define QE_TERM_BOLD        0x0800
#define QE_TERM_ITALIC      0x1000
#define QE_TERM_BLINK       0x2000
#define QE_TERM_BG_BITS     4
#define QE_TERM_BG_SHIFT    0
#define QE_TERM_FG_BITS     4
#define QE_TERM_FG_SHIFT    4

#endif

#define QE_TERM_DEF_FG      7
#define QE_TERM_DEF_BG      0
#define QE_TERM_BG_COLORS   (1 << QE_TERM_BG_BITS)
#define QE_TERM_FG_COLORS   (1 << QE_TERM_FG_BITS)
#define QE_TERM_BG_MASK     ((QETermStyle)(QE_TERM_BG_COLORS - 1) << QE_TERM_BG_SHIFT)
#define QE_TERM_FG_MASK     ((QETermStyle)(QE_TERM_FG_COLORS - 1) << QE_TERM_FG_SHIFT)
#define QE_TERM_MAKE_COLOR(fg, bg)  (((QETermStyle)(fg) << QE_TERM_FG_SHIFT) | ((QETermStyle)(bg) << QE_TERM_BG_SHIFT))
#define QE_TERM_SET_FG(col, fg)  ((col) = ((col) & ~QE_TERM_FG_MASK) | ((QETermStyle)(fg) << QE_TERM_FG_SHIFT))
#define QE_TERM_SET_BG(col, bg)  ((col) = ((col) & ~QE_TERM_BG_MASK) | ((QETermStyle)(bg) << QE_TERM_BG_SHIFT))
#define QE_TERM_GET_FG(color)  (int)(((color) & QE_TERM_FG_MASK) >> QE_TERM_FG_SHIFT)
#define QE_TERM_GET_BG(color)  (int)(((color) & QE_TERM_BG_MASK) >> QE_TERM_BG_SHIFT)

typedef struct ColorDef {
    const char *name;   /* color name */
    QEColor color;      /* 32-bit ARGB color */
} ColorDef;

extern QEColor const xterm_colors[];
extern ColorDef *qe_colors;
extern int nb_qe_colors;

/* XXX: should have a more generic API with precomputed mapping scales */
/* Convert RGB triplet to a composite color */
unsigned int qe_map_color(QEColor color, QEColor const *colors, int count, int *dist);
/* Convert a composite color to an RGBA triplet with alpha channel */
QEColor qe_unmap_color(int color, int count);

int css_define_color(const char *name, const char *value);
int css_get_color(QEColor *color_ptr, const char *p);
const char *css_get_color_name(char *dest, size_t size, QEColor color, int lookup);
void css_free_colors(void);
int css_get_font_family(const char *str);
int css_get_enum(const char *str, const char *enum_str);
int color_dist(QEColor c1, QEColor c2);
static inline int color_y(QEColor c) {
    return 30 * ((c >> 16) & 0xff) + 59 * ((c >> 8) & 0xff) + 11 * (c & 0xff);
}

typedef struct CSSRect {
    int x1, y1, x2, y2;
} CSSRect;

void css_union_rect(CSSRect *a, const CSSRect *b);
static inline int css_is_null_rect(const CSSRect *a) {
    return (a->x2 <= a->x1 || a->y2 <= a->y1);
}
static inline void css_set_rect(CSSRect *a, int x1, int y1, int x2, int y2) {
    a->x1 = x1;
    a->y1 = y1;
    a->x2 = x2;
    a->y2 = y2;
}
/* return true if a and b intersect */
static inline int css_is_inter_rect(const CSSRect *a, const CSSRect *b) {
    return (!(a->x2 <= b->x1 || a->x1 >= b->x2 ||
              a->y2 <= b->y1 || a->y1 >= b->y2));
}

int colors_init(void);

/*---- Font definitions ----*/

#define QE_FONT_STYLE_NORM         0x0001
#define QE_FONT_STYLE_BOLD         0x0002
#define QE_FONT_STYLE_ITALIC       0x0004
#define QE_FONT_STYLE_UNDERLINE    0x0008
#define QE_FONT_STYLE_LINE_THROUGH 0x0010
#define QE_FONT_STYLE_BLINK        0x0020
#define QE_FONT_STYLE_MASK         0x00ff

#define NB_FONT_FAMILIES           3
#define QE_FONT_FAMILY_SHIFT       8
#define QE_FONT_FAMILY_MASK        0xff00
#define QE_FONT_FAMILY_FIXED       0x0100
#define QE_FONT_FAMILY_SERIF       0x0200
#define QE_FONT_FAMILY_SANS        0x0300 /* sans serif */

/* fallback font handling */
#define QE_FONT_FAMILY_FALLBACK_SHIFT  16
#define QE_FONT_FAMILY_FALLBACK_MASK   0xff0000

typedef struct QEFont {
    int refcount;
    int ascent;
    int descent;
    void *priv_data;
    int system_font; /* TRUE if system font */
    /* cache data */
    int style;
    int size;
    int timestamp;
} QEFont;

typedef struct QECharMetrics {
    int font_ascent;    /* maximum font->ascent */
    int font_descent;   /* maximum font->descent */
    int width;          /* sum of glyph widths */
} QECharMetrics;

#endif  /* COLOR_H */
