/*
 * Elixir language mode for QEmacs.
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

/*---------------- Elixir coloring ----------------*/

static char const elixir_keywords[] = {
    "|do|end|cond|case|if|else|after|for|unless|when|quote|in"
    "|try|catch|rescue|raise"
    "|def|defp|defmodule|defcallback|defmacro|defsequence"
    "|defmacrop|defdelegate|defstruct|defexception|defimpl"
    "|require|alias|import|use|fn"
    "|setup|test|assert|refute|using"
    "|true|false|nil|and|or|not|_"
    "|"
};

static char const elixir_delim1[] = "\'\"/|([{<";
static char const elixir_delim2[] = "\'\"/|)]}>";

enum {
    IN_ELIXIR_DELIM  = 0x0F,
    IN_ELIXIR_STRING = 0x10,
    IN_ELIXIR_REGEX  = 0x20,
    IN_ELIXIR_TRIPLE = 0x40,
};

enum {
    ELIXIR_STYLE_TEXT =       QE_STYLE_DEFAULT,
    ELIXIR_STYLE_COMMENT =    QE_STYLE_COMMENT,
    ELIXIR_STYLE_CHARCONST =  QE_STYLE_STRING,
    ELIXIR_STYLE_STRING =     QE_STYLE_STRING,
    ELIXIR_STYLE_HEREDOC =    QE_STYLE_STRING,
    ELIXIR_STYLE_REGEX =      QE_STYLE_STRING,
    ELIXIR_STYLE_NUMBER =     QE_STYLE_NUMBER,
    ELIXIR_STYLE_KEYWORD =    QE_STYLE_KEYWORD,
    ELIXIR_STYLE_ATOM =       QE_STYLE_TYPE,
    ELIXIR_STYLE_TAG =        QE_STYLE_VARIABLE,
    ELIXIR_STYLE_FUNCTION =   QE_STYLE_FUNCTION,
    ELIXIR_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
};

static void elixir_colorize_line(QEColorizeContext *cp,
                                 const char32_t *str, int n,
                                 QETermStyle *sbuf, ModeDef *syn)
{
    int i = 0, start = i, style = 0, klen, nc, has_under;
    char32_t c, sep;
    int state = cp->colorize_state;
    char kbuf[64];

    if (state & IN_ELIXIR_STRING)
        goto parse_string;
    if (state & IN_ELIXIR_REGEX)
        goto parse_regex;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            i = n;
            style = ELIXIR_STYLE_COMMENT;
            break;

        case '?':
            i = erlang_match_char(str, i);
            style = ELIXIR_STYLE_CHARCONST;
            break;

        case '~':
            if (qe_tolower(str[i]) == 'r') {
                nc = qe_indexof(elixir_delim1, str[i + 1]);
                if (nc >= 0) {
                    i += 2;
                    state = IN_ELIXIR_REGEX | nc;
                    if (nc < 2) { /* '\'' or '\"' */
                        if (str[i] == c && str[i + 1] == c) {
                            state |= IN_ELIXIR_TRIPLE;
                            i += 2;
                        }
                    }
                parse_regex:
                    sep = elixir_delim2[state & 15];
                    /* parse regular expression */
                    while (i < n) {
                        if ((c = str[i++]) == '\\') {
                            if (i < n)
                                i += 1;
                            continue;
                        }
                        if (c == sep) {
                            if (!(state & IN_ELIXIR_TRIPLE)) {
                                state = 0;
                                break;
                            }
                            if (str[i] == sep && str[i + 1] == sep) {
                                i += 2;
                                state = 0;
                                break;
                            }
                        }
                    }
                    while (qe_islower(str[i])) {
                        /* regex suffix */
                        i++;
                    }
                    style = ELIXIR_STYLE_REGEX;
                    break;
                }
            }
            continue;

        case '\'':
        case '\"':
            /* parse string constants and here documents */
            state = IN_ELIXIR_STRING | (c == '\"');
            if (str[i] == c && str[i + 1] == c) {
                /* here documents */
                state |= IN_ELIXIR_TRIPLE;
                i += 2;
            }
        parse_string:
            sep = elixir_delim2[state & 15];
            style = (state & IN_ELIXIR_TRIPLE) ?
                ELIXIR_STYLE_HEREDOC : ELIXIR_STYLE_STRING;
            while (i < n) {
                c = str[i++];
                if (c == '\\') {
                    if (i < n)
                        i += 1;
                    continue;
                }
                /* XXX: should colorize <% %> expressions and interpolation */
                if (c == sep) {
                    if (!(state & IN_ELIXIR_TRIPLE)) {
                        state = 0;
                        break;
                    }
                    if (str[i] == sep && str[i + 1] == sep) {
                        i += 2;
                        state = 0;
                        break;
                    }
                }
            }
            break;

        case '@':
        case ':':
            if (qe_isalpha(str[i]))
                goto has_alpha;
            continue;

        case '<':
            if (str[i] == '%') {
                i++;
                if (str[i] == '=')
                    i++;
                style = ELIXIR_STYLE_PREPROCESS;
                break;
            }
            continue;

        case '%':
            if (str[i] == '>') {
                i++;
                style = ELIXIR_STYLE_PREPROCESS;
                break;
            }
            continue;

        default:
            if (qe_isdigit(c)) {
                if (c == '0' && qe_tolower(str[i]) == 'b') {
                    /* binary numbers */
                    for (i += 1; qe_isbindigit(str[i]); i++)
                        continue;
                } else
                if (c == '0' && qe_tolower(str[i]) == 'o') {
                    /* octal numbers */
                    for (i += 1; qe_isoctdigit(str[i]); i++)
                        continue;
                } else
                if (c == '0' && qe_tolower(str[i]) == 'x') {
                    /* hexadecimal numbers */
                    for (i += 1; qe_isxdigit(str[i]); i++)
                        continue;
                } else {
                    /* decimal numbers */
                    for (has_under = 0;; i++) {
                        if (qe_isdigit(str[i]))
                            continue;
                        if (str[i] == '_' && qe_isdigit(str[i + 1])) {
                            /* integers may contain embedded _ characters */
                            has_under = 1;
                            i++;
                            continue;
                        }
                        break;
                    }
                    if (!has_under && str[i] == '.' && qe_isdigit(str[i + 1])) {
                        i += 2;
                        /* decimal floats require a digit after the '.' */
                        for (; qe_isdigit(str[i]); i++)
                            continue;
                        /* exponent notation requires a decimal point */
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
                }
                style = ELIXIR_STYLE_NUMBER;
                break;
            }
            if (qe_isalpha_(c)) {
        has_alpha:
                klen = 0;
                kbuf[klen++] = c;
                for (; qe_isalnum_(c = str[i]); i++) {
                    if (klen < countof(kbuf) - 1)
                        kbuf[klen++] = c;
                }
                if (c == '!' || c == '?') {
                    i++;
                    if (klen < countof(kbuf) - 1)
                        kbuf[klen++] = c;
                }
                kbuf[klen] = '\0';

                style = 0;
                if (kbuf[0] == '@') {
                    style = ELIXIR_STYLE_PREPROCESS;
                } else
                if (kbuf[0] == ':') {
                    style = ELIXIR_STYLE_ATOM;
                } else
                if (strfind(syn->keywords, kbuf)) {
                    style = ELIXIR_STYLE_KEYWORD;
                } else
                if (c == ':') {
                    style = ELIXIR_STYLE_TAG;
                } else
                if (check_fcall(str, i)) {
                    style = ELIXIR_STYLE_FUNCTION;
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
    cp->colorize_state = state;
}

static ModeDef elixir_mode = {
    .name = "Elixir",
    .extensions = "ex|exs",
    .shell_handlers = "elixir",
    .keywords = elixir_keywords,
    .colorize_func = elixir_colorize_line,
};

static int elixir_init(QEmacsState *qs)
{
    qe_register_mode(qs, &elixir_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(elixir_init);
