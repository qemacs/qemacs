/*
 * Rust mode for QEmacs.
 *
 * Copyright (c) 2015-2017 Charlie Gordon.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* XXX: should handle :: */

static const char rust_keywords[] = {
    /* keywords */
    "_|as|box|break|const|continue|crate|else|enum|extern|"
    "fn|for|if|impl|in|let|loop|match|mod|move|mut|"
    "priv|proc|pub|ref|return|self|static|struct|trait|"
    "type|typeof|unsafe|use|where|while|"
    /* Constants */
    "false|true|"
};

static const char rust_types[] = {
    "bool|char|i8|i16|i32|i64|isize|u8|u16|u32|u64|usize|f32|f64|str|"
    "String|PathBuf|None|Option|Vec|List|Box|Cons|"
};

enum {
    RUST_STYLE_DEFAULT    = 0,
    RUST_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    RUST_STYLE_COMMENT    = QE_STYLE_COMMENT,
    RUST_STYLE_REGEX      = QE_STYLE_STRING_Q,
    RUST_STYLE_STRING     = QE_STYLE_STRING,
    RUST_STYLE_STRING_Q   = QE_STYLE_STRING_Q,
    RUST_STYLE_NUMBER     = QE_STYLE_NUMBER,
    RUST_STYLE_KEYWORD    = QE_STYLE_KEYWORD,
    RUST_STYLE_TYPE       = QE_STYLE_TYPE,
    RUST_STYLE_FUNCTION   = QE_STYLE_FUNCTION,
    RUST_STYLE_VARIABLE   = QE_STYLE_VARIABLE,
};

/* rust-mode colorization states */
enum {
    IN_RUST_COMMENT    = 0x01,  /* multiline comment */
    IN_RUST_STRING     = 0x04,  /* double-quoted string */
    IN_RUST_STRING_Q   = 0x08,  /* single-quoted string */
};

static void rust_colorize_line(QEColorizeContext *cp,
                               unsigned int *str, int n, ModeDef *syn)
{
    int i = 0, start, i1, i2, indent, c, state, style, klen, delim;
    char kbuf[64];

    for (indent = 0; qe_isblank(str[indent]); indent++)
        continue;

    state = cp->colorize_state;

    start = i;
    c = 0;
    style = RUST_STYLE_DEFAULT;

    if (i >= n)
        goto the_end;

    if (state) {
        /* if already in a state, go directly in the code parsing it */
        if (state & IN_RUST_COMMENT)
            goto parse_comment;
        if (state & IN_RUST_STRING)
            goto parse_string;
        if (state & IN_RUST_STRING_Q)
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
                style = RUST_STYLE_COMMENT;
                state |= IN_RUST_COMMENT;
                for (; i < n; i++) {
                    if (str[i] == '*' && str[i + 1] == '/') {
                        i += 2;
                        state &= ~IN_RUST_COMMENT;
                        break;
                    }
                }
                break;
            } else
            if (str[i] == '/') {
                /* line comment */
                /* XXX: handle doc-comments introduced by /// or //! */
                style = RUST_STYLE_COMMENT;
                i = n;
                break;
            }
            break;
        case '#':       /* preprocessor */
            if (i == indent + 1) {
                if (str[i] == '!') i++;
                style = RUST_STYLE_PREPROCESS;
                break;
            }
            break;
        // case 'r': /* XXX: rust language r" regex " */

        case '\'':      /* character constant */
            /* rust accepts quoted characters and quoted symbols */
            if (i + 1 < n && (str[i] == '\\' || str[i+1] == '\''))
                goto parse_string_q;
            else
                goto normal;

        parse_string_q:
            state |= IN_RUST_STRING_Q;
            style = RUST_STYLE_STRING_Q;
            delim = '\'';
            goto string;

        case '\"':      /* string literal */
        parse_string:
            state |= IN_RUST_STRING;
            style = RUST_STYLE_STRING;
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
                    state &= ~(IN_RUST_STRING | IN_RUST_STRING_Q);
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
                        if (ustrstart(str + i, suffixes[j], &klen)) {
                            i += klen;
                            break;
                        }
                    }
                }
                style = RUST_STYLE_NUMBER;
                break;
            }
            if (qe_isword(c) || c == '$') {
                /* identifiers match:
                 * "[a-zA-Z_\x80-\xff][a-zA-Z_0-9\x80-\xff]*"
                 */
                klen = get_c_identifier(kbuf, countof(kbuf),
                                        str + start, CLANG_RUST);
                i = start + klen;

                if (str[i] == '!'
                &&  (str[i + 1] == '(' || !strcmp(kbuf, "macro_rules"))) {
                    /* macro definition or invokation */
                    i += 1;
                    style = RUST_STYLE_PREPROCESS;
                    break;
                }

                if (strfind(syn->keywords, kbuf)) {
                    style = RUST_STYLE_KEYWORD;
                    break;
                }

                i1 = i;
                while (qe_isblank(str[i1]))
                    i1++;
                i2 = i1;
                while (qe_isblank(str[i2]))
                    i2++;

                if ((start == 0 || str[start - 1] != '.')
                &&  !qe_findchar(".(:", str[i])
                &&  strfind(syn->types, kbuf)) {
                    style = RUST_STYLE_TYPE;
                    break;
                }
                if (str[i1] == '(') {
                    /* function call */
                    /* XXX: different styles for call and definition */
                    style = RUST_STYLE_FUNCTION;
                    break;
                }
                break;
            }
            continue;
        }
        if (style) {
            SET_COLOR(str, start, i, style);
            style = 0;
        }
    }
 the_end:
    /* set style on eol char */
    SET_COLOR1(str, n, style);

    cp->colorize_state = state;
}

static ModeDef rust_mode = {
    .name = "Rust",
    .extensions = "rs",
    .shell_handlers = "rustc",
    .colorize_func = rust_colorize_line,
    .colorize_flags = CLANG_RUST,
    .keywords = rust_keywords,
    .types = rust_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

static int rust_init(void)
{
    qe_register_mode(&rust_mode, MODEF_SYNTAX);

    return 0;
}
