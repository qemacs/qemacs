/*
 * XML text mode for QEmacs.
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
    XML_TAG = 1,
    XML_COMMENT, 
    XML_TAG_SCRIPT,
    XML_TAG_STYLE,
    XML_STYLE,
    XML_SCRIPT = 0x10, /* special mode for inside a script, ored with c mode */
};

void xml_colorize_line(unsigned int *buf, int len, 
                       int *colorize_state_ptr, int state_only)
{
    int c, state;
    unsigned int *p, *p_start, *p1;
    
    state = *colorize_state_ptr;
    p = buf;
    p_start = p;

    /* if already in a state, go directly in the code parsing it */
    if (state & XML_SCRIPT)
        goto parse_script;
    switch(state) {
    case XML_COMMENT:
        goto parse_comment;
    case XML_TAG:
    case XML_TAG_SCRIPT:
        goto parse_tag;
    case XML_STYLE:
        goto parse_style;
    default:
        break;
    }

    for(;;) {
        p_start = p;
        c = *p;
        if (c == '\n') {
            goto the_end;
        } else if (c == '<' && state == 0) {
            p++;
            if (p[0] == '!' && p[1] == '-' && p[2] == '-') {
                p += 3;
                state = XML_COMMENT;
                /* wait until end of comment */
            parse_comment:
                while (*p != '\n') {
                    if (p[0] == '-' && p[1] == '-' && p[2] == '>') {
                        p += 3;
                        state = 0;
                        break;
                    } else {
                        p++;
                    }
                }
                set_color(p_start, p - p_start, QE_STYLE_COMMENT);
            } else {
                /* we are in a tag */
                if (ustristart(p, "SCRIPT", (const unsigned int **)&p)) {
                    state = XML_TAG_SCRIPT;
                } else if (ustristart(p, "STYLE", (const unsigned int **)&p)) {
                    state = XML_TAG_STYLE;
                }
            parse_tag:
                while (*p != '\n') {
                    if (*p == '>') {
                        p++;
                        if (state == XML_TAG_SCRIPT)
                            state = XML_SCRIPT;
                        else if (state == XML_TAG_STYLE)
                            state = XML_STYLE;
                        else
                            state = 0;
                        break;
                    } else {
                        p++;
                    }
                }
                set_color(p_start, p - p_start, QE_STYLE_TAG);
                if (state == XML_SCRIPT) {
                    /* javascript coloring */
                    p_start = p;
                parse_script:
                    for(;;) {
                        if (*p == '\n') {
                            state &= ~XML_SCRIPT;
                            c_colorize_line(p_start, p - p_start, &state, state_only);
                            state |= XML_SCRIPT;
                            break;
                        } else if (ustristart(p, "</SCRIPT", (const unsigned int **)&p1)) {
                            while (*p1 != '\n' && *p1 != '>') 
                                p1++;
                            if (*p1 == '>')
                                p1++;
                            /* XXX: need to add '\n' */
                            state &= ~XML_SCRIPT;
                            c_colorize_line(p_start, p - p_start, &state, state_only);
                            state |= XML_SCRIPT;
                            set_color(p, p1 - p, QE_STYLE_TAG);
                            p = p1;
                            state = 0;
                            break;
                        } else {
                            p++;
                        }
                    }
                } else if (state == XML_STYLE) {
                    /* stylesheet coloring */
                    p_start = p;
                parse_style:
                    for(;;) {
                        if (*p == '\n') {
                            set_color(p_start, p - p_start, QE_STYLE_CSS);
                            break;
                        } else if (ustristart(p, "</STYLE", (const unsigned int **)&p1)) {
                            while (*p1 != '\n' && *p1 != '>') 
                                p1++;
                            if (*p1 == '>')
                                p1++;
                            set_color(p_start, p - p_start, QE_STYLE_CSS);
                            set_color(p, p1 - p, QE_STYLE_TAG);
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
 the_end:
    *colorize_state_ptr = state;
}

int xml_mode_probe(ModeProbeData *p1)
{
    const char *p;

    p = p1->buf;
    while (css_is_space(*p))
        p++;
    if (*p != '<')
        return 0;
    p++;
    if (*p != '!' && *p != '?' && *p != '/' && 
        !isalpha(*p))
        return 0;
    return 90; /* leave some room for more specific XML parser */
}

int xml_mode_init(EditState *s, ModeSavedData *saved_data)
{
    int ret;
    ret = text_mode_init(s, saved_data);
    if (ret)
        return ret;
    set_colorize_func(s, xml_colorize_line);
    return 0;
}

static ModeDef xml_mode;

int xml_init(void)
{
    /* c mode is almost like the text mode, so we copy and patch it */
    memcpy(&xml_mode, &text_mode, sizeof(ModeDef));
    xml_mode.name = "xml";
    xml_mode.mode_probe = xml_mode_probe;
    xml_mode.mode_init = xml_mode_init;

    qe_register_mode(&xml_mode);

    return 0;
}

qe_module_init(xml_init);
