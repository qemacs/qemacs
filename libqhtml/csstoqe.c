/*
 * Convert a CSS style sheet to C buffer so that it can be statically
 * linked with qemacs
 *
 * Copyright (c) 2002 Fabrice Bellard.
 * Copyright (c) 2007-2008 Charlie Gordon.
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

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

static int peekc(FILE *f)
{
    int c = getc(f);
    if (c != EOF)
        ungetc(c, f);
    return c;
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
           "#include \"qe.h\"\n"
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
        if (!in_string && got_space && isalnum(c) && isalnum(last_c)) {
            putchar(' ');
            n++;
        }
        if (c == '\"' || c == '\'' || c == '\\') {
            putchar('\\');
            n++;
        }
        putchar(c);
        if (c == '\"') {
            /* CG: does not work for ' ' */
            in_string ^= 1;
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
