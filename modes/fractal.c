/*
 * QEmacs, character based fractal rendering
 *
 * Copyright (c) 2017-2024 Charlie Gordon.
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

#include <math.h>

#include "qe.h"
#include "variables.h"

/*---------------- Fractint formula syntax ----------------*/

static const char fractint_keywords[] = {
    "if|else|elseif|endif|pixel"
};

static const char fractint_types[] = {
    ""
};

enum {
    FRACTINT_STYLE_DEFAULT    = 0,
    FRACTINT_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    FRACTINT_STYLE_COMMENT    = QE_STYLE_COMMENT,
    FRACTINT_STYLE_DEFINITION = QE_STYLE_TYPE,
    FRACTINT_STYLE_NUMBER     = QE_STYLE_NUMBER,
    FRACTINT_STYLE_COLORS     = QE_STYLE_STRING,
    FRACTINT_STYLE_KEYWORD    = QE_STYLE_KEYWORD,
    FRACTINT_STYLE_FUNCTION   = QE_STYLE_FUNCTION,
    FRACTINT_STYLE_STRING     = QE_STYLE_STRING,
    FRACTINT_STYLE_STRING_Q   = QE_STYLE_STRING_Q,
    FRACTINT_STYLE_TYPE       = QE_STYLE_TYPE,
};

/* fractint-mode colorization states */
enum {
    IN_FRACTINT_COMMENT    = 0x01,  /* inside multiline comment */
    IN_FRACTINT_BLOCK      = 0x02,  /* inside definition block */
    IN_FRACTINT_COLORS     = 0x04,  /* inside a color palette definition */
    IN_FRACTINT_STRING     = 0x10,  /* double-quoted string */
    IN_FRACTINT_STRING_Q   = 0x20,  /* single-quoted string */
};

static void fractint_colorize_line(QEColorizeContext *cp,
                                   char32_t *str, int n, ModeDef *syn)
{
    int i = 0, start, indent, state, state1, style, klen;
    char32_t c, delim;
    char kbuf[64];

    for (indent = 0; qe_isblank(str[indent]); indent++)
        continue;

    state = cp->colorize_state;

    start = i;
    c = 0;
    style = FRACTINT_STYLE_DEFAULT;

    if (i >= n)
        goto the_end;

    if (state) {
        /* if already in a state, go directly in the code parsing it */
        if (state & IN_FRACTINT_COMMENT)
            goto parse_comment;
        if (state & IN_FRACTINT_COLORS) {
            i = indent;
            goto parse_colors;
        }
        if (state & IN_FRACTINT_STRING)
            goto parse_string;
        if (state & IN_FRACTINT_STRING_Q)
            goto parse_string_q;
    }

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case ';':      /* line comment */
            style = FRACTINT_STYLE_COMMENT;
            i = n;
            break;
        case ':':      /* iteration definition */
            style = FRACTINT_STYLE_KEYWORD;
            break;
        case '\'':     /* character constant */
        parse_string_q:
            state1 = IN_FRACTINT_STRING_Q;
            style = FRACTINT_STYLE_STRING_Q;
            delim = '\'';
            goto string;

        case '\"':      /* string literal */
        parse_string:
            state1 = IN_FRACTINT_STRING;
            style = FRACTINT_STYLE_STRING;
            delim = '\"';
        string:
            while (i < n) {
                c = str[i++];
                if (c == '\\') {
                    if (i >= n) {
                        /* continuation line */
                        state |= state1;
                        break;
                    }
                    i++;
                } else
                if (c == delim) {
                    break;
                }
            }
            break;
        case '{':
            if (state & IN_FRACTINT_BLOCK) {
                /* a '{' inside a definition seems to start a comment */
                /* consider it part of the comment, otherwise skip it */
                goto parse_comment;
            }
            if (str[i] == '-' || str[i] == '=') {
                /* invalid block, parse as comment */
                start++;
                goto parse_comment;
            }
            state |= IN_FRACTINT_BLOCK;
            break;
        case '}':
            state &= ~IN_FRACTINT_COMMENT;
            state &= ~IN_FRACTINT_BLOCK;
            break;
        case ' ':
        case '\t':
        case '\r':
            continue;
        default:
            if (!(state & IN_FRACTINT_BLOCK)) {
                klen = 0;
                kbuf[klen++] = qe_tolower(c);
                while (i < n && str[i] != '{') {
                    if (str[i] != ' ' && klen < countof(kbuf) - 1)
                        kbuf[klen++] = qe_tolower(str[i]);
                    i++;
                }
                if (kbuf[klen - 1] == '=')
                    klen--;
                kbuf[klen] = '\0';
                if (i >= n) {
                    style = FRACTINT_STYLE_COMMENT;
                    break;
                }
                if (strequal(kbuf, "comment")) {
                    SET_COLOR(str, start, i, FRACTINT_STYLE_PREPROCESS);
                    start = i + 1;
                parse_comment:
                    state |= IN_FRACTINT_COMMENT;
                    for (; i < n; i++) {
                        if (str[i] == '}') {
                            break;
                        }
                    }
                    style = FRACTINT_STYLE_COMMENT;
                } else {
                    eb_add_property(cp->b, cp->offset + start,
                                    QE_PROP_TAG, qe_strdup(kbuf));
                    style = FRACTINT_STYLE_DEFINITION;
                }
                break;
            }
            if (c == '.' || qe_isdigit(c)) {
                int j;
                // Integers:
                // 0x[0-9a-fA-F]+      //
                // [0-9][0-9]*         //
                // Floats:
                // [0-9][0-9]*\.[0-9]*([eE][-\+]?[0-9]+)?
                // [0-9][0-9]*(\.[0-9]*)?[eE][-\+]?[0-9]+
                // number suffixes:
                if (c == '0' && str[i] == 'x' && qe_isxdigit(str[i + 1])) {
                    for (i += 3; qe_isxdigit(str[i]); i++)
                        continue;
                } else {
                    while (qe_isdigit(str[i]))
                        i++;
                    if (c != '.' && str[i] == '.' && qe_isdigit(str[i + 1])) {
                        for (i += 2; qe_isdigit(str[i]); i++)
                            continue;
                    }
                    if (str[i] == 'e' || str[i] == 'E') {
                        j = i + 1;
                        if (str[j] == '+' || str[j] == '-')
                            j++;
                        if (qe_isdigit(str[j])) {
                            for (i = j + 1; qe_isdigit(str[i]); i++)
                                continue;
                        }
                    }
                }
                if (str[i] == 'i' || str[i] == 'I') {
                    /* imaginary number */
                    i++;
                }
                if (!qe_isalpha_(str[i])) {
                    style = FRACTINT_STYLE_NUMBER;
                    break;
                }
                i = start + 1;
            }
            if (qe_isalpha_(c)) {
                /* identifiers match:
                 * "[a-zA-Z_\x80-\xff][a-zA-Z_0-9\x80-\xff]*"
                 */
                klen = 0;
                kbuf[klen++] = qe_tolower(c);
                while (qe_isalnum_(str[i]) || str[i] == '.') {
                    if (klen < countof(kbuf) - 1)
                        kbuf[klen++] = qe_tolower(str[i]);
                    i++;
                }
                kbuf[klen] = '\0';
                if (strfind(syn->keywords, kbuf)) {
                    style = FRACTINT_STYLE_KEYWORD;
                    break;
                }
                if (strfind(syn->types, kbuf)) {
                    style = FRACTINT_STYLE_TYPE;
                    break;
                }
                if (check_fcall(str, i)) {
                    /* function call */
                    style = FRACTINT_STYLE_FUNCTION;
                    break;
                }
                if (strequal(kbuf, "colors") && str[i] == '=') {
                    start = ++i;
                parse_colors:
                    state &= ~IN_FRACTINT_COLORS;
                    for (; i < n; i++) {
                        c = str[i];
                        if (!qe_isalnum_(c) && c != '`' && c != '<' && c != '>')
                            break;
                    }
                    if (i == n - 1 && str[i] == '\\') {
                        state |= IN_FRACTINT_COLORS;
                        i++;
                    }
                    style = FRACTINT_STYLE_COLORS;
                    break;
                }
                break;
            }
            continue;
        }
        if (style) {
            SET_COLOR(str, start, i, style);
            style = 0;
        }
    }
 the_end:
    /* set style on eol char */
    SET_COLOR1(str, n, style);

    cp->colorize_state = state;
}

static int fractint_mode_probe(ModeDef *mode, ModeProbeData *pd)
{
    if (match_extension(pd->filename, mode->extensions)) {
        /* This is a quick and dirty hack: assume Fractint formula
         * files are located somewhere below a directory with a
         * name relating to fractals.
         */
        if (strstr(pd->real_filename, "frac")) {
            /* Favor Fractint mode for formula files */
            return 82;
        } else {
            /* Favor Visual Basic Form mode */
            return 78;
        }
    }
    return 1;
}

static ModeDef fractint_mode = {
    .name = "Fractint",
    .extensions = "frm|par|ifs|l",
    .mode_probe = fractint_mode_probe,
    .colorize_func = fractint_colorize_line,
    .keywords = fractint_keywords,
    .types = fractint_types,
    .fallback = &c_mode,
};

/*---------------- Interactive fractal explorer ----------------*/

#define USE_BITMAP_API    0   /* Using device bitmap API */
#define USE_DRAW_PICTURE  1   /* Using qe_draw_picture() */

static ModeDef fractal_mode;

#if 1
typedef long double fnum_t;
#define MFT  "%.21Lg"
#else
typedef double fnum_t;
#define MFT  "%.16g"
#endif

typedef struct { fnum_t a, b; } cnum_t;

typedef struct FractalState FractalState;

struct FractalState {
    QEModeData base;

    int width, height;  /* fractal size in pixels */
    int type;           /* fractal type 0..8 */
    int maxiter;        /* maximum iteration number */
    int cb, nc;         /* color palette base and length */
    int rot;            /* rotation in degrees */
    int zoom;           /* zoom level in dB */
    fnum_t scale;       /* zoom factor = pow(10, -mzoom/10) */
    fnum_t bailout;     /* maximum squared module (default 4.0) */
    fnum_t x, y;        /* center position */
    fnum_t m0, m1, m2, m3; /* rotation matrix */
    int shift;          /* color animation base */
    QEColor colors[256]; /* color palette */
    QEditScreen *screen;    /* for bmp_free() */
    QEBitmap *disp_bmp;     /* device image */
#if USE_DRAW_PICTURE
    QEPicture *ip;
#endif
};

static const char fractal_default_parameters[] = {
    " type=0"
    " maxiter=215"
    " rot=0"
    " zoom=0"
    " bailout=4"
    // This place zooms to level 180, scale=1e-18:
    " x=-0.747698434201463097446  y=0.0794508470293983774563"
    // This place on the X axis is interesting:
    //" x=-1.78935604483808219844, y=0"
};

static inline FractalState *fractal_get_state(EditState *e, int status)
{
    return qe_get_buffer_mode_data(e->b, &fractal_mode, status ? e : NULL);
}

static fnum_t cmod2(cnum_t z) {
    return z.a * z.a + z.b * z.b;
}

static cnum_t cpower(cnum_t z, int exp) {
    cnum_t r = { 1, 0 };
    fnum_t a;

    while (exp > 0) {
        if (exp & 1) {
            a = r.a;
            r.a = a * z.a - r.b * z.b;
            r.b = a * z.b + r.b * z.a;
        }
        exp >>= 1;
        a = z.a;
        z.a = a * a - z.b * z.b;
        z.b = 2 * a * z.b;
    }
    return r;
}

static int mandelbrot_func(fnum_t x, fnum_t y, fnum_t bailout, int maxiter) {
    fnum_t a, b, c;
    int i;
    for (a = b = 0, i = maxiter; a * a + b * b <= bailout && --i > 0;) {
        c = a;
        a = a * a - b * b + x;
        b = 2 * c * b + y;
    }
    return maxiter - i;
}

static int mandelbrot3_func(fnum_t x, fnum_t y, fnum_t bailout, int maxiter) {
    fnum_t a, b, c;
    int i;
    for (a = b = 0, i = maxiter; a * a + b * b <= bailout && --i > 0;) {
        c = a;
        a = a * a * a - 3 * a * b * b + x;
        b = 3 * c * c * b - b * b * b + y;
    }
    return maxiter - i;
}

static int mandelbrot4_func(fnum_t x, fnum_t y, fnum_t bailout, int maxiter) {
    fnum_t a, b, a2, b2;
    int i;
    for (a = b = 0, i = maxiter; a * a + b * b <= bailout && --i > 0;) {
        a2 = a * a - b * b;
        b2 = 2 * a * b;
        a = a2 * a2 - b2 * b2 + x;
        b = 2 * a2 * b2 + y;
    }
    return maxiter - i;
}

static int mandelbrot5_func(fnum_t x, fnum_t y, fnum_t bailout, int maxiter) {
    fnum_t a, b, a2, b2, a3, b3;
    int i;
    for (a = b = 0, i = maxiter; a * a + b * b <= bailout && --i > 0;) {
        a3 = a * a * a - 3 * a * b * b;
        b3 = 3 * a * a * b - b * b * b;
        a2 = a * a - b * b;
        b2 = 2 * a * b;
        a = a2 * a3 - b2 * b3 + x;
        b = b2 * a3 + a2 * b3 + y;
    }
    return maxiter - i;
}

static int mandelbrot6_func(fnum_t x, fnum_t y, fnum_t bailout, int maxiter) {
    fnum_t a, b, a3, b3;
    int i;
    for (a = b = 0, i = maxiter; a * a + b * b <= bailout && --i > 0;) {
        a3 = a * a * a - 3 * a * b * b;
        b3 = 3 * a * a * b - b * b * b;
        a = a3 * a3 - b3 * b3 + x;
        b = 2 * a3 * b3 + y;
    }
    return maxiter - i;
}

static int mandelbrot7_func(fnum_t x, fnum_t y, fnum_t bailout, int maxiter) {
    cnum_t z = { 0, 0 };
    int i;
    for (i = maxiter; cmod2(z) <= bailout && --i > 0;) {
        z = cpower(z, 7);
        z.a += x;
        z.b += y;
    }
    return maxiter - i;
}

static int mandelbrot8_func(fnum_t x, fnum_t y, fnum_t bailout, int maxiter) {
    cnum_t z = { 0, 0 };
    int i;
    for (i = maxiter; cmod2(z) <= bailout && --i > 0;) {
        z = cpower(z, 8);
        z.a += x;
        z.b += y;
    }
    return maxiter - i;
}

static int mandelbrot9_func(fnum_t x, fnum_t y, fnum_t bailout, int maxiter) {
    cnum_t z = { 0, 0 };
    int i;
    for (i = maxiter; cmod2(z) <= bailout && --i > 0;) {
        z = cpower(z, 9);
        z.a += x;
        z.b += y;
    }
    return maxiter - i;
}

static int mandelbrot10_func(fnum_t x, fnum_t y, fnum_t bailout, int maxiter) {
    cnum_t z = { 0, 0 };
    int i;
    for (i = maxiter; cmod2(z) <= bailout && --i > 0;) {
        z = cpower(z, 10);
        z.a += x;
        z.b += y;
    }
    return maxiter - i;
}

static cnum_t newton_next(cnum_t z) {
    fnum_t x2 = z.a * z.a;
    fnum_t y2 = z.b * z.b;
    fnum_t temp_deno = 3 * (x2 + y2) * (x2 + y2);
    return (cnum_t){
        z.a * 2 / 3 - (y2 - x2) / temp_deno,
        z.b * 2 / 3 - (2 * z.a * z.b) / temp_deno
    };
}

static int newton_func(fnum_t x, fnum_t y, fnum_t bailout, int maxiter) {
    cnum_t z = { x, y };
#define COS_PI_6  0.866025403784439  // sqrt(3) / 2
    static cnum_t const roots[3] = {{ 1, 0 }, { -0.5, COS_PI_6 }, { -0.5, -COS_PI_6 }};
    fnum_t min_dist = 1e-11;
    int i;
    for (i = 0; i < maxiter; i++) {
        z = newton_next(z);
        if (fabsl(z.a - roots[0].a) < min_dist
        ||  fabsl(z.b - roots[0].b) < min_dist
        ||  fabsl(z.a - roots[1].a) < min_dist
        ||  fabsl(z.b - roots[1].b) < min_dist
        ||  fabsl(z.a - roots[2].a) < min_dist
        ||  fabsl(z.b - roots[2].b) < min_dist)
            break;
    }
    return i;
}

static struct FractalType {
    const char *name;
    const char *formula;
    int (*func)(fnum_t x, fnum_t y, fnum_t bailout, int maxiter);
} const fractal_type[] = {
    { "Mandelbrot", "z=z^2+c", mandelbrot_func },
    { "Mandelbrot3", "z=z^3+c", mandelbrot3_func },
    { "Mandelbrot4", "z=z^4+c", mandelbrot4_func },
    { "Mandelbrot5", "z=z^5+c", mandelbrot5_func },
    { "Mandelbrot6", "z=z^6+c", mandelbrot6_func },
    { "Mandelbrot7", "z=z^7+c", mandelbrot7_func },
    { "Mandelbrot8", "z=z^8+c", mandelbrot8_func },
    { "Mandelbrot9", "z=x^9+c", mandelbrot9_func },
    { "Mandelbrot10", "z=x^10+c", mandelbrot10_func },
    { "Newton", "z=(z^3-1)/(3*z^2)", newton_func },
};

static void fractal_invalidate(FractalState *ms) {
    /* This will force fractal image recomputation */
    /* XXX: color changes should not cause recomputation
       if the fractal is computed as a paletted image */
    ms->width = ms->height = 0;
}

static void fractal_set_rotation(FractalState *ms, int rot) {
    ms->rot = rot;
    /* compute rotation matrix */
    ms->m0 = (fnum_t)cos(-ms->rot * M_PI / 180.0);
    ms->m1 = (fnum_t)sin(-ms->rot * M_PI / 180.0);
    ms->m2 = -ms->m1;
    ms->m3 = ms->m0;
    fractal_invalidate(ms);
}

static void fractal_set_zoom(FractalState *ms, int level) {
    ms->zoom = level;
    ms->scale = (fnum_t)pow(10.0, -ms->zoom / 10.0);
    fractal_invalidate(ms);
}

static int fractal_get_color(const char *p, int *dac) {
    /* convert a fractint 3 character color spec: 0-9A-Z_-z -> 0..63 */
    int i;
    for (i = 0; i < 3; i++) {
        int c = p[i];
        if (c >= '0' && c <= '9')
            c -= '0';
        else
        if (c >= 'A' && c <= 'Z')
            c -= 'A' - 10;
        else
        if (c >= '_' && c <= 'z')
            c -= '_' - 36;
        else
            return 0;
        dac[i] = (c << 2) | (c >> 4);
    }
    return 1;
}

static int fractal_set_colors(FractalState *ms, const char *p, const char **pp) {
    /* Set the default colors */
    blockcpy(ms->colors, xterm_colors, 256);
    ms->cb = 16;
    ms->nc = 216;
    fractal_invalidate(ms);

    if (p) {
        if (strmatchword(p, "gray256", pp)) {
            int c;
            for (c = 1; c < 256; c++) {
                ms->colors[256 - c] = QERGB(c, c, c);
            }
            ms->cb = 1;
            ms->nc = 255;
        } else
        if (strmatchword(p, "gray", pp)) {
            ms->cb = 232;
            ms->nc = 24;
        } else
        if (!strmatchword(p, "default", pp)) {
            int i, j, n;
            int last[3] = { 0, 0, 0 }, dac[3] = { 0, 0, 0 };

            /* parse a color fractint colors spec */
            for (i = 0; i < 256; i++) {
                if (!*p || *p == ',' || *p == ' ')
                    break;
                n = 0;
                if (*p == '<') {
                    if (i == 0)
                        return 0;
                    n = clamp_int(strtol_c(p + 1, &p, 10), 1, 255 - i);
                    if (*p != '>')
                        return 0;
                    p++;
                    memcpy(last, dac, sizeof dac);
                    i += n;
                }
                if (!fractal_get_color(p, dac)) {
                    return 0;
                }
                p += 3;
                for (j = 1, ++n; j < n; j++) {
                    ms->colors[i - j] = QERGB((last[0] * j + dac[0] * (n - j)) / n,
                                              (last[1] * j + dac[1] * (n - j)) / n,
                                              (last[2] * j + dac[2] * (n - j)) / n);
                }
                ms->colors[i] = QERGB(dac[0], dac[1], dac[2]);
            }
            if (pp)
                *pp = p;
            ms->cb = 1;
            ms->nc = i - ms->cb;
        }
    }
    return 1;
}

static void fractal_set_parameters(EditState *s, FractalState *ms, const char *parms)
{
    const char *p;

    ms->width = ms->height = 0;    /* force redraw */

    for (p = parms;;) {
        p += strspn(p, ";, \t\r\n");
        if (*p == '\0')
            break;
        if (strstart(p, "type=", &p)) {
            // XXX: should match type names
            ms->type = clamp_int(strtol_c(p, &p, 0), 0, countof(fractal_type) - 1);
        } else
        if (strstart(p, "maxiter=", &p)) {
            ms->maxiter = strtol_c(p, &p, 0);
        } else
        if (strstart(p, "colors=", &p)) {
            if (!fractal_set_colors(ms, p, &p)) {
                put_status(s, "invalid colors: %s", p);
                p += strcspn(p, ", ");
            }
        } else
        if (strstart(p, "cb=", &p)) {
            ms->cb = strtol_c(p, &p, 0);
        } else
        if (strstart(p, "nc=", &p)) {
            ms->nc = strtol_c(p, &p, 0);
        } else
        if (strstart(p, "shift=", &p)) {
            ms->shift = strtol_c(p, &p, 0);
        } else
        if (strstart(p, "rot=", &p)) {
            fractal_set_rotation(ms, strtol_c(p, &p, 0));
        } else
        if (strstart(p, "zoom=", &p)) {
            fractal_set_zoom(ms, strtol_c(p, &p, 0));
        } else
        if (strstart(p, "bailout=", &p)) {
            ms->bailout = strtold_c(p, &p);
        } else
        if (strstart(p, "x=", &p)) {
            ms->x = strtold_c(p, &p);
        } else
        if (strstart(p, "y=", &p)) {
            ms->y = strtold_c(p, &p);
        } else {
            put_status(s, "invalid parameter: %s", p);
            break;
        }
    }
}

static void do_fractal_draw(EditState *s, FractalState *ms)
{
#if USE_BITMAP_API
    int width = ms->width, height = ms->height;
    int maxiter = ms->maxiter + ms->zoom, nc = ms->nc;
    int i, nx, ny, shift;
    fnum_t x, y, dx, dy;
    int (*func)(fnum_t x, fnum_t y, fnum_t bailout, int maxiter) =
        fractal_type[ms->type].func;
    unsigned char *palette8 = NULL;
    uint32_t *palette32 = NULL;
    QEPicture pict;

    if (s->width == 0 || s->height == 0 || width == 0 || height == 0 || nc == 0)
        return;

    if (s->width == s->cols) {
        /* character based, assume 80x25 4/3 aspect ratio, 2 pixels per char */
        dx = 32 * ms->scale / width / 10;
        dy = dx * 12 / 10;
    } else {
        /* pixel based, assume 100% pixel aspect ratio */
        dy = dx = 32 * ms->scale / width / 10;
    }
    if (ms->disp_bmp == NULL
    ||  ms->disp_bmp->width != width
    ||  ms->disp_bmp->height != height) {
        bmp_free(ms->screen, &ms->disp_bmp);
        /* create the displayed bitmap and put the image in it */
        ms->screen = s->screen;
        ms->disp_bmp = bmp_alloc(ms->screen, width, height, 0);
    }
    if (!ms->disp_bmp)
        return;

    bmp_lock(ms->screen, ms->disp_bmp, &pict, 0, 0, width, height);

    /* Compute shifted palette */
    shift = nc + ms->shift % nc; /* 0 < shift < 2 * nc */
    if (pict.format == QEBITMAP_FORMAT_8BIT) {
        palette8 = qe_malloc_array(unsigned char, maxiter + 1);
        for (i = 0; i <= maxiter; i++) {
            palette8[i] = (i >= maxiter) ? 0 : (ms->cb + (i + shift) % nc) & 255;
        }
    } else
    if (pict.format == QEBITMAP_FORMAT_RGBA32) {
        palette32 = qe_malloc_array(uint32_t, maxiter + 1);
        for (i = 0; i <= maxiter; i++) {
            palette32[i] = ms->colors[(i >= maxiter) ? 0 : (ms->cb + (i + shift) % nc) & 255];
        }
    }

    /* Compute fractal bitmap */
    for (ny = 0, y = -dy * height / 2; ny < height; ny++, y += dy) {
        if (pict.format == QEBITMAP_FORMAT_8BIT) {
            unsigned char *pb = pict.data[0] + ny * pict.linesize[0];
            for (nx = 0, x = -dx * width / 2; nx < width; nx++, x += dx) {
                fnum_t xr = ms->x + x * ms->m0 + y * ms->m1;
                fnum_t yr = ms->y + x * ms->m2 + y * ms->m3;
                pb[nx] = palette8[(*func)(xr, yr, ms->bailout, maxiter)];
            }
        } else
        if (pict.format == QEBITMAP_FORMAT_RGBA32) {
            uint32_t *pb = (uint32_t *)(void*)(pict.data[0] + ny * pict.linesize[0]);
            for (nx = 0, x = -dx * width / 2; nx < width; nx++, x += dx) {
                fnum_t xr = ms->x + x * ms->m0 + y * ms->m1;
                fnum_t yr = ms->y + x * ms->m2 + y * ms->m3;
                pb[nx] = palette32[(*func)(xr, yr, ms->bailout, maxiter)];
            }
        }
    }
    bmp_unlock(ms->screen, ms->disp_bmp);
    qe_free(&palette8);
    qe_free(&palette32);
    edit_invalidate(s, 1);
#elif USE_DRAW_PICTURE
    int width = ms->width, height = ms->height, zoom = ms->zoom;
    int maxiter = ms->maxiter + zoom, cb = ms->cb, nc = ms->nc;
    int i, nx, ny;
    fnum_t xc = ms->x, yc = ms->y, scale = ms->scale;
    fnum_t bailout = ms->bailout;
    fnum_t x, y, dx, dy, xr, yr;
    int (*func)(fnum_t x, fnum_t y, fnum_t bailout, int maxiter) =
        fractal_type[ms->type].func;

    if (s->width == 0 || s->height == 0 || width == 0 || height == 0 || nc == 0)
        return;

    if (s->width == s->cols) {
        /* character based, assume 80x25 4/3 aspect ratio, 2 pixels per char */
        dx = 32 * scale / width / 10;
        dy = dx * 12 / 10;
    } else {
        /* pixel based, assume 100% pixel aspect ratio */
        dy = dx = 32 * scale / width / 10;
    }
    if (ms->ip == NULL || ms->ip->width != width || ms->ip->height != height) {
        qe_free_picture(&ms->ip);
        ms->ip = qe_create_picture(width, height, QEBITMAP_FORMAT_8BIT, 0);
    }
    if (!ms->ip)
        return;

    for (ny = 0, y = -dy * height / 2; ny < height; ny++, y += dy) {
        unsigned char *pb = ms->ip->data[0] + ny * ms->ip->linesize[0];
        for (nx = 0, x = -dx * width / 2; nx < width; nx++, x += dx) {
            xr = xc + x * ms->m0 + y * ms->m1;
            yr = yc + x * ms->m2 + y * ms->m3;
            i = (*func)(xr, yr, bailout, maxiter);
            pb[nx] = (i >= maxiter) ? 0 : cb + i % nc;
        }
    }
    edit_invalidate(s, 1);
#else
    int width = ms->width, height = ms->height / 2;
    int maxiter = ms->maxiter + ms->zoom, nc = ms->nc;
    int i, nx, ny, fg, bg, shift;
    fnum_t x, y, dx, dy;
    int (*func)(fnum_t x, fnum_t y, fnum_t bailout, int maxiter) =
        fractal_type[ms->type].func;
    unsigned char *palette8 = NULL;

    if (s->width == 0 || s->height == 0 || width == 0 || height == 0 || nc == 0)
        return;

    dx = 3.2 * ms->scale / width;
    if (s->width == s->cols) {
        /* character based, assume 80x25 4/3 aspect ratio, 2 pixels per char */
        dy = dx * 2.4;
    } else {
        /* pixel based, assume 100% pixel aspect ratio */
        dy = dx * width / s->width * s->height / height;
    }

    /* Compute shifted palette */
    shift = nc + ms->shift % nc; /* 0 < shift < 2 * nc */
    palette8 = qe_malloc_array(unsigned char, maxiter + 1);
    for (i = 0; i <= maxiter; i++) {
        palette8[i] = (i >= maxiter) ? 0 : (ms->cb + (i + shift) % nc) & 255;
    }

    s->b->flags &= ~BF_READONLY;

    eb_delete_range(s->b, 0, s->b->total_size);

    for (ny = 0, y = -dy * height / 2; ny < height; ny++, y += dy) {
        for (nx = 0, x = -dx * width / 2; nx < width; nx++, x += dx) {
            fnum_t xr = ms->x + x * ms->m0 + y * ms->m1;
            fnum_t yr = ms->y + x * ms->m2 + y * ms->m3;
            bg = palette8[(*func)(xr, yr, ms->bailout, maxiter)];
            xr += dy / 2 * ms->m1;
            yr += dy / 2 * ms->m3;
            fg = palette8[(*func)(xr, yr, ms->bailout, maxiter)];
            s->b->cur_style = QE_TERM_COMPOSITE | QE_TERM_MAKE_COLOR(fg, bg);
            eb_insert_char32(s->b, s->b->total_size, fg == bg ? ' ' : 0x2584);
        }
        s->b->cur_style = QE_STYLE_DEFAULT;
        eb_insert_char32(s->b, s->b->total_size, '\n');
    }
    s->b->flags |= BF_READONLY;
    qe_free(&palette8);
#endif
}

#if USE_BITMAP_API
static void fractal_display(EditState *s) {
    FractalState *ms = fractal_get_state(s, 0);
    QEColor col = qe_styles[QE_STYLE_GUTTER].bg_color;

    if (s->display_invalid) {
        if (ms && ms->disp_bmp) {
            int w = min_int(s->width, ms->disp_bmp->width);
            int h = min_int(s->height, ms->disp_bmp->height / s->screen->dpy.yfactor);
            int x = (s->width - w) / 2;
            int y = (s->height - h) / 2;

            bmp_draw(s->screen, ms->disp_bmp,
                     s->xleft + x, s->ytop + y, w, h, 0, 0, 0);
            fill_window_slack(s, x, y, w, h, col);
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
#endif

#if USE_DRAW_PICTURE
static void fractal_display(EditState *s) {
    FractalState *ms = fractal_get_state(s, 0);
    QEColor col = qe_styles[QE_STYLE_GUTTER].bg_color;

    if (s->display_invalid) {
        if (ms && ms->ip) {
            int w = min_int(s->width, ms->ip->width);
            int h = min_int(s->height, ms->ip->height / s->screen->dpy.yfactor);
            int x0 = (s->width - w) / 2;
            int y0 = (s->height - h) / 2;
            uint32_t palette[256];
            int c;

            palette[0] = ms->colors[0];
            for (c = 1; c < 256; c++) {
                palette[c] = ms->colors[(c + ms->shift) & 255];
            }
            ms->ip->palette = palette;
            ms->ip->palette_size = 256;
            qe_draw_picture(s->screen, s->xleft + x0, s->ytop + y0, w, h,
                            ms->ip, 0, 0, w, h * s->screen->dpy.yfactor,
                            0, QERGB(128, 128, 128));
            ms->ip->palette = NULL;
            fill_window_slack(s, x0, y0, w, h, col);
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
#endif

static void do_fractal_move(EditState *s, int deltax, int deltay) {
    FractalState *ms = fractal_get_state(s, 1);
    if (ms) {
        fnum_t dx = deltax * ms->scale / 40;
        fnum_t dy = deltay * ms->scale / 40;
        ms->x += dx * ms->m0 + dy * ms->m1;
        ms->y += dx * ms->m2 + dy * ms->m3;
        fractal_invalidate(ms);
    }
}

static void do_fractal_move_x(EditState *s, int n) {
    do_fractal_move(s, n, 0);
}

static void do_fractal_move_y(EditState *s, int n) {
    do_fractal_move(s, 0, n);
}

static void do_fractal_zoom(EditState *s, int n) {
    FractalState *ms = fractal_get_state(s, 1);
    if (ms) {
        fractal_set_zoom(ms, ms->zoom + n);
    }
}

static void do_fractal_rotate(EditState *s, int n) {
    FractalState *ms = fractal_get_state(s, 1);
    if (ms) {
        fractal_set_rotation(ms, n ? ms->rot + n : 0);
    }
}

static void do_fractal_shift_colors(EditState *s, int n) {
    FractalState *ms = fractal_get_state(s, 1);
    if (ms) {
        ms->shift += n;
#if USE_BITMAP_API
        fractal_invalidate(ms);
#else
        edit_invalidate(s, 1);
#endif
    }
}

static void do_fractal_set_colors(EditState *s, int type) {
    FractalState *ms = fractal_get_state(s, 1);
    if (ms) {
        ms->shift = 0;
        if (type == 0) {
            fractal_set_colors(ms, NULL, NULL);
        } else
        if (type == 1) {
            fractal_set_colors(ms, "gray", NULL);
        }
#if USE_BITMAP_API
        fractal_invalidate(ms);
#else
        edit_invalidate(s, 1);
#endif
    }
}

static void do_fractal_iter(EditState *s, int n) {
    FractalState *ms = fractal_get_state(s, 1);
    if (ms) {
        ms->maxiter += n;
        fractal_invalidate(ms);
    }
}

static void do_fractal_bailout(EditState *s, int n) {
    FractalState *ms = fractal_get_state(s, 1);
    if (ms) {
        ms->bailout += n;
        fractal_invalidate(ms);
    }
}

static void do_fractal_set_parameters(EditState *s, const char *params) {
    FractalState *ms = fractal_get_state(s, 1);
    if (ms) {
        fractal_set_parameters(s, ms, params);
    }
}

static void do_fractal_set_type(EditState *s, int key) {
    FractalState *ms = fractal_get_state(s, 1);
    if (ms) {
        fractal_set_parameters(s, ms, fractal_default_parameters);
        ms->type = key - '1';
        if (ms->type) {
            fractal_set_parameters(s, ms, "rot=0 zoom=0 x=0 y=0");
        }
    }
}

static void do_fractal_help(EditState *s)
{
    FractalState *ms = fractal_get_state(s, 1);
    EditBuffer *b;
    int w = 16;

    if (ms == NULL)
        return;

    b = new_help_buffer();
    if (!b)
        return;

    eb_printf(b, "Fractal description:\n\n");

    eb_printf(b, "%*s: %s\n", w, "type", fractal_type[ms->type].name);
    eb_printf(b, "%*s: %s\n", w, "formula", fractal_type[ms->type].formula);
    eb_printf(b, "%*s: "MFT"\n", w, "x", ms->x);
    eb_printf(b, "%*s: "MFT"\n", w, "y", ms->y);
    eb_printf(b, "%*s: %dx%d\n", w, "size", ms->width, ms->height);
    eb_printf(b, "%*s: %d\n", w, "zoom", ms->zoom);
    eb_printf(b, "%*s: %.6g\n", w, "scale", (double)ms->scale);
    eb_printf(b, "%*s: %d\n", w, "rot", ms->rot);
    eb_printf(b, "%*s: "MFT"\n", w, "bailout", ms->bailout);
    eb_printf(b, "%*s: %d\n", w, "maxiter", ms->maxiter);
    eb_printf(b, "%*s: cb=%d nc=%d shift=%d\n", w, "colors", ms->cb, ms->nc, ms->shift);

    eb_printf(b, "\nFractal navigator:\n\n");

    // XXX: should use print_bindings(b, "Fractal commands", 0, &fractal_mode);

    eb_printf(b, "%*s: %s\n", w, "left, right", "move center point horizontally");
    eb_printf(b, "%*s: %s\n", w, "up, down", "move center point vertically");
    eb_printf(b, "%*s: %s\n", w, "+, SP", "zoom in");
    eb_printf(b, "%*s: %s\n", w, "-, _", "zoom out");
    eb_printf(b, "%*s: %s\n", w, "/", "rotate right");
    eb_printf(b, "%*s: %s\n", w, "\\, .", "rotate left");
    eb_printf(b, "%*s: %s\n", w, "|", "reset rotation");
    eb_printf(b, "%*s: %s\n", w, "{, }", "change maxiter");
    eb_printf(b, "%*s: %s\n", w, "[, ]", "shift colors");
    eb_printf(b, "%*s: %s\n", w, "<, >", "change bailout");
    eb_printf(b, "%*s: %s\n", w, "=", "set fractal parameters");
    eb_printf(b, "%*s: %s\n", w, "g", "set gray colors");
    eb_printf(b, "%*s: %s\n", w, "c", "set default colors");

    show_popup(s, b, "About Fractal");
}

static void fractal_display_hook(EditState *s) {
    FractalState *ms = fractal_get_state(s, 0);
    if (ms) {
#if USE_BITMAP_API || USE_DRAW_PICTURE
        int width = s->width;
        int height = s->height * s->screen->dpy.yfactor;
#else
        int width = s->cols;
        int height = s->rows * 2;
#endif
        if (s->xleft == 0 && s->ytop == 0
        &&  (ms->height != height || ms->width != width)) {
            /* XXX: should use a separate thread for this */
            /* XXX: should use a different bitmap for each window */
            ms->width = width;
            ms->height = height;
            do_fractal_draw(s, ms);
        }
    }
}

static const CmdDef fractal_commands[] = {
    CMD2( "fractal-left", "left",
          "Move fractal origin left",
          do_fractal_move_x, ESi, "q")
    CMD2( "fractal-right", "right",
          "Move fractal origin right",
          do_fractal_move_x, ESi, "p")
    CMD2( "fractal-up", "up",
          "Move fractal origin up",
          do_fractal_move_y, ESi, "q")
    CMD2( "fractal-down", "down",
          "Move fractal origin down",
          do_fractal_move_y, ESi, "p")
    CMD2( "fractal-zoom-in", "+, SPC",
          "Increase fractal zoom level",
          do_fractal_zoom, ESi, "p")
    CMD2( "fractal-zoom-out", "-, _",
          "Decrease fractal zoom level",
          do_fractal_zoom, ESi, "q")
    CMD2( "fractal-rotate-left", "\\, .",
          "Rotate fractal figure counterclockwise",
          do_fractal_rotate, ESi, "p")
    CMD2( "fractal-rotate-right", "/",
          "Rotate fractal figure clockwise",
          do_fractal_rotate, ESi, "q")
    CMD3( "fractal-rotate-none", "|",
          "Reset fractal rotation",
          do_fractal_rotate, ESi, "v", 0)
    CMD3( "fractal-set-colors-default", "c",
          "Reset fractal colors to default",
          do_fractal_set_colors, ESi, "v", 0)
    CMD3( "fractal-set-colors-gray", "g",
          "Set fractal colors to gray scale",
          do_fractal_set_colors, ESi, "v", 1)
    CMD2( "fractal-shift-colors-left", "[",
          "Shift fractal color palette left",
          do_fractal_shift_colors, ESi, "q")
    CMD2( "fractal-shift-colors-right", "]",
          "Shift fractal color palette right",
          do_fractal_shift_colors, ESi, "p")
    CMD2( "fractal-iter-less", "{",
          "Decrease the fractal iteration count",
          do_fractal_iter, ESi, "q")
    CMD2( "fractal-iter-more", "}",
          "Increase the fractal iteration count",
          do_fractal_iter, ESi, "p")
    CMD2( "fractal-bailout-less", "<",
          "Decrease the fractal bailout value",
          do_fractal_bailout, ESi, "q")
    CMD2( "fractal-bailout-more", ">",
          "Increase the fractal bailout value",
          do_fractal_bailout, ESi, "p")
    CMD2( "fractal-set-type", "1, 2, 3, 4, 5, 6, 7, 8, 9",
          "Select the fractal type (1-9)",
          do_fractal_set_type, ESi, "k")
    CMD2( "fractal-set-parameters", "=",
          "Set the fractal parameters",
          do_fractal_set_parameters, ESs,
          "s{Fractal parameters: }[mparm]|mparm|")
    CMD0( "fractal-help", "?, f1",
          "Show the fractal information and help window",
          do_fractal_help)
};

static int fractal_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (qe_get_buffer_mode_data(p->b, &fractal_mode, NULL))
        return 100;
    else
        return 0;
}

static int fractal_mode_init(EditState *e, EditBuffer *b, int flags)
{
    if (e && (flags & MODEF_NEWINSTANCE)) {
        FractalState *ms;

        if (!(ms = fractal_get_state(e, 0)))
            return -1;

        fractal_set_parameters(e, ms, fractal_default_parameters);
        fractal_set_colors(ms, NULL, NULL);
    }
    return 0;
}

static void fractal_mode_free(EditBuffer *b, void *state) {
#if USE_BITMAP_API
    FractalState *ms = state;

    bmp_free(ms->screen, &ms->disp_bmp);
#endif
#if USE_DRAW_PICTURE
    FractalState *ms = state;

    /* free bitmap and palette */
    qe_free_picture(&ms->ip);
#endif
}

static void do_mandelbrot_test(EditState *s, int argval) {
    EditBuffer *b;

    if (!fractal_mode.name) {
        /* populate and register shell mode and commands */
        // XXX: remove this mess
        memcpy(&fractal_mode, &text_mode, offsetof(ModeDef, first_key));
        fractal_mode.name = "fractal";
        fractal_mode.mode_probe = fractal_mode_probe;
        fractal_mode.buffer_instance_size = sizeof(FractalState);
        fractal_mode.mode_init = fractal_mode_init;
        fractal_mode.mode_free = fractal_mode_free;
        fractal_mode.display_hook = fractal_display_hook;
        fractal_mode.default_wrap = WRAP_TRUNCATE;
#if USE_BITMAP_API || USE_DRAW_PICTURE
        fractal_mode.display = fractal_display;
#endif
        qe_register_mode(&fractal_mode, MODEF_NOCMD | MODEF_VIEW);
        qe_register_commands(&fractal_mode, fractal_commands, countof(fractal_commands));
    }

    b = eb_find("*Mandelbrot*");
    if (b) {
        eb_clear(b);
    } else {
        b = eb_new("*Mandelbrot*", BF_UTF8 | BF_STYLE4);
    }
    if (!b)
        return;

    b->default_mode = &fractal_mode;
    eb_set_charset(b, &charset_ucs2be, EOL_UNIX);
    do_delete_other_windows(s, 0);
    switch_to_buffer(s, b);
    if (argval != 1) {
        FractalState *ms = fractal_get_state(s, 1);
        if (ms) {
            ms->type = clamp_int(argval - 1, 0, countof(fractal_type) - 1);
            fractal_invalidate(ms);
        }
    }
}

static const CmdDef fractal_global_commands[] = {
    CMD2( "mandelbrot-test", "C-h m",
          "Explore the Mandelbrot set in fractal-mode",
          do_mandelbrot_test, ESi, "p")
};

static int fractal_init(void)
{
    qe_register_mode(&fractint_mode, MODEF_SYNTAX);
    qe_register_commands(NULL, fractal_global_commands, countof(fractal_global_commands));
    return 0;
}

qe_module_init(fractal_init);
