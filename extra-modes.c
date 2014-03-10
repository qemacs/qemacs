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
    ASM_TEXT =        QE_STYLE_DEFAULT,
    ASM_PREPROCESS =  QE_STYLE_PREPROCESS,
    ASM_COMMENT =     QE_STYLE_COMMENT,
    ASM_STRING =      QE_STYLE_STRING,
    ASM_NUMBER =      QE_STYLE_NUMBER,
    ASM_IDENTIFIER =  QE_STYLE_VARIABLE,
};

static void asm_colorize_line(unsigned int *str, int n, int *statep,
                              __unused__ int state_only)
{
    int i = 0, j, w;
    int wn = 0; /* word number on line */
    int colstate = *statep;

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
        SET_COLOR(str, w, i, ASM_COMMENT);
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
            SET_COLOR(str, i, j, ASM_PREPROCESS);
            i = j;
            continue;
        case ';':
            SET_COLOR(str, i, n, ASM_COMMENT);
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
            SET_COLOR(str, i, j, ASM_STRING);
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
            SET_COLOR(str, i, j, ASM_NUMBER);
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
                    SET_COLOR(str, i, w, ASM_PREPROCESS);
                    i = w + 1;
                    goto comment;
                }
                if (is_lc_keyword(str, i, j, asm_prepkeywords1))
                    goto prep;
            } else
            if (wn == 2) {
                if (is_lc_keyword(str, i, j, asm_prepkeywords1)) {
                    SET_COLOR(str, i, j, ASM_PREPROCESS);
                    i = j;
                    continue;
                }
            }
            SET_COLOR(str, i, j, ASM_IDENTIFIER);
            i = j;
            continue;
        }
        i++;
        continue;
    }
    *statep =  colstate;
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
    BASIC_TEXT =        QE_STYLE_DEFAULT,
    BASIC_KEYWORD =     QE_STYLE_KEYWORD,
    BASIC_PREPROCESS =  QE_STYLE_PREPROCESS,
    BASIC_COMMENT =     QE_STYLE_COMMENT,
    BASIC_STRING =      QE_STYLE_STRING,
    BASIC_IDENTIFIER =  QE_STYLE_VARIABLE,
};

static void basic_colorize_line(unsigned int *str, int n, int *statep,
                                __unused__ int state_only)
{
    int i = 0, j;

    while (i < n) {
        switch (str[i]) {
        case '\'':
            if (str[i + 1] == '$')
                SET_COLOR(str, i, n, BASIC_PREPROCESS);
            else
                SET_COLOR(str, i, n, BASIC_COMMENT);
            i = n;
            continue;
        case '\"':
            /* parse string const */
            for (j = i + 1; j < n; j++) {
                if (str[j] == str[i]) {
                    j++;
                    break;
                }
            }
            SET_COLOR(str, i, j, BASIC_STRING);
            i = j;
            continue;
        default:
            break;
        }
        /* parse numbers */
        if (qe_isdigit(str[i])) {
            for (j = i + 1; j < n; j++) {
                if (!qe_isalnum(str[j]) && str[j] != '.')
                    break;
            }
            SET_COLOR(str, i, j, BASIC_IDENTIFIER);
            i = j;
            continue;
        }
        /* parse identifiers and keywords */
        if (qe_isalpha_(str[i])) {
            for (j = i + 1; j < n; j++) {
                if (!qe_isalnum_(str[j])) {
                    if (qe_findchar("$&!@%#", str[j]))
                        j++;
                    break;
                }
            }
            if (is_lc_keyword(str, i, j, basic_keywords)) {
                SET_COLOR(str, i, j, BASIC_KEYWORD);
                i = j;
                continue;
            }
            SET_COLOR(str, i, j, BASIC_IDENTIFIER);
            i = j;
            continue;
        }
        i++;
        continue;
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

#define IN_COMMENT      0x01
#define IN_COMMENT1     0x02
#define IN_COMMENT2     0x04

enum {
    PAS_TEXT =        QE_STYLE_DEFAULT,
    PAS_KEYWORD =     QE_STYLE_KEYWORD,
    PAS_PREPROCESS =  QE_STYLE_PREPROCESS,
    PAS_COMMENT =     QE_STYLE_COMMENT,
    PAS_STRING =      QE_STYLE_STRING,
    PAS_IDENTIFIER =  QE_STYLE_VARIABLE,
    PAS_NUMBER =      QE_STYLE_NUMBER,
    PAS_FUNCTION =    QE_STYLE_FUNCTION,
};

static void pascal_colorize_line(unsigned int *str, int n, int *statep,
                                 __unused__ int state_only)
{
    int i = 0, j = i, k;
    int colstate =  *statep;

    if (colstate & IN_COMMENT)
        goto in_comment;

    if (colstate & IN_COMMENT1)
        goto in_comment1;

    if (colstate & IN_COMMENT2)
        goto in_comment2;

    while (i < n) {
        switch (str[i]) {
        case '/':
            if (str[i + 1] == '/') {    /* C++ comments, recent extension */
                SET_COLOR(str, i, n, PAS_COMMENT);
                i = n;
                continue;
            }
            break;
        case '{':
            /* check for preprocessor */
            if (str[i + 1] == '$') {
                colstate = IN_COMMENT1;
                j = i + 2;
            in_comment1:
                for (; j < n; j++) {
                    if (str[j] == '}') {
                        j += 1;
                        colstate = 0;
                        break;
                    }
                }
                SET_COLOR(str, i, j, PAS_PREPROCESS);
            } else
            {
                /* regular comment (recursive?) */
                colstate = IN_COMMENT;
                j = i + 1;
            in_comment:
                for (; j < n; j++) {
                    if (str[j] == '}') {
                        j += 1;
                        colstate = 0;
                        break;
                    }
                }
                SET_COLOR(str, i, j, PAS_COMMENT);
            }
            i = j;
            continue;
        case '(':
            /* check for preprocessor */
            if (str[i + 1] != '*')
                break;

            /* regular comment (recursive?) */
            colstate = IN_COMMENT2;
            j = i + 2;
        in_comment2:
            for (; j < n; j++) {
                if (str[j] == '*' && str[j + 1] == ')') {
                    j += 2;
                    colstate = 0;
                    break;
                }
            }
            SET_COLOR(str, i, j, PAS_COMMENT);
            i = j;
            continue;
        case '\'':
            /* parse string or char const */
            for (j = i + 1; j < n; j++) {
                if (str[j] == str[i]) {
                    j++;
                    break;
                }
            }
            SET_COLOR(str, i, j, PAS_STRING);
            i = j;
            continue;
        case '#':
            /* parse hex char const */
            for (j = i + 1; j < n; j++) {
                if (!qe_isdigit(str[j]))
                    break;
            }
            SET_COLOR(str, i, j, PAS_STRING);
            i = j;
            continue;
        default:
            break;
        }
        /* parse numbers */
        if (qe_isdigit(str[i]) || str[i] == '$') {
            for (j = i + 1; j < n; j++) {
                if (!qe_isalnum(str[j]) && str[j] != '.')
                    break;
            }
            SET_COLOR(str, i, j, PAS_NUMBER);
            i = j;
            continue;
        }
        /* parse identifiers and keywords */
        if (qe_isalpha_(str[i])) {
            for (j = i + 1; j < n; j++) {
                if (!qe_isalnum_(str[j]))
                    break;
            }
            if (is_lc_keyword(str, i, j, pascal_keywords)) {
                SET_COLOR(str, i, j, PAS_KEYWORD);
                i = j;
                continue;
            }
            for (k = j; k < n; k++) {
                if (str[k] != ' ' && str[k] != '\t')
                    break;
            }
            SET_COLOR(str, i, j,
                      str[k] == '(' ? PAS_FUNCTION : PAS_IDENTIFIER);
            i = j;
            continue;
        }
        i++;
        continue;
    }
    *statep = colstate;
}

#undef IN_COMMENT2
#undef IN_COMMENT1
#undef IN_COMMENT

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
    INI_TEXT =        QE_STYLE_DEFAULT,
    INI_COMMENT =     QE_STYLE_COMMENT,
    INI_STRING =      QE_STYLE_STRING,
    INI_FUNCTION =    QE_STYLE_FUNCTION,
    INI_NUMBER =      QE_STYLE_NUMBER,
    INI_IDENTIFIER =  QE_STYLE_VARIABLE,
    INI_PREPROCESS =  QE_STYLE_PREPROCESS,
};

static void ini_colorize_line(unsigned int *str, int n, int *statep,
                              __unused__ int state_only)
{
    int i = 0, j;
    int bol = 1;

    while (i < n) {
        switch (str[i]) {
        case ';':
            if (!bol)
                break;
            SET_COLOR(str, i, n, INI_COMMENT);
            i = n;
            continue;
        case '#':
            if (!bol)
                break;
            SET_COLOR(str, i, n, INI_PREPROCESS);
            i = n;
            continue;
        case '[':
            if (i == 0) {
                SET_COLOR(str, i, n, INI_FUNCTION);
                i = n;
                continue;
            }
            break;
        case '\"':
            /* parse string const */
            for (j = i + 1; j < n; j++) {
                if (str[j] == '\"') {
                    j++;
                    break;
                }
            }
            SET_COLOR(str, i, j, INI_STRING);
            i = j;
            continue;
        default:
            if (bol && qe_isspace(str[i])) {
                i++;
                continue;
            }
            break;
        }
        bol = 0;
        /* parse numbers */
        if (qe_isdigit(str[i])) {
            for (j = i + 1; j < n; j++) {
                if (!qe_isalnum(str[j]))
                    break;
            }
            SET_COLOR(str, i, j, INI_NUMBER);
            i = j;
            continue;
        }
        /* parse identifiers and keywords */
        if (i == 0
        &&  (qe_isalpha_(str[i])
        ||   str[i] == '@' || str[i] == '$')) {
            for (j = i + 1; j < n; j++) {
                if (str[j] == '=')
                    break;
            }
            if (j < n) {
                SET_COLOR(str, i, j, INI_IDENTIFIER);
                i = j;
                continue;
            } else {
                i = n;
                continue;
            }
        }
        i++;
        continue;
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

#define IN_STRING       0x0F            /* ( ... ) level */
#define IN_COMMENT      0x10

enum {
    PS_TEXT =         QE_STYLE_DEFAULT,
    PS_COMMENT =      QE_STYLE_COMMENT,
    PS_STRING =       QE_STYLE_STRING,
    PS_NUMBER =       QE_STYLE_DEFAULT,
    PS_IDENTIFIER =   QE_STYLE_FUNCTION,
};

#define ispssep(c)      (qe_findchar(" \t\r\n,()<>[]{}/", c))
#define wrap 0

static void ps_colorize_line(unsigned int *str, int n, int *statep,
                             __unused__ int state_only)
{
    int i = 0, j;
    int colstate = *statep;

    if (colstate & IN_COMMENT)
        goto in_comment;

    if (colstate & IN_STRING)
        goto in_string;

    colstate = 0;

    while (i < n) {
        switch (str[i]) {
            /* Should deal with '<...>' '<<...>>' '<~...~>' tokens. */
        case '%':
        in_comment:
            if (wrap)
                colstate |= IN_COMMENT;
            else
                colstate &= ~IN_COMMENT;
            SET_COLOR(str, i, n, PS_COMMENT);
            i = n;
            continue;
        case '(':
        in_string:
            /* parse string skipping embedded \\ */
            for (j = i; j < n;) {
                switch (str[j++]) {
                case '(':
                    colstate++;
                    continue;
                case ')':
                    colstate--;
                    if (!(colstate & IN_STRING))
                        break;
                    continue;
                case '\\':
                    if (j == n)
                        break;
                    j++;
                    continue;
                default:
                    continue;
                }
                break;
            }
            SET_COLOR(str, i, j, PS_STRING);
            i = j;
            continue;
        default:
            break;
        }
        /* parse numbers */
        if (qe_isdigit(str[i])) {
            for (j = i + 1; j < n; j++) {
                if (!qe_isalnum(str[j]) && str[j] != '.')
                    break;
            }
            SET_COLOR(str, i, j, PS_NUMBER);
            i = j;
            continue;
        }
        /* parse identifiers and keywords */
        if (qe_isalpha_(str[i])) {
            for (j = i + 1; j < n; j++) {
                if (ispssep(str[j]))
                    break;
            }
            SET_COLOR(str, i, j, PS_IDENTIFIER);
            i = j;
            continue;
        }
        i++;
        continue;
    }
    *statep = colstate;
}

#undef IN_STRING
#undef IN_COMMENT

static int ps_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (match_extension(p->filename, mode->extensions))
        return 80;

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

#define IN_COMMENT  1

enum {
    SQL_TEXT =         QE_STYLE_DEFAULT,
    SQL_COMMENT =      QE_STYLE_COMMENT,
    SQL_STRING =       QE_STYLE_STRING,
    SQL_IDENTIFIER =   QE_STYLE_KEYWORD,
    SQL_PREPROCESS =   QE_STYLE_PREPROCESS,
};

static void sql_colorize_line(unsigned int *str, int n, int *statep,
                              __unused__ int state_only)
{
    int i = 0, j = i;
    int state = *statep;

    if (state & IN_COMMENT)
        goto parse_c_comment;

    while (i < n) {
        switch (str[i]) {
        case '/':
            if (str[i + 1] == '/')
                goto line_comment;
            if (str[i + 1] == '*') {
                /* normal comment */
                j = i + 2;
            parse_c_comment:
                state |= IN_COMMENT;
                while (j < n) {
                    if (str[j] == '*' && str[j + 1] == '/') {
                        j += 2;
                        state &= ~IN_COMMENT;
                        break;
                    } else {
                        j++;
                    }
                }
                goto comment;
            }
            break;
        case '-':
            if (str[i + 1] == '-')
                goto line_comment;
            break;
        case '#':
        line_comment:
            j = n;
        comment:
            SET_COLOR(str, i, j, SQL_COMMENT);
            i = j;
            continue;
        case '\'':
        case '\"':
        case '`':
            /* parse string const */
            for (j = i + 1; j < n; j++) {
                /* FIXME: Should parse strings more accurately */
                if (str[j] == '\\' && j + 1 < n) {
                    j++;
                    continue;
                }
                if (str[j] == str[i]) {
                    j++;
                    break;
                }
            }
            SET_COLOR(str, i, j, str[i] == '`' ? SQL_IDENTIFIER : SQL_STRING);
            i = j;
            continue;
        default:
            break;
        }
        i++;
        continue;
    }
    *statep = state;
}

#undef IN_COMMENT

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

#define IN_COMMENT   0x10
#define IN_STRING    0x20
#define IN_STRING2   0x40
#define IN_LONGLIT   0x80
#define IN_LEVEL     0x0F

enum {
    LUA_TEXT =         QE_STYLE_DEFAULT,
    LUA_COMMENT =      QE_STYLE_COMMENT,
    LUA_STRING =       QE_STYLE_STRING,
    LUA_LONGLIT =      QE_STYLE_STRING,
    LUA_NUMBER =       QE_STYLE_NUMBER,
    LUA_KEYWORD =      QE_STYLE_KEYWORD,
    LUA_FUNCTION =     QE_STYLE_FUNCTION,
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

void lua_colorize_line(unsigned int *str, int n, int *statep,
                       __unused__ int state_only)
{
    int i = 0, j = i, c, sep = 0, level = 0, level1, klen, style;
    int state = *statep;
    char kbuf[32];

    if (state & IN_LONGLIT) {
        /* either a comment or a string */
        level = state & IN_LEVEL;
        goto parse_longlit;
    }

    if (state & IN_STRING) {
        sep = '\'';
        state = 0;
        goto parse_string;
    }
    if (state & IN_STRING2) {
        sep = '\"';
        state = 0;
        goto parse_string;
    }

    while (i < n) {
        switch (c = str[i]) {
        case '-':
            if (str[i + 1] == '-') {
                if (str[i + 2] == '['
                &&  lua_long_bracket(str + i + 2, &level)) {
                    state = IN_COMMENT | IN_LONGLIT | (level & IN_LEVEL);
                    goto parse_longlit;
                }
                SET_COLOR(str, i, n, LUA_COMMENT);
                i = n;
                continue;
            }
            break;
        case '\'':
        case '\"':
            /* parse string const */
            sep = str[i];
            j = i + 1;
        parse_string:
            for (; j < n;) {
                c = str[j++];
                if (c == '\\') {
                    if (str[j] == 'z' && j + 1 == n) {
                        /* XXX: partial support for \z */
                        state = (sep == '\'') ? IN_STRING : IN_STRING2;
                        j += 1;
                    } else
                    if (j == n) {
                        state = (sep == '\'') ? IN_STRING : IN_STRING2;
                    } else {
                        j += 1;
                    }
                } else
                if (c == sep) {
                    break;
                }
            }
            SET_COLOR(str, i, j, LUA_STRING);
            i = j;
            continue;
        case '[':
            if (lua_long_bracket(str + i, &level)) {
                state = IN_LONGLIT | (level & IN_LEVEL);
                goto parse_longlit;
            }
            break;
        parse_longlit:
            style = (state & IN_COMMENT) ? LUA_COMMENT : LUA_LONGLIT;
            for (j = i; j < n; j++) {
                if (str[j] != ']')
                    continue;
                if (lua_long_bracket(str + j, &level1) && level1 == level) {
                    state = 0;
                    j += level + 2;
                    break;
                }
            }
            SET_COLOR(str, i, j, style);
            i = j;
            continue;
        default:
            if (qe_isdigit(c)) {
                /* XXX: should parse actual number syntax */
                for (j = i + 1; j < n; j++) {
                    if (!qe_isalnum(str[j] && str[j] != '.'))
                        break;
                }
                SET_COLOR(str, i, j, LUA_NUMBER);
                i = j;
                continue;
            }
            if (qe_isalpha_(c)) {
                for (klen = 0, j = i; qe_isalnum_(str[j]); j++) {
                    if (klen < countof(kbuf) - 1)
                        kbuf[klen++] = str[j];
                }
                kbuf[klen] = '\0';

                if (strfind(lua_keywords, kbuf)) {
                    SET_COLOR(str, i, j, LUA_KEYWORD);
                    i = j;
                    continue;
                }
                while (qe_isblank(str[j]))
                    j++;
                if (str[j] == '(') {
                    SET_COLOR(str, i, j, LUA_FUNCTION);
                    i = j;
                    continue;
                }
                i = j;
                continue;
            }
            break;
        }
        i++;
        continue;
    }
    *statep = state;
}

#undef IN_COMMENT
#undef IN_STRING
#undef IN_STRING2
#undef IN_LONGLIT
#undef IN_LEVEL

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

/*---------------- Haskell coloring ----------------*/

static char const haskell_keywords[] = {
    "|_|case|class|data|default|deriving|do|else|foreign"
    "|if|import|in|infix|infixl|infixr|instance|let"
    "|module|newtype|of|then|type|where"
    "|"
};

#define IN_COMMENT   0x10
#define IN_STRING    0x20
#define IN_LEVEL     0x0F

enum {
    HASKELL_TEXT =         QE_STYLE_DEFAULT,
    HASKELL_COMMENT =      QE_STYLE_COMMENT,
    HASKELL_STRING =       QE_STYLE_STRING,
    HASKELL_NUMBER =       QE_STYLE_NUMBER,
    HASKELL_KEYWORD =      QE_STYLE_KEYWORD,
    HASKELL_FUNCTION =     QE_STYLE_FUNCTION,
    HASKELL_SYMBOL =       QE_STYLE_NUMBER,
};

static inline int haskell_is_symbol(int c)
{
    return qe_findchar("!#$%&+./<=>?@\\^|-~:", c);
}

void haskell_colorize_line(unsigned int *str, int n, int *statep,
                           __unused__ int state_only)
{
    int i = 0, j = i, c, sep = 0, level = 0, klen;
    int state = *statep;
    char kbuf[32];

    if (state & IN_COMMENT)
        goto parse_comment;

    if (state & IN_STRING) {
        sep = '\"';
        state = 0;
        while (qe_isspace(str[j]))
            j++;
        if (str[j] == '\\')
            j++;
        goto parse_string;
    }

    while (i < n) {
        switch (c = str[i]) {
        case '-':
            if (str[i + 1] == '-' && !haskell_is_symbol(str[i + 2])) {
                SET_COLOR(str, i, n, HASKELL_COMMENT);
                i = n;
                continue;
            }
            goto parse_symbol;
        case '{':
            if (str[i + 1] == '-') {
                state |= IN_COMMENT;
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
            level = state & IN_LEVEL;
            for (j = i; j < n; j++) {
                if (str[i] == '{' && str[i + 1] == '-') {
                    level++;
                    j++;
                    continue;
                }
                if (str[i] == '-' && str[i + 1] == '}') {
                    j++;
                    level--;
                    if (level == 0) {
                        j++;
                        state &= ~IN_COMMENT;
                        break;
                    }
                }
            }
            state &= ~IN_COMMENT;
            state |= level & IN_COMMENT;
            SET_COLOR(str, i, j, HASKELL_COMMENT);
            i = j;
            continue;

        case '\'':
        case '\"':
            /* parse string const */
            sep = str[i];
            j = i + 1;
        parse_string:
            for (; j < n;) {
                c = str[j++];
                if (c == '\\') {
                    if (j == n) {
                        if (sep == '\"') {
                            /* XXX: should ignore whitespace */
                            state = IN_STRING;
                        }
                    } else
                    if (str[j] == '^' && j + 1 < n && str[j + 1] != sep) {
                        j += 2;
                    } else {
                        j += 1;
                    }
                } else
                if (c == sep) {
                    break;
                }
            }
            SET_COLOR(str, i, j, HASKELL_STRING);
            i = j;
            continue;

        default:
            if (qe_isdigit(c)) {
                j = i + 1;
                if (c == '0' && qe_tolower(str[j]) == 'o') {
                    /* octal numbers */
                    for (j += 1; qe_isoctdigit(str[j]); j++)
                        continue;
                } else
                if (c == '0' && qe_tolower(str[j]) == 'x') {
                    /* hexadecimal numbers */
                    for (j += 1; qe_isxdigit(str[j]); j++)
                        continue;
                } else {
                    /* decimal numbers */
                    for (j = i + 1; qe_isdigit(str[j]); j++)
                        continue;
                    if (str[j] == '.' && qe_isdigit(str[j + 1])) {
                        /* decimal floats require a digit after the '.' */
                        for (j = i + 2; qe_isdigit(str[j]); j++)
                            continue;
                        if (qe_tolower(str[j]) == 'e') {
                            int k = j + 1;
                            if (str[k] == '+' || str[k] == '-')
                                k++;
                            if (qe_isdigit(str[k])) {
                                for (j = k + 1; qe_isdigit(str[j]); j++)
                                    continue;
                            }
                        }
                    }
                }
                /* XXX: should detect malformed number constants */
                SET_COLOR(str, i, j, HASKELL_NUMBER);
                i = j;
                continue;
            }
            if (qe_isalpha_(c)) {
                for (klen = 0, j = i;
                     qe_isalnum_(str[j]) || str[j] == '\'';
                     j++) {
                    if (klen < countof(kbuf) - 1)
                        kbuf[klen++] = str[j];
                }
                kbuf[klen] = '\0';

                if (strfind(haskell_keywords, kbuf)) {
                    SET_COLOR(str, i, j, HASKELL_KEYWORD);
                    i = j;
                    continue;
                }
                while (qe_isblank(str[j]))
                    j++;
                if (str[j] == '(') {
                    SET_COLOR(str, i, j, HASKELL_FUNCTION);
                    i = j;
                    continue;
                }
                i = j;
                continue;
            }
        parse_symbol:
            if (haskell_is_symbol(c)) {
                for (j = i + 1; haskell_is_symbol(str[j]); j++)
                    continue;
                SET_COLOR(str, i, j, HASKELL_SYMBOL);
                i = j;
                continue;
            }
            break;
        }
        i++;
        continue;
    }
    *statep = state;
}

#undef IN_COMMENT
#undef IN_STRING
#undef IN_LEVEL

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

#define IN_COMMENT       0x80
#define IN_STRING        0x40
#define IN_STRING2       0x20
#define IN_LONG_STRING   0x10
#define IN_LONG_STRING2  0x08
#define IN_RAW_STRING    0x04

enum {
    PYTHON_TEXT =         QE_STYLE_DEFAULT,
    PYTHON_COMMENT =      QE_STYLE_COMMENT,
    PYTHON_STRING =       QE_STYLE_STRING,
    PYTHON_NUMBER =       QE_STYLE_NUMBER,
    PYTHON_KEYWORD =      QE_STYLE_KEYWORD,
    PYTHON_FUNCTION =     QE_STYLE_FUNCTION,
};

void python_colorize_line(unsigned int *str, int n, int *statep,
                           __unused__ int state_only)
{
    int i = 0, j = i, c, sep = 0, klen;
    int state = *statep;
    char kbuf[32];

    if (state & IN_STRING) {
        sep = '\'';
        goto parse_string;
    }
    if (state & IN_STRING2) {
        sep = '\"';
        goto parse_string;
    }
    if (state & IN_LONG_STRING) {
        sep = '\'';
        goto parse_long_string;
    }
    if (state & IN_LONG_STRING2) {
        sep = '\"';
        goto parse_long_string;
    }

    while (i < n) {
        switch (c = str[i]) {
        case '#':
            j = n;
            SET_COLOR(str, i, j, PYTHON_COMMENT);
            i = j;
            continue;
            
        case '\'':
        case '\"':
            /* parse string const */
            j = i;
        has_quote:
            sep = str[j++];
            if (str[j] == sep && str[j + 1] == sep) {
                /* long string */
                state = (sep == '\"') ? IN_LONG_STRING2 : IN_LONG_STRING;
                j += 2;
            parse_long_string:
                while (j < n) {
                    c = str[j++];
                    if (!(state & IN_RAW_STRING) && c == '\\') {
                        if (j < n) {
                            j += 1;
                        }
                    } else
                    if (c == sep && str[j] == sep && str[j + 1] == sep) {
                        j += 2;
                        state = 0;
                        break;
                    }
                }
            } else {
                state = (sep == '\"') ? IN_STRING2 : IN_STRING;
            parse_string:
                while (j < n) {
                    c = str[j++];
                    if (!(state & IN_RAW_STRING) && c == '\\') {
                        if (j < n) {
                            j += 1;
                        }
                    } else
                    if (c == sep) {
                        state = 0;
                        break;
                    }
                }
            }
            SET_COLOR(str, i, j, PYTHON_STRING);
            i = j;
            continue;

        case '.':
            if (qe_isdigit(str[i + 1])) {
                j = i;
                goto parse_decimal;
            }
            break;

        case 'b':
        case 'B':
            if (qe_tolower(str[i + 1]) == 'r'
            &&  (str[i + 2] == '\'' || str[i + 2] == '\"')) {
                state |= IN_RAW_STRING;
                j = i + 2;
                goto has_quote;
            }
            goto has_alpha;

        case 'r':
        case 'R':
            if (qe_tolower(str[i + 1]) == 'b'
            &&  (str[i + 2] == '\'' || str[i + 2] == '\"')) {
                state |= IN_RAW_STRING;
                j = i + 2;
                goto has_quote;
            }
            if ((str[i + 1] == '\'' || str[i + 1] == '\"')) {
                state |= IN_RAW_STRING;
                j = i + 1;
                goto has_quote;
            }
            goto has_alpha;

        default:
            if (qe_isdigit(c)) {
                j = i + 1;
                if (c == '0' && qe_tolower(str[j]) == 'b') {
                    /* binary numbers */
                    for (j += 1; qe_isbindigit(str[j]); j++)
                        continue;
                } else
                if (c == '0' && qe_tolower(str[j]) == 'o') {
                    /* octal numbers */
                    for (j += 1; qe_isoctdigit(str[j]); j++)
                        continue;
                } else
                if (c == '0' && qe_tolower(str[j]) == 'x') {
                    /* hexadecimal numbers */
                    for (j += 1; qe_isxdigit(str[j]); j++)
                        continue;
                } else {
                    /* decimal numbers */
                    for (j = i + 1; qe_isdigit(str[j]); j++)
                        continue;
                parse_decimal:
                    if (str[j] == '.' && qe_isdigit(str[j + 1])) {
                        /* decimal floats require a digit after the '.' */
                        for (j = i + 2; qe_isdigit(str[j]); j++)
                            continue;
                    }
                    if (qe_tolower(str[j]) == 'e') {
                        int k = j + 1;
                        if (str[k] == '+' || str[k] == '-')
                            k++;
                        if (qe_isdigit(str[k])) {
                            for (j = k + 1; qe_isdigit(str[j]); j++)
                                continue;
                        }
                    }
                }
                if (qe_tolower(str[j]) == 'j') {
                    j++;
                }
                    
                /* XXX: should detect malformed number constants */
                SET_COLOR(str, i, j, PYTHON_NUMBER);
                i = j;
                continue;
            }
        has_alpha:
            if (qe_isalpha_(c)) {
                for (klen = 0, j = i; qe_isalnum_(str[j]); j++) {
                    if (klen < countof(kbuf) - 1)
                        kbuf[klen++] = str[j];
                }
                kbuf[klen] = '\0';

                if (strfind(python_keywords, kbuf)) {
                    SET_COLOR(str, i, j, PYTHON_KEYWORD);
                    i = j;
                    continue;
                }
                while (qe_isblank(str[j]))
                    j++;
                if (str[j] == '(') {
                    SET_COLOR(str, i, j, PYTHON_FUNCTION);
                    i = j;
                    continue;
                }
                i = j;
                continue;
            }
            break;
        }
        i++;
        continue;
    }
    *statep = state;
}

#undef IN_COMMENT
#undef IN_STRING
#undef IN_STRING2
#undef IN_LONG_STRING
#undef IN_LONG_STRING2
#undef IN_RAW_STRING

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

#define IN_HEREDOC    0x80
#define IN_HD_INDENT  0x40
#define IN_HD_SIG     0x3f
#define IN_COMMENT    0x40
#define IN_STRING     0x20      /* single quote */
#define IN_STRING2    0x10      /* double quote */
#define IN_STRING3    0x08      /* back quote */
#define IN_STRING4    0x04      /* %q{...} */
#define IN_REGEX      0x02
#define IN_POD        0x01

enum {
    RUBY_TEXT =         QE_STYLE_DEFAULT,
    RUBY_COMMENT =      QE_STYLE_COMMENT,
    RUBY_STRING =       QE_STYLE_STRING,
    RUBY_STRING2 =      QE_STYLE_STRING,
    RUBY_STRING3 =      QE_STYLE_STRING,
    RUBY_STRING4 =      QE_STYLE_STRING,
    RUBY_REGEX =        QE_STYLE_STRING_Q,
    RUBY_NUMBER =       QE_STYLE_NUMBER,
    RUBY_KEYWORD =      QE_STYLE_KEYWORD,
    RUBY_FUNCTION =     QE_STYLE_FUNCTION,
    RUBY_MEMBER =       QE_STYLE_VARIABLE,
    RUBY_HEREDOC =      QE_STYLE_PREPROCESS,
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

void ruby_colorize_line(unsigned int *str, int n, int *statep,
                        __unused__ int state_only)
{
    int i = 0, j = i, c, indent, sig;
    static int sep, sep0, level;        /* XXX: ugly patch */
    int state = *statep;
    char kbuf[32];

    if (state & IN_HEREDOC) {
        if (state & IN_HD_INDENT) {
            while (qe_isspace(str[j]))
                j++;
        }
        if (qe_isalpha_(str[j])) {
            sig = str[j++] % 61;
            for (; qe_isalnum_(str[j]); j++) {
                sig = ((sig << 6) + str[j]) % 61;
            }
        }
        for (; qe_isspace(str[j]); j++)
            continue;
        SET_COLOR(str, i, n, RUBY_HEREDOC);
        i = n;
        if (j > 0 && j == n && (state & IN_HD_SIG) == (sig & IN_HD_SIG))
            state &= ~(IN_HEREDOC | IN_HD_INDENT | IN_HD_SIG);
    } else {
        if (state & IN_COMMENT)
            goto parse_c_comment;

        if (state & IN_REGEX)
            goto parse_regex;

        if (state & IN_STRING)
            goto parse_string;

        if (state & IN_STRING2)
            goto parse_string2;

        if (state & IN_STRING3)
            goto parse_string3;

        if (state & IN_STRING4)
            goto parse_string4;

        if (str[i] == '=' && qe_isalpha(str[i + 1])) {
            state |= IN_POD;
        }
        if (state & IN_POD) {
            if (ustrstart(str + i, "=end", NULL)) {
                state &= ~IN_POD;
            }
            if (str[i] == '=' && qe_isalpha(str[i + 1])) {
                SET_COLOR(str, i, n, RUBY_KEYWORD);
            } else {
                SET_COLOR(str, i, n, RUBY_COMMENT);
            }
            i = n;
        }
    }

    while (i < n && qe_isspace(str[i]))
        i++;

    indent = i;

    while (i < n) {
        switch (c = str[i]) {
        case '/':
            if (str[i + 1] == '*') {
                /* C comment */
                j = i + 2;
            parse_c_comment:
                state = IN_COMMENT;
                for (; j < n; j++) {
                    if (str[j] == '*' && str[j + 1] == '/') {
                        j += 2;
                        state &= ~IN_COMMENT;
                        break;
                    }
                }
                goto comment;
            }
            if (i == indent
            ||  (str[i + 1] != ' ' && str[i + 1] != '='
            &&   !qe_isalnum(str[i - 1] & CHAR_MASK)
            &&   str[i - 1] != ')')) {
                /* XXX: should use context to tell regex from divide */
                /* parse regex */
                j = i + 1;
                state = IN_REGEX;
            parse_regex:
                while (j < n) {
                    /* XXX: should ignore / inside char classes */
                    c = str[j++];
                    if (c == '\\') {
                        if (j < n) {
                            j += 1;
                        }
                    } else
                    if (c == '#' && str[j] == '{') {
                        /* should parse full syntax */
                        while (j < n && str[j++] != '}')
                            continue;
                    } else
                    if (c == '/') {
                        while (qe_findchar("ensuimox", str[j])) {
                            j++;
                        }
                        state = 0;
                        break;
                    }
                }
                SET_COLOR(str, i, j, RUBY_REGEX);
                i = j;
                continue;
            }
            break;

        case '#':
            j = n;
        comment:
            SET_COLOR(str, i, j, RUBY_COMMENT);
            i = j;
            continue;
            
        case '%':
            /* parse alternate string/array syntaxes */
            if (!qe_isspace(str[i + 1]) && !qe_isalnum(str[i + 1])) {
                j = i + 1;
                goto has_string4;
            }
            if (str[i + 1] == 'q' || str[i + 1] == 'Q'
            ||  str[i + 1] == 'r' || str[i + 1] == 'x'
            ||  str[i + 1] == 'w' || str[i + 1] == 'W') {
                j = i + 2;
            has_string4:
                level = 0;
                sep = sep0 = str[j++];
                if (sep == '{') sep = '}';
                if (sep == '(') sep = ')';
                if (sep == '[') sep = ']';
                if (sep == '<') sep = '>';
                /* parse special string const */
                state = IN_STRING4;
            parse_string4:
                while (j < n) {
                    c = str[j++];
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
                    if (c == '#' && str[j] == '{') {
                        /* XXX: should no parse if %q */
                        /* XXX: should parse full syntax */
                        while (j < n && str[j++] != '}')
                            continue;
                    } else
                    if (c == '\\') {
                        if (j < n) {
                            j += 1;
                        }
                    }
                }
                SET_COLOR(str, i, j, RUBY_STRING4);
                i = j;
                continue;
            }
            break;

        case '\'':
            /* parse single quoted string const */
            j = i + 1;
            state = IN_STRING;
        parse_string:
            while (j < n) {
                c = str[j++];
                if (c == '\\' && (str[j] == '\\' || str[j] == '\'')) {
                    j += 1;
                } else
                if (c == '\'') {
                    state = 0;
                    break;
                }
            }
            SET_COLOR(str, i, j, RUBY_STRING);
            i = j;
            continue;

        case '`':
            /* parse single quoted string const */
            j = i + 1;
            state = IN_STRING3;
        parse_string3:
            while (j < n) {
                c = str[j++];
                if (c == '\\' && (str[j] == '\\' || str[j] == '\'')) {
                    j += 1;
                } else
                if (c == '#' && str[j] == '{') {
                    /* should parse full syntax */
                    while (j < n && str[j++] != '}')
                        continue;
                } else
                if (c == '`') {
                    state = 0;
                    break;
                }
            }
            SET_COLOR(str, i, j, RUBY_STRING3);
            i = j;
            continue;

        case '\"':
            /* parse double quoted string const */
            c = '\0';
            j = i + 1;
        parse_string2:
            while (j < n) {
                c = str[j++];
                if (c == '\\') {
                    if (j < n) {
                        j += 1;
                    }
                } else
                if (c == '#' && str[j] == '{') {
                    /* should parse full syntax */
                    while (j < n && str[j++] != '}')
                        continue;
                } else
                if (c == '\"') {
                    break;
                }
            }
            if (c == '\"') {
                if (state == IN_STRING2)
                    state = 0;
            } else {
                if (state == 0)
                    state = IN_STRING2;
            }
            SET_COLOR(str, i, j, RUBY_STRING2);
            i = j;
            continue;

        case '<':
            if (str[i + 1] == '<') {
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
                j = i + 2;
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
                    state &= ~(IN_HEREDOC | IN_HD_INDENT | IN_HD_SIG);
                    state |= IN_HEREDOC;
                    if (str[i + 2] == '-') {
                        state |= IN_HD_INDENT;
                    }
                    state |= (sig & IN_HD_SIG);
                    SET_COLOR(str, i, j, RUBY_HEREDOC);
                    i = j;
                }
            }
            break;

        case '?':
            /* XXX: should parse character constants */
            break;

        case '.':
            if (qe_isdigit_(str[i + 1])) {
                j = i;
                goto parse_decimal;
            }
            break;

        case '$':
            /* XXX: should parse precise $ syntax,
             * skip $" and $' for now
             */
            if (i + 1 < n)
                i++;
            break;

        case ':':
            /* XXX: should parse Ruby symbol */
            break;

        case '@':
            j = i + 1;
            j += ruby_get_name(kbuf, countof(kbuf), str + j);
            SET_COLOR(str, i, j, RUBY_MEMBER);
            i = j;
            continue;

        default:
            if (qe_isdigit(c)) {
                j = i + 1;
                if (c == '0' && qe_tolower(str[j]) == 'b') {
                    /* binary numbers */
                    for (j += 1; qe_isbindigit(str[j]) || str[j] == '_'; j++)
                        continue;
                } else
                if (c == '0' && qe_tolower(str[j]) == 'o') {
                    /* octal numbers */
                    for (j += 1; qe_isoctdigit(str[j]) || str[j] == '_'; j++)
                        continue;
                } else
                if (c == '0' && qe_tolower(str[j]) == 'x') {
                    /* hexadecimal numbers */
                    for (j += 1; qe_isxdigit(str[j]) || str[j] == '_'; j++)
                        continue;
                } else
                if (c == '0' && qe_tolower(str[j]) == 'd') {
                    /* hexadecimal numbers */
                    for (j += 1; qe_isdigit_(str[j]); j++)
                        continue;
                } else {
                    /* decimal numbers */
                    for (j = i + 1; qe_isdigit_(str[j]); j++)
                        continue;
                parse_decimal:
                    if (str[j] == '.') {
                        for (j = i + 1; qe_isdigit_(str[j]); j++)
                            continue;
                    }
                    if (qe_tolower(str[j]) == 'e') {
                        int k = j + 1;
                        if (str[k] == '+' || str[k] == '-')
                            k++;
                        if (qe_isdigit_(str[k])) {
                            for (j = k + 1; qe_isdigit_(str[j]); j++)
                                continue;
                        }
                    }
                }
                    
                /* XXX: should detect malformed number constants */
                SET_COLOR(str, i, j, RUBY_NUMBER);
                i = j;
                continue;
            }
            if (qe_isalpha_(c)) {
                j = i;
                j += ruby_get_name(kbuf, countof(kbuf), str + j);

                if (strfind(ruby_keywords, kbuf)) {
                    SET_COLOR(str, i, j, RUBY_KEYWORD);
                    i = j;
                    continue;
                }
                while (qe_isblank(str[j]))
                    j++;
                if (str[j] == '(') {
                    SET_COLOR(str, i, j, RUBY_FUNCTION);
                    i = j;
                    continue;
                }
                i = j;
                continue;
            }
            break;
        }
        i++;
        continue;
    }
    *statep = state;
}

#undef IN_HEREDOC
#undef IN_HD_INDENT
#undef IN_HD_SIG
#undef IN_COMMENT
#undef IN_STRING
#undef IN_STRING2
#undef IN_STRING3
#undef IN_STRING4
#undef IN_REGEX
#undef IN_POD

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

/*----------------*/

static int extra_modes_init(void)
{
    asm_init();
    basic_init();
    pascal_init();
    ini_init();
    ps_init();
    sql_init();
    lua_init();
    haskell_init();
    python_init();
    ruby_init();
    return 0;
}

qe_module_init(extra_modes_init);
