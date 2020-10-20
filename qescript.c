/*
 * QEmacs, tiny but powerful multimode editor
 *
 * Copyright (c) 2000-2002 Fabrice Bellard.
 * Copyright (c) 2000-2020 Charlie Gordon.
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
#include "variables.h"

/* qscript: config file parsing */

/* CG: error messages should go to the *error* buffer.
 * displayed as a popup upon start.
 */

typedef struct QEValue {
    int type;               // value type
    int len;                // string length
    union {
        long long value;    // number value
        char *str;          // string value
    } u;
} QEValue;

typedef struct QEmacsDataSource {
    EditState *s;
    char *allocated_buf;
    const char *buf;
    const char *p;
    const char *start_p;
    const char *filename;   // source filename
    int line_num;           // source line number
    int tok;                // token type
    int len;                // length of TOK_STRING and TOK_ID string
    QEValue *sp_max;
    char str[256];          // token source (XXX: should use source code)
    QEValue stack[16];
} QEmacsDataSource;

enum {
    TOK_EOF = -1, TOK_ERR = -2, TOK_VOID = 0, TOK_ALLOC = 1,
    TOK_NUMBER = 128, TOK_STRING, TOK_CHAR, TOK_ID, TOK_IF, TOK_ELSE,
    TOK_MUL_EQ, TOK_DIV_EQ, TOK_MOD_EQ, TOK_ADD_EQ, TOK_SUB_EQ, TOK_SHL_EQ, TOK_SHR_EQ,
    TOK_AND_EQ, TOK_XOR_EQ, TOK_OR_EQ,
    TOK_EQ, TOK_NE, TOK_SHL, TOK_SHR, TOK_LE, TOK_GE, TOK_INC, TOK_DEC, TOK_LOR, TOK_LAND,
};

enum {
    PREC_SUFFIX = 14, PREC_COMMA = 1, PREC_ASSIGN = 2, PREC_QUESTION = 3,
};

static const char ops2[] = "*= /= %= += -= <<= >>= &= ^= |= == != << >> <= >= ++ -- || && ";
static const char ops1[] = "=<>?:|^&+-*/%,;.!~()[]{}";

static const char prec[] = {
    '(', '[', '.', TOK_INC, TOK_DEC, 14,
    '*', '/', '%', 13,
    '+', '-', 12,
    TOK_SHL, TOK_SHR, 11,
    '<', '>', TOK_LE, TOK_GE, 10,
    TOK_EQ, TOK_NE, 9,
    '&', 8,
    '^', 7,
    '|', 6,
    TOK_LAND, 5,
    TOK_LOR, 4,
    '?', ':', 3,
    TOK_MUL_EQ, TOK_DIV_EQ, TOK_MOD_EQ, TOK_ADD_EQ, TOK_SUB_EQ, TOK_SHL_EQ, TOK_SHR_EQ,
    TOK_AND_EQ, TOK_OR_EQ, TOK_XOR_EQ, '=', 2,
    ',', 1,
    ')', ']', '{', '}', ';', '!', '~', 0,
};

static inline void qe_cfg_set_void(QEValue *sp) {
    if (sp->type & TOK_ALLOC)
        qe_free(&sp->u.str);
    sp->type = TOK_VOID;
}

static inline void qe_cfg_set_num(QEValue *sp, long long value) {
    if (sp->type & TOK_ALLOC)
        qe_free(&sp->u.str);
    sp->u.value = value;
    sp->type = TOK_NUMBER;
}

static inline void qe_cfg_set_char(QEValue *sp, int c) {
    if (sp->type & TOK_ALLOC)
        qe_free(&sp->u.str);
    sp->u.value = c;
    sp->type = TOK_CHAR;
}

static inline void qe_cfg_set_str(QEValue *sp, const char *str, int len) {
    if (sp->type & TOK_ALLOC)
        qe_free(&sp->u.str);
    sp->u.str = qe_malloc_array(char, len + 1);
    memcpy(sp->u.str, str, len);
    sp->u.str[len] = '\0';
    sp->len = len;
    sp->type = TOK_STRING;
}

static void qe_cfg_init(QEmacsDataSource *ds) {
    memset(ds, 0, sizeof(*ds));
    ds->sp_max = ds->stack;
}

static void qe_cfg_release(QEmacsDataSource *ds) {
    QEValue *sp;
    for (sp = ds->stack; sp < ds->sp_max; sp++)
        qe_cfg_set_void(sp);
}

static int qe_cfg_get_prec(int tok) {
    char *p = strchr(prec, tok);
    if (p) {
        while (*++p >= ' ')
            continue;
        return *p;
    }
    return 0;
}

static int qe_cfg_parse_string(EditState *s, const char **pp, int delim,
                               char *dest, int size, int *plen)
{
    char cbuf[8];
    const char *p = *pp;
    int res = 0;
    int pos = 0;
    int i, len;

    for (;;) {
        int c = *p;
        if (c == '\n' || c == '\0') {
            put_status(s, "unterminated string");
            res = -1;
            break;
        }
        p++;
        if (c == delim)
            break;
        if (c == '\\') {
            int maxc = -1;
            c = *p++;
            switch (c) {
            case 'a': c = '\a'; break;
            case 'b': c = '\b'; break;
            case 'e': c = '\e'; break;
            case 'f': c = '\f'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
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
                maxc += 4;
            case 'u':
                maxc += 5;
            case 'x':
                for (c = 0; qe_isxdigit(*p) && maxc-- != 0; p++) {
                    c = (c << 4) | qe_digit_value(*p);
                }
                len = utf8_encode(cbuf, c);
                for (i = 0; i < len && pos < size; i++)
                    dest[pos++] = cbuf[i];
                continue;
            }
        }
        /* XXX: silently truncate overlong string constants */
        if (pos < size - 1)
            dest[pos++] = c;
    }
    if (pos < size)
        dest[pos] = '\0';
    *pp = p;
    *plen = pos;
    return res;
}

static int qe_cfg_next_token(QEmacsDataSource *ds)
{
    for (;;) {
        int tok, len;
        char c;
        const char *op;

        ds->start_p = ds->p;
        c = *ds->p;
        if (c == '\0')
            return ds->tok = TOK_EOF;
        ds->p++;
        if (c == '\n') {
            ds->s->qe_state->ec.lineno = ++ds->line_num;
            continue;
        }
        if (qe_isspace(c))
            continue;
        if (c == '/') {
            if (*ds->p == '/') {  /* line comment */
                while ((c = *ds->p) != '\0' && c != '\n') {
                    ds->p++;
                }
                continue;
            }
            if (*ds->p == '*') { /* multiline comment */
                ds->p++;
                while ((c = *ds->p) != '\0') {
                    if (c == '*' && ds->p[1] == '/') {
                        ds->p += 2;
                        break;
                    }
                    if (c == '\n')
                        ds->s->qe_state->ec.lineno = ++ds->line_num;
                    ds->p++;
                }
                continue;
            }
            return ds->tok = '/';
        }
        if (qe_isalpha_(c)) {
            len = 0;
            ds->str[len++] = c;
            while (qe_isalnum_(c = *ds->p) || (c == '-' && qe_isalpha(ds->p[1]))) {
                if (c == '_')
                    c = '-';
                if (len < (int)sizeof(ds->str) - 1)
                    ds->str[len++] = c;
                ds->p++;
            }
            ds->str[len] = '\0';
            ds->len = len;
            if (!strcmp(ds->str, "if"))
                return ds->tok = TOK_IF;
            if (!strcmp(ds->str, "else"))
                return ds->tok = TOK_ELSE;
            return ds->tok = TOK_ID;
        }
        if (qe_isdigit(c)) {
            strtoll(ds->start_p, (char **)&ds->p, 0);
            if (qe_isalnum_(*ds->p)) {
                put_status(ds->s, "invalid number");
                return ds->tok = TOK_ERR;
            }
            return ds->tok = TOK_NUMBER;
        }
        if (c == '\'' || c == '\"') {
            if (qe_cfg_parse_string(ds->s, &ds->p, c, ds->str, sizeof(ds->str), &ds->len))
                return ds->tok = TOK_ERR;
            if (c == '\'') {
                return ds->tok = TOK_CHAR;
            }
            return ds->tok = TOK_STRING;
        }
        for (tok = TOK_MUL_EQ, op = ops2; *op; tok++) {
            if (*op++ == c) {
                const char *p1 = ds->p;
                for (;;) {
                    if (*op == ' ') {
                        ds->p = p1;
                        return ds->tok = tok;
                    }
                    if (*p1++ != *op++)
                        break;
                }
            }
            while (*op++ != ' ')
                continue;
        }
        if (strchr(ops1, c)) {
            return ds->tok = c;
        }
        put_status(ds->s, "unsupported operator: %c", c);
        return ds->tok = TOK_ERR;
    }
}

static int expect_token(QEmacsDataSource *ds, int tok)
{
    if (ds->tok == tok) {
        qe_cfg_next_token(ds);
        return 1;
    } else {
        /* XXX: pretty print token name */
        put_status(ds->s, "'%c' expected", tok);
        return 0;
    }
}

static int qe_cfg_getvalue(QEmacsDataSource *ds, QEValue *sp) {
#ifndef CONFIG_TINY
    if (sp->type == TOK_ID) {
        char buf[256];
        int num;
        switch (qe_get_variable(ds->s, ds->str, buf, sizeof(buf), &num, 0)) {
        case VAR_CHARS:
        case VAR_STRING:
            qe_cfg_set_str(sp, buf, strlen(buf));
            break;
        case VAR_NUMBER:
            qe_cfg_set_num(sp, num);
            break;
        default:
        case VAR_UNKNOWN:
            put_status(ds->s, "no variable %s", ds->str);
            qe_cfg_set_void(sp);
            return 1;
        }
    }
#endif
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
        len = utf8_encode(buf, (int)sp->u.value);
        qe_cfg_set_str(sp, buf, len);
        break;
    default:
        qe_cfg_set_str(sp, "", 0);
        break;
    }
    return 0;
}

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
    new_len = sp->len + len;
    new_p = qe_malloc_array(char, new_len + 1);
    memcpy(new_p, sp->u.str, sp->len);
    memcpy(new_p + sp->len, p, len);
    new_p[new_len] = '\0';
    qe_cfg_set_str(sp, new_p, new_len);
    return 0;
}

static int qe_cfg_format(QEmacsDataSource *ds, QEValue *sp) {
    char buf[256];
    char fmt[16];
    /* prevent warning on variable format */
    int (*fun)(char *buf, size_t size, const char *fmt, ...) = snprintf;
    char *start, *p;
    int c, len;

    if (qe_cfg_tostr(ds, sp))
        return 1;
    len = 0;

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

static int qe_cfg_call(QEmacsDataSource *ds, QEValue *sp, CmdDef *d);
static int qe_cfg_assign(QEmacsDataSource *ds, QEValue *sp, int op);
static int qe_cfg_op(QEmacsDataSource *ds, QEValue *sp, int op);

static int qe_cfg_expr(QEmacsDataSource *ds, QEValue *sp, int prec0) {
    int tok = ds->tok;

    if (sp >= ds->sp_max) {
        if (sp >= ds->stack + countof(ds->stack)) {
            put_status(ds->s, "stack overflow");
            return 1;
        }
        ds->sp_max = sp + 1;
    }
    /* handle prefix operators */
    switch (tok) {
    case '(':
        qe_cfg_next_token(ds);
        if (qe_cfg_expr(ds, sp, PREC_COMMA) || !expect_token(ds, ')'))
            return 1;
        break;
    case '+':
        qe_cfg_next_token(ds);
        if (qe_cfg_expr(ds, sp, PREC_SUFFIX) || qe_cfg_tonum(ds, sp))
            return 1;
        break;
    case '-':
        qe_cfg_next_token(ds);
        if (qe_cfg_expr(ds, sp, PREC_SUFFIX) || qe_cfg_tonum(ds, sp))
            return 1;
        sp->u.value = -sp->u.value;
        break;
    case '~':
        qe_cfg_next_token(ds);
        if (qe_cfg_expr(ds, sp, PREC_SUFFIX) || qe_cfg_tonum(ds, sp))
            return 1;
        sp->u.value = ~sp->u.value;
        break;
    case '!':
        qe_cfg_next_token(ds);
        if (qe_cfg_expr(ds, sp, PREC_SUFFIX) || qe_cfg_getvalue(ds, sp))
            return 1;
        qe_cfg_set_num(sp, (sp->type == TOK_STRING) ? 0 : !sp->u.value);
        break;
    case TOK_INC:
    case TOK_DEC:
        qe_cfg_next_token(ds);
        if (qe_cfg_expr(ds, sp, PREC_SUFFIX))
            return 1;
        if (sp->type != TOK_ID) {
            put_status(ds->s, "invalid increment");
            return 1;
        }
        qe_cfg_set_num(sp + 1, 1);
        if (qe_cfg_assign(ds, sp, tok))
            return 1;
        break;
    // case TOK_SIZEOF:
    case TOK_NUMBER:
        qe_cfg_set_num(sp, strtoll(ds->start_p, NULL, 0));
        qe_cfg_next_token(ds);
        break;
    case TOK_STRING:
#ifndef CONFIG_TINY
    case TOK_ID:
#endif
        qe_cfg_set_str(sp, ds->str, ds->len);   /* XXX: should parse string */
        sp->type = tok;
        qe_cfg_next_token(ds);
        break;
    case TOK_CHAR: {
            const char *p = ds->str;
            int c = utf8_decode(&p);  // XXX: should check for extra characters
            qe_cfg_set_char(sp, c);
            qe_cfg_next_token(ds);
            break;
        }
    default:
        qe_cfg_set_void(sp);
        put_status(ds->s, "invalid expression");
        return 1;
    }

    for (;;) {
        int op = ds->tok;
        int prec = qe_cfg_get_prec(op);

        if (prec < prec0)
            return 0;
        qe_cfg_next_token(ds);
        if (prec == PREC_SUFFIX) {
            switch (op) {
            case '(': /* function call */
                if (sp->type == TOK_ID) {
                    CmdDef *d = qe_find_cmd(sp->u.str);
                    if (!d) {
                        put_status(ds->s, "unknown command '%s'", sp->u.str);
                        return 1;
                    }
                    if (qe_cfg_call(ds, sp, d))
                        return 1;
                    continue;
                }
                put_status(ds->s, "invalid function call");
                return 1;
            case TOK_INC: /* post increment */
            case TOK_DEC: /* post decrement */
                qe_cfg_set_num(sp, 1);
                if (qe_cfg_assign(ds, sp, op))
                    return 1;
                sp->u.value -= (op == TOK_INC) ? 1 : -1;
                continue;
            case '[': /* subscripting */
                if (qe_cfg_expr(ds, sp + 1, PREC_COMMA) || !expect_token(ds, ']'))
                    return 1;
                if (qe_cfg_op(ds, sp, op))
                    return 1;
                continue;
            case '.': /* property / method accessor */
                if (ds->tok != TOK_ID) {
                    put_status(ds->s, "expected property name");
                    return 1;
                }
                if (qe_cfg_getvalue(ds, sp))
                    return 1;
                if (sp->type == TOK_STRING && !strcmp(ds->str, "length")) {
                    qe_cfg_set_num(sp, strlen(sp->u.str));  // utf8?
                    qe_cfg_next_token(ds);
                    continue;
                }
                put_status(ds->s, "no such property '%s'", ds->str);
                return 1;
            default:
                put_status(ds->s, "unsupported operator '%c'", op);
                return 1;
            }
        }
        if (prec == PREC_ASSIGN) {
            if (qe_cfg_expr(ds, sp + 1, prec))
                return 1;
            return qe_cfg_assign(ds, sp, op);
        }
        if (qe_cfg_expr(ds, sp + 1, prec + 1))
            return 1;
        if (qe_cfg_getvalue(ds, sp))
            return 1;
        if (qe_cfg_op(ds, sp, op))
            return 1;
    }
}

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
                qe_cfg_set_char(sp, sp->u.str[sp[1].u.value]);  // XXX: utf-8 ?
            } else {
                qe_cfg_set_void(sp);
            }
            break;
        case '%':
            if (qe_cfg_format(ds, sp))
                return 1;
            break;
        default:
            put_status(ds->s, "invalid string operator '%c'", op);
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
                put_status(ds->s, "'%c': division overflow", op);
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
            sp->u.value = sp->u.value && sp[1].u.value;
            break;
        case TOK_LOR:
            sp->u.value = sp->u.value || sp[1].u.value;
            break;
            //case '?':
        case ',':
            sp->u.value = sp[1].u.value;
            break;
        default:
            put_status(ds->s, "invalid numeric operator '%c'", op);
            return 1;
        }
    }
    return 0;
}

static int qe_cfg_assign(QEmacsDataSource *ds, QEValue *sp, int op) {
    if (sp->type != TOK_ID) {
        put_status(ds->s, "invalid assignment");
        return 1;
    }
#ifndef CONFIG_TINY
    {
        char cmd[128];
        pstrcpy(cmd, sizeof cmd, sp->u.str);
        if (qe_cfg_getvalue(ds, sp + 1))
            return 1;
        if (op == '=') {
            qe_cfg_set_void(sp);
            sp[0] = sp[1];
            qe_cfg_set_void(sp + 1);
        } else {
            if (qe_cfg_getvalue(ds, sp))
                return 1;
            if (qe_cfg_op(ds, sp, op))
                return 1;
        }
        if (sp->type == TOK_STRING) {
            qe_set_variable(ds->s, cmd, sp->u.str, 0);
        } else {
            qe_set_variable(ds->s, cmd, NULL, sp->u.value);
        }
    }
#endif
    return 0;
}

static int qe_cfg_skip(QEmacsDataSource *ds) {
    int level = 0;

    for (;;) {
        switch (ds->tok) {
        case TOK_EOF:
            return ds->tok;
        case ';':
            if (!level)
                goto done;
            break;
        case '{':
        case '[':
        case '(':
            level++;
            break;
        case '}':
        case ']':
        case ')':
            if (!--level)
                goto done;
            break;
        }
        qe_cfg_next_token(ds);
    }
done:
    return qe_cfg_next_token(ds);
}

static int qe_cfg_call(QEmacsDataSource *ds, QEValue *sp, CmdDef *d) {
    char str[1024], prompt[64];
    char *strp;
    const char *r;
    int nb_args, sep, i;
    CmdArg args[MAX_CMD_ARGS];
    unsigned char args_type[MAX_CMD_ARGS];
    EditState *s = ds->s;
    QEmacsState *qs = s->qe_state;

    nb_args = 0;

    /* construct argument type list */
    r = d->name + strlen(d->name) + 1;
    if (*r == '*') {
        r++;
        if (s->b->flags & BF_READONLY) {
            put_status(s, "Buffer is read only");
            return -1;
        }
    }

    /* first argument is always the window */
    args_type[nb_args++] = CMD_ARG_WINDOW;

    for (;;) {
        unsigned char arg_type;
        int ret;

        ret = parse_arg(&r, &arg_type, prompt, countof(prompt),
                        NULL, 0, NULL, 0);
        if (ret < 0)
            goto badcmd;
        if (ret == 0)
            break;
        if (nb_args >= MAX_CMD_ARGS) {
        badcmd:
            put_status(s, "Badly defined command '%s'", d->name);
            return -1;
        }
        args[nb_args].p = NULL;
        args_type[nb_args++] = arg_type & (CMD_ARG_TYPE_MASK | CMD_ARG_USE_ARGVAL);
    }

    sep = '\0';
    strp = str;

    for (i = 0; i < nb_args; i++) {
        /* pseudo arguments: skip them */
        switch (args_type[i]) {
        case CMD_ARG_WINDOW:
            args[i].s = s;
            continue;
        case CMD_ARG_INTVAL:
            args[i].n = (int)(intptr_t)d->val;
            continue;
        case CMD_ARG_STRINGVAL:
            /* CG: kludge for xxx-mode functions and named kbd macros */
            args[i].p = prompt;
            continue;
        case CMD_ARG_INT | CMD_ARG_USE_ARGVAL:  /* XXX: ui should always be last */
            if (ds->tok == ')') {
                args[i].n = NO_ARG;
                continue;
            }
            args_type[i] &= ~CMD_ARG_USE_ARGVAL;
            break;
        }

        if (sep) {
            /* CG: Should test for arg list too short. */
            /* CG: Could supply default arguments. */
            if (ds->tok != sep) {
                put_status(s, "missing arguments for %s", d->name);
                return -1;
            }
            qe_cfg_next_token(ds);
        }
        sep = ',';

        if (qe_cfg_expr(ds, sp, PREC_ASSIGN)) {
            put_status(s, "missing arguments for %s", d->name);
            return -1;
        }

        switch (args_type[i]) {
        case CMD_ARG_INT:
            qe_cfg_tonum(ds, sp); // XXX: should complain about type mismatch?
            args[i].n = sp->u.value;
            break;
        case CMD_ARG_STRING:
            qe_cfg_tostr(ds, sp); // XXX: should complain about type mismatch?
            pstrcpy(strp, str + countof(str) - strp, sp->u.str);
            args[i].p = strp;
            if (strp < str + countof(str) - 1)
                strp += strlen(strp) + 1;
            break;
        }
    }
    if (ds->tok != ')') {
        put_status(s, "too many arguments for %s", d->name);
        return -1;
    }
    qe_cfg_next_token(ds);

    qs->this_cmd_func = d->action.func;
    qs->ec.function = d->name;
    call_func(d->sig, d->action, nb_args, args, args_type);
    qs->last_cmd_func = qs->this_cmd_func;
    if (qs->active_window)
        s = qs->active_window;
    check_window(&s);
    ds->s = s;
    sp->type = TOK_VOID;
    return 0;
}

static int qe_cfg_stmt(QEmacsDataSource *ds, QEValue *sp) {
    const char *start = ds->p;
    int res = 0;

    if (ds->tok == '{') {
        qe_cfg_next_token(ds);
        while (ds->tok != '}') {
            if (ds->tok == TOK_EOF) {
                put_status(ds->s, "missing '}'");
                return 1;
            }
            res |= qe_cfg_stmt(ds, sp);
        }
        qe_cfg_next_token(ds);
        return res;
    }

    if (ds->tok == TOK_IF) {
        int skip;
        QEValue *sp = &ds->stack[0];

        qe_cfg_next_token(ds);
        sp->type = TOK_VOID;
        if (qe_cfg_expr(ds, sp, PREC_COMMA) || qe_cfg_getvalue(ds, sp))
            goto fail;
        skip = (sp->type == TOK_STRING) ? 0 : sp->u.value == 0;
        if (skip) {
            qe_cfg_skip(ds);
        } else {
            qe_cfg_stmt(ds, sp);
        }
        if (ds->tok == TOK_ELSE) {
            if (!skip) {
                qe_cfg_skip(ds);
            } else {
                qe_cfg_stmt(ds, sp);
            }
        }
        return res;
    }
    if (qe_cfg_expr(ds, sp, PREC_COMMA) || qe_cfg_getvalue(ds, sp))
        goto fail;
    if (ds->tok == ';') {
        qe_cfg_next_token(ds);
    }
    if (ds->tok != '}' && ds->tok != TOK_EOF) {
        // XXX: should check for implicit semicolon at end of line
    }
    return 0;

fail:
    ds->p = start;
    qe_cfg_skip(ds);
    return 1;
}

static int qe_parse_script(EditState *s, QEmacsDataSource *ds)
{
    QEmacsState *qs = s->qe_state;
    QErrorContext ec = qs->ec;
    QEValue *sp = &ds->stack[0];

    ds->s = s;
    ds->p = ds->buf;
    sp->type = TOK_VOID;

    qs->ec.filename = ds->filename;
    qs->ec.function = NULL;
    qs->ec.lineno = ds->line_num = 1;

    qe_cfg_next_token(ds);
    while (ds->tok != TOK_EOF && ds->tok != TOK_ERR) {
        if (qe_cfg_stmt(ds, sp))
            sp->type = TOK_VOID;
    }
    qe_free(&ds->allocated_buf);
    qs->ec = ec;
    return sp->type;
}

void do_eval_expression(EditState *s, const char *expression, int argval)
{
    QEmacsDataSource ds;
    QEValue *sp = &ds.stack[0];
    char buf[64];
    int len;

    if (argval != NO_ARG && (s->b->flags & BF_READONLY))
        return;

    qe_cfg_init(&ds);
    ds.buf = expression;
    ds.filename = "<string>";
    switch (qe_parse_script(s, &ds)) {
    case TOK_VOID:
        break;
    case TOK_NUMBER:
        len = snprintf(buf, sizeof buf, "%lld", sp->u.value);
        if (argval == NO_ARG) {
            put_status(s, "-> %s", buf);
        } else {
            s->offset += eb_insert_utf8_buf(s->b, s->offset, buf, len);
        }
        break;
    case TOK_STRING:
        if (argval == NO_ARG) {
            /* XXX: should unparse string */
            put_status(s, "-> \"%s\"", sp->u.str);
        } else {
            s->offset += eb_insert_utf8_buf(s->b, s->offset, sp->u.str, sp->len);
        }
        break;
    case TOK_CHAR:
        len = utf8_encode(buf, (int)sp->u.value);
        if (argval == NO_ARG) {
            /* XXX: should unparse character */
            put_status(s, "-> '%.*s'", len, buf);
        } else {
            s->offset += eb_insert_utf8_buf(s->b, s->offset, buf, len);
        }
        break;
    default:
        put_status(s, "unexpected value type: %d", sp->type);
        break;
    }
    qe_cfg_release(&ds);
    do_refresh(s);
}

#define MAX_SCRIPT_LENGTH  (128 * 1024 - 1)

static int do_eval_buffer_region(EditState *s, int start, int stop)
{
    QEmacsDataSource ds;
    char *buf;
    int length, res;

    qe_cfg_init(&ds);

    if (stop < start) {
        int tmp = start;
        start = stop;
        stop = tmp;
    }
    if (start < 0)
        start = 0;
    if (stop < start)
        stop = start;
    if (stop > s->b->total_size)
        stop = s->b->total_size;
    length = stop - start;
    if (length > MAX_SCRIPT_LENGTH || !(buf = qe_malloc_array(char, length + 1))) {
        put_status(s, "Buffer too large");
        return -1;
    }
    length = eb_read(s->b, start, buf, length);
    buf[length] = '\0';
    ds.buf = ds.allocated_buf = buf;
    ds.filename = s->b->name;
    res = qe_parse_script(s, &ds);
    do_refresh(s);
    qe_cfg_release(&ds);
    return res;
}

void do_eval_region(EditState *s)
{
    s->region_style = 0;  /* deactivate region hilite */

    do_eval_buffer_region(s, s->b->mark, s->offset);
}

void do_eval_buffer(EditState *s)
{
    do_eval_buffer_region(s, 0, s->b->total_size);
}

int parse_config_file(EditState *s, const char *filename)
{
    QEmacsDataSource ds;
    FILE *fp;
    char *buf;
    long length;
    int res;

    qe_cfg_init(&ds);

    fp = fopen(filename, "r");
    if (!fp)
        return -1;

    fseek(fp, 0, SEEK_END);
    length = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (length > MAX_SCRIPT_LENGTH || !(buf = qe_malloc_array(char, length + 1))) {
        fclose(fp);
        put_status(s, "File too large");
        return -1;
    }

    length = fread(buf, 1, length, fp);
    fclose(fp);
    buf[length] = '\0';
    ds.buf = ds.allocated_buf = buf;
    ds.filename = filename;
    res = qe_parse_script(s, &ds);
    qe_cfg_release(&ds);
    return res;
}

#ifndef CONFIG_TINY
int qe_load_session(EditState *s)
{
    return parse_config_file(s, ".qesession");
}

void do_save_session(EditState *s, int popup)
{
    EditBuffer *b = eb_scratch("*session*", BF_UTF8);
    time_t now;

    eb_printf(b, "// qemacs version: %s\n", QE_VERSION);
    now = time(NULL);
    eb_printf(b, "// session saved: %s\n", ctime(&now));

    qe_save_variables(s, b);
    qe_save_macros(s, b);
    qe_save_open_files(s, b);
    qe_save_window_layout(s, b);

    if (popup) {
        b->offset = 0;
        b->flags |= BF_READONLY;
        show_popup(s, b, "QEmacs session");
    } else {
        eb_write_buffer(b, 0, b->total_size, ".qesession");
        eb_free(&b);
    }
}
#endif

static void script_complete(CompleteState *cp) {
    command_complete(cp);
    variable_complete(cp);
}

static int script_print_entry(CompleteState *cp, EditState *s, const char *name) {
    CmdDef *d = qe_find_cmd(name);
    if (d) {
        return command_print_entry(cp, s, name);
    } else {
        return variable_print_entry(cp, s, name);
    }        
}

static CompletionDef script_completion = {
    "script", script_complete, script_print_entry, command_get_entry
};

static CmdDef parser_commands[] = {

    CMD2( KEY_META(':'), KEY_NONE,
          "eval-expression", do_eval_expression, ESsi,
          "s{Eval: }[.script]|expression|ui")
    CMD0( KEY_NONE, KEY_NONE,
          "eval-region", do_eval_region)
    CMD0( KEY_NONE, KEY_NONE,
          "eval-buffer", do_eval_buffer)
#ifndef CONFIG_TINY
    CMD1( KEY_NONE, KEY_NONE,
          "save-session", do_save_session, 1)
#endif
    CMD_DEF_END,
};

static int parser_init(void)
{
    qe_register_cmd_table(parser_commands, NULL);
    qe_register_completion(&script_completion);
    return 0;
}

qe_module_init(parser_init);
