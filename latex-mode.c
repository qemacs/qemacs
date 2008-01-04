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

#define MAX_BUF_SIZE    512

static ModeDef latex_mode;

/* TODO: add state handling to allow colorization of elements longer
 * than one line (eg, multi-line functions and strings)
 */
static void latex_colorize_line(unsigned int *buf, __unused__ int len,
                                int *colorize_state_ptr,
                                __unused__ int state_only)
{
    int c, state;
    unsigned int *p, *p_start;

    state = *colorize_state_ptr;
    p = buf;
    p_start = p;

    for (;;) {
        p_start = p;
        c = *p;
        switch (c) {
        case '\n':
            goto the_end;
        case '`':
            p++;
            /* a ``string'' */
            if (*p == '`') {
                while (1) {
                    p++;
                    if (*p == '\n' || (*p == '\'' && *(p+1) == '\''))
                        break;
                }
                if (*p == '\'' && *++p == '\'')
                    p++;
                set_color(p_start, p, QE_STYLE_STRING);
            }
            break;
        case '\\':
            p++;
            /* \function[keyword]{variable} */
            if (*p == '\'' || *p == '\"' || *p == '~' || *p == '%' || *p == '\\') {
                p++;
            } else {
                while (*p != '{' && *p != '[' && *p != '\n' && *p != ' ' && *p != '\\')
                    p++;
            }
            set_color(p_start, p, QE_STYLE_FUNCTION);
            while (*p == ' ' || *p == '\t') {
                /* skip space */
                p++;
            }
            while (*p == '{' || *p == '[') {
                if (*p++ == '[') {
                    /* handle [keyword] */
                    p_start = p;
                    while (*p != ']' && *p != '\n')
                        p++;
                    set_color(p_start, p, QE_STYLE_KEYWORD);
                    if (*p == ']')
                        p++;
                } else {
                    int braces = 0;
                    /* handle {variable} */
                    p_start = p;
                    while (*p != '\n') {
                        if (*p == '{') {
                            braces++;
                        } else
                        if (*p == '}') {
                            if (braces-- == 0)
                                break;
                        }
                        p++;
                    }
                    set_color(p_start, p, QE_STYLE_VARIABLE);
                    if (*p == '}')
                        p++;
                }
                while (*p == ' ' || *p == '\t') {
                    /* skip space */
                    p++;
                }
            }
            break;
        case '%':
            p++;
            /* line comment */
            while (*p != '\n')
                p++;
            set_color(p_start, p, QE_STYLE_COMMENT);
            break;
        default:
            p++;
            break;
        }
    }
 the_end:
    *colorize_state_ptr = state;
}

static int latex_mode_probe(ModeProbeData *p)
{
    /* currently, only use the file extension */
    if (match_extension(p->filename, "tex"))
        return 100;

    return 0;
}

static int latex_mode_init(EditState *s, ModeSavedData *saved_data)
{
    int ret;
    ret = text_mode_init(s, saved_data);
    if (ret)
        return ret;
    set_colorize_func(s, latex_colorize_line);
    return ret;
}

static void do_tex_insert_quote(EditState *s)
{
    int offset_bol, len, offset1;
    unsigned int buf[MAX_BUF_SIZE];
    int pos;

    offset_bol = eb_goto_bol2(s->b, s->offset, &pos);
    offset1 = offset_bol;
    len = eb_get_line(s->b, buf, countof(buf), &offset1);
    if (pos > len)
        return;
    if (pos >= 1 && buf[pos-1] == '\"') {
        eb_insert(s->b, s->offset, "\"", 1);
        s->offset++;
    } else
    if (pos >= 2 && (buf[pos-1] == '`' || buf[pos-1] == '\'') &&
          buf[pos-1] == buf[pos-2])
    {
        eb_delete(s->b, s->offset - 2, 2);
        eb_insert(s->b, s->offset, "\"", 1);
        s->offset++;
    } else {
        if (pos == 0 || buf[pos-1] == ' ') {
            eb_insert(s->b, s->offset, "``", 2);
            s->offset += 2;
        } else {
            eb_insert(s->b, s->offset, "''", 2);
            s->offset += 2;
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

static void latex_completion(StringArray *cs, const char *input)
{
    struct latex_function *func;

    for (func = latex_funcs; func->name; func++) {
        if (strxstart(func->name, input, NULL))
            add_string(cs, func->name);
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
    char cwd[MAX_FILENAME_SIZE];
    char dir[MAX_FILENAME_SIZE];
    const char *argv[4];
    char *p;
    int len;

    if (cmd == NULL) {
        put_status(func->es, "aborted");
        return;
    }

    argv[0] = "/bin/sh";
    argv[1] = "-c";
    argv[2] = cmd;
    argv[3] = NULL;

    getcwd(cwd, sizeof(cwd));

    /* get the directory of the open file and change into it
     */
    p = strrchr(func->es->b->filename, '/');
    if (p == func->es->b->filename)
        p++;
    len = p - func->es->b->filename + 1;
    pstrcpy(dir, sizeof(dir), func->es->b->filename);
    chdir(dir);

    if (func->output_to_buffer) {
        /* if the buffer already exists, kill it */
        EditBuffer *b = eb_find("*LaTeX output*");
        if (b) {
            /* XXX: e should not become invalid */
            b->modified = 0;
            do_kill_buffer(func->es, "*LaTeX output*");
        }

        /* create new buffer */
        b = new_shell_buffer(NULL, "*LaTeX output*", "/bin/sh", argv, 0);
        if (b) {
            /* XXX: try to split window if necessary */
            switch_to_buffer(func->es, b);
        }
    } else {
        int pid = fork();
        if (pid == 0) {
            /* child process */
            setsid();
            execv("/bin/sh", (char *const*)argv);
            exit(1);
        }
    }
    chdir(cwd);
}

static void do_latex(EditState *e, const char *cmd)
{
    char bname[MAX_FILENAME_SIZE];
    char buf[1024];
    int len;
    struct latex_function *func;

    /* strip extension from filename */
    pstrcpy(bname, sizeof(bname), e->b->filename);
    len = extension(bname) - bname;
    bname[len] = '\0';

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
            minibuffer_edit(buf, prompt, &func->history,
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
    CMD_( '\"', KEY_NONE, "tex-insert-quote", do_tex_insert_quote, ES, "*")
    /* this should actually be KEY_CTRLC(KEY_CTRL('c')), ie C-c C-c */
    CMD_( KEY_CTRL('c'), KEY_NONE, "TeX-command-master", do_latex, ESs,
          "s{Command: (default LaTeX) }[latex]|latex|")
    CMD_DEF_END,
};

static int latex_init(void)
{
    /* LaTeX mode is almost like the text mode, so we copy and patch it */
    memcpy(&latex_mode, &text_mode, sizeof(ModeDef));
    latex_mode.name = "LaTeX";
    latex_mode.mode_probe = latex_mode_probe;
    latex_mode.mode_init = latex_mode_init;

    qe_register_mode(&latex_mode);
    qe_register_cmd_table(latex_commands, &latex_mode);
    register_completion("latex", latex_completion);

    return 0;
}

qe_module_init(latex_init);
