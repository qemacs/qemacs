/* convert unicode cp pages to qe source code */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

char *getline(char *buf, int buf_size, FILE *f)
{
    char *str;
    int len;

    str = fgets(buf, buf_size, f);
    if (!str)
        return NULL;
    len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
    }
    return str;
}


void handle_cp(FILE *f, const char *name)
{
    char line[1024];
    const char *p;
    int table[256];
    int min_code, max_code, c1, c2, i, nb, j, c;
    char name1[256], *q;

    getline(line, sizeof(line), f);

    getline(line, sizeof(line), f);
    printf("static const char *aliases_%s[] = { %s, NULL };\n\n", name, line);
    for(i=0;i<256;i++)
        table[i] = i;
    nb = 0;
    for(;;) {
        if (!getline(line, sizeof(line), f))
            break;
        if (!strcasecmp(line, "# compatibility"))
            break;
        if (line[0] == '\0' || line[0] == '#')
            continue;
        p = line;
        c1 = strtol(p, (char **)&p, 0);
        if (!isspace(*p))
            continue;
        c2 = strtol(p, (char **)&p, 0);
        if (c1 >= 256) {
            fprintf(stderr, "%s: ERROR %d %d\n", name, c1, c2);
            continue;
        }
        table[c1] = c2;
        nb++;
    }
    if (table[10] != 10)
        fprintf(stderr, "%s: warning: newline is not preserved\n", name);
    
    min_code = 0x7fffffff;
    max_code = -1;
    for(i=0;i<256;i++) {
        if (table[i] != i) {
            if (i < min_code)
                min_code = i;
            if (i > max_code)
                max_code = i;
        }
    }
    //    fprintf(stderr, "%s: %3d %02x %02x\n", name, nb, min_code, max_code);
    
    if (max_code != -1) {
        printf("static const unsigned short table_%s[%d] = {\n",
               name, max_code - min_code + 1);
        j = 0;
        for(i=min_code;i<=max_code;i++) {
            printf("0x%04x, ", table[i]);
            if ((j++ & 7) == 7)
                printf("\n");
        }
        if ((j & 7) != 0)
            printf("\n");
        printf("};\n\n");
    }

    q = name1;
    for(p = name; *p != '\0'; p++) {
        c = *p;
        if (c == '_')
            c = '-';
        *q++ = c;
    }
    *q = '\0';
    
    printf("QECharset charset_%s = {\n"
           "\"%s\",\n"
           "aliases_%s,\n"
           "decode_8bit_init,\n"
           "NULL,\n"
           "encode_8bit,\n"
           "table_alloc: 1,\n"
           "min_char: %d,\n"
           "max_char: %d,\n"
           "private_table: table_%s,\n"
           "};\n\n",
           name, name1, name,
           min_code, max_code, name);
}

/* handle jis208 or jis212 table */
void handle_jis(FILE *f, int is_jis208)
{
    int c1, c2, b1, b2, b1_max, b2_max, i, j, nb, n;
    int table[94*94];
    int table_b2_max[94];
    char line[1024];
    const char *p;
    const char *name = is_jis208 ? "jis208" : "jis212";

    memset(table, 0, sizeof(table));
    memset(table_b2_max, 0, sizeof(table_b2_max));
    b1_max = 0;
    b2_max = 0;
    nb = 0;
    for(;;) {
        if (!getline(line, sizeof(line), f))
            break;
        if (line[0] == '\0' || line[0] == '#')
            continue;
        p = line;
        if (is_jis208)
            c1 = strtol(p, (char **)&p, 0);
        c1 = strtol(p, (char **)&p, 0);
        c2 = strtol(p, (char **)&p, 0);

        b1 = (c1 >> 8) & 0xff;
        b2 = (c1) & 0xff;
        
        /* compress the code */
        b1 = b1 - 0x21;
        b2 = b2 - 0x21;
        if (b1 > b1_max)
            b1_max = b1;
        if (b2 > b2_max)
            b2_max = b2;
        if (b2 > table_b2_max[b1])
            table_b2_max[b1] = b2;
        table[b1 * 94 + b2] = c2;
        nb++;
    }
    printf("/* max row = %d. The following rows are excluded:", b1_max);
    n = 0;
    for(i=0;i<=b1_max;i++) {
        if (table_b2_max[i] == 0) {
            printf(" %d", i);
        } else {
            n++;
        }
    }
    printf(", density=%d%% */\n",  nb * 100 / (n * (b2_max + 1)));

    printf("const unsigned short table_%s[%d] = {\n",
           name, n * (b2_max + 1));
    n = 0;
    for(i=0;i<=b1_max;i++) {
        if (table_b2_max[i] != 0) {
            for(j=0;j<=b2_max;j++) {
                printf("0x%04x, ", table[i * 94 + j]);
                if ((n++ & 7) == 7)
                    printf("\n");
            }
        }
    }
    if ((n & 7) != 0)
        printf("\n");
    printf("};\n\n");
}

int main(int argc, char **argv)
{
    int i;
    const char *filename, *p;
    char *q;
    char name[256];
    FILE *f;

    printf("#include \"qe.h\"\n\n");

    for(i=1;i<argc;i++) {
        filename = argv[i];

        p = strrchr(filename, '/');
        if (!p)
            p = filename;
        else
            p++;
        strcpy(name, p);
        q = strrchr(name, '.');
        if (q)
            *q = '\0';
        
        f = fopen(filename, "r");
        if (!f) {
            perror(filename);
            exit(1);
        }

        if (!strcmp(name, "JIS0208"))
            handle_jis(f, 1);
        else if (!strcmp(name, "JIS0212"))
            handle_jis(f, 0);
        else
            handle_cp(f, name);

        fclose(f);
    }
    return 0;
}
