/*
 * Tiger language mode for QEmacs.
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

/*---------------- Tiger script coloring ----------------*/

static char const tiger_keywords[] = {
    "|array|break|do|else|end|for|function|if|in|let|nil|of|then|to|type|var|while"
    "|"
};

static char const tiger_types[] = {
    "|int|string"
    "|"
};

#if 0
static char const tiger_tokens[] = {
    "(|)|[|]|{|}|:|:=|.|,|;|*|/|+|-|=|<>|>|<|>=|<=|&||."
};
#endif

enum {
    IN_TIGER_COMMENT = 0x01,
    IN_TIGER_COMMENT_MASK = 0x0F,
    IN_TIGER_STRING  = 0x10,
    IN_TIGER_STRING2 = 0x20,
};

enum {
    TIGER_STYLE_TEXT =     QE_STYLE_DEFAULT,
    TIGER_STYLE_COMMENT =  QE_STYLE_COMMENT,
    TIGER_STYLE_STRING =   QE_STYLE_STRING,
    TIGER_STYLE_NUMBER =   QE_STYLE_NUMBER,
    TIGER_STYLE_KEYWORD =  QE_STYLE_KEYWORD,
    TIGER_STYLE_TYPE =     QE_STYLE_TYPE,
    TIGER_STYLE_FUNCTION = QE_STYLE_FUNCTION,
};

static void tiger_colorize_line(QEColorizeContext *cp,
                                const char32_t *str, int n,
                                QETermStyle *sbuf, ModeDef *syn)
{
    int i = 0, j, start = i;
    char32_t c, sep = 0;
    int state = cp->colorize_state;
    char kbuf[64];

    if (state & IN_TIGER_COMMENT_MASK) {
        goto parse_comment;
    }
    if (state & IN_TIGER_STRING2) {
        goto parse_string2;
    }
    if (state & IN_TIGER_STRING) {
        goto parse_string;
    }

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '/':
            if (str[i] == '*') {
                /* normal comment */
                i++;
                state |= IN_TIGER_COMMENT;
            parse_comment:
                for (; i < n; i++) {
                    if (str[i] == '*' && str[i + 1] == '/') {
                        i += 2;
                        state -= IN_TIGER_COMMENT;
                        if (!(state & IN_TIGER_COMMENT_MASK))
                            break;
                    } else
                    if (str[i] == '/' && str[i + 1] == '*') {
                        i += 2;
                        if ((state & IN_TIGER_COMMENT_MASK) != IN_TIGER_COMMENT_MASK)
                            state += IN_TIGER_COMMENT;
                    }
                }
                SET_STYLE(sbuf, start, i, TIGER_STYLE_COMMENT);
                continue;
            }
            break;
        case '\"':
            /* parse string const */
            sep = c;
            state |= IN_TIGER_STRING;
        parse_string:
            while (i < n) {
                c = str[i++];
                if (c == '\\') {
                    if (i == n) {
                        state |= IN_TIGER_STRING2;
                        break;
                    }
                    if (str[i] == ' ') {
                        state |= IN_TIGER_STRING2;
                    parse_string2:
                        while (i < n && str[i] == ' ')
                            i++;
                        if (i == n)
                            break;
                        if (str[i] == '\\')
                            i++;
                        state &= ~IN_TIGER_STRING2;
                    } else {
                        i += 1;
                    }
                } else
                if (c == sep) {
                    state &= ~IN_TIGER_STRING;
                    break;
                }
            }
            SET_STYLE(sbuf, start, i, TIGER_STYLE_STRING);
            continue;
        default:
            if (qe_isdigit(c)) {
                for (; i < n; i++) {
                    if (!qe_isdigit(str[i]))
                        break;
                }
                SET_STYLE(sbuf, start, i, TIGER_STYLE_NUMBER);
                continue;
            }
            if (qe_isalpha(c)) {
                i += ustr_get_identifier(kbuf, countof(kbuf), c, str, i, n);
                if (strfind(syn->keywords, kbuf)) {
                    SET_STYLE(sbuf, start, i, TIGER_STYLE_KEYWORD);
                    continue;
                }
                if (strfind(syn->types, kbuf)) {
                    SET_STYLE(sbuf, start, i, TIGER_STYLE_TYPE);
                    continue;
                }
                for (j = i; j < n && qe_isspace(str[j]); j++)
                    continue;
                /* function calls use parenthesized argument list or
                   single string or table literal */
                if (str[j] == '(') {
                    SET_STYLE(sbuf, start, i, TIGER_STYLE_FUNCTION);
                    continue;
                }
                continue;
            }
            break;
        }
    }
    cp->colorize_state = state;
}

static ModeDef tiger_mode = {
    .name = "Tiger",
    .extensions = "tiger|tig",
    .shell_handlers = "tiger",
    .keywords = tiger_keywords,
    .types = tiger_types,
    .colorize_func = tiger_colorize_line,
};

static int tiger_init(QEmacsState *qs)
{
    qe_register_mode(qs, &tiger_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(tiger_init);
