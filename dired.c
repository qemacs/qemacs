/*
 * Directory editor mode for QEmacs.
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

typedef struct DiredState {
    StringArray items;
    int last_pos;
    char path[1024]; /* current path */
} DiredState;

static void open_new_file(EditState *s);

static void dired_display(EditState *s)
{
    DiredState *ds = s->mode_data;
    int pos;

    list_mode.display(s);

    /* open file so that user can see it before it is selected */
    /* XXX: find a better solution (callback) */
    pos = list_get_pos(s);
    if (pos != ds->last_pos) {
        ds->last_pos = pos;
        open_new_file(s);
    }
}

void dired_free(EditState *s)
{
    DiredState *ds = s->mode_data;
    int i;
    
    /* free opaques */
    for(i=0;i<ds->items.nb_items;i++) {
        free(ds->items.items[i]->opaque);
    }

    free_strings(&ds->items);

    /* reset cursor position */
    s->offset = 0;
}

/* sort alphabetically with directories first */
static int dired_sort_func(const void *p1, const void *p2)
{
    StringItem *item1 = *(StringItem **)p1;
    StringItem *item2 = *(StringItem **)p2;
    int len1, len2, is_dir1, is_dir2;

    len1 = strlen(item1->str);
    len2 = strlen(item2->str);
    is_dir1 = (strchr(item1->str, '/') != NULL);
    is_dir2 = (strchr(item2->str, '/') != NULL);
    
    if (is_dir1 != is_dir2)
        return is_dir2 - is_dir1;
    else
        return strcmp(item1->str, item2->str);
}

#define MAX_COL_FILE_SIZE 32

void build_dired_list(EditState *s, const char *path)
{
    DiredState *hs = s->mode_data;
    FindFileState *ffs;
    char filename[1024];
    char line[1024], buf[1024];
    const char *p;
    struct stat st;
    int ct, len, i;
    StringItem *item;
    EditBuffer *b;

    /* free previous list, if any */
    dired_free(s);
    hs->last_pos = -1;

    canonize_path(hs->path, sizeof(hs->path), path);

    ffs = find_file_open(hs->path, "*");
    while (!find_file_next(ffs, filename, sizeof(filename))) {
        if (lstat(filename, &st) < 0)
            continue;
        p = basename(filename);

        /* exclude redundant '.' and '..' */
        if (!strcmp(p, ".") || !strcmp(p, ".."))
            continue;

        strcpy(line, " ");
        pstrcat(line, sizeof(line), p);
        ct = 0;
        if (S_ISDIR(st.st_mode)) {
            ct = '/';
        } else if (S_ISFIFO(st.st_mode)) {
            ct = '|';
        } else if (S_ISSOCK(st.st_mode)) {
            ct = '=';
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
            sprintf(buf, "%9ld", (long)st.st_size);
        } else if (S_ISDIR(st.st_mode)) {
            sprintf(buf, "%9s", "<dir>");
        } else if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) {
            int major, minor;
            major = (st.st_rdev >> 8) & 0xff;
            minor = st.st_rdev & 0xff;
            sprintf(buf, "%c%4d%4d", 
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
        if (item)
            item->opaque = strdup(p);
    }
    find_file_close(ffs);
    qsort(hs->items.items, hs->items.nb_items, 
          sizeof(StringItem *), dired_sort_func);

    /* construct list buffer */
    b = s->b;
    eb_delete(b, 0, b->total_size);
    for(i=0;i<hs->items.nb_items;i++) {
        eb_printf(b, "%s", hs->items.items[i]->str);
        if (i != hs->items.nb_items - 1)
            eb_printf(b, "\n");
    }
    b->modified = 0;
}

static void get_dired_filename(EditState *s, 
                               char *buf, int buf_size, int index)
{
    DiredState *hs = s->mode_data;
    StringItem *item;

    buf[0] = '\0';
    
    if (index < 0 || index >= hs->items.nb_items)
        return;
    item = hs->items.items[index];
    
    /* build filename */
    pstrcpy(buf, buf_size, hs->path);
    pstrcat(buf, buf_size, "/");
    pstrcat(buf, buf_size, (const char *)item->opaque);
}


/* select current item */
static void dired_select(EditState *s)
{
    struct stat st;
    char filename[1024];
    EditState *e;

    get_dired_filename(s, filename, sizeof(filename), 
                       list_get_pos(s));

    /* now we can act */
    if (lstat(filename, &st) < 0)
        return;
    if (S_ISDIR(st.st_mode)) {
        build_dired_list(s, filename);
    } else if (S_ISREG(st.st_mode)) {
        e = find_window_right(s);
        if (e) {
            /* delete dired window */
            do_delete_window(s, 1);
            /* remove preview flag */
            e->b->flags &= ~BF_PREVIEW;
        } else {
            do_load(s, filename);
        }
    }
}

static void open_new_file(EditState *s)
{
    EditBuffer *b;
    EditState *e, *e1;
    char filename[1024];

    e = find_window_right(s);
    if (!e)
        return;
    /* close previous temporary buffers, if any */
    b = e->b;
    if ((b->flags & BF_PREVIEW) && !b->modified) {
        switch_to_buffer(e, NULL);
        /* Before freeing buffer, make sure it isn't used by another window.
         * This could happen if we split the view window and continue browsing. */
        for(e1 = qe_state.first_window; e1 != NULL; e1 = e1->next_window) {
            if (e1 != s && e1->b == b)
                break;
        }
        if (!e1)
            eb_free(b);
    }

    get_dired_filename(s, filename, sizeof(filename), 
                       list_get_pos(s));
    if (e) {
        do_load(e, filename);
        /* disable wrapping to get nicer display */
        e->wrap = WRAP_TRUNCATE;
        b = e->b;
        if (!b) {
            b = eb_new("*scratch*", BF_SAVELOG);
            e->b = b;
        }
        /* mark buffer as preview, so that it will get recycled if needed */
        b->flags |= BF_PREVIEW;
    }
}


static void dired_parent(EditState *s)
{
    DiredState *hs = s->mode_data;
    char filename[1024];
    
    pstrcpy(filename, sizeof(filename), hs->path);
    pstrcat(filename, sizeof(filename), "/..");

    build_dired_list(s, filename);
}

static int dired_mode_init(EditState *s, ModeSavedData *saved_data)
{
    list_mode.mode_init(s, saved_data);

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

ModeDef dired_mode;

/* open dired window on the left. The directory of the current file is
   used */
void do_dired(EditState *s)
{
    DiredState *hs;
    QEmacsState *qs = s->qe_state;
    EditBuffer *b;
    EditState *e;
    int width, i;
    char filename[1024], *p;

    b = eb_new("*dired*", BF_READONLY | BF_SYSTEM);

    /* set the filename to the directory of the current file */
    pstrcpy(filename, sizeof(filename), s->b->filename);
    p = strrchr(filename, '/');
    if (p)
        *p = '\0';
    set_filename(b, filename);
    
    width = qs->width / 4;
    e = insert_window_left(b, width, WF_MODELINE);
    do_set_mode(e, &dired_mode, NULL);
    hs = e->mode_data;

    /* if active file is found, go directly on it */
    for(i=0;i<hs->items.nb_items;i++) {
        get_dired_filename(e, filename, sizeof(filename), i);
        if (!strcmp(filename, s->b->filename)) {
            e->offset = eb_goto_pos(e->b, i, 0);
            break;
        }
    }

    /* modify active window */
    qs->active_window = e;
}

/* specific dired commands */
static CmdDef dired_commands[] = {
    CMD0( ' ', KEY_CTRL('t'), "dired-toggle_selection", list_toggle_selection)
    CMD0( KEY_RET, KEY_RIGHT, "dired-select", dired_select)
    CMD0( KEY_LEFT, KEY_NONE, "dired-parent", dired_parent)
    CMD1( KEY_CTRL('g'), KEY_NONE, "delete-window", do_delete_window, 0)
    CMD_DEF_END,
};

static CmdDef dired_global_commands[] = {
    CMD0( KEY_CTRLX(KEY_CTRL('d')), KEY_NONE, "dired", do_dired)
    CMD_DEF_END,
};

static int dired_init(void)
{
    /* inherit from list mode */
    memcpy(&dired_mode, &list_mode, sizeof(ModeDef));
    dired_mode.name = "dired";
    dired_mode.instance_size = sizeof(DiredState);
    dired_mode.display = dired_display;
    dired_mode.mode_probe =  dired_mode_probe;
    dired_mode.mode_init = dired_mode_init;
    dired_mode.mode_close = dired_mode_close;
    
    /* first register mode */
    qe_register_mode(&dired_mode);

    qe_register_cmd_table(dired_commands, "dired");
    qe_register_cmd_table(dired_global_commands, NULL);

    return 0;
}

qe_module_init(dired_init);
