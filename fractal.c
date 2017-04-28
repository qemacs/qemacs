/*
 * QEmacs, character based fractal rendering
 *
 * Copyright (c) 2017 Charlie Gordon.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <math.h>

#include "qe.h"
#include "qfribidi.h"
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
                               unsigned int *str, int n, ModeDef *syn)
{
    int i = 0, start, indent, c, state, state1, style, klen, delim;
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

static ModeDef fractal_mode;

#if 1
typedef long double fnum_t;
#define MFT  "%.21Lg"
#else
typedef long double fnum_t;
#define MFT  "%.16Lg"
#endif

typedef struct FractalState FractalState;

struct FractalState {
    QEModeData base;

    int cols, rows;
    int maxiter;        /* maximum iteration number */
    int cb, nc;         /* color palette base and length */
    int rot;            /* rotation in degrees */
    int zoom;           /* zoom level in dB */
    fnum_t scale;     /* zoom factor = pow(10, -mzoom/10) */
    fnum_t bailout; /* maximum module */
    fnum_t x, y;      /* center position */
    fnum_t m0, m1, m2, m3; /* rotation matrix */
};

const char fractal_default_parameters[] = {
    " maxiter=215"
    " cb=16"
    " nc=216"
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

static void fractal_set_rotation(FractalState *ms, int rot) {
    ms->rot = rot;
    /* compute rotation matrix */
    ms->m0 = cos(-ms->rot * M_PI / 180.0);
    ms->m1 = sin(-ms->rot * M_PI / 180.0);
    ms->m2 = -ms->m1;
    ms->m3 = ms->m0;
}

static void fractal_set_zoom(FractalState *ms, int level) {
    ms->zoom = level;
    ms->scale = pow(10.0, -ms->zoom / 10.0);
}

static void fractal_set_parameters(EditState *s, FractalState *ms, const char *parms)
{
    const char *p;

    ms->cols = ms->rows = 0;    /* force refresh */

    for (p = parms;;) {
        p += strspn(p, ";, \t\r\n");
        if (*p == '\0')
            break;
        if (strstart(p, "maxiter=", &p)) {
            ms->maxiter = strtol(p, (char **)&p, 0);
        } else
        if (strstart(p, "cb=", &p)) {
            ms->cb = strtol(p, (char **)&p, 0);
        } else
        if (strstart(p, "nc=", &p)) {
            ms->nc = strtol(p, (char **)&p, 0);
        } else
        if (strstart(p, "rot=", &p)) {
            fractal_set_rotation(ms, strtol(p, (char **)&p, 0));
        } else
        if (strstart(p, "zoom=", &p)) {
            fractal_set_zoom(ms, strtol(p, (char **)&p, 0));
        } else
        if (strstart(p, "bailout=", &p)) {
            ms->bailout = strtold(p, (char **)&p);
        } else
        if (strstart(p, "x=", &p)) {
            ms->x = strtold(p, (char **)&p);
        } else
        if (strstart(p, "y=", &p)) {
            ms->y = strtold(p, (char **)&p);
        } else {
            put_status(s, "invalid parameter: %s", p);
            break;
        }
    }
}

static void do_fractal_draw(EditState *s, FractalState *ms)
{
    int cols = ms->cols, rows = ms->rows, zoom = ms->zoom;
    int maxiter = ms->maxiter + zoom, cb = ms->cb, nc = ms->nc;
    int i, j, nx, ny, fg, bg;
    fnum_t xc = ms->x, yc = ms->y, scale = ms->scale;
    fnum_t bailout = ms->bailout;
    fnum_t sx, sy, x, y, dx, dy, xr, yr, a, b, c;

    if (s->height == 0 || s->width == 0 || rows == 0 || cols == 0 || nc == 0)
        return;

    sx = 3.2 * scale;
    if (s->width == s->cols) {
        /* character based, assume 80x25 4/3 aspect ratio */
        sy = sx * 3 / 4 * 80 / 23 * rows / cols;
    } else {
        /* pixel based, assume 100% pixel aspect ratio */
        sy = sx * s->height / s->width;
    }
    dx = sx / cols;
    dy = sy / rows;

    s->b->flags &= ~BF_READONLY;

    eb_delete_range(s->b, 0, s->b->total_size);

    for (ny = 0, y = -sy / 2; ny < rows; ny++, y += dy) {
        for (nx = 0, x = -sx / 2; nx < cols; nx++, x += dx) {
            xr = xc + x * ms->m0 + y * ms->m1;
            yr = yc + x * ms->m2 + y * ms->m3;
            for (a = b = i = 0;
                 a * a + b * b <= bailout && i++ < maxiter;
                 c = a, a = a * a - b * b + xr, b = 2 * c * b + yr)
                continue;
            //xr = xc + x * ms->m0 + (y + dy / 2) * ms->m1;
            //yr = yc + x * ms->m2 + (y + dy / 2) * ms->m3;
            xr += dy / 2 * ms->m1;
            yr += dy / 2 * ms->m3;
            for (a = b = j = 0;
                 a * a + b * b <= bailout && j++ < maxiter;
                 c = a, a = a * a - b * b + xr, b = 2 * c * b + yr)
                continue;
            bg = i >= maxiter ? 0 : cb + i % nc;
            fg = j >= maxiter ? 0 : cb + j % nc;
            s->b->cur_style = QE_TERM_COMPOSITE | QE_TERM_MAKE_COLOR(fg, bg);
            eb_insert_uchar(s->b, s->b->total_size, fg == bg ? ' ' : 0x2584);
        }
        s->b->cur_style = QE_STYLE_DEFAULT;
        eb_insert_uchar(s->b, s->b->total_size, '\n');
    }
    s->b->flags |= BF_READONLY;

    put_status(s, "Mandelbrot set x="MFT", y="MFT", zoom=%d, scale=%.6g, rot=%d",
               ms->x, ms->y, ms->zoom, (double)ms->scale, ms->rot);
}

static void do_fractal_refresh(EditState *s) {
    FractalState *ms = fractal_get_state(s, 0);
    if (ms) {
        ms->cols = s->cols;
        ms->rows = s->rows;
        do_fractal_draw(s, ms);
    }
}

static void do_fractal_move(EditState *s, int deltax, int deltay) {
    FractalState *ms = fractal_get_state(s, 1);
    if (ms) {
        fnum_t dx = deltax * ms->scale / 40;
        fnum_t dy = deltay * ms->scale / 40;
        ms->x += dx * ms->m0 + dy * ms->m1;
        ms->y += dx * ms->m2 + dy * ms->m3;
        do_fractal_refresh(s);
    }
}

static void do_fractal_move_x(EditState *s, int delta) {
    do_fractal_move(s, delta, 0);
}

static void do_fractal_move_y(EditState *s, int delta) {
    do_fractal_move(s, 0, delta);
}

static void do_fractal_zoom(EditState *s, int delta) {
    FractalState *ms = fractal_get_state(s, 1);
    if (ms) {
        fractal_set_zoom(ms, ms->zoom + delta);
        do_fractal_refresh(s);
    }
}

static void do_fractal_rotate(EditState *s, int delta) {
    FractalState *ms = fractal_get_state(s, 1);
    if (ms) {
        fractal_set_rotation(ms, delta ? ms->rot + delta : 0);
        do_fractal_refresh(s);
    }
}

static void do_fractal_iter(EditState *s, int delta) {
    FractalState *ms = fractal_get_state(s, 1);
    if (ms) {
        ms->maxiter += delta;
        do_fractal_refresh(s);
    }
}

static void do_fractal_module(EditState *s, int delta) {
    FractalState *ms = fractal_get_state(s, 1);
    if (ms) {
        ms->bailout += delta;
        do_fractal_refresh(s);
    }
}

static void do_fractal_set_parameters(EditState *s, const char *params) {
    FractalState *ms = fractal_get_state(s, 1);
    if (ms) {
        fractal_set_parameters(s, ms, params);
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

    eb_printf(b, "%*s: %s\n", w, "type", "Mandelbrot");
    eb_printf(b, "%*s: "MFT"\n", w, "x", ms->x);
    eb_printf(b, "%*s: "MFT"\n", w, "y", ms->y);
    eb_printf(b, "%*s: %d\n", w, "maxiter", ms->maxiter);
    eb_printf(b, "%*s: %d\n", w, "rot", ms->rot);
    eb_printf(b, "%*s: %d\n", w, "zoom", ms->zoom);
    eb_printf(b, "%*s: %.6g\n", w, "scale", (double)ms->scale);
    eb_printf(b, "%*s: "MFT"\n", w, "bailout", ms->bailout);
    eb_printf(b, "%*s: cb=%d nc=%d\n", w, "colors", ms->cb, ms->nc);

    eb_printf(b, "\nFractal navigator:\n\n");

    // XXX: should use print_bindings(b, "Fractal commands", 0, &fractal_mode);

    eb_printf(b, "%*s: %s\n", w, "left, right", "move center point horizontally");
    eb_printf(b, "%*s: %s\n", w, "up, down", "move center point vertically");
    eb_printf(b, "%*s: %s\n", w, "+, SP", "zoom in");
    eb_printf(b, "%*s: %s\n", w, "-, _", "zoom out");
    eb_printf(b, "%*s: %s\n", w, "/", "rotate right");
    eb_printf(b, "%*s: %s\n", w, "\\, .", "rotate left");
    eb_printf(b, "%*s: %s\n", w, "|", "reset rotation");
    eb_printf(b, "%*s: %s\n", w, "[, ]", "change maxiter");
    eb_printf(b, "%*s: %s\n", w, "<, >", "change bailout");
    eb_printf(b, "%*s: %s\n", w, "=", "set fractal parameters");

    b->flags |= BF_READONLY;
    show_popup(s, b);
}

static void fractal_display_hook(EditState *s) {
    FractalState *ms = fractal_get_state(s, 0);
    if (ms) {
        if (s->rows != ms->rows || s->cols != ms->cols)
            do_fractal_refresh(s);
    }
}

static CmdDef fractal_commands[] = {
    CMD3( KEY_LEFT, KEY_NONE,
          "fractal-left", do_fractal_move_x, ESi, -1, "v")
    CMD3( KEY_RIGHT, KEY_NONE,
          "fractal-right", do_fractal_move_x, ESi, +1, "v")
    CMD3( KEY_UP, KEY_NONE,
          "fractal-up", do_fractal_move_y, ESi, -1, "v")
    CMD3( KEY_DOWN, KEY_NONE,
          "fractal-down", do_fractal_move_y, ESi, +1, "v")
    CMD3( '+', ' ',
          "fractal-zoom-in", do_fractal_zoom, ESi, +1, "v")
    CMD3( '-', '_',
          "fractal-zoom-out", do_fractal_zoom, ESi, -1, "v")
    CMD3( '\\', '.',
          "fractal-rotate-left", do_fractal_rotate, ESi, +1, "v")
    CMD3( '/', KEY_NONE,
          "fractal-rotate-right", do_fractal_rotate, ESi, -1, "v")
    CMD3( '|', KEY_NONE,
          "fractal-rotate-none", do_fractal_rotate, ESi, 0, "v")
    CMD3( '[', KEY_NONE,
          "fractal-iter-less", do_fractal_iter, ESi, -1, "v")
    CMD3( ']', KEY_NONE,
          "fractal-iter-more", do_fractal_iter, ESi, +1, "v")
    CMD3( '<', KEY_NONE,
          "fractal-module-less", do_fractal_module, ESi, -1, "v")
    CMD3( '>', KEY_NONE,
          "fractal-module-more", do_fractal_module, ESi, +1, "v")
    CMD2( '=', KEY_NONE,
          "fractal-set-parameters", do_fractal_set_parameters, ESs,
          "s{Fractal parameters: }[mparm]|mparm|")
    CMD0( '?', KEY_F1,
          "fractal-help", do_fractal_help)
    CMD_DEF_END,
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
    if (e) {
        FractalState *ms;

        if (!(ms = fractal_get_state(e, 0)))
            return -1;

        fractal_set_parameters(e, ms, fractal_default_parameters);
        put_status(e, "fractal init");
    }
    return 0;
}

static void do_mandelbrot_test(EditState *s) {
    EditBuffer *b;

    if (!fractal_mode.name) {
        /* populate and register shell mode and commands */
        memcpy(&fractal_mode, &text_mode, sizeof(ModeDef));
        fractal_mode.name = "fractal";
        fractal_mode.mode_name = NULL;
        fractal_mode.mode_probe = fractal_mode_probe;
        fractal_mode.buffer_instance_size = sizeof(FractalState);
        fractal_mode.mode_init = fractal_mode_init;
        fractal_mode.display_hook = fractal_display_hook;
        fractal_mode.default_wrap = WRAP_TRUNCATE;
        qe_register_mode(&fractal_mode, MODEF_NOCMD | MODEF_VIEW);
        qe_register_cmd_table(fractal_commands, &fractal_mode);
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
}

static CmdDef fractal_global_commands[] = {
    CMD0( KEY_CTRLH('m'), KEY_NONE,
          "mandelbrot-test", do_mandelbrot_test)
    CMD_DEF_END,
};

static int fractal_init(void)
{
    qe_register_mode(&fractint_mode, MODEF_SYNTAX);
    qe_register_cmd_table(fractal_global_commands, NULL);
    return 0;
}

qe_module_init(fractal_init);
