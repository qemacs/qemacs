/*
 * Unicode table generator for QEmacs.
 *
 * Copyright (c) 2001-2017 Charlie Gordon.
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

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cutils.h"

static const char NAME[] = "unitable";

/* Compute tty width of unicode characters. */

#if 0
typedef struct block_t {
    unsigned int c1, c2;
    char *name;
} block_t;

static block_t *blocks;
static int blocks_count;
static int blocks_avail;

static int add_block(unsigned int c1, unsigned int c2, const char *name) {
    blocks_t *bp;
    if (blocks_count >= blocks_avail) {
        int new_avail = blocks_avail + (blocks_avail >> 1) + 16;
        blocks_t *new_blocks = realloc(blocks, new_avail * sizeof(*blocks));
        if (!new_blocks)
            return -1;
        blocks = new_blocks;
        blocks_avail = new_avail;
    }
    bp = blocks[blocks_count++];
    bp->c1 = c1;
    bp->c2 = c2;
    bp->name = strdup(name);
    if (!bp->name)
        return -1;
    return 0;
}
#endif

static unsigned int const unicode_glyph_ranges[] = {
#include "unicode_width.h"
};

static int unicode_tty_glyph_width(unsigned int ucs)
{
    unsigned int const *ip = unicode_glyph_ranges;

    /* Iterative lookup with fast initial jump, no boundary test needed */
    //ip = unicode_glyph_range_index[(ucs >> 12) & 0xF];

    while (ucs > ip[0]) {
        ip += 2;
    }
    return ip[1];
}

static int encode_utf8(char *dest, int c)
{
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

#include <termios.h>

static int filter_start = 0x20, filter_end = 0x10FFFF;
static int compute_widths = 0;
static int raw_dump = 0;
#define XPOS  10
static int last_c = 0, last_x = 0;
static char *comments[200];
static size_t comments_count;

static struct termios oldtty;

static void comments_output(int pos) {
    if (comments_count) {
        for (size_t i = 0; i < comments_count; i++) {
            printf("%*s%s\n", 17 - pos, "", comments[i]);
            pos = 0;
            free(comments[i]);
            comments[i] = NULL;
        }
        comments_count = 0;
    } else {
        printf("\n");
    }
}

static void set_cooked_tty(void) {
    int pos;

    tcsetattr(fileno(stdin), TCSANOW, &oldtty);
    fprintf(stderr, "\n");

    if (last_x && last_x != XPOS + 1) {
        pos = printf("    0x%05X, %d,", last_c - 1, last_x - XPOS);
        comments_output(pos);
        last_c = 0;
        last_x = 0;
    }
    comments[comments_count++] = strdup("/* catch all */");
    pos = printf("    0x%05X, %d,", 0xFFFFFFFF, 1);
    comments_output(pos);
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

static void make_width_table(int start, int end, const char *desc)
{
    char buf[100];
    int code, len, pos;
    int c, n, y, x;

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
        if (x != last_x) {
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
    if (compute_widths == 1) {
        pos = printf("    0x%05X, %d,", code - 1, last_x - XPOS);
        comments_output(pos);
        last_c = 0;
        last_x = 0;
    }
}

static void make_unicode_table(int start, int end, const char *desc)
{
    char buf[10];
    int code, len, width;

    if (start < filter_start)
        start = filter_start;
    if (end > filter_end)
        end = filter_end;
    if (start > end)
        return;

    if (raw_dump) {
        for (code = start; code <= end; code++) {
            len = encode_utf8(buf, code);
            fwrite(buf, 1, len, stdout);
        }
        return;
    }

    if (compute_widths) {
        make_width_table(start, end, desc);
        return;
    }

    printf("\n%04X-%04X  %s\n", start, end, desc);

    for (code = start & ~15; code <= end; code++) {
        if ((code & 15) == 0) {
            len = snprintf(buf, sizeof(buf), "%04X ", code);
            if (code <= start || (code & 0xff) == 0)
                printf("\n%*s  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F\n", len, "");
            printf("%s", buf);
        }
        if (code < start) {
            width = 1;
            buf[0] = ' ';
            buf[1] = '\0';
        } else {
            width = unicode_tty_glyph_width(code);
            encode_utf8(buf, code);
        }
        /* XXX: should surround character with embed codes to prevent bidir handling */
        /* prepend a space before accents */
        printf("  %s%s%s", width == 0 ? " " : "", buf, width <= 1 ? " " : "");
        if ((code & 15) == 15 || code == end) {
            printf("\n");
        }
    }
}

int main(int argc, char **argv)
{
    int i, narg = 0;
    int unassigned = 0, private_use = 0, surrogates = 0;
    const char *block_file = "Blocks.txt";
    FILE *fp;

    for (i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (!strcmp(arg, "-a")) {
            unassigned = private_use = surrogates = 1;
            continue;
        }
        if (!strcmp(arg, "-r")) {
            raw_dump++;
            continue;
        }
        if (!strcmp(arg, "-w")) {
            compute_widths = 2;
            continue;
        }
        if (*arg == '-') {
            fprintf(stderr, "usage: %s [-a] [-r] [-w] [BlocksFile] [start [end]]\n", NAME);
            return 2;
        }
        if (isdigit((unsigned char)*arg)) {
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
            block_file = arg;
        }
    }

    /* Block definitions taken from www.unicode.org/Public/xxx/ucd/Blocks.txt */
    fp = fopen(block_file, "r");
    if (fp) {
        char buf[256];
        char block_name[256];
        char *p;
        int lineno = 0;
        unsigned int from, to, last = 0;

        if (compute_widths) {
            set_raw_tty();
            setvbuf(stdin, NULL, _IONBF, 0);
            setvbuf(stderr, NULL, _IONBF, 0);
            printf("/* This file was generated automatically by %s from %s */\n\n",
                   NAME, block_file);
        }

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
                    make_unicode_table(last, from - 1, "unassigned");
                }
                make_unicode_table(from, to, block_name);
                last = to + 1;
            } else {
                fprintf(stderr, "%s:%d: invalid block\n", NAME, lineno);
            }
        }
        fclose(fp);
        return 0;
    } else {
        fprintf(stderr, "%s: cannot open file %s\n", NAME, block_file);
        return 1;
    }
}
