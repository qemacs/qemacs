/*
 * Miscellaneous language modes for QEmacs.
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
    int i = 0, start = 0, c, style = 0, len, wn = 0; /* word number on line */
    int colstate = cp->colorize_state;

    if (colstate)
        goto in_comment;

    for (; i < n && qe_isblank(str[i]); i++)
        continue;

    while (i < n) {
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
            style = ASM_STYLE_PREPROCESS;
            break;
        case ';':
            i = n;
            style = ASM_STYLE_COMMENT;
            break;
        case '\'':
        case '\"':
            /* parse string const */
            while (i < n && str[i++] != (unsigned int)c)
                continue;
            style = ASM_STYLE_STRING;
            break;
        default:
            /* parse numbers */
            if (qe_isdigit(c)) {
                for (; qe_isalnum(str[i]); i++)
                    continue;
                style = ASM_STYLE_NUMBER;
                break;
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
                    if (strequal(keyword, "comment") && n > i) {
                        SET_COLOR(str, start, i, ASM_STYLE_PREPROCESS);
                        for (; i < n && qe_isblank(str[i]); i++)
                            continue;
                        start = i;
                        colstate = str[i++];  /* end of comment character */
                        /* skip characters upto and including separator */
                    in_comment:
                        while (i < n) {
                            if ((char)str[i++] == (char)colstate) {
                                colstate = 0;
                                break;
                            }
                        }
                        style = ASM_STYLE_COMMENT;
                        break;
                    }
                    if (strfind(asm_prepkeywords1, keyword))
                        goto prep;
                } else
                if (wn == 2) {
                    if (strfind(asm_prepkeywords2, keyword)) {
                        style = ASM_STYLE_PREPROCESS;
                        break;
                    }
                }
                //SET_COLOR(str, start, i, ASM_STYLE_IDENTIFIER);
                continue;
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

/*----------------*/

static int extra_modes_init(void)
{
    asm_init();
    ini_init();
    sharp_init();
    ps_init();
    emf_init();
    return 0;
}

qe_module_init(extra_modes_init);
