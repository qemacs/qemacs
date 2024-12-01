/*
 * Wolfram language mode for QEmacs.
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

/*---------------- Mathematica / Wolfram language ----------------*/

static char const wolfram_keywords[] = {
    "|False|True|None"
    "|"
};

static char const wolfram_types[] = {
    "|"
};

enum {
    IN_WOLFRAM_DEFAULT  = 0,
    IN_WOLFRAM_STRING1  = 1,
    IN_WOLFRAM_STRING2  = 2,
    IN_WOLFRAM_COMMENT_SHIFT = 2,
};

enum {
    WOLFRAM_STYLE_TEXT =       QE_STYLE_DEFAULT,
    WOLFRAM_STYLE_KEYWORD =    QE_STYLE_KEYWORD,
    WOLFRAM_STYLE_TYPE =       QE_STYLE_TYPE,
    WOLFRAM_STYLE_COMMENT =    QE_STYLE_COMMENT,
    WOLFRAM_STYLE_STRING1 =    QE_STYLE_STRING,
    WOLFRAM_STYLE_STRING2 =    QE_STYLE_STRING,
    WOLFRAM_STYLE_IDENTIFIER = QE_STYLE_DEFAULT,
    WOLFRAM_STYLE_VARIABLE =   QE_STYLE_VARIABLE,
    WOLFRAM_STYLE_NUMBER =     QE_STYLE_NUMBER,
    WOLFRAM_STYLE_FUNCTION =   QE_STYLE_FUNCTION,
};

static int wolfram_get_identifier(char *dest, int size, char32_t c,
                                  const char32_t *str, int i, int n)
{
    int pos = 0, j;

    for (j = i;; j++) {
        if (pos + 1 < size) {
            /* c is assumed to be an ASCII character */
            dest[pos++] = (char)c;
        }
        if (j >= n)
            break;
        c = str[j];
        if (!qe_isalnum_(c) && c != '$' && c != '`')
            break;
    }
    if (pos < size) {
        dest[pos] = '\0';
    }
    return j - i;
}

static void wolfram_colorize_line(QEColorizeContext *cp,
                                  const char32_t *str, int n,
                                  QETermStyle *sbuf, ModeDef *syn)
{
    char kbuf[16];
    int i = 0, start = i, k, style = 0, comment_level, base;
    char32_t c, c1 = 0;
    int colstate = cp->colorize_state;

    switch (colstate) {
    case IN_WOLFRAM_DEFAULT:
        break;
    case IN_WOLFRAM_STRING1:
        goto in_string1;
    case IN_WOLFRAM_STRING2:
        goto in_string2;
    default:
        comment_level = colstate >> IN_WOLFRAM_COMMENT_SHIFT;
        goto in_comment;
    }

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '(':
            if (str[i] == '*') {    /* parse comment */
                comment_level = 1;
                i++;
            in_comment:
                while (i < n) {
                    c = str[i++];
                    if (c == '(' && str[i] == '*') {
                        /* nested comment */
                        i++;
                        comment_level++;
                    } else
                    if (c == '*' && str[i] == ')') {
                        i++;
                        if (--comment_level == 0)
                            break;
                    }
                }
                colstate = comment_level << IN_WOLFRAM_COMMENT_SHIFT;
                style = WOLFRAM_STYLE_COMMENT;
                break;
            }
            continue;
        case '\'':
            /* parse single quoted string */
            colstate = IN_WOLFRAM_STRING1;
        in_string1:
            /* XXX: should flag syntax error on
               unescaped newline except at EOF */
            style = WOLFRAM_STYLE_STRING1;
            while (i < n) {
                c1 = str[i++];
                if (c1 == '\'') {
                    colstate = 0;
                    break;
                }
                if (c1 == '\\') {
                    if (i < n)
                        i++;
                }
            }
            break;
        case '\"':
            /* parse double quoted string */
            colstate = IN_WOLFRAM_STRING2;
        in_string2:
            /* XXX: should flag syntax error on
               unescaped newline except at EOF */
            style = WOLFRAM_STYLE_STRING2;
            while (i < n) {
                c1 = str[i++];
                if (c1 == '\"') {
                    colstate = 0;
                    break;
                }
                if (c1 == '\\') {
                    if (i < n)
                        i++;
                }
            }
            break;
        default:
            /* parse numbers */
            base = 10;
            if (c == '.' && qe_isdigit(str[i]))
                goto in_number;
            if (qe_isdigit(c)) {
                int value = 0;
                while (qe_isdigit(str[i])) {
                    value = value * 10 + str[i++] - '0';
                }
                if (str[i] == '^' && str[i + 1] == '^' && value < 36) {
                    i += 2;
                    base = value;
                    while (qe_digit_value(str[i]) < base)
                        i++;
                }
                if (str[i] == '.') {
                    i++;
                in_number:
                    while (qe_digit_value(str[i]) < base)
                        i++;
                }
                if (str[i] == '`') {
                    i++;
                    if (str[i] == '`')
                        i++;
                    while (qe_isdigit(str[i]))
                        i++;
                }
                if (str[i] == '*' && str[i + 1] == '^' && qe_isdigit(str[i + 2])) {
                    i += 3;
                    while (qe_isdigit(str[i]))
                        i++;
                }
                style = WOLFRAM_STYLE_NUMBER;
                break;
            }
            /* parse identifiers and keywords */
            if (c == '$' || c == '#' || qe_isalpha_(c)) {
                i += wolfram_get_identifier(kbuf, countof(kbuf), c, str, i, n);
                if (strfind(syn->keywords, kbuf)) {
                    style = WOLFRAM_STYLE_KEYWORD;
                } else
                if (strfind(syn->types, kbuf)) {
                    style = WOLFRAM_STYLE_TYPE;
                } else {
                    k = i;
                    if (qe_isblank(str[k]))
                        k++;
                    if (str[k] == '[')
                        style = WOLFRAM_STYLE_FUNCTION;
                    else
                    if (qe_islower(c) || c == '#' || c == '_')
                        style = WOLFRAM_STYLE_VARIABLE;
                    else
                        style = WOLFRAM_STYLE_IDENTIFIER;
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

static ModeDef wolfram_mode = {
    .name = "Wolfram",
    .extensions = "nb",
    .keywords = wolfram_keywords,
    .types = wolfram_types,
    .colorize_func = wolfram_colorize_line,
};

static int wolfram_init(QEmacsState *qs)
{
    qe_register_mode(qs, &wolfram_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(wolfram_init);
