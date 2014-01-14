/*
 * Buffer handling for QEmacs
 *
 * Copyright (c) 2000 Fabrice Bellard.
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

#include "qe.h"
#ifdef CONFIG_MMAP
#include <sys/mman.h>
#endif

static void eb_addlog(EditBuffer *b, enum LogOperation op,
                      int offset, int size);

/************************************************************/
/* basic access to the edit buffer */

/* find a page at a given offset */
static Page *find_page(EditBuffer *b, int *offset_ptr)
{
    Page *p;
    int offset;

    offset = *offset_ptr;
    if (b->cur_page && offset >= b->cur_offset &&
        offset < b->cur_offset + b->cur_page->size) {
        /* use the cache */
        *offset_ptr -= b->cur_offset;
        return b->cur_page;
    } else {
        p = b->page_table;
        while (offset >= p->size) {
            offset -= p->size;
            p++;
        }
        b->cur_page = p;
        b->cur_offset = *offset_ptr - offset;
        *offset_ptr = offset;
        return p;
    }
}

/* prepare a page to be written */
static void update_page(Page *p)
{
    u8 *buf;

    /* if the page is read only, copy it */
    if (p->flags & PG_READ_ONLY) {
        buf = qe_malloc_dup(p->data, p->size);
        /* XXX: should return an error */
        if (!buf)
            return;
        p->data = buf;
        p->flags &= ~PG_READ_ONLY;
    }
    p->flags &= ~(PG_VALID_POS | PG_VALID_CHAR | PG_VALID_COLORS);
}

/* Read or write in the buffer. We must have 0 <= offset < b->total_size */
static int eb_rw(EditBuffer *b, int offset, u8 *buf, int size1, int do_write)
{
    Page *p;
    int len, size;

    if (offset < 0)
        return 0;

    if ((offset + size1) > b->total_size)
        size1 = b->total_size - offset;

    if (size1 <= 0)
        return 0;

    size = size1;
    if (do_write)
        eb_addlog(b, LOGOP_WRITE, offset, size);

    p = find_page(b, &offset);
    while (size > 0) {
        len = p->size - offset;
        if (len > size)
            len = size;
        if (do_write) {
            update_page(p);
            memcpy(p->data + offset, buf, len);
        } else {
            memcpy(buf, p->data + offset, len);
        }
        buf += len;
        size -= len;
        offset += len;
        if (offset >= p->size) {
            p++;
            offset = 0;
        }
    }
    return size1;
}

/* We must have: 0 <= offset < b->total_size */
/* Safety: request will be clipped */
int eb_read(EditBuffer *b, int offset, void *buf, int size)
{
    return eb_rw(b, offset, buf, size, 0);
}

/* Note: eb_write can be used to insert after the end of the buffer */
void eb_write(EditBuffer *b, int offset, const void *buf_arg, int size)
{
    int len, left;
    const u8 *buf = buf_arg;

    if (b->flags & BF_READONLY)
        return;

    len = eb_rw(b, offset, (void *)buf, size, 1);
    left = size - len;
    if (left > 0) {
        offset += len;
        buf += len;
        eb_insert(b, offset, buf, left);
    }
}

/* internal function for insertion : 'buf' of size 'size' at the
   beginning of the page at page_index */
static void eb_insert1(EditBuffer *b, int page_index, const u8 *buf, int size)
{
    int len, n;
    Page *p;

    if (page_index < b->nb_pages) {
        p = &b->page_table[page_index];
        len = MAX_PAGE_SIZE - p->size;
        if (len > size)
            len = size;
        if (len > 0) {
            update_page(p);
            /* CG: probably faster with qe_malloc + qe_free */
            qe_realloc(&p->data, p->size + len);
            memmove(p->data + len, p->data, p->size);
            memcpy(p->data, buf + size - len, len);
            size -= len;
            p->size += len;
        }
    }

    /* now add new pages if necessary */
    n = (size + MAX_PAGE_SIZE - 1) / MAX_PAGE_SIZE;
    if (n > 0) {
        b->nb_pages += n;
        qe_realloc(&b->page_table, b->nb_pages * sizeof(Page));
        p = &b->page_table[page_index];
        memmove(p + n, p, sizeof(Page) * (b->nb_pages - n - page_index));
        while (size > 0) {
            len = size;
            if (len > MAX_PAGE_SIZE)
                len = MAX_PAGE_SIZE;
            p->size = len;
            p->data = qe_malloc_dup(buf, len);
            p->flags = 0;
            buf += len;
            size -= len;
            p++;
        }
    }
}

/* We must have : 0 <= offset <= b->total_size */
static void eb_insert_lowlevel(EditBuffer *b, int offset,
                               const u8 *buf, int size)
{
    int len, len_out, page_index;
    Page *p;

    b->total_size += size;

    /* find the correct page */
    p = b->page_table;
    if (offset > 0) {
        offset--;
        p = find_page(b, &offset);
        offset++;

        /* compute what we can insert in current page */
        len = MAX_PAGE_SIZE - offset;
        if (len > size)
            len = size;
        /* number of bytes to put in next pages */
        len_out = p->size + len - MAX_PAGE_SIZE;
        page_index = p - b->page_table;
        if (len_out > 0)
            eb_insert1(b, page_index + 1,
                       p->data + p->size - len_out, len_out);
        else
            len_out = 0;
        /* now we can insert in current page */
        if (len > 0) {
            p = b->page_table + page_index;
            update_page(p);
            p->size += len - len_out;
            qe_realloc(&p->data, p->size);
            memmove(p->data + offset + len,
                    p->data + offset, p->size - (offset + len));
            memcpy(p->data + offset, buf, len);
            buf += len;
            size -= len;
        }
    } else {
        page_index = -1;
    }
    /* insert the remaining data in the next pages */
    if (size > 0)
        eb_insert1(b, page_index + 1, buf, size);

    /* the page cache is no longer valid */
    b->cur_page = NULL;
}

/* Insert 'size' bytes of 'src' buffer from position 'src_offset' into
 * buffer 'dest' at offset 'dest_offset'. 'src' MUST BE DIFFERENT from
 * 'dest'. Raw insertion performed, encoding is ignored.
 */
int eb_insert_buffer(EditBuffer *dest, int dest_offset,
                     EditBuffer *src, int src_offset,
                     int size)
{
    Page *p, *p_start, *q;
    int len, n, page_index, size0;

    if (dest->flags & BF_READONLY)
        return 0;

    /* Assert parameter consistency */
    if (dest_offset < 0 || src_offset < 0 || src_offset >= src->total_size)
        return 0;

    if (src_offset + size > src->total_size)
        size = src->total_size - src_offset;

    if (dest_offset > dest->total_size)
        dest_offset = dest->total_size;

    if (size <= 0)
        return 0;

    size0 = size;

    eb_addlog(dest, LOGOP_INSERT, dest_offset, size);

    /* insert the data from the first page if it is not completely
       selected */
    p = find_page(src, &src_offset);
    if (src_offset > 0) {
        len = p->size - src_offset;
        if (len > size)
            len = size;
        eb_insert_lowlevel(dest, dest_offset, p->data + src_offset, len);
        dest_offset += len;
        size -= len;
        p++;
    }

    if (size == 0)
        return size0;

    /* cut the page at dest offset if needed */
    if (dest_offset < dest->total_size) {
        q = find_page(dest, &dest_offset);
        page_index = q - dest->page_table;
        if (dest_offset > 0) {
            page_index++;
            eb_insert1(dest, page_index, q->data + dest_offset,
                       q->size - dest_offset);
            /* must reload q because page_table may have been
               realloced */
            q = dest->page_table + page_index - 1;
            update_page(q);
            qe_realloc(&q->data, dest_offset);
            q->size = dest_offset;
        }
    } else {
        page_index = dest->nb_pages;
    }

    /* update total_size */
    dest->total_size += size;

    /* compute the number of complete pages to insert */
    p_start = p;
    while (size > 0 && p->size <= size) {
        size -= p->size;
        p++;
    }
    n = p - p_start; /* number of pages to insert */
    p = p_start;
    if (n > 0) {
        /* add the pages */
        dest->nb_pages += n;
        qe_realloc(&dest->page_table, dest->nb_pages * sizeof(Page));
        q = dest->page_table + page_index;
        memmove(q + n, q, sizeof(Page) * (dest->nb_pages - n - page_index));
        p = p_start;
        while (n > 0) {
            len = p->size;
            q->size = len;
            if (p->flags & PG_READ_ONLY) {
                /* simply copy the reference */
                q->flags = PG_READ_ONLY;
                q->data = p->data;
            } else {
                /* allocate a new page */
                q->flags = 0;
                q->data = qe_malloc_dup(p->data, len);
            }
            n--;
            p++;
            q++;
        }
        page_index = q - dest->page_table;
    }

    /* insert the remaning bytes */
    if (size > 0) {
        eb_insert1(dest, page_index, p->data, size);
    }

    /* the page cache is no longer valid */
    dest->cur_page = NULL;
    return size0;
}

/* Insert 'size' bytes from 'buf' into 'b' at offset 'offset'. We must
   have : 0 <= offset <= b->total_size */
void eb_insert(EditBuffer *b, int offset, const void *buf, int size)
{
    if (b->flags & BF_READONLY)
        return;

    /* sanity checks */
    if (offset > b->total_size)
        offset = b->total_size;

    if (offset < 0 || size <= 0)
        return;

    eb_addlog(b, LOGOP_INSERT, offset, size);

    eb_insert_lowlevel(b, offset, buf, size);

    /* the page cache is no longer valid */
    b->cur_page = NULL;
}

/* We must have : 0 <= offset <= b->total_size */
void eb_delete(EditBuffer *b, int offset, int size)
{
    int n, len;
    Page *del_start, *p;

    if (b->flags & BF_READONLY)
        return;

    if (offset + size > b->total_size)
        size = b->total_size - offset;

    if (offset < 0 || size <= 0)
        return;

    /* dispatch callbacks before buffer update */
    eb_addlog(b, LOGOP_DELETE, offset, size);

    b->total_size -= size;

    /* find the correct page */
    p = find_page(b, &offset);
    n = 0;
    del_start = NULL;
    while (size > 0) {
        len = p->size - offset;
        if (len > size)
            len = size;
        if (len == p->size) {
            if (!del_start)
                del_start = p;
            /* we cannot free if read only */
            if (!(p->flags & PG_READ_ONLY))
                qe_free(&p->data);
            p++;
            offset = 0;
            n++;
        } else {
            update_page(p);
            memmove(p->data + offset, p->data + offset + len,
                    p->size - offset - len);
            p->size -= len;
            qe_realloc(&p->data, p->size);
            offset += len;
            if (offset >= p->size) {
                p++;
                offset = 0;
            }
        }
        size -= len;
    }

    /* now delete the requested pages */
    if (n > 0) {
        b->nb_pages -= n;
        memmove(del_start, del_start + n,
                (b->page_table + b->nb_pages - del_start) * sizeof(Page));
        qe_realloc(&b->page_table, b->nb_pages * sizeof(Page));
    }

    /* the page cache is no longer valid */
    b->cur_page = NULL;
}

/* flush the log */
void log_reset(EditBuffer *b)
{
    if (b->log_buffer) {
        eb_free(b->log_buffer);
        b->log_buffer = NULL;
        b->log_new_index = 0;
        b->log_current = 0;
        b->nb_logs = 0;
    }
    b->modified = 0;
}

/* rename a buffer and add characters so that the name is unique */
void eb_set_buffer_name(EditBuffer *b, const char *name1)
{
    char name[MAX_BUFFERNAME_SIZE];
    int n, pos;

    pstrcpy(name, sizeof(name) - 10, name1);
    /* set the buffer name to NULL since it will be changed */
    b->name[0] = '\0';
    pos = strlen(name);
    n = 1;
    while (eb_find(name) != NULL) {
        snprintf(name + pos, sizeof(name) - pos, "<%d>", n);
        n++;
    }
    pstrcpy(b->name, sizeof(b->name), name);
}

EditBuffer *eb_new(const char *name, int flags)
{
    QEmacsState *qs = &qe_state;
    EditBuffer *b;
    EditBuffer **pb;

    b = qe_mallocz(EditBuffer);
    if (!b)
        return NULL;

    // should ensure name uniqueness ?
    pstrcpy(b->name, sizeof(b->name), name);
    b->flags = flags;

    /* set default data type */
    b->data_type = &raw_data_type;

    /* initial value of save_log: 0 or 1 */
    b->save_log = ((flags & BF_SAVELOG) != 0);

    /* initialize default mode stuff */
    b->tab_size = 8;    /* CG: not finished */

    /* add buffer in global buffer list (at end for system buffers) */
    pb = &qs->first_buffer;
    if (*b->name == '*') {
        while (*pb)
            pb = &(*pb)->next;
    }
    b->next = *pb;
    *pb = b;

    if (flags & BF_UTF8) {
        eb_set_charset(b, &charset_utf8);
    } else {
        /* CG: default charset should be selectable */
        eb_set_charset(b, &charset_8859_1);
    }

    /* add mark move callback */
    eb_add_callback(b, eb_offset_callback, &b->mark, 0);
    eb_add_callback(b, eb_offset_callback, &b->offset, 1);

    if (strequal(name, "*trace*"))
        qs->trace_buffer = b;

    return b;
}

/* Return an empty scratch buffer, create one if necessary */
EditBuffer *eb_scratch(const char *name, int flags)
{
    EditBuffer *b;

    b = eb_find(name);
    if (b != NULL) {
        eb_clear(b);
    } else {
        b = eb_new(name, flags);
    }
    return b;
}

void eb_clear(EditBuffer *b)
{
    b->flags &= ~BF_READONLY;

    /* XXX: should just reset logging instead of disabling it */
    b->save_log = 0;
    b->last_log = 0;
    eb_delete(b, 0, b->total_size);
    log_reset(b);

    /* close and reset file handle */
    if (b->file_handle > 0) {
        close(b->file_handle);
    }
    b->file_handle = 0;

    /* TODO: clear buffer structure */
    //memset(b, 0, offsetof(EditBuffer, remanent_area));
}

void eb_free(EditBuffer *b)
{
    QEmacsState *qs = &qe_state;
    EditBuffer **pb;
    EditBufferCallbackList *l, *l1;

    if (b == NULL)
        return;

    /* call user defined close */
    if (b->close)
        b->close(b);

    /* free each callback */
    for (l = b->first_callback; l != NULL;) {
        l1 = l->next;
        qe_free(&l);
        l = l1;
    }
    b->first_callback = NULL;

    eb_clear(b);

    /* suppress from buffer list */
    pb = &qs->first_buffer;
    while (*pb != NULL) {
        if (*pb == b)
            break;
        pb = &(*pb)->next;
    }
    *pb = (*pb)->next;

    if (b == qs->trace_buffer)
        qs->trace_buffer = NULL;

    qe_free(&b);
}

EditBuffer *eb_find(const char *name)
{
    QEmacsState *qs = &qe_state;
    EditBuffer *b;

    b = qs->first_buffer;
    while (b != NULL) {
        if (strequal(b->name, name))
            return b;
        b = b->next;
    }
    return NULL;
}

EditBuffer *eb_find_new(const char *name, int flags)
{
    EditBuffer *b;

    b = eb_find(name);
    if (!b)
        b = eb_new(name, flags);
    return b;
}

EditBuffer *eb_find_file(const char *filename)
{
    QEmacsState *qs = &qe_state;
    EditBuffer *b;

    b = qs->first_buffer;
    while (b != NULL) {
        /* XXX: should also use stat to ensure this is same file */
        if (strequal(b->filename, filename))
            return b;
        b = b->next;
    }
    return NULL;
}

/* Find the next window showing a given buffer */
EditState *eb_find_window(EditBuffer *b, EditState *e)
{
    QEmacsState *qs = &qe_state;

    for (e = e ? e->next_window : qs->first_window;
         e != NULL;
         e = e->next_window)
    {
            if (e->b == b)
                return e;
    }
    return NULL;
}

void eb_trace_bytes(const void *buf, int size, int state)
{
    QEmacsState *qs = &qe_state;
    EditBuffer *b = qs->trace_buffer;
    EditState *e;
    int point;

    if (b) {
        point = b->total_size;
        if (qs->trace_buffer_state != state) {
            const char *str = NULL;
            switch (qs->trace_buffer_state) {
            case EB_TRACE_TTY:
                str = "|\n";
                break;
            case EB_TRACE_PTY:
                str = "|\n";
                break;
            case EB_TRACE_SHELL:
                str = "|\n";
                break;
            }
            if (str) {
                eb_write(b, b->total_size, str, strlen(str));
            }
            qs->trace_buffer_state = state;
            switch (qs->trace_buffer_state) {
            case EB_TRACE_TTY:
                str = "--|";
                break;
            case EB_TRACE_PTY:
                str = ">>|";
                break;
            case EB_TRACE_SHELL:
                str = "<<|";
                break;
            }
            if (str) {
                eb_write(b, b->total_size, str, strlen(str));
            }
        }
#if 0
        /* CG: could make traces more readable: */
        if (ch < 32 || ch == 127)
            fprintf(stderr, "got %d '^%c'\n", ch, ('@' + ch) & 127);
        else
            fprintf(stderr, "got %d '%c'\n", ch, ch);
#endif
        eb_write(b, b->total_size, buf, size);

        /* If point is visible in window, should keep it so */
        e = eb_find_window(b, NULL);
        if (e && e->offset == point)
            e->offset = b->total_size;
    }
}

/************************************************************/
/* callbacks */

int eb_add_callback(EditBuffer *b, EditBufferCallback cb, void *opaque, int arg)
{
    EditBufferCallbackList *l;

    l = qe_malloc(EditBufferCallbackList);
    if (!l)
        return -1;
    l->callback = cb;
    l->opaque = opaque;
    l->arg = arg;
    l->next = b->first_callback;
    b->first_callback = l;
    return 0;
}

void eb_free_callback(EditBuffer *b, EditBufferCallback cb, void *opaque)
{
    EditBufferCallbackList **pl, *l;

    for (pl = &b->first_callback; (*pl) != NULL; pl = &(*pl)->next) {
        l = *pl;
        if (l->callback == cb && l->opaque == opaque) {
            *pl = l->next;
            qe_free(&l);
            break;
       }
    }
}

/* standard callback to move offsets */
void eb_offset_callback(__unused__ EditBuffer *b, void *opaque, int edge,
                        enum LogOperation op, int offset, int size)
{
    int *offset_ptr = opaque;

    switch (op) {
    case LOGOP_INSERT:
        if (*offset_ptr > offset)
            *offset_ptr += size;
        /* special case for buffer's own point position and shell cursor:
         * edge position is pushed right */
        if (*offset_ptr == offset && edge)
            *offset_ptr += size;
        break;
    case LOGOP_DELETE:
        if (*offset_ptr > offset) {
            *offset_ptr -= size;
            if (*offset_ptr < offset)
                *offset_ptr = offset;
        }
        break;
    default:
        break;
    }
}



/************************************************************/
/* undo buffer */

static void eb_addlog(EditBuffer *b, enum LogOperation op,
                      int offset, int size)
{
    int was_modified, len, size_trailer;
    LogBuffer lb;
    EditBufferCallbackList *l;

    /* callbacks and logging disabled for composite undo phase */
    if (b->save_log & 2)
        return;

    /* call each callback */
    for (l = b->first_callback; l != NULL; l = l->next) {
        l->callback(b, l->opaque, l->arg, op, offset, size);
    }

    was_modified = b->modified;
    b->modified = 1;

    if (!b->save_log)
        return;

    if (!b->log_buffer) {
        char buf[MAX_BUFFERNAME_SIZE];
        /* Name should be unique because b->name is, but b->name may
         * later change if buffer is written to a different file.  This
         * should not be a problem since this log buffer is never
         * referenced by name.
         */
        snprintf(buf, sizeof(buf), "*log <%s>*", b->name);
        b->log_buffer = eb_new(buf, BF_SYSTEM);
        if (!b->log_buffer)
            return;
    }
    /* XXX: better test to limit size */
    if (b->nb_logs >= (NB_LOGS_MAX-1)) {
        /* no free space, delete least recent entry */
        /* XXX: should check undo record integrity */
        eb_read(b->log_buffer, 0, &lb, sizeof(lb));
        len = lb.size;
        if (lb.op == LOGOP_INSERT)
            len = 0;
        len += sizeof(LogBuffer) + sizeof(int);
        eb_delete(b->log_buffer, 0, len);
        b->log_new_index -= len;
        if (b->log_current > 1)
            b->log_current -= len;
        b->nb_logs--;
    }

    /* If inserting, try and coalesce log record with previous */
    if (op == LOGOP_INSERT && b->last_log == LOGOP_INSERT
    &&  b->log_new_index >= sizeof(lb) + sizeof(int)
    &&  eb_read(b->log_buffer, b->log_new_index - sizeof(int), &size_trailer,
                sizeof(int)) == sizeof(int)
    &&  size_trailer == 0
    &&  eb_read(b->log_buffer, b->log_new_index - sizeof(lb) - sizeof(int), &lb,
                sizeof(lb)) == sizeof(lb)
    &&  lb.op == LOGOP_INSERT
    &&  lb.offset + lb.size == offset) {
        lb.size += size;
        eb_write(b->log_buffer, b->log_new_index - sizeof(lb) - sizeof(int), &lb, sizeof(lb));
        return;
    }

    b->last_log = op;

    /* XXX: should check undo record integrity */

    /* header */
    lb.pad1 = '\n';   /* make log buffer display readable */
    lb.pad2 = ':';
    lb.op = op;
    lb.offset = offset;
    lb.size = size;
    lb.was_modified = was_modified;
    eb_write(b->log_buffer, b->log_new_index, &lb, sizeof(lb));
    b->log_new_index += sizeof(lb);

    /* data */
    switch (op) {
    case LOGOP_DELETE:
    case LOGOP_WRITE:
        eb_insert_buffer(b->log_buffer, b->log_new_index, b, offset, size);
        b->log_new_index += size;
        size_trailer = size;
        break;
    default:
        size_trailer = 0;
        break;
    }
    /* trailer */
    eb_write(b->log_buffer, b->log_new_index, &size_trailer, sizeof(int));
    b->log_new_index += sizeof(int);

    b->nb_logs++;
}

void do_undo(EditState *s)
{
    EditBuffer *b = s->b;
    int log_index, size_trailer;
    LogBuffer lb;

    if (!b->log_buffer)
        return;

    /* Should actually keep undo state current until new logs are added */
    if (s->qe_state->last_cmd_func != (CmdFunc)do_undo
    &&  s->qe_state->last_cmd_func != (CmdFunc)do_redo) {
        b->log_current = 0;
    }

    if (b->log_current == 0) {
        log_index = b->log_new_index;
    } else {
        log_index = b->log_current - 1;
    }
    if (log_index == 0) {
        put_status(s, "No further undo information");
        return;
    } else {
        put_status(s, "Undo!");
    }
    /* go backward */
    log_index -= sizeof(int);
    eb_read(b->log_buffer, log_index, &size_trailer, sizeof(int));
    log_index -= size_trailer + sizeof(LogBuffer);

    /* log_current is 1 + index to have zero as default value */
    b->log_current = log_index + 1;

    /* play the log entry */
    eb_read(b->log_buffer, log_index, &lb, sizeof(LogBuffer));
    log_index += sizeof(LogBuffer);

    b->last_log = 0;  /* prevent log compression */

    switch (lb.op) {
    case LOGOP_WRITE:
        /* we must disable the log because we want to record a single
           write (we should have the single operation: eb_write_buffer) */
        b->save_log |= 2;
        eb_delete(b, lb.offset, lb.size);
        eb_insert_buffer(b, lb.offset, b->log_buffer, log_index, lb.size);
        b->save_log &= ~2;
        eb_addlog(b, LOGOP_WRITE, lb.offset, lb.size);
        s->offset = lb.offset + lb.size;
        break;
    case LOGOP_DELETE:
        /* we must also disable the log there because the log buffer
           would be modified BEFORE we insert it by the implicit
           eb_addlog */
        b->save_log |= 2;
        eb_insert_buffer(b, lb.offset, b->log_buffer, log_index, lb.size);
        b->save_log &= ~2;
        eb_addlog(b, LOGOP_INSERT, lb.offset, lb.size);
        s->offset = lb.offset + lb.size;
        break;
    case LOGOP_INSERT:
        eb_delete(b, lb.offset, lb.size);
        s->offset = lb.offset;
        break;
    default:
        abort();
    }

    b->modified = lb.was_modified;
}

void do_redo(EditState *s)
{
    EditBuffer *b = s->b;
    int log_index, size_trailer;
    LogBuffer lb;

    if (!b->log_buffer)
        return;

    /* Should actually keep undo state current until new logs are added */
    if (s->qe_state->last_cmd_func != (CmdFunc)do_undo
    &&  s->qe_state->last_cmd_func != (CmdFunc)do_redo) {
        b->log_current = 0;
    }

    if (!b->log_current || !b->log_new_index) {
        put_status(s, "Nothing to redo");
        return;
    }
    put_status(s, "Redo!");

    /* go forward in undo stack */
    log_index = b->log_current - 1;
    eb_read(b->log_buffer, log_index, &lb, sizeof(LogBuffer));
    log_index += sizeof(LogBuffer);
    if (lb.op != LOGOP_INSERT)
        log_index += lb.size;
    log_index += sizeof(int);
    /* log_current is 1 + index to have zero as default value */
    b->log_current = log_index + 1;

    /* go backward from the end and remove undo record */
    log_index = b->log_new_index;
    log_index -= sizeof(int);
    eb_read(b->log_buffer, log_index, &size_trailer, sizeof(int));
    log_index -= size_trailer + sizeof(LogBuffer);

    /* play the log entry */
    eb_read(b->log_buffer, log_index, &lb, sizeof(LogBuffer));
    log_index += sizeof(LogBuffer);

    switch (lb.op) {
    case LOGOP_WRITE:
        /* we must disable the log because we want to record a single
           write (we should have the single operation: eb_write_buffer) */
        b->save_log |= 2;
        eb_delete(b, lb.offset, lb.size);
        eb_insert_buffer(b, lb.offset, b->log_buffer, log_index, lb.size);
        b->save_log &= ~3;
        eb_addlog(b, LOGOP_WRITE, lb.offset, lb.size);
        b->save_log |= 1;
        s->offset = lb.offset + lb.size;
        break;
    case LOGOP_DELETE:
        /* we must also disable the log there because the log buffer
           would be modified BEFORE we insert it by the implicit
           eb_addlog */
        b->save_log |= 2;
        eb_insert_buffer(b, lb.offset, b->log_buffer, log_index, lb.size);
        b->save_log &= ~3;
        eb_addlog(b, LOGOP_INSERT, lb.offset, lb.size);
        b->save_log |= 1;
        s->offset = lb.offset + lb.size;
        break;
    case LOGOP_INSERT:
        b->save_log &= ~1;
        eb_delete(b, lb.offset, lb.size);
        b->save_log |= 1;
        s->offset = lb.offset;
        break;
    default:
        abort();
    }

    b->modified = lb.was_modified;

    log_index -= sizeof(LogBuffer);
    eb_delete(b->log_buffer, log_index, b->log_new_index - log_index);
    b->log_new_index = log_index;

    if (b->log_current >= log_index + 1) {
        /* redone everything */
        b->log_current = 0;
    }
}

/************************************************************/
/* line related functions */

void eb_set_charset(EditBuffer *b, QECharset *charset)
{
    int n;

    if (b->charset) {
        charset_decode_close(&b->charset_state);
    }
    b->charset = charset;
    b->flags &= ~BF_UTF8;
    if (charset == &charset_utf8)
        b->flags |= BF_UTF8;
    charset_decode_init(&b->charset_state, charset);

    /* Reset page cache flags */
    for (n = 0; n < b->nb_pages; n++) {
        Page *p = &b->page_table[n];
        p->flags &= ~(PG_VALID_POS | PG_VALID_CHAR | PG_VALID_COLORS);
    }
}

/* XXX: change API to go faster */
int eb_nextc(EditBuffer *b, int offset, int *next_ptr)
{
    u8 buf[MAX_CHAR_BYTES];
    int ch;

    if (eb_read(b, offset, buf, 1) <= 0) {
        ch = '\n';
        if (offset < 0)
            offset = 0;
        if (offset >= b->total_size)
            offset = b->total_size;
    } else {
        /* we use the charset conversion table directly to go faster */
        ch = b->charset_state.table[buf[0]];
        offset++;
        if (ch == ESCAPE_CHAR) {
            eb_read(b, offset, buf + 1, MAX_CHAR_BYTES - 1);
            b->charset_state.p = buf;
            ch = b->charset_state.decode_func(&b->charset_state);
            offset += (b->charset_state.p - buf) - 1;
        }
    }
    *next_ptr = offset;
    return ch;
}

/* compute offset after moving 'n' chars from 'offset'.
 * 'n' can be negative
 */ 
int eb_skip_chars(EditBuffer *b, int offset, int n)
{
    while (n < 0) {
        eb_prevc(b, offset, &offset);
        n++;
    }
    while (n > 0) {
        eb_nextc(b, offset, &offset);
        n--;
    }
    return offset;
}

/* delete one character at offset 'offset', return number of bytes removed */
int eb_delete_uchar(EditBuffer *b, int offset)
{
    int offset1, size = 0;
    
    eb_nextc(b, offset, &offset1);
    if (offset < offset1) {
        size = offset1 - offset;
        eb_delete(b, offset, size);
    }
    return size;
}

/* return number of bytes deleted */
int eb_delete_chars(EditBuffer *b, int offset, int n)
{
    int offset1 = eb_skip_chars(b, offset, n);
    int size = offset1 - offset;

    if (size < 0) {
        offset += size;
        offset1 -= size;
        size = -size;
    }        

    eb_delete(b, offset, size);
    return size;
}

/* XXX: only stateless charsets are supported */
/* XXX: suppress that */
int eb_prevc(EditBuffer *b, int offset, int *prev_ptr)
{
    int ch, char_size;
    u8 buf[MAX_CHAR_BYTES], *q;

    if (offset <= 0) {
        offset = 0;
        ch = '\n';
    } else {
        /* XXX: it cannot be generic here. Should use the
           line/column system to be really generic */
        char_size = b->charset_state.char_size;
        offset -= char_size;
        q = buf + sizeof(buf) - char_size;
        eb_read(b, offset, q, char_size);
        if (b->charset == &charset_utf8) {
            while (*q >= 0x80 && *q < 0xc0) {
                if (offset == 0 || q == buf) {
                    /* error: take only previous byte */
                    offset += buf - 1 - q;
                    ch = buf[sizeof(buf) - 1];
                    goto the_end;
                }
                offset--;
                q--;
                eb_read(b, offset, q, 1);
            }
            ch = utf8_decode((const char **)(void *)&q);
        } else {
            /* CG: this only works for stateless charsets */
            b->charset_state.p = q;
            ch = b->charset_state.decode_func(&b->charset_state);
        }
    }
 the_end:
    *prev_ptr = offset;
    return ch;
}

int eb_goto_pos(EditBuffer *b, int line1, int col1)
{
    Page *p, *p_end;
    int line2, col2, line, col, offset, offset1, nl;

    line = 0;
    col = 0;
    offset = 0;

    p = b->page_table;
    p_end = b->page_table + b->nb_pages;
    while (p < p_end) {
        if (!(p->flags & PG_VALID_POS)) {
            p->flags |= PG_VALID_POS;
            b->charset_state.get_pos_func(&b->charset_state, p->data, p->size,
                                          &p->nb_lines, &p->col);
        }
        line2 = line + p->nb_lines;
        if (p->nb_lines)
            col2 = 0;
        col2 = col + p->col;
        if (line2 > line1 || (line2 == line1 && col2 >= col1)) {
            /* compute offset */
            if (line < line1) {
                /* seek to the correct line */
                offset += b->charset->goto_line_func(b->charset,
                    p->data, p->size, line1 - line);
                line = line1;
                col = 0;
            }
            nl = b->charset->eol_char;
            while (col < col1 && eb_nextc(b, offset, &offset1) != nl) {
                col++;
                offset = offset1;
            }
            return offset;
        }
        line = line2;
        col = col2;
        offset += p->size;
        p++;
    }
    return b->total_size;
}

int eb_get_pos(EditBuffer *b, int *line_ptr, int *col_ptr, int offset)
{
    Page *p, *p_end;
    int line, col, line1, col1;

    QASSERT(offset >= 0);

    line = 0;
    col = 0;
    p = b->page_table;
    p_end = p + b->nb_pages;
    for (;;) {
        if (p >= p_end)
            goto the_end;
        if (offset < p->size)
            break;
        if (!(p->flags & PG_VALID_POS)) {
            p->flags |= PG_VALID_POS;
            b->charset_state.get_pos_func(&b->charset_state, p->data, p->size,
                                          &p->nb_lines, &p->col);
        }
        line += p->nb_lines;
        if (p->nb_lines)
            col = 0;
        col += p->col;
        offset -= p->size;
        p++;
    }
    b->charset_state.get_pos_func(&b->charset_state, p->data, offset,
                                  &line1, &col1);
    line += line1;
    if (line1)
        col = 0;
    col += col1;

 the_end:
    *line_ptr = line;
    *col_ptr = col;
    return line;
}

/************************************************************/
/* char offset computation */

/* convert a char number into a byte offset according to buffer charset */
int eb_goto_char(EditBuffer *b, int pos)
{
    int offset;
    Page *p, *p_end;

    if (!b->charset->variable_size) {
        offset = min(pos * b->charset->char_size, b->total_size);
    } else {
        offset = 0;
        p = b->page_table;
        p_end = b->page_table + b->nb_pages;
        while (p < p_end) {
            if (!(p->flags & PG_VALID_CHAR)) {
                p->flags |= PG_VALID_CHAR;
                p->nb_chars = b->charset->get_chars_func(b->charset, p->data, p->size);
            }
            if (pos < p->nb_chars) {
                offset += b->charset->goto_char_func(b->charset, p->data, p->size, pos);
                break;
            } else {
                pos -= p->nb_chars;
                offset += p->size;
                p++;
            }
        }
    }
    return offset;
}

/* convert a byte offset into a char number according to buffer charset */
int eb_get_char_offset(EditBuffer *b, int offset)
{
    int pos;
    Page *p, *p_end;

    if (offset < 0)
        offset = 0;

    if (!b->charset->variable_size) {
        /* offset is round down to character boundary */
        pos = min(offset, b->total_size) / b->charset->char_size;
    } else {
        if (b->charset == &charset_utf8) {
            /* Round offset down to character boundary */
            u8 buf[1];
            while (offset > 0 && eb_read(b, offset, buf, 1) == 1 &&
                   (buf[0] & 0xC0) == 0x80) {
                /* backtrack over trailing bytes */
                offset--;
            }
        } else {
            /* CG: XXX: offset rounding to character boundary is undefined */
        }
        pos = 0;
        p = b->page_table;
        p_end = p + b->nb_pages;
        while (p < p_end) {
            if (!(p->flags & PG_VALID_CHAR)) {
                p->flags |= PG_VALID_CHAR;
                p->nb_chars = b->charset->get_chars_func(b->charset, p->data, p->size);
            }
            if (offset < p->size) {
                pos += b->charset->get_chars_func(b->charset, p->data, offset);
                break;
            } else {
                pos += p->nb_chars;
                offset -= p->size;
                p++;
            }
        }
    }
    return pos;
}

/* delete a range of bytes from the buffer, bounds in any order, return
 * lower bound.
 */
int eb_delete_range(EditBuffer *b, int p1, int p2)
{
    if (p1 > p2) {
        int tmp = p1;
        p1 = p2;
        p2 = tmp;
    }
    eb_delete(b, p1, p2 - p1);
    return p1;
}

/* replace 'size' bytes at offset 'offset' with 'size1' bytes from 'buf' */
void eb_replace(EditBuffer *b, int offset, int size,
                const void *buf, int size1)
{
    /* CG: behaviour is not exactly identical: mark, point and other
     * callback based offsets will be updated differently.  should
     * write portion that fits and insert or delete remainder?
     */
    if (size == size1) {
        eb_write(b, offset, buf, size1);
    } else {
        eb_delete(b, offset, size);
        eb_insert(b, offset, buf, size1);
    }
}

/************************************************************/
/* buffer I/O */

#define IOBUF_SIZE 32768

#if 0

typedef struct BufferIOState {
    URLContext *handle;
    void (*progress_cb)(void *opaque, int size);
    void (*completion_cb)(void *opaque, int err);
    void *opaque;
    int offset;
    int saved_flags;
    int saved_log;
    int nolog;
    unsigned char buffer[IOBUF_SIZE];
} BufferIOState;

static void load_connected_cb(void *opaque, int err);
static void load_read_cb(void *opaque, int size);
static void eb_io_stop(EditBuffer *b, int err);

/* load a buffer asynchronously and launch the callback. The buffer
   stays in 'loading' state while begin loaded. It is also marked
   readonly. */
int load_buffer(EditBuffer *b, const char *filename,
                int offset, int nolog,
                void (*progress_cb)(void *opaque, int size),
                void (*completion_cb)(void *opaque, int err), void *opaque)
{
    URLContext *h;
    BufferIOState *s;

    /* cannot load a buffer if already I/Os or readonly */
    if (b->flags & (BF_LOADING | BF_SAVING | BF_READONLY))
        return -1;
    s = qe_malloc(BufferIOState);
    if (!s)
        return -1;
    b->io_state = s;
    h = url_new();
    if (!h) {
        qe_free(&b->io_state);
        return -1;
    }
    s->handle = h;
    s->saved_flags = b->flags;
    s->nolog = nolog;
    if (s->nolog) {
        s->saved_log = b->save_log;
        b->save_log = 0;
    }
    b->flags |= BF_LOADING | BF_READONLY;
    s->handle = h;
    s->progress_cb = progress_cb;
    s->completion_cb = completion_cb;
    s->opaque = opaque;
    s->offset = offset;
    printf("connect_async: '%s'\n", filename);
    url_connect_async(s->handle, filename, URL_RDONLY,
                      load_connected_cb, b);
    return 0;
}

static void load_connected_cb(void *opaque, int err)
{
    EditBuffer *b = opaque;
    BufferIOState *s = b->io_state;
    printf("connect_cb: err=%d\n", err);
    if (err) {
        eb_io_stop(b, err);
        return;
    }
    url_read_async(s->handle, s->buffer, IOBUF_SIZE, load_read_cb, b);
}

static void load_read_cb(void *opaque, int size)
{
    EditBuffer *b = opaque;
    BufferIOState *s = b->io_state;

    printf("read_cb: size=%d\n", size);
    if (size < 0) {
        eb_io_stop(b, -EIO);
    } else if (size == 0) {
        /* end of file */
        eb_io_stop(b, 0);
    } else {
        eb_insert(b, s->offset, s->buffer, size);
        s->offset += size;
        /* launch next read request */
        url_read_async(s->handle, s->buffer, IOBUF_SIZE, load_read_cb, b);
    }
}

static void eb_io_stop(EditBuffer *b, int err)
{
    BufferIOState *s = b->io_state;

    b->flags = s->saved_flags;
    if (s->nolog) {
        b->modified = 0;
        b->save_log = s->saved_log;
    }
    url_close(s->handle);
    s->completion_cb(s->opaque, err);
    qe_free(&b->io_state);
}
#endif

/* CG: returns number of bytes read, or -1 upon read error */
int raw_buffer_load1(EditBuffer *b, FILE *f, int offset)
{
    unsigned char buf[IOBUF_SIZE];
    int len, size;

    //put_status(NULL, "loading %s", filename);
    size = 0;
    for (;;) {
        len = fread(buf, 1, IOBUF_SIZE, f);
        if (len <= 0) {
            if (ferror(f))
                return -1;
            break;
        }
        eb_insert(b, offset, buf, len);
        offset += len;
        size += len;
    }
    //put_status(NULL, "");
    return size;
}

#ifdef CONFIG_MMAP
int mmap_buffer(EditBuffer *b, const char *filename)
{
    int fd, len, file_size, n, size;
    u8 *file_ptr, *ptr;
    Page *p;

    fd = open(filename, O_RDONLY);
    if (fd < 0)
        return -1;
    file_size = lseek(fd, 0, SEEK_END);
    //put_status(NULL, "mapping %s", filename);
    file_ptr = mmap(NULL, file_size, PROT_READ, MAP_SHARED, fd, 0);
    if ((void*)file_ptr == MAP_FAILED) {
        close(fd);
        return -1;
    }
    n = (file_size + MAX_PAGE_SIZE - 1) / MAX_PAGE_SIZE;
    p = qe_malloc_array(Page, n);
    if (!p) {
        close(fd);
        return -1;
    }
    b->page_table = p;
    b->total_size = file_size;
    b->nb_pages = n;
    size = file_size;
    ptr = file_ptr;
    while (size > 0) {
        len = size;
        if (len > MAX_PAGE_SIZE)
            len = MAX_PAGE_SIZE;
        p->data = ptr;
        p->size = len;
        p->flags = PG_READ_ONLY;
        ptr += len;
        size -= len;
        p++;
    }
    b->file_handle = fd;
    //put_status(NULL, "");
    return 0;
}
#endif

static int raw_buffer_load(EditBuffer *b, FILE *f)
{
    QEmacsState *qs = &qe_state;
    struct stat st;

    /* TODO: Should produce error messages */

    if (stat(b->filename, &st))
        return -1;

#ifdef CONFIG_MMAP
    if (st.st_size >= qs->mmap_threshold) {
        if (mmap_buffer(b, b->filename))
            return 0;
    }
#endif
    if (st.st_size <= qs->max_load_size) {
        return raw_buffer_load1(b, f, 0);
    }
    return -1;
}

/* Write bytes between <start> and <end> to file filename,
 * return bytes written or -1 if error
 */
static int raw_buffer_save(EditBuffer *b, int start, int end,
                           const char *filename)
{
    int fd, len, size, written;
    unsigned char buf[IOBUF_SIZE];

    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return -1;

    //put_status(NULL, "writing %s", filename);
    if (end < start) {
        int tmp = start;
        start = end;
        end = tmp;
    }
    if (start < 0)
        start = 0;
    if (end > b->total_size)
        end = b->total_size;
    written = 0;
    size = end - start;
    while (size > 0) {
        len = size;
        if (len > IOBUF_SIZE)
            len = IOBUF_SIZE;
        eb_read(b, start, buf, len);
        len = write(fd, buf, len);
        if (len < 0) {
            close(fd);
            return -1;
        }
        written += len;
        start += len;
        size -= len;
    }
    close(fd);
    //put_status(NULL, "");
    return written;
}

static void raw_buffer_close(__unused__ EditBuffer *b)
{
    /* nothing to do */
}

/* Associate a buffer with a file and rename it to match the
   filename. Find a unique buffer name */
void eb_set_filename(EditBuffer *b, const char *filename)
{
    pstrcpy(b->filename, sizeof(b->filename), filename);
    eb_set_buffer_name(b, get_basename(filename));
}

/* Insert unicode character according to buffer encoding */
int eb_insert_uchar(EditBuffer *b, int offset, int c)
{
    char buf[MAX_CHAR_BYTES];
    int len;

    len = unicode_to_charset(buf, c, b->charset);
    eb_insert(b, offset, buf, len);
    return len;
}

/* Insert buffer with utf8 chars according to buffer encoding */
int eb_insert_utf8_buf(EditBuffer *b, int offset, const char *buf, int len)
{
    if (b->charset == &charset_utf8) {
        eb_insert(b, offset, buf, len);
        return len;
    } else {
        char buf1[1024];
        int size, size1;
        const char *bufend = buf + len;

        size = size1 = 0;
        while (buf < bufend) {
            int c = utf8_decode(&buf);
            int clen = unicode_to_charset(buf1 + size1, c, b->charset);
            size1 += clen;
            if (size1 > ssizeof(buf) - MAX_CHAR_BYTES || buf >= bufend) {
                eb_insert(b, offset + size, buf1, size1);
                size += size1;
                size1 = 0;
            }
        }
        return size;
    }
}

int eb_insert_str(EditBuffer *b, int offset, const char *str)
{
    return eb_insert_utf8_buf(b, offset, str, strlen(str));
}

int eb_match_uchar(EditBuffer *b, int offset, int c, int *offsetp)
{
    if (eb_nextc(b, offset, &offset) != c)
        return 0;
    if (offsetp)
        *offsetp = offset;
    return 1;
}

int eb_match_str(EditBuffer *b, int offset, const char *str, int *offsetp)
{
    const char *p = str;

    while (*p) {
        int c = utf8_decode((const char **)(void *)&p);
        if (eb_nextc(b, offset, &offset) != c)
            return 0;
    }
    if (offsetp)
        *offsetp = offset;
    return 1;
}

int eb_match_istr(EditBuffer *b, int offset, const char *str, int *offsetp)
{
    const char *p = str;

    while (*p) {
        int c = utf8_decode((const char **)(void *)&p);
        if (qe_toupper(eb_nextc(b, offset, &offset)) != qe_toupper(c))
            return 0;
    }
    if (offsetp)
        *offsetp = offset;
    return 1;
}

int eb_printf(EditBuffer *b, const char *fmt, ...)
{
    char buf0[1024];
    char *buf;
    int len, size;
    va_list ap;

    va_start(ap, fmt);
    size = sizeof(buf0);
    buf = buf0;
    len = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    if (len >= size) {
        va_start(ap, fmt);
        size = len + 1;
#ifdef CONFIG_WIN32
        buf = malloc(size);
#else
        buf = alloca(size);
#endif
        vsnprintf(buf, size, fmt, ap);
        va_end(ap);
    }
    /* CG: insert buffer translating according b->charset.
     * buf may contain \0 characters via the %c modifer.
     * XXX: %c does not encode non ASCII characters as utf8.
     */
    eb_insert_utf8_buf(b, b->total_size, buf, len);
#ifdef CONFIG_WIN32
    if (buf != buf0)
        free(buf);
#endif
    return len;
}

#if 0
/* pad current line with spaces so that it reaches column n */
void eb_line_pad(EditBuffer *b, int n)
{
    int offset, i;

    i = 0;
    offset = b->total_size;
    for (;;) {
        if (eb_prevc(b, offset, &offset) == '\n')
            break;
        i++;
    }
    while (i < n) {
        eb_insert_uchar(b, b->total_size, ' ');
        i++;
    }
}
#endif

int eb_get_contents(EditBuffer *b, char *buf, int buf_size)
{
    int len;

    len = b->total_size;
    if (len > buf_size - 1)
        len = buf_size - 1;
    eb_read(b, 0, buf, len);
    buf[len] = '\0';
    return len;
}

/* Insert 'size' bytes of 'src' buffer from position 'src_offset' into
 * buffer 'dest' at offset 'dest_offset'. 'src' MUST BE DIFFERENT from
 * 'dest'. Charset converson between source and destination buffer is
 * performed.
 */
int eb_insert_buffer_convert(EditBuffer *dest, int dest_offset,
                             EditBuffer *src, int src_offset,
                             int size)
{
    if (dest->charset == src->charset) {
        return eb_insert_buffer(dest, dest_offset, src, src_offset, size);
    } else {
        EditBuffer *b;
        int offset, offset_max;

        b = dest;
        if ((b->flags & BF_SAVELOG) || dest_offset != b->total_size) {
            b = eb_new("*tmp*", 0);
            eb_set_charset(b, dest->charset);
        }

        /* well, not very fast, but simple */
        /* XXX: should optimize save_log system for insert sequences */
        offset_max = min(src->total_size, src_offset + size);
        size = 0;
        for (offset = src_offset; offset < offset_max;) {
            char buf[MAX_CHAR_BYTES];
            int c = eb_nextc(src, offset, &offset);
            int len = unicode_to_charset(buf, c, b->charset);
            eb_write(b, b->total_size, buf, len);
            size += len;
        }

        if (b != dest) {
            size = eb_insert_buffer(dest, dest_offset, b, 0, b->total_size);
            eb_free(b);
        }
        return size;
    }
}

/* get the line starting at offset 'offset' as an array of code points */
/* offset is bumped to the beginning of the next line */
/* returns the number of code points stored in buf, excluding '\0' */
/* buf_size must be > 0 */
/* XXX: cannot detect truncation */
int eb_get_line(EditBuffer *b, unsigned int *buf, int buf_size,
                int *offset_ptr)
{
    unsigned int *buf_ptr, *buf_end;
    int c, offset;

    offset = *offset_ptr;

    buf_ptr = buf;
    buf_end = buf + buf_size - 1;
    for (;;) {
        c = eb_nextc(b, offset, &offset);
        if (c == '\n')
            break;
        if (buf_ptr < buf_end)
            *buf_ptr++ = c;
    }
    *buf_ptr = '\0';
    *offset_ptr = offset;
    return buf_ptr - buf;
}

/* get the line starting at offset 'offset' encoded in utf-8 */
/* offset is bumped to the beginning of the next line */
/* returns the number of bytes stored in buf, excluding '\0' */
/* buf_size must be > 0 */
/* XXX: cannot detect truncation */
int eb_get_strline(EditBuffer *b, char *buf, int buf_size,
                   int *offset_ptr)
{
    char utf8_buf[6];
    char *buf_ptr, *buf_end;
    int c, offset, len;

    offset = *offset_ptr;

    buf_ptr = buf;
    buf_end = buf + buf_size - 1;
    for (;;) {
        c = eb_nextc(b, offset, &offset);
        if (c == '\n')
            break;
        if (c < 0x80) {
            if (buf_ptr < buf_end) {
                *buf_ptr++ = c;
                continue;
            }
        } else {
            len = utf8_encode(utf8_buf, c);
            if (buf_ptr + len <= buf_end) {
                memcpy(buf_ptr, utf8_buf, len);
                buf_ptr += len;
                continue;
            }
        }
        /* overflow: skip past '\n' */
        offset = eb_next_line(b, offset);
        break;
    }
    *buf_ptr = '\0';
    *offset_ptr = offset;
    return buf_ptr - buf;
}

int eb_prev_line(EditBuffer *b, int offset)
{
    int offset1, seen_nl;

    for (seen_nl = 0;;) {
        if (eb_prevc(b, offset, &offset1) == '\n') {
            if (seen_nl++)
                break;
        }
        offset = offset1;
    }
    return offset;
}

/* return offset of the beginning of the line containing offset */
int eb_goto_bol(EditBuffer *b, int offset)
{
    int offset1;

    for (;;) {
        if (eb_prevc(b, offset, &offset1) == '\n')
            break;
        offset = offset1;
    }
    return offset;
}

/* move to the beginning of the line containing offset */
/* return offset of the beginning of the line containing offset */
/* store count of characters skipped at *countp */
int eb_goto_bol2(EditBuffer *b, int offset, int *countp)
{
    int count, offset1;

    for (count = 0;; count++) {
        if (eb_prevc(b, offset, &offset1) == '\n')
            break;
        offset = offset1;
    }
    *countp = count;
    return offset;
}

int eb_is_empty_line(EditBuffer *b, int offset)
{
    int c;

    for (;;) {
        c = eb_nextc(b, offset, &offset);
        if (c == '\n')
            return 1;
        if (!qe_isspace(c))
            break;
    }
    return 0;
}

/* return offset of the end of the line containing offset */
int eb_goto_eol(EditBuffer *b, int offset)
{
    int c, offset1;

    for (;;) {
        c = eb_nextc(b, offset, &offset1);
        if (c == '\n')
            break;
        offset = offset1;
    }
    return offset;
}

int eb_next_line(EditBuffer *b, int offset)
{
    int c;

    for (;;) {
        c = eb_nextc(b, offset, &offset);
        if (c == '\n')
            break;
    }
    return offset;
}

/* buffer data type handling */

void eb_register_data_type(EditBufferDataType *bdt)
{
    QEmacsState *qs = &qe_state;
    EditBufferDataType **lp;

    lp = &qs->first_buffer_data_type;
    while (*lp != NULL)
        lp = &(*lp)->next;
    bdt->next = NULL;
    *lp = bdt;
}

/* Write buffer contents between <start> and <end> to file <filename>,
 * return bytes written or -1 if error
 */
int eb_write_buffer(EditBuffer *b, int start, int end, const char *filename)
{
    if (!b->data_type->buffer_save)
        return -1;

    return b->data_type->buffer_save(b, start, end, filename);
}

/* Save buffer contents to buffer associated file, handle backups,
 * return bytes written or -1 if error
 */
int eb_save_buffer(EditBuffer *b)
{
    int ret, st_mode;
    char buf1[MAX_FILENAME_SIZE];
    const char *filename;
    struct stat st;

    if (!b->data_type->buffer_save)
        return -1;

    filename = b->filename;
    /* get old file permission */
    st_mode = 0644;
    if (stat(filename, &st) == 0)
        st_mode = st.st_mode & 0777;

    /* backup old file if present */
    if (strlen(filename) < MAX_FILENAME_SIZE - 1) {
        if (snprintf(buf1, sizeof(buf1), "%s~", filename) < ssizeof(buf1)) {
            // should check error code
            rename(filename, buf1);
        }
    }

    /* CG: should pass st_mode to buffer_save */
    ret = b->data_type->buffer_save(b, 0, b->total_size, filename);
    if (ret < 0)
        return ret;

#ifndef CONFIG_WIN32
    /* set correct file st_mode to old file permissions */
    chmod(filename, st_mode);
#endif
    /* reset log */
    /* CG: should not do this! */
    //log_reset(b);
    b->modified = 0;
    return ret;
}

/* invalidate buffer raw data */
void eb_invalidate_raw_data(EditBuffer *b)
{
    b->save_log = 0;
    eb_delete(b, 0, b->total_size);
    log_reset(b);
}

EditBufferDataType raw_data_type = {
    "raw",
    raw_buffer_load,
    raw_buffer_save,
    raw_buffer_close,
    NULL, /* next */
};

/* init buffer handling */
void eb_init(void)
{
    eb_register_data_type(&raw_data_type);
}
