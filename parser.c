/*
 * QEmacs, tiny but powerful multimode editor
 *
 * Copyright (c) 2000-2002 Fabrice Bellard.
 * Copyright (c) 2000-2017 Charlie Gordon.
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

/* config file parsing */

/* CG: error messages should go to the *error* buffer.
 * displayed as a popup upon start.
 */

static int expect_token(const char **pp, int tok)
{
    skip_spaces(pp);
    if (**pp == tok) {
        ++*pp;
        skip_spaces(pp);
        return 1;
    } else {
        put_status(NULL, "'%c' expected", tok);
        return 0;
    }
}

static int qe_cfg_parse_string(EditState *s, const char **pp,
                               char *dest, int size)
{
    const char *p = *pp;
    int c, delim = *p++;
    int res = 0;
    int pos = 0;

    for (;;) {
        /* encoding issues delibarately ignored */
        c = *p;
        if (c == '\0') {
            put_status(s, "Unterminated string");
            res = -1;
            break;
        }
        p++;
        if (c == delim)
            break;
        if (c == '\\') {
            c = *p++;
            switch (c) {
            case 'n':
                c = '\n';
                break;
            case 'r':
                c = '\r';
                break;
            case 't':
                c = '\t';
                break;
            }
        }
        if (pos < size - 1)
            dest[pos++] = c;
    }
    if (pos < size)
        dest[pos] = '\0';
    *pp = p;
    return res;
}

typedef struct QEmacsDataSource {
    const char *filename;
    FILE *f;
    EditBuffer *b;
    const char *str;
    int line_num;
    int pos, len;
    int offset, stop;
} QEmacsDataSource;

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

static int qe_parse_script(EditState *s, QEmacsDataSource *ds)
{
    QEmacsState *qs = s->qe_state;
    QErrorContext ec;
    char line[1024], str[1024];
    char prompt[64], cmd[128], *q, *strp;
    const char *p, *r;
    int line_num;
    CmdDef *d;
    int nb_args, sep, i, skip;
    CmdArg args[MAX_CMD_ARGS];
    unsigned char args_type[MAX_CMD_ARGS];

    ec = qs->ec;
    skip = 0;
    line_num = ds->line_num;
    /* Should parse whole config file in a single read, or load it via
     * a buffer */
    for (;;) {
        if (data_gets(ds, line, sizeof(line)) == NULL)
            break;
        line_num++;
        qs->ec.filename = ds->filename;
        qs->ec.function = NULL;
        qs->ec.lineno = line_num;

        /* XXX: line based parser does not handle multiline comments */
        p = line;
        skip_spaces(&p);
        if (p[0] == '}') {
            /* simplistic 1 level if block skip feature */
            p++;
            skip_spaces(&p);
            skip = 0;
        }
        if (skip)
            continue;

        /* skip comments */
        while (p[0] == '/' && p[1] == '*') {
            for (p += 2; *p; p++) {
                if (p[0] == '*' && p[1] == '/') {
                    p += 2;
                    break;
                }
            }
            skip_spaces(&p);
            /* XXX: unfinished comments silently unsupported */
        }
        if (p[0] == '/' && p[1] == '/')
            continue;
        if (p[0] == '\0')
            continue;

        get_str(&p, cmd, sizeof(cmd), "{}();=/");
        if (*cmd == '\0') {
            put_status(s, "Syntax error");
            continue;
        }
        /* transform '_' to '-' */
        q = cmd;
        while (*q) {
            if (*q == '_')
                *q = '-';
            q++;
        }
        /* simplistic 1 level if block skip feature */
        if (strequal(cmd, "if")) {
            if (!expect_token(&p, '('))
                goto fail;
            skip = !strtol(p, (char**)&p, 0);
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
                skip_spaces(&p);
                if (*p == '\"' || *p == '\'') {
                    if (qe_cfg_parse_string(s, &p, str, countof(str)))
                        goto fail;
                    qe_set_variable(s, cmd, str, 0);
                } else {
                    qe_set_variable(s, cmd, NULL, strtol(p, (char**)&p, 0));
                }
                skip_spaces(&p);
                if (*p != ';' && *p != '\0')
                    put_status(s, "Syntax error '%s'", cmd);
                continue;
            }
        }
#endif
        if (!expect_token(&p, '('))
            goto fail;

        /* search for command */
        d = qe_find_cmd(cmd);
        if (!d) {
            put_status(s, "Unknown command '%s'", cmd);
            continue;
        }
        nb_args = 0;

        /* construct argument type list */
        r = d->name + strlen(d->name) + 1;
        if (*r == '*') {
            r++;
            if (s->b->flags & BF_READONLY) {
                put_status(s, "Buffer is read only");
                continue;
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
                put_status(s, "Badly defined command '%s'", cmd);
                goto fail;
            }
            args[nb_args].p = NULL;
            args_type[nb_args++] = arg_type & CMD_ARG_TYPE_MASK;
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
            }

            skip_spaces(&p);
            if (sep) {
                /* CG: Should test for arg list too short. */
                /* CG: Could supply default arguments. */
                if (!expect_token(&p, sep))
                    goto fail;
            }
            sep = ',';

            switch (args_type[i]) {
            case CMD_ARG_INT:
                r = p;
                args[i].n = strtol(p, (char**)&p, 0);
                if (p == r) {
                    put_status(s, "Number expected for arg %d", i);
                    goto fail;
                }
                break;
            case CMD_ARG_STRING:
                if (*p != '\"' && *p != '\'') {
                    /* XXX: should convert number to string */
                    put_status(s, "String expected for arg %d", i);
                    goto fail;
                }
                if (qe_cfg_parse_string(s, &p, strp,
                                        str + countof(str) - strp) < 0)
                {
                    goto fail;
                }
                args[i].p = strp;
                strp += strlen(strp) + 1;
                break;
            }
        }
        skip_spaces(&p);
        if (*p != ')') {
            put_status(s, "Too many arguments for %s", d->name);
            goto fail;
        }

        qs->this_cmd_func = d->action.func;
        qs->ec.function = d->name;
        call_func(d->sig, d->action, nb_args, args, args_type);
        qs->last_cmd_func = qs->this_cmd_func;
        if (qs->active_window)
            s = qs->active_window;
        check_window(&s);
        continue;

    fail:
        ;
    }
    qs->ec = ec;

    return 0;
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

void do_eval_expression(EditState *s, const char *expression)
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
    /* deactivate region hilite */
    s->region_style = 0;

    do_eval_buffer_region(s, s->b->mark, s->offset);
}

void do_eval_buffer(EditState *s)
{
    do_eval_buffer_region(s, 0, -1);
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

        /* Should show window caption "qemacs session" */
        show_popup(s, b);
    } else {
        eb_write_buffer(b, 0, b->total_size, ".qesession");
        eb_free(&b);
    }
}
#endif

static CmdDef parser_commands[] = {

    CMD2( KEY_META(':'), KEY_NONE,
          "eval-expression", do_eval_expression, ESs,
          "s{Eval: }|expression|")
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
    return 0;
}

qe_module_init(parser_init);
