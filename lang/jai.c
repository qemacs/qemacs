/*
 * Jai mode for QEmacs.
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

/*---------------- Jai coloring ----------------*/

static char const jai_keywords[] = {
    /* Jai keywords */
    // #char #foreign #import #run
    "using|new|remove|delete|cast|struct|enum|if|else|for|while|switch|"
    "case|continue|break|return|defer|inline|"
    /* predefined constants */
    "false|true|null|it|void|"
};

static char const jai_types[] = {
    "bool|string|int|float|float32|float64|"
    "u8|u16|u32|u64|s8|s16|s32|s64|"
};

enum {
    JAI_STYLE_DEFAULT    = 0,
    JAI_STYLE_DIRECTIVE  = QE_STYLE_PREPROCESS,
    JAI_STYLE_COMMENT    = QE_STYLE_COMMENT,
    JAI_STYLE_REGEX      = QE_STYLE_STRING_Q,
    JAI_STYLE_STRING     = QE_STYLE_STRING,
    JAI_STYLE_STRING_Q   = QE_STYLE_STRING_Q,
    JAI_STYLE_NUMBER     = QE_STYLE_NUMBER,
    JAI_STYLE_KEYWORD    = QE_STYLE_KEYWORD,
    JAI_STYLE_TYPE       = QE_STYLE_TYPE,
    JAI_STYLE_FUNCTION   = QE_STYLE_FUNCTION,
    JAI_STYLE_VARIABLE   = QE_STYLE_VARIABLE,
};

/* jai-mode colorization states */
enum {
    IN_JAI_COMMENT    = 0x0F,  /* multiline comment (nested) */
    IN_JAI_COMMENT_SHIFT = 0,
    IN_JAI_STRING     = 0x10,  /* double-quoted string */
    IN_JAI_STRING_Q   = 0x20,  /* single-quoted string */
};

static void jai_colorize_line(QEColorizeContext *cp,
                              unsigned int *str, int n, ModeDef *syn)
{
    int i = 0, start = i, style = 0, c = 0, i1, i2, delim, level;
    int state = cp->colorize_state;
    char kbuf[64];

    if (state) {
        /* if already in a state, go directly in the code parsing it */
        if (state & IN_JAI_COMMENT)
            goto parse_comment;
        if (state & IN_JAI_STRING)
            goto parse_string;
        if (state & IN_JAI_STRING_Q)
            goto parse_string_q;
    }

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '/':
            if (str[i] == '*') {
                /* multi-line nested (!) comment */
                state |= 1 << IN_JAI_COMMENT_SHIFT;
                i++;
            parse_comment:
                level = (state & IN_JAI_COMMENT) >> IN_JAI_COMMENT_SHIFT;
                while (i < n) {
                    c = str[i++];
                    if (c == '/' && str[i] == '*') {
                        level++;
                        i++;
                        continue;
                    }
                    if (c == '*' && str[i] == '/') {
                        i++;
                        level--;
                        if (level == 0)
                            break;
                    }
                }
                state &= ~IN_JAI_COMMENT;
                state |= level << IN_JAI_COMMENT_SHIFT;
                style = JAI_STYLE_COMMENT;
                break;
            } else
            if (str[i] == '/') {
                /* line comment */
                style = JAI_STYLE_COMMENT;
                i = n;
                break;
            }
            break;
        case '#':       /* directive */
            while (qe_isalnum(str[i])) {
                i++;
            }
            style = JAI_STYLE_DIRECTIVE;
            break;

        case '\'':      /* character constant */
            /* jai accepts quoted characters and quoted symbols */
            if (i + 1 < n && (str[i] == '\\' || str[i+1] == '\''))
                goto parse_string_q;
            else
                goto normal;

        parse_string_q:
            state |= IN_JAI_STRING_Q;
            style = JAI_STYLE_STRING_Q;
            delim = '\'';
            goto string;

        case '\"':      /* string literal */
        parse_string:
            state |= IN_JAI_STRING;
            style = JAI_STYLE_STRING;
            delim = '\"';
        string:
            while (i < n) {
                c = str[i++];
                if (c == '\\') {
                    if (i >= n)
                        break;
                    i++;
                } else
                if (c == delim) {
                    state &= ~(IN_JAI_STRING | IN_JAI_STRING_Q);
                    break;
                }
            }
            break;
        default:
        normal:
            if (qe_isdigit(c)) {
                int j;
                // Integers:
                // 0x[0-9a-fA-F]+
                // [0-9]+
                // Floats:
                // [0-9]+\.[0-9]+([eE][-\+]?[0-9]+)?
                // [0-9]+(\.[0-9]+)?[eE][-\+]?[0-9]+
                if (c == '0' && str[i] == 'x' && qe_isxdigit_(str[i + 1])) {
                    for (i += 3; qe_isxdigit_(str[i]); i++)
                        continue;
                } else {
                    while (qe_isdigit_(str[i]))
                        i++;
                    if (str[i] == '.' && qe_isdigit_(str[i + 1])) {
                        for (i += 2; qe_isdigit_(str[i]); i++)
                            continue;
                    }
                    if (str[i] == 'e' || str[i] == 'E') {
                        j = i + 1;
                        if (str[j] == '+' || str[j] == '-')
                            j++;
                        if (qe_isdigit_(str[j])) {
                            for (i = j + 1; qe_isdigit_(str[i]); i++)
                                continue;
                        }
                    }
                }
                style = JAI_STYLE_NUMBER;
                break;
            }
            if (qe_isalpha_(c)) {
                i += ustr_get_identifier(kbuf, countof(kbuf), c, str, i, n);

                if (strfind(syn->keywords, kbuf)) {
                    style = JAI_STYLE_KEYWORD;
                    break;
                }

                i1 = i;
                while (qe_isblank(str[i1]))
                    i1++;
                i2 = i1;
                while (qe_isblank(str[i2]))
                    i2++;

                if ((start == 0 || str[start - 1] != '.')
                &&  !qe_findchar(".(:", str[i])
                &&  strfind(syn->types, kbuf)) {
                    style = JAI_STYLE_TYPE;
                    break;
                }
                if (str[i1] == '(') {
                    /* function call */
                    /* XXX: different styles for call and definition */
                    style = JAI_STYLE_FUNCTION;
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
    /* set style on eol char */
    SET_COLOR1(str, n, style);

    cp->colorize_state = state;
}

static ModeDef jai_mode = {
    .name = "Jai",
    .extensions = "jai",
    .keywords = jai_keywords,
    .types = jai_types,
    .colorize_func = jai_colorize_line,
    //.indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

static int jai_init(void)
{
    qe_register_mode(&jai_mode, MODEF_SYNTAX);

    return 0;
}

qe_module_init(jai_init);
