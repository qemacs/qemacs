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
    "after|append|apply|array|auto_execok|auto_import|auto_load|"
    "auto_mkindex|auto_mkindex_oldfilename|auto_qualify|auto_reset|"
    "bgerror|binary|break|catch|cd|chan|clock|close|concat|const|continue|"
    "coroinject|coroprobe|coroutine|"
    "dde|dict|encoding|eof|epoll|error|eval|exec|exit|expr|"
    "fblocked|fconfigure|fcopy|file|fileevent|finally|flush|for|foreach|"
    "format|gets|glob|global|history|http|if|incr|info|interp|join|kqueue|"
    "lappend|lassign|ledit|lindex|linsert|list|llength|lmap|load|lpop|"
    "lrange|lremove|lrepeat|lreplace|lreverse|lsearch|lseq|lset|lsort|"
    "mathfunc|mathop|memory|msgcat|namespace|open|package|parray|pid|"
    "pkg::create|pkg_mkIndex|platform|platform::shell|"
    "proc|puts|pwd|re_syntax|read|refchan|regexp|registry|regsub|"
    "rename|return|scan|seek|select|set|socket|source|split|string|subst|"
    "switch|tailcall|tcltest|tclvars|tell|throw|time|tm|trace|trap|try|"
    "unknown|unload|unset|update|uplevel|upvar|variable|vwait|while|"
    "yield|yieldto|zlib|"
};

static char const tcl_operators[] = {
    "eq|ne|lt|le|gt|ge|in|ni|"
};

enum {
    IN_TCL_CONTINUATION = 1,
    IN_TCL_STRING = 2,
    IN_TCL_COMMENT = 4,
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

static int tcl_get_word(char *dest, int size, char32_t c,
                        const char32_t *str, int i, int n,
                        const char *stop)
{
    /* @API utils
       Extract a Tcl word from a wide string into a char array.
       @argument `dest` a valid pointer to a destination array.
       @argument `size` the length of the destination array.
       @argument `c` the first codepoint to copy.
       @argument `str` a valid wide string pointer.
       @argument `i` the offset of the first codepoint to copy.
       @argument `n` the offset to the end of the wide string.
       @return the number of elements consumed from the source string.
       @note: the return value can be larger than the destination array length.
       In this case, the destination array contains a truncated string, null
       terminated unless `size <= 0`.
       @note: non ASCII code-points are copied as FF bytes
     */
    int pos = 0, j;

    for (j = i;; j++) {
        if (pos + 1 < size) {
            /* c is assumed to be an ASCII character */
            dest[pos++] = c < 0x80 ? c : 0xFF;
        }
        if (j >= n)
            break;
        c = str[j];
        if (c < 0x80 && strchr(stop, c))
            break;
    }
    if (pos < size) {
        dest[pos] = '\0';
    }
    return j - i;
}

static void tcl_colorize_line(QEColorizeContext *cp,
                              const char32_t *str, int n,
                              QETermStyle *sbuf, ModeDef *syn)
{
    char kbuf[64];
    int i = 0, start = i, k, style = 0, indent = -1, expr = -1, atclose = 0, klen;
    int colstate = cp->colorize_state;
    char32_t c, c1;

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
        if (state & IN_TCL_STRING) {
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
            if (start != indent)
                goto regular_word;
        in_comment:
            i = n;
            style = TCL_STYLE_COMMENT;
            if (str[i - 1] == '\\')
                colstate |= IN_TCL_COMMENT;
            break;
        case '\"':
            /* parse string */
        in_string:
            style = TCL_STYLE_STRING;
            colstate |= IN_TCL_STRING;
            while (i < n) {
                c1 = str[i++];
                if (c1 == '\\') {
                    if (i < n)
                        i++;
                    continue;
                }
                if (c1 == c) {
                    colstate &= ~IN_TCL_STRING;
                    break;
                }
                if (c1 == '$' && i < n) {
                    SET_STYLE(sbuf, start, i - 1, TCL_STYLE_STRING);
                    start = i - 1;
                    c1 = str[i++];
                    if (c1 == '{') {  // parse ${xxx}
                        while (i < n) {
                            c1 = str[i++];
                            if (c1 == '\\') {
                                if (i < n)
                                    i++;
                                continue;
                            }
                            if (c1 == '}') {
                                break;
                            }
                        }
                    } else
                    if (qe_isalnum_(c1) || c1 == ':') {
                        i += tcl_get_word(kbuf, countof(kbuf), c1, str, i, n, " \t$[]{}\"\\");
                    } else {
                        i--;
                        continue;
                    }
                    SET_STYLE(sbuf, start, i, TCL_STYLE_VARIABLE);
                    start = i;
                    continue;
                }
            }
            break;
        case '(':
        case ')':
            continue;
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
            if (i == n)
                continue;
            c1 = str[i];
            if (c1 == '{') {
                while (i < n) {
                    c1 = str[i++];
                    if (c1 == '\\') {
                        if (i < n)
                            i++;
                        continue;
                    }
                    if (c1 == '}') {
                        break;
                    }
                }
                style = TCL_STYLE_VARIABLE;
                break;
            }
            if (qe_isalnum_(c1) || c1 == ':') {
                i += tcl_get_word(kbuf, countof(kbuf), c1, str, i, n, " \t(){}[];$\\");
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
            SET_STYLE(sbuf, start, i, TCL_STYLE_PREPROCESS);
            if (i < n && str[i] != '\\' && !qe_isspace(str[i]))
                goto regular_word;
            continue;
        default:
        regular_word:
            i += tcl_get_word(kbuf, countof(kbuf), c, str, i, n, " \t)}[];$\\");
            /* check for numbers */
            if (qe_isdigit(c)) {
                u8 *p = (u8*)kbuf + 1;
                while (qe_isdigit_(*p))
                    p++;
                if (*p == '.') {
                    p++;
                    while (qe_isdigit_(*p))
                        p++;
                }
                if (*p == 'e' || *p == 'E') {
                    k = 1;
                    if (p[k] == '+' || p[k] == '-')
                        k++;
                    if (qe_isdigit(p[k])) {
                        p += k + 1;
                        while (qe_isdigit_(*p))
                            p++;
                    }
                }
                if (*p == '\0') {
                    style = TCL_STYLE_NUMBER;
                    break;
                }
            }
            /* check for predefined commands */
            // XXX: should use a state machine
            if (qe_isalpha(c)) {
                if (atclose) {
                    if (strfind("else|elseif|default|trap|on|finally", kbuf)) {
                        style = TCL_STYLE_KEYWORD;
                        break;
                    }
                } else
                if (start == indent) {
                    if (!strcmp("db", kbuf)
                    &&  ustr_match_str(str + i, " eval {", &klen)) {
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
            break;
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
    ||  strstart(cs8(p->buf), "#!/usr/bin/tclsh", NULL)
    ||  strstart(cs8(p->buf), "#!/usr/bin/env tclsh", NULL)
    ||  strstart(cs8(p->buf), "# created by tools/loadICU.tcl", NULL)
    ||  strstart(cs8(p->buf), "# created by tools/tclZIC.tcl", NULL)
    ||  strstr(cs8(p->buf), "# -*- tcl -*-")
    ||  strstr(cs8(p->buf), "# vim:se syntax=tcl:"))
        return 85;

    return 1;
}

static ModeDef tcl_mode = {
    .name = "Tcl",
    .extensions = "tcl",
    .keywords = tcl_keywords,
    .colorize_func = tcl_colorize_line,
    .mode_probe = tcl_mode_probe,
};

static int tcl_init(QEmacsState *qs) {
    qe_register_mode(qs, &tcl_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(tcl_init);
