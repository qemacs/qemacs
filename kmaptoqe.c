/* convert yudit kmap files to qe binary internal format */

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>

//#define TEST

#define NB_MAX 15000

typedef struct InputEntry {
    unsigned char input[10];
    int len;
    int output;
} InputEntry;

InputEntry inputs[NB_MAX];
int nb_inputs;
char kmap_names[300][128];
int kmap_offsets[300];
char name[128];
int table_start[NB_MAX], nb_starts;
int table_val[NB_MAX];
int is_chinese_cj, gen_table;
int freq[32];
FILE *outfile;
unsigned char outbuf[100000], *outbuf_ptr;

int sort_func(const void *a1, const void *b1)
{
    const InputEntry *a = a1;
    const InputEntry *b = b1;
    int val;

    if (is_chinese_cj) {
        val = a->input[0] - b->input[0];
        if (val != 0)
            return val;
    }
    return a->output - b->output;
}


int offset, write_data;

void put_byte(int b)
{
    if (write_data) 
        *outbuf_ptr++ = b;
    offset++;
}

void gen_map(void)
{
    int len, c, d, last, delta, k, j;
    int last_input0;

        last = 0;
        nb_starts = 0;
        last_input0 = 0;
        offset = 0;
        for(k=0;k<nb_inputs;k++) {
            unsigned char *input = inputs[k].input;
            int first, output;

            if (gen_table) {
                if (last_input0 != inputs[k].input[0]) {
                    if (last_input0 != 0)
                        put_byte(0);
                    last_input0 = inputs[k].input[0];
                    table_val[nb_starts] = last_input0;
                    table_start[nb_starts++] = offset;
                    last = 0;
                }
            }

            len = inputs[k].len;
            output = inputs[k].output;
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
            
            /* c = 0 : end of table 
               c = 1..0x1d : delta unicode
               c = 0x1e : unicode char follows.
               c = 0x1f : unicode mapping follows 
               bit 7 : eof and delta == 1
            */
            for(j=first;j<len;j++) {
                c = input[j];
                d = 0;
                if ((j == (len - 1) && delta == 1))
                    d = 0x80;
                if (c >= 0x20 && c <= 0x7f) {
                    put_byte(c | d);
                } else {
                    put_byte(d | 0x1f);
                    put_byte((c >> 8) & 0xff);
                    put_byte(c & 0xff);
                }
            }
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
        put_byte(0);
}

int main(int argc, char **argv)
{
    char *filename;
    int i, j, k, col, line_num;
    FILE *f;
    char line[1024], *p;
    unsigned char *q;
    int c, size;
    int nb_kmaps;

    if (argc < 3) {
        printf("usage: kmaptoqe outfile kmaps...\n"
               "Convert yudit keyboard maps to qemacs compressed format\n");
        exit(1);
    }
    outfile = fopen(argv[1], "w");
    if (!outfile) {
        perror(argv[1]);
        exit(1);
    }
    outbuf_ptr = outbuf;

    memset(freq, 0, sizeof(freq));

    nb_kmaps = 0;
    for(i=2;i<argc;i++) {
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
        q = name;
        while (*p != '.' && *p != '\0') {
            c = *p++;
            if (c == '-')
                c = '_';
            *q++ = c;
        }
        *q = '\0';
        strcpy(kmap_names[nb_kmaps], name);
        kmap_offsets[nb_kmaps] = outbuf_ptr - outbuf;
        nb_kmaps++;
        /* special compression for CJ */
        is_chinese_cj = !strcmp(name, "Chinese_CJ");
        gen_table = !strcmp(name, "Chinese_CJ") ||
            !strcmp(name, "TeX") || !strcmp(name, "SGML");

        col = 0;
        nb_inputs = 0;
        line_num = 0;
        for(;;) {
            if (fgets(line, sizeof(line), f) == NULL)
                break;
            line_num++;
            p = line;
            if (*p == '/')
                continue;
            while (*p != '\0' && *p != '\"') p++;
            if (*p != '\"')
                continue;
            p++;
            q = inputs[nb_inputs].input;
            for(;;) {
                while (isspace(*p))
                    p++;
                if (*p == '=') 
                    break;
                if (p[0] == '0' && p[1] == 'x') {
                    c = strtoul(p, &p, 0);
                } else {
                    c = *p++;
                }
                if (c >= 256) {
                    fprintf(stderr, "%s:%d: Invalid char %x %c\n", 
                            filename, line_num, c, c);
                }
                *q++ = c;
            }
            inputs[nb_inputs].len = q - inputs[nb_inputs].input;
            p++;
            inputs[nb_inputs].output = strtoul(p, NULL, 0);
            nb_inputs++;
        }

        qsort(inputs, nb_inputs, sizeof(InputEntry), sort_func);

        /* first pass to compute offsets */
        write_data = 0;
        gen_map();

        write_data = 1;

        /* write start map */
        put_byte(nb_starts | (is_chinese_cj ? 0x80 : 0));
        if (gen_table) {
            for(k=0;k<nb_starts;k++) {
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
    for(k=0;k<32;k++) {
        fprintf(stderr, "freq[%d] = %d\n", k, freq[k]);
    }
#endif

    /* write header */
    size = 4;
    for(i=0;i<nb_kmaps;i++) {
        size += strlen(kmap_names[i]) + 1 + 4;
    }
    size += 4; /* last offset at zero */

    fwrite("kmap", 1, 4, outfile);

    for(i=0;i<nb_kmaps;i++) {
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
