/*
 * Pascal language modes for QEmacs.
 *
 * Copyright (c) 2000-2020 Charlie Gordon.
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

/*---------------- Pascal/Turbo Pascal/Delphi coloring ----------------*/
/* Should do Delphi specific things */

static char const pascal_keywords[] = {
    "|absolute|and|array|asm|begin|case|comp|const|div|do|downto"
    "|else|end|extended|external|false|far|file|for|forward|function|goto"
    "|if|implementation|in|inline|interface|interrupt"
    "|label|mod|near|nil|not|of|or|overlay"
    "|packed|procedure|program|record|repeat"
    "|set|shl|shr|single|text|then|to|true|type"
    "|unit|until|uses|var|while|with|xor"
    "|"
};

static char const pascal_types[] = {
    "|boolean|byte|char|double|integer|longint|pointer|real|shortint"
    "|string|word"
    "|"
};

enum {
    IN_PASCAL_COMMENT  = 0x01,
    IN_PASCAL_COMMENT1 = 0x02,
    IN_PASCAL_COMMENT2 = 0x04,
};

enum {
    PASCAL_STYLE_TEXT =       QE_STYLE_DEFAULT,
    PASCAL_STYLE_KEYWORD =    QE_STYLE_KEYWORD,
    PASCAL_STYLE_TYPE =       QE_STYLE_TYPE,
    PASCAL_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    PASCAL_STYLE_COMMENT =    QE_STYLE_COMMENT,
    PASCAL_STYLE_STRING =     QE_STYLE_STRING,
    PASCAL_STYLE_IDENTIFIER = QE_STYLE_VARIABLE,
    PASCAL_STYLE_NUMBER =     QE_STYLE_NUMBER,
    PASCAL_STYLE_FUNCTION =   QE_STYLE_FUNCTION,
};

static void pascal_colorize_line(QEColorizeContext *cp,
                                 unsigned int *str, int n, ModeDef *syn)
{
    char kbuf[16];
    int i = 0, start = i, c, k, style = 0;
    int colstate = cp->colorize_state;

    if (colstate & IN_PASCAL_COMMENT)
        goto in_comment;

    if (colstate & IN_PASCAL_COMMENT1)
        goto in_comment1;

    if (colstate & IN_PASCAL_COMMENT2)
        goto in_comment2;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '/':
            if (str[i] == '/') {    /* C++ comments, recent extension */
                i = n;
                style = PASCAL_STYLE_COMMENT;
                break;
            }
            continue;
        case '{':
            /* check for preprocessor */
            if (str[i] == '$') {
                colstate = IN_PASCAL_COMMENT1;
                i++;
            in_comment1:
                style = PASCAL_STYLE_PREPROCESS;
            } else {
                /* regular comment, non-recursive */
                colstate = IN_PASCAL_COMMENT;
            in_comment:
                style = PASCAL_STYLE_COMMENT;
            }
            while (i < n) {
                if (str[i++] == '}') {
                    colstate = 0;
                    break;
                }
            }
            break;
        case '(':
            if (str[i] == '*') {
                /* regular comment non-recursive */
                /* in Standard Pascal, { and (* are equivalent,
                   so { *) would be a valid comment.
                   We do not support this as Turbo-Pascal does not either */
                colstate = IN_PASCAL_COMMENT2;
                i++;
            in_comment2:
                while (i < n) {
                    if (str[i++] == '*' && str[i] == ')') {
                        i++;
                        colstate = 0;
                        break;
                    }
                }
                style = PASCAL_STYLE_COMMENT;
                break;
            }
            continue;
        case '\'':
            /* parse string or char const */
            while (i < n) {
                /* XXX: escape sequences? */
                if (str[i++] == (unsigned int)c)
                    break;
            }
            style = PASCAL_STYLE_STRING;
            break;
        case '#':
            /* parse hex char const */
            for (; i < n; i++) {
                if (!qe_isxdigit(str[i]))
                    break;
            }
            style = PASCAL_STYLE_STRING;
            break;
        default:
            /* parse numbers */
            if (qe_isdigit(c) || c == '$') {
                for (; i < n; i++) {
                    if (!qe_isalnum(str[i]) && str[i] != '.')
                        break;
                }
                style = PASCAL_STYLE_NUMBER;
                break;
            }
            /* parse identifiers and keywords */
            if (qe_isalpha_(c)) {
                i += ustr_get_identifier_lc(kbuf, countof(kbuf), c, str, i, n);
                if (strfind(syn->keywords, kbuf)) {
                    style = PASCAL_STYLE_KEYWORD;
                } else
                if (strfind(syn->types, kbuf)) {
                    style = PASCAL_STYLE_TYPE;
                } else {
                    k = i;
                    if (qe_isblank(str[k]))
                        k++;
                    if (str[k] == '(' && str[k + 1] != '*')
                        style = PASCAL_STYLE_FUNCTION;
                    else
                        style = PASCAL_STYLE_IDENTIFIER;
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
    cp->colorize_state = colstate;
}

static ModeDef pascal_mode = {
    .name = "Pascal",
    .extensions = "p|pas",
    .keywords = pascal_keywords,
    .types = pascal_types,
    .colorize_func = pascal_colorize_line,
};

static int pascal_init(void)
{
    qe_register_mode(&pascal_mode, MODEF_SYNTAX);

    return 0;
}

qe_module_init(pascal_init);
