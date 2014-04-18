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

static const char lisp_keywords[] = {
    "defun|defvar|let|let*|if|concat|list|set|setq|when|and|or|max|min|"
    "unless|car|cdr|cons|cond|prog1|progn|case|setcar|setcdr|while|"
    "defsubst|eq|remove|not|otherwise|dolist|incf|decf|boundp|"
    "1+|1-|<|>|<=|>=|-|+|*|/|=|<>|/=|"
    //"interactive|"
};

static const char lisp_types[] = {
    "nil|t|"
};

enum {
    IN_LISP_COMMENT = 0x01,
    IN_LISP_STRING  = 0x02,
};

enum {
    LISP_STYLE_TEXT      = QE_STYLE_DEFAULT,
    LISP_STYLE_COMMENT   = QE_STYLE_COMMENT,
    LISP_STYLE_NUMBER    = QE_STYLE_NUMBER,
    LISP_STYLE_STRING    = QE_STYLE_STRING,
    LISP_STYLE_CHARCONST = QE_STYLE_STRING_Q,
    LISP_STYLE_KEYWORD   = QE_STYLE_KEYWORD,
    LISP_STYLE_TYPE      = QE_STYLE_TYPE,
    LISP_STYLE_QSYMBOL   = QE_STYLE_PREPROCESS,
    LISP_STYLE_MACRO     = QE_STYLE_TAG,
};

static int lisp_get_symbol(char *buf, int buf_size, unsigned int *p)
{
    buf_t outbuf, *out;
    unsigned int c;
    int i;

    out = buf_init(&outbuf, buf, buf_size);

    for (i = 0; (c = p[i]) != '\0'; i++) {
        if (qe_isspace(c) || qe_findchar(";(){}[]#'`,\"", c))
            break;
        buf_putc_utf8(out, c);
    }
    return i;
}

static int lisp_is_number(const char *str)
{
    int i;

    /* XXX: parse other syntaxes, ie hex constants */
    if (qe_isdigit(*str)) {
        for (; qe_isdigit(*str); str++)
            continue;
        if (*str == '.') {
            for (str++; qe_isdigit(*str); str++)
                continue;
        }
        if (qe_tolower(*str) == 'e') {
            i = 1;
            if (str[i] == '+' || str[i] == '-')
                i++;
            if (qe_isdigit(str[i])) {
                for (str += i + 1; qe_isdigit(*str); str++)
                    break;
            }
        }
    }
    return (*str) ? 0 : 1;
}

static void lisp_colorize_line(QEColorizeContext *cp,
                               unsigned int *str, int n, int mode_flags)
{
    int colstate = cp->colorize_state;
    int i = 0, start = i, len;
    char kbuf[32];

    if (colstate & IN_LISP_STRING) {
        while (i < n) {
            if (str[i] == '\\' && ++i < n) {
                i++;
            } else
            if (str[i++] == '"') {
                colstate &= ~IN_LISP_STRING;
                break;
            }
        }
        SET_COLOR(str, start, i, LISP_STYLE_STRING);
    }
    if (colstate & IN_LISP_COMMENT) {
        for (; i < n; i++) {
            if (str[i] == '|' && str[i + 1] == '#') {
                i += 2;
                colstate &= ~IN_LISP_COMMENT;
                break;
            }
        }
        SET_COLOR(str, start, i, LISP_STYLE_COMMENT);
    }
    while (i < n) {
        start = i;
        switch (str[i++]) {
        case '`':
        case ',':
            SET_COLOR(str, start, i, LISP_STYLE_MACRO);
            continue;
        case ';':
            i = n;
            SET_COLOR(str, start, i, LISP_STYLE_COMMENT);
            continue;
        case '#':
            /* check for block comment */
            if (str[i] == '|') {
                colstate |= IN_LISP_COMMENT;
                for (i++; i < n; i++) {
                    if (str[i] == '|' && str[i + 1] == '#') {
                        i += 2;
                        colstate &= ~IN_LISP_COMMENT;
                        break;
                    }
                }
                SET_COLOR(str, start, i, LISP_STYLE_COMMENT);
                continue;
            }
            break;
        case '"':
            /* parse string const */
            colstate |= IN_LISP_STRING;
            while (i < n) {
                if (str[i] == '\\' && ++i < n) {
                    i++;
                } else
                if (str[i++] == '"') {
                    colstate &= ~IN_LISP_STRING;
                    break;
                }
            }
            SET_COLOR(str, start, i, LISP_STYLE_STRING);
            continue;
        case '?':
            /* parse char const */
            /* XXX: Should parse keys syntax */
            if (str[i] == '\\' && i + 1 < n) {
                i += 2;
            } else
            if (i < n) {
                i += 1;
            }
            SET_COLOR(str, start, i, LISP_STYLE_CHARCONST);
            continue;
        case '\'':
            len = lisp_get_symbol(kbuf, sizeof(kbuf), str + i);
            if (len > 0) {
                i += len;
                SET_COLOR(str, start, i, LISP_STYLE_QSYMBOL);
                continue;
            }
            break;
        default:
            len = lisp_get_symbol(kbuf, sizeof(kbuf), str + i - 1);
            if (len > 0) {
                i += len - 1;
                if (lisp_is_number(kbuf)) {
                    SET_COLOR(str, start, i, LISP_STYLE_NUMBER);
                    continue;
                }
                if (strfind(lisp_keywords, kbuf)) {
                    SET_COLOR(str, start, i, LISP_STYLE_KEYWORD);
                    continue;
                }
                if (strfind(lisp_types, kbuf)) {
                    SET_COLOR(str, start, i, LISP_STYLE_TYPE);
                    continue;
                }
                /* skip other symbol */
                continue;
            }
            break;
        }
    }
    cp->colorize_state = colstate;
}

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
