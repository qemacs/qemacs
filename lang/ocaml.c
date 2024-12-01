/*
 * ML/Ocaml language mode for QEmacs.
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

/*---------------- ML/Ocaml coloring ----------------*/

static char const ocaml_keywords[] = {
    "|_|and|as|asr|assert|begin|class|constraint|do|done|downto"
    "|else|end|exception|external|false|for|fun|function|functor"
    "|if|ignore|in|include|incr|inherit|initializer"
    "|land|lazy|let|lnot|loop|lor|lsl|lsr|lxor"
    "|match|method|mod|module|mutable|new|not|object|of|open|or"
    "|parser|prec|private|raise|rec|ref|self|sig|struct"
    "|then|to|true|try|type|val|value|virtual|when|while|with"
    "|"
};

static char const ocaml_types[] = {
    "|array|bool|char|exn|float|format|format4||int|int32|int64"
    "|lazy_t|list|nativeint|option|string|unit"
    "|"
};

enum {
    IN_OCAML_COMMENT       = 0x01,
    IN_OCAML_COMMENT_MASK  = 0x0F,
    IN_OCAML_STRING        = 0x10,
};

enum {
    OCAML_STYLE_TEXT       = QE_STYLE_DEFAULT,
    OCAML_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    OCAML_STYLE_COMMENT    = QE_STYLE_COMMENT,
    OCAML_STYLE_STRING     = QE_STYLE_STRING,
    OCAML_STYLE_STRING1    = QE_STYLE_STRING,
    OCAML_STYLE_NUMBER     = QE_STYLE_NUMBER,
    OCAML_STYLE_KEYWORD    = QE_STYLE_KEYWORD,
    OCAML_STYLE_TYPE       = QE_STYLE_TYPE,
    OCAML_STYLE_IDENTIFIER = QE_STYLE_DEFAULT,
    OCAML_STYLE_FUNCTION   = QE_STYLE_FUNCTION,
};

static void ocaml_colorize_line(QEColorizeContext *cp,
                                const char32_t *str, int n,
                                QETermStyle *sbuf, ModeDef *syn)
{
    char keyword[16];
    int i = 0, start = i, k, style, len;
    char32_t c;
    int colstate = cp->colorize_state;

    if (colstate & IN_OCAML_COMMENT_MASK)
        goto parse_comment;

    if (colstate & IN_OCAML_STRING)
        goto parse_string;

    if (str[i] == '#') {
        /* Handle shbang script heading ^#!.+
         * and preprocessor # line directives
         */
        i = n;
        SET_STYLE(sbuf, start, i, OCAML_STYLE_PREPROCESS);
    }

    while (i < n) {
        start = i;
        style = OCAML_STYLE_TEXT;
        c = str[i++];
        switch (c) {
        case '(':
            /* check for comment */
            if (str[i] != '*')
                break;

            /* regular comment (recursive?) */
            colstate = IN_OCAML_COMMENT;
            i++;
        parse_comment:
            style = OCAML_STYLE_COMMENT;
            for (; i < n; i++) {
                /* OCaml comments do nest */
                if (str[i] == '(' && str[i + 1] == '*') {
                    i += 2;
                    colstate++;
                } else
                if (str[i] == '*' && str[i + 1] == ')') {
                    i += 2;
                    if (--colstate == 0)
                        break;
                }
            }
            SET_STYLE(sbuf, start, i, style);
            continue;
        case '\"':
            colstate = IN_OCAML_STRING;
        parse_string:
            /* parse string */
            style = OCAML_STYLE_STRING;
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
            /* parse type atom or char const */
            if ((i + 1 < n && str[i] != '\\' && str[i + 1] == '\'')
            ||  (i + 2 < n && str[i] == '\\' && str[i + 2] == '\'')
            ||  (str[i] == '\\' && str[i + 1] == 'x' &&
                 qe_isxdigit(str[i + 2]) && qe_isxdigit(str[i + 3]) &&
                 str[i + 4] == '\'')
            ||  (str[i] == '\\' && qe_isdigit(str[i + 1]) &&
                 qe_isdigit(str[i + 2]) && qe_isdigit(str[i + 3]) &&
                 str[i + 4] == '\'')) {
                style = OCAML_STYLE_STRING1;
                while (str[i++] != '\'')
                    continue;
            } else
            if (qe_isalpha_(str[i])) {
                while (i < n && (qe_isalnum_(str[i]) || str[i] == '\''))
                    i++;
                style = OCAML_STYLE_TYPE;
            }
            SET_STYLE(sbuf, start, i, style);
            continue;
        default:
            break;
        }
        /* parse numbers */
        if (qe_isdigit(c)) {
            style = OCAML_STYLE_NUMBER;
            if (c == '0' && qe_tolower(str[i]) == 'o'
            &&  qe_isoctdigit(str[i + 1])) {
                /* octal int: 0[oO][0-7][0-7_]*[lLn]? */
                for (i += 1; qe_isoctdigit_(str[i]); i++)
                    continue;
                if (qe_findchar("lLn", str[i]))
                    i++;
            } else
            if (c == '0' && qe_tolower(str[i]) == 'x'
            &&  qe_isxdigit(str[i + 1])) {
                /* hex int: 0[xX][0-9a-fA-F][0-9a-zA-Z_]*[lLn]? */
                for (i += 1; qe_isxdigit(str[i]); i++)
                    continue;
                if (qe_findchar("lLn", str[i]))
                    i++;
            } else
            if (c == '0' && qe_tolower(str[i]) == 'b'
            &&  qe_isbindigit(str[i + 1])) {
                /* binary int: 0[bB][01][01_]*[lLn]? */
                for (i += 1; qe_isbindigit_(str[i]); i++)
                    continue;
                if (qe_findchar("lLn", str[i]))
                    i++;
            } else {
                /* decimal integer: [0-9][0-9_]*[lLn]? */
                for (; qe_isdigit_(str[i]); i++)
                    continue;
                if (qe_findchar("lLn", str[i])) {
                    i++;
                } else {
                    /* float:
                     * [0-9][0-9_]*(.[0-9_]*])?([eE][-+]?[0-9][0-9_]*)? */
                    if (str[i] == '.') {
                        for (i += 1; qe_isdigit_(str[i]); i++)
                            continue;
                    }
                    if (qe_tolower(str[i]) == 'e') {
                        k = i + 1;
                        if (str[k] == '+' || str[k] == '-')
                            k++;
                        if (qe_isdigit(str[k])) {
                            for (i = k + 1; qe_isdigit_(str[i]); i++)
                                continue;
                        }
                    }
                }
            }
            SET_STYLE(sbuf, start, i, style);
            continue;
        }
        /* parse identifiers and keywords */
        if (qe_isalpha_(c)) {
            len = 0;
            keyword[len++] = c;
            for (; qe_isalnum_(str[i]) || str[i] == '\''; i++) {
                if (len < countof(keyword) - 1)
                    keyword[len++] = str[i];
            }
            keyword[len] = '\0';
            if (strfind(syn->types, keyword)) {
                style = OCAML_STYLE_TYPE;
            } else
            if (strfind(syn->keywords, keyword)) {
                style = OCAML_STYLE_KEYWORD;
            } else {
                style = OCAML_STYLE_IDENTIFIER;
                k = cp_skip_blanks(str, i, n);
                if (str[k] == '(' && str[k + 1] != '*')
                    style = OCAML_STYLE_FUNCTION;
            }
            SET_STYLE(sbuf, start, i, style);
            continue;
        }
    }
    cp->colorize_state = colstate;
}

static ModeDef ocaml_mode = {
    .name = "Ocaml",
    .extensions = "ml|mli|mll|mly",
    .shell_handlers = "ocaml",
    .keywords = ocaml_keywords,
    .types = ocaml_types,
    .colorize_func = ocaml_colorize_line,
};

/*---------------- Eff language coloring ----------------*/

static char const eff_keywords[] = {
    // eff-keywords
    "and|as|begin|check|do|done|downto|else|end|effect|external|finally|for|"
    "fun|function|handle|handler|if|in|match|let|new|of|operation|rec|val|"
    "while|to|type|then|with|"
    // eff-constants
    "asr|false|mod|land|lor|lsl|lsr|lxor|or|true|"
    // other
    "ref|try|raise|"
    // directives
    "help|reset|quit|use|"
};

static char const eff_types[] = {
    "empty|bool|float|double|int|exception|string|map|range|unit|"
};

static ModeDef eff_mode = {
    .name = "Eff",
    .extensions = "eff",
    .shell_handlers = "eff",
    .keywords = eff_keywords,
    .types = eff_types,
    .colorize_func = ocaml_colorize_line,
};

static int ocaml_init(QEmacsState *qs)
{
    qe_register_mode(qs, &ocaml_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &eff_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(ocaml_init);
