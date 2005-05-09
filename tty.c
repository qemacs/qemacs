/*
 * TTY handling for QEmacs
 * Copyright (c) 2000,2001 Fabrice Bellard.
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
#include <ctype.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include "qe.h"

typedef struct TTYChar {
    unsigned short ch;
    unsigned char bgcolor;
    unsigned char fgcolor;
} TTYChar;

enum InputState {
    IS_NORM,
    IS_ESC,
    IS_CSI,
    IS_CSI2,
    IS_ESC2,
};

typedef struct TTYState {
    TTYChar *old_screen;
    TTYChar *screen;
    unsigned char *line_updated;
    struct termios oldtty;
    int cursor_x, cursor_y;
    /* input handling */
    enum InputState input_state;
    int input_param;
    int utf8_state;
    int utf8_index;
    unsigned char buf[10];
} TTYState;

static void tty_resize(int sig);
static void term_exit(void);
static void tty_read_handler(void *opaque);

static struct TTYState tty_state;
static QEditScreen *tty_screen;

static int term_probe(void)
{
    return 1;
}

extern QEDisplay tty_dpy;

static int term_init(QEditScreen *s, int w, int h)
{
    TTYState *ts;
    struct termios tty;
    struct sigaction sig;
    char *term;

    memcpy(&s->dpy, &tty_dpy, sizeof(QEDisplay));

    tty_screen = s;
    ts = &tty_state;
    s->private = ts;
    s->media = CSS_MEDIA_TTY;

    tcgetattr (0, &tty);
    ts->oldtty = tty;

    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
                          |INLCR|IGNCR|ICRNL|IXON);
    tty.c_oflag |= OPOST;
    tty.c_lflag &= ~(ECHO|ECHONL|ICANON|IEXTEN|ISIG);
    tty.c_cflag &= ~(CSIZE|PARENB);
    tty.c_cflag |= CS8;
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;
    
    tcsetattr(0, TCSANOW, &tty);

    s->charset = &charset_8859_1;

#ifndef CONFIG_CYGWIN
    /* CG: Should also have a command line switch */
    /* test UTF8 support by looking at the cursor position (idea from
       Ricardas Cepas <rch@pub.osf.lt>). Since uClibc actually tests
       to ensure that the format string is a valid multibyte sequence
       in the current locale (ANSI/ISO C99), use a format specifier of
       %s to avoid printf() failing with EILSEQ. */
    {
        int y, x;
        
        /*               ^X  ^Z    ^M   \170101  */
        //printf("%s", "\030\032" "\r\xEF\x81\x81" "\033[6n\033D");
        /* Just print utf-8 encoding for eacute and check cursor position */
        printf("%s", "\030\032" "\r\xC3\xA9" "\033[6n\033D");
        scanf("\033[%u;%u", &y, &x);/* get cursor position */
        printf("\033[1F" "\033[%uX", (x-1)); /* go back; erase 1 or 3 char */
        if (x == 2) {
            s->charset = &charset_utf8;
        }
    }
#endif
    printf("\033)0\033(B\017"); /* select character sets in block 0 and 1 */
    
    atexit(term_exit);

    sig.sa_handler = tty_resize;
    sigemptyset(&sig.sa_mask);
    sig.sa_flags = 0;
    sigaction(SIGWINCH, &sig, NULL);
    fcntl(0, F_SETFL, O_NONBLOCK);
    /* If stdout is to a pty, make sure we aren't in nonblocking mode.
     * Otherwise, the printf()s in term_flush() can fail with EAGAIN,
     * causing repaint errors when running in an xterm or in a screen
     * session. */
    fcntl(1, F_SETFL, 0);

    set_read_handler(0, tty_read_handler, s);

    tty_resize(0);

    /* Test TERM env var:
     * linux and xterm -> kbs=\177
     * ansi cygwin vt100 -> kbs=^H
     */
    term = getenv("TERM");
    if (term) {
        if (strstart(term, "ansi", NULL) ||
            strstart(term, "cygwin", NULL) ||
            strstart(term, "vt", NULL)) {
            do_toggle_control_h(NULL, 1);
        }
    }
    return 0;
}

static void term_close(QEditScreen *s)
{
    fcntl(0, F_SETFL, 0);
    /* go to the last line */
    printf("\033[%d;%dH\033[m\033[K", s->height, 1);
    fflush(stdout);
}

static void term_exit(void)
{
    QEditScreen *s = tty_screen;
    TTYState *ts = s->private;

    tcsetattr (0, TCSANOW, &ts->oldtty);
}

static void tty_resize(int sig)
{
    QEditScreen *s = tty_screen;
    TTYState *ts = s->private;
    struct winsize ws;
    int size;

    s->width = 80;
    s->height = 24;
    if (ioctl(0, TIOCGWINSZ, &ws) == 0) {
        s->width = ws.ws_col;
        s->height = ws.ws_row;
    }
    
    size = s->width * s->height * sizeof(TTYChar);
    ts->old_screen = realloc(ts->old_screen, size);
    ts->screen = realloc(ts->screen, size);
    ts->line_updated = realloc(ts->line_updated, s->height);
    
    memset(ts->old_screen, 0, size);
    memset(ts->screen, ' ', size);
    memset(ts->line_updated, 1, s->height);

    s->clip_x1 = 0;
    s->clip_y1 = 0;
    s->clip_x2 = s->width;
    s->clip_y2 = s->height;
}

static void term_invalidate(void)
{
    tty_resize(0);
}

static void term_cursor_at(QEditScreen *s, int x1, int y1, int w, int h)
{
    TTYState *ts = s->private;
    ts->cursor_x = x1;
    ts->cursor_y = y1;
}

static int term_is_user_input_pending(QEditScreen *s)
{
    fd_set rfds;
    struct timeval tv;
    
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&rfds);
    FD_SET(0, &rfds);
    if (select(1, &rfds, NULL, NULL, &tv) > 0)
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
    TTYState *ts = s->private;
    int ch;
    QEEvent ev1, *ev = &ev1;

    if (read(0, ts->buf + ts->utf8_index, 1) != 1)
        return;

    if (trace_buffer) {
        eb_write(trace_buffer, trace_buffer->total_size,
                 ts->buf + ts->utf8_index, 1);
#if 0
        ch = ts->buf[ts->utf8_index];
        if (ch < 32 || ch == 127)
            fprintf(stderr, "got %d '^%c'\n", ch, ('@' + ch) & 127);
        else
            fprintf(stderr, "got %d '%c'\n", ch, ch);
#endif
    }

    /* charset handling */
    if (s->charset == &charset_utf8) {
        if (ts->utf8_state == 0) {
            const char *p;
            p = ts->buf;
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
        if (isdigit(ch)) {
            ts->input_param = ts->input_param * 10 + ch - '0';
            break;
        }
        ts->input_state = IS_NORM;
        switch(ch) {
        case '[':
            ts->input_state = IS_CSI2;
            break;
        case '~':
            if (ts->input_param < sizeof(csi_lookup)/sizeof(csi_lookup[0])) {
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
        switch(ch) {
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
        switch(ch) {
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

#define NB_COLORS 8

unsigned int tty_colors[NB_COLORS] = {
    QERGB(0x00, 0x00, 0x00),
    QERGB(0xff, 0x00, 0x00),
    QERGB(0x00, 0xff, 0x00),
    QERGB(0xff, 0xff, 0x00),
    QERGB(0x00, 0x00, 0xff),
    QERGB(0xff, 0x00, 0xff),
    QERGB(0x00, 0xff, 0xff),
    QERGB(0xff, 0xff, 0xff),
};

static int get_tty_color(QEColor color)
{
    int i, cmin, dmin, d;
    
    dmin = MAXINT;
    cmin = 0;
    for(i=0;i<NB_COLORS;i++) {
        d = color_dist(color, tty_colors[i]);
        if (d < dmin) {
            cmin = i;
            dmin = d;
        }
    }
    return cmin;
}

static void term_fill_rectangle(QEditScreen *s,
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
        for(y=y1;y<y2;y++) {
            ts->line_updated[y] = 1;
            for(x=x1;x<x2;x++) {
                ptr->bgcolor ^= 7;
                ptr->fgcolor ^= 7;
                ptr++;
            }
            ptr += wrap;
        }
    } else {
        bgcolor = get_tty_color(color);
        for(y=y1;y<y2;y++) {
            ts->line_updated[y] = 1;
            for(x=x1;x<x2;x++) {
                ptr->bgcolor = bgcolor;
                ptr->ch = ' ';
                ptr->fgcolor = 7;
                ptr++;
            }
            ptr += wrap;
        }
    }
}

/* XXX: could alloc font in wrapper */
static QEFont *term_open_font(QEditScreen *s,
                              int style, int size)
{
    QEFont *font;
    font = malloc(sizeof(QEFont));
    if (!font)
        return NULL;
    font->ascent = 0;
    font->descent = 1;
    font->private = NULL;
    return font;
}

static void term_close_font(QEditScreen *s, QEFont *font)
{
    free(font);
}

/*
 * Modified implementation of wcwidth() from Markus Kuhn. We do not
 * handle non spacing and enclosing combining characters and control
 * chars.  
 */

static int term_glyph_width(QEditScreen *s, unsigned int ucs)
{
  /* fast test for majority of non-wide scripts */
  if (ucs < 0x900)
    return 1;

  return 1 +
    ((ucs >= 0x1100 && ucs <= 0x115f) || /* Hangul Jamo */
     (ucs >= 0x2e80 && ucs <= 0xa4cf && (ucs & ~0x0011) != 0x300a &&
      ucs != 0x303f) ||                  /* CJK ... Yi */
     (ucs >= 0xac00 && ucs <= 0xd7a3) || /* Hangul Syllables */
     (ucs >= 0xf900 && ucs <= 0xfaff) || /* CJK Compatibility Ideographs */
     (ucs >= 0xfe30 && ucs <= 0xfe6f) || /* CJK Compatibility Forms */
     (ucs >= 0xff00 && ucs <= 0xff5f) || /* Fullwidth Forms */
     (ucs >= 0xffe0 && ucs <= 0xffe6)
     );
}

static void term_text_metrics(QEditScreen *s, QEFont *font, 
                              QECharMetrics *metrics,
                              const unsigned int *str, int len)
{
    int i, x;
    metrics->font_ascent = font->ascent;
    metrics->font_descent = font->descent;
    x = 0;
    for(i=0;i<len;i++)
        x += term_glyph_width(s, str[i]);
    metrics->width = x;
}
        
static void term_draw_text(QEditScreen *s, QEFont *font, 
                           int x, int y, const unsigned int *str, int len,
                           QEColor color)
{
    TTYState *ts = s->private;
    TTYChar *ptr;
    int fgcolor, w, n;
    unsigned int cc;
    
    if (y < s->clip_y1 ||
        y >= s->clip_y2 || 
        x >= s->clip_x2)
        return;
    
    ts->line_updated[y] = 1;
    fgcolor = get_tty_color(color);
    ptr = ts->screen + y * s->width;

    if (x < s->clip_x1) {
        ptr += s->clip_x1;
        /* left clip */
        while (len > 0) {
            cc = *str++;
            len--;
            w = term_glyph_width(s, cc);
            x += w;
            if (x >= s->clip_x1) {
                /* now we are on the screen. need to put spaces for
                   wide chars */
                n = x;
                if (s->clip_x2 < n)
                    n = s->clip_x2;
                n -= s->clip_x1;
                for(; n > 0; n--) {
                    ptr->fgcolor = fgcolor;
                    ptr->ch = ' ';
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
        w = term_glyph_width(s, cc);
        /* XXX: would need to put spacs for wide chars */
        if (x + w > s->clip_x2) 
            break;
        ptr->fgcolor = fgcolor;
        ptr->ch = cc;
        ptr++;
        n = w - 1;
        while (n > 0) {
            ptr->fgcolor = fgcolor;
            ptr->ch = 0xffff;
            ptr++;
            n--;
        }
        x += w;
    }
}

static void term_set_clip(QEditScreen *s,
                          int x, int y, int w, int h)
{
}

static void term_flush(QEditScreen *s)
{
    TTYState *ts = s->private;
    TTYChar *ptr, *optr;
    int x, y, bgcolor, fgcolor;
    char buf[10];
    unsigned int cc;

    bgcolor = -1;
    fgcolor = -1;
            
    for (y = 0; y < s->height; y++) {
        if (ts->line_updated[y]) {
            ts->line_updated[y] = 0;
            ptr = ts->screen + y * s->width;
            optr = ts->old_screen + y * s->width;

            if (memcmp(ptr, optr, sizeof(TTYChar) * s->width) != 0) {
                /* XXX: currently, we update the whole line */
                printf("\033[%d;%dH", y + 1, 1);
                for (x = 0; x < s->width; x++) {
                    cc = ptr->ch;
                    if (cc != 0xffff) {
                        /* output attributes */
                        if (((fgcolor != ptr->fgcolor && cc != ' ') ||
                             (bgcolor != ptr->bgcolor))) {
                            fgcolor = ptr->fgcolor;
                            bgcolor = ptr->bgcolor;
                            printf("\033[;%d;%dm", 
                                   30 + fgcolor, 40 + bgcolor);
                        }
                        /* do not display escape codes or invalid codes */
                        if (cc < 32) {
                            buf[0] = '.';
                            buf[1] = '\0';
                        } else
                        if (cc >= 128 && cc < 128 + 32) {
                            /* Kludge for linedrawing chars */
                            buf[0] = '\016';
                            buf[1] = cc - 32;
                            buf[2] = '\017';
                            buf[3] = '\0';
                        } else {
                            unicode_to_charset(buf, cc, s->charset);
                        }
                        if (x != s->width - 1 || y != s->height - 1)
                            printf("%s", buf);
                    }
                    /* update old screen data */
                    *optr++ = *ptr++;
                }
            }
        }
    }

    printf("\033[%d;%dH", ts->cursor_y + 1, ts->cursor_x + 1);
    fflush(stdout);
}


static QEDisplay tty_dpy = {
    "vt100",
    term_probe,
    term_init,
    term_close,
    term_cursor_at,
    term_flush,
    term_is_user_input_pending,
    term_fill_rectangle,
    term_open_font,
    term_close_font,
    term_text_metrics,
    term_draw_text,
    term_set_clip,
    NULL, /* no selection handling */
    NULL, /* no selection handling */
    term_invalidate,
};

static int tty_init(void)
{
    return qe_register_display(&tty_dpy);
}

qe_module_init(tty_init);
