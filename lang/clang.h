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
    CLANG_PPL,
    CLANG_JAKT,
    CLANG_C3,
    CLANG_SILK,
    CLANG_FLAVOR = 0x7F,
};

/* C mode options */
#define CLANG_C_TYPES     0x00080   /* add C types to syn->types list */
#define CLANG_C_KEYWORDS  0x00100   /* add C keywords to syn->keywords list */
#define CLANG_LEX         0x00200
#define CLANG_YACC        0x00400
#define CLANG_REGEX       0x00800   /* recognize / delimited regular expressions */
#define CLANG_WLITERALS   0x01000   /* recognize L, u, U, u8 string prefix */
#define CLANG_PREPROC     0x02000   /* colorize preprocessing directives */
#define CLANG_CAP_TYPE    0x04000   /* Mixed case initial cap is type */
#define CLANG_STR3        0x08000   /* Support """strings""" */
#define CLANG_LINECONT    0x10000   /* support \<newline> as line continuation */
#define CLANG_NEST_COMMENTS  0x20000  /* block comments are nested */
#define CLANG_T_TYPES     0x40000   /* _t suffix indicates type identifier */
#define CLANG_C_DEFS      0x80000   /* word followed by another word is a definition */

/* FIXME: need flags for
   '@' in identifiers(start / next),
   '$' in identifiers(start / next),
   '-' in identifiers next
   '#' as comment char
   UnicodeIDStart and UnicodeIDContinue in identifiers
 */

/* all C language features */
#define CLANG_CC  (CLANG_LINECONT | CLANG_WLITERALS | CLANG_PREPROC | \
                   CLANG_C_KEYWORDS | CLANG_C_TYPES | CLANG_T_TYPES | \
                   CLANG_C_DEFS)

int is_c_identifier_start(char32_t c, int flavor);
int is_c_identifier_part(char32_t c, int flavor);
int get_c_identifier(char *dest, int size, char32_t c,
                     const char32_t *str, int i, int n, int flavor);
int has_c_decl(const char32_t *str, int i, int flavor);
int is_cap_type(const char *str);
void c_colorize_line(QEColorizeContext *cp,
                     const char32_t *str, int n,
                     QETermStyle *sbuf, ModeDef *syn);
void c_indent_line(EditState *s, int offset0);

#endif /* CLANG_H */
