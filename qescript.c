/*
 * QEmacs, tiny but powerful multimode editor
 *
 * Copyright (c) 2000-2002 Fabrice Bellard.
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
#include "variables.h"

/* qscript: config file parsing */

/* CG: error messages should go to the *error* buffer.
 * displayed as a popup upon start.
 */

typedef struct QEmacsDataSource {
    EditState *s;
    const char *filename;   // source filename
    char *allocated_buf;
    const char *buf;        // start of source block
    const char *p;          // pointer past current token
    const char *start_p;    // start of token in source block
    int line_num;           // source line number at ds->p
    int start_line;         // the source line number of ds->start_p
    int newline_seen;       // current token is first on line
    int tok;                // token type
    int prec;               // operator precedence
    int len;                // length of TOK_STRING and TOK_ID string
    int str_size;
    char *str;
    QEValue *sp_max;
    QEValue stack[16];
    char str_buf[256];      // token string (XXX: should use local buffer?)
} QEmacsDataSource;

enum {
    TOK_EOF = -1, TOK_ERR = -2, TOK_VOID__ = 0,
    TOK_NUMBER__ = 128, TOK_STRING__, TOK_CHAR__, TOK_ID__,
    TOK_IF, TOK_ELSE,
#ifndef CONFIG_TINY
    TOK_FOR, TOK_WHILE, TOK_BREAK, TOK_CONTINUE,
    TOK_SWITCH, TOK_CASE, TOK_DEFAULT,
    TOK_VAR, TOK_FUNCTION, TOK_RETURN,
    TOK_MUL_EQ, TOK_DIV_EQ, TOK_MOD_EQ, TOK_ADD_EQ, TOK_SUB_EQ,
    TOK_SHL_EQ, TOK_SHR_EQ, TOK_AND_EQ, TOK_XOR_EQ, TOK_OR_EQ,
    TOK_EQ, TOK_NE, TOK_SHL, TOK_SHR, TOK_LE, TOK_GE,
    TOK_INC, TOK_DEC, TOK_LOR, TOK_LAND,
#endif
};

enum {
    PREC_NONE, PREC_EXPRESSION, PREC_ASSIGNMENT, PREC_CONDITIONAL,
    PREC_LOGICAL_OR, PREC_LOGICAL_AND, PREC_BITOR, PREC_BITXOR,
    PREC_BITAND, PREC_EQUALITY, PREC_RELATIONAL, PREC_SHIFT,
    PREC_ADDITIVE, PREC_MULTIPLICATIVE, PREC_POSTFIX,
};

struct opdef {
    char str[4];
    u8 op, prec;
};

#define op(str, op, prec) { str, op, prec },
#ifdef CONFIG_TINY
#define OP(str, op, prec)
#else
#define OP(str, op, prec) { str, op, prec },
#endif
/* This table must be sorted in lexicographical order */
static const struct opdef ops[] = {
    OP( "!",   '!',        PREC_NONE )
    OP( "!=",  TOK_NE,     PREC_EQUALITY )
    OP( "%",   '%',        PREC_MULTIPLICATIVE )
    OP( "%=",  TOK_MOD_EQ, PREC_ASSIGNMENT )
    OP( "&",   '&',        PREC_BITAND )
    OP( "&&",  TOK_LAND,   PREC_LOGICAL_AND )
    OP( "&=",  TOK_AND_EQ, PREC_ASSIGNMENT )
    op( "(",   '(',        PREC_POSTFIX )
    op( ")",   ')',        PREC_NONE )
    OP( "*",   '*',        PREC_MULTIPLICATIVE )
    OP( "*=",  TOK_MUL_EQ, PREC_ASSIGNMENT )
    OP( "+",   '+',        PREC_ADDITIVE )
    OP( "++",  TOK_INC,    PREC_POSTFIX )
    OP( "+=",  TOK_ADD_EQ, PREC_ASSIGNMENT )
    op( ",",   ',',        PREC_EXPRESSION )
    OP( "-",   '-',        PREC_ADDITIVE )
    OP( "--",  TOK_DEC,    PREC_POSTFIX )
    OP( "-=",  TOK_SUB_EQ, PREC_ASSIGNMENT )
    OP( ".",   '.',        PREC_POSTFIX )
    OP( "/",   '/',        PREC_MULTIPLICATIVE )
    OP( "/=",  TOK_DIV_EQ, PREC_ASSIGNMENT )
    OP( ":",   ':',        PREC_NONE )
    op( ";",   ';',        PREC_NONE )
    OP( "<",   '<',        PREC_SHIFT )
    OP( "<<",  TOK_SHL,    PREC_SHIFT )
    OP( "<<=", TOK_SHL_EQ, PREC_ASSIGNMENT )
    OP( "<=",  TOK_LE,     PREC_RELATIONAL )
    op( "=",   '=',        PREC_ASSIGNMENT )
    OP( "==",  TOK_EQ,     PREC_EQUALITY )
    OP( ">",   '>',        PREC_SHIFT )
    OP( ">=",  TOK_GE,     PREC_RELATIONAL )
    OP( ">>",  TOK_SHR,    PREC_SHIFT )
    OP( ">>=", TOK_SHR_EQ, PREC_ASSIGNMENT )
    OP( "?",   '?',        PREC_CONDITIONAL )
    OP( "[",   '[',        PREC_POSTFIX )
    OP( "]",   ']',        PREC_NONE )
    OP( "^",   '^',        PREC_BITXOR )
    OP( "^=",  TOK_XOR_EQ, PREC_ASSIGNMENT )
    op( "{",   '{',        PREC_NONE )
    OP( "|",   '|',        PREC_BITOR )
    OP( "|=",  TOK_OR_EQ,  PREC_ASSIGNMENT )
    OP( "||",  TOK_LOR,    PREC_LOGICAL_OR )
    op( "}",   '}',        PREC_NONE )
    OP( "~",   '~',        PREC_NONE )
};
#undef OP
#undef op

static void qe_cfg_init(QEmacsDataSource *ds) {
    memset(ds, 0, sizeof(*ds));
    ds->sp_max = ds->stack;
    ds->str = ds->str_buf;
    ds->str_size = sizeof(ds->str_buf);
}

static void qe_cfg_release(QEmacsDataSource *ds) {
    // XXX: should free ds->allocated_buf ?
    QEValue *sp;
    for (sp = ds->stack; sp < ds->sp_max; sp++)
        qe_cfg_set_void(sp);
}

#ifndef CONFIG_TINY
static int qe_cfg_append_str_char32(QEmacsDataSource *ds, char32_t ch) {
    if (ds->len + MAX_CHAR_BYTES + 1 > ds->str_size) {
        int size = ds->str_size + (ds->str_size >> 1);
        char *str = qe_malloc_array(char, size);
        if (str == NULL)
            return 0;
        memcpy(str, ds->str, ds->len);
        if (ds->str != ds->str_buf)
            qe_free(&ds->str);
        ds->str = str;
        ds->str_size = size;
    }
    return ds->len += utf8_encode(ds->str + ds->len, ch);
}
#endif

static int qe_cfg_append_str_byte(QEmacsDataSource *ds, unsigned char c) {
    if (ds->len + 1 + 1 > ds->str_size) {
        int size = ds->str_size + (ds->str_size >> 1);
        char *str = qe_malloc_array(char, size);
        if (str == NULL)
            return 0;
        memcpy(str, ds->str, ds->len);
        if (ds->str != ds->str_buf)
            qe_free(&ds->str);
        ds->str = str;
        ds->str_size = size;
    }
    ds->str[ds->len++] = c;
    return ds->len;
}

static void qe_cfg_error(QEmacsDataSource *ds, const char *fmt, ...) qe__attr_printf(2,3);
static void qe_cfg_error(QEmacsDataSource *ds, const char *fmt, ...) {
    char buf[MAX_SCREEN_WIDTH];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    put_status(ds->s, "!\007\006script error: %s", buf);
}

// XXX: Should use strunquote parser from util.c
// XXX: Should remove string length limitation using allocation
static int qe_cfg_parse_string(QEmacsDataSource *ds, const char **pp, int delim) {
    const char *p = *pp;
    int triple = 0;
    int res = 0;
#ifndef CONFIG_TINY
    char32_t ch;
#endif
    /* check for triple delimiter */
    if (p[0] == delim && p[1] == delim) {
        triple = 1;
        p += 2;
        if (*p == '\n') {
            /* ignore newline after """ */
            // XXX: should ignore leading white space?
            p++;
        }
    }
    ds->len = 0;
    for (;;) {
        /* encoding issues deliberately ignored */
        unsigned char c = *p;
        if (c == '\0') {
            qe_cfg_error(ds, "unterminated string");
            res = -1;
            break;
        }
        if (c == '\n') {
            if (!triple) {
                qe_cfg_error(ds, "newline in simple string");
                res = -1;
                break;
            }
            ds->s->qs->ec.lineno = ++ds->line_num;
        }
        p++;
        if (c == delim) {
            if (!triple)
                break;
            if (p[0] == delim && p[1] == delim) {
                p += 2;
                break;
            }
        }
        if (c == '\\') {
#ifndef CONFIG_TINY
            int maxc = -1;
#endif
            if (*p == '\0') {
                qe_cfg_error(ds, "unterminated string");
                res = -1;
                break;
            }
            c = *p++;
            switch (c) {
            case '\n':
                // escaped newlines contribute no character to the string
                // XXX: should ignore leading white space?
                ds->s->qs->ec.lineno = ++ds->line_num;
                continue;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
#ifndef CONFIG_TINY  /* ignore other escapes, including octal and hex */
            case 'a': c = '\a'; break;
            case 'b': c = '\b'; break;
            case 'e': c = '\033'; break;
            case 'f': c = '\f'; break;
            case 'v': c = '\v'; break;
            case '0': case '1': case '2': case '3':
            case '4': case '5': case '6': case '7':
                c -= '0';
                if (qe_isoctdigit(*p)) {
                    c = (c << 3) | (*p++ - '0');
                    if (c < 040 && qe_isoctdigit(*p)) {
                        c = (c << 3) | (*p++ - '0');
                    }
                }
                break;
            case 'U':
                maxc += 4;  /* maxc will be 8 */
                fallthrough;
            case 'u':
                maxc += 5;  /* maxc will be 4 */
                fallthrough;
            case 'x':
                for (ch = 0; qe_isxdigit(*p) && maxc-- != 0; p++) {
                    ch = (ch << 4) | qe_digit_value(*p);
                }
                qe_cfg_append_str_char32(ds, ch);
                continue;
#endif
            }
        }
        qe_cfg_append_str_byte(ds, c);
    }
    if (ds->len < ds->str_size)
        ds->str[ds->len] = '\0';
    *pp = p;
    return res;
}

static int qe_cfg_next_token(QEmacsDataSource *ds) {
    const u8 *p = (const u8 *)ds->p;
    ds->newline_seen = 0;
    for (;;) {
        int len;
        u8 c;
        const struct opdef *op;

        ds->start_p = cs8(p);
        ds->start_line = ds->line_num;
        ds->prec = PREC_NONE;
        c = *p;
        if (c == '\0') {
            ds->p = cs8(p);
            return ds->tok = TOK_EOF;
        }
        p++;
        if (c == '\n') {
            /* set has_seen_newline for automatic semicolon insertion */
            ds->newline_seen = 1;
            ds->s->qs->ec.lineno = ++ds->line_num;
            continue;
        }
        if (qe_isspace(c))
            continue;
        if (c == '/') {
            if (*p == '/') {  /* line comment */
                while ((c = *p) != '\0' && c != '\n')
                    p++;
                continue;
            }
            if (*p == '*') { /* multiline comment */
                for (;;) {
                    if ((c = *++p) == '\0') {
                        // XXX: should complain about unfinished comment
                        break;
                    }
                    if (c == '*' && p[1] == '/') {
                        p += 2;
                        break;
                    }
                    if (c == '\n')
                        ds->s->qs->ec.lineno = ++ds->line_num;
                }
                continue;
            }
        }
        if (ds->str != ds->str_buf) {
            qe_free(&ds->str);
            ds->str = ds->str_buf;
            ds->str_size = sizeof(ds->str_buf);
        }
        if (qe_isalpha_(c)) {   /* parse an identifier */
            // XXX: should have a list of symbols with command and
            //      variable names with transparent dash translation
            //      and use a hashtable in qecore to register symbol
            //      bindings commands, global variables, local variables
            //      and qscript keywords.
            len = 0;
            ds->str[len++] = c;
            while (qe_isalnum_(c = *p) || (c == '-' && qe_isalpha(p[1]))) {
                if (c == '_')
                    c = '-';
                /* silently truncate overlong identifiers */
                if (len < ssizeof(ds->str_buf) - 1)
                    ds->str[len++] = c;
                p++;
            }
            ds->str[len] = '\0';
            ds->len = len;
            ds->p = cs8(p);
#ifndef CONFIG_TINY
            // XXX: should use hash table
            switch (len) {
            case 2:
                if (!memcmp(ds->start_p, "if", 2)) return ds->tok = TOK_IF;
                break;
            case 3:
                if (!memcmp(ds->start_p, "for", 3)) return ds->tok = TOK_FOR;
                if (!memcmp(ds->start_p, "fun", 3)) return ds->tok = TOK_FUNCTION;
                if (!memcmp(ds->start_p, "def", 3)) return ds->tok = TOK_FUNCTION;
                if (!memcmp(ds->start_p, "var", 3)) return ds->tok = TOK_VAR;
                break;
            case 4:
                if (!memcmp(ds->start_p, "else", 4)) return ds->tok = TOK_ELSE;
                if (!memcmp(ds->start_p, "case", 4)) return ds->tok = TOK_CASE;
                break;
            case 5:
                if (!memcmp(ds->start_p, "break", 5)) return ds->tok = TOK_BREAK;
                if (!memcmp(ds->start_p, "while", 5)) return ds->tok = TOK_WHILE;
                break;
            case 6:
                if (!memcmp(ds->start_p, "switch", 6)) return ds->tok = TOK_SWITCH;
                if (!memcmp(ds->start_p, "return", 6)) return ds->tok = TOK_RETURN;
                break;
            case 7:
                if (!memcmp(ds->start_p, "default", 7)) return ds->tok = TOK_DEFAULT;
                break;
            case 8:
                if (!memcmp(ds->start_p, "continue", 8)) return ds->tok = TOK_CONTINUE;
                if (!memcmp(ds->start_p, "function", 8)) return ds->tok = TOK_FUNCTION;
                break;
            }
#else
            if (len == 2 && !memcmp(ds->start_p, "if", 2))
                return ds->tok = TOK_IF;
            if (len == 4 && !memcmp(ds->start_p, "else", 4))
                return ds->tok = TOK_ELSE;
#endif
            return ds->tok = TOK_ID;
        }
        if (qe_isdigit(c)) {   /* parse a number */
            ds->p = cs8(p);
            // XXX: should handle floating point and _ and ' digit separators
            strtoll_c(ds->start_p, &ds->p, 0);
            if (qe_isalnum_(*ds->p)) {
                /* type suffixes not supported */
                qe_cfg_error(ds, "invalid number");
                return ds->tok = TOK_ERR;
            }
            return ds->tok = TOK_NUMBER;
        }
        if (c == '\'' || c == '\"') {
            ds->len = 0;
            ds->p = cs8(p);
            if (qe_cfg_parse_string(ds, &ds->p, c) < 0) {
                return ds->tok = TOK_ERR;
            }
            return ds->tok = (c == '\'') ? TOK_CHAR : TOK_STRING;
        }
        // XXX: use binary search?
        op = ops + countof(ops);
        while (op --> ops) {
            for (len = 0; p[len - 1] == op->str[len]; len++) {
                if (op->str[len + 1] == '\0') {
                    ds->p = cs8(p + len);
                    ds->prec = op->prec;
                    return ds->tok = op->op;
                }
            }
        }
        ds->p = cs8(p);
        qe_cfg_error(ds, "unsupported operator: %c", c);
        return ds->tok = c;
    }
}

static int has_token(QEmacsDataSource *ds, int tok) {
    if (ds->tok == tok) {
        qe_cfg_next_token(ds);
        return 1;
    } else {
        return 0;
    }
}

static int expect_token(QEmacsDataSource *ds, int tok) {
    if (has_token(ds, tok)) {
        return 1;
    } else {
        /* tok is a single byte token, no need to pretty print */
        qe_cfg_error(ds, "'%c' expected", tok);
        return 0;
    }
}

static int qe_cfg_getvalue(QEmacsDataSource *ds, QEValue *sp) {
    if (sp->type == TOK_ID) {
#ifndef CONFIG_TINY
        char buf[2048];
        int num;
        // XXX: qe_get_variable should populate a QEValue
        switch (qe_get_variable(ds->s, sp->u.str, buf, sizeof(buf), &num, 0)) {
        case VAR_CHARS:
        case VAR_STRING:
            qe_cfg_set_str(sp, buf, strlen(buf));
            break;
        case VAR_NUMBER:
            qe_cfg_set_num(sp, num);
            break;
        default:
        case VAR_UNKNOWN:
            qe_cfg_error(ds, "no variable %s", sp->u.str);
            qe_cfg_set_void(sp);
            return 1;
        }
#else
        qe_cfg_error(ds, "no variable %s", sp->u.str);
#endif
    }
    return 0;
}

static int qe_cfg_tonum(QEmacsDataSource *ds, QEValue *sp) {
    if (qe_cfg_getvalue(ds, sp))
        return 1;
    switch (sp->type) {
    case TOK_NUMBER:
        return 0;
    case TOK_STRING:
        qe_cfg_set_num(sp, strtoll(sp->u.str, NULL, 0));
        break;
    case TOK_CHAR:
        sp->type = TOK_NUMBER;
        break;
    default:
        sp->u.value = 0;
        sp->type = TOK_NUMBER;
        break;
    }
    return 0;
}

static int qe_cfg_tostr(QEmacsDataSource *ds, QEValue *sp) {
    char buf[64];
    int len;

    if (qe_cfg_getvalue(ds, sp))
        return 1;

    switch (sp->type) {
    case TOK_STRING:
        return 0;
    case TOK_NUMBER:
        len = snprintf(buf, sizeof buf, "%lld", sp->u.value);
        qe_cfg_set_str(sp, buf, len);
        break;
    case TOK_CHAR:
        len = utf8_encode(buf, (char32_t)sp->u.value);
        qe_cfg_set_str(sp, buf, len);
        break;
    default:
        qe_cfg_set_str(sp, "", 0);
        break;
    }
    return 0;
}

#ifndef CONFIG_TINY
static int qe_cfg_tochar(QEmacsDataSource *ds, QEValue *sp) {
    const char *p;

    if (qe_cfg_getvalue(ds, sp))
        return 1;

    switch (sp->type) {
    case TOK_STRING:
        p = sp->u.str;
        qe_cfg_set_num(sp, utf8_decode(&p));
        break;
    case TOK_NUMBER:
    case TOK_CHAR:
        sp->type = TOK_CHAR;
        break;
    default:
        qe_cfg_set_num(sp, 0);
        break;
    }
    return 0;
}

static int qe_cfg_append(QEmacsDataSource *ds, QEValue *sp, const char *p, size_t len) {
    char *new_p;
    int new_len;

    if (qe_cfg_tostr(ds, sp))
        return 1;

    /* XXX: should cap length? */
    new_len = sp->len + len;
    new_p = qe_malloc_array(char, new_len + 1);
    if (new_p == NULL)
        return 1;

    memcpy(new_p, sp->u.str, sp->len);
    memcpy(new_p + sp->len, p, len);
    new_p[new_len] = '\0';
    qe_cfg_set_pstr(sp, new_p, new_len, 1);
    return 0;
}

static int qe_cfg_format(QEmacsDataSource *ds, QEValue *sp) {
    char buf[2048];
    char fmt[16];
    /* Use function pointer and cast to prevent warning on variable format */
    int (*fun)(char *buf, size_t size, const char *fmt, ...) =
        (int (*)(char *buf, size_t size, const char *fmt, ...))snprintf;
    char *start, *p;
    int c, len;

    if (qe_cfg_tostr(ds, sp))
        return 1;
    len = 0;

    /* XXX: should use buf_xxx */
    for (start = p = sp->u.str;;) {
        p += strcspn(p, "%");
        len += strlen(pstrncpy(buf + len, sizeof(buf) - len, start, p - start));
        if (*p == '\0')
            break;
        start = p++;
        if (*p == '%') {
            start++;
            p++;
        } else {
            p += strspn(p, "0123456789+- #.");
            c = *p++;
            if (strchr("diouxX", c)) {
                if (qe_cfg_tonum(ds, sp + 1))
                    return 1;
                snprintf(fmt, sizeof fmt, "%.*sll%c", (int)(p - 1 - start), start, c);
                (*fun)(buf + len, sizeof(buf) - len, fmt, sp[1].u.value);
                len += strlen(buf + len);
                start = p;
            } else
            if (c == 'c') {
                if (qe_cfg_tochar(ds, sp + 1))
                    return 1;
                goto hasstr;
            } else
            if (c == 's') {
            hasstr:
                if (qe_cfg_tostr(ds, sp + 1))
                    return 1;
                snprintf(fmt, sizeof fmt, "%.*ss", (int)(p - start), start);
                (*fun)(buf + len, sizeof(buf) - len, sp[1].u.str);
                len += strlen(buf + len);
                start = p;
            }
        }
    }
    qe_cfg_set_str(sp, buf, len);
    return 0;
}

static int qe_cfg_get_args(QEmacsDataSource *ds, QEValue *sp, int n1, int n2);
static int qe_cfg_op(QEmacsDataSource *ds, QEValue *sp, int op);
#endif

static int qe_cfg_call(QEmacsDataSource *ds, QEValue *sp, const CmdDef *d);
static int qe_cfg_assign(QEmacsDataSource *ds, QEValue *sp, int op);
static int qe_cfg_skip_expr(QEmacsDataSource *ds);

static int qe_cfg_check_lvalue(QEmacsDataSource *ds, QEValue *sp) {
    if (sp->type != TOK_ID) {
        qe_cfg_error(ds, "not a variable");
        return 1;
    }
    return 0;
}

static int qe_cfg_expr(QEmacsDataSource *ds, QEValue *sp, int prec0, int skip) {
    /* Parse and evaluate an expression upto and including operators with
       precedence prec0.
     */
    /* in CONFIG_TINY, only support function calls and setting variables */
    const char *start_p = ds->start_p;
    int start_line = ds->line_num;
    int tok = ds->tok;

    if (skip)
        return qe_cfg_skip_expr(ds);

    if (sp >= ds->sp_max) {
        if (sp >= ds->stack + countof(ds->stack)) {
            qe_cfg_error(ds, "stack overflow");
            return qe_cfg_skip_expr(ds);
        }
        ds->sp_max = sp + 1;
    }
again:
    /* handle prefix operators (ignoring precedence) */
    switch (tok) {
    case '(':   /* parenthesized expression, including if expression */
        qe_cfg_next_token(ds);
        if (qe_cfg_expr(ds, sp, PREC_EXPRESSION, 0) || !expect_token(ds, ')'))
            goto fail;
        break;
    case '-':
        qe_cfg_next_token(ds);
        if (qe_cfg_expr(ds, sp, PREC_POSTFIX, 0) || qe_cfg_tonum(ds, sp))
            goto fail;
        sp->u.value = -sp->u.value;
        break;
#ifndef CONFIG_TINY
    case '+':
        qe_cfg_next_token(ds);
        if (qe_cfg_expr(ds, sp, PREC_POSTFIX, 0) || qe_cfg_tonum(ds, sp))
            goto fail;
        break;
    case '~':
        qe_cfg_next_token(ds);
        if (qe_cfg_expr(ds, sp, PREC_POSTFIX, 0) || qe_cfg_tonum(ds, sp))
            goto fail;
        sp->u.value = ~sp->u.value;
        break;
    case '!':
        qe_cfg_next_token(ds);
        if (qe_cfg_expr(ds, sp, PREC_POSTFIX, 0) || qe_cfg_getvalue(ds, sp))
            goto fail;
        qe_cfg_set_num(sp, (sp->type == TOK_STRING) ? 0 : !sp->u.value);
        break;
    case TOK_INC: /* convert to x += 1 */
    case TOK_DEC: /* convert to x -= 1 */
        qe_cfg_next_token(ds);
        if (qe_cfg_expr(ds, sp, PREC_POSTFIX, 0))
            goto fail;
        if (qe_cfg_check_lvalue(ds, sp))
            goto fail;
        qe_cfg_set_num(sp + 1, 1);
        if (qe_cfg_assign(ds, sp, tok))
            goto fail;
        if (qe_cfg_getvalue(ds, sp))
            goto fail;
        break;
    // case TOK_SIZEOF:
#endif
    case TOK_NUMBER:
        qe_cfg_set_num(sp, strtoll(ds->start_p, NULL, 0));
        qe_cfg_next_token(ds);
        break;
    case TOK_STRING:
    case TOK_ID:
        /* XXX: could either parse here or delay parse till qe_cfg_getvalue() */
        qe_cfg_set_str(sp, ds->str, ds->len);
        sp->type = tok;
        qe_cfg_next_token(ds);
        break;
    case TOK_CHAR: {
            const char *p = ds->str;
            char32_t c = utf8_decode(&p);  // XXX: should check for extra characters
            qe_cfg_set_char(sp, c);
            qe_cfg_next_token(ds);
            break;
        }
    default:
        qe_cfg_set_void(sp);
        qe_cfg_error(ds, "invalid expression");
        goto fail;
    }

    for (;;) {
        int op = ds->tok;
        int prec = ds->prec;

        if (prec < prec0)
            return 0;
        qe_cfg_next_token(ds);
        if (op == ',')
            goto again;
#ifndef CONFIG_TINY
        if (op == '?') {
            int truth;
            if (qe_cfg_getvalue(ds, sp))
                goto again;
            truth = (sp->type == TOK_STRING) || (sp->u.value != 0);
            if (qe_cfg_expr(ds, sp, PREC_EXPRESSION, !truth) != !truth)
                goto again;
            if (!has_token(ds, ':'))
                goto again;
            if (qe_cfg_expr(ds, sp, PREC_CONDITIONAL, truth) != truth)
                goto again;
            continue;
        }
#endif
        if (prec == PREC_POSTFIX) {
            switch (op) {
            case '(': /* function call */
                /* XXX: should move this code to qe_cfg_call() */
                if (sp->type == TOK_ID) {
                    const CmdDef *d = qe_find_cmd(ds->s->qs, sp->u.str);
                    if (!d) {
#ifndef CONFIG_TINY
                        if (strequal(sp->u.str, "char")) {
                            if (qe_cfg_get_args(ds, sp, 1, 1) < 0)
                                goto fail;
                            qe_cfg_tochar(ds, sp);
                            continue;
                        } else
                        if (strequal(sp->u.str, "int")) {
                            if (qe_cfg_get_args(ds, sp, 1, 1) < 0)
                                goto fail;
                            qe_cfg_tonum(ds, sp);
                            continue;
                        } else
                        if (strequal(sp->u.str, "string")) {
                            if (qe_cfg_get_args(ds, sp, 1, 1) < 0)
                                goto fail;
                            qe_cfg_tostr(ds, sp);
                            continue;
                        } else
#endif
                        {
                            qe_cfg_error(ds, "unknown command '%s'", sp->u.str);
                            goto fail;
                        }
                    }
                    if (qe_cfg_call(ds, sp, d))
                        goto fail;
                    continue;
                }
                qe_cfg_error(ds, "invalid function call");
                goto fail;
#ifndef CONFIG_TINY
            case TOK_INC: /* post increment: convert to first(x, x += 1) */
            case TOK_DEC: /* post decrement: convert to first(x, x -= 1) */
                if (qe_cfg_check_lvalue(ds, sp))
                    goto fail;
                qe_cfg_set_void(sp + 1);
                sp[1] = sp[0];
                sp->alloc = 0;
                if (qe_cfg_getvalue(ds, sp))
                    goto fail;
                qe_cfg_set_num(sp + 2, 1);
                if (qe_cfg_assign(ds, sp + 1, op))
                    goto fail;
                continue;
            case '[': /* subscripting */
                if (qe_cfg_expr(ds, sp + 1, PREC_EXPRESSION, 0) || !expect_token(ds, ']'))
                    goto fail;
                if (qe_cfg_op(ds, sp, op))
                    return 1;
                continue;
            case '.': /* property / method accessor */
                if (ds->tok != TOK_ID) {
                    qe_cfg_error(ds, "expected property name");
                    goto fail;
                }
                if (qe_cfg_getvalue(ds, sp))
                    return 1;
                if (sp->type == TOK_STRING && strequal(ds->str, "length")) {
                    // XXX: use sp->len?
                    qe_cfg_set_num(sp, strlen(sp->u.str));  // utf8?
                    qe_cfg_next_token(ds);
                    continue;
                }
                qe_cfg_error(ds, "no such property '%s'", ds->str);
                goto fail;
#endif
            default:
                qe_cfg_error(ds, "unsupported operator '%c'", op);
                goto fail;
            }
            //continue; // never reached
        }
        if (prec == PREC_ASSIGNMENT) {
            /* assignments are right associative */
            if (qe_cfg_expr(ds, sp + 1, PREC_ASSIGNMENT, 0))
                goto fail;
            if (qe_cfg_assign(ds, sp, op))
                goto fail;
            continue;
        }
#ifndef CONFIG_TINY
        // XXX: should implement shortcut evaluation for || and &&
        /* other operators are left associative */
        if (qe_cfg_expr(ds, sp + 1, prec + 1, 0))
            goto fail;
        // XXX: may need to delay for qe_cfg_op() to decide if qe_cfg_getvalue is OK
        if (qe_cfg_getvalue(ds, sp))
            goto fail;
        if (qe_cfg_op(ds, sp, op))
            goto fail;
#else
        qe_cfg_error(ds, "unsupported operator '%c'", op);
        goto fail;
#endif
    }
fail:
    ds->p = start_p;
    ds->line_num = start_line;
    qe_cfg_next_token(ds);
    return qe_cfg_skip_expr(ds);
}

#ifndef CONFIG_TINY
static int qe_cfg_op(QEmacsDataSource *ds, QEValue *sp, int op) {
    if (sp->type == TOK_STRING) {
        switch (op) {
        case '<':
        case '>':
        case TOK_LE:
        case TOK_GE:
        case TOK_EQ:
        case TOK_NE:
            if (qe_cfg_tostr(ds, sp + 1))
                return 1;
            qe_cfg_set_num(sp, strcmp(sp->u.str, sp[1].u.str));
            qe_cfg_set_num(sp + 1, 0);
            goto num;
        case '+':
        case TOK_ADD_EQ:
            if (qe_cfg_tostr(ds, sp + 1))
                return 1;
            if (qe_cfg_append(ds, sp, sp[1].u.str, sp[1].len))
                return 1;
            break;
        case '[':
            if (qe_cfg_tonum(ds, sp + 1))
                return 1;
            if (sp[1].u.value >= 0 && sp[1].u.value < sp->len) {
                qe_cfg_set_char(sp, sp->u.str[sp[1].u.value]);  // XXX: UTF-8 ?
            } else {
                qe_cfg_set_void(sp);
            }
            break;
        case '%':
            // XXX: should pass format a tuple?
            if (qe_cfg_format(ds, sp))
                return 1;
            break;
        default:
            qe_cfg_error(ds, "invalid string operator '%c'", op);
            return 1;
        }
    } else {
        if (qe_cfg_tonum(ds, sp) || qe_cfg_tonum(ds, sp + 1))
            return 1;
    num:
        switch (op) {
        case '*':
        case TOK_MUL_EQ:
            sp->u.value *= sp[1].u.value;
            break;
        case '/':
        case '%':
        case TOK_DIV_EQ:
        case TOK_MOD_EQ:
            if (sp[1].u.value == 0 || (sp->u.value == LLONG_MIN && sp[1].u.value == -1)) {
                // XXX: should pretty print op for `/=` and `%=`
                qe_cfg_error(ds, "'%c': division overflow", op);
                return 1;
            }
            if (op == '/' || op == TOK_DIV_EQ)
                sp->u.value /= sp[1].u.value;
            else
                sp->u.value %= sp[1].u.value;
            break;
        case '+':
        case TOK_ADD_EQ:
        case TOK_INC:
            sp->u.value += sp[1].u.value;
            break;
        case '-':
        case TOK_SUB_EQ:
        case TOK_DEC:
            sp->u.value -= sp[1].u.value;
            break;
        case TOK_SHL:
        case TOK_SHL_EQ:
            sp->u.value <<= sp[1].u.value;
            break;
        case TOK_SHR:
        case TOK_SHR_EQ:
            sp->u.value >>= sp[1].u.value;
            break;
        case '<':
            sp->u.value = sp->u.value < sp[1].u.value;
            break;
        case '>':
            sp->u.value = sp->u.value > sp[1].u.value;
            break;
        case TOK_LE:
            sp->u.value = sp->u.value <= sp[1].u.value;
            break;
        case TOK_GE:
            sp->u.value = sp->u.value >= sp[1].u.value;
            break;
        case TOK_EQ:
            sp->u.value = sp->u.value == sp[1].u.value;
            break;
        case TOK_NE:
            sp->u.value = sp->u.value != sp[1].u.value;
            break;
        case '&':
        case TOK_AND_EQ:
            sp->u.value &= sp[1].u.value;
            break;
        case '^':
        case TOK_XOR_EQ:
            sp->u.value ^= sp[1].u.value;
            break;
        case '|':
        case TOK_OR_EQ:
            sp->u.value |= sp[1].u.value;
            break;
        case TOK_LAND:
            // XXX: should use shortcut evaluation
            sp->u.value = sp->u.value && sp[1].u.value;
            break;
        case TOK_LOR:
            // XXX: should use shortcut evaluation
            sp->u.value = sp->u.value || sp[1].u.value;
            break;
        case '?':       // Should not get here
        case ',':       // Should not get here
            sp->u.value = sp[1].u.value;
            break;
        default:
            qe_cfg_error(ds, "invalid numeric operator '%c'", op);
            return 1;
        }
    }
    return 0;
}
#endif

static int qe_cfg_assign(QEmacsDataSource *ds, QEValue *sp, int op) {
    if (qe_cfg_check_lvalue(ds, sp))
        return 1;
    if (qe_cfg_getvalue(ds, sp + 1))
        return 1;
    if (op != '=') {
#ifndef CONFIG_TINY
        QEValue val = *sp;
        sp->alloc = 0;
        if (qe_cfg_getvalue(ds, sp) || qe_cfg_op(ds, sp, op)) {
            qe_cfg_set_void(&val);
            return 1;
        }
        qe_cfg_move(sp + 1, sp);
        *sp = val;
#else
        qe_cfg_error(ds, "unsupported operator %c", op);
        return 1;
#endif
    }
    // XXX: should pass QEValue pointer to qe_set_variable()
#ifndef CONFIG_TINY
    if (sp[1].type == TOK_STRING) {
        qe_set_variable(ds->s, sp->u.str, sp[1].u.str, 0);
    } else {
        qe_set_variable(ds->s, sp->u.str, NULL, sp[1].u.value);
    }
    // XXX: should detect and report read-only variables and invalid assignments
#else
    qe_cfg_tonum(ds, sp + 1);
    if (strequal(sp->u.str, "tab-width")) {
        ds->s->b->tab_width = sp[1].u.value;
    } else
    if (strequal(sp->u.str, "default-tab-width")) {
        ds->s->qs->default_tab_width = sp[1].u.value;
    } else
    if (strequal(sp->u.str, "indent-tabs-mode")) {
        ds->s->indent_tabs_mode = sp[1].u.value;
    } else
    if (strequal(sp->u.str, "indent-width")) {
        ds->s->indent_width = sp[1].u.value;
    } else {
        /* ignore other variables without a warning */
        qe_cfg_error(ds, "unsupported variable %s", sp->u.str);
        return 1;
    }
    qe_cfg_swap(sp, sp + 1);    /* do not reload value */
#endif
    return 0;
}

static int qe_cfg_skip_expr(QEmacsDataSource *ds) {
    /* skip an expression: consume all tokens until prec <= 0.
       parentheses are skipped in pairs but not balanced.
     */
    int level = 0;

    // XXX: should match bracket types
    for (;;) {
        switch (ds->tok) {
        case TOK_EOF:
            // XXX: should potentially complain about missing )]} */
            return 1;
        case '?':
        case '{':
        case '[':
        case '(':
            level++;
            break;
        case ':':
        case '}':
        case ']':
        case ')':
            if (!level)
                return 1;
            level--;
            break;
        case ';':
            if (!level)
                return 1;
            break;
        }
        qe_cfg_next_token(ds);
    }
}

#ifndef CONFIG_TINY
static int qe_cfg_get_args(QEmacsDataSource *ds, QEValue *sp, int n1, int n2) {
    int nargs = 0;
    int sep = 0;
    for (nargs = 0; !has_token(ds, ')'); nargs++) {
        if (sep && !expect_token(ds, sep))
            return -1;
        sep = ',';
        if (qe_cfg_expr(ds, sp + nargs, PREC_ASSIGNMENT, 0)) {
            qe_cfg_error(ds, "invalid argument");     // need function name
            return -1;
        }
    }
    if (nargs < n1) {
        qe_cfg_error(ds, "missing arguments");     // need function name
        return -1;
    }
    if (nargs > n2) {
        qe_cfg_error(ds, "extra arguments");     // need function name
        return -1;
    }
    return nargs;
}
#endif

static void qe_cfg_free_args(QEmacsDataSource *ds, int nb_args,
                             CmdArg *args, unsigned char *args_type)
{
    int i;
    for (i = 0; i < nb_args; i++) {
        if (args_type[i] == CMD_ARG_STRING)
            qe_free(&args[i].p);
    }
}

static int qe_cfg_call(QEmacsDataSource *ds, QEValue *sp, const CmdDef *d) {
    EditState *s = ds->s;
    QEmacsState *qs = s->qs;
    const char *r;
    int nb_args, sep, i, ret;
    CmdArgSpec cas;
    CmdArg args[MAX_CMD_ARGS];
    unsigned char args_type[MAX_CMD_ARGS];

    nb_args = 0;

    /* construct argument type list */
    r = d->spec;
    if (*r == '*') {
        r++;
        if (check_read_only(s))
            return -1;
    }

    /* This argument is always the window */
    args_type[nb_args++] = CMD_ARG_WINDOW;

    while ((ret = parse_arg(&r, &cas)) != 0) {
        if (ret < 0 || nb_args >= MAX_CMD_ARGS) {
            qe_cfg_error(ds, "invalid command definition '%s'", d->name);
            return -1;
        }
        args[nb_args].p = NULL;
        args_type[nb_args++] = cas.arg_type;
    }

    sep = '\0';

    for (i = 0; i < nb_args; i++) {
        /* pseudo arguments: skip them */
        switch (args_type[i]) {
        case CMD_ARG_WINDOW:
            args[i].s = s;
            continue;
        case CMD_ARG_INTVAL:
            args[i].n = d->val;
            continue;
        case CMD_ARG_STRINGVAL:
            /* kludge for xxx-mode functions and named kbd macros,
               must be the last argument */
            args[i].p = cas.prompt;
            continue;
        }
        if (ds->tok == ')') {
            /* no more arguments: handle default values */
            switch (args_type[i]) {
            case CMD_ARG_INT | CMD_ARG_RAW_ARGVAL:
                args[i].n = NO_ARG;
                continue;
            case CMD_ARG_INT | CMD_ARG_NUM_ARGVAL:
                args[i].n = 1;
                continue;
            case CMD_ARG_INT | CMD_ARG_NEG_ARGVAL:
                args[i].n = -1;
                continue;
            case CMD_ARG_INT | CMD_ARG_USE_MARK:
                args[i].n = s->b->mark;
                continue;
            case CMD_ARG_INT | CMD_ARG_USE_POINT:
                args[i].n = s->offset;
                continue;
            case CMD_ARG_INT | CMD_ARG_USE_ZERO:
                args[i].n = 0;
                continue;
            case CMD_ARG_INT | CMD_ARG_USE_BSIZE:
                args[i].n = s->b->total_size;
                continue;
            }
            /* CG: Could supply default arguments. */
            /* source stays in front of the ')'. */
            /* Let the expression parser complain about the missing argument */
        } else {
            if (sep && !expect_token(ds, sep)) {
                qe_cfg_free_args(ds, i, args, args_type);
                return -1;
            }
            sep = ',';
        }

        /* XXX: should parse and evaluate all arguments and
           then match actual command arguments */

        if (qe_cfg_expr(ds, sp, PREC_ASSIGNMENT, 0)) {
            qe_cfg_error(ds, "missing arguments for %s", d->name);
            qe_cfg_free_args(ds, i, args, args_type);
            return -1;
        }

        switch (args_type[i] & CMD_ARG_TYPE_MASK) {
        case CMD_ARG_INT:
            qe_cfg_tonum(ds, sp); // XXX: should complain about type mismatch?
            args[i].n = sp->u.value;
            if (args_type[i] == (CMD_ARG_INT | CMD_ARG_NEG_ARGVAL))
                args[i].n *= -1;
            break;
        case CMD_ARG_STRING:
            qe_cfg_tostr(ds, sp); // XXX: should complain about type mismatch?
            /* use allocated string pointer or duplicate it */
            if (sp->alloc) {
                args[i].p = sp->u.str;
                sp->alloc = 0;
                sp->type = TOK_VOID;
            } else {
                args[i].p = qe_strdup(sp->u.str);
            }
            break;
        }
    }
    if (!has_token(ds, ')')) {
        qe_cfg_error(ds, "too many arguments for %s", d->name);
        qe_cfg_free_args(ds, nb_args, args, args_type);
        return -1;
    }

    qs->this_cmd_func = d->action.func;
    qs->ec.function = d->name;
    call_func(d->sig, d->action, nb_args, args, args_type);
    qs->ec.function = NULL;
    qs->last_cmd_func = qs->this_cmd_func;
    if (qs->active_window)
        s = qs->active_window;
    qe_check_window(qs, &s);
    ds->s = s;
    sp->type = TOK_VOID;
    qe_cfg_free_args(ds, nb_args, args, args_type);
    return 0;
}

static int qe_cfg_stmt(QEmacsDataSource *ds, QEValue *sp, int skip) {
    int res = 0;

    if (has_token(ds, '{')) {
        /* handle blocks */
        while (!has_token(ds, '}')) {
            if (ds->tok == TOK_EOF) {
                qe_cfg_error(ds, "missing '}'");
                return 1;
            }
            res |= qe_cfg_stmt(ds, sp, skip);
        }
        return res;
    }

    // XXX: should also parse do / while?
    if (has_token(ds, TOK_IF)) {
        int truth = 0;

        if (qe_cfg_expr(ds, sp, PREC_EXPRESSION, skip) || qe_cfg_getvalue(ds, sp)) {
            res = skip = 1;
        } else {
            truth = (sp->type == TOK_STRING) || (sp->u.value != 0);
        }
        res |= qe_cfg_stmt(ds, sp, skip | !truth);
        if (has_token(ds, TOK_ELSE))
            res |= qe_cfg_stmt(ds, sp, skip | truth);
        return res;
    }
    if (ds->tok != ';') {   /* test for empty statement */
        /*  accept comma expressions */
        if (qe_cfg_expr(ds, sp, PREC_EXPRESSION, skip) || qe_cfg_getvalue(ds, sp))
            res = 1;
    }
    /* consume `;` if any or is current token first on line */
    if (!has_token(ds, ';') && ds->tok != TOK_EOF && ds->tok != '}' && !ds->newline_seen) {
        //if (!res)
            qe_cfg_error(ds, "missing ';'");
    }
    return res;
}

static int qe_parse_script(EditState *s, QEmacsDataSource *ds) {
    QEmacsState *qs = s->qs;
    QErrorContext ec_save = qs->ec;
    QEValue *sp = &ds->stack[0];

    ds->s = s;
    ds->p = ds->buf;
    sp->type = TOK_VOID;

    qs->ec.filename = ds->filename;
    qs->ec.function = NULL;
    qs->ec.lineno = ds->line_num = 1;

    qe_cfg_next_token(ds);
    while (ds->tok != TOK_EOF && ds->tok != TOK_ERR) {
        if (qe_cfg_stmt(ds, sp, 0))
            sp->type = TOK_VOID;
    }
    if (ds->allocated_buf) {
        qe_free(&ds->allocated_buf);
        ds->p = ds->buf = NULL;
    }
    qs->ec = ec_save;
    return sp->type;
}

static void qe_cfg_postprocess(EditState *s, QEmacsDataSource *ds, int argval) {
#ifndef CONFIG_TINY
    QEValue *sp = &ds->stack[0];
    char buf[64];
    int len;

    if (argval == NO_ARG)
        argval = 0;

    if (argval > 0 && check_read_only(s))
        return;

    if (!qe_cfg_getvalue(ds, sp)) {
        switch (sp->type) {
        case TOK_VOID:
            break;
        case TOK_NUMBER:
            if (argval <= 0) {
                len = snprintf(buf, sizeof buf, "-> %lld  0x%llx", sp->u.value, sp->u.value);
                if (sp->u.value >= 32 && sp->u.value < 128)
                    len += snprintf(buf + len, sizeof(buf) - len, "  '%c'", (int)sp->u.value);
                put_status(s, "%s", buf);
            } else {
                if (argval == 16)
                    len = snprintf(buf, sizeof buf, "0x%llx", sp->u.value);
                else
                    len = snprintf(buf, sizeof buf, "%lld", sp->u.value);
                s->offset += eb_insert_utf8_buf(s->b, s->offset, buf, len);
            }
            break;
        case TOK_STRING:
            if (argval <= 0) {
                /* XXX: should optionally unparse string */
                put_status(s, "-> \"%s\"", sp->u.str);
            } else {
                s->offset += eb_insert_utf8_buf(s->b, s->offset, sp->u.str, sp->len);
            }
            break;
        case TOK_CHAR:
            len = utf8_encode(buf, (char32_t)sp->u.value);
            if (argval <= 0) {
                /* XXX: should optionally unparse character */
                put_status(s, "-> '%.*s'", len, buf);
            } else {
                s->offset += eb_insert_utf8_buf(s->b, s->offset, buf, len);
            }
            break;
        default:
            qe_cfg_error(ds, "unexpected value type: %d", sp->type);
            break;
        }
    }
#endif
}

void do_eval_expression(EditState *s, const char *expression, int argval)
{
    QEmacsDataSource ds;

    qe_cfg_init(&ds);
    ds.buf = expression;
    ds.filename = "<string>";
    if (qe_parse_script(s, &ds) == TOK_ERR) {
        // error already reported
    } else {
        qe_cfg_postprocess(s, &ds, argval);
    }
    qe_cfg_release(&ds);
}

#define MAX_SCRIPT_LENGTH  (128 * 1024 - 1)

static int do_eval_buffer_region(EditState *s, int start, int stop, int argval) {
    QEmacsDataSource ds;
    char *buf;
    int length, res = 0;

    qe_cfg_init(&ds);

    if (stop < start) {
        int tmp = start;
        start = stop;
        stop = tmp;
    }
    /* extract region as UTF-8 with a size limit */
    length = eb_get_region_content_size(s->b, start, stop);
    if (length > MAX_SCRIPT_LENGTH || !(buf = qe_malloc_array(char, length + 1))) {
        put_error(s, "Buffer region too large");
        return -1;
    }
    length = eb_get_region_contents(s->b, start, stop, buf, length + 1, 0);
    ds.buf = ds.allocated_buf = buf;
    ds.filename = s->b->name;
    if (qe_parse_script(s, &ds) == TOK_ERR) {
        // error already reported
        res = 1;
    } else {
        if (argval != NO_ARG && !check_read_only(s)) {
            // replace region with script result
            // assuming script did not move point, nor modify buffer
            eb_delete_range(s->b, start, stop);
        }
        qe_cfg_postprocess(s, &ds, argval);
    }
    qe_cfg_release(&ds);
    //do_refresh(s);
    return res;
}

void do_eval_region(EditState *s, int argval) {
    s->region_style = 0;  /* deactivate region hilite */

    do_eval_buffer_region(s, s->b->mark, s->offset, argval);
}

void do_eval_buffer(EditState *s, int argval) {
    do_eval_buffer_region(s, 0, s->b->total_size, argval);
}

int parse_config_file(EditState *s, const char *filename) {
    QEmacsDataSource ds;
    int res;

    qe_cfg_init(&ds);

    ds.allocated_buf = file_load(filename, MAX_SCRIPT_LENGTH + 1, NULL);
    if (!ds.allocated_buf) {
        if (errno == ERANGE || errno == ENOMEM) {
            put_error(s, "File too large");
        }
        return -1;
    }

    ds.filename = filename;
    ds.buf = ds.allocated_buf;
    res = qe_parse_script(s, &ds);
    qe_cfg_release(&ds);
    return res;
}

#ifndef CONFIG_TINY
static void symbol_complete(CompleteState *cp, CompleteFunc enumerate) {
    command_complete(cp, enumerate);
    variable_complete(cp, enumerate);
}

static int symbol_print_entry(CompleteState *cp, EditState *s, const char *name) {
    const CmdDef *d = qe_find_cmd(s->qs, name);
    if (d) {
        return command_print_entry(cp, s, name);
    } else {
        return variable_print_entry(cp, s, name);
    }
}

static CompletionDef symbol_completion = {
    .name = "symbol",
    .enumerate = symbol_complete,
    .print_entry = symbol_print_entry,
    .get_entry = command_get_entry,
    .flags = CF_SPACE_OK | CF_NO_AUTO_SUBMIT,
};
#endif

static const CmdDef parser_commands[] = {
    CMD2( "eval-expression", "M-:",
          "Evaluate a qemacs expression",
          do_eval_expression, ESsi,
          "s{Eval: }[.symbol]|expression|"
          "P")
    /* XXX: should take region as argument, implicit from keyboard */
    CMD2( "eval-region", "M-C-z",
          "Evaluate qemacs expressions in a region",
          do_eval_region, ESi, "P")
    CMD2( "eval-buffer", "",
          "Evaluate qemacs expressions in the buffer",
          do_eval_buffer, ESi, "P")
};

static int parser_init(QEmacsState *qs) {
    qe_register_commands(qs, NULL, parser_commands, countof(parser_commands));
#ifndef CONFIG_TINY
    qe_register_completion(qs, &symbol_completion);
#endif
    return 0;
}

qe_module_init(parser_init);
