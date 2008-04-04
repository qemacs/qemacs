/*
 * Lisp Source mode for QEmacs.
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

/* TODO: lisp-indent = 2 */

static const char *lisp_mode_extensions = "ll|li|lh|lo|lm|lisp|el";

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

    if (colstate & IN_STRING)
        goto instring;

    if (colstate & IN_COMMENT) {
        for (j = i; j < n; j++) {
            if (str[j] == '|'
            &&  str[j + 1] == '#') {
                j += 2;
                colstate = 0;
                break;
            }
        }
        set_color(str + i, str + j, LISP_COMMENT);
        i = j;
    }
    while (i < n) {
        switch (str[i]) {
        case ';':
            set_color(str + i, str + n, LISP_COMMENT);
            i = n;
            continue;
        case '#':
            /* check for block comment */
            if (str[i + 1] == '|') {
                colstate = IN_COMMENT;
                for (j = i + 2; j < n; j++) {
                    if (str[j] == '|'
                    &&  str[j + 1] == '#') {
                        j += 2;
                        colstate = 0;
                        break;
                    }
                }
                set_color(str + i, str + j, LISP_COMMENT);
                i = j;
                continue;
            }
            break;
        case '"':
            /* parse string const */
            for (j = i + 1; j < n;) {
              instring:
                colstate |= IN_STRING;
                if (str[j++] == '"') {
                    colstate &= ~IN_STRING;
                    break;
                }
            }
            set_color(str + i, str + j, LISP_STRING);
            i = j;
            continue;
        default:
            break;
        }
        i++;
        continue;
    }
    *statep = colstate;
}

#undef IN_STRING
#undef IN_COMMENT
#undef LISP_TEXT
#undef LISP_COMMENT
#undef LISP_STRING

static int lisp_mode_probe(ModeProbeData *p)
{
    /* just check file extension */
    if (match_extension(p->filename, lisp_mode_extensions)
    ||  strstart(p->filename, ".emacs", NULL))
        return 80;

    return 0;
}

static int lisp_mode_init(EditState *s, ModeSavedData *saved_data)
{
    int ret;

    ret = text_mode_init(s, saved_data);
    if (ret)
        return ret;
    set_colorize_func(s, lisp_colorize_line);
    return ret;
}

/* specific lisp commands */
static CmdDef lisp_commands[] = {
    CMD_DEF_END,
};

static ModeDef lisp_mode;

static int lisp_init(void)
{
    /* c mode is almost like the text mode, so we copy and patch it */
    memcpy(&lisp_mode, &text_mode, sizeof(ModeDef));
    lisp_mode.name = "Lisp";
    lisp_mode.mode_probe = lisp_mode_probe;
    lisp_mode.mode_init = lisp_mode_init;

    qe_register_mode(&lisp_mode);
    qe_register_cmd_table(lisp_commands, &lisp_mode);

    return 0;
}

qe_module_init(lisp_init);
