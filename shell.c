/*
 * Shell mode for QEmacs.
 *
 * Copyright (c) 2001-2002 Fabrice Bellard.
 * Copyright (c) 2002-2014 Charlie Gordon.
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

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <termios.h>
#include "qe.h"

/* XXX: status line */
/* XXX: better tab handling */
/* XXX: bold & italic ? */
/* XXX: send real cursor position (CSI n) */

static ModeDef shell_mode, pager_mode;

#define MAX_ESC_PARAMS 3

enum TTYState {
    TTY_STATE_NORM,
    TTY_STATE_UTF8,
    TTY_STATE_ESC,
    TTY_STATE_ESC2,
    TTY_STATE_CSI,
    TTY_STATE_STRING,
};

static int shell_signature;

typedef struct ShellState {
    void *signature;
    /* buffer state */
    int pty_fd;
    int pid; /* -1 if not launched */
    int color, attr, def_color;
    int cur_offset; /* current offset at position x, y */
    int esc_params[MAX_ESC_PARAMS];
    int has_params[MAX_ESC_PARAMS];
    int nb_esc_params;
    int state;
    int esc1, esc2;
    int shifted;
    int cset, charset[2];
    int grab_keys;
    unsigned char utf8_buf[8];
    int utf8_len, utf8_pos;
    EditBuffer *b;
    EditBuffer *b_color; /* color buffer, one byte per char */
    struct QEmacsState *qe_state;
    const char *ka1, *ka3, *kb2, *kc1, *kc3, *kcbt, *kspd;
    const char *kbeg, *kbs, *kent, *kdch1, *kich1;
    const char *kcub1, *kcud1, *kcuf1, *kcuu1;
    const char *kf1, *kf2, *kf3, *kf4, *kf5;
    const char *kf6, *kf7, *kf8, *kf9, *kf10;
    const char *kf11, *kf12, *kf13, *kf14, *kf15;
    const char *kf16, *kf17, *kf18, *kf19, *kf20;
    const char *khome, *kend, *kmous, *knp, *kpp;
    const char *caption;  /* process caption for exit message */
    int shell_flags;

} ShellState;

/* CG: these variables should be encapsulated in a global structure */
static char error_buffer[MAX_BUFFERNAME_SIZE];
static int error_offset = -1;
static int error_line_num = -1;
static char error_filename[MAX_FILENAME_SIZE];

static void set_error_offset(EditBuffer *b, int offset)
{
    pstrcpy(error_buffer, sizeof(error_buffer), b ? b->name : "");
    error_offset = offset - 1;
    error_line_num = -1;
    *error_filename = '\0';
}

#define PTYCHAR1 "pqrstuvwxyzabcde"
#define PTYCHAR2 "0123456789abcdef"

/* allocate one pty/tty pair */
static int get_pty(char *tty_str, int size)
{
    int fd;
    char ptydev[] = "/dev/pty??";
    char ttydev[] = "/dev/tty??";
    int len = strlen(ttydev);
    const char *c1, *c2;

#ifdef CONFIG_PTSNAME
    /* First try Unix98 pseudo tty master */

    /* CG: should check if posix_openpt is more appropriate than /dev/ptmx */
    //fd = posix_openpt(O_RDWR | O_NOCTTY);

    fd = open("/dev/ptmx", O_RDWR);
    if (fd >= 0) {
#if 0
        /* ptsname_r is a sensible renentrant version of ptsname, but
         * it lacks portability, notably on OpenBSD and cygwin. So we
         * have to use ill conceived ptsname.
         */
        if (!ptsname_r(fd, tty_str, size) && !grantpt(fd) && !unlockpt(fd))
            return fd;
#else
        const char *name = ptsname(fd);

        if (name) {
            pstrcpy(tty_str, size, name);
            if (!grantpt(fd) && !unlockpt(fd))
                return fd;
        }
#endif
        close(fd);
    }
#endif
    /* then try BSD pseudo tty pre-created pairs */
    for (c1 = PTYCHAR1; *c1; c1++) {
        ptydev[len-2] = ttydev[len-2] = *c1;
        for (c2 = PTYCHAR2; *c2; c2++) {
            ptydev[len-1] = ttydev[len-1] = *c2;
            if ((fd = open(ptydev, O_RDWR)) >= 0) {
                if (access(ttydev, R_OK|W_OK) == 0) {
                    pstrcpy(tty_str, size, ttydev);
                    return fd;
                }
                close(fd);
            }
        }
    }
    return -1;
}

const char *get_shell(void)
{
    const char *shell_path;

    /* find shell name */
    shell_path = getenv("SHELL");
    if (!shell_path)
        shell_path = "/bin/sh";

    return shell_path;
}

#define TTY_XSIZE  80
#define TTY_YSIZE  25
#define TTY_YSIZE_INFINITE  10000

static int run_process(const char *cmd, int *fd_ptr, int *pid_ptr,
                       int cols, int rows, int shell_flags)
{
    int pty_fd, pid, i, nb_fds;
    char tty_name[1024];
    struct winsize ws;

    pty_fd = get_pty(tty_name, sizeof(tty_name));
    if (pty_fd < 0) {
        put_status(NULL, "run_process: cannot get tty: %s",
                   strerror(errno));
        return -1;
    }
    fcntl(pty_fd, F_SETFL, O_NONBLOCK);

    /* set dummy screen size */
    ws.ws_col = cols;
    ws.ws_row = rows;
    ws.ws_xpixel = ws.ws_col;
    ws.ws_ypixel = ws.ws_row;
    ioctl(pty_fd, TIOCSWINSZ, &ws);

    pid = fork();
    if (pid < 0) {
        put_status(NULL, "run_process: cannot fork");
        return -1;
    }
    if (pid == 0) {
        /* child process */
        const char *argv[4];
        int argc = 0;

        argv[argc++] = get_shell();
        if (cmd) {
            argv[argc++] = "-c";
            argv[argc++] = cmd;
        }
        argv[argc] = NULL;

        /* detach controlling terminal */
#ifndef CONFIG_DARWIN
        setsid();
#endif
        /* close all files */
        nb_fds = getdtablesize();
        for (i = 0; i < nb_fds; i++)
            close(i);

        if (shell_flags & SF_INFINITE)
            setenv("LINES", "10000", 1);

        /* open pseudo tty for standard I/O */
        if (shell_flags & SF_INTERACTIVE) {
            /* interactive shell: input from / output to pseudo terminal */
            open(tty_name, O_RDWR);
            dup(0);
            dup(0);
        } else {
            /* collect output from non interactive process: no input */
            open("/dev/null", O_RDONLY);
            open(tty_name, O_RDWR);
            dup(1);
        }
#ifdef CONFIG_DARWIN
        setsid();
#endif
        setenv("TERM", "xterm", 1);
        unsetenv("PAGER");
        //setenv("QELEVEL", "1", 1);

        execv(argv[0], (char * const*)argv);
        exit(1);
    }
    /* return file info */
    *fd_ptr = pty_fd;
    *pid_ptr = pid;
    return 0;
}

/* VT100 emulation */

static void tty_init(ShellState *s)
{
    char *term;

    s->state = TTY_STATE_NORM;
    /* Should compute def_color from shell default style at display
     * time and force full redisplay upon style change.
     */
    s->color = s->def_color = TTY_MAKE_COLOR(TTY_DEFFG, TTY_DEFBG);
    s->attr = 0;

    term = getenv("TERM");
    /* vt100 terminfo definitions */
    s->kbs = "\010";
    s->ka1 = "\033Oq";
    s->ka3 = "\033Os";
    s->kb2 = "\033Or";
    s->kc1 = "\033Op";
    s->kc3 = "\033On";
    s->kcub1 = "\033OD";
    s->kcud1 = "\033OB";
    s->kcuf1 = "\033OC";
    s->kcuu1 = "\033OA";
    s->kent = "\033OM";
    s->kf1 = "\033OP";
    s->kf2 = "\033OQ";
    s->kf3 = "\033OR";
    s->kf4 = "\033OS";
    s->kf5 = "\033Ot";
    s->kf6 = "\033Ou";
    s->kf7 = "\033Ov";
    s->kf8 = "\033Ol";
    s->kf9 = "\033Ow";
    s->kf10 = "\033Ox";

    /* ansi terminfo definitions */
    if (strstart(term, "ansi", NULL)) {
        s->kbs = "\010";
        s->kcbt = "\033[Z";
        s->kcub1 = "\033[D";
        s->kcud1 = "\033[B";
        s->kcuf1 = "\033[C";
        s->kcuu1 = "\033[A";
        s->khome = "\033[H";
        s->kich1 = "\033[L";
    }
    if (strstart(term, "vt220", NULL)) {
        s->kcub1 = "\033[D";
        s->kcud1 = "\033[B";
        s->kcuf1 = "\033[C";
        s->kcuu1 = "\033[A";
        s->kdch1 = "\033[3~";
        s->kend = "\033[4~";
        s->khome = "\033[1~";
        s->kich1 = "\033[2~";
        s->knp = "\033[6~";
        s->kpp = "\033[5~";
        s->kf1 = "\033OP";
        s->kf2 = "\033OQ";
        s->kf3 = "\033OR";
        s->kf4 = "\033OS";
        s->kf5 = "\033[17~";
        s->kf6 = "\033[18~";
        s->kf7 = "\033[19~";
        s->kf8 = "\033[20~";
        s->kf9 = "\033[21~";
        s->kf10 = "\033[29~";
    }
    if (strstart(term, "cygwin", NULL)) {
        s->kbs = "\10";
        goto linux_cygwin;
    }
    if (strstart(term, "linux", NULL)) {
        s->kbs = "\177";
        s->kb2 = "\033[G";
        s->kcbt = "\033[Z";
        s->kspd = "\032";       // ^Z
    linux_cygwin:
        s->kcub1 = "\033[D";
        s->kcud1 = "\033[B";
        s->kcuf1 = "\033[C";
        s->kcuu1 = "\033[A";
        s->kdch1 = "\033[3~";
        s->kend = "\033[4~";
        s->khome = "\033[1~";
        s->kich1 = "\033[2~";
        s->knp = "\033[6~";
        s->kpp = "\033[5~";
        s->kf1 = "\033[[A";
        s->kf2 = "\033[[B";
        s->kf3 = "\033[[C";
        s->kf4 = "\033[[D";
        s->kf5 = "\033[[E";
        s->kf6 = "\033[17~";
        s->kf7 = "\033[18~";
        s->kf8 = "\033[19~";
        s->kf9 = "\033[20~";
        s->kf10 = "\033[21~";
        s->kf11 = "\033[23~";
        s->kf12 = "\033[24~";
        s->kf13 = "\033[25~";
        s->kf14 = "\033[26~";
        s->kf15 = "\033[28~";
        s->kf16 = "\033[29~";
        s->kf17 = "\033[31~";
        s->kf18 = "\033[32~";
        s->kf19 = "\033[33~";
        s->kf20 = "\033[34~";
    }
    if (strstart(term, "xterm", NULL)) {
        s->ka1 = "\033Ow";
        s->ka3 = "\033Ou";
        s->kb2 = "\033Oy";
        s->kbeg = "\033OE";
        s->kbs = "\03377";
        s->kc1 = "\033Oq";
        s->kc3 = "\033Os";
        s->kcub1 = "\033OD";
        s->kcud1 = "\033OB";
        s->kcuf1 = "\033OC";
        s->kcuu1 = "\033OA";
        s->kdch1 = "\033[3~";
        s->kend = "\033[4~";
        s->kent = "\033OM";
        s->khome = "\033[1~";
        s->kich1 = "\033[2~";
        s->kmous = "\033[M";
        s->knp = "\033[6~";
        s->kpp = "\033[5~";
        s->kf1 = "\033OP";
        s->kf2 = "\033OQ";
        s->kf3 = "\033OR";
        s->kf4 = "\033OS";
        s->kf5 = "\033[15~";
        s->kf6 = "\033[17~";
        s->kf7 = "\033[18~";
        s->kf8 = "\033[19~";
        s->kf9 = "\033[20~";
        s->kf10 = "\033[21~";
        s->kf11 = "\033[23~";
        s->kf12 = "\033[24~";
        s->kf13 = "\033[25~";
        s->kf14 = "\033[26~";
        s->kf15 = "\033[28~";
        s->kf16 = "\033[29~";
        s->kf17 = "\033[31~";
        s->kf18 = "\033[32~";
        s->kf19 = "\033[33~";
        s->kf20 = "\033[34~";
    }
}

static void tty_write(ShellState *s, const char *buf, int len)
{
    int ret;

    if (len < 0)
        len = strlen(buf);

    if (s->qe_state->trace_buffer)
        eb_trace_bytes(buf, len, EB_TRACE_PTY);

    while (len > 0) {
        ret = write(s->pty_fd, buf, len);
        if (ret == -1 && (errno == EAGAIN || errno == EINTR))
            continue;
        if (ret <= 0)
            break;
        buf += ret;
        len -= ret;
    }
}

/* Compute offset of the char at column x and row y (0 based).
 * Can insert spaces or rows if needed.
 * x and y may each be relative to the current position.
 */
/* XXX: optimize !!!!! */
static void tty_goto_xy(ShellState *s, int x, int y, int relative)
{
    int total_lines, cur_line, line_num, col_num, offset, offset1, c;

    /* compute offset */
    eb_get_pos(s->b, &total_lines, &col_num, s->b->total_size);
    if (s->cur_offset == s->b->total_size
    ||  eb_prevc(s->b, s->b->total_size, &offset1) != '\n')
        total_lines++;

    line_num = total_lines - TTY_YSIZE;
    if (line_num < 0)
        line_num = 0;

    if (relative) {
        eb_get_pos(s->b, &cur_line, &col_num, s->cur_offset);
        cur_line -= line_num;
        if (cur_line < 0)
            cur_line = 0;
        if (relative & 1)
            x += col_num;
        if (relative & 2)
            y += cur_line;
    }
    if (y < 0 || y >= TTY_YSIZE_INFINITE - 1)
        y = 0;
    else
    if (y >= TTY_YSIZE)
        y = TTY_YSIZE - 1;
    if (x < 0)
        x = 0;

    line_num += y;
    /* add lines if necessary */
    while (line_num >= total_lines) {
        /* XXX: color may be wrong */
        s->b->cur_style = QE_STYLE_TTY | s->color | s->attr;
        eb_insert_uchar(s->b, s->b->total_size, '\n');
        total_lines++;
    }
    offset = eb_goto_pos(s->b, line_num, 0);
    for (; x > 0; x--) {
        c = eb_nextc(s->b, offset, &offset1);
        if (c == '\n') {
            /* duplicate style of last char */
            offset += eb_insert_spaces(s->b, offset, x);
            break;
        } else {
            offset = offset1;
        }
    }
    s->cur_offset = offset;
}

/* CG: XXX: tty_put_char purposely ignores charset when inserting chars */
static int tty_put_char(ShellState *s, int c)
{
    char buf[1];
    int c1, cur_len, offset, offset1;

    offset = s->cur_offset;
    buf[0] = c;
    c1 = eb_nextc(s->b, offset, &offset1);
    s->b->cur_style = QE_STYLE_TTY | s->color | s->attr;
    if (c1 == '\n') {
        /* insert */
        eb_insert(s->b, offset, buf, 1);
    } else {
        /* check for (c1 != c) is not advisable optimisation because
         * re-writing the same character may cause color changes.
         */
        cur_len = offset1 - offset;
        if (cur_len == 1) {
            eb_write(s->b, offset, buf, 1);
        } else {
            eb_delete(s->b, offset, cur_len);
            eb_insert(s->b, offset, buf, 1);
        }
    }
    return s->cur_offset = offset + 1;
}

static void tty_csi_m(ShellState *s, int c, int has_param)
{
    /* Comment from putty/terminal.c:
     *
     * A VT100 without the AVO only had one
     * attribute, either underline or
     * reverse video depending on the
     * cursor type, this was selected by
     * CSI 7m.
     *
     * case 2:
     *  This is sometimes DIM, eg on the
     *  GIGI and Linux
     * case 8:
     *  This is sometimes INVIS various ANSI.
     * case 21:
     *  This like 22 disables BOLD, DIM and INVIS
     *
     * The ANSI colours appear on any
     * terminal that has colour (obviously)
     * but the interaction between sgr0 and
     * the colours varies but is usually
     * related to the background colour
     * erase item. The interaction between
     * colour attributes and the mono ones
     * is also very implementation
     * dependent.
     *
     * The 39 and 49 attributes are likely
     * to be unimplemented.
     */

    switch (has_param ? c : 0) {
    case 0:     /* exit_attribute_mode */
        s->color = s->def_color;
        s->attr = 0;
        break;
    case 1:     /* enter_bold_mode */
        s->attr |= TTY_BOLD;
        break;
    case 22:    /* exit_bold_mode */
        s->attr &= ~TTY_BOLD;
        break;
    case 4:     /* enter_underline_mode */
        s->attr |= TTY_UNDERLINE;
        break;
    case 24:    /* exit_underline_mode */
        s->attr &= ~TTY_UNDERLINE;
        break;
    case 5:     /* enter_blink_mode */
        s->attr |= TTY_BLINK;
        break;
    case 25:    /* exit_blink_mode */
        s->attr &= ~TTY_BLINK;
        break;
    case 7:     /* enter_reverse_mode, enter_standout_mode */
    case 27:    /* exit_reverse_mode, exit_standout_mode */
        /* TODO */
        break;
    case 6:     /* SCO light background */
    case 8:     /* enter_secure_mode */
    case 9:     /* cygwin dim mode */
    case 10:    /* SCO acs off */
    case 11:    /* SCO acs on (CP437) */
    case 12:    /* SCO acs on, |0x80 */
    case 28:    /* exit_secure_mode */
        break;
    case 39:    /* orig_pair(1) default-foreground */
        TTY_SET_FG_COLOR(s->color, TTY_DEFFG);
        break;
    case 49:    /* orig_pair(2) default-background */
        TTY_SET_BG_COLOR(s->color, TTY_DEFBG);
        break;
    case 38:    /* set extended foreground color */
        /* complete syntax is \033[38;5;Nm where N is in range 1..255 */
        if (s->esc_params[1] == 5) {
            /* set foreground color to third esc_param */
            int color = s->esc_params[2];

            /* simulate 256 colors */
            color = get_tty_color(tty_fg_colors[color & 255],
                                  tty_fg_colors, 16);

            TTY_SET_FG_COLOR(s->color, color);
            s->nb_esc_params = 1;
        }
        break;
    case 48:    /* set extended background color */
        /* complete syntax is \033[48;5;Nm where N is in range 1..255 */
        if (s->esc_params[1] == 5) {
            /* set background color to third esc_param */
            int color = s->esc_params[2];

            /* simulate 256 colors */
            color = get_tty_color(tty_fg_colors[color & 255],
                                  tty_fg_colors, 16);
            TTY_SET_BG_COLOR(s->color, color);
            s->nb_esc_params = 1;
        }
        break;
    default:
        /* 0:black 1:red 2:green 3:yellow 4:blue 5:magenta 6:cyan 7:white */
        if (c >= 30 && c <= 37) {
            /* set foreground color */
            TTY_SET_FG_COLOR(s->color, c - 30);
        } else
        if (c >= 40 && c <= 47) {
            /* set background color */
            TTY_SET_BG_COLOR(s->color, c - 40);
        } else
        if (c >= 90 && c <= 97) {
            /* set bright foreground color */
            TTY_SET_FG_COLOR(s->color, c - 90 + 8);
        } else
        if (c >= 100 && c <= 107) {
            /* set bright background color */
            TTY_SET_BG_COLOR(s->color, c - 100 + 8);
        }
        break;
    }
}


/* Well, almost a hack to update cursor */
static void tty_update_cursor(__unused__ ShellState *s)
{
#if 0
    QEmacsState *qs = s->qe_state;
    EditState *e;

    if (s->cur_offset == -1)
        return;

    for (e = qs->first_window; e != NULL; e = e->next_window) {
        if (s->b == e->b && e->interactive) {
            e->offset = s->cur_offset;
        }
    }
#endif
}

static ShellState *shell_get_state(EditState *e, int status)
{
    ShellState *s = e->b->priv_data;

    if (s && s->signature == &shell_signature)
        return s;

    if (status)
        put_status(e, "Not a shell buffer");

    return NULL;
}

/* CG: much cleaner way! */
/* would need a kill hook as well ? */
static void shell_display_hook(EditState *e)
{
    ShellState *s;

    if (e->interactive) {
        if ((s = shell_get_state(e, 0)) != NULL)
            e->offset = s->cur_offset;
    }
}

static void shell_key(void *opaque, int key)
{
    ShellState *s = opaque;
    char buf[10];
    const char *p;
    int len;

    if (!s || s->signature != &shell_signature)
        return;

    if (key == KEY_CTRL('o')) {
        qe_ungrab_keys();
        unget_key(key);
        return;
    }
    p = buf;
    len = -1;
    switch (key) {
    case KEY_UP:        p = s->kcuu1; break;
    case KEY_DOWN:      p = s->kcud1; break;
    case KEY_RIGHT:     p = s->kcuf1; break;
    case KEY_LEFT:      p = s->kcub1; break;
    //case KEY_CTRL_UP:
    //case KEY_CTRL_DOWN:
    //case KEY_CTRL_RIGHT:
    //case KEY_CTRL_LEFT:
    //case KEY_CTRL_END:
    //case KEY_CTRL_HOME:
    //case KEY_CTRL_PAGEUP:
    //case KEY_CTRL_PAGEDOWN:
    case KEY_SHIFT_TAB: p = s->kcbt;  break;
    case KEY_HOME:      p = s->khome; break;
    case KEY_INSERT:    p = s->kich1; break;
    case KEY_DELETE:    p = s->kdch1; break;
    case KEY_END:       p = s->kend;  break;
    case KEY_PAGEUP:    p = s->kpp;   break;
    case KEY_PAGEDOWN:  p = s->knp;   break;
    case KEY_F1:        p = s->kf1;   break;
    case KEY_F2:        p = s->kf2;   break;
    case KEY_F3:        p = s->kf3;   break;
    case KEY_F4:        p = s->kf4;   break;
    case KEY_F5:        p = s->kf5;   break;
    case KEY_F6:        p = s->kf6;   break;
    case KEY_F7:        p = s->kf7;   break;
    case KEY_F8:        p = s->kf8;   break;
    case KEY_F9:        p = s->kf9;   break;
    case KEY_F10:       p = s->kf10;  break;
    case KEY_F11:       p = s->kf11;  break;
    case KEY_F12:       p = s->kf12;  break;
    case KEY_F13:       p = s->kf13;  break;
    case KEY_F14:       p = s->kf14;  break;
    case KEY_F15:       p = s->kf15;  break;
    case KEY_F16:       p = s->kf16;  break;
    case KEY_F17:       p = s->kf17;  break;
    case KEY_F18:       p = s->kf18;  break;
    case KEY_F19:       p = s->kf19;  break;
    case KEY_F20:       p = s->kf20;  break;
    default:
        if (key < 256) {
            buf[0] = key;
            len = 1;
        } else
        if (key >= KEY_META(0) && key <= KEY_META(255)) {
            buf[0] = '\033';
            buf[1] = key;
            len = 2;
        } else {
            p = NULL;
        }
        break;
    }
    if (p) {
        tty_write(s, p, len);
    }
}

static unsigned char const sco_color[16] = {
    0, 4, 2, 6, 1, 5, 3, 7, 8, 12, 10, 14, 9, 13, 11, 15,
};

static void tty_emulate(ShellState *s, int c)
{
    int i, offset, offset1, offset2, n;
    char buf1[10];

    offset = s->cur_offset;

#define ESC2(c1,c2)  (((c1)<<8)|((unsigned char)c2))
    /* some bytes are state independent */
    switch (c) {
    case 0x18:
    case 0x1A:
        s->state = TTY_STATE_NORM;
        return;
    case 0x1B:
        s->state = TTY_STATE_ESC;
        return;
#if 0
    case 0x9B:
        goto csi_entry;
#endif
    }

    switch (s->state) {
    case TTY_STATE_NORM:
        switch (c) {
            /* BEL            Bell (Ctrl-G) */
            /* FF             Form Feed or New Page (NP) (Ctrl-L) same as LF */
            /* VT             Vertical Tab (Ctrl-K) same as LF */

        case 8:         /* ^H  BS = backspace */
            {
                int c1;
                c1 = eb_prevc(s->b, offset, &offset1);
                if (c1 != '\n') {
                    s->cur_offset = offset1;
                    /* back_color_erase */
                    //tty_put_char(s, ' ');
                }
            }
            break;
        case 9:        /* ^I  HT = horizontal tab */
            {
                int col_num, cur_line;
                eb_get_pos(s->b, &cur_line, &col_num, offset);
                tty_goto_xy(s, (col_num + 8) & ~7, 0, 2);
                break;
            }
        case 10:        /* ^J  NL = line feed */
            /* go to next line */
            /* CG: should check if column should be kept */
            for (;;) {
                if (offset == s->b->total_size) {
                    /* add a new line */
                    /* CG: XXX: ignoring charset */
                    s->b->cur_style = QE_STYLE_TTY | s->color | s->attr;
                    buf1[0] = '\n';
                    eb_insert(s->b, offset, buf1, 1);
                    offset = s->b->total_size;
                    break;
                }
                c = eb_nextc(s->b, offset, &offset);
                if (c == '\n')
                    break;
            }
            s->b->last_log = 0; /* close undo record */
            s->cur_offset = offset;
            break;
        case 13:        /* ^M  CR = carriage return */
            /* move to bol */
            s->cur_offset = eb_goto_bol(s->b, offset);
            break;
        case 14:        /* ^N  SO = shift out */
            s->shifted = s->charset[s->cset = 1];
            break;
        case 15:        /* ^O  SI = shift in */
            s->shifted = s->charset[s->cset = 0];
            break;
        default:
            if (c >= 32) {
                int c1, cur_len, len;
                /* CG: assuming ISO-8859-1 characters */
                /* CG: horrible kludge for alternate charset support */
                if (s->shifted && c >= 96 && c < 128) {
#if 0
                    /* Should actually use these tables: */
                    static const wchar_t unitab_xterm_poorman[32] =
                        "*#****o~**+++++-----++++|****L. ";
#endif
                    if (s->b->charset == &charset_utf8) {
                        static const wchar_t unitab_xterm_std[32] = {
                            0x2666, 0x2592, 0x2409, 0x240c,
                            0x240d, 0x240a, 0x00b0, 0x00b1,
                            0x2424, 0x240b, 0x2518, 0x2510,
                            0x250c, 0x2514, 0x253c, 0x23ba,
                            0x23bb, 0x2500, 0x23bc, 0x23bd,
                            0x251c, 0x2524, 0x2534, 0x252c,
                            0x2502, 0x2264, 0x2265, 0x03c0,
                            0x2260, 0x00a3, 0x00b7, 0x0020
                        };
                        c = unitab_xterm_std[c - 96];
                        len = utf8_encode(buf1, c);
                    } else {
                        /* CG: quick 8 bit hack: store line drawing
                         * characters in [96..127] as meta control
                         * characters in [128..159].
                         * This hack is reversed in tty_term_flush().
                         */
                        c += 32;
                        buf1[0] = c;
                        len = 1;
                    }
                } else {
                    /* write char (should factorize with do_char() code */
                    /* CG: Charset support is inherently broken here because
                     * bytes are inserted one at a time and charset conversion
                     * should not be performed between shell output and buffer
                     * contents. UTF8 is special cased, other charsets need work. 
                     */
                    /* CG: further improvement direction includes automatic
                     * conversion from ISO-8859-1 to UTF-8 for invalid UTF-8
                     * byte sequences.
                     */
                    if (s->b->charset == &charset_utf8) {
                        s->utf8_len = utf8_length[c];
                        if (s->utf8_len > 1) {
                            s->utf8_buf[0] = c;
                            s->utf8_pos = 1;
                            s->state = TTY_STATE_UTF8;
                            break;
                        }
                    }
                    //len = eb_encode_uchar(s->b, buf1, c);
                    buf1[0] = c;
                    len = 1;
                }
                c1 = eb_nextc(s->b, offset, &offset1);
                s->b->cur_style = QE_STYLE_TTY | s->color | s->attr;
                /* Should simplify with tty_put_char */
                if (c1 == '\n') {
                    /* insert */
                    eb_insert(s->b, offset, buf1, len);
                } else {
                    cur_len = offset1 - offset;
                    if (cur_len == len) {
                        eb_write(s->b, offset, buf1, len);
                    } else {
                        eb_delete(s->b, offset, cur_len);
                        eb_insert(s->b, offset, buf1, len);
                    }
                }
                s->cur_offset = offset + len;
            }
            break;
        }
        break;
    case TTY_STATE_UTF8:
        s->utf8_buf[s->utf8_pos++] = c;
        if (s->utf8_pos >= s->utf8_len) {
            int c1, cur_len, len;

            len = s->utf8_len;
            c1 = eb_nextc(s->b, offset, &offset1);
            s->b->cur_style = QE_STYLE_TTY | s->color | s->attr;
            if (c1 == '\n') {
                /* insert */
                eb_insert(s->b, offset, s->utf8_buf, len);
            } else {
                cur_len = offset1 - offset;
                if (cur_len == len) {
                    eb_write(s->b, offset, s->utf8_buf, len);
                } else {
                    eb_delete(s->b, offset, cur_len);
                    eb_insert(s->b, offset, s->utf8_buf, len);
                }
            }
            s->cur_offset = offset + len;
            s->state = TTY_STATE_NORM;
        }
        break;
    case TTY_STATE_ESC:
        if (c == '[') {
            for (i = 0; i < MAX_ESC_PARAMS; i++) {
                s->esc_params[i] = 1;
                s->has_params[i] = 0;
            }
            s->nb_esc_params = 0;
            s->esc1 = 0;
            s->state = TTY_STATE_CSI;
        } else {
            /* CG: should deal with other sequences:
             * ansi: hts=\EH, s0ds=\E(B, s1ds=\E)B, s2ds=\E*B, s3ds=\E+B,
             * linux: hts=\EH, rc=\E8, ri=\EM, rs1=\Ec\E]R, sc=\E7,
             * vt100: enacs=\E(B\E)0, hts=\EH, rc=\E8, ri=\EM$<5>,
             *        rmkx=\E[?1l\E>,
             *        rs2=\E>\E[?3l\E[?4l\E[?5l\E[?7h\E[?8h, sc=\E7,
             *        smkx=\E[?1h\E=,
             * xterm: enacs=\E(B\E)0, hts=\EH, is2=\E[!p\E[?3;4l\E[4l\E>,
             *        rc=\E8, ri=\EM, rmkx=\E[?1l\E>, rs1=\Ec,
             *        rs2=\E[!p\E[?3;4l\E[4l\E>, sc=\E7, smkx=\E[?1h\E=,
             *        set-window-title=\E]0;title text\007,
             */
            switch (c) {
            case '%':
            case '(':
            case ')':
            case '*':
            case '+':
            case ']':
                s->esc1 = c;
                s->state = TTY_STATE_ESC2;
                break;
            case '7':   // sc   (save_cursor)
            case '8':   // rc   (restore_cursor)
            case '=':   // smkx (DECKPAM: Keypad application mode)
            case '>':   // rmkx, is2, rs2  (DECKPNM: Keypad numeric mode)
            case 'D':   // IND: exactly equivalent to LF
            case 'E':   // NEL: exactly equivalent to CR-LF
            case 'M':   // ri   (scroll_reverse, RI: reverse index - backwards LF)
            case 'Z':   /* DECID: terminal type query */
            case 'c':   // rs1  (reset_1string)
                        /* RIS: restore power-on settings */
            case 'H':   // hts  (set_tab)
                // XXX: do these
            default:
                s->state = TTY_STATE_NORM;
                break;
            }
        }
        break;
    case TTY_STATE_ESC2:
        s->state = TTY_STATE_NORM;
        s->esc2 = c;
        switch (ESC2(s->esc1, c)) {
        case ESC2('%','G'):     /* set utf mode */
        case ESC2('%','8'):     /* set utf mode */
        case ESC2('%','@'):     /* reset utf mode */
            break;
        case ESC2('(','A'):     /* set charset0 CSET_GBCHR */
        case ESC2('(','U'):     /* set charset0 CSET_SCOACS */
        case ESC2('(','B'):     /* set charset0 CSET_ASCII */
            s->charset[0] = 0;
            break;
        case ESC2('(','0'):     /* set charset0 CSET_LINEDRW */
            s->charset[0] = 1;
            break;
        case ESC2(')','A'):     /* set charset1 CSET_GBCHR */
        case ESC2(')','U'):     /* set charset1 CSET_SCOACS */
        case ESC2(')','B'):     /* set charset1 CSET_ASCII */
            s->charset[1] = 0;
            break;
        case ESC2(')','0'):     /* set charset1 CSET_LINEDRW */
            s->charset[1] = 1;
            break;
        case ESC2('*','B'):
        case ESC2('+','B'):
            /* XXX: Todo */
            break;
        case ESC2(']','0'):     /* xterm's set-window-title and icon */
        case ESC2(']','1'):     /* xterm's set-window-title */
        case ESC2(']','2'):     /* xterm's set-window-title */
        case ESC2(']','4'):     /* xterm's define-extended color */
        case ESC2(']','W'):     /* word-set (define char wordness) */
            s->state = TTY_STATE_STRING;
            break;
        case ESC2(']','P'):     /* linux set palette */
        case ESC2(']','R'):     /* linux reset palette */
            /* XXX: Todo */
            break;
        }
        s->shifted = s->charset[s->cset];
        break;
    case TTY_STATE_STRING:
        /* CG: should store the string */
        /* Stop string on CR or LF, for protection */
        if (c == '\012' || c == '\015') {
            s->state = TTY_STATE_NORM;
            break;
        }
        /* Stop string on \a (^G) or M-\ -- need better test for ESC \ */
        if (c == '\007' || c == 0234 || c == '\\') {
            /* CG: ESC2(']','0') should set shell caption */
            /* CG: ESC2(']','4') should parse color definition string */
            /* (example: "\033]4;16;rgb:00/00/00\033\134" ) */
            s->state = TTY_STATE_NORM;
        }
        break;
    case TTY_STATE_CSI:
        if (c == '?' || c == '=') {
            s->esc1 = c;
            break;
        }
        if (qe_isdigit(c)) {
            if (s->nb_esc_params < MAX_ESC_PARAMS) {
                if (!s->has_params[s->nb_esc_params]) {
                    s->esc_params[s->nb_esc_params] = 0;
                    s->has_params[s->nb_esc_params] = 1;
                }
                s->esc_params[s->nb_esc_params] =
                    s->esc_params[s->nb_esc_params] * 10 + c - '0';
            }
            break;
        } else {
            s->nb_esc_params++;
            if (c == ';')
                break;
            s->state = TTY_STATE_NORM;
            switch (ESC2(s->esc1,c)) {
            case 'h':   /* SM: toggle modes to high */
            case ESC2('?','h'): /* set terminal mode */
                /* 1047: alternate screen
                 * 1048: save / restore cursor
                 * 1049: save / restore  cursor and alternate screen
                 * should grab all keys while active!
                 */
                if (s->esc_params[0] == 1047 ||
                    s->esc_params[0] == 1048 ||
                    s->esc_params[0] == 1049) {
                    if (s->shell_flags & SF_INTERACTIVE) {
                        /* only grab keys in interactive tty buffers */
                        s->grab_keys = 1;
                        qe_grab_keys(shell_key, s);
                        /* Should also clear screen */
                    }
                }
                break;
            case 'i':   /* MC: Media copy */
            case ESC2('?','i'):
                break;
            case ESC2('?','l'): /* reset terminal mode */
                if (s->esc_params[0] == 1047 ||
                    s->esc_params[0] == 1048 ||
                    s->esc_params[0] == 1049) {
                    if (s->shell_flags & SF_INTERACTIVE) {
                        qe_ungrab_keys();
                        s->grab_keys = 0;
                    }
                }
                break;
            case 'A':  /* CUU: move up N lines */
                tty_goto_xy(s, 0, -s->esc_params[0], 3);
                break;
            case 'e':  /* VPR: move down N lines */
            case 'B':  /* CUD: Cursor down */
                tty_goto_xy(s, 0, s->esc_params[0], 3);
                break;
            case 'a':  /* HPR: move right N cols */
            case 'C':  /* CUF: Cursor right */
                tty_goto_xy(s, s->esc_params[0], 0, 3);
                break;
            case 'D':  /* CUB: move left N cols */
                tty_goto_xy(s, -s->esc_params[0], 0, 3);
                break;
            case 'F':  /* CPL: move up N lines and CR */
                tty_goto_xy(s, 0, -s->esc_params[0], 2);
                break;
            case 'G':  /* CHA: goto column_address */
            case '`':  /* HPA: set horizontal posn */
                tty_goto_xy(s, s->esc_params[0] - 1, 0, 2);
                break;
            case 'H':  /* CUP: goto xy */
            case 'f':  /* HVP: set horz and vert posns at once */
                tty_goto_xy(s, s->esc_params[1] - 1, s->esc_params[0] - 1, 0);
                break;
            case 'd':
                /* goto y */
                tty_goto_xy(s, 0, s->esc_params[0] - 1, 1);
                break;
            case 'J':  /* ED: erase screen or parts of it */
                       /*     0: to end, 1: from begin, 2: all */
                //put_status(NULL, "erase screen %d", s->esc_params[0]);
                break;
            case 'K':  /* EL: erase line or parts of it */
                       /*     0: to end, 1: from begin, 2: all line */
                offset1 = eb_goto_eol(s->b, offset);
                eb_delete(s->b, offset, offset1 - offset);
                break;
            case 'L':  /* IL: insert lines */
                /* TODO! scroll down */
                //put_status(NULL, "insert lines %d", s->esc_params[0]);
                break;
            case 'M':  /* delete lines */
                /* TODO! scroll up */
                //put_status(NULL, "delete lines %d", s->esc_params[0]);
                break;
            case '@':  /* ICH: insert chars (no cursor update) */
                buf1[0] = ' ';
                offset1 = offset;
                s->b->cur_style = QE_STYLE_TTY | s->color | s->attr;
                for (n = s->esc_params[0]; n > 0; n--) {
                    /* XXX: incorrect for non 8 bit charsets */
                    eb_insert(s->b, offset1, buf1, 1);
                    offset1 += 1;
                }
                s->cur_offset = offset;
                break;
            case 'P':  /* DCH: delete chars */
                offset1 = offset;
                for (n = s->esc_params[0]; n > 0; n--) {
                    c = eb_nextc(s->b, offset1, &offset2);
                    if (c == '\n')
                        break;
                    offset1 = offset2;
                }
                eb_delete(s->b, offset, offset1 - offset);
                break;
            case 'c':  /* DA: terminal type query */
                break;
            case 'n':  /* DSR: cursor position query */
                if (s->esc_params[0] == 6) {
                    /* XXX: send cursor position, just to be able to
                       launch qemacs in qemacs (in 8859-1) ! */
                    char buf2[20];
                    int col_num, cur_line;
                    eb_get_pos(s->b, &cur_line, &col_num, offset);
                    /* XXX: actually send position of point in window */
                    snprintf(buf2, sizeof(buf2), "\033[%d;%dR",
                             1, col_num + 1);
                    tty_write(s, buf2, -1);
                }
                break;
            case 'g':  /* TBC: clear tabs */
                break;
            case 'r':  /* DECSTBM: set scroll margins */
                //put_status(NULL, "set scroll margins %d %d", 
                //           s->esc_params[0], s->esc_params[1]);
                break;
            case 'm':  /* SGR: set graphics rendition (style and colors) */
                for (i = 0;;) {
                    tty_csi_m(s, s->esc_params[i], s->has_params[i]);
                    if (++i >= s->nb_esc_params)
                        break;
                }
                break;
            case 's':  /* save cursor */
            case 'u':  /* restore cursor */
            case 't':  /* DECSLPP: set page size - ie window height */
                       /* also used for window changing and reports */
                break;
            case 'S':  /* SU: SCO scroll up (forward) n lines */
            case 'T':  /* SD: SCO scroll down (back) n lines */
                //put_status(NULL, "scroll '%c' %d", c, s->esc_params[0]);
                break;
            case 'X':  /* ECH: erase n characters w/o moving cursor */
                for (n = s->esc_params[0]; n > 0; n--) {
                    tty_put_char(s, ' ');
                }
                /* restore cursor */
                s->cur_offset = offset;
                break;
            case 'x':  /* DECREQTPARM: report terminal characteristics */
            case 'Z':  /* CBT: move cursor back n tabs */
            case ESC2('=','c'):   /* Hide or Show Cursor */
                        /* 0: hide, 1: restore, 2: block */
            case ESC2('=','C'):  /* set cursor shape */
            case ESC2('=','D'):  /* set blinking attr on/off */
            case ESC2('=','E'):  /* set blinking on/off */
                break;
            case ESC2('=','F'): /* select SCO foreground color */
                TTY_SET_FG_COLOR(s->color, sco_color[s->esc_params[0] & 15]);
                break;
            case ESC2('=','G'): /* select SCO background color */
                TTY_SET_BG_COLOR(s->color, sco_color[s->esc_params[0] & 15]);
                break;
            default:
                break;
            }
        }
        break;
    }
#undef ESC2
    tty_update_cursor(s);
}

/* buffer related functions */

/* called when characters are available on the tty */
static void shell_read_cb(void *opaque)
{
    ShellState *s = opaque;
    QEmacsState *qs;
    unsigned char buf[16 * 1024];
    int len, i;

    if (!s || s->signature != &shell_signature)
        return;

    qs = s->qe_state;
    len = read(s->pty_fd, buf, sizeof(buf));
    if (len <= 0)
        return;

    if (qs->trace_buffer)
        eb_trace_bytes(buf, len, EB_TRACE_SHELL);

    {
        /* Suspend BF_READONLY flag to allow shell output to readonly buffer */
        int save_readonly = s->b->flags & BF_READONLY;
        s->b->flags &= ~BF_READONLY;
        s->b->last_log = 0;

        for (i = 0; i < len; i++)
            tty_emulate(s, buf[i]);

        s->b->flags |= save_readonly;
    }
    /* now we do some refresh */
    edit_display(qs);
    dpy_flush(qs->screen);
}

static void shell_close(EditBuffer *b)
{
    ShellState *s = b->priv_data;
    int status;

    if (!s || s->signature != &shell_signature)
        return;

    eb_free_callback(b, eb_offset_callback, &s->cur_offset);

    if (s->pid != -1) {
        kill(s->pid, SIGINT);
        /* wait first 100 ms */
        usleep(100 * 1000);
        if (waitpid(s->pid, &status, WNOHANG) != s->pid) {
            /* if still not killed, then try harder (useful for
               shells) */
            kill(s->pid, SIGKILL);
            /* CG: should add timeout facility and error message */
            while (waitpid(s->pid, &status, 0) != s->pid)
                continue;
        }
	set_pid_handler(s->pid, NULL, NULL);
        s->pid = -1;
    }
    if (s->pty_fd >= 0) {
        set_read_handler(s->pty_fd, NULL, NULL);
        close(s->pty_fd);
        s->pty_fd = -1;
    }
    qe_free(&b->priv_data);
    if (b->close == shell_close)
        b->close = NULL;
}

static void shell_pid_cb(void *opaque, int status)
{
    ShellState *s = opaque;
    EditBuffer *b;
    QEmacsState *qs;
    EditState *e;
    char buf[1024];

    if (!s || s->signature != &shell_signature)
        return;

    b = s->b;
    qs = s->qe_state;

    *buf = '\0';
    if (s->caption) {
        time_t ti;
        char *time_str;

        ti = time(NULL);
        time_str = ctime(&ti);
        if (WIFEXITED(status))
            status = WEXITSTATUS(status);
        else
            status = -1;
        if (status == 0) {
            snprintf(buf, sizeof(buf), "\n%s finished at %s",
                     s->caption, time_str);
        } else {
            snprintf(buf, sizeof(buf), "\n%s exited abnormally with code %d at %s",
                     s->caption, status, time_str);
        }
    }
    {
        /* Flush output to buffer, bypassing readonly flag */
        int save_readonly = s->b->flags & BF_READONLY;
        s->b->flags &= ~BF_READONLY;

        eb_write(b, b->total_size, buf, strlen(buf));

        if (save_readonly) {
            s->b->modified = 0;
            s->b->flags |= save_readonly;
        }
    }

    set_pid_handler(s->pid, NULL, NULL);
    s->pid = -1;
    /* no need to leave the pty opened */
    if (s->pty_fd >= 0) {
        set_read_handler(s->pty_fd, NULL, NULL);
        close(s->pty_fd);
        s->pty_fd = -1;
    }

    /* remove shell input mode */
    s->grab_keys = 0;
    qe_ungrab_keys();
    for (e = qs->first_window; e != NULL; e = e->next_window) {
        if (e->b == b)
            e->interactive = 0;
        if (s->shell_flags & SF_AUTO_CODING)
            do_set_auto_coding(e, 0);
        if (s->shell_flags & SF_AUTO_MODE)
            do_set_next_mode(e, 0);
    }
    if (!(s->shell_flags & SF_INTERACTIVE)) {
        shell_close(b);
    }
    edit_display(qs);
    dpy_flush(qs->screen);
}

EditBuffer *new_shell_buffer(EditBuffer *b0, const char *bufname,
                             const char *caption, const char *cmd,
                             int shell_flags)
{
    QEmacsState *qs = &qe_state;
    ShellState *s;
    EditBuffer *b;
    const char *lang;
    int rows, cols;

    b = b0;
    if (b) {
        s = b->priv_data;
        if (s && s->signature != &shell_signature)
            return NULL;
    } else {
        int bf_flags = BF_SAVELOG;
        if (shell_flags & SF_COLOR)
            bf_flags |= BF_STYLE2;
        b = eb_new("", bf_flags);
        if (!b)
            return NULL;
    }

    eb_set_buffer_name(b, bufname); /* ensure that the name is unique */

    /* Select shell output buffer encoding from LANG setting */
    if (((lang = getenv("LANG")) != NULL && strstr(lang, "UTF-8")) ||
          qs->screen->charset == &charset_utf8) {
        eb_set_charset(b, &charset_utf8, b->eol_type);
    } else {
        eb_set_charset(b, &charset_vt100, b->eol_type);
    }

    s = b->priv_data;
    if (!s) {
        s = qe_mallocz(ShellState);
        if (!s) {
            if (!b0)
                eb_free(&b);
            return NULL;
        }
        s->signature = &shell_signature;
        b->priv_data = s;
        b->close = shell_close;
        /* Track cursor with edge effect */
        eb_add_callback(b, eb_offset_callback, &s->cur_offset, 1);
    }
    s->b = b;
    s->pty_fd = -1;
    s->pid = -1;
    s->qe_state = qs;
    s->caption = caption;
    s->shell_flags = shell_flags;
    s->cur_offset = b->total_size;
    tty_init(s);

    /* launch shell */
    cols = TTY_XSIZE;
    rows = TTY_YSIZE;
    if (shell_flags & SF_INFINITE)
        rows = TTY_YSIZE_INFINITE;

    if (run_process(cmd, &s->pty_fd, &s->pid, cols, rows, shell_flags) < 0) {
        if (!b0)
            eb_free(&b);
        return NULL;
    }

    set_read_handler(s->pty_fd, shell_read_cb, s);
    set_pid_handler(s->pid, shell_pid_cb, s);
    return b;
}

static EditBuffer *try_show_buffer(EditState *s, const char *bufname)
{
    QEmacsState *qs = s->qe_state;
    EditState *e;
    EditBuffer *b;

    b = eb_find(bufname);
    if (b) {
        e = edit_find(b);
        if (e)
            qs->active_window = e;
        else
            switch_to_buffer(s, b);
    }
    return b;
}

static void do_shell(EditState *s, int force)
{
    ShellState *shs;
    EditBuffer *b = NULL;

    /* CG: Should prompt for buffer name if arg:
     * find a syntax for optional string argument w/ prompt
     */
    /* find shell buffer if any */
    if (!force || force == NO_ARG) {
        /* XXX: if current buffer is a shell buffer without a process,
         * restart shell process.
         */
        b = s->b;
        shs = b->priv_data;
        if (strstart(b->name, "*shell*", NULL)
        &&  shs && shs->signature == &shell_signature) {
            if (shs->pid >= 0)
                return;
        } else {
            b = try_show_buffer(s, "*shell*");
            if (b) {
                shs = b->priv_data;
                if (shs) {
                    if (shs->signature != &shell_signature) {
                        b = NULL;
                    } else
                    if (shs->pid >= 0)
                        return;
                }
            }
        }
        if (b) {
            /* restart shell in *shell* buffer */
            s->offset = b->total_size;
        }
    }

    /* create new buffer */
    b = new_shell_buffer(b, "*shell*", "Shell process", NULL,
                         SF_COLOR | SF_INTERACTIVE);
    if (!b)
        return;

    b->default_mode = &shell_mode;
    switch_to_buffer(s, b);

    put_status(s, "Press C-o to toggle between shell/edit mode");
}

static void do_man(EditState *s, const char *arg)
{
    char bufname[32];
    char cmd[128];
    EditBuffer *b;
    
    /* Assume standard man command */
    snprintf(cmd, sizeof(cmd), "man %s", arg);

    snprintf(bufname, sizeof(bufname), "*Man %s*", arg);
    if (try_show_buffer(s, bufname))
        return;

    /* create new buffer */
    b = new_shell_buffer(NULL, bufname, NULL, cmd, SF_COLOR | SF_INFINITE);
    if (!b)
        return;

    b->flags |= BF_READONLY;
    switch_to_buffer(s, b);
    edit_set_mode(s, &pager_mode);
}

static void do_ssh(EditState *s, const char *arg)
{
    char bufname[64];
    char cmd[128];
    EditBuffer *b;
    
    /* Use standard ssh command */
    snprintf(cmd, sizeof(cmd), "ssh %s", arg);
    snprintf(bufname, sizeof(bufname), "*ssh-%s*", arg);

    /* create new buffer */
    b = new_shell_buffer(NULL, bufname, "ssh", cmd,
                         SF_COLOR | SF_INTERACTIVE);
    if (!b)
        return;

    b->default_mode = &shell_mode;
    switch_to_buffer(s, b);
    //edit_set_mode(s, &shell_mode);

    put_status(s, "Press C-o to toggle between shell/edit mode");
}

static void shell_move_left_right(EditState *e, int dir)
{
    if (e->interactive) {
        ShellState *s;

        if (!(s = shell_get_state(e, 1)))
            return;

        tty_write(s, dir > 0 ? s->kcuf1 : s->kcub1, -1);
    } else {
        text_move_left_right_visual(e, dir);
    }
}

static void shell_move_word_left_right(EditState *e, int dir)
{
    if (e->interactive) {
        ShellState *s;

        if (!(s = shell_get_state(e, 1)))
            return;

        tty_write(s, dir > 0 ? "\033f" : "\033b", -1);
    } else {
        text_move_word_left_right(e, dir);
    }
}

static void shell_move_up_down(EditState *e, int dir)
{
    ShellState *s;

    if (!(s = shell_get_state(e, 1)))
        return;

    if (e->interactive) {
        tty_write(s, dir > 0 ? s->kcud1 : s->kcuu1, -1);
    } else {
        text_move_up_down(e, dir);
        if (s->shell_flags & SF_INTERACTIVE)
            e->interactive = (e->offset == s->cur_offset);
    }
}

static void shell_scroll_up_down(EditState *e, int dir)
{
    ShellState *s;

    if (!(s = shell_get_state(e, 1)))
        return;

    e->interactive = 0;
    text_scroll_up_down(e, dir);
    if (s->shell_flags & SF_INTERACTIVE)
        e->interactive = (e->offset == s->cur_offset);
}

static void shell_move_bol(EditState *e)
{
    /* XXX: exit shell interactive mode on home / ^A */
    e->interactive = 0;

    if (e->interactive) {
        ShellState *s;

        if (!(s = shell_get_state(e, 1)))
            return;

        tty_write(s, "\001", 1); /* Control-A */
    } else {
        text_move_bol(e);
    }
}

static void shell_move_eol(EditState *e)
{
    ShellState *s;

    if (!(s = shell_get_state(e, 1)))
        return;

    if (e->interactive) {
        tty_write(s, "\005", 1); /* Control-E */
    } else {
        text_move_eol(e);
        /* XXX: restore shell interactive mode on end / ^E */
        if (s->shell_flags & SF_INTERACTIVE)
            e->interactive = (e->offset == s->cur_offset);
    }
}

static void shell_write_char(EditState *e, int c)
{
    char buf[10];
    int len;

    if (e->interactive) {
        ShellState *s;

        if (!(s = shell_get_state(e, 1)))
            return;

        if (c >= KEY_META(0) && c <= KEY_META(0xff)) {
            buf[0] = '\033';
            buf[1] = c - KEY_META(0);
            len = 2;
        } else {
            len = eb_encode_uchar(e->b, buf, c);
        }
        tty_write(s, buf, len);
    } else {
        /* Should dispatch as in fundamental mode */
        switch (c) {
        case KEY_CTRL('d'):
            do_delete_char(e, NO_ARG);
            break;
        // Do not do this: it is useless and causes infinite recursion
        //case 9:
        //    do_tab(e, 1);
        //    break;
        case KEY_CTRL('k'):
            do_kill_line(e, 1);
            break;
        case KEY_CTRL('y'):
            do_yank(e);
            break;
        case KEY_BS:
        case KEY_DEL:
            do_backspace(e, NO_ARG);
            break;
        case '\r':
            do_return(e, 1);
            break;
        case KEY_META('d'):
            do_kill_word(e, 1);
            break;
        case KEY_META(KEY_BS):
        case KEY_META(KEY_DEL):
            do_kill_word(e, -1);
            break;
        default:
            text_write_char(e, c);
            break;
        }
    }
    if (c == '\r') {
        /* skip errors from previous commands */
        set_error_offset(e->b, e->offset);
    }
}

static void do_shell_toggle_input(EditState *e)
{
    ShellState *s;

    if (!(s = shell_get_state(e, 1)))
        return;

    if (e->interactive) {
        e->interactive = 0;
    } else
    if (s->shell_flags & SF_INTERACTIVE) {
        e->interactive = 1;
        if (s->grab_keys)
            qe_grab_keys(shell_key, s);
    }
#if 0
    if (e->interactive) {
        tty_update_cursor(s);
    }
#endif
}

static void do_shell_command(EditState *e, const char *cmd)
{
    EditBuffer *b;

    /* if the buffer already exists, kill it */
    b = eb_find("*shell command output*");
    if (b) {
        kill_buffer_noconfirm(b);
    }

    /* create new buffer */
    b = new_shell_buffer(NULL, "*shell command output*", NULL, cmd,
                         SF_COLOR | SF_INFINITE);
    if (!b)
        return;

    /* XXX: try to split window if necessary */
    switch_to_buffer(e, b);
    edit_set_mode(e, &pager_mode);
}

static void do_compile(EditState *e, const char *cmd)
{
    EditBuffer *b;

    /* if the buffer already exists, kill it */
    b = eb_find("*compilation*");
    if (b) {
        kill_buffer_noconfirm(b);
    }

    if (!cmd || !*cmd)
        cmd = "make";

    /* create new buffer */
    b = new_shell_buffer(NULL, "*compilation*", "Compilation", cmd,
                         SF_COLOR | SF_INFINITE);
    if (!b)
        return;

    /* XXX: try to split window if necessary */
    switch_to_buffer(e, b);
    edit_set_mode(e, &pager_mode);
    set_error_offset(b, 0);
}

static void do_compile_error(EditState *s, int dir)
{
    QEmacsState *qs = s->qe_state;
    EditState *e;
    EditBuffer *b;
    int offset, found_offset;
    char filename[MAX_FILENAME_SIZE], *q;
    int line_num, c;
    char error_message[128];

    /* CG: should have a buffer flag for error source.
     * first check if current buffer is an error source.
     * if not, then scan for appropriate error source
     * in buffer least recently used order
     */

    if ((b = eb_find(error_buffer)) == NULL) {
        if ((b = eb_find("*compilation*")) == NULL
        &&  (b = eb_find("*shell*")) == NULL
        &&  (b = eb_find("*errors*")) == NULL) {
            put_status(s, "No compilation buffer");
            return;
        }
        set_error_offset(b, -1);
    }

    /* find next/prev error */
    offset = error_offset;

    /* CG: should use higher level parsing */
    for (;;) {
        if (dir > 0) {
            offset = eb_next_line(b, offset);
            if (offset >= b->total_size) {
                put_status(s, "No more errors");
                return;
            }
        } else {
            if (offset <= 0) {
                put_status(s, "No previous error");
                return;
            }
            offset = eb_prev_line(b, offset);
        }
        found_offset = offset;
        /* parse filename:linenum:message */
        /* extract filename */
        for (q = filename;;) {
            c = eb_nextc(b, offset, &offset);
            if (c == '\n' || c == '\t' || c == ' ')
                goto next_line;
            if (c == ':')
                break;
            /* CG: utf8 issue */
            if ((q - filename) < ssizeof(filename) - 1)
                *q++ = c;
        }
        *q = '\0';
        /* extract line number */
        for (line_num = 0;;) {
            c = eb_nextc(b, offset, &offset);
            if (c == ':')
                break;
            if (!qe_isdigit(c))
                goto next_line;
            line_num = line_num * 10 + c - '0';
        }
        eb_get_strline(b, error_message, sizeof(error_message), &offset);
        if (line_num >= 1) {
            if (line_num != error_line_num ||
                !strequal(filename, error_filename)) {
                error_line_num = line_num;
                pstrcpy(error_filename, sizeof(error_filename), filename);
                break;
            }
        }
      next_line:
        offset = found_offset;
    }
    error_offset = found_offset;
    /* update offsets */
    for (e = qs->first_window; e != NULL; e = e->next_window) {
        if (e->b == b) {
            e->offset = error_offset;
        }
    }

    /* CG: Should remove popups, sidepanes, helppanes... */

    /* go to the error */
    do_find_file(s, filename);
    do_goto_line(qs->active_window, line_num);

    put_status(s, "=> %s", error_message);
}

/* shell mode specific commands */
static CmdDef shell_commands[] = {
    CMD0( KEY_CTRL('o'), KEY_NONE,
          "shell-toggle-input", do_shell_toggle_input)
    CMD1( '\r', KEY_NONE,
          "shell-return", shell_write_char, '\r')
    /* CG: should send s->kbs */
    CMD1( KEY_DEL, KEY_NONE,
          "shell-backward-delete-char", shell_write_char, KEY_DEL)
    CMD1( KEY_CTRLC(KEY_CTRL('c')), KEY_NONE,   /* C-c C-c */
          "shell-intr", shell_write_char, 3)
    CMD1( KEY_CTRL('d'), KEY_DELETE,
          "shell-delete-char", shell_write_char, 4)
    CMD1( KEY_META('d'), KEY_NONE,
          "shell-delete-word", shell_write_char, KEY_META('d'))
    CMD2( KEY_META(KEY_DEL), KEY_META(KEY_BS) ,
          "shell-backward-delete-word", shell_write_char, ESi, "*ki")
    CMD2( KEY_META('p'), KEY_META('n'),
          "shell-history-search", shell_write_char, ESi, "*ki")
    CMD1( KEY_CTRL('i'), KEY_NONE,
          "shell-tabulate", shell_write_char, 9)
    CMD1( KEY_CTRL('k'), KEY_NONE,
          "shell-kill-line", shell_write_char, 11)
    CMD1( KEY_CTRL('y'), KEY_NONE,
          "shell-yank", shell_write_char, 25)
    CMD_DEF_END,
};

/* shell global commands */
static CmdDef shell_global_commands[] = {
    CMD2( KEY_CTRLXRET('\r'), KEY_NONE,
          "shell", do_shell, ESi, "ui")
    CMD2( KEY_META('!'), KEY_NONE,
          "shell-command", do_shell_command, ESs,
          "s{Shell command: }|shell-command|")
    CMD2( KEY_NONE, KEY_NONE,
          "ssh", do_ssh, ESs,
          "s{Open connection to (host or user@host: }|ssh|")
    CMD2( KEY_CTRLX(KEY_CTRL('e')), KEY_NONE,
          "compile", do_compile, ESs,
          "s{Compile command: }|compile|")
    CMD2( KEY_CTRLX('m'), KEY_NONE,
          "make", do_compile, ESs,
          "S{make}")
    CMD2( KEY_NONE, KEY_NONE,
          "man", do_man, ESs,
          "s{Show man page for: }|man|")
    CMD1( KEY_CTRLX(KEY_CTRL('p')), KEY_NONE,
          "previous-error", do_compile_error, -1) /* u */
    CMD1( KEY_CTRLX(KEY_CTRL('n')), KEY_CTRLX('`'),
          "next-error", do_compile_error, 1) /* u */
    CMD_DEF_END,
};

static int shell_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (p->b && p->b->priv_data) {
        ShellState *s = p->b->priv_data;
        if (s->signature == &shell_signature) {
            if (s->shell_flags & SF_INTERACTIVE)
                return 100;
        }
    }
    return 0;
}

static int shell_mode_init(EditState *e, ModeSavedData *saved_data)
{
    ShellState *s;

    if (!(s = shell_get_state(e, 1)))
        return -1;

    text_mode_init(e, saved_data);
    e->b->tab_width = 8;
    e->wrap = WRAP_TRUNCATE;
    if (s->shell_flags & SF_INTERACTIVE)
        e->interactive = 1;
    return 0;
}

static int pager_mode_init(EditState *e, ModeSavedData *saved_data)
{
    text_mode_init(e, saved_data);
    e->b->tab_width = 8;
    e->wrap = WRAP_TRUNCATE;
    return 0;
}

static int shell_init(void)
{
    /* populate and register shell mode and commands */
    memcpy(&shell_mode, &text_mode, sizeof(ModeDef));
    shell_mode.name = "shell";
    shell_mode.mode_probe = shell_mode_probe;
    shell_mode.mode_init = shell_mode_init;
    shell_mode.display_hook = shell_display_hook;
    shell_mode.move_left_right = shell_move_left_right;
    shell_mode.move_word_left_right = shell_move_word_left_right;
    shell_mode.move_up_down = shell_move_up_down;
    shell_mode.scroll_up_down = shell_scroll_up_down;
    shell_mode.move_bol = shell_move_bol;
    shell_mode.move_eol = shell_move_eol;
    shell_mode.write_char = shell_write_char;
    shell_mode.mode_flags |= MODEF_NOCMD;

    qe_register_mode(&shell_mode);
    qe_register_cmd_table(shell_commands, &shell_mode);

    /* global shell related commands and default keys */
    qe_register_cmd_table(shell_global_commands, NULL);

    /* populate and register pager mode and commands */
    memcpy(&pager_mode, &text_mode, sizeof(ModeDef));
    pager_mode.name = "pager";
    pager_mode.mode_probe = NULL;
    pager_mode.mode_init = pager_mode_init;
    pager_mode.mode_flags |= MODEF_NOCMD;

    qe_register_mode(&pager_mode);

    qe_mode_set_key(&pager_mode, "DEL", "scroll-down");
    qe_mode_set_key(&pager_mode, "SPC", "scroll-up");
    qe_mode_set_key(&pager_mode, "/", "search-forward");

    return 0;
}

qe_module_init(shell_init);
