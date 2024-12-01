/*
 * Haskell language mode for QEmacs.
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

/*---------------- Haskell coloring ----------------*/

static char const haskell_keywords[] = {
    "|_|case|class|data|default|deriving|do|else|foreign"
    "|if|import|in|infix|infixl|infixr|instance|let"
    "|module|newtype|of|then|type|where|as|qualified"
    "|return"
    "|True|False"
};

static char const haskell_types[] = {
    //String|Int|Char|Bool
    "|"
};

enum {
    HASKELL_STYLE_TEXT =     QE_STYLE_DEFAULT,
    HASKELL_STYLE_COMMENT =  QE_STYLE_COMMENT,
    HASKELL_STYLE_PP_COMMENT = QE_STYLE_PREPROCESS,
    HASKELL_STYLE_STRING =   QE_STYLE_STRING,
    HASKELL_STYLE_NUMBER =   QE_STYLE_NUMBER,
    HASKELL_STYLE_KEYWORD =  QE_STYLE_KEYWORD,
    HASKELL_STYLE_FUNCTION = QE_STYLE_FUNCTION,
    HASKELL_STYLE_TYPE =     QE_STYLE_TYPE,
    HASKELL_STYLE_SYMBOL =   QE_STYLE_NUMBER,
};

enum {
    IN_HASKELL_COMMENT = 0x0F,
    IN_HASKELL_COMMENT_SHIFT = 0,
    IN_HASKELL_PP_COMMENT  = 0x10,  /* compiler directives {-# ... #-} */
    IN_HASKELL_STRING  = 0x20,
};

static inline int haskell_is_symbol(char32_t c)
{
    return qe_findchar("!#$%&+./<=>?@\\^|-~:", c);
}

static void haskell_colorize_line(QEColorizeContext *cp,
                                  const char32_t *str, int n,
                                  QETermStyle *sbuf, ModeDef *syn)
{
    int i = 0, start = i, style = 0, level = 0, klen;
    char32_t c, sep = 0;
    int state = cp->colorize_state;
    char kbuf[64];

    if (state & IN_HASKELL_COMMENT)
        goto parse_comment;

    if (state & IN_HASKELL_STRING) {
        sep = '\"';
        state = 0;
        i = cp_skip_blanks(str, i, n);
        if (str[i] == '\\')
            i++;
        goto parse_string;
    }

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '-':
            if (str[i] == '-' && !haskell_is_symbol(str[i + 1])) {
                i = n;
                style = HASKELL_STYLE_COMMENT;
                break;
            }
            goto parse_symbol;

        case '{':
            if (str[i] == '-') {
                state |= 1 << IN_HASKELL_COMMENT_SHIFT;
                i++;
                if (str[i] == '#') {
                    state |= IN_HASKELL_PP_COMMENT;
                    i++;
                }
            parse_comment:
                level = (state & IN_HASKELL_COMMENT) >> IN_HASKELL_COMMENT_SHIFT;
                style = HASKELL_STYLE_COMMENT;
                if (state & IN_HASKELL_PP_COMMENT)
                    style = HASKELL_STYLE_PP_COMMENT;
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
                            state &= ~IN_HASKELL_PP_COMMENT;
                            i++;
                            break;
                        }
                    }
                }
                state &= ~IN_HASKELL_COMMENT;
                state |= level << IN_HASKELL_COMMENT_SHIFT;
                break;
            }
            fallthrough;

        case '}':
        case '(':
        case ')':
        case '[':
        case ']':
        case ',':
        case ';':
        case '`':
            /* special */
            continue;

        case '\'':
        case '\"':
            /* parse string const */
            sep = c;
        parse_string:
            while (i < n) {
                c = str[i++];
                if (c == '\\') {
                    if (i == n) {
                        if (sep == '\"') {
                            /* XXX: should ignore whitespace */
                            state |= IN_HASKELL_STRING;
                        }
                    } else
                    if (str[i] == '^' && i + 1 < n && str[i + 1] != sep) {
                        i += 2;
                    } else {
                        i += 1;
                    }
                } else
                if (c == sep) {
                    state &= ~IN_HASKELL_STRING;
                    break;
                }
            }
            style = HASKELL_STYLE_STRING;
            break;

        default:
            if (qe_isdigit(c)) {
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
                    for (; qe_isdigit(str[i]); i++)
                        continue;
                    if (str[i] == '.' && qe_isdigit(str[i + 1])) {
                        /* decimal floats require a digit after the '.' */
                        for (i += 2; qe_isdigit(str[i]); i++)
                            continue;
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
                /* XXX: should detect malformed number constants */
                style = HASKELL_STYLE_NUMBER;
                break;
            }
            if (qe_isalpha_(c)) {
                for (klen = 0, i--; qe_isalnum_(str[i]) || str[i] == '\''; i++) {
                    if (klen < countof(kbuf) - 1)
                        kbuf[klen++] = str[i];
                }
                kbuf[klen] = '\0';

                if (strfind(syn->keywords, kbuf)) {
                    style = HASKELL_STYLE_KEYWORD;
                    break;
                }
                if (strfind(syn->types, kbuf)) {
                    style = HASKELL_STYLE_TYPE;
                    break;
                }
                if (check_fcall(str, i)) {
                    style = HASKELL_STYLE_FUNCTION;
                    break;
                }
                continue;
            }
        parse_symbol:
            if (haskell_is_symbol(c)) {
                for (; haskell_is_symbol(str[i]); i++)
                    continue;
                style = HASKELL_STYLE_SYMBOL;
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

static ModeDef haskell_mode = {
    .name = "Haskell",
    .extensions = "hs|haskell",
    .shell_handlers = "haskell",
    .keywords = haskell_keywords,
    .types = haskell_types,
    .colorize_func = haskell_colorize_line,
};

static int haskell_init(QEmacsState *qs)
{
    qe_register_mode(qs, &haskell_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(haskell_init);
