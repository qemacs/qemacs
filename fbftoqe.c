/*
 * convert FBF fonts into source file so that so resources are needed
 * for fonts 
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


void dump_font(const char *filename, const char *name)
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
    for(;;) {
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

void getname(char *name, int name_size, const char *filename)
{
    const char *p;
    char *q;

    p = strrchr(filename, '/');
    if (!p)
        p = filename;
    else
        p++;
    strcpy(name, p);
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

    for(i=1;i<argc;i++) {
        filename = argv[i];
        getname(name, sizeof(name), filename);
        dump_font(filename, name);
    }

    /* dump font list */
    printf("const void *fbf_fonts[] = {\n");
    for(i=1;i<argc;i++) {
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
