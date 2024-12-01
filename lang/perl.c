/*
 * Perl Source mode for QEmacs.
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
#include "clang.h"

/*---------------- Perl colors ----------------*/

static char const perl_keywords[] = {
    /* Perl keywords */
    "ge|gt|le|lt|cmp|eq|ne|int|x|or|and|not|xor|"  // special case x=
    "do|else|elsif|if|for|until|while|foreach|unless|last|"
    "require|package|use|strict|BEGIN|bless|isa|"
    "sub|return|eval|try|catch|with|throw|except|otherwise|finally|"
    "undef|true|false|"
    "exit|die|warn|system|"
    "print|printf|open|close|readline|read|binmode|seek|tell|flock|"
    "opendir|closedir|readdir|unlink|rename|chdir|truncate|"
    "chmod|kill|killall|"
    "chomp|pos|length|substr|lc|uc|lcfirst|ucfirst|split|hex|"
    "sprintf|index|"
    "reverse|pop|push|shift|unshift|splice|join|map|sort|"
    "delete|insert|keys|values|exists|defined|"
    "scalar|wantarray|ref|"
    "STDIN|STDOUT|STDERR|"
};

static char const perl_types[] = {
    "my|local|"
};

// qq~ multiline string ~

enum {
    PERL_STYLE_TEXT    = QE_STYLE_DEFAULT,
    PERL_STYLE_SHBANG  = QE_STYLE_PREPROCESS,
    PERL_STYLE_COMMENT = QE_STYLE_COMMENT,
    PERL_STYLE_STRING  = QE_STYLE_STRING,
    PERL_STYLE_REGEX   = QE_STYLE_STRING,
    PERL_STYLE_DELIM   = QE_STYLE_KEYWORD,
    PERL_STYLE_KEYWORD = QE_STYLE_KEYWORD,
    PERL_STYLE_FUNCTION = QE_STYLE_FUNCTION,
    PERL_STYLE_VAR     = QE_STYLE_VARIABLE,
    PERL_STYLE_NUMBER  = QE_STYLE_NUMBER,
};

enum {
    IN_PERL_STRING1 = 0x01,    /* single quote */
    IN_PERL_STRING2 = 0x02,    /* double quote */
    IN_PERL_FORMAT  = 0x04,    /* format = ... */
    IN_PERL_HEREDOC = 0x08,
    IN_PERL_POD     = 0x10,
};

/* CG: bogus if multiple regions are colorized, should use signature */
/* XXX: should move this to mode data */
static char32_t perl_eos[100];
static int perl_eos_len;

static int perl_var(const char32_t *str, int j, int n)
{
    if (qe_isdigit_(str[j]))
        return j;
    for (; j < n; j++) {
        if (qe_isalnum_(str[j]))
            continue;
        if (str[j] == '\'' && qe_isalpha_(str[j + 1]))
            j++;
        else
            break;
    }
    return j;
}

static int perl_number(const char32_t *str, int j, qe__unused__ int n)
{
    if (str[j] == '0') {
        j++;
        if (str[j] == 'x' || str[j] == 'X') {
            /* hexadecimal numbers */
            // XXX: should verify if 'X' is really accepted
            // XXX: should accept embedded '_'
            do { j++; } while (qe_isxdigit(str[j]));
            return j;
        }
        if (str[j] >= '0' && str[j] <= '7') {
            /* octal numbers */
            // XXX: should accept embedded '_'
            do { j++; } while (str[j] >= '0' && str[j] <= '7');
            return j;
        }
    }
    // XXX: should accept embedded '_'
    while (qe_isdigit(str[j]))
        j++;

    /* integral part is optional */
    if (str[j] == '.') {
        // XXX: should accept embedded '_'
        do { j++; } while (qe_isdigit(str[j]));
    }

    // XXX: should verify if 'E' is really accepted
    if (str[j] == 'E' || str[j] == 'e') {
        j++;
        if (str[j] == '-' || str[j] == '+')
            j++;
        while (qe_isdigit(str[j]))
            j++;
    }
    return j;
}

/* return offset of matching delimiter or end of string */
static int perl_string(const char32_t *str, char32_t delim, int j, int n) {
    for (; j < n; j++) {
        if (str[j] == '\\')
            j++;
        else
        if (str[j] == delim)
            return j;
    }
    return j;
}

static void perl_colorize_line(QEColorizeContext *cp,
                               const char32_t *str, int n,
                               QETermStyle *sbuf, ModeDef *syn)
{
    int i = 0, start = i, j, s1, s2, delim = 0, indent, style, klen;
    char32_t c, c1, c2;
    int colstate = cp->colorize_state;
    char kbuf[64];

    indent = cp_skip_blanks(str, 0, n);

    if (colstate & (IN_PERL_STRING1 | IN_PERL_STRING2)) {
        delim = (colstate & IN_PERL_STRING1) ? '\'' : '\"';
        i = perl_string(str, delim, start, n);
        if (i < n) {
            i++;
            colstate &= ~(IN_PERL_STRING1 | IN_PERL_STRING2);
        }
        SET_STYLE(sbuf, start, i, PERL_STYLE_STRING);
    } else
    if (colstate & IN_PERL_FORMAT) {
        i = n;
        if (n == 1 && str[0] == '.')
            colstate &= ~IN_PERL_FORMAT;
        SET_STYLE(sbuf, start, i, PERL_STYLE_STRING);
    }
    if (colstate & IN_PERL_HEREDOC) {
        i = n;
        if (n == perl_eos_len && !umemcmp(perl_eos, str, n)) {
            colstate &= ~IN_PERL_HEREDOC;
            SET_STYLE(sbuf, start, i, PERL_STYLE_KEYWORD);
        } else {
            SET_STYLE(sbuf, start, i, PERL_STYLE_STRING);
        }
    }
    if (str[i] == '=' && qe_isalpha(str[i + 1])) {
        colstate |= IN_PERL_POD;
    }
    if (colstate & IN_PERL_POD) {
        if (ustrstart(str + i, "=cut", NULL)) {
            colstate &= ~IN_PERL_POD;
        }
        if (str[i] == '=' && qe_isalpha(str[i + 1])) {
            i = n;
            SET_STYLE(sbuf, start, i, PERL_STYLE_KEYWORD);
        } else {
            i = n;
            SET_STYLE(sbuf, start, i, PERL_STYLE_COMMENT);
        }
    }

    style = 0;
    while (i < n) {
        start = i;
        c = str[i++];
        c1 = str[i];
        switch (c) {
        case '$':
            if (c1 == '^' && qe_isalpha(str[i + 1])) {
                i += 2;
            } else
            if (c1 == '#' && qe_isalpha_(str[i + 1])) {
                i += 2;
            } else
            if (qe_findchar("|%=-~^123456789&`'+_./\\,\"#$?*0[];!@", c1)) {
                /* Special variable */
                i += 1;
                style = PERL_STYLE_KEYWORD;
                break;
            }
            fallthrough;
        case '*':
        case '@':       /* arrays */
        case '%':       /* associative arrays */
        case '&':
            if (i < n) {
                s1 = perl_var(str, i, n);
                if (s1 > i) {
                    i = s1;
                    style = PERL_STYLE_VAR;
                    break;
                }
            }
            continue;
        case '-':
            if (c1 == '-') {
                i += 1;
                continue;
            }
            if (qe_isalpha(c1) && !qe_isalnum(str[i + 1])) {
                i += 2;
                style = PERL_STYLE_KEYWORD;
                break;
            }
            continue;
        case '#':
            style = PERL_STYLE_COMMENT;
            if (start == 0 && c1 == '!')
                style = PERL_STYLE_SHBANG;
            i = n;
            break;
        case '<':
            if (c1 == '<') {
                /* Should check for unary context */
                s1 = cp_skip_blanks(str, i + 1, n);
                c2 = str[s1];
                if (c2 == '"' || c2 == '\'' || c2 == '`') {
                    s2 = perl_string(str, c2, ++s1, n);
                } else {
                    s2 = perl_var(str, s1, n);
                }
                if (s2 > s1) {
                    perl_eos_len = min_int((int)(s2 - s1), countof(perl_eos) - 1);
                    umemcpy(perl_eos, str + s1, perl_eos_len);
                    perl_eos[perl_eos_len] = '\0';
                    colstate |= IN_PERL_HEREDOC;
                }
                i += 1;
                continue;
            }
            delim = '>';
            goto string;
        case '/':
        case '?':
            /* Should check for unary context */
            /* parse regex */
            s1 = perl_string(str, c, i, n);
            if (s1 >= n)
                break;
            //SET_STYLE1(sbuf, start, PERL_STYLE_DELIM);
            i = s1;
            //SET_STYLE(sbuf, start + 1, i, PERL_STYLE_REGEX);
            start = i;
            while (++i < n && qe_isalpha(str[i]))
                continue;
            //SET_STYLE(sbuf, start, i, PERL_STYLE_DELIM);
            //continue;
            style = PERL_STYLE_REGEX;
            break;
        case '\'':
        case '`':
        case '"':
            delim = c;
        string:
            /* parse string const */
            s1 = perl_string(str, delim, i, n);
            if (s1 >= n) {
                if (c == '\'') {
                    colstate |= IN_PERL_STRING1;
                    i = n;
                    style = PERL_STYLE_STRING;
                    break;
                }
                if (c == '\"') {
                    colstate |= IN_PERL_STRING2;
                    i = n;
                    style = PERL_STYLE_STRING;
                    break;
                }
                /* ` string spanning more than one line treated as
                 * operator.
                 */
                break;
            }
            s1++;
            i = s1;
            style = PERL_STYLE_STRING;
            break;
        case '.':
            if (qe_isdigit(c1))
                goto number;
            continue;

        default:
            if (qe_isdigit(c)) {
            number:
                i = perl_number(str, start, n);
                style = PERL_STYLE_NUMBER;
                break;
            }
            if (!qe_isalpha_(c))
                continue;

            //j = perl_var(str, start, n);
            klen = ustr_get_identifier(kbuf, countof(kbuf), c, str, i, n);
            j = i += klen;

            if (i >= n)
                goto keyword;

            /* Should check for context */
            if ((klen == 1 && (c == 'm' || c == 'q'))
            ||  (klen == 2 && c == 'q' && (c1 == 'q' || c1 == 'x'))) {
                s1 = perl_string(str, str[j], j + 1, n);
                if (s1 >= n)
                    goto keyword;
                SET_STYLE(sbuf, start, j + 1, PERL_STYLE_DELIM);
                i = s1;
                SET_STYLE(sbuf, j + 1, i, PERL_STYLE_REGEX);
                while (++i < n && qe_isalpha(str[i]))
                    continue;
                SET_STYLE(sbuf, s1, i, PERL_STYLE_DELIM);
                continue;
            }
            /* Should check for context */
            if ((klen == 1 && (c == 's' /* || c == 'y' */))
            ||  (klen == 2 && c == 't' && c1 == 'r')) {
                s1 = perl_string(str, str[j], j + 1, n);
                if (s1 >= n)
                    goto keyword;
                s2 = perl_string(str, str[j], s1 + 1, n);
                if (s2 >= n)
                    goto keyword;
                SET_STYLE(sbuf, start, j + 1, PERL_STYLE_DELIM);
                SET_STYLE(sbuf, j + 1, s1, PERL_STYLE_REGEX);
                SET_STYLE1(sbuf, s1, PERL_STYLE_DELIM);
                SET_STYLE(sbuf, s1 + 1, s2, PERL_STYLE_REGEX);
                i = s2;
                while (++i < n && qe_isalpha(str[i]))
                    continue;
                SET_STYLE(sbuf, s2, i, PERL_STYLE_DELIM);
                continue;
            }
        keyword:
            if (klen == 6 && !strcasecmp(kbuf, "format")) {
                if (start == indent) {
                    /* keyword is first on line */
                    colstate |= IN_PERL_FORMAT;
                }
            }
            if (strfind(syn->keywords, kbuf)) {
                style = PERL_STYLE_KEYWORD;
            } else {
                style = PERL_STYLE_FUNCTION;
            }
            break;
        }
        if (style) {
            SET_STYLE(sbuf, start, i, style);
            style = 0;
        }
    }
    cp->colorize_state = colstate;
}

static ModeDef perl_mode = {
    .name = "Perl",
    .extensions = "pl|perl|pm",
    .shell_handlers = "perl|perl5",
    .colorize_func = perl_colorize_line,
    .keywords = perl_keywords,
    .types = perl_types,
    .indent_func = c_indent_line,
    .auto_indent = 1,
};

static int perl_init(QEmacsState *qs)
{
    qe_register_mode(qs, &perl_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(perl_init);
