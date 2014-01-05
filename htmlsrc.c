/*
 * HTML Source mode for QEmacs.
 *
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

#include "qe.h"

static int get_html_entity(unsigned int *p)
{
    unsigned int *p_start, c;

    p_start = p;
    c = (u8)*p;

    if (c != '&')
        return 0;

    p++;
    c = (u8)*p;

    if (c == '#') {
        do {
            p++;
            c = (u8)*p;
        } while (qe_isdigit(c));
    } else
    if (qe_isalpha(c)) {
        do {
            p++;
            c = (u8)*p;
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

/* color colorization states */
enum {
    HTML_COMMENT   = 0x01,      /* <!-- <> --> */
    HTML_COMMENT1  = 0x02,      /* <! ... > */
    HTML_STRING    = 0x04,      /* " ... " */
    HTML_STRING1   = 0x08,      /* ' ... ' */
    HTML_TAG       = 0x10,      /* <tag ... > */
    HTML_ENTITY    = 0x20,      /* &name[;] / &#123[;] */
    HTML_SCRIPTTAG = 0x40,      /* <SCRIPT ...> */
    HTML_SCRIPT    = 0x80,      /* <SCRIPT> [...] </SCRIPT> */
};

/* CG: Should rely on len instead of '\n' */
static void htmlsrc_colorize_line(unsigned int *buf, int len,
                                  int *colorize_state_ptr, int state_only)
{
    int c, state, js_state, l;
    unsigned int *p, *p_start, *p_end;

    state = *colorize_state_ptr;
    p = buf;
    p_start = p;
    p_end = p + len;

    /* Kludge for preprocessed html */
    if (*p == '#') {
        p = p_end;
        set_color(p_start, p, QE_STYLE_PREPROCESS);
        goto the_end;
    }

    while (p < p_end) {
        p_start = p;
        c = *p;

        if (c == '\n')
            break;

        if (state & HTML_SCRIPTTAG) {
            while (p < p_end) {
                if (*p++ == '>') {
                    state = HTML_SCRIPT;
                    break;
                }
            }
            set_color(p_start, p, QE_STYLE_HTML_SCRIPT);
            continue;
        }
        if (state & HTML_SCRIPT) {
            for (; p < p_end; p++) {
                if (*p == '<' && ustristart(p, "</script>", NULL))
                    break;
            }
            js_state = state & ~HTML_SCRIPT;
            c_colorize_line (p_start, p - p_start,
                             &js_state, state_only);
            state = js_state | HTML_SCRIPT;
            if (p < p_end) {
                p_start = p;
                p += strlen("</script>");
                state = 0;
                set_color(p_start, p, QE_STYLE_HTML_SCRIPT);
            }
            continue;
        }
        if (state & HTML_COMMENT) {
            for (; p < p_end; p++) {
                if (*p == '-' && p[1] == '-'
                &&  p[2] == '>') {
                    p += 2;
                    state &= ~HTML_COMMENT;
                    break;
                }
            }
            set_color(p_start, p, QE_STYLE_HTML_COMMENT);
            continue;
        }
        if (state & HTML_COMMENT1) {
            for (; p < p_end; p++) {
                if (*p == '>') {
                    p++;
                    state &= ~HTML_COMMENT1;
                    break;
                }
            }
            set_color(p_start, p, QE_STYLE_HTML_COMMENT);
            continue;
        }
        if (state & HTML_ENTITY) {
            if ((l = get_html_entity(p)) == 0)
                p++;
            else
                p += l;
            state &= ~HTML_ENTITY;
            set_color(p_start, p, QE_STYLE_HTML_ENTITY);
            continue;
        }
        if (state & (HTML_STRING|HTML_STRING1)) {
            char delim = (char)((state & HTML_STRING1) ? '\'' : '\"');

            for (; p < p_end; p++) {
                if (*p == '&' && get_html_entity(p)) {
                    state |= HTML_ENTITY;
                    break;
                }
                if (*p == delim) {
                    p++;
                    state &= ~(HTML_STRING|HTML_STRING1);
                    break;
                }
                /* Premature end of string */
                if (*p == '>') {
                    state &= ~(HTML_STRING|HTML_STRING1);
                    break;
                }
            }
            set_color(p_start, p, QE_STYLE_HTML_STRING);
            continue;
        }
        if (state & HTML_TAG) {
            for (; p < p_end; p++) {
                if (*p == '&' && get_html_entity(p)) {
                    state |= HTML_ENTITY;
                    break;
                }
                if (*p == '\"') {
                    state |= HTML_STRING;
                    break;
                }
                if (*p == '\'') {
                    state |= HTML_STRING1;
                    break;
                }
                if (*p == '>') {
                    p++;
                    state &= ~HTML_TAG;
                    break;
                }
            }
            set_color(p_start, p, QE_STYLE_HTML_TAG);
            if (state & (HTML_STRING|HTML_STRING1)) {
                set_color1(p, QE_STYLE_HTML_STRING);
                p++;
            }
            continue;
        }
        /* Plain text stream */
        for (; p < p_end; p++) {
            if (*p == '<'
            &&  (qe_isalpha(p[1])
            ||   p[1] == '!' || p[1] == '/' || p[1] == '?')) {
                //set_color(p_start, p, QE_STYLE_HTML_TEXT);
                p_start = p;
                if (ustristart (p, "<script", NULL)) {
                    state |= HTML_SCRIPTTAG;
                    break;
                }
                if (p[1] == '!') {
                    state |= HTML_COMMENT1;
                    p += 2;
                    if (*p == '-' && p[1] == '-') {
                        p += 2;
                        state |= HTML_COMMENT;
                    }
                    set_color(p_start, p, QE_STYLE_HTML_COMMENT);
                    p_start = p;
                } else
                    state |= HTML_TAG;
                break;
            }
            if (*p == '&' && get_html_entity (p)) {
                state |= HTML_ENTITY;
                break;
            }
        }
        //set_color(p_start, p - p_start, QE_STYLE_HTML_TEXT);
    }
 the_end:
    *colorize_state_ptr = state;
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
    return 0;
}

static int htmlsrc_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    const char *buf = cs8(p->buf);

    /* first check file extension */
    if (match_extension(p->filename, mode->extensions))
        return 80;

    /* then try buffer contents */
    if (p->buf_size >= 5 &&
        (!html_tagcmp(buf, "<HTML") ||
         !html_tagcmp(buf, "<SCRIPT") ||
         !html_tagcmp(buf, "<?XML") ||
         !html_tagcmp(buf, "<!DOCTYPE"))) {
        return 80;
    }

    return 0;
}

/* specific htmlsrc commands */
/* CG: need move / kill by tag level */
static CmdDef htmlsrc_commands[] = {
    CMD_DEF_END,
};

static ModeDef htmlsrc_mode;

static int htmlsrc_init(void)
{
    /* html-src mode is almost like the text mode, so we copy and patch it */
    memcpy(&htmlsrc_mode, &text_mode, sizeof(ModeDef));
    htmlsrc_mode.name = "html-src";
    htmlsrc_mode.extensions = "html|htm|asp|shtml|hta|htp|phtml";
    htmlsrc_mode.mode_probe = htmlsrc_mode_probe;
    htmlsrc_mode.colorize_func = htmlsrc_colorize_line;

    qe_register_mode(&htmlsrc_mode);
    qe_register_cmd_table(htmlsrc_commands, &htmlsrc_mode);

    return 0;
}

qe_module_init(htmlsrc_init);
