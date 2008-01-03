/*
 * Convert Yudit kmap files to QEmacs binary internal format
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
#include <ctype.h>
#include <assert.h>

#ifndef countof
#define countof(a)  ((int)(sizeof(a) / sizeof((a)[0])))
#endif

//#define TEST

#define NB_MAX 15000

typedef struct InputEntry {
    unsigned char input[20];
    int len;
    unsigned short output[20];
    int olen;
} InputEntry;

static InputEntry inputs[NB_MAX];
static int nb_inputs;
static char kmap_names[300][128];
static int kmap_offsets[300];
static int nb_kmaps;
static char name[128];
static int table_start[NB_MAX], nb_starts;
static int table_val[NB_MAX];
static int is_chinese_cj, gen_table;
static int freq[32];
static FILE *outfile;
static unsigned char outbuf[100000], *outbuf_ptr;

static int sort_func(const void *a1, const void *b1)
{
    const InputEntry *a = a1;
    const InputEntry *b = b1;
    int val;

    if (gen_table) {
        val = a->input[0] - b->input[0];
        if (val != 0)
            return val;
    }
    return a->output[0] - b->output[0];
}


static int offset, write_data;

static void put_byte(int b)
{
    if (write_data)
        *outbuf_ptr++ = b;
    offset++;
}

static void gen_map(void)
{
    int len, c, d, last, delta, i, k, j;
    int last_input0;
    InputEntry *ip;

    last = 0;
    nb_starts = 0;
    last_input0 = 0;
    offset = 0;
    for (k = 0, ip = inputs; k < nb_inputs; k++, ip++) {
        unsigned char *input = ip->input;
        int first, output;

        if (gen_table) {
            if (last_input0 != ip->input[0]) {
                if (last_input0 != 0)
                    put_byte(0);
                last_input0 = ip->input[0];
                table_val[nb_starts] = last_input0;
                table_start[nb_starts++] = offset;
                last = 0;
            }
        }

        len = ip->len;
        output = ip->output[0];
        delta = output - last;
        last = output;

#ifdef TEST
        /* frequencies */
        if (delta > 30)
            delta = 30;
        else if (delta < -1)
            delta = -1;
        freq[delta + 1]++;
#endif

        if (is_chinese_cj) {
            assert(input[len-1] == ' ');
            len--;
        }

        first = 0;
        if (gen_table)
            first = 1;

        /* c = 0x00        end of table
         * c = 0x01..0x1d  delta unicode
         * c = 0x1e        unicode output mapping follows
         * c = 0x1f        unicode input char follows.
         * c = 0x20..0x7f  input character
         * c = 0x80        unused
         * c = 0x81        unused
         * c = 0x82..0x9d  extra unicode outputs follow
         * c = 0x9e        first unicode output mapping follows
         * c = 0x9f        last unicode input char follows and delta==1.
         * c = 0xa0..0xff  last input character and delta==1
         */
        for (j = first; j < len; j++) {
            c = input[j];
            d = 0;
            if (j == (len - 1) && delta == 1 && ip->olen == 1)
                d = 0x80;
            if (c >= 0x20 && c <= 0x7f) {
                put_byte(c | d);
            } else {
                put_byte(d | 0x1f);
                put_byte((c >> 8) & 0xff);
                put_byte(c & 0xff);
            }
        }
        if (ip->olen > 1) {
            if (delta != 0) {
                put_byte(0x80 | 0x1e);
                put_byte((output >> 8) & 0xff);
                put_byte(output & 0xff);
            }
            put_byte(0x80 | ip->olen);
            for (i = 1; i < ip->olen; i++) {
                put_byte((ip->output[i] >> 8) & 0xff);
                put_byte(ip->output[i] & 0xff);
            }
        } else {
            /* XXX: potential problem if first == len. We avoid it
            by forcing an emission of a unicode char */
            if (first == len)
                delta = 0;
            if (delta != 1) {
                if (delta >= 1 && delta <= 0x1d) {
                    put_byte(delta);
                } else {
                    put_byte(0x1e);
                    put_byte((output >> 8) & 0xff);
                    put_byte(output & 0xff);
                }
            }
        }
    }
    put_byte(0);
}

static void putcp(int c, int *sp)
{
    switch (c) {
    case '=':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        goto x2;
    case '\\':
    case '"':
    case '-':
    case '+':
        printf("\\%c", c);
        *sp = 0;
        break;
    default:
        if (c >= 256) {
            if (!*sp)
                printf(" ");
            printf("0x%04X ", c);
            *sp = 1;
            break;
        } else
        if (c <= 0x20 || c >= 0x7F) {
        x2:
            if (!*sp)
                printf(" ");
            printf("0x%02X ", c);
            *sp = 1;
            break;
        } else {
            printf("%c", c);
            *sp = 0;
            break;
        }
    }
}

static int dump_kmap(const char *filename)
{
    FILE *f;
    char buf[1024];
    int i, n, c, off, pos, x;

    f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "kmaptoqe: cannot open %s\n", filename);
        return 1;
    }
    if (fread(buf, 4, 1, f) != 1 || memcmp(buf, "kmap", 4)) {
        fprintf(stderr, "kmaptoqe: invalid signature %s\n", filename);
        fclose(f);
        return 1;
    }
    printf("// Dump of QEmacs kmap file %s\n"
           "kmap {\n", filename);
    printf("    {\n");
    for (nb_kmaps = 0;; nb_kmaps++) {
        off  = getc(f) << 24;
        off |= getc(f) << 16;
        off |= getc(f) << 8;
        off |= getc(f) << 0;
        if (off == 0)
            break;
        kmap_offsets[nb_kmaps] = off;
        for (i = 0;; i++) {
            c = getc(f);
            if (c == EOF || i >= countof(kmap_names[nb_kmaps])) {
                fprintf(stderr, "%s: invalid map name\n", filename);
                fclose(f);
                return 1;
            }
            kmap_names[nb_kmaps][i] = c;
            if (c == 0)
                break;
        }
        printf("        0x%04x: %s\n", off, kmap_names[nb_kmaps]);
    }
    printf("    }\n");

    pos = ftell(f);

    for (n = x = 0;;) {

        if (pos == kmap_offsets[n]) {

            if (x > 0) {
                printf("\n");
                x = 0;
            }
            if (n > 0) {
                printf("        }\n");
                printf("    }\n");
            }
            printf("\n    { // %s\n", kmap_names[n]);
            c = getc(f);
            pos++;
            nb_starts = c & 0x7F;
            is_chinese_cj = c >> 7;
            gen_table = (nb_starts > 0);

            printf("        nb_starts=%d, is_chinese_cj=%d\n",
                   nb_starts, is_chinese_cj);

            if (gen_table) {
                printf("        {\n");
                for (i = 0; i < nb_starts; i++) {
                    c = getc(f);
                    off  = getc(f) << 16;
                    off |= getc(f) << 8;
                    off |= getc(f) << 0;
                    pos += 4;
                    table_val[i] = c;
                    table_start[i] = off;
                    printf("            table_val[%d]=0x%02x ('%c'), table_start[%d]=0x%04x\n",
                           i, c, c, i, off);
                }
                printf("        }\n");
            }
            n++;
            printf("        {\n");
            x = 0;

            if (1) {
                int len, olen, flag, k, s, last = 0, sp;

                s = 0;
                for (k = 0;; k++) {
                    unsigned char *input;

                    input = inputs[k].input;
                    olen = 1;
                    len = 0;
                    sp = 1;

                nextc:
                    c = getc(f);
                    pos++;
                    if (c == EOF)
                        break;

                    /* c = 0x00        end of table
                     * c = 0x01..0x1d  delta unicode
                     * c = 0x1e        unicode output mapping follows
                     * c = 0x1f        unicode input char follows.
                     * c = 0x20..0x7f  input character
                     * c = 0x80        unused
                     * c = 0x81        unused
                     * c = 0x82..0x9d  extra unicode outputs follow
                     * c = 0x9e        first unicode output mapping follows
                     * c = 0x9f        last unicode input char follows and delta==1.
                     * c = 0xa0..0xff  last input character and delta==1
                     */
                    if (c == 0) {
                        if (!gen_table || ++s >= nb_starts)
                            break;
                        last = 0;
                        goto nextc;
                    }

                    if (len == 0) {
                        printf("            \"");
                        if (gen_table) {
                            putcp(table_val[s], &sp);
                        }
                    }

                    flag = c >> 7;
                    c &= 0x7f;

                    if (c < 0x1e) {
                        if (flag) {
                            olen = c;
                        } else {
                            last += c;
                        }
                        goto nextk;
                    }
                    if (c == 0x1e) {
                        last  = getc(f) << 8;
                        last |= getc(f) << 0;
                        pos += 2;
                        if (flag)
                            goto nextc;
                        else
                            goto nextk;
                    }
                    if (c == 0x1f) {
                        c  = getc(f) << 8;
                        c |= getc(f) << 0;
                        pos += 2;
                        input[len++] = c;
                        putcp(c, &sp);
                        if (flag) {
                            last += 1;
                            goto nextk;
                        }
                        goto nextc;
                    }
                    if (c >= 0x20) {
                        input[len++] = c;
                        putcp(c, &sp);
                        if (flag) {
                            last += 1;
                            goto nextk;
                        }
                        goto nextc;
                    }
                nextk:
                    if (is_chinese_cj) {
                        putcp(' ', &sp);
                    }
                    if (!sp)
                        printf(" ");
                    printf("= 0x%04X", last);
                    /* multiple unicode chars */
                    for (i = 1; i < olen; i++) {
                        c  = getc(f) << 8;
                        c |= getc(f) << 0;
                        pos += 2;
                        printf(" 0x%04X", c);
                    }
                    printf("\",\n");
                }
            }
            if (pos == kmap_offsets[n])
                continue;
        }

        if ((c = getc(f)) == EOF)
            break;
        pos++;

        if (x == 0)
            printf("            ");
        printf("0x%02x, ", c);
        if (++x == 8) {
            printf("\n");
            x = 0;
        }
    }

    if (x > 0)
        printf("\n");

    if (n > 0) {
        printf("        }\n");
        printf("    }\n");
    }

    printf("}\n");
    fclose(f);
    return 0;
}

static inline char *skipspaces(char *p) {
    while (isspace((unsigned char)*p))
        p++;
    return p;
}

static int getcp(char *p, char **pp)
{
    if (*p == '\0') {
        return -1;
    } else
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        return strtoul(p, pp, 0);
    } else
    if (*p == '\\') {
        p++;
        if (*p == 't') {
            *pp = p + 1;
            return '\t';
        } else
        if (p[0] >= '0' && p[0] <= '3'
        &&  p[1] >= '0' && p[1] <= '7'
        &&  p[2] >= '0' && p[2] <= '7') {
            *pp = p + 3;
            return ((p[0] - '0') << 6) | ((p[1] - '0') << 3) |
                    ((p[2] - '0') << 0);
        } else {
            *pp = p + 1;
            return *p;
        }
    } else
    if ((*p & 0xe0) == 0xc0 && (p[1] & 0xc0) == 0x80) {
        /* UTF-8 character */
        *pp = p + 2;
        return ((*p - 0xc0) << 6) | ((p[1] - 0x80) << 0);
    } else {
        *pp = p + 1;
        return *p;
    }
}

int main(int argc, char **argv)
{
    char *filename;
    int i, j, k, col, line_num, len;
    FILE *f;
    char line[1024], *p;
    unsigned char *q;
    int c, size;
    InputEntry *ip;

    if (argc < 3) {
        printf("usage: kmaptoqe outfile kmaps...\n"
               "Convert yudit keyboard maps to qemacs compressed format\n");
        exit(1);
    }
    if (!strcmp(argv[1], "--dump")) {
        return dump_kmap(argv[2]);
    }

    outfile = fopen(argv[1], "wb");
    if (!outfile) {
        perror(argv[1]);
        exit(1);
    }
    outbuf_ptr = outbuf;

    memset(freq, 0, sizeof(freq));

    nb_kmaps = 0;
    for (i = 2; i < argc; i++) {
        filename = argv[i];
        f = fopen(filename, "r");
        if (!f) {
            perror(filename);
            exit(1);
        }
        p = strrchr(filename, '/');
        if (!p)
            p = filename;
        else
            p++;

        strcpy(name, p);
        for (p = name; *p != '\0' && *p != '.'; p++) {
            if (*p == '-')
                *p = '_';
        }
        *p = '\0';
        strcpy(kmap_names[nb_kmaps], name);
        kmap_offsets[nb_kmaps] = outbuf_ptr - outbuf;
        nb_kmaps++;
        /* special compression for CJ */
        is_chinese_cj = !strcmp(name, "Chinese_CJ");
        gen_table = !strcmp(name, "Chinese_CJ") ||
                    !strcmp(name, "TeX") ||
                    !strcmp(name, "Troff") ||
                    !strcmp(name, "SGML");

        col = 0;
        nb_inputs = 0;
        ip = inputs;
        line_num = 0;
        for (;;) {
            if (fgets(line, sizeof(line), f) == NULL)
                break;
            line_num++;
            p = skipspaces(line);
            if (*p == '\0' || *p == '/' || *p == '#')
                continue;
            if (*p != '\"')
                goto invalid;
            p++;
            len = 0;
            q = ip->input;
            for (;;) {
                p = skipspaces(p);
                if (*p == '=' && p[1] != '=')
                    break;
                c = getcp(p, &p);
                if (c < 0)
                    goto invalid;
                if (c >= 256) {
                    fprintf(stderr, "%s:%d: Invalid char 0x%x\n",
                            filename, line_num, c);
                    goto skip;
                }
                if (len >= countof(ip->input))
                    goto invalid;
                q[len++] = c;
            }
            ip->len = len;
            p = skipspaces(p + 1);
            for (ip->olen = 0;;) {
                c = getcp(p, &p);
                if (c < 0)
                    goto invalid;
                if (ip->olen >= countof(ip->output))
                    goto invalid;
                ip->output[ip->olen++] = c;
                p = skipspaces(p);
                if (*p == '"')
                    break;
            }
            ip++;
            nb_inputs++;
            continue;
        invalid:
            fprintf(stderr, "%s:%d: Invalid mapping: %s\n",
                    filename, line_num, line);
        skip:;
        }

        qsort(inputs, nb_inputs, sizeof(InputEntry), sort_func);

        /* first pass to compute offsets */
        write_data = 0;
        gen_map();

        write_data = 1;

        /* write start map */
        put_byte(nb_starts | (is_chinese_cj ? 0x80 : 0));
        if (gen_table) {
            for (k = 0; k < nb_starts; k++) {
                j = table_start[k];
                put_byte(table_val[k]);
                put_byte((j >> 16) & 0xff);
                put_byte((j >> 8) & 0xff);
                put_byte(j & 0xff);
            }
        }

        gen_map();

        fclose(f);
    }

#ifdef TEST
    for (k = 0; k < 32; k++) {
        fprintf(stderr, "freq[%d] = %d\n", k, freq[k]);
    }
#endif

    /* write header */
    size = 4;
    for (i = 0; i < nb_kmaps; i++) {
        size += strlen(kmap_names[i]) + 1 + 4;
    }
    size += 4; /* last offset at zero */

    fwrite("kmap", 1, 4, outfile);

    for (i = 0; i < nb_kmaps; i++) {
        int off;
        off = kmap_offsets[i] + size;
        fputc((off >> 24) & 0xff, outfile);
        fputc((off >> 16) & 0xff, outfile);
        fputc((off >> 8) & 0xff, outfile);
        fputc((off >> 0) & 0xff, outfile);
        fwrite(kmap_names[i], 1, strlen(kmap_names[i]) + 1, outfile);
    }
    fputc(0, outfile);
    fputc(0, outfile);
    fputc(0, outfile);
    fputc(0, outfile);

    fwrite(outbuf, 1, outbuf_ptr - outbuf, outfile);

    fclose(outfile);

    return 0;
}
