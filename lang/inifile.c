/*
 * ini file mode for QEmacs.
 *
 * Copyright (c) 2000-2022 Charlie Gordon.
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
                              char32_t *str, int n, ModeDef *syn)
{
    int i = 0, start, style = 0, indent;
    char32_t c;

    while (qe_isblank(str[i]))
        i++;

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
            SET_COLOR(str, start, i, style);
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

static int ini_init(void)
{
    qe_register_mode(&ini_mode, MODEF_SYNTAX);

    return 0;
}

qe_module_init(ini_init);
