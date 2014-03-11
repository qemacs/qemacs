/*
 * Directory editor mode for QEmacs.
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

#include "qe.h"

enum { DIRED_HEADER = 2 };

enum {
    DIRED_SORT_NAME = 1,
    DIRED_SORT_EXTENSION = 2,
    DIRED_SORT_SIZE = 4,
    DIRED_SORT_DATE = 8,
    DIRED_SORT_MASK = 1+2+4+8,
    DIRED_SORT_GROUP = 16,
    DIRED_SORT_DESCENDING = 32,
};

static int dired_signature;

typedef struct DiredState {
    void *signature;
    StringArray items;
    int sort_mode; /* DIRED_SORT_GROUP | DIRED_SORT_NAME */
    int last_index;
    char path[MAX_FILENAME_SIZE]; /* current path */
} DiredState;

/* opaque structure for sorting DiredState.items StringArray */
typedef struct DiredItem {
    DiredState *state;
    mode_t st_mode;
    off_t size;
    time_t mtime;
    int offset;
    char mark;
    char name[1];
} DiredItem;

static inline int dired_get_index(EditState *s) {
    return list_get_pos(s) - DIRED_HEADER;
}

static void dired_free(DiredState *ds)
{
    if (ds) {
        int i;

        for (i = 0; i < ds->items.nb_items; i++) {
            qe_free(&ds->items.items[i]->opaque);
        }

        free_strings(&ds->items);

        ds->last_index = -1;
    }
}

static DiredState *dired_get_state(EditState *s, int status)
{
    DiredState *ds = s->b->priv_data;

    if (ds && ds->signature == &dired_signature)
        return ds;

    if (status)
        put_status(s, "Not a dired buffer");

    return NULL;
}

static char *dired_get_filename(EditState *s,
                                char *buf, int buf_size, int index)
{
    DiredState *ds;
    const StringItem *item;
    const DiredItem *dip;

    if (!(ds = dired_get_state(s, 1)))
        return NULL;

    if (buf_size > 0)
        buf[0] = '\0';

    if (index < 0)
        index = dired_get_index(s);

    if (index < 0 || index >= ds->items.nb_items)
        return NULL;

    item = ds->items.items[index];
    dip = item->opaque;

    /* build filename */
    /* CG: Should canonicalize path */
    return makepath(buf, buf_size, ds->path, dip->name);
}

static int dired_find_target(EditState *s, const char *target)
{
    DiredState *ds;
    char filename[MAX_FILENAME_SIZE];
    int i;

    if (target) {
        if (!(ds = dired_get_state(s, 1)))
            return -1;

        for (i = 0; i < ds->items.nb_items; i++) {
            if (dired_get_filename(s, filename, sizeof(filename), i)
            &&  strequal(filename, target)) {
                return i;
            }
        }
    }
    return -1;
}

/* sort alphabetically with directories first */
static int dired_sort_func(const void *p1, const void *p2)
{
    const StringItem *item1 = *(const StringItem **)p1;
    const StringItem *item2 = *(const StringItem **)p2;
    const DiredItem *dip1 = item1->opaque;
    const DiredItem *dip2 = item2->opaque;
    int sort_mode = dip1->state->sort_mode, res;
    int is_dir1, is_dir2;

    if (sort_mode & DIRED_SORT_GROUP) {
        is_dir1 = !!S_ISDIR(dip1->st_mode);
        is_dir2 = !!S_ISDIR(dip2->st_mode);
        if (is_dir1 != is_dir2)
            return is_dir2 - is_dir1;
    }
    for (;;) {
        if (sort_mode & DIRED_SORT_DATE) {
            if (dip1->mtime != dip2->mtime) {
                res = (dip1->mtime < dip2->mtime) ? -1 : 1;
                break;
            }
        }
        if (sort_mode & DIRED_SORT_SIZE) {
            if (dip1->size != dip2->size) {
                res = (dip1->size < dip2->size) ? -1 : 1;
                break;
            }
        }
        if (sort_mode & DIRED_SORT_EXTENSION) {
            res = qe_strcollate(get_extension(dip1->name),
                                get_extension(dip2->name));
            if (res)
                break;
        }
        res = qe_strcollate(dip1->name, dip2->name);
        break;
    }
    return (sort_mode & DIRED_SORT_DESCENDING) ? -res : res;
}

/* select current item */
static void dired_sort_list(EditState *s)
{
    DiredState *ds;
    StringItem *item, *cur_item;
    DiredItem *dip;
    EditBuffer *b;
    int index, i;

    if (!(ds = dired_get_state(s, 1)))
        return;

    index = dired_get_index(s);
    cur_item = NULL;
    if (index >= 0 && index < ds->items.nb_items)
        cur_item = ds->items.items[index];

    qsort(ds->items.items, ds->items.nb_items,
          sizeof(StringItem *), dired_sort_func);

    /* construct list buffer */
    b = s->b;
    /* deleting buffer contents resets s->offset and s->offset_top */
    eb_clear(b);

    if (DIRED_HEADER) {
        long long total_bytes;
        int ndirs, nfiles;

        eb_printf(b, "  Directory of %s:\n", ds->path);

        ndirs = nfiles = 0;
        total_bytes = 0;
        for (i = 0; i < ds->items.nb_items; i++) {
            item = ds->items.items[i];
            dip = item->opaque;
            if (S_ISDIR(dip->st_mode)) {
                ndirs++;
            } else {
                nfiles++;
                total_bytes += dip->size;
            }
        }
        eb_printf(b, "    %d director%s, %d file%s, %lld byte%s\n",
                  ndirs, ndirs == 1 ? "y" : "ies",
                  nfiles, &"s"[nfiles == 1],
                  (long long)total_bytes, &"s"[total_bytes == 1]);
    }

    for (i = 0; i < ds->items.nb_items; i++) {
        item = ds->items.items[i];
        dip = item->opaque;
        dip->offset = b->total_size;
        if (item == cur_item) {
            ds->last_index = i;
            s->offset = b->total_size;
        }
        eb_printf(b, "%c %s\n", dip->mark, item->str);
    }
    b->modified = 0;
    b->flags |= BF_READONLY;
}

static void dired_mark(EditState *s, int mark)
{
    DiredState *ds;
    const StringItem *item;
    DiredItem *dip;
    unsigned char ch;
    int index;

    if (!(ds = dired_get_state(s, 1)))
        return;

    index = dired_get_index(s);
    if (index < 0 || index >= ds->items.nb_items)
        return;
    item = ds->items.items[index];
    dip = item->opaque;

    ch = dip->mark = mark;
    do_bol(s);
    s->b->flags &= ~BF_READONLY;
    eb_write(s->b, s->offset, &ch, 1);
    s->b->flags |= BF_READONLY;

    text_move_up_down(s, 1);
}

static void dired_sort(EditState *s, const char *sort_order)
{
    DiredState *ds;
    const char *p;

    if (!(ds = dired_get_state(s, 1)))
        return;

    for (p = sort_order; *p; p++) {
        switch (qe_tolower((unsigned char)*p)) {
        case 'n':       /* name */
            ds->sort_mode &= ~DIRED_SORT_MASK;
            ds->sort_mode |= DIRED_SORT_NAME;
            break;
        case 'e':       /* extension */
            ds->sort_mode &= ~DIRED_SORT_MASK;
            ds->sort_mode |= DIRED_SORT_EXTENSION;
            break;
        case 's':       /* size */
            ds->sort_mode &= ~DIRED_SORT_MASK;
            ds->sort_mode |= DIRED_SORT_SIZE;
            break;
        case 'd':       /* direct */
            ds->sort_mode &= ~DIRED_SORT_MASK;
            ds->sort_mode |= DIRED_SORT_DATE;
            break;
        case 'u':       /* ungroup */
            ds->sort_mode &= ~DIRED_SORT_GROUP;
            break;
        case 'g':       /* group */
            ds->sort_mode |= DIRED_SORT_GROUP;
            break;
        case '+':       /* ascending */
            ds->sort_mode &= ~DIRED_SORT_DESCENDING;
            break;
        case '-':       /* descending */
            ds->sort_mode |= DIRED_SORT_DESCENDING;
            break;
        case 'r':       /* reverse */
            ds->sort_mode ^= DIRED_SORT_DESCENDING;
            break;
        }
    }
    dired_sort_list(s);
}

#define MAX_COL_FILE_SIZE 32

static void dired_build_list(EditState *s, const char *path,
                             const char *target)
{
    DiredState *ds;
    FindFileState *ffst;
    char filename[MAX_FILENAME_SIZE];
    char line[1024], buf[1024];
    const char *p;
    struct stat st;
    int ct, len, index;
    StringItem *item;

    if (!(ds = dired_get_state(s, 1)))
        return;

    /* free previous list, if any */
    dired_free(ds);

    /* CG: should make absolute ? */
    canonicalize_path(ds->path, sizeof(ds->path), path);
    eb_set_filename(s->b, ds->path);
    s->b->flags |= BF_DIRED;

    ffst = find_file_open(ds->path, "*");
    /* Should scan directory/filespec before computing lines to adjust
     * filename gutter width
     */
    while (!find_file_next(ffst, filename, sizeof(filename))) {
        if (lstat(filename, &st) < 0)
            continue;
        p = get_basename(filename);

#if 1   /* CG: bad idea, but causes spurious bugs */
        /* exclude redundant '.' and '..' */
        if (strequal(p, ".") || strequal(p, ".."))
            continue;
#endif
        pstrcpy(line, sizeof(line), p);
        ct = 0;
        if (S_ISDIR(st.st_mode)) {
            ct = '/';
        } else
        if (S_ISFIFO(st.st_mode)) {
            ct = '|';
        } else
        if (S_ISSOCK(st.st_mode)) {
            ct = '=';
        } else
        if (S_ISLNK(st.st_mode)) {
            ct = '@';
        } else
        if ((st.st_mode & 0111) != 0) {
            ct = '*';
        }
        if (ct) {
            buf[0] = ct;
            buf[1] = '\0';
            pstrcat(line, sizeof(line), buf);
        }
        /* pad with ' ' */
        len = strlen(line);
        while (len < MAX_COL_FILE_SIZE)
            line[len++] = ' ';
        line[len] = '\0';
        /* add file size or file info */
        if (S_ISREG(st.st_mode)) {
            snprintf(buf, sizeof(buf), "%9ld", (long)st.st_size);
        } else
        if (S_ISDIR(st.st_mode)) {
            snprintf(buf, sizeof(buf), "%9s", "<dir>");
        } else
        if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) {
            int major, minor;
            major = (st.st_rdev >> 8) & 0xff;
            minor = st.st_rdev & 0xff;
            snprintf(buf, sizeof(buf), "%c%4d%4d",
                     S_ISCHR(st.st_mode) ? 'c' : 'b',
                     major, minor);
        } else
        if (S_ISLNK(st.st_mode)) {
            pstrcat(line, sizeof(line), "-> ");
            len = readlink(filename, buf, sizeof(buf) - 1);
            if (len < 0)
                len = 0;
            buf[len] = '\0';
        } else {
            buf[0] = '\0';
        }
        pstrcat(line, sizeof(line), buf);

        item = add_string(&ds->items, line);
        if (item) {
            DiredItem *dip;
            int plen = strlen(p);

            dip = qe_malloc_hack(DiredItem, plen);
            dip->state = ds;
            dip->st_mode = st.st_mode;
            dip->size = st.st_size;
            dip->mtime = st.st_mtime;
            dip->mark = ' ';
            memcpy(dip->name, p, plen + 1);
            item->opaque = dip;
        }
    }
    find_file_close(&ffst);
    
    dired_sort_list(s);

    index = dired_find_target(s, target);
    s->offset = eb_goto_pos(s->b, max(index, 0) + DIRED_HEADER, 0);
}

/* select current item */
static void dired_select(EditState *s)
{
    struct stat st;
    char filename[MAX_FILENAME_SIZE];
    EditState *e;

    if (!dired_get_filename(s, filename, sizeof(filename), -1))
        return;

    /* now we can act */
    if (lstat(filename, &st) < 0)
        return;
    if (S_ISDIR(st.st_mode)) {
        dired_build_list(s, filename, NULL);
    } else
    if (S_ISREG(st.st_mode)) {
        e = find_window(s, KEY_RIGHT);
        if (e) {
            /* delete dired window */
            do_delete_window(s, 1);
            /* remove preview flag */
            e->b->flags &= ~BF_PREVIEW;
        } else {
            do_find_file(s, filename);
        }
    }
}

static void dired_view_file(EditState *s, const char *filename)
{
    EditBuffer *b;
    EditState *e;

    e = find_window(s, KEY_RIGHT);
    if (!e)
        return;
    /* close previous temporary buffers, if any */
    /* CG: Should use the do_find_alternate to replace buffer */
    b = e->b;
    if (b && (b->flags & BF_PREVIEW) && !b->modified) {
        /* free the buffer if no longer viewed */
        b->flags |= BF_TRANSIENT;
        //switch_to_buffer(e, NULL);
    }

    if (e) {
        do_find_file(e, filename);
        /* disable wrapping to get nicer display */
        e->wrap = WRAP_TRUNCATE;
        b = e->b;
        if (!b) {
            b = eb_new("*scratch*", BF_SAVELOG | BF_UTF8);
            e->b = b;
        }
        /* mark buffer as preview, so that it will get recycled if needed */
        /* CG: this is wrong if buffer existed already */
        b->flags |= BF_PREVIEW;
    }
}

static void dired_execute(EditState *s)
{
    /* Actually delete, copy, or move the marked items */
    put_status(s, "Not yet implemented");
}

static void dired_parent(EditState *s)
{
    DiredState *ds;
    char target[MAX_FILENAME_SIZE];
    char filename[MAX_FILENAME_SIZE];

    if (!(ds = dired_get_state(s, 1)))
        return;

    pstrcpy(target, sizeof(target), ds->path);
    makepath(filename, sizeof(filename), ds->path, "..");

    dired_build_list(s, filename, target);
}

static void dired_refresh(EditState *s)
{
    DiredState *ds;
    char target[MAX_FILENAME_SIZE];

    if (!(ds = dired_get_state(s, 1)))
        return;

    dired_get_filename(s, target, sizeof(target), -1);
    dired_build_list(s, ds->path, target);
}

static void dired_display_hook(EditState *s)
{
    DiredState *ds;
    char filename[MAX_FILENAME_SIZE];
    int index;

    if (!(ds = dired_get_state(s, 1)))
        return;

    /* Prevent point from going beyond list */
    if (s->offset && s->offset == s->b->total_size)
        do_up_down(s, -1);

    /* open file so that user can see it before it is selected */
    /* XXX: find a better solution (callback) */
    index = dired_get_index(s);
    if (index < 0 || index >= ds->items.nb_items)
        return;
    /* Should not rely on last_index! */
    if (index != ds->last_index) {
        ds->last_index = index;
        if (dired_get_filename(s, filename, sizeof(filename), -1)) {
            dired_view_file(s, filename);
        }
    }
}

static void dired_close(EditBuffer *b)
{
    DiredState *ds = b->priv_data;

    if (ds && ds->signature == &dired_signature) {
        dired_free(ds);
    }

    qe_free(&b->priv_data);
    if (b->close == dired_close)
        b->close = NULL;
}

static int dired_mode_init(EditState *s, ModeSavedData *saved_data)
{
    DiredState *ds;

    list_mode.mode_init(s, saved_data);

    if (s->b->priv_data) {
        ds = s->b->priv_data;
        if (ds->signature != &dired_signature)
            return -1;
    } else {
        /* XXX: should be allocated by buffer_load API */
        ds = qe_mallocz(DiredState);
        if (!ds)
            return -1;

        ds->signature = &dired_signature;
        ds->sort_mode = DIRED_SORT_GROUP | DIRED_SORT_NAME;
        ds->last_index = -1;

        s->b->priv_data = ds;
        s->b->close = dired_close;

        /* XXX: should be built by buffer_load API */
        dired_build_list(s, s->b->filename, NULL);
    }

    /* XXX: File system charset should be detected automatically */
    /* XXX: If file system charset is not utf8, eb_printf will fail */
    eb_set_charset(s->b, &charset_utf8, s->b->eol_type);

    return 0;
}

/* can only apply dired mode on directories */
static int dired_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (p->b->priv_data) {
        DiredState *ds = p->b->priv_data;
        if (ds->signature != &dired_signature)
            return 0;
        else
            return 100;
    }
    if (S_ISDIR(p->st_mode))
        return 95;
    else
    if (strchr(p->real_filename, '*') || strchr(p->real_filename, '?'))
        return 90;
    else
        return 0;
}

static ModeDef dired_mode;

/* open dired window on the left. The directory of the current file is
   used */
void do_dired(EditState *s)
{
    QEmacsState *qs = s->qe_state;
    EditBuffer *b;
    EditState *e;
    int width, index;
    char filename[MAX_FILENAME_SIZE], *p;
    char target[MAX_FILENAME_SIZE];

    /* Should take directory argument with optional switches,
     * find dired window if exists,
     * else create one and do this.
     * recursive listing and multi directory patterns.
     */

    /* Should reuse previous dired buffer for same filespec */
    b = eb_scratch("*dired*", BF_READONLY | BF_SYSTEM | BF_UTF8);

    /* Remember target as current current buffer filename */
    pstrcpy(target, sizeof(target), s->b->filename);

    /* Set the filename to the directory of the current file */
    canonicalize_absolute_path(filename, sizeof(filename), target);
    if (!is_directory(filename)) {
        p = strrchr(filename, '/');
        if (p)
            *p = '\0';
    }
    eb_set_filename(b, filename);

    width = qs->width / 5;
    e = insert_window_left(b, width, WF_MODELINE);
    edit_set_mode(e, &dired_mode);

    index = dired_find_target(e, target);
    e->offset = eb_goto_pos(e->b, max(index, 0) + DIRED_HEADER, 0);

    /* modify active window */
    qs->active_window = e;
}

/* specific dired commands */
static CmdDef dired_commands[] = {
    CMD0( KEY_RET, KEY_RIGHT,
          "dired-select", dired_select)
    CMD0( KEY_TAB, KEY_NONE,
          "dired-tab", do_other_window)
    /* dired-abort should restore previous buffer in right-window */
    CMD1( KEY_CTRL('g'), KEY_NONE,
          "dired-abort", do_delete_window, 0)
    CMD0( ' ', KEY_CTRL('t'),
          "dired-toggle-selection", list_toggle_selection)
    /* BS should go back to previous item and unmark it */
    CMD2( 's', KEY_NONE,
          "dired-sort", dired_sort, ESs,
          "s{Sort order: }|sortkey|")
    /* s -> should also change switches */
    CMD1( 'd', KEY_NONE,
          "dired-delete", dired_mark, 'D')
    CMD1( 'c', KEY_NONE,
          "dired-copy", dired_mark, 'C')
    CMD1( 'm', KEY_NONE,
          "dired-move", dired_mark, 'M')
    CMD1( 'u', KEY_NONE,
          "dired-unmark", dired_mark, ' ')
    CMD0( 'x', KEY_NONE,
          "dired-execute", dired_execute)
    CMD1( 'n', KEY_NONE,
          "next-line", do_up_down, 1)
    CMD1( 'p', KEY_NONE,
          "previous-line", do_up_down, -1)
    CMD0( 'r', KEY_NONE,
          "dired-refresh", dired_refresh)
    /* g -> refresh all expanded dirs ? */
    /* l -> relist single directory or marked files ? */
    CMD0( '^', KEY_LEFT,
          "dired-parent", dired_parent)
    /* need commands for splitting, unsplitting, zooming, making subdirs */
    /* h -> info */
    /* i, + -> create subdirectory */
    /* o -> explore in other window */
    /* R -> rename a file or move selection to another directory */
    /* C -> copy files */
    /* mark files globally */
    CMD_DEF_END,
};

static CmdDef dired_global_commands[] = {
    CMD0( KEY_CTRLX(KEY_CTRL('d')), KEY_NONE,
          "dired", do_dired)
    CMD_DEF_END,
};

static int dired_init(void)
{
    /* inherit from list mode */
    /* CG: assuming list_mode already initialized ? */
    memcpy(&dired_mode, &list_mode, sizeof(ModeDef));
    dired_mode.name = "dired";
    dired_mode.mode_probe = dired_mode_probe;
    dired_mode.mode_init = dired_mode_init;
    /* CG: not a good idea, display hook has side effect on layout */
    dired_mode.display_hook = dired_display_hook;

    /* first register mode */
    qe_register_mode(&dired_mode);

    qe_register_cmd_table(dired_commands, &dired_mode);
    qe_register_cmd_table(dired_global_commands, NULL);

    return 0;
}

qe_module_init(dired_init);
