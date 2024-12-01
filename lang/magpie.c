/*
 * Magpie language mode for QEmacs.
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

/*---------------- Magpie coloring ----------------*/

static char const magpie_keywords[] = {
    "and|as|break|case|catch|defclass|def|do|else|end|fn|for|if|"
    "import|in|is|let|match|or|return|then|throw|var|val|while|with|"
    "not|native|namespace|class|struct|using|new|interface|"
    "get|set|shared|done|"
    "false|true|nothing|it|xor|"
};

enum {
    IN_MAGPIE_COMMENT   = 0x0F,     /* nested comment level */
    IN_MAGPIE_STRING    = 0x10,     /* double quote */
};

enum {
    MAGPIE_STYLE_TEXT =     QE_STYLE_DEFAULT,
    MAGPIE_STYLE_SHBANG =   QE_STYLE_PREPROCESS,
    MAGPIE_STYLE_COMMENT =  QE_STYLE_COMMENT,
    MAGPIE_STYLE_STRING =   QE_STYLE_STRING,
    MAGPIE_STYLE_CHAR =     QE_STYLE_STRING,
    MAGPIE_STYLE_NUMBER =   QE_STYLE_NUMBER,
    MAGPIE_STYLE_KEYWORD =  QE_STYLE_KEYWORD,
    MAGPIE_STYLE_TYPE =     QE_STYLE_TYPE,
    MAGPIE_STYLE_FUNCTION = QE_STYLE_FUNCTION,
};

static void magpie_colorize_line(QEColorizeContext *cp,
                                 const char32_t *str, int n,
                                 QETermStyle *sbuf, ModeDef *syn)
{
    int i = 0, start = i, style = 0, level;
    char32_t c;
    int state = cp->colorize_state;
    char kbuf[64];

    if ((level = state & IN_MAGPIE_COMMENT) != 0)
        goto parse_comment;

    if (state & IN_MAGPIE_STRING)
        goto parse_string;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            if (i == 1 && str[i] == '!') {
                i = n;
                style = MAGPIE_STYLE_SHBANG;
                break;
            }
            i = n;
            style = MAGPIE_STYLE_COMMENT;   /* probably incorrect */
            break;

        case '/':
            if (str[i] == '*') {
                /* nexted C block comments */
                i++;
                level = 1;
            parse_comment:
                for (; i < n; i++) {
                    if (str[i] == '*' && str[i + 1] == '/') {
                        i += 2;
                        if (--level == 0)
                            break;
                    } else
                    if (str[i] == '/' && str[i + 1] == '*') {
                        i += 2;
                        level++;
                    }
                }
                state &= ~IN_MAGPIE_COMMENT;
                state |= level & IN_MAGPIE_COMMENT;
                style = MAGPIE_STYLE_COMMENT;
                break;
            }
            if (str[i] == '/') {
                i = n;
                style = MAGPIE_STYLE_COMMENT;
                break;
            }
            continue;

        case '\'':
            /* a single quoted character */
            while (i < n) {
                c = str[i++];
                if (c == '\\') {
                    if (i < n) {
                        i += 1;
                    }
                } else
                if (c == '\'') {
                    break;
                }
            }
            style = MAGPIE_STYLE_CHAR;
            break;

        case '\"':
            /* parse double quoted string const */
            state = IN_MAGPIE_STRING;
        parse_string:
            c = '\0';
            while (i < n) {
                c = str[i++];
                if (c == '\\') {
                    if (i < n) {
                        i += 1;
                    }
                } else
                if (c == '#' && str[i] == '{') {
                    /* should parse full syntax */
                    while (i < n && str[i++] != '}')
                        continue;
                } else
                if (c == '\"') {
                    state = 0;
                    break;
                }
            }
            style = MAGPIE_STYLE_STRING;
            break;

        case '.':
            if (qe_isdigit_(str[i]))
                goto parse_decimal;
            continue;

        default:
            if (qe_isdigit(c)) {
                /* decimal numbers */
                for (; qe_isdigit_(str[i]); i++)
                    continue;
                if (str[i] == '.') {
                    i++;
                parse_decimal:
                    for (; qe_isdigit_(str[i]); i++)
                        continue;
                }
                style = MAGPIE_STYLE_NUMBER;
                break;
            }
            if (qe_isalpha_(c)) {
                i += ustr_get_identifier(kbuf, countof(kbuf), c, str, i, n);
                if (strfind(syn->keywords, kbuf)) {
                    style = MAGPIE_STYLE_KEYWORD;
                    break;
                }
                if (qe_isblank(str[i]))
                    i++;
                if (str[i] == '(' || str[i] == '{') {
                    style = MAGPIE_STYLE_FUNCTION;
                    break;
                }
                if (qe_isupper(kbuf[0]) && (start == 0 || str[start-1] != '.')) {
                    /* Types are capitalized */
                    style = MAGPIE_STYLE_TYPE;
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

static ModeDef magpie_mode = {
    .name = "Magpie",
    .extensions = "mag",
    .shell_handlers = "magpie",
    .keywords = magpie_keywords,
    .colorize_func = magpie_colorize_line,
};

static int magpie_init(QEmacsState *qs)
{
    qe_register_mode(qs, &magpie_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(magpie_init);
