/*
 * Makefile mode for QEmacs.
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
                                   const char32_t *str, int n,
                                   QETermStyle *sbuf, ModeDef *syn)
{
    char buf[32];
    int i = 0, start = i, bol = 1, from = 0, level, style;
    char32_t c;

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
                SET_STYLE(sbuf, start + 2, i, style);
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
            SET_STYLE(sbuf, from, i - 1, MAKEFILE_STYLE_TARGET);
            bol = 0;
            break;
        case '=':
            if (!bol)
                break;
        variable:
            SET_STYLE(sbuf, from, i - 1, MAKEFILE_STYLE_VARIABLE);
            bol = 0;
            break;
        case '#':
            if (i > 1 && str[i - 2] == '\\')
                break;
            i = n;
            SET_STYLE(sbuf, start, i, MAKEFILE_STYLE_COMMENT);
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
            SET_STYLE(sbuf, start, i, MAKEFILE_STYLE_PREPROCESS);
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
            SET_STYLE(sbuf, start, i, MAKEFILE_STYLE_STRING);
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
        /* XXX: should use the default values from mode variables */
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
                                const char32_t *str, int n,
                                QETermStyle *sbuf, ModeDef *syn)
{
    char buf[32];
    int i = 0, start = i, style;
    char32_t c;

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
                SET_STYLE(sbuf, start + 2, i, style);
                if (str[i] == '}')
                    i++;
                continue;
            }
            continue;
        case '#':
            if (i > 1 && str[i - 2] == '\\')
                break;
            i = n;
            SET_STYLE(sbuf, start, i, CMAKE_STYLE_COMMENT);
            continue;
        case '"':
            /* parse string const */
            while (i < n) {
                char32_t cc = str[i++];

                if (cc == c)
                    break;

                if (cc == '$' && str[i] == '{') {
                    SET_STYLE(sbuf, start, i + 1, CMAKE_STYLE_STRING);
                    for (start = i += 1; i < n && str[i] != c; i++) {
                        if (str[i] == '}')
                            break;
                    }
                    SET_STYLE(sbuf, start, i, CMAKE_STYLE_MACRO);
                    start = i;
                }
            }
            SET_STYLE(sbuf, start, i, CMAKE_STYLE_STRING);
            continue;
        default:
            if (qe_isalpha_(c)) {
                i += ustr_get_identifier_lc(buf, countof(buf), c, str, i, n);
                if (strfind("if|else|endif|set|true|false|include", buf)) {
                    SET_STYLE(sbuf, start, i, CMAKE_STYLE_KEYWORD);
                } else
                if (check_fcall(str, i)) {
                    SET_STYLE(sbuf, start, i, CMAKE_STYLE_FUNCTION);
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

static int makefile_init(QEmacsState *qs)
{
    qe_register_mode(qs, &makefile_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &cmake_mode, MODEF_SYNTAX);

    return 0;
}

qe_module_init(makefile_init);
