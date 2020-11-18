/*
 * Smalltalk language mode for QEmacs.
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

/*---------------- Smalltalk coloring ----------------*/

enum {
    IN_SMALLTALK_COMMENT = 0x01,
    IN_SMALLTALK_STRING  = 0x02,
};

enum {
    SMALLTALK_STYLE_TEXT =       QE_STYLE_DEFAULT,
    SMALLTALK_STYLE_COMMENT =    QE_STYLE_COMMENT,
    SMALLTALK_STYLE_STRING =     QE_STYLE_STRING,
    SMALLTALK_STYLE_CHARCONST =  QE_STYLE_STRING,
    SMALLTALK_STYLE_NUMBER =     QE_STYLE_NUMBER,
    SMALLTALK_STYLE_KEYWORD =    QE_STYLE_KEYWORD,
    SMALLTALK_STYLE_TYPE =       QE_STYLE_TYPE,
    SMALLTALK_STYLE_FUNCTION =   QE_STYLE_FUNCTION,
    SMALLTALK_STYLE_IDENTIFIER = QE_STYLE_DEFAULT,
};

static char const smalltalk_keywords[] = {
    "|super|self|new|true|false|nil"
    "|"
};

static char const smalltalk_types[] = {
    "|"
};

static void smalltalk_colorize_line(QEColorizeContext *cp,
                                    unsigned int *str, int n, ModeDef *syn)
{
    char keyword[16];
    int i = 0, start = i, c, style = 0, len;
    int state = cp->colorize_state;

    if (state & IN_SMALLTALK_COMMENT)
        goto parse_comment;
    if (state & IN_SMALLTALK_STRING)
        goto parse_string;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '\"':
            state = IN_SMALLTALK_COMMENT;
        parse_comment:
            for (; i < n; i++) {
                if (str[i] == (unsigned int)'\"') {
                    state = 0;
                    i++;
                    break;
                }
            }
            style = SMALLTALK_STYLE_COMMENT;
            break;

        case '\'':
            state = IN_SMALLTALK_STRING;
        parse_string:
            for (; i < n; i++) {
                if (str[i] == (unsigned int)'\'') {
                    state = 0;
                    i++;
                    break;
                }
            }
            style = SMALLTALK_STYLE_STRING;
            break;

        case '$':
            if (i < n) {
                i++;
                style = SMALLTALK_STYLE_CHARCONST;
            }
            break;

        default:
            if (qe_isalpha(c)) {
                /* parse identifiers and keywords */
                len = 0;
                keyword[len++] = c;
                /* should allow other chars: .+/\*~<>@%|&? */
                for (; i < n && qe_isalnum(str[i]); i++) {
                    if (len < countof(keyword) - 1)
                        keyword[len++] = str[i];
                }
                keyword[len] = '\0';
                if (strfind(syn->keywords, keyword))
                    style = SMALLTALK_STYLE_KEYWORD;
                else
                if (strfind(syn->types, keyword))
                    style = SMALLTALK_STYLE_TYPE;
                else
                    style = SMALLTALK_STYLE_IDENTIFIER;
                break;
            }
            if (qe_isdigit(c)) {
                /* parse numbers */
                int value = 0;

                while (qe_isdigit(str[i])) {
                    value = value * 10 + str[i++] - '0';
                }
                if (qe_findchar("rR", str[i]) && qe_inrange(value, 2, 36)) {
                    i++;
                    while (qe_digit_value(str[i]) < value)
                        i++;
                } else {
                    if (qe_isdigit(str[i]) == '.' && qe_isdigit(str[i+1])) {
                        i += 2;
                        while (qe_isdigit(str[i]))
                            i++;
                    }
                    if (qe_findchar("eE", str[i])) {
                        int j = i + 1;
                        if (qe_findchar("+-", str[j]))
                            j++;
                        if (qe_isdigit(str[j])) {
                            i = j + 1;
                            while (qe_isdigit(str[i]))
                                i++;
                        }
                    }
                }
                style = SMALLTALK_STYLE_NUMBER;
                break;
            }
            continue;
        }
        SET_COLOR(str, start, i, style);
        style = 0;
    }
    cp->colorize_state = state;
}

static int smalltalk_mode_probe(ModeDef *mode, ModeProbeData *pd)
{
    const char *p = cs8(pd->buf);

    if (match_extension(pd->filename, mode->extensions)) {
        if (*p == '"' || *p == '\'')
            return 80;
        else
            return 51;
    }

    while (qe_isspace(*p))
        p++;

    if (*p == '!') {
        while (*++p && p[1] != '\r' && p[1] != '\n')
            p++;
        if (*p == '!')
            return 60;
    }
    return 1;
}

static ModeDef smalltalk_mode = {
    .name = "Smalltalk",
    .extensions = "st|sts|sources|changes",
    .mode_probe = smalltalk_mode_probe,
    .keywords = smalltalk_keywords,
    .types = smalltalk_types,
    .colorize_func = smalltalk_colorize_line,
};

static int smalltalk_init(void)
{
    qe_register_mode(&smalltalk_mode, MODEF_SYNTAX);

    return 0;
}

qe_module_init(smalltalk_init);
