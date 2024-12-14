/*
 * Shell mode for QEmacs.
 *
 * Copyright (c) 2001-2002 Fabrice Bellard.
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
/* XXX: send real cursor position (CSI n) */

static ModeDef shell_mode, pager_mode;

#define MAX_CSI_PARAMS  16

enum QETermState {
    QE_TERM_STATE_NORM,
    QE_TERM_STATE_UTF8,
    QE_TERM_STATE_ESC,
    QE_TERM_STATE_ESC2,
    QE_TERM_STATE_CSI,
    QE_TERM_STATE_OSC1,
    QE_TERM_STATE_STRING,
};

typedef struct ShellState {
    QEModeData base;
    /* buffer state */
    int cols, rows;
    int use_alternate_screen;
    int screen_top, alternate_screen_top;
    int scroll_top, scroll_bottom;  /* scroll region (top included, bottom excluded) */
    int pty_fd;
    int pid; /* -1 if not launched */
    unsigned int attr, fgcolor, bgcolor, reverse;
    int cur_offset; /* current offset at position x, y */
    int cur_offset_hack; /* the target position is in the middle of a wide glyph */
    int cur_prompt; /* offset of end of prompt on current line */
    int save_x, save_y;
    int nb_params;
#define CSI_PARAM_OMITTED  0x80000000
    int params[MAX_CSI_PARAMS + 1];
    int state;
    int esc1, esc2;
    char32_t lastc;
    int shifted;
    int cset, charset[2];
    int grab_keys;      // XXX: should detect raw mode instead of relying on alternate_screen
    unsigned char term_buf[256];
    int term_len, term_pos;
    int utf8_len;
    EditBuffer *b;
    EditBuffer *b_color; /* color buffer, one byte per char */
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
    int last_char;  /* last char sent to the process */
    char curpath[MAX_FILENAME_SIZE]; /* should keep a list with validity ranges */
} ShellState;

/* CG: these variables should be encapsulated in a global structure */
static char error_buffer[MAX_BUFFERNAME_SIZE];
static int error_offset = -1;
static int error_line_num = -1;
static int error_col_num = -1;
static char error_filename[MAX_FILENAME_SIZE];

#define SR_UPDATE_SIZE  1
#define SR_REFRESH      2
#define SR_SILENT       4
static void do_shell_refresh(EditState *e, int flags);
static char *shell_get_curpath(EditBuffer *b, int offset,
                               char *buf, int buf_size);

static void set_error_offset(EditBuffer *b, int offset)
{
    pstrcpy(error_buffer, sizeof(error_buffer), b ? b->name : "");
    error_offset = offset - 1;
    error_line_num = error_col_num = -1;
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
    fd = posix_openpt(O_RDWR | O_NOCTTY);
    //fd = open("/dev/ptmx", O_RDWR);
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

#define QE_TERM_XSIZE  80
#define QE_TERM_YSIZE  25
#define QE_TERM_YSIZE_INFINITE  10000

static int run_process(ShellState *s,
                       const char *cmd, int *fd_ptr, int *pid_ptr,
                       int cols, int rows, const char *path,
                       int shell_flags)
{
    int pty_fd, pid, i, nb_fds;
    char tty_name[MAX_FILENAME_SIZE];
    char lines_string[20];
    char columns_string[20];
    struct winsize ws;

    pty_fd = get_pty(tty_name, sizeof(tty_name));
    if (pty_fd < 0) {
        put_error(s->b->qs->active_window, "run_process: cannot get tty: %s",
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
        put_error(s->b->qs->active_window, "run_process: cannot fork");
        return -1;
    }
    if (pid == 0) {
        /* child process */
        const char *argv[4];
        char qelevel[16];
        char *vp;
        int argc = 0;
        int fd0, fd1, fd2;

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

        /* open pseudo tty for standard I/O */
        if (shell_flags & SF_INTERACTIVE) {
            /* interactive shell: input from / output to pseudo terminal */
            fd0 = open(tty_name, O_RDWR);
            fd1 = dup(0);
            fd2 = dup(0);
        } else {
            /* collect output from non interactive process: no input */
            fd0 = open("/dev/null", O_RDONLY);
            fd1 = open(tty_name, O_RDWR);
            fd2 = dup(1);
        }
        if (fd0 != 0 || fd1 != 1 || fd2 != 2) {
            setenv("QESTATUS", "invalid handles", 1);
        }
#ifdef CONFIG_DARWIN
        setsid();
#endif
        if (shell_flags & SF_INFINITE) {
            rows += QE_TERM_YSIZE_INFINITE;
        }
        snprintf(lines_string, sizeof lines_string, "%d", rows);
        snprintf(columns_string, sizeof columns_string, "%d", cols);

        // XXX: should prevent less from paging:
        //      update the terminal size because
        //         ioctl TIOCGWINSZ or WIOCGETD take precedence
        //         over LINES and COLUMNS in linux
        //      MANWIDTH overrides COLUMNS and ioctl stuff?
        //      MAN_KEEP_FORMATING count be used
        //      "PAGER=less -E -z1000" does not seem to work
        //      "LESS=-E -z1000" does not work either
        setenv("LINES", lines_string, 1);
        setenv("COLUMNS", columns_string, 1);
        setenv("TERM", "xterm-256color", 1);
        setenv("TERM_PROGRAM", "qemacs", 1);
        setenv("TERM_PROGRAM_VERSION", str_version, 1);
        unsetenv("PAGER");
        vp = getenv("QELEVEL");
        snprintf(qelevel, sizeof qelevel, "%d", 1 + (vp ? atoi(vp) : 0));
        setenv("QELEVEL", qelevel, 1);

        if (path) {
            if (chdir(path)) {
                setenv("QESTATUS", "cannot chdir", 1);
            }
        }

        execv(argv[0], unconst(char * const *)argv);
        exit(1);
    }
    /* return file info */
    *fd_ptr = pty_fd;
    *pid_ptr = pid;
    return 0;
}

/* VT100 emulation */

static void qe_trace_term(ShellState *s, const char *msg) {
    QEmacsState *qs = s->base.qs;
    qe_trace_bytes(qs, msg, -1, EB_TRACE_FLUSH | EB_TRACE_EMULATE);
    qe_trace_bytes(qs, ": ", -1, EB_TRACE_EMULATE);
    qe_trace_bytes(qs, s->term_buf, s->term_len, EB_TRACE_EMULATE);
}

#define TRACE_MSG(s, m)  qe_trace_term(s, m)
#define TRACE_PRINTF(s, ...)  do { \
    if (s->base.qs->trace_buffer) { \
        if (s->base.qs->trace_buffer_state) \
            eb_putc(s->base.qs->trace_buffer, '\n'); \
        eb_printf(s->base.qs->trace_buffer, __VA_ARGS__); \
        s->base.qs->trace_buffer_state = 0; \
    } \
} while(0)

static void qe_term_init(ShellState *s)
{
    char *term;

    s->state = QE_TERM_STATE_NORM;
    /* Should compute def_color from shell default style at display
     * time and force full redisplay upon style change.
     */
    s->fgcolor = QE_TERM_DEF_FG;
    s->bgcolor = QE_TERM_DEF_BG;
    s->attr = 0;
    s->reverse = 0;
    s->lastc = ' ';

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

// XXX: should use an auxiliary buffer to make this asynchous
static void qe_term_write(ShellState *s, const char *buf, int len)
{
    int ret;

    if (len < 0)
        len = strlen(buf);

    if (s->base.qs->trace_buffer)
        qe_trace_bytes(s->base.qs, buf, len, EB_TRACE_PTY);

    while (len > 0) {
        ret = write(s->pty_fd, buf, len);
        if (ret == -1 && (errno == EAGAIN || errno == EINTR))
            continue;
        if (ret <= 0)
            break;
        buf += ret;
        len -= ret;
        s->last_char = buf[-1];
    }
}

static inline void qe_term_set_style(ShellState *s) {
    QETermStyle composite_color;

    if (s->reverse) {
        composite_color = QE_TERM_MAKE_COLOR(s->bgcolor, s->fgcolor);
    } else {
        composite_color = QE_TERM_MAKE_COLOR(s->fgcolor, s->bgcolor);
    }
    s->b->cur_style = QE_TERM_COMPOSITE | s->attr | composite_color;
}

/* return offset of the n-th terminal line from a given offset */
static int qe_term_skip_lines(ShellState *s, int offset, int n) {
    int x, y, w, offset1, offset2;
    x = y = 0;
    while (y < n && offset < s->b->total_size) {
        char32_t c = eb_nextc(s->b, offset, &offset1);
        if (c == '\n') {
            y++;
            x = 0;
        } else
        if (c == '\t') {
            w = (x + 8) & ~7;
            /* TAB at EOL does not move the cursor */
            x = min_int(x + w, s->cols - 1);
        } else {
            w = qe_wcwidth(c);
            x += w;
            if (x >= s->cols) {
                /* handle line wrapping */
                if (x > s->cols) {
                    /* wide character at EOL actually wraps to next line */
                    y++;
                    if (y == n)
                        break;
                    x = w;
                } else {
                    /* aggregate all accents */
                    while (qe_isaccent(c = eb_nextc(s->b, offset1, &offset2)))
                        offset1 = offset2;
                    if (c != '\n') {
                        /* character is at end of line, next character wraps to next line */
                        y++;
                        x = 0;
                    }
                }
            }
        }
        offset = offset1;
    }
    return offset;
}

typedef struct ShellPos {
    int screen_start; /* offset of the start of row 0 */
    int line_start; /* offset of the start of current row */
    int offset;     /* offset of the glyph */
    int line_end;   /* offset of the newline or the first character that wraps */
    int row;        /* row of the target offset */
    int col;        /* column of the target (0 based, newline may have col == s->cols) */
    int end_col;    /* column of the end of line_end */
    int flags;
#define SP_SCREEN_START_WRAP  1
#define SP_LINE_START_WRAP1   2
#define SP_LINE_START_WRAP2   4
#define SP_LINE_START_WRAP    6
#define SP_LINE_END_WRAP1     8
#define SP_LINE_END_WRAP2     16
#define SP_LINE_END_WRAP      24
} ShellPos;

#define SP_NO_UPDATE  1
static int qe_term_get_pos2(ShellState *s, int destoffset, ShellPos *spp, int flags) {
    int offset, offset0, offset1, start_offset, line_offset;
    int x, y, w, gpflags;
    char32_t c;

    if (s->use_alternate_screen) {
        start_offset = s->alternate_screen_top = min_offset(s->alternate_screen_top, s->b->total_size);
    } else {
        start_offset = s->screen_top = min_offset(s->screen_top, s->b->total_size);
    }
    if (spp) {
        gpflags = 0;
        destoffset = clamp_offset(destoffset, 0, s->b->total_size);
        offset = offset0 = line_offset = start_offset;
        for (x = y = 0; offset < destoffset;) {
            offset0 = offset;
            c = eb_nextc(s->b, offset, &offset);
            if (c == '\n') {
                y++;
                x = 0;
                gpflags &= ~SP_LINE_START_WRAP;
                line_offset = offset;
            } else
            if (c == '\t') {
                w = (x + 8) & ~7;
                /* TAB at EOL does not move the cursor */
                x = min_int(x + w, s->cols - 1);
            } else {
                w = qe_wcwidth(c);
                x += w;
                if (x >= s->cols) {
                    /* handle line wrapping */
                    if (x > s->cols) {
                        /* wide character at EOL actually wraps to next line */
                        y++;
                        x = w;
                        gpflags |= SP_LINE_START_WRAP2;
                        line_offset = offset;
                    } else {
                        /* aggregate all accents */
                        while (qe_isaccent(c = eb_nextc(s->b, offset, &offset1)))
                            offset = offset1;
                        if (c != '\n') {
                            y++;
                            x = 0;
                            gpflags |= SP_LINE_START_WRAP1;
                            line_offset = offset;
                        }
                    }
                }
            }
        }
        if (x >= s->cols - 1 && offset == destoffset) {
            /* check if current glyph causes line wrap */
            c = eb_nextc(s->b, offset, &offset1);
            if (c != '\n' && x + qe_wcwidth(c) > s->cols) {
                y++;
                x = 0;
                gpflags |= SP_LINE_START_WRAP1;
                line_offset = offset;
            }
        }
        if (y >= s->rows && !(flags & SP_NO_UPDATE)) {
            /* adjust start if row is too far */
            start_offset = qe_term_skip_lines(s, start_offset, y - s->rows + 1);
            y = s->rows - 1;
            /* update screen_top */
            if (s->use_alternate_screen)
                s->alternate_screen_top = start_offset;
            else
                s->screen_top = start_offset;
        }
        if (eb_prevc(s->b, start_offset, &offset1) != '\n')
            gpflags |= SP_SCREEN_START_WRAP;
        spp->col = x;
        spp->row = y;
        spp->line_start = line_offset;
        spp->screen_start = start_offset;
        spp->offset = offset;
        if (s->cur_offset_hack && offset == s->cur_offset) {
            c = eb_nextc(s->b, offset, &offset1);
            if (qe_iswide(c))
                spp->col += 1;
        }
        //TRACE_PRINTF(s, "qe_term_get_pos2 -> x=%d, y=%d\n", spp->col, y);
        for (;;) {
            offset0 = offset;
            c = eb_nextc(s->b, offset, &offset);
            if (c == '\n')
                break;
            if (c == '\t') {
                w = (x + 8) & ~7;
                x = min_int(x + w, s->cols - 1);
            } else {
                w = qe_wcwidth(c);
                x += w;
                if (x >= s->cols) {
                    if (x > s->cols) {
                        x -= w;
                        gpflags |= SP_LINE_END_WRAP2;
                        break;
                    }
                    /* aggregate all accents */
                    while (qe_isaccent(c = eb_nextc(s->b, offset, &offset1)))
                        offset = offset1;
                    if (c != '\n') {
                        offset0 = offset;
                        gpflags |= SP_LINE_END_WRAP1;
                        break;
                    }
                }
            }
        }
        spp->flags = gpflags;
        spp->end_col = x;
        spp->line_end = offset0;
    }
    return start_offset;
}

static int qe_term_get_pos(ShellState *s, int destoffset, int *px, int *py) {
    int offset, offset1;
    int x, y, w, start_offset;
    char32_t c;

    if (s->use_alternate_screen) {
        start_offset = s->alternate_screen_top = min_offset(s->alternate_screen_top, s->b->total_size);
    } else {
        start_offset = s->screen_top = min_offset(s->screen_top, s->b->total_size);
    }
    if (px || py) {
        destoffset = clamp_offset(destoffset, 0, s->b->total_size);
        offset = start_offset;
        for (x = y = 0; offset < destoffset;) {
            c = eb_nextc(s->b, offset, &offset);
            if (c == '\n') {
                y++;
                x = 0;
            } else
            if (c == '\t') {
                w = (x + 8) & ~7;
                /* TAB at EOL does not move the cursor */
                x = min_int(x + w, s->cols - 1);
            } else {
                w = qe_wcwidth(c);
                x += w;
                if (x >= s->cols) {
                    /* handle line wrapping */
                    if (x > s->cols) {
                        /* wide character at EOL actually wraps to next line */
                        y++;
                        x = w;
                    } else {
                        /* aggregate all accents */
                        while (qe_isaccent(c = eb_nextc(s->b, offset, &offset1)))
                            offset = offset1;
                        if (c != '\n') {
                            y++;
                            x = 0;
                        }
                    }
                }
            }
        }
        if (x >= s->cols - 1 && offset == destoffset) {
            /* check if current glyph causes line wrap */
            c = eb_nextc(s->b, offset, &offset1);
            if (c != '\n' && x + qe_wcwidth(c) > s->cols) {
                y++;
                x = 0;
            }
        }
        if (y >= s->rows) {
            // XXX: should take a flag to make this optional
            /* adjust start if row is too far */
            start_offset = qe_term_skip_lines(s, start_offset, y - s->rows + 1);
            y = s->rows - 1;
            /* update screen_top */
            if (s->use_alternate_screen)
                s->alternate_screen_top = start_offset;
            else
                s->screen_top = start_offset;
        }
        if (s->cur_offset_hack && offset == s->cur_offset) {
            c = eb_nextc(s->b, offset, &offset1);
            if (qe_iswide(c))
                x += 1;
        }
        if (px) *px = x;
        if (py) *py = y;
        //TRACE_PRINTF(s, "qe_term_get_pos -> x=%d, y=%d\n", x, y);
    }
    return start_offset;
}

/* Compute offset of the char at column x and row y (0 based).
 * Can insert spaces or newlines if needed.
 * x and y may each be relative to the current position.
 */
#define TG_RELATIVE_COL  0x01
#define TG_RELATIVE_ROW  0x02
#define TG_RELATIVE      0x03
#define TG_NOCLIP        0x04
#define TG_NOEXTEND      0x08
static int qe_term_goto_pos(ShellState *s, int offset, int destx, int desty, int flags) {
    int x, y, w, x1, y1, start_offset, offset1, offset2;
    char32_t c;

    s->cur_offset_hack = 0;

    if (flags & TG_RELATIVE) {
        start_offset = qe_term_get_pos(s, offset, &x, &y);
        if (flags & TG_RELATIVE_COL)
            destx += x;
        if (flags & TG_RELATIVE_ROW)
            desty += y;
    } else {
        start_offset = qe_term_get_pos(s, offset, NULL, NULL);
    }
    if (desty < 0 || desty >= QE_TERM_YSIZE_INFINITE - 1)
        desty = 0;
    else
    if (desty >= s->rows && !(flags & TG_NOCLIP))
        desty = s->rows - 1;
    if (destx < 0)
        destx = 0;
    else
    if (destx >= s->cols && !(flags & TG_NOCLIP))
        destx = s->cols;

    //TRACE_PRINTF(s, "goto col=%d row=%d flags=%d\n", destx, desty, flags);

    x = y = 0;
    offset = start_offset;
    while (y < desty || x < destx) {
        if (offset >= s->b->total_size) {
            // XXX: inefficient: should only test if '\n'
            offset = s->b->total_size;
            if (flags & TG_NOEXTEND)
                break;
            /* XXX: color may be wrong */
            s->b->cur_style = QE_STYLE_DEFAULT;
            //qe_term_set_style(s);
            if (y < desty) {
                // XXX: potential problem if previous line has s->cols characters
                offset += eb_insert_char32_n(s->b, offset, '\n', desty - y);
                y = desty;
                x = 0;
                /* update current screen_top */
                qe_term_get_pos(s, offset, &x1, &y1);
            }
            if (x < destx) {
                offset += eb_insert_spaces(s->b, offset, destx - x);
                x = destx;
            }
            break;
        } else {
            c = eb_nextc(s->b, offset, &offset1);
            if (c == '\n') {
                if (y < desty) {
                    y++;
                    x = 0;
                    offset = offset1;
                } else {
                    if (flags & TG_NOEXTEND)
                        break;
                    s->b->cur_style = QE_STYLE_DEFAULT;
                    offset += eb_insert_spaces(s->b, offset, destx - x);
                    x = destx;
                }
            } else
            if (c == '\t') {
                w = (x + 8) & ~7;
                /* TAB at EOL does not move the cursor */
                x1 = min_int(x + w, s->cols - 1);
                if (y == desty && x1 > destx) {
                    /* expand TAB if destination falls in the middle */
                    if (flags & TG_NOEXTEND)
                        break;
                    eb_delete_range(s->b, offset, offset1);
                    eb_insert_spaces(s->b, offset, x1 - x);
                    continue;
                }
                x = x1;
                offset = offset1;
            } else {
                /* if destination x is in the middle of a wide character,
                   actual x will be too far */
                w = qe_wcwidth(c);
                // XXX: this test is inefficient, should first skip desty rows
                //      then skip destx columns
                if (w > 1 && y == desty && x + w > destx) {
                    /* positioning request in the middle of a wide character */
                    s->cur_offset_hack = 1;
                    break;
                }
                x += w;
                if (x >= s->cols) {
                    /* handle line wrapping */
                    if (y == desty) {
                        if (x == destx) {
                            offset = offset1;
                            break;
                        }
                        break;
                    }
                    if (x > s->cols) {
                        x = 0;
                        y++;
                        continue;
                    }
                    /* aggregate all accents */
                    while (qe_isaccent(c = eb_nextc(s->b, offset1, &offset2)))
                        offset1 = offset2;
                    if (c != '\n') {
                        x = 0;
                        y++;
                    }
                }
                offset = offset1;
            }
        }
    }
    return eb_skip_accents(s->b, offset);
}

static void qe_term_goto_xy(ShellState *s, int destx, int desty, int flags) {
    s->cur_offset = qe_term_goto_pos(s, s->cur_offset, destx, desty, flags);
}

static void qe_term_goto_tab(ShellState *s, int n) {
    /* assuming tab stops every 8 positions */
    int x, y, col_num;
    qe_term_get_pos(s, s->cur_offset, &x, &y);
    col_num = max_int(0, x + n * 8) & ~7;
    if (col_num >= s->cols) {
        /* handle wrapping lines */
        if (x < s->cols)
            col_num = s->cols - 1;
        else
            col_num = s->cols;
    }
    qe_term_goto_xy(s, col_num, y, 0);
}

/* Overwrite the current contents with an encoded glyph
 * of width w.
 * Must replace overwritten wide glyphs with spaces
 */
static int qe_term_overwrite(ShellState *s, int offset, int w,
                             const char *buf, int len)
{
    int offset1, offset2;
    int w1, x, y, x1;
    char32_t c1, c2;

    // XXX: bypass all these tests if at end of buffer?
    c1 = eb_nextc(s->b, offset, &offset1);
    if (c1 == '\n' && offset1 > offset) {
        qe_term_get_pos(s, offset, &x, &y);
        if (x + w > s->cols) {
            /* the inserted character will wrap:
             * fuse the lines and overwrite at next BOL
             */
            eb_delete_range(s->b, offset, offset1);
            c1 = eb_nextc(s->b, offset, &offset1);
        }
    }
    qe_term_set_style(s);
    if (c1 == '\n') {
        /* insert */
        eb_insert(s->b, offset, buf, len);
    } else {
        if (c1 == '\t') {
            /* expand TAB */
            qe_term_get_pos(s, offset, &x, &y);
            eb_delete_range(s->b, offset, offset1);
            w1 = (x + 8) & ~7;
            x1 = min_int(x + w1, s->cols - 1);
            if (x1 > x) {
                eb_insert_spaces(s->b, offset, x1 - x);
                c1 = eb_nextc(s->b, offset, &offset1);
            }
        }
        offset1 = eb_skip_accents(s->b, offset1);
        if (qe_iswide(c1)) {
            /* must test here before buffer modifications change s->cur_offset */
            int has_hack = s->cur_offset_hack && offset == s->cur_offset;
            eb_delete_range(s->b, offset, offset1);
            offset1 = offset;
            c2 = eb_nextc(s->b, offset, &offset2);
            if (c2 != '\n' || has_hack) {
                offset1 += eb_insert_char32(s->b, offset1, ' ');
                offset2 = offset1;
                if (c2 != '\n')
                    offset2 += eb_insert_char32(s->b, offset2, ' ');
                if (has_hack) {
                    s->cur_offset_hack = 0;
                    offset = offset1;
                    offset1 = offset2;
                }
            }
        }
        if (w > 1) {
            /* overwriting 2 positions: potentially convert a second wide glyph */
            c2 = eb_nextc(s->b, offset1, &offset2);
            if (c2 == '\n') {
                /* no adjustment needed */
            } else
            if (c2 == '\t') {
                // XXX: should expand TAB
            } else {
                offset2 = eb_skip_accents(s->b, offset2);
                if (qe_iswide(c2)) {
                    eb_delete_range(s->b, offset1, offset2);
                    if (eb_nextc(s->b, offset1, &offset2) != '\n') {
                        offset1 += eb_insert_char32(s->b, offset1, ' ');
                        eb_insert_char32(s->b, offset1, ' ');
                    }
                } else {
                    offset1 = offset2;
                }
            }
        }
        /* check for buffer content change is not an advisable optimisation
         * because re-writing the same character may cause color changes.
         */
        if (offset1 - offset == len) {
            eb_write(s->b, offset, buf, len);
        } else {
            eb_delete_range(s->b, offset, offset1);
            eb_insert(s->b, offset, buf, len);
        }
    }
    return offset + len;
}

static int qe_term_delete_lines(ShellState *s, int offset, int n)
{
    int i, offset1, offset2;

    // XXX: should scan buffer contents to handle line wrapping
    // XXX: should insert a newline if offset is inside a wrapping line
    // XXX: should update screen_top if on row 0
    if (n > 0) {
        for (offset1 = offset, i = 0; i < n; i++) {
            offset1 = eb_next_line(s->b, offset1);
        }
        if (eb_prevc(s->b, offset1, &offset2) != '\n')
            eb_prevc(s->b, offset, &offset);
        eb_delete_range(s->b, offset, offset1);
    }
    return offset;
}

static int qe_term_insert_lines(ShellState *s, int offset, int n)
{
    if (n > 0) {
        // XXX: tricky if offset is in the middle of a wrapping line
        offset += eb_insert_char32_n(s->b, offset, '\n', n);
    }
    return offset;
}

#if QE_TERM_FG_COLORS < 256
#define MAP_FG_COLOR(color)  qe_map_color(xterm_colors[color], xterm_colors, QE_TERM_FG_COLORS, NULL)
#else
#define MAP_FG_COLOR(color)  (color)
#endif
#if QE_TERM_BG_COLORS < 256
#define MAP_BG_COLOR(color)  qe_map_color(xterm_colors[color], xterm_colors, QE_TERM_BG_COLORS, NULL)
#else
#define MAP_BG_COLOR(color)  (color)
#endif

static int qe_term_csi_m(ShellState *s, const int *params, int count)
{
    int c = *params;

    /* Comment from putty/terminal.c:
     *
     * A VT100 without the AVO only had one attribute, either underline or
     * reverse video depending on the cursor type, this was selected by
     * CSI 7m.
     *
     * case 2:
     *  This is sometimes DIM, eg on the GIGI and Linux (aka FAINT in iTerm2)
     * case 8:
     *  This is sometimes INVIS various ANSI.
     * case 21:
     *  This like 22 disables BOLD, DIM and INVIS
     *
     * The ANSI colours appear on any terminal that has colour (obviously)
     * but the interaction between sgr0 and the colours varies but is usually
     * related to the background colour erase item. The interaction between
     * colour attributes and the mono ones is also very implementation
     * dependent.
     *
     * The 39 and 49 attributes are likely to be unimplemented.
     */
    switch (c) {
    case CSI_PARAM_OMITTED:
    case 0:     /* Normal (default). [exit_attribute_mode] */
        s->fgcolor = QE_TERM_DEF_FG;
        s->bgcolor = QE_TERM_DEF_BG;
        s->reverse = 0;
        s->attr = 0;
        break;
    case 1:     /* Bold. [enter_bold_mode] */
        s->attr |= QE_TERM_BOLD;
        break;
    case 2:     /* Faint, decreased intensity (ISO 6429). */
        goto unhandled;
    case 3:     /* Italicized (ISO 6429). [enter_italic_mode] */
        s->attr |= QE_TERM_ITALIC;
        break;
    case 4:     /* Underlined. [enter_underline_mode] */
        s->attr |= QE_TERM_UNDERLINE;
        break;
    case 5:     /* Blink (appears as Bold). [enter_blink_mode] */
        s->attr |= QE_TERM_BLINK;
        break;
    case 6:     /* SCO light background, ANSI.SYS blink rapid */
        goto unhandled;
    case 7:     /* Inverse. [enter_reverse_mode, enter_standout_mode] */
        s->reverse = 1;
        break;
    case 8:     /* Invisible, i.e., hidden (VT300). enter_secure_mode */
    case 9:     /* Crossed-out characters (ISO 6429). cygwin dim mode */
    case 10:    /* SCO acs off, ANSI default font */
    case 11:    /* SCO acs on (CP437) */
    case 12:    /* SCO acs on, |0x80 */
    case 13: case 14: case 15:
    case 16: case 17: case 18: case 19:  /* 11-19: nth alternate font */
    case 20:    /* Fraktur */
        goto unhandled;
    case 21:    /* Doubly-underlined (ISO 6429). Bold off sometimes */
        goto unhandled;
    case 22:    /* Normal (neither bold nor faint). [exit_bold_mode] */
        s->attr &= ~QE_TERM_BOLD;
        break;
    case 23:    /* Not italicized (ISO 6429). Not Fraktur. [exit_italic_mode] */
        s->attr &= ~QE_TERM_ITALIC;
        break;
    case 24:    /* Not underlined. [exit_underline_mode] */
        s->attr &= ~QE_TERM_UNDERLINE;
        break;
    case 25:    /* Steady (not blinking). [exit_blink_mode] */
        s->attr &= ~QE_TERM_BLINK;
        break;
    case 26:    /* reserved */
        goto unhandled;
    case 27:    /* Positive (not inverse). [exit_reverse_mode, exit_standout_mode] */
        s->reverse = 0;
        break;
    case 28:    /* Visible, i.e., not hidden (VT300). [exit_secure_mode] */
    case 29:    /* Not crossed-out (ISO 6429). */
        goto unhandled;
    case 30: case 31: case 32: case 33:
    case 34: case 35: case 36: case 37:
        /* set foreground color */
        /* 0:black 1:red 2:green 3:yellow 4:blue 5:magenta 6:cyan 7:white */
        /* XXX: should distinguish system colors and palette colors */
        // FIXME: should map the color if it has been redefined only?
        s->fgcolor = MAP_FG_COLOR(c - 30);
        break;
    case 38:    /* set extended foreground color (ISO-8613-3) */
        // First subparam means:   # additional subparams:  Accepts optional params:
        // 1: transparent          0                        NO
        // 2: RGB                  3                        YES
        // 3: CMY                  3                        YES
        // 4: CMYK                 4                        YES
        // 5: Indexed color        1                        NO
        //
        // Optional paramters go at position 7 and 8, and indicate toleranace as an
        // integer; and color space (0=CIELUV, 1=CIELAB). Example:
        //
        // CSI 38:2:255:128:64:0:5:1 m
        //
        // Also accepted for xterm compatibility, but never with optional parameters:
        // CSI 38;2;255;128;64 m
        //
        // Set the foreground color to red=255, green=128, blue=64 with a tolerance of
        // 5 in the CIELAB color space. The 0 at the 6th position has no meaning and
        // is just a filler.
        //
        // For 256-color mode (indexed) use this for the foreground:
        // CSI 38;5;N m
        // where N is a value between 0 and 255. See the colors described in screen_char_t
        // in the comments for fgColorCode.

        if (count >= 3 && params[1] == 5) {
            /* set foreground color to third esc_param */
            /* complete syntax is \033[38;5;Nm where N is in range 0..255 */
            int color = clamp_int(params[2], 0, 255);

            /* map color to qe-term palette */
            // FIXME: should map the color if it has been redefined only?
            s->fgcolor = MAP_FG_COLOR(color);
            return 3;
        }
        if (count >= 5 && params[1] == 2) {
            /* set foreground color to 24-bit color */
            /* complete syntax is \033[38;2;r;g;bm where r,g,b are in 0..255 */
            /* ??? alternate syntax: ESC [ 3 8 : 2 : Pi : Pr : Pg : Pb m */
            QEColor rgb = QERGB25(clamp_int(params[2], 0, 255),
                                  clamp_int(params[3], 0, 255),
                                  clamp_int(params[4], 0, 255));

            /* map 24-bit colors to qe-term palette */
            s->fgcolor = qe_map_color(rgb, xterm_colors, QE_TERM_FG_COLORS, NULL);
            return 5;
        }
        return 2;
    case 39:    /* orig_pair(1) [default-foreground] */
        s->fgcolor = QE_TERM_DEF_FG;
        break;
    case 40: case 41: case 42: case 43:
    case 44: case 45: case 46: case 47:
        /* set background color */
        /* XXX: should distinguish system colors and palette colors */
        // FIXME: should map the color if it has been redefined only?
        s->bgcolor = MAP_BG_COLOR(c - 40);
        break;
    case 48:    /* set extended background color (ISO-8613-3) */
        if (count >= 3 && params[1] == 5) {
            /* set background color to third esc_param */
            /* complete syntax is \033[48;5;Nm where N is in range 0..255 */
            int color = clamp_int(params[2], 0, 255);

            /* map color to qe-term palette */
            // FIXME: should map the color if it has been redefined only?
            s->bgcolor = MAP_BG_COLOR(color);
            return 3;
        }
        if (count >= 5 && params[1] == 2) {
            /* set background color to 24-bit color */
            /* complete syntax is \033[48;2;r;g;bm where r,g,b are in 0..255 */
            QEColor rgb = QERGB25(clamp_int(params[2], 0, 255),
                                  clamp_int(params[3], 0, 255),
                                  clamp_int(params[4], 0, 255));

            /* map 24-bit colors to qe-term palette */
            s->bgcolor = qe_map_color(rgb, xterm_colors, QE_TERM_BG_COLORS, NULL);
            return 5;
        }
        return 2;
    case 49:    /* orig_pair(2) [default-background] */
        s->bgcolor = QE_TERM_DEF_BG;
        break;
    case 50:    /* Reserved */
    case 51:    /* Framed */
    case 52:    /* Encircled */
    case 53:    /* Overlined */
    case 54:    /* Not framed or encircled */
    case 55:    /* Not overlined */
    case 56: case 57: case 58: case 59: /* Reserved */
    case 60:    /* ideogram underline or right side line
                 * hardly ever supported */
    case 61:    /* ideogram double underline or double line on the right side
                 * hardly ever supported */
    case 62:    /* ideogram overline or left side line
                 * hardly ever supported */
    case 63:    /* ideogram double overline or double line on the left side
                 * hardly ever supported */
    case 64:    /* ideogram stress marking
                 * hardly ever supported */
    case 65:    /* ideogram attributes off, reset the effects of all of 60-64
                 * hardly ever supported */
        goto unhandled;
    case 90: case 91: case 92: case 93:
    case 94: case 95: case 96: case 97:
        /* Set foreground text color, high intensity, aixterm (not in standard) */
        /* XXX: should distinguish system colors and palette colors */
        // FIXME: should map the color if it has been redefined only?
        s->fgcolor = MAP_FG_COLOR(c - 90 + 8);
        break;
    case 100: case 101: case 102: case 103:
    case 104: case 105: case 106: case 107:
        /* Set background color, high intensity, aixterm (not in standard) */
        /* XXX: should distinguish system colors and palette colors */
        // FIXME: should map the color if it has been redefined only?
        s->bgcolor = MAP_BG_COLOR(c - 100 + 8);
        break;
    default:
    unhandled:
        TRACE_MSG(s, "unhandled SGR");
        break;
    }
    return 1;
}


/* Well, almost a hack to update cursor */
static void qe_term_update_cursor(qe__unused__ ShellState *s)
{
#if 0
    QEmacsState *qs = s->base.qs;
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

static inline ShellState *shell_get_state(EditState *e, int status)
{
    return qe_get_buffer_mode_data(e->b, &shell_mode, status ? e : NULL);
}

/* CG: much cleaner way! */
/* would need a kill hook as well ? */
static void shell_display_hook(EditState *e)
{
    ShellState *s;

    if (e->interactive) {
        if ((s = shell_get_state(e, 0)) != NULL)
            e->offset = s->cur_offset;
        if (s->use_alternate_screen)
            e->offset_top = s->alternate_screen_top;
    }
}

static void shell_key(void *opaque, int key)
{
    ShellState *s = opaque;
    char buf[10];
    const char *p;
    int len;

    if (!s || s->base.mode != &shell_mode)
        return;

    if (key == KEY_CTRL('o')) {
        qe_ungrab_keys(s->base.qs);
        qe_unget_key(s->base.qs, key);
        return;
    }
    p = buf;
    len = -1;
    switch (key) {
    case KEY_UP:        p = s->kcuu1; break;
    case KEY_DOWN:      p = s->kcud1; break;
    case KEY_RIGHT:     p = s->kcuf1; break;
    case KEY_LEFT:      p = s->kcub1; break;
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
        qe_term_write(s, p, len);
    }
}

static unsigned char const sco_color[16] = {
    0, 4, 2, 6, 1, 5, 3, 7, 8, 12, 10, 14, 9, 13, 11, 15,
};

static void qe_term_emulate(ShellState *s, int c)
{
    int i, param1, param2, len, offset, offset1, offset2;
    ShellPos pos;
    char buf1[10];

    offset = s->cur_offset = clamp_offset(s->cur_offset, 0, s->b->total_size);

    if (s->state == QE_TERM_STATE_NORM) {
        s->term_pos = 0;
    }
    if (s->term_pos < countof(s->term_buf)) {
        /* UTF-8 bytes are appended here (among other uses of the buffer) */
        s->term_buf[s->term_pos++] = c;
        s->term_len = s->term_pos;
    }

#define ESC2(c1,c2)  (((c1) << 8) | (unsigned char)(c2))
    /* some bytes are state independent */
    switch (c) {
    case 0x18:
    case 0x1A:
        s->state = QE_TERM_STATE_NORM;
        return;
    case 0x1B:
        s->state = QE_TERM_STATE_ESC;
        return;
#if 0
    case 0x9B:  /* incompatible with UTF-8 */
        goto csi_entry;
#endif
    }

    switch (s->state) {
    case QE_TERM_STATE_NORM:
        switch (c) {
        case 5:     /* ENQ  Return Terminal Status (Ctrl-E).
             * Default response is an empty string, but may be overridden
             * by a resource <b>answerbackString</b>.
             */
            break;
        case 7:     /* BEL  Bell (Ctrl-G). */
            // XXX: should check for visible-bell
            put_status(s->b->qs->active_window, "Ding!");
            break;
        case 8:     /* BS   Backspace (Ctrl-H). */
            //TRACE_PRINTF(s, "BS: ");
            qe_term_get_pos2(s, offset, &pos, 0);
            if (pos.col == 0) {
                if (pos.row > 0 && (pos.flags & SP_LINE_START_WRAP)) {
                    //char32_t c2 =
                    eb_prev_glyph(s->b, offset, &offset);
                    //if (qe_iswide(c2))
                    //    s->cur_offset_hack = 1;
                    s->cur_offset = offset;
                }
            } else {
                /* This is iTerm2's behavior */
                pos.col -= (pos.col >= s->cols);
                qe_term_goto_xy(s, pos.col - 1, pos.row, 0);
            }
            break;
        case 9:     /* HT   Horizontal Tab (TAB) (Ctrl-I). */
            qe_term_goto_tab(s, 1);
            break;
        case 10:    /* LF   Line Feed (Ctrl-J) or New Line (NL). */
        case 11:    /* VT   Vertical Tab (Ctrl-K).
                     *      VT treated the same as LF. */
        case 12:    /* FF   Form Feed (Ctrl-L) or New Page (NP).
                     *      FF is treated the same as LF. */
            //TRACE_PRINTF(s, "LF: ");
            if (s->use_alternate_screen) {
                qe_term_goto_xy(s, 0, 1, TG_RELATIVE | TG_NOCLIP);
            } else {
                /* go to next line */
                /* CG: should check if column should be kept in cooked mode */
                if (offset >= s->b->total_size) {
                    int x, y;
                    /* add a new line */
                    /* CG: XXX: ignoring charset */
                    qe_term_set_style(s);
                    offset += eb_insert_char32(s->b, offset, '\n');
                    s->cur_offset = offset;
                    /* update current screen_top */
                    qe_term_get_pos(s, offset, &x, &y);
                } else {
                    // XXX: test if cursor is on last row and append a newline
                    // XXX: potential style issue if appending a newline
                    qe_term_goto_xy(s, 0, 1, TG_RELATIVE | TG_NOCLIP);
                }
            }
            s->b->last_log = 0; /* close undo record */
            break;
        case 13:    /* CR   Carriage Return (Ctrl-M). */
            /* move to visual beginning of line */
            //TRACE_PRINTF(s, "CR: ");
            qe_term_goto_xy(s, 0, 0, TG_RELATIVE_ROW);
            break;
        case 14:    /* SO   Shift Out (Ctrl-N) ->
             * Switch to Alternate Character Set.  This
             * invokes the G1 character set. */
            s->shifted = s->charset[s->cset = 1];
            break;
        case 15:    /* SI   Shift In (Ctrl-O) ->
             * Switch to Standard Character Set.  This
             * invokes the G0 character set (the default). */
            s->shifted = s->charset[s->cset = 0];
            break;
        default:
            if (c >= 32) {
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
                        s->lastc = c = unitab_xterm_std[c - 96];
                        len = utf8_encode(buf1, c);
                    } else {
                        /* CG: quick 8 bit hack: store line drawing
                         * characters in [96..127] as meta control
                         * characters in [128..159].
                         * This hack is reversed in tty_term_flush().
                         */
                        c += 32;
                        buf1[0] = s->lastc = c;
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
                            s->state = QE_TERM_STATE_UTF8;
                            break;
                        }
                    }
                    //len = eb_encode_char32(s->b, buf1, c);
                    buf1[0] = s->lastc = c;
                    len = 1;
                }
                s->cur_offset = qe_term_overwrite(s, offset, 1, buf1, len);
            } else {
                TRACE_MSG(s, "control");
            }
            break;
        }
        break;
    case QE_TERM_STATE_UTF8:
        /* XXX: should check that c is a UTF-8 continuation byte */
        if (s->term_pos >= s->utf8_len) {
            const char *p = cs8(s->term_buf);
            char32_t ch = s->lastc = utf8_decode(&p);
            int w = qe_wcwidth(ch);
            if (w == 0) {
                /* accents are always inserted */
                // XXX: what if s->cur_offset_hack is not 0?
                // XXX: should insert a space if previous character is '\n'
                s->cur_offset += eb_insert(s->b, offset, s->term_buf, s->utf8_len);
            } else {
                s->cur_offset = qe_term_overwrite(s, offset, w, cs8(s->term_buf), s->utf8_len);
            }
            s->state = QE_TERM_STATE_NORM;
        }
        break;
    case QE_TERM_STATE_ESC:
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
         * tests: \E#8  fill screen with 'E's
         * tests: \Ec   reset
         */
        s->esc1 = c;
        s->state = QE_TERM_STATE_NORM;
        switch (c) {
        case '[':   // Control Sequence Introducer (CSI is 0x9b).
            s->nb_params = 0;
            s->params[0] = CSI_PARAM_OMITTED;
            s->params[1] = CSI_PARAM_OMITTED;
            s->esc1 = 0;   /* used for both leader and intermediary bytes */
            s->state = QE_TERM_STATE_CSI;
            break;
        case ' ':
        case '#':
        case '%':
        case '(':
        case ')':
        case '*':
        case '+':
        case '-':
        case '.':
        case '/':
            s->state = QE_TERM_STATE_ESC2;
            break;
        case ']':   // Operating System Command (OSC is 0x9d).
            s->params[0] = 0;
            s->esc2 = 0;
            s->state = QE_TERM_STATE_OSC1;
            break;
        case '^':   // Privacy Message (PM  is 0x9e).
        case '_':   // Application Program Command (APC is 0x9f).
        case 'P':   // Device Control String (DCS is 0x90).
            s->params[0] = 0;
            s->esc2 = 0;
            s->state = QE_TERM_STATE_STRING;
            break;
        case '\\':  // String Terminator (ST  is 0x9c).
            TRACE_MSG(s, "stray ST");
            break;
        case '6':   // Back Index (DECBI), VT420 and up.
            break;
        case '7':   // Save Cursor (DECSC). [sc]
            qe_term_get_pos(s, offset, &s->save_x, &s->save_y);
            break;
        case '8':   // Restore Cursor (DECRC). [rc]
            qe_term_goto_xy(s, s->save_x, s->save_y, 0);
            break;
        case 'c':   // Full Reset (RIS). [rs1, reset_1string]
            s->shifted = s->charset[s->cset = 0];
            break;
        case '9':   // Forward Index (DECFI), VT420 and up.
        case '=':   // Application Keypad (DECKPAM). [smkx]
        case '>':   // Normal Keypad (DECKPNM). [rmkx, is2, rs2]
            break;
        case 'D':   // Index (IND  is 0x84).
                    // move cursor down, scroll if at bottom
        case 'E':   // Next Line (NEL  is 0x85).
                    // move cursor to beginning of next line, scroll if at bottom
            {
                int col, row;
                qe_term_get_pos(s, offset, &col, &row);
                if (c == 'E')
                    col = 0;
                qe_term_goto_xy(s, col, row + 1, TG_NOCLIP);
            }
            break;
        case 'M':   // Reverse Index (RI  is 0x8d). [ri]
                    // move cursor up, scroll if at top line
            {
                int start, offset3, col, row;
                start = qe_term_get_pos(s, offset, &col, &row);
                if (--row < 0) {
                    /* if (start == 0) */ {
                        qe_term_insert_lines(s, start, 1);
                    }
                    offset3 = qe_term_skip_lines(s, start, s->rows - 1);
                    qe_term_delete_lines(s, offset3, 1);
                    row = 0;
                }
                qe_term_goto_xy(s, col, row, 0);
            }
            break;
        case 'F':   // Cursor to lower left corner of screen.
                    // This is enabled by the hpLowerleftBugCompat resource.
        case 'H':   // Tab Set (HTS  is 0x88). [set_tab]
        case 'O':   // Single Shift Select of G3 Character Set (SS3  is 0x8f).
                    // This affects next character only.
        case 'V':   // Start of Guarded Area (SPA  is 0x96).
        case 'W':   // End of Guarded Area (EPA  is 0x97).
        case 'X':   // Start of String (SOS  is 0x98).
        case 'Z':   // Return Terminal ID (DECID is 0x9a).
                    // Obsolete form of CSI c  (DA).
        case 'l':   // Memory Lock (per HP terminals).  Locks memory above the cursor.
        case 'm':   // Memory Unlock (per HP terminals).
        case 'n':   // Invoke the G2 Character Set as GL (LS2).
        case 'o':   // Invoke the G3 Character Set as GL (LS3).
        case '|':   // Invoke the G3 Character Set as GR (LS3R).
        case '}':   // Invoke the G2 Character Set as GR (LS2R).
        case '~':   // Invoke the G1 Character Set as GR (LS1R).
        default:
            TRACE_MSG(s, "unhandled");
            break;
        }
        break;
    case QE_TERM_STATE_ESC2:
        s->state = QE_TERM_STATE_NORM;
        s->esc2 = c;
        switch (ESC2(s->esc1, c)) {
        case ESC2('%','8'):     /* set utf mode */
        case ESC2('%','G'):     /* Select UTF-8 character set (ISO 2022). */
        case ESC2('%','@'):     /* Select default character set.
                                   That is ISO 8859-1 (ISO 2022). */
            TRACE_MSG(s, "utf mode");
            break;
            /* Designate G0 Character Set (ISO 2022, VT100). */
        case ESC2('(','0'):     /* set charset0 CSET_LINEDRW */
            s->charset[0] = 1;
            break;
        case ESC2('(','A'):     /* set charset0 CSET_GBCHR */
        case ESC2('(','B'):     /* set charset0 CSET_ASCII */
        case ESC2('(','U'):     /* set charset0 CSET_SCOACS */
            s->charset[0] = 0;
            break;
            /* Designate G1 Character Set (ISO 2022, VT100). */
        case ESC2(')','0'):     /* set charset1 CSET_LINEDRW */
            s->charset[1] = 1;
            break;
        case ESC2(')','A'):     /* set charset1 CSET_GBCHR */
        case ESC2(')','B'):     /* set charset1 CSET_ASCII */
        case ESC2(')','U'):     /* set charset1 CSET_SCOACS */
            s->charset[1] = 0;
            break;
            /* Designate G2 Character Set (ISO 2022, VT220). */
        case ESC2('*','B'):
            /* Designate G3 Character Set (ISO 2022, VT220). */
        case ESC2('+','B'):
            /* Designate G1 Character Set (VT300). */
        case ESC2('-','B'):
            /* Designate G2 Character Set (VT300). */
        case ESC2('.','B'):
            /* Designate G3 Character Set (VT300). */
        case ESC2('/','B'):
            TRACE_MSG(s, "set charset");
            break;
        default:
            TRACE_MSG(s, "unhandled");
            break;
        }
        s->shifted = s->charset[s->cset];
        break;
    case QE_TERM_STATE_OSC1:
            /* XXX: OSC sequences should use QE_TERM_STATE_STRING
               (eg: OSC Ps;Pt ST) but linux tty uses non standard
               sequences ESC ] R and `ESC ] P prrggbb, so we need
               these extra states to avoid hanging the terminal
             */
        if (s->term_pos == 3) {
            s->esc2 = c;
            if (c == 'R') {      /* linux reset palette */
                s->state = QE_TERM_STATE_NORM;
                break;
            }
        }
        if (s->esc2 == 'P') {
            if (s->term_pos < 10)
                break;
            /* linux set palette syntax is OSC P nrrggbb:
               n: letter 0-f for standard palette entries
                         g-m for extended attributes:
               rr, gg, bb 2 hex digit values
             */
            TRACE_MSG(s, "linux palette");
            s->state = QE_TERM_STATE_NORM;
            break;
        }
        if (c >= '0' && c <= '9') {
            s->params[0] *= 10;
            s->params[0] += c - '0';
            break;
        }
        s->state = QE_TERM_STATE_STRING;
        /* fall thru */
    case QE_TERM_STATE_STRING:
        /* CG: should store the string */
        /* Stop string on CR or LF, for protection */
        if ((c == '\012' || c == '\015') && s->params[0] != 1337) {
            s->state = QE_TERM_STATE_NORM;
            TRACE_MSG(s, "broken string");
            break;
        }
        /* Stop string on \a (^G) or ST (ESC \) */
        if (!(c == '\007' || c == 0234 || (s->lastc == 27 && c == '\\'))) {
            /* XXX: should store the string for specific cases */
            s->lastc = c;
            break;
        }
        s->state = QE_TERM_STATE_NORM;
        /* OSC / PM / APC / DCS string has been received. Should handle these cases:
           - OSC 0; Pt ST  Change Icon Name and Window Title to Pt.
           - OSC 1; Pt ST  Change Icon Name to Pt.
           - OSC 2; Pt ST  Change Window Title to Pt.
           - OSC 3; Pt ST  Set X property on top-level window: Pt should
           be in the form "prop=value", or just "prop" to delete the property
           - OSC 4; c; name... ST  xterm's define extended color (executeXtermSetRgb)
           Change Color #c to name. Any number of c / name pairs may be given.
           example: "\033]4;16;rgb:0000/00000/0000\033\134"

           iTerm2 reports the current rgb value with "<index>;?",
           e.g. "\033]4;105;?" -> report as \033]4;105;rgb:0000/cccc/ffff\007"

           iTerm2 has a specific behavior for colors 16 to 22:
                16: terminalSetForegroundColor
                17: terminalSetBackgroundColor
                18: terminalSetBoldColor
                19: terminalSetSelectionColor
                20: terminalSetSelectedTextColor
                21: terminalSetCursorColor
                22: terminalSetCursorTextColor

           xterm has the following set extended attribute:
           - OSC 10; c; name... ST    Change color names starting with
           text foreground (a list of one or more color names or RGB
           specifications, separated by semicolon, up to eight, name as
           as per XParseColor.

           - OSC 11; c; name... ST  Change colors starting with text background
           - OSC 12; c; name... ST  Change colors starting with text cursor
           - OSC 13; c; name... ST  Change colors starting with mouse foreground
           - OSC 14; c; name... ST  Change colors starting with mouse background
           - OSC 15; c; name... ST  Change colors starting with Tek foreground
           - OSC 16; c; name... ST  Change colors starting with Tek background
           - OSC 17; c; name... ST  Change colors starting with highlight
           - OSC 46; Pt ST  Change Log File to Pt (normally disabled by a
           compile-time option)
           - OSC 50; Pt ST  Set Font to Pt. If Pt begins with a "#", index in
           the font menu, relative (if the next character is a plus or minus
           sign) or absolute. A number is expected but not required after the
           sign (the default is the current entry for relative, zero for
           absolute indexing).
           - OSC W; Pt ST   word-set (define char wordness)
           xterm has a file download and image display protocol:
           - OSC 1337; Pt ST
         */
        TRACE_PRINTF(s, "unhandled string: %.*s", min_int(s->term_pos, 20), s->term_buf);
        break;
    case QE_TERM_STATE_CSI:
        if (c >= '<' && c <= '?') {
            /* leader bytes */
            s->esc1 = c;
            break;
        }
        if (c >= 0x20 && c <= 0x2F) {
            /* intermediary bytes */
            s->esc1 = c;    /* no need to distinguish leader and interm bytes */
            break;
        }
        if (qe_isdigit(c)) {
            s->params[s->nb_params] &= ~CSI_PARAM_OMITTED;
            s->params[s->nb_params] *= 10;
            s->params[s->nb_params] += c - '0';
            break;
        }
        /* XXX: should clarify */
        if (s->nb_params == 0
        ||  (s->nb_params < MAX_CSI_PARAMS && s->params[s->nb_params] >= 0)) {
            s->nb_params++;
            s->params[s->nb_params] = CSI_PARAM_OMITTED;
        }
        if (c == ';' || c == ':')
            break;
        s->state = QE_TERM_STATE_NORM;
        /* default param is 1 for most commands */
        param1 = s->params[0] >= 0 ? s->params[0] : 1;
        param2 = s->params[1] >= 0 ? s->params[1] : 1;
        // FIXME: should handle c < 0x20 and c >= 0x7F according to ECMA-48 */
        switch (ESC2(s->esc1,c)) {
        case '@':  /* ICH: Insert Ps (Blank) Character(s) (default = 1) */
            {
                char32_t c2;
                int x, y, x1, y1, offset3;

                // XXX: should simplify this mess
                offset1 = offset;
                while (param1-- > 0) {
                    qe_term_get_pos(s, offset1, &x, &y);
                    if (x >= s->cols)
                        break;
                    offset2 = qe_term_goto_pos(s, offset1, s->cols, y, TG_NOCLIP | TG_NOEXTEND);
                    if (offset2 > offset) {
                        qe_term_get_pos(s, offset2, &x1, &y1);
                        if (y1 > y || x1 >= s->cols) {
                            /* full line: must delete last glyph (including accents) */
                            c2 = eb_prev_glyph(s->b, offset2, &offset3);
                            eb_delete_range(s->b, offset3, offset2);
                            /* if last glyph was wide, pad with a space */
                            if (qe_iswide(c2) && eb_nextc(s->b, offset3, &offset2) != '\n')
                                eb_insert_char32(s->b, offset3, ' ');
                        }
                    }
                    qe_term_set_style(s);
                    offset1 += eb_insert_char32(s->b, offset1, ' ');
                }
                /* cur_offset may have been updated by callback */
                s->cur_offset = offset;
            }
            break;
        case 'A':  /* CUU: Cursor Up Ps Times (default = 1) */
            qe_term_goto_xy(s, 0, -param1, TG_RELATIVE);
            break;
        case 'B':  /* CUD: Cursor Down Ps Times (default = 1) */
        case 'e':  /* VPR: Line Position Relative [rows] (default = 1) */
            qe_term_goto_xy(s, 0, param1, TG_RELATIVE);
            break;
        case 'C':  /* CUF: Cursor Forward Ps Times (default = 1) */
        case 'a':  /* HPR: Character Position Relative [columns] (default = 1) */
            qe_term_goto_xy(s, param1, 0, TG_RELATIVE);
            break;
        case 'D':  /* CUB: Cursor Backward Ps Times (default = 1) */
            qe_term_goto_xy(s, -param1, 0, TG_RELATIVE);
            break;
        case 'E':  /* CNL: Cursor Next Line Ps Times (default = 1) and CR. */
            qe_term_goto_xy(s, 0, param1, TG_RELATIVE_ROW);
            break;
        case 'F':  /* CPL: Cursor Preceding Line Ps Times (default = 1) and CR. */
            qe_term_goto_xy(s, 0, -param1, TG_RELATIVE_ROW);
            break;
        case 'G':  /* CHA: Cursor Character Absolute [column]. */
        case '`':  /* HPA: Character Position Absolute [column] (default = 1) */
            qe_term_goto_xy(s, param1 - 1, 0, TG_RELATIVE_ROW);
            break;
        case 'H':  /* CUP: Cursor Position [row;column] (default = [1,1]). */
        case 'f':  /* HVP: Horizontal and Vertical Position [row;column] (default = [1,1]) */
            qe_term_goto_xy(s, param2 - 1, param1 - 1, 0);
            break;
        case 'I':  /* CHT: Cursor Forward Tabulation Ps tab stops (default = 1). */
            qe_term_goto_tab(s, param1);
            break;
        case 'J':  /* ED: Erase in Display. */
        case ESC2('?','J'):  /* DECSED: Selective Erase in Display. */
            /* XXX: should just force top of window to in infinite scroll mode */
            {   /*     0: Below (default), 1: Above, 2: All, 3: Saved Lines (xterm) */
                /* XXX: should handle eol style */
                int offset0, bos, eos, col, row;

                bos = eos = 0;
                // default param is 0
                if (s->params[0] <= 0) {
                    /* Erase to end of screen */
                    eos = 1;
                } else
                if (s->params[0] == 1) {
                    /* Erase from beginning of screen */
                    bos = 1;
                } else
                if (s->params[0] == 2 || s->params[0] == 3) {
                    /* Erase complete screen */
                    bos = eos = 1;
                }
                /* update cursor as overwriting characters may change offsets */
                if (bos) {
                    offset0 = qe_term_get_pos(s, offset, &col, &row);
                    qe_term_set_style(s);
                    if (row > 0) {
                        offset = qe_term_delete_lines(s, offset0, row);
                        offset = qe_term_insert_lines(s, offset, row);
                    } else {
                        /* goto to beginning of line */
                        offset = qe_term_goto_pos(s, offset, 0, 0, TG_RELATIVE_ROW);
                    }
                    //offset = qe_term_erase_chars(s, offset, col);
                    offset1 = qe_term_goto_pos(s, offset, col, 0, TG_RELATIVE_ROW | TG_NOEXTEND);
                    eb_delete(s->b, offset, offset1 - offset);
                    offset += eb_insert_spaces(s->b, offset, col);
                }
                if (eos) {
                    eb_delete(s->b, offset, s->b->total_size - offset);
                }
                s->cur_offset = offset;
            }
            break;
        case 'K':  /* EL: Erase in Line. */
        case ESC2('?','K'):  /* DECSEL: Selective Erase in Line. */
            {   /*     0: to Right (default), 1: to Left, 2: All */
                /* XXX: should handle eol style */
                int col, row, col2, row2, n1, n2, offset3;

                // XXX: should use qe_term_get_pos2()
                qe_term_get_pos(s, offset, &col, &row);
                offset1 = qe_term_goto_pos(s, offset, 0, row, TG_NOCLIP | TG_NOEXTEND);
                offset2 = qe_term_goto_pos(s, offset, s->cols, row, TG_NOCLIP | TG_NOEXTEND);
                qe_term_get_pos(s, offset2, &col2, &row2);
                if (row2 > row)
                    col2 = s->cols;

                n1 = n2 = 0;
                // default param is 0
                if (s->params[0] <= 0) {
                    n2 = offset2 - offset;
                } else
                if (s->params[0] == 1) {
                    n1 = col;
                } else
                if (s->params[0] == 2) {
                    n1 = col;
                    n2 = offset2 - offset;
                }
                qe_term_set_style(s);
                if (n2) {
                    if (eb_nextc(s->b, offset2, &offset3) == '\n') {
                        if (col == 0 && eb_prevc(s->b, offset1, &offset3) != '\n') {
                            /* keep a space to prevent line fusion */
                            eb_insert_spaces(s->b, offset2, 1);
                        }
                    } else {
                        eb_insert_spaces(s->b, offset2, col2 - col);
                    }
                    eb_delete(s->b, offset, n2);
                }
                if (n1) {
                    /* update offset as overwriting characters may change offsets */
                    //offset = qe_term_erase_chars(s, offset1, n1);
                    offset -= eb_delete(s->b, offset1, n1);
                    offset += eb_insert_spaces(s->b, offset1, col);
                }
                // XXX: could scan end of buffer for spaces with default style
                //      and shrink it.
                /* update cursor as callback may have changed it */
                s->cur_offset = offset;
            }
            break;
        case 'L':  /* IL: Insert Ps Line(s) (default = 1). */
            //TRACE_MSG(s, "insert lines");
            {
                int row, zone;
                // XXX: should use qe_term_get_pos() and qe_term_goto_xy()
                offset = eb_goto_bol(s->b, offset);
                qe_term_get_pos(s, offset, NULL, &row);
                zone = max_int(0, s->scroll_bottom - row);
                param1 = min_int(param1, zone);
                qe_term_set_style(s);
                offset1 = qe_term_insert_lines(s, offset, param1);
                offset1 = qe_term_skip_lines(s, offset1, zone - param1);
                qe_term_delete_lines(s, offset1, param1);
                s->cur_offset = offset;
            }
            break;
        case 'M':  /* DL: Delete Ps Line(s) (default = 1). */
            //TRACE_MSG(s, "delete lines");
            {
                int row, zone;
                // XXX: should use qe_term_get_pos() and qe_term_goto_xy()
                offset = eb_goto_bol(s->b, offset);
                qe_term_get_pos(s, offset, NULL, &row);
                zone = max_int(0, s->scroll_bottom - row);
                param1 = min_int(param1, zone);
                qe_term_set_style(s);
                offset1 = qe_term_delete_lines(s, offset, param1);
                offset1 = qe_term_skip_lines(s, offset1, zone - param1);
                qe_term_insert_lines(s, offset1, param1);
                s->cur_offset = offset;
            }
            break;
        case 'P':  /* DCH: Delete Ps Character(s) (default = 1). */
            qe_term_get_pos2(s, offset, &pos, 0);
            qe_term_set_style(s);
            if (!(pos.flags & SP_LINE_END_WRAP)) {
                /* no need to pad end of line */
                if (param1 >= pos.end_col - pos.col) {
                    eb_delete_range(s->b, offset, pos.line_end);
                    if (pos.col == 0 && (pos.flags & SP_LINE_START_WRAP)) {
                        /* if removed wrapped wide glyph, pad previous line */
                        if (pos.flags & SP_LINE_START_WRAP2)
                            s->cur_offset = offset += eb_insert_char32(s->b, offset, ' ');
                        /* we removed the wrapped glyph, must insert a space */
                        eb_insert_char32(s->b, offset, ' ');
                    }
                } else {
                    offset1 = qe_term_goto_pos(s, offset, param1, 0, TG_RELATIVE | TG_NOEXTEND);
                    eb_delete_range(s->b, offset, offset1);
                    /* if removed wrapped wide glyph, pad previous line */
                    if (pos.col == 0 && (pos.flags & SP_LINE_START_WRAP2)) {
                        s->cur_offset = offset += eb_insert_char32(s->b, offset, ' ');
                    }
                }
            } else {
                if (param1 >= pos.end_col - pos.col) {
                    eb_delete_range(s->b, offset, pos.line_end);
                    eb_insert_spaces(s->b, offset, s->cols - pos.col);
                } else {
                    // XXX: should untabify rest of line
                    offset1 = qe_term_goto_pos(s, offset, param1, 0, TG_RELATIVE | TG_NOEXTEND);
                    pos.line_end -= eb_delete_range(s->b, offset, offset1);
                    eb_insert_spaces(s->b, pos.line_end, param1);
                }
            }
            break;
        case 'S':  /* SU: Scroll up Ps lines (default = 1). */
            /* scroll the whole page up */
            TRACE_MSG(s, "scroll up");
            break;
        case 'T':  /* SD: Scroll down Ps lines (default = 1). */
            /* scroll the whole page down */
            TRACE_MSG(s, "scroll down");
            break;
        case 'X':  /* ECH: Erase Ps Character(s) (default = 1). */
            // XXX: this clipping is vain as current col may be > 0.
            //      should clip better
            param1 = min_int(param1, s->cols);
            len = eb_encode_char32(s->b, buf1, ' ');
            while (param1 --> 0) {
                offset = qe_term_overwrite(s, offset, 1, buf1, len);
            }
            break;
        case 'Z':  /* CBT: Cursor Backward Tabulation Ps tab stops (default = 1). */
            qe_term_goto_tab(s, -param1);
            break;
        case 'b':  /* REP: Repeat the preceding graphic character Ps times. */
            {
                int rep = min_int(param1, s->cols);
                int w = qe_wcwidth(s->lastc);
                len = eb_encode_char32(s->b, buf1, s->lastc);
                while (rep --> 0) {
                    s->cur_offset = qe_term_overwrite(s, s->cur_offset, w, buf1, len);
                }
            }
            break;
        case 'c':  /* DA: Send Device Attributes (Primary DA) */
            // default param is 0
            if (s->params[0] <= 0) {
                /* Report Advanced Video option (AVO) */
                qe_term_write(s, "\033[?1;2c", -1);
            }
            break;
        case ESC2('>','c'):  /* DA: Send Device Attributes (Secondary DA) */
            // default param is 0
            if (s->params[0] <= 0) {
                /* Report Qemacs emulator version 0.5 */
                qe_term_write(s, "\033[>42;0;5c", -1);
            }
            break;
        case 'd':  /* VPA: Line Position Absolute [row] (default = 1). */
            param1 = min_int(param1, s->rows);
            qe_term_goto_xy(s, 0, param1 - 1, TG_RELATIVE_COL);
            break;
        case 'g':  /* TBC: Tab Clear. */
            // 0 (default) -> Clear Current Column, 3 -> Clear All */
            TRACE_MSG(s, "clear tabs");
            break;
        case 'h':   /* SM: Set Mode. */
            for (i = 0; i < s->nb_params; i++) {
                switch (s->params[i]) {
                case 2:   /* Keyboard Action Mode (AM). */
                case 4:   /* Insert Mode (IRM). */
                case 12:  /* Send/receive (SRM). */
                case 20:  /* Automatic Newline (LNM). */
                    break;
                }
            }
            break;
        case ESC2('?','h'): /* DEC Private Mode Set (DECSET) */
            for (i = 0; i < s->nb_params; i++) {
                switch (s->params[i]) {
                case 1:     /* Application Cursor Keys (DECCKM). */
                case 3:     /* Set Number of Columns to 132 (DECCOLM) */
                case 4:     /* Smooth (Slow) Scroll (DECSCLM). */
                    break;
                case 5:     /* Reverse Video (DECSCNM) */
                    s->reverse = 1;
                    break;
                case 7:     /* Wraparound Mode (DECAWM). */
                    break;
                case 12:    /* Start Blinking Cursor (att610). */
                    break;
                case 25:    /* Show Cursor (DECTCEM). */
                    break;
                case 1000:  /* Send Mouse X & Y on button press and
                                release. */
                    break;
                case 1034:  /* Interpret "meta" key, sets eighth bit.
                     * (enables the eightBitInput resource).
                     */
                    break;
                case 1047:  /* Use Alternate Screen Buffer. */
                case 1048:  /* Save cursor as in DECSC. */
                case 1049:  /* Save cursor as in DECSC and use Alternate
                       Screen Buffer, clearing it first. */
                    if (s->shell_flags & SF_INTERACTIVE) {
                        /* only grab keys in interactive qe_term buffers */
                        s->grab_keys = 1;
                        qe_grab_keys(s->base.qs, shell_key, s);
                        /* Should also clear screen */
                    }
                    if (!s->use_alternate_screen) {
                        offset = s->b->total_size;
                        if (eb_prevc(s->b, offset, &offset1) != '\n') {
                            qe_term_set_style(s);
                            offset += eb_insert_char32(s->b, offset, '\n');
                        }
                        s->use_alternate_screen = 1;
                        s->cur_offset = s->alternate_screen_top = offset;
                        // XXX: should update window top?
                    }
                    break;
                default:
                    TRACE_MSG(s, "mode set");
                    break;
                }
            }
            break;
        case 'i':   /* MC: Media Copy */
            for (i = 0; i < s->nb_params; i++) {
                switch (s->params[i]) {
                case 0:   /* Print screen (default). */
                case 4:   /* Turn off printer controller mode. */
                case 5:   /* Turn on printer controller mode. */
                case 10:  /* HTML screen dump. */
                case 11:  /* SVG screen dump. */
                    break;
                }
            }
            TRACE_MSG(s, "media copy");
            break;
        case ESC2('?','i'):  /* MC: Media Copy (MC, DEC-specific). */
            for (i = 0; i < s->nb_params; i++) {
                switch (s->params[i]) {
                case 1:   /* Print line containing cursor. */
                case 4:   /* Turn off autoprint mode. */
                case 5:   /* Turn on autoprint mode. */
                case 10:  /* Print composed display, ignores DECPEX. */
                case 11:  /* Print all pages. */
                    break;
                }
            }
            TRACE_MSG(s, "media copy");
            break;
        case 'l':   /* RM: Reset Mode. */
            for (i = 0; i < s->nb_params; i++) {
                switch (s->params[i]) {
                case 2:   /* Keyboard Action Mode (AM). */
                case 4:   /* Insert Mode (IRM). */
                case 12:  /* Send/receive (SRM). */
                case 20:  /* Automatic Newline (LNM). */
                    break;
                }
            }
            break;
        case ESC2('?','l'): /* DEC Private Mode Reset (DECRST). */
            for (i = 0; i < s->nb_params; i++) {
                switch (s->params[i]) {
                case 1:     /* Normal Cursor Keys (DECCKM). */
                case 3:     /* Set Number of Columns to 80 (DECCOLM) */
                case 4:     /* Jump (Fast) Scroll (DECSCLM). */
                    break;
                case 5:     /* Normal Video (DECSCNM). */
                    s->reverse = 1;
                    break;
                case 7:     /* No Wraparound Mode (DECAWM). */
                    break;
                case 12:    /* Stop Blinking Cursor (att610). */
                    break;
                case 25:    /* Hide Cursor (DECTCEM). */
                    break;
                case 1000:  /* Don't send Mouse X & Y on button press and
                                release. */
                case 1034:  /* Don't interpret "meta" key.  (This disables
                       the eightBitInput resource).
                     */
                    break;
                case 1047:  /* Use Normal Screen Buffer, clearing screen
                       first if in the Alternate Screen. */
                case 1048:  /* Restore cursor as in DECRC. */
                case 1049:  /* Use Normal Screen Buffer and restore cursor
                       as in DECRC. */
                    if (s->shell_flags & SF_INTERACTIVE) {
                        qe_ungrab_keys(s->base.qs);
                        s->grab_keys = 0;
                    }
                    if (s->use_alternate_screen) {
                        // XXX: this will actually go to row s->rows-1
                        qe_term_goto_xy(s, 0, s->rows, 0);
                        eb_delete_range(s->b, s->cur_offset, s->b->total_size);
                        s->use_alternate_screen = 0;
                    }
                    s->cur_offset = s->b->total_size;
                    break;
                default:
                    TRACE_MSG(s, "mode reset");
                    break;
                }
            }
            break;
        case 'm':  /* SGR: Set Graphics Rendition (Character Attributes). */
            for (i = 0; i < s->nb_params;) {
                i += qe_term_csi_m(s, s->params + i, s->nb_params - i);
            }
            break;
        case 'n':  /* DSR: Device Status Report. */
            if (param1 == 5) {
                /* Status Report: terminal is OK */
                qe_term_write(s, "\033[0n", -1);
            } else
            if (param1 == 6) {
                /* Report Cursor Position (CPR) [row;column]. */
                char buf2[20];
                int col_num, cur_line, x, y;
                /* actually send position of point in window */
                qe_term_get_pos(s, offset, &x, &y);
                col_num = x + (x < s->cols);
                cur_line = y + (y < s->rows);
                snprintf(buf2, sizeof(buf2), "\033[%d;%dR", cur_line, col_num);
                qe_term_write(s, buf2, -1);
            }
            break;
        case 'p':  /* DECSCL: Set conformance level. */
        case 'q':  /* DECLL: Load LEDs. */
        case ESC2(' ','q'): /* Set cursor style (DECSCUSR, VT520). */
            goto unhandled;
        case 'r':  /* DECSTBM: Set Scrolling Region [top;bottom]
                      (default = full size of window) */
            /* XXX: the scrolling region should also affect LF operation */
            s->scroll_top = clamp_int(s->params[0] - 1, 0, s->rows);
            s->scroll_bottom = s->params[1] > 0 ? clamp_int(s->params[1], 1, s->rows) : s->rows;
            break;
        case ESC2('$','r'): /* DECCARA: Change Attributes in Rectangular
                               Area, VT400 and up. */
            goto unhandled;
        case ESC2('?','r'):
            /* Restore DEC Private Mode Values.  The value of Ps previously
               saved is restored.  Ps values are the same as for DECSET. */
            TRACE_MSG(s, "mode restore");
            break;
        case ESC2('?','s'):
            /* Save DEC Private Mode Values.  <i>Ps</i> values are the same as for
               DECSET.. */
            TRACE_MSG(s, "mode save");
            break;
        case 's':  /* Save cursor (ANSI.SYS), available only when
                      DECLRMM is disabled. */
            /* DECSLRM: Set left and right margins, available only when
               DECLRMM is enabled (VT420 and up). */
            qe_term_get_pos(s, offset, &s->save_x, &s->save_y);
            break;
        case 't':  /* DECSLPP: set page size - ie window height */
            /* also used for window changing and reports */
            TRACE_MSG(s, "set page size");
            break;
        case ESC2('>','t'):  /* Set one or more features of the title modes. */
            // Ps = 0  -> Set window/icon labels using hexadecimal.
            // Ps = 1  -> Query window/icon labels using hexadecimal.
            // Ps = 2  -> Set window/icon labels using UTF-8.
            // Ps = 3  -> Query window/icon labels using UTF-8.  (See discussion of "Title Modes")
            break;
        case ESC2('$','t'):  /* DECRARA: Reverse Attributes in Rectangular Area,
               VT400 and up. */
            goto unhandled;
        case 'u':  /* Restore cursor (ANSI.SYS). */
            qe_term_goto_xy(s, s->save_x, s->save_y, 0);
            break;
        case ESC2('$','v'):  /* DECCRA: Copy Rectangular Area (VT400 and up). */
            goto unhandled;
        case 'x':  /* DECREQTPARM: Request Terminal Parameters */
        case ESC2('$','x'):  /* DECFRA: Fill Rectangular Area, VT420 and up. */
        case ESC2('$','z'):  /* DECERA: Erase Rectangular Area, VT400 and up. */
        case ESC2('$','{'): /* DECSERA: Selective Erase Rectangular Area, VT400 and up. */
        case ESC2('\'','{'): /* DECSLE: Select Locator Events. */
        case ESC2('\'','|'): /* DECRQLP: Request Locator Position. */
        case ESC2('\'','}'): /* DECIC: Insert Ps Column(s) (default = 1), VT420 and up. */
        case ESC2('\'','~'): /* DECDC: Delete Ps Column(s) (default = 1), VT420 and up. */
        case ESC2('=','c'):   /* Hide or Show Cursor */
                              /* 0: hide, 1: restore, 2: block */
        case ESC2('=','C'):  /* set cursor shape */
        case ESC2('=','D'):  /* set blinking attr on/off */
        case ESC2('=','E'):  /* set blinking on/off */
            goto unhandled;
        case ESC2('=','F'): /* select SCO foreground color */
            s->fgcolor = sco_color[param1 & 15];
            break;
        case ESC2('=','G'): /* select SCO background color */
            s->bgcolor = sco_color[param1 & 15];
            break;
        default:
        unhandled:
            TRACE_MSG(s, "unhandled");
            break;
        }
        break;
    }
#undef ESC2
    qe_term_update_cursor(s);
}

/* buffer related functions */

/* called when characters are available from the process */
static void shell_read_cb(void *opaque)
{
    ShellState *s = opaque;
    QEmacsState *qs;
    EditBuffer *b;
    unsigned char buf[16 * 1024];
    int len, i, save_readonly;

    if (!s || s->base.mode != &shell_mode)
        return;

    len = read(s->pty_fd, buf, sizeof(buf));
    if (len <= 0)
        return;

    b = s->b;
    qs = s->base.qs;
    if (qs->trace_buffer)
        qe_trace_bytes(qs, buf, len, EB_TRACE_SHELL);

    /* Suspend BF_READONLY flag to allow shell output to readonly buffer */
    save_readonly = b->flags & BF_READONLY;
    b->flags &= ~BF_READONLY;
    b->last_log = 0;

    if (s->shell_flags & SF_COLOR) {
        /* optional terminal emulation (shell, ssh, make, latex, man modes) */
        for (i = 0; i < len; i++) {
            qe_term_emulate(s, buf[i]);
        }
        if (s->last_char == '\000' || s->last_char == '\001'
        ||  s->last_char == '\003'
        ||  s->last_char == '\r' || s->last_char == '\n') {
            /* if the last char sent to the process was the enter key, C-C
             * to kill the process, C-A to go to beginning of line, or if
             * nothing was sent to the process yet, assume the process is
             * prompting for input and save the current input position as
             * the start of input.
             */
            s->cur_prompt = s->cur_offset;
            if (qs->active_window
            &&  qs->active_window->b == b
            &&  qs->active_window->interactive) {
                /* Set mark to potential tentative position (useful?) */
                b->mark = s->cur_prompt;
            }
        }
        shell_get_curpath(b, s->cur_offset, s->curpath, sizeof(s->curpath));
    } else {
        int pos = b->total_size;
        int threshold = 3 << 20;    /* 3MB for large pictures */
        eb_write(b, b->total_size, buf, len);
        if (pos < threshold && pos + len >= threshold) {
            EditState *e;
            for (e = qs->first_window; e != NULL; e = e->next_window) {
                if (e->b == b) {
                    if (s->shell_flags & SF_AUTO_CODING)
                        do_set_auto_coding(e, 0);
                    if (s->shell_flags & SF_AUTO_MODE)
                        qe_set_next_mode(e, 0, 0);
                }
            }
        }
    }
    if (save_readonly) {
        b->modified = 0;
        b->flags |= save_readonly;
    }

    /* now we do some refresh (should just invalidate?) */
    qe_display(qs);
}

static void shell_mode_free(EditBuffer *b, void *state)
{
    ShellState *s = state;
    int tries = 5, sig, rc, status;

    eb_free_callback(b, eb_offset_callback, &s->cur_offset);
    eb_free_callback(b, eb_offset_callback, &s->cur_prompt);
    eb_free_callback(b, eb_offset_callback, &s->alternate_screen_top);
    eb_free_callback(b, eb_offset_callback, &s->screen_top);

    if (s->pid != -1) {
        sig = SIGINT;
        while (tries --> 0) {
            /* try and kill the child process */
            kill(s->pid, sig);
            /* wait first 100 ms */
            usleep(100 * 1000);
            rc = waitpid(s->pid, &status, WNOHANG);
            if (rc < 0 && errno == ECHILD)
                break;
            if (rc == s->pid) {
                /* if process exited or was killed, all good */
                if (WIFEXITED(status) || WIFSIGNALED(status))
                    break;
            }
            /* if still not killed, then try harder (useful for shells) */
            sig = SIGKILL;
        }
        set_pid_handler(s->pid, NULL, NULL);
        s->pid = -1;
    }
    if (s->pty_fd >= 0) {
        set_read_handler(s->pty_fd, NULL, NULL);
        close(s->pty_fd);
        s->pty_fd = -1;
    }
}

static void shell_pid_cb(void *opaque, int status)
{
    ShellState *s;
    EditBuffer *b;
    QEmacsState *qs;
    EditState *e;
    char buf[1024];

    /* Extra check in case mode data was freed already.
     * It is not completely fool proof as the same address might have
     * been reallocated for the same purpose, quite unlikely.
     */
    s = check_mode_data(&opaque);
    if (!s || s->base.mode != &shell_mode)
        return;

    b = s->b;
    qs = s->base.qs;

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
            snprintf(buf, sizeof(buf), "\n%s finished at %s\n",
                     s->caption, time_str);
        } else {
            snprintf(buf, sizeof(buf), "\n%s exited abnormally with code %d at %s\n",
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
    qe_ungrab_keys(qs);
    for (e = qs->first_window; e != NULL; e = e->next_window) {
        if (e->b == b) {
            e->interactive = 0;
            if (s->shell_flags & SF_AUTO_CODING)
                do_set_auto_coding(e, 0);
            if (s->shell_flags & SF_AUTO_MODE)
                qe_set_next_mode(e, 0, 0);
        }
    }
    if (!(s->shell_flags & SF_INTERACTIVE)) {
        /* Must Unlink the shell data to avoid potential crash */
        //shell_mode_free(b, s);  // called by qe_free_mode_data
        qe_free_mode_data(&s->base);
    }
    qe_display(qs);
}

EditBuffer *qe_new_shell_buffer(QEmacsState *qs, EditBuffer *b0, EditState *e,
                                const char *bufname, const char *caption,
                                const char *path, const char *cmd,
                                int shell_flags)
{
    ShellState *s;
    EditBuffer *b;
    const char *lang;
    int cols, rows;

    if (!b0 && (shell_flags & SF_REUSE_BUFFER)) {
        b0 = qe_find_buffer_name(qs, bufname);
        if (b0) {
            if (shell_flags & SF_ERASE_BUFFER) {
                /* XXX: this also clears the undo records */
                eb_clear(b0);
            }
        }
    }

    b = b0;
    if (!b) {
        b = qe_new_buffer(qs, bufname, BF_SAVELOG | BF_SHELL);
        if (!b)
            return NULL;
    }
    shell_flags &= ~(SF_REUSE_BUFFER | SF_ERASE_BUFFER);

    eb_set_buffer_name(b, bufname); /* ensure that the name is unique */
    if (shell_flags & SF_COLOR) {
        eb_create_style_buffer(b, BF_STYLE_COMP);
    }
    /* Select shell output buffer encoding from LANG setting */
    if (((lang = getenv("LANG")) != NULL && strstr(lang, "UTF-8")) ||
          qs->screen->charset == &charset_utf8) {
        eb_set_charset(b, &charset_utf8, b->eol_type);
    } else {
        eb_set_charset(b, &charset_vt100, b->eol_type);
    }

    s = qe_get_buffer_mode_data(b, &shell_mode, NULL);
    if (!s) {
        s = (ShellState*)qe_create_buffer_mode_data(b, &shell_mode);
        if (!s) {
            if (!b0)
                eb_free(&b);
            return NULL;
        }
        /* Track cursor with edge effect */
        eb_add_callback(b, eb_offset_callback, &s->cur_offset, 1);
        eb_add_callback(b, eb_offset_callback, &s->cur_prompt, 0);
        eb_add_callback(b, eb_offset_callback, &s->alternate_screen_top, 0);
        eb_add_callback(b, eb_offset_callback, &s->screen_top, 0);
    }
    s->b = b;
    s->pty_fd = -1;
    s->pid = -1;
    s->caption = caption;
    s->shell_flags = shell_flags;
    s->cur_prompt = s->cur_offset = b->total_size;
    qe_term_init(s);

    /* launch shell */
    /* default values for cols, rows should come from the screen size */
    cols = QE_TERM_XSIZE;
    rows = QE_TERM_YSIZE;
    if (e) {
        cols = e->cols;
        rows = e->rows;
    }
    s->cols = cols;
    s->rows = rows;

    if (run_process(s, cmd, &s->pty_fd, &s->pid, cols, rows, path, shell_flags) < 0) {
        if (!b0)
            eb_free(&b);
        return NULL;
    }

    /* XXX: ShellState life cycle is bogus */
    set_read_handler(s->pty_fd, shell_read_cb, s);
    set_pid_handler(s->pid, shell_pid_cb, s);
    return b;
}

/* If a window is attached to buffer b, activate it,
   otherwise attach window s to buffer b.
   *sp is not NULL.
 */
static EditBuffer *try_show_buffer(EditState **sp, const char *bufname)
{
    EditState *e, *s = *sp;
    QEmacsState *qs = s->qs;
    EditBuffer *b;

    b = qe_find_buffer_name(qs, bufname);
    if (b && s->b != b) {
        e = eb_find_window(b, NULL);
        if (e) {
            qs->active_window = *sp = e;
        } else {
            switch_to_buffer(s, b);
        }
    }
    return b;
}

static void do_shell(EditState *e, int argval)
{
    char curpath[MAX_FILENAME_SIZE];
    EditBuffer *b = NULL;

    if (e->flags & (WF_POPUP | WF_MINIBUF))
        return;

    /* Start the shell in the default directory of the current window */
    get_default_path(e->b, e->offset, curpath, sizeof curpath);

    /* avoid messing with the dired pane */
    e = qe_find_target_window(e, 1);

    /* find shell buffer if any */
    if (argval == 1) {
        if (strstart(e->b->name, "*shell", NULL)) {
            /* If the current buffer is a shell buffer, use it */
            b = e->b;
        } else {
            /* Find the last used shell buffer, if any */
            if (strstart(error_buffer, "*shell", NULL)) {
                b = try_show_buffer(&e, error_buffer);
            }
            if (b == NULL) {
                b = try_show_buffer(&e, "*shell*");
            }
        }
        if (b) {
            /* If the process is active, switch to interactive mode */
            ShellState *s = shell_get_state(e, 0);
            if (s && s->pid >= 0) {
                e->offset = b->total_size;
                if ((s->shell_flags & SF_INTERACTIVE) && !s->grab_keys) {
                    e->offset = s->cur_offset;
                    e->interactive = 1;
                }
                return;
            }
            /* otherwise, restart the process here */
            e->offset = b->total_size;
            /* restart the shell in the same directory */
            get_default_path(e->b, e->offset, curpath, sizeof curpath);
        }
    }

    /* create new shell buffer or restart shell in current buffer */
    b = qe_new_shell_buffer(e->qs, b, e, "*shell*", "Shell process",
                            curpath, NULL, SF_COLOR | SF_INTERACTIVE);
    if (!b)
        return;

    b->default_mode = &shell_mode;
    switch_to_buffer(e, b);
    /* force interactive mode if restarting */
    shell_mode.mode_init(e, b, 0);
    // XXX: should update the terminal size and notify the process
    //do_shell_refresh(e, 0);
    set_error_offset(b, 0);
    put_status(e, "Press C-o to toggle between shell/edit mode");
}

static void do_man(EditState *s, const char *arg)
{
    char bufname[32];
    char cmd[128];
    EditBuffer *b;

    if (s->flags & (WF_POPUP | WF_MINIBUF))
        return;

    if (s->flags & WF_POPLEFT) {
        /* avoid messing with the dired pane */
        s = find_window(s, KEY_RIGHT, s);
        s->qs->active_window = s;
    }

    /* Assume standard man command */
    snprintf(cmd, sizeof(cmd), "man %s", arg);

    snprintf(bufname, sizeof(bufname), "*Man %s*", arg);
    if (try_show_buffer(&s, bufname))
        return;

    /* create new buffer */
    b = qe_new_shell_buffer(s->qs, NULL, s, bufname, NULL,
                            NULL, cmd, SF_COLOR | SF_INFINITE);
    if (!b)
        return;

    b->data_type_name = "man";
    b->flags |= BF_READONLY;
    switch_to_buffer(s, b);
    edit_set_mode(s, &pager_mode);
}

static void do_ssh(EditState *s, const char *arg)
{
    char bufname[64];
    char cmd[128];
    EditBuffer *b;

    if (s->flags & (WF_POPUP | WF_MINIBUF))
        return;

    if (s->flags & WF_POPLEFT) {
        /* avoid messing with the dired pane */
        s = find_window(s, KEY_RIGHT, s);
        s->qs->active_window = s;
    }

    /* Use standard ssh command */
    snprintf(cmd, sizeof(cmd), "ssh %s", arg);
    snprintf(bufname, sizeof(bufname), "*ssh-%s*", arg);

    /* create new buffer */
    b = qe_new_shell_buffer(s->qs, NULL, s, bufname, "ssh",
                            NULL, cmd, SF_COLOR | SF_INTERACTIVE);
    if (!b)
        return;

    b->data_type_name = "ssh";
    b->default_mode = &shell_mode;
    switch_to_buffer(s, b);

    put_status(s, "Press C-o to toggle between shell/edit mode");
}

static void shell_move_left_right(EditState *e, int dir)
{
    ShellState *s = shell_get_state(e, 1);

    if (s && e->interactive) {
        qe_term_write(s, dir > 0 ? s->kcuf1 : s->kcub1, -1);
    } else {
        text_move_left_right_visual(e, dir);
    }
}

static void shell_move_word_left_right(EditState *e, int dir)
{
    ShellState *s = shell_get_state(e, 1);

    if (s && e->interactive) {
        qe_term_write(s, dir > 0 ? "\033f" : "\033b", -1);
    } else {
        text_move_word_left_right(e, dir);
    }
}

static void shell_move_up_down(EditState *e, int dir)
{
    ShellState *s = shell_get_state(e, 1);

    if (s && e->interactive) {
        qe_term_write(s, dir > 0 ? s->kcud1 : s->kcuu1, -1);
    } else {
        text_move_up_down(e, dir);
        // XXX: what if beyond?
        if (s && (s->shell_flags & SF_INTERACTIVE) && !s->grab_keys)
            e->interactive = (e->offset == s->cur_offset);
    }
}

static void shell_previous_next(EditState *e, int dir)
{
    ShellState *s = shell_get_state(e, 1);

    if (s && e->interactive) {
        /* hack: M-p silently converted to C-p */
        qe_term_write(s, dir > 0 ? s->kcud1 : s->kcuu1, -1);
    } else {
        /* hack: M-p silently converted to C-u C-p */
        text_move_up_down(e, dir * 4);
        // XXX: what if beyond?
        if (s && (s->shell_flags & SF_INTERACTIVE) && !s->grab_keys)
            e->interactive = (e->offset == s->cur_offset);
    }
}

static void shell_exchange_point_and_mark(EditState *e)
{
    ShellState *s = shell_get_state(e, 1);

    if (s && e->interactive) {
        qe_term_write(s, "\030\030", 2);  /* C-x C-x */
    } else {
        do_exchange_point_and_mark(e);
        // XXX: what if beyond?
        if (s && (s->shell_flags & SF_INTERACTIVE) && !s->grab_keys)
            e->interactive = (e->offset == s->cur_offset);
    }
}

static void shell_scroll_up_down(EditState *e, int dir)
{
    ShellState *s = shell_get_state(e, 1);

    e->interactive = 0;
    text_scroll_up_down(e, dir);
    // XXX: what if beyond?
    if (s && (s->shell_flags & SF_INTERACTIVE) && !s->grab_keys)
        e->interactive = (e->offset == s->cur_offset);
}

static void shell_move_bol(EditState *e)
{
    ShellState *s = shell_get_state(e, 1);

    /* exit shell interactive mode on home / ^A at start of shell input */
    if (!s || (e->offset == s->cur_prompt && !s->grab_keys))
        e->interactive = 0;

    if (s && e->interactive) {
        qe_term_write(s, "\001", 1); /* Control-A */
    } else {
        text_move_bol(e);
    }
}

static void shell_move_eol(EditState *e)
{
    ShellState *s = shell_get_state(e, 1);

    if (s && e->interactive) {
        qe_term_write(s, "\005", 1); /* Control-E */
    } else {
        text_move_eol(e);
        /* XXX: restore shell interactive mode on end / ^E */
        if (s && (s->shell_flags & SF_INTERACTIVE) && !s->grab_keys
        &&  e->offset >= s->cur_offset) {
            e->interactive = 1;
            if (e->offset > s->cur_offset)
                qe_term_write(s, "\005", 1); /* Control-E */
        }
    }
}

static void shell_move_bof(EditState *e)
{
    /* Exit shell interactive mode on home-buffer / M-< */
    e->interactive = 0;
    text_move_bof(e);
}

static void shell_move_eof(EditState *e)
{
    ShellState *s = shell_get_state(e, 1);

    if (s && e->interactive) {
        qe_term_write(s, "\005", 1); /* Control-E */
    } else {
        text_move_eof(e);
        /* Restore shell interactive mode on end-buffer / M-> */
        if (s && (s->shell_flags & SF_INTERACTIVE) && !s->grab_keys
        &&  e->offset >= s->cur_offset) {
            e->interactive = 1;
            if (e->offset != s->cur_offset)
                qe_term_write(s, "\005", 1); /* Control-E */
        }
    }
}

static void shell_write_char(EditState *e, int c)
{
    ShellState *s = shell_get_state(e, 1);

    if (s && e->interactive) {
        char buf[10];
        int len;

        if (c >= KEY_META(0) && c <= KEY_META(0xff)) {
            buf[0] = '\033';
            buf[1] = c - KEY_META(0);
            len = 2;
        } else {
            len = eb_encode_char32(e->b, buf, c);
        }
        qe_term_write(s, buf, len);
    } else {
        text_write_char(e, c);
    }
}

static void shell_delete_bytes(EditState *e, int offset, int size)
{
    ShellState *s = shell_get_state(e, 1);
    int start = offset;
    int end = offset + size;

    // XXX: should deal with regions spanning current input line and
    // previous buffer contents
    if (s && !s->grab_keys && end > s->cur_prompt) {
        int start_char, cur_char, end_char, size1;
        if (start < s->cur_prompt) {
            /* delete part before the interactive input */
            size1 = eb_delete_range(e->b, start, s->cur_prompt);
            end -= size1;
            start = s->cur_prompt;
        }
        start_char = eb_get_char_offset(e->b, start);
        cur_char = eb_get_char_offset(e->b, s->cur_offset);
        end_char = eb_get_char_offset(e->b, end);
        if (start == s->cur_prompt && cur_char > start_char + 2) {
            qe_term_write(s, "\001", 1);  /* C-a */
            cur_char = start_char;
        }
        while (cur_char > end_char) {
            qe_term_write(s, "\002", 1);  /* C-b */
            cur_char--;
        }
        while (cur_char < start_char) {
            qe_term_write(s, "\006", 1);  /* C-f */
            cur_char++;
        }
        if (start_char == cur_char && end == e->b->total_size) {
            /* kill to end of line with control-k */
            qe_term_write(s, "\013", 1);
        } else {
            /* Do not use C-d to delete characters because C-d may exit shell */
            while (cur_char < end_char) {
                qe_term_write(s, "\006", 1);  /* C-f */
                cur_char++;
            }
            while (start_char < cur_char) {
                qe_term_write(s, "\010", 1);  /* backspace */
                cur_char--;
                end_char--;
            }
        }
    } else {
        eb_delete(e->b, offset, size);
    }
}

static void do_shell_newline(EditState *e)
{
    struct timespec ts;

    if (e->interactive) {
        ShellState *s = shell_get_state(e, 1);

        if (s) {
            shell_get_curpath(e->b, e->offset, s->curpath, sizeof(s->curpath));
        }
        shell_write_char(e, '\r');
        /* give the process a chance to handle the input */
        ts.tv_sec = 0;
        ts.tv_nsec = 1000000;  /* 1 ms */
        nanosleep(&ts, NULL);
    } else {
        do_newline(e);
    }
    /* reset offset to scan errors and matches from */
    set_error_offset(e->b, e->offset);
}

static void do_shell_intr(EditState *e)
{
    if (e->interactive) {
        shell_write_char(e, 3);
    } else {
        text_write_char(e, 3);
    }
}

static void do_shell_delete_char(EditState *e)
{
    if (e->interactive) {
        shell_write_char(e, 4);
    } else {
        do_delete_char(e, NO_ARG);
    }
}

static void do_shell_backspace(EditState *e)
{
    if (e->interactive) {
        shell_write_char(e, KEY_DEL);
    } else {
        do_backspace(e, NO_ARG);
    }
}

static void do_shell_search(EditState *e, int dir)
{
    if (e->interactive) {
        shell_write_char(e, dir < 0 ? ('r' & 31) : ('s' & 31));
    } else {
        do_isearch(e, NO_ARG, dir);
    }
}

static void do_shell_kill_word(EditState *e, int dir)
{
    ShellState *s = shell_get_state(e, 1);

    if (s && e->interactive) {
        /* copy word to the kill ring */
        int start = e->offset;

        // XXX: word pattern is different for shell line editor?
        text_move_word_left_right(e, dir);
        if (e->offset < s->cur_prompt) {
            e->offset = s->cur_prompt;
        }
        do_kill(e, start, e->offset, dir, 1);
        // XXX: word pattern consistency issue
        shell_write_char(e, dir > 0 ? KEY_META('d') : KEY_META(KEY_DEL));
    } else {
        do_kill_word(e, dir);
    }
}

static void do_shell_kill_line(EditState *e, int argval)
{
    ShellState *s = shell_get_state(e, 1);
    int dir = (argval == NO_ARG || argval > 0) ? 1 : -1;
    int offset, p1 = e->offset, p2 = p1;

    if (s && e->interactive) {
        /* ignore count argument in interactive mode */
        if (dir < 0) {
            /* kill backwards upto prompt position */
            p2 = max_offset(eb_goto_bol(e->b, p1), s->cur_prompt);
            do_kill(e, p1, p2, dir, 0);
            //shell_write_char(e, KEY_META('k'));
        } else {
            p2 = eb_goto_eol(e->b, p1);
            do_kill(e, p1, p2, dir, 0);
            //shell_write_char(e, KEY_CTRL('k'));
        }
    } else {
        /* Cannot use do_kill_line() because it relies on mode specific
         * cursor movement methods, which are handled asynchronously in
         * shell mode.
         */
        if (argval == NO_ARG) {
            /* kill to end of line */
            if (eb_nextc(e->b, p2, &offset) == '\n') {
                p2 = offset;
            } else {
                p2 = eb_goto_eol(e->b, p2);
            }
        } else
        if (argval <= 0) {
            /* kill backwards */
            dir = -1;
            for (;;) {
                p2 = eb_goto_bol(e->b, p2);
                if (p2 <= 0 || argval == 0)
                    break;
                p2 = eb_prev(e->b, p2);
                argval += 1;
            }
        } else {
            for (;;) {
                p2 = eb_goto_eol(e->b, p2);
                if (p2 >= e->b->total_size || argval == 0)
                    break;
                p2 = eb_next(e->b, p2);
                argval -= 1;
            }
        }
        e->offset = p2;
        do_kill(e, p1, p2, dir, 0);
    }
}

static void do_shell_kill_beginning_of_line(EditState *s, int argval)
{
    do_shell_kill_line(s, argval == NO_ARG ? 0 : -argval);
}

// XXX: need shell_set_mark, shell_kill_region...

static void do_shell_yank(EditState *e)
{
    if (e->interactive) {
        /* yank from kill-ring and insert via shell_write_char().
         * This will cause a deadlock if kill buffer contents is too
         * large. Hard coded limit can be removed if shell input is
         * made asynchronous via an auxiliary buffer.
         */
        int offset;
        QEmacsState *qs = e->qs;
        EditBuffer *b = qs->yank_buffers[qs->yank_current];

        e->b->mark = e->offset;

        if (b) {
            if (b->total_size > 1024) {
                put_error(e, "Too much data to yank at shell prompt");
                return;
            }
            for (offset = 0; offset < b->total_size;) {
                char32_t c = eb_nextc(b, offset, &offset);
                if (c == '\n')
                    do_shell_newline(e);
                else
                    shell_write_char(e, c);
            }
        }
        qs->this_cmd_func = (CmdFunc)do_yank;
    } else {
        do_yank(e);
    }
}

static void do_shell_changecase_word(EditState *e, int dir)
{
    ShellState *s = shell_get_state(e, 1);

    if (s && e->interactive) {
        // XXX: word pattern consistency issue
        shell_write_char(e, dir == 2 ? KEY_META('c')
                         : dir < 0 ? KEY_META('l')
                         : KEY_META('u'));
    } else {
        do_changecase_word(e, dir);
    }
}

static void do_shell_transpose(EditState *e, int cmd)
{
    ShellState *s = shell_get_state(e, 1);

    if (s && e->interactive) {
        shell_write_char(e, cmd == CMD_TRANSPOSE_CHARS ?
                         KEY_CTRL('T') : KEY_META('t'));
    } else {
        do_transpose(e, cmd);
    }
}

static void do_shell_tabulate(EditState *e)
{
    if (e->interactive) {
        shell_write_char(e, 9);
    } else {
        //do_tabulate(s, 1);
        text_write_char(e, 9);
    }
}

static void do_shell_refresh(EditState *e, int flags)
{
    ShellState *s;

    if ((s = shell_get_state(e, 1)) != NULL) {
        QEmacsState *qs = e->qs;
        EditState *e1;

        /* update the terminal size and notify process */
        s->cols = e->wrap_cols = e->cols;
        s->rows = e->rows;

        for (e1 = qs->first_window; e1 != NULL; e1 = e1->next_window) {
            if (e1->b == e->b) {
                e1->wrap_cols = s->cols;
            }
        }

        if (s->pty_fd > 0 && (flags & SR_UPDATE_SIZE)) {
            struct winsize ws;
            ws.ws_col = s->cols;
            ws.ws_row = s->rows;
            ws.ws_xpixel = ws.ws_col;
            ws.ws_ypixel = ws.ws_row;
            ioctl(s->pty_fd, TIOCSWINSZ, &ws);
        }
    }
    if (flags & SR_REFRESH)
        do_refresh_complete(e);
    if (s && !(flags & SR_SILENT)) {
        put_status(e, "Terminal size set to %d by %d", s->cols, s->rows);
    }
}

static void do_shell_toggle_input(EditState *e)
{
    ShellState *s;

    if ((s = shell_get_state(e, 1)) != NULL) {
        if (e->interactive) {
            e->interactive = 0;
            return;
        }
        if ((s->shell_flags & SF_INTERACTIVE) && e->offset >= e->b->total_size) {
            e->interactive = 1;
            if (s->grab_keys)
                qe_grab_keys(s->base.qs, shell_key, s);
#if 0
            if (e->interactive) {
                qe_term_update_cursor(s);
            }
#endif
            return;
        }
    }
    do_open_line(e);
}

/* get current directory from prompt on current line */
/* XXX: should extend behavior to handle more subtile cases */
static char *shell_get_curpath(EditBuffer *b, int offset,
                               char *buf, int buf_size)
{
    char line[1024];
    char curpath[MAX_FILENAME_SIZE];
    int start, stop0, stop, i, len, offset1;

    offset = eb_goto_bol(b, offset);
again:
    len = eb_fgets(b, line, sizeof(line), offset, &offset1);
    line[len] = '\0';   /* strip the trailing newline if any */

    start = stop = stop0 = 0;
    for (i = 0; i < len;) {
        char c = line[i++];
        if (c == '#' || c == '$' || c == '>') {
            stop = stop0;
            break;
        }
        if (c == ':' && line[i] != '\\'
        &&  !(line[i] == '/' && line[i + 1] == '/')
        &&  !(line[i] == '/' && line[i + 1] == '*'))
            start = i;
        if (c == ' ') {
            if (!start || start == i - 1)
                start = i;
        } else {
            stop0 = i;
        }
    }
    if (stop > start) {
        line[stop] = '\0';
        /* XXX: should use a lower level function to avoid potential recursion */
        canonicalize_absolute_path(NULL, curpath, sizeof curpath, line + start);
        if (is_directory(curpath)) {
            append_slash(curpath, sizeof curpath);
            return pstrcpy(buf, buf_size, curpath);
        }
    }
    /* XXX: limit backlook? */
    if (offset > 0) {
        offset = eb_prev_line(b, offset);
        goto again;
    }
    return NULL;
}

static char *shell_get_default_path(EditBuffer *b, int offset,
                                    char *buf, int buf_size)
{
#if 0
    ShellState *s = qe_get_buffer_mode_data(b, &shell_mode, NULL);

    if (s && (s->curpath[0] || shell_get_curpath(b, offset, s->curpath, sizeof(s->curpath)))) {
        return pstrcpy(buf, buf_size, s->curpath);
    }
#endif
    return shell_get_curpath(b, offset, buf, buf_size);
}

static void do_shell_command(EditState *e, const char *cmd)
{
    char curpath[MAX_FILENAME_SIZE];
    QEmacsState *qs = e->qs;
    EditBuffer *b;

    get_default_path(e->b, e->offset, curpath, sizeof curpath);

    /* create new buffer */
    b = qe_new_shell_buffer(qs, NULL, e, "*shell command output*", NULL,
                            curpath, cmd, SF_COLOR | SF_INFINITE |
                            SF_REUSE_BUFFER | SF_ERASE_BUFFER);
    if (!b)
        return;

    /* XXX: try to split window if necessary */
    switch_to_buffer(e, b);
    // FIXME: if command prompts for input, the buffer should use shell_mode
    edit_set_mode(e, &pager_mode);
}

static void do_compile(EditState *s, const char *cmd)
{
    char curpath[MAX_FILENAME_SIZE];
    QEmacsState *qs = s->qs;
    EditBuffer *b;

    if (s->flags & (WF_POPUP | WF_MINIBUF))
        return;

    get_default_path(s->b, s->offset, curpath, sizeof curpath);

    if (s->flags & WF_POPLEFT) {
        /* avoid messing with the dired pane */
        s = find_window(s, KEY_RIGHT, s);
        qs->active_window = s;
    }

    if (!cmd || !*cmd)
        cmd = "make";

    /* create new buffer */
    b = qe_new_shell_buffer(qs, NULL, s, "*compilation*", "Compilation",
                            curpath, cmd, SF_COLOR | SF_INFINITE |
                            SF_REUSE_BUFFER | SF_ERASE_BUFFER);
    if (!b)
        return;

    b->data_type_name = "compile";
    /* XXX: try to split window if necessary */
    switch_to_buffer(s, b);
    edit_set_mode(s, &pager_mode);
    set_error_offset(b, 0);
}

static void do_next_error(EditState *s, int arg, int dir)
{
    QEmacsState *qs = s->qs;
    EditState *e;
    EditBuffer *b;
    int offset, found_offset;
    char filename[MAX_FILENAME_SIZE];
    char fullpath[MAX_FILENAME_SIZE];
    buf_t fnamebuf, *fname;
    int line_num, col_num, len;
    char32_t c;
    char error_message[128];

    if (arg != NO_ARG) {
        /* called with a prefix: set the error source to the current buffer */
        // XXX: has_arg should scan for errors from the next file
        set_error_offset(s->b, s->offset);
    }

    /* CG: should have a buffer flag for error source.
     * first check if current buffer is an error source.
     * if not, then scan for appropriate error source
     * in buffer least recently used order
     */

    if ((b = qe_find_buffer_name(qs, error_buffer)) == NULL) {
        if ((b = qe_find_buffer_name(qs, "*compilation*")) == NULL
        &&  (b = qe_find_buffer_name(qs, "*shell*")) == NULL
        &&  (b = qe_find_buffer_name(qs, "*errors*")) == NULL) {
            put_error(s, "No compilation buffer");
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
                put_error(s, "No more errors");
                return;
            }
        } else {
            if (offset <= 0) {
                put_error(s, "No previous error");
                return;
            }
            offset = eb_prev_line(b, offset);
        }
        found_offset = offset;
        /* parse filename:linenum:message */
        /* and:  filename(linenum[, col]) ...: ... */

        /* extract filename */
        fname = buf_init(&fnamebuf, filename, countof(filename));
        for (;;) {
            c = eb_nextc(b, offset, &offset);
            if (c == '\n' || c == '\t' || c == ' ')
                goto next_line;
            if (c == ':' || c == '(')
                break;
            buf_putc_utf8(fname, c);
        }

        /* XXX: should find directory backward from error offset */
        canonicalize_absolute_buffer_path(b, found_offset,
                                          fullpath, sizeof(fullpath), filename);

        /* extract line number */
        for (line_num = col_num = 0;;) {
            c = eb_nextc(b, offset, &offset);
            if (c == ':' || c == ',' || c == '.' || c == ')')
                break;
            if (!qe_isdigit(c))
                goto next_line;
            line_num = line_num * 10 + c - '0';
        }
        if (c == ':' || c == ',' || c == '.') {
            int offset0 = offset;
            char32_t c0 = c;
            for (;;) {
                c = eb_nextc(b, offset, &offset);
                if (c == ' ') continue;
                if (!qe_isdigit(c))
                    break;
                col_num = col_num * 10 + c - '0';
            }
            if (col_num == 0) {
                offset = offset0;
                c = c0;
            }
        }
        while (c != ':') {
            if (c == '\n')
                goto next_line;
            c = eb_nextc(b, offset, &offset);
        }
        len = eb_fgets(b, error_message, sizeof(error_message), offset, &offset);
        error_message[len] = '\0';   /* strip the trailing newline if any */
        if (line_num >= 1) {
            if (line_num != error_line_num
            ||  col_num != error_col_num
            ||  !strequal(fullpath, error_filename)) {
                error_line_num = line_num;
                error_col_num = col_num;
                pstrcpy(error_filename, sizeof(error_filename), fullpath);
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
    /* XXX: should check for file existence */
    do_find_file(s, fullpath, 0);
    do_goto_line(qs->active_window, line_num, col_num);

    if (!qs->first_transient_key) {
        qe_register_transient_binding(qs, "next-error", "M-n");
        qe_register_transient_binding(qs, "previous-error", "M-p");
    }

    put_status(s, "=> %s", error_message);
}

static int match_digits(const char32_t *buf, int n, char32_t sep) {
    if (n >= 2 && qe_isdigit(buf[0])) {
        int i = 1;
        while (i < n && qe_isdigit(buf[i]))
            i++;
        if (buf[i] == sep) {
            return i + 1;
        }
    }
    return 0;
}

static int match_string(const char32_t *buf, int n, const char *str) {
    int i;
    for (i = 0; i < n && str[i] && buf[i] == (u8)str[i]; i++)
        continue;
    return (str[i] == '\0') ? i : 0;
}

static int shell_grab_filename(const char32_t *buf, int n,
                               char *dest, int size, int filter)
{
    int i, j, len = 0;
    for (i = 0; i < n; i++) {
        char32_t c = buf[i];
        if (filter) {
            if (c == '(')
                break;
            if (c == ':' && i > 1)
                break;
            if (c == '-' && i > 1) {
                /* match -[0-9]+- for grep -[ABC] output */
                for (j = i + 1; qe_isdigit(buf[j]); j++)
                    continue;
                if (j > i + 1 && buf[j] == '-')
                    break;
            }
        }
        if (qe_isspace(c)) {
            if (len)
                break;
            continue;
        }
        if (len + 1 < size) {
            dest[len++] = c;
        }
    }
    if (size)
        dest[len] = '\0';
    return i;
}

#define STATE_SHELL_SHIFT  7
#define STATE_SHELL_MODE   0x001F
#define STATE_SHELL_SKIP   0x0020
#define STATE_SHELL_KEEP   0x0040
#define STATE_SHELL_MASK   ((1 << STATE_SHELL_SHIFT) - 1)

static ModeDef *mode_cache[STATE_SHELL_MODE + 1];
static int mode_cache_len = 1;

static int qe_shell_find_mode(QEmacsState *qs, const char *filename) {
    ModeDef *m = qe_find_mode_filename(qs, filename, MODEF_SYNTAX);
    int i;

    for (i = 0; i < mode_cache_len; i++) {
        if (m == mode_cache[i])
            return i;
    }
    if (i == countof(mode_cache))
        return 0;

    mode_cache[i] = m;
    mode_cache_len = i + 1;
    return i;
}

void shell_colorize_line(QEColorizeContext *cp,
                         const char32_t *str, int n,
                         QETermStyle *sbuf, ModeDef *syn)
{
    /* detect match lines for known languages and colorize accordingly */
    char filename[MAX_FILENAME_SIZE];
    ModeDef *m;
    int i = 0, start = 0;

    if (qe_isspace(str[0])) {
        if (cp->colorize_state & STATE_SHELL_SKIP)
            start = 1;
    } else {
        /* Detect patches and colorize according to filename */
        if (str[0] == '+' || str[0] == '-') {
            if (match_string(str, n, "+++ ") || match_string(str, n, "--- ")) {
                shell_grab_filename(str + 4, n - 4, filename, countof(filename), FALSE);
                cp->colorize_state = qe_shell_find_mode(cp->s->qs, filename);
                cp->colorize_state |= STATE_SHELL_SKIP | STATE_SHELL_KEEP;
                return;
            } else {
                start = 1;
            }
        } else
        if (str[0] == '<' || str[0] == '>') {
            if (str[1] == ' ')
                start = 2;
            else
                return;
        } else
        if (match_string(str, n, "diff ") || match_string(str, n, "Only in ")) {
            return;
        } else
        if (match_string(str, n, "@@")) {
            /* patch diff location lines */
            cp->colorize_state &= STATE_SHELL_MASK;  /* reset potential comment state */
            return;
        } else
        if (match_string(str, n, "==> ")) {
            /* head and tail file marker */
            i = 4;
            i += shell_grab_filename(str + i, n - i, filename, countof(filename), FALSE);
            if (match_string(str + i, n - i, " <==")) {
                cp->colorize_state = qe_shell_find_mode(cp->s->qs, filename);
                cp->colorize_state |= STATE_SHELL_KEEP;
                return;
            }
        } else {
            /* Detect compiler messages and colorize according to filename */
            while (i < n) {
                int w;
                char32_t c = str[i];
                if (qe_isspace(c)) {
                    i++;
                    if (match_string(str + i, n - i, "> ")
                    ||  match_string(str + i, n - i, "$ ")) {
                        static const char * const commands[] = {
                            "diff ", "head ", "tail ", "cat ", NULL
                        };
                        int k;
                        /* Detect invocations of diff, grep, d... */
                        i += 2;
                        /* This looks like a prompt: reset the current mode */
                        //cp->colorize_state = 0;
                        for (k = 0; commands[k]; k++) {
                            if ((w = match_string(str + i, n - i, commands[k])) != 0) {
                                i += w;
                                shell_grab_filename(str + i, n - i, filename, countof(filename), FALSE);
                                cp->colorize_state = qe_shell_find_mode(cp->s->qs, filename);
                                cp->colorize_state |= STATE_SHELL_KEEP;
                                start = n;
                                return;
                            }
                        }
                    }
                } else {
                    int mc;
                    w = shell_grab_filename(str + i, n - i, filename, countof(filename), TRUE);
                    if (i == 0) {
                        char *p = strchr(filename, '@');
                        if (p != NULL && p > filename && p[-1] != ' ') {
                            /* This looks like a prompt: reset the current mode */
                            cp->colorize_state = 0;
                            return;
                        }
                    }
                    if (w <= 0) {
                        i++;
                        continue;
                    }
                    i += w;
                    mc = qe_shell_find_mode(cp->s->qs, filename);
                    if (!mc)
                        continue;
                    c = str[i];
                    if (c == '(') {
                        /* this is an old style filename position */
                        i += match_digits(str + i, n - i, ')');
                        i += (str[i] == ':');
                        cp->colorize_state = mc;
                        start = i;
                        break;
                    }
                    /* colorize compiler and grep -[ABC] output */
                    if (c == ':' || (c == '-' && qe_isdigit(str[i + 1]))) {
                        /* this is a compiler message */
                        i += match_digits(str + i, n - i, c); /* line number */
                        i += match_digits(str + i, n - i, c); /* optional col number */
                        start = i;
                        cp->colorize_state = mc;
                        if (match_string(str + i, n - i, " error:")
                        ||  match_string(str + i, n - i, " note:")
                        ||  match_string(str + i, n - i, " warning:")) {
                            /* clang diagnostic, will colorize the next line */
                            start = n;
                            return;
                        }
                        break;
                    }
                }
            }
        }
    }
    if ((cp->colorize_state & STATE_SHELL_MODE)
    &&  (m = mode_cache[cp->colorize_state & STATE_SHELL_MODE]) != NULL) {
        int save_state = cp->colorize_state;
        cp->colorize_state >>= STATE_SHELL_SHIFT;
        cp->partial_file++;
        cp_colorize_line(cp, str, start, n, sbuf, m);
        cp->partial_file--;
        if (save_state & STATE_SHELL_KEEP) {
            cp->colorize_state <<= STATE_SHELL_SHIFT;
            cp->colorize_state |= save_state & STATE_SHELL_MASK;
        } else {
            cp->colorize_state = 0;
        }
        cp->combine_stop = start;
        /* Colorize trailing blanks if colorizing shell output */
        for (i = n; i > start && qe_isblank(str[i - 1]); i--) {
            SET_STYLE1(sbuf, i - 1, QE_STYLE_BLANK_HILITE);
        }
    } else {
        cp->colorize_state = 0;
    }
}

/* shell mode specific commands */
static const CmdDef shell_commands[] = {
    CMD0( "shell-toggle-input", "C-o",
          "Toggle between shell input and buffer navigation",
          do_shell_toggle_input)
    /* XXX: should have shell-execute-line on M-RET */
    CMD2( "shell-enter", "RET, LF",
          "Shell buffer RET key",
          do_shell_newline, ES, "*")
    /* CG: should send s->kbs */
    CMD2( "shell-backward-delete-char", "DEL",
          "Shell buffer DEL key",
          do_shell_backspace, ES, "*")
    CMD0( "shell-intr", "C-c C-c",
          "Shell buffer ^C key",
          do_shell_intr)
    CMD2( "shell-delete-char", "C-d, delete",
          "Shell buffer delete char",
          do_shell_delete_char, ES, "*")
    CMD3( "shell-kill-word", "M-d",
          "Shell buffer delete word",
          do_shell_kill_word, ESi, "v", 1)
    CMD3( "shell-backward-kill-word", "M-DEL, M-C-h",
          "Shell buffer delete word backward",
          do_shell_kill_word, ESi, "v", -1)
    CMD1( "shell-previous", "M-p",
          "Shell buffer previous command",
          shell_previous_next, -1)
    CMD1( "shell-next", "M-n",
          "Shell buffer next command",
          shell_previous_next, 1)
    CMD0( "shell-exchange-point-and-mark", "C-x C-x",
          "Shell buffer ^X^X",
          shell_exchange_point_and_mark)
    CMD2( "shell-tabulate", "TAB",
          "Shell buffer TAB key",
          do_shell_tabulate, ES, "*")
    CMD1( "shell-refresh", "C-l",
          "Refresh shell buffer window and update terminal size",
         do_shell_refresh, SR_UPDATE_SIZE | SR_REFRESH)
    CMD1( "shell-search-backward", "C-r",
          "Shell buffer ^R key",
          do_shell_search, -1)
    CMD1( "shell-search-forward", "C-s",
          "Shell buffer ^S key",
          do_shell_search, 1)
    CMD2( "shell-kill-line", "C-k",
          "Shell buffer kill line",
          do_shell_kill_line, ESi, "P")
    CMD2( "shell-kill-beginning-of-line", "M-k",
          "Shell buffer kill beginning of line",
          do_shell_kill_beginning_of_line, ESi, "P")
    CMD2( "shell-yank", "C-y",
          "Shell buffer yank",
          do_shell_yank, ES, "*")
    CMD3( "shell-capitalize-word", "M-c",
          "Shell buffer capitalize word",
          do_shell_changecase_word, ESi, "*" "v", 2)
    CMD3( "shell-downcase-word", "M-l",
          "Shell buffer downcase word",
          do_shell_changecase_word, ESi, "*" "v", -1)
    CMD3( "shell-upcase-word", "M-u",
          "Shell buffer upcase",
          do_shell_changecase_word, ESi, "*" "v", 1)
    CMD3( "shell-transpose-chars", "C-t",
          "Shell buffer ^T key",
          do_shell_transpose, ESi, "*" "v", CMD_TRANSPOSE_CHARS)
    CMD3( "shell-transpose-words", "M-t",
          "Shell buffer transpose words",
          do_shell_transpose, ESi, "*" "v", CMD_TRANSPOSE_WORDS)
};

/* shell global commands */
static const CmdDef shell_global_commands[] = {
    CMD2( "shell", "C-x RET RET, C-x LF LF",
          "Start a shell buffer or move to the last shell buffer used",
          do_shell, ESi, "p")
    CMD2( "shell-command", "M-!",
          "Run a shell command and display a new buffer with its collected output",
          do_shell_command, ESs,
          "s{Shell command: }|shell-command|")
    CMD2( "ssh", "",
          "Start a shell buffer with a new remote shell connection",
          do_ssh, ESs,
          "s{Open connection to (host or user@host: }|ssh|")
    CMD2( "compile", "C-x C-e",
          "Run a compiler command and display a new buffer with its collected output",
          do_compile, ESs,
          "s{Compile command: }|compile|")
    CMD2( "make", "C-x m",
          "Run make and display a new buffer with its collected output",
          do_compile, ESs,
          "@{make}")
    CMD2( "man", "",
          "Run man for a command and display a new buffer with its collected output",
          do_man, ESs,
          "s{Show man page for: }|man|")
    CMD3( "next-error", "C-x C-n, C-x `, M-g n, M-g M-n",
          "Move to the next error from the last shell command output",
          do_next_error, ESii, "P" "v", +1)
    CMD3( "previous-error", "C-x C-p, M-g p, M-g M-p",
          "Move to the previous error from the last shell command output",
          do_next_error, ESii, "P" "v", -1)
};

static int shell_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    ShellState *s = qe_get_buffer_mode_data(p->b, &shell_mode, NULL);

    if (s && s->shell_flags & SF_INTERACTIVE)
        return 100;

    return 0;
}

static int shell_mode_init(EditState *e, EditBuffer *b, int flags)
{
    if (e) {
        ShellState *s;

        if (!(s = shell_get_state(e, 1)))
            return -1;

        e->b->tab_width = 8;
        /* XXX: should come from mode.default_wrap */
        e->wrap = WRAP_TERM;
        e->wrap_cols = s->cols;
        if ((s->shell_flags & SF_INTERACTIVE) && !s->grab_keys)
            e->interactive = 1;
    }
    return 0;
}

static int pager_mode_init(EditState *e, EditBuffer *b, int flags)
{
    if (e) {
        e->b->tab_width = 8;
        /* XXX: should come from mode.default_wrap */
        e->wrap = WRAP_TRUNCATE;
    }
    return 0;
}

/* additional mode specific bindings */
static const char * const pager_bindings[] = {
    "DEL", "scroll-down",
    "SPC", "scroll-up",
    "/", "search-forward",
    NULL
};

static int shell_init(QEmacsState *qs)
{
    /* populate and register shell mode and commands */
    // XXX: remove this mess: should just inherit with fallback
    memcpy(&shell_mode, &text_mode, offsetof(ModeDef, first_key));
    shell_mode.name = "shell";
    shell_mode.flags |= MODEF_NO_TRAILING_BLANKS;
    shell_mode.mode_probe = shell_mode_probe;
    shell_mode.colorize_func = shell_colorize_line;
    shell_mode.buffer_instance_size = sizeof(ShellState);
    shell_mode.mode_init = shell_mode_init;
    shell_mode.mode_free = shell_mode_free;
    shell_mode.display_hook = shell_display_hook;
    shell_mode.move_left_right = shell_move_left_right;
    shell_mode.move_word_left_right = shell_move_word_left_right;
    shell_mode.move_up_down = shell_move_up_down;
    shell_mode.scroll_up_down = shell_scroll_up_down;
    shell_mode.move_bol = shell_move_bol;
    shell_mode.move_eol = shell_move_eol;
    shell_mode.move_bof = shell_move_bof;
    shell_mode.move_eof = shell_move_eof;
    shell_mode.write_char = shell_write_char;
    shell_mode.delete_bytes = shell_delete_bytes;
    shell_mode.get_default_path = shell_get_default_path;

    qe_register_mode(qs, &shell_mode, MODEF_NOCMD | MODEF_VIEW);
    qe_register_commands(qs, &shell_mode, shell_commands, countof(shell_commands));
    qe_register_commands(qs, NULL, shell_global_commands, countof(shell_global_commands));

    /* populate and register pager mode and commands */
    // XXX: remove this mess: should just inherit with fallback
    memcpy(&pager_mode, &text_mode, offsetof(ModeDef, first_key));
    pager_mode.name = "pager";
    pager_mode.mode_probe = NULL;
    pager_mode.mode_init = pager_mode_init;
    pager_mode.bindings = pager_bindings;

    qe_register_mode(qs, &pager_mode, MODEF_NOCMD | MODEF_VIEW);

    return 0;
}

qe_module_init(shell_init);
