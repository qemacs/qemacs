/*
 * Miscellaneous QEmacs modes for arm development related file formats
 *
 * Copyright (c) 2014-2017 Charlie Gordon.
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

char const arm_registers[] = {
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
    IN_ASM_TRAIL = 1,   /* beyond .end directive */
};

static int arm_asm_match_keyword(const unsigned int *buf, const char *str)
{
    while (*str) {
        if (*buf++ != *str++)
            return 0;
    }
    return !qe_isalnum_(*buf);
}

#define MAX_KEYWORD_SIZE  16

static void arm_asm_colorize_line(QEColorizeContext *cp,
                                  unsigned int *str, int n, ModeDef *syn)
{
    char keyword[MAX_KEYWORD_SIZE];
    int i = 0, start = 0, c, style, klen, w = 0, wn = 0;
    int colstate = cp->colorize_state;

    if (colstate & IN_ASM_TRAIL)
        goto comment;

    for (; qe_isblank(str[i]); i++)
        continue;

    style = 0;
    w = i;
    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '#':
            if (start == 0)
                goto comment;
            break;
        case '.':
            if (start > w)
                break;
            /* scan for comment */
            if (arm_asm_match_keyword(str + i, "end")) {
                colstate |= IN_ASM_TRAIL;
            }
            if (arm_asm_match_keyword(str + i, "byte")
            ||  arm_asm_match_keyword(str + i, "word")
            ||  arm_asm_match_keyword(str + i, "long")) {
                goto opcode;
            }
            for (; i < n; i++) {
                if (str[i] == '@')
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
            for (; i < n; i++) {
                if (str[i] == (unsigned int)c) {
                    i++;
                    break;
                }
            }
            style = ASM_STYLE_STRING;
            break;
        case ';':       /* instruction separator */
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
            SET_COLOR(str, start, i, style);
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

static int arm_asm_init(void)
{
    qe_register_mode(&arm_asm_mode, MODEF_SYNTAX);
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
                              unsigned int *str, int n, ModeDef *syn)
{
    /* Combined assembly / C source / filename listing:
     * determine line type by looking at line start
     */
    char kbuf[16];
    int i, w, start, c, colstate = cp->colorize_state;

    for (w = 0; qe_isblank(str[w]); w++)
        continue;

    if (str[0] && str[1] == ':' && str[2] == '\\') {
        /* has full DOS/Windows pathname */
        if (ustristr(str, ".c:") || ustristr(str, ".h:")) {
            colstate = IN_LST_CODE_C;
        } else
        if (ustristr(str, ".cpp:")) {
            colstate = IN_LST_CODE_CPP;
        }
        SET_COLOR(str, 0, n, LST_STYLE_FILENAME);
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
            SET_COLOR(str, start, i, LST_STYLE_OFFSET);

            for (; qe_isblank(str[i]); i++)
                continue;
            for (start = i; qe_isxdigit(str[i]); i++)
                continue;
            if (str[i] == ' ' && qe_isxdigit(str[i + 1])) {
                for (i += 2; qe_isxdigit(str[i]); i++)
                    continue;
            }
            SET_COLOR(str, start, i, LST_STYLE_DUMP);
            for (; qe_isblank(str[i]); i++)
                continue;
            for (start  = i; i < n && !qe_isblank(str[i]); i++)
                continue;
            SET_COLOR(str, start, i, LST_STYLE_OPCODE);
            for (; qe_isblank(str[i]); i++)
                continue;
            while (i < n) {
                start = i;
                c = str[i++];
                if (c == ';') {
                    i = n;
                    SET_COLOR(str, start, i, LST_STYLE_COMMENT);
                    continue;
                }
                if (qe_isdigit(c)) {
                    for (; qe_isalnum(str[i]); i++)
                        continue;
                    SET_COLOR(str, start, i, LST_STYLE_NUMBER);
                    continue;
                }
                if (qe_isalpha_(c)) {
                    i += ustr_get_identifier(kbuf, countof(kbuf), c, str, i, n);
                    if (strfind(syn->keywords, kbuf))
                        SET_COLOR(str, start, i, LST_STYLE_KEYWORD);
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
                c_mode.colorize_func(cp, str, n, &c_mode);
            } else
            if (colstate & IN_LST_CODE_CPP) {
                cpp_mode.colorize_func(cp, str, n, &cpp_mode);
            } else {
                SET_COLOR(str, 0, n, LST_STYLE_OUTPUT);
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

static int arm_lst_init(void)
{
    qe_register_mode(&arm_lst_mode, MODEF_SYNTAX);
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
                                    unsigned int *str, int n, ModeDef *syn)
{
    if (n > 10 && str[0] == ':') {
        /* Hex Load format: `:SSOOOOTTxx...xxCC` */
        int i, sh, sum = 0, chksum = 0;
        for (i = 1, sh = 4; i < n - 2; i++) {
            int x = to_hex(str[i]);
            if (x >= 0) {
                sum += x << sh;
                sh ^= 4;
            }
        }
        sum = (-sum & 0xFF);
        chksum = to_hex(str[i]) << 4;
        chksum += to_hex(str[i + 1]);

        SET_COLOR(str, 0, 1, INTEL_HEX_STYLE_LEAD);
        SET_COLOR(str, 1, 3, INTEL_HEX_STYLE_SIZE);
        SET_COLOR(str, 3, 7, INTEL_HEX_STYLE_OFFSET);
        SET_COLOR(str, 7, 9, INTEL_HEX_STYLE_RECTYPE);
        SET_COLOR(str, 9, n - 2, INTEL_HEX_STYLE_DUMP);
        SET_COLOR(str, n - 2, n, (chksum == sum) ?
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

static int intel_hex_init(void)
{
    qe_register_mode(&intel_hex_mode, MODEF_SYNTAX);
    return 0;
}

/*----------------*/

static int arm_modes_init(void)
{
    arm_asm_init();
    arm_lst_init();
    intel_hex_init();
    return 0;
}

qe_module_init(arm_modes_init);
