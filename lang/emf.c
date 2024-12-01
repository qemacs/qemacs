/*
 * EMF colorizer mode for QEmacs.
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

#define MAX_KEYWORD_SIZE  16

/*---------------- EMF (JASSPA microemacs macro files) ----------------*/

static char const emf_keywords[] = {
    "define-macro|!emacro|!if|!elif|!else|!endif|!while|!done|"
    "!repeat|!until|!force|!return|!abort|!goto|!jump|!bell|"
};

static char const emf_types[] = {
    "|"
};

enum {
    EMF_STYLE_TEXT =       QE_STYLE_DEFAULT,
    EMF_STYLE_COMMENT =    QE_STYLE_COMMENT,
    EMF_STYLE_STRING =     QE_STYLE_STRING,
    EMF_STYLE_KEYWORD =    QE_STYLE_KEYWORD,
    EMF_STYLE_TYPE =       QE_STYLE_TYPE,
    EMF_STYLE_FUNCTION =   QE_STYLE_FUNCTION,
    EMF_STYLE_NUMBER =     QE_STYLE_NUMBER,
    EMF_STYLE_VARIABLE =   QE_STYLE_VARIABLE,
    EMF_STYLE_IDENTIFIER = QE_STYLE_DEFAULT,
    EMF_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
};

static void emf_colorize_line(QEColorizeContext *cp,
                              const char32_t *str, int n,
                              QETermStyle *sbuf, ModeDef *syn)
{
    char keyword[MAX_KEYWORD_SIZE];
    int i = 0, start, nw = 1, len, style;
    char32_t c;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '-':
            if (qe_isdigit(str[i]))
                goto number;
            break;
        case ';':
            i = n;
            SET_STYLE(sbuf, start, i, EMF_STYLE_COMMENT);
            continue;
        case '\"':
            /* parse string const */
            while (i < n) {
                if (str[i] == '\\' && i + 1 < n) {
                    i += 2; /* skip escaped char */
                    continue;
                }
                if (str[i++] == '\"')
                    break;
            }
            SET_STYLE(sbuf, start, i, EMF_STYLE_STRING);
            continue;
        default:
            break;
        }
        /* parse numbers */
        if (qe_isdigit(c)) {
        number:
            for (; i < n; i++) {
                if (!qe_isalnum(str[i]))
                    break;
            }
            SET_STYLE(sbuf, start, i, EMF_STYLE_NUMBER);
            continue;
        }
        /* parse identifiers and keywords */
        if (c == '$' || c == '!' || c == '#' || qe_isalpha_(c)) {
            len = 0;
            keyword[len++] = c;
            for (; qe_isalnum_(str[i]) || str[i] == '-'; i++) {
                if (len < countof(keyword) - 1)
                    keyword[len++] = str[i];
            }
            keyword[len] = '\0';
            if (c == '$' || c == '#') {
                style = EMF_STYLE_VARIABLE;
            } else
            if (strfind(syn->keywords, keyword)) {
                style = EMF_STYLE_KEYWORD;
            } else
            if (strfind(syn->types, keyword)) {
                style = EMF_STYLE_TYPE;
            } else
            if (nw++ == 1) {
                style = EMF_STYLE_FUNCTION;
            } else {
                style = EMF_STYLE_IDENTIFIER;
            }
            SET_STYLE(sbuf, start, i, style);
            continue;
        }
    }
}

static ModeDef emf_mode = {
    .name = "emf",
    .extensions = "emf",
    .keywords = emf_keywords,
    .types = emf_types,
    .colorize_func = emf_colorize_line,
};

static int emf_init(QEmacsState *qs)
{
    qe_register_mode(qs, &emf_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(emf_init);
