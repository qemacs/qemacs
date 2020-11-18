/*
 * Basic language modes for QEmacs.
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

/*---------------- Basic/Visual Basic coloring ----------------*/

static char const basic_keywords[] = {
    "addhandler|addressof|alias|and|andalso|ansi|as|assembly|"
    "auto|byref|byval|call|case|catch|class|const|"
    "declare|default|delegate|dim|directcast|do|"
    "each|else|elseif|end|enum|erase|error|"
    "event|exit|false|finally|for|friend|function|get|"
    "gettype|gosub|goto|handles|if|implements|imports|in|"
    "inherits|interface|is|let|lib|like|"
    "loop|me|mod|module|mustinherit|mustoverride|mybase|myclass|"
    "namespace|new|next|not|nothing|notinheritable|notoverridable|"
    "on|option|optional|or|orelse|overloads|overridable|overrides|"
    "paramarray|preserve|private|property|protected|public|raiseevent|readonly|"
    "redim|rem|removehandler|resume|return|select|set|shadows|"
    "shared|static|step|stop|structure|"
    "sub|synclock|then|throw|to|true|try|typeof|"
    "unicode|until|when|while|with|withevents|writeonly|xor|"
};

static char const basic_types[] = {
    "boolean|byte|char|cbool|"
    "cbyte|cchar|cdate|cdec|cdbl|cint|clng|cobj|cshort|csng|cstr|ctype|"
    "date|decimal|double|integer|long|object|short|single|string|variant|"
};

enum {
    BASIC_STYLE_TEXT =        QE_STYLE_DEFAULT,
    BASIC_STYLE_COMMENT =     QE_STYLE_COMMENT,
    BASIC_STYLE_STRING =      QE_STYLE_STRING,
    BASIC_STYLE_KEYWORD =     QE_STYLE_KEYWORD,
    BASIC_STYLE_TYPE =        QE_STYLE_TYPE,
    BASIC_STYLE_PREPROCESS =  QE_STYLE_PREPROCESS,
    BASIC_STYLE_IDENTIFIER =  QE_STYLE_VARIABLE,
};

static void basic_colorize_line(QEColorizeContext *cp,
                                unsigned int *str, int n, ModeDef *syn)
{
    char kbuf[16];
    int i = 0, start, c, style;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '\'':
            style = BASIC_STYLE_COMMENT;
            if (str[i] == '$')
                style = BASIC_STYLE_PREPROCESS;
            i = n;
            SET_COLOR(str, start, i, style);
            continue;
        case '\"':
            /* parse string const */
            while (i < n) {
                if (str[i++] == (unsigned int)c)
                    break;
            }
            SET_COLOR(str, start, i, BASIC_STYLE_STRING);
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
            SET_COLOR(str, start, i, BASIC_STYLE_IDENTIFIER);
            continue;
        }
        /* parse identifiers and keywords */
        if (qe_isalpha_(c)) {
            i += ustr_get_identifier_lc(kbuf, countof(kbuf), c, str, i, n);
            if (i < n && qe_findchar("$&!@%#", str[i]))
                i++;

            if (strfind(syn->keywords, kbuf)) {
                SET_COLOR(str, start, i, BASIC_STYLE_KEYWORD);
                continue;
            }
            if (strfind(syn->types, kbuf)) {
                SET_COLOR(str, start, i, BASIC_STYLE_TYPE);
                continue;
            }
            SET_COLOR(str, start, i, BASIC_STYLE_IDENTIFIER);
            continue;
        }
    }
}

static ModeDef basic_mode = {
    .name = "Basic",
    .extensions = "bas|frm|mst|vb|vbs|cls",
    .keywords = basic_keywords,
    .types = basic_types,
    .colorize_func = basic_colorize_line,
};

static int basic_init(void)
{
    qe_register_mode(&basic_mode, MODEF_SYNTAX);

    return 0;
}

qe_module_init(basic_init);
