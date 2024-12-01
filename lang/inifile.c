/*
 * ini file mode for QEmacs.
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

/*---------------- Ini file (and similar) coloring ----------------*/

enum {
    INI_STYLE_TEXT =       QE_STYLE_DEFAULT,
    INI_STYLE_COMMENT =    QE_STYLE_COMMENT,
    INI_STYLE_STRING =     QE_STYLE_STRING,
    INI_STYLE_FUNCTION =   QE_STYLE_FUNCTION,
    INI_STYLE_NUMBER =     QE_STYLE_NUMBER,
    INI_STYLE_IDENTIFIER = QE_STYLE_VARIABLE,
    INI_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
};

static void ini_colorize_line(QEColorizeContext *cp,
                              const char32_t *str, int n,
                              QETermStyle *sbuf, ModeDef *syn)
{
    int i = 0, start, style = 0, indent;
    char32_t c;

    i = cp_skip_blanks(str, i, n);
    indent = i;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case ';':
            if (start == indent) {
                i = n;
                style = INI_STYLE_COMMENT;
                break;
            }
            continue;
        case '#':
            if (start == indent) {
                i = n;
                style = INI_STYLE_PREPROCESS;
                break;
            }
            continue;
        case '[':
            if (start == 0) {
                i = n;
                style = INI_STYLE_FUNCTION;
                break;
            }
            continue;
        case '\"':
            /* parse string const */
            while (i < n) {
                /* XXX: escape sequences? */
                if (str[i++] == '\"')
                    break;
            }
            style = INI_STYLE_STRING;
            break;
        case ' ':
        case '\t':
            continue;
        default:
            /* parse numbers */
            if (qe_isdigit(c)) {
                for (; i < n; i++) {
                    if (!qe_isalnum(str[i]))
                        break;
                }
                style = INI_STYLE_NUMBER;
                break;
            }
            /* parse identifiers and keywords */
            if (start == 0 && (qe_isalpha_(c) || c == '@' || c == '$')) {
                for (; i < n; i++) {
                    if (str[i] == '=')
                        break;
                }
                if (i < n) {
                    style = INI_STYLE_IDENTIFIER;
                }
                break;
            }
        }
        if (style) {
            SET_STYLE(sbuf, start, i, style);
            style = 0;
        }
    }
}

static int ini_mode_probe(ModeDef *mode, ModeProbeData *pd)
{
    const char *p = (const char *)pd->buf;
    const char *p_end = p + pd->buf_size;

    if (match_extension(pd->filename, mode->extensions))
        return 80;

    while (p < p_end) {
        /* skip comments */
        if (*p == ';' || *p == '#') {
            p = memchr(p, '\n', p_end - p);
            if (!p)
                return 1;
        }
        if (*p == '\n') {
            p++;
            continue;
        }
        /* Check for ^\[.+\]\n */
        if (*p == '[' && p[1] != '[' && p[1] != '{') {
            while (++p < p_end) {
                if (*p == ']')
                    return 40;
                if (*p == '\n')
                    return 1;
            }
        }
        break;
    }
    return 1;
}

static ModeDef ini_mode = {
    .name = "ini",
    .extensions = "ini|inf|INI|INF|reg",
    .mode_probe = ini_mode_probe,
    .colorize_func = ini_colorize_line,
};

static int ini_init(QEmacsState *qs)
{
    qe_register_mode(qs, &ini_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(ini_init);
