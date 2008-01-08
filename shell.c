/*
 * Shell mode for QEmacs.
 *
 * Copyright (c) 2001, 2002 Fabrice Bellard.
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

static ModeDef shell_mode;

#define MAX_ESC_PARAMS 3

enum TTYState {
    TTY_STATE_NORM,
    TTY_STATE_ESC,
    TTY_STATE_ESC2,
    TTY_STATE_CSI,
    TTY_STATE_STRING,
};

typedef struct ShellState {
    /* buffer state */
    int pty_fd;
    int pid; /* -1 if not launched */
    int color, def_color;
    int cur_offset; /* current offset at position x, y */
    int esc_params[MAX_ESC_PARAMS];
    int has_params[MAX_ESC_PARAMS];
    int nb_esc_params;
    int state;
    int esc1;
    int shifted;
    int grab_keys;
    EditBuffer *b;
    EditBuffer *b_color; /* color buffer, one byte per char */
    int is_shell; /* only used to display final message */
    struct QEmacsState *qe_state;
    const char *ka1, *ka3, *kb2, *kc1, *kc3, *kcbt, *kspd;
    const char *kbeg, *kbs, *kent, *kdch1, *kich1;
    const char *kcub1, *kcud1, *kcuf1, *kcuu1;
    const char *kf1, *kf2, *kf3, *kf4, *kf5;
    const char *kf6, *kf7, *kf8, *kf9, *kf10;
    const char *kf11, *kf12, *kf13, *kf14, *kf15;
    const char *kf16, *kf17, *kf18, *kf19, *kf20;
    const char *khome, *kend, *kmous, *knp, *kpp;

} ShellState;

static int shell_get_colorized_line(EditState *e,
                                    unsigned int *buf, int buf_size,
                                    int offset, int line_num);

/* move to mode */
static int shell_launched = 0;

static int shell_mode_init(EditState *s, __unused__ ModeSavedData *saved_data)
{
    s->tab_size = 8;
    s->wrap = WRAP_TRUNCATE;
    s->interactive = 1;
    set_colorize_func(s, NULL);
    s->get_colorized_line_func = shell_get_colorized_line;
    return 0;
}

#define PTYCHAR1 "pqrstuvwxyz"
#define PTYCHAR2 "0123456789abcdef"

/* allocate one pty/tty pair */
static int get_pty(char *tty_str)
{
   int fd;
   char ptydev[] = "/dev/pty??";
   char ttydev[] = "/dev/tty??";
   int len = strlen(ttydev);
   const char *c1, *c2;

   for (c1 = PTYCHAR1; *c1; c1++) {
       ptydev[len-2] = ttydev[len-2] = *c1;
       for (c2 = PTYCHAR2; *c2; c2++) {
           ptydev[len-1] = ttydev[len-1] = *c2;
           if ((fd = open(ptydev, O_RDWR)) >= 0) {
               if (access(ttydev, R_OK|W_OK) == 0) {
                   strcpy(tty_str, ttydev);
                   return fd;
               }
               close(fd);
           }
       }
   }
   return -1;
}

static int run_process(const char *path, const char **argv,
                       int *fd_ptr, int *pid_ptr)
{
    int pty_fd, pid, i, nb_fds;
    char tty_name[32];
    struct winsize ws;

    pty_fd = get_pty(tty_name);
    if (pty_fd < 0) {
        put_status(NULL, "run_process: cannot get tty");
        return -1;
    }
    fcntl(pty_fd, F_SETFL, O_NONBLOCK);
    /* set dummy screen size */
    ws.ws_col = 80;
    ws.ws_row = 25;
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
        nb_fds = getdtablesize();
        for (i = 0; i < nb_fds; i++)
            close(i);
        /* open pseudo tty for standard i/o */
        open(tty_name, O_RDWR);
        dup(0);
        dup(0);

        setsid();

        //setenv("TERM", "linux", 1);
        //setenv("QELEVEL", "1", 1);

        execv(path, (char *const*)argv);
        exit(1);
    }
    /* return file info */
    *fd_ptr = pty_fd;
    *pid_ptr = pid;
    return 0;
}

/* VT100 emulation */

#define TTY_YSIZE 25

static void tty_init(ShellState *s)
{
    char *term;

    s->state = TTY_STATE_NORM;
    s->cur_offset = 0;
    s->def_color = TTY_GET_COLOR(7, 0);
    s->color = s->def_color;

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

/* Compute offset the of the char at column x and row y (0 based).
 * Can insert spaces or rows if needed.
 * x and y may each be relative to the current position.
 */
/* XXX: optimize !!!!! */
static void tty_goto_xy(ShellState *s, int x, int y, int relative)
{
    int total_lines, cur_line, line_num, col_num, offset, offset1, c;
    unsigned char buf1[10];

    /* compute offset */
    eb_get_pos(s->b, &total_lines, &col_num, s->b->total_size);
    if (s->cur_offset == s->b->total_size
    ||  eb_prevc(s->b, s->b->total_size, NULL) != '\n')
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
    if (y < 0)
        y = 0;
    else
    if (y >= TTY_YSIZE)
        y = TTY_YSIZE - 1;
    if (x < 0)
        x = 0;

    line_num += y;
    /* add lines if necessary */
    while (line_num >= total_lines) {
        buf1[0] = '\n';
        eb_insert(s->b, s->b->total_size, buf1, 1);
        total_lines++;
    }
    offset = eb_goto_pos(s->b, line_num, 0);
    for (; x > 0; x--) {
        c = eb_nextc(s->b, offset, &offset1);
        if (c == '\n') {
            buf1[0] = ' ';
            for (; x > 0; x--) {
                eb_insert(s->b, offset, buf1, 1);
                offset++;
            }
            break;
        } else {
            offset = offset1;
        }
    }
    s->cur_offset = offset;
}

static int tty_put_char(ShellState *s, int c)
{
    char buf[1];
    int c1, cur_len, offset;

    buf[0] = c;
    c1 = eb_nextc(s->b, s->cur_offset, &offset);
    if (c1 == '\n') {
        /* insert */
        eb_insert(s->b, s->cur_offset, buf, 1);
    } else {
        /* check for (c1 != c) is not advisable optimisation because
         * re-writing the same character may cause color changes.
         */
        cur_len = offset - s->cur_offset;
        if (cur_len == 1) {
            eb_write(s->b, s->cur_offset, buf, 1);
        } else {
            eb_delete(s->b, s->cur_offset, cur_len);
            eb_insert(s->b, s->cur_offset, buf, 1);
        }
    }
    return s->cur_offset + 1;
}

static void tty_csi_m(ShellState *s, int c, int has_param)
{
    /* we handle only a few possible modes */
    switch (has_param ? c : 0) {
    case 0:     /* exit_attribute_mode */
        s->color = s->def_color;
        break;
    case 1:     /* enter_bold_mode */
        s->color |= TTY_BOLD;
        break;
    case 22:    /* exit_bold_mode */
        s->color &= ~TTY_BOLD;
        break;
    case 4:     /* enter_underline_mode */
    case 5:     /* enter_blink_mode */
    case 7:     /* enter_reverse_mode, enter_standout_mode */
    case 8:     /* enter_secure_mode */
    case 24:    /* exit_underline_mode */
    case 25:    /* exit_blink_mode */
    case 27:    /* exit_reverse_mode, exit_standout_mode */
    case 28:    /* exit_secure_mode */
    case 38:    /* set extended foreground color ? */
    case 39:    /* orig_pair(1) */
    case 48:    /* set extended background color ? */
    case 49:    /* orig_pair(2) */
        break;
    default:
        /* 0:black 1:red 2:green 3:yellow 4:blue 5:magenta 6:cyan 7:white */
        if (c >= 30 && c <= 37) {
            /* set foreground color */
            s->color &= ~(TTY_BOLD | TTY_BG_COLOR(7));
            s->color |= TTY_FG_COLOR(c - 30);
        } else
        if (c >= 40 && c <= 47) {
            /* set background color */
            s->color &= ~(TTY_BOLD | TTY_FG_COLOR(7));
            s->color |= TTY_BG_COLOR(c - 40);
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

/* CG: much cleaner way! */
/* would need a kill hook as well ? */
static void shell_display_hook(EditState *e)
{
    ShellState *s = e->b->priv_data;

    if (e->interactive)
        e->offset = s->cur_offset;
}

static void shell_key(void *opaque, int key)
{
    ShellState *s = opaque;
    char buf[10];
    const char *p;
    int len;

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
    if (p)
        tty_write(s, p, len);
}

static void tty_emulate(ShellState *s, int c)
{
    int i, offset, offset1, offset2, n;
    char buf1[10];

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
                c1 = eb_prevc(s->b, s->cur_offset, &offset);
                if (c1 != '\n') {
                    s->cur_offset = offset;
                    /* back_color_erase */
                    //tty_put_char(s, ' ');
                }
            }
            break;
        case 9:        /* ^I  HT = horizontal tab */
            {
                int col_num, cur_line;
                eb_get_pos(s->b, &cur_line, &col_num, s->cur_offset);
                tty_goto_xy(s, (col_num + 8) & ~7, 0, 2);
                break;
            }
        case 10:        /* ^J  NL = line feed */
            /* go to next line */
            /* CG: should check if column should be kept */
            offset = s->cur_offset;
            for (;;) {
                if (offset == s->b->total_size) {
                    /* add a new line */
                    buf1[0] = '\n';
                    eb_insert(s->b, offset, buf1, 1);
                    offset = s->b->total_size;
                    break;
                }
                c = eb_nextc(s->b, offset, &offset);
                if (c == '\n')
                    break;
            }
            s->cur_offset = offset;
            break;
        case 13:        /* ^M  CR = carriage return */
            /* move to bol */
            s->cur_offset = eb_goto_bol(s->b, s->cur_offset);
            break;
        case 14:        /* ^N  SO = shift out */
            // was in qemacs-0.3.1.g2.gw
            //eb_set_charset(s->b, &charset_8859_1);
            s->shifted = 1;
            break;
        case 15:        /* ^O  SI = shift in */
            // was in qemacs-0.3.1.g2.gw
            //eb_set_charset(s->b, &charset_cp1125);
            s->shifted = 0;
            break;
        default:
            if (c >= 32) {
                int c1, cur_len, len;
                /* CG: assuming ISO-8859-1 characters */
                /* CG: horrible kludge for alternate charset support */
                if (s->shifted && c >= 96 && c < 128)
                    c += 32;
                /* write char (should factorize with do_char() code */
                len = unicode_to_charset(buf1, c, s->b->charset);
                c1 = eb_nextc(s->b, s->cur_offset, &offset);
                /* Should simplify with tty_put_char */
                if (c1 == '\n') {
                    /* insert */
                    eb_insert(s->b, s->cur_offset, buf1, len);
                } else {
                    cur_len = offset - s->cur_offset;
                    if (cur_len == len) {
                        eb_write(s->b, s->cur_offset, buf1, len);
                    } else {
                        eb_delete(s->b, s->cur_offset, cur_len);
                        eb_insert(s->b, s->cur_offset, buf1, len);
                    }
                }
                s->cur_offset += len;
            }
            break;
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
            case '(':
            case ')':
            case '*':
            case '+':
            case ']':
                s->esc1 = c;
                s->state = TTY_STATE_ESC2;
                break;
            case 'H':   // hts  (set_tab)
            case '7':   // sc   (save_cursor)
            case '8':   // rc   (restore_cursor)
            case 'M':   // ri   (scroll_reverse)
            case 'c':   // rs1  (reset_1string)
            case '>':   // rmkx, is2, rs2  (keypad_local ???)
            case '=':   // smkx (keypad_xmit ???)
                // XXX: do these
            default:
                s->state = TTY_STATE_NORM;
                break;
            }
        }
        break;
    case TTY_STATE_ESC2:
        s->state = TTY_STATE_NORM;
        switch (ESC2(s->esc1, c)) {
        case ESC2('(','B'):     /* exit_alt_charset_mode */
            s->shifted = 0;
            break;
        case ESC2('(','0'):     /* enter_alt_charset_mode */
            s->shifted = 1;
            break;
        case ESC2(')','B'):
        case ESC2(')','0'):
        case ESC2('*','B'):
        case ESC2('+','B'):
        case ESC2(']','R'):
            /* XXX: ??? */
            break;
        case ESC2(']','0'):
            /* xterm's set-window-title */
            s->state = TTY_STATE_STRING;
            break;
        }
        break;
    case TTY_STATE_STRING:
        /* ignore string parameter upto \a (^G) */
        if (c == '\007') {
            s->state = TTY_STATE_NORM;
        }
        break;
    case TTY_STATE_CSI:
        if (c == '?') {
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
            case ESC2('?','h'): /* set terminal mode */
                /* 1047, 1048 -> cup mode:
                 * xterm 1049 private mode,
                 * should grab all keys while active!
                 */
                if (s->esc_params[0] == 1047 ||
                    s->esc_params[0] == 1048 ||
                    s->esc_params[0] == 1049) {
                    s->grab_keys = 1;
                    qe_grab_keys(shell_key, s);
                    /* Should also clear screen */
                }
                break;
            case ESC2('?','l'): /* reset terminal mode */
                if (s->esc_params[0] == 1047 ||
                    s->esc_params[0] == 1048 ||
                    s->esc_params[0] == 1049) {
                    qe_ungrab_keys();
                    s->grab_keys = 0;
                }
                break;
            case 'A':
                /* move relative up */
                tty_goto_xy(s, 0, -s->esc_params[0], 3);
                break;
            case 'B':
                /* move relative down */
                tty_goto_xy(s, 0, s->esc_params[0], 3);
                break;
            case 'C':
                /* move relative forward */
                tty_goto_xy(s, s->esc_params[0], 0, 3);
                break;
            case 'D':
                /* move relative backward */
                tty_goto_xy(s, -s->esc_params[0], 0, 3);
                break;
            case 'G':
                /* goto column_address */
                tty_goto_xy(s, s->esc_params[0] - 1, 0, 2);
                break;
            case 'H':
                /* goto xy */
                tty_goto_xy(s, s->esc_params[1] - 1, s->esc_params[0] - 1, 0);
                break;
            case 'd':
                /* goto y */
                tty_goto_xy(s, 0, s->esc_params[0] - 1, 1);
                break;
            case 'J':   /* clear to end of screen */
            case 'L':   /* insert lines */
            case 'M':   /* delete lines */
            case 'S':   /* scroll forward n lines */
            case 'T':   /* scroll back n lines */
                break;
            case 'X':   /* erase n characters */
                for (n = s->esc_params[0]; n > 0; n--) {
                    s->cur_offset = tty_put_char(s, ' ');
                }
                break;
            case 'K':   /* clear eol (parm=1 -> bol) */
                offset1 = eb_goto_eol(s->b, s->cur_offset);
                eb_delete(s->b, s->cur_offset, offset1 - s->cur_offset);
                break;
            case 'P':
                /* delete chars */
                offset1 = s->cur_offset;
                for (n = s->esc_params[0]; n > 0; n--) {
                    c = eb_nextc(s->b, offset1, &offset2);
                    if (c == '\n')
                        break;
                    offset1 = offset2;
                }
                eb_delete(s->b, s->cur_offset, offset1 - s->cur_offset);
                break;
            case '@':
                /* insert chars */
                buf1[0] = ' ';
                for (n = s->esc_params[0]; n > 0; n--) {
                    eb_insert(s->b, s->cur_offset, buf1, 1);
                }
                break;
            case 'm':
                /* colors */
                n = s->nb_esc_params;
                if (n == 0)
                    n = 1;
                for (i = 0; i < n; i++) {
                    tty_csi_m(s, s->esc_params[i], s->has_params[i]);
                }
                break;
            case 'n':
                if (s->esc_params[0] == 6) {
                    /* XXX: send cursor position, just to be able to
                       launch qemacs in qemacs (in 8859-1) ! */
                    char buf2[20];
                    int col_num, cur_line;
                    eb_get_pos(s->b, &cur_line, &col_num, s->cur_offset);
                    /* XXX: actually send position of point in window */
                    snprintf(buf2, sizeof(buf2), "\033[%d;%dR",
                             1, col_num + 1);
                    tty_write(s, buf2, -1);
                }
                break;
            case 'r': /* change_scroll_region (2 args) */
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

/* modify the color according to the current one (may be incorrect if
   we are editing because we should write default color) */
static void shell_color_callback(__unused__ EditBuffer *b,
                                 void *opaque,
                                 enum LogOperation op,
                                 int offset,
                                 int size)
{
    ShellState *s = opaque;
    unsigned char buf[32];
    int len;

    switch (op) {
    case LOGOP_WRITE:
        while (size > 0) {
            len = size;
            if (len > ssizeof(buf))
                len = ssizeof(buf);
            memset(buf, s->color, len);
            eb_write(s->b_color, offset, buf, len);
            size -= len;
            offset += len;
        }
        break;
    case LOGOP_INSERT:
        while (size > 0) {
            len = size;
            if (len > ssizeof(buf))
                len = ssizeof(buf);
            memset(buf, s->color, len);
            eb_insert(s->b_color, offset, buf, len);
            size -= len;
        }
        break;
    case LOGOP_DELETE:
        eb_delete(s->b_color, offset, size);
        break;
    default:
        break;
    }
}

static int shell_get_colorized_line(EditState *e,
                                    unsigned int *buf, int buf_size,
                                    int offset, __unused__ int line_num)
{
    EditBuffer *b = e->b;
    ShellState *s = b->priv_data;
    EditBuffer *b_color = s->b_color;
    int color, offset1, c;
    unsigned int *buf_ptr, *buf_end;
    unsigned char buf1[1];

    /* record line */
    buf_ptr = buf;
    buf_end = buf + buf_size;
    for (;;) {
        eb_read(b_color, offset, buf1, 1);
        color = buf1[0];
        c = eb_nextc(b, offset, &offset1);
        if (c == '\n')
            break;
        if (buf_ptr < buf_end) {
            /* XXX: test */
            if (color != s->def_color) {
                c |= (QE_STYLE_TTY | color) << STYLE_SHIFT;
            }
            *buf_ptr++ = c;
        }
        offset = offset1;
    }
    return buf_ptr - buf;
}

/* buffer related functions */

/* called when characters are available on the tty */
static void shell_read_cb(void *opaque)
{
    ShellState *s = opaque;
    QEmacsState *qs = s->qe_state;
    unsigned char buf[1024];
    int len, i;

    len = read(s->pty_fd, buf, sizeof(buf));
    if (len <= 0)
        return;

    if (qs->trace_buffer)
        eb_trace_bytes(buf, len, EB_TRACE_SHELL);

    for (i = 0; i < len; i++)
        tty_emulate(s, buf[i]);

    /* now we do some refresh */
    edit_display(qs);
    dpy_flush(qs->screen);
}

static void shell_pid_cb(void *opaque, int status)
{
    ShellState *s = opaque;
    EditBuffer *b = s->b;
    QEmacsState *qs = s->qe_state;
    EditState *e;
    char buf[1024];

    if (s->is_shell) {
        snprintf(buf, sizeof(buf), "\nProcess shell finished\n");
    } else {
        time_t ti;
        char *time_str;

        ti = time(NULL);
        time_str = ctime(&ti);
        if (WIFEXITED(status))
            status = WEXITSTATUS(status);
        else
            status = -1;
        if (status == 0) {
            snprintf(buf, sizeof(buf), "\nCompilation finished at %s",
                     time_str);
        } else {
            snprintf(buf, sizeof(buf), "\nCompilation exited abnormally with code %d at %s",
                     status, time_str);
        }
    }
    eb_write(b, b->total_size, buf, strlen(buf));
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
    }
    edit_display(qs);
    dpy_flush(qs->screen);
}

static void shell_close(EditBuffer *b)
{
    ShellState *s = b->priv_data;
    int status;

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
            while (waitpid(s->pid, &status, 0) != s->pid);
        }
        s->pid = -1;
    }
    if (s->pty_fd >= 0) {
        set_read_handler(s->pty_fd, NULL, NULL);
    }
    qe_free(&s);
}

EditBuffer *new_shell_buffer(EditBuffer *b0, const char *name,
                             const char *path, const char **argv,
                             int is_shell)
{
    ShellState *s;
    EditBuffer *b, *b_color;

    b = b0;
    if (!b)
        b = eb_new("", BF_SAVELOG);
    if (!b)
        return NULL;
    eb_set_buffer_name(b, name); /* ensure that the name is unique */
    eb_set_charset(b, &charset_vt100);

    s = qe_mallocz(ShellState);
    if (!s) {
        if (!b0)
            eb_free(b);
        return NULL;
    }
    b->priv_data = s;
    b->close = shell_close;
    eb_add_callback(b, eb_offset_callback, &s->cur_offset);
    s->b = b;
    s->pty_fd = -1;
    s->pid = -1;
    s->is_shell = is_shell;
    s->qe_state = &qe_state;
    tty_init(s);

    /* add color buffer */
    if (is_shell) {
        b_color = eb_new("*color*", BF_SYSTEM);
        if (!b_color) {
            if (!b0)
                eb_free(b);
            qe_free(&s);
            return NULL;
        }
        /* no undo info in this color buffer */
        b_color->save_log = 0;
        eb_add_callback(b, shell_color_callback, s);
        s->b_color = b_color;
    }

    /* launch shell */
    if (run_process(path, argv, &s->pty_fd, &s->pid) < 0) {
        if (!b0)
            eb_free(b);
        return NULL;
    }

    set_read_handler(s->pty_fd, shell_read_cb, s);
    set_pid_handler(s->pid, shell_pid_cb, s);
    return b;
}

static void do_shell(EditState *s, int force)
{
    QEmacsState *qs = s->qe_state;
    EditState *e;
    EditBuffer *b;
    const char *argv[3];
    const char *shell_path;

    /* CG: Should prompt for buffer name if arg:
     * find a syntax for optional string argument w/ prompt
     */
    /* find shell buffer if any */
    if (!force || force == NO_ARG) {
        b = eb_find("*shell*");
        if (b) {
            e = edit_find(b);
            if (e)
                qs->active_window = e;
            else
                switch_to_buffer(s, b);
            return;
        }
    }

    /* find shell name */
    shell_path = getenv("SHELL");
    if (!shell_path)
        shell_path = "/bin/sh";

    /* create new buffer */
    argv[0] = shell_path;
    argv[1] = NULL;
    b = new_shell_buffer(NULL, "*shell*", shell_path, argv, 1);
    if (!b)
        return;

    switch_to_buffer(s, b);
    edit_set_mode(s, &shell_mode, NULL);

    put_status(s, "Press C-o to toggle between shell/edit mode");
    shell_launched = 1;
}

static void shell_move_left_right(EditState *e, int dir)
{
    if (e->interactive) {
        ShellState *s = e->b->priv_data;
        tty_write(s, dir > 0 ? s->kcuf1 : s->kcub1, -1);
    } else {
        text_move_left_right_visual(e, dir);
    }
}

static void shell_move_word_left_right(EditState *e, int dir)
{
    if (e->interactive) {
        ShellState *s = e->b->priv_data;
        tty_write(s, dir > 0 ? "\033f" : "\033b", -1);
    } else {
        text_move_word_left_right(e, dir);
    }
}

static void shell_move_up_down(EditState *e, int dir)
{
    if (e->interactive) {
        ShellState *s = e->b->priv_data;
        tty_write(s, dir > 0 ? s->kcud1 : s->kcuu1, -1);
    } else {
        text_move_up_down(e, dir);
    }
}

static void shell_scroll_up_down(EditState *e, int dir)
{
    ShellState *s = e->b->priv_data;

    e->interactive = 0;
    text_scroll_up_down(e, dir);
    e->interactive = (e->offset == s->cur_offset);
}

static void shell_move_bol(EditState *e)
{
    if (e->interactive) {
        ShellState *s = e->b->priv_data;
        tty_write(s, "\001", 1); /* Control-A */
    } else {
        text_move_bol(e);
    }
}

static void shell_move_eol(EditState *e)
{
    if (e->interactive) {
        ShellState *s = e->b->priv_data;
        tty_write(s, "\005", 1); /* Control-E */
    } else {
        text_move_eol(e);
    }
}

static void shell_write_char(EditState *e, int c)
{
    char ch;

    if (e->interactive) {
        ShellState *s = e->b->priv_data;

        ch = c;
        /* TODO: convert to tty escape sequences? */
        tty_write(s, &ch, 1);
    } else {
        /* Should dispatch as in fundamental mode */
        switch (c) {
        case 4:
            do_delete_char(e, NO_ARG);
            break;
        // Do not do this: it is useless and causes infinite recursion
        //case 9:
        //    do_tab(e, 1);
        //    break;
        case 11:
            do_kill_line(e, 1);
            break;
        case 127:
            do_backspace(e, NO_ARG);
            break;
        case '\r':
            do_return(e, 1);
            break;
        default:
            text_write_char(e, c);
            break;
        }
    }
}

static void do_shell_toggle_input(EditState *e)
{
    e->interactive = !e->interactive;
    if (e->interactive) {
        ShellState *s = e->b->priv_data;
        if (s->grab_keys)
            qe_grab_keys(shell_key, s);
    }
#if 0
    if (e->interactive) {
        ShellState *s = e->b->priv_data;
        tty_update_cursor(s);
    }
#endif
}

/* CG: these variables should move to mode structure */
static int error_offset = -1;
static int last_line_num = -1;
static char last_filename[MAX_FILENAME_SIZE];

static void do_compile(EditState *e, const char *cmd)
{
    const char *argv[4];
    EditBuffer *b;

    /* if the buffer already exists, kill it */
    b = eb_find("*compilation*");
    if (b) {
        /* XXX: e should not become invalid */
        b->modified = 0;
        do_kill_buffer(e, "*compilation*");
    }

    error_offset = -1;
    last_line_num = -1;

    /* create new buffer */
    argv[0] = "/bin/sh";
    argv[1] = "-c";
    argv[2] = (char *)cmd;
    argv[3] = NULL;
    b = new_shell_buffer(NULL, "*compilation*", "/bin/sh", argv, 0);
    if (!b)
        return;

    /* XXX: try to split window if necessary */
    switch_to_buffer(e, b);
}

static void do_compile_error(EditState *s, int dir)
{
    QEmacsState *qs = s->qe_state;
    EditState *e;
    EditBuffer *b;
    int offset, found_offset;
    char filename[MAX_FILENAME_SIZE], *q;
    int line_num, c;

    /* CG: should have a buffer flag for error source.
     * first check if current buffer is an error source.
     * if not, then scan for appropriate error source
     * in buffer least recently used order
     */

    if ((b = eb_find("*compilation*")) == NULL
    &&  (b = eb_find("*shell*")) == NULL
    &&  (b = eb_find("*errors*")) == NULL) {
        put_status(s, "No compilation buffer");
        return;
    }
    /* find next/prev error */
    offset = error_offset;
    if (offset < 0) {
        offset = 0;
        goto find_error;
    }
    for (;;) {
        if (dir > 0) {
            if (offset >= b->total_size) {
                put_status(s, "No more errors");
                return;
            }
            offset = eb_next_line(b, offset);
        } else {
            if (offset <= 0) {
                put_status(s, "No previous error");
                return;
            }
            offset = eb_prev_line(b, offset);
        }
    find_error:
        found_offset = offset;
        /* extract filename */
        q = filename;
        for (;;) {
            c = eb_nextc(b, offset, &offset);
            if (c == '\n' || c == '\t' || c == ' ')
                goto next_line;
            if (c == ':')
                break;
            if ((q - filename) < ssizeof(filename) - 1)
                *q++ = c;
        }
        *q = '\0';
        /* extract line number */
        line_num = 0;
        for (;;) {
            c = eb_nextc(b, offset, &offset);
            if (c == ':')
                break;
            if (!qe_isdigit(c))
                goto next_line;
            line_num = line_num * 10 + c - '0';
        }
        if (line_num >= 1) {
            if (line_num != last_line_num ||
                !strequal(filename, last_filename)) {
                last_line_num = line_num;
                pstrcpy(last_filename, sizeof(last_filename), filename);
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
    do_goto_line(s, line_num);
}

/* specific shell commands */
static CmdDef shell_commands[] = {
    CMD0( KEY_CTRL('o'), KEY_NONE,
          "shell-toggle-input", do_shell_toggle_input)
    CMD1( '\r', KEY_NONE,
          "shell-return", shell_write_char, '\r')
    /* CG: should send s->kbs */
    CMD1( 127, KEY_NONE,
          "shell-backward-delete-char", shell_write_char, 127)
    CMD1( KEY_CTRL('c'), KEY_NONE,
          "shell-intr", shell_write_char, 3)
    CMD1( KEY_CTRL('d'), KEY_DELETE,
          "shell-delete-char", shell_write_char, 4)
    CMD1( KEY_CTRL('i'), KEY_NONE,
          "shell-tabulate", shell_write_char, 9)
    CMD1( KEY_CTRL('k'), KEY_NONE,
          "shell-kill-line", shell_write_char, 11)
    CMD1( KEY_CTRL('y'), KEY_NONE,
          "shell-yank", shell_write_char, 25)
    CMD_DEF_END,
};

/* compilation commands */
static CmdDef compile_commands[] = {
    CMD_( KEY_CTRLXRET('\r'), KEY_NONE,
          "shell", do_shell, ESi, "ui")
    CMD_( KEY_CTRLX(KEY_CTRL('e')), KEY_NONE,
          "compile", do_compile, ESs,
          "s{Compile command: }|compile|")
    CMD1( KEY_CTRLX(KEY_CTRL('p')), KEY_NONE,
          "previous-error", do_compile_error, -1) /* u */
    CMD1( KEY_CTRLX(KEY_CTRL('n')), KEY_CTRLX('`'),
          "next-error", do_compile_error, 1) /* u */
    CMD_DEF_END,
};

static int shell_init(void)
{
    /* first register mode */
    memcpy(&shell_mode, &text_mode, sizeof(ModeDef));
    shell_mode.name = "shell";
    shell_mode.mode_probe = NULL;
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

    /* commands and default keys */
    qe_register_cmd_table(shell_commands, &shell_mode);
    qe_register_cmd_table(compile_commands, NULL);

    return 0;
}

qe_module_init(shell_init);
