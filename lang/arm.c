/*
 * Miscellaneous QEmacs modes for arm development related file formats
 *
 * Copyright (c) 2014-2024 Charlie Gordon.
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

static char const arm_registers[] = {
    "r0|r1|r2|r3|r4|r5|r6|r7|r8|r9|r10|r11|r12|r13|r14|r15|lr|sp|pc|cpsr|spsr|lsl|lsr|"
};

/*---------------- ARM Assembly language coloring ----------------*/

enum {
    ASM_STYLE_TEXT        = QE_STYLE_DEFAULT,
    ASM_STYLE_LABEL       = QE_STYLE_DEFAULT,
    ASM_STYLE_PREPROCESS  = QE_STYLE_PREPROCESS,
    ASM_STYLE_COMMENT     = QE_STYLE_COMMENT,
    ASM_STYLE_STRING      = QE_STYLE_STRING,
    ASM_STYLE_NUMBER      = QE_STYLE_NUMBER,
    ASM_STYLE_OPCODE      = QE_STYLE_KEYWORD,
    ASM_STYLE_REGISTER    = QE_STYLE_KEYWORD,
};

enum {
    IN_ASM_TRAIL = 1,       /* beyond .end directive */
    IN_HAS_SEMI_COMMENT = 2,   /* use semi colons to introduce comments */
};

#define MAX_KEYWORD_SIZE  16

static void arm_asm_colorize_line(QEColorizeContext *cp,
                                  const char32_t *str, int n,
                                  QETermStyle *sbuf, ModeDef *syn)
{
    char keyword[MAX_KEYWORD_SIZE];
    int i = 0, start = 0, style = 0, klen, w = 0, wn = 0;
    int colstate = cp->colorize_state;
    char32_t c, sep;

    if (colstate & IN_ASM_TRAIL)
        goto comment;

    i = cp_skip_blanks(str, i, n);

    for (w = i; i < n;) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            if (start == 0 || !(colstate & IN_HAS_SEMI_COMMENT))
                goto comment;
            break;
        case '.':
            if (start > w)
                break;
            if (ustr_match_keyword(str + i, "end", NULL)) {
                colstate |= IN_ASM_TRAIL;
            }
            if (ustr_match_keyword(str + i, "byte", NULL)
            ||  ustr_match_keyword(str + i, "word", NULL)
            ||  ustr_match_keyword(str + i, "long", NULL)) {
                goto opcode;
            }
            /* scan for comment, skipping strings */
            sep = 0;
            for (; i < n; i++) {
                if (str[i] == '\'' || str[i] == '"') {
                    if (sep == 0) {
                        sep = str[i];
                    } else if (sep == str[i]) {
                        sep = 0;
                    }
                    continue;
                }
                if (sep == 0 && (str[i] == '@' || str[i] == '#'))
                    break;
            }
            style = ASM_STYLE_PREPROCESS;
            break;
        case '@':
        comment:
            i = n;
            style = ASM_STYLE_COMMENT;
            break;
        case '\'':
        case '\"':
            /* parse string const */
            while (i < n) {
                if (str[i++] == c)
                    break;
            }
            style = ASM_STYLE_STRING;
            break;
        case ';':       /* instruction separator */
            if (start == 0)
                colstate |= IN_HAS_SEMI_COMMENT;

            if (colstate & IN_HAS_SEMI_COMMENT)
                goto comment;
            w = i;
            wn = 0;
            continue;
        default:
            /* parse numbers */
            if (qe_isdigit(c)) {
                for (; qe_isalnum(str[i]) || str[i] == '.'; i++)
                    continue;
                if (str[i] == ':')
                    goto label;
                wn++;
                style = ASM_STYLE_NUMBER;
                break;
            }
            /* parse identifiers and keywords */
            if (qe_isalpha_(c)) {
            opcode:
                klen = 0;
                keyword[klen++] = qe_tolower(c);
                for (; qe_isalnum_(str[i]) || str[i] == '.'; i++) {
                    if (klen < countof(keyword) - 1)
                        keyword[klen++] = qe_tolower(str[i]);
                }
                keyword[klen] = '\0';
                if (str[i] == ':') {
                label:
                    style = ASM_STYLE_LABEL;
                    break;
                }
                if (++wn == 1) {
                    style = ASM_STYLE_OPCODE;
                    break;
                }
                if (strfind(syn->keywords, keyword)) {
                    style = ASM_STYLE_REGISTER;
                    break;
                }
                continue;
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

static ModeDef arm_asm_mode = {
    .name = "arm-asm",
    .extensions = "s",
    .keywords = arm_registers,
    .colorize_func = arm_asm_colorize_line,
};

static int arm_asm_init(QEmacsState *qs)
{
    qe_register_mode(qs, &arm_asm_mode, MODEF_SYNTAX);
    return 0;
}

/*---------------- Assembly listing coloring ----------------*/

enum {
    LST_STYLE_TEXT        = QE_STYLE_DEFAULT,
    LST_STYLE_OUTPUT      = QE_STYLE_COMMENT,
    LST_STYLE_FILENAME    = QE_STYLE_STRING,
    LST_STYLE_OPCODE      = QE_STYLE_KEYWORD,
    LST_STYLE_KEYWORD     = QE_STYLE_KEYWORD,
    LST_STYLE_IDENTIFIER  = QE_STYLE_VARIABLE,
    LST_STYLE_OFFSET      = QE_STYLE_COMMENT,
    LST_STYLE_COMMENT     = QE_STYLE_COMMENT,
    LST_STYLE_NUMBER      = QE_STYLE_NUMBER,
    LST_STYLE_DUMP        = QE_STYLE_FUNCTION,
};

enum {
    IN_LST_CODE_C      = 0x4000,
    IN_LST_CODE_CPP    = 0x8000,
    IN_LST_MASK        = 0xC000,
};

static void lst_colorize_line(QEColorizeContext *cp,
                              const char32_t *str, int n,
                              QETermStyle *sbuf, ModeDef *syn)
{
    /* Combined assembly / C source / filename listing:
     * determine line type by looking at line start
     */
    char kbuf[16];
    int i, w, start, colstate = cp->colorize_state;
    char32_t c;

    w = cp_skip_blanks(str, 0, n);

    if (str[0] && str[1] == ':' && str[2] == '\\') {
        /* has full DOS/Windows pathname */
        if (ustristr(str, ".c:") || ustristr(str, ".h:")) {
            colstate = IN_LST_CODE_C;
        } else
        if (ustristr(str, ".cpp:")) {
            colstate = IN_LST_CODE_CPP;
        }
        SET_STYLE(sbuf, 0, n, LST_STYLE_FILENAME);
    } else {
        int has_assembly = 0;

        i = 0;
        if (w > 0 && qe_isxdigit(str[w])) {
            for (i = w + 1; qe_isxdigit(str[i]); i++)
                continue;
            if (str[i] == ':')
                has_assembly = 1;
        }
        if (has_assembly) {
            colstate = 0;
            start = w;
            i += 1;
            SET_STYLE(sbuf, start, i, LST_STYLE_OFFSET);

            i = cp_skip_blanks(str, i, n);
            for (start = i; qe_isxdigit(str[i]); i++)
                continue;
            if (str[i] == ' ' && qe_isxdigit(str[i + 1])) {
                for (i += 2; qe_isxdigit(str[i]); i++)
                    continue;
            }
            SET_STYLE(sbuf, start, i, LST_STYLE_DUMP);
            i = cp_skip_blanks(str, i, n);
            for (start  = i; i < n && !qe_isblank(str[i]); i++)
                continue;
            SET_STYLE(sbuf, start, i, LST_STYLE_OPCODE);
            i = cp_skip_blanks(str, i, n);
            while (i < n) {
                start = i;
                c = str[i++];
                if (c == ';') {
                    i = n;
                    SET_STYLE(sbuf, start, i, LST_STYLE_COMMENT);
                    continue;
                }
                if (qe_isdigit(c)) {
                    for (; qe_isalnum(str[i]); i++)
                        continue;
                    SET_STYLE(sbuf, start, i, LST_STYLE_NUMBER);
                    continue;
                }
                if (qe_isalpha_(c)) {
                    i += ustr_get_identifier(kbuf, countof(kbuf), c, str, i, n);
                    if (strfind(syn->keywords, kbuf))
                        SET_STYLE(sbuf, start, i, LST_STYLE_KEYWORD);
                    continue;
                }
            }
        } else {
            /* Hack to try and detect C comments */
            if (str[w] == '*') {
                cp->colorize_state |= 0x01; /* IN_C_COMMENT */
            }
            cp->colorize_state &= ~IN_LST_MASK;
            if (colstate & IN_LST_CODE_C) {
                cp_colorize_line(cp, str, 0, n, sbuf, &c_mode);
            } else
            if (colstate & IN_LST_CODE_CPP) {
                cp_colorize_line(cp, str, 0, n, sbuf, &cpp_mode);
            } else {
                SET_STYLE(sbuf, 0, n, LST_STYLE_OUTPUT);
            }
            colstate &= IN_LST_MASK;
            colstate |= cp->colorize_state & ~IN_LST_MASK;
        }
    }
    cp->colorize_state = colstate;
}

static ModeDef arm_lst_mode = {
    .name = "arm-lst",
    .extensions = "lst",
    .colorize_func = lst_colorize_line,
    .keywords = arm_registers,
};

static int arm_lst_init(QEmacsState *qs)
{
    qe_register_mode(qs, &arm_lst_mode, MODEF_SYNTAX);
    return 0;
}

/*---------------- Intel Hex file coloring ----------------*/

enum {
    INTEL_HEX_STYLE_TEXT     = QE_STYLE_DEFAULT,
    INTEL_HEX_STYLE_LEAD     = QE_STYLE_FUNCTION,
    INTEL_HEX_STYLE_SIZE     = QE_STYLE_NUMBER,
    INTEL_HEX_STYLE_OFFSET   = QE_STYLE_COMMENT,
    INTEL_HEX_STYLE_RECTYPE  = QE_STYLE_KEYWORD,
    INTEL_HEX_STYLE_DUMP     = QE_STYLE_FUNCTION,
    INTEL_HEX_STYLE_CHECKSUM = QE_STYLE_DEFAULT,
    INTEL_HEX_STYLE_ERROR    = QE_STYLE_ERROR,
};

static void intel_hex_colorize_line(QEColorizeContext *cp,
                                    const char32_t *str, int n,
                                    QETermStyle *sbuf, ModeDef *syn)
{
    if (n > 10 && str[0] == ':') {
        /* Hex Load format: `:SSOOOOTTxx...xxCC` */
        int i, sh, sum = 0, chksum = 0;
        for (i = 1, sh = 4; i < n - 2; i++) {
            int x = qe_digit_value(str[i]);
            if (x < 16) {
                sum += x << sh;
                sh ^= 4;
            }
        }
        sum = (-sum & 0xFF);
        chksum = qe_digit_value(str[i]) << 4;
        chksum += qe_digit_value(str[i + 1]);

        SET_STYLE(sbuf, 0, 1, INTEL_HEX_STYLE_LEAD);
        SET_STYLE(sbuf, 1, 3, INTEL_HEX_STYLE_SIZE);
        SET_STYLE(sbuf, 3, 7, INTEL_HEX_STYLE_OFFSET);
        SET_STYLE(sbuf, 7, 9, INTEL_HEX_STYLE_RECTYPE);
        SET_STYLE(sbuf, 9, n - 2, INTEL_HEX_STYLE_DUMP);
        SET_STYLE(sbuf, n - 2, n, (chksum == sum) ?
                  INTEL_HEX_STYLE_CHECKSUM : INTEL_HEX_STYLE_ERROR);
    }
}

static int intel_hex_mode_probe(ModeDef *syn, ModeProbeData *pd)
{
    const char *p = (const char *)pd->buf;
    int i;

    if (match_extension(pd->filename, syn->extensions) && *p == ':') {
        for (i = 1; i < 11; i++) {
            if (!qe_isxdigit(p[i]))
                return 1;
        }
        return 70;
    }
    return 0;
}

static ModeDef intel_hex_mode = {
    .name = "intel-hex",
    .extensions = "hex",
    .mode_probe = intel_hex_mode_probe,
    .colorize_func = intel_hex_colorize_line,
};

static int intel_hex_init(QEmacsState *qs)
{
    qe_register_mode(qs, &intel_hex_mode, MODEF_SYNTAX);
    return 0;
}

/*----------------*/

static int arm_modes_init(QEmacsState *qs)
{
    arm_asm_init(qs);
    arm_lst_init(qs);
    intel_hex_init(qs);
    return 0;
}

qe_module_init(arm_modes_init);
