/* convert a CSS style sheet to C buffer so that it can be statically
   linked with qemacs */
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

int main(int argc, char **argv)
{
    int c, n, last_c, got_space, in_string;

    printf("/* Automatically generated file - do not edit */\n");
    printf("const char %s[] =\n", argv[1]);
    n = 0;
    got_space = 0;
    last_c = 0;
    in_string = 0;
    for(;;) {
        c = getchar();
    redo:
        if (c == EOF)
            break;
        if (!in_string) {
            if (c == ' ' || c == '\t' || c == '\n') {
                got_space = 1;
                continue;
            }
            /* comments */
            if (c == '/') {
                c = getchar();
                if (c != '*') {
                    putchar('/');
                    goto redo;
                }
                for(;;) {
                    c = getchar();
                    if (c == EOF)
                        goto the_end;
                    while (c == '*') {
                        c = getchar();
                        if (c == EOF)
                            goto the_end;
                        if (c == '/')
                            goto end_comment;
                    }
                }
            end_comment:
                got_space = 1;
                continue;
            }
        }
        if (n == 0)
            printf("\"");
        /* add separator if needed */
        if (!in_string && got_space && isalnum(c) && isalnum(last_c))
            putchar(' ');
        if (c == '\"' || c == '\'' || c == '\\')
            putchar('\\');
        putchar(c);
        if (c == '\"')
            in_string ^= 1;
        last_c = c;
        got_space = 0;
        if (++n >= 64) {
            printf("\"\n");
            n = 0;
        }
    }
 the_end:
    if (n > 0)
        printf("\"\n");
    printf(";\n\n");
    return 0;
}
