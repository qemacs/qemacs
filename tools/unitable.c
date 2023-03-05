/*
 * Unicode table generator for QEmacs.
 *
 * Copyright (c) 2001-2023 Charlie Gordon.
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
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cutils.h"

#define CHARCODE_MAX  0x10FFFF

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

static const char NAME[] = "unitable";

static const char *unicode_version = "15.0.0";
static const char *unicode_dir = "unidata";

static FILE *open_unicode_file(const char *dir, const char *version,
                               const char *base, char *filename, int size)
{
    char cmd[2048];
    FILE *fp;
    int pos = 0;

    if (dir && *dir) {
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

/*---------------- Unicode Blocks ----------------*/

static char block_file[1024];

typedef struct block_t {
    unsigned int c1, c2;
    char *name;
} block_t;

static block_t *blocks;
static int blocks_count;
static int blocks_avail;

static int add_block(unsigned int c1, unsigned int c2, const char *name) {
    block_t *bp;
    if (blocks_count >= blocks_avail) {
        int new_avail = blocks_avail + (blocks_avail >> 1) + 16;
        block_t *new_blocks = realloc(blocks, new_avail * sizeof(*blocks));
        if (!new_blocks)
            return -1;
        blocks = new_blocks;
        blocks_avail = new_avail;
    }
    bp = &blocks[blocks_count++];
    bp->c1 = c1;
    bp->c2 = c2;
    bp->name = strdup(name);
    if (!bp->name)
        return -1;
    return 0;
}

static int load_blocks(int unassigned, int private_use, int surrogates) {
    /* Load and filter the Blocks.txt file */
    const char *base_name = "Blocks.txt";
    FILE *fp;
    char buf[256];
    char block_name[256];
    char *p;
    int lineno = 0;
    unsigned int from, to, last = 0;

    /* Block definitions taken from www.unicode.org/Public/xxx/ucd/Blocks.txt */
    if (!(fp = open_unicode_file(unicode_dir, unicode_version,
                                 base_name,
                                 block_file, sizeof block_file)))
        return 1;

    while (fgets(buf, sizeof buf, fp)) {
        lineno++;
        for (p = buf; isspace((unsigned char)*p); p++)
            continue;
        /* skip comment and empty lines */
        if (*p == '#' || *p == '\0')
            continue;
        if (sscanf(buf, "%x .. %x ; %255[^\n]", &from, &to, block_name) == 3) {
            if (!surrogates && strstr(block_name, "Surrogates"))
                continue;
            if (!private_use && strstr(block_name, "Private Use"))
                continue;
            if (unassigned && from != last) {
                add_block(last, from - 1, "unassigned");
            }
            add_block(from, to, block_name);
            last = to + 1;
        } else {
            fprintf(stderr, "%s:%d: invalid block\n", NAME, lineno);
        }
    }
    fclose(fp);
    return 0;
}

/*---------------- TTY Unicode width tables ----------------*/

#include <termios.h>

static struct termios oldtty;

static void set_cooked_tty(void) {
    tcsetattr(fileno(stdin), TCSANOW, &oldtty);
    fprintf(stderr, "\n");
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
    atexit(set_cooked_tty);
}

#define XPOS  10
static char *comments[200];
static size_t comments_count;

static void comments_output(int pos) {
    if (comments_count) {
        size_t i;
        for (i = 0; i < comments_count; i++) {
            printf("%*s%s\n", max_int(1, 17 - pos), "", comments[i]);
            pos = 0;
            free(comments[i]);
            comments[i] = NULL;
        }
        comments_count = 0;
    } else {
        printf("\n");
    }
}

static int make_tty_width_table(int filter_start, int filter_end) {
    int bn, pos;
    unsigned int last_c = 0;
    int last_x = 0;

    if (getenv("QELEVEL")) {
        fprintf(stderr, "cannot run in quick emacs shell buffer\n");
        return 1;
    }

    set_raw_tty();
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    printf("/* This file was generated automatically by %s from %s and TTY */\n\n",
           NAME, block_file);

    printf("static unsigned int const unicode_glyph_ranges[] = {\n");

    for (bn = 0; bn < blocks_count; bn++) {
        unsigned int start = max_uint(blocks[bn].c1, filter_start);
        unsigned int end = min_uint(blocks[bn].c2, filter_end);
        const char *desc = blocks[bn].name;
        char buf[100];
        unsigned int code;
        int len, c, n, y, x;

        if (start > end)
            continue;

        /* measure the actual code point widths as reported by the terminal */
        for (code = start; code <= end; code++) {
            char line[32];

            encode_utf8(buf, code);
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
            if (n == 2 && x != last_x) {
                if (last_x) {
                    pos = printf("    0x%05X, %d,", code - 1, last_x - XPOS);
                    comments_output(pos);
                }
                last_x = x;
            }
            if (code == start && desc) {
                snprintf(buf, sizeof(buf), "/* %04X-%04X  %s */", start, end, desc);
                if (comments_count < countof(comments))
                    comments[comments_count++] = strdup(buf);
            }
        }
        last_c = code;
    }

    if (last_x && last_x != XPOS + 1) {
        pos = printf("    0x%05X, %d,", last_c - 1, last_x - XPOS);
        comments_output(pos);
        last_c = 0;
        last_x = 0;
    }
    comments[comments_count++] = strdup("/* catch all */");
    pos = printf("    0x%05X, %d,", 0xFFFFFFFF, 1);
    comments_output(pos);

    printf("};\n");

    return 0;
}

/*---------------- Raw Unicode blocks ----------------*/

static int make_raw_dump(int filter_start, int filter_end) {
    int bn;
    for (bn = 0; bn < blocks_count; bn++) {
        unsigned int start = max_uint(blocks[bn].c1, filter_start);
        unsigned int end = min_uint(blocks[bn].c2, filter_end);
        unsigned int code;

        for (code = start; code <= end; code++) {
            char buf[10];
            int len = encode_utf8(buf, code);
            fwrite(buf, 1, len, stdout);
        }
    }
    return 0;
}

/*---------------- Unicode Charts ----------------*/

#include "wcwidth.c"

static int check_tty_width_table(int filter_start, int filter_end) {
    unsigned char *width = calloc(CHARCODE_MAX + 1, sizeof(*width));
    int bn;

    if (getenv("QELEVEL")) {
        fprintf(stderr, "cannot run in quick emacs shell buffer\n");
        return 1;
    }

    set_raw_tty();
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    for (bn = 0; bn < blocks_count; bn++) {
        unsigned int start = max_uint(blocks[bn].c1, filter_start);
        unsigned int end = min_uint(blocks[bn].c2, filter_end);
        char buf[100];
        unsigned int code;
        int len, c, n, y, x;

        if (start > end)
            continue;

        /* measure the actual code point widths as reported by the terminal */
        for (code = start; code <= end; code++) {
            char line[32];

            encode_utf8(buf, code);
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
            if (n == 2) {
                width[code] = x - XPOS;
            }
        }
    }

    for (bn = 0; bn < blocks_count; bn++) {
        unsigned int start = max_uint(blocks[bn].c1, filter_start);
        unsigned int end = min_uint(blocks[bn].c2, filter_end);
        unsigned int code1, code2;

        if (start > end)
            continue;

        /* measure the actual code point widths as reported by the terminal */
        for (code1 = start; code1 <= end; code1++) {
            int wcwidth = qe_wcwidth(code1);
            int tty_w = width[code1];
            if (tty_w != wcwidth) {
                int variant = qe_wcwidth_variant(code1);
                unsigned int code = code1 + 1;
                for (code = code2 = code1;
                     (++code <= end &&
                      tty_w == width[code] &&
                      wcwidth == qe_wcwidth(code) &&
                      variant == qe_wcwidth_variant(code));
                     code++)
                {
                    code2 = code;
                }
                printf("%04X", code1);
                if (code1 != code2) {
                    printf("..%04X", code2);
                }
                printf(":  tty:%d  wcwidth:%d  variant:%d\n", tty_w, wcwidth, variant);
                code1 = code2;
            }
        }
    }

    return 0;
}

static int make_unicode_charts(int filter_start, int filter_end) {
    int bn;
    for (bn = 0; bn < blocks_count; bn++) {
        unsigned int start = max_uint(blocks[bn].c1, filter_start);
        unsigned int end = min_uint(blocks[bn].c2, filter_end);
        const char *desc = blocks[bn].name;
        char buf[10];
        unsigned int code;
        int i, len, width, lpad, rpad, hlen;
        static char const TL[] = "\xE2\x94\x8C";  /* 250C */
        static char const HB[] = "\xE2\x94\x80";  /* 2500 */
        static char const TR[] = "\xE2\x94\x90";  /* 2510 */
        static char const VB[] = "\xE2\x94\x82";  /* 2502 */
        static char const BL[] = "\xE2\x94\x94";  /* 2514 */
        static char const BR[] = "\xE2\x94\x98";  /* 2518 */
        static char const VL[] = "\xE2\x94\x9C";  /* 251C */
        static char const VR[] = "\xE2\x94\xA4";  /* 2524 */

        if (start > end)
            continue;

        len = snprintf(buf, sizeof(buf), "%04X ", end);
        hlen = len + 4 * 16 + 1;
        printf("%s", TL);
        for (i = 0; i < hlen; i++)
            printf("%s", HB);
        printf("%s\n", TR);
        lpad = hlen - len * 2 - 3 - strlen(desc);
        rpad = lpad / 2;
        lpad -= rpad;
        printf("%s%*s%0*X-%0*X  %s%*s%s\n",
               VB, lpad, "", len, start, len, end, desc, rpad, "", VB);
        for (code = start & ~15; code <= end; code++) {
            if ((code & 15) == 0) {
                if (code <= start || (code & 0xff) == 0) {
                    printf("%s", VL);
                    for (i = 0; i < len; i++)
                        printf("%s", HB);
                    for (i = 0; i < 16; i++) {
                        printf("%s %X ", HB, i);
                    }
                    printf("%s%s\n", HB, VR);
                    //printf("\n%s%*s  0   1   2   3   4   5   6   7   "
                    //       "8   9   A   B   C   D   E   F  %s\n", VB, len, "", VB);
                }
                printf("%s%0*X", VB, len, code);
            }
            if (code < start) {
                width = 1;
                buf[0] = ' ';
                buf[1] = '\0';
            } else {
                width = qe_wcwidth(code);
                encode_utf8(buf, code);
            }
            /* XXX: should surround character with embed codes to prevent bidir handling */
            /* prepend a space before accents */
            printf("  %s%s%s", width == 0 ? " " : "", buf, width <= 1 ? " " : "");
            if ((code & 15) == 15 || code == end) {
                printf(" %s\n", VB);
            }
        }
        printf("%s", BL);
        for (i = 0; i < hlen; i++)
            printf("%s", HB);
        printf("%s\n", BR);
    }
    return 0;
}

static int usage(const char *name) {
    fprintf(stderr,
            "usage: %s [-V version] [-D dir] [-a] {-r | -W} [start [end]]\n"
            "options:\n"
            "  -V version  specify the version suffix for the Unicode files to load\n"
            "  -D dir      specify the directory from which to load the Unicode files\n"
            "  -a          include surrogate, private and unassigned codepoints\n"
            "  -r          output raw UTF-8 encoded codepoints\n"
            "  -W          generate the unicode_width.h file from TTY get cursor responses\n"
            "  start stop  additional arguments to set the range of codepoints to test\n"
            , name);
    return 2;
}

int main(int argc, char *argv[]) {
    int i, narg = 0;
    int unassigned = 0, private_use = 0, surrogates = 0;
    unsigned int filter_start = 0x20, filter_end = 0x10FFFF;
    int raw_dump = 0;
    int compute_widths = 0;
    int check_widths = 0;

    for (i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (strequal(arg, "-V") && i + 1 < argc) {
            unicode_version = argv[++i];
            continue;
        }
        if (strequal(arg, "-D") && i + 1 < argc) {
            unicode_dir = argv[++i];
            continue;
        }
        if (strequal(arg, "-a")) {
            unassigned = private_use = surrogates = 1;
            continue;
        }
        if (strequal(arg, "-r")) {
            raw_dump = 1;
            continue;
        }
        if (strequal(arg, "-C")) {
            check_widths = 1;
            continue;
        }
        if (strequal(arg, "-W") || strequal(arg, "-w")) {
            compute_widths = 1;
            continue;
        }
        if (*arg == '-') {
            return usage(NAME);
        }
        if (isdigit((unsigned char)*arg) && narg < 2) {
            switch (narg++) {
            case 0:
                filter_start = strtoul(arg, NULL, 16);
                continue;
            case 1:
                filter_end = strtoul(arg, NULL, 16);
                continue;
            default:
                break;
            }
        } else {
            return usage(NAME);
        }
    }

    if (load_blocks(unassigned, private_use, surrogates))
        return 1;

    if (check_widths)
        return check_tty_width_table(filter_start, filter_end);

    if (compute_widths)
        return make_tty_width_table(filter_start, filter_end);

    if (raw_dump)
        return make_raw_dump(filter_start, filter_end);

    return make_unicode_charts(filter_start, filter_end);
}
