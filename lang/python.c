/*
 * Python language mode for QEmacs.
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

/*---------------- Python coloring ----------------*/

static char const python_keywords[] = {
    "|False|None|True|and|as|assert|break|class|continue"
    "|def|del|elif|else|except|finally|for|from|global"
    "|if|import|in|is|lambda|nonlocal|not|or|pass|raise"
    "|return|try|while|with|yield"
    "|"
};

// XXX: should add RapydScript keywords:
//    new, undefined, this, to, til, get, set, super
// XXX: colorize annotations
// XXX: parse unicode identifiers

enum {
    IN_PYTHON_COMMENT      = 0x80,
    IN_PYTHON_STRING       = 0x40,
    IN_PYTHON_STRING2      = 0x20,
    IN_PYTHON_LONG_STRING  = 0x10,
    IN_PYTHON_LONG_STRING2 = 0x08,
    IN_PYTHON_RAW_STRING   = 0x04,
    IN_PYTHON_REGEX1       = 0x02,
};

enum {
    PYTHON_STYLE_TEXT =     QE_STYLE_DEFAULT,
    PYTHON_STYLE_COMMENT =  QE_STYLE_COMMENT,
    PYTHON_STYLE_STRING =   QE_STYLE_STRING,
    PYTHON_STYLE_NUMBER =   QE_STYLE_NUMBER,
    PYTHON_STYLE_KEYWORD =  QE_STYLE_KEYWORD,
    PYTHON_STYLE_FUNCTION = QE_STYLE_FUNCTION,
    PYTHON_STYLE_REGEX    = QE_STYLE_STRING,
};

enum {  // Python flavors
    PYTHON_PYTHON = 0,
    PYTHON_RAPYDSCRIPT,
    PYTHON_BAZEL,
};

static void python_colorize_line(QEColorizeContext *cp,
                                 unsigned int *str, int n, ModeDef *syn)
{
    int i = 0, start = i, c, style = 0, sep, i1, tag = 0;
    int mode_flags = syn->colorize_flags;
    int state = cp->colorize_state;
    char kbuf[64];

    if (state & IN_PYTHON_STRING) {
        sep = '\'';
        goto parse_string;
    }
    if (state & IN_PYTHON_STRING2) {
        sep = '\"';
        goto parse_string;
    }
    if (state & IN_PYTHON_LONG_STRING) {
        sep = '\'';
        goto parse_long_string;
    }
    if (state & IN_PYTHON_LONG_STRING2) {
        sep = '\"';
        goto parse_long_string;
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

        case '\'':
        case '\"':
            /* parse string constant */
            i--;
        has_quote:
            sep = str[i++];
            if (str[i] == (unsigned int)sep && str[i + 1] == (unsigned int)sep) {
                /* long string */
                state = (sep == '\"') ? IN_PYTHON_LONG_STRING2 :
                        IN_PYTHON_LONG_STRING;
                i += 2;
            parse_long_string:
                while (i < n) {
                    c = str[i++];
                    if (!(state & IN_PYTHON_RAW_STRING) && c == '\\') {
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
                state = (sep == '\"') ? IN_PYTHON_STRING2 : IN_PYTHON_STRING;
            parse_string:
                while (i < n) {
                    c = str[i++];
                    if (!(state & IN_PYTHON_RAW_STRING) && c == '\\') {
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
                int i1, prev = ' ';
                for (i1 = start; i1 > 0; ) {
                    prev = str[--i1] & CHAR_MASK;
                    if (!qe_isblank(prev))
                        break;
                }
                if (qe_findchar(" [({},;=<>!~^&|*/%?:", prev)
                 || (str[i1] >> STYLE_SHIFT) == PYTHON_STYLE_KEYWORD
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
                if (check_fcall(str, i)) {
                    style = PYTHON_STYLE_FUNCTION;
                    if (tag) {
                        /* tag function definition */
                        eb_add_property(cp->b, cp->offset + start,
                                        QE_PROP_TAG, qe_strdup(kbuf));
                        tag = 0;
                    }
                    break;
                }
                if (tag) {
                    for (i1 = i; i1 < n && qe_isblank(str[i1]); i1++)
                        continue;
                    if (qe_findchar(",=", str[i1])) {
                        /* tag variable definition */
                        eb_add_property(cp->b, cp->offset + start,
                                        QE_PROP_TAG, qe_strdup(kbuf));
                        /* XXX: should colorize variable definition */
                    }
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

static ModeDef python_mode = {
    .name = "Python",
    .extensions = "py|pyt",
    .shell_handlers = "python|python2.6|python2.7",
    .keywords = python_keywords,
    .colorize_func = python_colorize_line,
    .colorize_flags = PYTHON_PYTHON,
};

static ModeDef rapydscript_mode = {
    .name = "RapydScript",
    .extensions = "pyj",
    .shell_handlers = "rapydscript",
    .keywords = python_keywords,
    .colorize_func = python_colorize_line,
    .colorize_flags = PYTHON_RAPYDSCRIPT,
};

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
        s->indent_size = 2;
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

static int python_init(void)
{
    qe_register_mode(&python_mode, MODEF_SYNTAX);
    qe_register_mode(&rapydscript_mode, MODEF_SYNTAX);
    qe_register_mode(&bazel_mode, MODEF_SYNTAX);

    return 0;
}

qe_module_init(python_init);
