/*
 * Unicode table generator for QEmacs
 *
 * Copyright (c) 2000-2023 Charlie Gordon.
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
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "cutils.h"

static const char NAME[] = "unicode_gen";

#define CHARCODE_MAX 0x10ffff

static int encode_utf8(char *dest, char32_t c) {
    char *q = dest;

    if (c < 0x80) {
        *q++ = c;
    } else {
        if (c < 0x800) {
            *q++ = (c >> 6) | 0xc0;
        } else {
            if (c < 0x10000) {
                *q++ = (c >> 12) | 0xe0;
            } else {
                if (c < 0x00200000) {
                    *q++ = (c >> 18) | 0xf0;
                } else {
                    if (c < 0x04000000) {
                        *q++ = (c >> 24) | 0xf8;
                    } else {
                        *q++ = (c >> 30) | 0xfc;
                        *q++ = ((c >> 24) & 0x3f) | 0x80;
                    }
                    *q++ = ((c >> 18) & 0x3f) | 0x80;
                }
                *q++ = ((c >> 12) & 0x3f) | 0x80;
            }
            *q++ = ((c >> 6) & 0x3f) | 0x80;
        }
        *q++ = (c & 0x3f) | 0x80;
    }
    *q = '\0';
    return q - dest;
}

static char *skip_space(char *p) {
    while (isspace((unsigned char)*p))
        p++;
    return p;
}

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

static FILE *fopen_verbose(const char *filename, const char *mode) {
    FILE *fp = fopen(filename, mode);
    if (fp == NULL) {
        fprintf(stderr, "%s: cannot open %s: %s\n",
                NAME, filename, strerror(errno));
    }
    return fp;
}

#define set_range(a, c1, c2, type) do {             \
    if (sizeof(*(a)) == 1) {                        \
        memset((a) + (c1), type, (c2) - (c1) + 1);  \
    } else {                                        \
        size_t i__ = (c1), n__ = (c2);              \
        for (; i__ <= n__; i__++)                   \
            (a)[i__] = (type);                      \
    }                                               \
} while(0)

/*---------------- simple vectors ----------------*/

typedef struct uint_vector_t {
    unsigned int *table;
    unsigned int length;
    unsigned int size;
} uint_vector_t;

static uint_vector_t *uint_vector_init(uint_vector_t *vp) {
    vp->table = NULL;
    vp->length = 0;
    vp->size = 0;
    return vp;
}

static void uint_vector_reset(uint_vector_t *vp) {
    free(vp->table);
    uint_vector_init(vp);
}

static void uint_vector_append(uint_vector_t *vp, int value) {
    if (vp->length == vp->size) {
        unsigned int new_size = vp->size + (vp->size >> 1) + 16;
        unsigned int *new_table = realloc(vp->table, new_size * sizeof(*vp->table));
        if (!new_table) {
            fprintf(stderr, "%s: allocation failure\n", NAME);
            exit(1);
        }
        vp->size = new_size;
        vp->table = new_table;
    }
    vp->table[vp->length++] = value;
}

/*---------------- Table generation state ----------------*/

static const char * const unicode_versions[] = {
    "4.1.0", "5.0.0", "5.1.0", "5.2.0", "6.0.0",
    "6.1.0", "6.2.0", "6.3.0", "7.0.0", "8.0.0",
    "9.0.0", "10.0.0", "11.0.0", "12.0.0", "12.1.0",
    "13.0.0", "14.0.0", "15.0.0",
};

typedef struct genstate_t {
    const char *unicode_version;
    const char *unicode_dir;
    const char *include_file;
    const char *source_file;
    int level;
    FILE *fi;
    FILE *fc;
    int ind, argc;
    char **argv;
    int use_names;
    unsigned int filter_start, filter_end;
} genstate_t;

#define has_arg(gp)   ((gp)->ind < (gp)->argc)
#define peek_arg(gp)  ((gp)->argv[(gp)->ind])
#define next_arg(gp)  ((gp)->ind++)
#define get_arg(gp)   ((gp)->argv[(gp)->ind++])

static FILE *open_unicode_file(const char *dir, const char *version,
                               const char *base, char *filename, int size)
{
    char cmd[2048];
    FILE *fp;
    int pos = 0;

    if (dir && *dir) {
        mkdir(dir, 0777);
        pos = snprintf(filename, size, "%s", dir);
        if (pos < 0 || pos + 1 >= size) {
            fprintf(stderr, "%s: Unicode directory too long: %s\n", NAME, dir);
            return NULL;
        }
        if (pos > 0 && filename[pos - 1] != '/')
            filename[pos++] = '/';
    }
    if (version && *version) {
        const char *ext = strrchr(base, '.');
        if (!ext)
            ext = base + strlen(base);
        pos += snprintf(filename + pos, size - pos, "%.*s-%s%s",
                        (int)(ext - base), base, version, ext);
    } else {
        pos += snprintf(filename + pos, size - pos, "%s", base);
    }
    if (pos >= size) {
        fprintf(stderr, "%s: Cannot compose filename for %s\n", NAME, base);
        return NULL;
    }
    fp = fopen(filename, "r");
    if (fp) {
        if (fgetc(fp) != EOF) {
            rewind(fp);
            return fp;
        }
        fclose(fp);
        fprintf(stderr, "%s: removing empty file: %s\n", NAME, filename);
        unlink(filename);
    }
    snprintf(cmd, sizeof cmd,
             "wget -q ftp://ftp.unicode.org/Public/%s/ucd/%s -O %s",
             version, base, filename);
    fprintf(stderr, "%s: %s\n", NAME, cmd);
    system(cmd);
    fp = fopen_verbose(filename, "r");
    if (fp) {
        if (fgetc(fp) != EOF) {
            rewind(fp);
            return fp;
        }
        fclose(fp);
        fprintf(stderr, "%s: removing empty file: %s\n", NAME, filename);
        unlink(filename);
    }
    return NULL;
}

/*---------------- Generic table generators ----------------*/

static int generate_skip_table(genstate_t *gp,
                               unsigned char *tab,
                               const char *table_name,
                               const char *function_name,
                               int shift_level, int gen)
{
    uint_vector_t table[1];
    unsigned int i, code;
    unsigned int last_w;
    unsigned int *ip;
    unsigned int min_c, max_c, code1, code2;
    int def_width;
#define GLYPH_RANGE_SHIFT  shift_level
    unsigned int index[(CHARCODE_MAX + 1) >> GLYPH_RANGE_SHIFT];
    unsigned int index_length;
    unsigned int ucs;
    unsigned int total_size;

    uint_vector_init(table);
    last_w = tab[0];
    for (code = 1; code <= CHARCODE_MAX; code++) {
        if (last_w != tab[code]) {
            uint_vector_append(table, code - 1);
            uint_vector_append(table, last_w);
        }
        last_w = tab[code];
    }
    if (last_w != tab[0]) {
        uint_vector_append(table, CHARCODE_MAX);
        uint_vector_append(table, last_w);
    }
    uint_vector_append(table, 0xFFFFFFFF);
    uint_vector_append(table, 1);

    /* compute index */
    ip = table->table;
    min_c = ip[0] + 1;
    max_c = ip[table->length - 4];
    def_width = ip[1];
    index_length = 0;

    for (ucs = 0; ucs < max_c; ucs += 1U << GLYPH_RANGE_SHIFT) {
        while (ucs > ip[0])
            ip += 2;
        index[index_length++] = ip - table->table;
    }
    total_size = sizeof(unsigned int) * table->length + sizeof(unsigned short) * index_length;

    if (gen) {
        fprintf(gp->fc, "\n");
        fprintf(gp->fc, "/* using %u byte page index table: %u bytes */\n",
                1U << GLYPH_RANGE_SHIFT, total_size);
        /* tables */
        fprintf(gp->fc, "\n");
        fprintf(gp->fc, "static unsigned int const %s[%u] = {\n", table_name, table->length);
        code1 = 0;
        for (i = 0; i < table->length; i += 2) {
            code2 = table->table[i];
            fprintf(gp->fc, "    0x%05X, %3d,  /* ", code2, table->table[i + 1]);
            fprintf(gp->fc, "U+%04X", code1);
            if (code1 != code2)
                fprintf(gp->fc, "..U+%04X", code2);
            code1 = code2 + 1;
            fprintf(gp->fc, " */\n");
        }
        fprintf(gp->fc, "};\n");

        fprintf(gp->fc, "\n");
        fprintf(gp->fc, "static const unsigned short %s_index[%u] = {", table_name, index_length);
        for (i = 0; i < index_length; i++) {
            fprintf(gp->fc, "%s%5u,", (i % 8) ? "" : "\n    ", index[i]);
            if (i % 8 == 7)
                fprintf(gp->fc, "  /* U+%04X..U+%04X */",
                        (i - 7) << GLYPH_RANGE_SHIFT, ((i + 1) << GLYPH_RANGE_SHIFT) - 1);
        }
        fprintf(gp->fc, "\n};\n");
        /* function implementation */
        fprintf(gp->fc, "\n");
        fprintf(gp->fc, "int %s(unsigned int ucs) {\n", function_name);
        fprintf(gp->fc, "    if (ucs - 0x%x > 0x%x - 0x%x) return %d;\n", min_c, max_c, min_c, def_width);
        fprintf(gp->fc, "    /* Iterative lookup with fast initial jump, no boundary test needed */\n");
        fprintf(gp->fc, "    unsigned int const *ip = %s + %s_index[ucs >> %d];\n",
                table_name, table_name, GLYPH_RANGE_SHIFT);
        fprintf(gp->fc, "\n");
        fprintf(gp->fc, "    while (ucs > ip[0]) {\n");
        fprintf(gp->fc, "        ip += 2;\n");
        fprintf(gp->fc, "    }\n");
        fprintf(gp->fc, "    return ip[1];\n");
        fprintf(gp->fc, "}\n");
        /* function prototype */
        fprintf(gp->fi, "extern int %s(unsigned int ucs);\n", function_name);
    }
    uint_vector_reset(table);
    return total_size;
}

static int generate_table_2_level(genstate_t *gp,
                                  unsigned char *table,
                                  const char *table_name,
                                  const char *function_name,
                                  int bits, int gen)
{
    unsigned int min_c, max_c;
    unsigned int level0_bits = bits;
    unsigned int level0_size = 1U << level0_bits;
    /* make a copy of the significant subset */
    unsigned int table1_len;
    unsigned int table0_len;
    unsigned char *table0;
    unsigned int *table1;
    unsigned int i0, i1, j1, n1;
    unsigned int total_size;

    for (min_c = 0; table[min_c] == table[0]; min_c++)
        continue;
    for (max_c = CHARCODE_MAX; table[max_c] == table[0]; max_c--)
        continue;

    /* make a copy of the significant subset */
    table1_len = (max_c + level0_size) / level0_size;
    table0_len = table1_len * level0_size;
    table0 = calloc(table0_len, sizeof(*table0));
    table1 = calloc(table1_len, sizeof(*table1));
    n1 = 0;

    if (!table0 || !table1) {
        free(table0);
        free(table1);
        return -1;
    }

    blockcpy(table0, table, table0_len);
    for (i1 = 0; i1 < table1_len; i1++) {
        for (j1 = 0; j1 < n1; j1++) {
            if (!blockcmp(table0 + j1 * level0_size, table + i1 * level0_size, level0_size))
                break;
        }
        table1[i1] = j1;
        if (j1 == n1) {
            blockcpy(table0 + n1 * level0_size, table + i1 * level0_size, level0_size);
            n1++;
        }
    }
    table0_len = n1 * level0_size;
    total_size = table0_len + (1 + (n1 >= 256)) * table1_len;
    if (gen) {
        /* function implementation */
        fprintf(gp->fc, "\n/* Using a 2 level lookup table: %u bytes */\n", total_size);
        fprintf(gp->fc, "\n");
        fprintf(gp->fc, "static const unsigned char %s_00[%u * %u] = {",
                table_name, level0_size, n1);
        for (i0 = 0; i0 < table0_len; i0++) {
            const char *sep = (i0 % 16) ? " " : "\n    ";
            if ((i0 % level0_size) == 0) {
                unsigned int x1;
                for (x1 = 0; x1 < table1_len; x1++) {
                    if (table1[x1] == i0 / level0_size) {
                        fprintf(gp->fc, "\n    /* %05X..%05X */",
                                x1 * level0_size,
                                (x1 + 1) * level0_size - 1);
                        break;
                    }
                }
            }
            fprintf(gp->fc, "%s%d,", sep, table0[i0]);
        }
        fprintf(gp->fc, "\n};\n\n");
        fprintf(gp->fc, "static const unsigned %s %s_01[%u] = {",
                n1 >= 256 ? "short" : "char", table_name, table1_len);
        for (i1 = 0; i1 < table1_len; i1++) {
            const char *sep = (i1 % 8) ? " " : "\n    ";
            fprintf(gp->fc, "%s%4d,", sep, table1[i1]);
            if ((i1 % 8) == 7)
                fprintf(gp->fc, "  /* %05X..%05X */", (i1 - 7) * level0_size, i1 * level0_size);
        }
        fprintf(gp->fc, "\n};\n\n");
        fprintf(gp->fc, "int %s(unsigned int cp) {\n", function_name);
        fprintf(gp->fc, "    if (cp - 0x%x > 0x%x - 0x%x) return %d;\n",
                min_c, max_c, min_c, table[0]);
        fprintf(gp->fc, "    return %s_00[cp %% %u + %u * %s_01[cp / %u]];\n",
                table_name, level0_size, level0_size, table_name, level0_size);
        fprintf(gp->fc, "}\n");
        /* function prototype */
        fprintf(gp->fi, "extern int %s(unsigned int cp);\n", function_name);
    }
    free(table0);
    free(table1);
    return total_size;
}

static int generate_table_3_level(genstate_t *gp,
                                  unsigned char *table,
                                  const char *table_name,
                                  const char *function_name,
                                  int bits, int bits2, int gen)
{
    unsigned int min_c, max_c;
    unsigned int level0_bits;
    unsigned int level0_size;
    unsigned int level1_bits;
    unsigned int level1_size;
    /* make a copy of the significant subset */
    unsigned int table0_len;
    unsigned int table1_len;
    unsigned int table2_len;
    unsigned char *table0;
    unsigned int *table1;
    unsigned int *table2;
    unsigned int i0, i1, j1, n1, i2, j2, n2;
    unsigned int total_size;

    for (min_c = 0; table[min_c] == table[0]; min_c++)
        continue;
    for (max_c = CHARCODE_MAX; table[max_c] == table[0]; max_c--)
        continue;

    level0_bits = bits;
    level0_size = 1U << level0_bits;
    level1_bits = bits2;
    level1_size = 1U << level1_bits;

    /* make a copy of the significant subset */
    table1_len = (max_c + level0_size * level1_size) / (level0_size * level1_size) * level1_size;
    table0_len = table1_len * level0_size;
    table2_len = table1_len / level1_size;
    table0 = calloc(table0_len, sizeof(*table0));
    table1 = calloc(table1_len, sizeof(*table1));
    table2 = calloc(table2_len, sizeof(*table2));

    if (!table0 || !table1 || !table2) {
        free(table0);
        free(table1);
        free(table2);
        return -1;
    }
    blockcpy(table0, table, table0_len);

    for (i1 = n1 = 0; i1 < table1_len; i1++) {
        for (j1 = 0; j1 < n1; j1++) {
            if (!blockcmp(table0 + j1 * level0_size, table0 + i1 * level0_size, level0_size))
                break;
        }
        table1[i1] = j1;
        if (j1 == n1) {
            if (n1 != i1)
                blockcpy(table0 + n1 * level0_size, table0 + i1 * level0_size, level0_size);
            n1++;
        }
    }
    table0_len = n1 * level0_size;

    for (i2 = n2 = 0; i2 < table2_len; i2++) {
        for (j2 = 0; j2 < n2; j2++) {
            if (!blockcmp(table1 + j2 * level1_size, table1 + i2 * level1_size, level1_size))
                break;
        }
        table2[i2] = j2;
        if (j2 == n2) {
            if (n2 != i2)
                blockcpy(table1 + n2 * level1_size, table1 + i2 * level1_size, level1_size);
            n2++;
        }
    }
    table1_len = n2 * level1_size;

    total_size = table0_len + (1 + (n1 >= 256)) * table1_len + (1 + (n2 >= 256)) * table2_len;
    if (gen) {
        /* function implementation */
        fprintf(gp->fc, "\n/* Using a 3 level lookup table: %u bytes */\n", total_size);
        fprintf(gp->fc, "\n");
        fprintf(gp->fc, "static const unsigned char %s_00[%u * %u] = {",
                table_name, level0_size, n1);
        for (i0 = 0; i0 < table0_len; i0++) {
            const char *sep = (i0 % 16) ? "" : "\n   ";
            if ((0)) if ((i0 % level0_size) == 0) {
                unsigned int x1;
                for (x1 = 0; x1 < table1_len; x1++) {
                    if (table1[x1] == i0 / level0_size) {
                        fprintf(gp->fc, "\n    /* %05X..%05X */",
                                x1 * level0_size,
                                (x1 + 1) * level0_size - 1);
                        break;
                    }
                }
            }
            fprintf(gp->fc, "%s %d,", sep, table0[i0]);
        }
        fprintf(gp->fc, "\n};\n");
        fprintf(gp->fc, "\n");
        fprintf(gp->fc, "static const unsigned %s %s_01[%u * %u] = {",
                n1 >= 256 ? "short" : "char", table_name, level1_size, n2);
        for (i1 = 0; i1 < table1_len; i1++) {
            const char *sep = (i1 % 8) ? "" : (i1 % level1_size) ? "\n   " : "\n\n   ";
            fprintf(gp->fc, "%s %4d,", sep, table1[i1]);
            if ((0)) if ((i1 % 8) == 7)
                fprintf(gp->fc, "  /* %05X..%05X */", (i1 - 7) * level0_size, i1 * level0_size);
        }
        fprintf(gp->fc, "\n};\n");
        fprintf(gp->fc, "\n");
        fprintf(gp->fc, "static const unsigned %s %s_02[%u] = {",
                n2 >= 256 ? "short" : "char", table_name, table2_len);
        for (i2 = 0; i2 < table2_len; i2++) {
            const char *sep = (i2 % 8) ? "" : "\n   ";
            fprintf(gp->fc, "%s %4d,", sep, table2[i2]);
            if ((i2 % 8) == 7)
                fprintf(gp->fc, "  /* %05X..%05X */", (i2 - 7) * level1_size * level0_size, i2 * level1_size * level0_size);
        }
        fprintf(gp->fc, "\n};\n");
        fprintf(gp->fc, "\n");
        fprintf(gp->fc, "int %s(unsigned int cp) {\n", function_name);
        fprintf(gp->fc, "    if (cp - 0x%x > 0x%x - 0x%x) return %d;\n", min_c, max_c, min_c, table[0]);
        fprintf(gp->fc, "    return %s_00[cp %% %u + %u * %s_01[cp / %u %% %u + %u * %s_02[cp / %u]]];\n",
                table_name, level0_size, level0_size,
                table_name, level0_size, level1_size, level1_size,
                table_name, level0_size * level1_size);
        fprintf(gp->fc, "}\n");
        /* function prototype */
        fprintf(gp->fi, "extern int %s(unsigned int cp);\n", function_name);
    }
    free(table0);
    free(table1);
    free(table2);
    return total_size;
}

static int generate_table(genstate_t *gp,
                          unsigned char *table,
                          const char *table_name,
                          const char *function_name,
                          int max_level)
{
    /* output packed tables and function */
    /* find significant subset */
    int bits, bits2, size, best_bits2, best_size2, best_bits3, best_bits23, best_size3;

    if (max_level < 2)
        return generate_skip_table(gp, table, table_name, function_name, 8, 1);

    best_size2 = 0;
    best_bits2 = 0;
    for (bits = 9; bits >= 4; bits--) {
        size = generate_table_2_level(gp, table, table_name, function_name, bits, 0);
        //printf("/* Testing a 2 level lookup table with %d bits: %d bytes */\n", bits, size);
        if (size > 0 && (!best_size2 || size < best_size2)) {
            best_size2 = size;
            best_bits2 = bits;
        }
    }
    if (max_level == 2) {
        return generate_table_2_level(gp, table, table_name, function_name, best_bits2, 1);
    }
    best_size3 = 0;
    best_bits3 = 0;
    best_bits23 = 0;
    for (bits = 9; bits >= 4; bits--) {
        for (bits2 = 9; bits2 >= 4; bits2--) {
            size = generate_table_3_level(gp, table, table_name, function_name, bits, bits2, 0);
            //printf("/* Testing a 3 level lookup table with %d,%d bits: %d bytes */\n", bits, bits2, size);
            if (size > 0 && (!best_size3 || size < best_size3)) {
                best_size3 = size;
                best_bits3 = bits;
                best_bits23 = bits2;
            }
        }
    }
    if (best_size2 < best_size3)
        return generate_table_2_level(gp, table, table_name, function_name, best_bits2, 1);
    else
        return generate_table_3_level(gp, table, table_name, function_name, best_bits3, best_bits23, 1);
}

/*---------------- TTY Unicode width tables ----------------*/

#include <termios.h>

static int israwtty;
static struct termios oldtty;

static void set_cooked_tty(void) {
    if (israwtty) {
        tcsetattr(fileno(stdin), TCSANOW, &oldtty);
        fprintf(stderr, "\n");
        israwtty = 0;
    }
}

static void set_raw_tty(void) {
    struct termios tty;

    tcgetattr(fileno(stdin), &tty);
    oldtty = tty;

    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
                     |INLCR|IGNCR|ICRNL|IXON);
    tty.c_oflag |= OPOST;
    tty.c_lflag &= ~(ECHO|ECHONL|ICANON|IEXTEN|ISIG);
    tty.c_cflag &= ~(CSIZE|PARENB);
    tty.c_cflag |= CS8;
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;

    tcsetattr(fileno(stdin), TCSANOW, &tty);
    israwtty = 1;
    atexit(set_cooked_tty);
}

static int make_tty_width_table(genstate_t *gp) {
    unsigned int code;
    unsigned char *width;

    if (getenv("QELEVEL")) {
        fprintf(stderr, "cannot run in quick emacs shell buffer\n");
        return 1;
    }
    width = calloc(sizeof(*width), CHARCODE_MAX + 1);
    /* set default widths */
    set_range(width, 0, CHARCODE_MAX, 1);
    set_range(width, 0x3400, 0x4DBF, 2);
    set_range(width, 0x4E00, 0x9FFF, 2);
    set_range(width, 0xD800, 0xDFFF, 3); /* surrogates? */
    set_range(width, 0xF900, 0xFAFF, 2);
    set_range(width, 0x20000, 0x2FFFD, 2);
    set_range(width, 0x30000, 0x3FFFD, 2);

    set_raw_tty();
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    for (code = gp->filter_start; code <= gp->filter_end; code++) {
        char buf[10];
        char line[32];
        int len, c, n, y, x;

        /* measure the actual code point width as reported by the terminal */
        encode_utf8(buf, code);
#define XPOS  10
        len = snprintf(line, sizeof line,
                       "\r%06X "
                       //"\xC2\xA0"  /* nbsp */
                       //" "         /* space */
                       "-"
                       "%s"
                       "-"
                       "\033[6n",
                       code, buf);
        write(fileno(stderr), line, len);
        /* flush pending input to avoid locking on scanf */
        while ((c = getchar()) != EOF && c != '\033')
            continue;
        if (c != '\033') {
            fprintf(stderr, "%s: premature end of file\n", NAME);
            exit(1);
        }
        n = scanf("[%d;%dR", &y, &x);  /* get cursor position */
        if (n == 2)
            width[code] = x - XPOS;
    }
    set_cooked_tty();

    generate_table(gp, width, "wcwidth", "qe_wcwidth", gp->level);
#if 0
    for (code = 0; code <= CHARCODE_MAX; code++) {
        width[code] = (width[code] == 0);
    }
    generate_table(gp, width, "accent", "qe_isaccent", gp->level);
#endif
    free(width);
    return 0;
}

/*---------------- Unicode width tables from EastAsianWidth ----------------*/

static int make_wcwidth_table(genstate_t *gp) {
    char filename[1024];
    char line[1024];
    FILE *fp;
    unsigned char *width;
    char **names;
    int lineno, w;
    unsigned long code, code1, code2;

    if (!(fp = open_unicode_file(gp->unicode_dir, gp->unicode_version,
                                 "EastAsianWidth.txt",
                                 filename, sizeof filename)))
        return 1;

    width = calloc(sizeof(*width), CHARCODE_MAX + 1);
    names = calloc(sizeof(*names), CHARCODE_MAX + 1);
    /* set default widths */
    set_range(width, 0, CHARCODE_MAX, 1);
    set_range(width, 0x3400, 0x4DBF, 2);
    set_range(width, 0x4E00, 0x9FFF, 2);
    set_range(width, 0xD800, 0xDFFF, 3); /* surrogates */
    set_range(width, 0xF900, 0xFAFF, 2);
    set_range(width, 0x20000, 0x2FFFD, 2);
    set_range(width, 0x30000, 0x3FFFD, 2);

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
        if (*wclass == 'W' || *wclass == 'F')
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

    fprintf(gp->fc, "\n/* This table was generated from %s */\n", filename);
#if 1
    generate_table(gp, width, "wcwidth", "qe_wcwidth", gp->level);
#else
    code1 = 0;
    for (code = 1; code <= CHARCODE_MAX; code++) {
        if (width[code] != width[code1]) {
            code2 = code - 1;
            fprintf(gp->fc, "    0x%05lX, %d,  /* ", code2, width[code1]);
            if (names[code1])
                fprintf(gp->fc, "%s", names[code1]);
            else
                fprintf(gp->fc, "U+%04lX", code1);
            if (code1 != code2) {
                if (names[code2])
                    fprintf(gp->fc, "..%s", names[code2]);
                else
                    fprintf(gp->fc, "..U+%04lX", code2);
            }
            fprintf(gp->fc, " */\n");
            code1 = code;
        }
    }
    code2 = 0xFFFFFFFF;
    fprintf(gp->fc, "    0x%05lX, %d,  /* ", code2, width[code1]);
    if (names[code1])
        fprintf(gp->fc, "%s", names[code1]);
    else
        fprintf(gp->fc, "U+%04lX", code1);
    fprintf(gp->fc, "..END");
    fprintf(gp->fc, " */\n");
#endif
    free(width);
    for (code = 0; code <= CHARCODE_MAX; code++)
        free(names[code]);
    free(names);
    return 0;
}

static unsigned short *load_east_easian_width(genstate_t *gp, const char *version_str) {
    char filename[1024];
    char line[1024];
    FILE *fp;
    unsigned short *width;
    int lineno;
    unsigned long code, code1, code2;

    if (!(fp = open_unicode_file(gp->unicode_dir, version_str,
                                 "EastAsianWidth.txt",
                                 filename, sizeof filename)))
        return NULL;

    width = calloc(sizeof(*width), CHARCODE_MAX + 1);

    /* set default widths */
    set_range(width, 0, CHARCODE_MAX, 1/*'N'*/);
    set_range(width, 0x3400, 0x4DBF, 2/*'W'*/);
    set_range(width, 0x4E00, 0x9FFF, 2/*'W'*/);
    //set_range(width, 0xD800, 0xDFFF, 0); /* surrogates */
    set_range(width, 0xF900, 0xFAFF, 2/*'W'*/);
    set_range(width, 0x20000, 0x2FFFD, 2/*'W'*/);
    set_range(width, 0x30000, 0x3FFFD, 2/*'W'*/);

    lineno = 0;
    while (fgets(line, sizeof line, fp)) {
        char wclass[3];
        char *p;

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
        if (code2 > CHARCODE_MAX)
            code2 = CHARCODE_MAX;
        for (code = code1; code <= code2; code++) {
            //width[code] = wclass[0] | (wclass[1] << 8);
            width[code] = (wclass[0] == 'W' || wclass[0] == 'F');
        }
        continue;
    fail:
        fprintf(stderr, "%s:%d:invalid line\n%s", filename, lineno, line);
    }

    if (!(fp = open_unicode_file(gp->unicode_dir, version_str,
                                 "UnicodeData.txt",
                                 filename, sizeof filename)))
        return width;

    /* Handle combining glyphs */
    lineno = 0;
    while (fgets(line, sizeof line, fp)) {
        char *p;
        const char *cclass;

        lineno++;
        p = skip_space(line);
        if (*p == '#' || *p == '\0')
            continue;
        code1 = code2 = strtoul(p, &p, 16);
        if (p[0] == '.' && p[1] == '.') {
            code2 = strtoul(p + 2, &p, 16);
        }
        if (*p++ != ';')
            goto fail2;

        get_field(&p);   /* skip name */
        cclass = get_field(&p);
        if (*cclass == 'M') {
            if (code2 > CHARCODE_MAX)
                code2 = CHARCODE_MAX;
            for (code = code1; code <= code2; code++) {
                width[code] = 0/*cclass[0] | (cclass[1] << 8)*/;
            }
        }
        continue;
    fail2:
        fprintf(stderr, "%s:%d:invalid line\n%s", filename, lineno, line);
    }
    return width;
}

static int make_wcwidth_variant_table(genstate_t *gp) {
    /* Create wcwidth_variant table with the unicode version for the most
       recent glyph width definition.

       wcwidth_variant[ucs] contains the unicode version from which
       the most width definition of wcwidth(ucs) is defined
     */
    /* enumerate all unicode versions for the EastAsianWidth.txt table */
    int i, j;
    unsigned char *wcwidth_variant = calloc(sizeof(*wcwidth_variant), CHARCODE_MAX + 1);
    const char *latest_version = unicode_versions[countof(unicode_versions) - 1];
    unsigned short *latest_width;
    unsigned char version;
    char *p;

    latest_width = load_east_easian_width(gp, latest_version);

    set_range(wcwidth_variant, 0x1885, 0x1886, 1); // MONGOLIAN LETTER ALI GALI BALUDA
    set_range(wcwidth_variant, 0x1886, 0x1886, 1); // MONGOLIAN LETTER ALI GALI THREE BALUDA
    set_range(wcwidth_variant, 0x200C, 0x200C, 1); // ZERO WIDTH NON-JOINER
    //2028;LINE SEPARATOR;Zl;0;WS;;;;;N;;;;;
    //2029;PARAGRAPH SEPARATOR;Zp;0;B;;;;;N;;;;;
    //202A;LEFT-TO-RIGHT EMBEDDING;Cf;0;LRE;;;;;N;;;;;
    //202B;RIGHT-TO-LEFT EMBEDDING;Cf;0;RLE;;;;;N;;;;;
    //202C;POP DIRECTIONAL FORMATTING;Cf;0;PDF;;;;;N;;;;;
    //202D;LEFT-TO-RIGHT OVERRIDE;Cf;0;LRO;;;;;N;;;;;
    //202E;RIGHT-TO-LEFT OVERRIDE;Cf;0;RLO;;;;;N;;;;;
    //202F;NARROW NO-BREAK SPACE;Zs;0;CS;<noBreak> 0020;;;;N;;;;;
    set_range(wcwidth_variant, 0x2028, 0x202F, 1); // LINE SEPARATOR..NARROW NO-BREAK SPACE
    set_range(wcwidth_variant, 0x200D, 0x200D, 1); // ZERO WIDTH JOINER
    set_range(wcwidth_variant, 0x312E, 0x312E, 1); // BOPOMOFO LETTER O WITH DOT ABOVE
    set_range(wcwidth_variant, 0x312F, 0x312F, 1); // BOPOMOFO LETTER NN
    set_range(wcwidth_variant, 0x31BB, 0x31BB, 1); // BOPOMOFO FINAL LETTER G
    set_range(wcwidth_variant, 0x31BC, 0x31BC, 1); // BOPOMOFO LETTER GW
    set_range(wcwidth_variant, 0x31BD, 0x31BD, 1); // BOPOMOFO LETTER KW
    set_range(wcwidth_variant, 0x31BE, 0x31BE, 1); // BOPOMOFO LETTER OE
    set_range(wcwidth_variant, 0x31BF, 0x31BF, 1); // BOPOMOFO LETTER AH
    set_range(wcwidth_variant, 0x32FF, 0x32FF, 1); // SQUARE ERA NAME REIWA
    set_range(wcwidth_variant, 0xD800, 0xDB7F, 1); // High Surrogates
    set_range(wcwidth_variant, 0xDB80, 0xDBFF, 1); // High Private Use Surrogates
    set_range(wcwidth_variant, 0xDC00, 0xDFFF, 1); // Low Surrogates
    //set_range(wcwidth_variant, 0xF870, 0xF87F, 1);
    set_range(wcwidth_variant, 0xE000, 0xF8FF, 1); // Private Use Area
    set_range(wcwidth_variant, 0xFEFF, 0xFEFF, 1); // ZERO WIDTH NO-BREAK SPACE
    set_range(wcwidth_variant, 0x16FE0, 0x16FE3, 1); // TANGUT ITERATION MARK..OLD CHINESE ITERATION MARK
    set_range(wcwidth_variant, 0x17000, 0x187F7, 1); // Tangut Ideograph
    set_range(wcwidth_variant, 0x18800, 0x18AFF, 1); // TANGUT COMPONENT
    set_range(wcwidth_variant, 0x18B00, 0x18CD5, 1); // KHITAN SMALL SCRIPT
    set_range(wcwidth_variant, 0x18D00, 0x18D07, 1); // Tangut Ideograph Supplement
    set_range(wcwidth_variant, 0x18D08, 0x18D08, 1); // Tangut Ideograph Supplement
    set_range(wcwidth_variant, 0x1AFF0, 0x1AFFD, 1); // KATAKANA LETTER MINNAN
    set_range(wcwidth_variant, 0x1AFFE, 0x1AFFE, 1); // KATAKANA LETTER MINNAN
    set_range(wcwidth_variant, 0x1B002, 0x1B0FF, 1); // HENTAIGANA LETTER
    set_range(wcwidth_variant, 0x1B100, 0x1B121, 1); // HENTAIGANA LETTER
    set_range(wcwidth_variant, 0x1B122, 0x1B122, 1); // KATAKANA LETTER ARCHAIC
    set_range(wcwidth_variant, 0x1B132, 0x1B132, 1); // HIRAGANA LETTER SMALL
    set_range(wcwidth_variant, 0x1B150, 0x1B151, 1); // HIRAGANA LETTER SMALL
    set_range(wcwidth_variant, 0x1B152, 0x1B152, 1); // HIRAGANA LETTER SMALL
    set_range(wcwidth_variant, 0x1B155, 0x1B155, 1); // KATAKANA LETTER SMALL
    set_range(wcwidth_variant, 0x1B164, 0x1B167, 1); // KATAKANA LETTER SMALL
    set_range(wcwidth_variant, 0x1B170, 0x1B2FB, 1); // NUSHU CHARACTER
    set_range(wcwidth_variant, 0x1F23B, 0x1F23B, 1); // SKI AND SKI BOOT
    set_range(wcwidth_variant, 0x1F260, 0x1F265, 1); // ROUNDED SYMBOL FOR FU..ROUNDED SYMBOL FOR CAI
    set_range(wcwidth_variant, 0x1F57A, 0x1F57A, 1); // MAN DANCING
    set_range(wcwidth_variant, 0x1F5A4, 0x1F5A4, 1); // BLACK HEART
    set_range(wcwidth_variant, 0x1F6D1, 0x1F6D2, 1); // OCTAGONAL SIGN..SHOPPING TROLLEY
    set_range(wcwidth_variant, 0x1F6D5, 0x1F6D6, 1); // HINDU TEMPLE..HUT
    set_range(wcwidth_variant, 0x1F6D7, 0x1F6D7, 1); // ELEVATOR
    set_range(wcwidth_variant, 0x1F6DC, 0x1F6DF, 1); // WIRELESS..RING BUOY
    set_range(wcwidth_variant, 0x1F6F4, 0x1F6FB, 1); // SCOOTER..PICKUP TRUCK
    set_range(wcwidth_variant, 0x1F6FC, 0x1F6FC, 1); // ROLLER SKATE
    set_range(wcwidth_variant, 0x1F7E0, 0x1F7EB, 1); // LARGE ORANGE CIRCLE..LARGE BROWN SQUARE
    set_range(wcwidth_variant, 0x1F7F0, 0x1F7F0, 1); // HEAVY EQUALS SIGN
    set_range(wcwidth_variant, 0x1F90C, 0x1F90F, 1); // PINCHED FINGERS..PINCHING HAND
    set_range(wcwidth_variant, 0x1F919, 0x1F944, 1); // CALL ME HAND..SPOON
    set_range(wcwidth_variant, 0x1F945, 0x1F945, 1); // GOAL NET..GOAL NET
    set_range(wcwidth_variant, 0x1F947, 0x1F97E, 1); // FIRST PLACE MEDAL..HIKING BOOT
    set_range(wcwidth_variant, 0x1F97F, 0x1F97F, 1); // FLAT SHOE
    set_range(wcwidth_variant, 0x1F985, 0x1F9BE, 1); // EAGLE..MECHANICAL ARM
    set_range(wcwidth_variant, 0x1F9BF, 0x1F9BF, 1); // MECHANICAL LEG
    set_range(wcwidth_variant, 0x1F9C1, 0x1F9FE, 1); // CUPCAKE..RECEIPT
    set_range(wcwidth_variant, 0x1F9FF, 0x1F9FF, 1); // NAZAR AMULET
    set_range(wcwidth_variant, 0x1FA70, 0x1FA7B, 1); // BALLET SHOES..X-RAY
    set_range(wcwidth_variant, 0x1FA7C, 0x1FA7C, 1); // CRUTCH
    set_range(wcwidth_variant, 0x1FA80, 0x1FA87, 1); // YO-YO..MARACAS
    set_range(wcwidth_variant, 0x1FA88, 0x1FA88, 1); // FLUTE
    set_range(wcwidth_variant, 0x1FA90, 0x1FAC5, 1); // RINGED PLANET..PERSON WITH CROWN
    set_range(wcwidth_variant, 0x1FACE, 0x1FADB, 1); // MOOSE..PEA POD
    set_range(wcwidth_variant, 0x1FAE0, 0x1FAE7, 1); // MELTING FACE..BUBBLES
    set_range(wcwidth_variant, 0x1FAE8, 0x1FAE8, 1); // SHAKING FACE
    set_range(wcwidth_variant, 0x1FAF0, 0x1FAF7, 1); // HAND WITH INDEX FINGER AND THUMB CROSSED..LEFTWARDS PUSHING HAND
    set_range(wcwidth_variant, 0x1FAF8, 0x1FAF8, 1); // RIGHTWARDS PUSHING HAND

    for (i = 0; i < countof(unicode_versions)- 1; i++) {
        const char *version_str = unicode_versions[i];
        unsigned short *width = load_east_easian_width(gp, version_str);
        if (!width)
            continue;
        version = 10 * strtol(version_str, &p, 10);
        if (*p == '.')
            version += strtol(p + 1, &p, 10);
        /* track glyphs with non standard width */
        for (j = 0; j < CHARCODE_MAX + 1; j++) {
            if (latest_width[j] != width[j])
                wcwidth_variant[j] = 1;
        }
        free(width);
    }
    // XXX: special case 200C ZWNJ, 200D ZWJ w=0
    //      0x03000 IDEOGRAPHIC SPACE w=2
    //      D800-DFFF Surrogates w=1
    //      E000-F8FF Private Use Area w=1
    //      FEFF BOM w=0
    fprintf(gp->fc, "\n/* This table was generated from EastAsianWidth-%s.txt */\n", latest_version);

    generate_table(gp, wcwidth_variant, "wcwidth_variant", "qe_wcwidth_variant", gp->level);
    free(latest_width);
    free(wcwidth_variant);
    return 0;
}

/*---------------- Unicode bidir tables from UnicodeData ----------------*/

#define BIDIR_CLASSES(X) \
    X(LTR,  "Left-To-Right letter")      \
    X(RTL,  "Right-To-Left letter")      \
    X(WL,   "Weak left to right")        \
    X(WR,   "Weak right to left")        \
    X(EN,   "European Numeral")          \
    X(ES,   "European number Separator") \
    X(ET,   "European number Terminator")\
    X(AN,   "Arabic Numeral")            \
    X(CS,   "Common Separator")          \
    X(BS,   "Block Separator")           \
    X(SS,   "Segment Separator")         \
    X(WS,   "Whitespace")                \
    X(AL,   "Arabic Letter")             \
    X(NSM,  "Non Spacing Mark")          \
    X(BN,   "Boundary Neutral")          \
    X(ON,   "Other Neutral")             \
    X(LRE,  "Left-to-Right Embedding")   \
    X(RLE,  "Right-to-Left Embedding")   \
    X(PDF,  "Pop Directional Flag")      \
    X(LRO,  "Left-to-Right Override")    \
    X(RLO,  "Right-to-Left Override")    \
    X(LRI,  "Left-to-Right Isolate")     \
    X(RLI,  "Right-to-Left Isolate")     \
    X(FSI,  "First-Strong Isolate")      \
    X(PDI,  "Pop Directional Isolate")

#define X(e,s) e,
enum bidir_class { BIDIR_CLASSES(X) };
#undef X

#define X(e,s) #e,
static const char * const bidir_class_names[] = { BIDIR_CLASSES(X) };
#undef X

#if 0
/* scans a comma separated list of entries, return index of match or -1 */
/* CG: very similar to strfind */
static int get_enum(const char *str, const char *enum_str) {
    int val, len;
    const char *s, *s0;

    len = strlen(str);
    s = enum_str;
    val = 0;
    for (;;) {
        for (s0 = s; *s && *s != ','; s++)
            continue;
        if ((s - s0) == len && !memcmp(s0, str, len))
            return val;
        if (!*s)
            break;
        s++;
        val++;
    }
    return -1;
}
#endif

static int find_enum(const char *str, const char * const *tab, int count) {
    int i;
    for (i = 0; i < count; i++) {
        if (strequal(str, tab[i]))
            return i;
    }
    return -1;
}

static int make_bidir_table(genstate_t *gp) {
    char filename[1024];
    char line[1024];
    FILE *fp;
    unsigned char *bidir_type;
    int c, lineno, pos, count, count2, last_bidir;
    unsigned long code, code1, code2;

    if (!(fp = open_unicode_file(gp->unicode_dir, gp->unicode_version,
                                 "UnicodeData.txt",
                                 filename, sizeof filename)))
        return 1;

    bidir_type = calloc(sizeof(*bidir_type), CHARCODE_MAX + 1);
    /* set default bidir_type values */
    set_range(bidir_type, 0, CHARCODE_MAX, 0);
    set_range(bidir_type, 0x0590, 0x0600, RTL);
    set_range(bidir_type, 0x07C0, 0x0900, RTL);
    set_range(bidir_type, 0xFB1D, 0xFB50, RTL);
    set_range(bidir_type, 0x0600, 0x07C0, AL);
    set_range(bidir_type, 0xFB50, 0xFDD0, AL);
    set_range(bidir_type, 0xFDF0, 0xFE00, AL);
    set_range(bidir_type, 0xFE70, 0xFF00, AL);
    set_range(bidir_type, 0x2060, 0x2070, BN);
    set_range(bidir_type, 0xFDD0, 0xFDF0, BN);
    set_range(bidir_type, 0xFFF0, 0xFFF9, BN);
    for (c = 0; c <= CHARCODE_MAX; c += 0x10000)
        bidir_type[c + 0xFFFE] = bidir_type[c + 0xFFFF] = BN;
    if (CHARCODE_MAX >= 0x10000) {
        set_range(bidir_type, 0x10800, 0x11000, RTL);
        set_range(bidir_type, 0xE0000, 0xE1000, BN);
    }

    lineno = 0;
    while (fgets(line, sizeof line, fp)) {
        char *p;
        const char *bidir_class_name;
        int bidir_class_type;

        lineno++;
        p = skip_space(line);
        if (*p == '#' || *p == '\0')
            continue;
        code1 = code2 = strtoul(p, &p, 16);
        if (p[0] == '.' && p[1] == '.') {
            code2 = strtoul(p + 2, &p, 16);
        }
        if (*p++ != ';')
            goto fail;

        get_field(&p);   /* skip name */
        get_field(&p);   /* skip cclass */
        get_field(&p);   /* skip num value */
        bidir_class_name = get_field(&p);
        if (bidir_class_name[1] == '\0') {
            /* handle aliasses explicitly */
            switch (bidir_class_name[0]) {
            case 'L': bidir_class_name = "LTR"; break;
            case 'R': bidir_class_name = "RTL"; break;
            case 'B': bidir_class_name = "BS";  break;
            case 'S': bidir_class_name = "SS";  break;
            }
        }
        bidir_class_type = find_enum(bidir_class_name, bidir_class_names, countof(bidir_class_names));
        if (bidir_class_type < 0) {
            fprintf(stderr, "%s:%d:unknown bidir class name: %s\n", filename, lineno, bidir_class_name);
        } else {
            if (code1 != code2)
                set_range(bidir_type, code1, code2, bidir_class_type);
            else
                bidir_type[code1] = bidir_class_type;
        }
        continue;
    fail:
        fprintf(stderr, "%s:%d:invalid line\n%s", filename, lineno, line);
    }
#if 0
    last_bidir = -1;
    count1 = 0;
    for (code = 0; code <= 255; code++) {
        if (last_bidir != bidir_type[code]) {
            count1++;
            last_bidir = bidir_type[code];
        }
    }
#endif
    last_bidir = -1;
    count2 = 0;
    for (code = 256; code <= CHARCODE_MAX; code++) {
        if (last_bidir != bidir_type[code]) {
            count2++;
            last_bidir = bidir_type[code];
        }
    }
    count = 256 + count2 * 4;

    fprintf(gp->fc, "\n" "/* Tables generated from %s: %d bytes */\n", filename, count);

    /* output the enum values */
    /* public type with package name prefixes */
#define X(e,s)  fprintf(gp->fi, "    QE_BIDIR_%s, /* %s */\n", #e, s);
    fprintf(gp->fi, "enum qe_bidir_class {\n");
    BIDIR_CLASSES(X);
    fprintf(gp->fi, "};\n");
#undef X

    /* private type with shorter aliases */
#define X(e,s)  fprintf(gp->fc, "    %s = QE_BIDIR_%s, /* %s */\n", #e, #e, s);
    fprintf(gp->fc, "\n" "enum bidir_class {\n");
    BIDIR_CLASSES(X);
    fprintf(gp->fc, "};\n");
#undef X

    /* output a direct table for single byte code points */
    pos = 0;
    last_bidir = -1;
    fprintf(gp->fc, "\n" "static const unsigned char bidir_table_00[%d] = {", 256);
    for (code = 0; code <= 255; code++) {
        if (!(pos++ % 8)) fprintf(gp->fc, "\n   ");
        if (gp->use_names) {
            fprintf(gp->fc, " %3s,", bidir_class_names[bidir_type[code]]);
        } else {
            fprintf(gp->fc, " 0x%02x,", bidir_type[code]);
        }
    }
    fprintf(gp->fc, "\n};\n");

    /* output a composite table for other code points */
    fprintf(gp->fc, "\n" "static const unsigned int bidir_table[%d] = {", count2);
    if (gp->use_names)
        fprintf(gp->fc, "\n" "#define X(c,v)  (((c) << 8) | (v))");
    pos = 0;
    last_bidir = -1;
    for (code = 256; code <= CHARCODE_MAX; code++) {
        if (last_bidir != bidir_type[code]) {
            /* output combined table */
            if (!(pos++ % 4)) fprintf(gp->fc, "\n   ");
            if (gp->use_names) {
                fprintf(gp->fc, " X(0x%06lx, %3s),", code, bidir_class_names[bidir_type[code]]);
            } else {
                fprintf(gp->fc, " 0x%08lx,", (code << 8) | bidir_type[code]);
            }
            last_bidir = bidir_type[code];
        }
    }
    if (gp->use_names)
        fprintf(gp->fc, "\n" "#undef X");
    fprintf(gp->fc, "\n};\n");
#if 0
    fprintf(gp->fc, "\n" "static const unsigned int property_start[%d] = {", count);
    pos = 0;
    last_bidir = -1;
    for (code = 0; code <= CHARCODE_MAX; code++) {
        if (last_bidir != bidir_type[code]) {
            if (!(pos++ % 8)) fprintf(gp->fc, "\n   ");
            fprintf(gp->fc, " 0x%04lx,", code);
            last_bidir = bidir_type[code];
        }
    }
    fprintf(gp->fc, "\n};\n");

    fprintf(gp->fc, "\n" "static const unsigned char property_val[%d] = {", count);
    pos = 0;
    last_bidir = -1;
    for (code = 0; code <= CHARCODE_MAX; code++) {
        if (last_bidir != bidir_type[code]) {
            if (!(pos++ % 8)) fprintf(gp->fc, "\n   ");
            if (gp->use_names) {
                fprintf(gp->fc, " %3s,", bidir_class_names[bidir_type[code]]);
            } else {
                fprintf(gp->fc, " 0x%02x,", bidir_type[code]);
            }
            last_bidir = bidir_type[code];
        }
    }
    fprintf(gp->fc, "\n};\n");
#endif
    /* use a binary lookup loop */
    // XXX: this is inefficient and slow
    //      should use multi-level indirect tables
#if 00
    enum qe_bidir_class qe_bidir_get_type(unsigned int ch) {
        int a, b;
        if (ch < 256)
            return bidir_table_00[ch];
        if (ch > CHARCODE_MAX)
            return LTR;
        a = 0;
        b = countof(bidir_table) - 1;
        while (a < b) {
            int m = (a + b) >> 1;
            if (ch < bidir_table[m] >> 8)
                b = m;
            else
                a = m;
        }
        return bidir_table[a] & 0xFF;
    }

    /* version for test with ASCII chars */
    static inline enum qe_bidir_class qe_bidir_get_type_test(unsigned int ch) {
        if (ch >= 'A' && ch <= 'Z')
            return QE_BIDIR_RTL;
        else
            return qe_bidir_get_type(ch);
    }
#else
    /* function implementation */
    fprintf(gp->fc, "\n");
    fprintf(gp->fc, "enum qe_bidir_class qe_bidir_get_type(unsigned int ch) {\n");
    fprintf(gp->fc, "    int a, b;\n");
    fprintf(gp->fc, "    if (ch < 256)\n");
    fprintf(gp->fc, "        return bidir_table_00[ch];\n");
    fprintf(gp->fc, "    if (ch > CHARCODE_MAX)\n");
    fprintf(gp->fc, "        return LTR;\n");
    fprintf(gp->fc, "    a = 0;\n");
    fprintf(gp->fc, "    b = countof(bidir_table) - 1;\n");
    fprintf(gp->fc, "    while (a < b) {\n");
    fprintf(gp->fc, "        int m = (a + b) >> 1;\n");
    fprintf(gp->fc, "        if (ch < bidir_table[m] >> 8)\n");
    fprintf(gp->fc, "            b = m;\n");
    fprintf(gp->fc, "        else\n");
    fprintf(gp->fc, "            a = m;\n");
    fprintf(gp->fc, "    }\n");
    fprintf(gp->fc, "    return bidir_table[a] & 0xFF;\n");
    fprintf(gp->fc, "}\n");
    /* function prototypes */
    fprintf(gp->fi, "\n");
    fprintf(gp->fi, "enum qe_bidir_class qe_bidir_get_type(unsigned int ch);\n");
    fprintf(gp->fi, "\n");
    fprintf(gp->fi, "/* version for test with ASCII chars */\n");
    fprintf(gp->fi, "static inline enum qe_bidir_class qe_bidir_get_type_test(unsigned int ch) {\n");
    fprintf(gp->fi, "    if (ch >= 'A' && ch <= 'Z')\n");
    fprintf(gp->fi, "        return QE_BIDIR_RTL;\n");
    fprintf(gp->fi, "    else\n");
    fprintf(gp->fi, "        return qe_bidir_get_type(ch);\n");
    fprintf(gp->fi, "}\n");
#endif
    free(bidir_type);
    return 0;
}

static int usage(const char *name) {
    fprintf(stderr,
            "usage: %s [-V version] [-D dir] [-c file] [-i file] [-{1,2,3}] -{b,w,S,W}\n"
            "options:\n"
            "  -V version  specify the version suffix for the Unicode files to load\n"
            "  -D dir      specify the directory from which to load the Unicode files\n"
            "  -i file     specify the name of the include file to generate\n"
            "  -c file     specify the name of the source file to generate\n"
            "  -1 -2 -3    specify table structure: 1=skip, 2/3 levels indirect\n"
            "  -b          generate the bidir_tables.h file from the Unicode files\n"
            "     -a       additional flag to use enum values in tables\n"
            "  -w          generate the qe_wcwidth function from the Unicode files\n"
            "  -S          generate the qe_wcwidth_variant function from the Unicode files\n"
            "  -W          generate the qe_wcwidth function from TTY get cursor responses\n"
            , name);
    return 1;
}

int main(int argc, char *argv[]) {
    genstate_t gp[1] = {{
        NULL, "unidata", NULL, NULL, 0,
        stdout, stdout, 1, argc, argv,
        0, 0x20, CHARCODE_MAX
    }};
    int tasks = 0;
#define MAKE_WCWIDTH_TABLE          (1 << 0)
#define MAKE_TTY_WIDTH_TABLE        (1 << 1)
#define MAKE_BIDIR_TABLE            (1 << 2)
#define MAKE_WCWIDTH_VARIANT_TABLE  (1 << 3)

    while (has_arg(gp)) {
        char *arg = get_arg(gp);
        if (strequal(arg, "-V") && has_arg(gp)) {
            gp->unicode_version = get_arg(gp);
        } else
        if (strequal(arg, "-D") && has_arg(gp)) {
            gp->unicode_dir = get_arg(gp);
        } else
        if (strequal(arg, "-i") && has_arg(gp)) {
            gp->include_file = get_arg(gp);
        } else
        if (strequal(arg, "-c") && has_arg(gp)) {
            gp->source_file = get_arg(gp);
        } else
        if (strequal(arg, "-1") || strequal(arg, "-2") || strequal(arg, "-3")) {
            gp->level = arg[1] - '0';
        } else
        if (strequal(arg, "-b")) {
            tasks |= MAKE_BIDIR_TABLE;
            if (has_arg(gp) && !strcmp(peek_arg(gp), "-a")) {
                gp->use_names = 1;
                next_arg(gp);
            }
        } else
        if (strequal(arg, "-w")) {
            tasks |= MAKE_WCWIDTH_TABLE;
        } else
        if (strequal(arg, "-S")) {
            tasks |= MAKE_WCWIDTH_VARIANT_TABLE;
        } else
        if (strequal(arg, "-W")) {
            tasks |= MAKE_TTY_WIDTH_TABLE;
            if (has_arg(gp) && isdigit((unsigned char)*peek_arg(gp)))
                gp->filter_start = strtoul(get_arg(gp), NULL, 16);
            if (has_arg(gp) && isdigit((unsigned char)*peek_arg(gp)))
                gp->filter_end = strtoul(get_arg(gp), NULL, 16);
        } else {
            fprintf(stderr, "%s: unknown option %s\n", NAME, arg);
            return usage(NAME);
        }
    }
    if (tasks) {
        char header_guard[32];
        int i, j;
        unsigned char uc;

        if (gp->include_file) {
            if (!(gp->fi = fopen_verbose(gp->include_file, "w")))
                return 1;
            fprintf(gp->fi, "/* This file was generated automatically by %s */\n\n", NAME);
            j = 0;
            header_guard[j++] = 'Q';
            header_guard[j++] = 'E';
            header_guard[j++] = '_';
            for (i = 0; (uc = gp->include_file[i]) != '\0'; i++) {
                if (uc == '.' || uc == '-')
                    uc = '_';
                else
                    uc = toupper(uc);
                header_guard[j++] = uc;
            }
            header_guard[j] = '\0';
            fprintf(gp->fi, "#ifndef %s\n", header_guard);
            fprintf(gp->fi, "#define %s\n\n", header_guard);
        }
        if (gp->source_file) {
            if (!(gp->fc = fopen_verbose(gp->source_file, "w")))
                return 1;
            fprintf(gp->fc, "/* This file was generated automatically by %s */\n", NAME);
            if (gp->include_file) {
                fprintf(gp->fc, "\n");
                fprintf(gp->fc, "#include \"%s\"\n", gp->include_file);
            }
        }
        if (tasks & MAKE_WCWIDTH_TABLE) {
            if (make_wcwidth_table(gp))
                return 1;
        }
        if (tasks & MAKE_WCWIDTH_VARIANT_TABLE) {
            if (make_wcwidth_variant_table(gp))
                return 1;
        }
        if (tasks & MAKE_TTY_WIDTH_TABLE) {
            if (make_tty_width_table(gp))
                return 1;
        }
        if (tasks & MAKE_BIDIR_TABLE) {
            if (make_bidir_table(gp))
                return 1;
        }
        if (gp->include_file) {
            fprintf(gp->fi, "\n#endif /* %s */\n", header_guard);
        }
        return 0;
    } else {
        return usage(NAME);
    }
}
