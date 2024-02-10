/*
 * REBOL language mode for QEmacs.
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

/*---------------- REBOL coloring ----------------*/

static char const rebol_keywords[] = {
    /* Constants */
    "none|true|false|on|off|yes|no|newline|tab|cr|lf|null|pi|"
    /* Evalute */
    "do|reduce|compose|"
    /* Branch */
    "if|either|all|any|case|switch|"
    /* Loop */
    "loop|repeat|foreach|while|remove-each|break|"
    /* Function */
    "function|funct|func|has|does|exit|return|"
    /* Error */
    "attempt|try|catch|throw|"
    /* Help */
    "help|what|docs|source|trace|probe|??|delta-time|"
    /* Compare */
    "<|>|<=|>=|=|==|<>|!=|!==|=?|same?|"
    /* Math */
    "+|-|*|/|**|remainder|negate|abs|absolute|round|min|max|"
    "and|or|xor|not|random|shift|sine|log-e|to|"
    /* Reflection */
    "words-of|values-of|title-of|spec-of|body-of|"
    /* Series */
    "find|select|first|last|pick|length?|index?|next|back|skip|"
    "make|copy|join|ajoin|rejoin|append|repend|insert|remove|"
    "take|clear|change|replace|trim|split|sort|swap|"
    /* Sets */
    "unique|union|intersect|difference|exclude|"
    /* Console */
    "print|probe|input|ask|confirm|halt|quit|"
    /* Output */
    "mold|form|to|"
    /* Files/Ports */
    "read|write|load|save|open|close|delete|exists?|size?|"
    "modified?|suffix?|dir?|split-path|dirize|to-local-file|"
    /* Context */
    "object|module|import|construct|bind|get|set|in|value?|use|"
    /* Other */
    "now|parse|secure|wait|browse|compress|decompress|"
    "lowercase|uppercase|entab|detab|"
    /* GUI/Graphics */
    "view|unview|layout|alert|request|request-file|draw|show|"
    "get-face|set-face|focus|"
    //"then|forall|rebol|end|native|self|some|"
};

static char const rebol_types[] = {
    "|"
};

enum {
    REBOL_STYLE_TEXT =        QE_STYLE_DEFAULT,
    REBOL_STYLE_COMMENT =     QE_STYLE_COMMENT,
    REBOL_STYLE_STRING =      QE_STYLE_STRING,
    REBOL_STYLE_NUMBER =      QE_STYLE_NUMBER,
    REBOL_STYLE_KEYWORD =     QE_STYLE_KEYWORD,
    REBOL_STYLE_TYPE =        QE_STYLE_TYPE,
    REBOL_STYLE_BINARY =      QE_STYLE_PREPROCESS,
    REBOL_STYLE_DEFINITION =  QE_STYLE_FUNCTION,
    REBOL_STYLE_ERROR =       QE_STYLE_ERROR,
};

enum {
    IN_REBOL_STRING1 = 0x0F,  /* allow embedded balanced { } */
    IN_REBOL_STRING2 = 0x10,
    IN_REBOL_BINARY =  0x20,
    IN_REBOL_COMMENT = 0x40,
};

static void rebol_colorize_line(QEColorizeContext *cp,
                                char32_t *str, int n, ModeDef *syn)
{
    char keyword[64];
    int i = 0, start = 0, style, style0 = 0, k, klen, level;
    char32_t c;
    int colstate = cp->colorize_state;

    level = colstate & IN_REBOL_STRING1;
    if (level)
        goto in_string1;

    if (colstate & IN_REBOL_STRING2)
        goto in_string2;

    if (colstate & IN_REBOL_BINARY)
        goto in_binary;

    if (colstate & IN_REBOL_COMMENT)
        style0 = REBOL_STYLE_COMMENT;

    style = style0;
    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case ';':
            i = n;
            style = REBOL_STYLE_COMMENT;
            break;

        case '{':
            level++;
        in_string1:
            while (i < n) {
                switch (str[i++]) {
                case '^':
                    if (i < n)
                        i++;
                    continue;
                case '{':
                    level++;
                    continue;
                case '}':
                    --level;
                    if (!level)
                        break;
                    continue;
                default:
                    continue;
                }
                break;
            }
            colstate &= ~IN_REBOL_STRING1;
            colstate |= level & IN_REBOL_STRING1;
            style = REBOL_STYLE_STRING;
            break;

        case '"':
        in_string2:
            colstate |= IN_REBOL_STRING2;
            while (i < n) {
                c = str[i++];
                if (c == '^' && i < n)
                    i++;
                else
                if (c == '"') {
                    colstate &= ~IN_REBOL_STRING2;
                    break;
                }
            }
            if (colstate & IN_REBOL_STRING2) {
                /* double quoted strings do not span lines */
                colstate &= ~IN_REBOL_STRING2;
                style = REBOL_STYLE_ERROR;
                break;
            }
            style = REBOL_STYLE_STRING;
            break;

        case '[':       /* start block */
            break;
        case ']':       /* end block */
            colstate &= ~IN_REBOL_COMMENT;
            break;
        case '(':
        case ')':
            break;

        case '<':
            /* XXX: should skip tag with embedded strings */
            goto normal;

        case '#':
            if (str[i] == '"') {
                /* character constant */
                break; /* keep # in default color */
            }
            if (str[i] == '{') {
            in_binary:
                colstate |= IN_REBOL_BINARY;
                while (i < n) {
                    if (str[i++] == '}') {
                        colstate &= ~IN_REBOL_BINARY;
                        break;
                    }
                }
                style = REBOL_STYLE_BINARY;
                break;
            }
            goto normal;

        case '6':  /* 64#{ base64 encoded data } */
            if (str[i] == '4' && str[i + 1] == '#' && str[i + 2] == '{')
                goto in_binary;
            goto normal;
        case '1':  /* 16#{ hex encoded data } */
            if (str[i] == '6' && str[i + 1] == '#' && str[i + 2] == '{')
                goto in_binary;
            goto normal;
        case '2':  /* 2#{ binary encoded data } */
            if (str[i] == '#' && str[i + 1] == '{')
                goto in_binary;
            goto normal;

        default:
        normal:
            if (c <= ' ')
                break;

            /* parse words */
            klen = 0;
            keyword[klen++] = qe_tolower(c);
            for (; i < n; i++) {
                if (qe_findchar(" \t;()[]\"", str[i]))
                    break;
                if (klen < countof(keyword) - 1)
                    keyword[klen++] = qe_tolower(str[i]);
            }
            keyword[klen] = '\0';
            if (qe_isdigit(c) || c == '+' || c == '-') {
                /* check numbers */
                int dots = 0;
                for (k = 1; k < klen; k++) {
                    if (qe_match2(keyword[k], '.', ',')) {
                        dots++;
                    } else
                    if (keyword[k] == 'e') {
                        if (qe_match2(keyword[k + 1], '+', '-'))
                            k++;
                    } else
                    if (!qe_match2(keyword[k], '\'', '%')
                    &&  !qe_isdigit(keyword[k]))
                        break;
                }
                if (k == klen && dots <= 1) {
                    style = REBOL_STYLE_NUMBER;
                    break;
                }
            }
            if (qe_isalpha_(c)) {
                /* check identifiers and keywords */
                if (strequal(keyword, "comment")) {
                    colstate |= IN_REBOL_COMMENT;
                    style = style0 = REBOL_STYLE_COMMENT;
                    break;
                }
                if (strfind(syn->keywords, keyword)) {
                    style = REBOL_STYLE_KEYWORD;
                    break;
                }
                if (strfind(syn->types, keyword)) {
                    style = REBOL_STYLE_TYPE;
                    break;
                }
            }
            if (klen > 1 && str[i - 1] == ':') {
                i--;
                style = REBOL_STYLE_DEFINITION;
                break;
            }
            break;
        }
        if (style) {
            SET_COLOR(str, start, i, style);
            style = style0;
        }
    }
    cp->colorize_state = colstate;
}

static int rebol_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    /* trust the file extension and/or shell handler */
    if (match_extension(p->filename, mode->extensions)
    &&  !qe_memicmp(cs8(p->buf), "REBOL", 5)) {
        return 81;
    }
    return 1;
}

static ModeDef rebol_mode = {
    .name = "Rebol",
    .extensions = "r",
    .mode_probe = rebol_mode_probe,
    .keywords = rebol_keywords,
    .types = rebol_types,
    .colorize_func = rebol_colorize_line,
};

static int rebol_init(void)
{
    qe_register_mode(&rebol_mode, MODEF_SYNTAX);

    return 0;
}

qe_module_init(rebol_init);
