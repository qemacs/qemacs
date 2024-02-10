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
    PERL_STYLE_COMMENT = QE_STYLE_COMMENT,
    PERL_STYLE_STRING  = QE_STYLE_STRING,
    PERL_STYLE_REGEX   = QE_STYLE_STRING,
    PERL_STYLE_DELIM   = QE_STYLE_KEYWORD,
    PERL_STYLE_KEYWORD = QE_STYLE_KEYWORD,
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
                               char32_t *str, int n, ModeDef *syn)
{
    int i = 0, j = i, s1, s2, delim = 0;
    char32_t c, c1, c2;
    int colstate = cp->colorize_state;

    if (colstate & (IN_PERL_STRING1 | IN_PERL_STRING2)) {
        delim = (colstate & IN_PERL_STRING1) ? '\'' : '\"';
        i = perl_string(str, delim, j, n);
        if (i < n) {
            i++;
            colstate &= ~(IN_PERL_STRING1 | IN_PERL_STRING2);
        }
        SET_COLOR(str, j, i, PERL_STYLE_STRING);
    } else
    if (colstate & IN_PERL_FORMAT) {
        i = n;
        if (n == 1 && str[0] == '.')
            colstate &= ~IN_PERL_FORMAT;
        SET_COLOR(str, j, i, PERL_STYLE_STRING);
    }
    if (colstate & IN_PERL_HEREDOC) {
        i = n;
        if (n == perl_eos_len && !umemcmp(perl_eos, str, n)) {
            colstate &= ~IN_PERL_HEREDOC;
            SET_COLOR(str, j, i, PERL_STYLE_KEYWORD);
        } else {
            SET_COLOR(str, j, i, PERL_STYLE_STRING);
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
            SET_COLOR(str, j, i, PERL_STYLE_KEYWORD);
        } else {
            i = n;
            SET_COLOR(str, j, i, PERL_STYLE_COMMENT);
        }
    }

    while (i < n) {
        j = i + 1;
        c1 = str[j];
        switch (c = str[i]) {
        case '$':
            if (c1 == '^' && qe_isalpha(str[i + 2])) {
                j = i + 3;
                goto keyword;
            }
            if (c1 == '#' && qe_isalpha_(str[i + 2]))
                j++;
            else
            if (memchr("|%=-~^123456789&`'+_./\\,\"#$?*0[];!@", c1, 35)) {
                /* Special variable */
                j = i + 2;
                goto keyword;
            }
            fallthrough;
        case '*':
        case '@':       /* arrays */
        case '%':       /* associative arrays */
        case '&':
            if (j >= n)
                break;
            s1 = perl_var(str, j, n);
            if (s1 > j) {
                SET_COLOR(str, i, s1, PERL_STYLE_VAR);
                i = s1;
                continue;
            }
            break;
        case '-':
            if (c1 == '-') {
                i += 2;
                continue;
            }
            if (qe_isalpha(c1) && !qe_isalnum(str[i + 2])) {
                j = i + 2;
                goto keyword;
            }
            break;
        case '#':
            SET_COLOR(str, i, n, PERL_STYLE_COMMENT);
            i = n;
            continue;
        case '<':
            if (c1 == '<') {
                /* Should check for unary context */
                s1 = i + 2;
                while (qe_isblank(str[s1]))
                    s1++;
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
                i += 2;
                continue;
            }
            delim = '>';
            goto string;
        case '/':
        case '?':
            /* Should check for unary context */
            /* parse regex */
            s1 = perl_string(str, c, j, n);
            if (s1 >= n)
                break;
            SET_COLOR1(str, i, PERL_STYLE_DELIM);
            SET_COLOR(str, i + 1, s1, PERL_STYLE_REGEX);
            i = s1;
            while (++i < n && qe_isalpha(str[i]))
                continue;
            SET_COLOR(str, s1, i, PERL_STYLE_DELIM);
            continue;
        case '\'':
        case '`':
        case '"':
            delim = c;
        string:
            /* parse string const */
            s1 = perl_string(str, delim, j, n);
            if (s1 >= n) {
                if (c == '\'') {
                    SET_COLOR(str, i, n, PERL_STYLE_STRING);
                    i = n;
                    colstate |= IN_PERL_STRING1;
                    continue;
                }
                if (c == '\"') {
                    SET_COLOR(str, i, n, PERL_STYLE_STRING);
                    i = n;
                    colstate |= IN_PERL_STRING2;
                    continue;
                }
                /* ` string spanning more than one line treated as
                 * operator.
                 */
                break;
            }
            s1++;
            SET_COLOR(str, i, s1, PERL_STYLE_STRING);
            i = s1;
            continue;
        case '.':
            if (qe_isdigit(c1))
                goto number;
            break;

        default:
            if (qe_isdigit(c)) {
            number:
                j = perl_number(str, i, n);
                SET_COLOR(str, i, j, PERL_STYLE_NUMBER);
                i = j;
                continue;
            }
            if (!qe_isalpha_(c))
                break;

            j = perl_var(str, i, n);
            if (j == i)
                break;

            if (j >= n)
                goto keyword;

            /* Should check for context */
            if ((j == i + 1 && (c == 'm' || c == 'q'))
            ||  (j == i + 2 && c == 'q' && (c1 == 'q' || c1 == 'x'))) {
                s1 = perl_string(str, str[j], j + 1, n);
                if (s1 >= n)
                    goto keyword;
                SET_COLOR(str, i, j + 1, PERL_STYLE_DELIM);
                SET_COLOR(str, j + 1, s1, PERL_STYLE_REGEX);
                i = s1;
                while (++i < n && qe_isalpha(str[i]))
                    continue;
                SET_COLOR(str, s1, i, PERL_STYLE_DELIM);
                continue;
            }
            /* Should check for context */
            if ((j == i + 1 && (c == 's' /* || c == 'y' */))
            ||  (j == i + 2 && c == 't' && c1 == 'r')) {
                s1 = perl_string(str, str[j], j + 1, n);
                if (s1 >= n)
                    goto keyword;
                s2 = perl_string(str, str[j], s1 + 1, n);
                if (s2 >= n)
                    goto keyword;
                SET_COLOR(str, i, j + 1, PERL_STYLE_DELIM);
                SET_COLOR(str, j + 1, s1, PERL_STYLE_REGEX);
                SET_COLOR1(str, s1, PERL_STYLE_DELIM);
                SET_COLOR(str, s1 + 1, s2, PERL_STYLE_REGEX);
                i = s2;
                while (++i < n && qe_isalpha(str[i]))
                    continue;
                SET_COLOR(str, s2, i, PERL_STYLE_DELIM);
                continue;
            }
        keyword:
            if (j - i == 6 && ustristart(str + i, "format", NULL)) {
                for (s1 = 0; s1 < i; s1++) {
                    if (!qe_isblank(str[s1]))
                        break;
                }
                if (s1 == i) {
                    /* keyword is first on line */
                    colstate |= IN_PERL_FORMAT;
                }
            }
            SET_COLOR(str, i, j, PERL_STYLE_KEYWORD);
            i = j;
            continue;
        }
        i++;
        continue;
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
};

static int perl_init(void)
{
    qe_register_mode(&perl_mode, MODEF_SYNTAX);

    return 0;
}

qe_module_init(perl_init);
