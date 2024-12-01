/*
 * Convert Unicode 8-bit code page files to QEmacs format
 *
 * Copyright (c) 2002 Fabrice Bellard.
 * Copyright (c) 2007-2024 Charlie Gordon.
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

static char module_init[4096];
static char *module_init_p = module_init;

#define add_init(s)  (module_init_p += snprintf(module_init_p, \
                      module_init + sizeof(module_init) - module_init_p, \
                      "%s", s))

static inline char *skipspaces(char *p) {
    while (isspace((unsigned char)*p))
        p++;
    return p;
}

#define getline my_getline        /* prevent name clash */
static char *getline(char *buf, int buf_size, FILE *f, int strip_comments)
{
    for (;;) {
        char *p;
        int len;

        if (!fgets(buf, buf_size, f))
            return NULL;
        len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') {
            buf[--len] = '\0';
        }
        p = skipspaces(buf);
        if (*p == 26) {
            /* handle obsolete DOS ctrl-Z marker */
            return NULL;
        }
        if (strip_comments && (*p == '\0' || *p == '#'))
            continue;

        return p;
    }
}

static void handle_cp(FILE *f0, const char *name, const char *fname, int lineno)
{
    char line[1024];
    char *p, *q;
    int table[256];
    int min_code, max_code, c1, c2, i, nb, j;
    char name_id[256];
    char iso_name[256];
    char alias_list[256];
    char includename[256];
    int has_iso_name, has_alias_list;
    int eol_char = 10;
    int base;
    FILE *f = f0;
    FILE *f1;
    int saveline = lineno;
    const char *filename = fname;
    const char *sourcename = fname;

    /* name_id is name with - changed into _ */
    pstrcpy(name_id, sizeof(name_id), name);
    for (p = name_id; *p != '\0'; p++) {
        if (*p == '-')
            *p = '_';
    }

    pstrcpy(iso_name, sizeof(iso_name), name);
    pstrcpy(alias_list, sizeof(alias_list), "");
    has_iso_name = has_alias_list = 0;

    for (i = 0; i < 256; i++) {
        table[i] = i;
    }

    nb = 0;
    for (;;) {
        lineno++;
        if (!(p = getline(line, sizeof(line), f, 0))
        ||  *p == '['
        ||  !strcasecmp(p, "# compatibility")) {
            if (f == f0)
                break;
            fclose(f);
            f = f0;
            filename = fname;
            lineno = saveline;
            continue;
        }
        if (*p == '\0' || p[0] == '#')
            continue;
        if (!memcmp(p, "include ", 8)) {
            if (filename != fname) {
                fprintf(stderr, "%s:%d: cannot include recursively %s\n",
                        filename, lineno, includename);
                continue;
            }
            pstrcpy(includename, sizeof(includename), filename);
            base = get_basename_offset(includename);
            pstrcpy(includename + base, sizeof(includename) - base,
                    skipspaces(p + 8));
            f1 = fopen(includename, "r");
            if (f1 == NULL) {
                fprintf(stderr, "%s:%d: cannot open %s\n", name, lineno, includename);
            } else {
                f = f1;
                sourcename = filename = includename;
                saveline = lineno;
                lineno = 0;
            }
            continue;
        }

        if (p[0] != '0' || (p[1] != 'x' && p[1] != 'X')) {
            if (!has_iso_name) {
                pstrcpy(iso_name, sizeof(iso_name), p);
                has_iso_name = 1;
                continue;
            }
            if (!has_alias_list) {
                pstrcpy(alias_list, sizeof(alias_list), p);
                has_alias_list = 1;
                continue;
            }
            if (!strcmp(iso_name, p) || !strcmp(alias_list, p))
                continue;

            if (!isdigit((unsigned char)*p)) {
                fprintf(stderr, "%s:%d: ignoring line: %s\n", filename, lineno, p);
                continue;
            }
        }

        c1 = strtol(p, &p, 16);
        if (!isspace((unsigned char)*p)) {
            /* ignore ranges such as "0x20-0x7e       idem" */
            continue;
        }
        p = skipspaces(p);
        if (*p == '\0' || *p == '#') {
            /* unknown */
            /* continue; */
        }
        c2 = strtol(p, &p, 16);
        if (c1 >= 256) {
            fprintf(stderr, "%s:%d: ERROR %d %d\n", filename, lineno, c1, c2);
            continue;
        }
        table[c1] = c2;
        nb++;
    }

    if (table[10] != 10) {
        if (table[0x25] == 0x0A) {
            /* EBCDIC file */
            eol_char = 0x25;
        } else {
            fprintf(stderr, "%s:%d: warning: newline is not preserved\n",
                    filename, lineno);
        }
    }

    min_code = 0x7fffffff;
    max_code = -1;
    for (i = 0; i < 256; i++) {
        if (table[i] != i) {
            if (min_code > i)
                min_code = i;
            if (max_code < i)
                max_code = i;
        }
    }

    printf("\n"
           "/*----------------------------------------------------------------\n"
           " * filename: %s\n"
           " * iso_name: %s\n"
           " *     name: %s\n"
           " *       id: %s\n"
           " */\n\n",
           sourcename, iso_name, name, name_id);

    if (max_code != -1) {
        printf("static const unsigned short table_%s[%d] = {\n",
               name_id, max_code - min_code + 1);
        j = 0;
        for (i = min_code; i <= max_code; i++) {
            if ((j & 7) == 0)
                printf("   ");
            printf(" 0x%04x,", table[i]);
            if ((j++ & 7) == 7)
                printf("\n");
        }
        if ((j & 7) != 0)
            printf("\n");
        printf("};\n\n");
    }

    if (strcmp(name_id, "mac_roman"))
        printf("static ");

    printf("struct QECharset charset_%s = {\n"
           "    \"%s\",\n",
           name_id, name);

    printf("    \"");
    {
        const char *sep = "";
        for (q = alias_list;;) {
            if ((p = strchr(q, '"')) == NULL
            ||  (q = strchr(++p, '"')) == NULL)
                break;
            *q++ = '\0';
            if (strcmp(name, p)) {
                printf("%s%s", sep, p);
                sep = "|";
            }
        }
    }
    printf("\",");

    printf("\n" "    NULL,"
           "\n" "    decode_8bit_init,"
           "\n" "    decode_8bit,"
           "\n" "    encode_8bit,"
           "\n" "    charset_get_pos_8bit,"
           "\n" "    charset_get_chars_8bit,"
           "\n" "    charset_goto_char_8bit,"
           "\n" "    charset_goto_line_8bit,"
           "\n" "    .char_size = 1,"
           "\n" "    .variable_size = 0,"
           "\n" "    .table_alloc = 1,"
           "\n" "    .eol_char = %d,"
           "\n" "    .min_char = %d,"
           "\n" "    .max_char = %d,"
           "\n" "    .private_table = table_%s,"
           "\n" "};"
           "\n\n",
           eol_char, min_code, max_code, name_id);

    add_init("    qe_register_charset(qs, &charset_");
    add_init(name_id);
    add_init(");\n");
}

static int namecmp(const char *p1, const char *p2, size_t len) {
    while (len--) {
        unsigned char c = *p1++;
        unsigned char d = *p2++;
        if (c == d)
            continue;
        if ((c == '-' || c == '_') && (d == '-' || d == '_'))
            continue;
        if (tolower(c) == tolower(d))
            continue;
        return c - d;
    }
    return 0;
}

static FILE *open_index(const char *indexname, const char *name, int *linep)
{
    char line[1024];
    FILE *f;
    int len = strlen(name);
    int lineno = 0;

    f = fopen(indexname, "r");
    if (f != NULL) {
        while (getline(line, sizeof(line), f, 1)) {
            lineno++;
            if (*line == '[' && line[1 + len] == ']'
            &&  !namecmp(line + 1, name, len)) {
                *linep = lineno;
                return f;
            }
        }
        fclose(f);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    int i, lineno;
    const char *filename;
    const char *indexname = NULL;
    char name[256], *p;
    FILE *f;

    printf("/* This file was generated automatically by cptoqe */\n");

    printf("\n" "/*"
           "\n" " * More Charsets and Tables for QEmacs"
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
           "\n" ""
           "\n" "#include \"qe.h\""
           "\n" "");

    add_init("int qe_charset_more_init(QEmacsState *qs)\n{\n");

    for (i = 1; i < argc; i++) {
        filename = argv[i];

        if (!strcmp(filename, "-i")) {
            if (++i >= argc) {
                fprintf(stderr, "cptoqe: missing index name after -i\n");
                exit(2);
            }
            indexname = argv[i];
            continue;
        }

        if (strstr(filename, "APPLE/")) {
            strcpy(name, "MAC-");
            pstrcat(name, sizeof(name), get_basename(filename));
        } else {
            pstrcpy(name, sizeof(name), get_basename(filename));
        }
        strip_extension(name);
        for (p = name; *p; p++) {
            if (*p == '_')
                *p = '-';
            else
                *p = tolower((unsigned char)*p);
        }

        f = open_index(indexname, name, &lineno);
        if (f) {
            handle_cp(f, name, indexname, lineno);
        } else {
            f = fopen(filename, "r");
            if (!f) {
                perror(filename);
                exit(1);
            }
            handle_cp(f, name, filename, 0);
        }
        fclose(f);
    }

    add_init("\n    return 0;\n}\n\n"
             "qe_module_init(qe_charset_more_init);\n");

    printf("%s", module_init);

    return 0;
}
