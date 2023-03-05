/*
 * Convert FBF fonts into source code to link them in QEmacs.
 *
 * Copyright (c) 2002 Fabrice Bellard.
 * Copyright (c) 2007-2023 Charlie Gordon.
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cutils.h"

static int dump_font(const char *filename, const char *name)
{
    int c;
    FILE *f;
    int j;

    f = fopen(filename, "rb");
    if (!f) {
        perror(filename);
        exit(1);
    }
    printf("static unsigned char const font_%s[] = {\n", name);
    j = 0;
    for (;;) {
        c = getc(f);
        if (c == EOF)
            break;
        if ((j & 7) == 0)
            printf("   ");
        j++;
        printf(" 0x%02x,", c);
        if ((j & 7) == 0)
            printf("\n");
    }
    if ((j & 7) != 0)
        printf("\n");
    printf("};\n\n");

    fclose(f);
    return j;
}

static void getname(char *name, int name_size, const char *filename)
{
    const char *p;
    char *q;

    p = strrchr(filename, '/');
    if (!p)
        p = filename;
    else
        p++;
    pstrcpy(name, name_size, p);
    q = strrchr(name, '.');
    if (q)
        *q = '\0';
}

#define FONT_MAX  128

int main(int argc, char **argv)
{
    const char *filename;
    char name[128];
    int font_size[FONT_MAX];
    int i;

    printf("/* This file was generated automatically by fbftoqe */\n\n"
           "#include \"qe.h\"\n"
           "#include \"fbfrender.h\"\n"
           "\n");

    for (i = 1; i < argc; i++) {
        filename = argv[i];
        getname(name, sizeof(name), filename);
        font_size[i] = dump_font(filename, name);
    }

    /* dump font list */
    printf("const struct fbf_font fbf_fonts[] = {\n");
    for (i = 1; i < argc; i++) {
        filename = argv[i];
        getname(name, sizeof(name), filename);
        printf("    { font_%s, %d },\n", name, font_size[i]);
    }
    printf("    { NULL, 0 },\n");
    printf("};\n");
    return 0;
}
