/*
 * Miscellaneous QEmacs modes for Forth variants
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

/* XXX: should have different flavors of Forth:
 * FreeForth, ficl, gforth...
 */

/*---------------- Free Forth language coloring ----------------*/

static char const ff_keywords[] = {
    "|rst|>SC|SC|>S1|>S0|>C1|>C0|c04|s09|s08|s01|s1|,3`|,4`|,2`|,1`"
    "|here`|allot`|align`|,`|w,`|c,`|swap`|2dup`|dup`|nipdup`|tuck`"
    "|over`|under`|pick`|2drop`|drop`|nip`|rot`|-rot`|>rswapr>`|depth"
    "|>r`|2>r`|dup>r`|r>`|2r>`|dropr>`|r`|2r`|rdrop`|2rdrop`|rp@`|sp@`"
    // |` requires a different separator
    "|over&`|over|`|over^`|2dup+`|over+`|over-`|over*`|&`|^`|+`|-`|*`|/`|%`"
    "|~`|negate`|bswap`|flip`|invert|not|and|or|xor|mod|1-`"
    "|1+`|2+`|4+`|2*`|2/`|4*`|4/`|8*`|8/`|<<`|>>`|m/mod`|/%`|min`|max`"
    "|within|bounds`|@`|c@`|w@`|2@`|dup@`|dupc@`|dupw@`|@+`|c@+`|w@+`"
    "|!`|c!`|w!`|2!`|+!`|-!`|over!`|overc!`|overw!`|over+!`|over-!`"
    "|tuck!`|tuckc!`|tuckw!`|tuck+!`|tuck-!`|2dup!`|2dupc!`|2dupw!`"
    "|2dup+!`|2dup-!`|on`|off`|erase|fill|move|cmove`|place`|$-|search"
    "|lit`|'`|-call|call,|callmark|;;`|tailrec|anon:`|anon|;`|[`|]`"
    "|H|header|find|which|>in|tp|tib|eob|\\`|(`|EOF`|parse|wsparse|lnparse"
    "|compiler|number|notfound|classes|:`|alias`|create`|variable`|constant`"
    "|equ`|:^`|^^`|!^`|@^`|execute|reverse`|catch|throw|:|;|?"
    "|+longconds`|-longconds`|?`|0>`|0<=`|0>=`|0<`|0<>`|0=`|C1?`|C0?`"
    "|0-`|`?1|`?#|<>`|=`|>`|<=`|>=`|<`|u>`|u<=`|u>=`|u<`|`?2|BOOL`"
    "|nzTRUE|zFALSE|`?off|`cond|IF`|CASE`|ELSE`|SKIP`|THEN`|;THEN`"
    "|BEGIN`|`mrk|TIMES`|RTIMES`|START`|ENTER`|0;`|TILL`|WHILE`|AGAIN`"
    "|BREAK`|END`|UNTIL`|REPEAT`|if`|0=if`|0<if`|0>=if`|=if`|<>if`|<if`"
    "|<=if`|u<if`|u<=if`|then`|;then`|else`|again`|while`|repeat`|for`"
    "|next`|[THEN]`|[ELSE]`|[IF]`|[0]`|[1]`|[~]`|[os]`|syscall|stdin|stdout"
    "|open'|openr|openw|openw0|close|read|write|lseek|ioctl|select"
    "|malloc|free|type|accept|emit|space|cr|key|.|.\\|.digit|base"
    "|.l|.w|.b|.#s|.dec|.dec\\|dump|2dump|;dump`|stopdump?|ui|prompt"
    "|.s`|.h`|words`|hid'm`|mark`|marker|loc:`|needs`|needed|eval|bye`"
    "|exit|#lib|#fun|#call|lib:`|fun:`|libc.`|libc|man`|k32.`|k32"
    "|win32.hlp`|ior|?ior|zt|cd`|shell|!!`|cls`|home|atxy|normal"
    "|background|foreground|.d|.wd|.dt|.t|.now`|now|ms|ms@|}}}`|{{{`"
    "|fcell|fsw@|fcw@|fcw!|floor|f>df|df>f|f>s|s>f|`f:`|finit`|fpi`"
    "|1.`|0.`|fdup`|fover`|fdrop`|fnip`|fswap`|f2drop`|f2dup`|ftuck`"
    "|funder`|frot`|f-rot`|fmax`|fmin`|fabs`|fnegate`|f+`|fover+`|f-`"
    "|fover-`|fswap-`|f*`|fover*`|f/`|fover/`|fswap/`|f1/`|`fscale`"
    "|`fxtract`|f2/|f2*|`fldln2|`fldlg2|`fldl2e|`fldl2t|`fxl2y|`fxl2yp1"
    "|`f2xm1|fln`|flog`|f**|faln|falog|fsqrt`|sqrt|fsinh|fcosh|ftanh"
    "|fasinh|facosh|fatanh|fsin`|fcos`|ftan`|fsincos`|fasin|facos"
    "|fatan`|fatan2`|f0<`|f0>=`|f0<>`|f0=`|f0<=`|f0>`|`f?1|f<`|f>=`"
    "|f<>`|f=`|f<=`|f>`|`f?2|f~|f@`|dupf@`|f+!`|f!`|dupf!`|f,`|fvariable`"
    "|flit`|fconstant`|f#|f.|f.s`|fnumber|uart!|port!|.ports`|COM"
    "|bps|.bps`|noParity|oddParity|evenParity|DSR?|CTS?|RI?|CD?"
    "|RTS0|RTS1|DTR0|DTR1|UBREAK|RX|RX?|key?|TX|XRECV|XSEND"
    "|dumpterm|dumbterm|utrace"
    "|"
};

enum {
    FF_STYLE_TEXT        = QE_STYLE_DEFAULT,
    FF_STYLE_COMMENT     = QE_STYLE_COMMENT,
    FF_STYLE_KEYWORD     = QE_STYLE_KEYWORD,
    FF_STYLE_STRING      = QE_STYLE_STRING,
    FF_STYLE_NUMBER      = QE_STYLE_NUMBER,
};

enum {
    IN_FF_TRAIL = 1,     /* beyond EOF directive */
    IN_FF_COMMENT = 2,   /* multiline comment ( ... ) */
};

static int ff_convert_date(int year, int month, int day)
{
    /* convert date to generalized gregorian day number */
    int gday = 0;

    if (year >= 0 && month > 0) {
        static int const elapsed_days[12] = {
            0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334,
        };
        int mm = month - 1;
        int yy = year + mm / 12;
        mm %= 12;
        yy -= 1;
        gday = year * 365 + yy / 4 - yy / 100 + yy / 400;
        gday += elapsed_days[mm];
        if (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0) && mm > 1)
            gday += 1;
        gday += day - 60;       /* starting on 1/3/0000 */
    }
    return gday;
}

static int ff_match_number(const char *str, int *pnum)
{
    int i = 0, base = 10, digit;
    char32_t c;
    int year = -1, month = -1;
    long long num = 0, stash = 0;

    if (str[i] == '-') {
        i++;
        if (str[i] == '\0')
            return 0;
    }

    for (; (c = str[i]) != '\0'; i++) {
        switch (c) {
        case '\'':
            continue;
        case '$':
            base = 16;
            continue;
        case '&':
            base = 8;
            continue;
        case '%':
            base = 2;
            continue;
        case '#':
            base = num;
            num = 0;
            continue;
        case ':':
            if (i == 0)
                break;
            stash = (stash + num) * 60;
            num = 0;
            continue;
        case '_':
            if (i == 0)
                break;
            if (year >= 0 && month >= 0) {
                num = ff_convert_date(year, month, num);
                num = 0;
                year = month = -1;
            }
            stash = (stash + num) * 24;
            num = 0;
            continue;
        case '-':
            if (i == 0)
                break;
            if (year < 0)
                year = num;
            else
                month = num;
            num = 0;
            continue;
        default:
            if (c >= '0' && c <= '9')
                digit = c - '0';
            else
            if (c >= 'a' && c <= 'z')
                digit = c - 'a' + 10;
            else
            if (c >= 'A' && c <= 'Z')
                digit = c - 'A' + 10;
            else
                digit = 255;
            if (digit >= base)
                break;
            num = num * base + digit;
            continue;
        }
        break;
    }
    if (year >= 0 && month >= 0) {
        stash = ff_convert_date(year, month, num);
        num = 0;
    }
    num += stash;
    if (i > 0 && str[i] == '\0') {
        if (pnum)
            *pnum = (*str == '-') ? -num : num;
        return i;
    }
    return 0;
}

static void ff_colorize_line(QEColorizeContext *cp,
                             const char32_t *str, int n,
                             QETermStyle *sbuf, ModeDef *syn)
{
    char word[64];
    int i = 0, start = 0, num = 0, len, numlen, colstate = cp->colorize_state;
    char32_t c;

    if (colstate & IN_FF_TRAIL)
        goto comment;

    if (str[0] == '#' && str[1] == '!')
        goto comment;

    for (; i < n;) {
        start = i;
        c = str[i++];
        if (c == '(' && str[i] == ' ') {
            colstate |= IN_FF_COMMENT;
        }
        if (colstate & IN_FF_COMMENT) {
            if (c == ')')
                colstate &= ~IN_FF_COMMENT;
            SET_STYLE1(sbuf, start, FF_STYLE_COMMENT);
            continue;
        }
        if (qe_isblank(c))
            continue;
        if (c == '\\' && str[i] == ' ') {
        comment:
            i = n;
            SET_STYLE(sbuf, start, i, FF_STYLE_COMMENT);
            continue;
        }
        switch (c) {
        case ',':
        case '!':
        case '.':
            if (str[i] == '\"') {
                i++;
                goto string;
            }
            break;
        case '\"':
        string:
            /* parse string const */
            for (; i < n; i++) {
                if (str[i] == '\\' && i + 1 < n) {
                    i++;
                } else
                if (str[i] == '\"') {
                    i++;
                    break;
                }
            }
        has_string:
            SET_STYLE(sbuf, start, i, FF_STYLE_STRING);
            continue;
        default:
            break;
        }
        /* scan for space and determine word type */
        len = 0;
        word[len++] = c;
        for (; i < n && !qe_isblank(str[i]); i++) {
            if (len < countof(word) - 1)
                word[len++] = str[i];
        }
        word[len] = '\0';
        if (strequal("EOF", word) || strequal("EOF`", word)) {
            SET_STYLE(sbuf, start, i, FF_STYLE_KEYWORD);
            colstate |= IN_FF_TRAIL;
            start = i;
            goto comment;
        }
        if (word[len - 1] == '\"')
            goto has_string;

        if (strequal("|`", word) || strfind(syn->keywords, word)) {
            SET_STYLE(sbuf, start, i, FF_STYLE_KEYWORD);
            continue;
        }
        if (len < countof(word) - 1 && word[len - 1] != '`') {
            word[len] = '`';
            word[len + 1] = '\0';
            if (strequal("|`", word) || strfind(syn->keywords, word)) {
                SET_STYLE(sbuf, start, i, FF_STYLE_KEYWORD);
                continue;
            }
        }
        numlen = len;
        if (numlen > 1 && qe_findchar("|&^+-*/%~,", word[numlen - 1]))
            word[--numlen] = '\0';
        if (ff_match_number(word, &num) == numlen) {
            SET_STYLE(sbuf, start, start + numlen, FF_STYLE_NUMBER);
            if (numlen < len)
                SET_STYLE1(sbuf, start + numlen, FF_STYLE_KEYWORD);
            continue;
        }
    }
    cp->colorize_state = colstate;
}

static int ff_probe(ModeDef *mode, ModeProbeData *pd)
{
    const char *p = (const char *)pd->buf;
    const char *p1 = (const char *)pd->buf + pd->line_len;

    if (match_extension(pd->filename, mode->extensions)
    ||  match_shell_handler(cs8(pd->buf), mode->shell_handlers)) {
        return 80;
    }

    if ((p[0] == ':' || p[0] == '\\') && p[1] == ' ')
        return 60;

    if ((p1[0] == ':' || p1[0] == '\\') && p[1] == ' ')
        return 50;

    return 1;
}

static ModeDef ff_mode = {
    .name = "Forth",
    .extensions = "ff|fth|fs|fr|4th",
    .shell_handlers = "forth|fth",
    .mode_probe = ff_probe,
    .keywords = ff_keywords,
    .colorize_func = ff_colorize_line,
};

static int ff_init(QEmacsState *qs)
{
    qe_register_mode(qs, &ff_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(ff_init);
