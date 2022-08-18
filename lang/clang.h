/*
 * C mode definitions for QEmacs.
 *
 * Copyright (c) 2002-2022 Charlie Gordon.
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

#ifndef CLANG_H
#define CLANG_H

/* C mode flavors */
enum {
    CLANG_C,
    CLANG_CPP,
    CLANG_C2,
    CLANG_OBJC,
    CLANG_CSHARP,
    CLANG_AWK,
    CLANG_CSS,
    CLANG_JSON,
    CLANG_JS,
    CLANG_TS,
    CLANG_JSPP,
    CLANG_KOKA,
    CLANG_AS,
    CLANG_JAVA,
    CLANG_SCALA,
    CLANG_PHP,
    CLANG_GO,
    CLANG_D,
    CLANG_LIMBO,
    CLANG_CYCLONE,
    CLANG_CH,
    CLANG_SQUIRREL,
    CLANG_ICI,
    CLANG_JSX,
    CLANG_HAXE,
    CLANG_DART,
    CLANG_PIKE,
    CLANG_IDL,
    CLANG_CALC,
    CLANG_ENSCRIPT,
    CLANG_QSCRIPT,
    CLANG_ELASTIC,
    CLANG_JED,
    CLANG_CSL,
    CLANG_NEKO,
    CLANG_NML,
    CLANG_ALLOY,
    CLANG_SCILAB,
    CLANG_KOTLIN,
    CLANG_CBANG,
    CLANG_VALA,
    CLANG_PAWN,
    CLANG_CMINUS,
    CLANG_GMSCRIPT,
    CLANG_WREN,
    CLANG_JACK,
    CLANG_SMAC,
    CLANG_RUST,
    CLANG_SWIFT,
    CLANG_ICON,
    CLANG_GROOVY,
    CLANG_VIRGIL,
    CLANG_V,
    CLANG_PROTOBUF,
    CLANG_ODIN,
    CLANG_SALMON,
    CLANG_CARBON,
    CLANG_FLAVOR = 0x3F,
};

int get_c_identifier(char *buf, int buf_size, const unsigned int *p, int flavor);
void c_indent_line(EditState *s, int offset0);

#endif /* CLANG_H */
