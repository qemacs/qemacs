/*
 * WYSIWYG Docbook mode for QEmacs.
 *
 * Copyright (c) 2002 Fabrice Bellard.
 * Copyright (c) 2003-2017 Charlie Gordon.
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

static int docbook_init(void)
{
    /* inherit from html mode */
    memcpy(&docbook_mode, &html_mode, sizeof(ModeDef));
    docbook_mode.name = "docbook";
    docbook_mode.extensions = NULL;
    /* buffer_instance_size must be non 0 for docbook_mode_init() to
       receive MODEF_NEWINSTANCE flag */
    docbook_mode.buffer_instance_size = sizeof(QEModeData);
    docbook_mode.mode_probe = docbook_mode_probe;
    docbook_mode.mode_init = docbook_mode_init;

    qe_register_mode(&docbook_mode, MODEF_VIEW);
    return 0;
}

qe_module_init(docbook_init);
