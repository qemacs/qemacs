/*
 * Shell mode for QEmacs.
 * Copyright (c) 2001, 2002 Fabrice Bellard.
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
#include <ctype.h>
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
    TTY_STATE_CSI,
};

typedef struct ShellState {
    /* buffer state */
    int pty_fd;
    int pid; /* -1 if not launched */
    int color, def_color;
    int cur_offset; /* current offset at position x, y */
    int esc_params[MAX_ESC_PARAMS];
    int nb_esc_params;
    int state;
    EditBuffer *b;
    EditBuffer *b_color; /* color buffer, one byte per char */
    int is_shell; /* only used to display final message */
} ShellState;

static int shell_get_colorized_line(EditState *e, unsigned int *buf, int buf_size,
                                    int offset, int line_num);

/* move to mode */
static int shell_launched = 0;

static int shell_mode_init(EditState *s, ModeSavedData *saved_data)
{
    s->tab_size = 8;
    s->wrap = WRAP_TRUNCATE;
    s->interactive = 1;
    set_colorize_func(s, NULL);
    s->get_colorized_line_func = shell_get_colorized_line;
    return 0;
}

#define	PTYCHAR1 "pqrstuvwxyz"
#define	PTYCHAR2 "0123456789abcdef"

/* allocate one pty/tty pair */
static int get_pty(char *tty_str)
{
   int fd;
   char ptydev[] = "/dev/pty??";
   char ttydev[] = "/dev/tty??";
   int len = strlen (ttydev);
   char *c1, *c2;

   for (c1 = PTYCHAR1; *c1; c1++) {
       ptydev [len-2] = ttydev [len-2] = *c1;
       for (c2 = PTYCHAR2; *c2; c2++) {
           ptydev [len-1] = ttydev [len-1] = *c2;
           if ((fd = open (ptydev, O_RDWR)) >= 0) {
               if (access (ttydev, R_OK|W_OK) == 0) {
                   strcpy(tty_str, ttydev);
                   return fd;
               }
               close (fd);
           }
       }
   }
   return -1;
}

static int run_process(const char *path, char **argv, 
                       int *fd_ptr, int *pid_ptr)
{
    int pty_fd, pid, i, nb_fds;
    char tty_name[32];
    struct winsize ws;

    pty_fd = get_pty(tty_name);
    fcntl(pty_fd, F_SETFL, O_NONBLOCK);
    if (pty_fd < 0)
        return -1;

    /* set dummy screen size */
    ws.ws_col = 80;
    ws.ws_row = 25;
    ws.ws_xpixel = ws.ws_col;
    ws.ws_ypixel = ws.ws_row;
    ioctl(pty_fd, TIOCSWINSZ, &ws);
    
    pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        /* child process */
        nb_fds = getdtablesize();
        for(i=0;i<nb_fds;i++)
            close(i);
        /* open pseudo tty for standard i/o */
        open(tty_name, O_RDWR);
        dup(0);
        dup(0);

        setsid();

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
    s->state = TTY_STATE_NORM;
    s->cur_offset = 0;
    s->def_color = TTY_GET_COLOR(7, 0);
    s->color = s->def_color;
}

static void tty_write(ShellState *s, unsigned char *buf, int len)
{
    int ret;

    if (len < 0)
        len = strlen(buf);
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

/* compute offset the char at 'x' and 'y'. Can insert spaces or lines
   if needed */
/* XXX: optimize !!!!! */
static void tty_gotoxy(ShellState *s, int x, int y)
{
    int total_lines, line_num, col_num, offset, offset1, c;
    unsigned char buf1[10];

    /* compute offset */
    eb_get_pos(s->b, &total_lines, &col_num, s->b->total_size);
    line_num = total_lines - TTY_YSIZE;
    if (line_num < 0)
        line_num = 0;
    line_num += y;
    /* add lines if necessary */
    while (line_num >= total_lines) {
        buf1[0] = '\n';
        eb_insert(s->b, s->b->total_size, buf1, 1);
        total_lines++;
    }
    offset = eb_goto_pos(s->b, line_num, 0);
    for(;x > 0; x--) {
        c = eb_nextc(s->b, offset, &offset1);
        if (c == '\n') {
            buf1[0] = ' ';
            for(;x > 0; x--) {
                eb_insert(s->b, offset, buf1, 1);
            }
            break;
        } else {
            offset = offset1;
        }
    }
    s->cur_offset = offset;
}

void tty_csi_m(ShellState *s, int c)
{
    /* we handle only a few possible modes */
    switch(c) {
    case 0:
        s->color = s->def_color;
        break;
    default:
        if (c >= 30 && c <= 37)
            s->color = TTY_GET_COLOR(c - 30, TTY_GET_BG(s->color));
        else if (c >= 40 && c <= 47) {
            s->color = TTY_GET_COLOR(TTY_GET_FG(s->color), c - 40);
        }
        break;
    }
}


/* Well, almost a hack to update cursor */
static void tty_update_cursor(ShellState *s)
{
    QEmacsState *qs = &qe_state;
    EditState *e;

    if (s->cur_offset == -1)
        return;

    for(e = qs->first_window; e != NULL; e = e->next_window) {
        if (s->b == e->b && e->interactive) {
            e->offset = s->cur_offset;
        }
    }
}

static void tty_emulate(ShellState *s, int c)
{
    int i, offset, offset1, offset2, n;
    unsigned char buf1[10];
    
    switch(s->state) {
    case TTY_STATE_NORM:
        switch(c) {
        case 8:
            {
                int c1;
                c1 = eb_prevc(s->b, s->cur_offset, &offset);
                if (c1 != '\n')
                    s->cur_offset = offset;
            }
            break;
        case 10:
            /* go to next line */
            offset = s->cur_offset;
            for(;;) {
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
        case 13:
            /* move to bol */
            for(;;) {
                c = eb_prevc(s->b, s->cur_offset, &offset1);
                if (c == '\n')
                    break;
                s->cur_offset = offset1;
            }
            break;
        case 27:
            s->state = TTY_STATE_ESC;
            break;
        default:
            if (c >= 32 || c == 9) {
                int c1, cur_len, len;
                /* write char (should factorize with do_char() code */
                len = unicode_to_charset(buf1, c, s->b->charset);
                c1 = eb_nextc(s->b, s->cur_offset, &offset);
                /* XXX: handle tab case */
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
            for(i=0;i<MAX_ESC_PARAMS;i++)
                s->esc_params[i] = 0;
            s->nb_esc_params = 0;
            s->state = TTY_STATE_CSI;
        } else {
            s->state = TTY_STATE_NORM;
        }
        break;
    case TTY_STATE_CSI:
        if (c >= '0' && c <= '9') {
            if (s->nb_esc_params < MAX_ESC_PARAMS) {
                s->esc_params[s->nb_esc_params] = 
                    s->esc_params[s->nb_esc_params] * 10 + c - '0';
            }
        } else {
            s->nb_esc_params++;
            if (c == ';')
                break;
            s->state = TTY_STATE_NORM;
            switch(c) {
            case 'H':
                /* goto xy */
                {
                    int x, y;
                    y = s->esc_params[0] - 1;
                    x = s->esc_params[1] - 1;
                    if (y < 0)
                        y = 0;
                    else if (y >= TTY_YSIZE)
                        y = TTY_YSIZE - 1;
                    if (x < 0)
                        x = 0;
                    tty_gotoxy(s, x, y);
                }
                break;
            case 'K':
                /* clear to eol */
                offset1 = s->cur_offset;
                for(;;) {
                    c = eb_nextc(s->b, offset1, &offset2);
                    if (c == '\n')
                        break;
                    offset1 = offset2;
                }
                eb_delete(s->b, s->cur_offset, offset1 - s->cur_offset);
                break;
            case 'P':
                /* delete chars */
                n = s->esc_params[0];
                if (n <= 0)
                    n = 1;
                offset1 = s->cur_offset;
                for(;n > 0;n--) {
                    c = eb_nextc(s->b, offset1, &offset2);
                    if (c == '\n')
                        break;
                    offset1 = offset2;
                }
                eb_delete(s->b, s->cur_offset, offset1 - s->cur_offset);
                break;
            case '@':
                /* insert chars */
                n = s->esc_params[0];
                if (n <= 0)
                    n = 1;
                buf1[0] = ' ';
                for(;n > 0;n--) {
                    eb_insert(s->b, s->cur_offset, buf1, 1);
                }
                break;
            case 'm':
                /* colors */
                n = s->nb_esc_params;
                if (n == 0)
                    n = 1;
                for(i=0;i<n;i++)
                    tty_csi_m(s, s->esc_params[i]);
                break;
            case 'n':
                if (s->esc_params[0] == 6) {
                    /* XXX: send cursor position, just to be able to
                       launch qemacs in qemacs ! */
                    char buf2[20];
                    snprintf(buf2, sizeof(buf2), "\033[%d;%dR", 1, 1);
                    tty_write(s, buf2, -1);
                }
                break;
            default:
                break;
            }
        }
        break;
    }
    tty_update_cursor(s);
}

/* modify the color according to the current one (may be incorrect if
   we are editing because we should write default color) */
static void shell_color_callback(EditBuffer *b,
                                 void *opaque,
                                 enum LogOperation op,
                                 int offset,
                                 int size)
{
    ShellState *s = opaque;
    unsigned char buf[32];
    int len;

    switch(op) {
    case LOGOP_WRITE:
        while (size > 0) {
            len = size;
            if (len > sizeof(buf))
                len = sizeof(buf);
            memset(buf, s->color, len);
            eb_write(s->b_color, offset, buf, len);
            size -= len;
            offset += len;
        }
        break;
    case LOGOP_INSERT:
        while (size > 0) {
            len = size;
            if (len > sizeof(buf))
                len = sizeof(buf);
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

static int shell_get_colorized_line(EditState *e, unsigned int *buf, int buf_size,
                                    int offset, int line_num)
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
    for(;;) {
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
    unsigned char buf[1024];
    int len, i;

    len = read(s->pty_fd, buf, sizeof(buf));
    if (len <= 0)
        return;
    
    for(i=0;i<len;i++) tty_emulate(s, buf[i]);

    /* now we do some refresh */
    edit_display(&qe_state);
    dpy_flush(qe_state.screen);
}

void shell_pid_cb(void *opaque, int status)
{
    ShellState *s = opaque;
    EditBuffer *b = s->b;
    QEmacsState *qs = &qe_state;
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
    for(e = qs->first_window; e != NULL; e = e->next_window) {
        if (e->b == b)
            e->interactive = 0;
    }
    edit_display(&qe_state);
    dpy_flush(qe_state.screen);
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
            while (waitpid(s->pid, &status, 0) != s->pid);
        }
        s->pid = -1;
    }
    if (s->pty_fd >= 0) {
        set_read_handler(s->pty_fd, NULL, NULL);
    }
    free(s);
}

EditBuffer *new_shell_buffer(const char *name, 
                             const char *path, char **argv, int is_shell)
{
    ShellState *s;
    EditBuffer *b, *b_color;

    b = eb_new("", BF_SAVELOG);
    if (!b)
        return NULL;
    set_buffer_name(b, name); /* ensure that the name is unique */

    s = malloc(sizeof(ShellState));
    if (!s) {
        eb_free(b);
        return NULL;
    }
    b->priv_data = s;
    b->close = shell_close;
    eb_add_callback(b, eb_offset_callback, &s->cur_offset);
    s->b = b;
    s->pid = -1;
    s->is_shell = is_shell;
    tty_init(s);

    /* add color buffer */
    if (is_shell) {
        b_color = eb_new("*color*", BF_SYSTEM);
        if (!b_color) {
            eb_free(b);
            free(s);
            return NULL;
        }
        /* no undo info in this color buffer */
        b_color->save_log = 0;
        eb_add_callback(b, shell_color_callback, s);
        s->b_color = b_color;
    }

    /* launch shell */
    if (run_process(path, argv, 
                    &s->pty_fd, &s->pid) < 0) {
        eb_free(b);
        return NULL;
    }

    set_read_handler(s->pty_fd, shell_read_cb, s);
    set_pid_handler(s->pid, shell_pid_cb, s);
    return b;
}


static void do_shell(EditState *e)
{
    EditBuffer *b;
    char *argv[3];
    char *shell_path;

    /* find shell name */
    shell_path = getenv("SHELL");
    if (!shell_path)
        shell_path = "/bin/sh";

    /* create new buffer */
    argv[0] = shell_path;
    argv[1] = NULL;
    b = new_shell_buffer("*shell*", shell_path, argv, 1);
    if (!b)
        return;
    
    switch_to_buffer(e, b);
    do_set_mode(e, &shell_mode, NULL);

    put_status(e, "Press C-o to toggle between shell/edit mode");
    shell_launched = 1;
}

void shell_move_left_right(EditState *e, int dir)
{
    if (e->interactive) {
        ShellState *s = e->b->priv_data;
        if (dir > 0)
            tty_write(s, "\033[C", -1);
        else
            tty_write(s, "\033[D", -1);
    } else {
        text_move_left_right_visual(e, dir);
    }
}

void shell_move_up_down(EditState *e, int dir)
{
    if (e->interactive) {
        ShellState *s = e->b->priv_data;

        if (dir > 0)
            tty_write(s, "\033[B", -1);
        else
            tty_write(s, "\033[A", -1);
    } else {
        text_move_up_down(e, dir);
    }
}

void shell_move_bol(EditState *e)
{
    if (e->interactive) {
        ShellState *s = e->b->priv_data;
        unsigned char ch;
        ch = 1;
        tty_write(s, &ch, 1);
    } else {
        text_move_bol(e);
    }
}

void shell_move_eol(EditState *e)
{
    if (e->interactive) {
        ShellState *s = e->b->priv_data;
        unsigned char ch;
        ch = 5;
        tty_write(s, &ch, 1);
    } else {
        text_move_eol(e);
    }
}

void shell_write_char(EditState *e, int c)
{
    unsigned char ch;

    if (e->interactive) {
        ShellState *s = e->b->priv_data;

        ch = c;
        tty_write(s, &ch, 1);
    } else {
        switch(c) {
        case 4:
            do_delete_char(e);
            break;
        case 9:
            do_tab(e);
            break;
        case 11:
            do_kill_region(e, 2);
            break;
        case 127:
            do_backspace(e);
            break;
        case '\r':
            do_return(e);
            break;
        default:
            text_write_char(e, c);
            break;
        }
    }
}

void do_shell_toggle_input(EditState *e)
{
    e->interactive = !e->interactive;
    if (e->interactive) {
        ShellState *s = e->b->priv_data;
        tty_update_cursor(s);
    }
}

static int error_offset = -1;
static int last_line_num = -1;
static char last_filename[1024];

static void do_compile(EditState *e, const char *cmd)
{
    char *argv[4];
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
    b = new_shell_buffer("*compilation*", "/bin/sh", argv, 0);
    if (!b)
        return;
    
    /* XXX: try to split window if necessary */
    switch_to_buffer(e, b);
}

static void do_compile_error(EditState *s, int dir)
{
    QEmacsState *qs = &qe_state;
    EditState *e;
    EditBuffer *b;
    int offset, offset1, found_offset;
    char filename[1024], *q;
    int line_num, c;

    b = eb_find("*compilation*");
    if (!b) {
        b = eb_find("*shell*");
        if (!b) {
            put_status(s, "No compilation buffer");
            return;
        }
    }
    /* find next/prev error */
    offset = error_offset;
    if (offset < 0) {
        offset = 0;
        goto find_error;
    }
    for(;;) {
        if (dir > 0) {
            if (offset >= b->total_size) {
                put_status(s, "No more errors");
                return;
            }
            for(;;) {
                c = eb_nextc(b, offset, &offset);
                if (c == '\n')
                    break;
            }
        } else {
            if (offset <= 0) {
                put_status(s, "No previous error");
                return;
            }
            eb_prevc(b, offset, &offset);
            for(;;) {
                c = eb_prevc(b, offset, &offset1);
                if (c == '\n')
                    break;
                offset = offset1;
            }
        }
    find_error:
        found_offset = offset;
        /* extract filename */
        q = filename;
        for(;;) {
            c = eb_nextc(b, offset, &offset);
            if (c == '\n' || c == '\t' || c == ' ')
                goto next_line;
            if (c == ':')
                break;
            if ((q - filename) < sizeof(filename) - 1)
                *q++ = c;
        }
        *q = '\0';
        /* extract line number */
        line_num = 0;
        for(;;) {
            c = eb_nextc(b, offset, &offset);
            if (c == ':')
                break;
            if (!isdigit(c))
                goto next_line;
            line_num = line_num * 10 + c - '0';
        }
        if (line_num >= 1) {
            if (line_num != last_line_num ||
                strcmp(filename, last_filename) != 0) {
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
    for(e = qs->first_window; e != NULL; e = e->next_window) {
        if (e->b == b) {
            e->offset = error_offset;
        }
    }

    /* go to the error */
    do_load(s, filename);
    do_goto_line(s, line_num);
}

/* specific shell commands */
static CmdDef shell_commands[] = {
    CMD0( KEY_NONE, KEY_NONE, "shell", do_shell)
    CMD0( KEY_CTRL('o'), KEY_NONE, "shell-toggle-input", do_shell_toggle_input)
    CMD1( '\r', KEY_NONE, "shell-return", shell_write_char, '\r')
    CMD1( 127, KEY_NONE, "shell-backward-delete-char", shell_write_char, 127)
    CMD1( KEY_CTRL('d'), KEY_DELETE, "shell-delete-char", shell_write_char, 4)
    CMD1( KEY_CTRL('i'), KEY_NONE, "shell-tabulate", shell_write_char, 9)
    CMD1( KEY_CTRL('k'), KEY_NONE, "shell-kill-line", shell_write_char, 11)
    CMD_DEF_END,
};

/* compilation commands */
static CmdDef compile_commands[] = {
    CMD( KEY_CTRLX(KEY_CTRL('e')), KEY_NONE, "compile\0s{Compile command: }", do_compile)
    CMD1( KEY_CTRLX(KEY_CTRL('p')), KEY_NONE, "previous-error", 
          do_compile_error, -1)
    CMD1( KEY_CTRLX(KEY_CTRL('n')), KEY_NONE, "next-error", 
          do_compile_error, 1)
    CMD_DEF_END,
};

static int shell_init(void)
{
    /* first register mode */
    memcpy(&shell_mode, &text_mode, sizeof(ModeDef));
    shell_mode.name = "shell";
    shell_mode.mode_probe = NULL;
    shell_mode.mode_init = shell_mode_init;
    shell_mode.move_left_right =  shell_move_left_right;
    shell_mode.move_up_down =  shell_move_up_down;
    shell_mode.move_bol = shell_move_bol;
    shell_mode.move_eol = shell_move_eol;
    shell_mode.write_char =  shell_write_char;
    shell_mode.mode_flags |= MODEF_NOCMD;

    qe_register_mode(&shell_mode);

    /* commands and default keys */
    qe_register_cmd_table(shell_commands, "shell");
    qe_register_cmd_table(compile_commands, NULL);
    
    return 0;
}

qe_module_init(shell_init);
