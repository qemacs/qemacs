/*
 * QEmacs, tiny but powerful multimode editor
 *
 * Copyright (c) 2000-2002 Fabrice Bellard.
 * Copyright (c) 2000-2014 Charlie Gordon.
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

int parse_config_file(EditState *s, const char *filename)
{
    QEmacsState *qs = s->qe_state;
    QErrorContext ec;
    FILE *f;
    char line[1024], str[1024];
    char prompt[64], cmd[128], *q, *strp;
    const char *p, *r;
    int line_num;
    CmdDef *d;
    int nb_args, c, sep, i, skip;
    CmdArg args[MAX_CMD_ARGS];
    unsigned char args_type[MAX_CMD_ARGS];

    f = fopen(filename, "r");
    if (!f)
        return -1;
    ec = qs->ec;
    skip = 0;
    line_num = 0;
    /* Should parse whole config file in a single read, or load it via
     * a buffer */
    for (;;) {
        if (fgets(line, sizeof(line), f) == NULL)
            break;
        line_num++;
        qs->ec.filename = filename;
        qs->ec.function = NULL;
        qs->ec.lineno = line_num;

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
                if (*p == '*' && p[1] == '/') {
                    p += 2;
                    break;
                }
            }
            skip_spaces(&p);
            /* CG: unfinished comments silently unsupported */
        }
        if (p[0] == '/' && p[1] == '/')
            continue;
        if (p[0] == '\0')
            continue;

        get_str(&p, cmd, sizeof(cmd), "(");
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
                if (*p != ';' && *p != '\n')
                    put_status(s, "Syntax error '%s'", cmd);
                continue;
            }
        }
#endif
        /* search for command */
        d = qe_find_cmd(cmd);
        if (!d) {
            put_status(s, "Unknown command '%s'", cmd);
            continue;
        }
        nb_args = 0;

        /* first argument is always the window */
        args_type[nb_args++] = CMD_ARG_WINDOW;

        /* construct argument type list */
        r = d->name + strlen(d->name) + 1;
        if (*r == '*') {
            r++;
            if (s->b->flags & BF_READONLY) {
                put_status(s, "Buffer is read only");
                continue;
            }
        }

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

        if (!expect_token(&p, '('))
            goto fail;

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
        c = ')';
        if (*p != c) {
            put_status(s, "Too many arguments for %s", d->name);
            goto fail;
        }

        qs->this_cmd_func = d->action.func;
        qs->ec.function = d->name;
        call_func(d->sig, d->action, nb_args, args, args_type);
        qs->last_cmd_func = qs->this_cmd_func;
        if (qs->active_window)
            s = qs->active_window;
        continue;

    fail:
        ;
    }
    fclose(f);
    qs->ec = ec;

    return 0;
}
