/*
 * Python language mode for QEmacs.
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

/*---------------- Python coloring ----------------*/

static char const python_keywords[] = {
    "|False|None|True|and|as|assert|break|class|continue"
    "|def|del|elif|else|except|finally|for|from|global"
    "|if|import|in|is|lambda|nonlocal|not|or|pass|raise"
    "|return|try|while|with|yield"
    "|"
};

// XXX: colorize annotations
// XXX: parse unicode identifiers

enum {
    IN_PYTHON_COMMENT      = 0x80,
    IN_PYTHON_STRING       = 0x40,
    IN_PYTHON_STRING2      = 0x20,
    IN_PYTHON_LONG_STRING  = 0x10,
    IN_PYTHON_RAW_STRING   = 0x08,
    IN_PYTHON_TEMPLATE_STRING = 0x04,
    IN_PYTHON_TEMPLATE_EXPR = 0x02,
    //IN_PYTHON_REGEX1       = 0x01,
};

enum {
    PYTHON_STYLE_TEXT =     QE_STYLE_DEFAULT,
    PYTHON_STYLE_COMMENT =  QE_STYLE_COMMENT,
    PYTHON_STYLE_STRING =   QE_STYLE_STRING,
    PYTHON_STYLE_NUMBER =   QE_STYLE_NUMBER,
    PYTHON_STYLE_KEYWORD =  QE_STYLE_KEYWORD,
    PYTHON_STYLE_TYPE =     QE_STYLE_TYPE,
    PYTHON_STYLE_FUNCTION = QE_STYLE_FUNCTION,
    PYTHON_STYLE_REGEX    = QE_STYLE_STRING,
    PYTHON_STYLE_ANNOTATION = QE_STYLE_PREPROCESS,
};

enum {  // Python flavors
    PYTHON_PYTHON = 0,
    PYTHON_RAPYDSCRIPT,
    PYTHON_BAZEL,
    PYTHON_MOJO,
};

static void python_colorize_line(QEColorizeContext *cp,
                                 const char32_t *str, int n,
                                 QETermStyle *sbuf, ModeDef *syn)
{
    int i = 0, start = i, style = 0, i1, tag = 0;
    char32_t c, sep;
    int mode_flags = syn->colorize_flags;
    int state = cp->colorize_state;
    char kbuf[64];

    if ((state & IN_PYTHON_STRING)
    &&  !(state & IN_PYTHON_TEMPLATE_EXPR)) {
        goto parse_string;
    }

    tag = !qe_isblank(str[i]);

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            i = n;
            style = PYTHON_STYLE_COMMENT;
            break;

        case '@':
            i += ustr_get_identifier(kbuf, countof(kbuf), c, str, i, n);
            style = PYTHON_STYLE_ANNOTATION;
            break;

        case '\'':
        case '\"':
            /* parse string constant */
            i--;
        has_quote:
            sep = str[i++];
            state |= IN_PYTHON_STRING;
            if (sep == '\"') state |= IN_PYTHON_STRING2;
            if (str[i] == sep && str[i + 1] == sep) {
                state |= IN_PYTHON_LONG_STRING;
                i += 2;
            }
        parse_string:
            sep = (state & IN_PYTHON_STRING2) ? '\"' : '\'';
            while (i < n) {
                c = str[i++];
                if (c == '\\' && !(state & IN_PYTHON_RAW_STRING)) {
                    if (i < n) {
                        i += 1;
                    }
                } else
                if (c == '{' && (state & IN_PYTHON_TEMPLATE_STRING)) {
                    state |= IN_PYTHON_TEMPLATE_EXPR;
                    break;
                } else
                if (c == sep) {
                    if (state & IN_PYTHON_LONG_STRING) {
                        if (str[i] != sep || str[i + 1] != sep)
                            continue;
                        i += 2;
                    }
                    state = 0;
                    break;
                }
            }
            style = PYTHON_STYLE_STRING;
            break;

        case '.':
            if (qe_isdigit(str[i]))
                goto parse_decimal;
            continue;

        case '/':
            /* XXX: should test for regular expression in PYTHON_RAPYDSCRIPT flavor */
            if (str[i] != '/' && mode_flags == PYTHON_RAPYDSCRIPT) {
                /* XXX: should use more context to tell regex from divide */
                int prev = ' ';
                for (i1 = start; i1 > 0; ) {
                    prev = str[--i1];
                    if (!qe_isblank(prev))
                        break;
                }
                if (qe_findchar(" [({},;=<>!~^&|*/%?:", prev)
                 || sbuf[i1] == PYTHON_STYLE_KEYWORD
                 || (str[i] != ' ' && (str[i] != '=' || str[i + 1] != ' ') && !(qe_isalnum(prev) || prev == ')'))) {
                     /* parse regex */
                     int in_charclass = 0;
                     while (i < n) {
                         c = str[i++];
                         if (c == '\\') {
                             if (i < n) {
                                 i += 1;
                             }
                         } else
                         if (in_charclass) {
                             if (c == ']') {
                                 in_charclass = 0;
                             }
                             /* ignore '/' inside char classes */
                         } else {
                             if (c == '[') {
                                 in_charclass = 1;
                             } else
                             if (c == '/') {
                                 while (qe_isalnum_(str[i])) {
                                     i++;
                                 }
                                 break;
                             }
                         }
                     }
                     style = PYTHON_STYLE_REGEX;
                     break;
                 }
            }
            continue;

        case 'b':
        case 'B':
            if (qe_tolower(str[i]) == 'r'
            &&  (str[i + 1] == '\'' || str[i + 1] == '\"')) {
                state |= IN_PYTHON_RAW_STRING;
                i += 1;
                goto has_quote;
            }
            goto has_alpha;

        case 'r':
        case 'R':
            if (qe_tolower(str[i]) == 'b'
            &&  (str[i + 1] == '\'' || str[i + 1] == '\"')) {
                state |= IN_PYTHON_RAW_STRING;
                i += 1;
                goto has_quote;
            }
            if ((str[i] == '\'' || str[i] == '\"')) {
                state |= IN_PYTHON_RAW_STRING;
                goto has_quote;
            }
            goto has_alpha;

        case 't':
            if (mode_flags == PYTHON_MOJO
            &&  (str[i] == '\'' || str[i] == '\"')) {
                state |= IN_PYTHON_TEMPLATE_STRING;
                goto has_quote;
            }
            goto has_alpha;

        case '}':
            if (state & IN_PYTHON_TEMPLATE_EXPR) {
                state &= ~IN_PYTHON_TEMPLATE_EXPR;
                if (state & IN_PYTHON_STRING)
                    goto parse_string;
            }
            continue;

        case '(':
        case '{':
            tag = 0;
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
                    for (; qe_isdigit(str[i]); i++)
                        continue;
                    if (str[i] == '.' && qe_isdigit(str[i + 1])) {
                        i++;
                parse_decimal:
                        /* decimal floats require a digit after the '.' */
                        for (; qe_isdigit(str[i]); i++)
                            continue;
                    }
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
                if (qe_tolower(str[i]) == 'j') {
                    i++;
                }

                /* XXX: should detect malformed number constants */
                style = PYTHON_STYLE_NUMBER;
                break;
            }
        has_alpha:
            if (qe_isalpha_(c)) {
                i += ustr_get_identifier(kbuf, countof(kbuf), c, str, i, n);
                if (strfind(syn->keywords, kbuf)) {
                    tag = strequal(kbuf, "def");
                    style = PYTHON_STYLE_KEYWORD;
                    break;
                }
                if ((syn->types && strfind(syn->types, kbuf))
                ||  (mode_flags == PYTHON_MOJO && qe_isupper(*kbuf) && start && str[start - 1] != '.')) {
                    style = PYTHON_STYLE_TYPE;
                    break;
                }
                if (check_fcall(str, i)) {
                    style = PYTHON_STYLE_FUNCTION;
                    if (tag) {
                        /* tag function definition */
                        eb_add_tag(cp->b, cp->offset + start, kbuf);
                        tag = 0;
                    }
                    break;
                }
                if (tag) {
                    i1 = cp_skip_blanks(str, i, n);
                    if (qe_findchar(",=", str[i1])) {
                        /* tag variable definition */
                        eb_add_tag(cp->b, cp->offset + start, kbuf);
                        /* XXX: should colorize variable definition */
                    }
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
    cp->colorize_state = state;
}

static ModeDef python_mode = {
    .name = "Python",
    .extensions = "py|pyt",
    // XXX: should accept wildcards "python*"
    .shell_handlers = "python|python2.6|python2.7|python2.8|python3",
    .keywords = python_keywords,
    .colorize_func = python_colorize_line,
    .colorize_flags = PYTHON_PYTHON,
};

/*---- Rapidscrypt: a Python-like syntax ----*/

static char const rapydscript_keywords[] = {
    "|False|None|True|and|as|assert|break|class|continue"
    "|def|del|elif|else|except|finally|for|from|global"
    "|if|import|in|is|lambda|nonlocal|not|or|pass|raise"
    "|return|try|while|with|yield"
    // Rapydscript keywords
    "|new|undefined|this|to|til|get|set|super"
    "|"
};

static ModeDef rapydscript_mode = {
    .name = "RapydScript",
    .extensions = "pyj",
    .shell_handlers = "rapydscript",
    .keywords = rapydscript_keywords,
    .colorize_func = python_colorize_line,
    .colorize_flags = PYTHON_RAPYDSCRIPT,
};

/*---- Bazel mode: a build system with a Python-like syntax ----*/

static int bazel_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    /* check file name or extension */
    if (match_extension(p->filename, mode->extensions)
    ||  strstart(p->filename, "WORKSPACE", NULL))
        return 70;

    return 1;
}

static int bazel_mode_init(EditState *s, EditBuffer *b, int flags)
{
    if (s) {
        /* XXX: should use the default values from mode variables */
        s->indent_tabs_mode = 0;
        s->indent_width = 2;
    }
    return 0;
}

static ModeDef bazel_mode = {
    .name = "Bazel",
    .extensions = "bzl|bazel",
    .keywords = python_keywords,
    .mode_probe = bazel_mode_probe,
    .mode_init = bazel_mode_init,
    .colorize_func = python_colorize_line,
    .colorize_flags = PYTHON_BAZEL,
};

/*---- Mojo: a high performace language with a Python like syntax ----*/

static char const mojo_keywords[] = {
    // Python keywords
    "|False|None|True"
    "|as|assert|break|continue|del|global"
    "|lambda|nonlocal|pass|return|with|yield"
    "|class|def"
    "|fn|struct|trait"          // Mojo specific
    "|inout|out|owned|borrowed|raises"  // Mojo specific
    "|elif|else|if|for|while"
    "|and|in|is|not|or"
    "|except|finally|raise|try"
    "|from|import|alias"
    "|async|await"
    "|var|let|mut|ref|comptime" // Mojo specific
    "|"
};

#if 0
// Useless as we colorize all capitalized words
static char const mojo_types[] = {
    "Bool"
    "|Int|UInt|Int8|UInt8|Int16|UInt16|Int32|UInt32|Int64|UInt64"
    "|Int128|UInt128|Int256|UInt256"
    "|Float16|Float32|Float64|BFloat16"
    // should also support Float4_e2m1fn, Float8_e5m2, Float8_e5m2fnuz,
    //   Float8_e4m3fn, Float8_e4m3fnuz...
    "|SIMD|DType|Scalar"
    "|IntLiteral|FloatLiteral"
    "|String|StaticString|StringSlice|TString"
    "|Tuple|List|Dict|Set|Optional|Variant"
};
#endif

static ModeDef mojo_mode = {
    .name = "Mojo",
    .extensions = "mojo|🔥",
    .keywords = mojo_keywords,
    //.types = mojo_types,
    .colorize_func = python_colorize_line,
    .colorize_flags = PYTHON_MOJO,
};

/*---- loading functions ----*/

static int python_init(QEmacsState *qs)
{
    qe_register_mode(qs, &python_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &rapydscript_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &bazel_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &mojo_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(python_init);
