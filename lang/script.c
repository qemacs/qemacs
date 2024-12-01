/*
 * Shell script mode for QEmacs.
 *
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

/*---------------- Shell script colors ----------------*/

enum {
    SHELL_SCRIPT_STYLE_TEXT =       QE_STYLE_DEFAULT,
    SHELL_SCRIPT_STYLE_COMMENT =    QE_STYLE_COMMENT,
    SHELL_SCRIPT_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    SHELL_SCRIPT_STYLE_COMMAND =    QE_STYLE_FUNCTION,
    SHELL_SCRIPT_STYLE_VARIABLE =   QE_STYLE_TYPE,
    SHELL_SCRIPT_STYLE_STRING =     QE_STYLE_STRING,
    SHELL_SCRIPT_STYLE_OP =         QE_STYLE_KEYWORD,
    SHELL_SCRIPT_STYLE_KEYWORD =    QE_STYLE_KEYWORD,
};

static const char shell_script_keywords[] = {
    /* reserved words */
    "if|then|elif|else|fi|case|esac|for|while|until|do|done|shift|break|"
    "function|return|export|alias|in|select|time|"
    /* internal commands */
    //"cd|echo|umask|"
};

static int shell_script_get_var(char *buf, int buf_size,
                                const char32_t *str, int j, int n)
{
    int i = 0;
    char32_t c;

    while (j < n && qe_isalnum_(c = str[j])) {
        if (i < buf_size - 1) {
            buf[i++] = c;
        }
        j++;
    }
    if (i < buf_size) {
        buf[i] = '\0';
    }
    return j;
}

static int shell_script_has_sep(const char32_t *str, int i, int n) {
    return i >= n || qe_findchar(" \t<>|&;()", str[i]);
}

static int shell_script_string(const char32_t *str, int i, int n,
                               char32_t sep, int escape, int dollar)
{
    while (i < n) {
        char32_t c = str[i++];
        if (c == '\\' && escape && i < n) {
            i++;
        } else
        if (c == '$' && dollar && i < n) {
            /* XXX: should highlight variable substitutions */
            i++;
        } else
        if (c == sep) {
            break;
        }
    }
    return i;
}

static void shell_script_colorize_line(QEColorizeContext *cp,
                                       const char32_t *str, int n,
                                       QETermStyle *sbuf, ModeDef *syn)
{
    int i = 0, j, start, style, bits = 0;
    char32_t c;

    /* special case sh-bang line */
    if (n >= 2 && str[0] == '#' && str[1] == '!') {
        SET_STYLE(sbuf, 0, n, SHELL_SCRIPT_STYLE_PREPROCESS);
        return;
    }

start_cmd:
    style = SHELL_SCRIPT_STYLE_COMMAND;
    i = cp_skip_blanks(str, i, n);

    while (i < n) {
        start = i;
        switch (c = str[i++]) {
        case '#':
            style = SHELL_SCRIPT_STYLE_COMMENT;
            i = n;
            break;
        case '`':
            /* XXX: should be a state */
            SET_STYLE1(sbuf, start, SHELL_SCRIPT_STYLE_OP);
            goto start_cmd;
        case '\'':
            i = shell_script_string(str, i, n, c, 0, 0);
            SET_STYLE(sbuf, start, i, SHELL_SCRIPT_STYLE_STRING);
            /* XXX: should support multi-line strings? */
            continue;
        case '"':
            i = shell_script_string(str, i, n, c, 1, 1);
            SET_STYLE(sbuf, start, i, SHELL_SCRIPT_STYLE_STRING);
            /* XXX: should support multi-line strings? */
            continue;
        case '\\':
            if (i >= n) {
                /* Should keep state for next line */
                SET_STYLE1(sbuf, start, SHELL_SCRIPT_STYLE_OP);
                continue;
            }
            /* Do not interpret the next character */
            i++;
            break;
        case '$':
            if (i == n || qe_findchar(" \t\"", str[i]))
                break;
            SET_STYLE1(sbuf, start++, SHELL_SCRIPT_STYLE_OP);
			switch (c = str[i++]) {
            case '\'':
                i = shell_script_string(str, i, n, c, 1, 0);
                SET_STYLE(sbuf, start, i, SHELL_SCRIPT_STYLE_STRING);
                continue;
            case '(':  /* expand command output */
                bits = (bits << 2) | 1;
                SET_STYLE1(sbuf, start, SHELL_SCRIPT_STYLE_OP);
                goto start_cmd;
            case '[':  /* expression */
                SET_STYLE1(sbuf, start, SHELL_SCRIPT_STYLE_OP);
                for (j = i; i < n; i++) {
                    if (str[i] == ']')
                        break;
                }
                SET_STYLE(sbuf, j, i, SHELL_SCRIPT_STYLE_TEXT);
                if (i < n) {
                    i++;
                    SET_STYLE(sbuf, i - 1, i, SHELL_SCRIPT_STYLE_OP);
                }
                continue;
            case '{':  /* variable substitution with options */
                SET_STYLE1(sbuf, start, SHELL_SCRIPT_STYLE_OP);
                /* XXX: should parse variable name or single char */
                /* XXX: should support % syntax with regex */
                for (j = i; i < n; i++) {
                    if (str[i] == '}')
                        break;
                }
                SET_STYLE(sbuf, j, i, SHELL_SCRIPT_STYLE_VARIABLE);
                if (i < n) {
                    i++;
                    SET_STYLE(sbuf, i - 1, i, SHELL_SCRIPT_STYLE_OP);
                }
                continue;
            case '$':
            case '?':
            case '#':
            default:
                if (qe_isalpha_(c)) {
                    i = shell_script_get_var(NULL, 0, str, i, n);
                    SET_STYLE(sbuf, start, i, SHELL_SCRIPT_STYLE_VARIABLE);
                } else {
                    SET_STYLE1(sbuf, start, SHELL_SCRIPT_STYLE_OP);
                }
                break;
            }
			continue;
		case ' ':
        case '\t':
			style = SHELL_SCRIPT_STYLE_TEXT;
            break;
        case '{':  /* compound command */
        case '}':
            /* XXX: should support numeric enumerations */
            if (i == n || qe_isblank(str[i])) {
                SET_STYLE(sbuf, start, i, SHELL_SCRIPT_STYLE_OP);
                goto start_cmd;
            }
			style = SHELL_SCRIPT_STYLE_TEXT;
            break;
        case '>':
        case '<':
            // XXX: Should support other punctuation syntaxes
            if (str[i] == c) {  /* handle >> and << */
                i++;
            }
            SET_STYLE(sbuf, start, i, SHELL_SCRIPT_STYLE_OP);
            // XXX: Should support << syntax
			style = SHELL_SCRIPT_STYLE_TEXT;
            continue;
        case '|':
        case '&':
            if (str[i] == c) {  /* handle || and && */
                i++;
            }
            SET_STYLE(sbuf, start, i, SHELL_SCRIPT_STYLE_OP);
            goto start_cmd;
        case ';':
            SET_STYLE1(sbuf, start, SHELL_SCRIPT_STYLE_OP);
            goto start_cmd;
        case '(':
            bits = (bits << 2) | 2;
            SET_STYLE1(sbuf, start, SHELL_SCRIPT_STYLE_OP);
            goto start_cmd;
        case ')':
            bits = (bits >> 2);
            SET_STYLE1(sbuf, start, SHELL_SCRIPT_STYLE_OP);
            goto start_cmd;
        case '[':
            if (style == SHELL_SCRIPT_STYLE_COMMAND) {
                bits = (bits << 2) | 3;
                SET_STYLE1(sbuf, start, SHELL_SCRIPT_STYLE_OP);
                style = SHELL_SCRIPT_STYLE_TEXT;
                continue;
            }
            break;
        case ']':
            if ((bits & 3) == 3) {
                bits = (bits >> 2);
                SET_STYLE1(sbuf, start, SHELL_SCRIPT_STYLE_OP);
                style = SHELL_SCRIPT_STYLE_TEXT;
                continue;
            }
            break;

        default:
            if (style == SHELL_SCRIPT_STYLE_COMMAND && qe_isalpha_(c)) {
                char kbuf[64];

                i = shell_script_get_var(kbuf, sizeof kbuf, str, i - 1, n);
                if (shell_script_has_sep(str, i, n) && strfind(syn->keywords, kbuf)) {
                    SET_STYLE(sbuf, start, i, SHELL_SCRIPT_STYLE_KEYWORD);
                    if (!strfind("for|case|export|in", kbuf))
                        goto start_cmd;
                    else
                        continue;
                }
                if (str[i] == '=') {
                    SET_STYLE(sbuf, start, i, SHELL_SCRIPT_STYLE_VARIABLE);
                    SET_STYLE1(sbuf, i, SHELL_SCRIPT_STYLE_OP);
                    i++;
                    style = SHELL_SCRIPT_STYLE_TEXT;
                    continue;
                }
            }
            break;
        }
        SET_STYLE(sbuf, start, i, style);
    }
}

static int shell_script_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    /* Match on file extension, shbang line and .bashrc .bash_history... */
    if (match_extension(p->filename, mode->extensions)
    ||  match_shell_handler(cs8(p->buf), mode->shell_handlers)
    ||  (*p->filename == '.'
     &&  stristart(p->filename + 1, mode->extensions, NULL))) {
        return 82;
    }

    if (stristart(p->filename, ".profile", NULL)) {
        /* XXX: Should check the user login shell */
        return 80;
    }

    if (p->buf[0] == '#') {
        if (p->buf[1] == '!')
            return 60;
        if (p->buf[1] == ' ')
            return 25;
    }
    return 1;
}

/* XXX: should have shell specific variations */
static ModeDef sh_mode = {
    .name = "Shell",
    .alt_name = "sh",
    .extensions = "sh",
    .shell_handlers = "sh",
    .mode_probe = shell_script_mode_probe,
    .colorize_func = shell_script_colorize_line,
    .keywords = shell_script_keywords,
};

static ModeDef bash_mode = {
    .name = "bash",
    .extensions = "bash",
    .shell_handlers = "bash",
    .mode_probe = shell_script_mode_probe,
    .colorize_func = shell_script_colorize_line,
    .keywords = shell_script_keywords,
};

static ModeDef csh_mode = {
    .name = "csh",
    .extensions = "csh",
    .shell_handlers = "csh",
    .mode_probe = shell_script_mode_probe,
    .colorize_func = shell_script_colorize_line,
    .keywords = shell_script_keywords,
};

static ModeDef ksh_mode = {
    .name = "ksh",
    .extensions = "ksh",
    .shell_handlers = "ksh",
    .mode_probe = shell_script_mode_probe,
    .colorize_func = shell_script_colorize_line,
    .keywords = shell_script_keywords,
};

static ModeDef zsh_mode = {
    .name = "zsh",
    .extensions = "zsh",
    .shell_handlers = "zsh",
    .mode_probe = shell_script_mode_probe,
    .colorize_func = shell_script_colorize_line,
    .keywords = shell_script_keywords,
};

static ModeDef tcsh_mode = {
    .name = "tcsh",
    .extensions = "tcsh",
    .shell_handlers = "tcsh",
    .mode_probe = shell_script_mode_probe,
    .colorize_func = shell_script_colorize_line,
    .keywords = shell_script_keywords,
};

static int shell_script_init(QEmacsState *qs)
{
    qe_register_mode(qs, &sh_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &bash_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &csh_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &ksh_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &zsh_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &tcsh_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(shell_script_init);
