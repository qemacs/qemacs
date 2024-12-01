/*
 * Vim script mode for QEmacs.
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

/*---------------- Vim/Visual Vim coloring ----------------*/

static char const vim_cmd_keywords[] = {
    /* Only a subset of commands are recognized */
    "|brea[k]|cal[l]|cat[ch]|command|con[tinue]|delc[ommand]"
    "|delf[unction]|el[se]|elsei[f]|end|endfo[r]|endfu[nction]|endi[f]"
    "|endt[ry]|endw[hile]|ex[ecute]|fina[lly]|fini[sh]|for"
    "|fun[ction]|if|hi[ghlight]|let|norm|pu[t]|redraws[tatus]|res[ize]"
    "|retu[rn]|ru[ntime]|se[t]|setl[ocal]|sil[ent]|syn|synt[ax]"
    "|try|unl[et]|ve[rsion]|wh[ile]|y[ank]"
    "|"
};

static char const vim_keywords[] = {
    "|self|in"
    "|"
};

static char const vim_syn_keywords[] = {
    "|case|ignore|match|keyword|include|cluster|region|sync|clear"
    "|nextgroup|contained|contains|display|oneline|start|end"
    "|skipwhite|keepend|excludenl|skipnl|skip|keepend|fromstart"
    "|minlines|maxlines|containedin|extend|transparent|fold"
    "|matchgroup|add|grouphere|groupthere|linebreaks"
    "|"
};

enum {
    VIM_STYLE_TEXT =        QE_STYLE_DEFAULT,
    VIM_STYLE_COMMENT =     QE_STYLE_COMMENT,
    VIM_STYLE_STRING =      QE_STYLE_STRING,
    VIM_STYLE_REGEX =       QE_STYLE_STRING,
    VIM_STYLE_NUMBER =      QE_STYLE_NUMBER,
    VIM_STYLE_KEYWORD =     QE_STYLE_KEYWORD,
    VIM_STYLE_IDENTIFIER =  QE_STYLE_DEFAULT,
    VIM_STYLE_FUNCTION =    QE_STYLE_FUNCTION,
};

enum {
    VIM_STATE_CMD,
    VIM_STATE_ARG,
    VIM_STATE_SYN,
};

static int is_vim_keyword(const char32_t *str, int from, int to,
                          const char *list)
{
    char keyword[16];
    const char *p;
    int i, len = to - from;
    char32_t c;

    if (len >= ssizeof(keyword))
        return 0;

    for (i = 0; i < len; i++) {
        c = str[from + i];
        if (c >= 0x80)
            return 0;
        keyword[i] = c;
    }
    keyword[len] = '\0';

    /* check for exact match or non ambiguous prefix */
    for (p = list; *p != '\0';) {
        for (i = 0;
             p[i] != '\0' && p[i] != ' ' && p[i] != '[' && p[i] != '|';
             i++) {
            continue;
        }
        if (i <= len && !memcmp(p, keyword, i)) {
            if (i == len)
                return 1;
            if (p[i] == '[' && !memcmp(p + i + 1, keyword + i, len - i))
                return 1;
        }
        for (p += i; *p != '\0' && (c = *p++) != ' ' && c != '|';)
            continue;
    }
    return 0;
}

static void vim_colorize_line(QEColorizeContext *cp,
                              const char32_t *str, int n,
                              QETermStyle *sbuf, ModeDef *syn)
{
    int i = 0, j, start, state, comm, level, style;
    char32_t c;

    i = cp_skip_blanks(str, i, n);
    if (str[i] == '\\') {
        i++;
        level = cp->colorize_state & 15;
        state = cp->colorize_state >> 4;
        comm = 0;
    } else {
        state = VIM_STATE_CMD;
        level = 0;
        comm = 1;
    }

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '\'':
            comm = 0;
            /* parse string const */
            while (i < n) {
                if (str[i++] == c)
                    break;
            }
            SET_STYLE(sbuf, start, i, VIM_STYLE_STRING);
            continue;
        case '/':
            if (state == VIM_STATE_SYN
            &&  (qe_isblank(str[i - 2]) || str[i - 2] == '=')) {
                /* parse regex */
                while (i < n) {
                    if (str[i] == '\\' && i + 1 < n) {
                        i += 2;
                    } else
                    if (str[i++] == c)
                        break;
                }
                SET_STYLE(sbuf, start, i, VIM_STYLE_REGEX);
                continue;
            }
            break;
        case '+':
            if (state == VIM_STATE_SYN
            &&  (qe_isblank(str[i - 2]) || str[i - 2] == '=')) {
                /* parse string const */
                for (j = i; j < n;) {
                    if (str[j++] == c) {
                        i = j;
                        SET_STYLE(sbuf, start, i, VIM_STYLE_STRING);
                        break;
                    }
                }
                continue;
            }
            break;
        case '\"':
            if (comm) {
                i = n;
                SET_STYLE(sbuf, start, i, VIM_STYLE_COMMENT);
                continue;
            }
            /* parse string const */
            style = VIM_STYLE_COMMENT;
            while (i < n) {
                if (str[i] == '\\' && i + 1 < n) {
                    i += 2;
                } else
                if (str[i++] == c) {
                    style = VIM_STYLE_STRING;
                    break;
                }
            }
            SET_STYLE(sbuf, start, i, style);
            continue;
        case '|':
            if (str[i] == '|') {
                i++;
            } else {
                state = VIM_STATE_CMD;
                comm = 1;
            }
            continue;
        case '(':
            comm = 0;
            level++;
            continue;
        case ')':
            level--;
            if (!level)
                comm = 1;
            continue;
        case ' ':
        case '\t':
        case ',':
        case '$':
            continue;
        default:
            comm = 0;
            break;
        }
        /* parse numbers */
        if (qe_isdigit(c)) {
            for (; i < n; i++) {
                if (!qe_isalnum(str[i]) && str[i] != '.')
                    break;
            }
            SET_STYLE(sbuf, start, i, VIM_STYLE_NUMBER);
            continue;
        }
        /* parse identifiers and keywords */
        if (qe_isalpha_(c)) {
            for (; i < n; i++) {
                if (!qe_isalnum_(str[i]) && str[i] != '#')
                    break;
            }
            style = VIM_STYLE_IDENTIFIER;

            if (state == VIM_STATE_CMD) {
                state = VIM_STATE_ARG;
                if (is_vim_keyword(str, start, i, vim_cmd_keywords)) {
                    if (is_vim_keyword(str, start, i, "syn[tax]")) {
                        state = VIM_STATE_SYN;
                    }
                    if (str[i] == '!')
                        i++;
                    style = VIM_STYLE_KEYWORD;
                }
            } else
            if (state == VIM_STATE_SYN) {
                if (is_vim_keyword(str, start, i, vim_syn_keywords)) {
                    style = VIM_STYLE_KEYWORD;
                }
            } else {
                if (is_vim_keyword(str, start, i, vim_keywords)) {
                    style = VIM_STYLE_KEYWORD;
                }
            }
            if (style == VIM_STYLE_IDENTIFIER) {
                if (check_fcall(str, i))
                    style = VIM_STYLE_FUNCTION;
            }
            SET_STYLE(sbuf, start, i, style);
            continue;
        }
    }
    cp->colorize_state = (state << 4) | (level & 15);
}

static ModeDef vim_mode = {
    .name = "Vim",
    .extensions = "vim",
    .colorize_func = vim_colorize_line,
};

static int vim_init(QEmacsState *qs)
{
    qe_register_mode(qs, &vim_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(vim_init);
