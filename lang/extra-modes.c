/*
 * Miscellaneous language modes for QEmacs.
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

#define MAX_KEYWORD_SIZE  16

/*---------------- x86 Assembly language coloring ----------------*/

static char const asm_prepkeywords1[] = {
    "|align|arg|assume|codeseg|const|dataseg|display|dosseg"
    "|else|elseif|elseif1|elseif2|elseifb|elseifdef|elseifdif"
    "|elseifdifi|elseife|elseifidn|elseifidni|elseifnb|elseifndef"
    "|emul|end|endif|endm|endp|err|errif|errif1|errif2"
    "|errifb|errifdef|errifdif|errifdifi|errife|errifidn"
    "|errifidni|errifnb|errifndef|even|evendata|exitm|fardata"
    "|ideal|if|if1|if2|ifb|ifdef|ifdif|ifdifi|ife|ifidn"
    "|ifidni|ifnb|ifndef|include|includelib|irp|irpc"
    "|jumps|largestack|local|locals|macro|masm|masm51|model|multerrs"
    "|noemul|nojumps|nolocals|nomasm51|nomulterrs|nosmart|nowarn"
    "|proc|purge|quirks|radix|record|rept"
    "|smart|smallstack|stack|startupcode|subttl|title"
    "|version|warn|while"
    "|"
};

static char const asm_prepkeywords2[] = {
    "|catstr|endp|ends|enum|equ|group"
    "|label|macro|proc|record|segment|struc"
    "|"
};

/* colstate is used to store the comment character */

enum {
    ASM_STYLE_TEXT =        QE_STYLE_DEFAULT,
    ASM_STYLE_PREPROCESS =  QE_STYLE_PREPROCESS,
    ASM_STYLE_COMMENT =     QE_STYLE_COMMENT,
    ASM_STYLE_STRING =      QE_STYLE_STRING,
    ASM_STYLE_NUMBER =      QE_STYLE_NUMBER,
    ASM_STYLE_IDENTIFIER =  QE_STYLE_VARIABLE,
};

static void asm_colorize_line(QEColorizeContext *cp,
                              unsigned int *str, int n, ModeDef *syn)
{
    char keyword[MAX_KEYWORD_SIZE];
    int i = 0, start = 0, c, style = 0, len, wn = 0; /* word number on line */
    int colstate = cp->colorize_state;

    if (colstate)
        goto in_comment;

    for (; i < n && qe_isblank(str[i]); i++)
        continue;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '\\':
            if (str[i] == '}' || str[i] == '{')
                goto prep;
            break;
        case '}':
        prep:
            /* scan for comment */
            for (; i < n; i++) {
                if (str[i] == ';')
                    break;
            }
            style = ASM_STYLE_PREPROCESS;
            break;
        case ';':
            i = n;
            style = ASM_STYLE_COMMENT;
            break;
        case '\'':
        case '\"':
            /* parse string const */
            while (i < n && str[i++] != (unsigned int)c)
                continue;
            style = ASM_STYLE_STRING;
            break;
        default:
            /* parse numbers */
            if (qe_isdigit(c)) {
                for (; qe_isalnum(str[i]); i++)
                    continue;
                style = ASM_STYLE_NUMBER;
                break;
            }
            /* parse identifiers and keywords */
            if (qe_isalpha_(c) || qe_findchar("@.$%?", c)) {
                len = 0;
                keyword[len++] = qe_tolower(c);
                for (; qe_isalnum_(str[i]) || qe_findchar("@$%?", str[i]); i++) {
                    if (len < countof(keyword) - 1)
                        keyword[len++] = qe_tolower(str[i]);
                }
                keyword[len] = '\0';
                if (++wn == 1) {
                    if (strequal(keyword, "comment") && n > i) {
                        SET_COLOR(str, start, i, ASM_STYLE_PREPROCESS);
                        for (; i < n && qe_isblank(str[i]); i++)
                            continue;
                        start = i;
                        colstate = str[i++];  /* end of comment character */
                        /* skip characters upto and including separator */
                    in_comment:
                        while (i < n) {
                            if ((char)str[i++] == (char)colstate) {
                                colstate = 0;
                                break;
                            }
                        }
                        style = ASM_STYLE_COMMENT;
                        break;
                    }
                    if (strfind(asm_prepkeywords1, keyword))
                        goto prep;
                } else
                if (wn == 2) {
                    if (strfind(asm_prepkeywords2, keyword)) {
                        style = ASM_STYLE_PREPROCESS;
                        break;
                    }
                }
                //SET_COLOR(str, start, i, ASM_STYLE_IDENTIFIER);
                continue;
            }
            continue;
        }
        if (style) {
            SET_COLOR(str, start, i, style);
            style = 0;
        }
    }
    cp->colorize_state = colstate;
}

static ModeDef asm_mode = {
    .name = "asm",
    .extensions = "asm|asi|cod",
    .colorize_func = asm_colorize_line,
};

static int asm_init(void)
{
    qe_register_mode(&asm_mode, MODEF_SYNTAX);

    return 0;
}

/*---------------- Ini file (and similar) coloring ----------------*/

enum {
    INI_STYLE_TEXT =       QE_STYLE_DEFAULT,
    INI_STYLE_COMMENT =    QE_STYLE_COMMENT,
    INI_STYLE_STRING =     QE_STYLE_STRING,
    INI_STYLE_FUNCTION =   QE_STYLE_FUNCTION,
    INI_STYLE_NUMBER =     QE_STYLE_NUMBER,
    INI_STYLE_IDENTIFIER = QE_STYLE_VARIABLE,
    INI_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
};

static void ini_colorize_line(QEColorizeContext *cp,
                              unsigned int *str, int n, ModeDef *syn)
{
    int i = 0, start, c, style = 0, indent;

    while (qe_isblank(str[i]))
        i++;

    indent = i;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case ';':
            if (start == indent) {
                i = n;
                style = INI_STYLE_COMMENT;
                break;
            }
            continue;
        case '#':
            if (start == indent) {
                i = n;
                style = INI_STYLE_PREPROCESS;
                break;
            }
            continue;
        case '[':
            if (start == 0) {
                i = n;
                style = INI_STYLE_FUNCTION;
                break;
            }
            continue;
        case '\"':
            /* parse string const */
            while (i < n) {
                /* XXX: escape sequences? */
                if (str[i++] == '\"')
                    break;
            }
            style = INI_STYLE_STRING;
            break;
        case ' ':
        case '\t':
            continue;
        default:
            /* parse numbers */
            if (qe_isdigit(c)) {
                for (; i < n; i++) {
                    if (!qe_isalnum(str[i]))
                        break;
                }
                style = INI_STYLE_NUMBER;
                break;
            }
            /* parse identifiers and keywords */
            if (start == 0 && (qe_isalpha_(c) || c == '@' || c == '$')) {
                for (; i < n; i++) {
                    if (str[i] == '=')
                        break;
                }
                if (i < n) {
                    style = INI_STYLE_IDENTIFIER;
                }
                break;
            }
        }
        if (style) {
            SET_COLOR(str, start, i, style);
            style = 0;
        }
    }
}

static int ini_mode_probe(ModeDef *mode, ModeProbeData *pd)
{
    const char *p = (const char *)pd->buf;
    const char *p_end = p + pd->buf_size;

    if (match_extension(pd->filename, mode->extensions))
        return 80;

    while (p < p_end) {
        /* skip comments */
        if (*p == ';' || *p == '#') {
            p = memchr(p, '\n', p_end - p);
            if (!p)
                return 1;
        }
        if (*p == '\n') {
            p++;
            continue;
        }
        /* Check for ^\[.+\]\n */
        if (*p == '[' && p[1] != '[' && p[1] != '{') {
            while (++p < p_end) {
                if (*p == ']')
                    return 40;
                if (*p == '\n')
                    return 1;
            }
        }
        break;
    }
    return 1;
}

static ModeDef ini_mode = {
    .name = "ini",
    .extensions = "ini|inf|INI|INF|reg",
    .mode_probe = ini_mode_probe,
    .colorize_func = ini_colorize_line,
};

static int ini_init(void)
{
    qe_register_mode(&ini_mode, MODEF_SYNTAX);

    return 0;
}

/*---------------- sharp file coloring ----------------*/

/* Very simple colorizer: # introduces comments, that's it! */

enum {
    SHARP_STYLE_TEXT =       QE_STYLE_DEFAULT,
    SHARP_STYLE_COMMENT =    QE_STYLE_COMMENT,
};

static void sharp_colorize_line(QEColorizeContext *cp,
                               unsigned int *str, int n, ModeDef *syn)
{
    int i = 0, start, c;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            i = n;
            SET_COLOR(str, start, i, SHARP_STYLE_COMMENT);
            continue;
        default:
            break;
        }
    }
}

static int sharp_mode_probe(ModeDef *mode, ModeProbeData *pd)
{
    const char *p = cs8(pd->buf);

    while (qe_isspace(*p))
        p++;

    if (*p == '#') {
        if (match_extension(pd->filename, mode->extensions))
            return 60;
        return 30;
    }
    return 1;
}

static ModeDef sharp_mode = {
    .name = "sharp",
    .extensions = "txt",
    .mode_probe = sharp_mode_probe,
    .colorize_func = sharp_colorize_line,
};

static int sharp_init(void)
{
    qe_register_mode(&sharp_mode, MODEF_SYNTAX);

    return 0;
}

/*---------------- PostScript colors ----------------*/

enum {
    IN_PS_STRING  = 0x0F            /* ( ... ) level */,
    IN_PS_COMMENT = 0x10,
};

enum {
    PS_STYLE_TEXT =       QE_STYLE_DEFAULT,
    PS_STYLE_COMMENT =    QE_STYLE_COMMENT,
    PS_STYLE_STRING =     QE_STYLE_STRING,
    PS_STYLE_NUMBER =     QE_STYLE_DEFAULT,
    PS_STYLE_IDENTIFIER = QE_STYLE_FUNCTION,
};

#define wrap 0

static void ps_colorize_line(QEColorizeContext *cp,
                             unsigned int *str, int n, ModeDef *syn)
{
    int i = 0, start = i, c;
    int colstate = cp->colorize_state;

    if (colstate & IN_PS_COMMENT)
        goto in_comment;

    if (colstate & IN_PS_STRING)
        goto in_string;

    colstate = 0;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
            /* Should deal with '<...>' '<<...>>' '<~...~>' tokens. */
        case '%':
        in_comment:
            if (wrap)
                colstate |= IN_PS_COMMENT;
            else
                colstate &= ~IN_PS_COMMENT;
            i = n;
            SET_COLOR(str, start, i, PS_STYLE_COMMENT);
            continue;
        case '(':
            colstate++;
        in_string:
            /* parse string skipping embedded \\ */
            while (i < n) {
                switch (str[i++]) {
                case '(':
                    colstate++;
                    continue;
                case ')':
                    colstate--;
                    if (!(colstate & IN_PS_STRING))
                        break;
                    continue;
                case '\\':
                    if (i == n)
                        break;
                    i++;
                    continue;
                default:
                    continue;
                }
                break;
            }
            SET_COLOR(str, start, i, PS_STYLE_STRING);
            continue;
        default:
            break;
        }
        /* parse numbers */
        if (qe_isdigit(c)) {
            for (; i < n; i++) {
                if (!qe_isalnum(str[i]) && str[i] != '.')
                    break;
            }
            SET_COLOR(str, start, i, PS_STYLE_NUMBER);
            continue;
        }
        /* parse identifiers and keywords */
        if (qe_isalpha_(c)) {
            for (; i < n; i++) {
                if (qe_findchar(" \t\r\n,()<>[]{}/", str[i]))
                    break;
            }
            SET_COLOR(str, start, i, PS_STYLE_IDENTIFIER);
            continue;
        }
    }
    cp->colorize_state = colstate;
#undef wrap
}

static int ps_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (match_extension(p->filename, mode->extensions))
        return 80;

    if (*p->buf == '%' && qe_stristr((const char *)p->buf, "script"))
        return 40;

    return 1;
}

static ModeDef ps_mode = {
    .name = "Postscript",
    .extensions = "ps|ms|eps",
    .mode_probe = ps_mode_probe,
    .colorize_func = ps_colorize_line,
};

static int ps_init(void)
{
    qe_register_mode(&ps_mode, MODEF_SYNTAX);

    return 0;
}

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

/*---------------- Coffee coloring ----------------*/

static char const coffee_keywords[] = {
    // keywords common with Javascript:
    "true|false|null|this|new|delete|typeof|in|instanceof|"
    "return|throw|break|continue|debugger|yield|if|else|"
    "switch|for|while|do|try|catch|finally|class|extends|super|"
    // CoffeeScript only keywords:
    "undefined|then|unless|until|loop|of|by|when|"
    // aliasses
    "and|or|is|isnt|not|yes|no|on|off|"
    // reserved: should be flagged as errors
    "case|default|function|var|void|with|const|let|enum|export|import|"
    "native|implements|interface|package|private|protected|public|static|"
    // proscribed in strict mode
    "arguments|eval|yield*|"
};

enum {
    IN_COFFEE_STRING       = 0x100,
    IN_COFFEE_STRING2      = 0x200,
    IN_COFFEE_REGEX        = 0x400,
    IN_COFFEE_LONG_STRING  = 0x01,
    IN_COFFEE_LONG_STRING2 = 0x02,
    IN_COFFEE_LONG_REGEX   = 0x04,
    IN_COFFEE_REGEX_CCLASS = 0x08,
    IN_COFFEE_JSTOKEN      = 0x10,
    IN_COFFEE_LONG_COMMENT = 0x20,
};

enum {
    COFFEE_STYLE_TEXT =     QE_STYLE_DEFAULT,
    COFFEE_STYLE_COMMENT =  QE_STYLE_COMMENT,
    COFFEE_STYLE_STRING =   QE_STYLE_STRING,
    COFFEE_STYLE_REGEX =    QE_STYLE_STRING,
    COFFEE_STYLE_JSTOKEN =  QE_STYLE_STRING,
    COFFEE_STYLE_NUMBER =   QE_STYLE_NUMBER,
    COFFEE_STYLE_KEYWORD =  QE_STYLE_KEYWORD,
    COFFEE_STYLE_FUNCTION = QE_STYLE_FUNCTION,
    COFFEE_STYLE_ERROR =    QE_STYLE_ERROR,
};

static void coffee_colorize_line(QEColorizeContext *cp,
                                 unsigned int *str, int n, ModeDef *syn)
{
    int i = 0, start = i, c, style = 0, sep, prev, i1;
    int state = cp->colorize_state;
    char kbuf[64];

    if (state & IN_COFFEE_STRING) {
        sep = '\'';
        goto parse_string;
    }
    if (state & IN_COFFEE_STRING2) {
        sep = '\"';
        goto parse_string;
    }
    if (state & IN_COFFEE_REGEX) {
        goto parse_regex;
    }
    if (state & IN_COFFEE_LONG_STRING) {
        sep = '\'';
        goto parse_long_string;
    }
    if (state & IN_COFFEE_LONG_STRING2) {
        sep = '\"';
        goto parse_long_string;
    }
    if (state & IN_COFFEE_LONG_REGEX) {
        goto parse_regex;
    }
    if (state & IN_COFFEE_JSTOKEN) {
        goto parse_jstoken;
    }
    if (state & IN_COFFEE_LONG_COMMENT) {
        goto parse_long_comment;
    }

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            if (str[i] == '#' && str[i + 1] == '#') {
                /* multi-line block comments with ### */
                state = IN_COFFEE_LONG_COMMENT;
            parse_long_comment:
                while (i < n) {
                    c = str[i++];
                    if (c == '#' && str[i] == '#' && str[i + 1] == '#') {
                        i += 2;
                        state = 0;
                        break;
                    }
                }
            } else {
                i = n;
            }
            style = COFFEE_STYLE_COMMENT;
            break;

        case '\'':
        case '\"':
            /* parse string constant */
            i--;
            sep = str[i++];
            if (str[i] == (unsigned int)sep && str[i + 1] == (unsigned int)sep) {
                /* long string */
                state = (sep == '\"') ? IN_COFFEE_LONG_STRING2 :
                        IN_COFFEE_LONG_STRING;
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
                state = (sep == '\"') ? IN_COFFEE_STRING2 : IN_COFFEE_STRING;
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
                if (state) {
                    state = 0;
                    // unterminated string literal, should flag unless
                    // point is at end of line.
                    style = COFFEE_STYLE_ERROR;
                    break;
                }
            }
            style = COFFEE_STYLE_STRING;
            break;

        case '`':
            /* parse multi-line JS token */
            state = IN_COFFEE_JSTOKEN;
        parse_jstoken:
            while (i < n) {
                c = str[i++];
                if (c == '\\') {
                    if (i < n) {
                        i += 1;
                    }
                } else
                if (c == '`') {
                    state = 0;
                    break;
                }
            }
            style = COFFEE_STYLE_JSTOKEN;
            break;

        case '.':
            if (qe_isdigit(str[i]))
                goto parse_decimal;
            if (str[i] == '.') /* .. range operator */
                i++;
            if (str[i] == '.') /* ... range operator */
                i++;
            continue;

        case '/':
            /* XXX: should use more context to tell regex from divide */
            if (str[i] == '/') {
                i++;
                if (str[i] == '/') {
                    /* multiline /// regex */
                    state = IN_COFFEE_LONG_REGEX;
                    i++;
                    goto parse_regex;
                } else {
                    /* floor divide // operator */
                    break;
                }
            }
            prev = ' ';
            for (i1 = start; i1 > 0; ) {
                prev = str[--i1] & CHAR_MASK;
                if (!qe_isblank(prev))
                    break;
            }
            if (qe_findchar(" [({},;=<>!~^&|*/%?:", prev)
            ||  qe_findchar("^\\?.[{},;<>!~&|*%:", str[i])
            ||  (str[i] == '=' && str[i + 1] == '/')
            ||  (str[i] == '(' && str[i + 1] == '?')
            ||  (str[i1] >> STYLE_SHIFT) == COFFEE_STYLE_KEYWORD
            ||  (str[i] != ' ' && (str[i] != '=' || str[i + 1] != ' ')
            &&   !(qe_isalnum(prev) || qe_findchar(")]}\"\'?:", prev)))) {
                state = IN_COFFEE_REGEX;
            parse_regex:
                style = COFFEE_STYLE_REGEX;
                while (i < n) {
                    c = str[i++];
                    if (c == '\\') {
                        if (i < n) {
                            i += 1;
                        }
                    } else
                    if (state & IN_COFFEE_REGEX_CCLASS) {
                        if (c == ']') {
                            state &= ~IN_COFFEE_REGEX_CCLASS;
                        }
                        /* ignore '/' inside char classes */
                    } else {
                        if (c == '[') {
                            state |= IN_COFFEE_REGEX_CCLASS;
                            if (str[i] == '^')
                                i++;
                            if (str[i] == ']')
                                i++;
                        } else
                        if (state & IN_COFFEE_LONG_REGEX) {
                            if (c == '/' && str[i] == '/' && str[i + 1] == '/') {
                                i += 2;
                                state = 0;
                                while (qe_isalpha(str[i]))
                                    i++;
                                break;
                            } else
                            if (qe_isblank(c) && str[i] == '#' && str[i+1] != '{') {
                                SET_COLOR(str, start, i, style);
                                start = i;
                                i = n;
                                style = COFFEE_STYLE_COMMENT;
                                break;
                            }
                        } else {
                            if (c == '/') {
                                state = 0;
                                while (qe_isalpha(str[i]))
                                    i++;
                                break;
                            }
                        }
                    }
                }
                if (state & ~IN_COFFEE_LONG_REGEX) {
                    state = 0;
                    // unterminated regex literal, should flag unless
                    // point is at end of line.
                    style = COFFEE_STYLE_ERROR;
                    break;
                }
                break;
            }
            continue;

        default:
            if (qe_isdigit(c)) {
                if (c == '0' && str[i] == 'b') {
                    /* binary numbers */
                    for (i += 1; qe_isbindigit(str[i]); i++)
                        continue;
                } else
                if (c == '0' && str[i] == 'o') {
                    /* octal numbers */
                    for (i += 1; qe_isoctdigit(str[i]); i++)
                        continue;
                } else
                if (c == '0' && str[i] == 'x') {
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
                    if (str[i] == 'e') {
                        int k = i + 1;
                        if (str[k] == '+' || str[k] == '-')
                            k++;
                        if (qe_isdigit(str[k])) {
                            for (i = k + 1; qe_isdigit(str[i]); i++)
                                continue;
                        }
                    }
                }

                /* XXX: should detect malformed number constants */
                style = COFFEE_STYLE_NUMBER;
                break;
            }
            if (qe_isalpha_(c)) {
                i += ustr_get_identifier(kbuf, countof(kbuf), c, str, i, n);
                if (strfind(syn->keywords, kbuf)) {
                    style = COFFEE_STYLE_KEYWORD;
                    break;
                }
                if (check_fcall(str, i)) {
                    style = COFFEE_STYLE_FUNCTION;
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
    cp->colorize_state = state;
}

static int coffee_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (match_extension(p->filename, mode->extensions)
    ||  match_shell_handler(cs8(p->buf), mode->shell_handlers)
    ||  stristart(p->filename, "Cakefile", NULL)) {
        return 80;
    }
    return 1;
}

static ModeDef coffee_mode = {
    .name = "CoffeeScript",
    .alt_name = "coffee",
    .extensions = "coffee",
    .shell_handlers = "coffee",
    .mode_probe = coffee_mode_probe,
    .keywords = coffee_keywords,
    .colorize_func = coffee_colorize_line,
};

static int coffee_init(void)
{
    qe_register_mode(&coffee_mode, MODEF_SYNTAX);

    return 0;
}

/*---------------- Erlang coloring ----------------*/

static char const erlang_keywords[] = {
    "|after|and|andalso|band|begin|bnot|bor|bsl|bsr|bxor|case|catch|cond"
    "|div|end|fun|if|let|not|of|or|orelse|receive|rem|try|when|xor"
    "|true|false|nil|_"
    "|"
};

static char const erlang_commands[] = {
    "|module|compile|define|export|import|vsn|on_load|record|include|file"
    "|mode|author|include_lib|behaviour"
    "|type|opaque|spec|callback|export_type"
    "|ifdef|ifndef|undef|else|endif"
    "|"
};

static char const erlang_types[] = {
    "|"
};

enum {
    IN_ERLANG_STRING   = 0x01,
};

enum {
    ERLANG_STYLE_TEXT       = QE_STYLE_DEFAULT,
    ERLANG_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    ERLANG_STYLE_COMMENT    = QE_STYLE_COMMENT,
    ERLANG_STYLE_STRING     = QE_STYLE_STRING,
    ERLANG_STYLE_CHARCONST  = QE_STYLE_STRING,
    ERLANG_STYLE_ATOM       = QE_STYLE_DEFAULT,
    ERLANG_STYLE_INTEGER    = QE_STYLE_NUMBER,
    ERLANG_STYLE_FLOAT      = QE_STYLE_NUMBER,
    ERLANG_STYLE_KEYWORD    = QE_STYLE_KEYWORD,
    ERLANG_STYLE_TYPE       = QE_STYLE_TYPE,
    ERLANG_STYLE_IDENTIFIER = QE_STYLE_DEFAULT,
    ERLANG_STYLE_FUNCTION   = QE_STYLE_FUNCTION,
};

static int erlang_match_char(unsigned int *str, int i)
{
    /* erlang character constant */
    if (str[i++] == '\\') {
        switch (str[i++]) {
        case '0': case '1': case '2': case '3':
        case '4': case '5': case '6': case '7':
            if (qe_isoctdigit(str[i])) i++;
            if (qe_isoctdigit(str[i])) i++;
            break;
        case 'x':
        case 'X':
            if (str[i] == '{') {
                for (i++; qe_isxdigit(str[i]); i++)
                    continue;
                if (str[i] == '}')
                    i++;
                break;
            }
            if (qe_isxdigit(str[i])) i++;
            if (qe_isxdigit(str[i])) i++;
            break;
        case '^':
            if (qe_isalpha(str[i])) i++;
            break;
        case 'b': /* backspace (8) */
        case 'd': /* delete (127) */
        case 'e': /* escape (27) */
        case 'f': /* formfeed (12) */
        case 'n': /* newline (10) */
        case 'r': /* return (13) */
        case 's': /* space (32) */
        case 't': /* tab (9) */
        case 'v': /* vtab (?) */
        case '\'': /* single quote */
        case '\"': /* double quote */
        case '\\': /* backslash */
            break;
        default:
            break;
        }
    }
    return i;
}

static void erlang_colorize_line(QEColorizeContext *cp,
                                unsigned int *str, int n, ModeDef *syn)
{
    char keyword[MAX_KEYWORD_SIZE];
    int i = 0, start = i, c, style, len, base;
    int colstate = cp->colorize_state;

    if (colstate & IN_ERLANG_STRING)
        goto parse_string;

    if (str[i] == '#' && str[i + 1] == '!') {
        /* Handle shbang script heading ^#!.+
         * and preprocessor # line directives
         */
        i = n;
        SET_COLOR(str, start, i, ERLANG_STYLE_PREPROCESS);
    }

    while (i < n) {
        start = i;
        style = ERLANG_STYLE_TEXT;
        c = str[i++];
        switch (c) {
        case '%':
            i = n;
            style = ERLANG_STYLE_COMMENT;
            SET_COLOR(str, start, i, style);
            continue;
        case '$':
            i = erlang_match_char(str, i);
            style = ERLANG_STYLE_CHARCONST;
            SET_COLOR(str, start, i, style);
            continue;

        case '\"':
            colstate = IN_ERLANG_STRING;
        parse_string:
            /* parse string */
            style = ERLANG_STYLE_STRING;
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
            SET_COLOR(str, start, i, style);
            continue;
        case '\'':
            /* parse an Erlang atom */
            style = ERLANG_STYLE_ATOM;
            while (i < n) {
                c = str[i++];
                if (c == '\\' && i < n)
                    i++;
                else
                if (c == '\'') {
                    break;
                }
            }
            SET_COLOR(str, start, i, style);
            continue;
        default:
            break;
        }
        if (qe_isdigit(c)) {
            /* parse numbers */
            style = ERLANG_STYLE_INTEGER;
            base = c - '0';
            while (qe_isdigit(str[i])) {
                base = base * 10 + str[i++] - '0';
            }
            if (base >= 2 && base <= 36 && str[i] == '#') {
                for (i += 1; qe_digit_value(str[i]) < base; i++)
                    continue;
                if (str[i - 1] == '#')
                    i--;
            } else {
                /* float: [0-9]+(.[0-9]+])?([eE][-+]?[0-9]+)? */
                if (str[i] == '.' && qe_isdigit(str[i + 1])) {
                    style = ERLANG_STYLE_FLOAT;
                    for (i += 2; qe_isdigit(str[i]); i++)
                        continue;
                }
                if (qe_tolower(str[i]) == 'e') {
                    int k = i + 1;
                    if (str[k] == '+' || str[k] == '-')
                        k++;
                    if (qe_isdigit(str[k])) {
                        style = ERLANG_STYLE_FLOAT;
                        for (i = k + 1; qe_isdigit(str[i]); i++)
                            continue;
                    }
                }
            }
            SET_COLOR(str, start, i, style);
            continue;
        }
        if (qe_isalpha_(c) || c == '@') {
            /* parse an Erlang atom or identifier */
            len = 0;
            keyword[len++] = c;
            for (; qe_isalnum_(str[i]) || str[i] == '@'; i++) {
                if (len < countof(keyword) - 1)
                    keyword[len++] = str[i];
            }
            keyword[len] = '\0';
            if (start && str[start - 1] == '-'
            &&  strfind(erlang_commands, keyword)) {
                style = ERLANG_STYLE_PREPROCESS;
            } else
            if (strfind(syn->types, keyword)) {
                style = ERLANG_STYLE_TYPE;
            } else
            if (strfind(syn->keywords, keyword)) {
                style = ERLANG_STYLE_KEYWORD;
            } else
            if (check_fcall(str, i)) {
                style = ERLANG_STYLE_FUNCTION;
            } else
            if (qe_islower(keyword[0])) {
                style = ERLANG_STYLE_ATOM;
            } else {
                style = ERLANG_STYLE_IDENTIFIER;
            }
            SET_COLOR(str, start, i, style);
            continue;
        }
    }
    cp->colorize_state = colstate;
}

static int erlang_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (match_extension(p->filename, mode->extensions)
    ||  match_shell_handler(cs8(p->buf), mode->shell_handlers)
    ||  strstr(cs8(p->buf), "-*- erlang -*-")) {
        return 80;
    }
    return 1;
}

static ModeDef erlang_mode = {
    .name = "Erlang",
    .extensions = "erl|hrl",
    .shell_handlers = "erlang",
    .mode_probe = erlang_mode_probe,
    .keywords = erlang_keywords,
    .types = erlang_types,
    .colorize_func = erlang_colorize_line,
};

static int erlang_init(void)
{
    qe_register_mode(&erlang_mode, MODEF_SYNTAX);

    return 0;
}

/*---------------- Elixir coloring ----------------*/

static char const elixir_keywords[] = {
    "|do|end|cond|case|if|else|after|for|unless|when|quote|in"
    "|try|catch|rescue|raise"
    "|def|defp|defmodule|defcallback|defmacro|defsequence"
    "|defmacrop|defdelegate|defstruct|defexception|defimpl"
    "|require|alias|import|use|fn"
    "|setup|test|assert|refute|using"
    "|true|false|nil|and|or|not|_"
    "|"
};

static char const elixir_delim1[] = "\'\"/|([{<";
static char const elixir_delim2[] = "\'\"/|)]}>";

enum {
    IN_ELIXIR_DELIM  = 0x0F,
    IN_ELIXIR_STRING = 0x10,
    IN_ELIXIR_REGEX  = 0x20,
    IN_ELIXIR_TRIPLE = 0x40,
};

enum {
    ELIXIR_STYLE_TEXT =       QE_STYLE_DEFAULT,
    ELIXIR_STYLE_COMMENT =    QE_STYLE_COMMENT,
    ELIXIR_STYLE_CHARCONST =  QE_STYLE_STRING,
    ELIXIR_STYLE_STRING =     QE_STYLE_STRING,
    ELIXIR_STYLE_HEREDOC =    QE_STYLE_STRING,
    ELIXIR_STYLE_REGEX =      QE_STYLE_STRING,
    ELIXIR_STYLE_NUMBER =     QE_STYLE_NUMBER,
    ELIXIR_STYLE_KEYWORD =    QE_STYLE_KEYWORD,
    ELIXIR_STYLE_ATOM =       QE_STYLE_TYPE,
    ELIXIR_STYLE_TAG =        QE_STYLE_VARIABLE,
    ELIXIR_STYLE_FUNCTION =   QE_STYLE_FUNCTION,
    ELIXIR_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
};

static void elixir_colorize_line(QEColorizeContext *cp,
                                 unsigned int *str, int n, ModeDef *syn)
{
    int i = 0, start = i, c, style = 0, sep, klen, nc, has_under;
    int state = cp->colorize_state;
    char kbuf[64];

    if (state & IN_ELIXIR_STRING)
        goto parse_string;
    if (state & IN_ELIXIR_REGEX)
        goto parse_regex;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            i = n;
            style = ELIXIR_STYLE_COMMENT;
            break;

        case '?':
            i = erlang_match_char(str, i);
            style = ELIXIR_STYLE_CHARCONST;
            break;

        case '~':
            if (qe_tolower(str[i]) == 'r') {
                nc = qe_indexof(elixir_delim1, str[i + 1]);
                if (nc >= 0) {
                    i += 2;
                    state = IN_ELIXIR_REGEX | nc;
                    if (nc < 2) { /* '\'' or '\"' */
                        if (str[i + 0] == (unsigned int)c
                        &&  str[i + 1] == (unsigned int)c) {
                            state |= IN_ELIXIR_TRIPLE;
                            i += 2;
                        }
                    }
                parse_regex:
                    sep = elixir_delim2[state & 15];
                    /* parse regular expression */
                    while (i < n) {
                        if ((c = str[i++]) == '\\') {
                            if (i < n)
                                i += 1;
                            continue;
                        }
                        if (c == sep) {
                            if (!(state & IN_ELIXIR_TRIPLE)) {
                                state = 0;
                                break;
                            }
                            if (str[i] == (unsigned int)sep
                            &&  str[i + 1] == (unsigned int)sep) {
                                i += 2;
                                state = 0;
                                break;
                            }
                        }
                    }
                    while (qe_islower(str[i])) {
                        /* regex suffix */
                        i++;
                    }
                    style = ELIXIR_STYLE_REGEX;
                    break;
                }
            }
            continue;

        case '\'':
        case '\"':
            /* parse string constants and here documents */
            state = IN_ELIXIR_STRING | (c == '\"');
            if (str[i + 0] == (unsigned int)c
            &&  str[i + 1] == (unsigned int)c) {
                /* here documents */
                state |= IN_ELIXIR_TRIPLE;
                i += 2;
            }
        parse_string:
            sep = elixir_delim2[state & 15];
            style = (state & IN_ELIXIR_TRIPLE) ?
                ELIXIR_STYLE_HEREDOC : ELIXIR_STYLE_STRING;
            while (i < n) {
                c = str[i++];
                if (c == '\\') {
                    if (i < n)
                        i += 1;
                    continue;
                }
                /* XXX: should colorize <% %> expressions and interpolation */
                if (c == sep) {
                    if (!(state & IN_ELIXIR_TRIPLE)) {
                        state = 0;
                        break;
                    }
                    if (str[i] == (unsigned int)sep
                    &&  str[i + 1] == (unsigned int)sep) {
                        i += 2;
                        state = 0;
                        break;
                    }
                }
            }
            break;

        case '@':
        case ':':
            if (qe_isalpha(str[i]))
                goto has_alpha;
            continue;

        case '<':
            if (str[i] == '%') {
                i++;
                if (str[i] == '=')
                    i++;
                style = ELIXIR_STYLE_PREPROCESS;
                break;
            }
            continue;

        case '%':
            if (str[i] == '>') {
                i++;
                style = ELIXIR_STYLE_PREPROCESS;
                break;
            }
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
                    for (has_under = 0;; i++) {
                        if (qe_isdigit(str[i]))
                            continue;
                        if (str[i] == '_' && qe_isdigit(str[i + 1])) {
                            /* integers may contain embedded _ characters */
                            has_under = 1;
                            i++;
                            continue;
                        }
                        break;
                    }
                    if (!has_under && str[i] == '.' && qe_isdigit(str[i + 1])) {
                        i += 2;
                        /* decimal floats require a digit after the '.' */
                        for (; qe_isdigit(str[i]); i++)
                            continue;
                        /* exponent notation requires a decimal point */
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
                }
                style = ELIXIR_STYLE_NUMBER;
                break;
            }
            if (qe_isalpha_(c)) {
        has_alpha:
                klen = 0;
                kbuf[klen++] = c;
                for (; qe_isalnum_(c = str[i]); i++) {
                    if (klen < countof(kbuf) - 1)
                        kbuf[klen++] = c;
                }
                if (c == '!' || c == '?') {
                    i++;
                    if (klen < countof(kbuf) - 1)
                        kbuf[klen++] = c;
                }
                kbuf[klen] = '\0';

                style = 0;
                if (kbuf[0] == '@') {
                    style = ELIXIR_STYLE_PREPROCESS;
                } else
                if (kbuf[0] == ':') {
                    style = ELIXIR_STYLE_ATOM;
                } else
                if (strfind(syn->keywords, kbuf)) {
                    style = ELIXIR_STYLE_KEYWORD;
                } else
                if (c == ':') {
                    style = ELIXIR_STYLE_TAG;
                } else
                if (check_fcall(str, i)) {
                    style = ELIXIR_STYLE_FUNCTION;
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
    cp->colorize_state = state;
}

static ModeDef elixir_mode = {
    .name = "Elixir",
    .extensions = "ex|exs",
    .shell_handlers = "elixir",
    .keywords = elixir_keywords,
    .colorize_func = elixir_colorize_line,
};

static int elixir_init(void)
{
    qe_register_mode(&elixir_mode, MODEF_SYNTAX);

    return 0;
}

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
                                unsigned int *str, int n, ModeDef *syn)
{
    char keyword[MAX_KEYWORD_SIZE];
    int i = 0, start = i, c, k, style, len;
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
        SET_COLOR(str, start, i, OCAML_STYLE_PREPROCESS);
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
            SET_COLOR(str, start, i, style);
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
            SET_COLOR(str, start, i, style);
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
            SET_COLOR(str, start, i, style);
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
                        int k = i + 1;
                        if (str[k] == '+' || str[k] == '-')
                            k++;
                        if (qe_isdigit(str[k])) {
                            for (i = k + 1; qe_isdigit_(str[i]); i++)
                                continue;
                        }
                    }
                }
            }
            SET_COLOR(str, start, i, style);
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
                k = i;
                if (qe_isblank(str[k]))
                    k++;
                if (str[k] == '(' && str[k + 1] != '*')
                    style = OCAML_STYLE_FUNCTION;
            }
            SET_COLOR(str, start, i, style);
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

static int ocaml_init(void)
{
    qe_register_mode(&ocaml_mode, MODEF_SYNTAX);

    return 0;
}

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

static int eff_init(void)
{
    qe_register_mode(&eff_mode, MODEF_SYNTAX);

    return 0;
}

/*---------------- EMF (JASSPA microemacs macro files) ----------------*/

static char const emf_keywords[] = {
    "define-macro|!emacro|!if|!elif|!else|!endif|!while|!done|"
    "!repeat|!until|!force|!return|!abort|!goto|!jump|!bell|"
};

static char const emf_types[] = {
    "|"
};

enum {
    EMF_STYLE_TEXT =       QE_STYLE_DEFAULT,
    EMF_STYLE_COMMENT =    QE_STYLE_COMMENT,
    EMF_STYLE_STRING =     QE_STYLE_STRING,
    EMF_STYLE_KEYWORD =    QE_STYLE_KEYWORD,
    EMF_STYLE_TYPE =       QE_STYLE_TYPE,
    EMF_STYLE_FUNCTION =   QE_STYLE_FUNCTION,
    EMF_STYLE_NUMBER =     QE_STYLE_NUMBER,
    EMF_STYLE_VARIABLE =   QE_STYLE_VARIABLE,
    EMF_STYLE_IDENTIFIER = QE_STYLE_DEFAULT,
    EMF_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
};

static void emf_colorize_line(QEColorizeContext *cp,
                              unsigned int *str, int n, ModeDef *syn)
{
    char keyword[MAX_KEYWORD_SIZE];
    int i = 0, start, c, nw = 1, len, style;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '-':
            if (qe_isdigit(str[i]))
                goto number;
            break;
        case ';':
            i = n;
            SET_COLOR(str, start, i, EMF_STYLE_COMMENT);
            continue;
        case '\"':
            /* parse string const */
            while (i < n) {
                if (str[i] == '\\' && i + 1 < n) {
                    i += 2; /* skip escaped char */
                    continue;
                }
                if (str[i++] == '\"')
                    break;
            }
            SET_COLOR(str, start, i, EMF_STYLE_STRING);
            continue;
        default:
            break;
        }
        /* parse numbers */
        if (qe_isdigit(c)) {
        number:
            for (; i < n; i++) {
                if (!qe_isalnum(str[i]))
                    break;
            }
            SET_COLOR(str, start, i, EMF_STYLE_NUMBER);
            continue;
        }
        /* parse identifiers and keywords */
        if (c == '$' || c == '!' || c == '#' || qe_isalpha_(c)) {
            len = 0;
            keyword[len++] = c;
            for (; qe_isalnum_(str[i]) || str[i] == '-'; i++) {
                if (len < countof(keyword) - 1)
                    keyword[len++] = str[i];
            }
            keyword[len] = '\0';
            if (c == '$' || c == '#') {
                style = EMF_STYLE_VARIABLE;
            } else
            if (strfind(syn->keywords, keyword)) {
                style = EMF_STYLE_KEYWORD;
            } else
            if (strfind(syn->types, keyword)) {
                style = EMF_STYLE_TYPE;
            } else
            if (nw++ == 1) {
                style = EMF_STYLE_FUNCTION;
            } else {
                style = EMF_STYLE_IDENTIFIER;
            }
            SET_COLOR(str, start, i, style);
            continue;
        }
    }
}

static ModeDef emf_mode = {
    .name = "emf",
    .extensions = "emf",
    .keywords = emf_keywords,
    .types = emf_types,
    .colorize_func = emf_colorize_line,
};

static int emf_init(void)
{
    qe_register_mode(&emf_mode, MODEF_SYNTAX);

    return 0;
}

/*---------------- AGENA script coloring ----------------*/

#define AGN_SHORTSTRINGDELIM " ,~[]{}();:#'=?&%$\xA7\\!^@<>|\r\n\t"

enum {
    IN_AGENA_COMMENT = 0x01,
    IN_AGENA_STRING1 = 0x02,
    IN_AGENA_STRING2 = 0x04,
};

enum {
    AGENA_STYLE_TEXT =       QE_STYLE_DEFAULT,
    AGENA_STYLE_COMMENT =    QE_STYLE_COMMENT,
    AGENA_STYLE_STRING =     QE_STYLE_STRING,
    AGENA_STYLE_NUMBER =     QE_STYLE_NUMBER,
    AGENA_STYLE_KEYWORD =    QE_STYLE_KEYWORD,
    AGENA_STYLE_TYPE =       QE_STYLE_TYPE,
    AGENA_STYLE_FUNCTION =   QE_STYLE_FUNCTION,
    AGENA_STYLE_IDENTIFIER = QE_STYLE_DEFAULT,
};

static char const agena_keywords[] = {
    /* abs alias and antilo2 antilog10 arccos arcsec arcsin arctan as
    assigned atendof bea bottom break by bye case catch char cis clear cls
    conjugate copy cos cosh cosxx create dec delete dict div do downto
    duplicate elif else end entier enum esac even exchange exp fail false fi
    filled first finite flip for from global if imag import in inc infinity
    insert int intersect into is join keys last left ln lngamma local lower
    minus mul nan nand nargs nor not numeric od of onsuccess or pop proc
    qmdev qsadd real redo reg relaunch replace restart return right rotate
    sadd seq shift sign signum sin sinc sinh size skip smul split sqrt
    subset tan tanh then to top trim true try type typeof unassigned
    undefined union unique until upper values when while xor xsubset
    yrt */

    /* keywords */
    "|alias|as|bottom|break|by|case|catch|clear|cls|create|dec|delete"
    "|dict|div|do|duplicate|elif|else|end|enum|epocs|esac|external|exchange"
    "|fi|for|from|if|import|inc|insert|into|is|keys|mul|nargs"
    "|od|of|onsuccess|pop|proc|quit|redo|reg|relaunch|return|rotate"
    "|scope|seq|skip|then|try|to|top|try|until|varargs"
    "|when|while|yrt"
    "|readlib"
    /* constants */
    "|infinity|nan|I"
    /* operators */
    "|or|xor|nor|and|nand|in|subset|xsubset|union|minus|intersect|atendof"
    "|split|shift|not"
    "|assigned|unassigned|size|type|typeof|left|right|filled|finite"
    "|"
};

static char const agena_types[] = {
    "|boolean|complex|lightuserdata|null|number|pair|register|procedure"
    "|sequence|set|string|table|thread|userdata"
    "|global|local|char|float|undefined|true|false|fail"
    "|"
};

static void agena_colorize_line(QEColorizeContext *cp,
                                unsigned int *str, int n, ModeDef *syn)
{
    char kbuf[MAX_KEYWORD_SIZE];
    int i = 0, start = i, c, style = 0, sep = 0;
    int state = cp->colorize_state;

    if (state & IN_AGENA_COMMENT)
        goto parse_block_comment;
    if (state & IN_AGENA_STRING1)
        goto parse_string1;
    if (state & IN_AGENA_STRING2)
        goto parse_string2;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            if (str[i] == '/') {
                /* block comment */
                i++;
            parse_block_comment:
                state |= IN_AGENA_COMMENT;
                for (; i < n; i++) {
                    if (str[i] == '/' && str[i + 1] == '#') {
                        i += 2;
                        state &= ~IN_AGENA_COMMENT;
                        break;
                    }
                }
            } else {
                i = n;
            }
            style = AGENA_STYLE_COMMENT;
            break;
        case '\"':
            state = IN_AGENA_STRING2;
        parse_string2:
            sep = '\"';
            goto parse_string;
        case '\'':
            state = IN_AGENA_STRING1;
        parse_string1:
            sep = '\'';
            /* parse string const */
        parse_string:
            for (; i < n; i++) {
                if (str[i] == '\\' && i + 1 < n) {
                    i++;
                    continue;
                }
                if (str[i] == (unsigned int)sep) {
                    state = 0;
                    i++;
                    break;
                }
            }
            style = AGENA_STYLE_STRING;
            break;
        case '`':   /* short string */
            while (i < n && !qe_findchar(AGN_SHORTSTRINGDELIM, str[i]))
                i++;
            style = AGENA_STYLE_IDENTIFIER;
            break;
        default:
            /* parse identifiers and keywords */
            if (qe_isalpha_(c)) {
                i += ustr_get_identifier(kbuf, countof(kbuf), c, str, i, n);
                if (strfind(syn->keywords, kbuf))
                    style = AGENA_STYLE_KEYWORD;
                else
                if (strfind(syn->types, kbuf))
                    style = AGENA_STYLE_TYPE;
                else
                if (check_fcall(str, i))
                    style = AGENA_STYLE_FUNCTION;
                else
                    style = AGENA_STYLE_IDENTIFIER;
                break;
            }
            if (qe_isdigit(c) || (c == '.' && qe_isdigit(str[i]))) {
                while (qe_isdigit_(str[i]) || str[i] == '\'' || str[i] == '.')
                    i++;
                if (qe_findchar("eE", str[i])) {
                    i++;
                    if (qe_findchar("+-", str[i]))
                        i++;
                }
                while (qe_isalnum(str[i]))
                    i++;
                style = AGENA_STYLE_NUMBER;
                break;
            }
            continue;
        }
        if (style) {
            SET_COLOR(str, start, i, style);
            style = 0;
        }
    }
    cp->colorize_state = state;
}

static ModeDef agena_mode = {
    .name = "Agena",
    .extensions = "agn",
    .keywords = agena_keywords,
    .types = agena_types,
    .colorize_func = agena_colorize_line,
};

static int agena_init(void)
{
    qe_register_mode(&agena_mode, MODEF_SYNTAX);

    return 0;
}

/*----------------*/

static int extra_modes_init(void)
{
    asm_init();
    ini_init();
    sharp_init();
    ps_init();
    julia_init();
    coffee_init();
    erlang_init();
    elixir_init();
    ocaml_init();
    eff_init();
    emf_init();
    agena_init();
    return 0;
}

qe_module_init(extra_modes_init);
