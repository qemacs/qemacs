/*
 * Algol68 language modes for QEmacs.
 *
 * Copyright (c) 2000-2026 Charlie Gordon.
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPPSS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qe.h"

/*---------------- Algol68 coloring ----------------*/

static char const algol68_keywords[] = {
    // Algol68 Final Report, unrevised
    "|priority|thef"
    "|btb|ctb|conj|quote|ct|ctab|either|sign"

    // Algol68 Revised Report
    "|true|false"
    "|if|then|else|elif|fi"
    "|case|in|out|ouse|esac"
    "|nil|skip|empty"
    "|mode|op|prio|proc"
    "|goto"
    "|not|up|down|lwb|upb"
    "|abs|bin|entier|leng|level|odd|repr|round|shorten" //|arg
    "|shl|shr|up|down|lwb|upb"  //|i"
    "|over|mod|elem"
    "|lt|le|ge|gt"
    "|eq|ne"
    "|and|or"
    "|andf|orf|andth|orel|andthen|orelse"
    "|minusab|plusab|timesab|divab|overab|modab|plusto"
    "|is|isnt|of|at"
    "|for|from|by|upto|downto|to|while|do|od"
    "|par|begin|exit|end"
    "|struct|union|ref"
    "|vector"

    // 20011222az: Added new items.
    "|todo|fixme|xxx|debug|note"
    // ALGOL 68r
    "|decs|context|configinfo|a68config|keep|finish|use|sysprocs|iostate|forall"
    // ALGOL 68c
    "|using|environ|foreach|assert"
    // ga68
    "|module|def|fed|pub|postlude|access"
};

static char const algol68_types[] = {
    "|flex|heap|loc|long|short"
    "|bits|bool|bytes|char|compl|int|real|complex|sema|string|void"
    "|channel|file|format"
};

enum {
    IN_ALGOL68_COMMENT_COMMENT = 0x01, // COMMENT
    IN_ALGOL68_COMMENT_CO =      0x02, // CO
    IN_ALGOL68_COMMENT_SHARP =   0x04, // #
    IN_ALGOL68_COMMENT_CENT =    0x08, // ¢ \u00A2
    IN_ALGOL68_COMMENT_POUND =   0x08, // £ \u00A3
    IN_ALGOL68_COMMENT_BRACES =  0x10, // { / }
    IN_ALGOL68_COMMENT_NOTE =    0x20, // NOTE / ETON
    IN_ALGOL68_COMMENT_PR =      0x40, // PR
    IN_ALGOL68_COMMENT =         0x7F, // all comment types
    IN_ALGOL68_STRING =          0x80, // line continuation inside a string
    IN_ALGOL68_CONTINUATION =   0x100, // line continuation
    IN_ALGOL68_COMMENT_LEVEL =  0x200, // nesting level
};

enum {
    ALGOL68_STYLE_TEXT =       QE_STYLE_DEFAULT,
    ALGOL68_STYLE_KEYWORD =    QE_STYLE_KEYWORD,
    ALGOL68_STYLE_TYPE =       QE_STYLE_TYPE,
    ALGOL68_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    ALGOL68_STYLE_COMMENT =    QE_STYLE_COMMENT,
    ALGOL68_STYLE_STRING =     QE_STYLE_STRING,
    ALGOL68_STYLE_IDENTIFIER = QE_STYLE_VARIABLE,
    ALGOL68_STYLE_NUMBER =     QE_STYLE_NUMBER,
    ALGOL68_STYLE_FUNCTION =   QE_STYLE_FUNCTION,
};

#define HAS_UPPER  1

static int algol68_get_tag(char *dest, int size, char32_t c,
                           const char32_t *str, int i, int n, int *flagsp)
{
    /*API algol68
       Extract an ASCII tag from a wide string into a char array and
       convert it to lowercase, storing case indications in flags.
       @argument `dest` a valid pointer to a destination array.
       @argument `size` the length of the destination array.
       @argument `c` the first code point to copy.
       @argument `str` a valid wide string pointer.
       @argument `i` the offset of the first code point to copy.
       @argument `n` the offset to the end of the wide string.
       @argument `flagsp` a valid int pointer to store the case indications.
       @return the number of characters to skip in the source string.
       @note: the return value can be larger than the destination array length.
       In this case, the destination array contains a truncated string, null
       terminated unless `size <= 0`.
     */
    int pos = 0, j, flags = 0;

    for (j = i;; j++) {
        if (pos + 1 < size) {
            /* c is assumed to be an ASCII character */
            if (qe_isupper(c)) {
                flags |= HAS_UPPER;
                c = qe_tolower(c);
            }
            dest[pos++] = (char)c;
        }
        if (j >= n)
            break;
        c = str[j];
        if (!qe_isalnum_(c))
            break;
    }
    if (pos < size) {
        dest[pos] = '\0';
    }
    if (flagsp) *flagsp = flags;
    return j - i;
}

static void algol68_colorize_line(QEColorizeContext *cp,
                                  const char32_t *str, int n,
                                  QETermStyle *sbuf, ModeDef *syn)
{
    char kbuf[16];
    int i = 0, start = i, k, style = 0, level = 0, flags;
    char32_t c = 0;
    int colstate = cp->colorize_state;

    if (colstate & IN_ALGOL68_COMMENT) {
        level = colstate / IN_ALGOL68_COMMENT_LEVEL;
        colstate &= ~(0xFF * IN_ALGOL68_COMMENT_LEVEL);
        if (colstate & IN_ALGOL68_COMMENT_COMMENT)
            goto in_comment_comment;
        if (colstate & IN_ALGOL68_COMMENT_CO)
            goto in_comment_co;
        if (colstate & IN_ALGOL68_COMMENT_NOTE)
            goto in_comment_note;
        if (colstate & IN_ALGOL68_COMMENT_PR)
            goto in_comment_pr;
        if (colstate & IN_ALGOL68_COMMENT_BRACES)
            goto in_comment_brace;
        c = '#';
        if (colstate & IN_ALGOL68_COMMENT_SHARP)
            goto in_comment_char;
        c = 0xA2;
        if (colstate & IN_ALGOL68_COMMENT_CENT)
            goto in_comment_char;
        c = 0xA3;
        if (colstate & IN_ALGOL68_COMMENT_POUND)
            goto in_comment_char;
        colstate &= ~IN_ALGOL68_COMMENT;
        level = 0;
    }

    if (colstate & IN_ALGOL68_STRING)
        goto in_string;

    if (colstate & IN_ALGOL68_CONTINUATION) {
        colstate &= ~IN_ALGOL68_CONTINUATION;
        if (i < n && qe_isalnum_(str[i])) {
            c = str[i++];
            i += algol68_get_tag(kbuf, countof(kbuf), c, str, i, n, &flags);
            goto in_broken_tag;
        }
    }

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            colstate |= IN_ALGOL68_COMMENT_SHARP;
            goto in_comment_char;
        case 0xA2:
            colstate |= IN_ALGOL68_COMMENT_CENT;
            goto in_comment_char;
        case 0xA3:
            colstate |= IN_ALGOL68_COMMENT_POUND;
        in_comment_char:
            style = ALGOL68_STYLE_COMMENT;
            while (i < n) {
                if (c == str[i++]) {
                    colstate &= ~IN_ALGOL68_COMMENT;
                    break;
                }
            }
            break;
        case '{':
            // new style comment, nested
            colstate |= IN_ALGOL68_COMMENT_BRACES;
            level = 1;
        in_comment_brace:
            style = ALGOL68_STYLE_COMMENT;
            while (i < n) {
                c = str[i++];
                if (c == '{') {
                    level++;
                } else
                if (c == '}' && --level == 0) {
                    colstate &= ~IN_ALGOL68_COMMENT;
                    break;
                }
            }
            colstate |= level * IN_ALGOL68_COMMENT_LEVEL;
            break;
        case '"':
        in_string:
            /* parse string or char const */
            style = ALGOL68_STYLE_STRING;
            while (i < n) {
                /* XXX: escape sequences? */
                c = str[i++];
                if (c == '\\' && i == n) {
                    colstate |= IN_ALGOL68_STRING;
                    break;
                }
                if (c == '"') {
                    colstate &= ~IN_ALGOL68_STRING;
                    break;
                }
            }
            break;
        case '$':
            /* XXX: handle format strings */
            break;
        default:
            /* parse numbers */
            if (qe_isdigit(c)) {
                for (; i < n; i++) {
                    c = str[i];
                    if (qe_isalnum(c)) continue;
                    if (c == '.') continue;
                    if ((c == '+' || c == '-') && qe_tolower(str[i-1]) == 'e') continue;
                    break;
                }
                style = ALGOL68_STYLE_NUMBER;
                break;
            }
            /* parse identifiers and keywords */
            if (qe_isalpha(c)) {
                i += algol68_get_tag(kbuf, countof(kbuf), c, str, i, n, &flags);
                if (i == n - 1 && str[i] == '\\') {
                    // broken tag, not a keyword
                    i++;
                in_broken_tag:
                    colstate |= IN_ALGOL68_CONTINUATION;
                    if (flags & HAS_UPPER)
                        style = ALGOL68_STYLE_TYPE;
                    else
                        style = ALGOL68_STYLE_IDENTIFIER;
                    break;
                } else
                if (strequal(kbuf, "note")) {
                    SET_STYLE(sbuf, start, i, ALGOL68_STYLE_KEYWORD);
                    colstate |= IN_ALGOL68_COMMENT_NOTE;
                    level = 1;
                in_comment_note:
                    start = i;
                    style = ALGOL68_STYLE_COMMENT;
                    while (i < n) {
                        c = str[i++];
                        if (qe_isalpha(c)) {
                            int j = i - 1;
                            i += algol68_get_tag(kbuf, countof(kbuf), c, str, i, n, NULL);
                            if (strequal(kbuf, "note")) {
                                level++;
                            } else
                            if (strequal(kbuf, "eton") && --level == 0) {
                                SET_STYLE(sbuf, start, j, style);
                                colstate &= ~IN_ALGOL68_COMMENT;
                                start = j;
                                style = ALGOL68_STYLE_KEYWORD;
                                break;
                            }
                        }
                    }
                    colstate |= level * IN_ALGOL68_COMMENT_LEVEL;
                    break;
                } else
                if (strequal(kbuf, "comment")) {
                    SET_STYLE(sbuf, start, i, ALGOL68_STYLE_KEYWORD);
                    colstate |= IN_ALGOL68_COMMENT_COMMENT;
                in_comment_comment:
                    start = i;
                    style = ALGOL68_STYLE_COMMENT;
                    while (i < n) {
                        c = str[i++];
                        if (qe_isalpha(c)) {
                            int j = i - 1;
                            i += algol68_get_tag(kbuf, countof(kbuf), c, str, i, n, NULL);
                            if (strequal(kbuf, "comment")) {
                                SET_STYLE(sbuf, start, j, style);
                                colstate &= ~IN_ALGOL68_COMMENT;
                                start = j;
                                style = ALGOL68_STYLE_KEYWORD;
                                break;
                            }
                        }
                    }
                    break;
                } else
                if (strequal(kbuf, "co")) {
                    SET_STYLE(sbuf, start, i, ALGOL68_STYLE_KEYWORD);
                    colstate |= IN_ALGOL68_COMMENT_CO;
                in_comment_co:
                    start = i;
                    style = ALGOL68_STYLE_COMMENT;
                    while (i < n) {
                        c = str[i++];
                        if (qe_isalpha(c)) {
                            int j = i - 1;
                            i += algol68_get_tag(kbuf, countof(kbuf), c, str, i, n, NULL);
                            if (strequal(kbuf, "co")) {
                                SET_STYLE(sbuf, start, j, style);
                                colstate &= ~IN_ALGOL68_COMMENT;
                                start = j;
                                style = ALGOL68_STYLE_KEYWORD;
                                break;
                            }
                        }
                    }
                    break;
                } else
                if (strequal(kbuf, "pr")) {
                    SET_STYLE(sbuf, start, i, ALGOL68_STYLE_KEYWORD);
                    colstate |= IN_ALGOL68_COMMENT_PR;
                in_comment_pr:
                    start = i;
                    style = ALGOL68_STYLE_PREPROCESS;
                    while (i < n) {
                        c = str[i++];
                        if (qe_isalpha(c)) {
                            int j = i - 1;
                            i += algol68_get_tag(kbuf, countof(kbuf), c, str, i, n, NULL);
                            if (strequal(kbuf, "pr")) {
                                SET_STYLE(sbuf, start, j, style);
                                colstate &= ~IN_ALGOL68_COMMENT;
                                start = j;
                                style = ALGOL68_STYLE_KEYWORD;
                                break;
                            }
                        }
                    }
                    break;
                } else
                if (strfind(syn->keywords, kbuf)) {
                    style = ALGOL68_STYLE_KEYWORD;
                } else
                if (strfind(syn->types, kbuf) || (flags & HAS_UPPER)) {
                    style = ALGOL68_STYLE_TYPE;
                } else {
                    k = i;
                    if (qe_isblank(str[k]))
                        k++;
                    if (str[k] == '(' && str[k + 1] != '*')
                        style = ALGOL68_STYLE_FUNCTION;
                    else
                        style = ALGOL68_STYLE_IDENTIFIER;
                }
                break;
            }
            continue;
        }
        if (style) {
            SET_STYLE(sbuf, start, i, style);
            style = 0;
        }
    }
    cp->colorize_state = colstate;
}

static ModeDef algol68_mode = {
    .name = "Algol68",
    .extensions = "a68",
    .keywords = algol68_keywords,
    .types = algol68_types,
    .colorize_func = algol68_colorize_line,
};

static int algol68_init(QEmacsState *qs)
{
    qe_register_mode(qs, &algol68_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(algol68_init);
