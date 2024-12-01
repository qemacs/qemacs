/*
 * Nanorc definition syntax for QEmacs.
 *
 * Copyright (c) 2024 Charlie Gordon.
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

/*---- Colorize nanorc syntax files ----*/

static const char nanorc_keywords[] = {
    "extendsyntax|syntax|header|magic|set|unset|bind|unbind|include|"
    "color|icolor|comment|tabgives|linter|formatter|start|end|"
};

static const char nanorc_options[] = {
    "boldtext|brackets|breaklonglines|casesensitive|constantshow|fill|"
    "historylog|linenumbers|magic|mouse|multibuffer|nohelp|nonewlines|"
    "nowrap|operatingdir|positionlog|preserve|punct|quotestr|quickblank|"
    "rawsequences|rebinddelete|regexp|saveonexit|speller|afterends|"
    "allow_insecure_backup|atblanks|autoindent|backup|backupdir|bookstyle|"
    "colonparsing|cutfromcursor|emptyline|guidestripe|indicator|"
    "jumpyscrolling|locking|matchbrackets|minibar|noconvert|showcursor|"
    "smarthome|softwrap|stateflags|tabsize|tabstospaces|trimblanks|"
    "unix|whitespace|wordbounds|wordchars|zap|zero|titlecolor|numbercolor|"
    "stripecolor|scrollercolor|selectedcolor|spotlightcolor|minicolor|"
    "promptcolor|statuscolor|errorcolor|keycolor|functioncolor|"
};

static const char nanorc_commands[] = {
    "cancel|help|exit|discardbuffer|writeout|savefile|"
    "insert|whereis|wherewas|findprevious|findnext|replace|"
    "cut|copy|paste|execute|cutrestoffile|zap|mark|tospell|"
    "speller|linter|formatter|location|gotoline|justify|fulljustify|"
    "beginpara|endpara|comment|complete|indent|unindent|chopwordleft|"
    "chopwordright|findbracket|wordcount|recordmacro|runmacro|"
    "anchor|prevanchor|nextanchor|undo|redo|suspend|left|back|"
    "right|forward|up|prevline|down|nextline|scrollup|scrolldown|"
    "prevword|nextword|home|end|prevblock|nextblock|toprow|bottomrow|"
    "center|cycle|pageup|prevpage|pagedown|nextpage|firstline|lastline|"
    "prevbuf|nextbuf|verbatim|tab|enter|delete|backspace|refresh|"
    "casesens|regexp|backwards|flipreplace|flipgoto|older|newer|"
    "dosformat|macformat|append|prepend|backup|flipexecute|flippipe|"
    "flipconvert|flipnewbuffer|tofiles|browser|gotodir|firstfile|"
    "lastfile|nohelp|zero|constantshow|softwrap|linenumbers|whitespacedisplay|"
    "nosyntax|smarthome|autoindent|cutfromcursor|breaklonglines|tabstospaces|"
    "mouse|"
};

static const char nanorc_colors[] = {
    "red|green|blue|yellow|cyan|magenta|white|black|"
    "normal|pink|purple|mauve|lagoon|mint|lime|peach|"
    "orange|latte|rosy|beet|plum|sea|sky|slate|teal|"
    "sage|brown|ocher|sand|tawny|brick|crimson|grey|gray|"
    "bold|italic|"
};

enum {
    NANORC_STYLE_TEXT =     QE_STYLE_DEFAULT,
    NANORC_STYLE_COMMENT =  QE_STYLE_COMMENT,
    NANORC_STYLE_STRING =   QE_STYLE_STRING,
    NANORC_STYLE_NUMBER =   QE_STYLE_NUMBER,
    NANORC_STYLE_KEYWORD =  QE_STYLE_KEYWORD,
    NANORC_STYLE_OPTION =   QE_STYLE_TYPE,
    NANORC_STYLE_COLOR =    QE_STYLE_TYPE,
    NANORC_STYLE_COMMAND  = QE_STYLE_FUNCTION,
};

static void nanorc_colorize_line(QEColorizeContext *cp,
                                 const char32_t *str, int n,
                                 QETermStyle *sbuf, ModeDef *syn)
{
    int i = 0, start = i, style = 0;
    char32_t c;
    enum { NANORC_NONE = 0, NANORC_SET, NANORC_BIND, NANORC_COLOR, NANORC_OTHER } cmd = 0;
    char kbuf[64];

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            if (!cmd) {
                i = n;
                style = NANORC_STYLE_COMMENT;
                break;
            }
            while (qe_isxdigit(str[i]))
                i++;
            style = NANORC_STYLE_NUMBER;
            break;

        case '"':
            /* quoted string literals; the parser is simplistic,
               a string ends on a double quote followed by a space */
            while (i < n) {
                c = str[i++];
                if (c == '"' && (i == n || qe_isblank(str[i]))) {
                    break;
                }
            }
            style = NANORC_STYLE_STRING;
            break;

        default:
            if (qe_isdigit(c)) {
                /* decimal numbers */
                while (qe_isdigit(str[i]))
                    i++;
                style = NANORC_STYLE_NUMBER;
                break;
            }
            if (qe_isalpha_(c)) {
                i += ustr_get_identifier(kbuf, countof(kbuf), c, str, i, n);
                if (!cmd) {
                    if (strequal(kbuf, "set"))
                        cmd = NANORC_SET;
                    else
                    if (strequal(kbuf, "bind"))
                        cmd = NANORC_BIND;
                    else
                    if (strstr(kbuf, "color"))
                        cmd = NANORC_COLOR;
                    else
                        cmd = NANORC_OTHER;
                }
                if (strfind(syn->keywords, kbuf)) {
                    style = NANORC_STYLE_KEYWORD;
                    break;
                }
                if (cmd == NANORC_BIND) {
                    if (strfind(nanorc_commands, kbuf)) {
                        style = NANORC_STYLE_COMMAND;
                        break;
                    }
                }
                if (cmd == NANORC_SET) {
                    if (strstr(kbuf, "color"))
                        cmd = NANORC_COLOR;
                    if (strfind(nanorc_options, kbuf)) {
                        style = NANORC_STYLE_OPTION;
                        break;
                    }
                }
                if (cmd == NANORC_COLOR) {
                    const char *p = kbuf;
                    strstart(p, "bright", &p);
                    strstart(p, "light", &p);
                    if (strfind(nanorc_colors, p)) {
                        style = NANORC_STYLE_COLOR;
                        break;
                    }
                }
                continue;
            }
            continue;
        }
        if (style) {
            SET_STYLE(sbuf, start, i, style);
            style = 0;
        }
    }
}

static ModeDef nanorc_mode = {
    .name = "NanoRC",
    .extensions = "nanorc",
    .colorize_func = nanorc_colorize_line,
    .keywords = nanorc_keywords,
};

static int nanorc_init(QEmacsState *qs)
{
    qe_register_mode(qs, &nanorc_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(nanorc_init);
