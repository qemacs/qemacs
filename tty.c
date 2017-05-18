/*
 * TTY Terminal handling for QEmacs
 *
 * Copyright (c) 2000-2001 Fabrice Bellard.
 * Copyright (c) 2002-2017 Charlie Gordon.
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

#if MAX_UNICODE_DISPLAY > 0xFFFF
typedef uint64_t TTYChar;
/* TTY composite style has 13-bit BG color, 4 attribute bits and 13-bit FG color */
#define TTY_STYLE_BITS        32
#define TTY_FG_COLORS         7936
#define TTY_BG_COLORS         7936
#define TTY_RGB_FG(r,g,b)     (0x1000 | (((r) & 0xF0) << 4) | ((g) & 0xF0) | ((b) >> 4))
#define TTY_RGB_BG(r,g,b)     (0x1000 | (((r) & 0xF0) << 4) | ((g) & 0xF0) | ((b) >> 4))
#define TTY_CHAR(ch,fg,bg)    ((uint32_t)(ch) | ((uint64_t)((fg) | ((bg) << 17)) << 32))
#define TTY_CHAR2(ch,col)     ((uint32_t)(ch) | ((uint64_t)(col) << 32))
#define TTY_CHAR_GET_CH(cc)   ((uint32_t)(cc))
#define TTY_CHAR_GET_COL(cc)  ((uint32_t)((cc) >> 32))
#define TTY_CHAR_GET_ATTR(cc) ((uint32_t)((cc) >> 32) & 0x1E000)
#define TTY_CHAR_GET_FG(cc)   ((uint32_t)((cc) >> 32) & 0x1FFF)
#define TTY_CHAR_GET_BG(cc)   ((uint32_t)((cc) >> (32 + 17)) & 0x1FFF)
#define TTY_CHAR_DEFAULT      TTY_CHAR(' ', 7, 0)
#define TTY_CHAR_COMB         0x200000
#define TTY_CHAR_BAD          0xFFFD
#define TTY_CHAR_NONE         0xFFFFFFFF
#define TTY_BOLD              0x02000
#define TTY_UNDERLINE         0x04000
#define TTY_ITALIC            0x08000
#define TTY_BLINK             0x10000
#define COMB_CACHE_SIZE       2048
#else
typedef uint32_t TTYChar;
/* TTY composite style has 4-bit BG color, 4 attribute bits and 8-bit FG color */
#define TTY_STYLE_BITS        16
#define TTY_FG_COLORS         256
#define TTY_BG_COLORS         16
#define TTY_RGB_FG(r,g,b)     (16 + ((r) / 51 * 36) + ((g) / 51 * 6) | ((b) / 51))
#define TTY_RGB_BG(r,g,b)     qe_map_color(QERGB(r, g, b), xterm_colors, 16, NULL)
#define TTY_CHAR(ch,fg,bg)    ((uint32_t)(ch) | ((uint32_t)((fg) | ((bg) << 12)) << 16))
#define TTY_CHAR2(ch,col)     ((uint32_t)(ch) | ((uint32_t)(col) << 16))
#define TTY_CHAR_GET_CH(cc)   ((cc) & 0xFFFF)
#define TTY_CHAR_GET_COL(cc)  (((cc) >> 16) & 0xFFFF)
#define TTY_CHAR_GET_ATTR(cc) ((uint32_t)((cc) >> 16) & 0x0F000)
#define TTY_CHAR_GET_FG(cc)   (((cc) >> 16) & 0xFF)
#define TTY_CHAR_GET_BG(cc)   (((cc) >> (16 + 12)) & 0x0F)
#define TTY_CHAR_DEFAULT      TTY_CHAR(' ', 7, 0)
#define TTY_CHAR_BAD          0xFFFD
#define TTY_CHAR_NONE         0xFFFF
#define TTY_BOLD              0x0100
#define TTY_UNDERLINE         0x0200
#define TTY_ITALIC            0x0400
#define TTY_BLINK             0x0800
#define COMB_CACHE_SIZE       1
#endif

#if defined(CONFIG_UNLOCKIO)
#  define TTY_PUTC(c,f)         putc_unlocked(c, f)
#ifdef CONFIG_DARWIN
#  define TTY_FWRITE(b,s,n,f)   fwrite(b, s, n, f)
#else
#  define TTY_FWRITE(b,s,n,f)   fwrite_unlocked(b, s, n, f)
#endif
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
    TERM_TW100,
};

typedef struct TTYState {
    TTYChar *screen;
    int screen_size;
    unsigned char *line_updated;
    struct termios oldtty;
    int cursor_x, cursor_y;
    /* input handling */
    enum InputState input_state;
    int input_param, input_param2;
    int utf8_index;
    unsigned char buf[8];
    char *term_name;
    enum TermCode term_code;
    int term_flags;
#define KBS_CONTROL_H           0x01
#define USE_ERASE_END_OF_LINE   0x02
#define USE_BOLD_AS_BRIGHT_FG   0x04
#define USE_BLINK_AS_BRIGHT_BG  0x08
#define USE_256_COLORS          0x10
#define USE_TRUE_COLORS         0x20
    /* number of colors supported by the actual terminal */
    const QEColor *term_colors;
    int term_fg_colors_count;
    int term_bg_colors_count;
    /* number of colors supported by the virtual terminal */
    const QEColor *tty_colors;
    int tty_fg_colors_count;
    int tty_bg_colors_count;
    unsigned int comb_cache[COMB_CACHE_SIZE];
} TTYState;

static QEditScreen *tty_screen;   /* for tty_term_exit and tty_term_resize */

static void tty_dpy_invalidate(QEditScreen *s);

static void tty_term_resize(int sig);
static void tty_term_exit(void);
static void tty_read_handler(void *opaque);

static int tty_dpy_probe(void)
{
    return 1;
}

static int tty_dpy_init(QEditScreen *s,
                        qe__unused__ int w, qe__unused__ int h)
{
    TTYState *ts;
    struct termios tty;
    struct sigaction sig;
    const char *p;

    ts = calloc(1, sizeof(*ts));
    if (ts == NULL) {
        return 1;
    }

    tty_screen = s;
    s->STDIN = stdin;
    s->STDOUT = stdout;
    s->priv_data = ts;
    s->media = CSS_MEDIA_TTY;

    /* Derive some settings from the TERM environment variable */
    ts->term_code = TERM_UNKNOWN;
    ts->term_flags = USE_ERASE_END_OF_LINE;
    ts->term_colors = xterm_colors;
    ts->term_fg_colors_count = 16;
    ts->term_bg_colors_count = 16;

    ts->term_name = getenv("TERM");
    if (ts->term_name) {
        /* linux and xterm -> kbs=\177
         * ansi cygwin vt100 -> kbs=^H
         */
        if (strstart(ts->term_name, "ansi", NULL)) {
            ts->term_code = TERM_ANSI;
            ts->term_flags |= KBS_CONTROL_H;
        } else
        if (strstart(ts->term_name, "vt100", NULL)) {
            ts->term_code = TERM_VT100;
            ts->term_flags |= KBS_CONTROL_H;
        } else
        if (strstart(ts->term_name, "xterm", NULL)) {
            ts->term_code = TERM_XTERM;
        } else
        if (strstart(ts->term_name, "linux", NULL)) {
            ts->term_code = TERM_LINUX;
        } else
        if (strstart(ts->term_name, "cygwin", NULL)) {
            ts->term_code = TERM_CYGWIN;
            ts->term_flags |= KBS_CONTROL_H |
                              USE_BOLD_AS_BRIGHT_FG | USE_BLINK_AS_BRIGHT_BG;
        } else
        if (strstart(ts->term_name, "tw100", NULL)) {
            ts->term_code = TERM_TW100;
            ts->term_flags |= KBS_CONTROL_H |
                              USE_BOLD_AS_BRIGHT_FG | USE_BLINK_AS_BRIGHT_BG;
        }
    }
    if (strstr(ts->term_name, "true") || strstr(ts->term_name, "24")) {
        ts->term_flags |= USE_TRUE_COLORS | USE_256_COLORS;
    }
    if (strstr(ts->term_name, "256")) {
        ts->term_flags |= USE_256_COLORS;
    }
    if ((p = getenv("TERM_PROGRAM")) && strequal(p, "iTerm.app")) {
        /* iTerm and iTerm2 support true colors */
        ts->term_flags |= USE_TRUE_COLORS | USE_256_COLORS;
    }        
    /* actual color mode can be forced via environment variables */
    /* XXX: should have qemacs variables too */
    if ((p = getenv("COLORTERM")) != NULL) {
        /* Check COLORTERM environment variable as documented in
         * https://gist.github.com/XVilka/8346728
         */
#if TTY_STYLE_BITS == 32
        if (strstr(p, "truecolor")
        ||  strstr(p, "24bit")
        ||  strstr(p, "hicolor")) {
            ts->term_flags &= ~(USE_BOLD_AS_BRIGHT_FG | USE_BLINK_AS_BRIGHT_BG |
                                USE_256_COLORS | USE_TRUE_COLORS);
            ts->term_flags |= USE_TRUE_COLORS;
        } else
#endif
        if (strstr(p, "256")) {
            ts->term_flags &= ~(USE_BOLD_AS_BRIGHT_FG | USE_BLINK_AS_BRIGHT_BG |
                                USE_256_COLORS | USE_TRUE_COLORS);
            ts->term_flags |= USE_256_COLORS;
        } else
        if (strstr(p, "16")) {
            ts->term_flags &= ~(USE_BOLD_AS_BRIGHT_FG | USE_BLINK_AS_BRIGHT_BG |
                                USE_256_COLORS | USE_TRUE_COLORS);
        }
    }
#if TTY_STYLE_BITS == 32
    if (ts->term_flags & USE_TRUE_COLORS) {
        ts->term_fg_colors_count = 0x1000000;
        ts->term_bg_colors_count = 0x1000000;
    } else
    if (ts->term_flags & USE_256_COLORS) {
        ts->term_fg_colors_count = 256;
        ts->term_bg_colors_count = 256;
    }
#else
    ts->term_flags &= ~USE_TRUE_COLORS;
    if (ts->term_flags & USE_256_COLORS) {
        ts->term_fg_colors_count = 256;
    }
#endif

    ts->tty_bg_colors_count = min(ts->term_bg_colors_count, TTY_BG_COLORS);
    ts->tty_fg_colors_count = min(ts->term_fg_colors_count, TTY_FG_COLORS);
    ts->tty_colors = xterm_colors;

    tcgetattr(fileno(s->STDIN), &tty);
    ts->oldtty = tty;

    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP |
                     INLCR | IGNCR | ICRNL | IXON);
    tty.c_oflag |= OPOST;
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN | ISIG);
    tty.c_cflag &= ~(CSIZE | PARENB);
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

    if (ts->term_code == TERM_CYGWIN)
        s->charset = &charset_8859_1;

    if (ts->term_code == TERM_TW100)
        s->charset = find_charset("atarist");

    if (!s->charset && !isatty(fileno(s->STDOUT)))
        s->charset = &charset_8859_1;

    if (!s->charset) {
        int y, x, n;

        s->charset = &charset_8859_1;

        /* Test UTF8 support by looking at the cursor position (idea
         * from Ricardas Cepas <rch@pub.osf.lt>). Since uClibc actually
         * tests to ensure that the format string is a valid multibyte
         * sequence in the current locale (ANSI/ISO C99), use a format
         * specifier of %s to avoid printf() failing with EILSEQ.
         */

        /*               ^X  ^Z    ^M   \170101  */
        //printf("%s", "\030\032" "\r\xEF\x81\x81" "\033[6n\033D");
        /* Just print utf-8 encoding for eacute and check cursor position */
        TTY_FPRINTF(s->STDOUT, "%s",
                    "\030\032" "\r\xC3\xA9" "\033[6n\033D");
        fflush(s->STDOUT);
        /* XXX: should have a timeout to avoid locking on unsupported terminals */
        n = fscanf(s->STDIN, "\033[%d;%d", &y, &x);  /* get cursor position */
        TTY_FPRINTF(s->STDOUT, "\r   \r");        /* go back, erase 3 chars */
        if (n == 2 && x == 2) {
            s->charset = &charset_utf8;
        }
    }
    put_status(NULL, "tty charset: %s", s->charset->name);

    atexit(tty_term_exit);

    sig.sa_handler = tty_term_resize;
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

    tty_dpy_invalidate(s);

    if (ts->term_flags & KBS_CONTROL_H) {
        do_toggle_control_h(NULL, 1);
    }

    return 0;
}

static void tty_dpy_close(QEditScreen *s)
{
    TTYState *ts = s->priv_data;

    fcntl(fileno(s->STDIN), F_SETFL, 0);
#if 0
    /* go to the last line */
    printf("\033[%d;%dH\033[m\033[K"
           "\033[?1047l\033[?1048l",    /* disable cup */
           s->height, 1);
#else
    /* go to last line and clear it */
    TTY_FPRINTF(s->STDOUT, "\033[%d;%dH" "\033[m\033[K", s->height, 1);
    TTY_FPRINTF(s->STDOUT,
                "\033[?1049l"       /* exit_ca_mode */
                "\033[?1l\033>"     /* keypad_local */
                "\033[?25h"         /* show cursor */
                "\r\033[m\033[K"    /* return erase eol */
               );
#endif
    fflush(s->STDOUT);

    qe_free(&ts->screen);
    qe_free(&ts->line_updated);
}

static void tty_term_exit(void)
{
    QEditScreen *s = tty_screen;

    if (s) {
        TTYState *ts = s->priv_data;
        if (ts) {
            tcsetattr(fileno(s->STDIN), TCSANOW, &ts->oldtty);
        }
    }
}

static void tty_term_resize(qe__unused__ int sig)
{
    QEditScreen *s = tty_screen;

    if (s) {
        tty_dpy_invalidate(s);

        //fprintf(stderr, "tty_term_resize: width=%d, height=%d\n", s->width, s->height);

        url_redisplay();
    }
}

static void tty_dpy_invalidate(QEditScreen *s)
{
    TTYState *ts;
    struct winsize ws;
    int i, count, size;
    const char *p;
    TTYChar tc;

    if (s == NULL)
        return;

    ts = s->priv_data;

    /* get screen default values from environment */
    s->width = (p = getenv("COLUMNS")) != NULL ? atoi(p) : 80;
    s->height = (p = getenv("LINES")) != NULL ? atoi(p) : 25;

    /* update screen dimensions from pseudo tty ioctl */
    if (ioctl(fileno(s->STDIN), TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_col >= 10 && ws.ws_row >= 4) {
            s->width = ws.ws_col;
            s->height = ws.ws_row;
        }
    }

    if (s->width > MAX_SCREEN_WIDTH)
        s->width = MAX_SCREEN_WIDTH;
    if (s->height >= 10000)
        s->height -= 10000;
    if (s->height > MAX_SCREEN_LINES)
        s->height = MAX_SCREEN_LINES;
    if (s->height < 3)
        s->height = 25;

    count = s->width * s->height;
    size = count * sizeof(TTYChar);
    /* screen buffer + shadow buffer + extra slot for loop guard */
    qe_realloc(&ts->screen, size * 2 + sizeof(TTYChar));
    qe_realloc(&ts->line_updated, s->height);
    ts->screen_size = count;

    /* Erase shadow buffer to impossible value */
    memset(ts->screen + count, 0xFF, size + sizeof(TTYChar));
    /* Fill screen buffer with black spaces */
    tc = TTY_CHAR_DEFAULT;
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

static void tty_dpy_cursor_at(QEditScreen *s, int x1, int y1,
                              qe__unused__ int w, qe__unused__ int h)
{
    TTYState *ts = s->priv_data;
    ts->cursor_x = x1;
    ts->cursor_y = y1;
}

static int tty_dpy_is_user_input_pending(QEditScreen *s)
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
    TTYState *ts = s->priv_data;
    QEEvent ev1, *ev = &ev1;
    u8 buf[1];
    int ch, len;

    if (read(fileno(s->STDIN), buf, 1) != 1)
        return;

    if (qs->trace_buffer &&
        qs->active_window &&
        qs->active_window->b != qs->trace_buffer) {
        eb_trace_bytes(buf, 1, EB_TRACE_TTY);
    }

    ch = buf[0];

    switch (ts->input_state) {
    case IS_NORM:
        /* charset handling */
        if (s->charset == &charset_utf8) {
            if (ts->utf8_index && !(ch > 0x80 && ch < 0xc0)) {
                /* not a valid continuation byte */
                /* flush stored prefix, restart from current byte */
                /* XXX: maybe should consume prefix byte as binary */
                ts->utf8_index = 0;
            }
            ts->buf[ts->utf8_index] = ch;
            len = utf8_length[ts->buf[0]];
            if (len > 1) {
                const char *p = cs8(ts->buf);
                if (++ts->utf8_index < len) {
                    /* valid utf8 sequence underway, wait for next */
                    return;
                }
                ch = utf8_decode(&p);
            }
        }
        if (ch == '\033') {
            if (!tty_dpy_is_user_input_pending(s)) {
                /* Trick to distinguish the ESC key from function and meta
                 * keys  transmitting escape sequences starting with \033
                 * but followed immediately by more characters.
                 */
                goto the_end;
            }
            ts->input_state = IS_ESC;
        } else {
            goto the_end;
        }
        break;
    case IS_ESC:
        if (ch == '\033') {
            /* cygwin A-right transmit ESC ESC[C ... */
            goto the_end;
        }
        if (ch == '[') {
            if (!tty_dpy_is_user_input_pending(s)) {
                ch = KEY_META('[');
                ts->input_state = IS_NORM;
                goto the_end;
            }
            ts->input_state = IS_CSI;
            ts->input_param = 0;
            ts->input_param2 = 0;
        } else if (ch == 'O') {
            ts->input_state = IS_ESC2;
            ts->input_param = 0;
            ts->input_param2 = 0;
        } else {
            ch = KEY_META(ch);
            ts->input_state = IS_NORM;
            goto the_end;
        }
        break;
    case IS_CSI:
        if (ch >= '0' && ch <= '9') {
            ts->input_param = ts->input_param * 10 + ch - '0';
            break;
        }
        ts->input_state = IS_NORM;
        switch (ch) {
        case ';': /* multi ignore but the last 2 */
            /* iterm2 uses this for some keys:
             * C-up, C-down, C-left, C-right, S-left, S-right,
             */
            ts->input_param2 = ts->input_param;
            ts->input_param = 0;
            ts->input_state = IS_CSI;
            break;
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
        default:
            if (ts->input_param == 5) {
                /* xterm CTRL-arrows */
                /* iterm2 CTRL-arrows:
                 * C-up    = ^[[1;5A
                 * C-down  = ^[[1;5B
                 * C-left  = ^[[1;5D
                 * C-right = ^[[1;5C
                 */
                switch (ch) {
                case 'A': ch = KEY_CTRL_UP;    goto the_end;
                case 'B': ch = KEY_CTRL_DOWN;  goto the_end;
                case 'C': ch = KEY_CTRL_RIGHT; goto the_end;
                case 'D': ch = KEY_CTRL_LEFT;  goto the_end;
                }
            } else
            if (ts->input_param == 2) {
                /* iterm2 SHIFT-arrows:
                 * S-left  = ^[[1;2D
                 * S-right = ^[[1;2C
                 * should set-mark if region not visible
                 */
                switch (ch) {
                case 'A': ch = KEY_UP;    goto the_end;
                case 'B': ch = KEY_DOWN;  goto the_end;
                case 'C': ch = KEY_RIGHT; goto the_end;
                case 'D': ch = KEY_LEFT;  goto the_end;
                }
            } else {
                switch (ch) {
                case 'A': ch = KEY_UP;        goto the_end; // kcuu1
                case 'B': ch = KEY_DOWN;      goto the_end; // kcud1
                case 'C': ch = KEY_RIGHT;     goto the_end; // kcuf1
                case 'D': ch = KEY_LEFT;      goto the_end; // kcub1
                case 'F': ch = KEY_END;       goto the_end; // kend
                //case 'G': ch = KEY_CENTER;  goto the_end; // kb2
                case 'H': ch = KEY_HOME;      goto the_end; // khome
                case 'L': ch = KEY_INSERT;    goto the_end; // kich1
                //case 'M': ch = KEY_MOUSE;   goto the_end; // kmous
                case 'Z': ch = KEY_SHIFT_TAB; goto the_end; // kcbt
                }
            }
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
        case 'A': ch = KEY_UP;         goto the_end;
        case 'B': ch = KEY_DOWN;       goto the_end;
        case 'C': ch = KEY_RIGHT;      goto the_end;
        case 'D': ch = KEY_LEFT;       goto the_end;
        case 'F': ch = KEY_CTRL_RIGHT; goto the_end; /* iterm2 F-right */
        case 'H': ch = KEY_CTRL_LEFT;  goto the_end; /* iterm2 F-left */
        case 'P': ch = KEY_F1;         goto the_end;
        case 'Q': ch = KEY_F2;         goto the_end;
        case 'R': ch = KEY_F3;         goto the_end;
        case 'S': ch = KEY_F4;         goto the_end;
        case 't': ch = KEY_F5;         goto the_end;
        case 'u': ch = KEY_F6;         goto the_end;
        case 'v': ch = KEY_F7;         goto the_end;
        case 'l': ch = KEY_F8;         goto the_end;
        case 'w': ch = KEY_F9;         goto the_end;
        case 'x': ch = KEY_F10;        goto the_end;
        }
        break;
    the_end:
        ev->key_event.type = QE_KEY_EVENT;
        ev->key_event.key = ch;
        qe_handle_event(ev);
        break;
    }
}

static void tty_dpy_fill_rectangle(QEditScreen *s,
                                   int x1, int y1, int w, int h, QEColor color)
{
    TTYState *ts = s->priv_data;
    int x, y;
    int x2 = x1 + w;
    int y2 = y1 + h;
    int wrap = s->width - w;
    TTYChar *ptr;
    unsigned int bgcolor;

    ptr = ts->screen + y1 * s->width + x1;
    bgcolor = qe_map_color(color, ts->tty_colors, ts->tty_bg_colors_count, NULL);
    for (y = y1; y < y2; y++) {
        ts->line_updated[y] = 1;
        for (x = x1; x < x2; x++) {
            *ptr = TTY_CHAR(' ', 7, bgcolor);
            ptr++;
        }
        ptr += wrap;
    }
}

static void tty_dpy_xor_rectangle(QEditScreen *s,
                                  int x1, int y1, int w, int h, QEColor color)
{
    TTYState *ts = s->priv_data;
    int x, y;
    int x2 = x1 + w;
    int y2 = y1 + h;
    int wrap = s->width - w;
    TTYChar *ptr;

    ptr = ts->screen + y1 * s->width + x1;
    for (y = y1; y < y2; y++) {
        ts->line_updated[y] = 1;
        for (x = x1; x < x2; x++) {
            /* XXX: should reverse fg and bg */
            *ptr ^= TTY_CHAR(0, 7, 7);
            ptr++;
        }
        ptr += wrap;
    }
}

/* XXX: could alloc font in wrapper */
static QEFont *tty_dpy_open_font(qe__unused__ QEditScreen *s,
                                 qe__unused__ int style, qe__unused__ int size)
{
    QEFont *font;

    font = qe_mallocz(QEFont);
    if (!font)
        return NULL;

    font->ascent = 0;
    font->descent = 1;
    font->priv_data = NULL;
    return font;
}

static void tty_dpy_close_font(qe__unused__ QEditScreen *s, QEFont **fontp)
{
    qe_free(fontp);
}

static inline int tty_term_glyph_width(qe__unused__ QEditScreen *s, unsigned int ucs)
{
    /* fast test for majority of non-wide scripts */
    if (ucs < 0x300)
        return 1;

    return unicode_tty_glyph_width(ucs);
}

static void tty_dpy_text_metrics(QEditScreen *s, qe__unused__ QEFont *font,
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

#if MAX_UNICODE_DISPLAY > 0xFFFF

static unsigned int comb_cache_add(TTYState *ts, const unsigned int *seq, int len) {
    unsigned int *ip;
    for (ip = ts->comb_cache; *ip; ip += *ip & 0xFFFF) {
        if (*ip == len + 1U && !memcmp(ip + 1, seq, len * sizeof(*ip))) {
            return TTY_CHAR_COMB + (ip - ts->comb_cache);
        }
    }
    for (ip = ts->comb_cache; *ip; ip += *ip & 0xFFFF) {
        if (*ip >= 0x10001U + len) {
            /* found free slot */
            if (*ip > 0x10001U + len) {
                /* split free block */
                ip[len + 1] = *ip - (len + 1);
            }
            break;
        }
    }
    if (*ip == 0) {
        if ((ip - ts->comb_cache) + len + 1 >= countof(ts->comb_cache)) {
            return TTY_CHAR_BAD;
        }
        ip[len + 1] = 0;
    }
    *ip = len + 1;
    memcpy(ip + 1, seq, len * sizeof(*ip));
    return TTY_CHAR_COMB + (ip - ts->comb_cache);
}

static void comb_cache_clean(TTYState *ts, const TTYChar *screen, int len) {
    unsigned int *ip;
    int i;

    if (ts->comb_cache[0] == 0)
        return;

    for (ip = ts->comb_cache; *ip != 0; ip += *ip & 0xFFFF) {
        *ip |= 0x10000;
    }
    ip = ts->comb_cache;
    for (i = 0; i < len; i++) {
        int ch = TTY_CHAR_GET_CH(screen[i]);
        if (ch >= TTY_CHAR_COMB && ch < TTY_CHAR_COMB + countof(ts->comb_cache) - 1) {
            ip[ch - TTY_CHAR_COMB] &= ~0x10000;
        }
    }
    for (; *ip != 0; ip += *ip & 0xFFFF) {
        if (*ip & 0x10000) {
            while (ip[*ip & 0xFFFF] & 0x10000) {
                /* coalesce subsequent free blocks */
                *ip += ip[*ip & 0xFFFF] & 0xFFFF;
            }
            if (ip[*ip & 0xFFFF] == 0) {
                /* truncate free list */
                *ip = 0;
                break;
            }
        }
    }
}

static void comb_cache_describe(QEditScreen *s, EditBuffer *b) {
    TTYState *ts = s->priv_data;
    unsigned int *ip;
    unsigned int i;
    int w = 16;

    eb_printf(b, "Device Description\n\n");

    eb_printf(b, "%*s: %s\n", w, "term_name", ts->term_name);
    eb_printf(b, "%*s: %d  %s\n", w, "term_code", ts->term_code,
              ts->term_code == TERM_UNKNOWN ? "UNKNOWN" :
              ts->term_code == TERM_ANSI ? "ANSI" :
              ts->term_code == TERM_VT100 ? "VT100" :
              ts->term_code == TERM_XTERM ? "XTERM" :
              ts->term_code == TERM_LINUX ? "LINUX" :
              ts->term_code == TERM_CYGWIN ? "CYGWIN" :
              ts->term_code == TERM_TW100 ? "TW100" :
              "");
    eb_printf(b, "%*s: %#x %s%s%s%s%s%s\n", w, "term_flags", ts->term_flags,
              ts->term_flags & KBS_CONTROL_H ? " KBS_CONTROL_H" : "",
              ts->term_flags & USE_ERASE_END_OF_LINE ? " USE_ERASE_END_OF_LINE" : "",
              ts->term_flags & USE_BOLD_AS_BRIGHT_FG ? " USE_BOLD_AS_BRIGHT_FG" : "",
              ts->term_flags & USE_BLINK_AS_BRIGHT_BG ? " USE_BLINK_AS_BRIGHT_BG" : "",
              ts->term_flags & USE_256_COLORS ? " USE_256_COLORS" : "",
              ts->term_flags & USE_TRUE_COLORS ? " USE_TRUE_COLORS" : "");
    eb_printf(b, "%*s: fg:%d, bg:%d\n", w, "terminal colors",
              ts->term_fg_colors_count, ts->term_bg_colors_count);
    eb_printf(b, "%*s: fg:%d, bg:%d\n", w, "virtual tty colors",
              ts->tty_fg_colors_count, ts->tty_bg_colors_count);
    
    eb_printf(b, "\n");
    eb_printf(b, "Unicode combination cache:\n\n");
    
    for (ip = ts->comb_cache; *ip != 0; ip += *ip & 0xFFFF) {
        if (*ip & 0x10000) {
            eb_printf(b, "   FREE   %d\n", (*ip & 0xFFFF) - 1);
        } else {
            eb_printf(b, "  %06X  %d:",
                      (unsigned int)(TTY_CHAR_COMB + (ip - ts->comb_cache)),
                      (*ip & 0xFFFF) - 1);
            for (i = 1; i < (*ip & 0xFFFF); i++) {
                eb_printf(b, " %04X", ip[i]);
            }
            eb_printf(b, "\n");
        }
    }
}
#else
#define comb_cache_add(s, p, n)  TTY_CHAR_BAD
#define comb_cache_clean(s, p, n)
#define comb_cache_describe(s, b)
#endif

static void tty_dpy_draw_text(QEditScreen *s, QEFont *font,
                              int x, int y, const unsigned int *str0, int len,
                              QEColor color)
{
    TTYState *ts = s->priv_data;
    TTYChar *ptr;
    int fgcolor, w, n;
    unsigned int cc;
    const unsigned int *str = str0;

    if (y < s->clip_y1 || y >= s->clip_y2 || x >= s->clip_x2)
        return;

    ts->line_updated[y] = 1;
    fgcolor = qe_map_color(color, ts->tty_colors, ts->tty_fg_colors_count, NULL);
    if (font->style & QE_FONT_STYLE_UNDERLINE)
        fgcolor |= TTY_UNDERLINE;
    if (font->style & QE_FONT_STYLE_BOLD)
        fgcolor |= TTY_BOLD;
    if (font->style & QE_FONT_STYLE_BLINK)
        fgcolor |= TTY_BLINK;
    if (font->style & QE_FONT_STYLE_ITALIC)
        fgcolor |= TTY_ITALIC;
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
                /* pad partially clipped wide char with spaces */
                n = x;
                if (n > s->clip_x2)
                    n = s->clip_x2;
                n -= s->clip_x1;
                for (; n > 0; n--) {
                    *ptr = TTY_CHAR(' ', fgcolor, TTY_CHAR_GET_BG(*ptr));
                    ptr++;
                }
                /* skip combining code points if any */
                while (len > 0 && tty_term_glyph_width(s, *str) == 0) {
                    len--;
                    str++;
                }
                break;
            }
        }
    } else {
        ptr += x;
    }
    for (; len > 0; len--, str++) {
        cc = *str;
        w = tty_term_glyph_width(s, cc);
        if (x + w > s->clip_x2) {
            /* pad partially clipped wide char with spaces */
            while (x < s->clip_x2) {
                *ptr = TTY_CHAR(' ', fgcolor, TTY_CHAR_GET_BG(*ptr));
                ptr++;
                x++;
            }
            break;
        }
        if (w == 0) {
            int nacc;

            if (str == str0)
                continue;
            /* allocate synthetic glyph for multicharacter combination */
            nacc = 1;
            while (nacc < len && tty_term_glyph_width(s, str[nacc]) == 0)
                nacc++;
            cc = comb_cache_add(ts, str - 1, nacc + 1);
            str += nacc - 1;
            len -= nacc - 1;
            ptr[-1] = TTY_CHAR(cc, fgcolor, TTY_CHAR_GET_BG(ptr[-1]));
        } else {
            *ptr = TTY_CHAR(cc, fgcolor, TTY_CHAR_GET_BG(*ptr));
            ptr++;
            x += w;
            while (w > 1) {
                /* put placeholders for wide chars */
                *ptr = TTY_CHAR(TTY_CHAR_NONE, fgcolor, TTY_CHAR_GET_BG(*ptr));
                ptr++;
                w--;
            }
        }
    }
}

static void tty_dpy_set_clip(qe__unused__ QEditScreen *s,
                             qe__unused__ int x, qe__unused__ int y,
                             qe__unused__ int w, qe__unused__ int h)
{
}

static void tty_dpy_flush(QEditScreen *s)
{
    TTYState *ts = s->priv_data;
    TTYChar *ptr, *ptr1, *ptr2, *ptr3, *ptr4, cc, blankcc;
    int y, shadow, ch, bgcolor, fgcolor, shifted, attr;

    /* Hide cursor, goto home, reset attributes */
    TTY_FPUTS("\033[?25l\033[H\033[0m", s->STDOUT);

    if (ts->term_code != TERM_CYGWIN) {
        TTY_FPUTS("\033(B\033)0", s->STDOUT);
    }

    bgcolor = -1;
    fgcolor = -1;
    attr = 0;
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
            ptr3 = ptr2 = ptr1 + s->width;

            /* quickly find the first difference on row:
             * patch loop guard cell to make sure the simple loop stops
             * without testing ptr1 < ptr2
             */
            cc = ptr2[shadow];
            ptr2[shadow] = ptr2[0] + 1;
            while (ptr1[0] == ptr1[shadow]) {
                ptr1++;
            }
            ptr2[shadow] = cc;

            if (ptr1 == ptr2)
                continue;

            /* quickly scan for last difference on row:
             * the first difference on row at ptr1 is before ptr2
             * so we do not need a test on ptr2 > ptr1
             */
            while (ptr2[-1] == ptr2[shadow - 1]) {
                --ptr2;
            }

            ptr4 = ptr2;

            /* Try to optimize with erase to end of line: if the last
             * difference on row is a space, measure the run of same
             * color spaces from the end of the row.  If this run
             * starts before the last difference, the row is a
             * candidate for a partial update with erase-end-of-line.
             * exception: do not use erase end of line for a bright
             * background color if emulated as bright.
             */
            if ((ts->term_flags & USE_ERASE_END_OF_LINE)
            &&  TTY_CHAR_GET_CH(ptr4[-1]) == ' '
            &&  (/*!(ts->term_flags & USE_BLINK_AS_BRIGHT_BG) ||*/
                 TTY_CHAR_GET_BG(ptr4[-1]) < 8))
            {
                /* find the last non blank char on row */
                blankcc = TTY_CHAR2(' ', TTY_CHAR_GET_COL(ptr3[-1]));
                while (ptr3 > ptr1 && ptr3[-1] == blankcc) {
                    --ptr3;
                }
                /* killing end of line is not worth it to erase 3
                 * spaces or less, because the escape sequence itself
                 * is 3 bytes long.
                 */
                if (ptr2 > ptr3 + 3) {
                    ptr4 = ptr3;
                    /* if the background color changes on the last
                     * space, use the generic loop to synchronize that
                     * space because the color change is non trivial
                     */
                    if (ptr3 == ptr1
                    ||  TTY_CHAR_GET_BG(*ptr3) != TTY_CHAR_GET_BG(ptr3[-1]))
                        ptr4++;
                }
            }

            /* Go to the first difference */
            /* CG: this probably does not work if there are
             * double-width glyphs on the row in front of this
             * difference (actually it should)
             */
            TTY_FPRINTF(s->STDOUT, "\033[%d;%dH",
                        y + 1, (int)(ptr1 - ptr + 1));

            while (ptr1 < ptr4) {
                cc = *ptr1;
                ptr1[shadow] = cc;
                ptr1++;
                ch = TTY_CHAR_GET_CH(cc);
                if ((unsigned int)ch != TTY_CHAR_NONE) {
                    /* output attributes */
                    if (bgcolor != (int)TTY_CHAR_GET_BG(cc)) {
                        int lastbg = bgcolor;
                        bgcolor = TTY_CHAR_GET_BG(cc);
#if TTY_STYLE_BITS == 32
                        if (ts->term_bg_colors_count > 256 && bgcolor >= 256) {
                            /* XXX: should special case dynamic palette */
                            QEColor rgb = qe_unmap_color(bgcolor, ts->tty_bg_colors_count);
                            TTY_FPRINTF(s->STDOUT, "\033[48;2;%d;%d;%dm",
                                        (rgb >> 16) & 255, (rgb >> 8) & 255, (rgb >> 0) & 255);
                        } else
#endif
                        if (ts->term_bg_colors_count > 16 && bgcolor >= 16) {
                            TTY_FPRINTF(s->STDOUT, "\033[48;5;%dm", bgcolor);
                        } else
                        if (ts->term_flags & USE_BLINK_AS_BRIGHT_BG) {
                            if (bgcolor > 7) {
                                if (lastbg <= 7) {
                                    TTY_FPUTS("\033[5m", s->STDOUT);
                                }
                            } else {
                                if (lastbg > 7) {
                                    TTY_FPUTS("\033[25m", s->STDOUT);
                                }
                            }
                            TTY_FPRINTF(s->STDOUT, "\033[%dm", 40 + (bgcolor & 7));
                        } else {
                            TTY_FPRINTF(s->STDOUT, "\033[%dm",
                                        bgcolor > 7 ? 100 + bgcolor - 8 :
                                        40 + bgcolor);
                        }
                    }
                    /* do not special case SPC on fg color change
                    * because of combining marks */
                    if (fgcolor != (int)TTY_CHAR_GET_FG(cc)) {
                        int lastfg = fgcolor;
                        fgcolor = TTY_CHAR_GET_FG(cc);
#if TTY_STYLE_BITS == 32
                        if (ts->term_fg_colors_count > 256 && fgcolor >= 256) {
                            QEColor rgb = qe_unmap_color(fgcolor, ts->tty_fg_colors_count);
                            TTY_FPRINTF(s->STDOUT, "\033[38;2;%d;%d;%dm",
                                        (rgb >> 16) & 255, (rgb >> 8) & 255, (rgb >> 0) & 255);
                        } else
#endif
                        if (ts->term_fg_colors_count > 16 && fgcolor >= 16) {
                            TTY_FPRINTF(s->STDOUT, "\033[38;5;%dm", fgcolor);
                        } else
                        if (ts->term_flags & USE_BOLD_AS_BRIGHT_FG) {
                            if (fgcolor > 7) {
                                if (lastfg <= 7) {
                                    TTY_FPUTS("\033[1m", s->STDOUT);
                                }
                            } else {
                                if (lastfg > 7) {
                                    TTY_FPUTS("\033[22m", s->STDOUT);
                                }
                            }
                            TTY_FPRINTF(s->STDOUT, "\033[%dm", 30 + (fgcolor & 7));
                        } else {
                            TTY_FPRINTF(s->STDOUT, "\033[%dm",
                                        fgcolor > 8 ? 90 + fgcolor - 8 :
                                        30 + fgcolor);
                        }
                    }
                    if (attr != (int)TTY_CHAR_GET_COL(cc)) {
                        int lastattr = attr;
                        attr = TTY_CHAR_GET_COL(cc);

                        if ((attr ^ lastattr) & TTY_BOLD) {
                            if (attr & TTY_BOLD) {
                                TTY_FPUTS("\033[1m", s->STDOUT);
                            } else {
                                TTY_FPUTS("\033[22m", s->STDOUT);
                            }
                        }
                        if ((attr ^ lastattr) & TTY_UNDERLINE) {
                            if (attr & TTY_UNDERLINE) {
                                TTY_FPUTS("\033[4m", s->STDOUT);
                            } else {
                                TTY_FPUTS("\033[24m", s->STDOUT);
                            }
                        }
                        if ((attr ^ lastattr) & TTY_BLINK) {
                            if (attr & TTY_BLINK) {
                                TTY_FPUTS("\033[5m", s->STDOUT);
                            } else {
                                TTY_FPUTS("\033[25m", s->STDOUT);
                            }
                        }
                        if ((attr ^ lastattr) & TTY_ITALIC) {
                            if (attr & TTY_ITALIC) {
                                TTY_FPUTS("\033[3m", s->STDOUT);
                            } else {
                                TTY_FPUTS("\033[23m", s->STDOUT);
                            }
                        }
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
                        /* Kludges for linedrawing chars */
                        if (ts->term_code == TERM_CYGWIN) {
                            static const char unitab_xterm_poorman[32] =
                                "*#****o~**+++++-----++++|****L. ";
                            TTY_PUTC(unitab_xterm_poorman[ch - 128],
                                     s->STDOUT);
                        } else {
                            if (!shifted) {
                                TTY_FPUTS("\033(0", s->STDOUT);
                                shifted = 1;
                            }
                            TTY_PUTC(ch - 32, s->STDOUT);
                        }
                    } else
#if COMB_CACHE_SIZE > 1
                    if (ch >= TTY_CHAR_COMB && ch < TTY_CHAR_COMB + COMB_CACHE_SIZE - 1) {
                        u8 buf[10], *q;
                        unsigned int *ip = ts->comb_cache + (ch - TTY_CHAR_COMB);
                        int ncc = *ip++;

                        if (ncc < 0x300) {
                            while (ncc-- > 1) {
                                q = s->charset->encode_func(s->charset, buf, *ip++);
                                if (q) {
                                    TTY_FWRITE(buf, 1, q - buf, s->STDOUT);
                                }
                            }
                        }
                    } else
#endif
                    {
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
                /* the current attribute is already set correctly */
                TTY_FPUTS("\033[K", s->STDOUT);
                while (ptr1 < ptr2) {
                    ptr1[shadow] = cc;
                    ptr1++;
                }
            }
//            if (ts->term_flags & USE_BLINK_AS_BRIGHT_BG)
            {
                if (bgcolor > 7) {
                    TTY_FPUTS("\033[0m", s->STDOUT);
                    fgcolor = bgcolor = -1;
                    attr = 0;
                }
            }
        }
    }

    TTY_FPUTS("\033[0m", s->STDOUT);
    if (ts->cursor_y + 1 >= 0 && ts->cursor_x + 1 >= 0) {
        TTY_FPRINTF(s->STDOUT, "\033[?25h\033[%d;%dH",
                    ts->cursor_y + 1, ts->cursor_x + 1);
    }
    fflush(s->STDOUT);

    /* Update combination cache from screen. Should do this before redisplay */
    comb_cache_clean(ts, ts->screen, ts->screen_size);
}

static int tty_dpy_bmp_alloc(QEditScreen *s, QEBitmap *bp) {
    /* the private data is a QEPicture */
    int linesize = (bp->width + 7) & ~7;   /* round up to 64 bit boundary */
    QEPicture *pp = qe_mallocz(QEPicture);
    if (!pp)
        return -1;
    pp->width = bp->width;
    pp->height = bp->height;
    pp->format = QEBITMAP_FORMAT_8BIT;  /* should depend on bp->flags */
    pp->linesize[0] = linesize;
    pp->data[0] = qe_mallocz_array(unsigned char, linesize * pp->height);
    if (!pp->data[0]) {
        qe_free(&pp);
        return -1;
    }
    bp->priv_data = pp;
    return 0;
}

static void tty_dpy_bmp_free(QEditScreen *s, QEBitmap *bp) {
    qe_free(&bp->priv_data);
}

static void tty_dpy_bmp_lock(QEditScreen *s, QEBitmap *bp, QEPicture *pict,
                             int x1, int y1, int w1, int h1)
{
    if (bp->priv_data) {
        QEPicture *pp = bp->priv_data;
        *pict = *pp;
        x1 = clamp(x1, 0, pp->width);
        y1 = clamp(y1, 0, pp->height);
        pict->width = clamp(w1, 0, pp->width - x1);
        pict->height = clamp(h1, 0, pp->height - y1);
        pict->data[0] += y1 * pict->linesize[0] + x1;
    }
}

static void tty_dpy_bmp_unlock(QEditScreen *s, QEBitmap *b) {
}

static void tty_dpy_bmp_draw(QEditScreen *s, QEBitmap *bp,
                             int dst_x, int dst_y, int dst_w, int dst_h,
                             int src_x, int src_y, qe__unused__ int flags)
{
    QEPicture *pp = bp->priv_data;
    TTYState *ts = s->priv_data;
    TTYChar *ptr = ts->screen + dst_y * s->width + dst_x;
    unsigned char *data = pp->data[0];
    int linesize = pp->linesize[0];
    int x, y;

    /* XXX: handle clipping ? */
    if (pp->format == QEBITMAP_FORMAT_8BIT) {
        for (y = 0; y < dst_h; y++) {
            unsigned char *p1 = data + (src_y + y * 2) * linesize + src_x;
            unsigned char *p2 = p1 + linesize;
            ts->line_updated[dst_y + y] = 1;
            for (x = 0; x < dst_w; x++) {
                int bg = p1[x];
                int fg = p2[x];
                if (fg == bg)
                    ptr[x] = TTY_CHAR(' ', fg, bg);
                else
                    ptr[x] = TTY_CHAR(0x2584, fg, bg);
            }
            ptr += s->width;
        }
    } else
    if (pp->format == QEBITMAP_FORMAT_RGBA32) {
#if 0
        /* XXX: inefficient and currently unused */
        for (y = 0; y < dst_h; y++) {
            QEColor *p1 = (QEColor *)(void*)(data + (src_y + y * 2) * linesize) + src_x;
            QEColor *p2 = (QEColor *)(void*)((unsigned char*)p1 + linesize);
            ts->line_updated[dst_y + y] = 1;
            for (x = 0; x < dst_w; x++) {
                QEColor bg3 = p1[x];
                QEColor fg3 = p2[x];
                int bg = QERGB_RED(bg3) / 43 * 36 + QERGB_GREEN(bg3) / 43 * 6 + QERGB_BLUE(bg3);
                int fg = QERGB_RED(fg3) / 43 * 36 + QERGB_GREEN(fg3) / 43 * 6 + QERGB_BLUE(fg3);
                if (fg == bg)
                    ptr[x] = TTY_CHAR(' ', fg, bg);
                else
                    ptr[x] = TTY_CHAR(0x2584, fg, bg);
            }
            ptr += s->width;
        }
#endif
    }
}

#ifdef CONFIG_TINY
#define tty_dpy_draw_picture   NULL
#else
static int tty_dpy_draw_picture(QEditScreen *s,
                                int dst_x, int dst_y, int dst_w, int dst_h,
                                const QEPicture *ip0,
                                int src_x, int src_y, int src_w, int src_h,
                                int flags)
{
    TTYState *ts = s->priv_data;
    TTYChar *ptr;
    int x, y;
    const QEPicture *ip = ip0;
    QEPicture *ip1 = NULL;

    if ((src_w == dst_w && src_h == 2 * dst_h)
    &&  ip->format == QEBITMAP_FORMAT_8BIT
    &&  ip->palette
    &&  ip->palette_size == 256
    &&  !memcmp(ip->palette, xterm_colors, 256 * sizeof(*ip->palette))) {
        /* Handle 8-bit picture with direct terminal colors */
        ptr = ts->screen + dst_y * s->width + dst_x;
        for (y = 0; y < dst_h; y++) {
            unsigned char *p1 = ip->data[0] + (src_y + y * 2) * ip->linesize[0] + src_x;
            unsigned char *p2 = p1 + ip->linesize[0];
            ts->line_updated[dst_y + y] = 1;
            for (x = 0; x < dst_w; x++) {
                int bg = p1[x];
                int fg = p2[x];
                if (fg == bg)
                    ptr[x] = TTY_CHAR(' ', fg, bg);
                else
                    ptr[x] = TTY_CHAR(0x2584, fg, bg);
            }
            ptr += s->width;
        }
    } else {
        /* Convert picture to true color bitmap */
        if (ip->format != QEBITMAP_FORMAT_RGBA32
        ||  !(src_w == dst_w && src_h == 2 * dst_h)) {
            ip1 = qe_create_picture(dst_w, 2 * dst_h, QEBITMAP_FORMAT_RGBA32, 0);
            if (!ip1)
                return -1;
            if (qe_picture_copy(ip1, 0, 0, ip1->width, ip1->height,
                                ip0, src_x, src_y, src_w, src_h, flags)) {
                /* unsupported conversion or scaling */
                qe_free_picture(&ip1);
                return -1;
            }
            ip = ip1;
            src_x = src_y = 0;
            src_w = ip1->width;
            src_h = ip1->height;
        }
        /* Use terminal true color emulation */
        ptr = ts->screen + dst_y * s->width + dst_x;
        for (y = 0; y < dst_h; y++) {
            uint32_t *p1 = (uint32_t*)(void*)(ip->data[0] + (src_y + y * 2) * ip->linesize[0]) + src_x;
            uint32_t *p2 = p1 + (ip->linesize[0] >> 2);
            ts->line_updated[dst_y + y] = 1;
            for (x = 0; x < dst_w; x++) {
                int bg = p1[x];
                int fg = p2[x];
                bg = TTY_RGB_BG(QERGB_RED(bg), QERGB_GREEN(bg), QERGB_BLUE(bg));
                fg = TTY_RGB_FG(QERGB_RED(fg), QERGB_GREEN(fg), QERGB_BLUE(fg));
                if (fg == bg)
                    ptr[x] = TTY_CHAR(' ', fg, bg);
                else
                    ptr[x] = TTY_CHAR(0x2584, fg, bg);
            }
            ptr += s->width;
        }
        qe_free_picture(&ip1);
    }
    return 0;
}
#endif

static void tty_dpy_describe(QEditScreen *s, EditBuffer *b)
{
    comb_cache_describe(s, b);
}

static QEDisplay tty_dpy = {
    "vt100", 1, 2,
    tty_dpy_probe,
    tty_dpy_init,
    tty_dpy_close,
    tty_dpy_flush,
    tty_dpy_is_user_input_pending,
    tty_dpy_fill_rectangle,
    tty_dpy_xor_rectangle,
    tty_dpy_open_font,
    tty_dpy_close_font,
    tty_dpy_text_metrics,
    tty_dpy_draw_text,
    tty_dpy_set_clip,
    NULL, /* dpy_selection_activate */
    NULL, /* dpy_selection_request */
    tty_dpy_invalidate,
    tty_dpy_cursor_at,
    tty_dpy_bmp_alloc,
    tty_dpy_bmp_free,
    tty_dpy_bmp_draw,
    tty_dpy_bmp_lock,
    tty_dpy_bmp_unlock,
    tty_dpy_draw_picture,
    NULL, /* dpy_full_screen */
    tty_dpy_describe,
    NULL, /* next */
};

static int tty_init(void)
{
    return qe_register_display(&tty_dpy);
}

qe_module_init(tty_init);
