/*
 * Unix main loop for QEmacs
 *
 * Copyright (c) 2002, 2003 Fabrice Bellard.
 * Copyright (c) 2000-2026 Charlie Gordon.
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

typedef struct IOHandler {
    void *read_opaque;
    void (*read_cb)(void *opaque);
    void *write_opaque;
    void (*write_cb)(void *opaque);
} IOHandler;

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

struct URLTimer {
    void *opaque;
    void (*cb)(void *opaque);
    int timeout;
    struct URLTimer *next;
};

struct URLState {
    fd_set rfds, wfds;
    int nfds;
    IOHandler handlers[256];
    int exit_request;
    void (*tail_cb)(void *opaque);
    void *tail_opaque;
    struct list_head pid_handlers;
    struct list_head bottom_halves;
    URLTimer *first_timer;
};

URLState *url_init(void) {
    URLState *up = qe_mallocz(URLState);
    if (up) {
        FD_ZERO(&up->rfds);
        FD_ZERO(&up->wfds);
        QE_LIST_INIT(up->pid_handlers);
        QE_LIST_INIT(up->bottom_halves);
    }
    return up;
}

int url_set_read_handler(URLState *up, int fd, void (*cb)(void *opaque), void *opaque)
{
    if (fd < 0 || fd >= countof(up->handlers))
        return -1;

    up->handlers[fd].read_cb = cb;
    up->handlers[fd].read_opaque = opaque;
    if (cb) {
        if (fd >= up->nfds)
            up->nfds = fd + 1;
        FD_SET((fdesc_t)fd, &up->rfds);
    } else {
        FD_CLR((fdesc_t)fd, &up->rfds);
    }
    return 0;
}

int url_set_write_handler(URLState *up, int fd, void (*cb)(void *opaque), void *opaque)
{
    if (fd < 0 || fd >= countof(up->handlers))
        return -1;

    up->handlers[fd].write_cb = cb;
    up->handlers[fd].write_opaque = opaque;
    if (cb) {
        if (fd >= up->nfds)
            up->nfds = fd + 1;
        FD_SET((fdesc_t)fd, &up->wfds);
    } else {
        FD_CLR((fdesc_t)fd, &up->wfds);
    }
    return 0;
}

/* register a callback which is called when process 'pid'
   terminates. When the callback is set to NULL, it is deleted */
/* XXX: add consistency check ? */
int url_set_pid_handler(URLState *up, int pid, void (*cb)(void *opaque, int status), void *opaque)
{
    PidHandler *p;

    if (cb == NULL) {
        list_for_each(p, &up->pid_handlers) {
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
        list_add(p, &up->pid_handlers);
    }
    return 0;
}

/*
 * add an explicit call back to avoid recursions
 */
int url_register_bottom_half(URLState *up, void (*cb)(void *opaque), void *opaque)
{
    BottomHalfEntry *bh;

    /* Should not fail */
    bh = qe_mallocz(BottomHalfEntry);
    if (!bh)
        return -1;
    bh->cb = cb;
    bh->opaque = opaque;
    list_add(bh, &up->bottom_halves);
    return 0;
}

/*
 * remove bottom half
 */
void url_unregister_bottom_half(URLState *up, void (*cb)(void *opaque), void *opaque)
{
    BottomHalfEntry *bh, *bh1;

    list_for_each_safe(bh, bh1, &up->bottom_halves) {
        if (bh->cb == cb && bh->opaque == opaque) {
            list_del(bh);
            qe_free(&bh);
        }
    }
}

URLTimer *url_add_timer(URLState *up, int delay, void *opaque, void (*cb)(void *opaque))
{
    URLTimer *ti;

    ti = qe_mallocz(URLTimer);
    if (!ti)
        return NULL;
    ti->timeout = get_clock_ms() + delay;
    ti->opaque = opaque;
    ti->cb = cb;
    ti->next = up->first_timer;
    up->first_timer = ti;
    return ti;
}

void url_kill_timer(URLState *up, URLTimer **tip)
{
    if (*tip) {
        URLTimer **pt;

        /* remove timer from list of active timers and free it */
        for (pt = &up->first_timer; *pt != NULL; pt = &(*pt)->next) {
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

int url_set_tail_handler(URLState *up, void (*cb)(void *opaque), void *opaque)
{
    up->tail_cb = cb;
    up->tail_opaque = opaque;
    return 0;
}

/* execute stacked bottom halves */
static inline void url_call_bottom_halves(URLState *up)
{
    BottomHalfEntry *bh;

    while (!list_empty(&up->bottom_halves)) {
        bh = (BottomHalfEntry *)up->bottom_halves.prev;
        list_del(bh);
        bh->cb(bh->opaque);
        qe_free(&bh);
    }
}

/* call timer callbacks and compute maximum next call time to
   check_timers() */
static inline int url_check_timers(URLState *up, int max_delay)
{
    URLTimer *ti, **pt;
    int timeout, cur_time;

    cur_time = get_clock_ms();
    timeout = cur_time + max_delay;
    pt = &up->first_timer;
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
            url_call_bottom_halves(up);
        } else {
            if ((ti->timeout - timeout) < 0)
                timeout = ti->timeout;
            pt = &ti->next;
        }
    }
    return timeout - cur_time;
}

#define MAX_DELAY 500  /* milliseconds */

/* block until one event */
static void url_block(URLState *up)
{
    IOHandler *uh;
    int ret, i, delay;
    fd_set rfds, wfds;
    struct timeval tv;

    delay = url_check_timers(up, MAX_DELAY);
#if 0
    {
        static int count;

        printf("%5d: delay=%d\n", count++, delay);
    }
#endif
    tv.tv_sec = delay / 1000;
    tv.tv_usec = (delay % 1000) * 1000;

    rfds = up->rfds;
    wfds = up->wfds;
    ret = select(up->nfds, &rfds, &wfds, NULL, &tv);

    /* call each handler */
    /* extra checks on callback function pointers because a callback
     * may unregister another callback.  This was causing crash bugs
     * when deleting a running shell output buffer such as a buffer
     * with a huge compressed file while it decompresses.
     * XXX: should break from the loop if callback changed the
     *      structures but need further investigation.
     * Here is an example of a problem: if a callback closes a resource
     * and opens another one that happens to get the same unix handle hd,
     * the state of FD_ISSET(hd) will refer to the original resource and
     * may erroneously call the new handler on a blocking event.
     * Similarly, if a callback consumes a resource for a different handle
     * the FD_ISSET will also be incorrect.
     */
    if (ret > 0) {
        uh = up->handlers;
        for (i = 0; i < up->nfds; i++) {
            if (FD_ISSET(i, &rfds) && uh->read_cb) {
                uh->read_cb(uh->read_opaque);
                url_call_bottom_halves(up);
            }
            if (FD_ISSET(i, &wfds) && uh->write_cb) {
                uh->write_cb(uh->write_opaque);
                url_call_bottom_halves(up);
            }
            uh++;
        }
    }

#ifndef CONFIG_WIN32
    /* handle terminated children */
    for (;;) {
        int pid, status;
        PidHandler *ph, *ph1;

        if (list_empty(&up->pid_handlers))
            break;
        pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0)
            break;
        list_for_each_safe(ph, ph1, &up->pid_handlers) {
            if (ph->pid == pid && ph->cb) {
                ph->cb(ph->opaque, status);
                url_call_bottom_halves(up);
                break;
            }
        }
    }
#endif
}

int url_main_loop(URLState *up)
{
    while (!up->exit_request) {
        url_block(up);
        if (*up->tail_cb) {
            // call one shot tail function
            void (*cb)(void *opaque) = up->tail_cb;
            up->tail_cb = NULL;
            (*cb)(up->tail_opaque);
            url_call_bottom_halves(up);
        }
    }
    up->exit_request = 0;
    return 0;
}

/* exit from url loop */
void url_exit(URLState *up)
{
    up->exit_request = 1;
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
