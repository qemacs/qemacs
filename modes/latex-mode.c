/*
 * LaTeX mode for QEmacs.
 *
 * Copyright (c) 2003 Martin Hedenfalk <mhe@home.se>
 * Based on c-mode by Fabrice Bellard
 * Requires the shell mode
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
                                const char32_t *str, int n,
                                QETermStyle *sbuf, ModeDef *syn)
{
    int i = 0, start;
    char32_t c;
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
                SET_STYLE(sbuf, start, i, LATEX_STYLE_STRING);
            }
            break;
        case '@':
            if (syn->colorize_flags != TEX_TEXINFO)
                break;
            if (str[i] == 'c' && !qe_isalnum_(str[i + 1])) {
                i = n;
                SET_STYLE(sbuf, start, i, LATEX_STYLE_COMMENT);
                break;
            }
            fallthrough;

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
            SET_STYLE(sbuf, start, i, LATEX_STYLE_FUNCTION);
            i = cp_skip_blanks(str, i, n);
            while (str[i] == '{' || str[i] == '[') {
                if (str[i++] == '[') {
                    /* handle [keyword] */
                    start = i;
                    while (str[i] != '\0' && str[i] != ']')
                        i++;
                    SET_STYLE(sbuf, start, i, LATEX_STYLE_KEYWORD);
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
                    SET_STYLE(sbuf, start, i, LATEX_STYLE_VARIABLE);
                    if (str[i] == '}')
                        i++;
                }
                i = cp_skip_blanks(str, i, n);
            }
            break;
        case '%':
            if (syn->colorize_flags == TEX_TEXINFO)
                break;
            /* line comment */
            i = n;
            SET_STYLE(sbuf, start, i, LATEX_STYLE_COMMENT);
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

// XXX: With prefix argument, should always insert " characters.
static void do_tex_insert_quote(EditState *s)
{
    EditBuffer *b = s->b;
    int offset = s->offset;
    char32_t c1 = eb_prevc(b, offset, &offset);
    char32_t c2 = eb_prevc(b, offset, &offset);

    // XXX: need more than 2 character backtrack
    if (c1 == '\"') {
        s->offset += eb_insert_char32(b, s->offset, '\"');
    } else
    if ((c1 == '`' || c1 == '\'') && c1 == c2) {
        eb_delete_range(b, offset, s->offset);
        s->offset += eb_insert_char32(b, s->offset, '\"');
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

static void latex_complete(CompleteState *cp, CompleteFunc enumerate) {
    struct latex_function *func;

    for (func = latex_funcs; func->name; func++) {
        enumerate(cp, func->name, CT_STRX);
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

static void latex_cmd_run(void *opaque, char *cmd,
                          qe__unused__ CompletionDef *completion)
{
    struct latex_function *func = (struct latex_function *)opaque;
    char dir[MAX_FILENAME_SIZE];
    const char *path = NULL;
    EditState *s = func->es;
    QEmacsState *qs = s->qs;

    if (cmd == NULL) {
        put_error(s, "Aborted");
        return;
    }

    /* get the directory of the open file and change into it */
    path = get_default_path(s->b, s->b->total_size, dir, sizeof dir);

    if (func->output_to_buffer) {
        EditBuffer *b;
        /* invoke command in shell buffer */
        b = qe_new_shell_buffer(qs, NULL, s, "*LaTeX output*", NULL,
                                path, cmd, SF_COLOR | SF_INFINITE |
                                SF_REUSE_BUFFER | SF_ERASE_BUFFER);
        if (b) {
            /* XXX: try to split window if necessary */
            switch_to_buffer(s, b);
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

            execv(argv[0], unconst(char * const*)argv);
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
            latex_cmd_run(func, buf, NULL);
        }
    } else {
        put_error(e, "%s: No match", buf);
    }
}

/* specific LaTeX commands */
static const CmdDef latex_commands[] = {
    CMD2( "tex-insert-quote", "\"",
          "Insert the appropriate quote marks for TeX",
          do_tex_insert_quote, ES, "*")
    CMD2( "TeX-command-master", "C-c C-c",
          "Run the latex process",
          do_latex, ESs,
          "s{Command: (default LaTeX) }[latex]|latex|")
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

static CompletionDef latex_completion = {
    .name = "latex",
    .enumerate = latex_complete,
};

static int latex_init(QEmacsState *qs)
{
    qe_register_mode(qs, &latex_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &texinfo_mode, MODEF_SYNTAX);
    qe_register_commands(qs, &latex_mode, latex_commands, countof(latex_commands));
    qe_register_completion(qs, &latex_completion);

    return 0;
}

qe_module_init(latex_init);
