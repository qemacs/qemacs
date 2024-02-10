/*
 * Virgil mode for QEmacs.
 *
 * Copyright (c) 2016-2024 Charlie Gordon.
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

static const char virgil_keywords[] = {
    /* operators */
    "instanceof|new|and|or|"
    /* expressions */
    "this|true|false|null|"
    /* statements */
    "atomic|break|continue|case|default|do|else|for|if|return|super|switch|"
    "while|"
    /* declarators */
    "class|component|constructor|extends|field|function|local|method|private|"
    "program|module|components|"
    /* other, for files in virgil/aeneas/src/ */
    "type|def|var|void|"
};

static const char virgil_types[] = {
    "boolean|char|int|string|"
    /* other, for files in virgil/aeneas/src/ */
    "bool|"
};

enum {
    IN_VIRGIL_COMMENT      = 0x01,
    IN_VIRGIL_STRING       = 0x02,
    IN_VIRGIL_STRING2      = 0x04,
    IN_VIRGIL_LONG_STRING  = 0x08,
    IN_VIRGIL_LONG_STRING2 = 0x10,
    IN_VIRGIL_DOLLAR_STRING = 0x20,
};

enum {
    VIRGIL_STYLE_TEXT =       QE_STYLE_DEFAULT,
    VIRGIL_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    VIRGIL_STYLE_COMMENT =    QE_STYLE_COMMENT,
    VIRGIL_STYLE_STRING =     QE_STYLE_STRING,
    VIRGIL_STYLE_DOLLAR_STRING = QE_STYLE_STRING,
    VIRGIL_STYLE_REGEX =      QE_STYLE_STRING_Q,
    VIRGIL_STYLE_NUMBER =     QE_STYLE_NUMBER,
    VIRGIL_STYLE_KEYWORD =    QE_STYLE_KEYWORD,
    VIRGIL_STYLE_TYPE =       QE_STYLE_TYPE,
    VIRGIL_STYLE_FUNCTION =   QE_STYLE_FUNCTION,
    VIRGIL_STYLE_ERROR =      QE_STYLE_ERROR,
};

static int qe_is_virgil_letter(char32_t c) {
    return qe_isalpha_(c) ||
            (qe_inrange(c, 0x00C0, 0xFFFE) && c != 0x00D7 && c != 0x00F7);
}

static int virgil_scan_number(const char32_t *str0, int flavor) {
    const char32_t *str = str0;
    char32_t c = *str++;
    int octal = 0, nonoctal = 0, isfloat = 0;

    /* Number types:
     * binary integers: 0[bB][01]([01_]*[01])?[gliGLI]?
     * octal integers:  0([0-7_]*[0-7])?[gliGLI]?
     * hex integers:    0[xX][0-9a-zA-Z]([0-9a-zA-Z_]*[0-9a-zA-Z])?
     *                      [gliGLI]?
     * decimal integers: [1-9]([0-9_]*[0-9])?[gliGLI]?
     * decimal floats:  [0-9]([0-9_]*[0-9])?
     *                      [.]([0-9]([0-9_]*[0-9])?)?
     *                      ([eE][-+]?[0-9]([0-9_]*[0-9])?)?[dfDF]?
     *                  [.][0-9]([0-9_]*[0-9])?
     *                      ([eE][-+]?[0-9]([0-9_]*[0-9])?)?[dfDF]?
     *                  [0-9]([0-9_]*[0-9])?
     *                      [eE][-+]?[0-9]([0-9_]*[0-9])?[dfDF]?
     *                  [0-9]([0-9_]*[0-9])?[dfDF]
     * hex floats:      0[xX][0-9a-zA-Z]([0-9a-zA-Z_]*[0-9a-zA-Z])?
     *                      ([.]([0-9a-zA-Z]([0-9a-zA-Z_]*[0-9a-zA-Z])?)?)?
     *                      [pP][-+]?[0-9]([0-9_]*[0-9])?[dfDF]?
     *                  0[xX][.][0-9a-zA-Z]([0-9a-zA-Z_]*[0-9a-zA-Z])?
     *                      [pP][-+]?[0-9]([0-9_]*[0-9])?[dfDF]?
     *
     * Virgil requires a digit after the decimal point.
     *
     * This scanner is relaxed to allow for partial numbers at end of
     * line to avoid showing errors as numbers are typed.
     */
    if (c == '0') {
        if (qe_match2(*str, 'b', 'B')) {
            /* binary numbers */
            str++;
            if (!*str) goto done;
            if (!qe_isbindigit(*str)) goto error;
            for (str += 1; qe_isbindigit_(*str); str++)
                continue;
            if (!*str)  goto done;
            if (str[-1] == '_') goto error;
            if (qe_findchar("gliGLI", *str))
                str++;
            goto done;
        }
        if (qe_match2(*str, 'x', 'X')) {
            /* hexadecimal numbers */
            str++;
            if (!*str) goto done;
            if (*str != '.') {
                if (!qe_isxdigit(*str)) goto error;
                for (str += 1; qe_isxdigit_(*str); str++)
                    continue;
                if (!*str) goto done;
                if (str[-1] == '_') goto error;
                if (qe_findchar("gliGLI", *str)) {
                    str++;
                    goto done;
                }
            }
            if (qe_findchar(".pP", *str)) {
                isfloat = 1;
                if (*str == '.') {
                    if (str == str0 + 2 && !qe_isxdigit(str[1])) goto error;
                    if (flavor == CLANG_VIRGIL && !qe_isxdigit(str[1])) goto done;
                    for (str += 1; qe_isxdigit_(*str); str++)
                        continue;
                }
                if (!*str) goto done;
                if (!qe_match2(*str, 'p', 'P')) goto error;
                str++;
                if (qe_match2(*str, '+', '-'))
                    str++;
                if (!*str) goto done;
                if (!qe_isdigit(*str)) goto error;
                for (str += 1; qe_isdigit_(*str); str++)
                    continue;
                if (str[-1] == '_') goto error;
            }
            if (qe_findchar("dfDF", *str))
                str++;
            goto done;
        }
        octal = 1;
    } else
    if (c == '.')
        str--;

    /* decimal and octal numbers */
    for (; qe_isdigit_(*str); str++) {
        nonoctal |= qe_match2(*str, '8', '9');
    }
    if (!*str) goto done;
    if (str[-1] == '_') goto error;
    if (*str == '.') {
        if (str == str0 && !qe_isdigit(str[1])) goto done;
        if (flavor == CLANG_VIRGIL && !qe_isdigit(str[1])) goto done;
        str++;
        isfloat = 1;
        if (!*str) goto done;
        if (qe_isdigit(*str)) {
            for (str += 1; qe_isdigit_(*str); str++)
                continue;
            if (!*str) goto done;
            if (str[-1] == '_') goto error;
        }
    }
    if (qe_match2(*str, 'e', 'E')) {
        str++;
        isfloat = 1;
        if (qe_match2(*str, '+', '-'))
            str++;
        if (!*str) goto done;
        if (!qe_isdigit(*str)) goto error;
        for (str += 1; qe_isdigit_(*str); str++)
            continue;
        if (!*str) goto done;
        if (str[-1] == '_') goto error;
    }
    if (qe_findchar("dfDF", *str)) {
        str++;
        isfloat = 1;
        goto done;
    }
    if (!*str) goto done;
    if (!isfloat) {
        if (octal && nonoctal) goto error;
        if (qe_findchar("gliGLI", *str)) {
            str++;
            goto done;
        }
    }

done:
    if (!qe_isalnum_(*str)) {
        return str - str0;
    }

error:
    while (qe_isalnum_(*str))
        str++;
    return -(str - str0);
}

static void virgil_colorize_line(QEColorizeContext *cp,
                                 char32_t *str, int n, ModeDef *syn)
{
    int i = 0, start = i, style, klen, haslower;
    char32_t c, sep = 0;
    int state = cp->colorize_state;
    char kbuf[64];

    /* all these states are exclusive */
    if (state & IN_VIRGIL_COMMENT) {
        goto parse_comment;
    }
    if (state & IN_VIRGIL_STRING) {
        sep = '\'';
        goto parse_string;
    }
    if (state & IN_VIRGIL_STRING2) {
        sep = '\"';
        goto parse_string;
    }
    if (state & IN_VIRGIL_LONG_STRING) {
        sep = '\'';
        goto parse_long_string;
    }
    if (state & IN_VIRGIL_LONG_STRING2) {
        sep = '\"';
        goto parse_long_string;
    }
    if (state & IN_VIRGIL_DOLLAR_STRING) {
        goto parse_dollar_string;
    }

    style = 0;
    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            if (start == 0 && str[i] == '!') {
                /* shebang line (should do this generically) */
                i = n;
                style = VIRGIL_STYLE_PREPROCESS;
                break;
            }
            continue;

        case '~':
            while (qe_isblank(str[i]))
                i++;
            if (str[i] == '/') {
                /* parse slashy string as regex */
                sep = '/';
                start = i++;
                goto parse_string;
            }
            continue;

        case '/':
            if (str[i] == '*') {
                /* normal comment */
                i++;
            parse_comment:
                state |= IN_VIRGIL_COMMENT;
                for (; i < n; i++) {
                    if (str[i] == '*' && str[i + 1] == '/') {
                        i += 2;
                        state &= ~IN_VIRGIL_COMMENT;
                        break;
                    }
                }
                style = VIRGIL_STYLE_COMMENT;
                break;
            } else
            if (str[i] == '/') {
                /* line comment */
                i = n;
                style = VIRGIL_STYLE_COMMENT;
                break;
            }
            /* XXX: should handle slashy strings */
            continue;

        case '\'':
        case '\"':
            /* parse string const */
            i--;
            sep = str[i++];
            /* XXX: should colorize interpolated strings */
            if (str[i] == sep && str[i + 1] == sep) {
                /* long string */
                state |= (sep == '\"') ? IN_VIRGIL_LONG_STRING2 :
                        IN_VIRGIL_LONG_STRING;
                i += 2;
            parse_long_string:
                while (i < n) {
                    c = str[i++];
                    if (c == '\\') {
                        if (i < n) {
                            i += 1;
                        }
                    } else
                    if (c == sep && str[i] == sep && str[i + 1] == sep) {
                        i += 2;
                        state &= (sep == '\"') ? ~IN_VIRGIL_LONG_STRING2 :
                                 ~IN_VIRGIL_LONG_STRING;
                        break;
                    }
                }
            } else {
                state |= (sep == '\"') ? IN_VIRGIL_STRING2 : IN_VIRGIL_STRING;
            parse_string:
                while (i < n) {
                    c = str[i++];
                    if (c == '\\') {
                        if (i < n) {
                            i += 1;
                        }
                    } else
                    if (c == sep) {
                        state &= (sep == '\"') ? ~IN_VIRGIL_STRING2 : ~IN_VIRGIL_STRING;
                        break;
                    }
                }
            }
            style = VIRGIL_STYLE_STRING;
            break;

        case '$':
            if (str[i] == '/') {
                /* $ slashy string */
                i++;
                state |= IN_VIRGIL_DOLLAR_STRING;
            parse_dollar_string:
                while (i < n) {
                    c = str[i++];
                    if (c == '$') {
                        if (i < n) {
                            i += 1;
                        }
                    } else
                    if (c == '/' && str[i] == '$') {
                        state &= ~IN_VIRGIL_DOLLAR_STRING;
                        i += 2;
                        break;
                    }
                }
                style = VIRGIL_STYLE_DOLLAR_STRING;
                break;
            }
            goto hasname;

        case '@':
            if (qe_isalpha(str[i])) {
                while (qe_isalnum_(str[i]) || qe_is_virgil_letter(str[i]) || str[i] == '.')
                    i++;
                if (start == 0 || str[start - 1] != '.')
                    style = VIRGIL_STYLE_PREPROCESS;
                break;
            }
            continue;

        case '.':
            if (!qe_isdigit(str[i]))
                continue;
            fallthrough;

        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            i--;
            klen = virgil_scan_number(str + i, CLANG_VIRGIL);
            if (klen > 0) {
                i += klen;
                style = VIRGIL_STYLE_NUMBER;
                break;
            } else
            if (klen < 0) {
                /* Malformed number constants */
                i -= klen;
                style = VIRGIL_STYLE_ERROR;
                break;
            }
            i++;
            continue;

        default:
            if (qe_is_virgil_letter(c)) {
            hasname:
                haslower = 0;
                klen = 0;
                kbuf[klen++] = c;
                for (; qe_isalnum_(str[i]) || qe_is_virgil_letter(str[i]); i++) {
                    haslower |= qe_islower(str[i]);
                    if (klen < countof(kbuf) - 1)
                        kbuf[klen++] = str[i];
                }
                kbuf[klen] = '\0';

                /* keywords are not recognised after '.',
                 * nor before a single '.' nor a map key indicator ':' */
                if ((start == 0 || str[start - 1] != '.')
                &&  (str[i] != '.' || str[i + 1] == '.')
                &&  str[i] != ':') {
                    if ((qe_isupper(c) && haslower && !check_fcall(str, i))
                    ||  strfind(syn->types, kbuf)) {
                        style = VIRGIL_STYLE_TYPE;
                        break;
                    }
                    if (strfind(syn->keywords, kbuf)) {
                        style = VIRGIL_STYLE_KEYWORD;
                        break;
                    }
                }
                if (check_fcall(str, i)) {
                    style = VIRGIL_STYLE_FUNCTION;
                    break;
                }
                continue;
            }
            continue;
        }
        if (style) {
            SET_COLOR(str, start, i, style);
            style = 0;
        }
    }
    /* set style on eol char */
    SET_COLOR1(str, n, style);

    cp->colorize_state = state;
}

static ModeDef virgil_mode = {
    .name = "Virgil",
    .extensions = "v3",
    .shell_handlers = "virgil",
    .colorize_func = virgil_colorize_line,
    .colorize_flags = CLANG_VIRGIL,
    .keywords = virgil_keywords,
    .types = virgil_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

static int virgil_init(void)
{
    qe_register_mode(&virgil_mode, MODEF_SYNTAX);

    return 0;
}

qe_module_init(virgil_init);
