/*
 * R language mode for QEmacs.
 *
 * Copyright (c) 2015-2017 Charlie Gordon.
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

/*---------------- R coloring ----------------*/

#define MAX_KEYWORD_SIZE  16

static char const r_keywords[] = {
    "|if|else|for|in|while|repeat|next|break|switch|function|..."
    "|NA_integer_|NA_real_|NA_complex_|NA_character_"
    /* predefined constants */
    "|FALSE|TRUE|NULL|NA|Inf|NaN"
};

static char const r_types[] = {
    "|"
};

enum {
    R_STYLE_TEXT       = QE_STYLE_DEFAULT,
    R_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    R_STYLE_COMMENT    = QE_STYLE_COMMENT,
    R_STYLE_STRING     = QE_STYLE_STRING,
    R_STYLE_NUMBER     = QE_STYLE_NUMBER,
    R_STYLE_KEYWORD    = QE_STYLE_KEYWORD,
    R_STYLE_TYPE       = QE_STYLE_TYPE,
    R_STYLE_FUNCTION   = QE_STYLE_FUNCTION,
    R_STYLE_SYMBOL     = QE_STYLE_VARIABLE,
    R_STYLE_ARGDEF     = QE_STYLE_VARIABLE,
    R_STYLE_ARGNAME    = QE_STYLE_TYPE,
};

enum {
    R_LEVEL_MAX    = 15,
    IN_R_LEVEL     = 0x0F,
    IN_R_FUNCLEVEL = 0x70,
    R_FUNCLEVEL_SHIFT = 4,
    IN_R_ARGLIST   = 0x80,
};

static void r_colorize_line(QEColorizeContext *cp,
                              unsigned int *str, int n, ModeDef *syn)
{
    char keyword[MAX_KEYWORD_SIZE];
    int i = 0, j, start, c, delim, style, len, level, funclevel;
    int colstate = cp->colorize_state;

    level = colstate & IN_R_LEVEL;
    funclevel = (colstate & IN_R_FUNCLEVEL) >> R_FUNCLEVEL_SHIFT;
    style = 0;
    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            if (ustrstart(str + i, "line", NULL) && !qe_isalnum(str[i + 4]))
                style = R_STYLE_PREPROCESS;
            else
                style = R_STYLE_COMMENT;
            i = n;
            break;
        case '\'':
        case '\"':
        case '`':
            delim = c;
            while (i < n) {
                if ((c = str[i++]) == delim)
                    break;
                if (c == '\\' && i < n)
                    i++;
            }
            style = (delim == '`') ? R_STYLE_SYMBOL : R_STYLE_STRING;
            break;
        case '%':
            for (j = i; qe_isalpha(str[j]); j++)
                continue;
            if (j > i && str[j] == '%') {
                i = j + 1;
                style = R_STYLE_KEYWORD;
                break;
            }
            continue;
        case '(':
            level++;
            if (level == funclevel)
                colstate |= IN_R_ARGLIST;
            continue;
        case ')':
            if (level)
                level--;
            if (level < funclevel)
                funclevel = 0;
            colstate &= ~IN_R_ARGLIST;
            continue;
        case ',':
            if (funclevel && level == funclevel)
                colstate |= IN_R_ARGLIST;
            continue;
        case '=':
            colstate &= ~IN_R_ARGLIST;
            continue;
        case 0x00A0: /* non breaking space */
        case 0x3000: /* ideographic space */
            continue;
        default:
            /* parse numbers */
            if (qe_isdigit(c) || (c == '.' && qe_isdigit(str[i]))) {
                for (; i < n; i++) {
                    /* should parse actual syntax */
                    if (!qe_isalnum(str[i]) && str[i] != '.' && str[i] != '+' && str[i] != '-')
                        break;
                }
                style = R_STYLE_NUMBER;
                break;
            }
            /* parse identifiers and keywords */
            if (qe_isalpha_(c) || c == '.') {
                len = 0;
                keyword[len++] = (c < 0xFF) ? c : 0xFF;
                for (; i < n; i++) {
                    if (qe_isalnum_(str[i]) || str[i] == '.') {
                        if (len < countof(keyword) - 1)
                            keyword[len++] = (str[i] < 0xFF) ? str[i] : 0xFF;
                    } else {
                        break;
                    }
                }
                keyword[len] = '\0';
                for (j = i; qe_isblank(str[j]); j++)
                    continue;
                if (strfind(syn->keywords, keyword)) {
                    if (strequal(keyword, "function"))
                        funclevel = level + 1;
                    style = R_STYLE_KEYWORD;
                    break;
                }
                if (strfind(syn->types, keyword)) {
                    style = R_STYLE_TYPE;
                    break;
                }
                if (colstate & IN_R_ARGLIST) {
                    style = R_STYLE_ARGDEF;
                    break;
                }
                if (str[j] == '=' && str[j + 1] != '=') {
                    style = R_STYLE_ARGNAME;
                    break;
                }
                if (str[j] == '(') {
                    style = R_STYLE_FUNCTION;
                    break;
                }
                //style = R_STYLE_IDENTIFIER;
                break;
            }
            continue;
        }
        if (style) {
            SET_COLOR(str, start, i, style);
            style = 0;
        }
    }
    colstate &= ~(IN_R_LEVEL | IN_R_FUNCLEVEL);
    colstate |= clamp(level, 0, R_LEVEL_MAX);
    colstate |= funclevel << R_FUNCLEVEL_SHIFT;
    cp->colorize_state = colstate;
}

static int r_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    /* check file name or extension */
    if (match_extension(p->filename, mode->extensions)
    &&  !(p->buf[0] == '/' && p->buf[1] == '*'))
        return 80;

    return 1;
}

static ModeDef r_mode = {
    .name = "R",
    .extensions = "R",
    .keywords = r_keywords,
    .types = r_types,
    .mode_probe = r_mode_probe,
    .colorize_func = r_colorize_line,
};

static int r_init(void)
{
    qe_register_mode(&r_mode, MODEF_SYNTAX);

    return 0;
}

qe_module_init(r_init);
