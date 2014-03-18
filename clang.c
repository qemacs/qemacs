/*
 * C mode for QEmacs.
 *
 * Copyright (c) 2001-2002 Fabrice Bellard.
 * Copyright (c) 2002-2014 Charlie Gordon.
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

#if 0
static const char cc_keywords[] =
    "asm|catch|class|delete|friend|inline|new|operator|"
    "private|protected|public|template|try|this|virtual|throw|";

static const char java_keywords[] =
    "abstract|boolean|byte|catch|class|extends|false|final|"
    "finally|function|implements|import|in|instanceof|"
    "interface|native|new|null|package|private|protected|"
    "public|super|synchronized|this|throw|throws|transient|"
    "true|try|var|with|";
#endif

static const char *c_mode_keywords =
    "auto|break|case|const|continue|default|do|else|enum|extern|for|goto|"
    "if|inline|register|restrict|return|sizeof|static|struct|switch|"
    "typedef|union|volatile|while|";

/* NOTE: 'var' is added for javascript */
static const char *c_mode_types =
    "char|double|float|int|long|unsigned|short|signed|void|var|"
    "_Bool|_Complex|_Imaginary|";

static const char c_mode_extensions[] =
    "c|h|C|H|"          /* C language */
    "y|l|lex|"          /* yacc, lex */
    "cc|hh|cpp|hpp|cxx|hxx|CPP|CC|c++|"   /* C++ */
    "m|"                /* Objective-C */
    "e|qe|cs|idl|"
    "jav|java|js|json|" /* Java, Javascript, JSon */
    "ec|ecp|"           /* Informix embedded C */
    "pgc|"              /* Postgres embedded C */
    "pcc|"              /* Oracle C++ */
    "cal"               /* GNU Calc */
    ;

/* grab a C identifier from a uchar buf, stripping color.
 * return char count.
 */
static int get_c_identifier(char *buf, int buf_size, unsigned int *p)
{
    unsigned int c;
    int i, j;

    i = j = 0;
    c = p[i];
    if (qe_isalpha_(c & CHAR_MASK)) {
        do {
            if (j < buf_size - 1)
                buf[j++] = c;
            i++;
            c = p[i];
        } while (qe_isalnum_(c & CHAR_MASK));
    }
    buf[j] = '\0';
    return i;
}

/* c-mode colorization states */
enum {
    C_COMMENT    = 1,   /* multiline comment pending */
    C_COMMENT1   = 2,   /* single line comment with \ at EOL */
    C_STRING     = 4,   /* double quoted string spanning multiple lines */
    C_STRING_Q   = 8,   /* single quoted string spanning multiple lines */
    C_PREPROCESS = 16,  /* preprocessor directive with \ at EOL */
};

void c_colorize_line(unsigned int *buf, int len,
                     int *colorize_state_ptr, __unused__ int state_only)
{
    int c, state, style, style1, type_decl, klen, delim;
    unsigned int *p, *p_start, *p_end, *p1, *p2;
    char kbuf[32];

    state = *colorize_state_ptr;
    p = buf;
    p_start = p;
    p_end = p + len;
    type_decl = 0;

    if (p >= p_end)
        goto the_end;

    c = 0;      /* turn off stupid egcs-2.91.66 warning */
    style = 0;

    if (state) {
        /* if already in a state, go directly in the code parsing it */
        if (state & C_PREPROCESS)
            style = QE_STYLE_PREPROCESS;
        if (state & C_COMMENT)
            goto parse_comment;
        if (state & C_COMMENT1)
            goto parse_comment1;
        if (state & C_STRING)
            goto parse_string;
        if (state & C_STRING_Q)
            goto parse_string_q;
    }

    while (p < p_end) {
        p_start = p;
        c = *p++;

        switch (c) {
        case '/':
            if (*p == '*') {
                /* normal comment */
                p++;
            parse_comment:
                state |= C_COMMENT;
                while (p < p_end) {
                    if (p[0] == '*' && p[1] == '/') {
                        p += 2;
                        state &= ~C_COMMENT;
                        break;
                    } else {
                        p++;
                    }
                }
                set_color(p_start, p, QE_STYLE_COMMENT);
                continue;
            } else
            if (*p == '/') {
                /* line comment */
            parse_comment1:
                state |= C_COMMENT1;
                p = p_end;
                set_color(p_start, p, QE_STYLE_COMMENT);
                goto the_end;
            }
            break;
        case '#':       /* preprocessor */
            state = C_PREPROCESS;
            style = QE_STYLE_PREPROCESS;
            break;
        case 'L':       /* wide character and string literals */
            if (*p == '\'') {
                p++;
                goto parse_string_q;
            }
            if (*p == '\"') {
                p++;
                goto parse_string;
            }
            goto normal;
        case '\'':      /* character constant */
        parse_string_q:
            state |= C_STRING_Q;
            style1 = QE_STYLE_STRING_Q;
            delim = '\'';
            goto string;
        case '\"':      /* string literal */
        parse_string:
            state |= C_STRING;
            style1 = QE_STYLE_STRING;
            delim = '\"';
        string:
            while (p < p_end) {
                if (*p == '\\') {
                    p++;
                    if (p >= p_end)
                        break;
                    p++;
                } else
                if ((int)*p == delim) {
                    p++;
                    state &= ~(C_STRING | C_STRING_Q);
                    break;
                } else {
                    p++;
                }
            }
            if (state & C_PREPROCESS)
                style1 = QE_STYLE_PREPROCESS;
            set_color(p_start, p, style1);
            continue;
        case '=':
            /* exit type declaration */
            /* does not handle this: int i = 1, j = 2; */
            type_decl = 0;
            break;
        case '<':       /* JavaScript extension */
            if (*p == '!' && p[1] == '-' && p[2] == '-')
                goto parse_comment1;
            break;
        default:
        normal:
            if (state & C_PREPROCESS)
                break;
            if (qe_isdigit(c)) {
                while (qe_isalnum(*p) || *p == '.') {
                    p++;
                }
                set_color(p_start, p, QE_STYLE_NUMBER);
                continue;
            }
            if (qe_isalpha_(c)) {

                /* XXX: should support :: and $ */
                klen = 0;
                p--;
                do {
                    if (klen < countof(kbuf) - 1)
                        kbuf[klen++] = c;
                    p++;
                    c = *p;
                } while (qe_isalnum_(c));
                kbuf[klen] = '\0';

                if (strfind(c_mode_keywords, kbuf)) {
                    set_color(p_start, p, QE_STYLE_KEYWORD);
                    continue;
                }

                p1 = p;
                while (qe_isblank(*p1))
                    p1++;
                p2 = p1;
                while (*p2 == '*' || qe_isblank(*p2))
                    p2++;

                if (strfind(c_mode_types, kbuf)
                ||  (klen > 2 && kbuf[klen - 2] == '_' && kbuf[klen - 1] == 't')) {
                    /* c type */
                    /* if not cast, assume type declaration */
                    if (*p2 != ')') {
                        type_decl = 1;
                    }
                    set_color(p_start, p, QE_STYLE_TYPE);
                    continue;
                }

                if (*p == '(') {
                    /* function call */
                    /* XXX: different styles for call and definition */
                    set_color(p_start, p, QE_STYLE_FUNCTION);
                    continue;
                }
                /* assume typedef if starting at first column */
                if (p_start == buf)
                    type_decl = 1;

                if (type_decl) {
                    if (p_start == buf) {
                        /* assume type if first column */
                        set_color(p_start, p, QE_STYLE_TYPE);
                    } else {
                        set_color(p_start, p, QE_STYLE_VARIABLE);
                    }
                }
                continue;
            }
            break;
        }
        set_color1(p_start, style);
    }
 the_end:
    /* strip state if not overflowing from a comment */
    if (!(state & C_COMMENT) && p > buf && ((p[-1] & CHAR_MASK) != '\\'))
        state &= ~(C_COMMENT1 | C_PREPROCESS);
    *colorize_state_ptr = state;
}

#define MAX_STACK_SIZE  64

/* gives the position of the first non white space character in
   buf. TABs are counted correctly */
static int find_indent1(EditState *s, unsigned int *buf)
{
    unsigned int *p;
    int pos, c;

    p = buf;
    pos = 0;
    for (;;) {
        c = *p++ & CHAR_MASK;
        if (c == '\t')
            pos += s->b->tab_width - (pos % s->b->tab_width);
        else if (c == ' ')
            pos++;
        else
            break;
    }
    return pos;
}

static int find_pos(EditState *s, unsigned int *buf, int size)
{
    int pos, c, i;

    pos = 0;
    for (i = 0; i < size; i++) {
        c = buf[i] & CHAR_MASK;
        if (c == '\t') {
            pos += s->b->tab_width - (pos % s->b->tab_width);
        } else {
            /* simplistic case: assume single width characters */
            pos++;
        }
    }
    return pos;
}

enum {
    INDENT_NORM,
    INDENT_FIND_EQ,
};

/* Check if indentation is already what it should be */
static int check_indent(EditState *s, int offset, int i, int *offset_ptr)
{
    int tw, col, ntabs, nspaces, bad;
    int offset1;

    tw = s->b->tab_width > 0 ? s->b->tab_width : 8;
    col = ntabs = nspaces = bad = 0;

    for (;;) {
        int c = eb_nextc(s->b, offset1 = offset, &offset);
        if (c == '\t') {
            col += tw - col % tw;
            bad |= nspaces;
            ntabs += 1;
        } else
        if (c == ' ') {
            col += 1;
            nspaces += 1;
        } else {
            break;
        }
    }

    *offset_ptr = offset1;

    if (col != i || bad)
        return 0;

    /* check tabs */
    if (s->indent_tabs_mode) {
        return (nspaces >= tw) ? 0 : 1;
    } else {
        return (ntabs > 0) ? 0 : 1;
    }
}

/* Insert n spaces at beginning of line at <offset>.
 * Store new offset after indentation to <*offset_ptr>.
 * Tabs are inserted if s->indent_tabs_mode is true.
 */
static void insert_indent(EditState *s, int offset, int i, int *offset_ptr)
{
    /* insert tabs */
    if (s->indent_tabs_mode) {
        int tw = s->b->tab_width > 0 ? s->b->tab_width : 8;
        while (i >= tw) {
            offset += eb_insert_uchar(s->b, offset, '\t');
            i -= tw;
        }
    }

    /* insert needed spaces */
    offset += eb_insert_spaces(s->b, offset, i);

    *offset_ptr = offset;
}

/* indent a line of C code starting at <offset> */
static void c_indent_line(EditState *s, int offset0)
{
    int offset, offset1, offsetl, c, pos, line_num, col_num;
    int i, j, eoi_found, len, pos1, lpos, style, line_num1, state;
    unsigned int buf[COLORED_MAX_LINE_SIZE], *p;
    unsigned char stack[MAX_STACK_SIZE];
    char buf1[64], *q;
    int stack_ptr;

    /* find start of line */
    eb_get_pos(s->b, &line_num, &col_num, offset0);
    line_num1 = line_num;
    offset = eb_goto_bol(s->b, offset0);
    /* now find previous lines and compute indent */
    pos = 0;
    lpos = -1; /* position of the last instruction start */
    offsetl = offset;
    eoi_found = 0;
    stack_ptr = 0;
    state = INDENT_NORM;
    for (;;) {
        if (offsetl == 0)
            break;
        line_num--;
        offsetl = eb_prev_line(s->b, offsetl);
        offset1 = offsetl;
        len = s->get_colorized_line(s, buf, countof(buf), &offset1, line_num);
        /* store indent position */
        pos1 = find_indent1(s, buf);
        p = buf + len;
        while (p > buf) {
            p--;
            c = *p;
            /* skip strings or comments */
            style = c >> STYLE_SHIFT;
            if (style == QE_STYLE_COMMENT
            ||  style == QE_STYLE_STRING
            ||  style == QE_STYLE_PREPROCESS) {
                continue;
            }
            c = c & CHAR_MASK;
            if (state == INDENT_FIND_EQ) {
                /* special case to search '=' or ; before { to know if
                   we are in data definition */
                if (c == '=') {
                    /* data definition case */
                    pos = lpos;
                    goto end_parse;
                } else if (c == ';') {
                    /* normal instruction case */
                    goto check_instr;
                }
            } else {
                switch (c) {
                case '}':
                    if (stack_ptr >= MAX_STACK_SIZE)
                        return;
                    stack[stack_ptr++] = c;
                    goto check_instr;
                case '{':
                    if (stack_ptr == 0) {
                        if (lpos == -1) {
                            pos = pos1 + s->indent_size;
                            eoi_found = 1;
                            goto end_parse;
                        } else {
                            state = INDENT_FIND_EQ;
                        }
                    } else {
			if (stack[--stack_ptr] != '}') {
			    /* XXX: syntax check ? */
			    goto check_instr;
			}
			goto check_instr;
                    }
                    break;
                case ')':
                case ']':
                    if (stack_ptr >= MAX_STACK_SIZE)
                        return;
                    stack[stack_ptr++] = c;
                    break;
                case '(':
                case '[':
                    if (stack_ptr == 0) {
                        pos = find_pos(s, buf, p - buf) + 1;
                        goto end_parse;
                    } else {
			if (stack[--stack_ptr] != (c == '(' ? ')' : ']')) {
			    /* XXX: syntax check ? */
			}
                    }
                    break;
                case ' ':
                case '\t':
                case '\n':
                    break;
                case ';':
                    /* level test needed for 'for(;;)' */
                    if (stack_ptr == 0) {
                        /* ; { or } are found before an instruction */
                    check_instr:
                        if (lpos >= 0) {
                            /* start of instruction already found */
                            pos = lpos;
                            if (!eoi_found)
                                pos += s->indent_size;
                            goto end_parse;
                        }
                        eoi_found = 1;
                    }
                    break;
                case ':':
                    /* a label line is ignored */
                    /* XXX: incorrect */
                    if (style == QE_STYLE_DEFAULT)
                        goto prev_line;
                    break;
                default:
                    if (stack_ptr == 0) {
                        if (style == QE_STYLE_KEYWORD) {
                            unsigned int *p1, *p2;
                            /* special case for if/for/while */
                            p1 = p;
                            while (p > buf &&
                                   (p[-1] >> STYLE_SHIFT) == QE_STYLE_KEYWORD)
                                p--;
                            p2 = p;
                            q = buf1;
                            while (q < buf1 + countof(buf1) - 1 && p2 <= p1) {
                                *q++ = *p2++ & CHAR_MASK;
                            }
                            *q = '\0';

                            if (!eoi_found && strfind("if|for|while", buf1)) {
                                pos = pos1 + s->indent_size;
                                goto end_parse;
                            }
                        }
                        lpos = pos1;
                    }
                    break;
                }
            }
        }
    prev_line: ;
    }
  end_parse:
    /* compute special cases which depend on the chars on the current line */
    offset1 = offset;
    len = s->get_colorized_line(s, buf, countof(buf), &offset1, line_num1);

    if (stack_ptr == 0) {
        if (!pos && lpos >= 0) {
            /* start of instruction already found */
            pos = lpos;
            if (!eoi_found)
                pos += s->indent_size;
        }
    }

    for (i = 0; i < len; i++) {
        c = buf[i];
        style = c >> STYLE_SHIFT;
        if (qe_isblank(c & CHAR_MASK))
            continue;
        /* if preprocess, no indent */
        if (style == QE_STYLE_PREPROCESS) {
            pos = 0;
            break;
        }
        if (qe_isalpha_(c & CHAR_MASK)) {
            j = get_c_identifier(buf1, countof(buf1), buf + i);

            if (style == QE_STYLE_KEYWORD) {
                if (strfind(buf1, "case|default"))
                    goto unindent;
            }
            for (j += i; qe_isblank(buf[j] & CHAR_MASK); j++)
                continue;
            if (buf[j] == ':')
                goto unindent;
        }
        /* NOTE: strings & comments are correctly ignored there */
        if ((c == '&' || c == '|') && buf[i + 1] == c)
            goto unindent;

        if (c == '}') {
        unindent:
            pos -= s->indent_size;
            if (pos < 0)
                pos = 0;
            break;
        }
        if (c == '{' && pos == s->indent_size && !eoi_found) {
            pos = 0;
            break;
        }
        break;
    }

    /* the computed indent is in 'pos' */
    /* if on a blank line, reset indent to 0 unless point is on it */
    if (eb_is_blank_line(s->b, offset, &offset1)
    &&  !(s->offset >= offset && s->offset < offset1)) {
        pos = 0;
    }
    /* Do not modify buffer if indentation in correct */
    if (!check_indent(s, offset, pos, &offset1)) {
        /* simple approach to normalization of indentation */
        eb_delete_range(s->b, offset, offset1);
        insert_indent(s, offset, pos, &offset1);
    }
    /* move to the indentation if point was in indent space */
    if (s->offset >= offset && s->offset < offset1) {
        s->offset = offset1;
    }
}

static void do_c_indent(EditState *s)
{
    if (s->qe_state->last_cmd_func == (CmdFunc)do_c_indent) {
        do_tab(s, 1);
    } else {
        c_indent_line(s, s->offset);
    }
}

static void do_c_indent_region(EditState *s)
{
    int col_num, line1, line2;

    /* deactivate region hilite */
    s->region_style = 0;

    /* Swap point and mark so mark <= point */
    if (s->offset < s->b->mark) {
        int tmp = s->b->mark;
        s->b->mark = s->offset;
        s->offset = tmp;
    }
    /* We do it with lines to avoid offset variations during indenting */
    eb_get_pos(s->b, &line1, &col_num, s->b->mark);
    eb_get_pos(s->b, &line2, &col_num, s->offset);

    if (col_num == 0)
        line2--;

    /* Iterate over all lines inside block */
    for (; line1 <= line2; line1++) {
        c_indent_line(s, eb_goto_pos(s->b, line1, 0));
    }
}

static void do_c_electric(EditState *s, int key)
{
    int offset = s->offset;

    do_char(s, key, 1);
    c_indent_line(s, offset);
}

static void do_c_return(EditState *s)
{
    int offset = s->offset;

    do_return(s, 1);
    /* reindent line to remove indent on blank line */
    c_indent_line(s, offset);
}

/* forward / backward preprocessor */
static void do_c_forward_preprocessor(EditState *s, int dir)
{
}

static int c_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    /* trust the file extension */
    if (match_extension(p->filename, mode->extensions))
        return 80;

    /* weaker match on C comment start */
    if (p->buf[0] == '/' && p->buf[1] == '*')
        return 60;

    /* even weaker match on C++ comment start */
    if (p->buf[0] == '/' && p->buf[1] == '/')
        return 50;

    if (p->buf[0] == '#') {
        if (p->buf[1] == '!'
        &&  memstr(p->buf, p->line_len, "bin/calc")) {
            /* GNU Calc script */
            return 80;
        }
        /* same for file starting with '#include' */
        if (strstart(cs8(p->buf), "#include", NULL))
            return 50;

        /* same for file starting with '#pragma' as in #pragma once */
        if (strstart(cs8(p->buf), "#pragma", NULL))
            return 50;
    }

    return 1;
}

/* C mode specific commands */
static CmdDef c_commands[] = {
    CMD2( KEY_CTRL('i'), KEY_NONE,
          "c-indent-command", do_c_indent, ES, "*")
    CMD2( KEY_META(KEY_CTRL('\\')), KEY_NONE,
          "c-indent-region", do_c_indent_region, ES, "*")
            /* should map to KEY_META + KEY_CTRL_LEFT ? */
    CMD3( KEY_META('['), KEY_NONE,
          "c-backward-preprocessor", do_c_forward_preprocessor, ESi, -1, "*v")
    CMD3( KEY_META(']'), KEY_NONE,
          "c-forward-preprocessor", do_c_forward_preprocessor, ESi, 1, "*v")
    CMD2( '{', '}',
          "c-electric-key", do_c_electric, ESi, "*ki")
    CMD2( KEY_RET, KEY_NONE,
          "c-newline", do_c_return, ES, "*v")
    CMD_DEF_END,
};

static ModeDef c_mode;

static int c_init(void)
{
    const char *p;

    /* C mode is almost like the text mode, so we copy and patch it */
    memcpy(&c_mode, &text_mode, sizeof(ModeDef));
    c_mode.name = "C";
    c_mode.extensions = c_mode_extensions;
    c_mode.mode_probe = c_mode_probe;
    c_mode.colorize_func = c_colorize_line;

    qe_register_mode(&c_mode);
    qe_register_cmd_table(c_commands, &c_mode);
    for (p = ";:#&|"; *p; p++) {
        qe_register_binding(*p, "c-electric-key", &c_mode);
    }
    return 0;
}

qe_module_init(c_init);
