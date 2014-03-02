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

#define SET_COLOR(str,a,b,style)  set_color((str) + (a), (str) + (b), style)

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
        case '"':
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
        case '"':
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
                    if (strchr("$&!@%#", str[j]))
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
        case '"':
            /* parse string const */
            for (j = i + 1; j < n; j++) {
                if (str[j] == '"') {
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
        if (*p == '[') {
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

#define ispssep(c)      (strchr(" \t\r\n,()<>[]{}/", (c)) != NULL)
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
        case '"':
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

/*----------------*/

static int extra_modes_init(void)
{
    asm_init();
    basic_init();
    pascal_init();
    ini_init();
    ps_init();
    sql_init();
    return 0;
}

qe_module_init(extra_modes_init);
