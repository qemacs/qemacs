/*
 * asm language mode for QEmacs.
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
    char keyword[16];
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

qe_module_init(asm_init);
