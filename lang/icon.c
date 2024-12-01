/*
 * Icon mode for QEmacs.
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
#include "clang.h"

static const char icon_keywords[] = {
    "break|by|case|create|default|do|else|end|every|fail|global|"
    "if|initial|invocable|link|local|next|not|of|procedure|"
    "record|repeat|return|static|suspend|then|to|until|while"
};

static const char icon_types[] = {
    "string|co-expression|table|integer|cset|procedure|set|"
    "real|list|"
};

static const char icon_directives[] = {
    "ifdef|ifndef|else|endif|include|define|undef|line|error|"
};

enum {
    ICON_STYLE_DEFAULT    = 0,
    ICON_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    ICON_STYLE_COMMENT    = QE_STYLE_COMMENT,
    ICON_STYLE_STRING     = QE_STYLE_STRING,
    ICON_STYLE_STRING_Q   = QE_STYLE_STRING_Q,
    ICON_STYLE_NUMBER     = QE_STYLE_NUMBER,
    ICON_STYLE_KEYWORD    = QE_STYLE_KEYWORD,
    ICON_STYLE_TYPE       = QE_STYLE_TYPE,
    ICON_STYLE_FUNCTION   = QE_STYLE_FUNCTION,
};

/* icon-mode colorization states */
enum {
    IN_ICON_STRING     = 0x04,  /* double-quoted string */
    IN_ICON_STRING_Q   = 0x08,  /* single-quoted string */
};

static void icon_colorize_line(QEColorizeContext *cp,
                               const char32_t *str, int n,
                               QETermStyle *sbuf, ModeDef *syn)
{
    int i, start, indent, state, style, klen;
    char32_t c, delim;
    char kbuf[64];

    i = cp_skip_blanks(str, 0, n);
    state = cp->colorize_state;
    indent = i;
    start = i;
    c = 0;
    style = ICON_STYLE_DEFAULT;

    if (i >= n)
        goto the_end;

    if (state) {
        /* if already in a state, go directly in the code parsing it */
        if (state & IN_ICON_STRING)
            goto parse_string;
        if (state & IN_ICON_STRING_Q)
            goto parse_string_q;
    }

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':       /* preprocessor */
            if ((i == 1 && str[i] == '!')
            ||  ustr_match_keyword(str + i, "line", &klen)) {
                i = n;
                style = ICON_STYLE_PREPROCESS;
                break;
            }
            i = n;
            style = ICON_STYLE_COMMENT;
            break;

        case '\'':      /* character constant */
        parse_string_q:
            state |= IN_ICON_STRING_Q;
            style = ICON_STYLE_STRING_Q;
            delim = '\'';
            goto string;

        case '\"':      /* string literal */
        parse_string:
            state |= IN_ICON_STRING;
            style = ICON_STYLE_STRING;
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
                    state &= ~(IN_ICON_STRING | IN_ICON_STRING_Q);
                    break;
                }
            }
            break;

        case '.':
            c = '0';
            i--;
            fallthrough;

        default:
            if (qe_isdigit(c)) {
                int real = 0;

                while (qe_isdigit(str[i]))
                    i++;
                if (str[i] == '.') {
                    i++;
                    real = 1;
                    while (qe_isdigit(str[i]))
                        i++;
                }
                if (str[i] == 'e' || str[i] == 'E') {
                    i++;
                    real = 1;
                    if (str[i] == '+' || str[i] == '-')
                        i++;
                    while (qe_isdigit(str[i]))
                        i++;
                }
                if (!real && (str[i] == 'r' || str[i] == 'R')) {
                    /* radix numbers */
                    while (qe_isalnum(str[i]))
                        i++;
                }
                style = ICON_STYLE_NUMBER;
                break;
            }
            if (qe_isalpha_(c) || (c == '$' && qe_isalnum_(str[i]))) {
                i += ustr_get_identifier(kbuf, countof(kbuf), c, str, i, n);

                if (start == indent && kbuf[0] == '$'
                &&  strfind(icon_directives, kbuf + 1)) {
                    style = ICON_STYLE_PREPROCESS;
                    break;
                }
                if (strfind(syn->keywords, kbuf)) {
                    style = ICON_STYLE_KEYWORD;
                    break;
                }
                if (check_fcall(str, i)) {
                    /* XXX: different styles for call and definition */
                    style = ICON_STYLE_FUNCTION;
                    break;
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
 the_end:
    /* set style on eol char */
    SET_STYLE1(sbuf, n, style);

    cp->colorize_state = state;
}

static ModeDef icon_mode = {
    .name = "Icon",
    .extensions = "icn",
    .shell_handlers = "iconc",
    .colorize_func = icon_colorize_line,
    .colorize_flags = CLANG_ICON,
    .keywords = icon_keywords,
    .types = icon_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

static int icon_init(QEmacsState *qs)
{
    qe_register_mode(qs, &icon_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(icon_init);
