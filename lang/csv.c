/*
 * CSV file mode for QEmacs.
 *
 * Copyright (c) 2000-2024 Charlie Gordon.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qe.h"

/*---------------- Ini file (and similar) coloring ----------------*/

#define CSV_SEP  ",;\t|"
enum {
    CSV_STATE_COMMA  = 0x00,
    CSV_STATE_SEMI   = 0x01,
    CSV_STATE_TAB    = 0x02,
    CSV_STATE_BAR    = 0x03,
    CSV_STATE_SEP    = 0x03,
    CSV_STATE_STRING = 0x04,
    CSV_STATE_HEADER = 0x08,
};

enum {
    CSV_STYLE_TEXT =    QE_STYLE_DEFAULT,
    CSV_STYLE_COMMENT = QE_STYLE_COMMENT,
    CSV_STYLE_STRING =  QE_STYLE_STRING,
    CSV_STYLE_NUMBER =  QE_STYLE_NUMBER,
    CSV_STYLE_DATE =    QE_STYLE_FUNCTION,
    CSV_STYLE_HEADER =  QE_STYLE_PREPROCESS,
    CSV_STYLE_ERROR =   QE_STYLE_ERROR,
};

enum {
    MATCH_FAIL = 0,
    MATCH_PARTIAL = 1,
    MATCH_FULL = 2,
    // XXX: could also have a prefix match like strtol and strtod

    MATCH_NUMBER = 0x10,
    MATCH_NUMBER_PARTIAL = 0x11,
    MATCH_NUMBER_FULL = 0x12,

    MATCH_DATE = 0x20,
    MATCH_DATE_PARTIAL = 0x21,
    MATCH_DATE_FULL = 0x22,
};

static int match_number(const char32_t *str, int start, int n, char32_t dot) {
    int i = start, digits = 0;
    char32_t c = 0;

    if (i == n)
        return MATCH_FAIL;
    c = str[i++];
    if (c == '+' || c == '-') {
        if (i == n)
            return MATCH_NUMBER_PARTIAL;
        c = str[i++];
    }
    while (qe_isdigit(c)) {
        if (i == n)
            return MATCH_NUMBER_FULL;
        digits++;
        c = str[i++];
    }
    // could also accept both `,`, `.`, `_` and ` ` as digit separators
    if (c == dot) {
        if (i == n) {
            if (digits == 0)
                return MATCH_NUMBER_PARTIAL;
            return MATCH_NUMBER_FULL;
        }
        c = str[i++];
        while (qe_isdigit(c)) {
            if (i == n)
                return MATCH_NUMBER_FULL;
            digits++;
            c = str[i++];
        }
    }
    if (digits == 0)
        return MATCH_FAIL;
    if (c == 'e' || c == 'E') {
        if (i == n)
            return MATCH_NUMBER_PARTIAL;
        c = str[i++];
        if (c == '+' || c == '-') {
            if (i == n)
                return MATCH_NUMBER_PARTIAL;
            c = str[i++];
        }
        while (qe_isdigit(c)) {
            if (i == n)
                return MATCH_NUMBER_FULL;
            c = str[i++];
        }
    }
    return MATCH_FAIL;
}

static int match_date_time(const char32_t *str, int start, int n) {
    int i = start, digits = 0;
    char32_t sep = ' ', nsep = 0;

    if (i == n)
        return MATCH_FAIL;
    while (i < n) {
        char32_t c = str[i++];
        if (c == '/' || c == ':' || c == '-' || c == '.' || c == ' ') {
            if (digits == 0 || digits == 3)
                return MATCH_FAIL;
            if (sep == ' ')
                nsep = 1;
            else
            if (c != sep || ++nsep > 2)
                return MATCH_FAIL;
            sep = c;
            digits = 0;
        } else
        if (!qe_isdigit(c) || ++digits > 4)
            return MATCH_FAIL;
    }
    if (digits < 2 || digits == 3)
        return MATCH_DATE_PARTIAL;
    else
        return MATCH_DATE_FULL;
}

static void csv_colorize_line(QEColorizeContext *cp,
                              const char32_t *str, int n,
                              QETermStyle *sbuf, ModeDef *syn)
{
    int i = 0, j, start = 0, style = 0;
    char32_t sep = ',', dot = '.';
    char32_t c;
    int colstate = cp->colorize_state;

    if (cp->offset == 0) {
        /* scan for the first separator */
        while (i < n) {
            switch (str[i++]) {
            case ',':   break;
            case '\t':  colstate |= CSV_STATE_TAB; break;
            case '|':   colstate |= CSV_STATE_BAR; break;
            case ';':   colstate |= CSV_STATE_SEMI; break;
            default:    continue;
            }
            break;
        }
        sep = CSV_SEP[colstate & CSV_STATE_SEP];
        start = i = 0;
        while (i < n) {
            c = str[i++];
            if (c == sep) {
                /* check for invalid field names */
                if (start != i - 1
                &&  ((match_number(str, start, i - 1, dot) & MATCH_FULL)
                ||   (match_date_time(str, start, i - 1) & MATCH_FULL)))
                    break;
                start = i;
            }
        }
        if (i == n) {
            colstate |= CSV_STATE_HEADER;
        }
        i = 0;
    }

    sep = CSV_SEP[colstate & CSV_STATE_SEP];
    if (sep == ';')
        dot = ',';

    if (colstate & CSV_STATE_STRING)
        goto in_string;

    while (i < n) {
        start = i;
    next:
        c = str[i++];
        if (qe_isspace(c))
            continue;
        if (c == '\"') {
            // XXX: should match numbers and dates inside strings
            colstate |= CSV_STATE_STRING;
        in_string:
            while (i < n) {
                /* XXX: escape sequences? */
                /* quotes are doubled for escaping */
                if (str[i++] == '\"') {
                    if (i == n || str[i] != '\"') {
                        colstate ^= CSV_STATE_STRING;
                        break;
                    }
                    i++;
                }
            }
            while (i < n && qe_isspace(str[i]))
                i++;
            if (i < n && str[i] != sep) {
                style = CSV_STYLE_ERROR;
                goto next;
            }
            if (colstate & CSV_STATE_HEADER)
                style = CSV_STYLE_HEADER;
            else
                style = CSV_STYLE_STRING;
        } else
        if (c != sep) {
            while (i < n && str[i] != sep)
                i++;
            for (j = i; j > start && qe_isspace(str[j - 1]); j--)
                continue;
            if (match_number(str, start, j, dot) & MATCH_FULL)
                style = CSV_STYLE_NUMBER;
            else
            if (match_date_time(str, start, j) & MATCH_FULL)
                style = CSV_STYLE_DATE;
            else
            if (colstate & CSV_STATE_HEADER)
                style = CSV_STYLE_HEADER;
            else
                style = CSV_STYLE_TEXT;
        }
        if (style) {
            SET_STYLE(sbuf, start, i, style);
            style = 0;
        }
    }
    if (!(colstate & CSV_STATE_STRING))
        colstate &= ~CSV_STATE_HEADER;

    cp->colorize_state = colstate;
}

static ModeDef csv_mode = {
    .name = "CSV",
    .extensions = "csv",
    .colorize_func = csv_colorize_line,
};

static int csv_init(QEmacsState *qs)
{
    qe_register_mode(qs, &csv_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(csv_init);
