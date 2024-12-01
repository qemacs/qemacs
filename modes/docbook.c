/*
 * WYSIWYG Docbook mode for QEmacs.
 *
 * Copyright (c) 2002 Fabrice Bellard.
 * Copyright (c) 2003-2024 Charlie Gordon.
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
#include "css.h"

static int docbook_mode_probe(ModeDef *mode, ModeProbeData *p1)
{
    if (xml_mode.mode_probe(&xml_mode, p1) == 0)
        return 0;

    /* well, very crude, but it may work OK */
    if (strstr(cs8(p1->buf), "DocBook"))
        return 85;
    return 0;
}

static int docbook_mode_init(EditState *s, EditBuffer *b, int flags)
{
    if (flags & MODEF_NEWINSTANCE) {
        /* Implement mode inheritance manually */
        qe_create_buffer_mode_data(b, &html_mode);
        return gxml_mode_init(b, XML_IGNORE_CASE | XML_DOCBOOK, docbook_style);
    }
    return 0;
}

static ModeDef docbook_mode;

static int docbook_init(QEmacsState *qs)
{
    /* inherit from html mode */
    // XXX: remove this mess: should just inherit with fallback
    memcpy(&docbook_mode, &html_mode, offsetof(ModeDef, first_key));
    docbook_mode.fallback = &html_mode;
    docbook_mode.name = "docbook";
    docbook_mode.extensions = NULL;
    /* buffer_instance_size must be non 0 for docbook_mode_init() to
       receive MODEF_NEWINSTANCE flag */
    docbook_mode.buffer_instance_size = sizeof(QEModeData);
    docbook_mode.mode_probe = docbook_mode_probe;
    docbook_mode.mode_init = docbook_mode_init;

    qe_register_mode(qs, &docbook_mode, MODEF_VIEW);
    return 0;
}

qe_module_init(docbook_init);
