/*
 * C mode for QEmacs.
 *
 * Copyright (c) 2001-2002 Fabrice Bellard.
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

/* C mode options */
#define CLANG_C_TYPES     0x00080   /* add C types to syn->types list */
#define CLANG_C_KEYWORDS  0x00100   /* add C keywords to syn->keywords list */
#define CLANG_LEX         0x00200
#define CLANG_YACC        0x00400
#define CLANG_REGEX       0x00800   /* recognize / delimited regular expressions */
#define CLANG_WLITERALS   0x01000   /* recognize L, u, U, u8 string prefix */
#define CLANG_PREPROC     0x02000   /* colorize preprocessing directives */
#define CLANG_CAP_TYPE    0x04000   /* Mixed case initial cap is type */
#define CLANG_STR3        0x08000   /* Support """strings""" */
#define CLANG_LINECONT    0x10000   /* support \<newline> as line continuation */
#define CLANG_NEST_COMMENTS  0x20000  /* block comments are nested */
#define CLANG_T_TYPES     0x40000   /* _t suffix indicates type identifier */

/* FIXME: need flags for
   '@' in identifiers(start / next),
   '$' in identifiers(start / next),
   '-' in identifiers next
   '#' as comment char
   UnicodeIDStart and UnicodeIDContinue in identifiers
 */

/* all C language features */
#define CLANG_CC          (CLANG_LINECONT | CLANG_WLITERALS | CLANG_PREPROC | \
                           CLANG_C_KEYWORDS | CLANG_C_TYPES | CLANG_T_TYPES)

static const char c_keywords[] = {
    "auto|break|case|const|continue|default|do|else|enum|extern|for|goto|"
    "if|inline|register|restrict|return|sizeof|static|struct|switch|"
    "typedef|union|volatile|while|"
    /* C99 and C11 keywords */
    "_Alignas|_Alignof|_Atomic|_Generic|_Noreturn|_Pragma|"
    "_Static_assert|_Thread_local|"
    /* C2x keywords */
    "alignas|alignof|static_assert|thread_local|"
    "constexpr|false|nullptr|true|typeof|typeof_unqual"
};

static const char c_types[] = {
    "char|double|float|int|long|unsigned|short|signed|void|"
    /* common types */
    "FILE|va_list|jmp_buf|"
    /* C99 and C11 types */
    "_Bool|_Complex|_Imaginary|bool|complex|imaginary|"
    /* C2x types */
    "_BitInt|_Decimal128|_Decimal32|_Decimal64|"
};

static const char c_extensions[] = {
    "c|h|i|C|H|I|"      /* C language */
    /* Other C flavors */
    "e|"                /* EEL */
    "ecp|"              /* Informix embedded C */
    "pgc|"              /* Postgres embedded C */
    "pcc|"              /* Oracle C++ */
    "h.in|c.in|"        /* preprocessed C input (should use more generic approach) */
};

static int is_c_identifier_start(char32_t c, int flavor) {
    // should accept unicode escape sequence, UnicodeIDStart
    return (qe_isalpha_(c)
        ||  c == '$'
        ||  (c == '@' && flavor != CLANG_PIKE)
        ||  (flavor == CLANG_RUST && c >= 128));
}

static int is_c_identifier_part(char32_t c, int flavor) {
    // should accept unicode escape sequence, UnicodeIDContinue, ZWJ or ZWNJ
    return (qe_isalnum_(c)
        ||  (c == '-' && flavor == CLANG_CSS)
        ||  (flavor == CLANG_RUST && c >= 128));
}

// FIXME: should merge into ustr_get_identifier()
int get_c_identifier(char *dest, int size, char32_t c,
                     const char32_t *str, int i0, int n, int flavor)
{
    /*@API utils
       Grab an identifier from a `char32_t` buffer for a given C flavor,
       accept non-ASCII identifiers and encode in UTF-8.
       @argument `dest` a pointer to the destination array
       @argument `size` the length of the destination array in bytes
       @argument `c` the initial code point or `0` if none
       @argument `str` a valid pointer to an array of codepoints
       @argument `i` the index to the next codepoint
       @argument `n` the length of the codepoint array
       @argument `flavor` the language variant for identifier syntax
       @return the number of codepoints used in the source array.
       @note `dest` can be a null pointer if `size` is `0`.
     */
    int pos = 0, i = i0;

    if (c == 0) {
        if (!(i < n && is_c_identifier_start(c = str[i++], flavor))) {
            if (size > 0)
                *dest = '\0';
            return 0;
        }
    }
    for (;; i++) {
        if (c < 128) {
            if (pos + 1 < size) {
                dest[pos++] = c;
            }
        } else {
            char buf[6];
            int len = utf8_encode(buf, c);
            if (pos + len < size) {
                memcpy(dest + pos, buf, len);
                pos += len;
            } else {
                size = pos + 1;
            }
        }
        if (i >= n)
            break;
        c = str[i];
        if (!is_c_identifier_part(c, flavor)) {
            /* include ugly c++ :: separator in identifier */
            if (c == ':' && str[i + 1] == ':'
            &&  (flavor == CLANG_CPP || flavor == CLANG_C3)
            &&  is_c_identifier_start(str[i + 2], flavor)) {
                if (pos + 1 < size)
                    dest[pos++] = ':';
                if (pos + 1 < size)
                    dest[pos++] = ':';
                i += 2;
                c = str[i];
            } else {
                break;
            }
        }
    }
    if (pos < size)
        dest[pos] = '\0';
    return i - i0;
}

enum {
    C_STYLE_DEFAULT    = 0,
    C_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    C_STYLE_COMMENT    = QE_STYLE_COMMENT,
    C_STYLE_REGEX      = QE_STYLE_STRING_Q,
    C_STYLE_STRING     = QE_STYLE_STRING,
    C_STYLE_STRING_Q   = QE_STYLE_STRING_Q,
    C_STYLE_STRING_BQ  = QE_STYLE_STRING,
    C_STYLE_NUMBER     = QE_STYLE_NUMBER,
    C_STYLE_KEYWORD    = QE_STYLE_KEYWORD,
    C_STYLE_TYPE       = QE_STYLE_TYPE,
    C_STYLE_FUNCTION   = QE_STYLE_FUNCTION,
    C_STYLE_VARIABLE   = QE_STYLE_VARIABLE,
};

/* c-mode colorization states */
enum {
    IN_C_COMMENT    = 0x03,  /* one of the comment styles */
    IN_C_COMMENT1   = 0x01,  /* single line comment with \ at EOL */
    IN_C_COMMENT2   = 0x02,  /* multiline C comment */
    IN_C_COMMENT3   = 0x03,  /* multiline D comment */
    IN_C_STRING     = 0x1C,  /* 3 bits for string styles */
    IN_C_STRING_D   = 0x04,  /* double-quoted string */
    IN_C_STRING_Q   = 0x08,  /* single-quoted string */
    IN_C_STRING_BQ  = 0x0C,  /* back-quoted string (go's multi-line string) */
    IN_C_STRING_D3  = 0x14,  /* """ multiline quoted string */
    IN_C_STRING_Q3  = 0x18,  /* ''' multiline quoted string */
    IN_C_STRING_BQ3 = 0x1C,  /* ``` multiline quoted string */
    IN_C_PREPROCESS = 0x20,  /* preprocessor directive with \ at EOL */
    IN_C_REGEX      = 0x40,  /* regex */
    IN_C_CHARCLASS  = 0x80,  /* regex char class */
    IN_C_COMMENT_SHIFT = 8,  /* shift for block comment nesting level */
    IN_C_COMMENT_LEVEL = 0x700, /* mask for block comment nesting level */
};

static void c_colorize_line(QEColorizeContext *cp,
                            const char32_t *str, int n,
                            QETermStyle *sbuf, ModeDef *syn)
{
    int i = 0, start, i1, i2, indent, level;
    int style, style0, style1, type_decl, tag;
    char32_t c, delim;
    char kbuf[64];
    int mode_flags = syn->colorize_flags;
    int flavor = (mode_flags & CLANG_FLAVOR);
    int state = cp->colorize_state;

    indent = cp_skip_blanks(str, 0, n);
    tag = !indent && cp->s->mode == syn;
    start = i;
    type_decl = 0;
    c = 0;
    style0 = style = C_STYLE_DEFAULT;

    if (i >= n)
        goto the_end;

    if (state) {
        /* if already in a state, go directly in the code parsing it */
        if (state & IN_C_PREPROCESS)
            style0 = style = C_STYLE_PREPROCESS;
        if (state & IN_C_COMMENT) {
            if ((state & IN_C_COMMENT) == IN_C_COMMENT1)
                goto parse_comment1;
            else
            if ((state & IN_C_COMMENT) == IN_C_COMMENT2)
                goto parse_comment2;
            else
                goto parse_comment3;
        }
        switch (state & IN_C_STRING) {
        case IN_C_STRING_D: goto parse_string;
        case IN_C_STRING_Q: goto parse_string_q;
        case IN_C_STRING_BQ: goto parse_string_bq;
        case IN_C_STRING_D3: goto parse_string3;
        }
        if (state & IN_C_REGEX) {
            delim = '/';
            goto parse_regex;
        }
    }

    while (i < n) {
        start = i;
    reswitch:
        c = str[i++];
        switch (c) {
        case '*':
            /* lone star at the beginning of a line in a shell buffer
             * is treated as a comment start.  This improves colorization
             * of diff and git output.
             */
            if (start == indent && cp->partial_file
            &&  (i == n || str[i] == ' ' || str[i] == '/')) {
                i--;
                goto parse_comment2;
            }
            break;
        case '/':
            if (str[i] == '*') {
                /* C style multi-line comment */
                i++;
            parse_comment2:
                state |= IN_C_COMMENT2;
                style = C_STYLE_COMMENT;
                level = (state & IN_C_COMMENT_LEVEL) >> IN_C_COMMENT_SHIFT;
                while (i < n) {
                    if (str[i] == '/' && str[i + 1] == '*' && (mode_flags & CLANG_NEST_COMMENTS)) {
                        i += 2;
                        level++;
                    } else
                    if (str[i] == '*' && str[i + 1] == '/') {
                        i += 2;
                        if (level == 0) {
                            state &= ~IN_C_COMMENT2;
                            style = style0;
                            break;
                        }
                        level--;
                    } else {
                        i++;
                    }
                }
                state = (state & ~IN_C_COMMENT_LEVEL) |
                        (min_int(level, 7) << IN_C_COMMENT_SHIFT);
                SET_STYLE(sbuf, start, i, C_STYLE_COMMENT);
                continue;
            } else
            if (str[i] == '/') {
                /* line comment */
            parse_comment1:
                state |= IN_C_COMMENT1;
                style = C_STYLE_COMMENT;
                if (str[n - 1] != '\\')
                    state &= ~IN_C_COMMENT1;
                i = n;
                SET_STYLE(sbuf, start, i, C_STYLE_COMMENT);
                continue;
            }
            if (flavor == CLANG_D && (str[i] == '+')) {
                /* D language nesting long comment */
                i++;
                state |= IN_C_COMMENT3;
            parse_comment3:
                style = C_STYLE_COMMENT;
                level = (state & IN_C_COMMENT_LEVEL) >> IN_C_COMMENT_SHIFT;
                while (i < n) {
                    if (str[i] == '/' && str[i + 1] == '+') {
                        i += 2;
                        level++;
                    } else
                    if (str[i] == '+' && str[i + 1] == '/') {
                        i += 2;
                        if (level == 0) {
                            state &= ~IN_C_COMMENT3;
                            style = style0;
                            break;
                        }
                        level--;
                    } else {
                        i++;
                    }
                }
                state = (state & ~IN_C_COMMENT_LEVEL) |
                        (min_int(level, 7) << IN_C_COMMENT_SHIFT);
                SET_STYLE(sbuf, start, i, C_STYLE_COMMENT);
                continue;
            }
            if (mode_flags & CLANG_REGEX) {
                /* XXX: should use more context to tell regex from divide */
                char32_t prev = ' ';
                for (i1 = start; i1 > indent; ) {
                    prev = str[--i1];
                    if (!qe_isblank(prev))
                        break;
                }
                /* ignore end of comment in grep output */
                if (start > indent && str[start - 1] == '*' && cp->partial_file)
                    break;

                if (!qe_findchar("])", prev)
                &&  (qe_findchar(" [({},;=<>!~^&|*/%?:", prev)
                ||   sbuf[i1] == C_STYLE_KEYWORD
                ||   (str[i] != ' ' && (str[i] != '=' || str[i + 1] != ' ')
                &&    !(qe_isalnum(prev) || prev == ')')))) {
                    /* parse regex */
                    state |= IN_C_REGEX;
                    delim = '/';
                parse_regex:
                    style = C_STYLE_REGEX;
                    while (i < n) {
                        c = str[i++];
                        if (c == '\\') {
                            if (i < n) {
                                i += 1;
                            }
                        } else
                        if (state & IN_C_CHARCLASS) {
                            if (c == ']') {
                                state &= ~IN_C_CHARCLASS;
                            }
                            /* ECMA 5: ignore '/' inside char classes */
                        } else {
                            if (c == '[') {
                                state |= IN_C_CHARCLASS;
                            } else
                            if (c == delim) {
                                while (qe_isalnum_(str[i])) {
                                    i++;
                                }
                                state &= ~IN_C_REGEX;
                                style = style0;
                                break;
                            }
                        }
                    }
                    SET_STYLE(sbuf, start, i, C_STYLE_REGEX);
                    continue;
                }
            }
            break;
        case '%':
            if (flavor == CLANG_JED) {
                goto parse_comment1;
            }
            break;
        case '#':       /* preprocessor */
            if (start == 0 && str[i] == '!') {
                /* recognize a shebang comment line */
                style = style0 = C_STYLE_PREPROCESS;
                i = n;
                SET_STYLE(sbuf, start, i, C_STYLE_PREPROCESS);
                break;
            }
            if (flavor == CLANG_AWK || flavor == CLANG_PHP
            ||  flavor == CLANG_LIMBO || flavor == CLANG_SQUIRREL) {
                goto parse_comment1;
            }
            if (flavor == CLANG_ICI) {
                delim = '#';
                goto parse_regex;
            }
            if (flavor == CLANG_HAXE || flavor == CLANG_CBANG) {
                i += get_c_identifier(kbuf, countof(kbuf), 0, str, i, n, flavor);
                // XXX: check for proper preprocessor directive?
                SET_STYLE(sbuf, start, i, C_STYLE_PREPROCESS);
                continue;
            }
            if (flavor == CLANG_PIKE) {
                int klen;
                if (str[i] == '\"') {
                    i++;
                    goto parse_string; // FIXME: accept embedded newlines
                }
                if (str[i] == '(') {
                    // FIXME: parse literal strings until `#)`
                }
                if (str[i] == '[') {
                    // FIXME: parse literal strings until `#]`
                }
                if (str[i] == '{') {
                    // FIXME: parse literal strings until `#}`
                }
                if (ustr_match_keyword(str + i, "string", &klen)) {
                    /* Pike's version of #embed */
                    style = C_STYLE_PREPROCESS;
                    i += klen;
                    break;
                }
            }
            if (mode_flags & CLANG_PREPROC) {
                state |= IN_C_PREPROCESS;
                style = style0 = C_STYLE_PREPROCESS;
            }
            break;
        // case 'r':
            /* XXX: D language r" wysiwyg chars " */
        // case 'X':
            /* XXX: D language X" hex string chars " */
        // case 'q':
            /* XXX: D language q" delim wysiwyg chars delim " */
            /* XXX: D language q{ tokens } */
        case '\'':      /* character constant */
            if (flavor == CLANG_SCILAB)
                goto normal;
        parse_string_q:
            state |= IN_C_STRING_Q;
            style1 = C_STYLE_STRING_Q;
            delim = '\'';
            goto string;
        case '`':
            // FIXME: Support `operator in Pike
            if (flavor == CLANG_SCALA || flavor == CLANG_GMSCRIPT) {
                /* scala quoted identifier */
                while (i < n) {
                    c = str[i++];
                    if (c == '`')
                        break;
                }
                SET_STYLE(sbuf, start, i, C_STYLE_VARIABLE);
                continue;
            }
            if (flavor == CLANG_GO || flavor == CLANG_D) {
                /* go language multi-line string, no escape sequences */
            parse_string_bq:
                state |= IN_C_STRING_BQ;
                style1 = C_STYLE_STRING_BQ;
                delim = '`';
                while (i < n) {
                    c = str[i++];
                    if (c == delim) {
                        state &= ~IN_C_STRING;
                        break;
                    }
                }
                if (state & IN_C_PREPROCESS)
                    style1 = C_STYLE_PREPROCESS;
                SET_STYLE(sbuf, start, i, style1);
                continue;
            }
            break;
        case '@':
            if (flavor == CLANG_C2) {
                // XXX: should colorize attributes as C_STYLE_PREPROC
                // @(...)
            }
            if (flavor == CLANG_CSHARP || flavor == CLANG_SQUIRREL) {
                if (str[i] == '\"') {
                    /* Csharp and Squirrel Verbatim strings */
                    /* ignore escape sequences and newlines */
                    state |= IN_C_STRING_D;   // XXX: IN_RAW_STRING
                    style1 = C_STYLE_STRING;
                    delim = str[i++];
                    style = style1;
                    while (i < n) {
                        c = str[i++];
                        if (c == delim) {
                            if (str[i] == c) {
                                i++;
                                continue;
                            }
                            state &= ~IN_C_STRING;
                            style = style0;
                            break;
                        }
                    }
                    SET_STYLE(sbuf, start, i, style1);
                    continue;
                }
            }
            if ((flavor == CLANG_JAVA || flavor == CLANG_SCALA) && qe_isalpha(str[i])) {
                /* Java annotations */
                while (qe_isalnum_(str[i]) || str[i] == '.')
                    i++;
                if (start == 0 || str[start - 1] != '.')
                    SET_STYLE(sbuf, start, i, C_STYLE_PREPROCESS);
                continue;
            }
            goto normal;

        case '\"':      /* string literal */
            if ((mode_flags & CLANG_STR3)
            &&  (str[i] == '\"' && str[i + 1] == '\"')) {
                /* multiline """ quoted string */
                i += 2;
            parse_string3:
                state |= IN_C_STRING_D3;
                style1 = C_STYLE_STRING;
                delim = '\"';
                while (i < n) {
                    c = str[i++];
                    if (c == '\\' && flavor != CLANG_KOTLIN) {
                        if (i < n)
                            i++;
                    } else
                    if (c == delim && str[i] == delim && str[i + 1] == delim) {
                        i += 2;
                        state &= ~IN_C_STRING;
                        style = style0;
                        break;
                    }
                }
                SET_STYLE(sbuf, start, i, style1);
                continue;
            }
        parse_string:
            state |= IN_C_STRING_D;
            style1 = C_STYLE_STRING;
            delim = '\"';
        string:
            style = style1;
            while (i < n) {
                c = str[i++];
                if (c == '\\' && flavor != CLANG_SCILAB) {
                    if (i >= n)
                        break;
                    i++;
                } else
                if (c == delim) {
                    if (flavor == CLANG_SCILAB && str[i] == delim) {
                        i++;
                        continue;
                    }
                    state &= ~IN_C_STRING;
                    style = style0;
                    break;
                }
            }
            if (flavor == CLANG_D) {
                /* ignore optional string postfix */
                if (qe_findchar("cwd", str[i]))
                    i++;
            }
            if (state & IN_C_PREPROCESS)
                style1 = C_STYLE_PREPROCESS;
            SET_STYLE(sbuf, start, i, style1);
            continue;
        case '=':
            /* exit type declaration */
            /* does not handle this: int i = 1, j = 2; */
            type_decl = 0;
            break;
        case '<':       /* JavaScript extension */
            if (flavor == CLANG_JS) {
                if (str[i] == '!' && str[i + 1] == '-' && str[i + 2] == '-')
                    goto parse_comment1;
            }
            break;
        case '(':
        case '{':
            tag = 0;
            break;
        default:
        normal:
            if (state & IN_C_PREPROCESS)
                break;
            if (qe_isdigit(c) || (c == '.' && qe_isdigit(str[i]))) {
                /* XXX: parsing ppnumbers, which is OK for C and C++ */
                /* XXX: should parse actual number syntax */
                /* XXX: D ignores embedded '_' and accepts l,u,U,f,F,i suffixes */
                /* XXX: Java accepts 0b prefix for binary literals,
                 * ignores '_' between digits and accepts 'l' or 'L' suffixes */
                /* scala ignores '_' in integers */
                /* XXX: should parse decimal and hex floating point syntaxes */
                /* C23/C++ syntax:
                   - accept ' between digits including hex and exponent part
                   - new suffixes: wb, WB, df, dd, dl, DF, DD, DL */
                /* 2 dots in a row are a range operator, not a decimal point */
                while (qe_isalnum_(str[i])
                ||     (str[i] == '\'' && qe_isalnum(str[i + 1]))
                ||     (str[i] == '.' && str[i + 1] != '.')) {
                    i++;
                }
                SET_STYLE(sbuf, start, i, C_STYLE_NUMBER);
                continue;
            }
            if (is_c_identifier_start(c, flavor)) {
                i += get_c_identifier(kbuf, countof(kbuf), c, str, i, n, flavor);
                if (str[i] == '\'' || str[i] == '\"') {
                    /* check for encoding prefix */
                    if ((mode_flags & CLANG_WLITERALS) && strfind("L|u|U|u8", kbuf))
                        goto reswitch;
                }
                if (strfind(syn->keywords, kbuf)
                ||  ((mode_flags & CLANG_C_KEYWORDS) && strfind(c_keywords, kbuf))
                ||  ((flavor == CLANG_CSS) && str[i] == ':')) {
                    SET_STYLE(sbuf, start, i, C_STYLE_KEYWORD);
                    continue;
                }

                i1 = cp_skip_blanks(str, i, n);
                i2 = i1;
                while (str[i2] == '*' || qe_isblank(str[i2]))
                    i2++;

                if (tag && qe_findchar("({[,;=", str[i1])) {
                    eb_add_tag(cp->b, cp->offset + start, kbuf);
                }

                if ((start == 0 || str[start - 1] != '.')
                &&  (!qe_findchar(".(:", str[i]) || flavor == CLANG_PIKE)
                &&  (sreg_match(syn->types, kbuf, 1)
                ||   ((mode_flags & CLANG_C_TYPES) && strfind(c_types, kbuf))
                ||   ((mode_flags & CLANG_T_TYPES) && strend(kbuf, "_t", NULL))
                ||   ((mode_flags & CLANG_CAP_TYPE) && qe_isupper(c) && qe_haslower(kbuf))
                ||   (flavor == CLANG_HAXE && qe_isupper(c) && qe_haslower(kbuf) &&
                      (start == 0 || !qe_findchar("(", str[start - 1]))))) {
                    /* if not cast, assume type declaration */
                    if (str[i2] != ')') {
                        type_decl = 1;
                    }
                    style1 = C_STYLE_TYPE;
                    if (str[i1] == '(' && flavor != CLANG_PIKE) {
                        /* function style cast */
                        style1 = C_STYLE_FUNCTION; //C_STYLE_KEYWORD;
                    }
                    SET_STYLE(sbuf, start, i, style1);
                    continue;
                }
                if (str[i1] == '(') {
                    /* function call */
                    /* XXX: different styles for call and definition */
                    SET_STYLE(sbuf, start, i, C_STYLE_FUNCTION);
                    continue;
                }
                if ((mode_flags & CLANG_CC) || flavor == CLANG_JAVA) {
                    /* assume typedef if starting at first column */
                    if (start == 0 && qe_isalpha_(str[i]))
                        type_decl = 1;

                    if (type_decl) {
                        if (start == 0) {
                            /* assume type if first column */
                            SET_STYLE(sbuf, start, i, C_STYLE_TYPE);
                        } else {
                            SET_STYLE(sbuf, start, i, C_STYLE_VARIABLE);
                        }
                    }
                }
                continue;
            }
            break;
        }
        SET_STYLE1(sbuf, start, style);
    }
 the_end:
    if (state & (IN_C_COMMENT | IN_C_PREPROCESS | IN_C_STRING)) {
        /* set style on eol char */
        SET_STYLE1(sbuf, n, style);
    }

    /* strip state if not overflowing from a comment */
    if (!(state & IN_C_COMMENT) &&
        (!(mode_flags & CLANG_LINECONT) || n <= 0 || str[n - 1] != '\\')) {
        state &= ~IN_C_PREPROCESS;
    }
    cp->colorize_state = state;
}

#define MAX_STACK_SIZE  64

/* gives the position of the first non white space character in
   buf. TABs are counted correctly */
static int find_indent1(EditState *s, const char32_t *p) {
    int tw = s->b->tab_width > 0 ? s->b->tab_width : 8;
    int pos = 0;

    for (;;) {
        char32_t c = *p++;
        if (c == '\t')
            pos += tw - (pos % tw);
        else if (c == ' ')
            pos++;
        else if (c == '\f')
            pos = 0;
        else
            break;
    }
    return pos;
}

static int find_pos(EditState *s, char32_t *buf, int size) {
    int pos, tw, i;
    char32_t c;

    tw = s->b->tab_width > 0 ? s->b->tab_width : 8;
    pos = 0;
    for (i = 0; i < size; i++) {
        c = buf[i];
        if (c == '\t') {
            pos += tw - (pos % tw);
        } else {
            /* simplistic case: assume single width characters */
            pos++;
        }
    }
    return pos;
}

enum {
    INDENT_NORM,
    INDENT_FIND_EQ,
};

/* Normalize indentation at <offset>, return offset past indentation */
static int normalize_indent(EditState *s, int offset, int indent)
{
    int ntabs, nspaces, offset1, offset2;

    if (indent < 0)
        indent = 0;
    ntabs = 0;
    nspaces = indent;
    if (s->indent_tabs_mode) {
        int tw = s->b->tab_width > 0 ? s->b->tab_width : 8;
        ntabs = nspaces / tw;
        nspaces = nspaces % tw;
    }
    for (offset1 = offset;;) {
        char32_t c = eb_nextc(s->b, offset2 = offset1, &offset1);
        if (c == '\t') {
            if (offset == offset2 && ntabs) {
                ntabs--;
                offset = offset1;
            }
        } else
        if (c == ' ') {
            if (offset == offset2 && !ntabs && nspaces) {
                nspaces--;
                offset = offset1;
            }
        } else {
            break;
        }
    }
    if (offset2 > offset)
        eb_delete_range(s->b, offset, offset2);
    if (ntabs)
        offset += eb_insert_char32_n(s->b, offset, '\t', ntabs);
    if (nspaces)
        offset += eb_insert_spaces(s->b, offset, nspaces);
    return offset;
}

/* Check if line starts with a label or a switch case */
static int c_line_has_label(EditState *s, const char32_t *buf, int len,
                            const QETermStyle *sbuf)
{
    char kbuf[64];
    int i, style;

    i = cp_skip_blanks(buf, 0, len);

    style = sbuf[i];
    if (style == C_STYLE_COMMENT
    ||  style == C_STYLE_STRING
    ||  style == C_STYLE_STRING_Q
    ||  style == C_STYLE_PREPROCESS)
        return 0;

    i += get_c_identifier(kbuf, countof(kbuf), 0, buf, i, len, CLANG_C);
    if (style == C_STYLE_KEYWORD && strfind("case|default", kbuf))
        return 1;
    i = cp_skip_blanks(buf, i, len);
    return (buf[i] == ':');
}

/* indent a line of C code starting at <offset> */
/* Rationale:
   - determine the current line indentation from the current expression or
     the indentation of the previous complete statement.
   - scan backwards previous lines, ignoring blank and comment lines.
   - scan backwards for a block start, skipping fully bracketed code fragments
     or a previous statement end and the ne before that.
   - if the block start char is a `(`, try and align the line at the same offset
     as the code fragment following the `(`.
   - if the block start char is a `{` or a `[`, indent the line one level down from
     the line where the block starts
   - if the previous statement is an `if|else|do|for|while|switch`, indent the line one level
   - if the line starts with a label, unindent by one level + c_label_offset
   - if the previous line starts with a label, increment the previous indent by one level - c_label_offset
   - by default, indent the line like the previous code line,
*/
void c_indent_line(EditState *s, int offset0)
{
    QEColorizeContext cp[1];
    int offset, offset1, offsetl, pos, line_num, col_num;
    int i, eoi_found, len, pos1, lpos, style, line_num1, state;
    int off, found_comma, has_else;
    char32_t c;
    //int found_semi = 0;
    char32_t stack[MAX_STACK_SIZE];
    char kbuf[64], *q;
    int stack_ptr;

    cp_initialize(cp, s);

    /* find start of line */
    eb_get_pos(s->b, &line_num, &col_num, offset0);
    line_num1 = line_num;
    offset = eb_goto_bol(s->b, offset0);
    /* now find previous lines and compute indent */
    pos = 0;
    lpos = -1; /* position of the last instruction start */
    offsetl = offset;
    eoi_found = 0;
    found_comma = has_else = 0;
    stack_ptr = 0;
    state = INDENT_NORM;
    for (;;) {
        if (offsetl == 0)
            break;
        line_num--;
        offsetl = eb_prev_line(s->b, offsetl);
        /* XXX: deal with truncation */
        len = get_colorized_line(cp, offsetl, &offset1, line_num);
        /* get current indentation of this line, adjust if it has a label */
        pos1 = find_indent1(s, cp->buf);
        /* ignore empty and preprocessor lines */
        if (pos1 == len || cp->sbuf[0] == C_STYLE_PREPROCESS)
            continue;
        if (c_line_has_label(s, cp->buf, len, cp->sbuf)) {
            pos1 = pos1 - s->qs->c_label_indent + s->indent_width;
        }
        /* scan the line from end to start */
        for (off = len; off-- > 0;) {
            c = cp->buf[off];
            style = cp->sbuf[off];
            /* skip strings or comments */
            if (style == C_STYLE_COMMENT
            ||  style == C_STYLE_STRING
            ||  style == C_STYLE_STRING_Q
            ||  style == C_STYLE_PREPROCESS) {
                continue;
            }
            if (state == INDENT_FIND_EQ) {
                /* special case to search '=' or ; before { to know if
                   we are in data definition */
                /* XXX: why do we need this special case? */
                if (c == '=') {
                    /* data definition case */
                    pos = lpos;
                    goto end_parse;
                }
                if (c == ';') {
                    /* normal instruction case */
                    goto check_instr;
                }
                continue;
            }
            if (style == C_STYLE_KEYWORD) {
                int off0, off1;
                /* special case for if/for/while */
                off1 = off;
                while (off > 0 && cp->sbuf[off - 1] == C_STYLE_KEYWORD)
                    off--;
                off0 = off;
                if (stack_ptr == 0) {
                    q = kbuf;
                    while (q < kbuf + countof(kbuf) - 1 && off0 <= off1) {
                        *q++ = cp->buf[off0++];
                    }
                    *q = '\0';

                    if (!eoi_found && strfind("if|for|while|do|switch|foreach", kbuf)) {
                        pos = pos1 + s->indent_width;
                        goto end_parse;
                    }
                    if (has_else == 0)
                        has_else = strequal(kbuf, "else") ? 1 : -1;
                    lpos = pos1;
                }
            } else {
                if (has_else == 0)
                    has_else = -1;
                switch (c) {
                case '}':
                    if (stack_ptr < MAX_STACK_SIZE)
                        stack[stack_ptr] = c;
                    stack_ptr++;
                    goto check_instr;
                case '{':
                    if (stack_ptr == 0) {
                        /* start of a block: check if object definition or code block */
                        if (found_comma) {
                            pos = pos1;
                            eoi_found = 1;
                            goto end_parse;
                        }
                        if (lpos == -1) {
                            pos = pos1 + s->indent_width;
                            eoi_found = 1;
                            goto end_parse;
                        } else {
                            state = INDENT_FIND_EQ;
                        }
                    } else {
                        --stack_ptr;
                        if (stack_ptr < MAX_STACK_SIZE && stack[stack_ptr] != '}') {
                            /* XXX: syntax check ? */
                            /* XXX: should set mark and complain */
                            goto check_instr;
                        }
                        goto check_instr;
                    }
                    break;
                case ')':
                case ']':
                    if (stack_ptr < MAX_STACK_SIZE)
                        stack[stack_ptr] = c;
                    stack_ptr++;
                    break;
                case '(':
                case '[':
                    if (stack_ptr == 0) {
                        pos = find_pos(s, cp->buf, off) + 1;
                        goto end_parse;
                    } else {
                        char32_t matchc = (c == '(') ? ')' : ']';
                        --stack_ptr;
                        if (stack_ptr < MAX_STACK_SIZE && stack[stack_ptr] != matchc) {
                            /* XXX: syntax check ? */
                            /* XXX: should set mark and complain */
                            pos = pos1;
                            goto end_parse;
                        }
                    }
                    break;
                case ' ':
                case '\f':
                case '\t':
                case '\n':
                    break;
                case ',':
                    if (stack_ptr == 0) {
                        found_comma = 1;
                    }
                    break;
                //case '=':
                    // if (p[1] == ' ' && !eoi_found && stack_ptr == 0) {
                    //    pos = find_pos(s, buf, p - buf) + 2;
                    //    goto end_parse;
                    //}
                    //break;
                case ';':
                    /* level test needed for 'for(;;)' */
                    if (stack_ptr == 0) {
                        //found_semi = 1;
                        /* ; { or } are found before an instruction */
                    check_instr:
                        if (lpos >= 0) {
                            /* start of instruction already found */
                            pos = lpos;
                            if (!eoi_found)
                                pos += s->indent_width;
                            goto end_parse;
                        }
                        eoi_found = 1;
                    }
                    break;
                case ':':
                    /* a label line is ignored: regular, case and default labels
                     * are assumed to have no preceding space
                     */
                    /* XXX: should handle labels differently:
                       measure the indent and adjust by label_indent */
                    if (style == C_STYLE_DEFAULT
                    &&  (off == 0 || !qe_isspace(cp->buf[off - 1]))) {
                        off = 0;
                    }
                    break;
                default:
                    if (stack_ptr == 0)
                        lpos = pos1;
                    break;
                }
            }
        }
        if (pos1 == 0 && len > 0) {
            style = cp->sbuf[0];
            if (style != C_STYLE_COMMENT
            &&  style != C_STYLE_STRING
            &&  style != C_STYLE_STRING_Q
            &&  style != C_STYLE_PREPROCESS) {
                pos = 0;
                break;
            }
        }
    }
  end_parse:
    /* compute special cases which depend on the chars on the current line */
    /* XXX: deal with truncation */
    len = get_colorized_line(cp, offset, &offset1, line_num1);
    if (cp->sbuf[0] == C_STYLE_PREPROCESS)
        goto done;

    if (stack_ptr == 0) {
        if (!pos && lpos >= 0) {
            /* start of instruction already found */
            pos = lpos;
            if (!eoi_found)
                pos += s->indent_width;
        }
    }

    for (i = 0; i < len; i++) {
        c = cp->buf[i];
        if (qe_isblank(c))
            continue;
        /* no looping from here down */
        style = cp->sbuf[i];
        if (style == C_STYLE_STRING || style == C_STYLE_STRING_Q)
            break;
        /* if preprocess, no indent */
        /* XXX: should indent macro definitions and align continuation marks */
        if (style == C_STYLE_PREPROCESS) {
            pos = 0;
            break;
        }
        if (style == C_STYLE_COMMENT) {
            if (c == '/') {
                break;
            }
            if (c == '*') {
                pos += 1;
                break;
            }
            /* comment paragraph alignment should depend on previous line */
            pos += 3;
            break;
        }
        if (qe_isalpha_(c)) {
            if (has_else == 1 && cp->buf[i] == 'i' && cp->buf[i + 1] == 'f' && !qe_isalnum_(cp->buf[i + 2])) {
                /* unindent if after naked else */
                pos -= s->indent_width;
                break;
            }
            if (c_line_has_label(s, cp->buf + i, len - i, cp->sbuf + i)) {
                pos -= s->indent_width + s->qs->c_label_indent;
                break;
            }
            break;
        }
        /* NOTE: strings & comments are correctly ignored there */
        if (c == '}') {
            pos -= s->indent_width;
            break;
        }
        if ((c == '&' || c == '|') && cp->buf[i + 1] == c) {
#if 0
            int j;
            // XXX: should try and indent according to boolean expression depth
            for (j = i + 2; cp->buf[j] == ' '; j++)
                continue;
            if (j == len) {
                pos -= s->indent_width;
            } else {
                pos -= j - i + 2;
            }
#else
            pos -= s->indent_width;
#endif
            break;
        }
        if (c == '{') {
            if (pos == s->indent_width && !eoi_found) {
                pos = 0;
                break;
            }
            // XXX: need fix for GNU style
            pos -= s->indent_width;
        }
        break;
    }
    if (pos < 0) {
        pos = 0;
    }
    // if (i == len && state is IN_COMMENT) {
    //     /* XXX: should indent comment paragraphs */
    //     pos += 3;
    // }

    /* the computed indent is in 'pos' */
    /* if on a blank line, reset indent to 0 unless point is on it */
    if (eb_is_blank_line(s->b, offset, NULL)
    &&  !(s->offset >= offset && s->offset <= eb_goto_eol(s->b, offset))) {
        pos = 0;
    }
    /* Normalize indentation, minimize buffer modifications */
    offset1 = normalize_indent(s, offset, pos);
#if 0
    if (s->mode->auto_indent > 1) {  /* auto format */
        /* recompute colorization of the current line (after re-indentation) */
        len = get_colorized_line(cp, offset, &offset1, line_num1);
        /* skip indentation */
        for (pos = 0; qe_isblank(cp->buf[pos]); pos++)
            continue;
        /* XXX: keywords "if|for|while|switch -> one space before `(` */
        /* XXX: keyword "return" -> one space before expression */
        /* XXX: keyword "do" -> one space before '{' */
        /* XXX: other words -> no space before '(', '[', `++`, `--`, `->`, `.` */
        /* XXX: other words and sequences -> space before `*`, but not after */
        /* XXX: unary prefix operators: ! ~ - + & * ++ -- */
        /* XXX: postfix operators: ++ -- -> . [ */
        /* XXX: grouping operators: ( ) [ ] */
        /* XXX: binary operators: = == === != !== < > <= >= && ||
        ^ & | + - * / % << >> ^= &= |= += -= *= /= %= <<= >>= ? : */
        /* XXX: sequence operators: , ; */
    }
#endif
    /* move to the indentation if point was in indent space */
    if (s->offset >= offset && s->offset < offset1) {
        s->offset = offset1;
    }
done:
    cp_destroy(cp);
}

static void do_c_indent(EditState *s)
{
    QEmacsState *qs = s->qs;

    if (!s->region_style
    &&  !(s->b->flags & BF_PREVIEW)
    &&  qs->last_cmd_func != (CmdFunc)do_tabulate
    &&  eb_is_in_indentation(s->b, s->offset)) {
        c_indent_line(s, s->offset);
    } else {
        do_tabulate(s, 1);
    }
    qs->this_cmd_func = (CmdFunc)do_tabulate;
}

static void do_c_electric_key(EditState *s, int key)
{
    int offset = s->offset;
    int was_preview = s->b->flags & BF_PREVIEW;

    do_char(s, key, 1);
    if (was_preview)
        return;
    /* reindent line at original point */
    if (s->mode->auto_indent && s->mode->indent_func)
        (s->mode->indent_func)(s, eb_goto_bol(s->b, offset));
}

static void do_c_newline(EditState *s)
{
    int offset = s->offset;
    int was_preview = s->b->flags & BF_PREVIEW;

    /* XXX: should also remove trailing spaces on current line */
    do_newline(s);
    if (was_preview)
        return;

    /* reindent line to remove indent on blank line */
    if (s->mode->auto_indent && s->mode->indent_func) {
        /* delete blanks at end of line (necessary for non blank lines) */
        /* XXX: should factorize with do_delete_horizontal_space() */
        int from = offset, to = offset;
        while (qe_isblank(eb_prevc(s->b, from, &offset)))
            from = offset;
        eb_delete_range(s->b, from, to);
        offset = from;

        (s->mode->indent_func)(s, eb_goto_bol(s->b, offset));
        (s->mode->indent_func)(s, s->offset);
    }
}

/* forward / backward preprocessor */
static void c_forward_conditional(EditState *s, int dir)
{
    QEColorizeContext cp[1];
    char32_t *p;
    int line_num, col_num, sharp, level;
    int offset, offset0, offset1;

    cp_initialize(cp, s);
    offset = offset0 = eb_goto_bol(s->b, s->offset);
    eb_get_pos(s->b, &line_num, &col_num, offset);
    level = 0;
    for (;;) {
        get_colorized_line(cp, offset, &offset1, line_num);
        sharp = 0;
        for (p = cp->buf; *p; p++) {
            char32_t c = *p;
            int style = cp->sbuf[p - cp->buf];
            if (qe_isblank(c))
                continue;
            if (c == '#' && style == C_STYLE_PREPROCESS)
                sharp++;
            else
                break;
        }
        if (sharp == 1) {
            if (ustrstart(p, dir < 0 ? "endif" : "if", NULL)) {
                if (level || offset == offset0)
                    level++;
                else
                    break;
            } else
            if (ustrstart(p, "el", NULL)) {
                if (offset == offset0)
                    level++;
                else
                if (level <= 1)
                    break;
            } else
            if (ustrstart(p, dir > 0 ? "endif" : "if", NULL)) {
                if (level)
                    level--;
                if (!level && offset != offset0)
                    break;
            }
        }
        if (dir > 0) {
            line_num++;
            offset = offset1;
            if (offset >= s->b->total_size)
                break;
        } else {
            if (offset <= 0)
                break;
            line_num--;
            offset = eb_prev_line(s->b, offset);
        }
    }
    s->offset = offset;
    cp_destroy(cp);
}

static void do_c_forward_conditional(EditState *s, int n)
{
    int dir = n < 0 ? -1 : 1;
    for (; n != 0; n -= dir) {
        c_forward_conditional(s, dir);
    }
}

static void do_c_list_conditionals(EditState *s)
{
    QEColorizeContext cp[1];
    char32_t *p;
    int line_num, col_num, sharp, level;
    int offset, offset1;
    EditBuffer *b;

    b = qe_new_buffer(s->qs, "Preprocessor conditionals", BC_REUSE | BC_CLEAR | BF_UTF8);
    if (!b)
        return;

    cp_initialize(cp, s);
    offset = eb_goto_bol(s->b, s->offset);
    eb_get_pos(s->b, &line_num, &col_num, offset);
    level = 0;
    while (offset > 0) {
        line_num--;
        offset = eb_prev_line(s->b, offset);
        get_colorized_line(cp, offset, &offset1, line_num);
        sharp = 0;
        for (p = cp->buf; *p; p++) {
            char32_t c = *p;
            int style = cp->sbuf[p - cp->buf];
            if (qe_isblank(c))
                continue;
            if (c == '#' && style == C_STYLE_PREPROCESS)
                sharp++;
            else
                break;
        }
        if (sharp == 1) {
            if (ustrstart(p, "endif", NULL)) {
                level++;
            } else
            if (ustrstart(p, "el", NULL)) {
                if (level == 0) {
                    eb_insert_buffer_convert(b, 0, s->b, offset, offset1 - offset);
                }
            } else
            if (ustrstart(p, "if", NULL)) {
                if (level) {
                    level--;
                } else {
                    eb_insert_buffer_convert(b, 0, s->b, offset, offset1 - offset);
                }
            }
        }
    }
    if (b->total_size > 0) {
        show_popup(s, b, "Preprocessor conditionals");
    } else {
        eb_free(&b);
        put_error(s, "Not in a #if conditional");
    }
    cp_destroy(cp);
}

/* C mode specific commands */
static const CmdDef c_commands[] = {
    CMD2( "c-indent-line-or-region", "TAB",
          "Indent the current line or highlighted region",
          do_c_indent, ES, "*")
    CMD2( "c-backward-conditional", "M-[",
          "Move to the beginning of the previous #if preprocessing directive",
          do_c_forward_conditional, ESi, "q")
    CMD2( "c-forward-conditional", "M-]",
          "Move to the end of the next #if preprocessing directive",
          do_c_forward_conditional, ESi, "p")
    CMD0( "c-list-conditionals", "M-i",
          "List the preprocessing directive controlling the current line",
          do_c_list_conditionals)
    CMD2( "c-electric-key", "{, }, ;, :, #, &, |, *",
          "Insert a character with side effects",
          do_c_electric_key, ESi, "*" "k")
    CMD2( "c-newline", "RET, LF",
          "Insert a newline, removing trailing whitespace and autoindent",
          do_c_newline, ES, "*")
};

static int c_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    /* trust the file extension and/or shell handler */
    if (match_extension(p->filename, mode->extensions)
    ||  match_shell_handler(cs8(p->buf), mode->shell_handlers)) {
        return 80;
    }
    /* weaker match on C comment start */
    if (p->buf[0] == '/' && p->buf[1] == '*')
        return 60;

    /* even weaker match on C++ comment start */
    if (p->buf[0] == '/' && p->buf[1] == '/')
        return 50;

    if (p->buf[0] == '#') {
        /* same for files starting with a preprocessor directive */
        if (strstart(cs8(p->buf), "#include", NULL)
        ||  strstart(cs8(p->buf), "#ifndef", NULL)
        ||  strstart(cs8(p->buf), "#ifdef", NULL)
        ||  strstart(cs8(p->buf), "#if ", NULL)
        ||  strstart(cs8(p->buf), "#define", NULL)
        ||  strstart(cs8(p->buf), "#pragma", NULL)) {
            return 50;
        }
    }
    return 1;
}

ModeDef c_mode = {
    .name = "C",
    .extensions = c_extensions,
    .shell_handlers = "tcc",
    .mode_probe = c_mode_probe,
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_C | CLANG_CC,
    .keywords = "", //c_keywords,
    .types = "", //c_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
};

/* XXX: support Yacc / Bison syntax extensions */

static ModeDef yacc_mode = {
    .name = "Yacc",
    .extensions = "y|yacc",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_C | CLANG_CC | CLANG_YACC,
    .keywords = c_keywords,
    .types = c_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/* XXX: support Lex / Flex syntax extensions */

static ModeDef lex_mode = {
    .name = "Lex",
    .extensions = "l|lex",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_C | CLANG_CC | CLANG_LEX,
    .keywords = c_keywords,
    .types = c_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- C++ programming language ----------------*/

static const char cpp_keywords[] = {
    "asm|catch|class|delete|friend|inline|namespace|new|operator|"
    "private|protected|public|template|try|this|virtual|throw|"
    "explicit|override|mutable|using|assert|true|false|nullptr|"
    // XXX: many missing keywords
};

static const char cpp_types[] = {
    "bool|exception|istream|ostream|ofstream|string|vector|map|set|stack|"
    "std::istream|std::ostream|std::ofstream|std::string|"
    "std::vector|std::unique_ptr|std::map|std::set|std::stack|"
    "std::hash|std::unordered_set|std::unordered_map|std::exception|"
    "std::string::iterator|std::stringstream|std::ostringstream|"
};

static int cpp_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    int score;

    /* trust the file extension */
    if (match_extension(p->filename, mode->extensions))
        return 80;

    score = c_mode_probe(&c_mode, p);
    if (score > 5) {
        if (strstr(cs8(p->buf), "namespace")
        ||  strstr(cs8(p->buf), "class")
        ||  strstr(cs8(p->buf), "::")) {
            return score + 5;
        }
        return score - 5;
    }
    return 1;
}

ModeDef cpp_mode = {
    .name = "C++",
    .alt_name = "cpp",
    .extensions = "cc|hh|cpp|hpp|cxx|hxx|CPP|CC|c++|lzz",
    .mode_probe = cpp_mode_probe,
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_CPP | CLANG_CC,
    .keywords = cpp_keywords,
    .types = cpp_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

#ifndef CONFIG_TINY
/*---------------- Carbon programming language ----------------*/

static const char carbon_keywords[] = {
    "abstract|addr|alias|and|api|as|auto|base|break|"
    "case|class|constraint|continue|default|else|extends|external|"
    "final|fn|for|forall|friend|if|impl|import|in|interface|is|"
    "let|library|like|match|namespace|not|observe|or|override|"
    "package|partial|private|protected|return|returned|then|"
    "var|virtual|where|while|"

    /* literals */
    "false|true|_|"

    /* keyword candidates? */
    "choice|const|destructor|dyn|me|public|sizeof|static|template|"

    /* builtins */
    "Optional|Assert|Self|"

    /* operator interfaces */
    "Negate|Add|AddWith|Sub|SubWith|Mul|MulWith|Div|DivWith|Mod|ModWith|"
    "BitComplement|BitAnd|BitAndWith|BitOr|BitOrWith|BitXor|BitXorWith|"
    "LeftShift|LeftShiftWith|RightShift|RightShiftWith|"
    "Eq|EqWith|Ordered|OrderedWith|As|ImplicitAs|CommonTypeWith|"
};

static const char carbon_types[] = {
    "bool|i8|i16|i32|i64|i128|u8|u16|u32|u64|u128|"
    "f16|f32|f64|f128|auto|"
    "Type|Array|Stack|String|StringView|Bfloat16|"
};

// identifiers: unicode based
// integer literals: case sensitive, `0x`, `0b`, uppercase hex,
//                    `_` digit separator, no suffix
// floating point literals: case sensitive, `0x`, `e`, `p` optional exponent sign,
//                    digit on both sides of `.`, no suffix
// string blocks: """ with optional type indicator and line position constraints
// raw strings: #"\ is a backslash, " is a quote, \#n is a newline"#
// hex digits must be uppercase (what a strange idea)

static ModeDef carbon_mode = {
    .name = "Carbon",
    .extensions = "carbon",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_CARBON | CLANG_STR3,
    .keywords = carbon_keywords,
    .types = carbon_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- C2 language ----------------*/

static const char c2_keywords[] = {
    // Module related
    "module|import|as|public|"
    // Types -> c2_types
    // Type related
    "auto|asm|cast|const|elemsof|enum|enum_min|enum_max|"
    "false|fn|local|nil|offsetof|to_container|public|"
    "sizeof|struct|template|true|type|union|volatile|"
    // Control flow related
    "break|case|continue|default|do|else|fallthrough|"
    "for|goto|if|return|switch|sswitch|while|"
    // other
    "assert|static_assert"
};

static const char c2_types[] = {
    "void|bool|char|i8|i16|i32|i64|u8|u16|u32|u64|isize|usize|f32|f64|"
    "reg8|reg16|reg32|reg64|va_list"
};

static ModeDef c2_mode = {
    .name = "C2",
    .extensions = "c2|c2h|c2i|c2t",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_C2 | CLANG_PREPROC | CLANG_CAP_TYPE,
    .keywords = c2_keywords,
    .types = c2_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- Objective C programming language ----------------*/

static const char objc_keywords[] = {
    "self|super|class|nil|YES|NO|"
    "@class|@interface|@implementation|@public|@private|@protected|"
    "@try|@catch|@throw|@finally|@end|@protocol|@selector|@synchronized|"
    "@encode|@defs|@optional|@required|@property|@dynamic|@synthesize|"
    "@compatibility_alias|"
    // context sensitive keywords
    "in|out|inout|bycopy|byref|oneway|"
    "getter|setter|readwrite|readonly|assign|retain|copy|nonatomic|"
};

static const char objc_types[] = {
    "id|BOOL|SEL|Class|Object|"
};

static int objc_mode_probe(ModeDef *mode, ModeProbeData *mp)
{
    const char *p = cs8(mp->buf);

    if (match_extension(mp->filename, mode->extensions)) {
        /* favor Objective C over Limbo for .m extension
         * if file is empty, starts with a comment or a #import
         */
        if (*p == '/' || *p == '\0'
        ||  strstart(p, "#import", NULL)) {
            return 81;
        } else {
            return 80;
        }
    }
    if (match_extension(mp->filename, "h")) {
        /* favor Objective C over C/C++ for .h extension
         * if file has #import or @keyword at the beginning of a line
         */
        const char *q;
        for (q = p;; q++) {
            if ((*q == '@' && qe_isalpha(q[1]))
            ||  (*q == '#' && strstart(p, "#import", NULL))) {
                return 85;
            }
            q = strchr(q, '\n');
            if (q == NULL)
                break;
        }
    }
    return 1;
}

static ModeDef objc_mode = {
    .name = "Objective C",
    .alt_name = "objc",
    .extensions = "m|mm",
    .mode_probe = objc_mode_probe,
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_OBJC | CLANG_CC,
    .keywords = objc_keywords,
    .types = objc_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- AWK programming language ----------------*/

static const char awk_keywords[] = {
    "BEGIN|break|case|continue|default|do|else|for|if|next|switch|while|"
    "print|printf|split|"
};

static const char awk_types[] = {
    "char|double|float|int|long|unsigned|short|signed|void|"
};

static ModeDef awk_mode = {
    .name = "awk",
    .extensions = "awk",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_AWK | CLANG_REGEX,
    .keywords = awk_keywords,
    .types = awk_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- less css preprocessor ----------------*/

static const char less_keywords[] = {
    "|"
};

static const char less_types[] = {
    "|"
};

static ModeDef less_mode = {
    .name = "less",
    .extensions = "less",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_CSS,
    .keywords = less_keywords,
    .types = less_types,
    .indent_func = c_indent_line,
    .fallback = &c_mode,
};
#endif  /* CONFIG_TINY */

/*---------------- Javascript programming language ----------------*/

static const char js_keywords[] = {
    "break|case|catch|continue|debugger|default|delete|do|"
    "else|finally|for|function|if|in|instanceof|new|"
    "return|switch|this|throw|try|typeof|while|with|"
    /* FutureReservedWord */
    "class|const|enum|import|export|extends|super|"
    /* The following tokens are also considered to be
     * FutureReservedWords when parsing strict mode code */
    "implements|interface|let|package|private|protected|"
    "public|static|yield|"
    /* constants */
    "undefined|null|true|false|Infinity|NaN|"
    /* strict mode quasi keywords */
    "eval|arguments|"
    /* ES6 extra keywords */
    "await|"
};

static const char js_types[] = {
    "void|var|"
};

static int is_js_identifier_start(char32_t c) {
    // should accept unicode escape sequence, UnicodeIDStart
    return (qe_isalpha_(c) || c == '$' || c >= 128);
}

static int is_js_identifier_part(char32_t c) {
    // should accept unicode escape sequence, UnicodeIDContinue, ZWJ or ZWNJ
    return (qe_isalnum_(c) || c == '$' || c >= 128);
}

// FIXME: should merge into ustr_get_identifier()
static int get_js_identifier(char *dest, int size, char32_t c,
                             const char32_t *str, int i0, int n)
{
    /*@API utils
       Grab an identifier from a `char32_t` buffer,
       accept non-ASCII identifiers and encode in UTF-8.
       @argument `dest` a pointer to the destination array
       @argument `size` the length of the destination array in bytes
       @argument `c` the initial code point or `0` if none
       @argument `str` a valid pointer to an array of codepoints
       @argument `i0` the index to the next codepoint
       @argument `n` the length of the codepoint array
       @return the number of codepoints used in the source array.
       @note `dest` can be a null pointer if `size` is `0`.
     */
    int pos = 0, i = i0;

    if (c == 0) {
        if (!(i < n && is_js_identifier_start(c = str[i++]))) {
            if (size > 0)
                *dest = '\0';
            return 0;
        }
    }
    for (;; i++) {
        if (c < 128) {
            if (pos + 1 < size) {
                dest[pos++] = c;
            }
        } else {
            char buf[6];
            int len = utf8_encode(buf, c);
            if (pos + len < size) {
                memcpy(dest + pos, buf, len);
                pos += len;
            } else {
                size = pos + 1;
            }
        }
        if (i >= n)
            break;
        c = str[i];
        if (!is_js_identifier_part(c))
            break;
    }
    if (pos < size)
        dest[pos] = '\0';
    return i - i0;
}

static void js_colorize_line(QEColorizeContext *cp,
                             const char32_t *str, int n,
                             QETermStyle *sbuf, ModeDef *syn)
{
    int i = 0, start, i1, indent;
    int style, tag, level;
    char32_t c, delim;
    char kbuf[64];
    int mode_flags = syn->colorize_flags;
    int flavor = (mode_flags & CLANG_FLAVOR);
    int state = cp->colorize_state;
    //int type_decl;  /* unused */

    indent = cp_skip_blanks(str, 0, n);
    tag = !qe_isblank(str[0]) && (cp->s->mode == syn || cp->s->mode == &htmlsrc_mode);

    start = i;
    //type_decl = 0;
    c = 0;
    style = 0;

    if (i >= n)
        goto the_end;

    if (state) {
        /* if already in a state, go directly in the code parsing it */
        if (state & IN_C_COMMENT2)
            goto parse_comment2;
        switch (state & IN_C_STRING) {
        case IN_C_STRING_D: goto parse_string;
        case IN_C_STRING_Q: goto parse_string_q;
        case IN_C_STRING_BQ: goto parse_string_bq;
        case IN_C_STRING_D3: goto parse_string3;
        case IN_C_STRING_Q3: goto parse_string_q3;
        }
        if (state & IN_C_REGEX) {
            delim = '/';
            goto parse_regex;
        }
    }

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '*':
            /* lone star at the beginning of a line in a shell buffer
             * is treated as a comment start.  This improves colorization
             * of diff and git output.
             */
            if (start == indent && cp->partial_file
            &&  (i == n || str[i] == ' ' || str[i] == '/')) {
                i--;
                goto parse_comment2;
            }
            continue;
        case '/':
            if (str[i] == '*') {
                /* C style multi-line comment */
                i++;
            parse_comment2:
                state |= IN_C_COMMENT2;
                style = C_STYLE_COMMENT;
                level = (state & IN_C_COMMENT_LEVEL) >> IN_C_COMMENT_SHIFT;
                while (i < n) {
                    if (str[i] == '/' && str[i + 1] == '*' && (mode_flags & CLANG_NEST_COMMENTS)) {
                        i += 2;
                        level++;
                    } else
                    if (str[i] == '*' && str[i + 1] == '/') {
                        i += 2;
                        if (level == 0) {
                            state &= ~IN_C_COMMENT2;
                            break;
                        }
                        level--;
                    } else {
                        i++;
                    }
                }
                state = (state & ~IN_C_COMMENT_LEVEL) |
                        (min_int(level, 7) << IN_C_COMMENT_SHIFT);
                break;
            } else
            if (str[i] == '/') {
                /* line comment */
            parse_comment1:
                state |= IN_C_COMMENT1;
                style = C_STYLE_COMMENT;
                i = n;
                break;
            }
            if (mode_flags & CLANG_REGEX) {
                /* XXX: should use more context to tell regex from divide */
                char32_t prev = ' ';
                for (i1 = start; i1 > indent; ) {
                    prev = str[--i1];
                    if (!qe_isblank(prev))
                        break;
                }
                /* ignore end of comment in grep output */
                if (start > indent && str[start - 1] == '*' && cp->partial_file)
                    break;

                if (!qe_findchar("])", prev)
                &&  (qe_findchar(" [({},;=<>!~^&|*/%?:", prev)
                ||   sbuf[i1] == C_STYLE_KEYWORD
                ||   (str[i] != ' ' && (str[i] != '=' || str[i + 1] != ' ')
                &&    !(qe_isalnum(prev) || prev == ')')))) {
                    /* parse regex */
                    state |= IN_C_REGEX;
                    delim = '/';
                parse_regex:
                    style = C_STYLE_REGEX;
                    while (i < n) {
                        c = str[i++];
                        if (c == '\\') {
                            if (i < n) {
                                i += 1;
                            }
                        } else
                        if (state & IN_C_CHARCLASS) {
                            if (c == ']') {
                                state &= ~IN_C_CHARCLASS;
                            }
                            /* ECMA 5: ignore '/' inside char classes */
                        } else {
                            if (c == '[') {
                                state |= IN_C_CHARCLASS;
                            } else
                            if (c == delim) {
                                while (qe_isalnum_(str[i])) {
                                    i++;
                                }
                                state &= ~IN_C_REGEX;
                                break;
                            }
                        }
                    }
                    break;
                }
            }
            continue;
        case '#':       /* preprocessor */
            if (start == 0 && str[i] == '!') {
                /* recognize a shebang comment line */
                style = C_STYLE_PREPROCESS;
                i = n;
                break;
            }
            if (flavor == CLANG_V8 && start == 0
            &&  ustrstart(str + i + 1, "include", NULL)) {
                /* v8: #include */
                // FIXME: handle multiline comments
                style = C_STYLE_PREPROCESS;
                i = n;
                break;
            }
            continue;
        case '@':       /* annotations */
            i += get_js_identifier(kbuf, countof(kbuf), c, str, i, n);
            style = C_STYLE_PREPROCESS;
            break;
        case '`':       /* ECMA 6 template strings */
        parse_string_bq:
            state |= IN_C_STRING_BQ;
            style = C_STYLE_STRING_BQ;
            while (i < n) {
                c = str[i++];
                if (c == '`') {
                    state &= ~IN_C_STRING;
                    break;
                }
            }
            break;

        case '\'':      /* character constant */
            if ((mode_flags & CLANG_STR3)
            &&  (str[i] == '\'' && str[i + 1] == '\'')) {
                /* multiline ''' quoted string */
                i += 2;
                state |= IN_C_STRING_Q3;
            parse_string_q3:
                style = C_STYLE_STRING_Q;
                delim = '\'';
                goto string3;
            }
            state |= IN_C_STRING_Q;
        parse_string_q:
            style = C_STYLE_STRING_Q;
            delim = '\'';
            goto string;

        case '\"':      /* string literal */
            if ((mode_flags & CLANG_STR3)
            &&  (str[i] == '\"' && str[i + 1] == '\"')) {
                /* multiline """ quoted string */
                i += 2;
                state |= IN_C_STRING_D3;
                goto parse_string3;
            }
            state |= IN_C_STRING_D;
        parse_string:
            style = C_STYLE_STRING;
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
                    state &= ~IN_C_STRING;
                    break;
                }
            }
            break;
        parse_string3:
            style = C_STYLE_STRING;
            delim = '\"';
        string3:
            while (i < n) {
                c = str[i++];
                if (c == '\\') {
                    if (i >= n)
                        break;
                    i++;
                } else
                if (c == delim && str[i] == delim && str[i + 1] == delim) {
                    i += 2;
                    state &= ~IN_C_STRING;
                    break;
                }
            }
            break;
        case '=':
            /* exit type declaration */
            /* does not handle this: int i = 1, j = 2; */
            //type_decl = 0;
            continue;
        case '<':       /* JavaScript extension */
            if (flavor == CLANG_JS) {
                if (str[i] == '!' && str[i + 1] == '-' && str[i + 2] == '-')
                    goto parse_comment1;
            }
            continue;
        case '(':
        case '{':
            tag = 0;
            continue;
        default:
            if (qe_isdigit(c)) {
                /* XXX: should parse actual number syntax */
                /* decimal, binary, octal and hexadecimal literals:
                 * 1 0b1 0o1 0x1, case insensitive. 01 is a syntax error */
                /* maybe ignore '_' in integers */
                /* XXX: should parse decimal and hex floating point syntaxes */
                while (qe_isalnum_(str[i]) || (str[i] == '.' && str[i + 1] != '.')) {
                    i++;
                }
                style = C_STYLE_NUMBER;
                break;
            }
            if (is_js_identifier_start(c)) {
                i += get_js_identifier(kbuf, countof(kbuf), c, str, i, n);
                if (cp->state_only && !tag)
                    continue;

                /* keywords used as object property tags are regular identifiers.
                 * `default` is always considered a keyword as the context cannot be
                   determined precisely by this simplistic lexical parser */
                if (strfind(syn->keywords, kbuf)
                &&  (str[i] != ':' || strequal(kbuf, "default"))
                &&  (start == 0 || str[start - 1] != '.')) {
                    style = C_STYLE_KEYWORD;
                    break;
                }

                i1 = cp_skip_blanks(str, i, n);

                if (str[i1] == '(') {
                    /* function call or definition */
                    style = C_STYLE_FUNCTION;
                    if (tag) {
                        /* tag function definition */
                        eb_add_tag(cp->b, cp->offset + start, kbuf);
                        tag = 0;
                    }
                    break;
                } else
                if (tag && qe_findchar("(,;=", str[i1])) {
                    /* tag variable definition */
                    eb_add_tag(cp->b, cp->offset + start, kbuf);
                }

                if ((start == 0 || str[start - 1] != '.')
                &&  !qe_findchar(".(:", str[i])
                &&  strfind(syn->types, kbuf)) {
                    /* if not cast, assume type declaration */
                    //type_decl = 1;
                    style = C_STYLE_TYPE;
                    break;
                }
                if (qe_isupper((unsigned char)kbuf[0])
                &&  (start >= 2 && str[start - 1] == ' ' && str[start - 2] == ':')) {
                    /* if type annotation and capitalized assume type name */
                    style = C_STYLE_TYPE;
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
 the_end:
    if (state & (IN_C_COMMENT | IN_C_STRING)) {
        /* set style on eol char */
        SET_STYLE1(sbuf, n, style);
        if ((state & IN_C_COMMENT) == IN_C_COMMENT1)
            state &= ~IN_C_COMMENT1;
    }
    cp->colorize_state = state;
}

ModeDef js_mode = {
    .name = "Javascript",
    .alt_name = "js",
    .extensions = "js",
    .shell_handlers = "node|qjs",
    .colorize_func = js_colorize_line,
    .colorize_flags = CLANG_JS | CLANG_REGEX,
    .keywords = js_keywords,
    .types = js_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

#ifndef CONFIG_TINY
/*---------------- V8 Torque programming language ----------------*/

static const char v8_keywords[] = {
    /* constants */
    "undefined|null|true|false|Infinity|NaN|"
    /* classic keywords */
    "import|let|const|return|if|else|break|continue|for|while|case|"
    "class|extends|struct|constexpr|extern|namespace|goto|"
    /* V8 specific */
    "typeswitch|tail|debug|enum|"
    "dcheck|check|static_assert|transitioning|operator|"
    "transient|shape|bitfield|intrinsic|javascript|"
    "macro|generates|otherwise|builtin|implicit|weak|"
    "never|label|labels|unreachable|runtime|deferred|"
};

static const char v8_types[] = {
    "void|var|type|bool|string|bit|"
    "int8|int16|int31|int32|int64|uint8|uint16|uint31|uint32|uint64|"
    "intptr|uintptr|bint|float16|float32|float64|"
    "ByteArray|Object|Map|JSAny|JSFunction|JSObject|Smi|String|Number|"
};

static ModeDef v8_mode = {
    .name = "V8 Torque",
    .alt_name = "tq",
    .extensions = "tq",
    .colorize_func = js_colorize_line,
    .colorize_flags = CLANG_V8 | CLANG_REGEX,
    .keywords = v8_keywords,
    .types = v8_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- Bee language syntax ----------------*/

/* work in progress */

static ModeDef bee_mode = {
    .name = "Bee",
    .alt_name = "bee",
    .extensions = "bee",
    .shell_handlers = "node",
    .colorize_func = js_colorize_line,
    .colorize_flags = CLANG_BEE | CLANG_REGEX,
    .keywords = js_keywords,
    .types = js_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- CSS syntax ----------------*/

static const char css_keywords[] = {
    "|"
};

static const char css_types[] = {
    "|"
};

ModeDef css_mode = {
    .name = "CSS",
    .extensions = "css",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_CSS,
    .keywords = css_keywords,
    .types = css_types,
    .indent_func = c_indent_line,
    .fallback = &c_mode,
};

/*---------------- Typescript programming language ----------------*/

static const char ts_keywords[] = {
    /* keywords */
    "break|case|catch|class|const|continue|debugger|default|"
    "delete|do|else|enum|export|extends|false|finally|"
    "for|function|if|import|in|instanceof|new|null|"
    "return|super|switch|this|throw|true|try|typeof|"
    "var|void|while|with|"
    /* keywords in strict mode */
    "implements|interface|let|package|"
    "private|protected|public|static|yield|"
    /* special names */
    "abstract|as|async|await|constructor|declare|from|"
    "get|is|module|namespace|of|require|set|type|"
    /* other names? */
    "readonly|"
    /* constants */
    "undefined|Infinity|NaN|"
    /* strict mode quasi keywords */
    "eval|arguments|"
};

static const char ts_types[] = {
    "any|boolean|number|string|symbol|"
};

static ModeDef ts_mode = {
    .name = "TypeScript",
    .alt_name = "ts",
    .extensions = "ts|tsx",
    //.shell_handlers = "ts",
    .colorize_func = js_colorize_line,
    .colorize_flags = CLANG_TS | CLANG_REGEX,
    .keywords = ts_keywords,
    .types = ts_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- Onux JS++ programming language ----------------*/

/* triple quoted strings
   '''
   """
   numeric: /0[Xx][A-Fa-f\d]+|[-+]?(?:\.\d+|\d+\.?\d*)(?:[Ee][-+]?\d+|[FfDdL]|UL)?/
 */

static const char jspp_keywords[] = {
    // JavaScript keywords
    "if|in|do|for|new|try|this|else|case|with|while|"
    "break|catch|throw|return|typeof|delete|switch|"
    "default|finally|continue|debugger|instanceof|"
    "true|false|null|"
    // JS++ warning keywords
    "let|const|yield|export|extends|implements|package|"
    // JS++ basic keywords
    "import|external|module|foreach|typeid|enum|interface|class|"
    "super|implicit|explicit|undefined|"
    // JS++ modifiers
    "private|protected|public|static|final|inline|property|abstract|"
    "optional|virtual|override|"
    // type keywords
    "var|void|function|"
    // JS++ operators
    //"; [ ] ( ) . , : === == = !== != ! <<= << <= < >>>= >>> >>= >> >= > "
    //"+= ++ + -= -- - *= * /= / %= % &&= && &= & ||= || |= | ^= ^ ~ ?=|"
    /* constants */
    "Infinity|NaN|"
    /* strict mode quasi keywords */
    "eval|arguments|"
};

static const char jspp_types[] = {
    "bool|string|byte|char|double|float|int|"
    "long|short|unsigned|signed|"
    //"var|void|function|"
};

static ModeDef jspp_mode = {
    .name = "JS++",
    .alt_name = "jspp",
    .extensions = "jspp|jpp",
    .shell_handlers = "js++",
    .colorize_func = js_colorize_line,
    .colorize_flags = CLANG_JSPP | CLANG_STR3 | CLANG_REGEX,
    .keywords = jspp_keywords,
    .types = jspp_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};


/*---------------- Microsoft Koka programming language ----------------*/

/* triple quoted strings
   '''
   """
   numeric: /0[Xx][A-Fa-f\d]+|[-+]?(?:\.\d+|\d+\.?\d*)(?:[Ee][-+]?\d+|[FfDdL]|UL)?/
   - in identifiers
   ' is not a string delimiter, but a name modifier?
   digits.string -> unit literal
   hex escapes: \x## \u#### \U######
   raw string: @" ... "" ... "
   string: " ... escapes ... "
   char: ' ... escapes ... '
   line comment: // ...
   block comment: / * ... * / (nested)
   line directive: # ...
   identifier: [a-zA-Z][a-zA-Z0-9_]+ embedded -, trailing [?]? and [']*
 */

static const char koka_keywords[] = {
    "fun|function|"
    "infix|infixr|infixl|prefix|type|cotype|rectype|alias|"
    "forall|exists|some|fun|function|val|var|con|"
    "if|then|else|elif|match|return|import|as|"
    "public|private|abstract|interface|instance|with|"
    "external|inline|include|effect|handle|handler|linear|"
    "yield|qualified|hiding|"

    "interleaved|catch|raise|resume|amb|for|foreach|"
    "module|not|open|extend|struct|linear|extern|"

    "False|True|Nothing|Nil|"
};

static const char koka_types[] = {
    "bool|int|double|string|"
    //"byte|char|float|long|short|unsigned|signed|"
};

static ModeDef koka_mode = {
    .name = "Koka",
    .extensions = "kk",
    //.shell_handlers = "koka",
    .colorize_func = js_colorize_line,
    .colorize_flags = CLANG_KOKA | CLANG_REGEX | CLANG_NEST_COMMENTS,
    .keywords = koka_keywords,
    .types = koka_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- JSON data format ----------------*/

static const char json_keywords[] = {
    "null|true|false|NaN"
};

static const char json_types[] = {
    ""
};

static int json_mode_probe(ModeDef *mode, ModeProbeData *pd)
{
    const char *p = cs8(pd->buf);

    if (match_extension(pd->filename, mode->extensions))
        return 80;

    if (*p == '{' && p[1] == '\n') {
        while (qe_isspace((unsigned char)*++p))
            continue;
        if (*p == '\"')
            return 50;
    }
    return 1;
}

static ModeDef json_mode = {
    .name = "json",
    .extensions = "json",
    .mode_probe = json_mode_probe,
    .colorize_func = js_colorize_line,
    .colorize_flags = CLANG_JSON,
    .keywords = json_keywords,
    .types = json_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- ActioScript programming language ----------------*/

static const char as_keywords[] = {
    "as|break|case|catch|class|continue|default|do|else|false|"
    "finally|for|function|if|import|interface|internal|is|new|null|"
    "package|private|protected|public|return|super|switch|this|throw|"
    "true|try|while|"
    // The following AS3 keywords are no longer in AS4:
    "delete|include|instanceof|namespace|typeof|use|with|in|const|"
    // other constants
    "undefined|Infinity|NaN|"
    // introduced in AS4 (spec abandoned in december 2012)
    //"let|defer|get|set|override|native|extends|implements|"
};

static const char as_types[] = {
    "void|var|bool|byte|int|uint|long|ulong|float|double|"
    "Array|Boolean|Number|Object|String|Function|Event|RegExp|"
    "Class|Interface|"
};

static ModeDef as_mode = {
    .name = "Actionscript",
    .alt_name = "as",
    .extensions = "as",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_AS | CLANG_REGEX,
    .keywords = as_keywords,
    .types = as_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};
#endif  /* CONFIG_TINY */

/*---------------- Java programming language ----------------*/

static const char java_keywords[] = {
    /* language keywords */
    "abstract|assert|break|case|catch|class|const|continue|"
    "default|do|else|enum|extends|final|finally|for|goto|"
    "if|implements|import|instanceof|interface|native|new|"
    "package|private|protected|public|return|"
    "static|strictfp|super|switch|synchronized|threadsafe|"
    "this|throw|throws|transient|try|volatile|while|"
    /* boolean and null literals */
    "false|null|true|"
};

static const char java_types[] = {
    "boolean|byte|char|double|float|int|long|short|void|"
};

ModeDef java_mode = {
    .name = "Java",
    .extensions = "jav|java",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_JAVA | CLANG_CAP_TYPE,
    .keywords = java_keywords,
    .types = java_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- C# programming language ----------------*/

static const char csharp_keywords[] = {
    "abstract|as|base|break|case|catch|checked|class|const|continue|"
    "default|delegate|do|else|enum|event|explicit|extern|false|finally|"
    "fixed|for|foreach|goto|if|implicit|in|interface|internal|is|lock|"
    "namespace|new|null|operator|out|override|params|private|protected|"
    "public|readonly|ref|return|sealed|sizeof|stackalloc|static|"
    "struct|switch|template|this|throw|true|try|typeof|unchecked|unsafe|"
    "using|virtual|volatile|while|"

    /* contextual keywords */
    "add|remove|yield|partial|get|set|where|"
};

static const char csharp_types[] = {
    "bool|byte|char|decimal|double|float|int|long|object|sbyte|short|"
    "string|uint|ulong|ushort|void|"
    "Boolean|Byte|DateTime|Exception|Int32|Int64|Object|String|Thread|"
    "UInt32|UInt64|"
};

ModeDef csharp_mode = {
    .name = "C#",   /* C Sharp */
    .alt_name = "csharp",
    .extensions = "cs",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_CSHARP | CLANG_PREPROC,
    .keywords = csharp_keywords,
    .types = csharp_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- PHP programming language ----------------*/

static const char php_keywords[] = {
    "abstract|as|assert|break|case|catch|class|clone|const|continue|"
    "declare|default|elseif|else|enddeclare|endif|endswitch|end|exit|"
    "extends|false|final|foreach|for|function|goto|if|implements|"
    "include_once|include|instanceof|interface|list|namespace|new|"
    "overload|parent|private|protected|public|require_once|require|return|"
    "self|sizeof|static|switch|throw|trait|true|try|use|var|while|"
    "NULL|null|$this"
    // built-in pseudo functions
    "array|count|defined|echo|empty|"
};

static const char php_types[] = {
    "array|boolean|bool|double|float|integer|int|object|real|string|"
};

ModeDef php_mode = {
    .name = "PHP",
    .extensions = "php|php3|php4",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_PHP | CLANG_REGEX,
    .keywords = php_keywords,
    .types = php_types,
    .fallback = &c_mode,
};

#ifndef CONFIG_TINY
/*---------------- Go programming language ----------------*/

static const char go_keywords[] = {
    /* keywords */
    "break|case|chan|const|continue|default|defer|else|fallthrough|"
    "for|func|go|goto|if|import|interface|map|package|range|"
    "return|select|struct|switch|type|var|"
    /* builtins */
    "append|cap|close|complex|copy|delete|imag|len|make|new|panic|"
    "print|println|real|recover|"
    /* Constants */
    "false|iota|nil|true|"
};

static const char go_types[] = {
    "bool|byte|complex128|complex64|error|float32|float64|"
    "int|int16|int32|int64|int8|rune|string|"
    "uint|uint16|uint32|uint64|uint8|uintptr|"
};

/* Go identifiers start with a Unicode letter or _ */

static ModeDef go_mode = {
    .name = "Go",
    .extensions = "go",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_GO,
    .keywords = go_keywords,
    .types = go_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- Scala programming language ----------------*/

static const char scala_keywords[] = {
    /* language keywords */
    "abstract|case|catch|class|def|do|else|extends|final|"
    "finally|for|forSome|if|implicit|import|lazy|match|new|"
    "object|override|package|private|protected|return|sealed|super|this|throw|"
    "trait|try|type|val|var|while|with|yield|"
    /* boolean and null literals */
    "false|null|true|_|"
};

static const char scala_types[] = {
    /* all mixed case identifiers starting with an uppercase letter are types */
    ""
};

static ModeDef scala_mode = {
    .name = "Scala",
    .extensions = "scala|sbt",
    .colorize_func = c_colorize_line,
    .colorize_flags = (CLANG_SCALA | CLANG_CAP_TYPE | CLANG_STR3 |
                       CLANG_NEST_COMMENTS),
    .keywords = scala_keywords,
    .types = scala_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- D programming language ----------------*/

static const char d_keywords[] = {
    "abstract|alias|align|asm|assert|auto|body|break|"
    "case|cast|catch|class|const|continue|debug|default|"
    "delegate|deprecated|do|else|enum|export|extern|false|"
    "final|finally|for|foreach|foreach_reverse|function|goto|"
    "if|immutable|import|in|inout|int|interface|invariant|is|"
    "lazy|mixin|module|new|nothrow|null|out|override|package|"
    "pragma|private|protected|public|pure|ref|return|scope|shared|"
    "static|struct|super|switch|synchronized|template|this|throw|"
    "true|try|typeid|typeof|union|unittest|version|while|with|"
    "delete|typedef|volatile|"  /* deprecated */
    "macro|"    /* reserved, unused */
    "__FILE__|__MODULE__|__LINE__|__FUNCTION__|__PRETTY_FUNCTION__|"
    "__gshared|__traits|__vector|__parameters|"
    "__DATE__|__EOF__|__TIME__|__TIMESPAMP__|__VENDOR__|__VERSION__|"
};

static const char d_types[] = {
    "bool|byte|ubyte|short|ushort|int|uint|long|ulong|char|wchar|dchar|"
    "float|double|real|ifloat|idouble|ireal|cfloat|cdouble|creal|void|"
    "|cent|ucent|string|wstring|dstring|size_t|ptrdiff_t|"
};

static ModeDef d_mode = {
    .name = "D",
    .extensions = "d|di",
    .colorize_func = c_colorize_line,
    /* only #line is supported, but can occur anywhere */
    .colorize_flags = CLANG_D | CLANG_PREPROC | CLANG_T_TYPES,
    .keywords = d_keywords,
    .types = d_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- Limbo programming language ----------------*/

static const char limbo_keywords[] = {
    "adt|alt|array|break|case|chan|con|continue|cyclic|do|else|exit|"
    "fn|for|hd|if|implement|import|include|len|list|load|module|nil|"
    "of|or|pick|ref|return|self|spawn|tagof|tl|to|type|while|"
};

static const char limbo_types[] = {
    "big|byte|int|real|string|"
};

static ModeDef limbo_mode = {
    .name = "Limbo",
    .extensions = "m",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_LIMBO,
    .keywords = limbo_keywords,
    .types = limbo_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- Cyclone programming language ----------------*/

static const char cyclone_keywords[] = {
    "auto|break|case|const|continue|default|do|else|enum|extern|for|goto|"
    "if|inline|register|restrict|return|sizeof|static|struct|switch|"
    "typedef|union|volatile|while|"
    "abstract|alias|as|catch|datatype|export|fallthru|inject|let|"
    "namespace|new|numelts|offsetof|region|regions|reset_region|rnew|"
    "tagcheck|throw|try|using|valueof|"
    "calloc|malloc|rcalloc|rmalloc|"
    "NULL|"
};

static const char cyclone_types[] = {
    "char|double|float|int|long|unsigned|short|signed|void|"
    "_Bool|_Complex|_Imaginary|"
    "bool|dynregion_t|region_t|tag_t|valueof_t|"
    "@numelts|@region|@thin|@fat|@zeroterm|@nozeroterm|@notnull|@nullable|"
    "@extensible|@tagged"
};

static ModeDef cyclone_mode = {
    .name = "Cyclone",
    .extensions = "cyc|cyl|cys",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_CYCLONE | CLANG_CC,
    .keywords = cyclone_keywords,
    .types = cyclone_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- Ch programming language ----------------*/

static const char ch_keywords[] = {
    "local|offsetof|Inf|NaN|"
};

static const char ch_types[] = {
    "complex|"
};

static ModeDef ch_mode = {
    .name = "Ch",
    .extensions = "chf",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_CH | CLANG_CC,
    .keywords = ch_keywords,
    .types = ch_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- Squirrel programming language ----------------*/

static const char squirrel_keywords[] = {
    "base|break|continue|const|extends|for|null|throw|try|instanceof|true|"
    "case|catch|class|clone|default|delete|else|enum|foreach|function|if|in|"
    "resume|return|switch|this|typeof|while|yield|constructor|false|static|"
};

static const char squirrel_types[] = {
    "local|"
};

static ModeDef squirrel_mode = {
    .name = "Squirrel",
    .extensions = "nut",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_SQUIRREL,
    .keywords = squirrel_keywords,
    .types = squirrel_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- ICI programming language ----------------*/

static const char ici_keywords[] = {
    "array|break|case|class|continue|default|do|else|extern|float|"
    "for|forall|func|if|in|module|NULL|onerror|return|set|static|struct|"
    "switch|try|while|"
};

static const char ici_types[] = {
    "auto|"
};

static ModeDef ici_mode = {
    .name = "ICI",
    .extensions = "ici",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_ICI,
    .keywords = ici_keywords,
    .types = ici_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- JSX programming language ----------------*/

static const char jsx_keywords[] = {
    // literals shared with ECMA 262
    "null|true|false|NaN|Infinity|"
    // keywords shared with ECMA 262
    "break|case|const|do|else|finally|for|function|if|in|"
    "instanceof|new|return|switch|this|throw|try|typeof|var|while|"
    // JSX keywords
    "class|extends|super|import|implements|static|"
    "__FILE__|__LINE__|undefined|"
    // contextual keywords
    // "assert|log|catch|continue|default|delete|interface",
    // ECMA 262 literals but not used by JSX
    "debugger|with|"
    // ECMA 262 future reserved words
    "export|"
    // ECMA 262 strict mode future reserved words
    "let|private|public|yield|protected|"
    // JSX specific reserved words
    "extern|native|as|operator|abstract|"
};

static const char jsx_types[] = {
    "void|variant|boolean|int|number|string|Error|"
};

static ModeDef jsx_mode = {
    .name = "JSX",
    .extensions = "jsx",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_JSX | CLANG_REGEX,
    .keywords = jsx_keywords,
    .types = jsx_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- Haxe programming language ----------------*/

static const char haxe_keywords[] = {
    "abstract|break|case|cast|catch|class|continue|default|do|dynamic|else|"
    "enum|extends|extern|false|for|function|if|implements|import|inline|"
    "interface|in|macro|new|null|override|package|private|public|return|"
    "static|switch|this|throw|true|try|typedef|untyped|using|var|while|"
};

static const char haxe_types[] = {
    "Void|Array|Bool|Int|Float|Class|Enum|Dynamic|String|Date|Null|"
    "Iterator|"
};

static ModeDef haxe_mode = {
    .name = "Haxe",
    .extensions = "hx",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_HAXE | CLANG_REGEX,
    .keywords = haxe_keywords,
    .types = haxe_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- Dart programming language ----------------*/

static const char dart_keywords[] = {
    "abstract|as|assert|break|call|case|catch|class|const|continue|default|do|"
    "else|equals|extends|external|factory|false|final|finally|for|"
    "get|if|implements|in|interface|is|negate|new|null|on|operator|return|"
    "set|show|static|super|switch|this|throw|true|try|typedef|while|"
    // should match only at line start
    "import|include|source|library|"
    "@observable|@published|@override|@runTest|"
    // XXX: should colorize is! as a keyword
};

static const char dart_types[] = {
    "bool|double|dynamic|int|num|var|void|"
    "String|StringBuffer|Object|RegExp|Function|"
    "Date|DateTime|TimeZone|Duration|Stopwatch|DartType|"
    "Collection|Comparable|Completer|Future|Match|Options|Pattern|"
    "HashMap|HashSet|Iterable|Iterator|LinkedHashMap|List|Map|Queue|Set|"
    "Dynamic|Exception|Error|AssertionError|TypeError|FallThroughError|"
};

static ModeDef dart_mode = {
    .name = "Dart",
    .extensions = "dart",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_DART | CLANG_STR3,
    .keywords = dart_keywords,
    .types = dart_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- Pike programming language ----------------*/

static const char pike_keywords[] = {
    "auto|break|case|catch|class|constant|continue|const|default|do|else|enum|extern|"
    "final|for|foreach|gauge|global|if|import|inherit|inline|"
    "lambda|local|optional|predef|private|protected|public|"
    "return|sscanf|static|switch|throw|typedef|typeof|while|"
    "_Static_assert|__async__|__attribute__|__deprecated__|"
    "__experimental__|__func__|__generic__|__generator__|__weak__|"
    "__unused__|__unknown__"
};

static const char pike_types[] = {
    "array|float|int|string|function|mapping|mixed|multiset|object|program|"
    "variant|void|"
};

static ModeDef pike_mode = {
    .name = "Pike",
    .extensions = "pike",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_PIKE | CLANG_PREPROC,
    .keywords = pike_keywords,
    .types = pike_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- IDL syntax ----------------*/

static const char idl_keywords[] = {
    "abstract|attribute|case|component|const|consumes|context|custom|"
    "default|emits|enum|eventtype|exception|factory|finder|"
    "fixed|getraises|home|import|in|inout|interface|local|module|multiple|"
    "native|oneway|out|primarykey|private|provides|public|publishes|raises|"
    "readonly|setraises|struct|supports|switch|"
    "typedef|typeid|typeprefix|union|uses|ValueBase|valuetype|"
    "sequence|iterable|truncatable|"
    "unrestricted|namespace|dictionary|or|implements|optional|partial|required|"
    "getter|setter|creator|deleter|callback|legacycaller|"
    "false|true|null|FALSE|TRUE|"
};

static const char idl_types[] = {
    "unsigned|short|long|float|double|char|wchar|string|wstring|octet|any|void|"
    "byte|boolean|object|"
};

static ModeDef idl_mode = {
    .name = "IDL",
    .extensions = "idl",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_IDL | CLANG_PREPROC | CLANG_WLITERALS |
                      CLANG_REGEX | CLANG_CAP_TYPE,
    .keywords = idl_keywords,
    .types = idl_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- GNU Calc syntax ----------------*/

static const char calc_keywords[] = {
    "if|else|for|while|do|continue|break|goto|return|local|global|static|"
    "switch|case|default|quit|exit|define|read|show|help|write|mat|obj|"
    "print|cd|undefine|abort|"
};

static const char calc_types[] = {
    "|"
};

static ModeDef calc_mode = {
    .name = "calc", /* GNU Calc */
    .extensions = "cal|calc",
    .shell_handlers = "calc",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_CALC | CLANG_CC,
    .keywords = calc_keywords,
    .types = calc_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- GNU Enscript programming language ----------------*/

static const char enscript_keywords[] = {
    "if|else|return|state|extends|BEGIN|END|forever|continue|do|"
    "not|and|or|orelse|switch|case|default|true|false|"
};

static const char enscript_types[] = {
    "|"
};

static int enscript_mode_probe(ModeDef *mode, ModeProbeData *pd)
{
    if (match_extension(pd->filename, mode->extensions)) {
        if (*cs8(pd->buf) == '/')
            return 80;
        else
            return 50;
    }
    return 1;
}

static ModeDef enscript_mode = {
    .name = "Enscript", /* GNU Enscript */
    .extensions = "st", /* syntax files */
    .mode_probe = enscript_mode_probe,
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_ENSCRIPT | CLANG_REGEX,
    .keywords = enscript_keywords,
    .types = enscript_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- QuickScript programming language ----------------*/

static const char qs_keywords[] = {
    "break|case|catch|class|continue|default|delete|do|else|"
    "finally|for|function|if|import|in|instanceof|module|new|"
    "return|switch|this|throw|try|typeof|while|"
    "true|false|null|void|"
    "get|set|"
    "struct|self|def|func|as|from|arguments|target|super|"
};

static const char qs_types[] = {
    "const|let|var|bool|char|double|int|string|"
    "Array|Boolean|Function|Number|Object|String|Date|"
};

static int qs_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (match_extension(p->filename, mode->extensions)
    ||  match_shell_handler(cs8(p->buf), mode->shell_handlers)) {
        return 80;
    }
    if (strequal(p->filename, ".qerc")
    ||  strstr(p->real_filename, "/.qe/config"))
        return 80;

    return 1;
}

static ModeDef qscript_mode = {
    .name = "QScript",
    .alt_name = "qs",
    .extensions = "qe|qs",
    .shell_handlers = "qscript|qs|qsn",
    .mode_probe = qs_mode_probe,
    .colorize_func = js_colorize_line,
    .colorize_flags = CLANG_QSCRIPT | CLANG_STR3 | CLANG_REGEX,
    .keywords = qs_keywords,
    .types = qs_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- Elastic C like language ----------------*/

static const char ec_keywords[] = {
    "@false|@nil|@true|new|self|"
    "break|catch|class|continue|do|else|extends|for|from|function|goto|if|"
    "import|in|local|method|package|private|public|return|static|super|"
    "throw|try|while|"
};

static const char ec_types[] = {
    "none|short|ushort|int|uint|long|ulong|char|uchar|float|double|bool|"
    "string|static_string|array|callback|symbol|"
};

static ModeDef ec_mode = {
    .name = "elastiC",
    .alt_name = "ec",
    .extensions = "ec",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_ELASTIC,
    .keywords = ec_keywords,
    .types = ec_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- Jed's S-Lang extension language ----------------*/

static const char sl_keywords[] = {
    "define|if|else|return|static|while|break|do|"
};

static const char sl_types[] = {
    "variable|"
};

static ModeDef sl_mode = {
    .name = "Jed",  /* S-Lang */
    .extensions = "sl",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_JED | CLANG_PREPROC,
    .keywords = sl_keywords,
    .types = sl_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- Peter Koch's CSL C Scripting Language ----------------*/

static const char csl_keywords[] = {
    "const|sizeof|try|catch|throw|static|extern|resize|exists|if|else|"
    "switch|case|default|while|do|break|continue|for|trace|true|false|"
};

static const char csl_types[] = {
    "var|void|string|int|"
};

static ModeDef csl_mode = {
    .name = "CSL",
    .extensions = "csl",
    .shell_handlers = "csl",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_CSL | CLANG_PREPROC,
    .keywords = csl_keywords,
    .types = csl_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- Neko programming language ----------------*/

static const char neko_keywords[] = {
    "function|if|else|return|while|do|switch|default|"
    "try|catch|break|continue|"
    "this|null|true|false|"
};

static const char neko_types[] = {
    "var|"
};

static ModeDef neko_mode = {
    .name = "Neko",
    .extensions = "neko",
    .shell_handlers = NULL,
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_NEKO,
    .keywords = neko_keywords,
    .types = neko_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- NekoML programming language ----------------*/

static const char nml_keywords[] = {
    "function|rec|if|then|else|return|while|do|switch|default|"
    "try|catch|break|continue|when|"
    "this|null|true|false|or|and|xor|"
    "match|type|exception|throw|mutable|list|"
};

static const char nml_types[] = {
    "var|int|float|string|bool|char|void|"
};

static ModeDef nml_mode = {
    .name = "NekoML",
    .extensions = "nml",
    .shell_handlers = NULL,
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_NML,
    .keywords = nml_keywords,
    .types = nml_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- Alloy programming language ----------------*/

static const char alloy_keywords[] = {
    "if|else|do|for|loop|while|break|continue|match|return|use|"
    "mut|_|true|false|"
    "struct|enum|fn|func|self|impl"
};

static const char alloy_types[] = {
    "void|bool|char|int|float|double|usize|string|"
    "u8|u16|u32|u64|i8|i16|i32|i64|f64|f32|"
};

static ModeDef alloy_mode = {
    .name = "Alloy",
    .extensions = "ay",
    .shell_handlers = NULL,
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_ALLOY,
    .keywords = alloy_keywords,
    .types = alloy_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- SCILab programming language ----------------*/

static const char scilab_keywords[] = {
    "if|else|for|while|end|select|case|quit|return|help|what|who|"
    "pause|clear|resume|then|do|apropos|abort|break|elseif|pwd|"
    "function|endfunction|clc|continue|try|catch|exit|"
    "global|local|get|sorted|"
};

static const char scilab_types[] = {
    ""
};

static int scilab_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (match_extension(p->filename, mode->extensions)
    ||  match_shell_handler(cs8(p->buf), mode->shell_handlers)) {
        return 80;
    }
    if (match_extension(p->filename, "start|quit")
    &&  (p->buf[0] == '/' && p->buf[1] == '/')) {
        return 80;
    }
    return 1;
}

static ModeDef scilab_mode = {
    .name = "SciLab",
    .extensions = "sce|sci",
    .shell_handlers = NULL,
    .mode_probe = scilab_mode_probe,
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_SCILAB,
    .keywords = scilab_keywords,
    .types = scilab_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- Kotlin programming language ----------------*/

static const char kotlin_keywords[] = {
    /* language keywords */
    "package|import|as|fun|val|var|if|else|is|return|for|do|while|"
    "break|continue|when|it|to|by|in|out|try|catch|throw|finally|"
    "class|object|interface|public|private|protected|internal|inner|"
    "constructor|this|super|open|override|final|abstract|enum|companion|"
    "vararg|inline|reified|annotation|data|"
    "infix|operator|step|downTo|until|lazy|with|also|"
    //"get|set|"  // do not colorize as keyword because used as function
    //"case|def|extends|forSome|implicit|lazy|"  // unused java keywords
    //"match|new|sealed|trait|type|with|yield|"  // unused java keywords
    /* boolean and null literals */
    "false|true|null|"
};

/* numbers: hex, binary, L suffix for Long, f/F suffix for Float */
/* escape keywords as identifiers with backticks: a.`if` */

static const char kotlin_types[] = {
    /* all mixed case identifiers starting with an uppercase letter are types */
    "dynamic|"
};

static ModeDef kotlin_mode = {
    .name = "Kotlin",
    .extensions = "kt",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_KOTLIN | CLANG_CAP_TYPE | CLANG_STR3,
    .keywords = kotlin_keywords,
    .types = kotlin_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- C! (cbang) from EPITA's LSE laboratory ----------------*/

static const char cbang_keywords[] = {
    /* language keywords */
    "enum|struct|union|packed|sizeof|static|volatile|"
    "const|local|inline|if|else|while|do|for|switch|case|break|continue|"
    "default|typedef|class|macro|init|del|return|import|include|open_import|"
    "goto|as|interface|support|property|"
    /* Builtins */
    "false|true|NULL|"
};

/* integers: hex, octal, binary */
/* floats: [0-9]+([.][0-9]*)?([eE][+-]?[0-9]+)? */

static const char cbang_types[] = {
    /* all mixed case identifiers starting with an uppercase letter are types */
    "int|char|float|void|"
};

static ModeDef cbang_mode = {
    .name = "C!",
    .alt_name = "cbang",
    .extensions = "cb|cbi",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_CBANG | CLANG_CAP_TYPE,
    .keywords = cbang_keywords,
    .types = cbang_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- Gnome's Vala language ----------------*/

static const char vala_keywords[] = {
    //"asm|catch|delete|friend|inline|namespace|operator|"
    //"private|protected|template|try|virtual|throw|"
    //"explicit|mutable|assert|"

    "do|if|for|case|else|enum|break|const|while|extern|public|"
    "sizeof|static|struct|switch|default|continue|volatile|"
    "using|private|public|protected|class|override|abstract|weak|base|"
    "foreach|in|is|as|new|this|try|lock|async|catch|throw|yield|"
    "signal|throws|typeof|dynamic|ensures|finally|abstract|delegate|"
    "internal|requires|construct|interface|namespace|errordomain|"
    "var|get|set|out|ref|owned|inline|params|sealed|unowned|virtual|"
    "null|true|false|"
};

static const char vala_types[] = {
    //"void|"
    "bool|string|int|uint|uchar|nt8|short|ushort|long|ulong|size_t|ssize_t|"
    "double|va_list|unichar|"
};

static ModeDef vala_mode = {
    .name = "Vala",
    .extensions = "vala|vapi",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_VALA | CLANG_CC | CLANG_REGEX |
                      CLANG_CAP_TYPE | CLANG_STR3,
    .keywords = vala_keywords,
    .types = vala_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- Compuphase's Pawn language ----------------*/

static const char pawn_keywords[] = {
    "assert|break|case|const|continue|default|defined|do|else|exit|"
    "for|forward|goto|if|native|new|operator|public|return|sizeof|sleep|"
    "state|static|stock|switch|tagof|while|",
};

//static const char pawn_preprocessor_keywords[] = {
//    "assert|define|else|elseif|endif|endinput|error|file|if|include|"
//    "line|pragma|tryinclude|undef"
//};

static const char pawn_types[] = {
    ""
    //"void|"
    //"bool|string|int|uint|uchar|nt8|short|ushort|long|ulong|size_t|ssize_t|"
    //"double|va_list|unichar|"
};

static int pawn_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (match_shell_handler(cs8(p->buf), mode->shell_handlers))
        return 81;

    if (match_extension(p->filename, mode->extensions)) {
        /* disambiguate .p extension, can be Pascal too. */
#define NUL  "\0"
        static const char pawn_checks[] =
            "@" NUL "//" NUL "/*" NUL
            "#include" NUL "#define" NUL "#if" NUL
            "forward" NUL "new" NUL "main()" NUL
            NUL;
        const char *pref;
        const char *cp = cs8(p->buf);

        while (qe_isspace(*cp))
            cp++;
        for (pref = pawn_checks; *pref; pref += strlen(pref) + 1) {
            if (strstart(cp, pref, NULL))
                return 81;
        }
        return 79;
    }
    return 1;
}

static ModeDef pawn_mode = {
    .name = "Pawn",
    .extensions = "p",
    .mode_probe = pawn_mode_probe,
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_PAWN | CLANG_CC,  // needs '' delimited strings
    .keywords = pawn_keywords,
    .types = pawn_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- Kenneth Louden's C-minus language ----------------*/

/* C minus supports a very crude subset of C:
 *
 * numbers: [0-9]+
 * identifiers: [a-zA-Z]+
 * comments: multi-line C comments
 * keywords: if else return while
 * types: int void
 * operators: = ( ) { } [ ] \ . ; - + * / > < >= <= == !=
 * no strings, charconst, pointers, preproc...
 */

static const char cminus_keywords[] = {
    "if|else|return|while|",
};

static const char cminus_types[] = {
    "int|void|"
};

static ModeDef cminus_mode = {
    .name = "C-minus",
    .alt_name = "cminus",
    .extensions = "cm",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_CMINUS,
    .keywords = cminus_keywords,
    .types = cminus_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- Game Monkey scripting language ----------------*/

/* Simple C like syntax with some extensions:

   c = "My " "house"; // creates the string 'My house'
   d = `c:\windows\sys`; // d is a string
   c = `Chris``s bike`; // creates the string 'Chris`s bike'
   c = 2.4f // c is a float
   a = 'BLCK'; // characters as four bytes making integer
   a = 179; // decimal
   a = 0xB3; // hexadecimal
   a = 0b10110011; // binary
   myRect = CreateRect(0, 0, 5, 10); // Construct a table that describes
                                     // a rectangle
   area = myRect.Area(); // myRect is automatically assigned to 'this'
                         // within the area method.
   Size = function() { return .width * .height; };
   s = myRect:Size(); // Calls Size function passing 'myRect' as 'this'
 */

static const char gmscript_keywords[] = {
    "if|else|for|while|foreach|in|and|or|function|"
    "dowhile|break|continue|return|"
    "array|table|global|local|member|this|"
    "true|false|null|",
};

static const char gmscript_types[] = {
    ""
};

static ModeDef gmscript_mode = {
    .name = "Game Monkey",
    .alt_name = "gmscript",
    .extensions = "gm",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_GMSCRIPT,
    .keywords = gmscript_keywords,
    .types = gmscript_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- Wren embeddable scripting language ----------------*/

/* Simple C like syntax with some extensions */

static const char wren_keywords[] = {
    "break|class|construct|else|false|for|foreign|if|import|"
    "in|is|null|return|static|super|this|true|while|"
};

static const char wren_types[] = {
    "var|"
};

static ModeDef wren_mode = {
    .name = "Wren",
    .extensions = "wren",
    .shell_handlers = "wren",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_WREN | CLANG_CAP_TYPE | CLANG_NEST_COMMENTS,
    .keywords = wren_keywords,
    .types = wren_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- Jack language from nand2tetris ----------------*/

/* Simple object oriented language with C like syntax
   see https://www.nand2tetris.org/ for details
 */

static const char jack_keywords[] = {
    "class|constructor|method|function|"
    "var|static|field|"
    "let|do|if|else|while|return|"
    "true|false|null|this"
};

static const char jack_types[] = {
    "int|boolean|char|void"
};

static ModeDef jack_mode = {
    .name = "Jack",
    .extensions = "jack",
    .shell_handlers = "jack",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_JACK | CLANG_CAP_TYPE,
    .keywords = jack_keywords,
    .types = jack_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- Smac language by Bruno Pagès ----------------*/

/* Simple C-like language used in XCoral. */

static const char smac_keywords[] = {
    // recognized and used:
    "break|case|continue|default|do|else|for|if|return|sizeof|switch|while|"
    // reserved and discarded:
    "auto|const|double|enum|extern|float|goto|long|register|short|signed|"
    "static|struct|typedef|union|unsigned|volatile"
};

static const char smac_types[] = {
    "void|char|int"
};

static ModeDef smac_mode = {
    .name = "Smac",
    .extensions = "smac",
    .shell_handlers = "smac",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_SMAC,
    .keywords = smac_keywords,
    .types = smac_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- V programming language ----------------*/

// strings enclosed in '' or "", UTF-8 encoded
// interpolate strings with 'Hello $var.name', 'Hello ${1+2}'
// char constants use `c`, with embedded \ sequences

static const char v_keywords[] = {
    /* keywords */
    "fn|mut|in|map|pub|struct|const|module|import|interface|enum|asm|type|"
    "as|atomic|embed|__global|sizeof|union|static|"
    "if|else|for|break|continue|match|return|or|assert|defer|$if|go|goto|"
    "switch|case|default|"
    ///* builtins */
    //"append|cap|close|complex|copy|delete|imag|len|make|new|panic|"
    //"print|println|real|recover|"
    /* Constants */
    "true|false|none|err|"
};

static const char v_types[] = {
    "bool|string|i8|i16|i32|i64|i128|u8|u16|u32|u64|u128|"
    "byte|" // alias for u8
    "int|"  // alias for i32
    "rune|" // alias for i32, represents a Unicode code point
    "f32|f64|byteptr|voidptr|"
};

/* V identifiers start with a Unicode letter or _ */

static ModeDef v_mode = {
    .name = "V",
    .extensions = "v",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_V | CLANG_PREPROC | CLANG_CAP_TYPE | CLANG_NEST_COMMENTS,
    .keywords = v_keywords,
    .types = v_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- Google Protocol Buffers ----------------*/

// strings enclosed in '' or "", UTF-8 encoded

static const char protobuf_keywords[] = {
    /* keywords */
    "required|optional|repeated|package|import|default|"
    "message|enum|service|extensions|reserved|extend|rpc|"
    "option|returns|group|to|max|oneof|"

    /* builtins */

    /* constants */
    "true|false|"
};

static const char protobuf_types[] = {
    "double|float|int32|int64|uint32|uint64|sint32|"
    "sint64|fixed32|fixed64|sfixed32|sfixed64|bool|"
    "string|bytes|"
};

/* protobuf identifiers start with a Unicode letter or _ */

static ModeDef protobuf_mode = {
    .name = "protobuf",
    .desc = "Major mode for editing Protocol Buffers description language",
    .extensions = "proto",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_PROTOBUF | CLANG_CAP_TYPE,
    .keywords = protobuf_keywords,
    .types = protobuf_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- Odin programming language ----------------*/

// string literals enclosed in "", UTF-8 encoded
// raw string literals enclosed in ``, UTF-8 encoded
// runes enclosed in ''

static const char odin_keywords[] = {
    /* keywords */
    "align_of|auto_cast|bit_field|bit_set|break|case|cast|const|context|"
    "continue|defer|distinct|do|dynamic|else|enum|fallthrough|for|foreign|"
    "if|import|in|inline|macro|map|no_inline|notin|offset_of|opaque|"
    "package|proc|return|size_of|struct|switch|transmute|type_of|typeid|"
    "union|using|when|"

    /* predeclared identifiers */
    "len|cap|complex|real|imag|conj|swizzle|expand_to_tuple|min|max|abs|clamp|"

    /* constants */
    "true|false|nil|_|"
};

static const char odin_types[] = {
    "bool|b8|b16|b32|b64|i8|i16|i32|i64|i128|u8|u16|u32|u64|u128|"
    "i16le|i32le|i64le|i128le|u16le|u32le|u64le|u128le|"
    "i16be|i32be|i64be|i128be|u16be|u32be|u64be|u128be|"
    "f32|f64|complex64|complex128|byte|rune|"
    "uintptr|uint|int|string|cstring|any|rawptr|"
};

/* Odin identifiers start with a Unicode letter or _ */

static ModeDef odin_mode = {
    .name = "Odin",
    .desc = "Major mode for editing Odin programs",
    .extensions = "odin",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_ODIN | CLANG_CAP_TYPE | CLANG_NEST_COMMENTS,
    .keywords = odin_keywords,
    .types = odin_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- Salmon programming language ----------------*/

static const char salmon_keywords[] = {
    "single|routine|function|procedure|class|variable|"
    "immutable|tagalong|lepton|quark|lock|static|"
    "virtual|pure|construct|type|arguments|this|break"
    "continue|forall|exists|"
};

static const char salmon_types[] = {
    "|"
};

static void salmon_colorize_line(QEColorizeContext *cp,
                                 const char32_t *str, int n,
                                 QETermStyle *sbuf, ModeDef *syn)
{
    int i = 0, start, i1;
    int style, tag, level;
    char32_t c, delim;
    char kbuf[64];
    int mode_flags = syn->colorize_flags;
    int state = cp->colorize_state;
    //int type_decl;  /* unused */

    //int indent = cp_skip_blanks(str, 0, n);
    tag = !qe_isblank(str[0]) && (cp->s->mode == syn || cp->s->mode == &htmlsrc_mode);

    start = i;
    //type_decl = 0;
    c = 0;
    style = 0;

    if (i >= n)
        goto the_end;

    if (state) {
        /* if already in a state, go directly in the code parsing it */
        if (state & IN_C_COMMENT2)
            goto parse_comment2;
        switch (state & IN_C_STRING) {
        case IN_C_STRING_D: goto parse_string;
        case IN_C_STRING_Q: goto parse_string_q;
        case IN_C_STRING_BQ: goto parse_string_bq;
        }
        if (state & IN_C_REGEX) {
            goto parse_regex;
        }
    }

    while (i < n) {
        start = i;
        c = str[i++];

        switch (c) {
        case '/':
            if (str[i] == '*') {
                /* C style multi-line comment */
                i++;
                state |= IN_C_COMMENT2;
            parse_comment2:
                style = C_STYLE_COMMENT;
                level = (state & IN_C_COMMENT_LEVEL) >> IN_C_COMMENT_SHIFT;
                while (i < n) {
                    if (str[i] == '/' && str[i + 1] == '*' && (mode_flags & CLANG_NEST_COMMENTS)) {
                        i += 2;
                        level++;
                    } else
                    if (str[i] == '*' && str[i + 1] == '/') {
                        i += 2;
                        if (level == 0) {
                            state &= ~IN_C_COMMENT2;
                            break;
                        }
                        level--;
                    } else {
                        i++;
                    }
                }
                state = (state & ~IN_C_COMMENT_LEVEL) |
                        (min_int(level, 7) << IN_C_COMMENT_SHIFT);
                break;
            } else
            if (str[i] == '/') {
                /* line comment */
                state |= IN_C_COMMENT1;
                style = C_STYLE_COMMENT;
                i = n;
                break;
            }
            continue;
        case '#':       /* preprocessor */
            if (start == 0 && str[i] == '!') {
                /* recognize a shebang comment line */
                style = C_STYLE_PREPROCESS;
                i = n;
                break;
            }
            style = C_STYLE_COMMENT;
            i = n;
            break;
        case '`':       /* ECMA 6 template strings */
        parse_string_bq:
            state |= IN_C_STRING_BQ;
            style = C_STYLE_STRING_BQ;
            while (i < n) {
                c = str[i++];
                if (c == '`') {
                    state &= ~IN_C_STRING;
                    break;
                }
            }
            break;

        case '@':      /* regular expression literal */
        parse_regex:
            state |= IN_C_REGEX;
            style = C_STYLE_REGEX;
            delim = '@';
            while (i < n) {
                c = str[i++];
                if (c == '\\') {
                    if (i < n) {
                        i += 1;
                    }
                } else
                if (state & IN_C_CHARCLASS) {
                    if (c == ']') {
                        state &= ~IN_C_CHARCLASS;
                    }
                } else {
                    if (c == '[') {
                        state |= IN_C_CHARCLASS;
                    } else
                    if (c == delim) {
                        while (qe_isalnum_(str[i])) {
                            i++;
                        }
                        state &= ~IN_C_REGEX;
                        break;
                    }
                }
            }
            break;
        case '\'':      /* character constant */
        parse_string_q:
            state |= IN_C_STRING_Q;
            style = C_STYLE_STRING_Q;
            delim = '\'';
            goto string;

        case '\"':      /* string literal */
        parse_string:
            state |= IN_C_STRING_D;
            style = C_STYLE_STRING;
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
                    state &= ~IN_C_STRING;
                    break;
                }
            }
            break;
        case '=':
            /* exit type declaration */
            /* does not handle this: int i = 1, j = 2; */
            //type_decl = 0;
            continue;
        case '(':
        case '{':
            tag = 0;
            continue;
        default:
            if (qe_isdigit(c)) {
                /* XXX: should parse actual number syntax */
                /* decimal, binary, octal and hexadecimal literals:
                 * 1 0b1 0o1 0x1, case insensitive. 01 is a syntax error */
                /* maybe ignore '_' in integers */
                /* XXX: should parse decimal and hex floating point syntaxes */
                while (qe_isalnum_(str[i]) || (str[i] == '.' && str[i + 1] != '.')) {
                    i++;
                }
                style = C_STYLE_NUMBER;
                break;
            }
            if (qe_isalpha_(c)) {
                i += ustr_get_identifier(kbuf, countof(kbuf), c, str, i, n);
                if (cp->state_only && !tag)
                    continue;

                /* keywords used as object property tags are regular identifiers */
                if (strfind(syn->keywords, kbuf) &&
                    // XXX: this is incorrect for `default` inside a switch statement */
                    str[i] != ':' && (start == 0 || str[start - 1] != '.')) {
                    style = C_STYLE_KEYWORD;
                    break;
                }

                i1 = cp_skip_blanks(str, i, n);

                if (str[i1] == '(') {
                    /* function call or definition */
                    style = C_STYLE_FUNCTION;
                    if (tag) {
                        /* tag function definition */
                        eb_add_tag(cp->b, cp->offset + start, kbuf);
                        tag = 0;
                    }
                    break;
                } else
                if (tag && qe_findchar("(,;=", str[i1])) {
                    /* tag variable definition */
                    eb_add_tag(cp->b, cp->offset + start, kbuf);
                }

                if ((start == 0 || str[start - 1] != '.')
                &&  !qe_findchar(".(:", str[i])
                &&  strfind(syn->types, kbuf)) {
                    /* if not cast, assume type declaration */
                    //type_decl = 1;
                    style = C_STYLE_TYPE;
                    break;
                }
                if (qe_isupper((unsigned char)kbuf[0])
                &&  (start >= 2 && str[start - 1] == ' ' && str[start - 2] == ':')) {
                    /* if type annotation and capitalized assume type name */
                    style = C_STYLE_TYPE;
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
 the_end:
    if (state & (IN_C_COMMENT | IN_C_STRING)) {
        /* set style on eol char */
        SET_STYLE1(sbuf, n, style);
        if ((state & IN_C_COMMENT) == IN_C_COMMENT1)
            state &= ~IN_C_COMMENT1;
    }
    cp->colorize_state = state;
}

static ModeDef salmon_mode = {
    .name = "Salmon",
    //.alt_name = "js",
    .extensions = "salm",
    .shell_handlers = "salmoneye",
    .colorize_func = salmon_colorize_line,
    .colorize_flags = CLANG_SALMON | CLANG_REGEX | CLANG_NEST_COMMENTS,
    .keywords = salmon_keywords,
    .types = salmon_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
    .fallback = &c_mode,
};

/*---------------- PPL: Christian Neumanns' Practical Programming Language ----------------*/

static const char ppl_keywords[] = {
    /* language keywords */
    "factory|service|functions|function|command|script|template|param|"
    "record|enum|throw|as|type|inherit|creator|default|"
    "java|java_header|end|var|variable|redefine|"
    "and|or|xor|is|not|may|be|out_check|assert|this|const|"
    "on_error|throw_error|att|attribute|attributes|"
    "return|if|then|else|"
    "when|otherwise|repeat|times|to|try|catch_any|on|"
    "check|and_check|attributes_check|tests|test|verify|verify_error|"
    "private|public|get|set|in|out|in_out|in_all|"
    /* boolean and null literals */
    "yes|no|null|void|"
};

static const char ppl_phrases[] = {
    "repeat for each|repeat from|repeat while|repeat forever|"
    "exit repeat|next repeat|"
    "case type of|case enum of|case value of|case reference of|"
};

static const char ppl_types[] = {
    "any|none|non_null|yes_no|character|string|regex|list|map|"
    "signed_int_64|zero_neg_64|zero_pos_64|neg_64|pos_64|"
    "signed_int_32|zero_neg_32|zero_pos_32|neg_32|pos_32|"
    "signed_integer_64|zero_negative_64|zero_positive_64|negative_64|positive_64|"
    "signed_integer_32|zero_negative_32|zero_positive_32|negative_32|positive_32|"
    "float_64|float_32|number|"
};

enum {
    PPL_STYLE_DEFAULT    = 0,
    PPL_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    PPL_STYLE_COMMENT    = QE_STYLE_COMMENT,
    PPL_STYLE_STRING     = QE_STYLE_STRING,
    PPL_STYLE_STRING_Q   = QE_STYLE_STRING_Q,
    PPL_STYLE_NUMBER     = QE_STYLE_NUMBER,
    PPL_STYLE_KEYWORD    = QE_STYLE_KEYWORD,
    PPL_STYLE_TYPE       = QE_STYLE_TYPE,
    PPL_STYLE_FUNCTION   = QE_STYLE_FUNCTION,
};

enum {
    IN_PPL_COMMENT    = 0x03,  /* one of the comment styles */
    IN_PPL_COMMENT1   = 0x01,  /* single line comment // ... EOL */
    IN_PPL_COMMENT2   = 0x02,  /* multiline PPL comment /// ... ./// */
    IN_PPL_STRING     = 0x1C,  /* 3 bits for string styles */
    IN_PPL_STRING_D   = 0x04,  /* double-quoted string */
    IN_PPL_STRING_Q   = 0x08,  /* single-quoted string */
    IN_PPL_STRING_D3  = 0x14,  /* """ multiline quoted string with interpolation */
    IN_PPL_STRING_Q3  = 0x18,  /* ''' multiline quoted string */
    IN_PPL_PREPROCESS = 0x20,  /* preprocessor directive */
    IN_PPL_COMMENT_SHIFT = 8,  /* shift for block comment nesting level */
    IN_PPL_COMMENT_LEVEL = 0x700, /* mask for block comment nesting level */
    IN_PPL_JAVA = 0x800,
};

static int cp_match_keywords(const char32_t *str, int n, int start, const char *s, int *end) {
    /*@API utils
       Match a sequence of words from a | separated list of phrases.
       A space in the string matches a non empty white space sequence in the source array.
       Phrases are delimited by `|` characters.
       @argument `str` a valid pointer to an array of codepoints
       @argument `start` the index to the next codepoint
       @argument `n` the length of the codepoint array
       @argument `s` a valid pointer to a string containing phrases delimited by `|`.
       @argument `end` a valid pointer to store the index of the codepoint after the end of a match.
       @return a boolean success indicator.
     */
    int i = start;
    size_t j = 0;
    for (;;) {
        unsigned char cc = s[j++];
        if (cc == '|' || cc == '\0') {
            if (i == n || !qe_isalnum_(str[i])) {
                *end = i;
                return 1;
            }
            if (cc == '\0')
                return 0;
            i = start;
        } else {
            if (cc == ' ') {
                int i1 = i;
                i = cp_skip_blanks(str, i, n);
                if (i > i1)
                    continue;
            } else {
                if (i < n && cc == str[i++])
                    continue;
            }
            for (;;) {
                cc = s[j++];
                if (cc == '\0')
                    return 0;
                if (cc == '|')
                    break;
            }
            i = start;
        }
    }
}

static void ppl_colorize_line(QEColorizeContext *cp,
                              const char32_t *str, int n,
                              QETermStyle *sbuf, ModeDef *syn)
{
    int i = 0, start, i1;
    int indent = 0, style, level, type_decl;
    char32_t c, delim, last;
    char kbuf[64];
    int state = cp->colorize_state;

    indent = cp_skip_blanks(str, 0, n);
    start = i;
    type_decl = 0;
    c = 0;
    style = 0;
    last = n > 0 ? str[n - 1] : 0;
    kbuf[0] = '\0';

    if (state) {
        /* if already in a state, go directly in the code parsing it */
        if (state & IN_PPL_JAVA) {
            if (cp_match_keywords(str, n, 0, " end java", &i1)
            ||  cp_match_keywords(str, n, 0, " end java_header", &i1)) {
                state = 0;
            } else {
                cp->colorize_state = state & ~IN_PPL_JAVA;
                cp_colorize_line(cp, str, 0, n, sbuf, &java_mode);
                state = cp->colorize_state | IN_PPL_JAVA;
                i = n;
                goto done;
            }
        }
        if (state & IN_PPL_COMMENT2)
            goto parse_comment2;
        switch (state & IN_PPL_STRING) {
        case IN_PPL_STRING_D: goto parse_string;
        case IN_PPL_STRING_Q: goto parse_string_q;
        case IN_PPL_STRING_D3: goto parse_string3;
        case IN_PPL_STRING_Q3: goto parse_string_q3;
        }
    }

    while (i < n) {
        start = i;
        c = str[i++];

        switch (c) {
        case ' ':
        case '\t':
            continue;
        case '*':
            if (start == indent && cp->partial_file)
                goto parse_comment2;
            goto normal;
        case '/':
            if (str[i] == '/') {
                if (str[i + 1] == '/') {
                    /* PPL multi-line comment */
                    i += 2;
                parse_comment2:
                    state |= IN_PPL_COMMENT2;
                    style = PPL_STYLE_COMMENT;
                    level = (state & IN_PPL_COMMENT_LEVEL) >> IN_PPL_COMMENT_SHIFT;
                    while (i < n) {
                        if (str[i] == '/' && str[i + 1] == '/' && str[i + 2] == '/') {
                            i += 3;
                            level++;
                        } else
                        if (str[i] == '.' && str[i + 1] == '/' && str[i + 2] == '/' && str[i + 3] == '/') {
                            i += 4;
                            if (level == 0) {
                                state &= ~IN_PPL_COMMENT2;
                                break;
                            }
                            level--;
                        } else {
                            i++;
                        }
                    }
                    state = (state & ~IN_PPL_COMMENT_LEVEL) |
                        (min_int(level, 7) << IN_PPL_COMMENT_SHIFT);
                    break;
                } else {
                    /* line comment */
                    state |= IN_PPL_COMMENT1;
                    style = PPL_STYLE_COMMENT;
                    i = n;
                    break;
                }
            }
            type_decl = 0;  /* division operator */
            continue;
        case '%':       /* template instantiation */
            if (is_js_identifier_start(str[i])) {
                c = str[i++];
                i += get_js_identifier(kbuf, countof(kbuf), c, str, i, n);
                style = PPL_STYLE_PREPROCESS;
            }
            type_decl = 0;
            break;
        case '\'':      /* character constant */
            if (str[i] == '\'' && str[i + 1] == '\'') {
                /* multiline ''' quoted string */
                i += 2;
                state |= IN_PPL_STRING_Q3;
            parse_string_q3:
                style = PPL_STYLE_STRING_Q;
                delim = '\'';
                goto string3;
            }
            state |= IN_PPL_STRING_Q;
        parse_string_q:
            style = PPL_STYLE_STRING_Q;
            delim = '\'';
            goto string;

        case '\"':      /* string literal */
            if (str[i] == '\"' && str[i + 1] == '\"') {
                /* multiline """ quoted string */
                i += 2;
                state |= IN_PPL_STRING_D3;
                goto parse_string3;
            }
            state |= IN_PPL_STRING_D;
        parse_string:
            style = PPL_STYLE_STRING;
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
                    state &= ~IN_PPL_STRING;
                    break;
                }
            }
            type_decl = 0;
            break;
        parse_string3:
            style = PPL_STYLE_STRING;
            delim = '\"';
        string3:
            while (i < n) {
                c = str[i++];
                // XXX: should detect and colorize {{ expression }}
                if (c == delim && str[i] == delim && str[i + 1] == delim) {
                    i += 2;
                    if (str[i] == delim)
                        i++;
                    state &= ~IN_PPL_STRING;
                    break;
                }
            }
            type_decl = 0;
            break;
        case '-':
            if (str[i] == '>') {  /* function return type */
                i++;
                type_decl = 1;
                style = PPL_STYLE_KEYWORD;
                break;
            }
            type_decl = 0;  /* subtraction operator */
            continue;
        case '<':
            if (str[i] != '=' && type_decl == 2)
                type_decl = 1;
            else
                type_decl = 0;
            continue;
        case '>':
            if (!(str[i] != '=' && type_decl == 2))
                type_decl = 0;
            continue;
        case '#':
            if (start == 0 && str[i] == '!') {
                /* recognize a shebang comment line */
                style = PPL_STYLE_PREPROCESS;
                i = n;
                break;
            }
            /* fallthrough */
        case '=':
            if (str[i] == 'v' || str[i] == 'r')
                i++;
            type_decl = 0;
            continue;
        case ':':
            if (strequal(kbuf, "type"))
                type_decl = 1;
            else
                type_decl = 0;
            continue;
        case '.':
            type_decl = 0;
            if (start == indent && i == n) {
                style = PPL_STYLE_KEYWORD;
                break;
            }
            continue;
        default:
        normal:
            if (qe_isdigit(c)) {
                /* XXX: should parse actual number syntax */
                while (qe_isalnum(str[i]) ||
                       (str[i] == '.' && qe_isdigit(str[i + 1])) ||
                       ((str[i] == '+' || str[i] == '-') &&
                        qe_tolower(str[i - 1]) == 'e' &&
                        qe_isdigit(str[i + 1])))
                {
                    i++;
                }
                style = PPL_STYLE_NUMBER;
                break;
            }
            if (is_js_identifier_start(c)) {
                if (start == indent && cp_match_keywords(str, n, i - 1, ppl_phrases, &i)) {
                    style = PPL_STYLE_KEYWORD;
                    type_decl = 0;
                    break;
                }
                i += get_js_identifier(kbuf, countof(kbuf), c, str, i, n);
                if (cp->state_only)
                    continue;

                if (strfind(syn->keywords, kbuf) || str[i] == ':') {
                    if (strequal(kbuf, "null") && type_decl == 1) {
                        // null as a type
                    } else {
                        style = PPL_STYLE_KEYWORD;
                        if (strfind("on|factory|type|when|inherit", kbuf)) {
                            type_decl = 1;
                        } else
                        if (strequal(kbuf, "or") && type_decl == 2) {
                            type_decl = 1;
                        } else {
                            type_decl = 0;
                        }
                        if (start == indent
                        &&  strfind("function|creator|command|template|service|factory|type", kbuf))
                        {
                            int fstart = cp_skip_blanks(str, i, n);
                            if (get_js_identifier(kbuf, countof(kbuf), 0, str, fstart, n))
                                eb_add_tag(cp->b, cp->offset + start, kbuf);
                        } else
                        if (start == indent && strfind("java|java_header", kbuf)) {
                            state |= IN_PPL_JAVA;
                        }
                        break;
                    }
                }

                type_decl++;

                i1 = cp_skip_blanks(str, i, n);
                if (str[i1] == '(') {
                    /* function call or definition */
                    style = PPL_STYLE_FUNCTION;
                    type_decl = 0;
                    break;
                }

                if (type_decl == 2) {
                    style = PPL_STYLE_TYPE;
                    break;
                }

                if (strfind(syn->types, kbuf)) {
                    style = PPL_STYLE_TYPE;
                    break;
                }
                continue;
            }
            type_decl = 0;
            continue;
        }
        if (style) {
            if (!cp->state_only) {
                SET_STYLE(sbuf, start, i, style);
            }
            style = 0;
        }
    }

    if (state & (IN_PPL_COMMENT | IN_PPL_STRING)) {
        /* set style on eol char */
        SET_STYLE1(sbuf, n, style);
        if ((state & IN_PPL_COMMENT) == IN_PPL_COMMENT1)
            state &= ~IN_PPL_COMMENT1;
    } else {
        if (last != '\\' && last != '&') {
            // should line continuation extend single line comment?
            state &= ~IN_PPL_PREPROCESS;
        }
    }
 done:
    cp->colorize_state = state;
}

static ModeDef ppl_mode = {
    .name = "PPL",
    .extensions = "ppl",
    .shell_handlers = "ppl",
    .colorize_func = ppl_colorize_line,
    .colorize_flags = CLANG_PPL,
    .keywords = ppl_keywords,
    .types = ppl_types,
    .indent_func = c_indent_line,   // not really appropriate
    .auto_indent = 1,
    .fallback = &c_mode,
};
#endif  /* CONFIG_TINY */

/*---------------- SerenityOS yakt programming language ----------------*/

static const char jakt_keywords[] = {
    "and|anon|boxed|break|catch|class|continue|cpp|defer|else|enum|"
    "extern|false|for|fn|comptime|if|import|in|is|let|loop|match|"
    "must|mut|namespace|not|or|private|public|raw|return|restricted|"
    "struct|this|throw|throws|true|try|unsafe|weak|while|yield|guard|"
    "as|never|null|forall|type|trait|requires|implements"
};

static const char jakt_types[] = {
    "bool|i8|i16|i32|i64|u8|u16|u32|u64|f32|f64|usize|c_int|c_char|void|"
    /* could use CLANG_CAP_TYPE but would not match TT */
    "[A-Z][A-Za-z0-9]+"
};

// FIXME: Should support Attributes: start="#!\[" end="\]"

static ModeDef jakt_mode = {
    .name = "Jakt",
    .extensions = "jakt",
    .colorize_func = c_colorize_line,
    .colorize_flags = CLANG_JAKT,
    .keywords = jakt_keywords,
    .types = jakt_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
};

/*---------------- Christoffer Lerno's C3 programming language (c3-lang.org) ----------------*/

static const char c3_keywords[] = {
    "asm|assert|bitstruct|break|case|catch|const|continue|def|"
    "default|defer|distinct|do|else|enum|extern|false|fault|"
    "for|foreach|foreach_r|fn|tlocal|if|inline|import|macro|"
    "module|nextcase|null|return|static|struct|switch|true|try|"
    "union|var|while|"
    /* directives */
    "$alignof|$assert|$case|$checks|$default|$defined|"
    "$echo|$else|$endfor|$endforeach|$endif|$endswitch|"
    "$for|$foreach|$if|$include|$nameof|$offsetof|"
    "$qnameof|$sizeof|$stringify|$switch|$vacount|$vaconst|"
    "$varef|$vaarg|$vaexpr|$vasplat|"
};

static const char c3_types[] = {
    "void|bool|ichar|char|"
    // Integer types
    "short|ushort|int|uint|long|ulong|int128|uint128|iptr|uptr|isz|usz|"
    // Floating point types
    "float16|float|double|float128|"
    // Other types
    "any|anyfault|typeid|"
    // C compatibility types
    "CChar|CShort|CUShort|CInt|CUInt|CLong|CULong|CLongLong|CULongLong|CFloat|CDouble|CLongDouble|"
    // CT types
    "$typefrom|$tyypeof|$vatype|"
    /* could use CLANG_CAP_TYPE but would not match TT */
    "[A-Z][A-Za-z0-9]+"
};

enum {
    C3_STYLE_DEFAULT    = 0,
    C3_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    C3_STYLE_COMMENT    = QE_STYLE_COMMENT,
    C3_STYLE_STRING     = QE_STYLE_STRING,
    C3_STYLE_STRING_Q   = QE_STYLE_STRING_Q,
    C3_STYLE_STRING_BQ  = QE_STYLE_STRING,
    C3_STYLE_NUMBER     = QE_STYLE_NUMBER,
    C3_STYLE_KEYWORD    = QE_STYLE_KEYWORD,
    C3_STYLE_TYPE       = QE_STYLE_TYPE,
    C3_STYLE_FUNCTION   = QE_STYLE_FUNCTION,
    C3_STYLE_VARIABLE   = QE_STYLE_VARIABLE,
};

/* c-mode colorization states */
enum {
    IN_C3_COMMENT    = 0x03,  /* one of the comment styles */
    IN_C3_COMMENT1   = 0x01,  /* single line comment with \ at EOL */
    IN_C3_COMMENT2   = 0x02,  /* multiline C3 comment */
    IN_C3_STRING_BQ  = 0x04,  /* back-quoted string */
    IN_C3_CONTRACT1  = 0x40,  /* in the beginning of a contracts specification */
    IN_C3_CONTRACT2  = 0x80,  /* in the contracts part of a contracts specification */
    IN_C3_CONTRACTS  = 0xC0,  /* in a contracts specification */
    IN_C3_COMMENT_SHIFT = 8,  /* shift for block comment nesting level */
    IN_C3_COMMENT_LEVEL = 0x700, /* mask for block comment nesting level */
};

// FIXME: clean this, keep only C3 features
static void c3_colorize_line(QEColorizeContext *cp,
                             const char32_t *str, int n,
                             QETermStyle *sbuf, ModeDef *syn)
{
    int i = 0, start, i1, i2, indent;
    int style, level, tag;
    char32_t c, delim;
    char kbuf[64];
    int state = cp->colorize_state;
    //int type_decl;  /* unused */

    indent = cp_skip_blanks(str, 0, n);
    tag = !indent && cp->s->mode == syn;
    start = i;
    //type_decl = 0;
    c = 0;
    style = 0;

    if (i >= n)
        goto the_end;

    if (state) {
        /* if already in a state, go directly in the code parsing it */
        if (state & IN_C3_COMMENT2)
            goto parse_comment2;
        if (state & IN_C3_STRING_BQ)
            goto parse_string_bq;
        if (state & IN_C3_CONTRACT1)
            goto parse_contracts;
    }

    while (i < n) {
        start = i;
    reswitch:
        c = str[i++];
        switch (c) {
        case '*':
            if ((state & IN_C3_CONTRACTS) && str[i] == '>') {
                i += 2;
                state &= ~IN_C3_CONTRACTS;
                style = C3_STYLE_PREPROCESS;
                break;
            }
            /* lone star at the beginning of a line in a shell buffer
             * is treated as a comment start.  This improves colorization
             * of diff and git output.
             */
            if (start == indent && cp->partial_file
            &&  (i == n || str[i] == ' ' || str[i] == '/')) {
                i--;
                goto parse_comment2;
            }
            continue;
        case '/':
            if (str[i] == '*') {
                /* C style multi-line comment */
                i++;
            parse_comment2:
                state |= IN_C3_COMMENT2;
                style = C3_STYLE_COMMENT;
                level = (state & IN_C3_COMMENT_LEVEL) >> IN_C3_COMMENT_SHIFT;
                while (i < n) {
                    if (str[i] == '/' && str[i + 1] == '*') {
                        i += 2;
                        level++;
                    } else
                    if (str[i] == '*' && str[i + 1] == '/') {
                        i += 2;
                        if (level == 0) {
                            state &= ~IN_C3_COMMENT2;
                            break;
                        }
                        level--;
                    } else {
                        i++;
                    }
                }
                state = (state & ~IN_C3_COMMENT_LEVEL) |
                        (min_int(level, 7) << IN_C3_COMMENT_SHIFT);
                break;
            } else
            if (str[i] == '/') {
                /* line comment */
                style = C3_STYLE_COMMENT;
                i = n;
                break;
            }
            continue;
        case '#':       /* preprocessor */
            if (start == 0 && str[i] == '!') {
                /* recognize a shebang comment line */
                style = C3_STYLE_PREPROCESS;
                i = n;
                break;
            }
            continue;
        case '@':       /* annotations or constraints */
            i += get_js_identifier(kbuf, countof(kbuf), c, str, i, n);
            style = C3_STYLE_PREPROCESS;
            break;
        case '`':
        parse_string_bq:
            state |= IN_C3_STRING_BQ;
            style = C3_STYLE_STRING_BQ;
            while (i < n) {
                c = str[i++];
                if (c == '`' && str[i] != '`') {
                    state &= ~IN_C3_STRING_BQ;
                    break;
                }
            }
            break;

        case '\'':      /* character constant */
            style = C3_STYLE_STRING_Q;
            delim = '\'';
            goto string;

        case '\"':      /* string literal */
            style = C3_STYLE_STRING;
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
                    break;
                }
            }
            break;
        case '=':
            /* exit type declaration */
            /* does not handle this: int i = 1, j = 2; */
            //type_decl = 0;
            tag = 0;
            continue;
        case '<':
            if (!(state & IN_C3_CONTRACTS) && str[i] == '*') {
                state |= IN_C3_CONTRACT1;
                i++;
                SET_STYLE(sbuf, start, i, C3_STYLE_PREPROCESS);
                start = i;
            parse_contracts:
                /* C3 contracts initial part */
                while (i < n && qe_isspace(str[i]))
                    i++;
                style = C3_STYLE_COMMENT;
                if (str[i] == '@' && qe_islower(str[i + 1])) {
                    state |= IN_C3_CONTRACT2;
                    break;
                }
                while (i < n && (str[i] != '*' || str[i + 1] != '>'))
                    i++;
                break;
            }
            continue;
        case '(':
        case '{':
            tag = 0;
            continue;
        default:
            if (qe_isdigit(c)) {
                /* XXX: should parse actual number syntax */
                /* decimal, binary, octal and hexadecimal literals:
                 * 1 0b1 0o1 0x1, case insensitive. 01 is a syntax error */
                /* ignore '_' between digits */
                /* XXX: should parse decimal and hex floating point syntaxes */
                while (qe_isalnum_(str[i]) || (str[i] == '.' && str[i + 1] != '.')) {
                    i++;
                }
                style = C3_STYLE_NUMBER;
                break;
            }
            if (is_js_identifier_start(c)) {
                i += get_js_identifier(kbuf, countof(kbuf), c, str, i, n);
                if (cp->state_only)
                    continue;
                if (str[i] == '\'' || str[i] == '\"') {
                    /* check for encoding prefix */
                    if (strfind("x|b64", kbuf))
                        goto reswitch;
                }
                /* keywords used as object property tags are regular identifiers.
                 * `default` is always considered a keyword as the context cannot be
                 * determined precisely by this simplistic lexical parser */
                if (strfind(syn->keywords, kbuf)
                &&  (str[i] != ':' || strequal(kbuf, "default") || strequal(kbuf, "$default"))
                &&  (start == 0 || str[start - 1] != '.')) {
                    if (*kbuf == '$')
                        style = C3_STYLE_PREPROCESS;
                    else
                        style = C3_STYLE_KEYWORD;
                    break;
                }

                i1 = cp_skip_blanks(str, i, n);

                if (str[i1] == '(') {
                    /* function call or definition */
                    style = C3_STYLE_FUNCTION;
                    if (tag) {
                        /* tag function definition */
                        eb_add_tag(cp->b, cp->offset + start, kbuf);
                        tag = 0;
                    }
                    break;
                } else
                if (tag && qe_findchar("(,;=", str[i1])) {
                    /* tag variable definition */
                    eb_add_tag(cp->b, cp->offset + start, kbuf);
                }

                if ((start == 0 || str[start - 1] != '.')
                &&  !qe_findchar(".(:", str[i])
                &&  strfind(syn->types, kbuf)) {
                    /* if not cast, assume type declaration */
                    //type_decl = 1;
                    style = C3_STYLE_TYPE;
                    break;
                }
                if (qe_isupper((unsigned char)kbuf[0])) {
                    for (i2 = 1; kbuf[i2]; i2++) {
                        if (qe_islower((unsigned char)kbuf[i2]))
                            break;
                    }
                    /* if capitalized but not all caps assume type name */
                    if (kbuf[i2]) {
                        style = C3_STYLE_TYPE;
                        break;
                    }
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
 the_end:
    if (style == C3_STYLE_COMMENT || (state & IN_C3_STRING_BQ)) {
        /* set style on eol char */
        SET_STYLE1(sbuf, n, style);
    }
    cp->colorize_state = state;
}

static ModeDef c3_mode = {
    .name = "C3",
    .extensions = "c3|c3i|c3t",
    .colorize_func = c3_colorize_line,
    .colorize_flags = CLANG_C3 | CLANG_NEST_COMMENTS,
    .keywords = c3_keywords,
    .types = c3_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
};

/*---------------- Common initialization code ----------------*/

static int c_init(QEmacsState *qs)
{
    qe_register_mode(qs, &c_mode, MODEF_SYNTAX);
    qe_register_commands(qs, &c_mode, c_commands, countof(c_commands));
    qe_register_mode(qs, &cpp_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &js_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &java_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &php_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &go_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &yacc_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &lex_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &csharp_mode, MODEF_SYNTAX);
#ifndef CONFIG_TINY
    qe_register_mode(qs, &v8_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &bee_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &idl_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &carbon_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &c2_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &objc_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &awk_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &css_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &less_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &json_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &ts_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &jspp_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &koka_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &as_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &scala_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &d_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &limbo_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &cyclone_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &ch_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &squirrel_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &ici_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &jsx_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &haxe_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &dart_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &pike_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &idl_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &calc_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &enscript_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &qscript_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &ec_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &sl_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &csl_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &neko_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &nml_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &alloy_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &scilab_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &kotlin_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &cbang_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &vala_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &pawn_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &cminus_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &gmscript_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &wren_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &jack_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &smac_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &v_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &protobuf_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &odin_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &salmon_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &ppl_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &jakt_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &c3_mode, MODEF_SYNTAX);
#endif
    return 0;
}

qe_module_init(c_init);
