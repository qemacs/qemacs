/*
 * TXL language mode for QEmacs.
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

/*---------------- TXL coloring ----------------*/

#define MAX_KEYWORD_SIZE  16

static char const txl_keywords[] = {
    "|all|assert|attr|by|comments|compounds|construct|deconstruct"
    "|define|each|end|export|external|function|import|include"
    "|keys|list|match|not|opt|push|pop|redefine|repeat|replace"
    "|rule|see|skipping|tokens|where"
};

static char const txl_types[] = {
    "|"
};

enum {
    TXL_STYLE_TEXT =        QE_STYLE_DEFAULT,
    TXL_STYLE_COMMENT =     QE_STYLE_COMMENT,
    TXL_STYLE_STRING =      QE_STYLE_STRING,
    TXL_STYLE_KEYWORD =     QE_STYLE_KEYWORD,
    TXL_STYLE_SYMBOL =      QE_STYLE_NUMBER,
    TXL_STYLE_TYPE =        QE_STYLE_TYPE,
    TXL_STYLE_PREPROCESS =  QE_STYLE_PREPROCESS,
    TXL_STYLE_IDENTIFIER =  QE_STYLE_VARIABLE,
};

enum {
    IN_TXL_COMMENT1 = 0x01,
    IN_TXL_COMMENT2 = 0x02,
};

static void txl_colorize_line(QEColorizeContext *cp,
                              unsigned int *str, int n, ModeDef *syn)
{
    char keyword[MAX_KEYWORD_SIZE];
    int i = 0, start = 0, c, style, klen;
    int colstate = cp->colorize_state;

    if (colstate & IN_TXL_COMMENT1)
        goto in_comment1;

    if (colstate & IN_TXL_COMMENT2)
        goto in_comment2;

    style = 0;
    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '%':
            if (str[i] == '(') {
                colstate = IN_TXL_COMMENT1;
                i++;
            in_comment1:
                while (i < n) {
                    if (str[i++] == ')' && str[i] == '%') {
                        colstate = 0;
                        break;
                    }
                }
            } else
            if (str[i] == '{') {
                colstate = IN_TXL_COMMENT2;
                i++;
            in_comment2:
                while (i < n) {
                    if (str[i++] == '}' && str[i] == '%') {
                        colstate = 0;
                        break;
                    }
                }
            } else {
                i = n;
            }
            style = TXL_STYLE_COMMENT;
            break;
        case '\'':
            /* parse quoted token */
            for (; i < n && !qe_isblank(str[i]); i++)
                continue;
            style = TXL_STYLE_SYMBOL;
            break;
        default:
            /* parse numbers */
            if (qe_isdigit(c)) {
                for (; i < n; i++) {
                    if (!qe_isalnum(str[i]) && str[i] != '.')
                        break;
                }
                style = TXL_STYLE_IDENTIFIER;
                break;
            }
            /* parse identifiers and keywords */
            if (qe_isalpha_(c)) {
                klen = 0;
                keyword[klen++] = qe_tolower(c);
                for (; i < n; i++) {
                    if (qe_isalnum_(str[i])) {
                        if (klen < countof(keyword) - 1)
                            keyword[klen++] = qe_tolower(str[i]);
                    } else {
                        if (qe_findchar("$&!@%#", str[i]))
                            i++;
                        break;
                    }
                }
                keyword[klen] = '\0';
                if (strfind(syn->keywords, keyword)) {
                    style = TXL_STYLE_KEYWORD;
                    break;
                }
                if (strfind(syn->types, keyword)) {
                    style = TXL_STYLE_TYPE;
                    break;
                }
                style = TXL_STYLE_IDENTIFIER;
                break;
            }
            continue;
        }
        if (style) {
            SET_COLOR(str, start, i, style);
            style = 0;
        }
    }
    cp->colorize_state = colstate;
}

static ModeDef txl_mode = {
    .name = "Txl",
    .extensions = "txl",
    .keywords = txl_keywords,
    .types = txl_types,
    .colorize_func = txl_colorize_line,
};

static int txl_init(void)
{
    qe_register_mode(&txl_mode, MODEF_SYNTAX);

    return 0;
}

qe_module_init(txl_init);
