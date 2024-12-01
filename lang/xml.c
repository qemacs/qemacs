/*
 * XML text mode for QEmacs.
 *
 * Copyright (c) 2002 Fabrice Bellard.
 * Copyright (c) 2014-2024 Charlie Gordon.
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

/* colorization states */
enum {
    IN_XML_TAG = 1,
    IN_XML_COMMENT,
    IN_XML_TAG_SCRIPT,
    IN_XML_TAG_STYLE,
    IN_XML_SCRIPT = 0x80, /* Inside a script tag, ored with c-mode state */
    IN_XML_STYLE = 0x100, /* Inside a style tag, ored with c-mode state */
};

enum {
    XML_STYLE_COMMENT = QE_STYLE_COMMENT,
    XML_STYLE_TAG     = QE_STYLE_TAG,
    XML_STYLE_CSS     = QE_STYLE_CSS,
};

static int xml_tag_match(const char32_t *buf, int i, const char *str,
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

static void xml_colorize_line(QEColorizeContext *cp,
                              const char32_t *str, int n,
                              QETermStyle *sbuf, ModeDef *syn)
{
    int i = 0, start = i;
    char32_t c;
    int state = cp->colorize_state;

    /* XXX: should recognize and colorize entities, attribute strings */

    /* if already in a state, go directly in the code parsing it */
    if (state & IN_XML_SCRIPT)
        goto parse_script;
    if (state & IN_XML_STYLE)
        goto parse_style;

    switch (state) {
    case IN_XML_COMMENT:
        goto parse_comment;
    case IN_XML_TAG:
    case IN_XML_TAG_SCRIPT:
    case IN_XML_TAG_STYLE:
        goto parse_tag;
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
            parse_comment:
                /* scan for end of comment */
                for (; str[i] != '\0'; i++) {
                    if (str[i] == '-' && str[i + 1] == '-'
                    &&  str[i + 2] == '>') {
                        i += 3;
                        state = 0;
                        break;
                    }
                }
                SET_STYLE(sbuf, start, i, XML_STYLE_COMMENT);
            } else {
                /* we are in a tag */
                if (xml_tag_match(str, i, "script", &i)) {
                    state = IN_XML_TAG_SCRIPT;
                } else
                if (xml_tag_match(str, i, "style", &i)) {
                    state = IN_XML_TAG_STYLE;
                }
            parse_tag:
                while (str[i] != '\0') {
                    c = str[i++];
                    if (c == '/' && str[i] == '>') {
                        i++;
                        state = 0;
                        break;
                    }
                    if (c == '>') {
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
                SET_STYLE(sbuf, start, i, XML_STYLE_TAG);
                start = i;
                if (state & IN_XML_SCRIPT) {
                    /* javascript coloring */
                    /* XXX: should support actual scripting language if supported */
                parse_script:
                    for (; str[i] != '\0'; i++) {
                        if (str[i] == '<'
                        &&  xml_tag_match(str, i + 1, "/script", NULL)) {
                            break;
                        }
                    }
                    cp->colorize_state = state & ~IN_XML_SCRIPT;
                    cp_colorize_line(cp, str, start, i, sbuf, &js_mode);
                    state = cp->colorize_state | IN_XML_SCRIPT;
                    if (str[i]) {
                        /* found </script> tag */
                        state = 0;
                    }
                    continue;
                } else
                if (state & IN_XML_STYLE) {
                    /* stylesheet coloring */
                parse_style:
                    for (; str[i] != '\0'; i++) {
                        if (str[i] == '<'
                        &&  xml_tag_match(str, i + 1, "/style", NULL)) {
                            break;
                        }
                    }
                    cp->colorize_state = state & ~IN_XML_STYLE;
                    cp_colorize_line(cp, str, start, i, sbuf, &css_mode);
                    state = cp->colorize_state | IN_XML_STYLE;
                    if (str[i]) {
                        /* found </style> tag */
                        state = 0;
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

    return 60; /* leave some room for more specific XML parser */
}

ModeDef xml_mode = {
    .name = "xml",
    .mode_probe = xml_mode_probe,
    .colorize_func = xml_colorize_line,
};

static int xml_init(QEmacsState *qs)
{
    qe_register_mode(qs, &xml_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(xml_init);
