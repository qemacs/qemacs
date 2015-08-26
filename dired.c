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
#include "variables.h"

#include <grp.h>
#include <pwd.h>

enum {
    DIRED_STYLE_NORMAL = QE_STYLE_DEFAULT,
    DIRED_STYLE_HEADER = QE_STYLE_STRING,
    DIRED_STYLE_DIRECTORY = QE_STYLE_COMMENT,
    DIRED_STYLE_FILENAME = QE_STYLE_FUNCTION,
};

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

enum time_format {
    TF_COMPACT,
    TF_DOS,
    TF_DOS_LONG,
    TF_TOUCH,
    TF_TOUCH_LONG,
    TF_FULL,
    TF_SECONDS,
    TF_MAX = TF_SECONDS,
};

static ModeDef dired_mode;

static time_t curtime;

/* XXX: need a function to set these variables and trigger refresh */
static enum time_format dired_time_format;
static int dired_show_dot_files = 1;
#ifdef CONFIG_DARWIN
static int dired_show_ds_store = 0;
#endif

typedef struct DiredState {
    ModeDef *signature;
    StringArray items;
    int sort_mode; /* DIRED_SORT_GROUP | DIRED_SORT_NAME */
    int last_index;
    long long total_bytes;
    int ndirs, nfiles;
    int blocksize;
    int hflag, nflag, last_width;
    int no_blocks, no_mode, no_link, no_uid, no_gid, no_size, no_date;
    int blockslen, modelen, linklen, uidlen, gidlen, sizelen, datelen, namelen;
    int fnamecol;
    char path[MAX_FILENAME_SIZE]; /* current path */
} DiredState;

/* opaque structure for sorting DiredState.items StringArray */
typedef struct DiredItem {
    DiredState *state;
    mode_t  mode;   /* inode protection mode */
    nlink_t nlink;  /* number of hard links to the file */
    uid_t   uid;    /* user-id of owner */
    gid_t   gid;    /* group-id of owner */
    dev_t   rdev;   /* device type, for special file inode */
    time_t  mtime;
    off_t   size;
    int     offset;
    char    mark;
    char    name[1];
} DiredItem;

static DiredState *dired_get_state(EditState *s, int status)
{
    DiredState *ds = s->b->priv_data;

    if (ds && ds->signature == &dired_mode)
        return ds;

    if (status)
        put_status(s, "Not a dired buffer");

    return NULL;
}

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

static char *dired_get_filename(DiredState *ds, int index,
                                char *buf, int buf_size)
{
    const StringItem *item;
    const DiredItem *dip;

    if (buf_size > 0)
        buf[0] = '\0';

    if (index < 0 || index >= ds->items.nb_items)
        return NULL;

    item = ds->items.items[index];
    dip = item->opaque;

    /* build filename */
    /* CG: Should canonicalize path */
    return makepath(buf, buf_size, ds->path, dip->name);
}

static int dired_find_target(DiredState *ds, const char *target)
{
    char filename[MAX_FILENAME_SIZE];
    int i;

    if (target) {
        for (i = 0; i < ds->items.nb_items; i++) {
            if (dired_get_filename(ds, i, filename, sizeof(filename))
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
        is_dir1 = !!S_ISDIR(dip1->mode);
        is_dir2 = !!S_ISDIR(dip2->mode);
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

static int format_number(char *buf, int size, int human, off_t number)
{
    if (human == 0) {
        return snprintf(buf, size, "%lld", (long long)number);
    }
    if (human > 1) {
        const char *suffix = "BkMGTPEZY";

        /* metric version, powers of 1000 */
        while (suffix[1] && number >= 1000) {
            if (number < 10000) {
                buf[0] = '0' + (number / 1000);
                buf[1] = '.';
                buf[2] = '0' + ((number / 100) % 10);
                buf[3] = suffix[1];
                buf[4] = '\0';
                return 4;
            }
            number /= 1000;
            suffix++;
        }
        return snprintf(buf, size, "%d%c", (int)number, *suffix);
    } else {
        const char *suffix = "BKMGTPEZY";

        /* geek version, powers of 1024 */
        while (suffix[1] && number >= 1000) {
            if (number < 10200) {
                buf[0] = '0' + (number / 1020);
                buf[1] = '.';
                buf[2] = '0' + ((number / 102) % 10);
                buf[3] = suffix[1];
                buf[4] = '\0';
                return 4;
            }
            number >>= 10;
            suffix++;
        }
        return snprintf(buf, size, "%d%c", (int)number, *suffix);
    }
}

static int format_gid(char *buf, int size, int nflag, gid_t gid)
{
    // group_from_gid ?
    struct group *grp;

    if (!nflag && (grp = getgrgid(gid)) != NULL && grp->gr_name)
        return snprintf(buf, size, "%s", grp->gr_name);
    else
        return snprintf(buf, size, "%d", (int)gid);
}

static int format_uid(char *buf, int size, int nflag, uid_t uid)
{
    // user_from_uid ?
    struct passwd *pwp;

    if (!nflag && (pwp = getpwuid(uid)) != NULL && pwp->pw_name)
        return snprintf(buf, size, "%s", pwp->pw_name);
    else
        return snprintf(buf, size, "%d", (int)uid);
}

static int format_size(char *buf, int size, int human, const DiredItem *fp)
{
    if (S_ISCHR(fp->mode) || S_ISBLK(fp->mode)) {
        int major = fp->rdev >> ((sizeof(dev_t) == 2) ? 8 : 24);
        int minor = fp->rdev & ((sizeof(dev_t) == 2) ? 0xff : 0xffffff);
        return snprintf(buf, size, "%3d, %3d", major, minor);
    } else {
        return format_number(buf, size, human, fp->size);
    }
}

static int format_date(char *dest, int size,
                       const time_t systime,
                       enum time_format time_format)
{
    buf_t outbuf, *out;
    struct tm systm;
    int fmonth;
    static const char * const month[] = {
        "***",
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };

    /* Should test for valid conversion */
    /* Should support extra precision on enabled systems */
    systm = *localtime(&systime);

    fmonth = systm.tm_mon + 1;
    if (fmonth <= 0 || fmonth > 12)
        fmonth = 0;

    out = buf_init(&outbuf, dest, size);

    switch (time_format) {
    case TF_TOUCH:
    case TF_TOUCH_LONG:
        buf_printf(out, "%02d%02d%02d%02d%02d",
                   systm.tm_year % 100,  /* year */ 
                   fmonth,               /* month */
                   systm.tm_mday,        /* day */
                   systm.tm_hour,        /* hours */
                   systm.tm_min);        /* minutes */
        if (time_format == TF_TOUCH_LONG) {
            buf_printf(out, ".%02d", systm.tm_sec); /* seconds */
        }
        break;
    case TF_DOS:
    case TF_DOS_LONG:
        buf_printf(out, "%s %2d %4d  %2d:%02d",
                   month[fmonth],        /* month */
                   systm.tm_mday,        /* day */
                   systm.tm_year + 1900, /* year */ 
                   systm.tm_hour,        /* hours */
                   systm.tm_min);        /* minutes */
        if (time_format == TF_DOS_LONG) {
            buf_printf(out, ":%02d", systm.tm_sec); /* seconds */
        }
        break;
    case TF_FULL:
        buf_printf(out, "%s %2d %02d:%02d:%02d %4d",
                   month[fmonth],        /* month */
                   systm.tm_mday,        /* day */
                   systm.tm_hour,        /* hours */
                   systm.tm_min,         /* minutes */
                   systm.tm_sec,         /* seconds */
                   systm.tm_year + 1900); /* year */ 
        break;
    case TF_SECONDS:
        buf_printf(out, "%10lu", systime); /* seconds */
        break;
    default:
    case TF_COMPACT:
        if (systime > curtime - 182 * 86400
        &&  systime < curtime + 182 * 86400) {
            buf_printf(out,     "%s %2d %02d:%02d",
                       month[fmonth],        /* month */
                       systm.tm_mday,        /* day */
                       systm.tm_hour,        /* hours */
                       systm.tm_min);        /* minutes */
        } else {
            buf_printf(out,     "%s %2d  %4d",
                       month[fmonth],        /* month */
                       systm.tm_mday,        /* day */
                       systm.tm_year + 1900); /* year */ 
        }
        break;
    }

    if (!fmonth) {
        memset(dest, ' ', out->len);
    }
    return out->pos;
}

static int get_trailchar(mode_t mode)
{
    int trailchar = 0;

    if (mode & S_IEXEC) {
        trailchar = '*';
    }
    if (S_ISDIR(mode)) {
        trailchar = '/';
    }
    if (S_ISLNK(mode)) {
        trailchar = '@';
    }
#ifdef S_ISSOCK
    if (S_ISSOCK(mode))
        trailchar = '=';
#endif
#ifdef S_ISWHT
    if (S_ISWHT(mode))
        trailchar = '%';
#endif
#ifdef S_ISFIFO
    if (S_ISFIFO(mode))
        trailchar = '|';
#endif
    return trailchar;
}

static char *getentryslink(char *path, int size,
                           const char *dir, const char *name)
{
    char filename[MAX_FILENAME_SIZE];
    int len;

    snprintf(filename, sizeof(filename), "%s/%s", dir, name);
    len = readlink(filename, path, size - 1);
    if (len < 0)
        len = 0;
    path[len] = '\0';
    if (len)
        return path;
    else
        return NULL;
}

static char *compute_attr(char *atts, mode_t mode)
{
    strcpy(atts, "----------");

    /* File type */
    if (!S_ISREG(mode)) {
        if (S_ISDIR(mode))  /* directory */
            atts[0] = 'd';
#ifdef S_ISBLK
        if (S_ISBLK(mode))  /* block special */
            atts[0] = 'b';
#endif
#ifdef S_ISCHR
        if (S_ISCHR(mode))  /* char special */
            atts[0] = 'c';
#endif
#ifdef S_ISFIFO
        if (S_ISFIFO(mode))  /* fifo or socket */
            atts[0] = 'p';
#endif
#ifdef S_ISSOCK
        if (S_ISSOCK(mode)  /* socket */)
            atts[0] = 's';
#endif
        if (S_ISLNK(mode))  /* symbolic link */  
            atts[0] = 'l';  /* overrides directory */
    }

    /* File mode */
    /* Read, write, execute/search by owner */
    if (mode & S_IRUSR)  /* [XSI] R for owner */
        atts[1] = 'r';
    if (mode & S_IWUSR)  /* [XSI] W for owner */
        atts[2] = 'w';
    if (mode & S_IXUSR)  /* [XSI] X for owner */
        atts[3] = 'x';
#ifdef S_ISUID
    if (mode & S_ISUID)  /* [XSI] set user id on execution */
        atts[3] = (mode & S_IXUSR) ? 's' : 'S';
#endif
            /* Read, write, execute/search by group */
    if (mode & S_IRGRP)  /* [XSI] R for group */
        atts[4] = 'r';
    if (mode & S_IWGRP)  /* [XSI] W for group */
        atts[5] = 'w';
    if (mode & S_IXGRP)  /* [XSI] X for group */
        atts[6] = 'x';
#ifdef S_ISGID
    if (mode & S_ISGID)  /* [XSI] set group id on execution */
        atts[6] = (mode & S_IXGRP) ? 's' : 'S';
#endif
            /* Read, write, execute/search by others */
    if (mode & S_IROTH)
        atts[7] = 'r';  /* [XSI] R for other */
    if (mode & S_IWOTH)
        atts[8] = 'w';  /* [XSI] W for other */
    if (mode & S_IXOTH)
        atts[9] = 'x';  /* [XSI] X for other */
#ifdef S_ISVTX
    if (mode & S_ISVTX)  /* [XSI] directory restrcted delete */
        atts[6] = (mode & S_IXOTH) ? 't' : 'T';
#endif
    return atts;
}

/* `ds` and `b` are valid, `s` may be NULL */
static void dired_sort_list(DiredState *ds, EditBuffer *b, EditState *s)
{
    char buf[MAX_FILENAME_SIZE];
    StringItem *item, *cur_item;
    DiredItem *dip;
    int index, i, col, width, top_line;

    /* Try and preserve scroll position */
    if (s) {
        eb_get_pos(s->b, &top_line, &col, s->offset_top);
        index = dired_get_index(s);
        width = s->width;
    } else {
        index = col = top_line = 0;
        width = 80;
    }

    cur_item = NULL;
    if (index >= 0 && index < ds->items.nb_items)
        cur_item = ds->items.items[index];

    qsort(ds->items.items, ds->items.nb_items,
          sizeof(StringItem *), dired_sort_func);

    /* construct list buffer */
    /* deleting buffer contents resets s->offset and s->offset_top */
    eb_clear(b);

    if (DIRED_HEADER) {
        b->cur_style = DIRED_STYLE_HEADER;
        eb_printf(b, "  Directory of ");
        b->cur_style = DIRED_STYLE_DIRECTORY;
        eb_printf(b, "%s", ds->path);
        b->cur_style = DIRED_STYLE_HEADER;
        eb_printf(b, "\n    %d director%s, %d file%s, %lld byte%s\n",
                  ds->ndirs, ds->ndirs == 1 ? "y" : "ies",
                  ds->nfiles, &"s"[ds->nfiles == 1],
                  (long long)ds->total_bytes, &"s"[ds->total_bytes == 1]);
    }
    b->cur_style = DIRED_STYLE_NORMAL;

    /* XXX: should use screen specific space_width, separator_width or em_width */
    ds->last_width = width;
    width -= clamp(ds->namelen, 16, 40);
    ds->no_size = ((width -= ds->sizelen + 2) < 0);
    ds->no_date = ((width -= ds->datelen + 2) < 0);
    ds->no_mode = ((width -= ds->modelen + 1) < 0);
    ds->no_uid = ((width -= ds->uidlen + 1) < 0);
    ds->no_gid = ((width -= ds->gidlen + 1) < 0);
    ds->no_link = ((width -= ds->linklen + 1) < 0);
    ds->no_blocks = ((width -= ds->blockslen + 1) < 0);
    ds->no_blocks = 1;  // disable blocks display to avoid confusing output

    for (i = 0; i < ds->items.nb_items; i++) {
        item = ds->items.items[i];
        dip = item->opaque;
        dip->offset = b->total_size;
        if (item == cur_item) {
            if (ds->last_index == index)
                ds->last_index = i;
            if (s)
                s->offset = b->total_size;
        }
        col = eb_printf(b, "%c ", dip->mark);
        if (!ds->no_blocks) {
            col += eb_printf(b, "%*lu ", ds->blockslen,
                             (long)((dip->size + ds->blocksize - 1) /
                                    ds->blocksize));
        }
        if (!ds->no_mode) {
            compute_attr(buf, dip->mode);
            col += eb_printf(b, "%s ", buf);
        }
        if (!ds->no_link) {
            col += eb_printf(b, "%*d ", ds->linklen, (int)dip->nlink);
        }
        if (!ds->no_uid) {
            format_uid(buf, sizeof(buf), ds->nflag, dip->uid);
            col += eb_printf(b, "%-*s ", ds->uidlen, buf);
        }
        if (!ds->no_gid) {
            format_gid(buf, sizeof(buf), ds->nflag, dip->gid);
            col += eb_printf(b, "%-*s ", ds->gidlen, buf);
        }
        if (!ds->no_size) {
            format_size(buf, sizeof(buf), ds->hflag, dip);
            col += eb_printf(b, " %*s  ", ds->sizelen, buf);
        }
        if (!ds->no_date) {
            format_date(buf, sizeof(buf), dip->mtime, dired_time_format);
            col += eb_printf(b, "%s  ", buf);
        }
        ds->fnamecol = col - 1;

        if (S_ISDIR(dip->mode))
            b->cur_style = DIRED_STYLE_DIRECTORY;
        else
            b->cur_style = DIRED_STYLE_FILENAME;

        eb_printf(b, "%s", dip->name);

        if (1) {
            int trailchar = get_trailchar(dip->mode);
            if (trailchar) {
                eb_printf(b, "%c", trailchar);
            }
        }
        if (S_ISLNK(dip->mode)
        &&  getentryslink(buf, sizeof(buf), ds->path, dip->name)) {
            eb_printf(b, " -> %s", buf);
        }
        b->cur_style = DIRED_STYLE_NORMAL;
        eb_printf(b, "\n");
    }
    b->modified = 0;
    b->flags |= BF_READONLY;
    if (s) {
        s->offset_top = eb_goto_pos(b, top_line, 0);
    }
}

/* dired-mode commands */

static void dired_up_down(EditState *s, int dir)
{
    DiredState *ds;
    int line, col;

    if (!(ds = dired_get_state(s, 1)))
        return;

    if (dir) {
        text_move_up_down(s, dir);
    }
    if (s->offset && s->offset == s->b->total_size)
        text_move_up_down(s, -1);

    eb_get_pos(s->b, &line, &col, s->offset);
    s->offset = eb_goto_pos(s->b, line, ds->fnamecol);
}

static void dired_mark(EditState *s, int mark)
{
    DiredState *ds;
    const StringItem *item;
    DiredItem *dip;
    int ch, index, dir = 1, flags;

    if (!(ds = dired_get_state(s, 1)))
        return;

    if (mark < 0) {
        dir = -1;
        mark = ' ';
    }
    if (dir < 0)
        dired_up_down(s, -1);

    index = dired_get_index(s);
    if (index < 0 || index >= ds->items.nb_items)
        return;
    item = ds->items.items[index];
    dip = item->opaque;

    ch = dip->mark = mark;
    do_bol(s);
    flags = s->b->flags & BF_READONLY;
    s->b->flags ^= flags;
    eb_delete_uchar(s->b, s->offset);
    eb_insert_uchar(s->b, s->offset, ch);
    s->b->flags ^= flags;

    if (dir > 0)
        dired_up_down(s, 1);
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
    dired_sort_list(ds, s->b, s);
}

static void dired_set_time_format(EditState *s, int format)
{
    char buf[32];
    DiredState *ds;
    int i, len;

    if (!(ds = dired_get_state(s, 1)))
        return;

    if (format < 0 || format > TF_MAX)
        return;

    dired_time_format = format;

    ds->datelen = 0;
    for (i = 0; i < ds->items.nb_items; i++) {
        const StringItem *item = ds->items.items[i];
        const DiredItem *dip = item->opaque;

        len = format_date(buf, sizeof(buf), dip->mtime, format);
        if (ds->datelen < len)
            ds->datelen = len;
    }
    dired_sort_list(ds, s->b, s);
}

/* `ds` and `b` are valid, `s` and `target` may be NULL */
static void dired_build_list(DiredState *ds, const char *path,
                             const char *target, EditBuffer *b, EditState *s)
{
    FindFileState *ffst;
    char filename[MAX_FILENAME_SIZE];
    char line[1024], buf[1024];
    const char *p;
    struct stat st;
    int len, index;
    StringItem *item;

    /* free previous list, if any */
    dired_free(ds);

    curtime = time(NULL);
    ds->blocksize = 1024;
    ds->ndirs = ds->nfiles = 0;
    ds->total_bytes = 0;
    ds->last_width = 0;
    ds->blockslen = ds->modelen = ds->linklen = 0;
    ds->uidlen = ds->gidlen = 0;
    ds->sizelen = ds->datelen = ds->namelen = 0;

    /* CG: should make absolute ? */
    canonicalize_path(ds->path, sizeof(ds->path), path);
    eb_set_filename(b, ds->path);
    b->flags |= BF_DIRED;

    eb_clear(b);

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
        if (!dired_show_dot_files && *p == '.')
            continue;

#ifdef CONFIG_DARWIN
        if (!dired_show_ds_store && strequal(p, ".DS_Store"))
            continue;
#endif
        pstrcpy(line, sizeof(line), p);
        if (S_ISDIR(st.st_mode)) {
            ds->ndirs++;
        } else {
            ds->nfiles++;
            ds->total_bytes += st.st_size;
        }
        item = add_string(&ds->items, line, 0);
        if (item) {
            DiredItem *dip;
            int plen = strlen(p);

            dip = qe_malloc_hack(DiredItem, plen);
            dip->state = ds;
            dip->mode = st.st_mode;
            dip->nlink = st.st_nlink;
            dip->uid = st.st_uid;
            dip->gid = st.st_gid;
            dip->rdev = st.st_rdev;
            dip->mtime = st.st_mtime;
            dip->size = st.st_size;
            dip->mark = ' ';
            memcpy(dip->name, p, plen + 1);
            item->opaque = dip;

            if (ds->namelen < plen)
                ds->namelen = plen;

            len = snprintf(buf, sizeof(buf), "%lu",
                           ((unsigned long)st.st_size + ds->blocksize - 1) /
                           ds->blocksize);
            if (ds->blockslen < len)
                ds->blockslen = len;

            ds->modelen = 10;

            len = snprintf(buf, sizeof(buf), "%d", (int)st.st_nlink);
            if (ds->linklen < len)
                ds->linklen = len;

            len = format_uid(buf, sizeof(buf), ds->nflag, st.st_uid);
            if (ds->uidlen < len)
                ds->uidlen = len;

            len = format_gid(buf, sizeof(buf), ds->nflag, st.st_gid);
            if (ds->gidlen < len)
                ds->gidlen = len;

            len = format_size(buf, sizeof(buf), ds->hflag, dip);
            if (ds->sizelen < len)
                ds->sizelen = len;

            len = format_date(buf, sizeof(buf), dip->mtime, dired_time_format);
            if (ds->datelen < len)
                ds->datelen = len;
        }
    }
    find_file_close(&ffst);
    
    dired_sort_list(ds, b, s);
    if (s) {
        index = dired_find_target(ds, target);
        s->offset = eb_goto_pos(b, max(index, 0) + DIRED_HEADER, ds->fnamecol);
    }
}

/* select current item */
static void dired_select(EditState *s)
{
    DiredState *ds;
    struct stat st;
    char filename[MAX_FILENAME_SIZE];
    EditState *e;

    if (!(ds = dired_get_state(s, 1)))
        return;

    if (!dired_get_filename(ds, dired_get_index(s), filename, sizeof(filename)))
        return;

    /* Check if path leads somewhere */
    if (stat(filename, &st) < 0)
        return;

    if (S_ISDIR(st.st_mode)) {
        /* DO descend into directories pointed to by symlinks */
        /* XXX: should expand directory below current position */
        dired_build_list(ds, filename, NULL, s->b, s);
    } else
    if (S_ISREG(st.st_mode)) {
        /* do explore files pointed to by symlinks */
        e = find_window(s, KEY_RIGHT, NULL);
        if (e) {
#if 1
            s->qe_state->active_window = e;
#else
            /* delete dired window */
            do_delete_window(s, 1);
            /* XXX: should keep BF_PREVIEW flag and set pager-mode */
            e->b->flags &= ~BF_PREVIEW;
#endif
        } else {
            do_find_file(s, filename, 0);
        }
    }
}

static void dired_view_file(EditState *s, const char *filename)
{
    EditBuffer *b;
    EditState *e;
    int rc;

    e = find_window(s, KEY_RIGHT, NULL);
    if (!e)
        return;

    /* close previous temporary buffers, if any */
    b = e->b;
    if (b && (b->flags & BF_PREVIEW) && !b->modified) {
        /* will free the buffer if no longer viewed */
        b->flags |= BF_TRANSIENT;
    }

    /* Load file and attach to window. If file not loaded already, mark
     * new buffer as BF_PREVIEW, to trigger paging mode and so that it
     * will get freed if closed.
     */
    rc = qe_load_file(e, filename, 0, 0, BF_PREVIEW);
    if (rc >= 0) {
        /* disable wrapping to get nicer display */
        /* XXX: should wrap lines unless window is narrow */
        //e->wrap = WRAP_TRUNCATE;
    }
    if (rc < 0) {
        /* if file failed to load, show a scratch buffer */
        switch_to_buffer(e, eb_new("*scratch*", BF_SAVELOG | BF_UTF8 | BF_PREVIEW));
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

    if (s->b->flags & BF_PREVIEW) {
        EditState *e = find_window(s, KEY_LEFT, NULL);
        if (e && (e->b->flags & BF_DIRED)) {
            s->qe_state->active_window = e;
            return;
        }        
    }

    if (!(ds = dired_get_state(s, 1)))
        return;

    pstrcpy(target, sizeof(target), ds->path);
    makepath(filename, sizeof(filename), ds->path, "..");

    dired_build_list(ds, filename, target, s->b, s);
}

static void dired_refresh(EditState *s)
{
    DiredState *ds;
    char target[MAX_FILENAME_SIZE];
    char dirname[MAX_FILENAME_SIZE];

    if (!(ds = dired_get_state(s, 1)))
        return;

    dired_get_filename(ds, dired_get_index(s), target, sizeof(target));
    pstrcpy(dirname, sizeof(dirname), ds->path);
    dired_build_list(ds, dirname, target, s->b, s);
}

static void dired_toggle_dot_files(EditState *s, int val)
{
    if (val == -1)
        val = !dired_show_dot_files;

    if (dired_show_dot_files != val) {
        dired_show_dot_files = val;
        dired_refresh(s);
        put_status(s, "dot files are %s", val ? "visible" : "hidden");
    }
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
        dired_up_down(s, -1);

    if (s->x1 == 0) {
        if (s->y1 == 0 && ds->last_width != s->width) {
            /* rebuild buffer contents according to new window width */
            /* XXX: this may cause problems if buffer is displayed in
             * multiple windows, hence the test on s->y1.
             * Should test for current window */
            dired_sort_list(ds, s->b, s);
        }
        /* open file so that user can see it before it is selected */
        /* XXX: find a better solution (callback) */
        index = dired_get_index(s);
        if (index < 0 || index >= ds->items.nb_items)
            return;
        /* Should not rely on last_index! */
        if (index != ds->last_index) {
            ds->last_index = index;
            if (dired_get_filename(ds, index, filename, sizeof(filename))) {
                dired_view_file(s, filename);
            }
        }
    }
}

static void dired_close(EditBuffer *b)
{
    DiredState *ds = b->priv_data;

    if (ds && ds->signature == &dired_mode) {
        dired_free(ds);
    }

    qe_free(&b->priv_data);
    if (b->close == dired_close)
        b->close = NULL;
}

static int dired_mode_init(EditState *s, EditBuffer *b, int flags)
{
    DiredState *ds;

    list_mode.mode_init(s, b, flags);

    if (s) {
        if (s->b->priv_data) {
            ds = s->b->priv_data;
            if (ds->signature != &dired_mode)
                return -1;
        } else {
            /* XXX: should be allocated by buffer_load API */
            ds = qe_mallocz(DiredState);
            if (!ds)
                return -1;

            ds->signature = &dired_mode;
            /* XXX: Should use last sort mode, a global variable */
            ds->sort_mode = DIRED_SORT_GROUP | DIRED_SORT_NAME;
            ds->last_index = -1;

            s->b->priv_data = ds;
            s->b->close = dired_close;

            eb_create_style_buffer(b, BF_STYLE1);
            /* XXX: should be built by buffer_load API */
            dired_build_list(ds, s->b->filename, NULL, s->b, s);
            /* XXX: File system charset should be detected automatically */
            /* XXX: If file system charset is not utf8, eb_printf will fail */
            eb_set_charset(s->b, &charset_utf8, s->b->eol_type);
        }
    }
    return 0;
}

/* can only apply dired mode on directories */
static int dired_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (p->b->priv_data) {
        DiredState *ds = p->b->priv_data;
        if (ds->signature != &dired_mode)
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

/* open dired window on the left. The directory of the current file is
   used */
void do_dired(EditState *s)
{
    QEmacsState *qs = s->qe_state;
    EditBuffer *b;
    EditState *e;
    DiredState *ds;
    int width, index;
    char filename[MAX_FILENAME_SIZE], *p;
    char target[MAX_FILENAME_SIZE];

    /* Should take directory argument with optional switches,
     * find dired window if exists,
     * else create one and do this.
     * recursive listing and multi directory patterns.
     */

    /* Should reuse previous dired buffer for same filespec */
    b = eb_scratch("*dired*", BF_READONLY | BF_UTF8);

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
    /* set dired mode: dired_mode_init() will load buffer content */
    edit_set_mode(e, &dired_mode);

    ds = dired_get_state(e, 0);
    if (ds) {
        index = dired_find_target(ds, target);
        e->offset = eb_goto_pos(e->b, max(index, 0) + DIRED_HEADER,
                                ds->fnamecol);
    }
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
    /* XXX: merge with other dired-next-line */
    CMD1( ' ', KEY_DOWN,
          "dired-next-line", dired_up_down, 1)
    CMD1( KEY_DEL, KEY_NONE,
          "dired-unmark-backward", dired_mark, -1)
    CMD2( 's', KEY_NONE,
          "dired-sort", dired_sort, ESs,
          "s{Sort order: }|sortkey|")
    CMD2( 't', KEY_NONE,
          "dired-set-time-format", dired_set_time_format, ESi,
          "i{Time format: }[timeformat]")
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
    CMD1( 'n', KEY_CTRL('n'),
          "dired-next-line", dired_up_down, 1)
    CMD1( 'p', KEY_CTRL('p'), /* KEY_UP */
          "dired-previous-line", dired_up_down, -1)
    CMD0( 'r', KEY_NONE,
          "dired-refresh", dired_refresh)
    CMD1( '.', KEY_NONE,
          "dired-toggle-dot-files", dired_toggle_dot_files, -1)
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

static VarDef dired_variables[] = {
    G_VAR( "dired-time-format", dired_time_format, VAR_NUMBER, VAR_RW_SAVE )
    G_VAR( "dired-show-dot-files", dired_show_dot_files, VAR_NUMBER, VAR_RW_SAVE )
#ifdef CONFIG_DARWIN
    G_VAR( "dired-show-ds-store", dired_show_ds_store, VAR_NUMBER, VAR_RW_SAVE )
#endif
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
    qe_register_mode(&dired_mode, /* MODEF_DATATYPE | */ MODEF_MAJOR | MODEF_VIEW);

    qe_register_variables(dired_variables, countof(dired_variables));
    qe_register_cmd_table(dired_commands, &dired_mode);
    qe_register_cmd_table(dired_global_commands, NULL);

    return 0;
}

qe_module_init(dired_init);
