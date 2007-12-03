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

static void dump_font(const char *filename, const char *name)
{
    int c;
    FILE *f;
    int j;

    f = fopen(filename, "r");
    if (!f) {
        perror(filename);
        exit(1);
    }
    printf("static const unsigned char font_%s[] = {\n", name);
    j = 0;
    for (;;) {
        c = fgetc(f);
        if (c == EOF)
            break;
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

int main(int argc, char **argv)
{
    const char *filename;
    char name[128];
    int i;

    printf("#include \"qe.h\"\n\n");

    for (i = 1; i < argc; i++) {
        filename = argv[i];
        getname(name, sizeof(name), filename);
        dump_font(filename, name);
    }

    /* dump font list */
    printf("const void *fbf_fonts[] = {\n");
    for (i = 1; i < argc; i++) {
        FILE *f;
        int size;
        filename = argv[i];

        f = fopen(filename, "r");
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fclose(f);

        getname(name, sizeof(name), filename);
        printf("font_%s, (void *)%d,\n", name, size);
    }
    printf("NULL,\n");
    printf("};\n");
    return 0;
}
