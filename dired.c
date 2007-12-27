/*
 * Directory editor mode for QEmacs.
 *
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

#include "qe.h"

enum { DIRED_HEADER = 0 };

enum {
    DIRED_SORT_NAME = 1,
    DIRED_SORT_EXTENSION = 2,
    DIRED_SORT_SIZE = 4,
    DIRED_SORT_DATE = 8,
    DIRED_SORT_MASK = 1+2+4+8,
    DIRED_SORT_GROUP = 16,
    DIRED_SORT_DESCENDING = 32,
};

typedef struct DiredState {
    StringArray items;
    int sort_mode; /* DIRED_SORT_GROUP | DIRED_SORT_NAME */
    int last_index;
    char path[MAX_FILENAME_SIZE]; /* current path */
} DiredState;

/* opaque structure for sorting DiredState.items StringArray */
typedef struct DiredItem {
    DiredState *state;
    mode_t mode;
    off_t size;
    time_t mtime;
    int offset;
    char mark;
    char name[1];
} DiredItem;

static void dired_view_file(EditState *s, const char *filename);

static inline int dired_get_index(EditState *s) {
    return list_get_pos(s) - DIRED_HEADER;
}

static void dired_free(EditState *s)
{
    DiredState *ds = s->mode_data;
    int i;
    
    /* free opaques */
    for (i = 0; i < ds->items.nb_items; i++) {
        qe_free(&ds->items.items[i]->opaque);
    }

    free_strings(&ds->items);

    ds->last_index = -1;

    /* reset cursor position */
    s->offset = 0;
}

/* sort alphabetically with directories first */
static int dired_sort_func(const void *p1, const void *p2)
{
    const StringItem *item1 = *(const StringItem **)p1;
    const StringItem *item2 = *(const StringItem **)p2;
    const DiredItem *dip1 = item1->opaque;
    const DiredItem *dip2 = item2->opaque;
    int mode = dip1->state->sort_mode, res;
    int is_dir1, is_dir2;

    if (mode & DIRED_SORT_GROUP) {
        is_dir1 = !!S_ISDIR(dip1->mode);
        is_dir2 = !!S_ISDIR(dip2->mode);
        if (is_dir1 != is_dir2)
            return is_dir2 - is_dir1;
    }
    for (;;) {
        if (mode & DIRED_SORT_DATE) {
            if (dip1->mtime != dip2->mtime) {
                res = (dip1->mtime < dip2->mtime) ? -1 : 1;
                break;
            }
        }
        if (mode & DIRED_SORT_SIZE) {
            if (dip1->size != dip2->size) {
                res = (dip1->size < dip2->size) ? -1 : 1;
                break;
            }
        }
        if (mode & DIRED_SORT_EXTENSION) {
            res = strcmp(extension(dip1->name), extension(dip2->name));
            if (res)
                break;
        }
        res = strcmp(dip1->name, dip2->name);
        break;
    }
    return (mode & DIRED_SORT_DESCENDING) ? -res : res;
}

/* select current item */
static void do_dired_sort(EditState *s)
{
    DiredState *hs = s->mode_data;
    StringItem *item, *cur_item;
    DiredItem *dip;
    EditBuffer *b;
    int index, i;

    index = dired_get_index(s);
    cur_item = NULL;
    if (index >= 0 && index < hs->items.nb_items)
        cur_item = hs->items.items[index];

    qsort(hs->items.items, hs->items.nb_items, 
          sizeof(StringItem *), dired_sort_func);

    /* construct list buffer */
    b = s->b;
    b->flags &= ~BF_READONLY;
    eb_delete(b, 0, b->total_size);
    s->offset = 0;
    if (DIRED_HEADER)
        eb_printf(b, "  %s:\n", hs->path);
    for (i = 0; i < hs->items.nb_items; i++) {
        item = hs->items.items[i];
        dip = item->opaque;
        dip->offset = b->total_size;
        if (item == cur_item)
            s->offset = b->total_size;
        eb_printf(b, "%c %s\n", dip->mark, item->str);
    }
    b->modified = 0;
    b->flags |= BF_READONLY;
}

static void dired_mark(EditState *s, int mark)
{
    DiredState *hs = s->mode_data;
    const StringItem *item;
    DiredItem *dip;
    unsigned char ch;
    int index;

    index = dired_get_index(s);

    if (index < 0 || index >= hs->items.nb_items)
        return;
    item = hs->items.items[index];
    dip = item->opaque;

    ch = dip->mark = mark;
    eb_write(s->b, dip->offset, &ch, 1);

    text_move_up_down(s, 1);
}

static void dired_sort(EditState *s, const char *sort_order)
{
    DiredState *hs = s->mode_data;
    const char *p;
    
    for (p = sort_order; *p; p++) {
        switch (qe_tolower((unsigned char)*p)) {
        case 'n':       /* name */
            hs->sort_mode &= ~DIRED_SORT_MASK;
            hs->sort_mode |= DIRED_SORT_NAME;
            break;
        case 'e':       /* extension */
            hs->sort_mode &= ~DIRED_SORT_MASK;
            hs->sort_mode |= DIRED_SORT_EXTENSION;
            break;
        case 's':       /* size */
            hs->sort_mode &= ~DIRED_SORT_MASK;
            hs->sort_mode |= DIRED_SORT_SIZE;
            break;
        case 'd':       /* direct */
            hs->sort_mode &= ~DIRED_SORT_MASK;
            hs->sort_mode |= DIRED_SORT_DATE;
            break;
        case 'u':       /* ungroup */
            hs->sort_mode &= ~DIRED_SORT_GROUP;
            break;
        case 'g':       /* group */
            hs->sort_mode |= DIRED_SORT_GROUP;
            break;
        case '+':       /* ascending */
            hs->sort_mode &= ~DIRED_SORT_DESCENDING;
            break;
        case '-':       /* descending */
            hs->sort_mode |= DIRED_SORT_DESCENDING;
            break;
        case 'r':       /* reverse */
            hs->sort_mode ^= DIRED_SORT_DESCENDING;
            break;
        }
    }
    do_dired_sort(s);
}

#define MAX_COL_FILE_SIZE 32

static void build_dired_list(EditState *s, const char *path)
{
    DiredState *hs = s->mode_data;
    FindFileState *ffst;
    char filename[MAX_FILENAME_SIZE];
    char line[1024], buf[1024];
    const char *p;
    struct stat st;
    int ct, len;
    StringItem *item;

    /* free previous list, if any */
    dired_free(s);

    /* CG: should make absolute ? */
    canonize_path(hs->path, sizeof(hs->path), path);
    eb_set_filename(s->b, hs->path);
    s->b->flags |= BF_DIRED;

    ffst = find_file_open(hs->path, "*");
    while (!find_file_next(ffst, filename, sizeof(filename))) {
        if (lstat(filename, &st) < 0)
            continue;
        p = basename(filename);

#if 1   /* CG: bad idea, but causes spurious bugs */
        /* exclude redundant '.' and '..' */
        if (!strcmp(p, ".") || !strcmp(p, ".."))
            continue;
#endif
        pstrcpy(line, sizeof(line), p);
        ct = 0;
        if (S_ISDIR(st.st_mode)) {
            ct = '/';
        } else if (S_ISFIFO(st.st_mode)) {
            ct = '|';
        } else if (S_ISSOCK(st.st_mode)) {
            ct = '=';
        } else if (S_ISLNK(st.st_mode)) {
            ct = '@';
        } else if ((st.st_mode & 0111) != 0) {
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
        } else if (S_ISDIR(st.st_mode)) {
            snprintf(buf, sizeof(buf), "%9s", "<dir>");
        } else if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) {
            int major, minor;
            major = (st.st_rdev >> 8) & 0xff;
            minor = st.st_rdev & 0xff;
            snprintf(buf, sizeof(buf), "%c%4d%4d", 
                     S_ISCHR(st.st_mode) ? 'c' : 'b', 
                     major, minor);
        } else if (S_ISLNK(st.st_mode)) {
            pstrcat(line, sizeof(line), "-> ");
            len = readlink(filename, buf, sizeof(buf) - 1);
            if (len < 0)
                len = 0;
            buf[len] = '\0';
        } else {
            buf[0] = '\0';
        }
        pstrcat(line, sizeof(line), buf);
        
        item = add_string(&hs->items, line);
        if (item) {
            DiredItem *dip;

            dip = qe_malloc_hack(DiredItem, strlen(p));
            dip->state = hs;
            dip->mode = st.st_mode;
            dip->size = st.st_size;
            dip->mtime = st.st_mtime;
            dip->mark = ' ';
            strcpy(dip->name, p);
            item->opaque = dip;
        }
    }
    find_file_close(ffst);
    do_dired_sort(s);
}

static char *get_dired_filename(EditState *s, 
                                char *buf, int buf_size, int index)
{
    DiredState *hs = s->mode_data;
    const StringItem *item;
    const DiredItem *dip;

    /* CG: assuming buf_size > 0 */
    buf[0] = '\0';
    
    if (index < 0 || index >= hs->items.nb_items)
        return NULL;
    
    item = hs->items.items[index];
    dip = item->opaque;
    
    /* build filename */
    /* CG: Should canonize path */
    return makepath(buf, buf_size, hs->path, dip->name);
}

/* select current item */
static void dired_select(EditState *s)
{
    struct stat st;
    char filename[MAX_FILENAME_SIZE];
    EditState *e;

    if (!get_dired_filename(s, filename, sizeof(filename), 
                            dired_get_index(s))) {
        return;
    }

    /* now we can act */
    if (lstat(filename, &st) < 0)
        return;
    if (S_ISDIR(st.st_mode)) {
        build_dired_list(s, filename);
    } else if (S_ISREG(st.st_mode)) {
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
    EditState *e, *e1;

    e = find_window(s, KEY_RIGHT);
    if (!e)
        return;
    /* close previous temporary buffers, if any */
    /* CG: Should use the do_find_alternate to replace buffer */
    b = e->b;
    if ((b->flags & BF_PREVIEW) && !b->modified) {
        switch_to_buffer(e, NULL);
        /* Before freeing buffer, make sure it isn't used by another window.
         * This could happen if we split the view window and continue browsing. */
        for (e1 = s->qe_state->first_window; e1 != NULL; e1 = e1->next_window) {
            if (e1 != s && e1->b == b)
                break;
        }
        if (!e1)
            eb_free(b);
    }

    if (e) {
        do_find_file(e, filename);
        /* disable wrapping to get nicer display */
        e->wrap = WRAP_TRUNCATE;
        b = e->b;
        if (!b) {
            b = eb_new("*scratch*", BF_SAVELOG);
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
    DiredState *hs = s->mode_data;
    char filename[MAX_FILENAME_SIZE];
    
    makepath(filename, sizeof(filename), hs->path, "..");

    /* CG: Should make current directory current item in parent */
    build_dired_list(s, filename);
}

static void dired_refresh(EditState *s)
{
    DiredState *hs = s->mode_data;
    
    /* CG: Should try and keep current entry */
    build_dired_list(s, hs->path);
}

static void dired_display_hook(EditState *s)
{
    DiredState *ds = s->mode_data;
    char filename[MAX_FILENAME_SIZE];
    int index;

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
        if (get_dired_filename(s, filename, sizeof(filename), 
                               dired_get_index(s))) {
            dired_view_file(s, filename);
        }
    }
}

static int dired_mode_init(EditState *s, ModeSavedData *saved_data)
{
    DiredState *hs;

    list_mode.mode_init(s, saved_data);

    hs = s->mode_data;
    hs->sort_mode = DIRED_SORT_GROUP | DIRED_SORT_NAME;

    build_dired_list(s, s->b->filename);

    return 0;
}

static void dired_mode_close(EditState *s)
{
    dired_free(s);
    list_mode.mode_close(s);
}

/* can only apply dired mode on directories */
static int dired_mode_probe(ModeProbeData *p)
{
    if (S_ISDIR(p->mode))
        return 100;
    else
        return 0;
}

static ModeDef dired_mode;

/* open dired window on the left. The directory of the current file is
   used */
void do_dired(EditState *s)
{
    DiredState *hs;
    QEmacsState *qs = s->qe_state;
    EditBuffer *b, *b0;
    EditState *e, *e1;
    int width, index, i;
    char filename[MAX_FILENAME_SIZE], *p;

    /* Should take directory argument with optional switches,
     * find dired window if exists,
     * else create one and do this.
     * recursive listing and multi directory patterns.
     */

    /* remember current buffer for target positioning, because
     * s may be destroyed by  insert_window_left
     */
    b0 = s->b;

    b = eb_new("*dired*", BF_READONLY | BF_SYSTEM);

    /* set the filename to the directory of the current file */
    pstrcpy(filename, sizeof(filename), s->b->filename);
    p = strrchr(filename, '/');
    if (p)
        *p = '\0';
    canonize_absolute_path(filename, sizeof(filename), filename);
    eb_set_filename(b, filename);
    
    width = qs->width / 5;
    e = insert_window_left(b, width, WF_MODELINE);
    do_set_mode(e, &dired_mode, NULL);
    hs = e->mode_data;

    e1 = find_window(e, KEY_RIGHT);
    if (e1)
        b0 = e1->b;

    index = 0;
    /* CG: target file should be an argument to this command */
    for (i = 0; i < hs->items.nb_items; i++) {
        if (get_dired_filename(e, filename, sizeof(filename), i)
        &&  !strcmp(filename, b0->filename)) {
            index = i;
            break;
        }
    }
    e->offset = eb_goto_pos(e->b, index + DIRED_HEADER, 0);

    /* modify active window */
    qs->active_window = e;
}

/* specific dired commands */
static CmdDef dired_commands[] = {
    CMD0( KEY_RET, KEY_RIGHT, "dired-select", dired_select)
    CMD0( KEY_TAB, KEY_NONE, "dired-tab", do_other_window)
    /* dired-abort should restore previous buffer in right-window */
    CMD1( KEY_CTRL('g'), KEY_NONE, "dired-abort", do_delete_window, 0)
    CMD0( ' ', KEY_CTRL('t'), "dired-toggle_selection", list_toggle_selection)
    /* BS should go back to previous item and unmark it */
    CMD_( 's', KEY_NONE, "dired-sort", dired_sort, ESs, "s{Sort order: }")
    /* s -> should also change switches */
    CMD1( 'd', KEY_NONE, "dired-delete", dired_mark, 'D')
    CMD1( 'c', KEY_NONE, "dired-copy", dired_mark, 'C')
    CMD1( 'm', KEY_NONE, "dired-move", dired_mark, 'M')
    CMD1( 'u', KEY_NONE, "dired-unmark", dired_mark, ' ')
    CMD0( 'x', KEY_NONE, "dired-execute", dired_execute)
    CMD1( 'n', KEY_NONE, "next-line", do_up_down, 1 )
    CMD1( 'p', KEY_NONE, "previous-line", do_up_down, -1 )
    CMD0( 'r', KEY_NONE, "dired-refresh", dired_refresh)
    /* g -> refresh all expanded dirs ? */
    /* l -> relist single directory or marked files ? */
    CMD0( '^', KEY_LEFT, "dired-parent", dired_parent)
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
    CMD0( KEY_CTRLX(KEY_CTRL('d')), KEY_NONE, "dired", do_dired)
    CMD_DEF_END,
};

static int dired_init(void)
{
    /* inherit from list mode */
    /* CG: assuming list_mode already initialized ? */
    memcpy(&dired_mode, &list_mode, sizeof(ModeDef));
    dired_mode.name = "dired";
    dired_mode.instance_size = sizeof(DiredState);
    dired_mode.mode_probe = dired_mode_probe;
    dired_mode.mode_init = dired_mode_init;
    dired_mode.mode_close = dired_mode_close;
    dired_mode.display_hook = dired_display_hook;
    
    /* first register mode */
    qe_register_mode(&dired_mode);

    qe_register_cmd_table(dired_commands, "dired");
    qe_register_cmd_table(dired_global_commands, NULL);

    return 0;
}

qe_module_init(dired_init);
