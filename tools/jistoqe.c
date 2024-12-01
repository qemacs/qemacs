/*
 * Convert Unicode JIS tables to QEmacs format
 *
 * Copyright (c) 2002 Fabrice Bellard.
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "cutils.h"

#define getline my_getline        /* prevent name clash */
static char *getline(char *buf, int buf_size, FILE *f, int strip_comments)
{
    for (;;) {
        char *str;
        int len;

        str = fgets(buf, buf_size, f);
        if (!str)
            return NULL;
        len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') {
            buf[len - 1] = '\0';
        }
        if (buf[0] == 26) {
            /* handle obsolete DOS ctrl-Z marker */
            return NULL;
        }
        if (strip_comments && (buf[0] == '\0' || buf[0] == '#'))
            continue;

        return str;
    }
}

/* handle jis208 or jis212 table */
static void handle_jis(FILE *f, const char *name, const char *filename)
{
    int c1, c2, b1, b2, b1_max, b2_max, i, j, nb, n;
    int table[94*94];
    int table_b2_max[94];
    char line[1024];
    const char *p;
    int is_jis208;

    if (!strcmp(name, "JIS0208")) {
        is_jis208 = 1;
        name = "jis208";
    } else
    if (!strcmp(name, "JIS0212")) {
        is_jis208 = 0;
        name = "jis212";
    } else {
        fprintf(stderr, "%s: unsupported JIS file\n", filename);
        return;
    }

    memset(table, 0, sizeof(table));
    memset(table_b2_max, 0, sizeof(table_b2_max));
    b1_max = 0;
    b2_max = 0;
    nb = 0;
    while (getline(line, sizeof(line), f, 1)) {
        p = line;
        if (is_jis208)
            c1 = strtol_c(p, &p, 0);
        c1 = strtol_c(p, &p, 0);
        c2 = strtol_c(p, &p, 0);

        b1 = (c1 >> 8) & 0xff;
        b2 = (c1) & 0xff;

        /* compress the code */
        b1 = b1 - 0x21;
        b2 = b2 - 0x21;
        if (b1_max < b1)
            b1_max = b1;
        if (b2_max < b2)
            b2_max = b2;
        if (table_b2_max[b1] < b2)
            table_b2_max[b1] = b2;
        table[b1 * 94 + b2] = c2;
        nb++;
    }
    printf("\n/* max row = %d. The following rows are excluded:\n   ", b1_max);
    n = 0;
    for (i = 0; i <= b1_max; i++) {
        if (table_b2_max[i] == 0) {
            printf(" %d", i);
        } else {
            n++;
        }
    }
    printf(", density=%d%% */\n",  nb * 100 / (n * (b2_max + 1)));

    printf("static unsigned short const table_%s[%d] = {\n",
           name, n * (b2_max + 1));
    n = 0;
    for (i = 0; i <= b1_max; i++) {
        if (table_b2_max[i] != 0) {
            for (j = 0; j <= b2_max; j++) {
                if ((n & 7) == 0)
                    printf("   ");
                printf(" 0x%04x,", table[i * 94 + j]);
                if ((n++ & 7) == 7)
                    printf("\n");
            }
        }
    }
    if ((n & 7) != 0)
        printf("\n");
    printf("};\n");
}

int main(int argc, char **argv)
{
    int i;
    const char *filename;
    char name[256];
    FILE *f;

    printf("/* This file was generated automatically by jistoqe */\n");

    printf("\n" "/*"
           "\n" " * JIS Tables for QEmacs"
           "\n" " *"
           "\n" " * Copyright (c) 2002 Fabrice Bellard."
           "\n" " * Copyright (c) 2002-2024 Charlie Gordon."
           "\n" " *"
           "\n" " * Permission is hereby granted, free of charge, to any person obtaining a copy"
           "\n" " * of this software and associated documentation files (the \"Software\"), to deal"
           "\n" " * in the Software without restriction, including without limitation the rights"
           "\n" " * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell"
           "\n" " * copies of the Software, and to permit persons to whom the Software is"
           "\n" " * furnished to do so, subject to the following conditions:"
           "\n" " *"
           "\n" " * The above copyright notice and this permission notice shall be included in"
           "\n" " * all copies or substantial portions of the Software."
           "\n" " *"
           "\n" " * THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR"
           "\n" " * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,"
           "\n" " * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL"
           "\n" " * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER"
           "\n" " * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,"
           "\n" " * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN"
           "\n" " * THE SOFTWARE."
           "\n" " */"
           "\n");

    for (i = 1; i < argc; i++) {
        filename = argv[i];

        pstrcpy(name, sizeof(name), get_basename(filename));
        strip_extension(name);

        f = fopen(filename, "r");
        if (!f) {
            perror(filename);
            exit(1);
        }

        handle_jis(f, name, filename);

        fclose(f);
    }

    return 0;
}
