/*
 * Basic language modes for QEmacs.
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
                                const char32_t *str, int n,
                                QETermStyle *sbuf, ModeDef *syn)
{
    char kbuf[16];
    int i = 0, start, style;
    char32_t c;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '\'':
            style = BASIC_STYLE_COMMENT;
            if (str[i] == '$')
                style = BASIC_STYLE_PREPROCESS;
            i = n;
            SET_STYLE(sbuf, start, i, style);
            continue;
        case '\"':
            /* parse string const */
            while (i < n) {
                if (str[i++] == c)
                    break;
            }
            SET_STYLE(sbuf, start, i, BASIC_STYLE_STRING);
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
            SET_STYLE(sbuf, start, i, BASIC_STYLE_IDENTIFIER);
            continue;
        }
        /* parse identifiers and keywords */
        if (qe_isalpha_(c)) {
            i += ustr_get_identifier_lc(kbuf, countof(kbuf), c, str, i, n);
            if (i < n && qe_findchar("$&!@%#", str[i]))
                i++;

            if (strfind(syn->keywords, kbuf)) {
                SET_STYLE(sbuf, start, i, BASIC_STYLE_KEYWORD);
                continue;
            }
            if (strfind(syn->types, kbuf)) {
                SET_STYLE(sbuf, start, i, BASIC_STYLE_TYPE);
                continue;
            }
            SET_STYLE(sbuf, start, i, BASIC_STYLE_IDENTIFIER);
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

static int basic_init(QEmacsState *qs)
{
    qe_register_mode(qs, &basic_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(basic_init);
