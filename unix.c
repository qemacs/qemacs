/*
 * Unix main loop for QEmacs
 * Copyright (c) 2002, 2003 Fabrice Bellard.
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
#include "qe.h"
#include <sys/wait.h>

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
        FD_SET(fd, &url_rfds);
    } else {
        FD_CLR(fd, &url_rfds);
    }
}

void set_write_handler(int fd, void (*cb)(void *opaque), void *opaque)
{
    url_handlers[fd].write_cb = cb;
    url_handlers[fd].write_opaque = opaque;
    if (cb) {
        if (fd >= url_fdmax)
            url_fdmax = fd;
        FD_SET(fd, &url_wfds);
    } else {
        FD_CLR(fd, &url_wfds);
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
                free(p);
                break;
            }
        }
    } else {
        p = malloc(sizeof(PidHandler));
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
    bh = malloc(sizeof(BottomHalfEntry));
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
            free(bh);
        }
    }
}

QETimer *qe_add_timer(int delay, void *opaque, void (*cb)(void *opaque))
{
    QETimer *ti;

    ti = malloc(sizeof(QETimer));
    if (!ti)
        return NULL;
    ti->timeout = get_clock_ms() + delay;
    ti->opaque = opaque;
    ti->cb = cb;
    ti->next = first_timer;
    first_timer = ti;
    return ti;
}

void qe_kill_timer(QETimer *ti)
{
    QETimer **pt;
    pt = &first_timer;
    while (*pt != NULL) {
        if (*pt == ti) {
            *pt = ti->next;
            free(ti);
        } else {
            pt = &(*pt)->next;
        }
    }
}

/* execute stacked bottom halves */
static void __call_bottom_halves(void)
{
    BottomHalfEntry *bh;

    while (!list_empty(&bottom_halves)) {
        bh = (BottomHalfEntry *)bottom_halves.prev;
        list_del(bh);
        bh->cb(bh->opaque);
        free(bh);
    }
}

static inline void call_bottom_halves(void)
{
    if (!list_empty(&bottom_halves))
        __call_bottom_halves();
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
    for(;;) {
        ti = *pt;
        if (ti == NULL)
            break;
        if ((ti->timeout - cur_time) <= 0) {
            /* timer expired : suppress it from timer list and call callback */
            *pt = ti->next;
            /* warning: a new timer can be added in the callback */
            ti->cb(ti->opaque);
            free(ti);
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

#define MAX_DELAY 500

/* block until one event */
static void url_block(void)
{
    URLHandler *uh;
    int ret, i, pid, status, delay;
    fd_set rfds, wfds;
    PidHandler *ph, *ph1;
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
    if (ret > 0) {
        uh = url_handlers;
        for(i = 0;i <= url_fdmax; i++) {
            if (FD_ISSET(i, &rfds)) {
                uh->read_cb(uh->read_opaque);
                call_bottom_halves();
            }
            if (FD_ISSET(i, &wfds)) {
                uh->write_cb(uh->write_opaque);
                call_bottom_halves();
            }
            uh++;
        }
    }
    
    /* handle terminated childs */
    for(;;) {
        if (list_empty(&pid_handlers))
            break;
        pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0)
            break;
        list_for_each_safe(ph, ph1, &pid_handlers) {
            if (ph->pid == pid) {
                ph->cb(ph->opaque, status);
                call_bottom_halves();
                break;
            }
        }
    }
}

void url_main_loop(void (*init)(void *opaque), void *opaque)
{
    url_block_reset();
    init(opaque);
    for(;;) {
        if (url_exit_request)
            break;
        url_block();
    }
}

/* exit from url loop */
void url_exit(void)
{
    url_exit_request = 1;
}

