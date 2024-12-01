/*
 * Elm mode for QEmacs.
 *
 * Copyright (c) 2015-2024 Charlie Gordon.
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

/*---------------- Elm coloring ----------------*/

static char const elm_keywords[] = {
    /* Elm keywords */
    "if|then|else|case|of|let|in|type|"
    "module|where|import|as|hiding|exposing|port|export|foreign|"
    "perform|deriving|var|"
    /* operators */
    "not|"
    /* predefined constants */
    "False|True|_|"
};

static char const elm_types[] = {
    "number|"
    //"Bool|Char|String|Int|Float|" // matched generically
};

enum {
    ELM_STYLE_DEFAULT    = 0,
    ELM_STYLE_COMMENT    = QE_STYLE_COMMENT,
    ELM_STYLE_PP_COMMENT = QE_STYLE_PREPROCESS,
    ELM_STYLE_STRING     = QE_STYLE_STRING,
    ELM_STYLE_STRING_Q   = QE_STYLE_STRING_Q,
    ELM_STYLE_NUMBER     = QE_STYLE_NUMBER,
    ELM_STYLE_KEYWORD    = QE_STYLE_KEYWORD,
    ELM_STYLE_TYPE       = QE_STYLE_TYPE,
    ELM_STYLE_FUNCTION   = QE_STYLE_FUNCTION,
};

/* elm-mode colorization states */
enum {
    IN_ELM_COMMENT     = 0x0F,  /* multiline comment (nested) */
    IN_ELM_COMMENT_SHIFT = 0,
    IN_ELM_PP_COMMENT  = 0x10,  /* compiler directives {-# ... #-} */
    IN_ELM_STRING      = 0x20,  /* double-quoted string */
    IN_ELM_LONG_STRING = 0x40,  /* double-quoted multiline string */
    IN_ELM_STRING_Q    = 0x80,  /* single-quoted string */
};

static void elm_colorize_line(QEColorizeContext *cp,
                              const char32_t *str, int n,
                              QETermStyle *sbuf, ModeDef *syn)
{
    int i = 0, start = i, style = 0, level, klen;
    char32_t c = 0, delim;
    int state = cp->colorize_state;
    char kbuf[64];

    if (state) {
        /* if already in a state, go directly in the code parsing it */
        if (state & IN_ELM_COMMENT)
            goto parse_comment;
        if (state & IN_ELM_STRING)
            goto parse_string;
        if (state & IN_ELM_LONG_STRING)
            goto parse_long_string;
        if (state & IN_ELM_STRING_Q)
            goto parse_string_q;
    }

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '-':
            if (str[i] == '-') {
                /* line comment */
                i = n;
                style = ELM_STYLE_COMMENT;
                break;
            }
            continue;

        case '{':
            if (str[i] == '-') {
                /* multi-line nested (!) comment */
                state |= 1 << IN_ELM_COMMENT_SHIFT;
                i++;
                if (str[i] == '#') {
                    state |= IN_ELM_PP_COMMENT;
                    i++;
                }
            parse_comment:
                level = (state & IN_ELM_COMMENT) >> IN_ELM_COMMENT_SHIFT;
                style = ELM_STYLE_COMMENT;
                if (state & IN_ELM_PP_COMMENT)
                    style = ELM_STYLE_PP_COMMENT;
                while (i < n) {
                    c = str[i++];
                    if (c == '{' && str[i] == '-') {
                        level++;
                        i++;
                        continue;
                    }
                    if (c == '-' && str[i] == '}') {
                        i++;
                        level--;
                        if (level == 0) {
                            state &= ~IN_ELM_PP_COMMENT;
                            i++;
                            break;
                        }
                    }
                }
                state &= ~IN_ELM_COMMENT;
                state |= level << IN_ELM_COMMENT_SHIFT;
                break;
            }
            continue;

        case '\'':      /* character constant */
        parse_string_q:
            state |= IN_ELM_STRING_Q;
            style = ELM_STYLE_STRING_Q;
            delim = '\'';
            goto string;

        case '\"':      /* string literal */
            state |= IN_ELM_STRING;
            if (str[i] == '"' && str[i + 1] == '"') {
                state ^= IN_ELM_STRING | IN_ELM_LONG_STRING;
            parse_long_string:
                style = ELM_STYLE_STRING;
                while (i < n) {
                    c = str[i++];
                    if (c == '\\') {
                        if (i >= n)
                            break;
                        i++;
                    } else
                    if (c == '"' && str[i] == '"' && str[i + 1] == '"') {
                        state &= ~IN_ELM_LONG_STRING;
                        break;
                    }
                }
                break;
            }
        parse_string:
            style = ELM_STYLE_STRING;
            delim = '\"';
        string:
            while (i < n) {
                c = str[i++];
                if (c == '\\') {
                    if (i >= n)
                        break;
                    i++;
                } else
                if (c == delim) {
                    state &= ~(IN_ELM_STRING | IN_ELM_STRING_Q);
                    break;
                }
            }
            break;

        default:
            if (qe_isdigit(c)) {
                int j;
                // Integers:
                // 0x[0-9a-fA-F]+
                // [0-9]+
                // Floats:
                // [0-9]+\.[0-9]*([eE][-\+]?[0-9]+)?
                // [0-9]+(\.[0-9]*)?[eE][-\+]?[0-9]+
                if (c == '0' && str[i] == 'x' && qe_isxdigit(str[i + 1])) {
                    for (i += 3; qe_isxdigit(str[i]); i++)
                        continue;
                } else {
                    while (qe_isdigit(str[i]))
                        i++;
                    if (str[i] == '.' && qe_isdigit(str[i + 1])) {
                        for (i += 2; qe_isdigit(str[i]); i++)
                            continue;
                    }
                    if (str[i] == 'e' || str[i] == 'E') {
                        j = i + 1;
                        if (str[j] == '+' || str[j] == '-')
                            j++;
                        if (qe_isdigit(str[j])) {
                            for (i = j + 1; qe_isdigit(str[i]); i++)
                                continue;
                        }
                    }
                }
                style = ELM_STYLE_NUMBER;
                break;
            }
            if (qe_isalpha_(c)) {
                int haslower = 0;
                for (klen = 0, i--; qe_isalnum_(str[i]) || str[i] == '\''; i++) {
                    haslower |= qe_islower(str[i]);
                    if (klen < countof(kbuf) - 1)
                        kbuf[klen++] = str[i];
                }
                kbuf[klen] = '\0';

                if (strfind(syn->keywords, kbuf)) {
                    style = ELM_STYLE_KEYWORD;
                    break;
                }

                if ((start == 0 || str[start - 1] != '.')
                &&  (str[i] != '.')) {
                    if (strfind(syn->types, kbuf)
                    ||  (qe_isupper(c) && haslower)) {
                        style = ELM_STYLE_TYPE;
                        break;
                    }
                }
#if 0
                if (check_fcall(str, i)) {
                    style = ELM_STYLE_FUNCTION;
                    break;
                }
#endif
                continue;
            }
            continue;
        }
        if (style) {
            SET_STYLE(sbuf, start, i, style);
            style = 0;
        }
    }
    /* set style on eol char */
    SET_STYLE1(sbuf, n, style);    /* set style on eol char */

    cp->colorize_state = state;
}

static ModeDef elm_mode = {
    .name = "Elm",
    .extensions = "elm",
    .keywords = elm_keywords,
    .types = elm_types,
    .colorize_func = elm_colorize_line,
};

static int elm_init(QEmacsState *qs)
{
    qe_register_mode(qs, &elm_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(elm_init);
