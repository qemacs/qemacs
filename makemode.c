/*
 * Makefile mode for QEmacs.
 *
 * Copyright (c) 2000-2017 Charlie Gordon.
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

/*---------------- Makefile colors ----------------*/

enum {
    MAKEFILE_STYLE_TEXT       = QE_STYLE_DEFAULT,
    MAKEFILE_STYLE_COMMENT    = QE_STYLE_COMMENT,
    MAKEFILE_STYLE_STRING     = QE_STYLE_STRING,
    MAKEFILE_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    MAKEFILE_STYLE_TARGET     = QE_STYLE_FUNCTION,
    MAKEFILE_STYLE_FUNCTION   = QE_STYLE_FUNCTION,
    MAKEFILE_STYLE_VARIABLE   = QE_STYLE_VARIABLE,
    MAKEFILE_STYLE_MACRO      = QE_STYLE_TYPE,
};

static void makefile_colorize_line(QEColorizeContext *cp,
                                   unsigned int *str, int n, ModeDef *syn)
{
    char buf[32];
    int i = 0, start = i, bol = 1, from = 0, level, style;
    unsigned int c;

    if (qe_isalpha_(str[i])) {
        ustr_get_identifier_lc(buf, countof(buf), str[i], str, i + 1, n);
        if (strfind("ifeq|ifneq|ifdef|ifndef|include|else|endif", buf))
            goto preprocess;
    }
    if (str[i] == '-' && ustristart(str + i + 1, "include ", NULL))
        goto preprocess;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '$':
            style = MAKEFILE_STYLE_MACRO;
            if (str[i] == '(') {
                level = 1;
                for (i += 1; i < n; i++) {
                    if (str[i] == '(')
                        level++;
                    if (str[i] == ')' && --level <= 0)
                        break;
                    if (str[i] == ' ' || str[i] == '$') {
                        /* should have function color */
                    }
                }
                from = i + 1;
                SET_COLOR(str, start + 2, i, style);
                continue;
            }
            /* Should colorize non parenthesized macro */
            continue;
        case ' ':
        case '\t':
            if (start == 0)
                bol = 0;
            break;
        case '+':
        case '?':
            if (bol && str[i] == '=')
                goto variable;
            break;
        case ':':
            if (!bol)
                break;
            if (str[i] == '=')
                goto variable;
            SET_COLOR(str, from, i - 1, MAKEFILE_STYLE_TARGET);
            bol = 0;
            break;
        case '=':
            if (!bol)
                break;
        variable:
            SET_COLOR(str, from, i - 1, MAKEFILE_STYLE_VARIABLE);
            bol = 0;
            break;
        case '#':
            if (i > 1 && str[i - 2] == '\\')
                break;
            i = n;
            SET_COLOR(str, start, i, MAKEFILE_STYLE_COMMENT);
            continue;
        case '!':
            /*          case '.':*/
            if (start > 0)
                break;
        preprocess:
            /* scan for comment */
            for (; i < n; i++) {
                if (str[i] == '#')
                    break;
            }
            SET_COLOR(str, start, i, MAKEFILE_STYLE_PREPROCESS);
            continue;
        case '\'':
        case '`':
        case '"':
            /* parse string const */
            while (i < n) {
                if (str[i++] == c) {
                    break;
                }
            }
            SET_COLOR(str, start, i, MAKEFILE_STYLE_STRING);
            continue;
        default:
            break;
        }
    }
}

static int makefile_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    /* check file name or extension */
    if (match_extension(p->filename, mode->extensions)
    ||  stristart(p->filename, "makefile", NULL)
    ||  stristart(p->filename, "gnumakefile", NULL))
        return 70;

    return 1;
}

static int makefile_mode_init(EditState *s, EditBuffer *b, int flags)
{
    if (s) {
        s->b->tab_width = 8;
        s->indent_tabs_mode = 1;
    }
    return 0;
}

static ModeDef makefile_mode = {
    .name = "Makefile",
    .extensions = "mak|make|mk|gmk",
    .mode_probe = makefile_mode_probe,
    .mode_init = makefile_mode_init,
    .colorize_func = makefile_colorize_line,
};

enum {
    CMAKE_STYLE_TEXT       = QE_STYLE_DEFAULT,
    CMAKE_STYLE_COMMENT    = QE_STYLE_COMMENT,
    CMAKE_STYLE_STRING     = QE_STYLE_STRING,
    CMAKE_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    CMAKE_STYLE_TARGET     = QE_STYLE_FUNCTION,
    CMAKE_STYLE_FUNCTION   = QE_STYLE_FUNCTION,
    CMAKE_STYLE_KEYWORD    = QE_STYLE_KEYWORD,
    CMAKE_STYLE_VARIABLE   = QE_STYLE_VARIABLE,
    CMAKE_STYLE_MACRO      = QE_STYLE_TYPE,
};

static void cmake_colorize_line(QEColorizeContext *cp,
                                unsigned int *str, int n, ModeDef *syn)
{
    char buf[32];
    int i = 0, start = i, style;
    unsigned int c;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '$':
            style = CMAKE_STYLE_MACRO;
            if (str[i] == '{') {
                for (i += 1; i < n; i++) {
                    if (str[i] == '}')
                        break;
                }
                SET_COLOR(str, start + 2, i, style);
                if (str[i] == '}')
                    i++;
                continue;
            }
            continue;
        case '#':
            if (i > 1 && str[i - 2] == '\\')
                break;
            i = n;
            SET_COLOR(str, start, i, CMAKE_STYLE_COMMENT);
            continue;
        case '"':
            /* parse string const */
            while (i < n) {
                unsigned int cc = str[i++];

                if (cc == c)
                    break;

                if (cc == '$' && str[i] == '{') {
                    SET_COLOR(str, start, i + 1, CMAKE_STYLE_STRING);
                    for (start = i += 1; i < n && str[i] != c; i++) {
                        if (str[i] == '}')
                            break;
                    }
                    SET_COLOR(str, start, i, CMAKE_STYLE_MACRO);
                    start = i;
                }
            }
            SET_COLOR(str, start, i, CMAKE_STYLE_STRING);
            continue;
        default:
            if (qe_isalpha_(c)) {
                i += ustr_get_identifier_lc(buf, countof(buf), c, str, i, n);
                if (strfind("if|else|endif|set|true|false|include", buf)) {
                    SET_COLOR(str, start, i, CMAKE_STYLE_KEYWORD);
                } else
                if (str[i] == '(' || (str[i] == ' ' && str[i+1] == '(')) {
                    SET_COLOR(str, start, i, CMAKE_STYLE_FUNCTION);
                }
            }
            break;
        }
    }
}

static int cmake_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    /* check file name or extension */
    if (match_extension(p->filename, mode->extensions)
    ||  stristart(p->filename, "cmakelists.txt", NULL))
        return 70;

    return 1;
}

static ModeDef cmake_mode = {
    .name = "CMake",
    .extensions = "cmake",
    .mode_probe = cmake_mode_probe,
    .colorize_func = cmake_colorize_line,
};

static int makefile_init(void)
{
    qe_register_mode(&makefile_mode, MODEF_SYNTAX);
    qe_register_mode(&cmake_mode, MODEF_SYNTAX);

    return 0;
}

qe_module_init(makefile_init);
