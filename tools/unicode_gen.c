/*
 * Unicode table generator for QEmacs
 *
 * Copyright (c) 2000-2022 Charlie Gordon.
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

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include "cutils.h"

#define CHARCODE_MAX 0x10ffff

static const char NAME[] = "unicode_gen";

static char *skip_space(char *p) {
    while (isspace((unsigned char)*p))
        p++;
    return p;
}

#if 0
static char *get_field(char **pp) {
    char *p, *p0;
    for (p = p0 = *pp; *p; p++) {
        if (*p == ';') {
            *p++ = '\0';
            break;
        }
    }
    *pp = p;
    return p0;
}
#endif

static int make_width_table(int argc, char *argv[]) {
    const char *filename = "EastAsianWidth.txt";
    char line[1024];
    int lineno, w, last_w;
    unsigned long code, code1, code2;

    if (argc > 1) {
        filename = argv[1];
    }
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        fprintf(stderr, "%s: cannot open %s: %s\n", NAME, filename, strerror(errno));
        return 1;
    }
    unsigned char *width = calloc(sizeof(*width), CHARCODE_MAX + 1);
    char **names = calloc(sizeof(*names), CHARCODE_MAX + 1);
    /* set default widths */
    memset(width, 1, CHARCODE_MAX + 1);
    memset(width + 0x3400, 2, 0x4DBF - 0x3400 + 1);
    memset(width + 0x4E00, 2, 0x9FFF - 0x4E00 + 1);
    memset(width + 0xD800, 3, 0xDFFF - 0xD800 + 1); /* surrogates */
    memset(width + 0xF900, 2, 0xFAFF - 0xF900 + 1);
    memset(width + 0x20000, 2, 0x2FFFD - 0x20000 + 1);
    memset(width + 0x30000, 2, 0x3FFFD - 0x30000 + 1);

    lineno = 0;
    while (fgets(line, sizeof line, fp)) {
        char wclass[3];
        char cclass[3];
        char *p, *name1, *name2;

        lineno++;
        p = skip_space(line);
        if (*p == '#' || *p == '\0')
            continue;
        code1 = code2 = strtoul(p, &p, 16);
        if (p[0] == '.' && p[1] == '.') {
            code2 = strtoul(p + 2, &p, 16);
        }
        if (*p != ';' || !isalpha((unsigned char)*++p))
            goto fail;

        wclass[0] = *p++;
        wclass[1] = 0;
        if (isalpha((unsigned char)*p)) {
            wclass[1] = *p++;
            wclass[2] = 0;
        }
        p = skip_space(p);
        if (*p != '#' || p[1] != ' ')
            goto fail;
        p += 2;
        if (!isalpha((unsigned char)p[0]) || !(isalpha((unsigned char)p[1]) || p[1] == '&'))
            goto fail;
        cclass[0] = *p++;
        cclass[1] = *p++;
        cclass[2] = 0;
        p = skip_space(p);
        if (*p == '[') {
            strtol(p + 1, &p, 10);
            if (*p != ']')
                goto fail;
            p = skip_space(p + 1);
        }
        name1 = p;
        name2 = NULL;
        while (*p && *p != '\n') {
            if (p[0] == '.' && p[1] == '.') {
                *p = '\0';
                name2 = p += 2;
            } else {
                p++;
            }
        }
        *p = '\0';
        if (name1 && *name1)
            names[code1] = strdup(name1);
        if (code2 != code1 && name2 && *name2)
            names[code2] = strdup(name2);
        if (code2 > CHARCODE_MAX)
            goto fail;
        w = 1;
        if (*wclass == 'W')
            w = 2;
        if (*cclass == 'M')
            w = 0;
        for (code = code1; code <= code2; code++) {
            width[code] = w;
        }
        continue;
    fail:
        fprintf(stderr, "%s:%d:invalid line\n%s", filename, lineno, line);
    }
    printf("/* This file was generated automatically from %s */\n\n", filename);

    code1 = 0;
    last_w = width[code1];
    for (code = 1; code <= CHARCODE_MAX; code++) {
        if (width[code] != last_w) {
            code2 = code - 1;
            printf("    0x%05lX, %d,  /* ", code2, last_w);
            if (names[code1])
                printf("%s", names[code1]);
            else
                printf("U+%04lX", code1);
            if (code1 != code2) {
                if (names[code2])
                    printf("..%s", names[code2]);
                else
                    printf("..U+%04lX", code2);
            }
            printf(" */\n");
            last_w = width[code];
            code1 = code;
        }
    }
    code2 = 0xFFFFFFFF;
    printf("    0x%05lX, %d,  /* ", code2, last_w);
    if (names[code1])
        printf("%s", names[code1]);
    else
        printf("U+%04lX", code1);
    printf("..END");
    printf(" */\n");

    free(width);
    for (code = 0; code <= CHARCODE_MAX; code++)
        free(names[code]);
    free(names);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc > 1) {
        if (!strcmp(argv[1], "-w")) {
            return make_width_table(argc - 1, argv + 1);
        }
    }
    fprintf(stderr, "usage: %s [-w [EastAsianWidthFile]]\n", NAME);
    return 1;
}
