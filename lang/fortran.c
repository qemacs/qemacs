/*
 * Fortran language modes for QEmacs.
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

/*---------------- Fortran coloring ----------------*/

static char const fortran_keywords[] = {
    "recursive|block|call|case|common|contains|continue|"
    "default|do|else|elseif|elsewhere|end|enddo|endif|exit|format|"
    "function|goto|if|implicit|kind|module|private|procedure|"
    "program|public|return|select|stop|subroutine|then|"
    "use|where|in|out|inout|interface|none|while|"
    "forall|equivalence|any|assign|go|to|pure|elemental|"
    "external|intrinsic|"
    "open|close|read|write|rewind|backspace|print|inquire|"
    "allocate|deallocate|associated|nullify|present|"
    ".and.|.eq.|.false.|.ge.|.gt.|.le.|.lt.|.ne.|.not.|.or.|.true.|"
};

static char const fortran_types[] = {
    "character|complex|digits|double|dimension|epsilon|huge|"
    "integer|logical|maxexponent|minexponent|operator|target|"
    "parameter|pointer|precision|radix|range|real|tiny|intent|"
    "optional|allocatable|type|"
};

enum {
    FORTRAN_STYLE_TEXT =       QE_STYLE_DEFAULT,
    FORTRAN_STYLE_KEYWORD =    QE_STYLE_KEYWORD,
    FORTRAN_STYLE_TYPE =       QE_STYLE_TYPE,
    FORTRAN_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    FORTRAN_STYLE_COMMENT =    QE_STYLE_COMMENT,
    FORTRAN_STYLE_STRING =     QE_STYLE_STRING,
    FORTRAN_STYLE_IDENTIFIER = QE_STYLE_DEFAULT,
    FORTRAN_STYLE_NUMBER =     QE_STYLE_NUMBER,
    FORTRAN_STYLE_FUNCTION =   QE_STYLE_FUNCTION,
};

static void fortran_colorize_line(QEColorizeContext *cp,
                                  const char32_t *str, int n,
                                  QETermStyle *sbuf, ModeDef *syn)
{
    char keyword[16];
    int i = 0, start = i, style, len, w;
    char32_t c;
    int colstate = cp->colorize_state;

    w = cp_skip_blanks(str, 0, n);

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            if (start == 0)
                goto preprocess;
            break;
        case '*':
        case 'c':
        case 'C':
            if (start == 0 && !qe_isalpha(str[i]))
                goto comment;
            break;
        case '!':
        comment:
            while (str[i] == ' ')
                i++;
            if (str[i] == '{') {
            preprocess:
                i = n;
                SET_STYLE(sbuf, start, i, FORTRAN_STYLE_PREPROCESS);
                continue;
            }
            i = n;
            SET_STYLE(sbuf, start, i, FORTRAN_STYLE_COMMENT);
            continue;
        case '\'':
        case '\"':
            /* parse string or char const */
            while (i < n) {
                /* XXX: escape sequences? */
                if (str[i++] == c)
                    break;
            }
            SET_STYLE(sbuf, start, i, FORTRAN_STYLE_STRING);
            continue;
        default:
            break;
        }
        /* parse numbers */
        if (qe_isdigit(c)) {
            /* Parse actual Fortran number syntax, with D or E for exponent */
            for (; qe_isdigit(str[i]); i++)
                continue;
            if (str[i] == '.' && qe_isdigit(str[i + 1])) {
                for (i += 2; qe_isdigit(str[i]); i++)
                    continue;
            }
            if ((c = qe_tolower(str[i])) == 'e' || c == 'd') {
                int k = i + 1;
                if (str[k] == '+' || str[k] == '-')
                    k++;
                if (qe_isdigit(str[k])) {
                    for (i = k + 1; qe_isdigit(str[i]); i++)
                        continue;
                }
            }
            SET_STYLE(sbuf, start, i, FORTRAN_STYLE_NUMBER);
            continue;
        }
        /* parse identifiers and keywords */
        if (qe_isalpha_(c) || (c == '.' && qe_isalpha(str[i]))) {
            len = 0;
            keyword[len++] = qe_tolower(c);
            for (; qe_isalnum_(str[i]); i++) {
                if (len < countof(keyword) - 1)
                    keyword[len++] = qe_tolower(str[i]);
            }
            if (c == '.' && str[i] == '.' && len < countof(keyword) - 1)
                keyword[len++] = str[i++];
            keyword[len] = '\0';

            if (strfind(syn->keywords, keyword)
            ||  (start == w && strfind("data|save", keyword))) {
                style = FORTRAN_STYLE_KEYWORD;
            } else
            if (strfind(syn->types, keyword)) {
                style = FORTRAN_STYLE_TYPE;
            } else
            if (check_fcall(str, i))
                style = FORTRAN_STYLE_FUNCTION;
            else
                style = FORTRAN_STYLE_IDENTIFIER;

            SET_STYLE(sbuf, start, i, style);
            continue;
        }
    }
    cp->colorize_state = colstate;
}

static ModeDef fortran_mode = {
    .name = "Fortran",
    .extensions = "f|for|f77|f90|f95|f03",
    .keywords = fortran_keywords,
    .types = fortran_types,
    .colorize_func = fortran_colorize_line,
};

static int fortran_init(QEmacsState *qs)
{
    qe_register_mode(qs, &fortran_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(fortran_init);
