/*
 * Pascal language modes for QEmacs.
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
                                 const char32_t *str, int n,
                                 QETermStyle *sbuf, ModeDef *syn)
{
    char kbuf[16];
    int i = 0, start = i, k, style = 0;
    char32_t c;
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
                if (str[i++] == c)
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
            SET_STYLE(sbuf, start, i, style);
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

static int pascal_init(QEmacsState *qs)
{
    qe_register_mode(qs, &pascal_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(pascal_init);
