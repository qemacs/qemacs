/*
 * Shell script mode for QEmacs.
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

/*---------------- Shell script colors ----------------*/

enum {
    SCRIPT_STYLE_TEXT =       QE_STYLE_DEFAULT,
    SCRIPT_STYLE_COMMENT =    QE_STYLE_COMMENT,
    SCRIPT_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    SCRIPT_STYLE_COMMAND =    QE_STYLE_FUNCTION,
    SCRIPT_STYLE_VARIABLE =   QE_STYLE_TYPE,
    SCRIPT_STYLE_STRING =     QE_STYLE_STRING,
    SCRIPT_STYLE_BACKTICK =   QE_STYLE_STRING_Q,
};

static int script_var(const unsigned int *str, int j, int n)
{
    for (; j < n; j++) {
        if (qe_isalnum_(str[j]) || str[j] == '-')
            continue;
        break;
    }
    return j;
}

static void script_colorize_line(QEColorizeContext *cp,
                                 unsigned int *str, int n, int mode_flags)
{
    int i = 0, j, c, start, style;

    style = SCRIPT_STYLE_COMMAND;

    while (i < n) {
        start = i;
        switch (str[i]) {
        case '#':
            if (i > 0 && str[i - 1] == '$')
                break;
            style = SCRIPT_STYLE_COMMENT;
            if (str[i + 1] == '!')
                style = SCRIPT_STYLE_PREPROCESS;
            i = n;
            SET_COLOR(str, start, i, style);
            continue;
        case '`':
            style = SCRIPT_STYLE_BACKTICK;
            goto has_string;
        case '\'':
        case '"':
            style = SCRIPT_STYLE_STRING;
        has_string:
            /* parse string const */
            for (i++; i < n;) {
                c = str[i++];
                if (c == '\\' && i < n && str[start] == '"')
                    i++;
		else
                if (c == str[start])
                    break;
            }
            SET_COLOR(str, start, i, style);
            if (i < n)
                style = SCRIPT_STYLE_TEXT;
            continue;
        case ' ':
        case '\t':
            break;
        default:
            i = script_var(str, i, n);
            if (i > start) {
                j = i;
                while (qe_isblank(str[j]))
                    j++;
                if (str[j] == '=')
                    style = SCRIPT_STYLE_VARIABLE;
                SET_COLOR(str, start, i, style);
                style = SCRIPT_STYLE_TEXT;
                continue;
            }
            // Should support << syntax
            // Should support $ syntax
            style = SCRIPT_STYLE_TEXT;
            break;
        }
        i++;
    }
}

static int script_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (match_extension(p->filename, mode->extensions))
        return 80;

    if (p->buf[0] == '#') {
        if (p->buf[1] == '!')
            return 60;
        if (p->buf[1] == ' ')
            return 30;
    }
    return 1;
}

static ModeDef script_mode;

static int script_init(void)
{
    /* Shell-script mode is almost like the text mode, so we copy and patch it */
    memcpy(&script_mode, &text_mode, sizeof(ModeDef));
    script_mode.name = "Shell-script";
    script_mode.extensions = "sh|bash|csh|ksh|zsh";
    script_mode.mode_probe = script_mode_probe;
    script_mode.colorize_func = script_colorize_line;

    qe_register_mode(&script_mode, MODEF_SYNTAX);

    return 0;
}

qe_module_init(script_init);
