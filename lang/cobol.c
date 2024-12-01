/*
 * Cobol language mode for QEmacs.
 *
 * Copyright (c) 2015-2024 Charlie Gordon.
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

/*---------------- COBOL coloring ----------------*/

#define COBOL_KEYWORD_SIZE  24

static char const cobol_keywords[] = {
    "|identification|procedure|data|division|program-id|author|file|linkage"
    "|section|working-storage|environment|configuration|input-output"
    "|source-computer|object-computer|special-names"
    "|date-written|date-compiled|file-control|i-o-control"
    "|if|then|else|end-if|of|is|equal|less|greater|than|to|into|not|or|and"
    "|compute|end-compute|call|end-call|using|length|rounded"
    "|move|set|up|down|address|add|end-add|subtract|end-subtract"
    "|multiply|end-multiply|divide|by|giving|remainder|end-divide"
    "|perform|end-perform|varying|from|until|thru|after|before|test"
    "|exec|end-exec|on|size|error|exit|initialize|continue"
    "|evaluate|when|other|end-evaluate|search"
    "|display|at|line|column|col|plus|minus|with|highlight|lowlight"
    "|screen|blank|erase|background-color|foreground-color|reverse-video|blink"
    "|with|no|advancing|upon|end-display|eos"
    "|function|end|program|stop|run|returning"
    "|filler|value|values|occurs|times|redefines|indexed|auto"
    "|constant|as"
    "|accept|end-accept|goback|go|to|depending|on"
    "|copy|inspect|replacing|converting|leading|trailing|to|tallying|"
    "|first|last|for|all|by|characters|initial"
    "|string|end-string|unstring|end-unstring|delimited|by|into"
    "|open|input|output|close|read|write"
    "|select|assign|organization|line|sequential|status"
    "|label|records|contains|are|record|block|recording|mode|standard"
    "|next|sentence"
    "|usage|any|length"
};

static char const cobol_types[] = {
    "|fd|pic|picture|zero|zeros|zeroes|space|spaces|true|false"
    //"|byteint|char|float|display-numeric"
    "|group|native|binary|pointer|binary-char|binary-long|unsigned"
    "|character|date|decimal|graphic|integer|numeric|smallint"
    "|time|timestamp|varchar|vargraphic"
    "|comp|comp-1|comp-2|comp-3|comp-4|comp-5|comp-6|display-1"
};

enum {
    COBOL_STYLE_TEXT =        QE_STYLE_DEFAULT,
    COBOL_STYLE_COMMENT =     QE_STYLE_COMMENT,
    COBOL_STYLE_STRING =      QE_STYLE_STRING,
    COBOL_STYLE_KEYWORD =     QE_STYLE_KEYWORD,
    COBOL_STYLE_SYMBOL =      QE_STYLE_NUMBER,
    COBOL_STYLE_TYPE =        QE_STYLE_TYPE,
    COBOL_STYLE_NUMBER =      QE_STYLE_NUMBER,
    COBOL_STYLE_PREPROCESS =  QE_STYLE_PREPROCESS,
    COBOL_STYLE_HEADING =     QE_STYLE_PREPROCESS,
    COBOL_STYLE_IDENTIFIER =  QE_STYLE_VARIABLE,
};

enum {
    IN_COBOL_SOURCE_FORMAT = 0x03,
    IN_COBOL_FIXED_FORMAT  = 0x01,  /* source format is fixed */
    IN_COBOL_FREE_FORMAT   = 0x02,  /* source format is free */
};

static void cobol_colorize_line(QEColorizeContext *cp,
                                const char32_t *str, int n,
                                QETermStyle *sbuf, ModeDef *syn)
{
    char keyword[COBOL_KEYWORD_SIZE];
    int i = 0, start = i, j, style, len, indent, heading = 0, preproc = 0, comment = -1;
    char32_t c;
    int state = cp->colorize_state;

    i = cp_skip_blanks(str, i, n);
    indent = i;

    if (str[i] == '*' && str[i + 1] == '>')
        comment = i;

    if (str[i] == '>')
        goto check_source_format;

    if (!(state & IN_COBOL_FREE_FORMAT)) {
        if (!(state & IN_COBOL_FIXED_FORMAT)) {
    check_source_format:
            if (comment < 0 && ustristr(str, "source") != NULL) {
                if (ustristr(str, "free")) {
                    state &= ~IN_COBOL_FIXED_FORMAT;
                    state |= IN_COBOL_FREE_FORMAT;
                    preproc = 1;
                } else
                if (ustristr(str, "fixed")) {
                    state &= ~IN_COBOL_FREE_FORMAT;
                    state |= IN_COBOL_FIXED_FORMAT;
                    preproc = 1;
                }
                if (preproc) {
                    i = n;
                    SET_STYLE(sbuf, start, i, COBOL_STYLE_PREPROCESS);
                }
            } else {
                if (comment >= 0 && comment < 6) {
                    state |= IN_COBOL_FREE_FORMAT;
                } else
                if (i < 6 && qe_isdigit(str[i])) {
                    for (j = i + 1; j < n && qe_isdigit(str[j]); j++)
                        continue;
                    if (j == 6) {
                        heading = 6;
                        if (i == 0) {
                            state |= IN_COBOL_FIXED_FORMAT;
                        }
                    }
                }
            }
        }
        if ((state & IN_COBOL_FIXED_FORMAT) || heading || i == 6) {
            i = heading = 6;
            SET_STYLE(sbuf, start, i, COBOL_STYLE_HEADING);
        }
    }

    style = 0;
    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '*':
            if ((i == 7 && heading == 6) || i == indent + 1 || str[i] == '>') {
                i = n;
                style = COBOL_STYLE_COMMENT;
                break;
            }
            continue;
        case '\"':
        case '\'':
            while (i < n && str[i++] != c)
                continue;
            style = COBOL_STYLE_STRING;
            break;
        case '-':
        case '+':
            if (qe_isdigit(str[i]) || (str[i] == '.' && qe_isdigit(str[i + 1])))
                goto number;
            break;
        case '.':
            if (qe_isdigit(str[i]))
                goto number;
            style = COBOL_STYLE_KEYWORD;
            break;
        default:
            if (qe_isdigit(c)) {
                /* try and parse a number */
            number:
                for (; i < n && qe_isdigit(str[i]); i++)
                    continue;
                if (str[i] == '.' && qe_isdigit(str[i + 1])) {
                    for (i += 2; i < n && qe_isdigit(str[i]); i++)
                        continue;
                }
                if (i >= n || !(qe_isalnum(str[i]) || str[i] == '-')) {
                    style = COBOL_STYLE_NUMBER;
                    break;
                } else {
                    /* restart for identifier */
                    i = start;
                    c = str[i++];
                }
            }
            if (qe_isalnum_(c)) {
                /* parse identifiers and keywords */
                len = 0;
                keyword[len++] = qe_tolower(c);
                for (; i < n; i++) {
                    char32_t c1 = str[i];
                    if (!qe_isalnum_(c1) && !qe_findchar("-$", c1))
                        break;
                    if (len < countof(keyword) - 1)
                        keyword[len++] = qe_tolower(c1);
                }
                keyword[len] = '\0';
                if (strfind(syn->keywords, keyword)) {
                    style = COBOL_STYLE_KEYWORD;
                    break;
                }
                if (strfind(syn->types, keyword)) {
                    style = COBOL_STYLE_TYPE;
                    break;
                }
                //style = COBOL_STYLE_IDENTIFIER;
                break;
            }
            continue;
        }
        if (style) {
            SET_STYLE(sbuf, start, i, style);
            style = 0;
        }
    }
    cp->colorize_state = state;
}

static int cobol_mode_probe(ModeDef *mode, ModeProbeData *pd)
{
    const u8 *p;

    /* trust the file extension and/or shell handler */
    if (match_extension(pd->filename, mode->extensions)
    ||  match_shell_handler(cs8(pd->buf), mode->shell_handlers)) {
        return 80;
    }
    /* scan for a cobol comment */
    /* XXX: need flags in ModeProbeData to verify text buffer */
    for (p = pd->buf; *p == ' '; p++)
        continue;
    if (p[0] == '*' && p[1] == '>')
        return 60;

    return 1;
}

static ModeDef cobol_mode = {
    .name = "Cobol",
    .extensions = "cbl|cob|cpy",
    .keywords = cobol_keywords,
    .types = cobol_types,
    .mode_probe = cobol_mode_probe,
    .colorize_func = cobol_colorize_line,
};

static int cobol_init(QEmacsState *qs)
{
    qe_register_mode(qs, &cobol_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(cobol_init);
