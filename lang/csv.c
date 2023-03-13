/*
 * CSV file mode for QEmacs.
 *
 * Copyright (c) 2000-2023 Charlie Gordon.
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
    IN_CSV_SEMI   = 0x01,
    IN_CSV_TAB    = 0x02,
    IN_CSV_BAR    = 0x03,
    IN_CSV_SEP    = 0x03,
    IN_CSV_STRING = 0x04,
    IN_CSV_HEADER = 0x08,
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

    MATCH_NUMBER = 4,
    MATCH_NUMBER_PARTIAL = 4+1,
    MATCH_NUMBER_FULL = 4+2,

    MATCH_DATE = 8,
    MATCH_DATE_PARTIAL = 8+1,
    MATCH_DATE_FULL = 8+2,
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
    if (i == n)
        return MATCH_NUMBER_FULL;
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
    char32_t c;

    while (i < n) {
        c = str[i++];
        if (c == '/' || c == ':' || c == '-' || c == '.' || c == ' ') {
            if (digits == 0 || digits == 3)
                return MATCH_FAIL;
            digits = 0;
        } else
        if (!qe_isdigit(c) || ++digits > 4)
            return MATCH_FAIL;
    }
    if (digits == 0)
        return MATCH_FAIL;
    if (digits == 1 || digits == 3)
        return MATCH_DATE_PARTIAL;
    else
        return MATCH_DATE_FULL;
}

static void csv_colorize_line(QEColorizeContext *cp,
                              char32_t *str, int n, ModeDef *syn)
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
            case '\t':  colstate |= IN_CSV_TAB; break;
            case '|':   colstate |= IN_CSV_BAR; break;
            case ';':   colstate |= IN_CSV_SEMI; break;
            default:    continue;
            }
            break;
        }
        sep = CSV_SEP[colstate & IN_CSV_SEP];
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
            colstate |= IN_CSV_HEADER;
        }
        i = 0;
    }

    sep = CSV_SEP[colstate & IN_CSV_SEP];
    if (sep == ';')
        dot = ',';

    if (colstate & IN_CSV_STRING)
        goto in_string;

    while (i < n) {
        start = i;
    next:
        c = str[i++];
        if (qe_isspace(c))
            continue;
        if (c == '\"') {
            // XXX: should match numbers and dates inside strings
            colstate |= IN_CSV_STRING;
        in_string:
            while (i < n) {
                /* XXX: escape sequences? */
                /* quotes are doubled for escaping */
                if (str[i++] == '\"') {
                    if (i == n || str[i] != '\"') {
                        colstate ^= IN_CSV_STRING;
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
            if (colstate & IN_CSV_HEADER)
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
            if (colstate & IN_CSV_HEADER)
                style = CSV_STYLE_HEADER;
            else
                style = CSV_STYLE_TEXT;
        }
        if (style) {
            SET_COLOR(str, start, i, style);
            style = 0;
        }
    }
    if (!(colstate & IN_CSV_STRING))
        colstate &= ~IN_CSV_HEADER;

    cp->colorize_state = colstate;
}

static ModeDef csv_mode = {
    .name = "CSV",
    .extensions = "csv",
    .colorize_func = csv_colorize_line,
};

static int csv_init(void) {
    qe_register_mode(&csv_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(csv_init);
