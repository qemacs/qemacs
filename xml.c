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

static void xml_colorize_line(QEColorizeContext *cp,
                              unsigned int *str, int n, int mode_flags)
{
    int i = 0, i1, start, c, state;
    const unsigned int *p;

    state = cp->colorize_state;

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
        start = i;
        c = str[i++];

        if (c == '\0')
            break;

        if (c == '<' && state == 0) {
            if (str[i] == '!' && str[i + 1] == '-' && str[i + 2] == '-') {
                i += 3;
                state = IN_XML_COMMENT;
                /* wait until end of comment */
            parse_comment:
                while (str[i] != '\0') {
                    if (str[i] == '-' && str[i + 1] == '-'
                    &&  str[i + 2] == '>') {
                        i += 3;
                        state = 0;
                        break;
                    } else {
                        i++;
                    }
                }
                SET_COLOR(str, start, i, XML_STYLE_COMMENT);
            } else {
                /* we are in a tag */
                if (ustristart(str + i, "SCRIPT", &p)) {
                    state = IN_XML_TAG_SCRIPT;
                } else
                if (ustristart(str + i, "STYLE", &p)) {
                    state = IN_XML_TAG_STYLE;
                }
            parse_tag:
                /* XXX: bogus for <style src="toto" /> */
                while (str[i] != '\0') {
                    if (str[i++] == '>') {
                        if (state == IN_XML_TAG_SCRIPT)
                            state = IN_XML_SCRIPT;
                        else
                        if (state == IN_XML_TAG_STYLE)
                            state = IN_XML_STYLE;
                        else
                            state = 0;
                        break;
                    }
                }
                SET_COLOR(str, start, i, XML_STYLE_TAG);
                if (state == IN_XML_SCRIPT) {
                    /* javascript coloring */
                    start = i;
                parse_script:
                    for (;; i++) {
                        if (str[i] == '\0') {
                            state &= ~IN_XML_SCRIPT;
                            cp->colorize_state = state;
                            /* XXX: should have js_colorize_func */
                            c_colorize_line(cp, str + start, i - start,
                                            CLANG_JS | CLANG_REGEX);
                            state = cp->colorize_state;
                            state |= IN_XML_SCRIPT;
                            break;
                        } else
                        if (ustristart(str + i, "</SCRIPT", &p)) {
                            i1 = p - str;
                            /* XXX: bogus for </script LF > */
                            while (str[i1] != '\0') {
                                if (str[i1++] == '>')
                                    break;
                            }
                            c = str[i];
                            str[i] = '\0';
                            state &= ~IN_XML_SCRIPT;
                            cp->colorize_state = state;
                            /* XXX: should have js_colorize_func */
                            c_colorize_line(cp, str + start, i - start,
                                            CLANG_JS | CLANG_REGEX);
                            str[i] = c;
                            state = 0;
                            start = i;
                            i = i1;
                            SET_COLOR(str, start, i, XML_STYLE_TAG);
                            break;
                        }
                    }
                } else
                if (state == IN_XML_STYLE) {
                    /* stylesheet coloring */
                    start = i;
                parse_style:
                    for (;; i++) {
                        if (str[i] == '\0') {
                            /* XXX: should use css_colorize_line */
                            SET_COLOR(str, start, i, XML_STYLE_CSS);
                            break;
                        } else
                        if (ustristart(str + i, "</STYLE", &p)) {
                            /* XXX: bogus for </style LF > */
                            i1 = p - str;
                            while (str[i1] != '\0') {
                                if (str[i1++] != '>')
                                    break;
                            }
                            /* XXX: should use css_colorize_line */
                            SET_COLOR(str, start, i, XML_STYLE_CSS);
                            SET_COLOR(str, i, i1, XML_STYLE_TAG);
                            i = i1;
                            state = 0;
                            break;
                        }
                    }
                }
            }
        }
    }
    cp->colorize_state = state;
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
