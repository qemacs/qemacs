/*
 * EBNF and AntLR language mode for QEmacs.
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

/*---------------- EBNF coloring ----------------*/

#define MAX_KEYWORD_SIZE  16

static char const ebnf_keywords[] = {
    "|all|assert|attr|by|comments|compounds|construct|deconstruct"
    "|define|each|end|export|external|function|import|include"
    "|keys|list|match|not|opt|push|pop|redefine|repeat|replace"
    "|rule|see|skipping|tokens|where"
};

static char const ebnf_types[] = {
    "|"
};

enum {
    EBNF_STYLE_TEXT =        QE_STYLE_DEFAULT,
    EBNF_STYLE_COMMENT =     QE_STYLE_COMMENT,
    EBNF_STYLE_CHARCONST =   QE_STYLE_STRING,
    EBNF_STYLE_STRING =      QE_STYLE_STRING,
    EBNF_STYLE_KEYWORD =     QE_STYLE_KEYWORD,
    EBNF_STYLE_NUMBER =      QE_STYLE_NUMBER,
    EBNF_STYLE_TYPE =        QE_STYLE_TYPE,
    EBNF_STYLE_PREPROCESS =  QE_STYLE_PREPROCESS,
    EBNF_STYLE_IDENTIFIER =  QE_STYLE_KEYWORD,
};

enum {
    IN_EBNF_COMMENT1 = 0x01,
    IN_EBNF_COMMENT2 = 0x02,
    IN_EBNF_COMMENT3 = 0x04,
};

#define U_HORIZONTAL_ELLIPSIS          0x2026
#define U_LEFT_SINGLE_QUOTATION_MARK   0x2018
#define U_RIGHT_SINGLE_QUOTATION_MARK  0x2019

static void ebnf_colorize_line(QEColorizeContext *cp,
                               unsigned int *str, int n, ModeDef *syn)
{
    char kbuf[MAX_KEYWORD_SIZE];
    int i = 0, start = 0, c, style;
    int colstate = cp->colorize_state;

    if (colstate & IN_EBNF_COMMENT1)
        goto in_comment1;

    if (colstate & IN_EBNF_COMMENT2)
        goto in_comment2;

    if (colstate & IN_EBNF_COMMENT3)
        goto in_comment3;

    style = 0;
    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '%':
            if (str[i] == '(') {
                colstate = IN_EBNF_COMMENT1;
                i++;
            in_comment1:
                while (i < n) {
                    if (str[i++] == ')' && str[i] == '%') {
                        i++;
                        colstate = 0;
                        break;
                    }
                }
            } else
            if (str[i] == '{') {
                colstate = IN_EBNF_COMMENT2;
                i++;
            in_comment2:
                while (i < n) {
                    if (str[i++] == '}' && str[i] == '%') {
                        i++;
                        colstate = 0;
                        break;
                    }
                }
            } else {
                i = n;
            }
            style = EBNF_STYLE_COMMENT;
            break;

        case '/':
            if (str[i] == '*') {  /* C block comment */
                colstate |= IN_EBNF_COMMENT3;
                i++;
            in_comment3:
                while (i < n) {
                    if (str[i++] == '*' && str[i] == '/') {
                        i++;
                        colstate &= ~IN_EBNF_COMMENT3;
                        break;
                    }
                }
                style = EBNF_STYLE_COMMENT;
                break;
            }
            if (str[i] == '/') { /* line comment */
                i = n;
                style = EBNF_STYLE_COMMENT;
                break;
            }
            continue;

        case '\'':
        case '`':
        case U_LEFT_SINGLE_QUOTATION_MARK:
            /* parse quoted token */
            while (i < n) {
                int c = str[i++];
                if (c == '\'' || c == U_RIGHT_SINGLE_QUOTATION_MARK)
                    break;
            }
            style = EBNF_STYLE_CHARCONST;
            break;

        case '\"':
            /* parse quoted token */
            while (i < n) {
                int c = str[i++];
                if (c == '\"')
                    break;
            }
            style = EBNF_STYLE_STRING;
            break;

        default:
            /* parse numbers */
            if (qe_isdigit(c)) {
                for (; i < n; i++) {
                    if (!qe_isalnum(str[i]) && str[i] != '.')
                        break;
                }
                style = EBNF_STYLE_NUMBER;
                break;
            }
            /* parse identifiers and keywords */
            if (qe_isalpha_(c) || c == U_HORIZONTAL_ELLIPSIS) {
                i += ustr_get_identifier(kbuf, countof(kbuf), c, str, i, n);
                if (strfind(syn->keywords, kbuf)) {
                    style = EBNF_STYLE_KEYWORD;
                    break;
                }
                if (strfind(syn->types, kbuf)) {
                    style = EBNF_STYLE_TYPE;
                    break;
                }
                style = EBNF_STYLE_IDENTIFIER;
                break;
            }
            continue;
        }
        if (style) {
            SET_COLOR(str, start, i, style);
            style = 0;
        }
    }
}

static ModeDef ebnf_mode = {
    .name = "ebnf",
    .extensions = "ebnf",
    .keywords = ebnf_keywords,
    .types = ebnf_types,
    .colorize_func = ebnf_colorize_line,
};

static ModeDef antlr_mode = {
    .name = "antlr",
    .extensions = "antlr",
    .keywords = ebnf_keywords,
    .types = ebnf_types,
    .colorize_func = ebnf_colorize_line,
};

static int ebnf_init(void)
{
    qe_register_mode(&antlr_mode, MODEF_SYNTAX);
    qe_register_mode(&ebnf_mode, MODEF_SYNTAX);

    return 0;
}

qe_module_init(ebnf_init);
