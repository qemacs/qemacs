/*
 * Swift mode for QEmacs.
 *
 * Copyright (c) 2002-2024 Charlie Gordon.
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

enum {
    SWIFT_STYLE_DEFAULT    = 0,
    SWIFT_STYLE_COMMENT    = QE_STYLE_COMMENT,
    SWIFT_STYLE_REGEX      = QE_STYLE_STRING_Q,
    SWIFT_STYLE_STRING     = QE_STYLE_STRING,
    SWIFT_STYLE_NUMBER     = QE_STYLE_NUMBER,
    SWIFT_STYLE_KEYWORD    = QE_STYLE_KEYWORD,
    SWIFT_STYLE_TYPE       = QE_STYLE_TYPE,
    SWIFT_STYLE_FUNCTION   = QE_STYLE_FUNCTION,
};

/* c-mode colorization states */
enum {
    IN_SWIFT_COMMENT    = 0x03,  /* one of the comment styles */
    IN_SWIFT_COMMENT1   = 0x01,  /* single line comment with \ at EOL */
    IN_SWIFT_COMMENT2   = 0x02,  /* multiline swift comment (nested) */
    IN_SWIFT_COMMENT_SHIFT = 4,  /* shift for block comment nesting level */
    IN_SWIFT_COMMENT_LEVEL = 0x70, /* mask for block comment nesting level */
};

#define R(a, b)  a, b
#define S(a)     a, a
#define E()      0x110000, 0

static char32_t const swift_identifier_head[] = {
    R('A', 'Z'), R('a', 'z'),
    S(0x00A8), S(0x00AA), S(0x00AD), S(0x00AF),
    R(0x00B2, 0x00B5), R(0x00B7, 0x00BA),
    R(0x00BC, 0x00BE), R(0x00C0, 0x00D6),
    R(0x00D8, 0x00F6), R(0x00F8, 0x00FF),
    R(0x0100, 0x02FF), R(0x0370, 0x167F),
    R(0x1681, 0x180D), R(0x180F, 0x1DBF),
    R(0x1E00, 0x1FFF), R(0x200B, 0x200D),
    R(0x202A, 0x202E), R(0x203F, 0x2040),
    S(0x2054), R(0x2060, 0x206F),
    R(0x2070, 0x20CF), R(0x2100, 0x218F),
    R(0x2460, 0x24FF), R(0x2776, 0x2793),
    R(0x2C00, 0x2DFF), R(0x2E80, 0x2FFF),
    R(0x3004, 0x3007), R(0x3021, 0x302F),
    R(0x3031, 0x303F), R(0x3040, 0xD7FF),
    R(0xF900, 0xFD3D), R(0xFD40, 0xFDCF),
    R(0xFDF0, 0xFE1F), R(0xFE30, 0xFE44),
    R(0xFE47, 0xFFFD), R(0x10000, 0x1FFFD),
    R(0x20000, 0x2FFFD), R(0x30000, 0x3FFFD),
    R(0x40000, 0x4FFFD), R(0x50000, 0x5FFFD),
    R(0x60000, 0x6FFFD), R(0x70000, 0x7FFFD),
    R(0x80000, 0x8FFFD), R(0x90000, 0x9FFFD),
    R(0xA0000, 0xAFFFD), R(0xB0000, 0xBFFFD),
    R(0xC0000, 0xCFFFD), R(0xD0000, 0xDFFFD),
    R(0xE0000, 0xEFFFD), E(),
};

static char32_t const swift_identifier_other_chars[] = {
    R(0x0300, 0x036F), R(0x1DC0, 0x1DFF),
    R(0x20D0, 0x20FF), R(0xFE20, 0xFE2F),
    E(),
};

#undef R
#undef S
#undef E

static int qe_find_range(char32_t c, const char32_t *rangep) {
    if (c > 0x10FFFF)
        return 0;

    for (;;) {
        if (c < *rangep++)
            return 0;
        if (c <= *rangep++)
            return 1;
    }
}

static int is_swift_identifier_head(char32_t c)
{
    return qe_find_range(c, swift_identifier_head);
}

static int is_swift_identifier_char(char32_t c)
{
    if (qe_isalnum_(c)) {
        return 1;
    } else {
        return qe_find_range(c, swift_identifier_head)
        ||     qe_find_range(c, swift_identifier_other_chars);
    }
}

static int swift_parse_identifier(char *dest, int size, char32_t c, const char32_t *p)
{
    buf_t outbuf, *out;
    int i = 0;

    out = buf_init(&outbuf, dest, size);

    buf_putc_utf8(out, c);
    for (; is_swift_identifier_char(p[i]); i++) {
        buf_putc_utf8(out, p[i]);
    }
    if (p[i] == '`' && dest[0] == '`')
        buf_put_byte(out, p[i++]);

    return i;
}

static int swift_parse_number(const char32_t *p)
{
    int i = 0, j;

    if (*p == '0') {
        if (p[1] == 'b') {
            if (qe_isbindigit(p[2])) {
                for (i = 3; qe_isbindigit_(p[i]); i++)
                    continue;
                return i;
            }
            return 1;
        } else
        if (p[1] == 'o') {
            if (qe_isoctdigit(p[2])) {
                for (i = 3; qe_isoctdigit_(p[i]); i++)
                    continue;
                return i;
            }
            return 1;
        } else
        if (p[1] == 'x') {
            if (qe_isxdigit(p[2])) {
                for (i = 3; qe_isxdigit_(p[i]); i++)
                    continue;
                if (p[i] == '.' && qe_isxdigit(p[i + 1])) {
                    for (i += 2; qe_isxdigit_(p[i]); i++)
                        continue;
                }
                if (p[i] == 'p' || p[i] == 'P') {
                    j = i + 1;
                    if (p[j] == '-' || p[j] == '+')
                        j++;
                    /* There is a bug in the Swift Programming Language
                     * book, page 665:
                     * hexadecimal-exponent ->
                     * floating-point-p sign(opt) hexadecimal-literal
                     * should be decimal-literal instead
                     */
                    if (qe_isdigit(p[j])) {
                        for (i = j + 1; qe_isdigit_(p[i]); i++)
                            continue;
                    }
                }
                return i;
            }
            return 1;
        }
    }
    if (qe_isdigit(p[0])) {
        for (i = 1; qe_isdigit_(p[i]); i++)
            continue;
        /* floats require digits before and after . */
        if (p[i] == '.' && qe_isdigit(p[i + 1])) {
            for (i += 2; qe_isdigit_(p[i]); i++)
                continue;
        }
        if (p[i] == 'e' || p[i] == 'E') {
            j = i + 1;
            if (p[j] == '-' || p[j] == '+')
                j++;
            if (qe_isdigit(p[j])) {
                for (i = j + 1; qe_isdigit_(p[i]); i++)
                    continue;
            }
        }
        return i;
    }
    return 0;
}

static void swift_colorize_line(QEColorizeContext *cp,
                                const char32_t *str, int n,
                                QETermStyle *sbuf, ModeDef *syn)
{
    int i = 0, start = 0, style, level;
    char32_t c;
    int state = cp->colorize_state;
    char kbuf[64];

    if (state) {
        /* if already in a state, go directly in the code parsing it */
        if (state & IN_SWIFT_COMMENT2)
            goto parse_comment2;
    }

    style = 0;
    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '/':
            if (str[i] == '*') {
                /* Swift multi-line comments can nest */
                i++;
                state |= IN_SWIFT_COMMENT2;
            parse_comment2:
                level = (state & IN_SWIFT_COMMENT_LEVEL) >> IN_SWIFT_COMMENT_SHIFT;
                while (i < n) {
                    if (str[i] == '/' && str[i + 1] == '*') {
                        i += 2;
                        level++;
                    } else
                    if (str[i] == '*' && str[i + 1] == '/') {
                        i += 2;
                        if (level == 0) {
                            state &= ~IN_SWIFT_COMMENT2;
                            break;
                        }
                        level--;
                    } else {
                        i++;
                    }
                }
                state = (state & ~IN_SWIFT_COMMENT_LEVEL) |
                        (min_int(level, 7) << IN_SWIFT_COMMENT_SHIFT);
                if (state & IN_SWIFT_COMMENT2) {
                    /* set style on eol char to allow skip block from
                     * end of comment line.
                     */
                    i++;
                }
                style = SWIFT_STYLE_COMMENT;
                break;
            }
            if (str[i] == '/') {
                /* end of line comment (include eol char, see above) */
                i = n + 1;
                style = SWIFT_STYLE_COMMENT;
                break;
            }
            continue;

        case '`':       /* `symbol` for reserved words */
        case '@':       /* @attribute */
            goto identifier;

        case '\"':      /* string literal */
            while (i < n) {
                c = str[i++];
                if (c == '\\') {
                    /* Should colorize \(expr) interpolation */
                    if (i >= n)
                        break;
                    i++;
                } else
                if (c == '\"') {
                    break;
                }
            }
            style = SWIFT_STYLE_STRING;
            break;

        default:
            if (qe_isdigit(c)) {
                i -= 1;
                i += swift_parse_number(str + i);
                style = SWIFT_STYLE_NUMBER;
                break;
            }
            if (is_swift_identifier_head(c)) {
            identifier:
                i += swift_parse_identifier(kbuf, countof(kbuf), c, str + start);
                if (strfind(syn->keywords, kbuf)) {
                    style = SWIFT_STYLE_KEYWORD;
                    break;
                }
                if (strfind(syn->types, kbuf)) {
                    style = SWIFT_STYLE_TYPE;
                    if (check_fcall(str, i)) {
                        /* function style cast */
                        style = SWIFT_STYLE_KEYWORD;
                    }
                    break;
                }
                if (check_fcall(str, i)) {
                    style = SWIFT_STYLE_FUNCTION;
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
    cp->colorize_state = state;
}

static const char swift_keywords[] = {
    "var|let|for|in|do|while|if|else|switch|nil|case|default|where|func|"
    "return|class|override|self|super|init|deinit|get|set|willSet|didSet|"
    "enum|struct|protocol|mutating|extension|typealias|true|false|_|"
    "break|continue|fallthrough|inout|static|subscript|convenience|"
    "weak|unowned|is|as|import|dynamicType|new|nonmutating|associativity|"
    "safe|unsafe|precedence|"
    "@lazy|@final|@objc|@optional|@infix|@prefix|@postfix|@assignment|"
    "@auto_closure|@required|@noreturn|@class_protocol|@exported|"
    "@NSCopying|@NSManaged|"
    "operator|infix|prefix|postfix|"
    "__COLUMN__|__FILE__|__FUNCTION__|__LINE__|"
};

static const char swift_types[] = {
    "Bool|Int|Uint|Float|Double|Character|String|Array|Dictionary|"
    "Int8|Int16|Int32|Int64|UInt8|UInt16|UInt32|UInt64|Void|"
    "Any|AnyObject|Self|Type|"

    "CBool|CChar|CUnsignedChar|CShort|CUnsignedShort|CInt|CUnsignedInt|"
    "CLong|CUnsignedLong|CLongLong|CUnsignedLongLong|CWideChar|CChar16|"
    "CChar32|CFloat|CDouble|"
};

static ModeDef swift_mode = {
    .name = "Swift",
    .extensions = "swift",
    .shell_handlers = "swift",
    .colorize_func = swift_colorize_line,
    .colorize_flags = CLANG_SWIFT,
    .keywords = swift_keywords,
    .types = swift_types,
    .fallback = &c_mode,
};

static int swift_init(QEmacsState *qs)
{
    qe_register_mode(qs, &swift_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(swift_init);
