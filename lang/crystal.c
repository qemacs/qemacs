/*
 * Crystal language mode for QEmacs.
 *
 * Copyright (c) 2000-2023 Charlie Gordon.
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

/*---------------- Crystal coloring ----------------*/

static char const crystal_keywords[] = {
    // atomWords
    "|false|nil|true|self"
    // keywords
    "|abstract|alias|annotation|asm|begin|break|case|class"
    "|def|do|else|elsif|end|ensure|enum|extend|for|fun"
    "|if|in|include|instance_sizeof|lib|macro|module"
    "|next|of|offsetof|out|pointerof|private|protected|require"
    "|rescue|return|select|sizeof|struct|super"
    "|then|type|typeof|union|uninitialized|unless|until"
    "|verbatim|when|while|with|yield"
    // pseudo keywords
    "|as|as?|in|is_a?|nil|nil?|responds_to?"
    // PREPROC
    "|__DIR__|__END_LINE__|__FILE__|__LINE__"
    //???
    //"|describe|eq|getter|getter?|it|loop|property|property!|property?"
    //"|raise|record|spawn|"
    //???
    //"|and|as|assert|continue|del|except|finally|for|from|global"
    //"|import|in|is|lambda|nonlocal|not|or|pass|raise|try|"
    "|"
};

static char const crystal_types[] = {
    "|Bool|Char|Int32|String"
    "|"
};

enum {
    IN_CRYSTAL_HEREDOC   = 0x80,
    IN_CRYSTAL_HD_INDENT = 0x40,
    IN_CRYSTAL_HD_SIG    = 0x3f,
    IN_CRYSTAL_COMMENT   = 0x40,
    IN_CRYSTAL_STRING    = 0x20      /* single quote */,
    IN_CRYSTAL_STRING2   = 0x10      /* double quote */,
    IN_CRYSTAL_STRING3   = 0x08      /* back quote */,
    IN_CRYSTAL_STRING4   = 0x04      /* %q{...} */,
    IN_CRYSTAL_REGEX     = 0x02,
    IN_CRYSTAL_POD       = 0x01,
};

enum {
    CRYSTAL_STYLE_TEXT =     QE_STYLE_DEFAULT,
    CRYSTAL_STYLE_COMMENT =  QE_STYLE_COMMENT,
    CRYSTAL_STYLE_STRING =   QE_STYLE_STRING,
    CRYSTAL_STYLE_STRING2 =  QE_STYLE_STRING,
    CRYSTAL_STYLE_STRING3 =  QE_STYLE_STRING,
    CRYSTAL_STYLE_STRING4 =  QE_STYLE_STRING,
    CRYSTAL_STYLE_REGEX =    QE_STYLE_STRING_Q,
    CRYSTAL_STYLE_NUMBER =   QE_STYLE_NUMBER,
    CRYSTAL_STYLE_KEYWORD =  QE_STYLE_KEYWORD,
    CRYSTAL_STYLE_FUNCTION = QE_STYLE_FUNCTION,
    CRYSTAL_STYLE_MEMBER =   QE_STYLE_DEFAULT,
    CRYSTAL_STYLE_HEREDOC =  QE_STYLE_PREPROCESS,
};

static int crystal_get_name(char *buf, int size, const char32_t *str) {
    int len, i = 0, j;

    for (len = 0, j = i; qe_isalnum_(str[j]); j++) {
        if (len < size - 1)
            buf[len++] = str[j];
    }
    if (str[j] == '?' || str[j] == '!') {
        if (len < size - 1)
            buf[len++] = str[j];
        j++;
    }
    if (len < size) {
        buf[len] = '\0';
    }
    return j - i;
}

static void crystal_colorize_line(QEColorizeContext *cp,
                                  char32_t *str, int n, ModeDef *syn)
{
    int i = 0, j, start = i, style = 0, indent, sig;
    char32_t c;
    static char32_t sep, sep0;      /* XXX: ugly patch */
    static int level;               /* XXX: ugly patch */
    int state = cp->colorize_state;
    char kbuf[64];

    for (indent = 0; qe_isblank(str[indent]); indent++)
        continue;

    if (state & IN_CRYSTAL_HEREDOC) {
        if (state & IN_CRYSTAL_HD_INDENT) {
            while (qe_isblank(str[i]))
                i++;
        }
        sig = 0;
        if (qe_isalpha_(str[i])) {
            sig = str[i++] % 61;
            for (; qe_isalnum_(str[i]); i++) {
                sig = ((sig << 6) + str[i]) % 61;
            }
        }
        for (; qe_isblank(str[i]); i++)
            continue;
        i = n;
        SET_COLOR(str, start, i, CRYSTAL_STYLE_HEREDOC);
        if (i > 0 && i == n && (state & IN_CRYSTAL_HD_SIG) == (sig & IN_CRYSTAL_HD_SIG))
            state &= ~(IN_CRYSTAL_HEREDOC | IN_CRYSTAL_HD_INDENT | IN_CRYSTAL_HD_SIG);
    } else {
        if (state & IN_CRYSTAL_COMMENT)
            goto parse_c_comment;

        if (state & IN_CRYSTAL_REGEX)
            goto parse_regex;

        if (state & IN_CRYSTAL_STRING)
            goto parse_string;

        if (state & IN_CRYSTAL_STRING2)
            goto parse_string2;

        if (state & IN_CRYSTAL_STRING3)
            goto parse_string3;

        if (state & IN_CRYSTAL_STRING4)
            goto parse_string4;

        if (str[i] == '=' && qe_isalpha(str[i + 1])) {
            state |= IN_CRYSTAL_POD;
        }
        if (state & IN_CRYSTAL_POD) {
            if (ustrstart(str + i, "=end", NULL)) {
                state &= ~IN_CRYSTAL_POD;
            }
            style = CRYSTAL_STYLE_COMMENT;
            if (str[i] == '=' && qe_isalpha(str[i + 1]))
                style = CRYSTAL_STYLE_KEYWORD;
            i = n;
            SET_COLOR(str, start, i, style);
        }
    }

    while (i < n && qe_isblank(str[i]))
        i++;

    indent = i;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '/':
            if (str[i] == '*') {
                /* C comment */
                i++;
            parse_c_comment:
                state = IN_CRYSTAL_COMMENT;
                for (; i < n; i++) {
                    if (str[i] == '*' && str[i + 1] == '/') {
                        i += 2;
                        state &= ~IN_CRYSTAL_COMMENT;
                        break;
                    }
                }
                goto comment;
            }
            if (start == indent
            ||  (str[i] != ' ' && str[i] != '='
            &&   i >= 2
            &&   !qe_isalnum(str[i - 2] & CHAR_MASK)
            &&   (str[i - 2] & CHAR_MASK) != ')')) {
                /* XXX: should use context to tell regex from divide */
                /* parse regex */
                state = IN_CRYSTAL_REGEX;
            parse_regex:
                while (i < n) {
                    /* XXX: should ignore / inside char classes */
                    c = str[i++];
                    if (c == '\\') {
                        if (i < n) {
                            i += 1;
                        }
                    } else
                    if (c == '#' && str[i] == '{') {
                        /* should parse full syntax */
                        while (i < n && str[i++] != '}')
                            continue;
                    } else
                    if (c == '/') {
                        while (qe_findchar("ensuimox", str[i])) {
                            i++;
                        }
                        state = 0;
                        break;
                    }
                }
                style = CRYSTAL_STYLE_REGEX;
                break;
            }
            continue;

        case '#':
            i = n;
        comment:
            style = CRYSTAL_STYLE_COMMENT;
            break;

        case '%':
            /* parse alternate string/array syntaxes */
            if (str[i] != '\0' && !qe_isblank(str[i]) && !qe_isalnum(str[i]))
                goto has_string4;

            if (str[i] == 'q' || str[i] == 'Q'
            ||  str[i] == 'r' || str[i] == 'x'
            ||  str[i] == 'w' || str[i] == 'W') {
                i++;
            has_string4:
                level = 0;
                sep = sep0 = str[i++];
                if (sep == '{') sep = '}';
                if (sep == '(') sep = ')';
                if (sep == '[') sep = ']';
                if (sep == '<') sep = '>';
                /* parse special string const */
                state = IN_CRYSTAL_STRING4;
            parse_string4:
                while (i < n) {
                    c = str[i++];
                    if (c == sep) {
                        if (level-- == 0) {
                            state = level = 0;
                            break;
                        }
                        /* XXX: should parse regex modifiers if %r */
                    } else
                    if (c == sep0) {
                        level++;
                    } else
                    if (c == '#' && str[i] == '{') {
                        /* XXX: should no parse if %q */
                        /* XXX: should parse full syntax */
                        while (i < n && str[i++] != '}')
                            continue;
                    } else
                    if (c == '\\') {
                        if (i < n) {
                            i += 1;
                        }
                    }
                }
                style = CRYSTAL_STYLE_STRING4;
                break;
            }
            continue;

        case '\'':
            /* parse single quoted string const */
            state = IN_CRYSTAL_STRING;
        parse_string:
            while (i < n) {
                c = str[i++];
                if (c == '\\' && (str[i] == '\\' || str[i] == '\'')) {
                    i += 1;
                } else
                if (c == '\'') {
                    state = 0;
                    break;
                }
            }
            style = CRYSTAL_STYLE_STRING;
            break;

        case '`':
            /* parse single quoted string const */
            state = IN_CRYSTAL_STRING3;
        parse_string3:
            while (i < n) {
                c = str[i++];
                if (c == '\\' && (str[i] == '\\' || str[i] == '\'')) {
                    i += 1;
                } else
                if (c == '#' && str[i] == '{') {
                    /* should parse full syntax */
                    while (i < n && str[i++] != '}')
                        continue;
                } else
                if (c == '`') {
                    state = 0;
                    break;
                }
            }
            style = CRYSTAL_STYLE_STRING3;
            break;

        case '\"':
            /* parse double quoted string const */
        parse_string2:
            c = '\0';
            while (i < n) {
                c = str[i++];
                if (c == '\\') {
                    if (i < n) {
                        i += 1;
                    }
                } else
                if (c == '#' && str[i] == '{') {
                    /* should parse full syntax */
                    while (i < n && str[i++] != '}')
                        continue;
                } else
                if (c == '\"') {
                    break;
                }
            }
            if (c == '\"') {
                if (state == IN_CRYSTAL_STRING2)
                    state = 0;
            } else {
                if (state == 0)
                    state = IN_CRYSTAL_STRING2;
            }
            style = CRYSTAL_STYLE_STRING2;
            break;

        case '<':
            if (str[i] == '<') {
                /* XXX: should use context to tell lshift from heredoc:
                 * here documents are introduced by monadic <<.
                 * Monadic use could be detected in some contexts, such
                 * as eval(<<EOS), but not in the general case.
                 * We use a heuristical approach: let's assume here
                 * document ids are not separated from the << by white
                 * space.
                 * XXX: should parse full here document syntax.
                 */
                sig = 0;
                j = i + 1;
                if (str[j] == '-') {
                    j++;
                }
                if ((str[j] == '\'' || str[j] == '\"')
                &&  qe_isalpha_(str[j + 1])) {
                    sep = str[j++];
                    sig = str[j++] % 61;
                    for (; qe_isalnum_(str[j]); j++) {
                        sig = ((sig << 6) + str[j]) % 61;
                    }
                    if (str[j++] != sep)
                        break;
                } else
                if (qe_isalpha_(str[j])) {
                    sig = str[j++] % 61;
                    for (; qe_isalnum_(str[j]); j++) {
                        sig = ((sig << 6) + str[j]) % 61;
                    }
                }
                if (sig) {
                    /* Multiple here documents can be specified on the
                     * same line, only the last one will prevail, which
                     * is OK for coloring purposes.
                     * state will be cleared if a string or a comment
                     * start on the line after the << operator.  This
                     * is a bug due to limited state bits.
                     */
                    state &= ~(IN_CRYSTAL_HEREDOC | IN_CRYSTAL_HD_INDENT | IN_CRYSTAL_HD_SIG);
                    state |= IN_CRYSTAL_HEREDOC;
                    if (str[i + 1] == '-') {
                        state |= IN_CRYSTAL_HD_INDENT;
                    }
                    state |= (sig & IN_CRYSTAL_HD_SIG);
                    i = j;
                    style = CRYSTAL_STYLE_HEREDOC;
                    break;
                }
            }
            continue;

        case '?':
            /* XXX: should parse character constants */
            continue;

        case '.':
            if (qe_isdigit_(str[i]))
                goto parse_decimal;
            continue;

        case '$':
            /* XXX: should parse precise $ syntax,
             * skip $" and $' for now
             */
            if (i < n)
                i++;
            continue;

        case ':':
            /* XXX: should parse Crystal symbol */
            continue;

        case '@':
            i += crystal_get_name(kbuf, countof(kbuf), str + i);
            style = CRYSTAL_STYLE_MEMBER;
            break;

        default:
            if (qe_isdigit(c)) {
                if (c == '0' && qe_tolower(str[i]) == 'b') {
                    /* binary numbers */
                    for (i += 1; qe_isbindigit_(str[i]); i++)
                        continue;
                } else
                if (c == '0' && qe_tolower(str[i]) == 'o') {
                    /* octal numbers */
                    for (i += 1; qe_isoctdigit_(str[i]); i++)
                        continue;
                } else
                if (c == '0' && qe_tolower(str[i]) == 'x') {
                    /* hexadecimal numbers */
                    for (i += 1; qe_isxdigit_(str[i]); i++)
                        continue;
                } else
                if (c == '0' && qe_tolower(str[i]) == 'd') {
                    /* hexadecimal numbers */
                    for (i += 1; qe_isdigit_(str[i]); i++)
                        continue;
                } else {
                    /* decimal numbers */
                    for (; qe_isdigit_(str[i]); i++)
                        continue;
                    if (str[i] == '.') {
                        i++;
                    parse_decimal:
                        for (; qe_isdigit_(str[i]); i++)
                            continue;
                    }
                    if (qe_tolower(str[i]) == 'e') {
                        int k = i + 1;
                        if (str[k] == '+' || str[k] == '-')
                            k++;
                        if (qe_isdigit_(str[k])) {
                            for (i = k + 1; qe_isdigit_(str[i]); i++)
                                continue;
                        }
                    }
                }
                /* XXX: should detect malformed number constants */
                style = CRYSTAL_STYLE_NUMBER;
                break;
            }
            if (qe_isalpha_(c)) {
                i--;
                i += crystal_get_name(kbuf, countof(kbuf), str + i);

                if (strfind(syn->keywords, kbuf)) {
                    style = CRYSTAL_STYLE_KEYWORD;
                    break;
                }
                if (qe_isblank(str[i]))
                    i++;
                if (str[i] == '(' || str[i] == '{') {
                    style = CRYSTAL_STYLE_FUNCTION;
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

static ModeDef crystal_mode = {
    .name = "Crystal",
    .extensions = "cr",
    .shell_handlers = "crystal",
    .keywords = crystal_keywords,
    .types = crystal_types,
    .colorize_func = crystal_colorize_line,
};

static int crystal_init(void)
{
    qe_register_mode(&crystal_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(crystal_init);
