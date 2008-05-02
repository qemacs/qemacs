/*
 * Convert Unicode 8-bit code page files to QEmacs format
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

static char *getline(char *buf, int buf_size, FILE *f, int strip_comments)
{
    for (;;) {
        char *p;
        int len;

        if (!fgets(buf, buf_size, f))
            return NULL;
        len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') {
            buf[len - 1] = '\0';
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

static void handle_cp(FILE *f0, const char *name, const char *fname)
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
    const char *filename = fname;

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
        if (!(p = getline(line, sizeof(line), f, 0))
        ||  *p == '['
        ||  !strcasecmp(p, "# compatibility")) {
            if (f == f0)
                break;
            fclose(f);
            f = f0;
            filename = fname;
            continue;
        }
        if (*p == '\0' || p[0] == '#')
            continue;
        if (!memcmp(p, "include ", 8)) {
            pstrcpy(includename, sizeof(includename), filename);
            base = get_basename_offset(includename);
            pstrcpy(includename + base, sizeof(includename) - base,
                    skipspaces(p + 8));
            f = fopen(includename, "r");
            if (f == NULL) {
                fprintf(stderr, "%s: cannot open %s\n", name, includename);
                f = f0;
            }
            filename = includename;
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
                fprintf(stderr, "%s: ignoring line: %s\n", filename, p);
                continue;
            }
        }

        c1 = strtol(p, (char **)&p, 16);
        if (!isspace((unsigned char)*p)) {
            /* ignore ranges such as "0x20-0x7e       idem" */
            continue;
        }
        p = skipspaces(p);
        if (*p == '\0' || *p == '#') {
            /* unknown */
            /* continue; */
        }
        c2 = strtol(p, (char **)&p, 16);
        if (c1 >= 256) {
            fprintf(stderr, "%s: ERROR %d %d\n", filename, c1, c2);
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
            fprintf(stderr, "%s: warning: newline is not preserved\n",
                    filename);
        }
    }

    min_code = 0x7fffffff;
    max_code = -1;
    for (i = 0; i < 256; i++) {
        if (table[i] != i) {
            if (i < min_code)
                min_code = i;
            if (i > max_code)
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
           filename, iso_name, name, name_id);

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

    printf("static QECharset charset_%s = {\n"
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
    printf("\",\n");

    printf("    decode_8bit_init,\n"
           "    decode_8bit,\n"
           "    encode_8bit,\n"
           "    charset_get_pos_8bit,\n"
           "    charset_get_chars_8bit,\n"
           "    charset_goto_char_8bit,\n"
           "    charset_goto_line_8bit,\n"
           "    .char_size = 1,\n"
           "    .variable_size = 0,\n"
           "    .table_alloc = 1,\n"
           "    .eol_char = %d,\n"
           "    .min_char = %d,\n"
           "    .max_char = %d,\n"
           "    .private_table = table_%s,\n"
           "};\n\n",
           eol_char, min_code, max_code, name_id);

    add_init("    qe_register_charset(&charset_");
    add_init(name_id);
    add_init(");\n");
}

static int namecmp(const char *p1, const char *p2, size_t len)
{
    while (len--) {
        int c = (unsigned char)*p1++;
        int d = (unsigned char)*p2++;
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

static FILE *open_index(const char *indexname, const char *name)
{
    char line[1024];
    FILE *f;
    int len = strlen(name);

    f = fopen(indexname, "r");
    if (f != NULL) {
        while (getline(line, sizeof(line), f, 1)) {
            if (*line == '[' && line[1 + len] == ']'
            &&  !namecmp(line + 1, name, len)) {
                return f;
            }
        }
        fclose(f);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    int i;
    const char *filename;
    const char *indexname = NULL;
    char name[256], *p;
    FILE *f;

    printf("/* This file was generated automatically by cptoqe */\n");

    printf("\n" "/*"
           "\n" " * More Charsets and Tables for QEmacs"
           "\n" " *"
           "\n" " * Copyright (c) 2002 Fabrice Bellard."
           "\n" " * Copyright (c) 2002-2008 Charlie Gordon."
           "\n" " *"
           "\n" " * This library is free software; you can redistribute it and/or"
           "\n" " * modify it under the terms of the GNU Lesser General Public"
           "\n" " * License as published by the Free Software Foundation; either"
           "\n" " * version 2 of the License, or (at your option) any later version."
           "\n" " *"
           "\n" " * This library is distributed in the hope that it will be useful,"
           "\n" " * but WITHOUT ANY WARRANTY; without even the implied warranty of"
           "\n" " * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU"
           "\n" " * Lesser General Public License for more details."
           "\n" " *"
           "\n" " * You should have received a copy of the GNU Lesser General Public"
           "\n" " * License along with this library; if not, write to the Free Software"
           "\n" " * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA"
           "\n" " */"
           "\n" ""
           "\n" "#include \"qe.h\""
           "\n" "");

    add_init("int charset_more_init(void)\n{\n");

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

        pstrcpy(name, sizeof(name), get_basename(filename));
        strip_extension(name);
        for (p = name; *p; p++) {
            if (*p == '_')
                *p = '-';
            else
                *p = tolower((unsigned char)*p);
        }

        f = open_index(indexname, name);
        if (!f) {
            f = fopen(filename, "r");
            if (!f) {
                perror(filename);
                exit(1);
            }
        }

        handle_cp(f, name, filename);

        fclose(f);
    }

    add_init("\n    return 0;\n}\n\n"
             "qe_module_init(charset_more_init);\n");

    printf("%s", module_init);

    return 0;
}
