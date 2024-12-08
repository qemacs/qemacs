/*
 * Unix main loop for QEmacs
 *
 * Copyright (c) 2002, 2003 Fabrice Bellard.
 * Copyright (c) 2000-2024 Charlie Gordon.
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

#include "qe.h"

#ifdef CONFIG_WIN32
#include <winsock.h>
#include <sys/timeb.h>
/* Use a conditional typedef to avoid compilation warning */
typedef u_int fdesc_t;
#else
#include <sys/wait.h>
typedef int fdesc_t;
#endif

/* NOTE: it is strongly inspirated from the 'links' browser API */

typedef struct URLHandler {
    void *read_opaque;
    void (*read_cb)(void *opaque);
    void *write_opaque;
    void (*write_cb)(void *opaque);
} URLHandler;

typedef struct PidHandler {
    struct PidHandler *next, *prev;
    int pid;
    void (*cb)(void *opaque, int status);
    void *opaque;
} PidHandler;

typedef struct BottomHalfEntry {
    struct BottomHalfEntry *next, *prev;
    void (*cb)(void *opaque);
    void *opaque;
} BottomHalfEntry;

struct QETimer {
    void *opaque;
    void (*cb)(void *opaque);
    int timeout;
    struct QETimer *next;
};

static fd_set url_rfds, url_wfds;
static int url_fdmax;
static URLHandler url_handlers[256];
static int url_exit_request;
static int url_display_request;
static LIST_HEAD(pid_handlers);
static LIST_HEAD(bottom_halves);
static QETimer *first_timer;


void set_read_handler(int fd, void (*cb)(void *opaque), void *opaque)
{
    url_handlers[fd].read_cb = cb;
    url_handlers[fd].read_opaque = opaque;
    if (cb) {
        if (fd >= url_fdmax)
            url_fdmax = fd;
        FD_SET((fdesc_t)fd, &url_rfds);
    } else {
        FD_CLR((fdesc_t)fd, &url_rfds);
    }
}

void set_write_handler(int fd, void (*cb)(void *opaque), void *opaque)
{
    url_handlers[fd].write_cb = cb;
    url_handlers[fd].write_opaque = opaque;
    if (cb) {
        if (fd >= url_fdmax)
            url_fdmax = fd;
        FD_SET((fdesc_t)fd, &url_wfds);
    } else {
        FD_CLR((fdesc_t)fd, &url_wfds);
    }
}

/* register a callback which is called when process 'pid'
   terminates. When the callback is set to NULL, it is deleted */
/* XXX: add consistency check ? */
int set_pid_handler(int pid,
                    void (*cb)(void *opaque, int status), void *opaque)
{
    PidHandler *p;

    if (cb == NULL) {
        list_for_each(p, &pid_handlers) {
            if (p->pid == pid) {
                list_del(p);
                qe_free(&p);
                break;
            }
        }
    } else {
        p = qe_mallocz(PidHandler);
        if (!p)
            return -1;
        p->pid = pid;
        p->cb = cb;
        p->opaque = opaque;
        list_add(p, &pid_handlers);
    }
    return 0;
}

/*
 * add an explicit call back to avoid recursions
 */
void register_bottom_half(void (*cb)(void *opaque), void *opaque)
{
    BottomHalfEntry *bh;

    /* Should not fail */
    bh = qe_mallocz(BottomHalfEntry);
    bh->cb = cb;
    bh->opaque = opaque;
    list_add(bh, &bottom_halves);
}

/*
 * remove bottom half
 */
void unregister_bottom_half(void (*cb)(void *opaque), void *opaque)
{
    BottomHalfEntry *bh, *bh1;

    list_for_each_safe(bh, bh1, &bottom_halves) {
        if (bh->cb == cb && bh->opaque == opaque) {
            list_del(bh);
            qe_free(&bh);
        }
    }
}

QETimer *qe_add_timer(int delay, void *opaque, void (*cb)(void *opaque))
{
    QETimer *ti;

    ti = qe_mallocz(QETimer);
    if (!ti)
        return NULL;
    ti->timeout = get_clock_ms() + delay;
    ti->opaque = opaque;
    ti->cb = cb;
    ti->next = first_timer;
    first_timer = ti;
    return ti;
}

void qe_kill_timer(QETimer **tip)
{
    if (*tip) {
        QETimer **pt;

        /* remove timer from list of active timers and free it */
        for (pt = &first_timer; *pt != NULL; pt = &(*pt)->next) {
            if (*pt == (*tip)) {
                *pt = (*tip)->next;
                qe_free(tip);
                return;
            }
        }
        /* timer not found, was probably alread freed */
        *tip = NULL;
    }
}

/* execute stacked bottom halves */
static void qe__call_bottom_halves(void)
{
    BottomHalfEntry *bh;

    while (!list_empty(&bottom_halves)) {
        bh = (BottomHalfEntry *)bottom_halves.prev;
        list_del(bh);
        bh->cb(bh->opaque);
        qe_free(&bh);
    }
}

static inline void call_bottom_halves(void)
{
    if (!list_empty(&bottom_halves))
        qe__call_bottom_halves();
}

/* call timer callbacks and compute maximum next call time to
   check_timers() */
static inline int check_timers(int max_delay)
{
    QETimer *ti, **pt;
    int timeout, cur_time;

    cur_time = get_clock_ms();
    timeout = cur_time + max_delay;
    pt = &first_timer;
    for (;;) {
        ti = *pt;
        if (ti == NULL)
            break;
        if ((ti->timeout - cur_time) <= 0) {
            /* timer expired : suppress it from timer list and call callback */
            *pt = ti->next;
            /* warning: a new timer can be added in the callback */
            ti->cb(ti->opaque);
            qe_free(&ti);
            call_bottom_halves();
        } else {
            if ((ti->timeout - timeout) < 0)
                timeout = ti->timeout;
            pt = &ti->next;
        }
    }
    return timeout - cur_time;
}

static void url_block_reset(void)
{
    FD_ZERO(&url_rfds);
    FD_ZERO(&url_wfds);
    url_fdmax = -1;
    url_exit_request = 0;
}

#define MAX_DELAY 500  /* milliseconds */

/* block until one event */
static void url_block(void)
{
    URLHandler *uh;
    int ret, i, delay;
    fd_set rfds, wfds;
    struct timeval tv;

    delay = check_timers(MAX_DELAY);
#if 0
    {
        static int count;

        printf("%5d: delay=%d\n", count++, delay);
    }
#endif
    tv.tv_sec = delay / 1000;
    tv.tv_usec = (delay % 1000) * 1000;

    rfds = url_rfds;
    wfds = url_wfds;
    ret = select(url_fdmax + 1, &rfds, &wfds, NULL, &tv);

    /* call each handler */
    /* extra checks on callback function pointers because a callback
     * may unregister another callback.  This was causing crash bugs
     * when deleting a running shell output buffer such as a buffer
     * with a huge compressed file while it decompresses.
     * XXX: should break from the loop if callback changed the
     *      structures but need further investigation.
     */
    if (ret > 0) {
        uh = url_handlers;
        for (i = 0;i <= url_fdmax; i++) {
            if (FD_ISSET(i, &rfds) && uh->read_cb) {
                uh->read_cb(uh->read_opaque);
                call_bottom_halves();
            }
            if (FD_ISSET(i, &wfds) && uh->write_cb) {
                uh->write_cb(uh->write_opaque);
                call_bottom_halves();
            }
            uh++;
        }
    }

#ifndef CONFIG_WIN32
    /* handle terminated children */
    for (;;) {
        int pid, status;
        PidHandler *ph, *ph1;

        if (list_empty(&pid_handlers))
            break;
        pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0)
            break;
        list_for_each_safe(ph, ph1, &pid_handlers) {
            if (ph->pid == pid && ph->cb) {
                ph->cb(ph->opaque, status);
                call_bottom_halves();
                break;
            }
        }
    }
#endif
}

int url_main_loop(int (*init)(void *opaque), void *opaque)
{
    QEArgs *ap = opaque;
    url_block_reset();
    if ((*init)(opaque))
        return 1;
    for (;;) {
        if (url_exit_request)
            break;
        url_block();
        if (url_display_request) {
            QEmacsState *qs = ap->qs;

            //qs->complete_refresh = 1;
            do_refresh(qs->first_window);
            qe_display(qs);
            url_display_request = 0;
        }
    }
    return 0;
}

/* exit from url loop */
void url_exit(void)
{
    url_exit_request = 1;
}

/* asynchronous redisplay signal received */
void url_redisplay(void)
{
    url_display_request = 1;
}

int get_clock_ms(void) {
#ifdef CONFIG_WIN32
    struct _timeb tb;

    _ftime(&tb);
    return tb.time * 1000 + tb.millitm;
#else
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + (tv.tv_usec / 1000);
#endif
}

int get_clock_usec(void) {
#ifdef CONFIG_WIN32
    struct _timeb tb;

    _ftime(&tb);
    return tb.time * 1000000 + tb.millitm * 1000;
#else
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
#endif
}

#ifdef __TINYC__

/* the glibc folks use wrappers, but forgot to put a compatibility
   function for non GCC compilers ! */
int stat(__const char *__path,
         struct stat *__statbuf)
{
    return __xstat(_STAT_VER, __path, __statbuf);
}
#endif
