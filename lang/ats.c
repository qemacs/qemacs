/*
 * ATS (Applied Type System) mode for QEmacs.
 *
 * Copyright (c) 2016-2017 Charlie Gordon.
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

/*---------------- ATS (Applied Type System) coloring ----------------*/

static char const ats_keywords[] = {
    "|extern|symintr|overload|exception|staload|dynload"
    "|fun|prfun|fn|prfn|implement|fnx|castfn|praxi|val|prval"
    "|abstype|absprop|absview|absviewtype|absvtype"
    "|datatype|dataprop|dataview|dataviewtype|datavtype"
    "|stadef|sortdef|typedef|propdef|viewdef|viewtypedef|vtypedef"
    "|var|let|local|of|with|in|and|when|assume|macdef"
    "|if|then|else|for|fix|where|while|case|end|try"
    "|mod|true|false"
    "|infix|infixl|infixr|prefix|postfix|nonfix|op|lam|rec"
    "|"
};

static char const ats_types[] = {
    "|bool|int|double|void|string|type|prop|view|viewtype|vtype|ptr|ref|nat"
    "|"
};

/* XXX: should colorize $MACRO substitutions. */

enum {
    IN_ATS_COMMENT  = 0x0F,
    ATS_COMMENT_MAX_LEVEL = 0x0F,
    ATS_COMMENT_SHIFT = 0,
    IN_ATS_STRING   = 0x10,
    IN_ATS_CBLOCK   = 0x8000,
};

enum {
    ATS_STYLE_TEXT =       QE_STYLE_DEFAULT,
    ATS_STYLE_KEYWORD =    QE_STYLE_KEYWORD,
    ATS_STYLE_TYPE =       QE_STYLE_TYPE,
    ATS_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    ATS_STYLE_COMMENT =    QE_STYLE_COMMENT,
    ATS_STYLE_STRING =     QE_STYLE_STRING,
    ATS_STYLE_IDENTIFIER = QE_STYLE_DEFAULT,
    ATS_STYLE_NUMBER =     QE_STYLE_NUMBER,
    ATS_STYLE_FUNCTION =   QE_STYLE_FUNCTION,
};

static void ats_colorize_line(QEColorizeContext *cp,
                              unsigned int *str, int n, ModeDef *syn)
{
    char keyword[32];
    int i = 0, start = i, c, k, style = 0, len, level;
    int colstate = cp->colorize_state;

    if (colstate & IN_ATS_CBLOCK) {
        if (str[i] == '%' && str[i + 1] == '}') {
            colstate = 0;
            SET_COLOR(str, i, n, ATS_STYLE_PREPROCESS);
            i = n;
        } else {
            ModeDef *md = &c_mode;
            cp->colorize_state = colstate & ~IN_ATS_CBLOCK;
            md->colorize_func(cp, str + i, n - i, md);
            colstate = cp->colorize_state | IN_ATS_CBLOCK;
            i = n;
        }
    } else {
        level = (colstate & IN_ATS_COMMENT) >> ATS_COMMENT_SHIFT;
        if (level > 0)
            goto in_comment;
        if (colstate & IN_ATS_STRING)
            goto in_string;
    }

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '/':
            if (str[i] == '/') {    /* C++ comments, recent extension */
                i = n;
                style = ATS_STYLE_COMMENT;
                break;
            }
            continue;
        case '%':
            if (i == 1 && str[i] == '{') {
                colstate = IN_ATS_CBLOCK;
                i = n;
                style = ATS_STYLE_PREPROCESS;
                break;
            }
            continue;
        case '(':
            /* check for preprocessor */
            if (str[i] == '*') {
                /* regular comment (recursive?) */
                i++;
                level = 1;
            in_comment:
                while (i < n) {
                    c = str[i++];
                    if (c == '(' && str[i] == '*'
                    &&  level < ATS_COMMENT_MAX_LEVEL) {
                        i++;
                        level++;
                    } else
                    if (c == '*' && str[i] == ')') {
                        i++;
                        level--;
                        if (level <= 0)
                            break;
                    }
                }
                colstate &= ~(IN_ATS_COMMENT << ATS_COMMENT_SHIFT);
                colstate |= level << ATS_COMMENT_SHIFT;
                style = ATS_STYLE_COMMENT;
                break;
            }
            continue;
        case '"':
            /* parse string or char const */
        in_string:
            colstate &= ~IN_ATS_STRING;
            while (i < n) {
                c = str[i++];
                if (c == '"')
                    break;
                if (c == '\\') {
                    if (i == n) {
                        colstate |= IN_ATS_STRING;
                        break;
                    }
                    /* skip next character */
                    i++;
                }
            }
            style = ATS_STYLE_STRING;
            break;
        case '#':
            while (qe_isalpha(str[i]))
                i++;
            style = ATS_STYLE_PREPROCESS;
            break;
        case '~':
            if (qe_isdigit(str[i]))
                goto number;
            continue;
        default:
            /* parse numbers */
            if (qe_isdigit(c)) {
            number:
                for (; i < n; i++) {
                    if (!qe_isalnum(str[i]) && str[i] != '.')
                        break;
                }
                style = ATS_STYLE_NUMBER;
                break;
            }
            /* parse identifiers and keywords */
            if (qe_isalpha_(c) || c == '$') {
                len = 0;
                keyword[len++] = qe_tolower(c);
                for (; qe_isalnum_(str[i]); i++) {
                    if (len < countof(keyword) - 1)
                        keyword[len++] = qe_tolower(str[i]);
                }
                if (str[i] == '!') {
                    if (len < countof(keyword) - 1)
                        keyword[len++] = str[i];
                    i++;
                }
                keyword[len] = '\0';
                if (strfind(syn->keywords, keyword)) {
                    style = ATS_STYLE_KEYWORD;
                } else
                if (strfind(syn->types, keyword)) {
                    style = ATS_STYLE_TYPE;
                } else {
                    k = i;
                    if (qe_isblank(str[k]))
                        k++;
                    if (str[k] == '(' && str[k + 1] != '*')
                        style = ATS_STYLE_FUNCTION;
                    else
                        style = ATS_STYLE_IDENTIFIER;
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
    cp->colorize_state = colstate;
}

static ModeDef ats_mode = {
    .name = "ATS",
    .extensions = "dats|sats|hats", // dats for dynamic, sats for static files
    .keywords = ats_keywords,
    .types = ats_types,
    .colorize_func = ats_colorize_line,
};

static int ats_init(void)
{
    qe_register_mode(&ats_mode, MODEF_SYNTAX);

    return 0;
}

qe_module_init(ats_init);
