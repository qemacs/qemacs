/*
 * Miscellaneous language modes for QEmacs.
 *
 * Copyright (c) 2000-2017 Charlie Gordon.
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

#define MAX_KEYWORD_SIZE  16

/*---------------- x86 Assembly language coloring ----------------*/

static char const asm_prepkeywords1[] = {
    "|align|arg|assume|codeseg|const|dataseg|display|dosseg"
    "|else|elseif|elseif1|elseif2|elseifb|elseifdef|elseifdif"
    "|elseifdifi|elseife|elseifidn|elseifidni|elseifnb|elseifndef"
    "|emul|end|endif|endm|endp|err|errif|errif1|errif2"
    "|errifb|errifdef|errifdif|errifdifi|errife|errifidn"
    "|errifidni|errifnb|errifndef|even|evendata|exitm|fardata"
    "|ideal|if|if1|if2|ifb|ifdef|ifdif|ifdifi|ife|ifidn"
    "|ifidni|ifnb|ifndef|include|includelib|irp|irpc"
    "|jumps|largestack|local|locals|macro|masm|masm51|model|multerrs"
    "|noemul|nojumps|nolocals|nomasm51|nomulterrs|nosmart|nowarn"
    "|proc|purge|quirks|radix|record|rept"
    "|smart|smallstack|stack|startupcode|subttl|title"
    "|version|warn|while"
    "|"
};

static char const asm_prepkeywords2[] = {
    "|catstr|endp|ends|enum|equ|group"
    "|label|macro|proc|record|segment|struc"
    "|"
};

/* colstate is used to store the comment character */

enum {
    ASM_STYLE_TEXT =        QE_STYLE_DEFAULT,
    ASM_STYLE_PREPROCESS =  QE_STYLE_PREPROCESS,
    ASM_STYLE_COMMENT =     QE_STYLE_COMMENT,
    ASM_STYLE_STRING =      QE_STYLE_STRING,
    ASM_STYLE_NUMBER =      QE_STYLE_NUMBER,
    ASM_STYLE_IDENTIFIER =  QE_STYLE_VARIABLE,
};

static void asm_colorize_line(QEColorizeContext *cp,
                              unsigned int *str, int n, ModeDef *syn)
{
    char keyword[MAX_KEYWORD_SIZE];
    int i = 0, start = 0, c, w, len, wn = 0; /* word number on line */
    int colstate = cp->colorize_state;

    if (colstate) {
        /* skip characters upto and including separator */
    comment:
        for (start = i; i < n; i++) {
            if (str[i] == (char)colstate) {
                i++;
                colstate = 0;
                break;
            }
        }
        SET_COLOR(str, start, i, ASM_STYLE_COMMENT);
    }
    for (; i < n && qe_isblank(str[i]); i++)
        continue;

    for (w = i; i < n;) {
        start = i;
        c = str[i++];
        switch (c) {
        case '\\':
            if (str[i] == '}' || str[i] == '{')
                goto prep;
            break;
        case '}':
        prep:
            /* scan for comment */
            for (; i < n; i++) {
                if (str[i] == ';')
                    break;
            }
            SET_COLOR(str, start, i, ASM_STYLE_PREPROCESS);
            continue;
        case ';':
            i = n;
            SET_COLOR(str, start, i, ASM_STYLE_COMMENT);
            continue;
        case '\'':
        case '\"':
            /* parse string const */
            for (; i < n; i++) {
                if (str[i] == (unsigned int)c) {
                    i++;
                    break;
                }
            }
            SET_COLOR(str, start, i, ASM_STYLE_STRING);
            continue;
        default:
            break;
        }
        /* parse numbers */
        if (qe_isdigit(c)) {
            for (; qe_isalnum(str[i]); i++)
                continue;
            SET_COLOR(str, start, i, ASM_STYLE_NUMBER);
            continue;
        }
        /* parse identifiers and keywords */
        if (qe_isalpha_(c) || qe_findchar("@.$%?", c)) {
            len = 0;
            keyword[len++] = qe_tolower(c);
            for (; qe_isalnum_(str[i]) || qe_findchar("@$%?", str[i]); i++) {
                if (len < countof(keyword) - 1)
                    keyword[len++] = qe_tolower(str[i]);
            }
            keyword[len] = '\0';
            if (++wn == 1) {
                if (!strcmp(keyword, "comment") && n - i >= 2) {
                    for (w = i; qe_isblank(str[w]); w++)
                        continue;
                    colstate = str[w];  /* end of comment character */
                    SET_COLOR(str, start, w, ASM_STYLE_PREPROCESS);
                    i = w + 1;
                    goto comment;
                }
                if (strfind(asm_prepkeywords1, keyword))
                    goto prep;
            } else
            if (wn == 2) {
                if (strfind(asm_prepkeywords2, keyword)) {
                    SET_COLOR(str, start, i, ASM_STYLE_PREPROCESS);
                    continue;
                }
            }
            SET_COLOR(str, start, i, ASM_STYLE_IDENTIFIER);
            continue;
        }
    }
    cp->colorize_state = colstate;
}

static ModeDef asm_mode = {
    .name = "asm",
    .extensions = "asm|asi|cod",
    .colorize_func = asm_colorize_line,
};

static int asm_init(void)
{
    qe_register_mode(&asm_mode, MODEF_SYNTAX);

    return 0;
}

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
    char kbuf[MAX_KEYWORD_SIZE];
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

/*---------------- Vim/Visual Vim coloring ----------------*/

static char const vim_cmd_keywords[] = {
    /* Only a subset of commands are recognized */
    "|brea[k]|cal[l]|cat[ch]|command|con[tinue]|delc[ommand]"
    "|delf[unction]|el[se]|elsei[f]|end|endfo[r]|endfu[nction]|endi[f]"
    "|endt[ry]|endw[hile]|ex[ecute]|fina[lly]|fini[sh]|for"
    "|fun[ction]|if|hi[ghlight]|let|norm|pu[t]|redraws[tatus]|res[ize]"
    "|retu[rn]|ru[ntime]|se[t]|setl[ocal]|sil[ent]|syn|synt[ax]"
    "|try|unl[et]|ve[rsion]|wh[ile]|y[ank]"
    "|"
};

static char const vim_keywords[] = {
    "|self|in"
    "|"
};

static char const vim_syn_keywords[] = {
    "|case|ignore|match|keyword|include|cluster|region|sync|clear"
    "|nextgroup|contained|contains|display|oneline|start|end"
    "|skipwhite|keepend|excludenl|skipnl|skip|keepend|fromstart"
    "|minlines|maxlines|containedin|extend|transparent|fold"
    "|matchgroup|add|grouphere|groupthere|linebreaks"
    "|"
};

enum {
    VIM_STYLE_TEXT =        QE_STYLE_DEFAULT,
    VIM_STYLE_COMMENT =     QE_STYLE_COMMENT,
    VIM_STYLE_STRING =      QE_STYLE_STRING,
    VIM_STYLE_REGEX =       QE_STYLE_STRING,
    VIM_STYLE_NUMBER =      QE_STYLE_NUMBER,
    VIM_STYLE_KEYWORD =     QE_STYLE_KEYWORD,
    VIM_STYLE_IDENTIFIER =  QE_STYLE_DEFAULT,
    VIM_STYLE_FUNCTION =    QE_STYLE_FUNCTION,
};

enum {
    VIM_STATE_CMD,
    VIM_STATE_ARG,
    VIM_STATE_SYN,
};

static int is_vim_keyword(unsigned int *str, int from, int to,
                          const char *list)
{
    char keyword[MAX_KEYWORD_SIZE];
    const char *p;
    int c, i, len = to - from;

    if (len >= MAX_KEYWORD_SIZE)
        return 0;

    for (i = 0; i < len; i++) {
        c = str[from + i];
        if (c >= 0x80)
            return 0;
        keyword[i] = c;
    }
    keyword[len] = '\0';

    /* check for exact match or non ambiguous prefix */
    for (p = list; *p != '\0';) {
        for (i = 0;
             p[i] != '\0' && p[i] != ' ' && p[i] != '[' && p[i] != '|';
             i++) {
            continue;
        }
        if (i <= len && !memcmp(p, keyword, i)) {
            if (i == len)
                return 1;
            if (p[i] == '[' && !memcmp(p + i + 1, keyword + i, len - i))
                return 1;
        }
        for (p += i; *p != '\0' && (c = *p++) != ' ' && c != '|';)
            continue;
    }
    return 0;
}

static void vim_colorize_line(QEColorizeContext *cp,
                              unsigned int *str, int n, ModeDef *syn)
{
    int i = 0, j, start, c, state, comm, level, style;

    while (qe_isblank(str[i])) {
        i++;
    }
    if (str[i] == '\\') {
        i++;
        level = cp->colorize_state & 15;
        state = cp->colorize_state >> 4;
        comm = 0;
    } else {
        state = VIM_STATE_CMD;
        level = 0;
        comm = 1;
    }

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '\'':
            comm = 0;
            /* parse string const */
            while (i < n) {
                if (str[i++] == (unsigned int)c)
                    break;
            }
            SET_COLOR(str, start, i, VIM_STYLE_STRING);
            continue;
        case '/':
            if (state == VIM_STATE_SYN
            &&  (qe_isblank(str[i - 2]) || str[i - 2] == '=')) {
                /* parse regex */
                while (i < n) {
                    if (str[i] == '\\' && i + 1 < n) {
                        i += 2;
                    } else
                    if (str[i++] == (unsigned int)c)
                        break;
                }
                SET_COLOR(str, start, i, VIM_STYLE_REGEX);
                continue;
            }
            break;
        case '+':
            if (state == VIM_STATE_SYN
            &&  (qe_isblank(str[i - 2]) || str[i - 2] == '=')) {
                /* parse string const */
                for (j = i; j < n;) {
                    if (str[j++] == (unsigned int)c) {
                        i = j;
                        SET_COLOR(str, start, i, VIM_STYLE_STRING);
                        break;
                    }
                }
                continue;
            }
            break;
        case '\"':
            if (comm) {
                i = n;
                SET_COLOR(str, start, i, VIM_STYLE_COMMENT);
                continue;
            }
            /* parse string const */
            style = VIM_STYLE_COMMENT;
            while (i < n) {
                if (str[i] == '\\' && i + 1 < n) {
                    i += 2;
                } else
                if (str[i++] == (unsigned int)c) {
                    style = VIM_STYLE_STRING;
                    break;
                }
            }
            SET_COLOR(str, start, i, style);
            continue;
        case '|':
            if (str[i] == '|') {
                i++;
            } else {
                state = VIM_STATE_CMD;
                comm = 1;
            }
            continue;
        case '(':
            comm = 0;
            level++;
            continue;
        case ')':
            level--;
            if (!level)
                comm = 1;
            continue;
        case ' ':
        case '\t':
        case ',':
        case '$':
            continue;
        default:
            comm = 0;
            break;
        }
        /* parse numbers */
        if (qe_isdigit(c)) {
            for (; i < n; i++) {
                if (!qe_isalnum(str[i]) && str[i] != '.')
                    break;
            }
            SET_COLOR(str, start, i, VIM_STYLE_NUMBER);
            continue;
        }
        /* parse identifiers and keywords */
        if (qe_isalpha_(c)) {
            for (; i < n; i++) {
                if (!qe_isalnum_(str[i]) && str[i] != '#')
                    break;
            }
            style = VIM_STYLE_IDENTIFIER;

            if (state == VIM_STATE_CMD) {
                state = VIM_STATE_ARG;
                if (is_vim_keyword(str, start, i, vim_cmd_keywords)) {
                    if (is_vim_keyword(str, start, i, "syn[tax]")) {
                        state = VIM_STATE_SYN;
                    }
                    if (str[i] == '!')
                        i++;
                    style = VIM_STYLE_KEYWORD;
                }
            } else
            if (state == VIM_STATE_SYN) {
                if (is_vim_keyword(str, start, i, vim_syn_keywords)) {
                    style = VIM_STYLE_KEYWORD;
                }
            } else {
                if (is_vim_keyword(str, start, i, vim_keywords)) {
                    style = VIM_STYLE_KEYWORD;
                }
            }
            if (style == VIM_STYLE_IDENTIFIER) {
                if (check_fcall(str, i))
                    style = VIM_STYLE_FUNCTION;
            }
            SET_COLOR(str, start, i, style);
            continue;
        }
    }
    cp->colorize_state = (state << 4) | (level & 15);
}

static ModeDef vim_mode = {
    .name = "Vim",
    .extensions = "vim",
    .colorize_func = vim_colorize_line,
};

static int vim_init(void)
{
    qe_register_mode(&vim_mode, MODEF_SYNTAX);

    return 0;
}

/*---------------- Pascal/Turbo Pascal/Delphi coloring ----------------*/
/* Should do Delphi specific things */

static char const pascal_keywords[] = {
    "|absolute|and|array|asm|begin|case|comp|const|div|do|downto"
    "|else|end|extended|external|false|far|file|for|forward|function|goto"
    "|if|implementation|in|inline|interface|interrupt"
    "|label|mod|near|nil|not|of|or|overlay"
    "|packed|procedure|program|record|repeat"
    "|set|shl|shr|single|text|then|to|true|type"
    "|unit|until|uses|var|while|with|xor"
    "|"
};

static char const pascal_types[] = {
    "|boolean|byte|char|double|integer|longint|pointer|real|shortint"
    "|string|word"
    "|"
};

enum {
    IN_PASCAL_COMMENT  = 0x01,
    IN_PASCAL_COMMENT1 = 0x02,
    IN_PASCAL_COMMENT2 = 0x04,
};

enum {
    PASCAL_STYLE_TEXT =       QE_STYLE_DEFAULT,
    PASCAL_STYLE_KEYWORD =    QE_STYLE_KEYWORD,
    PASCAL_STYLE_TYPE =       QE_STYLE_TYPE,
    PASCAL_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    PASCAL_STYLE_COMMENT =    QE_STYLE_COMMENT,
    PASCAL_STYLE_STRING =     QE_STYLE_STRING,
    PASCAL_STYLE_IDENTIFIER = QE_STYLE_VARIABLE,
    PASCAL_STYLE_NUMBER =     QE_STYLE_NUMBER,
    PASCAL_STYLE_FUNCTION =   QE_STYLE_FUNCTION,
};

static void pascal_colorize_line(QEColorizeContext *cp,
                                 unsigned int *str, int n, ModeDef *syn)
{
    char kbuf[MAX_KEYWORD_SIZE];
    int i = 0, start = i, c, k, style = 0;
    int colstate = cp->colorize_state;

    if (colstate & IN_PASCAL_COMMENT)
        goto in_comment;

    if (colstate & IN_PASCAL_COMMENT1)
        goto in_comment1;

    if (colstate & IN_PASCAL_COMMENT2)
        goto in_comment2;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '/':
            if (str[i] == '/') {    /* C++ comments, recent extension */
                i = n;
                style = PASCAL_STYLE_COMMENT;
                break;
            }
            continue;
        case '{':
            /* check for preprocessor */
            if (str[i] == '$') {
                colstate = IN_PASCAL_COMMENT1;
                i++;
            in_comment1:
                style = PASCAL_STYLE_PREPROCESS;
            } else {
                /* regular comment (recursive?) */
                colstate = IN_PASCAL_COMMENT;
            in_comment:
                style = PASCAL_STYLE_COMMENT;
            }
            while (i < n) {
                if (str[i++] == '}') {
                    colstate = 0;
                    break;
                }
            }
            break;
        case '(':
            /* check for preprocessor */
            if (str[i] == '*') {
                /* regular comment (recursive?) */
                colstate = IN_PASCAL_COMMENT2;
                i++;
            in_comment2:
                while (i < n) {
                    if (str[i++] == '*' && str[i] == ')') {
                        i++;
                        colstate = 0;
                        break;
                    }
                }
                style = PASCAL_STYLE_COMMENT;
                break;
            }
            continue;
        case '\'':
            /* parse string or char const */
            while (i < n) {
                /* XXX: escape sequences? */
                if (str[i++] == (unsigned int)c)
                    break;
            }
            style = PASCAL_STYLE_STRING;
            break;
        case '#':
            /* parse hex char const */
            for (; i < n; i++) {
                if (!qe_isxdigit(str[i]))
                    break;
            }
            style = PASCAL_STYLE_STRING;
            break;
        default:
            /* parse numbers */
            if (qe_isdigit(c) || c == '$') {
                for (; i < n; i++) {
                    if (!qe_isalnum(str[i]) && str[i] != '.')
                        break;
                }
                style = PASCAL_STYLE_NUMBER;
                break;
            }
            /* parse identifiers and keywords */
            if (qe_isalpha_(c)) {
                i += ustr_get_identifier_lc(kbuf, countof(kbuf), c, str, i, n);
                if (strfind(syn->keywords, kbuf)) {
                    style = PASCAL_STYLE_KEYWORD;
                } else
                if (strfind(syn->types, kbuf)) {
                    style = PASCAL_STYLE_TYPE;
                } else {
                    k = i;
                    if (qe_isblank(str[k]))
                        k++;
                    if (str[k] == '(' && str[k + 1] != '*')
                        style = PASCAL_STYLE_FUNCTION;
                    else
                        style = PASCAL_STYLE_IDENTIFIER;
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

static ModeDef pascal_mode = {
    .name = "Pascal",
    .extensions = "p|pas",
    .keywords = pascal_keywords,
    .types = pascal_types,
    .colorize_func = pascal_colorize_line,
};

static int pascal_init(void)
{
    qe_register_mode(&pascal_mode, MODEF_SYNTAX);

    return 0;
}

/*---------------- Ada coloring ----------------*/

static char const ada_keywords[] = {
    "asm|begin|case|const|constructor|destructor|do|downto|else|elsif|end|"
    "file|for|function|goto|if|implementation|in|inline|interface|label|"
    "nil|object|of|procedure|program|repeat|then|to|type|unit|until|"
    "uses|var|while|with|use|is|new|all|package|private|loop|body|"
    "raise|return|pragma|constant|exception|when|out|range|tagged|access|"
    "record|exit|subtype|generic|limited|"

    "and|div|mod|not|or|shl|shr|xor|false|true|null|eof|eoln|"
    //"'class|'first|'last|"
};

static char const ada_types[] = {
    "array|boolean|byte|char|comp|double|extended|integer|longint|"
    "packed|real|shortint|single|string|text|word|"
    "duration|time|character|set|"
    "wide_character|wide_string|wide_wide_character|wide_wide_string|"
};

enum {
    IN_ADA_COMMENT1 = 0x01,
    IN_ADA_COMMENT2 = 0x02,
};

enum {
    ADA_STYLE_TEXT =       QE_STYLE_DEFAULT,
    ADA_STYLE_KEYWORD =    QE_STYLE_KEYWORD,
    ADA_STYLE_TYPE =       QE_STYLE_TYPE,
    ADA_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    ADA_STYLE_COMMENT =    QE_STYLE_COMMENT,
    ADA_STYLE_STRING =     QE_STYLE_STRING,
    ADA_STYLE_IDENTIFIER = QE_STYLE_DEFAULT,
    ADA_STYLE_NUMBER =     QE_STYLE_NUMBER,
    ADA_STYLE_FUNCTION =   QE_STYLE_FUNCTION,
};

static void ada_colorize_line(QEColorizeContext *cp,
                                 unsigned int *str, int n, ModeDef *syn)
{
    char kbuf[MAX_KEYWORD_SIZE];
    int i = 0, start = i, c, k, style;
    int colstate = cp->colorize_state;

    if (colstate & IN_ADA_COMMENT1)
        goto in_comment1;

    if (colstate & IN_ADA_COMMENT2)
        goto in_comment2;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '-':
        case '/':
            if (str[i] == (unsigned int)c) {  /* // or -- comments */
                i = n;
                SET_COLOR(str, start, i, ADA_STYLE_COMMENT);
                continue;
            }
            break;
        case '{':
            /* regular comment (recursive?) */
            colstate = IN_ADA_COMMENT1;
        in_comment1:
            while (i < n) {
                if (str[i++] == '}') {
                    colstate = 0;
                    break;
                }
            }
            SET_COLOR(str, start, i, ADA_STYLE_COMMENT);
            continue;
        case '(':
            if (str[i] != '*')
                break;

            /* regular comment (recursive?) */
            colstate = IN_ADA_COMMENT2;
            i++;
        in_comment2:
            for (; i < n; i++) {
                if (str[i] == '*' && str[i + 1] == ')') {
                    i += 2;
                    colstate = 0;
                    break;
                }
            }
            SET_COLOR(str, start, i, ADA_STYLE_COMMENT);
            continue;
        case '\'':
            if (i + 2 < n && str[i + 2] == '\'') {
                i += 2;
                SET_COLOR(str, start, i, ADA_STYLE_STRING);
                continue;
            }
            break;
        case '\"':
            /* parse string or char const */
            while (i < n) {
                /* XXX: escape sequences? */
                if (str[i++] == (unsigned int)c)
                    break;
            }
            SET_COLOR(str, start, i, ADA_STYLE_STRING);
            continue;
        default:
            break;
        }
        /* parse numbers */
        if (qe_isdigit(c)) {
            for (; qe_isdigit_(str[i]) || str[i] == '.'; i++)
                continue;
            if (str[i] == '#') {
                for (k = 1; qe_isalnum_(str[k]) || str[i] == '.'; k++)
                    continue;
                if (k > 1 && str[k] == '#')
                    i = k + 1;
            }
            if (qe_tolower(str[i]) == 'e') {
                k = i + 1;
                if (str[k] == '+' || str[k] == '-')
                    k++;
                if (qe_isdigit(str[k])) {
                    for (i = k + 1; qe_isdigit_(str[i]); i++)
                        continue;
                }
            }
            SET_COLOR(str, start, i, ADA_STYLE_NUMBER);
            continue;
        }
        /* parse identifiers and keywords */
        if (qe_isalpha_(c)) {
            i += ustr_get_identifier_lc(kbuf, countof(kbuf), c, str, i, n);
            if (strfind(syn->keywords, kbuf)) {
                style = ADA_STYLE_KEYWORD;
            } else
            if (strfind(syn->types, kbuf)) {
                style = ADA_STYLE_TYPE;
            } else
            if (check_fcall(str, i))
                style = ADA_STYLE_FUNCTION;
            else
                style = ADA_STYLE_IDENTIFIER;

            SET_COLOR(str, start, i, style);
            continue;
        }
    }
    cp->colorize_state = colstate;
}

static ModeDef ada_mode = {
    .name = "Ada",
    .extensions = "ada|adb|ads",
    .keywords = ada_keywords,
    .types = ada_types,
    .colorize_func = ada_colorize_line,
};

static int ada_init(void)
{
    qe_register_mode(&ada_mode, MODEF_SYNTAX);

    return 0;
}

/*---------------- Fortran coloring ----------------*/

static char const fortran_keywords[] = {
    "recursive|block|call|case|common|contains|continue|"
    "default|do|else|elseif|elsewhere|end|enddo|endif|exit|format|"
    "function|goto|if|implicit|kind|module|private|procedure|"
    "program|public|return|select|stop|subroutine|then|"
    "use|where|in|out|inout|interface|none|while|"
    "forall|equivalence|any|assign|go|to|pure|elemental|"
    "external|intrinsic|"
    "open|close|read|write|rewind|backspace|print|inquire|"
    "allocate|deallocate|associated|nullify|present|"
    ".and.|.eq.|.false.|.ge.|.gt.|.le.|.lt.|.ne.|.not.|.or.|.true.|"
};

static char const fortran_types[] = {
    "character|complex|digits|double|dimension|epsilon|huge|"
    "integer|logical|maxexponent|minexponent|operator|target|"
    "parameter|pointer|precision|radix|range|real|tiny|intent|"
    "optional|allocatable|type|"
};

enum {
    FORTRAN_STYLE_TEXT =       QE_STYLE_DEFAULT,
    FORTRAN_STYLE_KEYWORD =    QE_STYLE_KEYWORD,
    FORTRAN_STYLE_TYPE =       QE_STYLE_TYPE,
    FORTRAN_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    FORTRAN_STYLE_COMMENT =    QE_STYLE_COMMENT,
    FORTRAN_STYLE_STRING =     QE_STYLE_STRING,
    FORTRAN_STYLE_IDENTIFIER = QE_STYLE_DEFAULT,
    FORTRAN_STYLE_NUMBER =     QE_STYLE_NUMBER,
    FORTRAN_STYLE_FUNCTION =   QE_STYLE_FUNCTION,
};

static void fortran_colorize_line(QEColorizeContext *cp,
                                 unsigned int *str, int n, ModeDef *syn)
{
    char keyword[MAX_KEYWORD_SIZE];
    int i = 0, start = i, c, style, len, w;
    int colstate = cp->colorize_state;

    for (w = 0; qe_isblank(str[w]); w++)
        continue;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            if (start == 0)
                goto preprocess;
            break;
        case '*':
        case 'c':
        case 'C':
            if (start == 0 && !qe_isalpha(str[i]))
                goto comment;
            break;
        case '!':
        comment:
            while (str[i] == ' ')
                i++;
            if (str[i] == '{') {
            preprocess:
                i = n;
                SET_COLOR(str, start, i, FORTRAN_STYLE_PREPROCESS);
                continue;
            }
            i = n;
            SET_COLOR(str, start, i, FORTRAN_STYLE_COMMENT);
            continue;
        case '\'':
        case '\"':
            /* parse string or char const */
            while (i < n) {
                /* XXX: escape sequences? */
                if (str[i++] == (unsigned int)c)
                    break;
            }
            SET_COLOR(str, start, i, FORTRAN_STYLE_STRING);
            continue;
        default:
            break;
        }
        /* parse numbers */
        if (qe_isdigit(c)) {
            /* Parse actual Fortran number syntax, with D or E for exponent */
            for (; qe_isdigit(str[i]); i++)
                continue;
            if (str[i] == '.' && qe_isdigit(str[i + 1])) {
                for (i += 2; qe_isdigit(str[i]); i++)
                    continue;
            }
            if ((c = qe_tolower(str[i])) == 'e' || c == 'd') {
                int k = i + 1;
                if (str[k] == '+' || str[k] == '-')
                    k++;
                if (qe_isdigit(str[k])) {
                    for (i = k + 1; qe_isdigit(str[i]); i++)
                        continue;
                }
            }
            SET_COLOR(str, start, i, FORTRAN_STYLE_NUMBER);
            continue;
        }
        /* parse identifiers and keywords */
        if (qe_isalpha_(c) || (c == '.' && qe_isalpha(str[i]))) {
            len = 0;
            keyword[len++] = qe_tolower(c);
            for (; qe_isalnum_(str[i]); i++) {
                if (len < countof(keyword) - 1)
                    keyword[len++] = qe_tolower(str[i]);
            }
            if (c == '.' && str[i] == '.' && len < countof(keyword) - 1)
                keyword[len++] = str[i++];
            keyword[len] = '\0';

            if (strfind(syn->keywords, keyword)
            ||  (start == w && strfind("data|save", keyword))) {
                style = FORTRAN_STYLE_KEYWORD;
            } else
            if (strfind(syn->types, keyword)) {
                style = FORTRAN_STYLE_TYPE;
            } else
            if (check_fcall(str, i))
                style = FORTRAN_STYLE_FUNCTION;
            else
                style = FORTRAN_STYLE_IDENTIFIER;

            SET_COLOR(str, start, i, style);
            continue;
        }
    }
    cp->colorize_state = colstate;
}

static ModeDef fortran_mode = {
    .name = "Fortran",
    .extensions = "f|for|f77|f90|f95|f03",
    .keywords = fortran_keywords,
    .types = fortran_types,
    .colorize_func = fortran_colorize_line,
};

static int fortran_init(void)
{
    qe_register_mode(&fortran_mode, MODEF_SYNTAX);

    return 0;
}

/*---------------- Ini file (and similar) coloring ----------------*/

enum {
    INI_STYLE_TEXT =       QE_STYLE_DEFAULT,
    INI_STYLE_COMMENT =    QE_STYLE_COMMENT,
    INI_STYLE_STRING =     QE_STYLE_STRING,
    INI_STYLE_FUNCTION =   QE_STYLE_FUNCTION,
    INI_STYLE_NUMBER =     QE_STYLE_NUMBER,
    INI_STYLE_IDENTIFIER = QE_STYLE_VARIABLE,
    INI_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
};

static void ini_colorize_line(QEColorizeContext *cp,
                              unsigned int *str, int n, ModeDef *syn)
{
    int i = 0, start, c, style = 0, indent;

    while (qe_isblank(str[i]))
        i++;

    indent = i;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case ';':
            if (start == indent) {
                i = n;
                style = INI_STYLE_COMMENT;
                break;
            }
            continue;
        case '#':
            if (start == indent) {
                i = n;
                style = INI_STYLE_PREPROCESS;
                break;
            }
            continue;
        case '[':
            if (start == 0) {
                i = n;
                style = INI_STYLE_FUNCTION;
                break;
            }
            continue;
        case '\"':
            /* parse string const */
            while (i < n) {
                /* XXX: escape sequences? */
                if (str[i++] == '\"')
                    break;
            }
            style = INI_STYLE_STRING;
            break;
        case ' ':
        case '\t':
            continue;
        default:
            /* parse numbers */
            if (qe_isdigit(c)) {
                for (; i < n; i++) {
                    if (!qe_isalnum(str[i]))
                        break;
                }
                style = INI_STYLE_NUMBER;
                break;
            }
            /* parse identifiers and keywords */
            if (start == 0 && (qe_isalpha_(c) || c == '@' || c == '$')) {
                for (; i < n; i++) {
                    if (str[i] == '=')
                        break;
                }
                if (i < n) {
                    style = INI_STYLE_IDENTIFIER;
                }
                break;
            }
        }
        if (style) {
            SET_COLOR(str, start, i, style);
            style = 0;
        }
    }
}

static int ini_mode_probe(ModeDef *mode, ModeProbeData *pd)
{
    const char *p = (const char *)pd->buf;
    const char *p_end = p + pd->buf_size;

    if (match_extension(pd->filename, mode->extensions))
        return 80;

    while (p < p_end) {
        /* skip comments */
        if (*p == ';' || *p == '#') {
            p = memchr(p, '\n', p_end - p);
            if (!p)
                return 1;
        }
        if (*p == '\n') {
            p++;
            continue;
        }
        /* Check for ^\[.+\]\n */
        if (*p == '[' && p[1] != '[' && p[1] != '{') {
            while (++p < p_end) {
                if (*p == ']')
                    return 40;
                if (*p == '\n')
                    return 1;
            }
        }
        break;
    }
    return 1;
}

static ModeDef ini_mode = {
    .name = "ini",
    .extensions = "ini|inf|INI|INF|reg",
    .mode_probe = ini_mode_probe,
    .colorize_func = ini_colorize_line,
};

static int ini_init(void)
{
    qe_register_mode(&ini_mode, MODEF_SYNTAX);

    return 0;
}

/*---------------- sharp file coloring ----------------*/

/* Very simple colorizer: # introduces comments, that's it! */

enum {
    SHARP_STYLE_TEXT =       QE_STYLE_DEFAULT,
    SHARP_STYLE_COMMENT =    QE_STYLE_COMMENT,
};

static void sharp_colorize_line(QEColorizeContext *cp,
                               unsigned int *str, int n, ModeDef *syn)
{
    int i = 0, start, c;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            i = n;
            SET_COLOR(str, start, i, SHARP_STYLE_COMMENT);
            continue;
        default:
            break;
        }
    }
}

static int sharp_mode_probe(ModeDef *mode, ModeProbeData *pd)
{
    const char *p = cs8(pd->buf);

    while (qe_isspace(*p))
        p++;

    if (*p == '#') {
        if (match_extension(pd->filename, mode->extensions))
            return 60;
        return 30;
    }
    return 1;
}

static ModeDef sharp_mode = {
    .name = "sharp",
    .extensions = "txt",
    .mode_probe = sharp_mode_probe,
    .colorize_func = sharp_colorize_line,
};

static int sharp_init(void)
{
    qe_register_mode(&sharp_mode, MODEF_SYNTAX);

    return 0;
}

/*---------------- PostScript colors ----------------*/

enum {
    IN_PS_STRING  = 0x0F            /* ( ... ) level */,
    IN_PS_COMMENT = 0x10,
};

enum {
    PS_STYLE_TEXT =       QE_STYLE_DEFAULT,
    PS_STYLE_COMMENT =    QE_STYLE_COMMENT,
    PS_STYLE_STRING =     QE_STYLE_STRING,
    PS_STYLE_NUMBER =     QE_STYLE_DEFAULT,
    PS_STYLE_IDENTIFIER = QE_STYLE_FUNCTION,
};

#define wrap 0

static void ps_colorize_line(QEColorizeContext *cp,
                             unsigned int *str, int n, ModeDef *syn)
{
    int i = 0, start = i, c;
    int colstate = cp->colorize_state;

    if (colstate & IN_PS_COMMENT)
        goto in_comment;

    if (colstate & IN_PS_STRING)
        goto in_string;

    colstate = 0;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
            /* Should deal with '<...>' '<<...>>' '<~...~>' tokens. */
        case '%':
        in_comment:
            if (wrap)
                colstate |= IN_PS_COMMENT;
            else
                colstate &= ~IN_PS_COMMENT;
            i = n;
            SET_COLOR(str, start, i, PS_STYLE_COMMENT);
            continue;
        case '(':
            colstate++;
        in_string:
            /* parse string skipping embedded \\ */
            while (i < n) {
                switch (str[i++]) {
                case '(':
                    colstate++;
                    continue;
                case ')':
                    colstate--;
                    if (!(colstate & IN_PS_STRING))
                        break;
                    continue;
                case '\\':
                    if (i == n)
                        break;
                    i++;
                    continue;
                default:
                    continue;
                }
                break;
            }
            SET_COLOR(str, start, i, PS_STYLE_STRING);
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
            SET_COLOR(str, start, i, PS_STYLE_NUMBER);
            continue;
        }
        /* parse identifiers and keywords */
        if (qe_isalpha_(c)) {
            for (; i < n; i++) {
                if (qe_findchar(" \t\r\n,()<>[]{}/", str[i]))
                    break;
            }
            SET_COLOR(str, start, i, PS_STYLE_IDENTIFIER);
            continue;
        }
    }
    cp->colorize_state = colstate;
#undef wrap
}

static int ps_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (match_extension(p->filename, mode->extensions))
        return 80;

    if (*p->buf == '%' && qe_stristr((const char *)p->buf, "script"))
        return 40;

    return 1;
}

static ModeDef ps_mode = {
    .name = "Postscript",
    .extensions = "ps|ms|eps",
    .mode_probe = ps_mode_probe,
    .colorize_func = ps_colorize_line,
};

static int ps_init(void)
{
    qe_register_mode(&ps_mode, MODEF_SYNTAX);

    return 0;
}

/*---------------- SQL script coloring ----------------*/

enum {
    IN_SQL_COMMENT = 1,
};

enum {
    SQL_STYLE_TEXT =       QE_STYLE_DEFAULT,
    SQL_STYLE_COMMENT =    QE_STYLE_COMMENT,
    SQL_STYLE_STRING =     QE_STYLE_STRING,
    SQL_STYLE_KEYWORD =    QE_STYLE_KEYWORD,
    SQL_STYLE_TYPE =       QE_STYLE_TYPE,
    SQL_STYLE_IDENTIFIER = QE_STYLE_DEFAULT,
    SQL_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
};

static char const sql_keywords[] = {
    "abs|acos|add|aes_decrypt|aes_encrypt|after|all|alter|analyse|analyze|"
    "and|as|asc|ascii|asin|atan|atan2|auto_increment|avg|backup|begin|"
    "benchmark|between|bin|binlog|bit_and|bit_count|bit_length|bit_or|"
    "bit_xor|both|btree|by|call|case|cast|ceil|ceiling|change|character|"
    "character_length|char_length|check|checksum|clob|clock|coalesce|"
    "collate|column|columns|comment|commit|compressed|concat|concat_ws|"
    "concurrent|constraint|contents|controlfile|conv|convert|cos|cot|"
    "count|crc32|crc64|create|current_date|current_time|current_timestamp|"
    "current_user|data|database|databases|declare|default|degrees|delayed|"
    "delete|desc|describe|directory|disable|discard|div|do|drop|dump|elt|"
    "enable|enclosed|end|engine|enum|escaped|event|events|execute|exists|"
    "exp|explain|export_set|fail|false|field|fields|find_in_set|first|"
    "floor|for|foreign|format|found_rows|from|full|fulltext|function|"
    "global|go|grant|greatest|group_concat|handler|hash|having|help|hex|"
    "high_priority|hsieh_hash|if|ifnull|ignore|import|in|index|inet|infile|"
    "insert|install|instr|interval|into|is|isnull|iterate|jenkins_hash|key|"
    "keys|last|last_insert_id|lcase|leading|least|leave|left|length|like|"
    "limit|lines|ln|load|load_file|local|localtime|localtimestamp|locate|"
    "lock|log|log10|log2|logs|loop|lower|low_priority|lpad|ltrim|make_set|"
    "max|md5|md5_bin|memory|mid|min|mod|modify|no|none|not|now|null|nullif|"
    "oct|off|offset|on|optionally|or|ord|order|outfile|password|pi|pid|pow|"
    "power|prepare|primary|print|procedure|quote|radians|rand|recno|"
    "release|rename|repair|repeat|replace|restore|return|reverse|revoke|"
    "right|rollback|round|rpad|rtree|rtrim|rule|savepoint|schema|select|"
    "sequence|serial|server|session|set|sha|sha1|sha128_bin|sha224_bin|"
    "sha256_bin|sha384_bin|sha512_bin|show|sign|signed|sin|soundex|source|"
    "space|spatial|sql_calc_found_rows|sqrt|start|starting|stats|std|"
    "stddev|stddev_pop|stddev_samp|strcmp|string|structure|substr|"
    "substring|substring_index|sum|table|tables|tan|temporary|terminated|"
    "time|timestamp|timings|to|trailing|transaction|trigger|trim|true|"
    "truncate|type|ucase|unhex|uninstall|unique|unix_timestamp|unknown|"
    "unlock|update|upper|use|user|using|utf8|value|values|varbinary|"
    "variables|variance|var_pop|var_samp|verbose|version_comment|view|"
    "when|where|while|xml|year|yes|"
    "pragma|"
    "adddate|addtime|curdate|curtime|date_add|date_sub|date_format|"
    "datediff|day|dayname|dayofmonth|dayofweek|dayofyear|extract|"
    "from_days|from_unixtime|get_format|hour|last_day|makedate|maketime|"
    "microsecond|minute|month|monthname|period_add|period_diff|quarter|"
    "sec_to_time|second|str_to_date|subdate|subtime|sysdate|timediff|"
    "time_format|time_to_sec|to_days|utc_date|utc_time|utc_timestamp|"
    "week|weekday|weekofyear|yearweek|second_microsecond|"
    "minute_microsecond|minute_second|hour_microsecond|hour_second|"
    "hour_minute|day_microsecond|day_second|day_minute|day_hour|"
    "year_month|"
};

static char const sql_types[] = {
    "bigint|binary|bit|blob|bool|char|counter|date|datetime|dec|decimal|"
    "double|fixed|float|int|int16|int24|int32|int48|int64|int8|integer|"
    "largeint|long|longblob|longtext|mediumblob|mediumint|mediumtext|"
    "memo|number|numeric|real|smallint|text|tinyblob|tinyint|tinytext|"
    "uint16|uint24|uint32|uint48|uint64|uint8|ulong|unsigned|varchar|"
    "varchar2|"
};

static void sql_colorize_line(QEColorizeContext *cp,
                              unsigned int *str, int n, ModeDef *syn)
{
    char kbuf[MAX_KEYWORD_SIZE];
    int i = 0, start = i, c, style;
    int state = cp->colorize_state;

    if (state & IN_SQL_COMMENT)
        goto parse_c_comment;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '/':
            if (str[i] == '/')
                goto line_comment;
            if (str[i] == '*') {
                /* normal comment */
                i++;
            parse_c_comment:
                state |= IN_SQL_COMMENT;
                for (; i < n; i++) {
                    if (str[i] == '*' && str[i + 1] == '/') {
                        i += 2;
                        state &= ~IN_SQL_COMMENT;
                        break;
                    }
                }
                goto comment;
            }
            break;
        case '-':
            if (str[i] == '-')
                goto line_comment;
            break;
        case '#':
        line_comment:
            i = n;
        comment:
            SET_COLOR(str, start, i, SQL_STYLE_COMMENT);
            continue;
        case '\'':
        case '\"':
        case '`':
            /* parse string const */
            for (; i < n; i++) {
                /* FIXME: Should parse strings more accurately */
                if (str[i] == '\\' && i + 1 < n) {
                    i++;
                    continue;
                }
                if (str[i] == (unsigned int)c) {
                    i++;
                    break;
                }
            }
            style = SQL_STYLE_STRING;
            if (c == '`')
                style = SQL_STYLE_IDENTIFIER;
            SET_COLOR(str, start, i, style);
            continue;
        default:
            break;
        }
        /* parse identifiers and keywords */
        if (qe_isalpha_(c)) {
            i += ustr_get_identifier_lc(kbuf, countof(kbuf), c, str, i, n);
            if (strfind(syn->keywords, kbuf)) {
                SET_COLOR(str, start, i, SQL_STYLE_KEYWORD);
                continue;
            }
            if (strfind(syn->types, kbuf)) {
                SET_COLOR(str, start, i, SQL_STYLE_TYPE);
                continue;
            }
            SET_COLOR(str, start, i, SQL_STYLE_IDENTIFIER);
            continue;
        }
    }
    cp->colorize_state = state;
}

static int sql_mode_probe(ModeDef *mode, ModeProbeData *pd)
{
    const char *p = cs8(pd->buf);

    if (strstart(p, "PRAGMA foreign_keys=OFF;", NULL)
    ||  strstart(p, "-- phpMyAdmin SQL Dump", NULL)) {
        return 80;
    }
    if (match_extension(pd->filename, mode->extensions))
        return 60;

    return 1;
}

static ModeDef sql_mode = {
    .name = "SQL",
    .extensions = "sql|Sql|mysql|sqlite|sqlplus|rdb|xdb|db",
    .mode_probe = sql_mode_probe,
    .keywords = sql_keywords,
    .types = sql_types,
    .colorize_func = sql_colorize_line,
};

static int sql_init(void)
{
    qe_register_mode(&sql_mode, MODEF_SYNTAX);

    return 0;
}

/*---------------- Lua script coloring ----------------*/

static char const lua_keywords[] = {
    "|and|break|do|else|elseif|end|false|for|function|goto|if|in"
    "|local|nil|not|or|repeat|return|then|true|until|while"
    "|self"
    "|"
};

#if 0
static char const lua_tokens[] = {
    "|+|-|*|/|%|^|#|==|~=|<=|>=|<|>|=|(|)|{|}|[|]|::|;|:|,|...|..|.|"
};
#endif

enum {
    IN_LUA_COMMENT = 0x10,
    IN_LUA_STRING  = 0x20,
    IN_LUA_STRING2 = 0x40,
    IN_LUA_LONGLIT = 0x80,
    IN_LUA_LEVEL   = 0x0F,
};

enum {
    LUA_STYLE_TEXT =     QE_STYLE_DEFAULT,
    LUA_STYLE_COMMENT =  QE_STYLE_COMMENT,
    LUA_STYLE_STRING =   QE_STYLE_STRING,
    LUA_STYLE_LONGLIT =  QE_STYLE_STRING,
    LUA_STYLE_NUMBER =   QE_STYLE_NUMBER,
    LUA_STYLE_KEYWORD =  QE_STYLE_KEYWORD,
    LUA_STYLE_FUNCTION = QE_STYLE_FUNCTION,
};

static int lua_long_bracket(unsigned int *str, int *level)
{
    int i;

    for (i = 1; str[i] == '='; i++)
        continue;
    if (str[i] == str[0]) {
        *level = i - 1;
        return 1;
    } else {
        return 0;
    }
}

static void lua_colorize_line(QEColorizeContext *cp,
                              unsigned int *str, int n, ModeDef *syn)
{
    int i = 0, j, start = i, c, sep = 0, level = 0, level1, style;
    int state = cp->colorize_state;
    char kbuf[64];

    if (state & IN_LUA_LONGLIT) {
        /* either a comment or a string */
        level = state & IN_LUA_LEVEL;
        goto parse_longlit;
    }

    if (state & IN_LUA_STRING) {
        sep = '\'';
        state = 0;
        goto parse_string;
    }
    if (state & IN_LUA_STRING2) {
        sep = '\"';
        state = 0;
        goto parse_string;
    }

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '-':
            if (str[i] == '-') {
                if (str[i + 1] == '['
                &&  lua_long_bracket(str + i + 1, &level)) {
                    state = IN_LUA_COMMENT | IN_LUA_LONGLIT |
                            (level & IN_LUA_LEVEL);
                    goto parse_longlit;
                }
                i = n;
                SET_COLOR(str, start, i, LUA_STYLE_COMMENT);
                continue;
            }
            break;
        case '\'':
        case '\"':
            /* parse string const */
            sep = c;
        parse_string:
            while (i < n) {
                c = str[i++];
                if (c == '\\') {
                    if (str[i] == 'z' && i + 1 == n) {
                        /* XXX: partial support for \z */
                        state = (sep == '\'') ? IN_LUA_STRING : IN_LUA_STRING2;
                        i += 1;
                    } else
                    if (i == n) {
                        state = (sep == '\'') ? IN_LUA_STRING : IN_LUA_STRING2;
                    } else {
                        i += 1;
                    }
                } else
                if (c == sep) {
                    break;
                }
            }
            SET_COLOR(str, start, i, LUA_STYLE_STRING);
            continue;
        case '[':
            if (lua_long_bracket(str + i - 1, &level)) {
                state = IN_LUA_LONGLIT | (level & IN_LUA_LEVEL);
                goto parse_longlit;
            }
            break;
        parse_longlit:
            style = (state & IN_LUA_COMMENT) ?
                    LUA_STYLE_COMMENT : LUA_STYLE_LONGLIT;
            for (; i < n; i++) {
                if (str[i] == ']'
                &&  lua_long_bracket(str + i, &level1)
                &&  level1 == level) {
                    state = 0;
                    i += level + 2;
                    break;
                }
            }
            SET_COLOR(str, start, i, style);
            continue;
        default:
            if (qe_isdigit(c)) {
                /* XXX: should parse actual number syntax */
                for (; i < n; i++) {
                    if (!qe_isalnum(str[i]) && str[i] != '.')
                        break;
                }
                SET_COLOR(str, start, i, LUA_STYLE_NUMBER);
                continue;
            }
            if (qe_isalpha_(c)) {
                i += ustr_get_identifier(kbuf, countof(kbuf), c, str, i, n);
                if (strfind(syn->keywords, kbuf)) {
                    SET_COLOR(str, start, i, LUA_STYLE_KEYWORD);
                    continue;
                }
                for (j = i; j < n && qe_isspace(str[j]); j++)
                    continue;
                /* function calls use parenthesized argument list or
                   single string or table literal */
                if (qe_findchar("('\"{", str[j])) {
                    SET_COLOR(str, start, i, LUA_STYLE_FUNCTION);
                    continue;
                }
                continue;
            }
            break;
        }
    }
    cp->colorize_state = state;
}

static ModeDef lua_mode = {
    .name = "Lua",
    .extensions = "lua",
    .shell_handlers = "lua",
    .keywords = lua_keywords,
    .colorize_func = lua_colorize_line,
};

static int lua_init(void)
{
    qe_register_mode(&lua_mode, MODEF_SYNTAX);

    return 0;
}

/*---------------- Julia coloring ----------------*/

static char const julia_keywords[] = {
    "abstract|assert|baremodule|begin|bitstype|break|catch|ccall|"
    "const|continue|do|else|elseif|end|export|finally|for|function|"
    "global|if|immutable|import|importall|in|let|local|macro|module|"
    "quote|return|sizeof|throw|try|type|typeof|using|while|yieldto|"
};

static char const julia_types[] = {
    "Int8|Uint8|Int16|Uint16|Int32|Uint32|Int64|Uint64|Int128|Uint128|"
    "Bool|Char|Float16|Float32|Float64|Int|Uint|BigInt|BigFloat|"
    "Array|Union|Nothing|SubString|UTF8String|"
    "None|Any|ASCIIString|DataType|Complex|RegexMatch|Symbol|Expr|"
    "VersionNumber|Exception|"
    "Number|Real|FloatingPoint|Integer|Signed|Unsigned|"
    "Vector|Matrix|UnionType|"
    "ArgumentError|BoundsError|DivideError|DomainError|EOFError|"
    "ErrorException|InexactError|InterruptException|KeyError|LoadError|"
    "MemoryError|MethodError|OverflowError|ParseError|SystemError|"
    "TypeError|UndefRefError|"
    "Range|Function|Dict|"
};

static char const julia_constants[] = {
    "false|true|Inf16|NaN16|Inf32|NaN32|Inf|NaN|im|nothing|pi|e|"
};

#if 0
static char const julia_builtin[] = {
    "include|new|convert|promote|eval|super|isa|bits|eps|"
    "nextfloat|prevfloat|typemin|typemax|println|zero|one|"
    "complex|num|den|float|int|char|length|endof|"
    "info|warn|error|"
};
#endif

enum {
    IN_JULIA_STRING      = 0x10,
    IN_JULIA_STRING_BQ   = 0x20,
    IN_JULIA_LONG_STRING = 0x40,
};

enum {
    JULIA_STYLE_TEXT =     QE_STYLE_DEFAULT,
    JULIA_STYLE_COMMENT =  QE_STYLE_COMMENT,
    JULIA_STYLE_STRING =   QE_STYLE_STRING,
    JULIA_STYLE_NUMBER =   QE_STYLE_NUMBER,
    JULIA_STYLE_KEYWORD =  QE_STYLE_KEYWORD,
    JULIA_STYLE_TYPE =     QE_STYLE_TYPE,
    JULIA_STYLE_FUNCTION = QE_STYLE_FUNCTION,
    JULIA_STYLE_SYMBOL =   QE_STYLE_NUMBER,
};

static inline int julia_is_name(int c) {
    return qe_isalpha_(c) || c > 0xA0;
}

static inline int julia_is_name1(int c) {
    return qe_isalnum_(c) || c == '!' || c > 0xA0;
}

static int julia_get_name(char *buf, int buf_size, const unsigned int *p)
{
    buf_t outbuf, *out;
    int i = 0;

    out = buf_init(&outbuf, buf, buf_size);

    if (julia_is_name(p[i])) {
        buf_putc_utf8(out, p[i]);
        for (i++; julia_is_name1(p[i]); i++) {
            buf_putc_utf8(out, p[i]);
        }
    }
    return i;
}

static int julia_get_number(const unsigned int *p)
{
    const unsigned int *p0 = p;
    int c;

    c = *p++;
    if (c == '0' && qe_tolower(*p) == 'o' && qe_isoctdigit(p[1])) {
        /* octal numbers */
        for (p += 2; qe_isoctdigit(*p); p++)
            continue;
    } else
    if (c == '0' && qe_tolower(*p) == 'x' && qe_isxdigit(p[1])) {
        /* hexadecimal numbers */
        for (p += 2; qe_isxdigit(*p); p++)
            continue;
        /* parse hexadecimal floats */
        if (*p == '.') {
            for (p += 1; qe_isxdigit(*p); p++)
                continue;
        }
        if (qe_tolower(*p) == 'p') {
            int k = 1;
            if (p[k] == '+' || p[k] == '-')
                k++;
            if (qe_isdigit(p[k])) {
                for (p += k + 1; qe_isdigit(*p); p++)
                    continue;
            }
        }
    } else
    if (qe_isdigit(c)) {
        /* decimal numbers */
        for (; qe_isdigit(*p); p++)
            continue;
        if (*p == '.') {
            for (p += 1; qe_isdigit(*p); p++)
                continue;
        }
        if ((c = qe_tolower(*p)) == 'e' || c == 'f') {
            int k = 1;
            if (p[k] == '+' || p[k] == '-')
                k++;
            if (qe_isdigit(p[k])) {
                for (p += k + 1; qe_isdigit(*p); p++)
                    continue;
            }
        }
    } else {
        p -= 1;
    }
    return p - p0;
}

static void julia_colorize_line(QEColorizeContext *cp,
                                unsigned int *str, int n, ModeDef *syn)
{
    int i = 0, start = i, c, sep = 0, klen;
    int state = cp->colorize_state;
    char kbuf[64];

    if (state & IN_JULIA_STRING) {
        sep = '\"';
        goto parse_string;
    }
    if (state & IN_JULIA_STRING_BQ) {
        sep = '`';
        goto parse_string;
    }
    if (state & IN_JULIA_LONG_STRING) {
        sep = '\"';
        goto parse_long_string;
    }

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            i = n;
            SET_COLOR(str, start, i, JULIA_STYLE_COMMENT);
            continue;

        case '\'':
            if (start > 0 && (julia_is_name1(str[i - 2]) || str[i - 2] == '.'))
                break;
            sep = c;
            state = IN_JULIA_STRING_BQ;
            goto parse_string;

        case '`':
            sep = c;
            goto parse_string;

        case '\"':
        has_string:
            /* parse string or character const */
            sep = c;
            state = IN_JULIA_STRING;
            if (str[i] == (unsigned int)sep && str[i + 1] == (unsigned int)sep) {
                /* multi-line string """ ... """ */
                state = IN_JULIA_LONG_STRING;
                i += 2;
            parse_long_string:
                while (i < n) {
                    c = str[i++];
                    if (c == '\\') {
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
            parse_string:
                while (i < n) {
                    c = str[i++];
                    if (c == '\\') {
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
            while (qe_findchar("imsx", str[i])) {
                /* regex suffix */
                i++;
            }
            SET_COLOR(str, start, i, JULIA_STYLE_STRING);
            continue;

        default:
            if (qe_isdigit(c)) {
                /* numbers can be directly adjacent to identifiers */
                klen = julia_get_number(str + i - 1);
                i += klen - 1;
                SET_COLOR(str, start, i, JULIA_STYLE_NUMBER);
                continue;
            }
            if (julia_is_name(c)) {
                klen = julia_get_name(kbuf, sizeof(kbuf), str + i - 1);
                i += klen - 1;
                if (str[i] == '"') {
                    c = str[i++];
                    goto has_string;
                }
                if (strfind(syn->keywords, kbuf)
                ||  strfind(julia_constants, kbuf)) {
                    SET_COLOR(str, start, i, JULIA_STYLE_KEYWORD);
                    continue;
                }
                if (strfind(syn->types, kbuf)) {
                    SET_COLOR(str, start, i, JULIA_STYLE_TYPE);
                    continue;
                }
                if (check_fcall(str, i)) {
                    SET_COLOR(str, start, i, JULIA_STYLE_FUNCTION);
                    continue;
                }
                continue;
            }
            break;
        }
    }
    cp->colorize_state = state;
}

static ModeDef julia_mode = {
    .name = "Julia",
    .extensions = "jl",
    .keywords = julia_keywords,
    .types = julia_types,
    .colorize_func = julia_colorize_line,
};

static int julia_init(void)
{
    qe_register_mode(&julia_mode, MODEF_SYNTAX);

    return 0;
}

/*---------------- Haskell coloring ----------------*/

static char const haskell_keywords[] = {
    "|_|case|class|data|default|deriving|do|else|foreign"
    "|if|import|in|infix|infixl|infixr|instance|let"
    "|module|newtype|of|then|type|where|as|qualified"
    "|return"
    "|True|False"
};

static char const haskell_types[] = {
    //String|Int|Char|Bool
    "|"
};

enum {
    HASKELL_STYLE_TEXT =     QE_STYLE_DEFAULT,
    HASKELL_STYLE_COMMENT =  QE_STYLE_COMMENT,
    HASKELL_STYLE_PP_COMMENT = QE_STYLE_PREPROCESS,
    HASKELL_STYLE_STRING =   QE_STYLE_STRING,
    HASKELL_STYLE_NUMBER =   QE_STYLE_NUMBER,
    HASKELL_STYLE_KEYWORD =  QE_STYLE_KEYWORD,
    HASKELL_STYLE_FUNCTION = QE_STYLE_FUNCTION,
    HASKELL_STYLE_TYPE =     QE_STYLE_TYPE,
    HASKELL_STYLE_SYMBOL =   QE_STYLE_NUMBER,
};

enum {
    IN_HASKELL_COMMENT = 0x0F,
    IN_HASKELL_COMMENT_SHIFT = 0,
    IN_HASKELL_PP_COMMENT  = 0x10,  /* compiler directives {-# ... #-} */
    IN_HASKELL_STRING  = 0x20,
};

static inline int haskell_is_symbol(int c)
{
    return qe_findchar("!#$%&+./<=>?@\\^|-~:", c);
}

static void haskell_colorize_line(QEColorizeContext *cp,
                                  unsigned int *str, int n, ModeDef *syn)
{
    int i = 0, start = i, c, style = 0, sep = 0, level = 0, klen;
    int state = cp->colorize_state;
    char kbuf[64];

    if (state & IN_HASKELL_COMMENT)
        goto parse_comment;

    if (state & IN_HASKELL_STRING) {
        sep = '\"';
        state = 0;
        while (qe_isblank(str[i]))
            i++;
        if (str[i] == '\\')
            i++;
        goto parse_string;
    }

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '-':
            if (str[i] == '-' && !haskell_is_symbol(str[i + 1])) {
                i = n;
                style = HASKELL_STYLE_COMMENT;
                break;
            }
            goto parse_symbol;

        case '{':
            if (str[i] == '-') {
                state |= 1 << IN_HASKELL_COMMENT_SHIFT;
                i++;
                if (str[i] == '#') {
                    state |= IN_HASKELL_PP_COMMENT;
                    i++;
                }
            parse_comment:
                level = (state & IN_HASKELL_COMMENT) >> IN_HASKELL_COMMENT_SHIFT;
                style = HASKELL_STYLE_COMMENT;
                if (state & IN_HASKELL_PP_COMMENT)
                    style = HASKELL_STYLE_PP_COMMENT;
                while (i < n) {
                    c = str[i++];
                    if (c == '{' && str[i] == '-') {
                        level++;
                        i++;
                        continue;
                    }
                    if (c == '-' && str[i] == '}') {
                        i++;
                        level--;
                        if (level == 0) {
                            state &= ~IN_HASKELL_PP_COMMENT;
                            i++;
                            break;
                        }
                    }
                }
                state &= ~IN_HASKELL_COMMENT;
                state |= level << IN_HASKELL_COMMENT_SHIFT;
                break;
            }
            /* FALL THRU */
        case '}':
        case '(':
        case ')':
        case '[':
        case ']':
        case ',':
        case ';':
        case '`':
            /* special */
            continue;

        case '\'':
        case '\"':
            /* parse string const */
            sep = c;
        parse_string:
            while (i < n) {
                c = str[i++];
                if (c == '\\') {
                    if (i == n) {
                        if (sep == '\"') {
                            /* XXX: should ignore whitespace */
                            state |= IN_HASKELL_STRING;
                        }
                    } else
                    if (str[i] == '^' && i + 1 < n && str[i + 1] != (unsigned int)sep) {
                        i += 2;
                    } else {
                        i += 1;
                    }
                } else
                if (c == sep) {
                    state &= ~IN_HASKELL_STRING;
                    break;
                }
            }
            style = HASKELL_STYLE_STRING;
            break;

        default:
            if (qe_isdigit(c)) {
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
                        /* decimal floats require a digit after the '.' */
                        for (i += 2; qe_isdigit(str[i]); i++)
                            continue;
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
                }
                /* XXX: should detect malformed number constants */
                style = HASKELL_STYLE_NUMBER;
                break;
            }
            if (qe_isalpha_(c)) {
                for (klen = 0, i--; qe_isalnum_(str[i]) || str[i] == '\''; i++) {
                    if (klen < countof(kbuf) - 1)
                        kbuf[klen++] = str[i];
                }
                kbuf[klen] = '\0';

                if (strfind(syn->keywords, kbuf)) {
                    style = HASKELL_STYLE_KEYWORD;
                    break;
                }
                if (strfind(syn->types, kbuf)) {
                    style = HASKELL_STYLE_TYPE;
                    break;
                }
                if (check_fcall(str, i)) {
                    style = HASKELL_STYLE_FUNCTION;
                    break;
                }
                continue;
            }
        parse_symbol:
            if (haskell_is_symbol(c)) {
                for (; haskell_is_symbol(str[i]); i++)
                    continue;
                style = HASKELL_STYLE_SYMBOL;
                break;
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

static ModeDef haskell_mode = {
    .name = "Haskell",
    .extensions = "hs|haskell",
    .shell_handlers = "haskell",
    .keywords = haskell_keywords,
    .types = haskell_types,
    .colorize_func = haskell_colorize_line,
};

static int haskell_init(void)
{
    qe_register_mode(&haskell_mode, MODEF_SYNTAX);

    return 0;
}

/*---------------- Coffee coloring ----------------*/

static char const coffee_keywords[] = {
    // keywords common with Javascript:
    "true|false|null|this|new|delete|typeof|in|instanceof|"
    "return|throw|break|continue|debugger|yield|if|else|"
    "switch|for|while|do|try|catch|finally|class|extends|super|"
    // CoffeeScript only keywords:
    "undefined|then|unless|until|loop|of|by|when|"
    // aliasses
    "and|or|is|isnt|not|yes|no|on|off|"
    // reserved: should be flagged as errors
    "case|default|function|var|void|with|const|let|enum|export|import|"
    "native|implements|interface|package|private|protected|public|static|"
    // proscribed in strict mode
    "arguments|eval|yield*|"
};

enum {
    IN_COFFEE_STRING       = 0x100,
    IN_COFFEE_STRING2      = 0x200,
    IN_COFFEE_REGEX        = 0x400,
    IN_COFFEE_LONG_STRING  = 0x01,
    IN_COFFEE_LONG_STRING2 = 0x02,
    IN_COFFEE_LONG_REGEX   = 0x04,
    IN_COFFEE_REGEX_CCLASS = 0x08,
    IN_COFFEE_JSTOKEN      = 0x10,
    IN_COFFEE_LONG_COMMENT = 0x20,
};

enum {
    COFFEE_STYLE_TEXT =     QE_STYLE_DEFAULT,
    COFFEE_STYLE_COMMENT =  QE_STYLE_COMMENT,
    COFFEE_STYLE_STRING =   QE_STYLE_STRING,
    COFFEE_STYLE_REGEX =    QE_STYLE_STRING,
    COFFEE_STYLE_JSTOKEN =  QE_STYLE_STRING,
    COFFEE_STYLE_NUMBER =   QE_STYLE_NUMBER,
    COFFEE_STYLE_KEYWORD =  QE_STYLE_KEYWORD,
    COFFEE_STYLE_FUNCTION = QE_STYLE_FUNCTION,
    COFFEE_STYLE_ERROR =    QE_STYLE_ERROR,
};

static void coffee_colorize_line(QEColorizeContext *cp,
                                 unsigned int *str, int n, ModeDef *syn)
{
    int i = 0, start = i, c, style = 0, sep, prev, i1;
    int state = cp->colorize_state;
    char kbuf[64];

    if (state & IN_COFFEE_STRING) {
        sep = '\'';
        goto parse_string;
    }
    if (state & IN_COFFEE_STRING2) {
        sep = '\"';
        goto parse_string;
    }
    if (state & IN_COFFEE_REGEX) {
        goto parse_regex;
    }
    if (state & IN_COFFEE_LONG_STRING) {
        sep = '\'';
        goto parse_long_string;
    }
    if (state & IN_COFFEE_LONG_STRING2) {
        sep = '\"';
        goto parse_long_string;
    }
    if (state & IN_COFFEE_LONG_REGEX) {
        goto parse_regex;
    }
    if (state & IN_COFFEE_JSTOKEN) {
        goto parse_jstoken;
    }
    if (state & IN_COFFEE_LONG_COMMENT) {
        goto parse_long_comment;
    }

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            if (str[i] == '#' && str[i + 1] == '#') {
                /* multi-line block comments with ### */
                state = IN_COFFEE_LONG_COMMENT;
            parse_long_comment:
                while (i < n) {
                    c = str[i++];
                    if (c == '#' && str[i] == '#' && str[i + 1] == '#') {
                        i += 2;
                        state = 0;
                        break;
                    }
                }
            } else {
                i = n;
            }
            style = COFFEE_STYLE_COMMENT;
            break;

        case '\'':
        case '\"':
            /* parse string constant */
            i--;
            sep = str[i++];
            if (str[i] == (unsigned int)sep && str[i + 1] == (unsigned int)sep) {
                /* long string */
                state = (sep == '\"') ? IN_COFFEE_LONG_STRING2 :
                        IN_COFFEE_LONG_STRING;
                i += 2;
            parse_long_string:
                while (i < n) {
                    c = str[i++];
                    if (c == '\\') {
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
                state = (sep == '\"') ? IN_COFFEE_STRING2 : IN_COFFEE_STRING;
            parse_string:
                while (i < n) {
                    c = str[i++];
                    if (c == '\\') {
                        if (i < n) {
                            i += 1;
                        }
                    } else
                    if (c == sep) {
                        state = 0;
                        break;
                    }
                }
                if (state) {
                    state = 0;
                    // unterminated string literal, should flag unless
                    // point is at end of line.
                    style = COFFEE_STYLE_ERROR;
                    break;
                }
            }
            style = COFFEE_STYLE_STRING;
            break;

        case '`':
            /* parse multi-line JS token */
            state = IN_COFFEE_JSTOKEN;
        parse_jstoken:
            while (i < n) {
                c = str[i++];
                if (c == '\\') {
                    if (i < n) {
                        i += 1;
                    }
                } else
                if (c == '`') {
                    state = 0;
                    break;
                }
            }
            style = COFFEE_STYLE_JSTOKEN;
            break;

        case '.':
            if (qe_isdigit(str[i]))
                goto parse_decimal;
            if (str[i] == '.') /* .. range operator */
                i++;
            if (str[i] == '.') /* ... range operator */
                i++;
            continue;

        case '/':
            /* XXX: should use more context to tell regex from divide */
            if (str[i] == '/') {
                i++;
                if (str[i] == '/') {
                    /* multiline /// regex */
                    state = IN_COFFEE_LONG_REGEX;
                    i++;
                    goto parse_regex;
                } else {
                    /* floor divide // operator */
                    break;
                }
            }
            prev = ' ';
            for (i1 = start; i1 > 0; ) {
                prev = str[--i1] & CHAR_MASK;
                if (!qe_isblank(prev))
                    break;
            }
            if (qe_findchar(" [({},;=<>!~^&|*/%?:", prev)
            ||  qe_findchar("^\\?.[{},;<>!~&|*%:", str[i])
            ||  (str[i] == '=' && str[i + 1] == '/')
            ||  (str[i] == '(' && str[i + 1] == '?')
            ||  (str[i1] >> STYLE_SHIFT) == COFFEE_STYLE_KEYWORD
            ||  (str[i] != ' ' && (str[i] != '=' || str[i + 1] != ' ')
            &&   !(qe_isalnum(prev) || qe_findchar(")]}\"\'?:", prev)))) {
                state = IN_COFFEE_REGEX;
            parse_regex:
                style = COFFEE_STYLE_REGEX;
                while (i < n) {
                    c = str[i++];
                    if (c == '\\') {
                        if (i < n) {
                            i += 1;
                        }
                    } else
                    if (state & IN_COFFEE_REGEX_CCLASS) {
                        if (c == ']') {
                            state &= ~IN_COFFEE_REGEX_CCLASS;
                        }
                        /* ignore '/' inside char classes */
                    } else {
                        if (c == '[') {
                            state |= IN_COFFEE_REGEX_CCLASS;
                            if (str[i] == '^')
                                i++;
                            if (str[i] == ']')
                                i++;
                        } else
                        if (state & IN_COFFEE_LONG_REGEX) {
                            if (c == '/' && str[i] == '/' && str[i + 1] == '/') {
                                i += 2;
                                state = 0;
                                while (qe_isalpha(str[i]))
                                    i++;
                                break;
                            } else
                            if (qe_isblank(c) && str[i] == '#' && str[i+1] != '{') {
                                SET_COLOR(str, start, i, style);
                                start = i;
                                i = n;
                                style = COFFEE_STYLE_COMMENT;
                                break;
                            }
                        } else {
                            if (c == '/') {
                                state = 0;
                                while (qe_isalpha(str[i]))
                                    i++;
                                break;
                            }
                        }
                    }
                }
                if (state & ~IN_COFFEE_LONG_REGEX) {
                    state = 0;
                    // unterminated regex literal, should flag unless
                    // point is at end of line.
                    style = COFFEE_STYLE_ERROR;
                    break;
                }
                break;
            }
            continue;

        default:
            if (qe_isdigit(c)) {
                if (c == '0' && str[i] == 'b') {
                    /* binary numbers */
                    for (i += 1; qe_isbindigit(str[i]); i++)
                        continue;
                } else
                if (c == '0' && str[i] == 'o') {
                    /* octal numbers */
                    for (i += 1; qe_isoctdigit(str[i]); i++)
                        continue;
                } else
                if (c == '0' && str[i] == 'x') {
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
                    if (str[i] == 'e') {
                        int k = i + 1;
                        if (str[k] == '+' || str[k] == '-')
                            k++;
                        if (qe_isdigit(str[k])) {
                            for (i = k + 1; qe_isdigit(str[i]); i++)
                                continue;
                        }
                    }
                }

                /* XXX: should detect malformed number constants */
                style = COFFEE_STYLE_NUMBER;
                break;
            }
            if (qe_isalpha_(c)) {
                i += ustr_get_identifier(kbuf, countof(kbuf), c, str, i, n);
                if (strfind(syn->keywords, kbuf)) {
                    style = COFFEE_STYLE_KEYWORD;
                    break;
                }
                if (check_fcall(str, i)) {
                    style = COFFEE_STYLE_FUNCTION;
                    break;
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

static int coffee_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (match_extension(p->filename, mode->extensions)
    ||  match_shell_handler(cs8(p->buf), mode->shell_handlers)
    ||  stristart(p->filename, "Cakefile", NULL)) {
        return 80;
    }
    return 1;
}

static ModeDef coffee_mode = {
    .name = "CoffeeScript",
    .alt_name = "coffee",
    .extensions = "coffee",
    .shell_handlers = "coffee",
    .mode_probe = coffee_mode_probe,
    .keywords = coffee_keywords,
    .colorize_func = coffee_colorize_line,
};

static int coffee_init(void)
{
    qe_register_mode(&coffee_mode, MODEF_SYNTAX);

    return 0;
}

/*---------------- Python coloring ----------------*/

static char const python_keywords[] = {
    "|False|None|True|and|as|assert|break|class|continue"
    "|def|del|elif|else|except|finally|for|from|global"
    "|if|import|in|is|lambda|nonlocal|not|or|pass|raise"
    "|return|try|while|with|yield"
    "|"
};

enum {
    IN_PYTHON_COMMENT      = 0x80,
    IN_PYTHON_STRING       = 0x40,
    IN_PYTHON_STRING2      = 0x20,
    IN_PYTHON_LONG_STRING  = 0x10,
    IN_PYTHON_LONG_STRING2 = 0x08,
    IN_PYTHON_RAW_STRING   = 0x04,
};

enum {
    PYTHON_STYLE_TEXT =     QE_STYLE_DEFAULT,
    PYTHON_STYLE_COMMENT =  QE_STYLE_COMMENT,
    PYTHON_STYLE_STRING =   QE_STYLE_STRING,
    PYTHON_STYLE_NUMBER =   QE_STYLE_NUMBER,
    PYTHON_STYLE_KEYWORD =  QE_STYLE_KEYWORD,
    PYTHON_STYLE_FUNCTION = QE_STYLE_FUNCTION,
};

static void python_colorize_line(QEColorizeContext *cp,
                                 unsigned int *str, int n, ModeDef *syn)
{
    int i = 0, start = i, c, style = 0, sep, i1, tag = 0;
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
};

static int python_init(void)
{
    qe_register_mode(&python_mode, MODEF_SYNTAX);

    return 0;
}

/*---------------- Ruby script coloring ----------------*/

static char const ruby_keywords[] = {
    "|__ENCODING__|__END__|__FILE__|__LINE__"
    "|BEGIN|END|alias|and|assert|begin|break"
    "|call|case|catch|class|def|defined?|do"
    "|else|elsif|end|ensure|eval|exit|extend"
    "|false|for|if|in|include|lambda|lambda?|loop"
    "|module|new|next|nil|not|or|private|proc"
    "|raise|refute|require|rescue|retry|return"
    "|self|super|then|throw|true|unless|until"
    "|when|while|yield"
    "|"
};

/* Ruby operators:
 *  `  +  -  +@  -@  *  /  %  <<  >>  <  <=  >  >=  =
 *  ==  ===  <=>  []  []=  **  !  ~  !=  !~  =~  &  |  ^
 */

enum {
    IN_RUBY_HEREDOC   = 0x80,
    IN_RUBY_HD_INDENT = 0x40,
    IN_RUBY_HD_SIG    = 0x3f,
    IN_RUBY_COMMENT   = 0x40,
    IN_RUBY_STRING    = 0x20      /* single quote */,
    IN_RUBY_STRING2   = 0x10      /* double quote */,
    IN_RUBY_STRING3   = 0x08      /* back quote */,
    IN_RUBY_STRING4   = 0x04      /* %q{...} */,
    IN_RUBY_REGEX     = 0x02,
    IN_RUBY_POD       = 0x01,
};

enum {
    RUBY_STYLE_TEXT =     QE_STYLE_DEFAULT,
    RUBY_STYLE_COMMENT =  QE_STYLE_COMMENT,
    RUBY_STYLE_STRING =   QE_STYLE_STRING,
    RUBY_STYLE_STRING2 =  QE_STYLE_STRING,
    RUBY_STYLE_STRING3 =  QE_STYLE_STRING,
    RUBY_STYLE_STRING4 =  QE_STYLE_STRING,
    RUBY_STYLE_REGEX =    QE_STYLE_STRING_Q,
    RUBY_STYLE_NUMBER =   QE_STYLE_NUMBER,
    RUBY_STYLE_KEYWORD =  QE_STYLE_KEYWORD,
    RUBY_STYLE_FUNCTION = QE_STYLE_FUNCTION,
    RUBY_STYLE_MEMBER =   QE_STYLE_VARIABLE,
    RUBY_STYLE_HEREDOC =  QE_STYLE_PREPROCESS,
};

static int ruby_get_name(char *buf, int size, unsigned int *str)
{
    int len, i = 0, j;

    for (len = 0, j = i; qe_isalnum_(str[j]); j++) {
        if (len < size - 1)
            buf[len++] = str[j];
    }
    if (str[j] == '?' || str[j] == '!') {
        if (len < size - 1)
            buf[len++] = str[j];
        j++;
    }
    if (len < size) {
        buf[len] = '\0';
    }
    return j - i;
}

static void ruby_colorize_line(QEColorizeContext *cp,
                               unsigned int *str, int n, ModeDef *syn)
{
    int i = 0, j, start = i, c, style = 0, indent, sig;
    static int sep, sep0, level;        /* XXX: ugly patch */
    int state = cp->colorize_state;
    char kbuf[64];

    for (indent = 0; qe_isblank(str[indent]); indent++)
        continue;

    if (state & IN_RUBY_HEREDOC) {
        if (state & IN_RUBY_HD_INDENT) {
            while (qe_isblank(str[i]))
                i++;
        }
        sig = 0;
        if (qe_isalpha_(str[i])) {
            sig = str[i++] % 61;
            for (; qe_isalnum_(str[i]); i++) {
                sig = ((sig << 6) + str[i]) % 61;
            }
        }
        for (; qe_isblank(str[i]); i++)
            continue;
        i = n;
        SET_COLOR(str, start, i, RUBY_STYLE_HEREDOC);
        if (i > 0 && i == n && (state & IN_RUBY_HD_SIG) == (sig & IN_RUBY_HD_SIG))
            state &= ~(IN_RUBY_HEREDOC | IN_RUBY_HD_INDENT | IN_RUBY_HD_SIG);
    } else {
        if (state & IN_RUBY_COMMENT)
            goto parse_c_comment;

        if (state & IN_RUBY_REGEX)
            goto parse_regex;

        if (state & IN_RUBY_STRING)
            goto parse_string;

        if (state & IN_RUBY_STRING2)
            goto parse_string2;

        if (state & IN_RUBY_STRING3)
            goto parse_string3;

        if (state & IN_RUBY_STRING4)
            goto parse_string4;

        if (str[i] == '=' && qe_isalpha(str[i + 1])) {
            state |= IN_RUBY_POD;
        }
        if (state & IN_RUBY_POD) {
            if (ustrstart(str + i, "=end", NULL)) {
                state &= ~IN_RUBY_POD;
            }
            style = RUBY_STYLE_COMMENT;
            if (str[i] == '=' && qe_isalpha(str[i + 1]))
                style = RUBY_STYLE_KEYWORD;
            i = n;
            SET_COLOR(str, start, i, style);
        }
    }

    while (i < n && qe_isblank(str[i]))
        i++;

    indent = i;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '/':
            if (str[i] == '*') {
                /* C comment */
                i++;
            parse_c_comment:
                state = IN_RUBY_COMMENT;
                for (; i < n; i++) {
                    if (str[i] == '*' && str[i + 1] == '/') {
                        i += 2;
                        state &= ~IN_RUBY_COMMENT;
                        break;
                    }
                }
                goto comment;
            }
            if (start == indent
            ||  (str[i] != ' ' && str[i] != '='
            &&   i >= 2
            &&   !qe_isalnum(str[i - 2] & CHAR_MASK)
            &&   (str[i - 2] & CHAR_MASK) != ')')) {
                /* XXX: should use context to tell regex from divide */
                /* parse regex */
                state = IN_RUBY_REGEX;
            parse_regex:
                while (i < n) {
                    /* XXX: should ignore / inside char classes */
                    c = str[i++];
                    if (c == '\\') {
                        if (i < n) {
                            i += 1;
                        }
                    } else
                    if (c == '#' && str[i] == '{') {
                        /* should parse full syntax */
                        while (i < n && str[i++] != '}')
                            continue;
                    } else
                    if (c == '/') {
                        while (qe_findchar("ensuimox", str[i])) {
                            i++;
                        }
                        state = 0;
                        break;
                    }
                }
                style = RUBY_STYLE_REGEX;
                break;
            }
            continue;

        case '#':
            i = n;
        comment:
            style = RUBY_STYLE_COMMENT;
            break;

        case '%':
            /* parse alternate string/array syntaxes */
            if (str[i] != '\0' && !qe_isblank(str[i]) && !qe_isalnum(str[i]))
                goto has_string4;

            if (str[i] == 'q' || str[i] == 'Q'
            ||  str[i] == 'r' || str[i] == 'x'
            ||  str[i] == 'w' || str[i] == 'W') {
                i++;
            has_string4:
                level = 0;
                sep = sep0 = str[i++];
                if (sep == '{') sep = '}';
                if (sep == '(') sep = ')';
                if (sep == '[') sep = ']';
                if (sep == '<') sep = '>';
                /* parse special string const */
                state = IN_RUBY_STRING4;
            parse_string4:
                while (i < n) {
                    c = str[i++];
                    if (c == sep) {
                        if (level-- == 0) {
                            state = level = 0;
                            break;
                        }
                        /* XXX: should parse regex modifiers if %r */
                    } else
                    if (c == sep0) {
                        level++;
                    } else
                    if (c == '#' && str[i] == '{') {
                        /* XXX: should no parse if %q */
                        /* XXX: should parse full syntax */
                        while (i < n && str[i++] != '}')
                            continue;
                    } else
                    if (c == '\\') {
                        if (i < n) {
                            i += 1;
                        }
                    }
                }
                style = RUBY_STYLE_STRING4;
                break;
            }
            continue;

        case '\'':
            /* parse single quoted string const */
            state = IN_RUBY_STRING;
        parse_string:
            while (i < n) {
                c = str[i++];
                if (c == '\\' && (str[i] == '\\' || str[i] == '\'')) {
                    i += 1;
                } else
                if (c == '\'') {
                    state = 0;
                    break;
                }
            }
            style = RUBY_STYLE_STRING;
            break;

        case '`':
            /* parse single quoted string const */
            state = IN_RUBY_STRING3;
        parse_string3:
            while (i < n) {
                c = str[i++];
                if (c == '\\' && (str[i] == '\\' || str[i] == '\'')) {
                    i += 1;
                } else
                if (c == '#' && str[i] == '{') {
                    /* should parse full syntax */
                    while (i < n && str[i++] != '}')
                        continue;
                } else
                if (c == '`') {
                    state = 0;
                    break;
                }
            }
            style = RUBY_STYLE_STRING3;
            break;

        case '\"':
            /* parse double quoted string const */
        parse_string2:
            c = '\0';
            while (i < n) {
                c = str[i++];
                if (c == '\\') {
                    if (i < n) {
                        i += 1;
                    }
                } else
                if (c == '#' && str[i] == '{') {
                    /* should parse full syntax */
                    while (i < n && str[i++] != '}')
                        continue;
                } else
                if (c == '\"') {
                    break;
                }
            }
            if (c == '\"') {
                if (state == IN_RUBY_STRING2)
                    state = 0;
            } else {
                if (state == 0)
                    state = IN_RUBY_STRING2;
            }
            style = RUBY_STYLE_STRING2;
            break;

        case '<':
            if (str[i] == '<') {
                /* XXX: should use context to tell lshift from heredoc:
                 * here documents are introduced by monadic <<.
                 * Monadic use could be detected in some contexts, such
                 * as eval(<<EOS), but not in the general case.
                 * We use a heuristical approach: let's assume here
                 * document ids are not separated from the << by white
                 * space.
                 * XXX: should parse full here document syntax.
                 */
                sig = 0;
                j = i + 1;
                if (str[j] == '-') {
                    j++;
                }
                if ((str[j] == '\'' || str[j] == '\"')
                &&  qe_isalpha_(str[j + 1])) {
                    sep = str[j++];
                    sig = str[j++] % 61;
                    for (; qe_isalnum_(str[j]); j++) {
                        sig = ((sig << 6) + str[j]) % 61;
                    }
                    if (str[j++] != (unsigned int)sep)
                        break;
                } else
                if (qe_isalpha_(str[j])) {
                    sig = str[j++] % 61;
                    for (; qe_isalnum_(str[j]); j++) {
                        sig = ((sig << 6) + str[j]) % 61;
                    }
                }
                if (sig) {
                    /* Multiple here documents can be specified on the
                     * same line, only the last one will prevail, which
                     * is OK for coloring purposes.
                     * state will be cleared if a string or a comment
                     * start on the line after the << operator.  This
                     * is a bug due to limited state bits.
                     */
                    state &= ~(IN_RUBY_HEREDOC | IN_RUBY_HD_INDENT | IN_RUBY_HD_SIG);
                    state |= IN_RUBY_HEREDOC;
                    if (str[i + 1] == '-') {
                        state |= IN_RUBY_HD_INDENT;
                    }
                    state |= (sig & IN_RUBY_HD_SIG);
                    i = j;
                    style = RUBY_STYLE_HEREDOC;
                    break;
                }
            }
            continue;

        case '?':
            /* XXX: should parse character constants */
            continue;

        case '.':
            if (qe_isdigit_(str[i]))
                goto parse_decimal;
            continue;

        case '$':
            /* XXX: should parse precise $ syntax,
             * skip $" and $' for now
             */
            if (i < n)
                i++;
            continue;

        case ':':
            /* XXX: should parse Ruby symbol */
            continue;

        case '@':
            i += ruby_get_name(kbuf, countof(kbuf), str + i);
            style = RUBY_STYLE_MEMBER;
            break;

        default:
            if (qe_isdigit(c)) {
                if (c == '0' && qe_tolower(str[i]) == 'b') {
                    /* binary numbers */
                    for (i += 1; qe_isbindigit_(str[i]); i++)
                        continue;
                } else
                if (c == '0' && qe_tolower(str[i]) == 'o') {
                    /* octal numbers */
                    for (i += 1; qe_isoctdigit_(str[i]); i++)
                        continue;
                } else
                if (c == '0' && qe_tolower(str[i]) == 'x') {
                    /* hexadecimal numbers */
                    for (i += 1; qe_isxdigit_(str[i]); i++)
                        continue;
                } else
                if (c == '0' && qe_tolower(str[i]) == 'd') {
                    /* hexadecimal numbers */
                    for (i += 1; qe_isdigit_(str[i]); i++)
                        continue;
                } else {
                    /* decimal numbers */
                    for (; qe_isdigit_(str[i]); i++)
                        continue;
                    if (str[i] == '.') {
                        i++;
                    parse_decimal:
                        for (; qe_isdigit_(str[i]); i++)
                            continue;
                    }
                    if (qe_tolower(str[i]) == 'e') {
                        int k = i + 1;
                        if (str[k] == '+' || str[k] == '-')
                            k++;
                        if (qe_isdigit_(str[k])) {
                            for (i = k + 1; qe_isdigit_(str[i]); i++)
                                continue;
                        }
                    }
                }
                /* XXX: should detect malformed number constants */
                style = RUBY_STYLE_NUMBER;
                break;
            }
            if (qe_isalpha_(c)) {
                i--;
                i += ruby_get_name(kbuf, countof(kbuf), str + i);

                if (strfind(syn->keywords, kbuf)) {
                    style = RUBY_STYLE_KEYWORD;
                    break;
                }
                if (qe_isblank(str[i]))
                    i++;
                if (str[i] == '(' || str[i] == '{') {
                    style = RUBY_STYLE_FUNCTION;
                    break;
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

static int ruby_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (match_extension(p->filename, mode->extensions)
    ||  match_shell_handler(cs8(p->buf), mode->shell_handlers)
    ||  stristart(p->filename, "Rakefile", NULL)) {
        return 80;
    }
    return 1;
}

static ModeDef ruby_mode = {
    .name = "Ruby",
    .extensions = "rb|gemspec",
    .shell_handlers = "ruby",
    .mode_probe = ruby_mode_probe,
    .keywords = ruby_keywords,
    .colorize_func = ruby_colorize_line,
};

static int ruby_init(void)
{
    qe_register_mode(&ruby_mode, MODEF_SYNTAX);

    return 0;
}

/*---------------- Erlang coloring ----------------*/

static char const erlang_keywords[] = {
    "|after|and|andalso|band|begin|bnot|bor|bsl|bsr|bxor|case|catch|cond"
    "|div|end|fun|if|let|not|of|or|orelse|receive|rem|try|when|xor"
    "|true|false|nil|_"
    "|"
};

static char const erlang_commands[] = {
    "|module|compile|define|export|import|vsn|on_load|record|include|file"
    "|mode|author|include_lib|behaviour"
    "|type|opaque|spec|callback|export_type"
    "|ifdef|ifndef|undef|else|endif"
    "|"
};

static char const erlang_types[] = {
    "|"
};

enum {
    IN_ERLANG_STRING   = 0x01,
};

enum {
    ERLANG_STYLE_TEXT       = QE_STYLE_DEFAULT,
    ERLANG_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    ERLANG_STYLE_COMMENT    = QE_STYLE_COMMENT,
    ERLANG_STYLE_STRING     = QE_STYLE_STRING,
    ERLANG_STYLE_CHARCONST  = QE_STYLE_STRING,
    ERLANG_STYLE_ATOM       = QE_STYLE_DEFAULT,
    ERLANG_STYLE_INTEGER    = QE_STYLE_NUMBER,
    ERLANG_STYLE_FLOAT      = QE_STYLE_NUMBER,
    ERLANG_STYLE_KEYWORD    = QE_STYLE_KEYWORD,
    ERLANG_STYLE_TYPE       = QE_STYLE_TYPE,
    ERLANG_STYLE_IDENTIFIER = QE_STYLE_DEFAULT,
    ERLANG_STYLE_FUNCTION   = QE_STYLE_FUNCTION,
};

static inline int qe_basedigit(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 255;
}

static int erlang_match_char(unsigned int *str, int i)
{
    /* erlang character constant */
    if (str[i++] == '\\') {
        switch (str[i++]) {
        case '0': case '1': case '2': case '3':
        case '4': case '5': case '6': case '7':
            if (qe_isoctdigit(str[i])) i++;
            if (qe_isoctdigit(str[i])) i++;
            break;
        case 'x':
        case 'X':
            if (str[i] == '{') {
                for (i++; qe_isxdigit(str[i]); i++)
                    continue;
                if (str[i] == '}')
                    i++;
                break;
            }
            if (qe_isxdigit(str[i])) i++;
            if (qe_isxdigit(str[i])) i++;
            break;
        case '^':
            if (qe_isalpha(str[i])) i++;
            break;
        case 'b': /* backspace (8) */
        case 'd': /* delete (127) */
        case 'e': /* escape (27) */
        case 'f': /* formfeed (12) */
        case 'n': /* newline (10) */
        case 'r': /* return (13) */
        case 's': /* space (32) */
        case 't': /* tab (9) */
        case 'v': /* vtab (?) */
        case '\'': /* single quote */
        case '\"': /* double quote */
        case '\\': /* backslash */
            break;
        default:
            break;
        }
    }
    return i;
}

static void erlang_colorize_line(QEColorizeContext *cp,
                                unsigned int *str, int n, ModeDef *syn)
{
    char keyword[MAX_KEYWORD_SIZE];
    int i = 0, start = i, c, style, len, base;
    int colstate = cp->colorize_state;

    if (colstate & IN_ERLANG_STRING)
        goto parse_string;

    if (str[i] == '#' && str[i + 1] == '!') {
        /* Handle shbang script heading ^#!.+
         * and preprocessor # line directives
         */
        i = n;
        SET_COLOR(str, start, i, ERLANG_STYLE_PREPROCESS);
    }

    while (i < n) {
        start = i;
        style = ERLANG_STYLE_TEXT;
        c = str[i++];
        switch (c) {
        case '%':
            i = n;
            style = ERLANG_STYLE_COMMENT;
            SET_COLOR(str, start, i, style);
            continue;
        case '$':
            i = erlang_match_char(str, i);
            style = ERLANG_STYLE_CHARCONST;
            SET_COLOR(str, start, i, style);
            continue;

        case '\"':
            colstate = IN_ERLANG_STRING;
        parse_string:
            /* parse string */
            style = ERLANG_STYLE_STRING;
            while (i < n) {
                c = str[i++];
                if (c == '\\' && i < n)
                    i++;
                else
                if (c == '\"') {
                    colstate = 0;
                    break;
                }
            }
            SET_COLOR(str, start, i, style);
            continue;
        case '\'':
            /* parse an Erlang atom */
            style = ERLANG_STYLE_ATOM;
            while (i < n) {
                c = str[i++];
                if (c == '\\' && i < n)
                    i++;
                else
                if (c == '\'') {
                    break;
                }
            }
            SET_COLOR(str, start, i, style);
            continue;
        default:
            break;
        }
        if (qe_isdigit(c)) {
            /* parse numbers */
            style = ERLANG_STYLE_INTEGER;
            base = c - '0';
            while (qe_isdigit(str[i])) {
                base = base * 10 + str[i++] - '0';
            }
            if (base >= 2 && base <= 36 && str[i] == '#') {
                for (i += 1; qe_basedigit(str[i]) < base; i++)
                    continue;
                if (str[i - 1] == '#')
                    i--;
            } else {
                /* float: [0-9]+(.[0-9]+])?([eE][-+]?[0-9]+)? */
                if (str[i] == '.' && qe_isdigit(str[i + 1])) {
                    style = ERLANG_STYLE_FLOAT;
                    for (i += 2; qe_isdigit(str[i]); i++)
                        continue;
                }
                if (qe_tolower(str[i]) == 'e') {
                    int k = i + 1;
                    if (str[k] == '+' || str[k] == '-')
                        k++;
                    if (qe_isdigit(str[k])) {
                        style = ERLANG_STYLE_FLOAT;
                        for (i = k + 1; qe_isdigit(str[i]); i++)
                            continue;
                    }
                }
            }
            SET_COLOR(str, start, i, style);
            continue;
        }
        if (qe_isalpha_(c) || c == '@') {
            /* parse an Erlang atom or identifier */
            len = 0;
            keyword[len++] = c;
            for (; qe_isalnum_(str[i]) || str[i] == '@'; i++) {
                if (len < countof(keyword) - 1)
                    keyword[len++] = str[i];
            }
            keyword[len] = '\0';
            if (start && str[start - 1] == '-'
            &&  strfind(erlang_commands, keyword)) {
                style = ERLANG_STYLE_PREPROCESS;
            } else
            if (strfind(syn->types, keyword)) {
                style = ERLANG_STYLE_TYPE;
            } else
            if (strfind(syn->keywords, keyword)) {
                style = ERLANG_STYLE_KEYWORD;
            } else
            if (check_fcall(str, i)) {
                style = ERLANG_STYLE_FUNCTION;
            } else
            if (qe_islower(keyword[0])) {
                style = ERLANG_STYLE_ATOM;
            } else {
                style = ERLANG_STYLE_IDENTIFIER;
            }
            SET_COLOR(str, start, i, style);
            continue;
        }
    }
    cp->colorize_state = colstate;
}

static int erlang_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (match_extension(p->filename, mode->extensions)
    ||  match_shell_handler(cs8(p->buf), mode->shell_handlers)
    ||  strstr(cs8(p->buf), "-*- erlang -*-")) {
        return 80;
    }
    return 1;
}

static ModeDef erlang_mode = {
    .name = "Erlang",
    .extensions = "erl|hrl",
    .shell_handlers = "erlang",
    .mode_probe = erlang_mode_probe,
    .keywords = erlang_keywords,
    .types = erlang_types,
    .colorize_func = erlang_colorize_line,
};

static int erlang_init(void)
{
    qe_register_mode(&erlang_mode, MODEF_SYNTAX);

    return 0;
}

/*---------------- Elixir coloring ----------------*/

static char const elixir_keywords[] = {
    "|do|end|cond|case|if|else|after|for|unless|when|quote|in"
    "|try|catch|rescue|raise"
    "|def|defp|defmodule|defcallback|defmacro|defsequence"
    "|defmacrop|defdelegate|defstruct|defexception|defimpl"
    "|require|alias|import|use|fn"
    "|setup|test|assert|refute|using"
    "|true|false|nil|and|or|not|_"
    "|"
};

static char const elixir_delim1[] = "\'\"/|([{<";
static char const elixir_delim2[] = "\'\"/|)]}>";

enum {
    IN_ELIXIR_DELIM  = 0x0F,
    IN_ELIXIR_STRING = 0x10,
    IN_ELIXIR_REGEX  = 0x20,
    IN_ELIXIR_TRIPLE = 0x40,
};

enum {
    ELIXIR_STYLE_TEXT =       QE_STYLE_DEFAULT,
    ELIXIR_STYLE_COMMENT =    QE_STYLE_COMMENT,
    ELIXIR_STYLE_CHARCONST =  QE_STYLE_STRING,
    ELIXIR_STYLE_STRING =     QE_STYLE_STRING,
    ELIXIR_STYLE_HEREDOC =    QE_STYLE_STRING,
    ELIXIR_STYLE_REGEX =      QE_STYLE_STRING,
    ELIXIR_STYLE_NUMBER =     QE_STYLE_NUMBER,
    ELIXIR_STYLE_KEYWORD =    QE_STYLE_KEYWORD,
    ELIXIR_STYLE_ATOM =       QE_STYLE_TYPE,
    ELIXIR_STYLE_TAG =        QE_STYLE_VARIABLE,
    ELIXIR_STYLE_FUNCTION =   QE_STYLE_FUNCTION,
    ELIXIR_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
};

static void elixir_colorize_line(QEColorizeContext *cp,
                                 unsigned int *str, int n, ModeDef *syn)
{
    int i = 0, start = i, c, style = 0, sep, klen, nc, has_under;
    int state = cp->colorize_state;
    char kbuf[64];

    if (state & IN_ELIXIR_STRING)
        goto parse_string;
    if (state & IN_ELIXIR_REGEX)
        goto parse_regex;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            i = n;
            style = ELIXIR_STYLE_COMMENT;
            break;

        case '?':
            i = erlang_match_char(str, i);
            style = ELIXIR_STYLE_CHARCONST;
            break;

        case '~':
            if (qe_tolower(str[i]) == 'r') {
                nc = qe_indexof(elixir_delim1, str[i + 1]);
                if (nc >= 0) {
                    i += 2;
                    state = IN_ELIXIR_REGEX | nc;
                    if (nc < 2) { /* '\'' or '\"' */
                        if (str[i + 0] == (unsigned int)c
                        &&  str[i + 1] == (unsigned int)c) {
                            state |= IN_ELIXIR_TRIPLE;
                            i += 2;
                        }
                    }
                parse_regex:
                    sep = elixir_delim2[state & 15];
                    /* parse regular expression */
                    while (i < n) {
                        if ((c = str[i++]) == '\\') {
                            if (i < n)
                                i += 1;
                            continue;
                        }
                        if (c == sep) {
                            if (!(state & IN_ELIXIR_TRIPLE)) {
                                state = 0;
                                break;
                            }
                            if (str[i] == (unsigned int)sep
                            &&  str[i + 1] == (unsigned int)sep) {
                                i += 2;
                                state = 0;
                                break;
                            }
                        }
                    }
                    while (qe_islower(str[i])) {
                        /* regex suffix */
                        i++;
                    }
                    style = ELIXIR_STYLE_REGEX;
                    break;
                }
            }
            continue;

        case '\'':
        case '\"':
            /* parse string constants and here documents */
            state = IN_ELIXIR_STRING | (c == '\"');
            if (str[i + 0] == (unsigned int)c
            &&  str[i + 1] == (unsigned int)c) {
                /* here documents */
                state |= IN_ELIXIR_TRIPLE;
                i += 2;
            }
        parse_string:
            sep = elixir_delim2[state & 15];
            style = (state & IN_ELIXIR_TRIPLE) ?
                ELIXIR_STYLE_HEREDOC : ELIXIR_STYLE_STRING;
            while (i < n) {
                c = str[i++];
                if (c == '\\') {
                    if (i < n)
                        i += 1;
                    continue;
                }
                /* XXX: should colorize <% %> expressions and interpolation */
                if (c == sep) {
                    if (!(state & IN_ELIXIR_TRIPLE)) {
                        state = 0;
                        break;
                    }
                    if (str[i] == (unsigned int)sep
                    &&  str[i + 1] == (unsigned int)sep) {
                        i += 2;
                        state = 0;
                        break;
                    }
                }
            }
            break;

        case '@':
        case ':':
            if (qe_isalpha(str[i]))
                goto has_alpha;
            continue;

        case '<':
            if (str[i] == '%') {
                i++;
                if (str[i] == '=')
                    i++;
                style = ELIXIR_STYLE_PREPROCESS;
                break;
            }
            continue;

        case '%':
            if (str[i] == '>') {
                i++;
                style = ELIXIR_STYLE_PREPROCESS;
                break;
            }
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
                    for (has_under = 0;; i++) {
                        if (qe_isdigit(str[i]))
                            continue;
                        if (str[i] == '_' && qe_isdigit(str[i + 1])) {
                            /* integers may contain embedded _ characters */
                            has_under = 1;
                            i++;
                            continue;
                        }
                        break;
                    }
                    if (!has_under && str[i] == '.' && qe_isdigit(str[i + 1])) {
                        i += 2;
                        /* decimal floats require a digit after the '.' */
                        for (; qe_isdigit(str[i]); i++)
                            continue;
                        /* exponent notation requires a decimal point */
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
                }
                style = ELIXIR_STYLE_NUMBER;
                break;
            }
            if (qe_isalpha_(c)) {
        has_alpha:
                klen = 0;
                kbuf[klen++] = c;
                for (; qe_isalnum_(c = str[i]); i++) {
                    if (klen < countof(kbuf) - 1)
                        kbuf[klen++] = c;
                }
                if (c == '!' || c == '?') {
                    i++;
                    if (klen < countof(kbuf) - 1)
                        kbuf[klen++] = c;
                }
                kbuf[klen] = '\0';

                style = 0;
                if (kbuf[0] == '@') {
                    style = ELIXIR_STYLE_PREPROCESS;
                } else
                if (kbuf[0] == ':') {
                    style = ELIXIR_STYLE_ATOM;
                } else
                if (strfind(syn->keywords, kbuf)) {
                    style = ELIXIR_STYLE_KEYWORD;
                } else
                if (c == ':') {
                    style = ELIXIR_STYLE_TAG;
                } else
                if (check_fcall(str, i)) {
                    style = ELIXIR_STYLE_FUNCTION;
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
    cp->colorize_state = state;
}

static ModeDef elixir_mode = {
    .name = "Elixir",
    .extensions = "ex|exs",
    .shell_handlers = "elixir",
    .keywords = elixir_keywords,
    .colorize_func = elixir_colorize_line,
};

static int elixir_init(void)
{
    qe_register_mode(&elixir_mode, MODEF_SYNTAX);

    return 0;
}

/*---------------- ML/Ocaml coloring ----------------*/

static char const ocaml_keywords[] = {
    "|_|and|as|asr|assert|begin|class|constraint|do|done|downto"
    "|else|end|exception|external|false|for|fun|function|functor"
    "|if|ignore|in|include|incr|inherit|initializer"
    "|land|lazy|let|lnot|loop|lor|lsl|lsr|lxor"
    "|match|method|mod|module|mutable|new|not|object|of|open|or"
    "|parser|prec|private|raise|rec|ref|self|sig|struct"
    "|then|to|true|try|type|val|value|virtual|when|while|with"
    "|"
};

static char const ocaml_types[] = {
    "|array|bool|char|exn|float|format|format4||int|int32|int64"
    "|lazy_t|list|nativeint|option|string|unit"
    "|"
};

enum {
    IN_OCAML_COMMENT  = 0x01,
    IN_OCAML_STRING   = 0x02,
};

enum {
    OCAML_STYLE_TEXT       = QE_STYLE_DEFAULT,
    OCAML_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    OCAML_STYLE_COMMENT    = QE_STYLE_COMMENT,
    OCAML_STYLE_STRING     = QE_STYLE_STRING,
    OCAML_STYLE_STRING1    = QE_STYLE_STRING,
    OCAML_STYLE_NUMBER     = QE_STYLE_NUMBER,
    OCAML_STYLE_KEYWORD    = QE_STYLE_KEYWORD,
    OCAML_STYLE_TYPE       = QE_STYLE_TYPE,
    OCAML_STYLE_IDENTIFIER = QE_STYLE_DEFAULT,
    OCAML_STYLE_FUNCTION   = QE_STYLE_FUNCTION,
};

static void ocaml_colorize_line(QEColorizeContext *cp,
                                unsigned int *str, int n, ModeDef *syn)
{
    char keyword[MAX_KEYWORD_SIZE];
    int i = 0, start = i, c, k, style, len;
    int colstate = cp->colorize_state;

    if (colstate & IN_OCAML_COMMENT)
        goto parse_comment;

    if (colstate & IN_OCAML_STRING)
        goto parse_string;

    if (str[i] == '#') {
        /* Handle shbang script heading ^#!.+
         * and preprocessor # line directives
         */
        i = n;
        SET_COLOR(str, start, i, OCAML_STYLE_PREPROCESS);
    }

    while (i < n) {
        start = i;
        style = OCAML_STYLE_TEXT;
        c = str[i++];
        switch (c) {
        case '(':
            /* check for comment */
            if (str[i] != '*')
                break;

            /* regular comment (recursive?) */
            colstate = IN_OCAML_COMMENT;
            i++;
        parse_comment:
            style = OCAML_STYLE_COMMENT;
            for (; i < n; i++) {
                if (str[i] == '*' && str[i + 1] == ')') {
                    i += 2;
                    colstate = 0;
                    break;
                }
            }
            SET_COLOR(str, start, i, style);
            continue;
        case '\"':
            colstate = IN_OCAML_STRING;
        parse_string:
            /* parse string */
            style = OCAML_STYLE_STRING;
            while (i < n) {
                c = str[i++];
                if (c == '\\' && i < n)
                    i++;
                else
                if (c == '\"') {
                    colstate = 0;
                    break;
                }
            }
            SET_COLOR(str, start, i, style);
            continue;
        case '\'':
            /* parse type atom or char const */
            if ((i + 1 < n && str[i] != '\\' && str[i + 1] == '\'')
            ||  (i + 2 < n && str[i] == '\\' && str[i + 2] == '\'')
            ||  (str[i] == '\\' && str[i + 1] == 'x' &&
                 qe_isxdigit(str[i + 2]) && qe_isxdigit(str[i + 3]) &&
                 str[i + 4] == '\'')
            ||  (str[i] == '\\' && qe_isdigit(str[i + 1]) &&
                 qe_isdigit(str[i + 2]) && qe_isdigit(str[i + 3]) &&
                 str[i + 4] == '\'')) {
                style = OCAML_STYLE_STRING1;
                while (str[i++] != '\'')
                    continue;
            } else
            if (qe_isalpha_(str[i])) {
                while (i < n && (qe_isalnum_(str[i]) || str[i] == '\''))
                    i++;
                style = OCAML_STYLE_TYPE;
            }
            SET_COLOR(str, start, i, style);
            continue;
        default:
            break;
        }
        /* parse numbers */
        if (qe_isdigit(c)) {
            style = OCAML_STYLE_NUMBER;
            if (c == '0' && qe_tolower(str[i]) == 'o'
            &&  qe_isoctdigit(str[i + 1])) {
                /* octal int: 0[oO][0-7][0-7_]*[lLn]? */
                for (i += 1; qe_isoctdigit_(str[i]); i++)
                    continue;
                if (qe_findchar("lLn", str[i]))
                    i++;
            } else
            if (c == '0' && qe_tolower(str[i]) == 'x'
            &&  qe_isxdigit(str[i + 1])) {
                /* hex int: 0[xX][0-9a-fA-F][0-9a-zA-Z_]*[lLn]? */
                for (i += 1; qe_isxdigit(str[i]); i++)
                    continue;
                if (qe_findchar("lLn", str[i]))
                    i++;
            } else
            if (c == '0' && qe_tolower(str[i]) == 'b'
            &&  qe_isbindigit(str[i + 1])) {
                /* binary int: 0[bB][01][01_]*[lLn]? */
                for (i += 1; qe_isbindigit_(str[i]); i++)
                    continue;
                if (qe_findchar("lLn", str[i]))
                    i++;
            } else {
                /* decimal integer: [0-9][0-9_]*[lLn]? */
                for (; qe_isdigit_(str[i]); i++)
                    continue;
                if (qe_findchar("lLn", str[i])) {
                    i++;
                } else {
                    /* float:
                     * [0-9][0-9_]*(.[0-9_]*])?([eE][-+]?[0-9][0-9_]*)? */
                    if (str[i] == '.') {
                        for (i += 1; qe_isdigit_(str[i]); i++)
                            continue;
                    }
                    if (qe_tolower(str[i]) == 'e') {
                        int k = i + 1;
                        if (str[k] == '+' || str[k] == '-')
                            k++;
                        if (qe_isdigit(str[k])) {
                            for (i = k + 1; qe_isdigit_(str[i]); i++)
                                continue;
                        }
                    }
                }
            }
            SET_COLOR(str, start, i, style);
            continue;
        }
        /* parse identifiers and keywords */
        if (qe_isalpha_(c)) {
            len = 0;
            keyword[len++] = c;
            for (; qe_isalnum_(str[i]) || str[i] == '\''; i++) {
                if (len < countof(keyword) - 1)
                    keyword[len++] = str[i];
            }
            keyword[len] = '\0';
            if (strfind(syn->types, keyword)) {
                style = OCAML_STYLE_TYPE;
            } else
            if (strfind(syn->keywords, keyword)) {
                style = OCAML_STYLE_KEYWORD;
            } else {
                style = OCAML_STYLE_IDENTIFIER;
                k = i;
                if (qe_isblank(str[k]))
                    k++;
                if (str[k] == '(' && str[k + 1] != '*')
                    style = OCAML_STYLE_FUNCTION;
            }
            SET_COLOR(str, start, i, style);
            continue;
        }
    }
    cp->colorize_state = colstate;
}

static ModeDef ocaml_mode = {
    .name = "Ocaml",
    .extensions = "ml|mli|mll|mly",
    .shell_handlers = "ocaml",
    .keywords = ocaml_keywords,
    .types = ocaml_types,
    .colorize_func = ocaml_colorize_line,
};

static int ocaml_init(void)
{
    qe_register_mode(&ocaml_mode, MODEF_SYNTAX);

    return 0;
}

/*---------------- Eff language coloring ----------------*/

static char const eff_keywords[] = {
    // eff-keywords
    "and|as|begin|check|do|done|downto|else|end|effect|external|finally|for|"
    "fun|function|handle|handler|if|in|match|let|new|of|operation|rec|val|"
    "while|to|type|then|with|"
    // eff-constants
    "asr|false|mod|land|lor|lsl|lsr|lxor|or|true|"
    // other
    "ref|try|raise|"
    // directives
    "help|reset|quit|use|"
};

static char const eff_types[] = {
    "empty|bool|float|double|int|exception|string|map|range|unit|"
};

static ModeDef eff_mode = {
    .name = "Eff",
    .extensions = "eff",
    .shell_handlers = "eff",
    .keywords = eff_keywords,
    .types = eff_types,
    .colorize_func = ocaml_colorize_line,
};

static int eff_init(void)
{
    qe_register_mode(&eff_mode, MODEF_SYNTAX);

    return 0;
}

/*---------------- EMF (JASSPA microemacs macro files) ----------------*/

static char const emf_keywords[] = {
    "define-macro|!emacro|!if|!elif|!else|!endif|!while|!done|"
    "!repeat|!until|!force|!return|!abort|!goto|!jump|!bell|"
};

static char const emf_types[] = {
    "|"
};

enum {
    EMF_STYLE_TEXT =       QE_STYLE_DEFAULT,
    EMF_STYLE_COMMENT =    QE_STYLE_COMMENT,
    EMF_STYLE_STRING =     QE_STYLE_STRING,
    EMF_STYLE_KEYWORD =    QE_STYLE_KEYWORD,
    EMF_STYLE_TYPE =       QE_STYLE_TYPE,
    EMF_STYLE_FUNCTION =   QE_STYLE_FUNCTION,
    EMF_STYLE_NUMBER =     QE_STYLE_NUMBER,
    EMF_STYLE_VARIABLE =   QE_STYLE_VARIABLE,
    EMF_STYLE_IDENTIFIER = QE_STYLE_DEFAULT,
    EMF_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
};

static void emf_colorize_line(QEColorizeContext *cp,
                              unsigned int *str, int n, ModeDef *syn)
{
    char keyword[MAX_KEYWORD_SIZE];
    int i = 0, start, c, nw = 1, len, style;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '-':
            if (qe_isdigit(str[i]))
                goto number;
            break;
        case ';':
            i = n;
            SET_COLOR(str, start, i, EMF_STYLE_COMMENT);
            continue;
        case '\"':
            /* parse string const */
            while (i < n) {
                if (str[i] == '\\' && i + 1 < n) {
                    i += 2; /* skip escaped char */
                    continue;
                }
                if (str[i++] == '\"')
                    break;
            }
            SET_COLOR(str, start, i, EMF_STYLE_STRING);
            continue;
        default:
            break;
        }
        /* parse numbers */
        if (qe_isdigit(c)) {
        number:
            for (; i < n; i++) {
                if (!qe_isalnum(str[i]))
                    break;
            }
            SET_COLOR(str, start, i, EMF_STYLE_NUMBER);
            continue;
        }
        /* parse identifiers and keywords */
        if (c == '$' || c == '!' || c == '#' || qe_isalpha_(c)) {
            len = 0;
            keyword[len++] = c;
            for (; qe_isalnum_(str[i]) || str[i] == '-'; i++) {
                if (len < countof(keyword) - 1)
                    keyword[len++] = str[i];
            }
            keyword[len] = '\0';
            if (c == '$' || c == '#') {
                style = EMF_STYLE_VARIABLE;
            } else
            if (strfind(syn->keywords, keyword)) {
                style = EMF_STYLE_KEYWORD;
            } else
            if (strfind(syn->types, keyword)) {
                style = EMF_STYLE_TYPE;
            } else
            if (nw++ == 1) {
                style = EMF_STYLE_FUNCTION;
            } else {
                style = EMF_STYLE_IDENTIFIER;
            }
            SET_COLOR(str, start, i, style);
            continue;
        }
    }
}

static ModeDef emf_mode = {
    .name = "emf",
    .extensions = "emf",
    .keywords = emf_keywords,
    .types = emf_types,
    .colorize_func = emf_colorize_line,
};

static int emf_init(void)
{
    qe_register_mode(&emf_mode, MODEF_SYNTAX);

    return 0;
}

/*---------------- AGENA script coloring ----------------*/

#define AGN_SHORTSTRINGDELIM " ,~[]{}();:#'=?&%$\xA7\\!^@<>|\r\n\t"

enum {
    IN_AGENA_COMMENT = 0x01,
    IN_AGENA_STRING1 = 0x02,
    IN_AGENA_STRING2 = 0x04,
};

enum {
    AGENA_STYLE_TEXT =       QE_STYLE_DEFAULT,
    AGENA_STYLE_COMMENT =    QE_STYLE_COMMENT,
    AGENA_STYLE_STRING =     QE_STYLE_STRING,
    AGENA_STYLE_NUMBER =     QE_STYLE_NUMBER,
    AGENA_STYLE_KEYWORD =    QE_STYLE_KEYWORD,
    AGENA_STYLE_TYPE =       QE_STYLE_TYPE,
    AGENA_STYLE_FUNCTION =   QE_STYLE_FUNCTION,
    AGENA_STYLE_IDENTIFIER = QE_STYLE_DEFAULT,
};

static char const agena_keywords[] = {
    /* abs alias and antilo2 antilog10 arccos arcsec arcsin arctan as
    assigned atendof bea bottom break by bye case catch char cis clear cls
    conjugate copy cos cosh cosxx create dec delete dict div do downto
    duplicate elif else end entier enum esac even exchange exp fail false fi
    filled first finite flip for from global if imag import in inc infinity
    insert int intersect into is join keys last left ln lngamma local lower
    minus mul nan nand nargs nor not numeric od of onsuccess or pop proc
    qmdev qsadd real redo reg relaunch replace restart return right rotate
    sadd seq shift sign signum sin sinc sinh size skip smul split sqrt
    subset tan tanh then to top trim true try type typeof unassigned
    undefined union unique until upper values when while xor xsubset
    yrt */

    /* keywords */
    "|alias|as|bottom|break|by|case|catch|clear|cls|create|dec|delete"
    "|dict|div|do|duplicate|elif|else|end|enum|epocs|esac|external|exchange"
    "|fi|for|from|if|import|inc|insert|into|is|keys|mul|nargs"
    "|od|of|onsuccess|pop|proc|quit|redo|reg|relaunch|return|rotate"
    "|scope|seq|skip|then|try|to|top|try|until|varargs"
    "|when|while|yrt"
    "|readlib"
    /* constants */
    "|infinity|nan|I"
    /* operators */
    "|or|xor|nor|and|nand|in|subset|xsubset|union|minus|intersect|atendof"
    "|split|shift|not"
    "|assigned|unassigned|size|type|typeof|left|right|filled|finite"
    "|"
};

static char const agena_types[] = {
    "|boolean|complex|lightuserdata|null|number|pair|register|procedure"
    "|sequence|set|string|table|thread|userdata"
    "|global|local|char|float|undefined|true|false|fail"
    "|"
};

static void agena_colorize_line(QEColorizeContext *cp,
                                unsigned int *str, int n, ModeDef *syn)
{
    char kbuf[MAX_KEYWORD_SIZE];
    int i = 0, start = i, c, style = 0, sep = 0;
    int state = cp->colorize_state;

    if (state & IN_AGENA_COMMENT)
        goto parse_block_comment;
    if (state & IN_AGENA_STRING1)
        goto parse_string1;
    if (state & IN_AGENA_STRING2)
        goto parse_string2;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            if (str[i] == '/') {
                /* block comment */
                i++;
            parse_block_comment:
                state |= IN_AGENA_COMMENT;
                for (; i < n; i++) {
                    if (str[i] == '/' && str[i + 1] == '#') {
                        i += 2;
                        state &= ~IN_AGENA_COMMENT;
                        break;
                    }
                }
            } else {
                i = n;
            }
            style = AGENA_STYLE_COMMENT;
            break;
        case '\"':
            state = IN_AGENA_STRING2;
        parse_string2:
            sep = '\"';
            goto parse_string;
        case '\'':
            state = IN_AGENA_STRING1;
        parse_string1:
            sep = '\'';
            /* parse string const */
        parse_string:
            for (; i < n; i++) {
                if (str[i] == '\\' && i + 1 < n) {
                    i++;
                    continue;
                }
                if (str[i] == (unsigned int)sep) {
                    state = 0;
                    i++;
                    break;
                }
            }
            style = AGENA_STYLE_STRING;
            break;
        case '`':   /* short string */
            while (i < n && !qe_findchar(AGN_SHORTSTRINGDELIM, str[i]))
                i++;
            style = AGENA_STYLE_IDENTIFIER;
            break;
        default:
            /* parse identifiers and keywords */
            if (qe_isalpha_(c)) {
                i += ustr_get_identifier(kbuf, countof(kbuf), c, str, i, n);
                if (strfind(syn->keywords, kbuf))
                    style = AGENA_STYLE_KEYWORD;
                else
                if (strfind(syn->types, kbuf))
                    style = AGENA_STYLE_TYPE;
                else
                if (check_fcall(str, i))
                    style = AGENA_STYLE_FUNCTION;
                else
                    style = AGENA_STYLE_IDENTIFIER;
                break;
            }
            if (qe_isdigit(c) || (c == '.' && qe_isdigit(str[i]))) {
                while (qe_isdigit_(str[i]) || str[i] == '\'' || str[i] == '.')
                    i++;
                if (qe_findchar("eE", str[i])) {
                    i++;
                    if (qe_findchar("+-", str[i]))
                        i++;
                }
                while (qe_isalnum(str[i]))
                    i++;
                style = AGENA_STYLE_NUMBER;
                break;
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

static ModeDef agena_mode = {
    .name = "Agena",
    .extensions = "agn",
    .keywords = agena_keywords,
    .types = agena_types,
    .colorize_func = agena_colorize_line,
};

static int agena_init(void)
{
    qe_register_mode(&agena_mode, MODEF_SYNTAX);

    return 0;
}

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
    char keyword[MAX_KEYWORD_SIZE];
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
                    while (qe_basedigit(str[i]) < value)
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
                               unsigned int *str, int n, ModeDef *syn)
{
    char keyword[MAX_KEYWORD_SIZE];
    int i = 0, start = i, c, style = 0, k, len, laststyle = 0, isnum;
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
                if (str[i++] == (unsigned int)c)
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
                    k = i;
                    if (qe_isblank(str[k]))
                        k++;
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
            SET_COLOR(str, start, i, style);
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

static int scad_init(void)
{
    qe_register_mode(&scad_mode, MODEF_SYNTAX);

    return 0;
}

/*---------------- Magpie coloring ----------------*/

static char const magpie_keywords[] = {
    "and|as|break|case|catch|defclass|def|do|else|end|fn|for|if|"
    "import|in|is|let|match|or|return|then|throw|var|val|while|with|"
    "not|native|namespace|class|struct|using|new|interface|"
    "get|set|shared|done|"
    "false|true|nothing|it|xor|"
};

enum {
    IN_MAGPIE_COMMENT   = 0x0F,     /* nested comment level */
    IN_MAGPIE_STRING    = 0x10,     /* double quote */
};

enum {
    MAGPIE_STYLE_TEXT =     QE_STYLE_DEFAULT,
    MAGPIE_STYLE_SHBANG =   QE_STYLE_PREPROCESS,
    MAGPIE_STYLE_COMMENT =  QE_STYLE_COMMENT,
    MAGPIE_STYLE_STRING =   QE_STYLE_STRING,
    MAGPIE_STYLE_CHAR =     QE_STYLE_STRING,
    MAGPIE_STYLE_NUMBER =   QE_STYLE_NUMBER,
    MAGPIE_STYLE_KEYWORD =  QE_STYLE_KEYWORD,
    MAGPIE_STYLE_TYPE =     QE_STYLE_TYPE,
    MAGPIE_STYLE_FUNCTION = QE_STYLE_FUNCTION,
};

static void magpie_colorize_line(QEColorizeContext *cp,
                                 unsigned int *str, int n, ModeDef *syn)
{
    int i = 0, start = i, c, style = 0, level;
    int state = cp->colorize_state;
    char kbuf[64];

    if ((level = state & IN_MAGPIE_COMMENT) != 0)
        goto parse_comment;

    if (state & IN_MAGPIE_STRING)
        goto parse_string;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            if (i == 1 && str[i] == '!') {
                i = n;
                style = MAGPIE_STYLE_SHBANG;
                break;
            }
            i = n;
            style = MAGPIE_STYLE_COMMENT;   /* probably incorrect */
            break;

        case '/':
            if (str[i] == '*') {
                /* nexted C block comments */
                i++;
                level = 1;
            parse_comment:
                for (; i < n; i++) {
                    if (str[i] == '*' && str[i + 1] == '/') {
                        i += 2;
                        if (--level == 0)
                            break;
                    } else
                    if (str[i] == '/' && str[i + 1] == '*') {
                        i += 2;
                        level++;
                    }
                }
                state &= ~IN_MAGPIE_COMMENT;
                state |= level & IN_MAGPIE_COMMENT;
                style = MAGPIE_STYLE_COMMENT;
                break;
            }
            if (str[i] == '/') {
                i = n;
                style = MAGPIE_STYLE_COMMENT;
                break;
            }            
            continue;

        case '\'':
            /* a single quoted character */
            while (i < n) {
                c = str[i++];
                if (c == '\\') {
                    if (i < n) {
                        i += 1;
                    }
                } else
                if (c == '\'') {
                    break;
                }
            }
            style = MAGPIE_STYLE_CHAR;
            break;

        case '\"':
            /* parse double quoted string const */
            state = IN_MAGPIE_STRING;
        parse_string:
            c = '\0';
            while (i < n) {
                c = str[i++];
                if (c == '\\') {
                    if (i < n) {
                        i += 1;
                    }
                } else
                if (c == '#' && str[i] == '{') {
                    /* should parse full syntax */
                    while (i < n && str[i++] != '}')
                        continue;
                } else
                if (c == '\"') {
                    state = 0;
                    break;
                }
            }
            style = MAGPIE_STYLE_STRING;
            break;

        case '.':
            if (qe_isdigit_(str[i]))
                goto parse_decimal;
            continue;

        default:
            if (qe_isdigit(c)) {
                /* decimal numbers */
                for (; qe_isdigit_(str[i]); i++)
                    continue;
                if (str[i] == '.') {
                    i++;
                parse_decimal:
                    for (; qe_isdigit_(str[i]); i++)
                        continue;
                }
                style = MAGPIE_STYLE_NUMBER;
                break;
            }
            if (qe_isalpha_(c)) {
                i += ustr_get_identifier(kbuf, countof(kbuf), c, str, i, n);
                if (strfind(syn->keywords, kbuf)) {
                    style = MAGPIE_STYLE_KEYWORD;
                    break;
                }
                if (qe_isblank(str[i]))
                    i++;
                if (str[i] == '(' || str[i] == '{') {
                    style = MAGPIE_STYLE_FUNCTION;
                    break;
                }
                if (qe_isupper(kbuf[0]) && (start == 0 || str[start-1] != '.')) {
                    /* Types are capitalized */
                    style = MAGPIE_STYLE_TYPE;
                    break;
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

static ModeDef magpie_mode = {
    .name = "Magpie",
    .extensions = "mag",
    .shell_handlers = "magpie",
    .keywords = magpie_keywords,
    .colorize_func = magpie_colorize_line,
};

static int magpie_init(void)
{
    qe_register_mode(&magpie_mode, MODEF_SYNTAX);

    return 0;
}

/*---- Giancarlo Niccolai's Falcon scripting language -----*/

static const char falcon_keywords[] = {
    "if|elif|else|end|switch|select|case|default|"
    "while|loop|for|break|continue|dropping|"
    "forfirst|formiddle|forlast|"
    "try|catch|raise|state|callable|launch|"
    "function|return|innerfunc|fself|"
    "object|self|provides|from|init|"
    "class|const|enum|global|static|"
    "export|load|import|as|"
    "directive|def|macro|"
    "to|and|or|not|in|notin|true|false|nil|"
};

enum {
    IN_FALCON_COMMENT   = 0x01,     /* nested comment level */
    IN_FALCON_STRING1   = 0x10,     /* single quote */
    IN_FALCON_STRING2   = 0x20,     /* double quote */
};

enum {
    FALCON_STYLE_TEXT =     QE_STYLE_DEFAULT,
    FALCON_STYLE_SHBANG =   QE_STYLE_PREPROCESS,
    FALCON_STYLE_COMMENT =  QE_STYLE_COMMENT,
    FALCON_STYLE_STRING1 =  QE_STYLE_STRING,
    FALCON_STYLE_STRING2 =  QE_STYLE_STRING,
    FALCON_STYLE_NUMBER =   QE_STYLE_NUMBER,
    FALCON_STYLE_KEYWORD =  QE_STYLE_KEYWORD,
    FALCON_STYLE_TYPE =     QE_STYLE_TYPE,
    FALCON_STYLE_FUNCTION = QE_STYLE_FUNCTION,
};

static void falcon_colorize_line(QEColorizeContext *cp,
                                 unsigned int *str, int n, ModeDef *syn)
{
    int i = 0, start = i, c, style = 0;
    int state = cp->colorize_state;
    char kbuf[64];

    if (state & IN_FALCON_COMMENT)
        goto parse_comment;

    if (state & IN_FALCON_STRING1)
        goto parse_string1;

    if (state & IN_FALCON_STRING2)
        goto parse_string2;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            if (i == 1 && str[i] == '!') {
                i = n;
                style = FALCON_STYLE_SHBANG;
                break;
            }
            continue;

        case '/':
            if (str[i] == '*') {
                /* C block comments */
                i++;
                state |= IN_FALCON_COMMENT;
            parse_comment:
                for (; i < n; i++) {
                    if (str[i] == '*' && str[i + 1] == '/') {
                        i += 2;
                        state &= ~IN_FALCON_COMMENT;
                        break;
                    }
                }
                style = FALCON_STYLE_COMMENT;
                break;
            }
            if (str[i] == '/') {
                i = n;
                style = FALCON_STYLE_COMMENT;
                break;
            }            
            continue;

        case '\'':
            /* a single quoted string literal */
            state = IN_FALCON_STRING1;
        parse_string1:
            while (i < n) {
                c = str[i++];
                if (c == '\\') {
                    if (i < n) {
                        i += 1;
                    }
                } else
                if (c == '\'') {
                    state &= ~IN_FALCON_STRING1;
                    break;
                }
            }
            style = FALCON_STYLE_STRING1;
            break;

        case '\"':
            /* parse double quoted string literal */
            state = IN_FALCON_STRING2;
        parse_string2:
            c = '\0';
            while (i < n) {
                c = str[i++];
                if (c == '\\') {
                    if (i < n) {
                        i += 1;
                    }
                } else
                if (c == '\"') {
                    state &= ~IN_FALCON_STRING2;
                    break;
                }
            }
            style = FALCON_STYLE_STRING1;
            break;

        case '.':
            if (qe_isdigit(str[i]))
                goto parse_decimal;
            continue;

        default:
            if (qe_isdigit(c)) {
                if (c == '0' && qe_tolower(str[i]) == 'x') {
                    /* hexadecimal numbers */
                    for (i += 1; qe_isxdigit(str[i]); i++)
                        continue;
                } else
                if (c == '0') {
                    /* octal numbers */
                    for (i += 1; qe_isoctdigit(str[i]); i++)
                        continue;
                } else {
                    /* decimal numbers */
                    for (; qe_isdigit(str[i]); i++)
                        continue;
                    if (str[i] == '.') {
                        i++;
                    parse_decimal:
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
                /* XXX: should detect malformed number constants */
                style = FALCON_STYLE_NUMBER;
                break;
            }
            if (qe_isalpha_(c) || c > 0xA0) {
                i += ustr_get_word(kbuf, countof(kbuf), c, str, i, n);
                if (strfind(syn->keywords, kbuf)) {
                    style = FALCON_STYLE_KEYWORD;
                    break;
                }
                if (check_fcall(str, i)) {
                    style = FALCON_STYLE_FUNCTION;
                    break;
                }
                if (qe_isupper(kbuf[0]) && (start == 0 || str[start-1] != '.')) {
                    /* Types are capitalized */
                    style = FALCON_STYLE_TYPE;
                    break;
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

static ModeDef falcon_mode = {
    .name = "Falcon",
    .extensions = "fal",
    .shell_handlers = "falcon",
    .colorize_func = falcon_colorize_line,
    .keywords = falcon_keywords,
};

static int falcon_init(void)
{
    qe_register_mode(&falcon_mode, MODEF_SYNTAX);

    return 0;
}

/*----------------*/

static int extra_modes_init(void)
{
    asm_init();
    basic_init();
    vim_init();
    pascal_init();
    ada_init();
    fortran_init();
    ini_init();
    sharp_init();
    ps_init();
    sql_init();
    lua_init();
    julia_init();
    haskell_init();
    coffee_init();
    python_init();
    ruby_init();
    erlang_init();
    elixir_init();
    ocaml_init();
    eff_init();
    emf_init();
    agena_init();
    smalltalk_init();
    scad_init();
    magpie_init();
    falcon_init();
    return 0;
}

qe_module_init(extra_modes_init);
