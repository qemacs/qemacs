/*
 * Buffer handling for QEmacs
 *
 * Copyright (c) 2000-2002 Fabrice Bellard.
 * Copyright (c) 2002-2025 Charlie Gordon.
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
#ifdef CONFIG_MMAP
#include <sys/mman.h>
#endif

static void eb_addlog(EditBuffer *b, enum LogOperation op,
                      int offset, int size);

/************************************************************/
/* basic access to the edit buffer */

/* find a page at a given offset */
static inline Page *find_page(EditBuffer *b, int offset, int *page_offset_ptr)
{
    Page *p = b->page_table;
    int page_offset = offset;

    if (b->cur_page && offset >= b->cur_offset) {
        p = b->cur_page;
        page_offset -= b->cur_offset;
        if (page_offset < p->size) {
            *page_offset_ptr = page_offset;
            return p;
        }
    }
    while (page_offset >= p->size) {
        page_offset -= p->size;
        p++;
    }
    *page_offset_ptr = page_offset;
    b->cur_offset = offset - page_offset;
    b->cur_page = p;
    return p;
}

/* prepare a page to be written */
static void update_page(Page *p)
{
    u8 *buf;

    /* if the page is read only, copy it */
    if (p->flags & PG_READ_ONLY) {
        buf = qe_malloc_dup_bytes(p->data, p->size);
        /* XXX: should return an error */
        if (!buf)
            return;
        p->data = buf;
        p->flags &= ~PG_READ_ONLY;
    }
    p->flags &= ~(PG_VALID_POS | PG_VALID_CHAR | PG_VALID_COLORS);
}

/* Read one raw byte from the buffer:
 * We should have: 0 <= offset < b->total_size
 * Returns the byte or -1 upon failure.
 */
int eb_read_one_byte(EditBuffer *b, int offset)
{
    const Page *p;

    /* We clip the request for safety */
    if (offset < 0 || offset >= b->total_size)
        return -1;

    p = find_page(b, offset, &offset);
    return p->data[offset];
}

/* Read raw data from the buffer:
 * We should have: 0 <= offset < b->total_size, size >= 0
 */
int eb_read(EditBuffer *b, int offset, void *buf, int size)
{
    int len, remain;
    const Page *p;

    /* We carefully clip the request, avoiding integer overflow */
    if (offset < 0 || size <= 0 || offset >= b->total_size)
        return 0;

    len = b->total_size - offset;
    if (size > len)
        size = len;

    p = find_page(b, offset, &offset);
    for (remain = size;;) {
        len = p->size - offset;
        if (len > remain)
            len = remain;
        memcpy(buf, p->data + offset, len);
        if ((remain -= len) <= 0)
            break;
        buf = (u8*)buf + len;
        p++;
        offset = 0;
    }
    return size;
}

/* Write raw data into the buffer.
 * We should have 0 <= offset <= b->total_size, size >= 0.
 * Note: eb_write can be used to append data at the end of the buffer
 */
int eb_write(EditBuffer *b, int offset, const void *buf, int size)
{
    int len, remain, write_size, page_offset;
    Page *p;

    if (b->flags & BF_READONLY)
        return 0;

    /* We carefully clip the request, avoiding integer overflow */
    if (offset < 0 || size <= 0 || offset > b->total_size)
        return 0;

    write_size = size;
    len = b->total_size - offset;
    if (write_size > len)
        write_size = len;

    if (write_size > 0) {
        eb_addlog(b, LOGOP_WRITE, offset, write_size);

        p = find_page(b, offset, &page_offset);
        for (remain = write_size;;) {
            len = p->size - page_offset;
            if (len > remain)
                len = remain;
            update_page(p);
            memcpy(p->data + page_offset, buf, len);
            buf = (const u8*)buf + len;
            if ((remain -= len) <= 0)
                break;
            p++;
            page_offset = 0;
        }
    }
    if (size > write_size)
        eb_insert(b, offset + write_size, buf, size - write_size);
    return size;
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
            // XXX: test for failure
            qe_realloc_bytes(&p->data, p->size + len);
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
        // XXX: test for failure
        qe_realloc_array(&b->page_table, b->nb_pages);
        p = &b->page_table[page_index];
        blockmove(p + n, p, b->nb_pages - n - page_index);
        while (size > 0) {
            len = size;
            if (len > MAX_PAGE_SIZE)
                len = MAX_PAGE_SIZE;
            p->size = len;
            p->data = qe_malloc_dup_bytes(buf, len);
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
        p = find_page(b, offset, &offset);
        offset++;
    retry:
        /* compute what we can insert in current page */
        len = MAX_PAGE_SIZE - offset;
        if (len > size)
            len = size;
        /* number of bytes to put in next pages */
        len_out = p->size + len - MAX_PAGE_SIZE;
        page_index = p - b->page_table;
        if (len_out > 0) {
#if 1
            /* First try and shift some of these bytes to the previous pages */
            if (page_index > 0 && p[-1].size < MAX_PAGE_SIZE) {
                int chunk;
                update_page(p - 1);
                update_page(p);
                chunk = min_offset(MAX_PAGE_SIZE - p[-1].size, offset);
                // XXX: test for failure
                qe_realloc_bytes(&p[-1].data, p[-1].size + chunk);
                memcpy(p[-1].data + p[-1].size, p->data, chunk);
                p[-1].size += chunk;
                p->size -= chunk;
                if (p->size == 0) {
                    /* if page was completely fused with previous one */
                    b->nb_pages -= 1;
                    qe_free(&p->data);
                    blockmove(p, p + 1, b->nb_pages - page_index);
                    // XXX: test for failure
                    qe_realloc_array(&b->page_table, b->nb_pages);
                    p = b->page_table + page_index - 1;
                    offset = p->size;
                    goto retry;
                }
                memmove(p->data, p->data + chunk, p->size);
                // XXX: test for failure
                qe_realloc_bytes(&p->data, p->size);
                offset -= chunk;
                if (offset == 0 && p[-1].size < MAX_PAGE_SIZE) {
                    /* restart from previous page */
                    p--;
                    offset = p->size;
                }
                goto retry;
            }
#endif
            eb_insert1(b, page_index + 1,
                       p->data + p->size - len_out, len_out);
        } else {
            len_out = 0;
        }
        /* now we can insert in current page */
        if (len > 0) {
            /* reload p because page_table may have been reallocated */
            p = b->page_table + page_index;
            update_page(p);
            p->size += len - len_out;
            // XXX: test for failure
            qe_realloc_bytes(&p->data, p->size);
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
    Page *p;
    int len, size0;

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
#if 1
    /* Much simpler algorithm with fewer pathological cases */
    p = find_page(src, src_offset, &src_offset);
    while (size > 0) {
        len = p->size - src_offset;
        if (len > size)
            len = size;
        if ((p->flags & PG_READ_ONLY) && src_offset == 0 && len == MAX_PAGE_SIZE) {
            /* XXX: should share complete read-only pages.  This is
             * actually a little tricky: the mapping may be removed
             * upon buffer close. We need a ref count scheme to keep
             * track of these pages.
             * A brute force approach may prove best: upon unmapping a
             * file, scan all buffers for shared pages and delay
             * unmapping until these get freed.  We may keep a global
             * and a buffer based count of shared pages and a list of
             * mappings to accelerate this phase.
             */
        }
        eb_insert_lowlevel(dest, dest_offset, p->data + src_offset, len);
        dest_offset += len;
        src_offset = 0;
        p++;
        size -= len;
    }
    return size0;
#else
    Page *p_start, *q;
    int n, page_index;

    /* insert the data from the first page if it is not completely
       selected */
    p = find_page(src, src_offset, &src_offset);
    if (src_offset > 0 /* || size <= p->size */ ) {
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
        q = find_page(dest, dest_offset, &dest_offset);
        page_index = q - dest->page_table;
        if (dest_offset > 0) {
            page_index++;
            eb_insert1(dest, page_index, q->data + dest_offset,
                       q->size - dest_offset);
            /* must reload q because page_table may have been
               realloced */
            q = dest->page_table + page_index - 1;
            update_page(q);
            // XXX: test for failure
            qe_realloc_bytes(&q->data, dest_offset);
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
        // XXX: test for failure
        qe_realloc_array(&dest->page_table, dest->nb_pages);
        q = dest->page_table + page_index;
        blockmove(q + n, q, dest->nb_pages - n - page_index);
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
                q->data = qe_malloc_dup_bytes(p->data, len);
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
#endif
}

/* Insert 'size' bytes from 'buf' into 'b' at offset 'offset'. We must
   have : 0 <= offset <= b->total_size */
/* Return number of bytes inserted */
int eb_insert(EditBuffer *b, int offset, const void *buf, int size)
{
    if (b->flags & BF_READONLY)
        return 0;

    /* sanity checks */
    if (offset > b->total_size)
        offset = b->total_size;

    if (offset < 0 || size <= 0)
        return 0;

    eb_addlog(b, LOGOP_INSERT, offset, size);

    eb_insert_lowlevel(b, offset, buf, size);

    /* the page cache is no longer valid */
    b->cur_page = NULL;
    return size;
}

/* We must have : 0 <= offset <= b->total_size,
 * return actual number of bytes removed.
 */
int eb_delete(EditBuffer *b, int offset, int size)
{
    int n, len, size0;
    Page *del_start, *p;

    if (b->flags & BF_READONLY)
        return 0;

    if (offset < 0 || offset >= b->total_size || size <= 0)
        return 0;

    if (size > b->total_size - offset)
        size = b->total_size - offset;

    size0 = size;

    /* dispatch callbacks before buffer update */
    eb_addlog(b, LOGOP_DELETE, offset, size);

    b->total_size -= size;

    /* find the correct page */
    p = find_page(b, offset, &offset);
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
            // XXX: test for failure
            qe_realloc_bytes(&p->data, p->size);
            offset += len;
            /* XXX: should merge with adjacent pages if size becomes small? */
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
        blockmove(del_start, del_start + n,
                  b->page_table + b->nb_pages - del_start);
        // XXX: test for failure
        qe_realloc_array(&b->page_table, b->nb_pages);
    }

    /* the page cache is no longer valid */
    b->cur_page = NULL;

    return size0;
}

/*---------------- finding buffers ----------------*/

/* Verify that window still exists, return argument or NULL,
 * update handle if window is invalid.
 */
EditBuffer *qe_check_buffer(QEmacsState *qs, EditBuffer **sp)
{
    EditBuffer *b0 = *sp;
    EditBuffer *b;

    if (b0 == NULL)
        return NULL;

    for (b = qs->first_buffer; b != NULL; b = b->next) {
        if (b == b0)
            return b;
    }
    return *sp = NULL;
}

#ifdef CONFIG_TINY
EditBuffer *qe_find_buffer_name(QEmacsState *qs, const char *name)
{
    EditBuffer *b;

    b = qs->first_buffer;
    while (b != NULL) {
        if (strequal(b->name, name))
            return b;
        b = b->next;
    }
    return NULL;
}
#define eb_cache_remove(b)
#define eb_cache_insert(b)  0
#else
/* return index >= 0 if found, -1-insert_pos if not found */
static int eb_cache_locate(EditBuffer **cache, int len, const char *name)
{
    int aa, bb, m, cmp;

    for (aa = 0, bb = len; aa < bb;) {
        m = (aa + bb) >> 1;
        cmp = strcmp(name, cache[m]->name);
        if (cmp < 0) {
            bb = m;
        } else
        if (cmp > 0) {
            aa = m + 1;
        } else {
            return m;
        }
    }
    return -aa - 1;
}

static int eb_cache_remove(EditBuffer *b)
{
    QEmacsState *qs = b->qs;
    EditBuffer **cache = qs->buffer_cache;
    int len = qs->buffer_cache_len;
    int pos = eb_cache_locate(cache, len, b->name);

    if (pos < 0)
        return -1;

    if (cache[pos] != b)
        return -2;

    blockmove(cache + pos, cache + pos + 1, len - pos - 1);
    len -= 1;
    qs->buffer_cache_len = len;
    return 0;
}

static int eb_cache_insert(EditBuffer *b)
{
    QEmacsState *qs = b->qs;
    EditBuffer **cache = qs->buffer_cache;
    int len = qs->buffer_cache_len;
    int pos = eb_cache_locate(cache, len, b->name);

    if (pos >= 0)
        return (cache[pos] == b) ? -3 : -2;

    if (len >= qs->buffer_cache_size) {
        int size = max_int(32, len + (len >> 1) + (len >> 3));
        cache = qe_realloc_array(&qs->buffer_cache, size);
        if (!cache)
            return -1;
        qs->buffer_cache_size = size;
    }
    pos = -pos - 1;
    blockmove(cache + pos + 1, cache + pos, len - pos);
    cache[pos] = b;
    len += 1;
    qs->buffer_cache_len = len;
    return 0;
}

EditBuffer *qe_find_buffer_name(QEmacsState *qs, const char *name)
{
    EditBuffer **cache = qs->buffer_cache;
    int len = qs->buffer_cache_len;
    int pos = eb_cache_locate(cache, len, name);

    if (pos < 0)
        return NULL;

    return cache[pos];
}
#endif

/* flush the log */
void eb_free_log_buffer(EditBuffer *b)
{
    // FIXME: what if log buffer is shown in a window?
    eb_free(&b->log_buffer);
    b->log_new_index = 0;
    b->log_current = 0;
    b->nb_logs = 0;
}

/* rename a buffer: modify name to ensure uniqueness */
/* eb_set_buffer_name() may fail only for a newly created buffer */
int eb_set_buffer_name(EditBuffer *b, const char *name1)
{
    char name[MAX_BUFFERNAME_SIZE];
    int n, pos;
    const char *prefix = "<";
    const char *suffix = ">";
    EditBuffer *b1;

    pstrcpy(name, sizeof(name) - 10, name1);
    pos = strlen(name);
    if (pos > 0 && name[pos - 1] == '*') {
        pos--;
        prefix = "-";
        suffix = "*";
    }
    n = 0;
    /* do not allow an empty name */
    while ((b1 = qe_find_buffer_name(b->qs, name)) != NULL || *name == '\0') {
        if (b == b1)
            return 0;
        n++;
        snprintf(name + pos, sizeof(name) - pos, "%s%d%s", prefix, n, suffix);
    }
    /* This is the only place where b->name is modified */
    eb_cache_remove(b);
    pstrcpy(unconst(char *)b->name, sizeof(b->name), name);
    /* eb_cache_insert may fail only for a newly created buffer */
    return eb_cache_insert(b);
}

EditBuffer *qe_new_buffer(QEmacsState *qs, const char *name, int flags)
{
    EditBuffer *b;
    EditBuffer **pb;

    if (flags & BC_REUSE) {
        b = qe_find_buffer_name(qs, name);
        if (b != NULL) {
            if (flags & BC_CLEAR)
                eb_clear(b);
            return b;
        }
    }
    flags &= ~(BC_REUSE | BC_CLEAR);

    b = qe_mallocz(EditBuffer);
    if (!b) {
        qe_put_error(qs, "Out of memory for buffer '%s'", name);
        return NULL;
    }

    b->qs = qs;
    b->flags = flags & ~BF_STYLES;

    /* set default data type */
    b->data_type = &raw_data_type;

    /* initial value of save_log: 0 or 1 */
    b->save_log = ((flags & BF_SAVELOG) != 0);

    /* initialize default mode stuff */
    b->tab_width = qs->default_tab_width;
    b->fill_column = qs->default_fill_column;
    b->eol_type = qs->default_eol_type;

    /* set the buffer name to a unique name */
    // XXX: `b` is not in the buffer list nor in the buffer cache
    //      `eb_set_buffer_name` will insert it into the cache.
    //      This is somewhat sloppy.
    if (eb_set_buffer_name(b, name)) {
        qe_put_error(qs, "Cannot set buffer name '%s'", name);
        qe_free(&b);
        return NULL;
    }

    /* add buffer in global buffer list (at end for system buffers) */
    pb = &qs->first_buffer;
    if (*b->name == '*') {
        while (*pb)
            pb = &(*pb)->next;
    }
    b->next = *pb;
    *pb = b;

    if (flags & BF_UTF8) {
        eb_set_charset(b, &charset_utf8, b->eol_type);
    } else
    if (flags & BF_RAW) {
        eb_set_charset(b, &charset_raw, EOL_UNIX);
    } else {
        /* CG: default charset should be selectable */
        eb_set_charset(b, &charset_8859_1, b->eol_type);
    }

    /* add mark move callback */
    eb_add_callback(b, eb_offset_callback, &b->mark, 0);
    eb_add_callback(b, eb_offset_callback, &b->offset, 1);

    if (flags & BF_STYLES)
        eb_create_style_buffer(b, flags);

    return b;
}

void eb_clear(EditBuffer *b)
{
    b->flags &= ~BF_READONLY;

    /* XXX: should just reset logging instead of disabling it */
    b->save_log = 0;
    b->last_log = 0;
    eb_delete(b, 0, b->total_size);
    eb_free_log_buffer(b);

#ifdef CONFIG_MMAP
    eb_munmap_buffer(b);

    /* close and reset file handle */
    if (b->map_handle > 0) {
        close(b->map_handle);
    }
    b->map_handle = 0;
#endif
    b->modified = 0;

    /* TODO: clear buffer structure */
    //memset(b, 0, offsetof(EditBuffer, remanent_area));
}

void eb_free(EditBuffer **bp)
{
    if (*bp) {
        EditBuffer *b = *bp;
        QEmacsState *qs = b->qs;
        EditBuffer **pb;
        EditBuffer *b1;

        /* free b->mode_data_list by calling destructors */
        while (b->mode_data_list) {
            QEModeData *md = b->mode_data_list;
            b->mode_data_list = md->next;
            md->next = NULL;
            if (md->mode && md->mode->mode_free)
                md->mode->mode_free(b, md);
            qe_free(&md);
        }

        /* free each callback */
        while (b->first_callback) {
            EditBufferCallbackList *cb = b->first_callback;
            b->first_callback = cb->next;
            qe_free(&cb);
        }

        eb_delete_properties(b, 0, INT_MAX);
        eb_cache_remove(b);
        /* eb_clear frees b->log_buffer.
         * it should also call eb_free_style_buffer(b)
         * and eb_delete_properties(b, 0, INT_MAX);
         */
        eb_clear(b);

        /* suppress from buffer list */
        pb = &qs->first_buffer;
        while ((b1 = *pb) != NULL) {
            if (b1->log_buffer == b) {
                b1->log_buffer = NULL;
            }
            if (b1->b_styles == b) {
                b1->b_styles = NULL;
            }
            if (b1 == b)
                *pb = b1->next;
            else
                pb = &b1->next;
        }

        if (b == qs->trace_buffer)
            qs->trace_buffer = NULL;

        if (b->flags & BF_SYSTEM) {
            int i;
            for (i = 0; i < NB_YANK_BUFFERS; i++) {
                if (qs->yank_buffers[i] == b)
                    qs->yank_buffers[i] = NULL;
            }
        }

        eb_free_style_buffer(b);

        qe_free(&b->saved_data);
        // XXX: cannot use qe_free(bp) because *bp may have been set to NULL
        //      already, eg: eb_free_log_buffer() and eb_free_style_buffer()
        qe_free(&b);
        *bp = NULL;
    }
}

EditBuffer *qe_find_buffer_filename(QEmacsState *qs, const char *filename)
{
    EditBuffer *b;

    for (b = qs->first_buffer; b != NULL; b = b->next) {
        /* XXX: should also use stat to ensure this is same file */
        if (strequal(b->filename, filename))
            return b;
    }
    return NULL;
}

/* Find a window attached to a given buffer, different from s */
EditState *eb_find_window(EditBuffer *b, EditState *s)
{
    EditState *e;

    if (!b)
        return NULL;

    for (e = b->qs->first_window; e != NULL; e = e->next_window) {
        if (e != s && e->b == b)
            break;
    }
    return e;
}

void qe_trace_bytes(QEmacsState *qs, const void *buf, int size, int state)
{
    EditBuffer *b = qs->trace_buffer;
    EditState *e;
    const char *str = NULL;
    const u8 *p0, *endp, *p;
    int line, col, len, flush;
    int prev_state = qs->trace_buffer_state;

    /* prevent tracing if nagivating the *trace* buffer */
    if (!b || !(qs->trace_flags & state)
    ||  (qs->active_window && qs->active_window->b == b))
        return;

    /* output at end of trace buffer */
    b->offset = b->total_size;
    if (size < 0)
        size = strlen(buf);

    eb_get_pos(b, &line, &col, b->offset);
    flush = state & EB_TRACE_FLUSH;
    state &= ~EB_TRACE_FLUSH;
    if (prev_state == state) {
        if (state == EB_TRACE_COMMAND)
            flush = 1;
    } else {
        if (!(state <= EB_TRACE_COMMAND && prev_state <= state)
        &&  !(state == EB_TRACE_EMULATE && prev_state == EB_TRACE_PTY))
            flush = 1;
    }
    if (flush && col) {
        eb_putc(b, '\n');
        col = 0;
    }
    if (col == 0 || prev_state != state) {
        size_t i;
        qs->trace_buffer_state = state;
        for (i = 0; i < qe_trace_defs_count; i++) {
            if (state == qe_trace_defs[i].flags) {
                str = qe_trace_defs[i].name;
                break;
            }
        }
        if (str) {
            int len1 = (int)strlen(str);
            int width = (col == 0 ? 7 :
                         col <= 24 ? 31 - col :
                         col <= 52 ? 59 - col : len1 + 2);
            col += eb_printf(b, "%*s: ", width, str);
        }
    }
    p0 = buf;
    endp = p0 + size;

#define MAX_TRACE_WIDTH  98
    for (p = p0; p0 < endp; p++) {
        while (p >= endp || *p < 32 || *p >= 127 || *p == '\\') {
            if (p0 >= endp)
                break;
            if (col >= MAX_TRACE_WIDTH) {
                eb_puts(b, "\n       | ");
                col = 9;
            }
            if (p0 < p) {
                len = min_offset(p - p0, MAX_TRACE_WIDTH - col);
                eb_printf(b, "%.*s", len, p0);
                p0 += len;
                col += len;
                continue;
            }
            if (p < endp) {
                char buf1[16];
                len = byte_quote(buf1, sizeof buf1, *p);
                eb_puts(b, buf1);
                p0 = p + 1;
                col += len;
            }
            break;
        }
    }
    /* Make output visible in window */
    /* XXX: proper tracking should do this automatically */
    e = eb_find_window(b, NULL);
    if (e)
        e->offset = b->offset;
}

/************************************************************/
/* callbacks */

int eb_add_callback(EditBuffer *b, EditBufferCallback cb,
                    void *opaque, int arg)
{
    EditBufferCallbackList *l;

    l = qe_mallocz(EditBufferCallbackList);
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
void eb_offset_callback(qe__unused__ EditBuffer *b, void *opaque, int edge,
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

int eb_create_style_buffer(EditBuffer *b, int flags)
{
    if (b->b_styles) {
        /* FIXME: should extend style width if needed */
        return 0;
    } else {
        char name[MAX_BUFFERNAME_SIZE];
        snprintf(name, sizeof(name), "*S<%.*s>", MAX_BUFFERNAME_SIZE - 5, b->name);
        b->b_styles = qe_new_buffer(b->qs, name, BF_SYSTEM | BF_IS_STYLE | BF_RAW);
        if (!b->b_styles)
            return -1;
        b->flags |= flags & BF_STYLES;
        b->style_shift = (((unsigned)flags & BF_STYLES) / BF_STYLE1) - 1;
        b->style_bytes = 1 << b->style_shift;
        eb_set_style(b, 0, LOGOP_INSERT, 0, b->total_size);
        eb_add_callback(b, eb_style_callback, NULL, 0);
        return 1;
    }
}

void eb_free_style_buffer(EditBuffer *b)
{
    // FIXME: what if style buffer is shown in a window?
    eb_free(&b->b_styles);
    b->style_shift = b->style_bytes = 0;
    eb_free_callback(b, eb_style_callback, NULL);
}

/* XXX: should compress styles buffer with run length encoding */
void eb_set_style(EditBuffer *b, QETermStyle style, enum LogOperation op,
                  int offset, int size)
{
    union {
        uint64_t buf8[256 / 8];
        uint32_t buf4[256 / 4];
        uint16_t buf2[256 / 2];
        unsigned char buf[256];
        QETermStyle align;
    } s;
    int i, len;

    if (!b->b_styles || !size)
        return;

    offset = (offset >> b->char_shift) << b->style_shift;
    size = (size >> b->char_shift) << b->style_shift;

    switch (op) {
    case LOGOP_WRITE:
    case LOGOP_INSERT:
        /* XXX: should use a single loop to initialize buf */
        /* XXX: should initialize buf just once */
        while (size > 0) {
            len = min_offset(size, ssizeof(s.buf));
            if (b->style_shift == 3) {
                for (i = 0; i < len >> 3; i++) {
                    s.buf8[i] = style;
                }
            } else
            if (b->style_shift == 2) {
                for (i = 0; i < len >> 2; i++) {
                    s.buf4[i] = style;
                }
            } else
            if (b->style_shift == 1) {
                for (i = 0; i < len >> 1; i++) {
                    s.buf2[i] = style;
                }
            } else {
                memset(s.buf, style, len);
            }
            if (op == LOGOP_WRITE)
                eb_write(b->b_styles, offset, s.buf, len);
            else
                eb_insert(b->b_styles, offset, s.buf, len);
            size -= len;
            offset += len;
        }
        break;
    case LOGOP_DELETE:
        eb_delete(b->b_styles, offset, size);
        break;
    default:
        break;
    }
}

void eb_style_callback(EditBuffer *b, void *opaque, int arg,
                       enum LogOperation op, int offset, int size)
{
    eb_set_style(b, b->cur_style, op, offset, size);
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
        snprintf(buf, sizeof(buf), "*L<%.*s>", MAX_BUFFERNAME_SIZE - 5, b->name);
        b->log_buffer = qe_new_buffer(b->qs, buf, BF_SYSTEM | BF_IS_LOG | BF_RAW);
        if (!b->log_buffer)
            return;
        b->log_new_index = 0;
        b->log_current = 0;
        b->last_log = 0;
        b->last_log_char = 0;
        b->nb_logs = 0;
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
    &&  (size_t)b->log_new_index >= sizeof(lb) + sizeof(int)
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
    QEmacsState *qs = s->qs;
    EditBuffer *b = s->b;
    int log_index, size_trailer;
    LogBuffer lb;

    if (!b->log_buffer) {
        put_error(s, "No undo information");
        return;
    }

    /* deactivate region hilite and multi-cursor */
    s->region_style = 0;
    s->multi_cursor_active = 0;

    /* Should actually keep undo state current until new logs are added */
    if (qs->last_cmd_func != (CmdFunc)do_undo
    &&  qs->last_cmd_func != (CmdFunc)do_redo) {
        b->log_current = 0;
    }

    if (b->log_current == 0) {
        log_index = b->log_new_index;
    } else {
        log_index = b->log_current - 1;
    }
    if (log_index == 0) {
        put_error(s, "No further undo information");
        return;
    } else {
        put_status(s, "Undo!");
    }
    if (!qs->first_transient_key
    &&  (qs->last_key == 'u' || qs->last_key == 'u')) {
        put_status(s, "Repeat with 'u', 'r'");
        qe_register_transient_binding(qs, "undo", "u");
        qe_register_transient_binding(qs, "redo", "r");
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

    if (!b->log_buffer) {
        put_error(s, "No undo information");
        return;
    }

    /* deactivate region hilite */
    s->region_style = 0;

    /* Should actually keep undo state current until new logs are added */
    if (s->qs->last_cmd_func != (CmdFunc)do_undo
    &&  s->qs->last_cmd_func != (CmdFunc)do_redo) {
        b->log_current = 0;
    }

    if (!b->log_current || !b->log_new_index) {
        put_error(s, "Nothing to redo");
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

void eb_set_charset(EditBuffer *b, QECharset *charset, EOLType eol_type)
{
    int n;

    if (b->charset) {
        charset_decode_close(&b->charset_state);
    }
    b->eol_type = eol_type;
    b->charset = charset;
    b->flags &= ~BF_UTF8;
    if (charset == &charset_utf8)
        b->flags |= BF_UTF8;

    if (charset)
        charset_decode_init(&b->charset_state, charset, eol_type);

    b->char_bytes = 1;
    b->char_shift = 0;
    if (charset) {
        b->char_bytes = charset->char_size;
        if (charset->char_size == 4)
            b->char_shift = 2;
        else
            b->char_shift = charset->char_size - 1;
    }

    /* Reset page cache flags */
    for (n = 0; n < b->nb_pages; n++) {
        Page *p = &b->page_table[n];
        p->flags &= ~(PG_VALID_POS | PG_VALID_CHAR | PG_VALID_COLORS);
    }
}

/* XXX: change API to go faster */
char32_t eb_nextc(EditBuffer *b, int offset, int *next_ptr)
{
    u8 buf[MAX_CHAR_BYTES];
    char32_t ch;
    int c;

    /* XXX: should inline this */
    c = eb_read_one_byte(b, offset);
    if (c < 0) {
        /* to simplify calling code, return '\n' at buffer boundaries */
        ch = '\n';
        if (offset < 0)
            offset = 0;
        if (offset >= b->total_size)
            offset = b->total_size;
    } else {
        /* we use the charset conversion table directly to go faster */
        offset++;
        ch = b->charset_state.table[c];
        if (ch == ESCAPE_CHAR) {
            /* clear decode buffer to avoid undefined behavior at EOB */
            memset(buf, 0, MAX_CHAR_BYTES);
            eb_read(b, offset - 1, buf, MAX_CHAR_BYTES);
            b->charset_state.p = buf;
            /* XXX: incorrect behaviour on ill encoded utf8 sequences */
            ch = b->charset_state.decode_func(&b->charset_state);
            offset += (b->charset_state.p - buf) - 1;
        }
        if (ch == '\r') {
            if (b->eol_type == EOL_DOS) {
                b->charset_state.p = buf;
                if (eb_read(b, offset, buf, MAX_CHAR_BYTES) >= 1
                &&  b->charset_state.decode_func(&b->charset_state) == '\n') {
                    offset += b->charset_state.char_size;
                    ch = '\n';
                }
            } else
            if (b->eol_type == EOL_MAC) {
                ch = '\n';
            }
        } else
        if (ch == '\n') {
            if (b->eol_type == EOL_MAC) {
                ch = '\r';
            }
        }
    }
    *next_ptr = offset;
    return ch;
}

QETermStyle eb_get_style(EditBuffer *b, int offset)
{
    if (b->b_styles) {
        if (b->style_shift == 3) {
            uint64_t style = 0;
            eb_read(b->b_styles, (offset >> b->char_shift) << 3, &style, 8);
            return style;
        } else
        if (b->style_shift == 2) {
            uint32_t style = 0;
            eb_read(b->b_styles, (offset >> b->char_shift) << 2, &style, 4);
            return style;
        } else
        if (b->style_shift == 1) {
            uint16_t style = 0;
            eb_read(b->b_styles, (offset >> b->char_shift) << 1, &style, 2);
            return style;
        } else {
            uint8_t style = 0;
            eb_read(b->b_styles, (offset >> b->char_shift) << 0, &style, 1);
            return style;
        }
    }
    return 0;
}

/* compute offset after moving 'n' chars from 'offset'.
 * 'n' can be negative
 */
int eb_skip_chars(EditBuffer *b, int offset, int n)
{
    /*@API buffer
       Compute offset after moving `n` codepoints from `offset`.
       @argument `b` a valid pointer to an `EditBuffer`
       @argument `offset` the position in bytes in the buffer
       @argument `n` the number of codepoints to skip forward or backward
       @return the new buffer position
       @note 'n' can be negative
     */
    for (; n < 0 && offset > 0; n++) {
        offset = eb_prev(b, offset);
    }
    for (; n > 0 && offset < b->total_size; n--) {
        offset = eb_next(b, offset);
    }
    return offset;
}

int eb_delete_char32(EditBuffer *b, int offset) {
    /*@API buffer
       Delete one character at offset `offset`, return number of bytes removed
       @argument `b` a valid pointer to an `EditBuffer`
       @argument `offset` the position in bytes in the buffer
       @return the number of bytes removed
     */
    return eb_delete_range(b, offset, eb_next(b, offset));
}

int eb_skip_accents(EditBuffer *b, int offset) {
    /*@API buffer
       Skip over combining glyphs
       @argument `b` a valid pointer to an `EditBuffer`
       @argument `offset` the position in bytes in the buffer
       @return the new buffer position past any combining glyphs
     */
    int offset1;
    while (qe_isaccent(eb_nextc(b, offset, &offset1)))
        offset = offset1;
    return offset;
}

char32_t eb_next_glyph(EditBuffer *b, int offset, int *next_ptr) {
    /*@API buffer
       Read the main character for the next glyph,
       update offset to next_ptr
       @argument `b` a valid pointer to an `EditBuffer`
       @argument `offset` the position in bytes in the buffer
       @argument `next_ptr` a pointer to a variable for the updated
       buffer position after the codepoint and any combining glyphs
       @return the main codepoint value
     */
    char32_t c = eb_nextc(b, offset, &offset);
    if (c >= ' ') {
        offset += eb_skip_accents(b, offset);
    }
    *next_ptr = offset;
    return c;
}

char32_t eb_prev_glyph(EditBuffer *b, int offset, int *next_ptr) {
    /*@API buffer
       Return the main character for the previous glyph,
       update offset to next_ptr
       @argument `b` a valid pointer to an `EditBuffer`
       @argument `offset` the position in bytes in the buffer
       @argument `next_ptr` a pointer to a variable for the updated
       buffer position of the codepoint before any combining glyphs
       @return the main codepoint value
     */
    for (;;) {
        char32_t c = eb_prevc(b, offset, &offset);
        if (!qe_isaccent(c)) {
            *next_ptr = offset;
            return c;
        }
    }
}

/* compute offset after moving 'n' glyphs from 'offset'.
 * 'n' can be negative,
 * combining accents are skipped as part of the previous character.
 */
int eb_skip_glyphs(EditBuffer *b, int offset, int n) {
    int offset1;

    if (n < 0) {
        while (offset > 0) {
            char32_t c = eb_prevc(b, offset, &offset);
            n += !qe_isaccent(c);
            if (n >= 0)
                break;
        }
    } else {
        while (offset < b->total_size) {
            char32_t c = eb_nextc(b, offset, &offset1);
            if (!qe_isaccent(c) && n-- <= 0)
                break;
            offset = offset1;
        }
    }
    return offset;
}

/* return number of bytes deleted. n can be negative to delete
 * characters before offset
 */
int eb_delete_chars(EditBuffer *b, int offset, int n)
{
    return eb_delete_range(b, offset, eb_skip_chars(b, offset, n));
}

/* return number of bytes deleted. n can be negative to delete
 * characters before offset
 */
int eb_delete_glyphs(EditBuffer *b, int offset, int n)
{
    return eb_delete_range(b, offset, eb_skip_glyphs(b, offset, n));
}

/* XXX: only stateless charsets are supported */
/* XXX: suppress that? */
char32_t eb_prevc(EditBuffer *b, int offset, int *prev_ptr)
{
    char32_t ch;
    int char_size;
    u8 buf[MAX_CHAR_BYTES + 1], *q;

    if (offset <= 0) {
        offset = 0;
        ch = '\n';
    } else {
        if (b->charset == &charset_utf8) {
            char_size = 1;
            offset -= 1;
            ch = eb_read_one_byte(b, offset);
            if (utf8_is_trailing_byte(ch)) {
                int offset1 = offset;
                q = buf + sizeof(buf);
                *--q = '\0';
                *--q = ch;
                while (utf8_is_trailing_byte(ch) && offset > 0 && q > buf) {
                    offset -= 1;
                    *--q = ch = eb_read_one_byte(b, offset);
                }
                if (ch >= 0xc0) {
                    ch = utf8_decode((const char **)(void *)&q);
                }
                if (q != buf + sizeof(buf) - 1) {
                    /* decoding error: only take the last byte */
                    offset = offset1;
                    ch = buf[sizeof(buf) - 2];
                }
            }
        } else {
            /* XXX: this only works for stateless charsets.
             * it would fail for utf-16 and east-asian encodings.
             * Should use the line/column system to be really generic
             */
            char_size = b->charset_state.char_size;
            offset -= char_size;
            q = buf + sizeof(buf) - char_size;
            eb_read(b, offset, q, char_size);
            b->charset_state.p = q;
            ch = b->charset_state.decode_func(&b->charset_state);
        }
        if (ch == '\r') {
            if (b->eol_type == EOL_MAC) {
                ch = '\n';
            }
        } else
        if (ch == '\n') {
            if (b->eol_type == EOL_DOS) {
                if (offset >= char_size) {
                    eb_read(b, offset - char_size, buf, char_size);
                    b->charset_state.p = buf;
                    if (b->charset_state.decode_func(&b->charset_state) == '\r')
                        offset -= char_size;
                }
            } else
            if (b->eol_type == EOL_MAC) {
                ch = '\r';
            }
        }
    }
    *prev_ptr = offset;
    return ch;
}

int eb_goto_pos(EditBuffer *b, int line1, int col1)
{
    Page *p, *p_end;
    int line2, col2, line, col, offset, offset1;

    line = 0;
    col = 0;
    offset = 0;

    p = b->page_table;
    if (!p)
        return 0;
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
                offset += b->charset->goto_line_func(&b->charset_state,
                    p->data, p->size, line1 - line);
                line = line1;
                col = 0;
            }
            while (col < col1 && eb_nextc(b, offset, &offset1) != '\n') {
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
    if (!p)
        goto the_end;
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

    if (!b->charset->variable_size && b->eol_type != EOL_DOS) {
        offset = min_offset(pos * b->charset->char_size, b->total_size);
    } else {
        offset = 0;
        p = b->page_table;
        p_end = b->page_table + b->nb_pages;
        while (p < p_end) {
            if (!(p->flags & PG_VALID_CHAR)) {
                p->flags |= PG_VALID_CHAR;
                p->nb_chars = b->charset->get_chars_func(&b->charset_state, p->data, p->size);
            }
            if (pos < p->nb_chars) {
                offset += b->charset->goto_char_func(&b->charset_state, p->data, p->size, pos);
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

    if (!b->charset->variable_size && b->eol_type != EOL_DOS) {
        /* offset is round down to character boundary */
        pos = min_offset(offset, b->total_size) / b->charset->char_size;
    } else {
        /* XXX: should handle rounding if EOL_DOS */
        /* XXX: should fix buffer offset via charset specific method */
        /* XXX: fails in case of encoding error */
        if (b->charset == &charset_utf8) {
            /* Round offset down to character boundary */
            u8 buf[1];
            while (offset > 0 && eb_read(b, offset, buf, 1) == 1 &&
                   utf8_is_trailing_byte(buf[0])) {
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
                p->nb_chars = b->charset->get_chars_func(&b->charset_state, p->data, p->size);
            }
            if (offset < p->size) {
                pos += b->charset->get_chars_func(&b->charset_state, p->data, offset);
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
 * number of bytes removed.
 */
int eb_delete_range(EditBuffer *b, int p1, int p2)
{
    if (p1 > p2) {
        int tmp = p1;
        p1 = p2;
        p2 = tmp;
    }
    return eb_delete(b, p1, p2 - p1);
}

/* replace 'size' bytes at offset 'offset' with 'size1' bytes from 'buf'
 * return the number of bytes written
 */
int eb_replace(EditBuffer *b, int offset, int size,
               const void *buf, int size1)
{
    /* CG: behaviour is not exactly identical: mark, point and other
     * callback based offsets will be updated differently.  should
     * write portion that fits and insert or delete remainder?
     * or should simulate callbacks?
     */
    if (size == size1) {
        return eb_write(b, offset, buf, size1);
    } else {
        eb_delete(b, offset, size);
        return eb_insert(b, offset, buf, size1);
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
    s = qe_mallocz(BufferIOState);
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
        s->offset += eb_insert(b, s->offset, s->buffer, size);
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
int eb_raw_buffer_load1(EditBuffer *b, FILE *f, int offset)
{
    unsigned char buf[IOBUF_SIZE];
    int len, size, inserted;

    //put_status(b->qs->active_window, "Loading %s", filename);
    size = inserted = 0;
    for (;;) {
        len = fread(buf, 1, IOBUF_SIZE, f);
        if (len <= 0) {
            if (ferror(f))
                return -1;
            break;
        }
        inserted += eb_insert(b, offset, buf, len);
        offset += len;
        size += len;
    }
    //put_status(b->qs->active_window, "");
    return size;
}

#ifdef CONFIG_MMAP
void eb_munmap_buffer(EditBuffer *b)
{
    if (b->map_address) {
        munmap(b->map_address, b->map_length);
        b->map_address = NULL;
        b->map_length = 0;
    }
}

int eb_mmap_buffer(EditBuffer *b, const char *filename)
{
    int fd, len, file_size, n, size;
    u8 *file_ptr, *ptr;
    Page *p;

    eb_munmap_buffer(b);

    fd = open(filename, O_RDONLY);
    if (fd < 0)
        return -1;
    file_size = lseek(fd, 0, SEEK_END);
    //put_status(b->qs->active_window, "Mapping %s", filename);
    file_ptr = mmap(NULL, file_size, PROT_READ, MAP_SHARED, fd, 0);
    if ((void*)file_ptr == MAP_FAILED) {
        close(fd);
        return -1;
    }
    b->map_address = file_ptr;
    b->map_length = file_size;

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
    // XXX: not needed
    b->map_handle = fd;
    //put_status(b->qs->active_window, "");
    return 0;
}
#endif

static int raw_buffer_load(EditBuffer *b, FILE *f)
{
    struct stat st;

    /* TODO: Should produce error messages */

    if (stat(b->filename, &st))
        return -1;

#ifdef CONFIG_MMAP
    if (st.st_size >= b->qs->mmap_threshold) {
        if (!eb_mmap_buffer(b, b->filename))
            return 0;
    }
#endif
    if (st.st_size <= b->qs->max_load_size) {
        return eb_raw_buffer_load1(b, f, 0);
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

    //put_status(b->qs->active_window, "Writing %s", filename);
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
    //put_status(b->qs->active_window, "");
    return written;
}

static void raw_buffer_close(qe__unused__ EditBuffer *b)
{
    /* nothing to do */
}

/* Associate a buffer with a file and rename it to match the
   filename. Find a unique buffer name */
void eb_set_filename(EditBuffer *b, const char *filename)
{
    pstrcpy(unconst(char *)b->filename, sizeof(b->filename), filename);
    eb_set_buffer_name(b, get_basename(filename));
}

/* Encode unicode character according to buffer charset and eol_type */
/* Return number of bytes of conversion */
/* the function uses '?' to indicate that no match could be found in
   buffer charset */
int eb_encode_char32(EditBuffer *b, char *buf, char32_t c) {
    QECharset *charset = b->charset;
    u8 *q = (u8 *)buf;

    if (c == '\n') {
        if (b->eol_type == EOL_MAC)
            c = '\r';
        else
        if (b->eol_type == EOL_DOS) {
            q = charset->encode_func(charset, q, '\r');
        }
    }
    q = charset->encode_func(charset, q, c);
    if (!q) {
        q = (u8 *)buf;
        *q++ = '?';
    }
    *q = '\0';
    return q - (u8 *)buf;
}

/* Insert unicode character according to buffer encoding */
/* Return number of bytes inserted */
int eb_insert_char32(EditBuffer *b, int offset, char32_t c) {
    char buf[MAX_CHAR_BYTES];
    int len;

    len = eb_encode_char32(b, buf, c);
    return eb_insert(b, offset, buf, len);
}

/* Replace the character at `offset` with `c`,
 * return number of bytes to move past `c`.
 */
int eb_replace_char32(EditBuffer *b, int offset, char32_t c) {
    char buf[MAX_CHAR_BYTES];
    int len;
    int offset1;

    len = eb_encode_char32(b, buf, c);
    eb_nextc(b, offset, &offset1);
    return eb_replace(b, offset, offset1 - offset, buf, len);
}

int eb_insert_char32_n(EditBuffer *b, int offset, char32_t c, int n) {
    char buf[1024];
    int size, pos;

    size = pos = 0;
    while (n --> 0) {
        pos += eb_encode_char32(b, buf + pos, c);
        if (pos > ssizeof(buf) - MAX_CHAR_BYTES || n == 0) {
            size += eb_insert(b, offset + size, buf, pos);
            pos = 0;
        }
    }
    return size;
}

/* Insert buffer with utf8 chars according to buffer encoding */
/* Return number of bytes inserted */
int eb_insert_utf8_buf(EditBuffer *b, int offset, const char *str, int len)
{
    if (b->charset == &charset_utf8 && b->eol_type == EOL_UNIX) {
        return eb_insert(b, offset, str, len);
    } else {
        char buf[1024];
        int size, pos;
        const char *p = str;
        const char *strend = str + len;

        size = pos = 0;
        while (p < strend) {
            char32_t c = utf8_decode(&p);
            int clen = eb_encode_char32(b, buf + pos, c);
            pos += clen;
            if (pos > countof(buf) - MAX_CHAR_BYTES || p >= strend) {
                size += eb_insert(b, offset + size, buf, pos);
                pos = 0;
            }
        }
        return size;
    }
}

/* Insert chars from char32 array according to buffer encoding */
/* Return number of bytes inserted */
int eb_insert_char32_buf(EditBuffer *b, int offset, const char32_t *p, int len)
{
    char buf[1024];
    int i, size, pos;

    i = size = pos = 0;
    while (i < len) {
        char32_t c = p[i++];
        int clen = eb_encode_char32(b, buf + pos, c);
        pos += clen;
        if (pos > countof(buf) - MAX_CHAR_BYTES || i >= len) {
            size += eb_insert(b, offset + size, buf, pos);
            pos = 0;
        }
    }
    return size;
}

int eb_insert_str(EditBuffer *b, int offset, const char *str)
{
    return eb_insert_utf8_buf(b, offset, str, strlen(str));
}

int eb_match_char32(EditBuffer *b, int offset, char32_t c, int *offsetp)
{
    if (eb_nextc(b, offset, &offset) != c)
        return 0;
    if (offsetp)
        *offsetp = offset;
    return 1;
}

int eb_match_str_utf8(EditBuffer *b, int offset, const char *str, int *offsetp) {
    const char *p = str;

    while (*p) {
        char32_t c = utf8_decode(&p);
        if (eb_nextc(b, offset, &offset) != c)
            return 0;
    }
    if (offsetp)
        *offsetp = offset;
    return 1;
}

int eb_match_str_utf8_reverse(EditBuffer *b, int offset, const char *str, int pos, int *offsetp) {
    const char *p = str + pos;
    while (p > str) {
        char32_t c = utf8_decode_prev(&p, str);
        if (eb_prevc(b, offset, &offset) != c)
            return 0;
    }
    if (offsetp)
        *offsetp = offset;
    return 1;
}

int eb_match_istr_utf8(EditBuffer *b, int offset, const char *str, int *offsetp) {
    const char *p = str;

    while (*p) {
        char32_t c = utf8_decode(&p);
        // XXX: should ignore accents?
        if (qe_wtoupper(eb_nextc(b, offset, &offset)) != qe_wtoupper(c))
            return 0;
    }
    if (offsetp)
        *offsetp = offset;
    return 1;
}

int eb_putc(EditBuffer *b, char32_t c) {
    char buf[8];
    int len = eb_encode_char32(b, buf, c);

    return eb_insert(b, b->offset, buf, len);
}

int eb_puts(EditBuffer *b, const char *s) {
    return eb_insert_utf8_buf(b, b->offset, s, strlen(s));
}

int eb_vprintf(EditBuffer *b, const char *fmt, va_list ap) {
    char buf0[1024];
    char *buf;
    int len, size, written;
    va_list ap2;

#ifndef va_copy
#define va_copy(s,d)  __va_copy(s,d)
#endif

    va_copy(ap2, ap);
    size = sizeof(buf0);
    buf = buf0;
    len = vsnprintf(buf, size, fmt, ap2);
    va_end(ap2);
    if (len >= size) {
        size = len + 1;
        buf = qe_malloc_bytes(size);
        vsnprintf(buf, size, fmt, ap);
    }
    /* CG: insert buf encoding according to b->charset and b->eol_type.
     * buf may contain \0 characters via the %c modifer.
     * XXX: %c does not encode non ASCII characters as utf8.
     */
    written = eb_insert_utf8_buf(b, b->offset, buf, len);
    if (buf != buf0)
        qe_free(&buf);
    return written;
}

int eb_printf(EditBuffer *b, const char *fmt, ...) {
    va_list ap;
    int written;

    va_start(ap, fmt);
    written = eb_vprintf(b, fmt, ap);
    va_end(ap);
    return written;
}

#if 0
/* pad current line with spaces so that it reaches column n */
void eb_line_pad(EditBuffer *b, int offset, int n) {
    /* Compute visual column visual column */
    int tw = b->tab_width > 0 ? b->tab_width : 8;
    int col = text_screen_width(b, eb_goto_bol(b, offset), offset, tw);
    if (n > col)
        eb_insert_spaces(b, offset, n - col);
}
#endif

/* Read the contents of a buffer region encoded in a utf8 string */
int eb_get_region_contents(EditBuffer *b, int start, int stop,
                           char *buf, int buf_size, int encode_zero)
{
    int size, offset;
    buf_t outbuf, *out;

    stop = clamp_offset(stop, 0, b->total_size);
    start = clamp_offset(start, 0, stop);
    size = stop - start;

    /* do not use eb_read if overflow to avoid partial characters */
    if (b->charset == &charset_utf8 && b->eol_type == EOL_UNIX
    &&  size < buf_size) {
        eb_read(b, start, buf, size);
        buf[size] = '\0';
        if (!encode_zero || (int)strlen(buf) == size)
            return size;
    }
    out = buf_init(&outbuf, buf, buf_size);
    for (offset = start; offset < stop;) {
        char32_t c = eb_nextc(b, offset, &offset);
        if (c == 0 && encode_zero) {
            /* special case: use redundant utf8x encoding */
            buf_write(out, "\xC0\x80", 2);
        } else {
            buf_putc_utf8(out, c);
        }
    }
    return out->len;
}

/* Compute the size of the contents of a buffer region encoded in utf8 */
int eb_get_region_content_size(EditBuffer *b, int start, int stop)
{
    stop = clamp_offset(stop, 0, b->total_size);
    start = clamp_offset(start, 0, stop);

    /* assuming start and stop fall on character boundaries */
    if (b->charset == &charset_utf8 && b->eol_type == EOL_UNIX) {
        return stop - start;
    } else {
        int offset, size;
        char buf[MAX_CHAR_BYTES];

        for (size = 0, offset = start; offset < stop;) {
            char32_t c = eb_nextc(b, offset, &offset);
            size += utf8_encode(buf, c);
        }
        return size;
    }
}

/* Insert 'size' bytes of 'src' buffer from position 'src_offset' into
 * buffer 'dest' at offset 'dest_offset'. 'src' MUST BE DIFFERENT from
 * 'dest'. Charset converson between source and destination buffer is
 * performed.
 * Return the number of bytes inserted.
 */
int eb_insert_buffer_convert(EditBuffer *dest, int dest_offset,
                             EditBuffer *src, int src_offset,
                             int size)
{
    int styles_flags = min_int((dest->flags & BF_STYLES), (src->flags & BF_STYLES));

    if (dest->charset == src->charset
    &&  dest->eol_type == src->eol_type
    &&  !styles_flags) {
        return eb_insert_buffer(dest, dest_offset, src, src_offset, size);
    } else {
        EditBuffer *b;
        int offset, offset_max, offset1 = dest_offset;

        b = dest;
        if (!styles_flags
        &&  ((b->flags & BF_SAVELOG) || dest_offset != b->total_size)) {
            b = qe_new_buffer(b->qs, "*tmp*", BF_SYSTEM);
            if (!b)
                return 0;
            eb_set_charset(b, dest->charset, dest->eol_type);
            offset1 = 0;
        }

        /* well, not very fast, but simple */
        /* XXX: should optimize save_log system for insert sequences */
        // XXX: should optimize styles transfer
        offset_max = min_offset(src->total_size, src_offset + size);
        size = 0;
        for (offset = src_offset; offset < offset_max;) {
            char buf[MAX_CHAR_BYTES];
            QETermStyle style = eb_get_style(src, offset);
            char32_t c = eb_nextc(src, offset, &offset);
            int len = eb_encode_char32(b, buf, c);
            b->cur_style = style;
            size += eb_insert(b, offset1 + size, buf, len);
        }

        if (b != dest) {
            size = eb_insert_buffer(dest, dest_offset, b, 0, b->total_size);
            eb_free(&b);
        }
        return size;
    }
}

int eb_get_line(EditBuffer *b, char32_t *buf, int size,
                int offset, int *offset_ptr)
{
    /*@API buffer
       Get contents of the line starting at offset `offset` as an array of
       code points. `offset` is bumped to point to the first unread character.
       @argument `b` a valid pointer to an `EditBuffer`
       @argument `buf` a valid pointer to the destination array
       @argument `size` the length of the destination array
       @argument `offset` the offset in bytes of the beginning of the line in
       the buffer
       @argument `offset_ptr` a pointer to a variable to receive the offset
       of the first unread character in the buffer.
       @returns the number of codepoints stored into the destination array
       before the newline if any.
       @note: the return value `len` verifies `len >= 0` and `len < buf_size`.
       If a complete line was read, `buf[len] == '\n'` and `buf[len + 1] == '\0'`.
       Truncation can be detected by checking `buf[len] != '\n'` or `len < buf_size - 1`.
     */
    int len = 0;

    if (size > 0) {
        for (;;) {
            int next;
            char32_t c = eb_nextc(b, offset, &next);
            if (len + 1 >= size) {
                buf[len] = '\0';
                break;
            }
            buf[len++] = c;
            offset = next;
            if (c == '\n') {
                /* end of line: offset points to the beginning of the next line */
                /* adjust return value for easy stripping and truncation test */
                buf[len--] = '\0';
                break;
            }
        }
    }
    if (offset_ptr)
        *offset_ptr = offset;
    return len;
}

int eb_get_line_length(EditBuffer *b, int offset, int *offset_ptr)
{
    /*@API buffer
       Get the length in codepoints of the line starting at offset `offset`
       as an number of code points. `offset` is bumped to point to the first
       character after the newline.
       @argument `b` a valid pointer to an `EditBuffer`
       @argument `offset` the offset in bytes of the beginning of the line in
       the buffer
       @argument `offset_ptr` a pointer to a variable to receive the offset
       of the first unread character in the buffer.
       @returns the number of codepoints in the line including the newline if any.
     */
    int len = 0;

    while (eb_nextc(b, offset, &offset) != '\n') {
        len++;
    }
    if (offset_ptr)
        *offset_ptr = offset;
    return len;
}

int eb_fgets(EditBuffer *b, char *buf, int size,
             int offset, int *offset_ptr)
{
    /*@API buffer
       Get the contents of the line starting at offset `offset` encoded
       in UTF-8. `offset` is bumped to point to the first unread character.
       @argument `b` a valid pointer to an `EditBuffer`
       @argument `buf` a valid pointer to the destination array
       @argument `size` the length of the destination array
       @argument `offset` the offset in bytes of the beginning of the line in
       the buffer
       @argument `offset_ptr` a pointer to a variable to receive the offset
       of the first unread character in the buffer.
       @returns the number of bytes stored into the destination array before
       the newline if any. No partial encoding is stored into the array.
       @note: the return value `len` verifies `len >= 0` and `len < buf_size`.
       If a complete line was read, `buf[len] == '\n'` and `buf[len + 1] == '\0'`.
       Truncation can be detected by checking `buf[len] != '\n'` or `len < buf_size - 1`.
     */
    int len = 0;
    buf_t outbuf, *out;

    if (size > 0) {
        out = buf_init(&outbuf, buf, size);
        for (;;) {
            int next;
            char32_t c = eb_nextc(b, offset, &next);
            if (!buf_putc_utf8(out, c)) {
                /* truncation: offset points to the first unread character */
                break;
            }
            offset = next;
            if (c == '\n') {
                /* end of line: offset points to the beginning of the next line */
                /* adjust return value for easy stripping and truncation test */
                out->len--;
                break;
            }
        }
        len = out->len;
    }
    if (offset_ptr)
        *offset_ptr = offset;
    return len;
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

/* test for blank line starting at <offset>.
 * return 0 if not blank.
 * return 1 if blank and store start of next line in <*offset1>.
 */
int eb_is_blank_line(EditBuffer *b, int offset, int *offset1) {
    char32_t c;

    while ((c = eb_nextc(b, offset, &offset)) != '\n') {
        if (!qe_isblank(c))
            return 0;
    }
    if (offset1)
        *offset1 = offset;
    return 1;
}

/* check if <offset> is within indentation. */
int eb_is_in_indentation(EditBuffer *b, int offset) {
    char32_t c;

    while ((c = eb_prevc(b, offset, &offset)) != '\n') {
        if (!qe_isblank(c))
            return 0;
    }
    return 1;
}

/* return offset of the end of the line containing offset */
int eb_goto_eol(EditBuffer *b, int offset1) {
    for (;;) {
        int offset = offset1;
        char32_t c = eb_nextc(b, offset, &offset1);
        if (c == '\n')
            return offset;
    }
}

int eb_next_line(EditBuffer *b, int offset) {
    for (;;) {
        char32_t c = eb_nextc(b, offset, &offset);
        if (c == '\n')
            return offset;
    }
}

/* buffer property handling */

static void eb_plist_callback(EditBuffer *b, void *opaque, int edge,
                              enum LogOperation op, int offset, int size)
{
    QEProperty **pp;
    QEProperty *p;

    /* update properties */
    if (op == LOGOP_INSERT) {
        for (p = b->property_list; p; p = p->next) {
            if (p->offset >= offset)
                p->offset += size;
        }
    } else
    if (op == LOGOP_DELETE) {
        for (pp = &b->property_list; (p = *pp) != NULL;) {
            if (p->offset >= offset) {
                if (p->offset < offset + size) {
                    /* property is anchored inside block: remove it */
                    *pp = (*pp)->next;
                    if (p->type & QE_PROP_FREE) {
                        qe_free(&p->data);
                    }
                    qe_free(&p);
                    continue;
                }
                p->offset -= size;
            }
            pp = &(*pp)->next;
        }
    }
}

void eb_add_property(EditBuffer *b, int offset, int type, void *data) {
    QEProperty *p;
    QEProperty **pp;

    if (!b->property_list) {
        eb_add_callback(b, eb_plist_callback, NULL, 0);
    }

    /* insert property in ascending order of offset */
    for (pp = &b->property_list; (p = *pp) != NULL; pp = &(*pp)->next) {
        if (p->offset > offset)
            break;
    }

    p = qe_mallocz(QEProperty);
    p->offset = offset;
    p->type = type;
    p->data = data;
    p->next = *pp;
    *pp = p;
}

void eb_add_tag(EditBuffer *b, int offset, const char *s) {
    QEProperty *p;

    /* prevent tag duplicates */
    for (p = b->property_list; p != NULL; p = p->next) {
        if (p->offset == offset && p->type == QE_PROP_TAG && strequal(p->data, s))
            return;
    }
    eb_add_property(b, offset, QE_PROP_TAG, qe_strdup(s));
}

QEProperty *eb_find_property(EditBuffer *b, int offset, int offset2, int type) {
    QEProperty *found = NULL;
    QEProperty *p;
    for (p = b->property_list; p && p->offset < offset2; p = p->next) {
        if (p->offset >= offset && p->type == type) {
            /* return the last property between offset and offset2 */
            found = p;
        }
    }
    return found;
}

void eb_delete_properties(EditBuffer *b, int offset, int offset2) {
    QEProperty *p;
    QEProperty **pp;

    if (!b->property_list)
        return;

    for (pp = &b->property_list; (p = *pp) != NULL && p->offset < offset2;) {
        if (p->offset >= offset && p->offset < offset2) {
            *pp = (*pp)->next;
            if (p->type & QE_PROP_FREE) {
                qe_free(&p->data);
            }
            qe_free(&p);
        } else {
            pp = &(*pp)->next;
        }
    }
    if (!b->property_list) {
        eb_free_callback(b, eb_plist_callback, NULL);
    }
}

/* buffer data type handling */

void qe_register_data_type(QEmacsState *qs, EditBufferDataType *bdt)
{
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

    if (!b->qs->backup_inhibited
    &&  strlen(filename) < MAX_FILENAME_SIZE - 1) {
        /* backup old file if present */
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
    //eb_free_log_buffer(b);
    b->modified = 0;
    return ret;
}

/* invalidate buffer raw data */
void eb_invalidate_raw_data(EditBuffer *b)
{
    b->save_log = 0;
    eb_delete(b, 0, b->total_size);
    eb_free_log_buffer(b);
    b->modified = 0;
}

EditBufferDataType raw_data_type = {
    "raw",
    raw_buffer_load,
    raw_buffer_save,
    raw_buffer_close,
    NULL, /* next */
};

/* init buffer handling */
void qe_data_init(QEmacsState *qs)
{
    qe_register_data_type(qs, &raw_data_type);
}
