/*
 * Directory editor mode for QEmacs.
 *
 * Copyright (c) 2001-2002 Fabrice Bellard.
 * Copyright (c) 2002-2017 Charlie Gordon.
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

enum {
    DIRED_UPDATE_SORT = 1,
    DIRED_UPDATE_FILTER = 2,
    DIRED_UPDATE_COLUMNS = 4,
    DIRED_UPDATE_REBUILD = 8,
    DIRED_UPDATE_ALL = 15,
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

typedef struct DiredState DiredState;
typedef struct DiredItem DiredItem;

struct DiredState {
    QEModeData base;    /* derived from QEModeData */
    StringArray items;  /* XXX: should just use a generic array */
    enum time_format time_format;
    int show_dot_files;
    int show_ds_store;
    int hflag, nflag;
    int sort_mode;
    DiredItem *last_cur;
    long long total_bytes;
    int ndirs, nfiles, ndirs_hidden, nfiles_hidden;
    int blocksize;
    int last_width;
    int no_blocks, no_mode, no_link, no_uid, no_gid, no_size, no_date;
    int blockslen, modelen, linklen, uidlen, gidlen, sizelen, datelen, namelen;
    int fnamecol;
    char path[MAX_FILENAME_SIZE]; /* current path */
};

/* opaque structure for sorting DiredState.items StringArray */
struct DiredItem {
    mode_t  mode;   /* inode protection mode */
    nlink_t nlink;  /* number of hard links to the file */
    uid_t   uid;    /* user-id of owner */
    gid_t   gid;    /* group-id of owner */
    dev_t   rdev;   /* device type, for special file inode */
    time_t  mtime;
    off_t   size;
    int     offset;
    char    hidden;
    char    mark;
    char    name[1];
};

static ModeDef dired_mode;

static time_t dired_curtime;

static enum time_format dired_time_format;
static int dired_show_dot_files = 1;
static int dired_show_ds_store = 0;
static int dired_nflag = 0; /* 0=name, 1=numeric, 2=hidden */
static int dired_hflag = 0; /* 0=exact, 1=human-decimal, 2=human-binary */
static int dired_sort_mode = DIRED_SORT_GROUP | DIRED_SORT_NAME;

static QVarType dired_sort_mode_set_value(EditState *s, VarDef *vp,
    void *ptr, const char *str, int sort_mode);
static QVarType dired_time_format_set_value(EditState *s, VarDef *vp,
    void *ptr, const char *str, int format);

static VarDef dired_variables[] = {
    G_VAR_F( "dired-sort-mode", dired_sort_mode, VAR_NUMBER, VAR_RW_SAVE,
            dired_sort_mode_set_value )
    G_VAR_F( "dired-time-format", dired_time_format, VAR_NUMBER, VAR_RW_SAVE,
            dired_time_format_set_value )
    G_VAR( "dired-show-dot-files", dired_show_dot_files, VAR_NUMBER, VAR_RW_SAVE )
    G_VAR( "dired-show-ds-store", dired_show_ds_store, VAR_NUMBER, VAR_RW_SAVE )
};

static inline DiredState *dired_get_state(EditState *e, int status)
{
    return qe_get_buffer_mode_data(e->b, &dired_mode, status ? e : NULL);
}

static DiredItem *dired_get_cur_item(DiredState *ds, EditState *s) {
    int i, index = list_get_pos(s) - DIRED_HEADER;

    if (index >= 0) {
        for (i = 0; i < ds->items.nb_items; i++) {
            DiredItem *dip = ds->items.items[i]->opaque;
            if (!dip->hidden && index-- == 0)
                return dip;
        }
    }
    return NULL;
}

static void dired_free(DiredState *ds)
{
    if (ds) {
        int i;

        for (i = 0; i < ds->items.nb_items; i++) {
            qe_free(&ds->items.items[i]->opaque);
        }

        free_strings(&ds->items);

        ds->last_cur = NULL;
    }
}

static char *dired_get_filename(DiredState *ds, const DiredItem *dip,
                                char *buf, int buf_size)
{
    if (buf_size > 0)
        buf[0] = '\0';

    if (!dip)
        return NULL;

    /* build filename */
    /* CG: Should canonicalize path */
    if (is_directory(ds->path)) {
        return makepath(buf, buf_size, ds->path, dip->name);
    } else {
        get_dirname(buf, buf_size, ds->path);
        append_slash(buf, buf_size);
        return pstrcat(buf, buf_size, dip->name);
    }
}

static int dired_find_target(DiredState *ds, const char *target)
{
    char filename[MAX_FILENAME_SIZE];
    int i, row;

    if (target) {
        row = DIRED_HEADER;
        for (i = 0; i < ds->items.nb_items; i++) {
            DiredItem *dip = ds->items.items[i]->opaque;
    
            if (dired_get_filename(ds, dip, filename, sizeof(filename))
            &&  strequal(filename, target)) {
                return row;
            }
            if (!dip->hidden)
                row++;
        }
    }
    return DIRED_HEADER;
}

/* sort alphabetically with directories first */
static int dired_sort_func(void *opaque, const void *p1, const void *p2)
{
    const StringItem *item1 = *(const StringItem **)p1;
    const StringItem *item2 = *(const StringItem **)p2;
    const DiredItem *dip1 = item1->opaque;
    const DiredItem *dip2 = item2->opaque;
    DiredState *ds = opaque;
    int sort_mode = ds->sort_mode, res;
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
        if (systime > dired_curtime - 182 * 86400
        &&  systime < dired_curtime + 182 * 86400) {
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

static void dired_filter_files(DiredState *ds)
{
    int i;

    ds->show_dot_files = dired_show_dot_files;
    ds->show_ds_store = dired_show_ds_store;
    ds->total_bytes = 0;
    ds->ndirs = ds->nfiles = 0;
    ds->ndirs_hidden = ds->nfiles_hidden = 0;

    for (i = 0; i < ds->items.nb_items; i++) {
        DiredItem *dip = ds->items.items[i]->opaque;
        const char *p = ds->items.items[i]->str;
        int hidden = 0;

        if (*p == '.') {
            if ((!dired_show_dot_files)
            ||  (!dired_show_ds_store && strequal(p, ".DS_Store")))
                hidden = 1;
        }
        /* XXX: should apply other filters? */
        dip->hidden = hidden;
        if (hidden) {
            if (S_ISDIR(dip->mode)) {
                ds->ndirs_hidden++;
            } else {
                ds->nfiles_hidden++;
            }
        } else {
            if (S_ISDIR(dip->mode)) {
                ds->ndirs++;
            } else {
                ds->nfiles++;
                ds->total_bytes += dip->size;
            }
        }
    }
}

static void dired_compute_columns(DiredState *ds)
{
    char buf[32];
    int i, len;

    dired_curtime = time(NULL);
    ds->time_format = dired_time_format;
    ds->hflag = dired_hflag;
    ds->nflag = dired_nflag;
    ds->blockslen = ds->modelen = ds->linklen = 0;
    ds->uidlen = ds->gidlen = 0;
    ds->sizelen = ds->datelen = ds->namelen = 0;

    for (i = 0; i < ds->items.nb_items; i++) {
        DiredItem *dip = ds->items.items[i]->opaque;
    
        len = strlen(dip->name);
        if (ds->namelen < len)
            ds->namelen = len;

        len = snprintf(buf, sizeof(buf), "%ld",
                       (long)(((long long)dip->size + ds->blocksize - 1) /
                              ds->blocksize));
        if (ds->blockslen < len)
            ds->blockslen = len;

        ds->modelen = 10;

        len = snprintf(buf, sizeof(buf), "%d", (int)dip->nlink);
        if (ds->linklen < len)
            ds->linklen = len;

        len = format_uid(buf, sizeof(buf), ds->nflag, dip->uid);
        if (ds->uidlen < len)
            ds->uidlen = len;

        len = format_gid(buf, sizeof(buf), ds->nflag, dip->gid);
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

#define inflect(n, singular, plural)  ((n) == 1 ? (singular) : (plural))

/* `b` is valid, `ds` and `s` may be NULL */
static void dired_update_buffer(DiredState *ds, EditBuffer *b, EditState *s,
                                int flags)
{
    char buf[MAX_FILENAME_SIZE];
    DiredItem *dip, *cur_item;
    int i, col, w, width, window_width, top_line;

    if (!ds)
        return;

    /* Try and preserve scroll position */
    if (s) {
        w = max(1, get_glyph_width(s->screen, s, QE_STYLE_DEFAULT, '0'));
        window_width = s->width;
        width = window_width / w;

        eb_get_pos(s->b, &top_line, &col, s->offset_top);
        /* XXX: should use dip->offset and delay to rebuild phase */
        cur_item = dired_get_cur_item(ds, s);
    } else {
        width = window_width = 80;
        col = top_line = 0;
        cur_item = NULL;
    }

    if (ds->sort_mode != dired_sort_mode)
        flags |= DIRED_UPDATE_SORT;

    if (flags & DIRED_UPDATE_SORT) {
        flags |= DIRED_UPDATE_REBUILD;
        ds->sort_mode = dired_sort_mode;
        qe_qsort_r(ds->items.items, ds->items.nb_items,
                   sizeof(StringItem *), ds, dired_sort_func);
    }

    if (ds->show_dot_files != dired_show_dot_files
    ||  ds->show_ds_store != dired_show_ds_store) {
        flags |= DIRED_UPDATE_FILTER;
    }

    if (flags & DIRED_UPDATE_FILTER) {
        flags |= DIRED_UPDATE_REBUILD;
        dired_filter_files(ds);
    }

    if (ds->time_format != dired_time_format
    ||  ds->nflag != dired_nflag
    ||  ds->hflag != dired_hflag) {
        flags |= DIRED_UPDATE_COLUMNS;
    }

    if (flags & DIRED_UPDATE_COLUMNS) {
        flags |= DIRED_UPDATE_REBUILD;
        dired_compute_columns(ds);
    }

    if (!(flags & DIRED_UPDATE_REBUILD))
        return;

    ds->last_width = window_width;
    ds->last_cur = NULL;
    width -= clamp(ds->namelen, 16, 40);
    ds->no_size = ((width -= ds->sizelen + 2) < 0);
    ds->no_date = ((width -= ds->datelen + 2) < 0);
    ds->no_mode = ((width -= ds->modelen + 1) < 0);
    ds->no_uid = (ds->nflag == 2) || ((width -= ds->uidlen + 1) < 0);
    ds->no_gid = (ds->nflag == 2) || ((width -= ds->gidlen + 1) < 0);
    ds->no_link = ((width -= ds->linklen + 1) < 0);
    ds->no_blocks = ((width -= ds->blockslen + 1) < 0);
    ds->no_blocks = 1;  // disable blocks display to avoid confusing output

    /* construct list buffer */
    /* deleting buffer contents resets s->offset and s->offset_top */
    eb_clear(b);

    if (DIRED_HEADER) {
        int seq = ' ';
        b->cur_style = DIRED_STYLE_HEADER;
        eb_printf(b, "  Directory of ");
        b->cur_style = DIRED_STYLE_DIRECTORY;
        eb_printf(b, "%s", ds->path);
        b->cur_style = DIRED_STYLE_HEADER;
        eb_printf(b, "\n  ");
        if (ds->ndirs) {
            eb_printf(b, "%c %d %s", seq, ds->ndirs,
                      inflect(ds->ndirs, "directory", "directories"));
            seq = ',';
        }
        if (ds->ndirs_hidden) {
            eb_printf(b, "%c %d %s", seq, ds->ndirs_hidden,
                      inflect(ds->ndirs_hidden, "hidden directory", "hidden directories"));
            seq = ',';
        }
        if (ds->nfiles) {
            eb_printf(b, "%c %d %s", seq, ds->nfiles,
                      inflect(ds->nfiles, "file", "files"));
            seq = ',';
        }
        if (ds->nfiles_hidden) {
            eb_printf(b, "%c %d %s", seq, ds->nfiles_hidden,
                      inflect(ds->nfiles_hidden, "hidden file", "hidden files"));
            seq = ',';
        }
        if (ds->total_bytes) {
            format_number(buf, sizeof(buf), ds->hflag, ds->total_bytes);
            eb_printf(b, "%c %s %s", seq, buf,
                      inflect(ds->total_bytes, "byte", "bytes"));
            seq = ',';
        }
        if (ds->ndirs + ds->ndirs_hidden + ds->nfiles + ds->nfiles_hidden == 0) {
            eb_printf(b, "%c empty", seq);
        }
        eb_printf(b, "\n");
    }
    b->cur_style = DIRED_STYLE_NORMAL;

    for (i = 0; i < ds->items.nb_items; i++) {
        dip = ds->items.items[i]->opaque;
        dip->offset = b->total_size;
        if (dip == cur_item) {
            ds->last_cur = dip;
            if (s)
                s->offset = b->total_size;
        }
        if (dip->hidden)
            continue;
        col = eb_printf(b, "%c ", dip->mark);
        if (!ds->no_blocks) {
            col += eb_printf(b, "%*ld ", ds->blockslen,
                             (long)(((long long)dip->size + ds->blocksize - 1) /
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
    DiredItem *dip;
    int ch, dir = 1, flags;

    if (!(ds = dired_get_state(s, 1)))
        return;

    if (mark < 0) {
        dir = -1;
        mark = ' ';
    }
    if (dir < 0)
        dired_up_down(s, -1);

    dip = dired_get_cur_item(ds, s);
    if (dip) {
        ch = dip->mark = mark;
        do_bol(s);
        flags = s->b->flags & BF_READONLY;
        s->b->flags ^= flags;
        eb_replace_uchar(s->b, s->offset, ch);
        s->b->flags ^= flags;
    }
    if (dir > 0)
        dired_up_down(s, 1);
}

static QVarType dired_sort_mode_set_value(EditState *s, VarDef *vp,
    void *ptr, const char *str, int sort_mode)
{
    const char *p;

    for (p = str; p && *p; p++) {
        switch (qe_tolower((unsigned char)*p)) {
        case 'n':       /* name */
            sort_mode &= ~DIRED_SORT_MASK;
            sort_mode |= DIRED_SORT_NAME;
            break;
        case 'e':       /* extension */
            sort_mode &= ~DIRED_SORT_MASK;
            sort_mode |= DIRED_SORT_EXTENSION;
            break;
        case 's':       /* size */
            sort_mode &= ~DIRED_SORT_MASK;
            sort_mode |= DIRED_SORT_SIZE;
            break;
        case 'd':       /* date */
            sort_mode &= ~DIRED_SORT_MASK;
            sort_mode |= DIRED_SORT_DATE;
            break;
        case 'g':       /* group */
            sort_mode |= DIRED_SORT_GROUP;
            break;
        case 'u':       /* ungroup */
            sort_mode &= ~DIRED_SORT_GROUP;
            break;
        case 'r':       /* reverse */
            sort_mode ^= DIRED_SORT_DESCENDING;
            break;
        case '+':       /* ascending */
            sort_mode &= ~DIRED_SORT_DESCENDING;
            break;
        case '-':       /* descending */
            sort_mode |= DIRED_SORT_DESCENDING;
            break;
        }
    }
    if (dired_sort_mode != sort_mode) {
        dired_sort_mode = sort_mode;
        vp->modified = 1;
    }
    return VAR_NUMBER;
}

static void dired_sort(EditState *s, const char *sort_order)
{
    int sort_mode = dired_sort_mode;

    dired_sort_mode_set_value(s, &dired_variables[0], &dired_sort_mode,
                              sort_order, sort_mode);

    if (sort_mode != dired_sort_mode)
        dired_update_buffer(dired_get_state(s, 0), s->b, s, DIRED_UPDATE_SORT);
}

static QVarType dired_time_format_set_value(EditState *s, VarDef *vp,
    void *ptr, const char *str, int format)
{
    if (str) {
        if (!strxcmp(str, "default"))    format = TF_COMPACT;    else
        if (!strxcmp(str, "compact"))    format = TF_COMPACT;    else
        if (!strxcmp(str, "dos"))        format = TF_DOS;        else
        if (!strxcmp(str, "dos-long"))   format = TF_DOS_LONG;   else
        if (!strxcmp(str, "touch"))      format = TF_TOUCH;      else
        if (!strxcmp(str, "touch-long")) format = TF_TOUCH_LONG; else
        if (!strxcmp(str, "full"))       format = TF_FULL;       else
        if (!strxcmp(str, "seconds"))    format = TF_SECONDS;    else
            return VAR_UNKNOWN;
    }
    if (format < 0 || format > TF_MAX)
       return VAR_UNKNOWN;

    if (dired_time_format != (enum time_format)format) {
        dired_time_format = format;
        vp->modified = 1;
    }
    return VAR_NUMBER;
}

static void dired_set_time_format(EditState *s, int format)
{
    dired_time_format_set_value(s, &dired_variables[1], &dired_time_format,
                                NULL, format);
}

/* `ds` and `b` are valid, `s` and `target` may be NULL */
static void dired_build_list(DiredState *ds, const char *path,
                             const char *target, EditBuffer *b, EditState *s)
{
    FindFileState *ffst;
    char filename[MAX_FILENAME_SIZE];
    char dir[MAX_FILENAME_SIZE];
    char line[1024];
    const char *p;
    const char *pattern;
    struct stat st;
    StringItem *item;

    /* free previous list, if any */
    dired_free(ds);

    ds->blocksize = 1024;
    ds->last_width = 0;

    /* CG: should make absolute ? */
    canonicalize_path(ds->path, sizeof(ds->path), path);
    eb_set_filename(b, ds->path);
    b->flags |= BF_DIRED;

    eb_clear(b);

    pstrcpy(dir, sizeof(dir), ds->path);
    pattern = "*";

    if (!is_directory(dir)) {
        get_dirname(dir, sizeof(dir), ds->path);
        pattern = get_basename(ds->path);
    }

    /* XXX: should scan directory for subdirectories and filter with
     * pattern only for regular files.
     * XXX: should handle generalized file patterns.
     * XXX: should use a separate thread to make the scan asynchronous.
     * XXX: should compute recursive size data.
     * XXX: should track file creation, deletion and modifications.
     */
    ffst = find_file_open(dir, pattern);
    /* Should scan directory/filespec before computing lines to adjust
     * filename gutter width.
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
        item = add_string(&ds->items, line, 0);
        if (item) {
            DiredItem *dip;
            int plen = strlen(p);

            dip = qe_malloc_hack(DiredItem, plen);
            dip->mode = st.st_mode;
            dip->nlink = st.st_nlink;
            dip->uid = st.st_uid;
            dip->gid = st.st_gid;
            dip->rdev = st.st_rdev;
            dip->mtime = st.st_mtime;
            dip->size = st.st_size;
            dip->hidden = 0;
            dip->mark = ' ';
            memcpy(dip->name, p, plen + 1);
            item->opaque = dip;
        }
    }
    find_file_close(&ffst);

    dired_update_buffer(ds, b, s, DIRED_UPDATE_ALL);
    if (s) {
        s->offset = eb_goto_pos(b, dired_find_target(ds, target), ds->fnamecol);
    }
}

/* select current item */
static void dired_select(EditState *s)
{
    char filename[MAX_FILENAME_SIZE];
    DiredState *ds;
    struct stat st;
    EditState *e;

    if (!(ds = dired_get_state(s, 1)))
        return;

    if (!dired_get_filename(ds, dired_get_cur_item(ds, s),
                            filename, sizeof(filename))) {
        return;
    }

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

static EditState *dired_view_file(EditState *s, const char *filename)
{
    EditBuffer *b;
    EditState *e;
    int rc;

    e = find_window(s, KEY_RIGHT, NULL);
    if (!e)
        return NULL;

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
    rc = qe_load_file(e, filename, LF_NOWILDCARD, BF_PREVIEW);
    if (rc >= 0) {
        return e;
    } else {
        /* if file failed to load, show a scratch buffer */
        b = eb_new("*scratch*", BF_SAVELOG | BF_UTF8 | BF_PREVIEW);
        eb_printf(b, "Cannot load file %s", filename);
        switch_to_buffer(e, b);
        return NULL;
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
        if (e && (e->flags & WF_FILELIST)) {
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

static void dired_toggle_human(EditState *s)
{
    dired_hflag = (dired_hflag + 1) % 3;
}

static void dired_toggle_nflag(EditState *s)
{
    dired_nflag = (dired_nflag + 1) % 3;
}

static void dired_refresh(EditState *s)
{
    DiredState *ds;
    char target[MAX_FILENAME_SIZE];
    char dirname[MAX_FILENAME_SIZE];

    if (!(ds = dired_get_state(s, 1)))
        return;

    dired_get_filename(ds, dired_get_cur_item(ds, s),
                       target, sizeof(target));
    pstrcpy(dirname, sizeof(dirname), ds->path);
    dired_build_list(ds, dirname, target, s->b, s);
}

static void dired_toggle_dot_files(EditState *s, int val)
{
    if (val == -1)
        val = !dired_show_dot_files;

    if (dired_show_dot_files != val) {
        dired_show_dot_files = val;
        dired_update_buffer(dired_get_state(s, 0), s->b, s, DIRED_UPDATE_FILTER);
        put_status(s, "dot files are %s", val ? "visible" : "hidden");
    }
}

static void dired_display_hook(EditState *s)
{
    char filename[MAX_FILENAME_SIZE];
    DiredState *ds;
    DiredItem *dip;
    int flags = 0;

    if (!(ds = dired_get_state(s, 0)))
        return;

    /* Prevent point from going beyond list */
    if (s->offset && s->offset == s->b->total_size)
        dired_up_down(s, -1);

    if (s->x1 == 0 && s->y1 == 0 && ds->last_width != s->width) {
        /* rebuild buffer contents according to new window width */
        /* XXX: this may cause problems if buffer is displayed in
         * multiple windows, hence the test on s->y1.
         * Should test for current window */
        flags |= DIRED_UPDATE_REBUILD;
    }

    dired_update_buffer(ds, s->b, s, flags);

    if (s->x1 == 0) {
        /* open file so that user can see it before it is selected */
        /* XXX: find a better solution (callback) */
        dip = dired_get_cur_item(ds, s);
        if (dip != ds->last_cur) {
            ds->last_cur = dip;
            if (dired_get_filename(ds, dip, filename, sizeof(filename))) {
                dired_view_file(s, filename);
            }
        }
    }
}

static char *dired_get_default_path(EditBuffer *b, int offset, 
                                    char *buf, int buf_size)
{
    if (is_directory(b->filename)) {
        return makepath(buf, buf_size, b->filename, "");
    } else
    if (b->filename[0]) {
        get_dirname(buf, buf_size, b->filename);
        append_slash(buf, buf_size);
        return buf;
    } else {
        //// unused because buffer filename has been changed upon navigation
        //// should deal with file view from multiple directories
        //DiredState *ds = qe_get_buffer_mode_data(b, &dired_mode, NULL);
        //if (ds) {
        //    return makepath(buf, buf_size, ds->path, "");
        //}
        return NULL;
    }
}

static int dired_mode_init(EditState *s, EditBuffer *b, int flags)
{
    DiredState *ds = qe_get_buffer_mode_data(b, &dired_mode, NULL);

    if (!ds)
        return -1;

    list_mode.mode_init(s, b, flags);

    if (flags & MODEF_NEWINSTANCE) {
        eb_create_style_buffer(b, BF_STYLE1);
        /* XXX: should be built by buffer_load API */
        dired_build_list(ds, b->filename, NULL, b, s);
        /* XXX: File system charset should be detected automatically */
        /* XXX: If file system charset is not utf8, eb_printf will fail */
        eb_set_charset(b, &charset_utf8, b->eol_type);
    }
    return 0;
}

static void dired_mode_free(EditBuffer *b, void *state)
{
    DiredState *ds = state;

    dired_free(ds);
}

/* can only apply dired mode on directories and file patterns */
static int dired_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (qe_get_buffer_mode_data(p->b, &dired_mode, NULL))
        return 100;

    if (S_ISDIR(p->st_mode))
        return 95;

    if (p->st_errno == ENOENT && is_filepattern(p->real_filename))
        return 90;

    return 0;
}

/* open dired window on the left. The directory of the current file is
   used */
void do_dired(EditState *s, int argval)
{
    QEmacsState *qs = s->qe_state;
    EditBuffer *b;
    EditState *e;
    DiredState *ds;
    int width;
    char filename[MAX_FILENAME_SIZE], *p;
    char target[MAX_FILENAME_SIZE];

    /* Should take directory argument with optional switches,
     * find dired window if exists,
     * else create one and do this.
     * recursive listing and multi directory patterns.
     */

    if (argval != NO_ARG) {
        do_filelist(s, argval);
        return;
    }

    /* Should reuse previous dired buffer for same filespec */
    b = eb_scratch("*dired*", BF_READONLY | BF_UTF8);

    /* Remember target as current current buffer filename */
    pstrcpy(target, sizeof(target), s->b->filename);

    /* Set the filename to the directory of the current file */
    canonicalize_absolute_path(s, filename, sizeof(filename), target);
    if (!is_directory(filename) && !is_filepattern(filename)) {
        p = strrchr(filename, '/');
        if (p)
            *p = '\0';
    }
    eb_set_filename(b, filename);

    width = qs->width / 5;
    e = insert_window_left(b, width, WF_MODELINE | WF_FILELIST);
    /* set dired mode: dired_mode_init() will load buffer content */
    edit_set_mode(e, &dired_mode);

    ds = dired_get_state(e, 0);
    if (ds) {
        e->offset = eb_goto_pos(e->b, dired_find_target(ds, target),
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
          "s{Sort order [nesdug+-r]: }|sortkey|")
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
    CMD0( 'H', KEY_NONE,
          "dired-toggle-human", dired_toggle_human)
    CMD0( 'N', KEY_NONE,
          "dired-toggle-nflag", dired_toggle_nflag)
    CMD_DEF_END,
};

static CmdDef dired_global_commands[] = {
    CMD2( KEY_CTRLX(KEY_CTRL('d')), KEY_NONE,
          "dired", do_dired, ESi, "ui")
    CMD_DEF_END,
};

static int filelist_init(void);

static int dired_init(void)
{
    /* inherit from list mode */
    /* CG: assuming list_mode already initialized ? */
    memcpy(&dired_mode, &list_mode, sizeof(ModeDef));
    dired_mode.name = "dired";
    dired_mode.mode_probe = dired_mode_probe;
    dired_mode.buffer_instance_size = sizeof(DiredState);
    dired_mode.mode_init = dired_mode_init;
    dired_mode.mode_free = dired_mode_free;
    /* CG: not a good idea, display hook has side effect on layout */
    dired_mode.display_hook = dired_display_hook;
    dired_mode.get_default_path = dired_get_default_path;

    qe_register_mode(&dired_mode, /* MODEF_DATATYPE | */ MODEF_MAJOR | MODEF_VIEW);
    qe_register_variables(dired_variables, countof(dired_variables));
    qe_register_cmd_table(dired_commands, &dired_mode);
    qe_register_cmd_table(dired_global_commands, NULL);

    filelist_init();

    return 0;
}

/*---------------- filelist mode ----------------*/

static char filelist_last_buf[MAX_FILENAME_SIZE];

static ModeDef filelist_mode;

static void filelist_display_hook(EditState *s)
{
    char buf[MAX_FILENAME_SIZE];
    char dir[MAX_FILENAME_SIZE];
    char filename[MAX_FILENAME_SIZE];
    QEmacsState *qs = s->qe_state;
    EditState *e;
    int i, len, offset, target_line;

    offset = eb_goto_bol(s->b, s->offset);
    len = eb_fgets(s->b, buf, sizeof(buf), offset, &offset);
    buf[len] = '\0';

    if (s->x1 == 0 && s->y1 == 0 && s->width != qs->width
    &&  *buf && !strequal(buf, filelist_last_buf)) {
        /* open file so that user can see it before it is selected */
        /* XXX: find a better solution (callback) */
        pstrcpy(filelist_last_buf, sizeof(filelist_last_buf), buf);
        get_default_path(s->b, offset, dir, sizeof(dir));
        makepath(filename, sizeof(filename), dir, buf);
        target_line = 0;
        if (access(filename, R_OK)) {
            for (i = 0; i < len; i++) {
                if (buf[i] == ':' || buf[i] == *"()") {
                    buf[i] = '\0';
                    target_line = strtol(buf + i + 1, NULL, 10);
                    break;
                }
            }
            if (*buf) {
                makepath(filename, sizeof(filename), dir, buf);
            }
        }
        if (!access(filename, R_OK)) {
            e = dired_view_file(s, filename);
            if (e) {
                if (target_line > 0)
                    do_goto_line(e, target_line, 0);
            }
            put_status(e, "Previewing %s", filename);
        } else {
            put_status(s, "No access to %s", filename);
        }
    }
}

void do_filelist(EditState *s, int argval)
{
    QEmacsState *qs = s->qe_state;
    EditState *e;

    e = insert_window_left(s->b, qs->width / 5, WF_MODELINE | WF_FILELIST);
    if (e != NULL) {
        edit_set_mode(e, &filelist_mode);
        /* XXX: should come from mode.default_wrap */
        e->wrap = WRAP_TRUNCATE;
        filelist_last_buf[0] = '\0';
        qs->active_window = e;
    }
}

static int filelist_mode_init(EditState *s, EditBuffer *b, int flags)
{
    if (s) {
        /* XXX: should come from mode.default_wrap */
        s->wrap = WRAP_TRUNCATE;
    }
    return 0;
}

static CmdDef filelist_commands[] = {
    CMD0( KEY_RET, KEY_RIGHT,
          "filelist-select", do_other_window)
    CMD0( KEY_TAB, KEY_NONE,
          "filelist-tab", do_other_window)
    /* filelist-abort should restore previous buffer in right-window
     * or at least exit preview mode */
    CMD1( KEY_CTRL('g'), KEY_NONE,
          "filelist-abort", do_delete_window, 0)
    CMD_DEF_END,
};

static CmdDef filelist_global_commands[] = {
    CMD2( KEY_NONE, KEY_NONE,
          "filelist", do_filelist, ESi, "ui")
    CMD_DEF_END,
};

static int filelist_init(void)
{
    memcpy(&filelist_mode, &text_mode, sizeof(ModeDef));
    filelist_mode.name = "filelist";
    filelist_mode.mode_probe = NULL;
    filelist_mode.mode_init = filelist_mode_init;
    filelist_mode.display_hook = filelist_display_hook;

    qe_register_mode(&filelist_mode, MODEF_VIEW);
    qe_register_cmd_table(filelist_commands, &filelist_mode);
    qe_register_cmd_table(filelist_global_commands, NULL);
    return 0;
}

qe_module_init(dired_init);
