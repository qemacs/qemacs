/*
 * WYSIWYG Docbook ode for QEmacs.
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
#include "css.h"

extern const char docbook_style[];

static int docbook_mode_probe(ModeProbeData *p1)
{
    if (xml_mode_probe(p1) == 0)
        return 0;
    /* well, very crude, but it may work OK */
    if (strstr(p1->buf, "DocBook"))
        return 100;
    return 0;
}

static int docbook_mode_init(EditState *s, ModeSavedData *saved_data)
{
    return gxml_mode_init(s, saved_data, XML_IGNORE_CASE | XML_DOCBOOK, docbook_style);
}

ModeDef docbook_mode;

static int docbook_init(void)
{
    /* inherit from html mode */
    memcpy(&docbook_mode, &html_mode, sizeof(ModeDef));
    docbook_mode.name = "docbook";
    docbook_mode.mode_probe = docbook_mode_probe;
    docbook_mode.mode_init = docbook_mode_init;

    qe_register_mode(&docbook_mode);
    return 0;
}

qe_module_init(docbook_init);
