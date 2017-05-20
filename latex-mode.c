/*
 * LaTeX mode for QEmacs.
 *
 * Copyright (c) 2003 Martin Hedenfalk <mhe@home.se>
 * Based on c-mode by Fabrice Bellard
 * Requires the shell mode
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

enum {
    TEX_TEX,
    TEX_LATEX,
    TEX_TEXINFO,
};

enum {
    LATEX_STYLE_COMMENT  = QE_STYLE_COMMENT,
    LATEX_STYLE_STRING   = QE_STYLE_STRING,
    LATEX_STYLE_FUNCTION = QE_STYLE_FUNCTION,
    LATEX_STYLE_KEYWORD  = QE_STYLE_KEYWORD,
    LATEX_STYLE_VARIABLE = QE_STYLE_VARIABLE,
};

/* TODO: add state handling to allow colorization of elements longer
 * than one line (eg, multi-line functions and strings)
 */
static void latex_colorize_line(QEColorizeContext *cp,
                                unsigned int *str, int n, ModeDef *syn)
{
    int i = 0, start, c;
    int state = cp->colorize_state;

    for (i = 0; i < n;) {
        start = i;
        c = str[i++];
        switch (c) {
        case '\0':
        case '\n':      /* Should not happen */
            goto the_end;
        case '`':
            /* a ``string'' */
            if (str[i] == '`') {
                for (;;) {
                    i++;
                    if (str[i] == '\0') {
                        /* Should either flag an error or propagate
                         * string style to the next line
                         */
                        break;
                    }
                    if (str[i] == '\'' && str[i + 1] == '\'') {
                        i += 2;
                        break;
                    }
                }
                SET_COLOR(str, start, i, LATEX_STYLE_STRING);
            }
            break;
        case '@':
            if (syn->colorize_flags != TEX_TEXINFO)
                break;
            if (str[i] == 'c' && !qe_isalnum_(str[i + 1])) {
                i = n;
                SET_COLOR(str, start, i, LATEX_STYLE_COMMENT);
                break;
            }
            /* fall thru */
        case '\\':
            /* \function[keyword]{variable} */
            if (str[i] == '\'' || str[i] == '\"' || str[i] == '~'
            ||  str[i] == '%' || str[i] == '\\') {
                i++;
            } else {
                while (str[i] != '\0' && str[i] != '{' && str[i] != '['
                &&     str[i] != ' ' && str[i] != '\\') {
                    i++;
                }
            }
            SET_COLOR(str, start, i, LATEX_STYLE_FUNCTION);
            while (qe_isblank(str[i])) {
                i++;
            }
            while (str[i] == '{' || str[i] == '[') {
                if (str[i++] == '[') {
                    /* handle [keyword] */
                    start = i;
                    while (str[i] != '\0' && str[i] != ']')
                        i++;
                    SET_COLOR(str, start, i, LATEX_STYLE_KEYWORD);
                    if (str[i] == ']')
                        i++;
                } else {
                    int braces = 0;
                    /* handle {variable} */
                    start = i;
                    while (str[i] != '\0') {
                        if (str[i] == '{') {
                            braces++;
                        } else
                        if (str[i] == '}') {
                            if (braces-- == 0)
                                break;
                        }
                        i++;
                    }
                    SET_COLOR(str, start, i, LATEX_STYLE_VARIABLE);
                    if (str[i] == '}')
                        i++;
                }
                while (qe_isblank(str[i])) {
                    i++;
                }
            }
            break;
        case '%':
            if (syn->colorize_flags == TEX_TEXINFO)
                break;
            /* line comment */
            i = n;
            SET_COLOR(str, start, i, LATEX_STYLE_COMMENT);
            break;
        default:
            break;
        }
    }
 the_end:
    cp->colorize_state = state;
}

static int latex_mode_probe(ModeDef *mode, ModeProbeData *mp)
{
    const u8 *p = mp->buf;

    /* currently, only use the file extension */
    /* Halibut (by Simon Tatham) has a syntax similar to TeX and uses
     * .but extension */
    if (match_extension(mp->filename, mode->extensions))
        return 80;

    /* Match TeX style sheets if they start with a comment */
    if (match_extension(mp->filename, "sty|cls") && *p == '%')
        return 80;

    if (*p == '\\') {
        /* match [\][a-z0-0_]+[{] */
        while (qe_isalnum_(*++p))
            continue;
        if (*p == '{')
            return 60;
    }
    return 1;
}

static void do_tex_insert_quote(EditState *s)
{
    EditBuffer *b = s->b;
    int offset = s->offset;
    int c1 = eb_prevc(b, offset, &offset);
    int c2 = eb_prevc(b, offset, &offset);

    if (c1 == '\"') {
        s->offset += eb_insert_uchar(b, s->offset, '\"');
    } else
    if ((c1 == '`' || c1 == '\'') && c1 == c2) {
        eb_delete_chars(b, s->offset, -2);
        s->offset += eb_insert_uchar(b, s->offset, '\"');
    } else {
        if (c1 == '\n' || c1 == ' ') {
            s->offset += eb_insert_str(b, s->offset, "``");
        } else {
            s->offset += eb_insert_str(b, s->offset, "''");
        }
    }
}

static struct latex_function {
    const char *name;
    const char *fmt;
    int ask;
    int output_to_buffer;
    StringArray history;
    EditState *es;
} latex_funcs[] = {
#define INIT_TAIL  NULL_STRINGARRAY, NULL
    { "AmSTeX", "amstex '\\nonstopmode\\input %s'", 0, 1, INIT_TAIL },
    { "PDFLaTeX", "pdflatex '\\nonstopmode\\input{%s}'", 0, 1, INIT_TAIL },
    { "PDFTeX", "pdftex '\\nonstopmode\\input %s'", 0, 1, INIT_TAIL },
    { "Check", "lacheck %s", 0, 1, INIT_TAIL },
    { "BibTeX", "bibtex %s", 0, 1, INIT_TAIL },
    { "LaTeX", "latex --src-specials '\\nonstopmode\\input{%s}'", 0, 1, INIT_TAIL },
    { "ThumbPDF", "thumbpdf %s", 0, 1, INIT_TAIL },
    { "View", "xdvi %s.dvi -paper a4", 1, 0, INIT_TAIL },
    { "Print", "dvips %s -Plp", 1, 0, INIT_TAIL },
    { "File", "dvips %s.dvi -o %s.ps", 1, 1, INIT_TAIL },
    { NULL, NULL, 0, 0, INIT_TAIL },
#undef INIT_TAIL
};

static void latex_completion(CompleteState *cp)
{
    struct latex_function *func;

    for (func = latex_funcs; func->name; func++) {
        if (strxstart(func->name, cp->current, NULL))
            add_string(&cp->cs, func->name, 0);
    }
}

static struct latex_function *find_latex_func(const char *name)
{
    struct latex_function *func;

    for (func = latex_funcs; func->name; func++) {
        if (!strxcmp(func->name, name))
            return func;
    }
    return NULL;
}

static void latex_cmd_run(void *opaque, char *cmd)
{
    struct latex_function *func = (struct latex_function *)opaque;
    char dir[MAX_FILENAME_SIZE];
    const char *path = NULL;

    if (cmd == NULL) {
        put_status(func->es, "Aborted");
        return;
    }

    /* get the directory of the open file and change into it */
    if (func->es->b) {
        path = get_default_path(func->es->b, func->es->b->total_size,
                                dir, sizeof dir);
    }

    if (func->output_to_buffer) {
        /* if the buffer already exists, kill it */
        EditBuffer *b = eb_find("*LaTeX output*");
        if (b) {
            /* XXX: e should not become invalid */
            qe_kill_buffer(b);
        }

        /* create new buffer */
        b = new_shell_buffer(NULL, NULL, "*LaTeX output*", NULL, path, cmd,
                             SF_COLOR | SF_INFINITE);
        if (b) {
            /* XXX: try to split window if necessary */
            switch_to_buffer(func->es, b);
        }
    } else {
        int pid = fork();
        if (pid == 0) {
            const char *argv[4];

            if (path) chdir(path);

            /* child process */
            setsid();

            argv[0] = get_shell();
            argv[1] = "-c";
            argv[2] = cmd;
            argv[3] = NULL;

            execv(argv[0], (char * const*)argv);
            exit(1);
        }
    }
}

static void do_latex(EditState *e, const char *cmd)
{
    char bname[MAX_FILENAME_SIZE];
    char buf[1024];
    struct latex_function *func;

    /* strip extension from filename */
    pstrcpy(bname, sizeof(bname), e->b->filename);
    strip_extension(bname);

    if (!cmd || cmd[0] == '\0')
        cmd = "LaTeX";

    /* check what command to run */
    func = find_latex_func(cmd);
    if (func) {
        /* pass the EditState through to latex_cmd_run() */
        func->es = e;
        /* construct the command line to run */
        strsubst(buf, sizeof(buf), func->fmt, "%s", bname);
        if (func->ask) {
            char prompt[128];
            snprintf(prompt, sizeof(prompt), "%s command: ", func->name);
            minibuffer_edit(e, buf, prompt, &func->history,
                            NULL /* completion */,
                            latex_cmd_run, func);
        } else {
            latex_cmd_run(func, buf);
        }
    } else {
        put_status(e, "%s: No match", buf);
    }
}

/* specific LaTeX commands */
static CmdDef latex_commands[] = {
    CMD2( '\"', KEY_NONE,
          "tex-insert-quote", do_tex_insert_quote, ES, "*")
    CMD2( KEY_CTRLC(KEY_CTRL('c')), KEY_NONE,   /* C-c C-c */
          "TeX-command-master", do_latex, ESs,
          "s{Command: (default LaTeX) }[latex]|latex|")
    CMD_DEF_END,
};

static ModeDef latex_mode = {
    .name = "LaTeX",
    .extensions = "tex|but",
    .mode_probe = latex_mode_probe,
    .colorize_func = latex_colorize_line,
    .colorize_flags = TEX_LATEX,
};

static ModeDef texinfo_mode = {
    .name = "TeXinfo",
    .extensions = "texi",
    .colorize_func = latex_colorize_line,
    .colorize_flags = TEX_TEXINFO,
};

static int latex_init(void)
{
    qe_register_mode(&latex_mode, MODEF_SYNTAX);
    qe_register_mode(&texinfo_mode, MODEF_SYNTAX);
    qe_register_cmd_table(latex_commands, &latex_mode);
    register_completion("latex", latex_completion);

    return 0;
}

qe_module_init(latex_init);
