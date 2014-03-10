/*
 * Lisp Source mode for QEmacs.
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

/* TODO: lisp-indent = 2 */

/*---------------- Lisp colors ----------------*/

#define IN_COMMENT      0x01
#define IN_STRING       0x02

#define LISP_TEXT       QE_STYLE_DEFAULT
#define LISP_COMMENT    QE_STYLE_COMMENT
#define LISP_STRING     QE_STYLE_STRING

static void lisp_colorize_line(unsigned int *str, int n, int *statep,
                               __unused__ int state_only)
{
    int colstate = *statep;
    int i = 0, j = 0;

    if (colstate & IN_STRING) {
        for (j = i; j < n;) {
            if (str[j] == '\\' && ++j < n) {
                j++;
            } else
            if (str[j++] == '"') {
                colstate &= ~IN_STRING;
                break;
            }
        }
        SET_COLOR(str, i, j, LISP_STRING);
        i = j;
    }
    if (colstate & IN_COMMENT) {
        for (j = i; j < n; j++) {
            if (str[j] == '|' && j + 1 < n && str[j + 1] == '#') {
                j += 2;
                colstate &= ~IN_COMMENT;
                break;
            }
        }
        SET_COLOR(str, i, j, LISP_COMMENT);
        i = j;
    }
    while (i < n) {
        switch (str[i]) {
        case ';':
            SET_COLOR(str, i, n, LISP_COMMENT);
            i = n;
            continue;
        case '#':
            /* check for block comment */
            if (str[i + 1] == '|') {
                colstate |= IN_COMMENT;
                for (j = i + 2; j < n; j++) {
                    if (str[j] == '|' && str[j + 1] == '#') {
                        j += 2;
                        colstate &= ~IN_COMMENT;
                        break;
                    }
                }
                SET_COLOR(str, i, j, LISP_COMMENT);
                i = j;
                continue;
            }
            break;
        case '"':
            /* parse string const */
            colstate |= IN_STRING;
            for (j = i + 1; j < n;) {
                if (str[j++] == '"') {
                    colstate &= ~IN_STRING;
                    break;
                }
            }
            SET_COLOR(str, i, j, LISP_STRING);
            i = j;
            continue;
        default:
            break;
        }
        i++;
    }
    *statep = colstate;
}

#undef IN_STRING
#undef IN_COMMENT
#undef LISP_TEXT
#undef LISP_COMMENT
#undef LISP_STRING

static int lisp_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    /* check file name or extension */
    if (match_extension(p->filename, mode->extensions)
    ||  strstart(p->filename, ".emacs", NULL))
        return 80;

    return 1;
}

/* specific lisp commands */
static CmdDef lisp_commands[] = {
    CMD_DEF_END,
};

ModeDef lisp_mode;

static int lisp_init(void)
{
    /* lisp mode is almost like the text mode, so we copy and patch it */
    memcpy(&lisp_mode, &text_mode, sizeof(ModeDef));
    lisp_mode.name = "Lisp";
    lisp_mode.extensions = "ll|li|lh|lo|lm|lisp|el";
    lisp_mode.mode_probe = lisp_mode_probe;
    lisp_mode.colorize_func = lisp_colorize_line;

    qe_register_mode(&lisp_mode);
    qe_register_cmd_table(lisp_commands, &lisp_mode);

    return 0;
}

qe_module_init(lisp_init);
