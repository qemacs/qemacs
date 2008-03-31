/*
 * TTY handling for QEmacs
 *
 * Copyright (c) 2000,2001 Fabrice Bellard.
 * Copyright (c) 2002-2008 Charlie Gordon.
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
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include "qe.h"

typedef unsigned int TTYChar;
#define TTYCHAR(ch,fg,bg)   ((ch) | ((fg) << 16) | ((bg) << 24))
#define TTYCHAR2(ch,col)    ((ch) | ((col) << 16))
#define TTYCHAR_GETCH(cc)   ((cc) & 0xFFFF)
#define TTYCHAR_GETCOL(cc)  (((cc) >> 16) & 0xFFFF)
#define TTYCHAR_GETFG(cc)   (((cc) >> 16) & 0xFF)
#define TTYCHAR_GETBG(cc)   (((cc) >> 24) & 0xFF)
#define TTYCHAR_DEFAULT     TTYCHAR(' ', 0, 0)

#if defined(__GNUC__) && !defined(CONFIG_CYGWIN)
#  define TTY_PUTC(c,f)         putc_unlocked(c, f)
#  define TTY_FWRITE(b,s,n,f)   fwrite_unlocked(b, s, n, f)
#  define TTY_FPRINTF           fprintf
static inline void TTY_FPUTS(const char *s, FILE *fp) {
    TTY_FWRITE(s, 1, strlen(s), fp);
}
#else
#  define TTY_PUTC(c,f)         putc(c, f)
#  define TTY_FWRITE(b,s,n,f)   fwrite(b, s, n, f)
#  define TTY_FPRINTF           fprintf
#  define TTY_FPUTS             fputs
#endif

enum InputState {
    IS_NORM,
    IS_ESC,
    IS_CSI,
    IS_CSI2,
    IS_ESC2,
};

enum TermCode {
    TERM_UNKNOWN = 0,
    TERM_ANSI,
    TERM_VT100,
    TERM_XTERM,
    TERM_LINUX,
    TERM_CYGWIN,
};

typedef struct TTYState {
    TTYChar *screen;
    int screen_size;
    unsigned char *line_updated;
    struct termios oldtty;
    int cursor_x, cursor_y;
    /* input handling */
    enum InputState input_state;
    int input_param;
    int utf8_state;
    int utf8_index;
    unsigned char buf[10];
    char *term_name;
    enum TermCode term_code;
    int term_flags;
#define KBS_CONTROL_H  1
} TTYState;

static void tty_resize(int sig);
static void tty_term_exit(void);
static void tty_read_handler(void *opaque);

static struct TTYState tty_state;
static QEditScreen *tty_screen;

static int tty_term_probe(void)
{
    return 1;
}

static int tty_term_init(QEditScreen *s,
                         __unused__ int w, __unused__ int h)
{
    TTYState *ts;
    struct termios tty;
    struct sigaction sig;

    s->STDIN = stdin;
    s->STDOUT = stdout;

    tty_screen = s;
    ts = &tty_state;
    s->private = ts;
    s->media = CSS_MEDIA_TTY;

    /* Derive some settings from the TERM environment variable */
    tty_state.term_code = TERM_UNKNOWN;
    tty_state.term_flags = 0;
    tty_state.term_name = getenv("TERM");
    if (tty_state.term_name) {
        /* linux and xterm -> kbs=\177
         * ansi cygwin vt100 -> kbs=^H
         */
        if (strstart(tty_state.term_name, "ansi", NULL)) {
            tty_state.term_code = TERM_ANSI;
            tty_state.term_flags = KBS_CONTROL_H;
        } else
        if (strstart(tty_state.term_name, "vt100", NULL)) {
            tty_state.term_code = TERM_VT100;
            tty_state.term_flags = KBS_CONTROL_H;
        } else
        if (strstart(tty_state.term_name, "xterm", NULL)) {
            tty_state.term_code = TERM_XTERM;
        } else
        if (strstart(tty_state.term_name, "linux", NULL)) {
            tty_state.term_code = TERM_LINUX;
        } else
        if (strstart(tty_state.term_name, "cygwin", NULL)) {
            tty_state.term_code = TERM_CYGWIN;
            tty_state.term_flags = KBS_CONTROL_H;
        }
    }

    tcgetattr(fileno(s->STDIN), &tty);
    ts->oldtty = tty;

    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
                     |INLCR|IGNCR|ICRNL|IXON);
    tty.c_oflag |= OPOST;
    tty.c_lflag &= ~(ECHO|ECHONL|ICANON|IEXTEN|ISIG);
    tty.c_cflag &= ~(CSIZE|PARENB);
    tty.c_cflag |= CS8;
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;

    tcsetattr(fileno(s->STDIN), TCSANOW, &tty);

    /* First switch to full screen mode */
#if 0
    printf("\033[?1048h\033[?1047h"     /* enable cup */
           "\033)0\033(B"       /* select character sets in block 0 and 1 */
           "\017");             /* shift out */
#else
    TTY_FPRINTF(s->STDOUT,
                "\033[?1049h"       /* enter_ca_mode */
                "\033[m\033(B"      /* exit_attribute_mode */
                "\033[4l"           /* exit_insert_mode */
                "\033[?7h"          /* enter_am_mode */
                "\033[39;49m"       /* orig_pair */
                "\033[?1h\033="     /* keypad_xmit */
               );
#endif

    /* Get charset from command line option */
    s->charset = find_charset(qe_state.tty_charset);

    if (!s->charset && !isatty(fileno(s->STDOUT)))
        s->charset = &charset_utf8;

    if (!s->charset) {
        s->charset = &charset_8859_1;

        /* Test UTF8 support by looking at the cursor position (idea
         * from Ricardas Cepas <rch@pub.osf.lt>). Since uClibc actually
         * tests to ensure that the format string is a valid multibyte
         * sequence in the current locale (ANSI/ISO C99), use a format
         * specifier of %s to avoid printf() failing with EILSEQ.
         */
        if (tty_state.term_code != TERM_CYGWIN) {
            int y, x, n;

            /*               ^X  ^Z    ^M   \170101  */
            //printf("%s", "\030\032" "\r\xEF\x81\x81" "\033[6n\033D");
            /* Just print utf-8 encoding for eacute and check cursor position */
            TTY_FPRINTF(s->STDOUT, "%s",
                        "\030\032" "\r\xC3\xA9" "\033[6n\033D");
            fflush(s->STDOUT);
            n = fscanf(s->STDIN, "\033[%u;%u", &y, &x);  /* get cursor position */
            TTY_FPRINTF(s->STDOUT, "\r   \r");        /* go back, erase 3 chars */
            if (n == 2 && x == 2) {
                s->charset = &charset_utf8;
            }
        }
    }
    put_status(NULL, "tty charset: %s", s->charset->name);

    atexit(tty_term_exit);

    sig.sa_handler = tty_resize;
    sigemptyset(&sig.sa_mask);
    sig.sa_flags = 0;
    sigaction(SIGWINCH, &sig, NULL);
    fcntl(fileno(s->STDIN), F_SETFL, O_NONBLOCK);
    /* If stdout is to a pty, make sure we aren't in nonblocking mode.
     * Otherwise, the printf()s in term_flush() can fail with EAGAIN,
     * causing repaint errors when running in an xterm or in a screen
     * session. */
    fcntl(fileno(s->STDOUT), F_SETFL, 0);

    set_read_handler(fileno(s->STDIN), tty_read_handler, s);

    tty_resize(0);

    if (tty_state.term_flags & KBS_CONTROL_H) {
        do_toggle_control_h(NULL, 1);
    }

    return 0;
}

static void tty_term_close(QEditScreen *s)
{
    fcntl(fileno(s->STDIN), F_SETFL, 0);
#if 0
    /* go to the last line */
    printf("\033[%d;%dH\033[m\033[K"
           "\033[?1047l\033[?1048l",    /* disable cup */
           s->height, 1);
#else
    /* go to last line and clear it */
    TTY_FPRINTF(s->STDOUT, "\033[%d;%dH\033[m\033[K", s->height, 1);
    TTY_FPRINTF(s->STDOUT,
                "\033[?1049l"        /* exit_ca_mode */
                "\r"                 /* return */
                "\033[?1l\033>"      /* keypad_local */
                "\r"                 /* return */
               );
#endif
    fflush(s->STDOUT);
}

static void tty_term_exit(void)
{
    QEditScreen *s = tty_screen;
    TTYState *ts = s->private;

    tcsetattr(fileno(s->STDIN), TCSANOW, &ts->oldtty);
}

static void tty_resize(__unused__ int sig)
{
    QEditScreen *s = tty_screen;
    TTYState *ts = s->private;
    struct winsize ws;
    int i, count, size;
    TTYChar tc;

    s->width = 80;
    s->height = 24;
    if (ioctl(fileno(s->STDIN), TIOCGWINSZ, &ws) == 0) {
        s->width = ws.ws_col;
        s->height = ws.ws_row;
        if (s->width > MAX_SCREEN_WIDTH)
            s->width = MAX_SCREEN_WIDTH;
        if (s->height < 3)
            s->height = 3;
    }

    count = s->width * s->height;
    size = count * sizeof(TTYChar);
    /* screen buffer + shadow buffer + extra slot for loop guard */
    qe_realloc(&ts->screen, size * 2 + 1);
    qe_realloc(&ts->line_updated, s->height);
    ts->screen_size = count;

    /* Erase shadow buffer to impossible value */
    memset(ts->screen + count, 0xFF, size);
    /* Fill screen buffer with black spaces */
    tc = TTYCHAR_DEFAULT;
    for (i = 0; i < count; i++) {
        ts->screen[i] = tc;
    }
    /* All rows need refresh */
    memset(ts->line_updated, 1, s->height);

    s->clip_x1 = 0;
    s->clip_y1 = 0;
    s->clip_x2 = s->width;
    s->clip_y2 = s->height;
}

static void tty_term_invalidate(QEditScreen *s)
{
    tty_resize(0);
}

static void tty_term_cursor_at(QEditScreen *s, int x1, int y1,
                               __unused__ int w, __unused__ int h)
{
    TTYState *ts = s->private;
    ts->cursor_x = x1;
    ts->cursor_y = y1;
}

static int tty_term_is_user_input_pending(QEditScreen *s)
{
    fd_set rfds;
    struct timeval tv;

    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&rfds);
    FD_SET(fileno(s->STDIN), &rfds);
    if (select(fileno(s->STDIN) + 1, &rfds, NULL, NULL, &tv) > 0)
        return 1;
    else
        return 0;
}

static int const csi_lookup[] = {
    KEY_NONE,   /* 0 */
    KEY_HOME,   /* 1 */
    KEY_INSERT, /* 2 */
    KEY_DELETE, /* 3 */
    KEY_END,    /* 4 */
    KEY_PAGEUP, /* 5 */
    KEY_PAGEDOWN, /* 6 */
    KEY_NONE,   /* 7 */
    KEY_NONE,   /* 8 */
    KEY_NONE,   /* 9 */
    KEY_NONE,   /* 10 */
    KEY_F1,     /* 11 */
    KEY_F2,     /* 12 */
    KEY_F3,     /* 13 */
    KEY_F4,     /* 14 */
    KEY_F5,     /* 15 */
    KEY_NONE,   /* 16 */
    KEY_F6,     /* 17 */
    KEY_F7,     /* 18 */
    KEY_F8,     /* 19 */
    KEY_F9,     /* 20 */
    KEY_F10,    /* 21 */
    KEY_NONE,   /* 22 */
    KEY_F11,    /* 23 */
    KEY_F12,    /* 24 */
    KEY_F13,    /* 25 */
    KEY_F14,    /* 26 */
    KEY_NONE,   /* 27 */
    KEY_F15,    /* 28 */
    KEY_F16,    /* 29 */
    KEY_NONE,   /* 30 */
    KEY_F17,    /* 31 */
    KEY_F18,    /* 32 */
    KEY_F19,    /* 33 */
    KEY_F20,    /* 34 */
};

static void tty_read_handler(void *opaque)
{
    QEditScreen *s = opaque;
    QEmacsState *qs = &qe_state;
    TTYState *ts = s->private;
    int ch;
    QEEvent ev1, *ev = &ev1;

    if (read(fileno(s->STDIN), ts->buf + ts->utf8_index, 1) != 1)
        return;

    if (qs->trace_buffer &&
        qs->active_window &&
        qs->active_window->b != qs->trace_buffer) {
        eb_trace_bytes(ts->buf + ts->utf8_index, 1, EB_TRACE_TTY);
    }

    /* charset handling */
    if (s->charset == &charset_utf8) {
        if (ts->utf8_state == 0) {
            const char *p;
            p = (const char *)ts->buf;
            ch = utf8_decode(&p);
        } else {
            ts->utf8_state = utf8_length[ts->buf[0]] - 1;
            ts->utf8_index = 0;
            return;
        }
    } else {
        ch = ts->buf[0];
    }

    switch (ts->input_state) {
    case IS_NORM:
        if (ch == '\033')
            ts->input_state = IS_ESC;
        else
            goto the_end;
        break;
    case IS_ESC:
        if (ch == '\033') {
            /* cygwin A-right transmit ESC ESC[C ... */
            goto the_end;
        }
        if (ch == '[') {
            ts->input_state = IS_CSI;
            ts->input_param = 0;
        } else if (ch == 'O') {
            ts->input_state = IS_ESC2;
        } else {
            ch = KEY_META(ch);
            ts->input_state = IS_NORM;
            goto the_end;
        }
        break;
    case IS_CSI:
        if (qe_isdigit(ch)) {
            ts->input_param = ts->input_param * 10 + ch - '0';
            break;
        }
        ts->input_state = IS_NORM;
        switch (ch) {
        case '[':
            ts->input_state = IS_CSI2;
            break;
        case '~':
            if (ts->input_param < countof(csi_lookup)) {
                ch = csi_lookup[ts->input_param];
                goto the_end;
            }
            break;
            /* All these for ansi|cygwin */
        case 'A': ch = KEY_UP; goto the_end;    // kcuu1
        case 'B': ch = KEY_DOWN; goto the_end;  // kcud1
        case 'C': ch = KEY_RIGHT; goto the_end; // kcuf1
        case 'D': ch = KEY_LEFT; goto the_end;  // kcub1
        case 'F': ch = KEY_END; goto the_end;   // kend
        //case 'G': ch = KEY_CENTER; goto the_end;    // kb2
        case 'H': ch = KEY_HOME; goto the_end;  // khome
        case 'L': ch = KEY_INSERT; goto the_end;        // kich1
        //case 'M': ch = KEY_MOUSE; goto the_end;     // kmous
        case 'Z': ch = KEY_SHIFT_TAB; goto the_end;     // kcbt
        default:
#if 0           /* xterm CTRL-arrows */
            if (ts->input_param == 5) {
                switch (ch) {
                case 'A': ch = KEY_CTRL_UP; goto the_end;
                case 'B': ch = KEY_CTRL_DOWN; goto the_end;
                case 'C': ch = KEY_CTRL_RIGHT; goto the_end;
                case 'D': ch = KEY_CTRL_LEFT; goto the_end;
                }
            }
#endif
            break;
        }
        break;
    case IS_CSI2:
        /* cygwin/linux terminal */
        ts->input_state = IS_NORM;
        switch (ch) {
        case 'A': ch = KEY_F1; goto the_end;
        case 'B': ch = KEY_F2; goto the_end;
        case 'C': ch = KEY_F3; goto the_end;
        case 'D': ch = KEY_F4; goto the_end;
        case 'E': ch = KEY_F5; goto the_end;
        }
        break;
    case IS_ESC2:       // "\EO"
        /* xterm/vt100 fn */
        ts->input_state = IS_NORM;
        switch (ch) {
        case 'A': ch = KEY_UP; goto the_end;
        case 'B': ch = KEY_DOWN; goto the_end;
        case 'C': ch = KEY_RIGHT; goto the_end;
        case 'D': ch = KEY_LEFT; goto the_end;
        case 'P': ch = KEY_F1; goto the_end;
        case 'Q': ch = KEY_F2; goto the_end;
        case 'R': ch = KEY_F3; goto the_end;
        case 'S': ch = KEY_F4; goto the_end;
        case 't': ch = KEY_F5; goto the_end;
        case 'u': ch = KEY_F6; goto the_end;
        case 'v': ch = KEY_F7; goto the_end;
        case 'l': ch = KEY_F8; goto the_end;
        case 'w': ch = KEY_F9; goto the_end;
        case 'x': ch = KEY_F10; goto the_end;
        }
        break;
    the_end:
        ev->key_event.type = QE_KEY_EVENT;
        ev->key_event.key = ch;
        qe_handle_event(ev);
        break;
    }
}

static inline int color_dist(unsigned int c1, unsigned c2)
{

    return (abs( (c1 & 0xff) - (c2 & 0xff)) +
            2 * abs( ((c1 >> 8) & 0xff) - ((c2 >> 8) & 0xff)) +
            abs( ((c1 >> 16) & 0xff) - ((c2 >> 16) & 0xff)));
}

unsigned int const tty_bg_colors[8] = {
    QERGB(0x00, 0x00, 0x00),
    QERGB(0xff, 0x00, 0x00),
    QERGB(0x00, 0xff, 0x00),
    QERGB(0xff, 0xff, 0x00),
    QERGB(0x00, 0x00, 0xff),
    QERGB(0xff, 0x00, 0xff),
    QERGB(0x00, 0xff, 0xff),
    QERGB(0xff, 0xff, 0xff),
};

unsigned int const tty_fg_colors[16] = {
    QERGB(0x00, 0x00, 0x00),
    QERGB(0xbb, 0x00, 0x00),
    QERGB(0x00, 0xbb, 0x00),
    QERGB(0xbb, 0xbb, 0x00),
    QERGB(0x00, 0x00, 0xbb),
    QERGB(0xbb, 0x00, 0xbb),
    QERGB(0x00, 0xbb, 0xbb),
    QERGB(0xbb, 0xbb, 0xbb),

    QERGB(0x55, 0x55, 0x55),
    QERGB(0xff, 0x55, 0x55),
    QERGB(0x55, 0xff, 0x55),
    QERGB(0xff, 0xff, 0x55),
    QERGB(0x55, 0x55, 0xff),
    QERGB(0xff, 0x55, 0xff),
    QERGB(0x55, 0xff, 0xff),
    QERGB(0xff, 0xff, 0xff),
};

static int get_tty_color(QEColor color, unsigned int const *colors, int count)
{
    int i, cmin, dmin, d;

    dmin = INT_MAX;
    cmin = 0;
    for (i = 0; i < count; i++) {
        d = color_dist(color, colors[i]);
        if (d < dmin) {
            cmin = i;
            dmin = d;
        }
    }
    return cmin;
}

static void tty_term_fill_rectangle(QEditScreen *s,
                                    int x1, int y1, int w, int h, QEColor color)
{
    TTYState *ts = s->private;
    int x2 = x1 + w;
    int y2 = y1 + h;
    int x, y;
    TTYChar *ptr;
    int wrap = s->width - w;
    int bgcolor;

    ptr = ts->screen + y1 * s->width + x1;
    if (color == QECOLOR_XOR) {
        for (y = y1; y < y2; y++) {
            ts->line_updated[y] = 1;
            for (x = x1; x < x2; x++) {
                *ptr ^= TTYCHAR(0, 7, 7);
                ptr++;
            }
            ptr += wrap;
        }
    } else {
        bgcolor = get_tty_color(color, tty_bg_colors, countof(tty_bg_colors));
        for (y = y1; y < y2; y++) {
            ts->line_updated[y] = 1;
            for (x = x1; x < x2; x++) {
                *ptr = TTYCHAR(' ', 7, bgcolor);
                ptr++;
            }
            ptr += wrap;
        }
    }
}

/* XXX: could alloc font in wrapper */
static QEFont *tty_term_open_font(__unused__ QEditScreen *s,
                                  __unused__ int style, __unused__ int size)
{
    QEFont *font;

    font = qe_malloc(QEFont);
    if (!font)
        return NULL;

    font->ascent = 0;
    font->descent = 1;
    font->private = NULL;
    return font;
}

static void tty_term_close_font(__unused__ QEditScreen *s, QEFont *font)
{
    qe_free(&font);
}

/*
 * Modified implementation of wcwidth() from Markus Kuhn. We do not
 * handle non spacing and enclosing combining characters and control
 * chars.
 */

static unsigned int const tty_term_glyph_ranges[] = {
    0x10FF, 1, 0x115f, 2,     /*  0: Hangul Jamo */
    0x2328, 1, 0x232a, 2,     /*  1: wide Angle brackets */
    0x2E7F, 1, 0x2efd, 2,     /*  2: CJK Radicals */
    0x2EFF, 1, 0x303e, 2,     /*  3: Kangxi Radicals */
    0x303F, 1, 0x4dbf, 2,     /*  4: CJK */
    0x4DFF, 1, 0xa4cf, 2,     /*  5: CJK */
    0xABFF, 1, 0xd7a3, 2,     /*  6: Hangul Syllables */
    0xF8FF, 1, 0xfaff, 2,     /*  7: CJK Compatibility Ideographs */
    0xFDFF, 1, 0xFE1F, 2,     /*  8: */
    0xFE2F, 1, 0xfe6f, 2,     /*  9: CJK Compatibility Forms */
    0xFEFF, 1, 0xff5f, 2,     /* 10: Fullwidth Forms */
    0xFFDF, 1, 0xffe6, 2,     /* 11: */
    0x1FFFF, 1, 0x3fffd, 2,   /* 12: CJK Compatibility */
    UINT_MAX, 1,              /* 13: catchall */
};

static unsigned int const tty_term_glyph_index[16] = {
    4 * 0,  /* 0000-0FFF */
    4 * 0,  /* 1000-1FFF */
    4 * 1,  /* 2000-2FFF */
    4 * 3,  /* 3000-3FFF */
    4 * 4,  /* 4000-4FFF */
    4 * 5,  /* 5000-5FFF */
    4 * 5,  /* 6000-6FFF */
    4 * 5,  /* 7000-7FFF */
    4 * 5,  /* 8000-8FFF */
    4 * 5,  /* 9000-9FFF */
    4 * 5,  /* A000-AFFF */
    4 * 6,  /* B000-BFFF */
    4 * 6,  /* C000-CFFF */
    4 * 6,  /* D000-DFFF */
    4 * 7,  /* E000-EFFF */
    4 * 7,  /* F000-FFFF */
};

static int tty_term_glyph_width(__unused__ QEditScreen *s, unsigned int ucs)
{
    unsigned int const *ip;

    /* fast test for majority of non-wide scripts */
    if (ucs < 0x1100)
        return 1;

    /* Iterative lookup with fast initial jump, no boundary test needed */
    ip = tty_term_glyph_ranges +
         tty_term_glyph_index[(ucs >> 12) & 0xF] - 2;

    for (;;) {
        ip += 2;
        if (ucs <= ip[0])
            return ip[1];
    }
}

static void tty_term_text_metrics(QEditScreen *s, __unused__ QEFont *font,
                                  QECharMetrics *metrics,
                                  const unsigned int *str, int len)
{
    int i, x;

    metrics->font_ascent = font->ascent;
    metrics->font_descent = font->descent;
    x = 0;
    for (i = 0; i < len; i++)
        x += tty_term_glyph_width(s, str[i]);

    metrics->width = x;
}

static void tty_term_draw_text(QEditScreen *s, __unused__ QEFont *font,
                               int x, int y, const unsigned int *str, int len,
                               QEColor color)
{
    TTYState *ts = s->private;
    TTYChar *ptr;
    int fgcolor, w, n;
    unsigned int cc;

    if (y < s->clip_y1 || y >= s->clip_y2 || x >= s->clip_x2)
        return;

    ts->line_updated[y] = 1;
    fgcolor = get_tty_color(color, tty_fg_colors, countof(tty_fg_colors));
    ptr = ts->screen + y * s->width;

    if (x < s->clip_x1) {
        ptr += s->clip_x1;
        /* left clip */
        while (len > 0) {
            cc = *str++;
            len--;
            w = tty_term_glyph_width(s, cc);
            x += w;
            if (x >= s->clip_x1) {
                /* now we are on the screen. need to put spaces for
                   wide chars */
                n = x;
                if (s->clip_x2 < n)
                    n = s->clip_x2;
                n -= s->clip_x1;
                for (; n > 0; n--) {
                    *ptr = TTYCHAR(' ', fgcolor, TTYCHAR_GETBG(*ptr));
                    ptr++;
                }
                break;
            }
        }
    } else {
        ptr += x;
    }
    for (; len > 0; len--) {
        cc = *str++;
        w = tty_term_glyph_width(s, cc);
        /* XXX: would need to put spaces for wide chars */
        if (x + w > s->clip_x2)
            break;
        *ptr = TTYCHAR(cc, fgcolor, TTYCHAR_GETBG(*ptr));
        ptr++;
        n = w - 1;
        while (n > 0) {
            *ptr = TTYCHAR(0xFFFFF, fgcolor, TTYCHAR_GETBG(*ptr));
            ptr++;
            n--;
        }
        x += w;
    }
}

static void tty_term_set_clip(__unused__ QEditScreen *s,
                              __unused__ int x, __unused__ int y,
                              __unused__ int w, __unused__ int h)
{
}

static void tty_term_flush(QEditScreen *s)
{
    TTYState *ts = s->private;
    TTYChar *ptr, *ptr1, *ptr2, *ptr3, *ptr4, cc, blankcc;
    int y, shadow, ch, bgcolor, fgcolor, shifted;

    bgcolor = -1;
    fgcolor = -1;
    shifted = 0;

    /* CG: Should optimize output by computing it in a temporary buffer
     * and flushing it in one call to fwrite()
     */

    shadow = ts->screen_size;
    /* We cannot print anything on the bottom right screen cell,
     * pretend it's OK: */
    ts->screen[shadow - 1] = ts->screen[2 * shadow - 1];
    for (y = 0; y < s->height; y++) {
        if (ts->line_updated[y]) {
            ts->line_updated[y] = 0;
            ptr = ptr1 = ts->screen + y * s->width;
            ptr2 = ptr1 + s->width;

            /* make sure the loop stops */
            cc = ptr2[shadow];
            ptr2[shadow] = ptr2[0] + 1;
            /* quickly loop over one row */
            while (ptr1[0] == ptr1[shadow]) {
                ptr1++;
            }
            ptr2[shadow] = cc;

            /* ptr1 points to first difference on row */
            /* find the last non blank char on row */
            ptr3 = ptr2;
            blankcc = TTYCHAR2(' ', TTYCHAR_GETCOL(ptr2[-1]));
            while (ptr3 > ptr1 && ptr3[-1] == blankcc) {
                --ptr3;
            }
            /* scan for last difference on row: */
            while (ptr2 > ptr1 && ptr2[-1] == ptr2[shadow - 1]) {
                --ptr2;
            }
            if (ptr1 == ptr2)
                continue;

            TTY_FPRINTF(s->STDOUT, "\033[%d;%dH", y + 1, ptr1 - ptr + 1);

            ptr4 = ptr2;
            if (ptr2 > ptr3 + 3)
                ptr4 = ptr3;

            while (ptr1 < ptr4) {
                cc = *ptr1;
                ptr1[shadow] = cc;
                ptr1++;
                ch = TTYCHAR_GETCH(cc);
                if (ch != 0xffff) {
                    /* output attributes */
                    if (bgcolor != (int)TTYCHAR_GETBG(cc)) {
                        bgcolor = TTYCHAR_GETBG(cc);
                        TTY_FPRINTF(s->STDOUT, "\033[%dm", 40 + bgcolor);
                    }
                    if (fgcolor != (int)TTYCHAR_GETFG(cc) && ch != ' ') {
                        fgcolor = TTYCHAR_GETFG(cc);
                        TTY_FPRINTF(s->STDOUT, "\033[%dm",
                                    (fgcolor > 7) ? 1 : 22);
                        TTY_FPRINTF(s->STDOUT, "\033[%dm",
                                    30 + (fgcolor & 7));
                    }
                    if (shifted) {
                        /* Kludge for linedrawing chars */
                        if (ch < 128 || ch >= 128 + 32) {
                            TTY_FPUTS("\033(B", s->STDOUT);
                            shifted = 0;
                        }
                    }

                    /* do not display escape codes or invalid codes */
                    if (ch < 32 || ch == 127) {
                        TTY_PUTC('.', s->STDOUT);
                    } else
                    if (ch < 127) {
                        TTY_PUTC(ch, s->STDOUT);
                    } else
                    if (ch < 128 + 32) {
                        /* Kludge for linedrawing chars */
                        if (!shifted) {
                            TTY_FPUTS("\033(0", s->STDOUT);
                            shifted = 1;
                        }
                        TTY_PUTC(ch - 32, s->STDOUT);
                    } else {
                        u8 buf[10], *q;
                        int nc;

                        // was in qemacs-0.3.1.g2.gw/tty.c:
                        // if (cc == 0x2500)
                        //    printf("\016x\017");
                        /* s->charset is either latin1 or utf-8 */
                        q = s->charset->encode_func(s->charset, buf, ch);
                        if (!q) {
                            if (s->charset == &charset_8859_1) {
                                /* upside down question mark */
                                buf[0] = 0xBF;
                            } else {
                                buf[0] = '?';
                            }
                            q = buf + 1;
                            if (tty_term_glyph_width(s, ch) == 2) {
                                *q++ = '?';
                            }
                        }

                        nc = q - buf;
                        if (nc == 1) {
                            TTY_PUTC(*buf, s->STDOUT);
                        } else {
                            TTY_FWRITE(buf, 1, nc, s->STDOUT);
                        }
                    }
                }
            }
            if (shifted) {
                TTY_FPUTS("\033(B", s->STDOUT);
                shifted = 0;
            }
            if (ptr1 < ptr2) {
                /* More differences to synch in shadow, erase eol */
                cc = *ptr1;
                if (bgcolor != (int)TTYCHAR_GETBG(cc)) {
                    bgcolor = TTYCHAR_GETBG(cc);
                    TTY_FPRINTF(s->STDOUT, "\033[%dm", 40 + bgcolor);
                }
                TTY_FPUTS("\033[K", s->STDOUT);
                while (ptr1 < ptr2) {
                    ptr1[shadow] = cc;
                    ptr1++;
                }
            }
        }
    }

    TTY_FPRINTF(s->STDOUT, "\033[%d;%dH", ts->cursor_y + 1, ts->cursor_x + 1);
    fflush(s->STDOUT);
}

static QEDisplay tty_dpy = {
    "vt100",
    tty_term_probe,
    tty_term_init,
    tty_term_close,
    tty_term_flush,
    tty_term_is_user_input_pending,
    tty_term_fill_rectangle,
    tty_term_open_font,
    tty_term_close_font,
    tty_term_text_metrics,
    tty_term_draw_text,
    tty_term_set_clip,
    NULL, /* dpy_selection_activate */
    NULL, /* dpy_selection_request */
    tty_term_invalidate,
    tty_term_cursor_at,
    NULL, /* dpy_bmp_alloc */
    NULL, /* dpy_bmp_free */
    NULL, /* dpy_bmp_draw */
    NULL, /* dpy_bmp_lock */
    NULL, /* dpy_bmp_unlock */
    NULL, /* dpy_full_screen */
    NULL, /* next */
};

static int tty_init(void)
{
    return qe_register_display(&tty_dpy);
}

qe_module_init(tty_init);
