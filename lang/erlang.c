/*
 * Erlang language mode for QEmacs.
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

/*---------------- Erlang coloring ----------------*/

static char const erlang_keywords[] = {
    "|after|and|andalso|band|begin|bnot|bor|bsl|bsr|bxor|case|catch|cond"
    "|div|end|fun|if|let|not|of|or|orelse|receive|rem|try|when|xor"
    "|true|false|nil|_"
    "|"
};

static char const erlang_commands[] = {
    "|module|compile|define|export|import|vsn|on_load|record|include|file"
    "|mode|author|include_lib|behaviour"
    "|type|opaque|spec|callback|export_type"
    "|ifdef|ifndef|undef|else|endif"
    "|"
};

static char const erlang_types[] = {
    "|"
};

enum {
    IN_ERLANG_STRING   = 0x01,
};

enum {
    ERLANG_STYLE_TEXT       = QE_STYLE_DEFAULT,
    ERLANG_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    ERLANG_STYLE_COMMENT    = QE_STYLE_COMMENT,
    ERLANG_STYLE_STRING     = QE_STYLE_STRING,
    ERLANG_STYLE_CHARCONST  = QE_STYLE_STRING,
    ERLANG_STYLE_ATOM       = QE_STYLE_DEFAULT,
    ERLANG_STYLE_INTEGER    = QE_STYLE_NUMBER,
    ERLANG_STYLE_FLOAT      = QE_STYLE_NUMBER,
    ERLANG_STYLE_KEYWORD    = QE_STYLE_KEYWORD,
    ERLANG_STYLE_TYPE       = QE_STYLE_TYPE,
    ERLANG_STYLE_IDENTIFIER = QE_STYLE_DEFAULT,
    ERLANG_STYLE_FUNCTION   = QE_STYLE_FUNCTION,
};

int erlang_match_char(const char32_t *str, int i) {
    /* erlang character constant */
    if (str[i++] == '\\') {
        switch (str[i++]) {
        case '0': case '1': case '2': case '3':
        case '4': case '5': case '6': case '7':
            if (qe_isoctdigit(str[i])) i++;
            if (qe_isoctdigit(str[i])) i++;
            break;
        case 'x':
        case 'X':
            if (str[i] == '{') {
                for (i++; qe_isxdigit(str[i]); i++)
                    continue;
                if (str[i] == '}')
                    i++;
                break;
            }
            if (qe_isxdigit(str[i])) i++;
            if (qe_isxdigit(str[i])) i++;
            break;
        case '^':
            if (qe_isalpha(str[i])) i++;
            break;
        case 'b': /* backspace (8) */
        case 'd': /* delete (127) */
        case 'e': /* escape (27) */
        case 'f': /* formfeed (12) */
        case 'n': /* newline (10) */
        case 'r': /* return (13) */
        case 's': /* space (32) */
        case 't': /* tab (9) */
        case 'v': /* vtab (?) */
        case '\'': /* single quote */
        case '\"': /* double quote */
        case '\\': /* backslash */
            break;
        default:
            break;
        }
    }
    return i;
}

static void erlang_colorize_line(QEColorizeContext *cp,
                                 const char32_t *str, int n,
                                 QETermStyle *sbuf, ModeDef *syn)
{
    char keyword[16];
    int i = 0, start = i, style, len, base;
    char32_t c;
    int colstate = cp->colorize_state;

    if (colstate & IN_ERLANG_STRING)
        goto parse_string;

    if (str[i] == '#' && str[i + 1] == '!') {
        /* Handle shbang script heading ^#!.+
         * and preprocessor # line directives
         */
        i = n;
        SET_STYLE(sbuf, start, i, ERLANG_STYLE_PREPROCESS);
    }

    while (i < n) {
        start = i;
        style = ERLANG_STYLE_TEXT;
        c = str[i++];
        switch (c) {
        case '%':
            i = n;
            style = ERLANG_STYLE_COMMENT;
            SET_STYLE(sbuf, start, i, style);
            continue;
        case '$':
            i = erlang_match_char(str, i);
            style = ERLANG_STYLE_CHARCONST;
            SET_STYLE(sbuf, start, i, style);
            continue;

        case '\"':
            colstate = IN_ERLANG_STRING;
        parse_string:
            /* parse string */
            style = ERLANG_STYLE_STRING;
            while (i < n) {
                c = str[i++];
                if (c == '\\' && i < n)
                    i++;
                else
                if (c == '\"') {
                    colstate = 0;
                    break;
                }
            }
            SET_STYLE(sbuf, start, i, style);
            continue;
        case '\'':
            /* parse an Erlang atom */
            style = ERLANG_STYLE_ATOM;
            while (i < n) {
                c = str[i++];
                if (c == '\\' && i < n)
                    i++;
                else
                if (c == '\'') {
                    break;
                }
            }
            SET_STYLE(sbuf, start, i, style);
            continue;
        default:
            break;
        }
        if (qe_isdigit(c)) {
            /* parse numbers */
            style = ERLANG_STYLE_INTEGER;
            base = c - '0';
            while (qe_isdigit(str[i])) {
                base = base * 10 + str[i++] - '0';
            }
            if (base >= 2 && base <= 36 && str[i] == '#') {
                for (i += 1; qe_digit_value(str[i]) < base; i++)
                    continue;
                if (str[i - 1] == '#')
                    i--;
            } else {
                /* float: [0-9]+(.[0-9]+])?([eE][-+]?[0-9]+)? */
                if (str[i] == '.' && qe_isdigit(str[i + 1])) {
                    style = ERLANG_STYLE_FLOAT;
                    for (i += 2; qe_isdigit(str[i]); i++)
                        continue;
                }
                if (qe_tolower(str[i]) == 'e') {
                    int k = i + 1;
                    if (str[k] == '+' || str[k] == '-')
                        k++;
                    if (qe_isdigit(str[k])) {
                        style = ERLANG_STYLE_FLOAT;
                        for (i = k + 1; qe_isdigit(str[i]); i++)
                            continue;
                    }
                }
            }
            SET_STYLE(sbuf, start, i, style);
            continue;
        }
        if (qe_isalpha_(c) || c == '@') {
            /* parse an Erlang atom or identifier */
            len = 0;
            keyword[len++] = c;
            for (; qe_isalnum_(str[i]) || str[i] == '@'; i++) {
                if (len < countof(keyword) - 1)
                    keyword[len++] = str[i];
            }
            keyword[len] = '\0';
            if (start && str[start - 1] == '-'
            &&  strfind(erlang_commands, keyword)) {
                style = ERLANG_STYLE_PREPROCESS;
            } else
            if (strfind(syn->types, keyword)) {
                style = ERLANG_STYLE_TYPE;
            } else
            if (strfind(syn->keywords, keyword)) {
                style = ERLANG_STYLE_KEYWORD;
            } else
            if (check_fcall(str, i)) {
                style = ERLANG_STYLE_FUNCTION;
            } else
            if (qe_islower(keyword[0])) {
                style = ERLANG_STYLE_ATOM;
            } else {
                style = ERLANG_STYLE_IDENTIFIER;
            }
            SET_STYLE(sbuf, start, i, style);
            continue;
        }
    }
    cp->colorize_state = colstate;
}

static int erlang_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (match_extension(p->filename, mode->extensions)
    ||  match_shell_handler(cs8(p->buf), mode->shell_handlers)
    ||  strstr(cs8(p->buf), "-*- erlang -*-")) {
        return 80;
    }
    return 1;
}

static ModeDef erlang_mode = {
    .name = "Erlang",
    .extensions = "erl|hrl",
    .shell_handlers = "erlang",
    .mode_probe = erlang_mode_probe,
    .keywords = erlang_keywords,
    .types = erlang_types,
    .colorize_func = erlang_colorize_line,
};

static int erlang_init(QEmacsState *qs)
{
    qe_register_mode(qs, &erlang_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(erlang_init);
