/*
 * OpenSCAD language mode for QEmacs.
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

/*---------------- OpenSCAD language ----------------*/

static const char scad_keywords[] = {
    "true|false|undef|"
    "module|function|for|if|else|len|"
//    "assert|break|case|const|continue|default|defined|do|exit|"
//    "forward|goto|native|new|operator|public|return|sizeof|sleep|"
//    "state|static|stock|switch|tagof|while|",
};

static const char scad_preprocessor_keywords[] = {
    "use|include|"
};

static const char scad_types[] = {
    ""
    //"void|"
    //"bool|string|int|uint|uchar|nt8|short|ushort|long|ulong|size_t|ssize_t|"
    //"double|va_list|unichar|"
};

enum {
    IN_SCAD_COMMENT  = 0x01,
};

enum {
    SCAD_STYLE_TEXT =       QE_STYLE_DEFAULT,
    SCAD_STYLE_KEYWORD =    QE_STYLE_KEYWORD,
    SCAD_STYLE_TYPE =       QE_STYLE_TYPE,
    SCAD_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    SCAD_STYLE_COMMENT =    QE_STYLE_COMMENT,
    SCAD_STYLE_STRING =     QE_STYLE_STRING,
    SCAD_STYLE_NUMBER =     QE_STYLE_NUMBER,
    SCAD_STYLE_FUNCTION =   QE_STYLE_FUNCTION,
    SCAD_STYLE_ARGNAME =    QE_STYLE_FUNCTION,
};

static void scad_colorize_line(QEColorizeContext *cp,
                               const char32_t *str, int n,
                               QETermStyle *sbuf, ModeDef *syn)
{
    char keyword[16];
    int i = 0, start = i, style = 0, k, len, laststyle = 0, isnum;
    char32_t c;
    int colstate = cp->colorize_state;
    int level = colstate >> 1;

    if (colstate & IN_SCAD_COMMENT)
        goto in_comment;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '/':
            if (str[i] == '/') {  /* single line comment */
                i = n;
                style = SCAD_STYLE_COMMENT;
                break;
            }
            if (str[i] == '*') {  /* multi-line comment */
                colstate |= IN_SCAD_COMMENT;
                i++;
            in_comment:
                for (; i < n; i++) {
                    if (str[i] == '*' && str[i + 1] == '/') {
                        i += 2;
                        colstate &= ~IN_SCAD_COMMENT;
                        break;
                    }
                }
                style = SCAD_STYLE_COMMENT;
                break;
            }
            continue;
        case '<':
            if (laststyle == SCAD_STYLE_PREPROCESS) {
                /* filename for include and use directives */
                while (i < n) {
                    if (str[i++] == '>')
                        break;
                }
                style = SCAD_STYLE_STRING;
                break;
            }
            continue;
        case '(':
        case '[':
        case '{':
            level <<= 1;
            continue;
        case '}':
        case ']':
        case ')':
            level >>= 1;
            continue;
        case '\'':
        case '\"':
            /* parse string or char const */
            while (i < n) {
                /* XXX: escape sequences? */
                if (str[i] == '\\' && i + 1 < n) {
                    i++;
                    continue;
                }
                if (str[i++] == c)
                    break;
            }
            style = SCAD_STYLE_STRING;
            break;
        default:
            /* parse identifiers, keywords and numbers */
            if (qe_isalnum_(c) || c == '$') {
                isnum = qe_isdigit(c);
                len = 0;
                keyword[len++] = c;
                for (; qe_isalnum_(str[i]) || str[i] == '.'; i++) {
                    if (str[i] == '.') {
                        if (!isnum)
                            break;
                    } else {
                    if (!qe_isdigit(str[i]))
                        isnum = 0;
                    }
                    if (len < countof(keyword) - 1)
                        keyword[len++] = str[i];
                }
                keyword[len] = '\0';
                if (isnum) {
                    style = SCAD_STYLE_NUMBER;
                }
                if (strfind(syn->keywords, keyword)) {
                    style = SCAD_STYLE_KEYWORD;
                } else
                if (strfind(scad_preprocessor_keywords, keyword)) {
                    style = SCAD_STYLE_PREPROCESS;
                } else
                if (strfind(syn->types, keyword)) {
                    style = SCAD_STYLE_TYPE;
                } else {
                    k = cp_skip_blanks(str, i, n);
                    if ((level & 2) && str[k] == '=') {
                        style = SCAD_STYLE_ARGNAME;
                    } else
                    if (str[k] == '(') {
                        style = SCAD_STYLE_FUNCTION;
                        level |= 1;
                    }
                }
                break;
            }
            continue;
        }
        if (style) {
            laststyle = style;
            SET_STYLE(sbuf, start, i, style);
            style = 0;
        }
    }
    cp->colorize_state = (colstate & IN_SCAD_COMMENT) | (level << 1);
}

static ModeDef scad_mode = {
    .name = "OpenSCAD",
    .extensions = "scad",
    .colorize_func = scad_colorize_line,
    .keywords = scad_keywords,
    .types = scad_types,
    //.indent_func = c_indent_line,
    //.auto_indent = 1,
};

static int scad_init(QEmacsState *qs)
{
    qe_register_mode(qs, &scad_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(scad_init);
