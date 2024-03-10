/*
 * C mode definitions for QEmacs.
 *
 * Copyright (c) 2002-2024 Charlie Gordon.
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
    CLANG_BEE,
    CLANG_V8,
    CLANG_FLAVOR = 0x3F,
};

int get_c_identifier(char *buf, int buf_size, const char32_t *p, int flavor);
void c_indent_line(EditState *s, int offset0);

#endif /* CLANG_H */
