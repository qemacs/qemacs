/*
 * XML text mode for QEmacs.
 *
 * Copyright (c) 2002 Fabrice Bellard.
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

/* colorization states */
enum {
    IN_XML_TAG = 1,
    IN_XML_COMMENT,
    IN_XML_TAG_SCRIPT,
    IN_XML_TAG_STYLE,
    IN_XML_STYLE,
    IN_XML_SCRIPT = 0x80, /* special mode for inside a script, ored with c mode */
};

enum {
    XML_STYLE_COMMENT = QE_STYLE_COMMENT,
    XML_STYLE_TAG     = QE_STYLE_TAG,
    XML_STYLE_CSS     = QE_STYLE_CSS,
};

static void xml_colorize_line(unsigned int *buf, int len, int mode_flags,
                              int *colorize_state_ptr, int state_only)
{
    int c, state;
    unsigned int *p, *p_start, *p1;

    state = *colorize_state_ptr;
    p = buf;
    p_start = p;

    /* if already in a state, go directly in the code parsing it */
    if (state & IN_XML_SCRIPT)
        goto parse_script;
    switch (state) {
    case IN_XML_COMMENT:
        goto parse_comment;
    case IN_XML_TAG:
    case IN_XML_TAG_SCRIPT:
        goto parse_tag;
    case IN_XML_STYLE:
        goto parse_style;
    default:
        break;
    }

    for (;;) {
        p_start = p;
        c = *p;

        if (c == '\0')
            break;

        if (c == '<' && state == 0) {
            p++;
            if (p[0] == '!' && p[1] == '-' && p[2] == '-') {
                p += 3;
                state = IN_XML_COMMENT;
                /* wait until end of comment */
            parse_comment:
                while (*p != '\0') {
                    if (p[0] == '-' && p[1] == '-' && p[2] == '>') {
                        p += 3;
                        state = 0;
                        break;
                    } else {
                        p++;
                    }
                }
                set_color(p_start, p, XML_STYLE_COMMENT);
            } else {
                /* we are in a tag */
                if (ustristart(p, "SCRIPT", (const unsigned int **)&p)) {
                    state = IN_XML_TAG_SCRIPT;
                } else
                if (ustristart(p, "STYLE", (const unsigned int **)&p)) {
                    state = IN_XML_TAG_STYLE;
                }
            parse_tag:
                while (*p != '\0') {
                    if (*p == '>') {
                        p++;
                        if (state == IN_XML_TAG_SCRIPT)
                            state = IN_XML_SCRIPT;
                        else
                        if (state == IN_XML_TAG_STYLE)
                            state = IN_XML_STYLE;
                        else
                            state = 0;
                        break;
                    } else {
                        p++;
                    }
                }
                set_color(p_start, p, XML_STYLE_TAG);
                if (state == IN_XML_SCRIPT) {
                    /* javascript coloring */
                    p_start = p;
                parse_script:
                    for (;;) {
                        if (*p == '\0') {
                            state &= ~IN_XML_SCRIPT;
                            /* XXX: should have javascript specific colorize_func */
                            c_colorize_line(p_start, p - p_start,
                                CLANG_C | CLANG_REGEX, &state, state_only);
                            state |= IN_XML_SCRIPT;
                            break;
                        } else
                        if (ustristart(p, "</SCRIPT", (const unsigned int **)&p1)) {
                            while (*p1 != '\0') {
                                if (*p1++ == '>')
                                    break;
                            }
                            state &= ~IN_XML_SCRIPT;
                            c = *p;
                            *p = '\0';
                            /* XXX: should have javascript specific colorize_func */
                            c_colorize_line(p_start, p - p_start,
                                CLANG_C | CLANG_REGEX, &state, state_only);
                            *p = c;
                            state |= IN_XML_SCRIPT;
                            set_color(p, p1, XML_STYLE_TAG);
                            p = p1;
                            state = 0;
                            break;
                        } else {
                            p++;
                        }
                    }
                } else
                if (state == IN_XML_STYLE) {
                    /* stylesheet coloring */
                    p_start = p;
                parse_style:
                    for (;;) {
                        if (*p == '\0') {
                            set_color(p_start, p, XML_STYLE_CSS);
                            break;
                        } else
                        if (ustristart(p, "</STYLE", (const unsigned int **)&p1)) {
                            while (*p1 != '\0') {
                                if (*p1++ != '>')
                                    break;
                            }
                            set_color(p_start, p, XML_STYLE_CSS);
                            set_color(p, p1, XML_STYLE_TAG);
                            p = p1;
                            state = 0;
                            break;
                        } else {
                            p++;
                        }
                    }
                }
            }
        } else {
            /* text */
            p++;
        }
    }
    *colorize_state_ptr = state;
}

static int xml_mode_probe(ModeDef *mode, ModeProbeData *p1)
{
    const u8 *p;

    p = p1->buf;
    while (qe_isspace(*p))
        p++;
    if (*p != '<')
        return 0;
    p++;
    if (*p != '!' && *p != '?' && !qe_isalpha(*p))
        return 0;
    return 80; /* leave some room for more specific XML parser */
}

ModeDef xml_mode;

static int xml_init(void)
{
    /* xml mode is almost like the text mode, so we copy and patch it */
    memcpy(&xml_mode, &text_mode, sizeof(ModeDef));
    xml_mode.name = "xml";
    xml_mode.mode_probe = xml_mode_probe;
    xml_mode.colorize_func = xml_colorize_line;

    qe_register_mode(&xml_mode);

    return 0;
}

qe_module_init(xml_init);
