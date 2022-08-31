/*
 * QEmacs, tiny but powerful multimode editor
 *
 * Copyright (c) 2000-2002 Fabrice Bellard.
 * Copyright (c) 2000-2022 Charlie Gordon.
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

/* minimalist config file parsing */

/* CG: error messages should go to the *error* buffer.
 * displayed as a popup upon start.
 */

typedef struct QEmacsDataSource {
    EditState *s;
    const char *filename;   // source filename
    const char *p;
    FILE *f;
    EditBuffer *b;
    const char *str;
    int line_num;           // source line number
    int pos, len;
    int offset, stop;
} QEmacsDataSource;

static int has_token(const char **pp, int tok) {
    if (qe_skip_spaces(pp) == tok) {
        ++*pp;
        qe_skip_spaces(pp);
        return 1;
    } else {
        return 0;
    }
}

static int expect_token(const char **pp, int tok) {
    if (has_token(pp, tok)) {
        return 1;
    } else {
        put_status(NULL, "'%c' expected", tok);
        return 0;
    }
}

// XXX: Should use strunquote parser from util.c
static int qe_cfg_parse_string(EditState *s, const char **pp,
                               char *dest, int size)
{
    const char *p = *pp;
    int delim = *p++;
    int res = 0;
    int pos = 0;
    int end = size - 1;

    /* should check for delim at *p and return -1 if no string */
    for (;;) {
        /* encoding issues deliberately ignored */
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
            c = *p++;
            switch (c) {
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
                // ignore other escapes, including octal and hex */
            }
        }
        /* XXX: silently truncate overlong string constants */
        if (pos < end)
            dest[pos++] = c;
    }
    if (pos <= end)
        dest[pos] = '\0';
    *pp = p;
    return res;
}

static int get_cmd(const char **pp, char *buf, int buf_size, const char *stop) {
    char *q;
    int len = get_str(pp, buf, buf_size, stop);
    /* convert '_' to '-' */
    for (q = buf; *q; q++) {
        if (*q == '_')
            *q = '-';
    }
    return len;
}

static char *data_gets(QEmacsDataSource *ds, char *buf, int size)
{
    if (ds->f) {
        return fgets(buf, size, ds->f);
    }
    if (ds->b) {
        /* EditBuffer should not be modified during parse! */
        if (ds->offset < ds->stop && ds->offset < ds->b->total_size) {
            // XXX: parsing will continue beyond `stop` to end of line */
            eb_fgets(ds->b, buf, size, ds->offset, &ds->offset);
            return buf;
        }
        return NULL;
    }
    if (ds->str) {
        int i;
        for (i = 0; i < size - 1; i++) {
            if (ds->pos >= ds->len
            ||  (buf[i] = ds->str[ds->pos++]) == '\n')
                break;
        }
        buf[i] = '\0';
        return i ? buf : NULL;
    }
    return NULL;
}

static int qe_cfg_call(QEmacsDataSource *ds, const CmdDef *d);

static int qe_parse_script(EditState *s, QEmacsDataSource *ds)
{
    QEmacsState *qs = s->qe_state;
    QErrorContext ec = qs->ec;
    char line[1024];
    char cmd[128], arg[128];
    const char *p;
    const CmdDef *d;
    int skip, incomment;

    ds->s = s;

    qs->ec.filename = ds->filename;
    qs->ec.function = NULL;
    qs->ec.lineno = ds->line_num = 1;

    incomment = skip = 0;
    /* Should parse whole config file in a single read, or load it via
     * a buffer */
    while (data_gets(ds, line, sizeof(line))) {
        qs->ec.lineno = ds->line_num++;

        p = line;
    again:
        if (qe_skip_spaces(&p) == '\0')
            continue;
        if (incomment) {
            while (*p) {
                if (*p++ == '*' && *p == '/') {
                    p++;
                    incomment = 0;
                    break;
                }
            }
            goto again;
        }
        if (*p == '/') {
            if (p[1] == '/')  /* line comment */
                continue;
            if (p[1] == '*') { /* multiline comment */
                p += 2;
                incomment = 1;
                goto again;
            }
        }
        if (has_token(&p, '}')) {
            /* simplistic 1 level if block skip feature */
            skip = 0;
            goto again;
        }
        if (skip)
            continue;

        /* XXX: should parse numbers, strings and symbols */
        if (!get_cmd(&p, cmd, sizeof(cmd), "{}();=/"))
            goto syntax;

        /* simplistic 1 level if block skip feature */
        if (strequal(cmd, "if")) {
            if (!expect_token(&p, '('))
                goto fail;
            if (!get_cmd(&p, arg, sizeof(arg), ")"))
                goto syntax;
            skip = !strtol_c(arg, NULL, 0);
            if (!expect_token(&p, ')') || !expect_token(&p, '{'))
                goto fail;
            continue;
        }
#ifndef CONFIG_TINY
        {
            /* search for variable */
            struct VarDef *vp;

            vp = qe_find_variable(cmd);
            if (vp) {
                if (!expect_token(&p, '='))
                    goto fail;
                if (*p == '\"' || *p == '\'') {
                    if (qe_cfg_parse_string(s, &p, str, countof(str)))
                        goto fail;
                    qe_set_variable(s, cmd, str, 0);
                } else {
                    qe_set_variable(s, cmd, NULL, strtol_c(p, &p, 0));
                }
                goto next;
            }
        }
#else
        if (has_token(&p, '=')) {
            int value = strtol_c(p, &p, 0);
            if (strequal(cmd, "tab-width")) {
                s->b->tab_width = value;
                goto next;
            }
            if (strequal(cmd, "default-tab-width")) {
                qs->default_tab_width = value;
                goto next;
            }
            if (strequal(cmd, "indent-tabs-mode")) {
                s->indent_tabs_mode = value;
                goto next;
            }
            if (strequal(cmd, "indent-width")) {
                s->indent_size = value;
                goto next;
            }
            /* ignore other variables without a warning */
            put_status(s, "unsupported variable %s", cmd);
            continue;
        }
#endif
        if (!expect_token(&p, '('))
            goto fail;

        /* search for command */
        d = qe_find_cmd(cmd);
        if (!d || d->sig >= CMD_ISS) {
            put_status(s, "unknown command '%s'", cmd);
            continue;
        }
        ds->p = p;
        if (qe_cfg_call(ds, d))
            continue;
        s = ds->s;
        p = ds->p;
    next:
        if (has_token(&p, ';'))
            goto again;
        if (*p != '\0')
            put_status(s, "missing ';' at '%s'", p);
        continue;
    syntax:
        put_status(s, "syntax error at '%s'", p);
        continue;
    fail:
        continue;
    }
    qs->ec = ec;
    return 0;
}

static int qe_cfg_call(QEmacsDataSource *ds, const CmdDef *d) {
    EditState *s = ds->s;
    QEmacsState *qs = s->qe_state;
    const char *p = ds->p;
    char str[1024];
    char *strp;
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
            put_status(s, "invalid command definition '%s'", d->name);
            return -1;
        }
        args[nb_args].p = NULL;
        args_type[nb_args++] = cas.arg_type;
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
            args[i].n = d->val;
            continue;
        case CMD_ARG_STRINGVAL:
            /* kludge for xxx-mode functions and named kbd macros,
               must be the last argument */
            args[i].p = cas.prompt;
            continue;
        }

        qe_skip_spaces(&p);
        if (*p == ')') {
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
            /* source stays in front of the ')'. */
            /* Let the argument matcher complain about the missing argument */
        } else {
            if (sep && !expect_token(&p, sep))
                return -1;
            sep = ',';
        }

        /* XXX: should parse and evaluate argument */

        switch (args_type[i] & CMD_ARG_TYPE_MASK) {
        case CMD_ARG_INT:
            args[i].n = strtol_c(p, &r, 0);
            if (p == r) {
                put_status(s, "number expected for arg %d", i);
                return -1;
            }
            p = r;
            if (args_type[i] == (CMD_ARG_INT | CMD_ARG_NEG_ARGVAL))
                args[i].n *= -1;
            break;
        case CMD_ARG_STRING:
            if (*p != '\"' && *p != '\'') {
                put_status(s, "string expected for arg %d", i);
                return -1;
            }
            if (qe_cfg_parse_string(s, &p, strp, str + countof(str) - strp) < 0)
                return -1;
            args[i].p = strp;
            if (strp < str + countof(str) - 1)
                strp += strlen(strp) + 1;
            break;
        }
    }
    if (!has_token(&p, ')')) {
        put_status(s, "too many arguments for %s", d->name);
        return -1;
    }

    qs->this_cmd_func = d->action.func;
    qs->ec.function = d->name;
    call_func(d->sig, d->action, nb_args, args, args_type);
    qs->ec.function = NULL;
    qs->last_cmd_func = qs->this_cmd_func;
    if (qs->active_window)
        s = qs->active_window;
    check_window(&s);
    ds->s = s;
    ds->p = p;
    return 0;
}

void do_eval_expression(EditState *s, const char *expression, int argval)
{
    QEmacsDataSource ds = { 0 };

    ds.filename = "<string>";
    ds.str = expression;
    ds.len = strlen(expression);
    qe_parse_script(s, &ds);
}

static int do_eval_buffer_region(EditState *s, int start, int stop)
{
    QEmacsDataSource ds = { 0 };

    ds.filename = s->b->name;
    ds.b = s->b;
    if (stop < 0)
        stop = INT_MAX;
    if (start < 0)
        start = INT_MAX;
    if (start < stop) {
        ds.offset = start;
        ds.stop = stop;
    } else {
        ds.offset = stop;
        ds.stop = start;
    }
    return qe_parse_script(s, &ds);
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
    QEmacsDataSource ds = { 0 };
    int res;

    ds.filename = filename;
    ds.f = fopen(filename, "r");
    if (!ds.f)
        return -1;
    res = qe_parse_script(s, &ds);
    fclose(ds.f);
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
        show_popup(s, b, "QEmacs session");
    } else {
        eb_write_buffer(b, 0, b->total_size, ".qesession");
        eb_free(&b);
    }
}
#endif

static const CmdDef parser_commands[] = {
    CMD2( "eval-expression", "M-:",
          "Evaluate a qemacs expression",
          do_eval_expression, ESsi,
          "s{Eval: }|expression|"
          "P")
    /* XXX: should take region as argument, implicit from keyboard */
    CMD0( "eval-region", "",
          "Evaluate qemacs expressions in a region",
          do_eval_region)
    CMD0( "eval-buffer", "",
          "Evaluate qemacs expressions in the buffer",
          do_eval_buffer)
#ifndef CONFIG_TINY
    CMD1( "save-session", "",
          "Save the current session in a .qesession file",
          do_save_session, 1)
#endif
};

static int parser_init(void) {
    qe_register_commands(NULL, parser_commands, countof(parser_commands));
    return 0;
}

qe_module_init(parser_init);
