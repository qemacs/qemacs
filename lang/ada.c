/*
 * Ada language mode for QEmacs.
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

/*---------------- Ada coloring ----------------*/

static char const ada_keywords[] = {
    "asm|begin|case|const|constructor|destructor|do|downto|else|elsif|end|"
    "file|for|function|goto|if|implementation|in|inline|interface|label|"
    "nil|object|of|procedure|program|repeat|then|to|type|unit|until|"
    "uses|var|while|with|use|is|new|all|package|private|loop|body|"
    "raise|return|pragma|constant|exception|when|out|range|tagged|access|"
    "record|exit|subtype|generic|limited|"

    "and|div|mod|not|or|shl|shr|xor|false|true|null|eof|eoln|"
    //"'class|'first|'last|"
};

static char const ada_types[] = {
    "array|boolean|byte|char|comp|double|extended|integer|longint|"
    "packed|real|shortint|single|string|text|word|"
    "duration|time|character|set|"
    "wide_character|wide_string|wide_wide_character|wide_wide_string|"
};

enum {
    IN_ADA_COMMENT1 = 0x01,
    IN_ADA_COMMENT2 = 0x02,
};

enum {
    ADA_STYLE_TEXT =       QE_STYLE_DEFAULT,
    ADA_STYLE_KEYWORD =    QE_STYLE_KEYWORD,
    ADA_STYLE_TYPE =       QE_STYLE_TYPE,
    ADA_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    ADA_STYLE_COMMENT =    QE_STYLE_COMMENT,
    ADA_STYLE_STRING =     QE_STYLE_STRING,
    ADA_STYLE_IDENTIFIER = QE_STYLE_DEFAULT,
    ADA_STYLE_NUMBER =     QE_STYLE_NUMBER,
    ADA_STYLE_FUNCTION =   QE_STYLE_FUNCTION,
};

static void ada_colorize_line(QEColorizeContext *cp,
                              const char32_t *str, int n,
                              QETermStyle *sbuf, ModeDef *syn)
{
    char kbuf[16];
    int i = 0, start = i, k, style;
    int colstate = cp->colorize_state;
    char32_t c;

    if (colstate & IN_ADA_COMMENT1)
        goto in_comment1;

    if (colstate & IN_ADA_COMMENT2)
        goto in_comment2;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '-':
        case '/':
            if (str[i] == c) {  /* // or -- comments */
                i = n;
                SET_STYLE(sbuf, start, i, ADA_STYLE_COMMENT);
                continue;
            }
            break;
        case '{':
            /* regular comment (recursive?) */
            colstate = IN_ADA_COMMENT1;
        in_comment1:
            while (i < n) {
                if (str[i++] == '}') {
                    colstate = 0;
                    break;
                }
            }
            SET_STYLE(sbuf, start, i, ADA_STYLE_COMMENT);
            continue;
        case '(':
            if (str[i] != '*')
                break;

            /* regular comment (recursive?) */
            colstate = IN_ADA_COMMENT2;
            i++;
        in_comment2:
            for (; i + 1 < n; i++) {
                if (str[i] == '*' && str[i + 1] == ')') {
                    i += 2;
                    colstate = 0;
                    break;
                }
            }
            SET_STYLE(sbuf, start, i, ADA_STYLE_COMMENT);
            continue;
        case '\'':
            if (i + 2 < n && str[i + 2] == '\'') {
                i += 2;
                SET_STYLE(sbuf, start, i, ADA_STYLE_STRING);
                continue;
            }
            break;
        case '\"':
            /* parse string or char const */
            while (i < n) {
                /* XXX: escape sequences? */
                if (str[i++] == c)
                    break;
            }
            SET_STYLE(sbuf, start, i, ADA_STYLE_STRING);
            continue;
        default:
            break;
        }
        /* parse numbers */
        if (qe_isdigit(c)) {
            for (; qe_isdigit_(str[i]); i++)
                continue;
            if (str[i] == '#') {
                /* binary, octal and hex literals */
                for (k = i + 1; qe_isalnum_(str[k]); k++)
                    continue;
                if (k == n)
                    i = k;
                else
                if (str[k] == '#')
                    i = k + 1;
            } else {
                if (str[i] == '.') {
                    i++;
                    for (; qe_isdigit_(str[i]); i++)
                        continue;
                }
                if (str[i] == 'e' || str[i] == 'E') {
                    k = i + 1;
                    if (str[k] == '+' || str[k] == '-')
                        k++;
                    if (qe_isdigit(str[k])) {
                        for (i = k + 1; qe_isdigit_(str[i]); i++)
                            continue;
                    }
                }
            }
            SET_STYLE(sbuf, start, i, ADA_STYLE_NUMBER);
            continue;
        }
        /* parse identifiers and keywords */
        if (qe_isalpha_(c)) {
            i += ustr_get_identifier_lc(kbuf, countof(kbuf), c, str, i, n);
            if (strfind(syn->keywords, kbuf)) {
                style = ADA_STYLE_KEYWORD;
            } else
            if (strfind(syn->types, kbuf)) {
                style = ADA_STYLE_TYPE;
            } else
            if (check_fcall(str, i))
                style = ADA_STYLE_FUNCTION;
            else
                style = ADA_STYLE_IDENTIFIER;

            SET_STYLE(sbuf, start, i, style);
            continue;
        }
    }
    cp->colorize_state = colstate;
}

static ModeDef ada_mode = {
    .name = "Ada",
    .extensions = "ada|adb|ads",
    .keywords = ada_keywords,
    .types = ada_types,
    .colorize_func = ada_colorize_line,
};

static int ada_init(QEmacsState *qs) {
    qe_register_mode(qs, &ada_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(ada_init);
