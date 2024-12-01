/*
 * Nim mode for QEmacs.
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

#define MAX_KEYWORD_SIZE  16

/*---------------- Nim coloring ----------------*/

static char const nim_keywords[] = {
    /* Nim keywords */
    "addr|and|as|asm|atomic|bind|block|break|case|cast|concept|const|"
    "continue|converter|defer|discard|distinct|div|do|elif|else|end|"
    "enum|except|export|finally|for|from|func|generic|if|import|in|include|"
    "interface|is|isnot|iterator|let|macro|method|mixin|mod|nil|not|notin|"
    "object|of|or|out|proc|ptr|raise|ref|return|shl|shr|static|template|"
    "try|tuple|type|using|var|when|while|with|without|xor|yield|"
    /* predefined operators */
    "inc|dec|"
    /* predefined constants */
    "true|false|"
};

static char const nim_types[] = {
    "int|uint|cint|cuint|clong|cstring|string|char|byte|bool|"
    "openArray|seq|array|void|pointer|float|csize|cdouble|"
    "cchar|cschar|cshort|cu|nil|expr|stmt|typedesc|auto|any|"
    "range|openarray|varargs|set|cfloat|"
    "int8|int16|int32|int64|uint8|uint16|uint32|uint64|"
};

enum {
    NIM_STYLE_TEXT =     QE_STYLE_DEFAULT,
    NIM_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    NIM_STYLE_COMMENT =  QE_STYLE_COMMENT,
    NIM_STYLE_STRING =   QE_STYLE_STRING,
    NIM_STYLE_NUMBER =   QE_STYLE_NUMBER,
    NIM_STYLE_KEYWORD =  QE_STYLE_KEYWORD,
    NIM_STYLE_TYPE =     QE_STYLE_TYPE,
    NIM_STYLE_FUNCTION = QE_STYLE_FUNCTION,
    NIM_STYLE_PRAGMA =   QE_STYLE_PREPROCESS,
};

/* nim-mode colorization states */
enum {
    IN_NIM_COMMENT      = 0x80,
    IN_NIM_CHARLIT      = 0x40,
    IN_NIM_STRING       = 0x20,
    IN_NIM_LONG_STRING  = 0x10,
    IN_NIM_RAW_STRING   = 0x08,
    IN_NIM_STRING_BQ    = 0x04,
    IN_NIM_PRAGMA       = 0x02,
};

static void nim_colorize_line(QEColorizeContext *cp,
                              const char32_t *str, int n,
                              QETermStyle *sbuf, ModeDef *syn)
{
    int i = 0, start = i, style = 0, klen;
    char32_t c, sep = 0;
    int state = cp->colorize_state;
    char kbuf[64];

    if (state & IN_NIM_COMMENT) {
        goto parse_comment;
    }
    if (state & IN_NIM_CHARLIT) {
        sep = '\'';
        goto parse_string;
    }
    if (state & IN_NIM_STRING) {
        sep = '\"';
        goto parse_string;
    }
    if (state & IN_NIM_LONG_STRING) {
        sep = '\"';
        goto parse_long_string;
    }
    if (state & IN_NIM_STRING_BQ) {
        sep = '`';
        goto parse_string;
    }

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            if (start == 0 && str[i] == '!') {
                i = n;
                style = NIM_STYLE_PREPROCESS;
                break;
            }

        parse_comment:
            state &= ~IN_NIM_COMMENT;
            /* XXX: should handle trailing backslash more generically */
            for (; i < n; i++) {
                if (str[i] == '\\')
                    state |= IN_NIM_COMMENT;
                else
                if (!qe_isblank(str[i]))
                    state &= ~IN_NIM_COMMENT;
            }
            style = NIM_STYLE_COMMENT;
            break;
#if 0
        case 'r':
        case 'R':
            if (str[i] == '\"') {
                state |= IN_NIM_RAW_STRING;
                goto has_quote;
            }
            goto has_alpha;
#endif
        case '`':
            sep = c;
            style = IN_NIM_STRING_BQ;
            goto parse_string;

        case '\'':
            sep = c;
            style = IN_NIM_CHARLIT;
            goto parse_string;

        case '\"':
            /* parse string const */
            i--;
        has_quote:
            sep = str[i++];
            if (str[i] == sep && str[i + 1] == sep) {
                /* long string */
                state |= IN_NIM_LONG_STRING | IN_NIM_RAW_STRING;
                i += 2;
            parse_long_string:
                while (i < n) {
                    c = str[i++];
                    if (!(state & IN_NIM_RAW_STRING) && c == '\\') {
                        if (i < n) {
                            i += 1;
                        }
                    } else
                    if (c == sep && str[i] == sep
                    &&  str[i + 1] == sep && str[i + 2] != sep) {
                        i += 2;
                        state &= ~(IN_NIM_LONG_STRING | IN_NIM_RAW_STRING);
                        break;
                    }
                }
            } else {
                state = IN_NIM_STRING;
            parse_string:
                while (i < n) {
                    c = str[i++];
                    if (!(state & IN_NIM_RAW_STRING) && c == '\\') {
                        if (i < n) {
                            i += 1;
                        }
                        continue;
                    }
                    if (c == sep) {
                        if ((state & IN_NIM_RAW_STRING) && str[i] == '"') {
                            i += 1;
                            continue;
                        }
                        state &= ~(IN_NIM_STRING | IN_NIM_RAW_STRING);
                        break;
                    }
                }
            }
            style = NIM_STYLE_STRING;
            break;

        case '.':
            if (str[i] == '}') {
                i += 1;
                state &= ~IN_NIM_PRAGMA;
                style = NIM_STYLE_PRAGMA;
                break;
            }
            continue;

            // Handle ( ) { } [ ] (. .) {. .} [. .] : :: .. `

        case '{':
            if (str[i] == '.' && str[i + 1] != '.') {
                /* Nim pragmas */
                for (i++;; i++) {
                    if (qe_isalnum_(str[i]))
                        continue;
                    if (str[i] == '.' && str[i + 1] != '}')
                        continue;
                    break;
                }
                state |= IN_NIM_PRAGMA;
                style = NIM_STYLE_PRAGMA;
                break;
            }
            continue;

        default:
            if (qe_isdigit(c)) {
                int j, k;
                static const char * const suffixes[] = {
                    "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
                    "f32", "f64", "f128",
                };

                if (c == '0' && qe_match2(str[i], 'b', 'B')) {
                    /* binary numbers */
                    for (i += 1; qe_isbindigit_(str[i]); i++)
                        continue;
                } else
                if (c == '0' && (str[i] == 'o' || qe_match2(str[i], 'c', 'C'))) {
                    /* octal numbers */
                    for (i += 1; qe_isoctdigit_(str[i]); i++)
                        continue;
                } else
                if (c == '0' && qe_match2(str[i], 'x', 'X')) {
                    /* hexadecimal numbers */
                    for (i += 1; qe_isxdigit_(str[i]); i++)
                        continue;
                } else {
                    /* decimal numbers */
                    for (; qe_isdigit_(str[i]); i++)
                        continue;
                    if (str[i] == '.' && qe_isdigit_(str[i + 1])) {
                        i++;
                        /* decimal floats require a digit after the '.' */
                        for (; qe_isdigit_(str[i]); i++)
                            continue;
                    }
                    if (qe_match2(str[i], 'e', 'E')) {
                        /* skip exponent: potential sign and decimal number if present */
                        int k1 = i + 1;
                        if (qe_match2(str[k1], '+', '-'))
                            k1++;
                        if (qe_isdigit(str[k1])) {
                            for (i = k1 + 1; qe_isdigit_(str[i]); i++)
                                continue;
                        }
                    }
                }
                /* handle optional ' and type suffix */
                j = i;
                if (str[j] == '\'')
                    j++;
                if (qe_isalpha(str[j])) {
                    for (k = 0; k < countof(suffixes); k++) {
                        /* match keyword, not just prefix */
                        if (ustr_match_keyword(str + j, suffixes[k], &klen)) {
                            i = j + klen;
                            break;
                        }
                    }
                }
                /* XXX: should detect malformed number constants */
                style = NIM_STYLE_NUMBER;
                break;
            }
        //has_alpha:
            if (qe_isalpha_(c)) {
                i += ustr_get_identifier(kbuf, countof(kbuf), c, str, i, n);
                if (str[i] == '"') {
                    /* generalized raw string literal */
                    state |= IN_NIM_RAW_STRING;
                    goto has_quote;
                }

                if (strfind(syn->keywords, kbuf)) {
                    style = NIM_STYLE_KEYWORD;
                    break;
                }
                if ((start == 0 || str[start - 1] != '.')
                &&  (str[i] != '.')) {
                    if (strfind(syn->types, kbuf)) {
                        //|| (qe_isupper(c) && haslower)
                        style = NIM_STYLE_TYPE;
                        break;
                    }
                }
                if (check_fcall(str, i)) {
                    style = NIM_STYLE_FUNCTION;
                    break;
                }
                continue;
            }
            continue;
        }
        if (style) {
            SET_STYLE(sbuf, start, i, style);
            style = 0;
        }
    }
    // XXX: should handle line continuation with trailing \\ followed
    // by white space
    SET_STYLE1(sbuf, n, style);    /* set style on eol char */

    cp->colorize_state = state;
}

static ModeDef nim_mode = {
    .name = "Nim",
    .extensions = "nim",
    .shell_handlers = "nim",
    .keywords = nim_keywords,
    .types = nim_types,
    .colorize_func = nim_colorize_line,
};

static int nim_init(QEmacsState *qs)
{
    qe_register_mode(qs, &nim_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(nim_init);
