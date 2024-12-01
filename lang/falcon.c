/*
 * Falcon language mode for QEmacs.
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

/*---- Giancarlo Niccolai's Falcon scripting language -----*/

static const char falcon_keywords[] = {
    "if|elif|else|end|switch|select|case|default|"
    "while|loop|for|break|continue|dropping|"
    "forfirst|formiddle|forlast|"
    "try|catch|raise|state|callable|launch|"
    "function|return|innerfunc|fself|"
    "object|self|provides|from|init|"
    "class|const|enum|global|static|"
    "export|load|import|as|"
    "directive|def|macro|"
    "to|and|or|not|in|notin|true|false|nil|"
};

enum {
    IN_FALCON_COMMENT   = 0x01,     /* nested comment level */
    IN_FALCON_STRING1   = 0x10,     /* single quote */
    IN_FALCON_STRING2   = 0x20,     /* double quote */
};

enum {
    FALCON_STYLE_TEXT =     QE_STYLE_DEFAULT,
    FALCON_STYLE_SHBANG =   QE_STYLE_PREPROCESS,
    FALCON_STYLE_COMMENT =  QE_STYLE_COMMENT,
    FALCON_STYLE_STRING1 =  QE_STYLE_STRING,
    FALCON_STYLE_STRING2 =  QE_STYLE_STRING,
    FALCON_STYLE_NUMBER =   QE_STYLE_NUMBER,
    FALCON_STYLE_KEYWORD =  QE_STYLE_KEYWORD,
    FALCON_STYLE_TYPE =     QE_STYLE_TYPE,
    FALCON_STYLE_FUNCTION = QE_STYLE_FUNCTION,
};

static void falcon_colorize_line(QEColorizeContext *cp,
                                 const char32_t *str, int n,
                                 QETermStyle *sbuf, ModeDef *syn)
{
    int i = 0, start = i, style = 0;
    char32_t c;
    int state = cp->colorize_state;
    char kbuf[64];

    if (state & IN_FALCON_COMMENT)
        goto parse_comment;

    if (state & IN_FALCON_STRING1)
        goto parse_string1;

    if (state & IN_FALCON_STRING2)
        goto parse_string2;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            if (i == 1 && str[i] == '!') {
                i = n;
                style = FALCON_STYLE_SHBANG;
                break;
            }
            continue;

        case '/':
            if (str[i] == '*') {
                /* C block comments */
                i++;
                state |= IN_FALCON_COMMENT;
            parse_comment:
                for (; i < n; i++) {
                    if (str[i] == '*' && str[i + 1] == '/') {
                        i += 2;
                        state &= ~IN_FALCON_COMMENT;
                        break;
                    }
                }
                style = FALCON_STYLE_COMMENT;
                break;
            }
            if (str[i] == '/') {
                i = n;
                style = FALCON_STYLE_COMMENT;
                break;
            }
            continue;

        case '\'':
            /* a single quoted string literal */
            state = IN_FALCON_STRING1;
        parse_string1:
            while (i < n) {
                c = str[i++];
                if (c == '\\') {
                    if (i < n) {
                        i += 1;
                    }
                } else
                if (c == '\'') {
                    state &= ~IN_FALCON_STRING1;
                    break;
                }
            }
            style = FALCON_STYLE_STRING1;
            break;

        case '\"':
            /* parse double quoted string literal */
            state = IN_FALCON_STRING2;
        parse_string2:
            c = '\0';
            while (i < n) {
                c = str[i++];
                if (c == '\\') {
                    if (i < n) {
                        i += 1;
                    }
                } else
                if (c == '\"') {
                    state &= ~IN_FALCON_STRING2;
                    break;
                }
            }
            style = FALCON_STYLE_STRING1;
            break;

        case '.':
            if (qe_isdigit(str[i]))
                goto parse_decimal;
            continue;

        default:
            if (qe_isdigit(c)) {
                if (c == '0' && qe_tolower(str[i]) == 'x') {
                    /* hexadecimal numbers */
                    for (i += 1; qe_isxdigit(str[i]); i++)
                        continue;
                } else
                if (c == '0') {
                    /* octal numbers */
                    for (i += 1; qe_isoctdigit(str[i]); i++)
                        continue;
                } else {
                    /* decimal numbers */
                    for (; qe_isdigit(str[i]); i++)
                        continue;
                    if (str[i] == '.') {
                        i++;
                    parse_decimal:
                        for (; qe_isdigit(str[i]); i++)
                            continue;
                    }
                    if (qe_tolower(str[i]) == 'e') {
                        int k = i + 1;
                        if (str[k] == '+' || str[k] == '-')
                            k++;
                        if (qe_isdigit(str[k])) {
                            for (i = k + 1; qe_isdigit(str[i]); i++)
                                continue;
                        }
                    }
                }
                /* XXX: should detect malformed number constants */
                style = FALCON_STYLE_NUMBER;
                break;
            }
            if (qe_isalpha_(c) || c > 0xA0) {
                i += utf8_get_word(kbuf, countof(kbuf), c, str, i, n);
                if (strfind(syn->keywords, kbuf)) {
                    style = FALCON_STYLE_KEYWORD;
                    break;
                }
                if (check_fcall(str, i)) {
                    style = FALCON_STYLE_FUNCTION;
                    break;
                }
                if (qe_isupper(c) && (start == 0 || str[start-1] != '.')) {
                    /* Types are capitalized */
                    style = FALCON_STYLE_TYPE;
                    break;
                }
                continue;
            }
            continue;
        }
        if (style) {
            SET_STYLE(sbuf, start, i, style);
            style = 0;
        }
    }
    cp->colorize_state = state;
}

static ModeDef falcon_mode = {
    .name = "Falcon",
    .extensions = "fal",
    .shell_handlers = "falcon",
    .colorize_func = falcon_colorize_line,
    .keywords = falcon_keywords,
};

static int falcon_init(QEmacsState *qs)
{
    qe_register_mode(qs, &falcon_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(falcon_init);
