/*
 * Convert a CSS style sheet to C buffer so that it can be statically
 * linked with qemacs
 *
 * Copyright (c) 2002 Fabrice Bellard.
 * Copyright (c) 2007-2025 Charlie Gordon.
 *
 * This utility generates C source code for a string containing the minimized
 * version of the css input file for embedding in a C application.
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

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static int peekc(FILE *f)
{
    int c = getc(f);
    if (c != EOF)
        ungetc(c, f);
    return c;
}

static int compat_char(int c1, int c2)
{
    if ((isalnum(c1) || c1 == '_' || c1 == '$')
    &&  (isalnum(c2) || c2 == '_' || c2 == '$'))
        return 0;

    if ((c1 == c2 && strchr("+-<>&|=", c1))
    ||  (c2 == '=' && strchr("<>!+-*/&|^%", c1))
    ||  (c1 == '-' && c2 == '>')
    ||  (c1 == '/' && c2 == '/')
    ||  (c1 == '/' && c2 == '*')
    ||  (c1 == '*' && c2 == '/')
    ||  (c1 == '<' && c2 == '/')
    ||  (c1 == '.' && isdigit(c2))
    ||  (isdigit(c1) && c2 == '.'))
        return 0;

    return 1;
}

int main(int argc, char **argv)
{
    int c, n, last_c, got_space, in_string;

    if (argc < 2) {
        fprintf(stderr, "usage: csstoqe array_name\n");
        exit(1);
    }

    printf("/* Automatically generated file - do not edit */\n"
           "\n"
           "#include \"css.h\"\n"
           "\n");
    printf("const char %s[] = {\n", argv[1]);
    n = 0;
    got_space = 0;
    last_c = 0;
    in_string = 0;
    for (;;) {
        c = getchar();
        if (c == EOF)
            break;
        if (!in_string) {
            if (c == ' ' || c == '\t' || c == '\n') {
                got_space = 1;
                continue;
            }
            /* comments */
            if (c == '/' && peekc(stdin) == '/') {
                /* C++ like comment */
                for (;;) {
                    c = getchar();
                    if (c == EOF)
                        goto the_end;
                    if (c == '\n')
                        goto end_comment;
                }
            }
            if (c == '/' && peekc(stdin) == '*') {
                /* C like comment */
                getchar();
                for (;;) {
                    c = getchar();
                    if (c == EOF)
                        goto the_end;
                    while (c == '*') {
                        c = getchar();
                        if (c == EOF)
                            goto the_end;
                        if (c == '/')
                            goto end_comment;
                    }
                }
            end_comment:
                got_space = 1;
                continue;
            }
        }
        if (n == 0) {
            printf("    \"");
        }
        /* add separator if needed */
        if (!in_string && got_space) {
            if (!compat_char(last_c, c)) {
                putchar(' ');
                n++;
            }
        }
        if (c == '\"' || c == '\'' || c == '\\') {
            putchar('\\');
            n++;
        }
        putchar(c);
        if (c == '\"' || c == '\'') {
            if (in_string == c || in_string == 0)
                in_string ^= c;
        }
        last_c = c;
        got_space = 0;
        if (++n >= 64) {
            printf("\"\n");
            n = 0;
        }
    }
 the_end:
    if (n > 0)
        printf("\"\n");
    printf("};\n\n");
    return 0;
}
