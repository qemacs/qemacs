/*
 * Silk by Julia Ortiz mode for QEmacs.
 *
 * Copyright (c) 2026 Charlie Gordon.
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

/*---------------- Julia Ortiz silk language ----------------*/

static const char silk_keywords[] = {
    /* Constants */
    "false|true|"
    /* Keywords */

    "Without|compileError|"

    "addrspace|align|as|at|break|const|continue|drop|effect|"
    "else|enum|export|extern|fail|fn|for|if|impl|import|in|"
    "interface|let|match|move|mut|once|provides|pub|return|"
    "role|run|service|some|static|struct|tuple|type|typeof|"
    "union|unsafe|use|where|while|#endif|"
};

static const char silk_types[] = {
  "bool|char|string|u8|u16|u32|u64|u128|usize|i8|i16|i32|i64|i128|isize|f32|f64"
};

enum {
    SILK_STYLE_DEFAULT    = 0,
    SILK_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    SILK_STYLE_COMMENT    = QE_STYLE_COMMENT,
    SILK_STYLE_REGEX      = QE_STYLE_STRING_Q,
    SILK_STYLE_STRING     = QE_STYLE_STRING,
    SILK_STYLE_STRING_Q   = QE_STYLE_STRING_Q,
    SILK_STYLE_NUMBER     = QE_STYLE_NUMBER,
    SILK_STYLE_KEYWORD    = QE_STYLE_KEYWORD,
    SILK_STYLE_TYPE       = QE_STYLE_TYPE,
    SILK_STYLE_FUNCTION   = QE_STYLE_FUNCTION,
    SILK_STYLE_VARIABLE   = QE_STYLE_VARIABLE,
};

/* silk-mode colorization states */
enum {
    IN_SILK_COMMENT    = 0x01,  /* multiline comment */
    IN_SILK_STRING     = 0x04,  /* double-quoted string */
    IN_SILK_STRING_Q   = 0x08,  /* single-quoted string */
};

static void silk_colorize_line(QEColorizeContext *cp,
                               const char32_t *str, int n,
                               QETermStyle *sbuf, ModeDef *syn)
{
    int i = 0, start, i1, indent, state, style, klen;
    char32_t c, delim;
    char kbuf[64];

    indent = cp_skip_blanks(str, 0, n);

    state = cp->colorize_state;

    start = i;
    c = 0;
    style = SILK_STYLE_DEFAULT;

    if (i >= n)
        goto the_end;

    if (state) {
        /* if already in a state, go directly in the code parsing it */
        if (state & IN_SILK_COMMENT)
            goto parse_comment;
        if (state & IN_SILK_STRING)
            goto parse_string;
        if (state & IN_SILK_STRING_Q)
            goto parse_string_q;
    }

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '/':
            if (str[i] == '*') {
                /* multi-line comment */
                i++;
            parse_comment:
                style = SILK_STYLE_COMMENT;
                state |= IN_SILK_COMMENT;
                for (; i < n; i++) {
                    if (str[i] == '*' && str[i + 1] == '/') {
                        i += 2;
                        state &= ~IN_SILK_COMMENT;
                        break;
                    }
                }
                break;
            } else
            if (str[i] == '/') {
                /* line comment */
                /* XXX: handle doc-comments introduced by /// or //! */
                style = SILK_STYLE_COMMENT;
                i = n;
                break;
            }
            break;
        case '#':       /* preprocessor */
            if (i == indent + 1) {
                if (str[i] == '!') i++;
                style = SILK_STYLE_PREPROCESS;
                break;
            }
            break;
        case '\'':      /* character constant */
            /* rust accepts quoted characters and quoted symbols */
            if (i + 1 < n && (str[i] == '\\' || str[i+1] == '\''))
                goto parse_string_q;
            else
                goto normal;

        parse_string_q:
            state |= IN_SILK_STRING_Q;
            style = SILK_STYLE_STRING_Q;
            delim = '\'';
            goto string;

        case 'b': /* XXX: silk language b"xxx" byte strings " */
            if (str[i] != '"') goto normal;
            i++;
            goto parse_string;

        case '\"':      /* string literal */
        parse_string:
            state |= IN_SILK_STRING;
            style = SILK_STYLE_STRING;
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
                    state &= ~(IN_SILK_STRING | IN_SILK_STRING_Q);
                    break;
                }
            }
            break;
        default:
        normal:
            if (qe_isdigit(c)) {
                int j;
                // Integers:
                // 0x[0-9a-fA-F_]+      //
                // 0o[0-8_]+            //
                // 0b[01_]+             //
                // [0-9][0-9_]*         //
                // Floats:
                // [0-9][0-9_]*\.[0-9_]*([eE][-\+]?[0-9_]+)?
                // [0-9][0-9_]*(\.[0-9_]*)?[eE][-\+]?[0-9_]+
                // number suffixes:
                static const char * const suffixes[] = {
                    "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
                    "f32", "f64",
                };
                if (c == '0' && str[i] == 'x' && qe_isxdigit_(str[i + 1])) {
                    for (i += 3; qe_isxdigit_(str[i]); i++)
                        continue;
                } else
                if (c == '0' && str[i] == 'o' && qe_isoctdigit_(str[i + 1])) {
                    for (i += 3; qe_isoctdigit_(str[i]); i++)
                        continue;
                } else
                if (c == '0' && str[i] == 'b' && qe_isbindigit_(str[i + 1])) {
                    for (i += 3; qe_isbindigit_(str[i]); i++)
                        continue;
                } else {
                    while (qe_isdigit_(str[i]))
                        i++;
                    if (str[i] == '.' && qe_isdigit_(str[i + 1])) {
                        for (i += 2; qe_isdigit_(str[i]); i++)
                            continue;
                    }
                    if (str[i] == 'e' || str[i] == 'E') {
                        j = i + 1;
                        if (str[j] == '+' || str[j] == '-')
                            j++;
                        if (qe_isdigit_(str[j])) {
                            for (i = j + 1; qe_isdigit_(str[i]); i++)
                                continue;
                        }
                    }
                }
                if (qe_isalpha(str[i])) {
                    for (j = 0; j < countof(suffixes); j++) {
                        if (ustr_match_keyword(str + i, suffixes[j], &klen)) {
                            i += klen;
                            break;
                        }
                    }
                }
                style = SILK_STYLE_NUMBER;
                break;
            }
            if (qe_isword(c) || c == '$') {
                /* identifiers match:
                 * "[a-zA-Z_\x80-\xff][a-zA-Z_0-9\x80-\xff]*"
                 */
                i += get_c_identifier(kbuf, countof(kbuf), c, str, i, n, CLANG_SILK);

                if (str[i] == '!'
                &&  (str[i + 1] == '(' || strequal(kbuf, "macro_rules"))) {
                    /* macro definition or invokation */
                    i += 1;
                    style = SILK_STYLE_PREPROCESS;
                    break;
                }

                if (strfind(syn->keywords, kbuf)) {
                    style = SILK_STYLE_KEYWORD;
                    break;
                }

                if ((start == 0 || str[start - 1] != '.')
                &&  !qe_findchar(".(:", str[i])
                &&  (strfind(syn->types, kbuf) || is_cap_type(kbuf))) {
                    style = SILK_STYLE_TYPE;
                    break;
                }
                i1 = cp_skip_blanks(str, i, n);
                if (str[i1] == '(') {
                    /* function call */
                    /* XXX: different styles for call and definition */
                    style = SILK_STYLE_FUNCTION;
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

static ModeDef silk_mode = {
    .name = "Silk",
    .extensions = "silk",
    //.shell_handlers = "silkc",
    .colorize_func = silk_colorize_line,
    .colorize_flags = CLANG_SILK | CLANG_CAP_TYPE,
    .keywords = silk_keywords,
    .types = silk_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

static int silk_init(QEmacsState *qs)
{
    qe_register_mode(qs, &silk_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(silk_init);
