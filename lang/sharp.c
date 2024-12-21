/*
 * Sharp mode (generic unix script colorizer) modes for QEmacs.
 *
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

#include "qe.h"

/*---------------- sharp file coloring ----------------*/

/* Very simple colorizer: # introduces comments, that's it! */

enum {
    SHARP_STYLE_TEXT =       QE_STYLE_DEFAULT,
    SHARP_STYLE_COMMENT =    QE_STYLE_COMMENT,
    SHARP_STYLE_STRING =     QE_STYLE_STRING,
    SHARP_STYLE_CONFIG =     QE_STYLE_PREPROCESS,
};

#define SHARP_STRING1  2
#define SHARP_STRING2  4
#define SHARP_CONFIG   8

static void sharp_colorize_line(QEColorizeContext *cp,
                                const char32_t *str, int n,
                                QETermStyle *sbuf, ModeDef *syn)
{
    int i = 0, start, style = SHARP_STYLE_TEXT;
    char32_t c;
    int mode_flags = syn->colorize_flags;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            i = n;
            style = SHARP_STYLE_COMMENT;
            break;
        case '\'':
            if (mode_flags & SHARP_STRING1)
                goto parse_string;
            continue;
        case '\"':
            if (mode_flags & SHARP_STRING2)
                goto parse_string;
            continue;
        parse_string:
            while (i < n && str[i++] != c)
                continue;
            style = SHARP_STYLE_STRING;
            break;
        case '$':
            if (mode_flags & SHARP_CONFIG) {
                while (i < n && !qe_isspace(str[i]))
                    i++;
                style = SHARP_STYLE_CONFIG;
                break;
            }
            continue;
        default:
            continue;
        }
        SET_STYLE(sbuf, start, i, style);
        style = SHARP_STYLE_TEXT;
    }
}

static int sharp_mode_probe(ModeDef *mode, ModeProbeData *pd)
{
    const char *p = cs8(pd->buf);

    if (stristart(pd->filename, ".gitignore", NULL)
    ||  stristart(pd->filename, ".clang-format", NULL))
        return 70;

    while (qe_isspace(*p))
        p++;

    if (*p == '#') {
        if (match_extension(pd->filename, "txt"))
            return 60;
        return 30;
    }
    return 1;
}

static ModeDef sharp_mode = {
    .name = "sharp",
    .mode_probe = sharp_mode_probe,
    .colorize_flags = 0,
    .colorize_func = sharp_colorize_line,
};

/*---------------- Yaml file coloring ----------------*/

/* Very simple colorizer: comments, strings */

static ModeDef yaml_mode = {
    .name = "Yaml",
    .extensions = "yaml|yml",
    .colorize_flags = SHARP_STRING1 | SHARP_STRING2,
    .colorize_func = sharp_colorize_line,
};

/*---------------- C2 recipe file coloring ----------------*/

/* Very simple colorizer: comments, strings, config */

static int recipe_mode_probe(ModeDef *mode, ModeProbeData *pd)
{
    if (stristart(pd->filename, "recipe.txt", NULL))
        return 70;

    return 1;
}

static ModeDef recipe_mode = {
    .name = "C2-recipe",
    .mode_probe = recipe_mode_probe,
    .colorize_flags = SHARP_STRING1 | SHARP_STRING2 | SHARP_CONFIG,
    .colorize_func = sharp_colorize_line,
};

static int sharp_init(QEmacsState *qs)
{
    qe_register_mode(qs, &sharp_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &yaml_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &recipe_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(sharp_init);
