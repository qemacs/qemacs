/*
 * HTML Source mode for QEmacs.
 *
 * Copyright (c) 2000-2018 Charlie Gordon.
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

static int get_html_entity(unsigned int *p)
{
    unsigned int *p_start, c;

    p_start = p;
    c = *p;

    if (c != '&')
        return 0;

    c = *++p;
    if (c == '#') {
        c = *++p;
        if (c == 'x') {
            do {
                c = *++p;
            } while (qe_isxdigit(c));
        } else {
            while (qe_isdigit(c))
                c = *++p;
        }
    } else
    if (qe_isalpha(c)) {
        do {
            c = *++p;
        } while (qe_isalnum(c));
    } else {
        /* not an entity */
        return 0;
    }
    if (c == ';') {
        p++;
    }
    return p - p_start;
}

/* colorization states */
enum {
    IN_HTML_COMMENT    = 0x0001,      /* <!-- ... --> */
    IN_HTML_COMMENT1   = 0x0002,      /* <! ... > */
    IN_HTML_ENTITY     = 0x0004,      /* &name[;] / &#123[;] */
    IN_HTML_EMBEDDED   = 0x00ff,      /* flags for embedded php, js, css, c# */
    IN_HTML_TAG        = 0x0100,      /* <tag ... > */
    IN_HTML_STRING     = 0x0200,      /* <tag " ... " > */
    IN_HTML_STRING1    = 0x0400,      /* <tag ' ... ' > */
    IN_HTML_SCRIPT     = 0x1000,      /* <script> [...] </script> */
    IN_HTML_STYLE      = 0x2000,      /* <style> [...] </style> */
    IN_HTML_PHP        = 0x4000,      /* <?php ... ?> */
    IN_HTML_ASP        = 0x8000,      /* <% ... %> */
};

enum {
    HTML_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
    HTML_STYLE_COMMENT    = QE_STYLE_HTML_COMMENT,
    HTML_STYLE_COMMENT1   = QE_STYLE_HTML_COMMENT,
    HTML_STYLE_ENTITY     = QE_STYLE_HTML_ENTITY,
    HTML_STYLE_STRING     = QE_STYLE_HTML_STRING,
    HTML_STYLE_TAG        = QE_STYLE_HTML_TAG,
    HTML_STYLE_CSS        = QE_STYLE_CSS,
};

static int htmlsrc_tag_match(const unsigned int *buf, int i, const char *str,
                             int *iend)
{
    int len;

    if (ustristart(buf + i, str, &len)
    &&  buf[i + len] != '-' && !qe_isalnum_(buf[i + len])) {
        if (iend)
            *iend = i + len;
        return 1;
    } else {
        return 0;
    }
}

static void htmlsrc_colorize_line(QEColorizeContext *cp,
                                  unsigned int *str, int n, ModeDef *syn)
{
    int i = 0, start, c, len;
    int state = cp->colorize_state;

    while (i < n) {
        start = i;

        if (state & IN_HTML_PHP) {
            for (; i < n; i++) {
                if (str[i] == '?' && str[i + 1] == '>')
                    break;
            }
            c = str[i];     /* save char to set '\0' delimiter */
            str[i] = '\0';
            cp->colorize_state = state & IN_HTML_EMBEDDED;
            php_mode.colorize_func(cp, str + start, i - start, &php_mode);
            state &= ~IN_HTML_EMBEDDED;
            state |= cp->colorize_state & IN_HTML_EMBEDDED;
            str[i] = c;
            if (c) {
                state &= ~(IN_HTML_EMBEDDED | IN_HTML_PHP);
                start = i;
                i += 2;
                SET_COLOR(str, start, i, HTML_STYLE_PREPROCESS);
            }
            continue;
        }
        if (state & IN_HTML_ASP) {
            for (; i < n; i++) {
                if (str[i] == '%' && str[i + 1] == '>')
                    break;
            }
            c = str[i];     /* save char to set '\0' delimiter */
            str[i] = '\0';
            cp->colorize_state = state & IN_HTML_EMBEDDED;
            csharp_mode.colorize_func(cp, str + start, i - start, &csharp_mode);
            state &= ~IN_HTML_EMBEDDED;
            state |= cp->colorize_state & IN_HTML_EMBEDDED;
            str[i] = c;
            if (c) {
                state &= ~(IN_HTML_EMBEDDED | IN_HTML_ASP);
                start = i;
                i += 2;
                SET_COLOR(str, start, i, HTML_STYLE_PREPROCESS);
            }
            continue;
        }
        if ((state & (IN_HTML_SCRIPT | IN_HTML_TAG)) == IN_HTML_SCRIPT) {
            for (; i < n; i++) {
                if (str[i] == '<'
                &&  htmlsrc_tag_match(str, i + 1, "/script", NULL)) {
                    break;
                }
            }
            c = str[i];     /* save char to set '\0' delimiter */
            str[i] = '\0';
            cp->colorize_state = state & IN_HTML_EMBEDDED;
            js_mode.colorize_func(cp, str + start, i - start, &js_mode);
            state &= ~IN_HTML_EMBEDDED;
            state |= cp->colorize_state & IN_HTML_EMBEDDED;
            str[i] = c;
            if (c) {
                state &= ~(IN_HTML_EMBEDDED | IN_HTML_SCRIPT);
            }
            continue;
        }
        if ((state & (IN_HTML_STYLE | IN_HTML_TAG)) == IN_HTML_STYLE) {
            for (; i < n; i++) {
                if (str[i] == '<'
                &&  htmlsrc_tag_match(str, i + 1, "/style", NULL)) {
                    break;
                }
            }
            c = str[i];     /* save char to set '\0' delimiter */
            str[i] = '\0';
            cp->colorize_state = state & IN_HTML_EMBEDDED;
            css_mode.colorize_func(cp, str + start, i - start, &css_mode);
            state &= ~IN_HTML_EMBEDDED;
            state |= cp->colorize_state & IN_HTML_EMBEDDED;
            str[i] = c;
            if (c) {
                state &= ~(IN_HTML_EMBEDDED | IN_HTML_STYLE);
            }
            continue;
        }
        if (state & IN_HTML_COMMENT) {
            for (; i < n; i++) {
                if (str[i] == '-' && str[i + 1] == '-' && str[i + 2] == '>') {
                    i += 3;
                    state &= ~(IN_HTML_COMMENT | IN_HTML_COMMENT1);
                    break;
                }
            }
            SET_COLOR(str, start, i, HTML_STYLE_COMMENT);
            continue;
        }
        if (state & IN_HTML_COMMENT1) {
            for (; i < n; i++) {
                if (str[i] == '>') {
                    i++;
                    state &= ~IN_HTML_COMMENT1;
                    break;
                }
            }
            SET_COLOR(str, start, i, HTML_STYLE_COMMENT1);
            continue;
        }
        if (state & IN_HTML_ENTITY) {
            if ((len = get_html_entity(str + i)) == 0)
                i++;
            else
                i += len;
            state &= ~IN_HTML_ENTITY;
            SET_COLOR(str, start, i, HTML_STYLE_ENTITY);
            continue;
        }
        if (state & (IN_HTML_STRING | IN_HTML_STRING1)) {
            int delim = (state & IN_HTML_STRING1) ? '\'' : '\"';

            for (; i < n; i++) {
                if (str[i] == '&' && get_html_entity(str + i)) {
                    state |= IN_HTML_ENTITY;
                    break;
                }
                if (str[i] == (unsigned int)delim) {
                    i++;
                    state &= ~(IN_HTML_STRING | IN_HTML_STRING1);
                    break;
                }
                if (str[i] == '<'
                &&  htmlsrc_tag_match(str, i, "<?php", NULL)) {
                    SET_COLOR(str, start, i, HTML_STYLE_STRING);
                    SET_COLOR(str, i, i + 5, HTML_STYLE_PREPROCESS);
                    i += 5;
                    start = i;
                    state |= IN_HTML_PHP;
                    break;
                } else
                if (str[i] == '<' && str[i + 1] == '%') {
                    SET_COLOR(str, start, i, HTML_STYLE_STRING);
                    SET_COLOR(str, i, i + 2, HTML_STYLE_PREPROCESS);
                    i += 2;
                    start = i;
                    state |= IN_HTML_ASP;
                    break;
                } else
                if (str[i] == '?' && str[i + 1] == '>') {
                    /* special case embedded php script tags */
                    i += 1;
                } else
                if (str[i] == '%' && str[i + 1] == '>') {
                    /* special case embedded asp script tags */
                    i += 1;
                } else
                if (str[i] == '>') {
                    /* Premature end of string */
                    state &= ~(IN_HTML_STRING | IN_HTML_STRING1);
                    break;
                }
            }
            SET_COLOR(str, start, i, HTML_STYLE_STRING);
            continue;
        }
        if (state & IN_HTML_TAG) {
            for (; i < n; i++) {
                c = str[i];
                if (c == '&' && get_html_entity(str + i)) {
                    state |= IN_HTML_ENTITY;
                    break;
                }
                if (c == '\"') {
                    state |= IN_HTML_STRING;
                    break;
                }
                if (c == '\'') {
                    state |= IN_HTML_STRING1;
                    break;
                }
                if (c == '/' && str[i + 1] == '>') {
                    i += 2;
                    state = 0;
                    break;
                }
                if (c == '>') {
                    i++;
                    state &= ~IN_HTML_TAG;
                    break;
                }
            }
            SET_COLOR(str, start, i, HTML_STYLE_TAG);
            if (state & (IN_HTML_STRING | IN_HTML_STRING1)) {
                SET_COLOR1(str, i, HTML_STYLE_STRING);
                i++;
            }
            continue;
        }
        /* Plain text stream */
        for (; i < n; i++) {
            start = i;
            c = str[i];

            /* Kludge for preprocessed html */
            if (c == '#' && i == 0) {
                i = n;
                SET_COLOR(str, start, i, HTML_STYLE_PREPROCESS);
                break;
            }
            if (c == '<'
            &&  htmlsrc_tag_match(str, i, "<?php", &i)) {
                SET_COLOR(str, start, i, HTML_STYLE_PREPROCESS);
                state |= IN_HTML_PHP;
                break;
            }
            if (c == '<' && str[i + 1] == '%') {
                i += 2;
                SET_COLOR(str, start, i, HTML_STYLE_PREPROCESS);
                state |= IN_HTML_ASP;
                break;
            }
            if (c == '<'
            &&  (qe_isalpha(str[i + 1]) || str[i + 1] == '!'
            ||   str[i + 1] == '/' || str[i + 1] == '?')) {
                state |= IN_HTML_TAG;
                if (htmlsrc_tag_match(str, i, "<script", NULL)) {
                    state |= IN_HTML_SCRIPT;
                    break;
                }
                if (htmlsrc_tag_match(str, i, "<style", NULL)) {
                    state |= IN_HTML_STYLE;
                    break;
                }
                if (str[i + 1] == '!') {
                    state ^= IN_HTML_TAG ^ IN_HTML_COMMENT1;
                    i += 2;
                    if (str[i] == '-' && str[i + 1] == '-') {
                        i += 2;
                        state ^= IN_HTML_COMMENT ^ IN_HTML_COMMENT1;
                    }
                    SET_COLOR(str, start, i, HTML_STYLE_COMMENT);
                }
                break;
            }
            if (str[i] == '&' && get_html_entity(str + i)) {
                state |= IN_HTML_ENTITY;
                break;
            }
        }
    }
    cp->colorize_state = state;
}

static int html_tagcmp(const char *s1, const char *s2)
{
    int d;

    while (*s2) {
        d = *s2 - qe_toupper(*s1);
        if (d)
            return d;
        s2++;
        s1++;
    }
    if (qe_isalnum_(*s1))
        return -1;
    return 0;
}

static int htmlsrc_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    const char *buf = cs8(p->buf);

    /* first check file extension */
    if (match_extension(p->filename, mode->extensions))
        return 85;

    /* then try buffer contents */
    if (buf[0] == '<'
    &&  (!html_tagcmp(buf, "<HTML") ||
         !html_tagcmp(buf, "<SCRIPT") ||
         !html_tagcmp(buf, "<?XML") ||
         !html_tagcmp(buf, "<PLIST") ||
         !html_tagcmp(buf, "<!DOCTYPE"))) {
        return 85;
    }

    return 1;
}

/* specific htmlsrc commands */
/* CG: need move / kill by tag level */
static CmdDef htmlsrc_commands[] = {
    CMD_DEF_END,
};

ModeDef htmlsrc_mode = {
    .name = "html-src",
    .extensions = "html|htm|asp|aspx|shtml|hta|htp|phtml|"
                  "php|php3|php4|xml|eex|plist",
    .mode_probe = htmlsrc_mode_probe,
    .colorize_func = htmlsrc_colorize_line,
};

static int htmlsrc_init(void)
{
    qe_register_mode(&htmlsrc_mode, MODEF_SYNTAX);
    qe_register_cmd_table(htmlsrc_commands, &htmlsrc_mode);

    return 0;
}

qe_module_init(htmlsrc_init);
