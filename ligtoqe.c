/* convert ligature files to qe binary internal format */

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <getopt.h>
#include <assert.h>

int unicode_to_utf8(char *buf, unsigned int c)
{
    char *q;

    q = buf;
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
    return q - buf;
}

void put_be16(FILE *outfile, int off)
{
    fputc((off >> 8) & 0xff, outfile);
    fputc((off) & 0xff, outfile);
}

#define MAX_LIGS 5000

typedef struct Ligature {
    int buf_in[20], buf_out[20];
    int buf_in_size, buf_out_size;
} Ligature;

Ligature ligs[MAX_LIGS];
int nb_ligs;

int sort_func(const void *a1, const void *b1)
{
    const Ligature *a = a1;
    const Ligature *b = b1;
    int val;

    val = a->buf_in[0] - b->buf_in[0];
    if (val == 0 && 
        a->buf_in_size >= 2 && b->buf_in_size >= 2) {
        val = a->buf_in[1] - b->buf_in[1];
    }
    return val;
}

void help(void)
{
    printf("usage: ligtoqe [-u] sourcefile.lig output_ligature_file\n"
           "Build a ligature resource file for qemacs\n"
           "\n"
           "-u : output an UTF8 encode summy on stdout\n");
    exit(1);
}

int main(int argc, char **argv)
{
    FILE *f, *outfile;
    char buf[1024], *p;
    Ligature *l;
    char buf1[128];
    int i, c, to_utf8, j, l1, l2, n;
    const char *lig_filename;
    const char *lig_res_filename;
    int subst1_count, subst2_count, subst_long_count;

    to_utf8 = 0;
    for(;;) {
        c = getopt(argc, argv, "uh");
        if (c == -1)
            break;
        switch(c) {
        case 'h':
            help();
            break;
        case 'u':
            to_utf8 = 1;
            break;
        }
    }
    if (optind + 2 > argc)
        help();
    lig_filename = argv[optind++];
    lig_res_filename = argv[optind++];

    f = fopen(lig_filename, "r");
    if (!f) {
        perror(lig_filename);
        return 1;
    }
    for(;;) {
        if (fgets(buf, sizeof(buf), f) == NULL)
            break;
        p = buf + strlen(buf) - 1;
        if (p >= buf && *p == '\n')
            *p = '\0';
        p = buf;

        while (isspace(*p))
          p++;

        if (*p == '#' || *p == '\0') {
            if (to_utf8) {
                printf("%s\n", buf);
            }
            continue;
        }
        
        l = &ligs[nb_ligs++];

        l->buf_in_size = 0;
        for(;;) {
            while (isspace(*p))
                p++;
            if (*p == '=')
                break;
            if (*p == '\0') {
                fprintf(stderr, "'=' expected\n");
                exit(1);
            }
            l->buf_in[l->buf_in_size++] = strtoul(p, &p, 0);
        }
        p++;
        
        l->buf_out_size = 0;
        for(;;) {
            while (isspace(*p))
                p++;
            /* stop at the first comment */
            if (*p == '\0' || *p == '/')
                break;
            l->buf_out[l->buf_out_size++] = strtoul(p, &p, 0);
        }

        if (l->buf_in_size < 1 ||
            l->buf_out_size < 1) {
            fprintf(stderr, "syntax error: '%s'\n", buf);
            exit(1);
        }

        /* output UTF8 encoded list */
        if (to_utf8) {
            printf("%s // ", buf);
            for(i=0;i<l->buf_in_size;i++) {
                unicode_to_utf8(buf1, l->buf_in[i]);
                printf("%s ", buf1);
            }
            printf("=");
            for(i=0;i<l->buf_out_size;i++) {
                unicode_to_utf8(buf1, l->buf_out[i]);
                printf(" %s", buf1);
            }
            printf("\n");
        }
    }
    fclose(f);


    /* sort everything */
    qsort(ligs, nb_ligs, sizeof(Ligature), sort_func);
    
    outfile = fopen(lig_res_filename, "w");
    if (!outfile) {
        perror(lig_res_filename);
        exit(1);
    }

    /* header */
    fwrite("liga", 1, 4, outfile);

    /* number of entries */
    put_be16(outfile, 0);
    put_be16(outfile, 0);
    put_be16(outfile, 0);

    /* output the subst table */
    n = 0;
    for(i=0;i<nb_ligs;i++) {
        l = &ligs[i];
        if (l->buf_in_size == 1) {
            if (l->buf_out_size != 1) {
                fprintf(stderr, "substitutions of only one char are handled\n");
                exit(1);
            }
            put_be16(outfile, l->buf_in[0]);
            put_be16(outfile, l->buf_out[0]);
            n++;
        }
    }
    subst1_count = n;
    
    /* output the ligature table (2 chars -> 1 char) */
    j = 0;
    l1 = 0;
    l2 = 0;
    n = 0;
    for(i=0;i<nb_ligs;i++) {
        l = &ligs[i];
        if (l->buf_in_size >= 2) {
            if (l->buf_in_size > 2 || l->buf_out_size > 1) {
                if (l1 == l->buf_in[0] &&
                    l2 == l->buf_in[1]) {
                } else {
                    put_be16(outfile, l->buf_in[0]);
                    put_be16(outfile, l->buf_in[1]);
                    put_be16(outfile, 0);
                    l1 = l->buf_in[0];
                    l2 = l->buf_in[1];
                    n++;
                }
            } else {
                put_be16(outfile, l->buf_in[0]);
                put_be16(outfile, l->buf_in[1]);
                put_be16(outfile, l->buf_out[0]);
                n++;
            }
        }
    }
    
    subst2_count = n;
    
    /* output the long ligature table */
    n = 0;
    for(i=0;i<nb_ligs;i++) {
        l = &ligs[i];
        if (l->buf_in_size > 2 || l->buf_out_size > 1) {
            put_be16(outfile, l->buf_in_size);
            put_be16(outfile, l->buf_out_size);
            for(j=0;j<l->buf_in_size;j++) 
                put_be16(outfile, l->buf_in[j]);
            for(j=0;j<l->buf_out_size;j++) 
                put_be16(outfile, l->buf_out[j]);
            n += 2 + l->buf_in_size + l->buf_out_size;
        }
    }
    put_be16(outfile, 0);
    n++;

    subst_long_count = n;

    fseek(outfile, 4, SEEK_SET);
    put_be16(outfile, subst1_count);
    put_be16(outfile, subst2_count);
    put_be16(outfile, subst_long_count);

    fclose(outfile);

    return 0;
}
