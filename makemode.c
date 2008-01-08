/*
 * Makefile mode for QEmacs.
 *
 * Copyright (c) 2000-2008 Charlie Gordon.
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

/*---------------- Makefile colors ----------------*/

#define MAKEFILE_TEXT           QE_STYLE_DEFAULT
#define MAKEFILE_COMMENT        QE_STYLE_COMMENT
#define MAKEFILE_STRING         QE_STYLE_STRING
#define MAKEFILE_PREPROCESS     QE_STYLE_PREPROCESS
#define MAKEFILE_TARGET         QE_STYLE_FUNCTION
#define MAKEFILE_VARIABLE       QE_STYLE_VARIABLE
#define MAKEFILE_MACRO          QE_STYLE_TYPE

static void makefile_colorize_line(unsigned int *str, int n, int *statep,
                                   int state_only)
{
    int i, j, level;

    i = j = 0;

    /* CG: should check for end of word */
    if (str[0] == 'i') {
        if (ustristart(str, "ifeq", NULL)
        ||  ustristart(str, "ifneq", NULL)
        ||  ustristart(str, "ifdef", NULL)
        ||  ustristart(str, "ifndef", NULL)
        ||  ustristart(str, "include", NULL))
            goto preprocess;
    } else
    if (str[0] == 'e') {
        if (ustristart(str, "else", NULL)
        ||  ustristart(str, "endif", NULL))
            goto preprocess;
    }

    while (i < n) {
        switch (str[i]) {
        case '$':
            i += 1;
            j = i + 1;
            if (str[i] == '(') {
                i += 1;
                level = 1;
                for (j = i; j < n; j++) {
                    if (str[j] == '(')
                        level++;
                    if (str[j] == ')' && --level <= 0)
                        break;
                    if (str[j] == ' ' || str[j] == '$') {
                        /* should have function color */
                        j = i;
                        break;
                    }
                }
            }
            if (i < j)
                set_color(str + i, str + j, MAKEFILE_MACRO);
            i = j;
            continue;
        case ' ':
        case '\t':
            if (i == 0)
                j = 1;
            break;
        case '+':
            if (!j && str[i+1] == '=')
                goto variable;
        case ':':
            if (j)
                break;
            if (str[i+1] == '=')
                goto variable;
            set_color(str + j, str + i, MAKEFILE_TARGET);
            break;
        case '=':
            if (j)
                break;
        variable:
            set_color(str + j, str + i, MAKEFILE_VARIABLE);
            break;
        case '#':
            if (i > 0 && str[i - 1] == '\\')
                break;
            set_color(str + i, str + n, MAKEFILE_COMMENT);
            i = n;
            continue;
        case '!':
            /*          case '.':*/
            if (i > 0)
                break;
        preprocess:
            /* scan for comment */
            for (j = i + 1; j < n; j++) {
                if (str[j] == '#')
                    break;
            }
            set_color(str + i, str + j, MAKEFILE_PREPROCESS);
            i = j;
            continue;
        case '\'':
        case '`':
        case '"':
            /* parse string const */
            for (j = i + 1; j < n; j++) {
                if (str[j] == str[i]) {
                    j++;
                    break;
                }
            }
            set_color(str + i, str + j, MAKEFILE_STRING);
            i = j;
            continue;
        default:
            break;
        }
        i++;
    }
}

#undef MAKEFILE_TEXT
#undef MAKEFILE_COMMENT
#undef MAKEFILE_STRING
#undef MAKEFILE_PREPROCESS
#undef MAKEFILE_VARIABLE
#undef MAKEFILE_TARGET
#undef MAKEFILE_MACRO

static int makefile_mode_probe(ModeProbeData *p)
{
    const char *base = basename(p->filename);

    /* check file name or extension */
    if (match_extension(base, "mk|mak")
    ||  stristart(base, "makefile", NULL))
        return 70;

    return 0;
}

static int makefile_mode_init(EditState *s, ModeSavedData *saved_data)
{
    int ret;

    ret = text_mode_init(s, saved_data);
    if (ret)
        return ret;
    set_colorize_func(s, makefile_colorize_line);
    return ret;
}

/* specific makefile commands */
static CmdDef makefile_commands[] = {
    CMD_DEF_END,
};

static ModeDef makefile_mode;

static int makefile_init(void)
{
    /* c mode is almost like the text mode, so we copy and patch it */
    memcpy(&makefile_mode, &text_mode, sizeof(ModeDef));
    makefile_mode.name = "Makefile";
    makefile_mode.mode_probe = makefile_mode_probe;
    makefile_mode.mode_init = makefile_mode_init;

    qe_register_mode(&makefile_mode);
    qe_register_cmd_table(makefile_commands, &makefile_mode);

    return 0;
}

qe_module_init(makefile_init);
