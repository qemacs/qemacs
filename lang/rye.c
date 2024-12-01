/*
 * Rye syntax mode for QEmacs.
 *
 * Copyright (c) 2024 Charlie Gordon.
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

/*---------------- Rye: Refaktor's homoiconic dynamic programming language ----------------*/

static const char rye_keywords[] = {
    // Printing functions
    "print|prn|prns|print\\val|probe|"
    // Logic functions
    "true|false|not|and|or|xor|all|any|"
    // Working with numbers
    "inc|is-positive|is-zero|factor-of|odd|even|mod|"
    // Working with strings
    "capitalize|to-lower|to-upper|join|join\\with|split|split\\quoted|split\\every|"
    // Conditional functions
    "if|otherwise|^if|^otherwise|either|switch|cases|"
    // Looping functions
    "loop|for|forever|forever\\with|"
    // Doers and evaluators
    "do|do-in|with|try|do-in\\try|vals|vals\\with|time-it|"
    // Function creating functions
    "does|fn|fn1|pfn|closure|"
    /* language keywords (sortof) */
    "section|group|do\\in|fn\\in|"
    "extends|private|isolate|cc|ccp|import|rye|"
    "return|"
    /* operators */
    "equal|"
    /* literals */
    "stdout|newline|true|false|"
};

static const char rye_types[] = {
    "object|group|context|"
};

enum {
    RYE_STYLE_DEFAULT    = 0,
    RYE_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    RYE_STYLE_COMMENT    = QE_STYLE_COMMENT,
    RYE_STYLE_STRING     = QE_STYLE_STRING,
    RYE_STYLE_STRING_Q   = QE_STYLE_STRING_Q,
    RYE_STYLE_NUMBER     = QE_STYLE_NUMBER,
    RYE_STYLE_KEYWORD    = QE_STYLE_KEYWORD,
    RYE_STYLE_TYPE       = QE_STYLE_TYPE,
    RYE_STYLE_FUNCTION   = QE_STYLE_FUNCTION,
    RYE_STYLE_VARIABLE   = QE_STYLE_VARIABLE,
};

enum {
    IN_RYE_STRING        = 1,  /* back-quoted string */
};

/* XXX: recognize different literals:
   1                   ; integer number
   3.14                ; decimal number
   "Jane"              ; string
   jane@example.com    ; email
   https://ryelang.org ; uri
   %foo/readme.md      ; file path
   blue                ; word
   context/word        ; cpath (context path)
 */

static int is_rye_identifier_start(char32_t c) {
    return !qe_isalpha(c);
}

static int get_rye_identifier(char *dest, int size, char32_t c,
                              const char32_t *str, int i, int n)
{
    /*@API utils
       Grab a rye identifier from a char32_t buffer, accept non-ASCII identifiers
       and encode in UTF-8.
       @argument `dest` a pointer to the destination array
       @argument `size` the length of the destination array in bytes
       @argument `c` the initial code point or `0` if none
       @argument `str` a valid pointer to an array of codepoints
       @argument `i` the index to the next codepoint
       @argument `n` the length of the codepoint array
       @return the number of codepoints used in the source array.
       @note `dest` can be a null pointer if `size` is `0`.
     */
    int pos = 0, j = i;

    if (c == 0) {
        if (!(j < n && is_rye_identifier_start(c = str[j++]))) {
            if (size > 0)
                *dest = '\0';
            return 0;
        }
    }

    for (;; j++) {
        if (c < 128) {
            if (pos < size - 1) {
                dest[pos++] = c;
            }
        } else {
            char buf[6];
            int i1, len = utf8_encode(buf, c);
            for (i1 = 0; i1 < len; i1++) {
                if (pos < size - 1) {
                    dest[pos++] = buf[i1];
                }
            }
        }
        if (j >= n)
            break;
        c = str[j];
        if (qe_isblank(c))
            break;
    }
    if (pos < size)
        dest[pos] = '\0';
    return j - i;
}

static void rye_colorize_line(QEColorizeContext *cp,
                              const char32_t *str, int n,
                              QETermStyle *sbuf, ModeDef *syn)
{
    int i = 0, start;
    int style;
    char32_t c, delim;
    char kbuf[64];
    int state = cp->colorize_state;

    start = i;
    c = 0;
    style = 0;
    kbuf[0] = '\0';

    if (state) {
        /* if already in a state, go directly in the code parsing it */
        if (state & IN_RYE_STRING)
            goto parse_string;
    }

    while (i < n) {
        start = i;
        c = str[i++];

        switch (c) {
        case ' ':
        case '\t':
            continue;
        case ';':
            style = RYE_STYLE_COMMENT;
            i = n;
            break;
        case '"':      /* string literal */
            style = RYE_STYLE_STRING;
            delim = '\"';
            goto string;
        case '`':      /* string literal */
            state |= IN_RYE_STRING;
        parse_string:
            style = RYE_STYLE_STRING_Q;
            delim = '`';
        string:
            while (i < n) {
                c = str[i++];
                if (c == delim) {
                    state &= ~IN_RYE_STRING;
                    break;
                }
            }
            break;
        case '#':
            if (start == 0 && str[i] == '!') {
                /* recognize a shebang comment line */
                style = RYE_STYLE_PREPROCESS;
                i = n;
                break;
            }
            /* FALLTHROUGH */
        default:
            if (qe_isdigit(c) || (c == '-' && qe_isdigit(str[i]))) {
                /* XXX: rye does not yet support hex, binary or exponential notations */
                while (qe_isalnum(str[i]) ||
                       (str[i] == '.' && qe_isdigit(str[i + 1])) ||
                       ((str[i] == '+' || str[i] == '-') &&
                        qe_tolower(str[i - 1]) == 'e' &&
                        qe_isdigit(str[i + 1])))
                {
                    i++;
                }
                style = RYE_STYLE_NUMBER;
                break;
            }
            if (is_rye_identifier_start(c) || is_rye_identifier_start(str[i])) {
                i += get_rye_identifier(kbuf, countof(kbuf), c, str, i, n);
                if (cp->state_only)
                    continue;
                switch (*kbuf) {
                case '\'': style = RYE_STYLE_PREPROCESS; break; /* lit word */
                case ':':  style = RYE_STYLE_VARIABLE;   break; /* lset word */
                case '.':  style = RYE_STYLE_FUNCTION;   break; /* op word */
                case '?':  style = RYE_STYLE_PREPROCESS; break; /* get word */
                case '|':  style = RYE_STYLE_FUNCTION;   break; /* pipe word */
                }
                if (style)
                    break;
                if (str[i - 1] == ':') {
                    style = RYE_STYLE_VARIABLE;     /* rset word */
                    break;
                }
                if (strfind(syn->keywords, kbuf)) {
                    style = RYE_STYLE_KEYWORD;
                    break;
                }
                if (strfind(syn->types, kbuf)) {
                    style = RYE_STYLE_TYPE;
                    break;
                }
                continue;
            }
            continue;
        }
        if (style) {
            if (!cp->state_only) {
                SET_STYLE(sbuf, start, i, style);
            }
            style = 0;
        }
    }

    if (state & IN_RYE_STRING) {
        /* set style on eol char */
        SET_STYLE1(sbuf, n, style);
    }
    cp->colorize_state = state;
}

static ModeDef rye_mode = {
    .name = "Rye",
    .extensions = "rye",
    .shell_handlers = "rye",
    .colorize_func = rye_colorize_line,
    .keywords = rye_keywords,
    .types = rye_types,
    .indent_func = c_indent_line,   // not really appropriate
    .auto_indent = 1,
    .fallback = &c_mode,
};

static int rye_init(QEmacsState *qs) {
    qe_register_mode(qs, &rye_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(rye_init);
