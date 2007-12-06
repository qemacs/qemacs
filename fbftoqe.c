/*
 * Convert FBF fonts into source code to link them in QEmacs.
 *
 * Copyright (c) 2002 Fabrice Bellard.
 * Copyright (c) 2007 Charlie Gordon.
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
            printf("    ");
        j++;
        printf("0x%02x,", c);
        if ((j & 7) == 0)
            printf("\n");
        else
            printf(" ");
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
    printf("const void *fbf_fonts[] = {\n");
    for (i = 1; i < argc; i++) {
        filename = argv[i];
        getname(name, sizeof(name), filename);
        printf("    font_%s, (void *)%d,\n", name, font_size[i]);
    }
    printf("    NULL,\n");
    printf("};\n");
    return 0;
}
