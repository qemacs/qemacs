/*
 * Julia language mode for QEmacs.
 *
 * Copyright (c) 2000-2020 Charlie Gordon.
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

#include "qe.h"

/*---------------- Julia coloring ----------------*/

static char const julia_keywords[] = {
    "abstract|assert|baremodule|begin|bitstype|break|catch|ccall|"
    "const|continue|do|else|elseif|end|export|finally|for|function|"
    "global|if|immutable|import|importall|in|let|local|macro|module|"
    "quote|return|sizeof|throw|try|type|typeof|using|while|yieldto|"
};

static char const julia_types[] = {
    "Int8|Uint8|Int16|Uint16|Int32|Uint32|Int64|Uint64|Int128|Uint128|"
    "Bool|Char|Float16|Float32|Float64|Int|Uint|BigInt|BigFloat|"
    "Array|Union|Nothing|SubString|UTF8String|"
    "None|Any|ASCIIString|DataType|Complex|RegexMatch|Symbol|Expr|"
    "VersionNumber|Exception|"
    "Number|Real|FloatingPoint|Integer|Signed|Unsigned|"
    "Vector|Matrix|UnionType|"
    "ArgumentError|BoundsError|DivideError|DomainError|EOFError|"
    "ErrorException|InexactError|InterruptException|KeyError|LoadError|"
    "MemoryError|MethodError|OverflowError|ParseError|SystemError|"
    "TypeError|UndefRefError|"
    "Range|Function|Dict|"
};

static char const julia_constants[] = {
    "false|true|Inf16|NaN16|Inf32|NaN32|Inf|NaN|im|nothing|pi|e|"
};

#if 0
static char const julia_builtin[] = {
    "include|new|convert|promote|eval|super|isa|bits|eps|"
    "nextfloat|prevfloat|typemin|typemax|println|zero|one|"
    "complex|num|den|float|int|char|length|endof|"
    "info|warn|error|"
};
#endif

enum {
    IN_JULIA_STRING      = 0x10,
    IN_JULIA_STRING_BQ   = 0x20,
    IN_JULIA_LONG_STRING = 0x40,
};

enum {
    JULIA_STYLE_TEXT =     QE_STYLE_DEFAULT,
    JULIA_STYLE_COMMENT =  QE_STYLE_COMMENT,
    JULIA_STYLE_STRING =   QE_STYLE_STRING,
    JULIA_STYLE_NUMBER =   QE_STYLE_NUMBER,
    JULIA_STYLE_KEYWORD =  QE_STYLE_KEYWORD,
    JULIA_STYLE_TYPE =     QE_STYLE_TYPE,
    JULIA_STYLE_FUNCTION = QE_STYLE_FUNCTION,
    JULIA_STYLE_SYMBOL =   QE_STYLE_NUMBER,
};

static inline int julia_is_name(int c) {
    return qe_isalpha_(c) || c > 0xA0;
}

static inline int julia_is_name1(int c) {
    return qe_isalnum_(c) || c == '!' || c > 0xA0;
}

static int julia_get_name(char *buf, int buf_size, const unsigned int *p)
{
    buf_t outbuf, *out;
    int i = 0;

    out = buf_init(&outbuf, buf, buf_size);

    if (julia_is_name(p[i])) {
        buf_putc_utf8(out, p[i]);
        for (i++; julia_is_name1(p[i]); i++) {
            buf_putc_utf8(out, p[i]);
        }
    }
    return i;
}

static int julia_get_number(const unsigned int *p)
{
    const unsigned int *p0 = p;
    int c;

    c = *p++;
    if (c == '0' && qe_tolower(*p) == 'o' && qe_isoctdigit(p[1])) {
        /* octal numbers */
        for (p += 2; qe_isoctdigit(*p); p++)
            continue;
    } else
    if (c == '0' && qe_tolower(*p) == 'x' && qe_isxdigit(p[1])) {
        /* hexadecimal numbers */
        for (p += 2; qe_isxdigit(*p); p++)
            continue;
        /* parse hexadecimal floats */
        if (*p == '.') {
            for (p += 1; qe_isxdigit(*p); p++)
                continue;
        }
        if (qe_tolower(*p) == 'p') {
            int k = 1;
            if (p[k] == '+' || p[k] == '-')
                k++;
            if (qe_isdigit(p[k])) {
                for (p += k + 1; qe_isdigit(*p); p++)
                    continue;
            }
        }
    } else
    if (qe_isdigit(c)) {
        /* decimal numbers */
        for (; qe_isdigit(*p); p++)
            continue;
        if (*p == '.') {
            for (p += 1; qe_isdigit(*p); p++)
                continue;
        }
        if ((c = qe_tolower(*p)) == 'e' || c == 'f') {
            int k = 1;
            if (p[k] == '+' || p[k] == '-')
                k++;
            if (qe_isdigit(p[k])) {
                for (p += k + 1; qe_isdigit(*p); p++)
                    continue;
            }
        }
    } else {
        p -= 1;
    }
    return p - p0;
}

static void julia_colorize_line(QEColorizeContext *cp,
                                unsigned int *str, int n, ModeDef *syn)
{
    int i = 0, start = i, c, sep = 0, klen;
    int state = cp->colorize_state;
    char kbuf[64];

    if (state & IN_JULIA_STRING) {
        sep = '\"';
        goto parse_string;
    }
    if (state & IN_JULIA_STRING_BQ) {
        sep = '`';
        goto parse_string;
    }
    if (state & IN_JULIA_LONG_STRING) {
        sep = '\"';
        goto parse_long_string;
    }

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            i = n;
            SET_COLOR(str, start, i, JULIA_STYLE_COMMENT);
            continue;

        case '\'':
            if (start > 0 && (julia_is_name1(str[i - 2]) || str[i - 2] == '.'))
                break;
            sep = c;
            state = IN_JULIA_STRING_BQ;
            goto parse_string;

        case '`':
            sep = c;
            goto parse_string;

        case '\"':
        has_string:
            /* parse string or character const */
            sep = c;
            state = IN_JULIA_STRING;
            if (str[i] == (unsigned int)sep && str[i + 1] == (unsigned int)sep) {
                /* multi-line string """ ... """ */
                state = IN_JULIA_LONG_STRING;
                i += 2;
            parse_long_string:
                while (i < n) {
                    c = str[i++];
                    if (c == '\\') {
                        if (i < n) {
                            i += 1;
                        }
                    } else
                    if (c == sep && str[i] == (unsigned int)sep && str[i + 1] == (unsigned int)sep) {
                        i += 2;
                        state = 0;
                        break;
                    }
                }
            } else {
            parse_string:
                while (i < n) {
                    c = str[i++];
                    if (c == '\\') {
                        if (i < n) {
                            i += 1;
                        }
                    } else
                    if (c == sep) {
                        state = 0;
                        break;
                    }
                }
            }
            while (qe_findchar("imsx", str[i])) {
                /* regex suffix */
                i++;
            }
            SET_COLOR(str, start, i, JULIA_STYLE_STRING);
            continue;

        default:
            if (qe_isdigit(c)) {
                /* numbers can be directly adjacent to identifiers */
                klen = julia_get_number(str + i - 1);
                i += klen - 1;
                SET_COLOR(str, start, i, JULIA_STYLE_NUMBER);
                continue;
            }
            if (julia_is_name(c)) {
                klen = julia_get_name(kbuf, sizeof(kbuf), str + i - 1);
                i += klen - 1;
                if (str[i] == '"') {
                    c = str[i++];
                    goto has_string;
                }
                if (strfind(syn->keywords, kbuf)
                ||  strfind(julia_constants, kbuf)) {
                    SET_COLOR(str, start, i, JULIA_STYLE_KEYWORD);
                    continue;
                }
                if (strfind(syn->types, kbuf)) {
                    SET_COLOR(str, start, i, JULIA_STYLE_TYPE);
                    continue;
                }
                if (check_fcall(str, i)) {
                    SET_COLOR(str, start, i, JULIA_STYLE_FUNCTION);
                    continue;
                }
                continue;
            }
            break;
        }
    }
    cp->colorize_state = state;
}

static ModeDef julia_mode = {
    .name = "Julia",
    .extensions = "jl",
    .keywords = julia_keywords,
    .types = julia_types,
    .colorize_func = julia_colorize_line,
};

static int julia_init(void)
{
    qe_register_mode(&julia_mode, MODEF_SYNTAX);

    return 0;
}

qe_module_init(julia_init);
