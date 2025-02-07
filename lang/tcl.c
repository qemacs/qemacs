/*
 * TCL language mode for QEmacs.
 *
 * Copyright (c) 2025 Charlie Gordon.
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

/*---------------- Tool Command Language coloring ----------------*/

static char const tcl_keywords[] = {
    "package|require|if|else|elseif|namespace|proc|return|variable|"
    "eval|export|for|format|foreach|provide|self|method|constructor|"
    "try|catch|finally|throw|ne|eq|on|error|while|switch|default|global|"
    "array|dict|string|regexp|regsub|file|list|concat|append|split|"
    "lsort|llength|lrange|lappend|lindex|lassign|lsearch|lseq|lset|"
    "lreplace|lmap|lreverse|"
    "incr|decr|exec|break|continue|parray|exit|expr|set|"
    "unset|join|after|trace|debug|interp|auto_load|"
    "uplevel|upvar|clock|"
    "rename|close|cd|info|source|filter|open|chan|glob|read|write|"
    "fconfigure|subst|gets|puts|cat|"
};

static char const tcl_operators[] = {
    "eq|ne|lt|le|gt|ge|in|ni|"
};

static char const tcl_types[] = {
    "|"
};

enum {
    IN_TCL_CONTINUATION = 1,
    IN_TCL_STRING1 = 2,
    IN_TCL_STRING2 = 4,
    IN_TCL_COMMENT = 8,
    IN_TCL_DB = 0x80,
};

enum {
    TCL_STYLE_TEXT =       QE_STYLE_DEFAULT,
    TCL_STYLE_KEYWORD =    QE_STYLE_KEYWORD,
    TCL_STYLE_TYPE =       QE_STYLE_TYPE,
    TCL_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    TCL_STYLE_COMMENT =    QE_STYLE_COMMENT,
    TCL_STYLE_STRING =     QE_STYLE_STRING,
    TCL_STYLE_IDENTIFIER = QE_STYLE_DEFAULT,
    TCL_STYLE_NUMBER =     QE_STYLE_NUMBER,
    TCL_STYLE_FUNCTION =   QE_STYLE_FUNCTION,
    TCL_STYLE_VARIABLE =   QE_STYLE_VARIABLE,
};

static void tcl_colorize_line(QEColorizeContext *cp,
                              const char32_t *str, int n,
                              QETermStyle *sbuf, ModeDef *syn)
{
    char kbuf[16];
    int i = 0, start = i, k, style = 0, indent = -1, expr = -1, atclose = 0, klen;
    int colstate = cp->colorize_state;
    char32_t c;

dispatch_colstate:
    if (colstate) {
        int state = colstate;
        colstate = 0;
        if (state & IN_TCL_DB) {
            start = i;
            while (i < n && str[i] != '}')
                i++;
            cp->colorize_state = state & ~IN_TCL_DB;
            cp_colorize_line(cp, str, start, i, sbuf, &sql_mode);
            if (i == n)
                colstate = cp->colorize_state | IN_TCL_DB;
        } else
        if (state & IN_TCL_STRING1) {
            c = '\'';
            goto in_string;
        } else
        if (state & IN_TCL_STRING2) {
            c = '\"';
            goto in_string;
        } else
        if (state & IN_TCL_COMMENT) {
            goto in_comment;
        }
        if (state & IN_TCL_CONTINUATION) {
        }
    } else {
        while (i < n && qe_isspace(str[i]))
            i++;
        indent = expr = i;
    }
    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case ' ':
        case '\t':
            continue;
        case ';':
            while (i < n && qe_isspace(str[i]))
                i++;
            indent = expr = i;
            continue;
        case '#':
        in_comment:
            i = n;
            style = TCL_STYLE_COMMENT;
            if (str[i - 1] == '\\')
                colstate |= IN_TCL_COMMENT;
            break;
        //case '\'':
        case '\"':
            /* parse string */
        in_string:
            while (i < n) {
                char32_t c1 = str[i++];
                if (c1 == '\\') {
                    if (i == n) {
                        colstate |= (c == '\'') ? IN_TCL_STRING1 : IN_TCL_STRING2;
                        break;
                    }
                    i++;
                } else
                if (c1 == c)
                    break;
            }
            style = TCL_STYLE_STRING;
            break;
        case '[':
            atclose = 0;
            indent = i;
            continue;
        case '{':
            atclose = 0;
            expr = i;
            continue;
        case ']':
            atclose = 0;
            expr = -1;
            continue;
        case '}':
            atclose = 1;
            expr = -1;
            continue;
        case '$':
            if (str[i] == '{') {
                while (i < n && str[i++] != '}')
                    continue;
                style = TCL_STYLE_VARIABLE;
                break;
            }
            if (qe_isalpha_(str[i])) {
                i += ustr_get_identifier(kbuf, countof(kbuf), c, str, i, n);
                style = TCL_STYLE_VARIABLE;
                break;
            }
            continue;
        case '\\':
            if (i == n)
                colstate |= IN_TCL_CONTINUATION;
            c = str[i++];
            k = 0;
            if (c == 'u')
                k = 4;
            if (c == 'x')
                k = 2;
            while (k-- > 0 && qe_isxdigit(str[i]))
                i++;
            style = TCL_STYLE_PREPROCESS;
            break;
        case ':':
            if (str[i] == ':') {
                i++;
                goto has_word;
            }
            continue;
        default:
            /* parse numbers */
            if (qe_isdigit(c)) {
                for (; qe_isdigit_(str[i]); i++)
                    continue;
                if (str[i] == '.') {
                    i++;
                    for (; qe_isdigit_(str[i]); i++)
                        continue;
                }
                if (str[i] == 'e' || str[i] == 'E') {
                    k = i + 1;
                    if (str[k] == '+' || str[k] == '-')
                        k++;
                    if (qe_isdigit(str[k])) {
                        for (i = k + 1; qe_isdigit_(str[i]); i++)
                            continue;
                    }
                }
                if (qe_isalpha_(str[i]))
                    goto has_word;
                style = TCL_STYLE_NUMBER;
                break;
            }
            /* parse identifiers and keywords */
            if (qe_isalpha_(c)) {
            has_word:
                i += ustr_get_identifier(kbuf, countof(kbuf), c, str, i, n);
                if (str[i] == ':' && str[i + 1] == ':') {
                    c = ':';
                    i += 2;
                    goto has_word;
                }
                if (atclose) {
                    if (strfind("else|elseif|default|trap|on|finally", kbuf)) {
                        style = TCL_STYLE_KEYWORD;
                        break;
                    }
                } else
                if (start == indent) {
                    if (!strcmp("db", kbuf)
                    &&  ustr_match_keyword(str + i, " eval {", &klen)) {
                        i += klen;
                        SET_STYLE(sbuf, start, i - 1, TCL_STYLE_KEYWORD);
                        colstate = IN_TCL_DB;
                        goto dispatch_colstate;
                    } else
                    if (strfind(syn->keywords, kbuf)) {
                        // XXX: handle keyword sequences: namespace xxx...
                        style = TCL_STYLE_KEYWORD;
                    } else {
                        style = TCL_STYLE_FUNCTION;
                    }
                } else
                if (expr >= 0) {
                    if (start == expr)
                        style = TCL_STYLE_FUNCTION;
                    else
                    if (strfind(tcl_operators, kbuf))
                        style = TCL_STYLE_KEYWORD;
                } else {
                    style = TCL_STYLE_IDENTIFIER;
                }
                break;
            }
            continue;
        }
        atclose = 0;
        if (style) {
            SET_STYLE(sbuf, start, i, style);
            style = 0;
        }
    }
    cp->colorize_state = colstate;
}

static int tcl_mode_probe(ModeDef *mode, ModeProbeData *p) {
    if (match_extension(p->filename, mode->extensions)
    ||  strstart(p->filename, "tclIndex", NULL))
        return 85;

    if (strstr(cs8(p->buf), "package require Tk")
    ||  strstart(cs8(p->buf), "# created by tools/loadICU.tcl", NULL)
    ||  strstart(cs8(p->buf), "# created by tools/tclZIC.tcl", NULL)
    ||  strstart(cs8(p->buf), "# -*- tcl -*-", NULL)
    ||  strstart(cs8(p->buf), "#!/usr/bin/tclsh", NULL)
    ||  strstart(cs8(p->buf), "#!/usr/bin/env tclsh", NULL))
        return 85;

    return 1;
}

static ModeDef tcl_mode = {
    .name = "Tcl",
    .extensions = "tcl",
    .keywords = tcl_keywords,
    .types = tcl_types,
    .colorize_func = tcl_colorize_line,
    .mode_probe = tcl_mode_probe,
};

static int tcl_init(QEmacsState *qs) {
    qe_register_mode(qs, &tcl_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(tcl_init);
