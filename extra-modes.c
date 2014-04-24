/*
 * Miscellaneous language modes for QEmacs.
 *
 * Copyright (c) 2000-2014 Charlie Gordon.
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

static int is_keyword(unsigned int *str, int from, int to, const char *list)
{
    char keyword[MAX_KEYWORD_SIZE];
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

    if (strfind(list, keyword))
        return 1;

    return 0;
}

static int is_lc_keyword(unsigned int *str, int from, int to, const char *list)
{
    char keyword[MAX_KEYWORD_SIZE];
    int c, i, len = to - from;

    if (len >= MAX_KEYWORD_SIZE)
        return 0;

    for (i = 0; i < len; i++) {
        c = str[from + i];
        if (c >= 0x80)
            return 0;
        keyword[i] = qe_tolower(c);
    }
    keyword[len] = '\0';

    if (strfind(list, keyword))
        return 1;

    return 0;
}

/*---------------- Assembly mode coloring ----------------*/

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
                              unsigned int *str, int n, int mode_flags)
{
    int i = 0, j, w;
    int wn = 0; /* word number on line */
    int colstate = cp->colorize_state;

    if (colstate) {
        /* skip characters upto and including separator */
        w = i;
    comment:
        for (; i < n; i++) {
            if (str[i] == (char)colstate) {
                i++;
                colstate = 0;
                break;
            }
        }
        SET_COLOR(str, w, i, ASM_STYLE_COMMENT);
    }
    for (w = i; i < n && qe_isspace(str[i]); i++)
        continue;

    for (w = i; i < n;) {
        switch (str[i]) {
        case '.':
            if (i > w)
                break;
        prep:
            /* scan for comment */
            for (j = i + 1; j < n; j++) {
                if (str[j] == ';')
                    break;
            }
            SET_COLOR(str, i, j, ASM_STYLE_PREPROCESS);
            i = j;
            continue;
        case ';':
            SET_COLOR(str, i, n, ASM_STYLE_COMMENT);
            i = n;
            continue;
        case '\'':
        case '\"':
            /* parse string const */
            for (j = i + 1; j < n; j++) {
                if (str[j] == str[i]) {
                    j++;
                    break;
                }
            }
            SET_COLOR(str, i, j, ASM_STYLE_STRING);
            i = j;
            continue;
        default:
            break;
        }
        /* parse numbers */
        if (qe_isdigit(str[i])) {
            for (j = i + 1; j < n; j++) {
                if (!qe_isalnum(str[j]))
                    break;
            }
            SET_COLOR(str, i, j, ASM_STYLE_NUMBER);
            i = j;
            continue;
        }
        /* parse identifiers and keywords */
        if (qe_isalpha_(str[i]) || str[i] == '@'
        ||  str[i] == '$' || str[i] == '%' || str[i] == '?') {
            for (j = i + 1; j < n; j++) {
                if (!qe_isalnum_(str[j])
                &&  str[j] != '@' && str[j] != '$'
                &&  str[j] != '%' && str[j] != '?')
                    break;
            }
            if (++wn == 1) {
                if (j - i == 7
                &&  n - j >= 2
                &&  !ustristart(str + i, "comment", NULL)) {
                    for (w = j; w < n; w++) {
                        if (!qe_isspace(str[w]))
                            break;
                    }
                    colstate = str[w];
                    SET_COLOR(str, i, w, ASM_STYLE_PREPROCESS);
                    i = w + 1;
                    goto comment;
                }
                if (is_lc_keyword(str, i, j, asm_prepkeywords1))
                    goto prep;
            } else
            if (wn == 2) {
                if (is_lc_keyword(str, i, j, asm_prepkeywords2)) {
                    SET_COLOR(str, i, j, ASM_STYLE_PREPROCESS);
                    i = j;
                    continue;
                }
            }
            SET_COLOR(str, i, j, ASM_STYLE_IDENTIFIER);
            i = j;
            continue;
        }
        i++;
        continue;
    }
    cp->colorize_state = colstate;
}

static int asm_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (match_extension(p->filename, mode->extensions))
        return 80;

    return 1;
}

static CmdDef asm_commands[] = {
    CMD_DEF_END,
};

static ModeDef asm_mode;

static int asm_init(void)
{
    /* asm mode is almost like the text mode, so we copy and patch it */
    memcpy(&asm_mode, &text_mode, sizeof(ModeDef));
    asm_mode.name = "asm";
    asm_mode.extensions = "asm|asi|cod";
    asm_mode.mode_probe = asm_mode_probe;
    asm_mode.colorize_func = asm_colorize_line;

    qe_register_mode(&asm_mode);
    qe_register_cmd_table(asm_commands, &asm_mode);

    return 0;
}

/*---------------- Basic/Visual Basic coloring ----------------*/

static char const basic_keywords[] = {
    "|access|alias|and|any|as|base|begin|boolean|byval"
    "|call|case|circle|close|const|currency"
    "|declare|defcur|defdbl|defint|deflng|defsng"
    "|defstr|defvar|delete|dim|do|double"
    "|else|elseif|end|eqv|erase|error|exit|explicit"
    "|false|for|function|get|global|gosub|goto"
    "|if|imp|input|integer|is"
    "|len|let|lib|like|line|local|lock|long|loop|lset"
    "|me|mod|name|new|next|not|null"
    "|on|open|option|or|print|pset|put"
    "|redim|rem|resume|return|rset"
    "|scale|select|set|shared|single|spc|static"
    "|step|stop|string|sub"
    "|tab|then|to|true|type|typeof"
    "|unlock|until|variant|wend|while|width|xor"
    "|"
};

enum {
    BASIC_STYLE_TEXT =        QE_STYLE_DEFAULT,
    BASIC_STYLE_KEYWORD =     QE_STYLE_KEYWORD,
    BASIC_STYLE_PREPROCESS =  QE_STYLE_PREPROCESS,
    BASIC_STYLE_COMMENT =     QE_STYLE_COMMENT,
    BASIC_STYLE_STRING =      QE_STYLE_STRING,
    BASIC_STYLE_IDENTIFIER =  QE_STYLE_VARIABLE,
};

static void basic_colorize_line(QEColorizeContext *cp,
                                unsigned int *str, int n, int mode_flags)
{
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
                if (str[i++] == c)
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
            for (; i < n; i++) {
                if (!qe_isalnum_(str[i])) {
                    if (qe_findchar("$&!@%#", str[i]))
                        i++;
                    break;
                }
            }
            if (is_lc_keyword(str, start, i, basic_keywords)) {
                SET_COLOR(str, start, i, BASIC_STYLE_KEYWORD);
                continue;
            }
            SET_COLOR(str, start, i, BASIC_STYLE_IDENTIFIER);
            continue;
        }
    }
}

static int basic_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (match_extension(p->filename, mode->extensions))
        return 80;

    return 1;
}

static CmdDef basic_commands[] = {
    CMD_DEF_END,
};

static ModeDef basic_mode;

static int basic_init(void)
{
    /* basic mode is almost like the text mode, so we copy and patch it */
    memcpy(&basic_mode, &text_mode, sizeof(ModeDef));
    basic_mode.name = "Basic";
    basic_mode.extensions = "bas|frm|mst|vb|vbs";
    basic_mode.mode_probe = basic_mode_probe;
    basic_mode.colorize_func = basic_colorize_line;

    qe_register_mode(&basic_mode);
    qe_register_cmd_table(basic_commands, &basic_mode);

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
                              unsigned int *str, int n, int mode_flags)
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
                if (str[i++] == c)
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
                    if (str[i++] == c)
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
                    if (str[j++] == c) {
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
                if (str[i++] == c) {
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
                if (str[i] == '(' || (str[i] == ' ' && str[i + 1] == '('))
                    style = VIM_STYLE_FUNCTION;
            }
            SET_COLOR(str, start, i, style);
            continue;
        }
    }
    cp->colorize_state = (state << 4) | (level & 15);
}

static int vim_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (match_extension(p->filename, mode->extensions))
        return 80;

    return 1;
}

static CmdDef vim_commands[] = {
    CMD_DEF_END,
};

static ModeDef vim_mode;

static int vim_init(void)
{
    /* vim mode is almost like the text mode, so we copy and patch it */
    memcpy(&vim_mode, &text_mode, sizeof(ModeDef));
    vim_mode.name = "Vim";
    vim_mode.extensions = "vim";
    vim_mode.mode_probe = vim_mode_probe;
    vim_mode.colorize_func = vim_colorize_line;

    qe_register_mode(&vim_mode);
    qe_register_cmd_table(vim_commands, &vim_mode);

    return 0;
}

/*---------------- Pascal/Turbo Pascal/Delphi coloring ----------------*/
/* Should do Delphi specific things */

static char const pascal_keywords[] = {
    "|absolute|and|array|asm|begin|boolean|byte"
    "|case|char|comp|const|div|do|double|downto"
    "|else|end|extended|external"
    "|false|far|file|for|forward|function|goto"
    "|if|implementation|in|inline|integer|interface|interrupt"
    "|label|longint|mod|near|nil|not|of|or|overlay"
    "|packed|pointer|procedure|program|real|record|repeat"
    "|set|shl|shortint|shr|single|string|text|then|to|true|type"
    "|unit|until|uses|var|while|with|word|xor"
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
    PASCAL_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    PASCAL_STYLE_COMMENT =    QE_STYLE_COMMENT,
    PASCAL_STYLE_STRING =     QE_STYLE_STRING,
    PASCAL_STYLE_IDENTIFIER = QE_STYLE_VARIABLE,
    PASCAL_STYLE_NUMBER =     QE_STYLE_NUMBER,
    PASCAL_STYLE_FUNCTION =   QE_STYLE_FUNCTION,
};

static void pascal_colorize_line(QEColorizeContext *cp,
                                 unsigned int *str, int n, int mode_flags)
{
    int i = 0, start = i, c, k, style;
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
                SET_COLOR(str, start, i, PASCAL_STYLE_COMMENT);
                continue;
            }
            break;
        case '{':
            /* check for preprocessor */
            if (str[i] == '$') {
                colstate = IN_PASCAL_COMMENT1;
                i++;
            in_comment1:
                while (i < n) {
                    if (str[i++] == '}') {
                        colstate = 0;
                        break;
                    }
                }
                SET_COLOR(str, start, i, PASCAL_STYLE_PREPROCESS);
            } else {
                /* regular comment (recursive?) */
                colstate = IN_PASCAL_COMMENT;
            in_comment:
                while (i < n) {
                    if (str[i++] == '}') {
                        colstate = 0;
                        break;
                    }
                }
                SET_COLOR(str, start, i, PASCAL_STYLE_COMMENT);
            }
            continue;
        case '(':
            /* check for preprocessor */
            if (str[i] != '*')
                break;

            /* regular comment (recursive?) */
            colstate = IN_PASCAL_COMMENT2;
            i++;
        in_comment2:
            for (; i < n; i++) {
                if (str[i] == '*' && str[i + 1] == ')') {
                    i += 2;
                    colstate = 0;
                    break;
                }
            }
            SET_COLOR(str, start, i, PASCAL_STYLE_COMMENT);
            continue;
        case '\'':
            /* parse string or char const */
            while (i < n) {
                /* XXX: escape sequences? */
                if (str[i++] == c)
                    break;
            }
            SET_COLOR(str, start, i, PASCAL_STYLE_STRING);
            continue;
        case '#':
            /* parse hex char const */
            for (; i < n; i++) {
                if (!qe_isxdigit(str[i]))
                    break;
            }
            SET_COLOR(str, start, i, PASCAL_STYLE_STRING);
            continue;
        default:
            break;
        }
        /* parse numbers */
        if (qe_isdigit(c) || c == '$') {
            for (; i < n; i++) {
                if (!qe_isalnum(str[i]) && str[i] != '.')
                    break;
            }
            SET_COLOR(str, start, i, PASCAL_STYLE_NUMBER);
            continue;
        }
        /* parse identifiers and keywords */
        if (qe_isalpha_(c)) {
            for (; i < n; i++) {
                if (!qe_isalnum_(str[i]))
                    break;
            }
            if (is_lc_keyword(str, start, i, pascal_keywords)) {
                SET_COLOR(str, start, i, PASCAL_STYLE_KEYWORD);
                continue;
            }
            style = PASCAL_STYLE_IDENTIFIER;
            k = i;
            if (qe_isblank(str[k]))
                k++;
            if (str[k] == '(' && str[k + 1] != '*')
                style = PASCAL_STYLE_FUNCTION;
            SET_COLOR(str, start, i, style);
            continue;
        }
    }
    cp->colorize_state = colstate;
}

static int pascal_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (match_extension(p->filename, mode->extensions))
        return 80;

    return 1;
}

static CmdDef pascal_commands[] = {
    CMD_DEF_END,
};

static ModeDef pascal_mode;

static int pascal_init(void)
{
    /* pascal mode is almost like the text mode, so we copy and patch it */
    memcpy(&pascal_mode, &text_mode, sizeof(ModeDef));
    pascal_mode.name = "Pascal";
    pascal_mode.extensions = "pas";
    pascal_mode.mode_probe = pascal_mode_probe;
    pascal_mode.colorize_func = pascal_colorize_line;

    qe_register_mode(&pascal_mode);
    qe_register_cmd_table(pascal_commands, &pascal_mode);

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
                              unsigned int *str, int n, int mode_flags)
{
    int i = 0, start, c, bol = 1;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case ';':
            if (!bol)
                break;
            i = n;
            SET_COLOR(str, start, i, INI_STYLE_COMMENT);
            continue;
        case '#':
            if (!bol)
                break;
            i = n;
            SET_COLOR(str, start, i, INI_STYLE_PREPROCESS);
            continue;
        case '[':
            if (start == 0) {
                i = n;
                SET_COLOR(str, start, i, INI_STYLE_FUNCTION);
                continue;
            }
            break;
        case '\"':
            /* parse string const */
            while (i < n) {
                /* XXX: escape sequences? */
                if (str[i++] == '\"')
                    break;
            }
            SET_COLOR(str, start, i, INI_STYLE_STRING);
            continue;
        case ' ':
        case '\t':
            if (bol)
                continue;
            break;
        default:
            break;
        }
        bol = 0;
        /* parse numbers */
        if (qe_isdigit(c)) {
            for (; i < n; i++) {
                if (!qe_isalnum(str[i]))
                    break;
            }
            SET_COLOR(str, start, i, INI_STYLE_NUMBER);
            continue;
        }
        /* parse identifiers and keywords */
        if (start == 0 && (qe_isalpha_(c) || c == '@' || c == '$')) {
            for (; i < n; i++) {
                if (str[i] == '=')
                    break;
            }
            if (i < n) {
                SET_COLOR(str, start, i, INI_STYLE_IDENTIFIER);
            }
            continue;
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
        if (*p == '[' && p[1] != '[') {
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

/* specific script commands */
static CmdDef ini_commands[] = {
    CMD_DEF_END,
};

static ModeDef ini_mode;

static int ini_init(void)
{
    /* ini mode is almost like the text mode, so we copy and patch it */
    memcpy(&ini_mode, &text_mode, sizeof(ModeDef));
    ini_mode.name = "ini";
    ini_mode.extensions = "ini|inf|INI|INF";
    ini_mode.mode_probe = ini_mode_probe;
    ini_mode.colorize_func = ini_colorize_line;

    qe_register_mode(&ini_mode);
    qe_register_cmd_table(ini_commands, &ini_mode);

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
                             unsigned int *str, int n, int mode_flags)
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

static CmdDef ps_commands[] = {
    CMD_DEF_END,
};

static ModeDef ps_mode;

static int ps_init(void)
{
    /* Poscript mode is almost like the text mode, so we copy and patch it */
    memcpy(&ps_mode, &text_mode, sizeof(ModeDef));
    ps_mode.name = "Postscript";
    ps_mode.extensions = "ps|ms|eps";
    ps_mode.mode_probe = ps_mode_probe;
    ps_mode.colorize_func = ps_colorize_line;

    qe_register_mode(&ps_mode);
    qe_register_cmd_table(ps_commands, &ps_mode);

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
    SQL_STYLE_IDENTIFIER = QE_STYLE_KEYWORD,
    SQL_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
};

static void sql_colorize_line(QEColorizeContext *cp,
                              unsigned int *str, int n, int mode_flags)
{
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
                if (str[i] == c) {
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
    }
    cp->colorize_state = state;
}

static int sql_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (match_extension(p->filename, mode->extensions))
        return 80;

    return 1;
}

static CmdDef sql_commands[] = {
    CMD_DEF_END,
};

static ModeDef sql_mode;

static int sql_init(void)
{
    /* sql mode is almost like the text mode, so we copy and patch it */
    memcpy(&sql_mode, &text_mode, sizeof(ModeDef));
    sql_mode.name = "SQL";
    sql_mode.extensions = "sql|mysql|sqlite|sqlplus";
    sql_mode.mode_probe = sql_mode_probe;
    sql_mode.colorize_func = sql_colorize_line;

    qe_register_mode(&sql_mode);
    qe_register_cmd_table(sql_commands, &sql_mode);

    return 0;
}

/*---------------- Lua script coloring ----------------*/

static char const lua_keywords[] = {
    "|and|break|do|else|elseif|end|false|for|function|goto|if|in"
    "|local|nil|not|or|repeat|return|then|true|until|while"
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

void lua_colorize_line(QEColorizeContext *cp,
                       unsigned int *str, int n, int mode_flags)
{
    int i = 0, start = i, c, sep = 0, level = 0, level1, klen, style;
    int state = cp->colorize_state;
    char kbuf[32];

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
                    if (!qe_isalnum(str[i] && str[i] != '.'))
                        break;
                }
                SET_COLOR(str, start, i, LUA_STYLE_NUMBER);
                continue;
            }
            if (qe_isalpha_(c)) {
                for (klen = 0, i--; qe_isalnum_(str[i]); i++) {
                    if (klen < countof(kbuf) - 1)
                        kbuf[klen++] = str[i];
                }
                kbuf[klen] = '\0';

                if (strfind(lua_keywords, kbuf)) {
                    SET_COLOR(str, start, i, LUA_STYLE_KEYWORD);
                    continue;
                }
                while (qe_isblank(str[i]))
                    i++;
                if (str[i] == '(') {
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

static int lua_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (match_extension(p->filename, mode->extensions))
        return 80;

    return 1;
}

static CmdDef lua_commands[] = {
    CMD_DEF_END,
};

static ModeDef lua_mode;

static int lua_init(void)
{
    /* lua mode is almost like the text mode, so we copy and patch it */
    memcpy(&lua_mode, &text_mode, sizeof(ModeDef));
    lua_mode.name = "Lua";
    lua_mode.extensions = "lua";
    lua_mode.mode_probe = lua_mode_probe;
    lua_mode.colorize_func = lua_colorize_line;

    qe_register_mode(&lua_mode);
    qe_register_cmd_table(lua_commands, &lua_mode);

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

int julia_get_number(const unsigned int *p)
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

void julia_colorize_line(QEColorizeContext *cp,
                         unsigned int *str, int n, int mode_flags)
{
    int i = 0, start = i, c, sep = 0, klen;
    int state = cp->colorize_state;
    char kbuf[32];

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
            if (str[i] == sep && str[i + 1] == sep) {
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
                    if (c == sep && str[i] == sep && str[i + 1] == sep) {
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
                if (strfind(julia_keywords, kbuf)
                ||  strfind(julia_constants, kbuf)) {
                    SET_COLOR(str, start, i, JULIA_STYLE_KEYWORD);
                    continue;
                }
                if (strfind(julia_types, kbuf)) {
                    SET_COLOR(str, start, i, JULIA_STYLE_TYPE);
                    continue;
                }
                if (str[i] == '(' || (str[i] == ' ' && str[i + 1] == '(')) {
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

static int julia_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (match_extension(p->filename, mode->extensions))
        return 80;

    return 1;
}

static CmdDef julia_commands[] = {
    CMD_DEF_END,
};

static ModeDef julia_mode;

static int julia_init(void)
{
    /* julia mode is almost like the text mode, so we copy and patch it */
    memcpy(&julia_mode, &text_mode, sizeof(ModeDef));
    julia_mode.name = "Julia";
    julia_mode.extensions = "jl";
    julia_mode.mode_probe = julia_mode_probe;
    julia_mode.colorize_func = julia_colorize_line;

    qe_register_mode(&julia_mode);
    qe_register_cmd_table(julia_commands, &julia_mode);

    return 0;
}

/*---------------- Haskell coloring ----------------*/

static char const haskell_keywords[] = {
    "|_|case|class|data|default|deriving|do|else|foreign"
    "|if|import|in|infix|infixl|infixr|instance|let"
    "|module|newtype|of|then|type|where"
    "|"
};

enum {
    IN_HASKELL_COMMENT = 0x10,
    IN_HASKELL_STRING  = 0x20,
    IN_HASKELL_LEVEL   = 0x0F,
};

enum {
    HASKELL_STYLE_TEXT =     QE_STYLE_DEFAULT,
    HASKELL_STYLE_COMMENT =  QE_STYLE_COMMENT,
    HASKELL_STYLE_STRING =   QE_STYLE_STRING,
    HASKELL_STYLE_NUMBER =   QE_STYLE_NUMBER,
    HASKELL_STYLE_KEYWORD =  QE_STYLE_KEYWORD,
    HASKELL_STYLE_FUNCTION = QE_STYLE_FUNCTION,
    HASKELL_STYLE_SYMBOL =   QE_STYLE_NUMBER,
};

static inline int haskell_is_symbol(int c)
{
    return qe_findchar("!#$%&+./<=>?@\\^|-~:", c);
}

void haskell_colorize_line(QEColorizeContext *cp,
                           unsigned int *str, int n, int mode_flags)
{
    int i = 0, start = i, c, sep = 0, level = 0, klen;
    int state = cp->colorize_state;
    char kbuf[32];

    if (state & IN_HASKELL_COMMENT)
        goto parse_comment;

    if (state & IN_HASKELL_STRING) {
        sep = '\"';
        state = 0;
        while (qe_isspace(str[i]))
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
                SET_COLOR(str, start, i, HASKELL_STYLE_COMMENT);
                continue;
            }
            goto parse_symbol;
        case '{':
            if (str[i] == '-') {
                state |= IN_HASKELL_COMMENT;
                i++;
                goto parse_comment;
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
            break;
            
        parse_comment:
            level = state & IN_HASKELL_LEVEL;
            for (; i < n; i++) {
                if (str[i] == '{' && str[i + 1] == '-') {
                    level++;
                    i++;
                    continue;
                }
                if (str[i] == '-' && str[i + 1] == '}') {
                    i++;
                    level--;
                    if (level == 0) {
                        i++;
                        state &= ~IN_HASKELL_COMMENT;
                        break;
                    }
                }
            }
            state &= ~IN_HASKELL_COMMENT;
            state |= level & IN_HASKELL_COMMENT;
            SET_COLOR(str, start, i, HASKELL_STYLE_COMMENT);
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
                            state = IN_HASKELL_STRING;
                        }
                    } else
                    if (str[i] == '^' && i + 1 < n && str[i + 1] != sep) {
                        i += 2;
                    } else {
                        i += 1;
                    }
                } else
                if (c == sep) {
                    break;
                }
            }
            SET_COLOR(str, start, i, HASKELL_STYLE_STRING);
            continue;

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
                SET_COLOR(str, start, i, HASKELL_STYLE_NUMBER);
                continue;
            }
            if (qe_isalpha_(c)) {
                for (klen = 0, i--; qe_isalnum_(str[i]) || str[i] == '\''; i++) {
                    if (klen < countof(kbuf) - 1)
                        kbuf[klen++] = str[i];
                }
                kbuf[klen] = '\0';

                if (strfind(haskell_keywords, kbuf)) {
                    SET_COLOR(str, start, i, HASKELL_STYLE_KEYWORD);
                    continue;
                }
                while (qe_isblank(str[i]))
                    i++;
                if (str[i] == '(') {
                    SET_COLOR(str, start, i, HASKELL_STYLE_FUNCTION);
                    continue;
                }
                continue;
            }
        parse_symbol:
            if (haskell_is_symbol(c)) {
                for (; haskell_is_symbol(str[i]); i++)
                    continue;
                SET_COLOR(str, start, i, HASKELL_STYLE_SYMBOL);
                continue;
            }
            break;
        }
    }
    cp->colorize_state = state;
}

static int haskell_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (match_extension(p->filename, mode->extensions))
        return 80;

    return 1;
}

static CmdDef haskell_commands[] = {
    CMD_DEF_END,
};

static ModeDef haskell_mode;

static int haskell_init(void)
{
    /* haskell mode is almost like the text mode, so we copy and patch it */
    memcpy(&haskell_mode, &text_mode, sizeof(ModeDef));
    haskell_mode.name = "Haskell";
    haskell_mode.extensions = "hs|haskell";
    haskell_mode.mode_probe = haskell_mode_probe;
    haskell_mode.colorize_func = haskell_colorize_line;

    qe_register_mode(&haskell_mode);
    qe_register_cmd_table(haskell_commands, &haskell_mode);

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

void python_colorize_line(QEColorizeContext *cp,
                          unsigned int *str, int n, int mode_flags)
{
    int i = 0, start = i, c, sep = 0, klen;
    int state = cp->colorize_state;
    char kbuf[32];

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

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            i = n;
            SET_COLOR(str, start, i, PYTHON_STYLE_COMMENT);
            continue;
            
        case '\'':
        case '\"':
            /* parse string const */
            i--;
        has_quote:
            sep = str[i++];
            if (str[i] == sep && str[i + 1] == sep) {
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
                    if (c == sep && str[i] == sep && str[i + 1] == sep) {
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
            SET_COLOR(str, start, i, PYTHON_STYLE_STRING);
            continue;

        case '.':
            if (qe_isdigit(str[i]))
                goto parse_decimal;
            break;

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
                SET_COLOR(str, start, i, PYTHON_STYLE_NUMBER);
                continue;
            }
        has_alpha:
            if (qe_isalpha_(c)) {
                for (klen = 0, i--; qe_isalnum_(str[i]); i++) {
                    if (klen < countof(kbuf) - 1)
                        kbuf[klen++] = str[i];
                }
                kbuf[klen] = '\0';

                if (strfind(python_keywords, kbuf)) {
                    SET_COLOR(str, start, i, PYTHON_STYLE_KEYWORD);
                    continue;
                }
                while (qe_isblank(str[i]))
                    i++;
                if (str[i] == '(') {
                    SET_COLOR(str, start, i, PYTHON_STYLE_FUNCTION);
                    continue;
                }
                continue;
            }
            break;
        }
    }
    cp->colorize_state = state;
}

static int python_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (match_extension(p->filename, mode->extensions))
        return 80;

    return 1;
}

static CmdDef python_commands[] = {
    CMD_DEF_END,
};

static ModeDef python_mode;

static int python_init(void)
{
    /* python mode is almost like the text mode, so we copy and patch it */
    memcpy(&python_mode, &text_mode, sizeof(ModeDef));
    python_mode.name = "Python";
    python_mode.extensions = "py";
    python_mode.mode_probe = python_mode_probe;
    python_mode.colorize_func = python_colorize_line;

    qe_register_mode(&python_mode);
    qe_register_cmd_table(python_commands, &python_mode);

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

void ruby_colorize_line(QEColorizeContext *cp,
                        unsigned int *str, int n, int mode_flags)
{
    int i = 0, j, start = i, c, indent, sig, style;
    static int sep, sep0, level;        /* XXX: ugly patch */
    int state = cp->colorize_state;
    char kbuf[32];

    for (indent = 0; qe_isspace(str[indent]); indent++)
        continue;

    if (state & IN_RUBY_HEREDOC) {
        if (state & IN_RUBY_HD_INDENT) {
            while (qe_isspace(str[i]))
                i++;
        }
	sig = 0;
        if (qe_isalpha_(str[i])) {
            sig = str[i++] % 61;
            for (; qe_isalnum_(str[i]); i++) {
                sig = ((sig << 6) + str[i]) % 61;
            }
        }
        for (; qe_isspace(str[i]); i++)
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

    while (i < n && qe_isspace(str[i]))
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
            &&   !qe_isalnum(str[i - 2] & CHAR_MASK)
            &&   str[i - 2] != ')')) {
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
                SET_COLOR(str, start, i, RUBY_STYLE_REGEX);
                continue;
            }
            break;

        case '#':
            i = n;
        comment:
            SET_COLOR(str, start, i, RUBY_STYLE_COMMENT);
            continue;
            
        case '%':
            /* parse alternate string/array syntaxes */
            if (str[i] != '\0' && !qe_isspace(str[i]) && !qe_isalnum(str[i]))
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
                SET_COLOR(str, start, i, RUBY_STYLE_STRING4);
                continue;
            }
            break;

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
            SET_COLOR(str, start, i, RUBY_STYLE_STRING);
            continue;

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
            SET_COLOR(str, start, i, RUBY_STYLE_STRING3);
            continue;

        case '\"':
            /* parse double quoted string const */
            c = '\0';
        parse_string2:
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
            SET_COLOR(str, start, i, RUBY_STYLE_STRING2);
            continue;

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
                    if (str[j++] != sep)
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
                    SET_COLOR(str, start, i, RUBY_STYLE_HEREDOC);
                }
            }
            break;

        case '?':
            /* XXX: should parse character constants */
            break;

        case '.':
            if (qe_isdigit_(str[i]))
                goto parse_decimal;
            break;

        case '$':
            /* XXX: should parse precise $ syntax,
             * skip $" and $' for now
             */
            if (i < n)
                i++;
            break;

        case ':':
            /* XXX: should parse Ruby symbol */
            break;

        case '@':
            i += ruby_get_name(kbuf, countof(kbuf), str + i);
            SET_COLOR(str, start, i, RUBY_STYLE_MEMBER);
            continue;

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
                SET_COLOR(str, start, i, RUBY_STYLE_NUMBER);
                continue;
            }
            if (qe_isalpha_(c)) {
                i--;
                i += ruby_get_name(kbuf, countof(kbuf), str + i);

                if (strfind(ruby_keywords, kbuf)) {
                    SET_COLOR(str, start, i, RUBY_STYLE_KEYWORD);
                    continue;
                }
                while (qe_isblank(str[i]))
                    i++;
                if (str[i] == '(' || str[i] == '{') {
                    SET_COLOR(str, start, i, RUBY_STYLE_FUNCTION);
                    continue;
                }
                continue;
            }
            break;
        }
    }
    cp->colorize_state = state;
}

static int ruby_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (match_extension(p->filename, mode->extensions)
    ||  stristart(p->filename, "Rakefile", NULL))
        return 80;

    return 1;
}

static CmdDef ruby_commands[] = {
    CMD_DEF_END,
};

static ModeDef ruby_mode;

static int ruby_init(void)
{
    /* ruby mode is almost like the text mode, so we copy and patch it */
    memcpy(&ruby_mode, &text_mode, sizeof(ModeDef));
    ruby_mode.name = "Ruby";
    ruby_mode.extensions = "rb|gemspec";
    ruby_mode.mode_probe = ruby_mode_probe;
    ruby_mode.colorize_func = ruby_colorize_line;

    qe_register_mode(&ruby_mode);
    qe_register_cmd_table(ruby_commands, &ruby_mode);

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
                                 unsigned int *str, int n, int mode_flags)
{
    int i = 0, start = i, c, k, style;
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
            for (; i < n; i++) {
                if (!qe_isalnum_(str[i]) || str[i] == '\'')
                    break;
            }
            if (is_keyword(str, start, i, ocaml_types)) {
                style = OCAML_STYLE_TYPE;
            } else
            if (is_keyword(str, start, i, ocaml_keywords)) {
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

static int ocaml_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (match_extension(p->filename, mode->extensions))
        return 80;

    return 1;
}

static CmdDef ocaml_commands[] = {
    CMD_DEF_END,
};

static ModeDef ocaml_mode;

static int ocaml_init(void)
{
    /* ocaml mode is almost like the text mode, so we copy and patch it */
    memcpy(&ocaml_mode, &text_mode, sizeof(ModeDef));
    ocaml_mode.name = "Ocaml";
    ocaml_mode.extensions = "ml|mli|mll|mly";
    ocaml_mode.mode_probe = ocaml_mode_probe;
    ocaml_mode.colorize_func = ocaml_colorize_line;

    qe_register_mode(&ocaml_mode);
    qe_register_cmd_table(ocaml_commands, &ocaml_mode);

    return 0;
}

/*----------------*/

static int extra_modes_init(void)
{
    asm_init();
    basic_init();
    vim_init();
    pascal_init();
    ini_init();
    ps_init();
    sql_init();
    lua_init();
    julia_init();
    haskell_init();
    python_init();
    ruby_init();
    ocaml_init();
    return 0;
}

qe_module_init(extra_modes_init);
