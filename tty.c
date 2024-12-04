/*
 * TTY Terminal handling for QEmacs
 *
 * Copyright (c) 2000-2001 Fabrice Bellard.
 * Copyright (c) 2002-2024 Charlie Gordon.
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

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
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
    IS_SS3,
    IS_OSC,
};

enum TermCode {
    TERM_UNKNOWN = 0,
    TERM_ANSI,
    TERM_VT100,
    TERM_XTERM,
    TERM_LINUX,
    TERM_CYGWIN,
    TERM_TW100,
    TERM_SCREEN,
    TERM_QEMACS,
    TERM_ITERM,
    TERM_ITERM2,
    TERM_WEZTERM,
    TERM_APPLE_TERMINAL,
};

static const char * const term_code_name[] = {
    "UNKNOWN",
    "ANSI",
    "VT100",
    "XTERM",
    "LINUX",
    "CYGWIN",
    "TW100",
    "SCREEN",
    "QEMACS",
    "ITERM",
    "ITERM2",
    "WEZTERM",
    "APPLE_TERMINAL",
};

typedef struct TTYState {
    TTYChar *screen;
    int screen_size;
    unsigned char *line_updated;
    struct termios newtty;
    struct termios oldtty;
    int cursor_x, cursor_y;
    /* input handling */
    enum InputState input_state;
    u8 last_ch, this_ch;
    int has_meta;
    int nb_params;
#define CSI_PARAM_OMITTED  0x80000000
    int params[3];
    int leader;
    int interm;
    int utf8_index;
    unsigned char buf[8];
    const char *term_name;
    const char *term_program;
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
    /* cache for glyph combinations */
    // XXX: should keep track of max_comb and max_max_comb
    char32_t comb_cache[COMB_CACHE_SIZE];
    char *clipboard;
    size_t clipboard_size;
    int got_focus;
} TTYState;

static QEditScreen *tty_screen;   /* for tty_term_exit and tty_term_resize */

static void tty_dpy_invalidate(QEditScreen *s);

static void tty_term_resize(int sig);
static void tty_term_suspend(int sig);
static void tty_term_resume(int sig);
static void tty_term_exit(void);
static void tty_read_handler(void *opaque);

static void tty_term_set_raw(QEditScreen *s) {
    TTYState *ts;

    if (!s)
        return;

    ts = s->priv_data;

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
                "\033[?7h"          /* enter_am_mode (autowrap on) */
                "\033[39;49m"       /* orig_pair */
                "\033[?1h\033="     /* keypad_xmit */
               );
    if (tty_mk > 0) {
        /* modifyOtherKeys: report shift states */
        TTY_FPRINTF(s->STDOUT, "\033[>4;%dm", tty_mk);
    }
    if (tty_mouse > 0) {
        /* enable mouse reporting using SGR */
        TTY_FPRINTF(s->STDOUT, "\033[?%d;1006h", tty_mouse == 1 ? 1002 : 1003);
    }
    if (tty_mouse > 0 || tty_clipboard > 0) {
        /* enable focus reporting */
        TTY_FPRINTF(s->STDOUT, "\033[?1004h");
    }
#endif
    fflush(s->STDOUT);
    tcsetattr(fileno(s->STDIN), TCSANOW, &ts->newtty);
}

static void tty_term_set_cooked(QEditScreen *s) {
    TTYState *ts;

    if (!s)
        return;

    ts = s->priv_data;
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
    if (tty_mk > 0) {
        /* reset modifyOtherKeys: report regular control characters */
        TTY_FPRINTF(s->STDOUT, "\033[>4m");
    }
    if (tty_mouse > 0) {
        /* disable mouse reporting using SGR */
        TTY_FPRINTF(s->STDOUT, "\033[?%d;1006l", tty_mouse == 1 ? 1002 : 1003);
    }
    if (tty_mouse > 0 || tty_clipboard > 0) {
        /* disable focus reporting */
        TTY_FPRINTF(s->STDOUT, "\033[?1004l");
    }
#endif
    fflush(s->STDOUT);
    tcsetattr(fileno(s->STDIN), TCSANOW, &ts->oldtty);
}

static int tty_dpy_probe(void)
{
    return 1;
}

static const char *getenv1(const char *name) {
    const char *p = getenv(name);
    return p ? p : "";
}

static int tty_dpy_init(QEditScreen *s, QEmacsState *qs,
                        qe__unused__ int w, qe__unused__ int h)
{
    TTYState *ts;
    struct termios tty;
    struct sigaction sig;
    const char *p;

    ts = qe_mallocz(TTYState);
    if (ts == NULL) {
        return 1;
    }

    tty_screen = s;
    s->qs = qs;
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

    ts->term_program = getenv("TERM_PROGRAM");
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
        if (strstart(ts->term_name, "xterm", NULL) || getenv("XTERM_VERSION")) {
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
        } else
        if (strstart(ts->term_name, "screen", NULL)) {
            ts->term_code = TERM_SCREEN;
        }
    }

    /* Use alternate environment variables */
    // OSTYPE=darwin23
    // TERM_PROGRAM=Apple_Terminal / TERM_PROGRAM_VERSION=453
    // TERM_PROGRAM=iTerm.app / TERM_PROGRAM_VERSION=3.5.5
    //    LC_TERMINAL=iTerm2 / LC_TERMINAL_VERSION=3.5.5
    // XTERM_LOCALE=C / XTERM_VERSION='XTerm(378)'
    // TERM_PROGRAM=WezTerm / TERM_PROGRAM_VERSION=20240203-110809-5046fc22
    // TERM_PROGRAM=qemacs / TERM_PROGRAM_VERSION=6.3.2

    if (ts->term_program) {
        /* This mostly works for local use, remote connections typically
         * do not set the TERM_PROGRAM variable.
         */
        // TODO: should query terminal capabilities
        if (strequal(ts->term_program, "iTerm.app")) {
            ts->term_code = TERM_ITERM;
            if (strequal(getenv1("LC_TERMINAL"), "iTerm2"))
                ts->term_code = TERM_ITERM2;
        } else
        if (strequal(ts->term_program, "WezTerm")) {
            ts->term_code = TERM_WEZTERM;
        } else
        if (strequal(ts->term_program, "qemacs")) {
            ts->term_code = TERM_QEMACS;
        } else
        if (strequal(ts->term_program, "Apple_Terminal")) {
            ts->term_code = TERM_APPLE_TERMINAL;
        }
    }
    switch (ts->term_code) {
    case TERM_XTERM:
    case TERM_ITERM:
    case TERM_ITERM2:
    case TERM_WEZTERM:
        if (tty_mk < 0)
            tty_mk = 2;
        if (tty_mouse < 0)
            tty_mouse = 1;
        if (tty_clipboard < 0)
            tty_clipboard = 1;
        break;
    case TERM_APPLE_TERMINAL:
        if (tty_mouse < 0)
            tty_mouse = 1;
#ifdef CONFIG_DARWIN
        /* use pbcopy / pbpaste if running natively */
        if (tty_clipboard < 0)
            tty_clipboard = 2;
#endif
        break;
    case TERM_QEMACS:
    case TERM_SCREEN:
        if (tty_mk < 0)
            tty_mk = 0;
        if (tty_mouse < 0)
            tty_mouse = 0;
        if (tty_clipboard < 0)
            tty_clipboard = 0;
        break;
    default:
        break;
    }

    if (ts->term_name) {
        if (strstr(ts->term_name, "true") || strstr(ts->term_name, "24")) {
            ts->term_flags |= USE_TRUE_COLORS | USE_256_COLORS;
        }
        if (strstr(ts->term_name, "256")) {
            ts->term_flags |= USE_256_COLORS;
        }
    }
    if (ts->term_code == TERM_ITERM || ts->term_code == TERM_ITERM2) {
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

    ts->tty_bg_colors_count = min_int(ts->term_bg_colors_count, TTY_BG_COLORS);
    ts->tty_fg_colors_count = min_int(ts->term_fg_colors_count, TTY_FG_COLORS);
    ts->tty_colors = xterm_colors;

    tcgetattr(fileno(s->STDIN), &tty);
    ts->oldtty = tty;

    /* input modes: no break, no CR to NL, no parity check, no strip char,
     * no start/stop output control. */
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP |
                     INLCR | IGNCR | ICRNL | IXON);
    /* output modes - disable post processing */
    tty.c_oflag &= ~(OPOST);
    /* local modes - echoing off, canonical off, no extended functions,
     * no signal chars (^Z,^C) */
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN | ISIG);
    /* control modes - set 8 bit chars, disable parity handling */
    tty.c_cflag &= ~(CSIZE | PARENB);
    tty.c_cflag |= CS8;
    /* control chars - set return condition: min number of bytes and timer.
     * We want read to return every single byte, without timeout. */
    tty.c_cc[VMIN] = 1;   /* 1 byte */
    tty.c_cc[VTIME] = 0;  /* no timer */
    if (tty.c_cc[VERASE] == 8)
        ts->term_flags |= KBS_CONTROL_H;

    ts->newtty = tty;
    tty_term_set_raw(s);

    /* Get charset from command line option */
    s->charset = qe_find_charset(qs, qs->tty_charset);

    if (ts->term_code == TERM_CYGWIN)
        s->charset = &charset_8859_1;

    if (ts->term_code == TERM_TW100)
        s->charset = qe_find_charset(qs, "atarist");

    // XXX: default charset for non tty invocations should be UTF-8
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
        /* Just print UTF-8 encoding for eacute and check cursor position */
        TTY_FPRINTF(s->STDOUT, "%s",
                    "\030\032"
                    "\r\xC3\xA9"
                    "\033[6n"
                    /* "\033D" */);
        fflush(s->STDOUT);
        /* XXX: should have a timeout to avoid locking on unsupported terminals */
        n = fscanf(s->STDIN, "\033[%d;%dR", &y, &x);  /* get cursor position */
        TTY_FPRINTF(s->STDOUT, "\r   \r");        /* go back, erase 3 chars */
        if (n == 2 && x == 2) {
#if 0
            /* determine the unicode version supported */
            static char const wide_by_version[][6] = {
                /*  4.1.0: default, 5.0.0: same wide characters */
                {  41, 0x00 },
                /*  5.1.0:  40892 U+9FBC  */
                {  51, 0xE9, 0xBE, 0xBC },
                /*  5.2.0: 127535 U+1F22F */
                {  52, 0xF0, 0x9F, 0x88, 0xAF },
                /*  6.0.0: 127489 U+1F201 SQUARED KATAKANA KOKO */
                {  60, 0xF0, 0x9F, 0x88, 0x81 },
                /*  8.0.0: 127568 U+1F250 CIRCLED IDEOGRAPH ADVANTAGE */
                {  80, 0xF0, 0x9F, 0x89, 0x90 },
                /*  9.0.0: 128057 U+1F439 HAMSTER FACE */
                {  90, 0xF0, 0x9F, 0x90, 0xB9 },
                /* 10.0.0: 129430 U+1F996 T-REX */
                { 100, 0xF0, 0x9F, 0xA6, 0x96 },
                /* 11.0.0: 129514 U+1F9EA TEST TUBE */
                { 110, 0xF0, 0x9F, 0xA7, 0xAA },
                /* 12.0.0: 129680 U+1FA90 RINGED PLANET */
                { 120, 0xF0, 0x9F, 0xAA, 0x90 },
                /* 12.1.0:  13055 U+32FF  SQUARE ERA NAME REIWA */
                { 121, 0xE3, 0x8B, 0xBF },
                /* 13.0.0: 129749 U+1FAD5 FONDUE */
                { 130, 0xF0, 0x9F, 0xAB, 0x95 },
                /* 14.0.0: 128733 U+1F6DD PLAYGROUND SLIDE */
                { 140, 0xF0, 0x9F, 0x9B, 0x9D },
                /* 15.0.0: 128732 U+1F6DC WIRELESS */
                { 150, 0xF0, 0x9F, 0x9B, 0x9C },
            };
            int i;
            for (i = countof(wide_by_version); i --> 1;) {
                TTY_FPRINTF(s->STDOUT, "\r%s\033[6n", wide_by_version[i] + 1);
                fflush(s->STDOUT);
                n = fscanf(s->STDIN, "\033[%d;%dR", &y, &x);  /* get cursor position */
                TTY_FPRINTF(s->STDOUT, "\r    \r");          /* go back, erase 4 chars */
                if (n != 2)
                    break;
                if (x == 3) {
                    s->unicode_version = (unsigned char)wide_by_version[i][0];
                    break;
                }
            }
#endif
            s->charset = &charset_utf8;
        }
    }
    put_status(qs->active_window, "TTY charset: %s", s->charset->name);

    atexit(tty_term_exit);

    sig.sa_handler = tty_term_resize;
    sigemptyset(&sig.sa_mask);
    sig.sa_flags = 0;
    sigaction(SIGWINCH, &sig, NULL);
    sig.sa_handler = tty_term_suspend;
    sigaction(SIGTSTP, &sig, NULL);
    sig.sa_handler = tty_term_resume;
    sigaction(SIGCONT, &sig, NULL);

    fcntl(fileno(s->STDIN), F_SETFL, O_NONBLOCK);
    /* If stdout is to a pty, make sure we aren't in nonblocking mode.
     * Otherwise, the printf()s in term_flush() can fail with EAGAIN,
     * causing repaint errors when running in an xterm or in a screen
     * session. */
    fcntl(fileno(s->STDOUT), F_SETFL, 0);

    set_read_handler(fileno(s->STDIN), tty_read_handler, s);

    tty_dpy_invalidate(s);

#if 0
    if (ts->term_flags & KBS_CONTROL_H) {
        qe_toggle_control_h(qs, 1);
    }
#endif
    return 0;
}

static void tty_dpy_close(QEditScreen *s)
{
    TTYState *ts = s->priv_data;

    fcntl(fileno(s->STDIN), F_SETFL, 0);

    tty_term_set_cooked(s);

    qe_free(&ts->screen);
    qe_free(&ts->line_updated);
    qe_free(&ts->clipboard);
    qe_free(&s->priv_data);
}

static void tty_dpy_suspend(QEditScreen *s)
{
    kill(getpid(), SIGTSTP);
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

static void tty_term_suspend(qe__unused__ int sig)
{
    tty_term_set_cooked(tty_screen);

    kill(getpid(), SIGSTOP);
}

static void tty_term_resume(qe__unused__ int sig)
{
    tty_term_set_raw(tty_screen);
    tty_term_resize(0);
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
    // XXX: test for failure
    qe_realloc_array(&ts->screen, count * 2 + 1);
    qe_realloc_array(&ts->line_updated, s->height);
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

static int tty_get_clipboard(QEditScreen *s, int ch)
{
    QEmacsState *qs = s->qs;
    TTYState *ts = s->priv_data;
    EditBuffer *b;
    size_t size;
    char *contents;

    // OSC 52;Ps;string ST  report clipboard contents
    const char *p1 = cs8(qs->input_buf) + 2;
    const char *p4 = cs8(qs->input_buf) + qs->input_len - 1 - (ch != 7);
    const char *p2 = strchr(p1, ';');
    const char *p3 = NULL;
    if (p2 == NULL)
        return -1;
    p2++;
    p3 = strchr(p2, ';');
    if (p3 == NULL)
        return -1;
    p3++;
    if (p3 >= p4)
        return -1;

    contents = qe_decode64(p3, p4 - p3, &size);
    if (!contents)
        return -1;
    if (qs->trace_buffer)
        qe_trace_bytes(qs, contents, size, EB_TRACE_CLIPBOARD);
    if (size == ts->clipboard_size && !memcmp(ts->clipboard, contents, size)) {
        qe_free(&contents);
        return 0;
    } else {
        qe_free(&ts->clipboard);
        ts->clipboard = contents;
        ts->clipboard_size = size;
        /* copy terminal selection a new yank buffer */
        b = qe_new_yank_buffer(qs, NULL);
        eb_set_charset(b, &charset_utf8, EOL_UNIX);
        eb_write(b, 0, contents, size);
        return 1;
    }
}

static int tty_request_clipboard(QEditScreen *s)
{
    if (tty_clipboard == 1) {
        qe_trace_bytes(s->qs, "tty-request-clipboard", -1, EB_TRACE_COMMAND);
        TTY_FPUTS("\033]52;;?\007", s->STDOUT);
        fflush(s->STDOUT);
        // FIXME: should read tty response with a 100ms timeout
        return 1;
    }
#ifdef CONFIG_DARWIN
    if (tty_clipboard == 2) {
        QEmacsState *qs = s->qs;
        TTYState *ts = s->priv_data;
        FILE *fp;
        char *contents = NULL;
        size_t size = 0, allocated_size = 0;
        int c;
        EditBuffer *b;
        qe_trace_bytes(qs, "pbpaste", -1, EB_TRACE_COMMAND);
        fp = popen("pbpaste", "r");
        if (fp == NULL) {
            qe_trace_bytes(qs, "failed", -1, EB_TRACE_COMMAND);
            qe_free(&contents);
            return -1;
        }
        // FIXME: should have a timeout?
        while ((c = getc(fp)) != EOF) {
            if (size == allocated_size) {
                allocated_size += allocated_size / 2 + 32;
                qe_realloc_array(&contents, allocated_size);
            }
            contents[size++] = (char)c;
        }
        pclose(fp);
        if (qs->trace_buffer)
            qe_trace_bytes(qs, contents, size, EB_TRACE_CLIPBOARD);
        if (size == ts->clipboard_size && !memcmp(ts->clipboard, contents, size)) {
            qe_free(&contents);
            return 0;
        }
        qe_free(&ts->clipboard);
        ts->clipboard = contents;
        ts->clipboard_size = size;
        /* copy terminal selection a new yank buffer */
        b = qe_new_yank_buffer(qs, NULL);
        eb_set_charset(b, &charset_utf8, EOL_UNIX);
        eb_write(b, 0, contents, size);
        return 1;
    }
#endif
    return -1;
}

static int tty_set_clipboard(QEditScreen *s)
{
    // TODO: make this selectable
    QEmacsState *qs = s->qs;
    TTYState *ts = s->priv_data;
    EditBuffer *b = qs->yank_buffers[qs->yank_current];
    size_t size;
    char *contents;

    if (!b)
        return 0;
    size = eb_get_region_content_size(b, 0, b->total_size);
    contents = qe_malloc_bytes(size + 1);
    if (!contents)
        return -1;
    eb_get_region_contents(b, 0, b->total_size, contents, size + 1, FALSE);
    if (size == ts->clipboard_size && !memcmp(ts->clipboard, contents, size)) {
        qe_free(&contents);
        return 0;
    }
#ifdef CONFIG_DARWIN
    if (tty_clipboard == 2) {
        FILE *fp;
        qe_trace_bytes(qs, "pbcopy", -1, EB_TRACE_COMMAND);
        fp = popen("pbcopy", "w");
        if (fp == NULL) {
            qe_trace_bytes(qs, "failed", -1, EB_TRACE_COMMAND);
            qe_free(&contents);
            return -1;
        }
        fwrite(contents, 1, size, fp);
        pclose(fp);
    }
#endif
    if (tty_clipboard == 1) {
        size_t encoded_size;
        char *encoded_contents = qe_encode64(contents, size, &encoded_size);
        if (!encoded_contents) {
            qe_free(&contents);
            return -1;
        }
        qe_trace_bytes(qs, "tty-set-clipboard", -1, EB_TRACE_COMMAND);
        TTY_FPRINTF(s->STDOUT, "\033]52;;%s\033\\", encoded_contents);
        fflush(s->STDOUT);
        qe_free(&encoded_contents);
    }
    qe_free(&ts->clipboard);
    ts->clipboard = contents;
    ts->clipboard_size = size;
    return 0;
}

static int const csi_lookup[] = {
    KEY_UNKNOWN,  /* 0 */
    KEY_HOME,     /* 1 */
    KEY_INSERT,   /* 2 */
    KEY_DELETE,   /* 3 */
    KEY_END,      /* 4 */
    KEY_PAGEUP,   /* 5 */
    KEY_PAGEDOWN, /* 6 */
    KEY_UNKNOWN,  /* 7 */
    KEY_UNKNOWN,  /* 8 */
    KEY_UNKNOWN,  /* 9 */
    KEY_UNKNOWN,  /* 10 */
    KEY_F1,       /* 11 */
    KEY_F2,       /* 12 */
    KEY_F3,       /* 13 */
    KEY_F4,       /* 14 */
    KEY_F5,       /* 15 */
    KEY_UNKNOWN,  /* 16 */
    KEY_F6,       /* 17 */
    KEY_F7,       /* 18 */
    KEY_F8,       /* 19 */
    KEY_F9,       /* 20 */
    KEY_F10,      /* 21 */
    KEY_UNKNOWN,  /* 22 */
    KEY_F11,      /* 23 */
    KEY_F12,      /* 24 */
    KEY_F13,      /* 25 */
    KEY_F14,      /* 26 */
    KEY_UNKNOWN,  /* 27 */
    KEY_F15,      /* 28 */
    KEY_F16,      /* 29 */
    KEY_UNKNOWN,  /* 30 */
    KEY_F17,      /* 31 */
    KEY_F18,      /* 32 */
    KEY_F19,      /* 33 */
    KEY_F20,      /* 34 */
};

static void tty_read_handler(void *opaque)
{
    QEditScreen *s = opaque;
    QEmacsState *qs = s->qs;
    TTYState *ts = s->priv_data;
    QEEvent ev1, *ev = qe_event_clear(&ev1);
    u8 buf[1];
    int ch, len, n1, n2, pending, shift;

    if (read(fileno(s->STDIN), buf, 1) != 1)
        return;

    if (qs->trace_buffer)
        qe_trace_bytes(qs, buf, 1, EB_TRACE_TTY);

    shift = 0;
    ch = buf[0];
    ts->last_ch = ts->this_ch;
    ts->this_ch = ch;
    /* keep TTY bytes for error messages */
    if (qs->input_len >= qs->input_size) {
        qs->input_size += qs->input_size / 2 + 64;
        if (qs->input_buf == qs->input_buf_def) {
            qs->input_buf = qe_malloc_bytes(qs->input_size);
            memcpy(qs->input_buf, qs->input_buf_def, qs->input_len);
        } else {
            qe_realloc_bytes(&qs->input_buf, qs->input_size);
        }
    }
    qs->input_buf[qs->input_len++] = ch;

    switch (ts->input_state) {
    case IS_NORM:
        qs->input_len = 1;
        qs->input_buf[0] = ch;
        /* charset handling */
        if (s->charset == &charset_utf8) {
            if (ts->utf8_index && (ch ^ 0x80) > 0x3f) {
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
                 * keys transmitting escape sequences starting with \033
                 * but followed immediately by more characters.
                 */
                goto the_end_meta;
            }
            ts->input_state = IS_ESC;
            if (qs->input_buf != qs->input_buf_def) {
                qe_free(&qs->input_buf);
                qs->input_buf = qs->input_buf_def;
                qs->input_size = countof(qs->input_buf_def);
                qs->input_buf[0] = ch;
                qs->input_len = 1;
            }
            break;
        }
        if (ch == '\010') {
            /* backspace */
            if (ts->term_flags & KBS_CONTROL_H)
                ch = KEY_DEL;
        }
        goto the_end_meta;

    case IS_ESC:
        pending = tty_dpy_is_user_input_pending(s);
        if (ch == '\033') {
            if (!pending) {
                /* Distinguish alt-ESC from ESC prefix applied to other keys */
                ch = KEY_META(KEY_ESC);
                goto the_end;
            }
            /* cygwin A-right transmit ESC ESC[C ... */
            ts->has_meta = KEY_STATE_META;
            break;
        }
        if (ch == '[' && pending) { // CSI
            ts->input_state = IS_CSI;
            ts->nb_params = 0;
            ts->params[0] = CSI_PARAM_OMITTED;
            ts->params[1] = CSI_PARAM_OMITTED;
            ts->params[2] = CSI_PARAM_OMITTED;
            ts->leader = 0;
            ts->interm = 0;
            break;
        }
        if (ch == 'O' && pending) {
            ts->input_state = IS_SS3;
            ts->nb_params = 0;
            ts->params[0] = 0;
            ts->interm = 0;
            break;
        }
        if (ch == ']' && pending) {
            /* OSC terminal responses
               eg: set palette entry: \033]4;0;rgb:0000/0000/0000\007
                   set clipboard: \033]52;0;base64contents\033\\
             */
            ts->input_state = IS_OSC;
            ts->has_meta = 0;
            break;
        }
        /* FIXME: handle terminal answers */
        ch = KEY_META(ch);
        goto the_end;

    case IS_CSI:
        /* CSI syntax is: CSI P ... P I ... I F
         * P are parameter bytes (range 30 to 3F)
         *   '<', '=', '>' and '?' parameter bytes usually only appear
         *   first and are referred to as leader bytes. Note however that
         *   requests may be issued for multiple arguments, each using a
         *   '?' before the argument value.
         * I are intermediary bytes (range 20 to 2F)
         * F is the final byte (range 40..7E)
         * we only support a single leader and intermediary byte.
         */
        if (ch >= 0x20 && ch <= 0x2F) {
            /* intermediary byte: only keep the last one */
            ts->interm = ch;
            break;
        }
        if (ch >= 0x3C && ch <= 0x3F) {
            /* leader byte: only keep the last one */
            ts->leader = ch;
            break;
        }
        if (ch >= '0' && ch <= '9') {
            if (ts->interm) {
                /* syntax error: ignore CSI sequence */
                ts->input_state = IS_NORM;
                ts->has_meta = 0;
                break;
            }
            if (ts->nb_params < countof(ts->params)) {
                ts->params[ts->nb_params] &= ~CSI_PARAM_OMITTED;
                ts->params[ts->nb_params] *= 10;
                ts->params[ts->nb_params] += ch - '0';
            }
            break;
        }
        ts->nb_params++;
        if (ts->nb_params < countof(ts->params)) {
            ts->params[ts->nb_params] = CSI_PARAM_OMITTED;
        }
        if (ch == ':' || ch == ';')
            break;
        /* FIXME: Should only accept final bytes in range 40..7E,
         * and ignore other bytes.
         */
        n1 = ts->params[0] >= 0 ? ts->params[0] : 0;
        n2 = ts->params[1] >= 0 ? ts->params[1] : 1;
        switch ((ts->leader << 16) | (ts->interm << 8) | ch) {
        case '[':
            /* cygwin/linux terminal: non standard sequence */
            ts->input_state = IS_CSI2;
            break;
        case '~':
            /* extended key:
             * first argument is the key number
             * second argument if present is the shift state + 1
             * 1:shift, 2:alt, 4:control, 8:command/hyper
             */
            if (n2) n2 -= 1;
            if (n1 == 27 && ts->nb_params >= 3 && ts->params[2] >= 0) {
                /* xterm modifyOtherKeys extension */
                ch = ts->params[2];
                if (ch == 8 && (ts->term_flags |= KBS_CONTROL_H))
                    ch = KEY_DEL;
                // XXX: iTerm2 3.14.19 encoding bug for M-C-a to M-C-z
                if (n2 == 4 && ch >= 'A' && ch <= 'Z') {
                    n2 |= 2;
                    ch += 'a' - 'A';
                }
                ch = get_modified_key(ch, n2);
                goto the_end_meta;
            }
            if (n1 < countof(csi_lookup)) {
                ch = csi_lookup[n1];
                goto the_end_modified;
            }
            ch = KEY_UNKNOWN;
            goto the_end;
        case 'u':
            /* Paul "LeoNerd" Evans' CSI u protocol:
             * see https://www.leonerd.org.uk/hacks/fixterms/
             * supported by xterm if formatOtherKeys resource is selected
             */
            ch = get_modified_key(n1, n2 - 1);
            goto the_end_meta;
            /* All these for ansi|cygwin */
        case ('<'<<16)|'M':
        case ('<'<<16)|'m':
            /* Mouse events */
            ts->input_state = IS_NORM;
            ts->has_meta = 0;
            if (ts->got_focus) {
                /* Ignore mouse events until either:
                   - down event is received after 100ms threshold
                   - up event is received (and ignored)
                 */
                if (ch == 'M' && get_clock_ms() - ts->got_focus > 100) {
                    ts->got_focus = 0;
                } else {
                    if (ch == 'm')
                        ts->got_focus = 0;
                    break;
                }
            }
            if (ch == 'M')
                ev->button_event.type = QE_BUTTON_PRESS_EVENT;
            else
                ev->button_event.type = QE_BUTTON_RELEASE_EVENT;
            if (n1 & 32) {
                /* button drag events (enabled by 1002) */
                /* any motion events (enabled by 1003) */
                ev->button_event.type = QE_MOTION_EVENT;
                /* if n1 & 3 == 3 -> non button move */
            }
            ev->button_event.x = ts->params[1] - 1;
            ev->button_event.y = ts->params[2] - 1;
            ev->button_event.button = 0;
            if (n1 & 4)
                shift |= KEY_STATE_SHIFT;
            if (n1 & 8)
                shift |= KEY_STATE_META;
            if (n1 & 16)
                shift |= KEY_STATE_CONTROL;
            ev->button_event.shift = shift;
            switch (n1 & ~(4|8|16|32)) {
            case 0:
                ev->button_event.button = QE_BUTTON_LEFT;
                break;
            case 1:
                ev->button_event.button = QE_BUTTON_MIDDLE;
                break;
            case 2:
                ev->button_event.button = QE_BUTTON_RIGHT;
                break;
            case 3:
                ev->button_event.button = QE_BUTTON_NONE;
                break;
            case 64:
                ev->button_event.button = QE_WHEEL_UP;
                break;
            case 65:
                ev->button_event.button = QE_WHEEL_DOWN;
                break;
            default:
                break;
            }
            qe_handle_event(qs, ev);
            break;
        case 'I': // FocusIn  enabled by CSI ? 1004 h
            ts->input_state = IS_NORM;
            ts->has_meta = 0;
            ts->got_focus = get_clock_ms();
            qe_trace_bytes(qs, "tty-focus-in", -1, EB_TRACE_COMMAND);
            if (tty_clipboard > 0) {
                /* request clipboard contents into kill buffer if changed */
                tty_request_clipboard(s);
            }
            break;
        case 'O': // FocusOut
            ts->input_state = IS_NORM;
            ts->has_meta = 0;
            qe_trace_bytes(qs, "tty-focus-out", -1, EB_TRACE_COMMAND);
            if (tty_clipboard > 0) {
                /* push last kill to clipboard if new */
                tty_set_clipboard(s);
            }
            break;
        default:
            /* n2 contains the shift status + 1:
             * bit 1 is SHIFT
             * bit 2 is ALT
             * bit 4 is CTRL
             */
            /* xterm CTRL-arrows
             * iterm2 CTRL-arrows:
             * C-up    = ^[[1;5A
             * C-down  = ^[[1;5B
             * C-right = ^[[1;5C
             * C-left  = ^[[1;5D
             * C-end   = ^[[1;5F
             * C-home  = ^[[1;5H
             */
            /* iterm2 SHIFT-arrows:
             * S-up    = ^[[1;2A
             * S-down  = ^[[1;2B
             * S-right = ^[[1;2C
             * S-left  = ^[[1;2D
             * S-f1 = ^[[1;2P
             * S-f2 = ^[[1;2Q
             * S-f3 = ^[[1;2R
             * S-f4 = ^[[1;2S
             */
            if (n2) n2 -= 1;
            switch (ch) {
            case 'A': ch = KEY_UP;        goto the_end_modified; // kcuu1
            case 'B': ch = KEY_DOWN;      goto the_end_modified; // kcud1
            case 'C': ch = KEY_RIGHT;     goto the_end_modified; // kcuf1
            case 'D': ch = KEY_LEFT;      goto the_end_modified; // kcub1
            case 'F': ch = KEY_END;       goto the_end_modified; // kend
            //case 'G': ch = KEY_CENTER;  goto the_end_modified; // kb2
            case 'H': ch = KEY_HOME;      goto the_end_modified; // khome
            case 'L': ch = KEY_INSERT;    goto the_end_modified; // kich1
            case 'P': ch = KEY_F1;        goto the_end_modified;
            case 'Q': ch = KEY_F2;        goto the_end_modified;
            case 'R': ch = KEY_F3;        goto the_end_modified;
            case 'S': ch = KEY_F4;        goto the_end_modified;
            case 'Z': ch = KEY_SHIFT_TAB; goto the_end_modified; // kcbt
            }
            ch = KEY_UNKNOWN;
            goto the_end;
        }
        break;
    case IS_CSI2:
        /* cygwin/linux terminal */
        switch (ch) {
        case 'A': ch = KEY_F1; goto the_end_meta;
        case 'B': ch = KEY_F2; goto the_end_meta;
        case 'C': ch = KEY_F3; goto the_end_meta;
        case 'D': ch = KEY_F4; goto the_end_meta;
        case 'E': ch = KEY_F5; goto the_end_meta;
        }
        ch = KEY_UNKNOWN;
        goto the_end;

    case IS_SS3:       // "\EO"
        /* xterm/vt100 fn */
        if (ch >= '0' && ch <= '9') {
            ts->params[0] *= 10;
            ts->params[0] += ch - '0';
            break;
        }
        n2 = ts->params[0] > 0 ? ts->params[0] - 1 : 0;
        switch (ch) {
        case 'A': ch = KEY_UP;         goto the_end_modified;
        case 'B': ch = KEY_DOWN;       goto the_end_modified;
        case 'C': ch = KEY_RIGHT;      goto the_end_modified;
        case 'D': ch = KEY_LEFT;       goto the_end_modified;
        case 'F': ch = KEY_END;        goto the_end_modified; /* iterm2 F-right */
        case 'H': ch = KEY_HOME;       goto the_end_modified; /* iterm2 F-left */
        case 'M': ch = KEY_RET;        goto the_end_modified; /* Enter on keypad */
        case 'P': ch = KEY_F1;         goto the_end_modified;
        case 'Q': ch = KEY_F2;         goto the_end_modified;
        case 'R': ch = KEY_F3;         goto the_end_modified;
        case 'S': ch = KEY_F4;         goto the_end_modified;
        case 't': ch = KEY_F5;         goto the_end_modified;
        case 'u': ch = KEY_F6;         goto the_end_modified;
        case 'v': ch = KEY_F7;         goto the_end_modified;
        case 'l': ch = KEY_F8;         goto the_end_modified;
        case 'w': ch = KEY_F9;         goto the_end_modified;
        case 'x': ch = KEY_F10;        goto the_end_modified;
        default:  ch = KEY_UNKNOWN;    goto the_end;
        }
    the_end_modified:
        /* modify the special key */
        if (n2 & 1) {
            shift |= KEY_STATE_SHIFT;
            ch = KEY_SHIFT(ch);
        }
        if (n2 & (2 | 8)) {
            shift |= KEY_STATE_META;
            ch = KEY_META(ch);
        }
        if (n2 & 4) {
            shift |= KEY_STATE_CONTROL;
            ch = KEY_CONTROL(ch);
        }

    the_end_meta:
        if (ts->has_meta)
            ch = KEY_META(ch);

    the_end:
        ts->input_state = IS_NORM;
        ts->has_meta = 0;
        // XXX: should add custom key sequences, especially for KEY_UNKNOWN
        ev->key_event.type = QE_KEY_EVENT;
        ev->key_event.shift = shift;
        ev->key_event.key = ch;
        qe_handle_event(qs, ev);
        break;

    case IS_OSC:
        /* OSC syntax is: OSC string 07 or OSC string ST (ESC \) */
        /* stop reading messge on BEL, ST and ESC \ */
        if (!(ch == 7 || ch == 0x9C || (ch == '\\' && ts->last_ch == 27)))
            break;
        ts->input_state = IS_NORM;
        ts->has_meta = 0;
        n1 = strtol(cs8(qs->input_buf) + 2, NULL, 10);
        if (qs->trace_buffer) {
            char buf1[32];
            snprintf(buf1, sizeof buf1, "tty-osc-%d", n1);
            qe_trace_bytes(qs, buf1, -1, EB_TRACE_COMMAND);
        }
        if (n1 == 52) {
            if (tty_clipboard > 0) {
                tty_get_clipboard(s, ch);
            }
        }
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

static inline int tty_term_glyph_width(qe__unused__ QEditScreen *s, char32_t ucs)
{
    /* fast test for majority of non-wide scripts */
    if (ucs < 0x300)
        return 1;

    return qe_wcwidth(ucs);
}

static void tty_dpy_text_metrics(QEditScreen *s, QEFont *font,
                                 QECharMetrics *metrics,
                                 const char32_t *str, int len)
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

static char32_t comb_cache_add(TTYState *ts, const char32_t *seq, int len) {
    char32_t *ip;
    for (ip = ts->comb_cache; *ip; ip += *ip & 0xFFFF) {
        if (*ip == len + 1U && !blockcmp(ip + 1, seq, len)) {
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
    blockcpy(ip + 1, seq, len);
    return TTY_CHAR_COMB + (ip - ts->comb_cache);
}

static void comb_cache_clean(TTYState *ts, const TTYChar *screen, int len) {
    char32_t *ip;
    int i;

    /* quick exit if cache is empty */
    if (ts->comb_cache[0] == 0)
        return;

    /* mark all entries as free */
    for (ip = ts->comb_cache; *ip != 0; ip += *ip & 0xFFFF) {
        *ip |= 0x10000;
    }
    ip = ts->comb_cache;
    /* scan the actual screen for combining glyphs */
    for (i = 0; i < len; i++) {
        char32_t ch = TTY_CHAR_GET_CH(screen[i]);
        if (ch >= TTY_CHAR_COMB && ch < TTY_CHAR_COMB + countof(ts->comb_cache) - 1) {
            /* mark the cache entry as used */
            ip[ch - TTY_CHAR_COMB] &= ~0x10000;
        }
    }
    /* scan the cache to coalesce free entries */
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

static void comb_cache_describe(QEditScreen *s, EditBuffer *b, TTYState *ts) {
    char32_t *ip;
    unsigned int i;

    eb_printf(b, "\nUnicode combination cache:\n\n");

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
#define comb_cache_describe(s, b, ts)
#endif

static void tty_dpy_draw_text(QEditScreen *s, QEFont *font,
                              int x, int y, const char32_t *str0, int len,
                              QEColor color)
{
    TTYState *ts = s->priv_data;
    TTYChar *ptr;
    int fgcolor, w, n;
    char32_t cc;
    const char32_t *str = str0;

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
    int y, shadow, ch, bgcolor, fgcolor, shifted, gotopos, attr;

    /* CG: Should optimize output by computing it in a temporary buffer
     * and flushing it in one call to fwrite()
     */

    /* Hide cursor, goto home, reset attributes */
    TTY_FPUTS("\033[?25l\033[H\033[0m", s->STDOUT);

    if (ts->term_code != TERM_CYGWIN) {
        TTY_FPUTS("\033(B\033)0", s->STDOUT);
    }

    bgcolor = -1;
    fgcolor = -1;
    attr = 0;
    shifted = 0;
    gotopos = 0;

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
            gotopos = 1;
            while (ptr1 < ptr4) {
                cc = *ptr1;
                ptr1[shadow] = cc;
                ptr1++;
                ch = TTY_CHAR_GET_CH(cc);
                if ((char32_t)ch == TTY_CHAR_NONE)
                    continue;
                if (gotopos) {
                    /* Move the cursor: row and col are 1 based
                       but ptr1 has already been incremented */
                    gotopos = 0;
                    TTY_FPRINTF(s->STDOUT, "\033[%d;%dH",
                                y + 1, (int)(ptr1 - ptr));
                }
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
                    char32_t *ip = ts->comb_cache + (ch - TTY_CHAR_COMB);
                    int ncc = *ip++;

                    /* this is a sanity test: check that we have a valid combination
                       offset: should actually test against some maximum number of
                       combining glyphs which should likely be below 32.
                     */
                    if (ncc < 0x300) {
                        while (ncc-- > 1) {
                            q = s->charset->encode_func(s->charset, buf, *ip++);
                            if (q) {
                                TTY_FWRITE(buf, 1, q - buf, s->STDOUT);
                                // XXX: should check s->unicode_version for
                                //      terminal support of non ASCII codepoint
                                //      and force GOTOPOS if unsupported
                                /* force cursor repositioning if glyph may have variants */
                                gotopos |= qe_wcwidth_variant(ip[-1]);
                            } else {
                                gotopos = 1;
                            }
                        }
                    } else {
                        /* invalid comb cache offset: must issue gotopos */
                        gotopos = 1;
                    }
                } else
#endif
                {
                    u8 buf[10], *q;
                    int nc;

                    // was in qemacs-0.3.1.g2.gw/tty.c:
                    // if (cc == 0x2500)
                    //    printf("\016x\017");
                    /* s->charset is either Latin1 or UTF-8 */
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
                    } else {
                        // XXX: check s->unicode_version for
                        //      terminal support of non ASCII codepoint
                        //      and force GOTOPOS if unsupported
                        /* force cursor repositioning if glyph may have variants */
                        gotopos |= qe_wcwidth_variant(ch);
                    }
                    nc = q - buf;
                    if (nc == 1) {
                        TTY_PUTC(*buf, s->STDOUT);
                    } else {
                        TTY_FWRITE(buf, 1, nc, s->STDOUT);
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
                if (gotopos) {
                    /* Move the cursor: row and col are 1 based
                       but ptr1 has already been incremented */
                    gotopos = 0;
                    TTY_FPRINTF(s->STDOUT, "\033[%d;%dH",
                                y + 1, (int)(ptr1 - ptr));
                }
                /* the current attribute is already set correctly */
                TTY_FPUTS("\033[K", s->STDOUT);
                while (ptr1 < ptr2) {
                    ptr1[shadow] = cc;
                    ptr1++;
                }
            }
            // XXX: should check if needed
            //if (ts->term_flags & USE_BLINK_AS_BRIGHT_BG)
            {
                if (bgcolor > 7) {
                    TTY_FPUTS("\033[0m", s->STDOUT);
                    fgcolor = bgcolor = -1;
                    attr = 0;
                }
            }
        }
    }

    // XXX: should check if needed
    TTY_FPUTS("\033[0m", s->STDOUT);
    if (ts->cursor_y + 1 >= 0 && ts->cursor_x + 1 >= 0) {
        TTY_FPRINTF(s->STDOUT, "\033[?25h\033[%d;%dH",
                    ts->cursor_y + 1, ts->cursor_x + 1);
    }
    fflush(s->STDOUT);

    /* Update combination cache from screen.
     * Shadow is identical to screen so no need to scan it.
     * Should do this before redisplay?
     */
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
        x1 = clamp_int(x1, 0, pp->width);
        y1 = clamp_int(y1, 0, pp->height);
        pict->width = clamp_int(w1, 0, pp->width - x1);
        pict->height = clamp_int(h1, 0, pp->height - y1);
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
    &&  !blockcmp(ip->palette, xterm_colors, 256)) {
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
    TTYState *ts = s->priv_data;
    int w = 16;

    eb_printf(b, "Device Description\n\n");

    if (ts->term_name)
        eb_printf(b, "%*s: %s\n", w, "term_name", ts->term_name);
    eb_printf(b, "%*s: %d  %s\n", w, "term_code", ts->term_code,
              term_code_name[ts->term_code]);
    eb_printf(b, "%*s: %d\n", w, "tty_mk", tty_mk);
    eb_printf(b, "%*s: %d\n", w, "tty_mouse", tty_mouse);
    eb_printf(b, "%*s: %d\n", w, "tty_clipboard", tty_clipboard);
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

    comb_cache_describe(s, b, ts);
}

static void tty_dpy_sound_bell(QEditScreen *s)
{
    fputc('\007', s->STDOUT);
    fflush(s->STDOUT);
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
    tty_dpy_sound_bell,
    tty_dpy_suspend,
    qe_dpy_error,
    NULL, /* next */
};

static int tty_init(QEmacsState *qs)
{
    return qe_register_display(qs, &tty_dpy);
}

qe_module_init(tty_init);
