/*
 * Makefile mode for QEmacs.
 *
 * Copyright (c) 2000-2014 Charlie Gordon.
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

/* grab an identifier from a uint buf, stripping color.
 * return char count.
 */
static int get_word_lc(char *buf, int buf_size, unsigned int *p)
{
    unsigned int c;
    int i, j;

    i = j = 0;
    c = p[i] & CHAR_MASK;
    if (qe_isalpha_(c)) {
        do {
            if (j < buf_size - 1)
                buf[j++] = qe_tolower(c);
            i++;
            c = p[i] & CHAR_MASK;
        } while (qe_isalnum_(c));
    }
    buf[j] = '\0';
    return i;
}

enum {
    MAKEFILE_STYLE_TEXT       = QE_STYLE_DEFAULT,
    MAKEFILE_STYLE_COMMENT    = QE_STYLE_COMMENT,
    MAKEFILE_STYLE_STRING     = QE_STYLE_STRING,
    MAKEFILE_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    MAKEFILE_STYLE_TARGET     = QE_STYLE_FUNCTION,
    MAKEFILE_STYLE_VARIABLE   = QE_STYLE_VARIABLE,
    MAKEFILE_STYLE_MACRO      = QE_STYLE_TYPE,
};

static void makefile_colorize_line(QEColorizeContext *cp,
                                   unsigned int *str, int n, int mode_flags)
{
    char buf[32];
    int i = 0, j = i, level;

    if (qe_isalnum_(str[i])) {
        get_word_lc(buf, countof(buf), str);
        if (strfind("ifeq|ifneq|ifdef|ifndef|include|else|endif", buf))
            goto preprocess;
    }
    if (str[i] == '-' && ustristart(str + i + 1, "include ", NULL))
        goto preprocess;

    while (i < n) {
        switch (str[i]) {
        case '$':
            i += 1;
            j = i + 1;
            if (str[i] == '(') {
                i += 1;
                level = 1;
                for (j = i; j < n; j++) {
                    if (str[j] == '(')
                        level++;
                    if (str[j] == ')' && --level <= 0)
                        break;
                    if (str[j] == ' ' || str[j] == '$') {
                        /* should have function color */
                        j = i;
                        break;
                    }
                }
            }
            if (i < j)
                SET_COLOR(str, i, j, MAKEFILE_STYLE_MACRO);
            i = j;
            continue;
        case ' ':
        case '\t':
            if (i == 0)
                j = 1;
            break;
        case '+':
            if (!j && str[i+1] == '=')
                goto variable;
        case ':':
            if (j)
                break;
            if (str[i+1] == '=')
                goto variable;
            SET_COLOR(str, j, i, MAKEFILE_STYLE_TARGET);
            break;
        case '=':
            if (j)
                break;
        variable:
            SET_COLOR(str, j, i, MAKEFILE_STYLE_VARIABLE);
            break;
        case '#':
            if (i > 0 && str[i - 1] == '\\')
                break;
            SET_COLOR(str, i, n, MAKEFILE_STYLE_COMMENT);
            i = n;
            continue;
        case '!':
            /*          case '.':*/
            if (i > 0)
                break;
        preprocess:
            /* scan for comment */
            for (j = i + 1; j < n; j++) {
                if (str[j] == '#')
                    break;
            }
            SET_COLOR(str, i, j, MAKEFILE_STYLE_PREPROCESS);
            i = j;
            continue;
        case '\'':
        case '`':
        case '"':
            /* parse string const */
            for (j = i + 1; j < n; j++) {
                if (str[j] == str[i]) {
                    j++;
                    break;
                }
            }
            SET_COLOR(str, i, j, MAKEFILE_STYLE_STRING);
            i = j;
            continue;
        default:
            break;
        }
        i++;
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

static ModeDef makefile_mode;

static int makefile_mode_init(EditState *s)
{
    s->b->tab_width = 8;
    s->indent_tabs_mode = 1;
    return 0;
}

static int makefile_init(void)
{
    /* Makefile mode is almost like the text mode, so we copy and patch it */
    memcpy(&makefile_mode, &text_mode, sizeof(ModeDef));
    makefile_mode.name = "Makefile";
    makefile_mode.extensions = "mak|make|mk";
    makefile_mode.mode_probe = makefile_mode_probe;
    makefile_mode.mode_init = makefile_mode_init;
    makefile_mode.colorize_func = makefile_colorize_line;

    qe_register_mode(&makefile_mode, MODEF_SYNTAX);

    return 0;
}

qe_module_init(makefile_init);
