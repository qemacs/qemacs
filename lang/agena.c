/*
 * Agena language mode for QEmacs.
 *
 * Copyright (c) 2000-2022 Charlie Gordon.
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

/*---------------- AGENA script coloring ----------------*/

#define AGN_SHORTSTRINGDELIM " ,~[]{}();:#'=?&%$\xA7\\!^@<>|\r\n\t"

enum {
    IN_AGENA_COMMENT = 0x01,
    IN_AGENA_STRING1 = 0x02,
    IN_AGENA_STRING2 = 0x04,
};

enum {
    AGENA_STYLE_TEXT =       QE_STYLE_DEFAULT,
    AGENA_STYLE_COMMENT =    QE_STYLE_COMMENT,
    AGENA_STYLE_STRING =     QE_STYLE_STRING,
    AGENA_STYLE_NUMBER =     QE_STYLE_NUMBER,
    AGENA_STYLE_KEYWORD =    QE_STYLE_KEYWORD,
    AGENA_STYLE_TYPE =       QE_STYLE_TYPE,
    AGENA_STYLE_FUNCTION =   QE_STYLE_FUNCTION,
    AGENA_STYLE_IDENTIFIER = QE_STYLE_DEFAULT,
};

static char const agena_keywords[] = {
    /* abs alias and antilo2 antilog10 arccos arcsec arcsin arctan as
    assigned atendof bea bottom break by bye case catch char cis clear cls
    conjugate copy cos cosh cosxx create dec delete dict div do downto
    duplicate elif else end entier enum esac even exchange exp fail false fi
    filled first finite flip for from global if imag import in inc infinity
    insert int intersect into is join keys last left ln lngamma local lower
    minus mul nan nand nargs nor not numeric od of onsuccess or pop proc
    qmdev qsadd real redo reg relaunch replace restart return right rotate
    sadd seq shift sign signum sin sinc sinh size skip smul split sqrt
    subset tan tanh then to top trim true try type typeof unassigned
    undefined union unique until upper values when while xor xsubset
    yrt */

    /* keywords */
    "|alias|as|bottom|break|by|case|catch|clear|cls|create|dec|delete"
    "|dict|div|do|duplicate|elif|else|end|enum|epocs|esac|external|exchange"
    "|fi|for|from|if|import|inc|insert|into|is|keys|mul|nargs"
    "|od|of|onsuccess|pop|proc|quit|redo|reg|relaunch|return|rotate"
    "|scope|seq|skip|then|try|to|top|try|until|varargs"
    "|when|while|yrt"
    "|readlib"
    /* constants */
    "|infinity|nan|I"
    /* operators */
    "|or|xor|nor|and|nand|in|subset|xsubset|union|minus|intersect|atendof"
    "|split|shift|not"
    "|assigned|unassigned|size|type|typeof|left|right|filled|finite"
    "|"
};

static char const agena_types[] = {
    "|boolean|complex|lightuserdata|null|number|pair|register|procedure"
    "|sequence|set|string|table|thread|userdata"
    "|global|local|char|float|undefined|true|false|fail"
    "|"
};

static void agena_colorize_line(QEColorizeContext *cp,
                                char32_t *str, int n, ModeDef *syn)
{
    char kbuf[16];
    int i = 0, start = i, style = 0;
    int state = cp->colorize_state;
    char32_t c, sep = 0;

    if (state & IN_AGENA_COMMENT)
        goto parse_block_comment;
    if (state & IN_AGENA_STRING1)
        goto parse_string1;
    if (state & IN_AGENA_STRING2)
        goto parse_string2;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            if (str[i] == '/') {
                /* block comment */
                i++;
            parse_block_comment:
                state |= IN_AGENA_COMMENT;
                for (; i < n; i++) {
                    if (str[i] == '/' && str[i + 1] == '#') {
                        i += 2;
                        state &= ~IN_AGENA_COMMENT;
                        break;
                    }
                }
            } else {
                i = n;
            }
            style = AGENA_STYLE_COMMENT;
            break;
        case '\"':
            state = IN_AGENA_STRING2;
        parse_string2:
            sep = '\"';
            goto parse_string;
        case '\'':
            state = IN_AGENA_STRING1;
        parse_string1:
            sep = '\'';
            /* parse string const */
        parse_string:
            for (; i < n; i++) {
                if (str[i] == '\\' && i + 1 < n) {
                    i++;
                    continue;
                }
                if (str[i] == sep) {
                    state = 0;
                    i++;
                    break;
                }
            }
            style = AGENA_STYLE_STRING;
            break;
        case '`':   /* short string */
            while (i < n && !qe_findchar(AGN_SHORTSTRINGDELIM, str[i]))
                i++;
            style = AGENA_STYLE_IDENTIFIER;
            break;
        default:
            /* parse identifiers and keywords */
            if (qe_isalpha_(c)) {
                i += ustr_get_identifier(kbuf, countof(kbuf), c, str, i, n);
                if (strfind(syn->keywords, kbuf))
                    style = AGENA_STYLE_KEYWORD;
                else
                if (strfind(syn->types, kbuf))
                    style = AGENA_STYLE_TYPE;
                else
                if (check_fcall(str, i))
                    style = AGENA_STYLE_FUNCTION;
                else
                    style = AGENA_STYLE_IDENTIFIER;
                break;
            }
            if (qe_isdigit(c) || (c == '.' && qe_isdigit(str[i]))) {
                while (qe_isdigit_(str[i]) || str[i] == '\'' || str[i] == '.')
                    i++;
                if (qe_findchar("eE", str[i])) {
                    i++;
                    if (qe_findchar("+-", str[i]))
                        i++;
                }
                while (qe_isalnum(str[i]))
                    i++;
                style = AGENA_STYLE_NUMBER;
                break;
            }
            continue;
        }
        if (style) {
            SET_COLOR(str, start, i, style);
            style = 0;
        }
    }
    cp->colorize_state = state;
}

static ModeDef agena_mode = {
    .name = "Agena",
    .extensions = "agn",
    .keywords = agena_keywords,
    .types = agena_types,
    .colorize_func = agena_colorize_line,
};

static int agena_init(void)
{
    qe_register_mode(&agena_mode, MODEF_SYNTAX);

    return 0;
}

qe_module_init(agena_init);
