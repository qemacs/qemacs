/*
 * Utilities for qemacs.
 *
 * Copyright (c) 2001 Fabrice Bellard.
 * Copyright (c) 2002-2008 Charlie Gordon.
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
#include <dirent.h>

#ifdef WIN32
#include <sys/timeb.h>

/* XXX: not sufficient, but OK for basic operations */
int fnmatch(const char *pattern, const char *string, int flags)
{
    if (pattern[0] == '*')
        return 0;
    else
        return !strequal(pattern, string);
}

#else
#include <fnmatch.h>
#endif

struct FindFileState {
    char path[MAX_FILENAME_SIZE];
    char dirpath[MAX_FILENAME_SIZE]; /* current dir path */
    char pattern[MAX_FILENAME_SIZE]; /* search pattern */
    const char *bufptr;
    DIR *dir;
};

FindFileState *find_file_open(const char *path, const char *pattern)
{
    FindFileState *s;

    s = qe_malloc(FindFileState);
    if (!s)
        return NULL;
    pstrcpy(s->path, sizeof(s->path), path);
    pstrcpy(s->pattern, sizeof(s->pattern), pattern);
    s->bufptr = s->path;
    s->dirpath[0] = '\0';
    s->dir = NULL;
    return s;
}

int find_file_next(FindFileState *s, char *filename, int filename_size_max)
{
    struct dirent *dirent;
    const char *p;
    char *q;

    if (s->dir == NULL)
        goto redo;

    for (;;) {
        dirent = readdir(s->dir);
        if (dirent == NULL) {
        redo:
            if (s->dir) {
                closedir(s->dir);
                s->dir = NULL;
            }
            p = s->bufptr;
            if (*p == '\0')
                return -1;
            /* CG: get_str(&p, s->dirpath, sizeof(s->dirpath), ":") */
            q = s->dirpath;
            while (*p != ':' && *p != '\0') {
                if ((q - s->dirpath) < ssizeof(s->dirpath) - 1)
                    *q++ = *p;
                p++;
            }
            *q = '\0';
            if (*p == ':')
                p++;
            s->bufptr = p;
            s->dir = opendir(s->dirpath);
            if (!s->dir)
                goto redo;
        } else {
            if (fnmatch(s->pattern, dirent->d_name, 0) == 0) {
                makepath(filename, filename_size_max,
                         s->dirpath, dirent->d_name);
                return 0;
            }
        }
    }
}

void find_file_close(FindFileState *s)
{
    if (s->dir)
        closedir(s->dir);
    qe_free(&s);
}

#ifdef WIN32
/* convert '\' to '/' */
static void path_win_to_unix(char *buf)
{
    char *p;
    p = buf;
    while (*p) {
        if (*p == '\\')
            *p = '/';
        p++;
    }
}
#endif

/* suppress redundant ".", ".." and "/" from paths */
/* XXX: make it better */
static void canonicalize_path1(char *buf, int buf_size, const char *path)
{
    const char *p;
    char *q, *q1;
    int c, abs_path;
    char file[MAX_FILENAME_SIZE];

    p = path;
    abs_path = (p[0] == '/');
    buf[0] = '\0';
    for (;;) {
        /* extract file */
        q = file;
        for (;;) {
            c = *p;
            if (c == '\0')
                break;
            p++;
            if (c == '/')
                break;
            if ((q - file) < (int)sizeof(file) - 1)
                *q++ = c;
        }
        *q = '\0';

        if (file[0] == '\0') {
            /* nothing to do */
        } else if (file[0] == '.' && file[1] == '\0') {
            /* nothing to do */
        } else if (file[0] == '.' && file[1] == '.' && file[2] == '\0') {
            /* go up one dir */
            if (buf[0] == '\0') {
                if (!abs_path)
                    goto copy;
            } else {
                /* go to previous directory, if possible */
                q1 = strrchr(buf, '/');
                /* if already going up, cannot do more */
                if (!q1 || (q1[1] == '.' && q1[2] == '.' && q1[3] == '\0'))
                    goto copy;
                else
                    *q1 = '\0';
            }
        } else {
        copy:
            /* add separator if needed */
            if (buf[0] != '\0' || (buf[0] == '\0' && abs_path))
                pstrcat(buf, buf_size, "/");
            pstrcat(buf, buf_size, file);
        }
        if (c == '\0')
            break;
    }

    /* add at least '.' or '/' */
    if (buf[0] == '\0') {
        if (abs_path)
            pstrcat(buf, buf_size, "/");
        else
            pstrcat(buf, buf_size, ".");
    }
}

void canonicalize_path(char *buf, int buf_size, const char *path)
{
    const char *p;
    /* check for URL protocol or windows drive */
    /* CG: should not skip '/' */
    p = strchr(path, ':');
    if (p) {
        if ((p - path) == 1) {
            /* windows drive: we canonicalize only the following path */
            buf[0] = p[0];
            buf[1] = p[1];
            /* CG: this will not work for non current drives */
            canonicalize_path1(buf + 2, buf_size - 2, p);
        } else {
            /* URL: it is already canonical */
            pstrcpy(buf, buf_size, path);
        }
    } else {
        /* simple unix path */
        canonicalize_path1(buf, buf_size, path);
    }
}

/* return TRUE if absolute path. works for files and URLs */
static int is_abs_path(const char *path)
{
    const char *p;

    p = strchr(path, ':');
    if (p)
        p++;
    else
        p = path;
    return *p == '/';
}

/* canonicalize the path and make it absolute */
void canonicalize_absolute_path(char *buf, int buf_size, const char *path1)
{
    char cwd[MAX_FILENAME_SIZE];
    char path[MAX_FILENAME_SIZE];
    char *homedir;

    if (!is_abs_path(path1)) {
        if (*path1 == '~') {
            if (path1[1] == '\0' || path1[1] == '/') {
                homedir = getenv("HOME");
                if (homedir) {
                    pstrcpy(path, sizeof(path), homedir);
#ifdef WIN32
                    path_win_to_unix(path);
#endif
                    remove_slash(path);
                    pstrcat(path, sizeof(path), path1 + 1);
                    path1 = path;
                }
            } else {
                /* CG: should get info from getpwnam */
                pstrcpy(path, sizeof(path), "/home/");
                pstrcat(path, sizeof(path), path1 + 1);
                path1 = path;
            }
        } else {
            /* CG: not sufficient for windows drives */
            /* CG: should test result */
            getcwd(cwd, sizeof(cwd));
#ifdef WIN32
            path_win_to_unix(cwd);
#endif
            makepath(path, sizeof(path), cwd, path1);
            path1 = path;
        }
    }
    canonicalize_path(buf, buf_size, path1);
}

/* Get the filename portion of a path */
const char *basename(const char *filename)
{
    const char *p;
    const char *base;

    base = filename;
    if (base) {
        for (p = base; *p; p++) {
#ifdef WIN32
            /* Simplistic DOS filename support */
            if (*p == '/' || *p == '\\' || *p == ':')
                base = p + 1;
#else
            if (*p == '/')
                base = p + 1;
#endif
        }
    }
    return base;
}

/* Return the last extension in a path, ignoring leading dots */
const char *extension(const char *filename)
{
    const char *p, *ext;

    p = basename(filename);
    ext = NULL;
    if (p) {
        while (*p == '.')
            p++;
        for (; *p; p++) {
            if (*p == '.')
                ext = p;
        }
        if (!ext)
            ext = p;
    }
    return ext;
}

/* Extract the directory portion of a path:
 * This leaves out the trailing slash if any.  The complete path is
 * obtained by catenating dirname + '/' + basename.
 * if the original path doesn't contain anything dirname is just "."
 */
char *get_dirname(char *dest, int size, const char *file)
{
    char *p;

    if (dest) {
        p = dest;
        if (file) {
            pstrcpy(dest, size, file);
            p = dest + (basename(dest) - dest);
            if (p > dest + 1 && p[-1] != ':' && p[-2] != ':')
                p--;

            if (p == dest)
                *p++ = '.';
        }
        *p = '\0';
    }
    return dest;
}

char *reduce_filename(char *dest, int size, const char *filename)
{
    const char *base = basename(filename);
    char *dbase, *ext, *p;

    /* Copy path unchanged */
    pstrncpy(dest, size, filename, base - filename);

    /* Strip cvs temp file prefix */
    if (base[0] == '.' && base[1] == '#' && base[2] != '\0')
        base += 2;
    
    pstrcat(dest, size, base);

    dbase = dest + (basename(dest) - dest);

    /* Strip numeric extensions (vcs version numbers) */
    for (;;) {
        ext = dbase + (extension(dbase) - dbase);
        if (*ext != '.' || !qe_isdigit(ext[1]))
            break;
        *ext = '\0';
    }
    
    if (*ext == '.') {
        /* Convert all upper case filenames with extension to lower
         * case */
        for (p = dbase; *p; p++) {
            if (qe_islower(*p))
                break;
        }
        if (!*p) {
            qe_strtolower(dbase, dest + size - dbase, dbase);
        }
    }

    /* Strip backup file suffix */
    p = dbase + strlen(dbase);
    if (p > dbase + 1 && p[-1] == '~')
        *--p = '\0';

    return dest;
}

int match_extension(const char *filename, const char *extlist)
{
    const char *r;

    r = extension(filename);
    if (*r == '.') {
        return strfind(extlist, r + 1);
    } else {
        return 0;
    }
}

/* Remove trailing slash from path, except for / directory */
int remove_slash(char *buf)
{
    int len;

    len = strlen(buf);
    if (len > 1 && buf[len - 1] == '/') {
        buf[--len] = '\0';
    }
    return len;
}

/* Append trailing slash to path if none there already */
int append_slash(char *buf, int buf_size)
{
    int len;

    len = strlen(buf);
    if (len > 0 && buf[len - 1] != '/' && len + 1 < buf_size) {
        buf[len++] = '/';
        buf[len] = '\0';
    }
    return len;
}

char *makepath(char *buf, int buf_size, const char *path,
               const char *filename)
{
    pstrcpy(buf, buf_size, path);
    append_slash(buf, buf_size);
    return pstrcat(buf, buf_size, filename);
}

void splitpath(char *dirname, int dirname_size,
               char *filename, int filename_size, const char *pathname)
{
    const char *base;

    base = basename(pathname);
    if (dirname)
        pstrncpy(dirname, dirname_size, pathname, base - pathname);
    if (filename)
        pstrcpy(filename, filename_size, base);
}

/* smart compare strings, lexicographical order, but collate numbers in
 * numeric order */
int qe_collate(const char *s1, const char *s2)
{
    int last, c1, c2, res, flags;

    last = '\0';
    for (;;) {
        c1 = (unsigned char)*s1++;
        c2 = (unsigned char)*s2++;
        if (c1 == c2) {
            last = c1;
            if (c1 == '\0')
                return 0;
        } else {
            break;
        }
    }
    res = (c1 < c2) ? -1 : 1;
    for (;;) {
        flags = qe_isdigit(c1) * 2 + qe_isdigit(c2);
        if (flags == 3) {
            last = c1;
            c1 = (unsigned char)*s1++;
            c2 = (unsigned char)*s2++;
        } else {
            break;
        }
    }
    if (!qe_isdigit(last) || flags == 0)
        return res;
    return (flags == 1) ? -1 : 1;
}

/* Should return int, length of converted string? */
void qe_strtolower(char *buf, int size, const char *str)
{
    int c;

    /* This version only handles ASCII */
    if (size > 0) {
        while ((c = (unsigned char)*str++) != '\0' && size > 1) {
            *buf++ = qe_tolower(c);
            size--;
        }
        *buf = '\0';
    }
}

void skip_spaces(const char **pp)
{
    const char *p;

    p = *pp;
    while (qe_isspace(*p))
        p++;
    *pp = p;
}

#if 0
/* find a word in a list using '|' as separator */
int strfind(const char *keytable, const char *str)
{
    int c, len;
    const char *p;

    c = *str;
    len = strlen(str);
    /* need to special case the empty string */
    if (len == 0)
        return strstr(keytable, "||") != NULL;

    /* initial and trailing | are optional */
    /* they do not cause the empty string to match */
    for (p = keytable;;) {
        if (!memcmp(p, str, len) && (p[len] == '|' || p[len] == '\0'))
            return 1;
        for (;;) {
            p = strchr(p + 1, c);
            if (!p)
                return 0;
            if (p[-1] == '|')
                break;
        }
    }
}
#else
/* Search for the string s in '|' delimited list of strings */
int strfind(const char *list, const char *s)
{
    const char *p, *q;
    int c1, c2;

    q = list;
    if (!q)
        return 0;

    if (*s == '\0') {
        /* special case the empty string: must match || in list */
        while (*q) {
            if (q[0] == '|' && q[1] == '|')
                return 1;
            q++;
        }
        return 0;
    } else {
    scan:
        p = s;
        for (;;) {
            c1 = *p++;
            c2 = *q++;
            if (c1 == '\0') {
                if (c2 == '\0' || c2 == '|')
                    return 1;
                goto skip;
            }
            if (c1 != c2) {
                for (;;) {
                    if (c2 == '|')
                        goto scan;
                    
                    if (c2 == '\0')
                        return 0;
                skip:
                    c2 = *q++;
                }
            }
        }
    }
}
#endif

/* find a word in a list using '|' as separator, fold case to lower case. */
int strcasefind(const char *list, const char *s)
{
    char buf[128];

    qe_strtolower(buf, sizeof(buf), s);
    return strfind(list, buf);
}

const void *memstr(const void *buf, int size, const char *str)
{
    int c, len;
    const u8 *p, *buf_max;

    c = *str++;
    if (!c) {
        /* empty string matches start of buffer */
        return buf;
    }

    len = strlen(str);
    if (len >= size)
        return NULL;

    buf_max = (const u8*)buf + size - len;
    for (p = buf; p < buf_max; p++) {
        if (*p == c && !memcmp(p + 1, str, len))
            return p;
    }
    return NULL;
}

/**
 * Return TRUE if val is a prefix of str (case independent). If it
 * returns TRUE, ptr is set to the next character in 'str' after the
 * prefix.
 *
 * @param str input string
 * @param val prefix to test
 * @param ptr updated after the prefix in str in there is a match
 * @return TRUE if there is a match */
int stristart(const char *str, const char *val, const char **ptr)
{
    const char *p, *q;

    p = str;
    q = val;
    while (*q != '\0') {
        if (qe_toupper((unsigned char)*p) != qe_toupper((unsigned char)*q)) {
            return 0;
        }
        p++;
        q++;
    }
    if (ptr)
        *ptr = p;
    return 1;
}

/**
 * Return TRUE if val is a prefix of str (case independent). If it
 * returns TRUE, ptr is set to the next character in 'str' after the
 * prefix.
 *
 * Spaces, dashes and underscores are also ignored in this comparison.
 *
 * @param str input string
 * @param val prefix to test
 * @param ptr updated after the prefix in str in there is a match
 * @return TRUE if there is a match */
int strxstart(const char *str, const char *val, const char **ptr)
{
    const char *p, *q;
    p = str;
    q = val;
    while (*q != '\0') {
        if (qe_toupper((unsigned char)*p) != qe_toupper((unsigned char)*q)) {
            if (*p == '-' || *p == '_' || *p == ' ') {
                p++;
                continue;
            }
            if (*q == '-' || *q == '_' || *q == ' ') {
                q++;
                continue;
            }
            return 0;
        }
        p++;
        q++;
    }
    if (ptr)
        *ptr = p;
    return 1;
}

/**
 * Compare strings str1 and str2 case independently.
 * Spaces, dashes and underscores are also ignored in this comparison.
 *
 * @param str1 input string 1 (left operand)
 * @param str2 input string 2 (right operand)
 * @return -1, 0, +1 reflecting the sign of str1 <=> str2
 */
int strxcmp(const char *str1, const char *str2)
{
    const char *p, *q;
    int d;

    p = str1;
    q = str2;
    for (;;) {
        d = qe_toupper((unsigned char)*p) - qe_toupper((unsigned char)*q);
        if (d) {
            if (*p == '-' || *p == '_' || *p == ' ') {
                p++;
                continue;
            }
            if (*q == '-' || *q == '_' || *q == ' ') {
                q++;
                continue;
            }
            return d < 0 ? -1 : +1;
        }
        if (!*p)
            break;
        p++;
        q++;
    }
    return 0;
}

int ustristart(const unsigned int *str, const char *val,
               const unsigned int **ptr)
{
    const unsigned int *p;
    const char *q;
    p = str;
    q = val;
    while (*q != '\0') {
        /* XXX: should filter style information */
        if (qe_toupper(*p) != qe_toupper(*q))
            return 0;
        p++;
        q++;
    }
    if (ptr)
        *ptr = p;
    return 1;
}

int umemcmp(const unsigned int *s1, const unsigned int *s2, int count)
{
    for (; count > 0; count--, s1++, s2++) {
        if (*s1 != *s2) {
            return *s1 < *s2 ? -1 : 1;
        }
    }
    return 0;
}

/* Read a token from a string, stop on a set of characters.
 * Skip spaces before and after token.
 */
void get_str(const char **pp, char *buf, int buf_size, const char *stop)
{
    char *q;
    const char *p;
    int c;

    skip_spaces(pp);
    p = *pp;
    q = buf;
    for (;;) {
        c = *p;
        /* Stop on spaces and eat them */
        if (c == '\0' || qe_isspace(c) || strchr(stop, c))
            break;
        if ((q - buf) < buf_size - 1)
            *q++ = c;
        p++;
    }
    *q = '\0';
    *pp = p;
    skip_spaces(pp);
}

/* scans a comma separated list of entries, return index of match or -1 */
/* CG: very similar to strfind */
int css_get_enum(const char *str, const char *enum_str)
{
    int val, len;
    const char *s, *s1;

    s = enum_str;
    val = 0;
    len = strlen(str);
    for (;;) {
        s1 = strchr(s, ',');
        if (s1) {
            if (len == (s1 - s) && !memcmp(s, str, len))
                return val;
            s = s1 + 1;
        } else {
            if (strequal(s, str))
                return val;
            else
                break;
        }
        val++;
    }
    return -1;
}

static unsigned short const keycodes[] = {
    KEY_SPC, KEY_DEL, KEY_RET, KEY_ESC, KEY_TAB, KEY_SHIFT_TAB,
    KEY_CTRL(' '), KEY_DEL, KEY_CTRL('\\'),
    KEY_CTRL(']'), KEY_CTRL('^'), KEY_CTRL('_'), KEY_CTRL('_'),
    KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN,
    KEY_HOME, KEY_END, KEY_PAGEUP, KEY_PAGEDOWN,
    KEY_CTRL_LEFT, KEY_CTRL_RIGHT, KEY_CTRL_UP, KEY_CTRL_DOWN,
    KEY_CTRL_HOME, KEY_CTRL_END, KEY_CTRL_PAGEUP, KEY_CTRL_PAGEDOWN,
    KEY_PAGEUP, KEY_PAGEDOWN, KEY_CTRL_PAGEUP, KEY_CTRL_PAGEDOWN,
    KEY_INSERT, KEY_DELETE, KEY_DEFAULT,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5,
    KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10,
    KEY_F11, KEY_F12, KEY_F13, KEY_F14, KEY_F15,
    KEY_F16, KEY_F17, KEY_F18, KEY_F19, KEY_F20,
};

static const char * const keystr[] = {
    "SPC",    "DEL",      "RET",      "ESC",    "TAB", "S-TAB",
    "C-SPC",  "C-?",      "C-\\",     "C-]",    "C-^", "C-_", "C-/",
    "left",   "right",    "up",       "down",
    "home",   "end",      "prior",    "next",
    "C-left", "C-right",  "C-up",     "C-down",
    "C-home", "C-end",    "C-prior",  "C-next",
    "pageup", "pagedown", "C-pageup", "C-pagedown",
    "insert", "delete",   "default",
    "f1",     "f2",       "f3",       "f4",    "f5",
    "f6",     "f7",       "f8",       "f9",    "f10",
    "f11",    "f12",      "f13",      "f14",   "f15",
    "f16",    "f17",      "f18",      "f19",   "f20",
};

int compose_keys(unsigned int *keys, int *nb_keys)
{
    unsigned int *keyp;

    if (*nb_keys < 2)
        return 0;

    /* compose KEY_ESC as META prefix */
    keyp = keys + *nb_keys - 2;
    if (keyp[0] == KEY_ESC) {
        if (keyp[1] <= 0xff) {
            keyp[0] = KEY_META(keyp[1]);
            --*nb_keys;
            return 1;
        }
    }
    return 0;
}

/* CG: this code is still quite inelegant */
static int strtokey1(const char **pp)
{
    const char *p, *p1, *q;
    int i, key;

    /* should return KEY_NONE at end and KEY_UNKNOWN if unrecognized */
    p = *pp;

    /* scan for separator */
    for (p1 = p; *p1 && *p1 != ' '; p1++)
        continue;

    for (i = 0; i < countof(keycodes); i++) {
        if (strstart(p, keystr[i], &q) && q == p1) {
            key = keycodes[i];
            *pp = p1;
            return key;
        }
    }
#if 0
    /* Cannot do this because KEY_F1..KEY_F20 are not consecutive */
    if (p[0] == 'f' && p[1] >= '1' && p[1] <= '9') {
        i = p[1] - '0';
        p += 2;
        if (qe_isdigit(*p))
            i = i * 10 + *p++ - '0';
        key = KEY_F1 + i - 1;
        *pp = p1;
        return key;
    }
#endif
    /* Should also support backslash escapes: \000 \x00 \u0000 */
    /* Should also support ^x and syntax and Ctrl- prefix for control keys */
    /* Should test for p[2] in range 'a'..'z', '@'..'_', '?' */
    if (p[0] == 'C' && p[1] == '-' && p1 == p + 3) {
        /* control */
        key = KEY_CTRL(p[2]);
    } else {
        key = utf8_decode(&p);
    }
    *pp = p1;

    return key;
}

int strtokey(const char **pp)
{
    const char *p;
    int key;

    p = *pp;
    /* Should also support A- and Alt- prefix for meta keys */
    if (p[0] == 'M' && p[1] == '-') {
        p += 2;
        key = KEY_META(strtokey1(&p));
    } else
    if (p[0] == 'C' && p[1] == '-' && p[2] == 'M' && p[3] == '-') {
        /* Should pass buffer with C-xxx to strtokey1 */
        p += 4;
        key = KEY_META(KEY_CTRL(strtokey1(&p)));
    } else {
        key = strtokey1(&p);
    }
    *pp = p;
    return key;
}

int strtokeys(const char *kstr, unsigned int *keys, int max_keys)
{
    int key, nb_keys;
    const char *p;

    p = kstr;
    nb_keys = 0;

    for (;;) {
        skip_spaces(&p);
        if (*p == '\0')
            break;
        key = strtokey(&p);
        keys[nb_keys++] = key;
        compose_keys(keys, &nb_keys);
        if (nb_keys >= max_keys)
            break;
    }
    return nb_keys;
}

void keytostr(char *buf, int buf_size, int key)
{
    int i;
    char buf1[32];
    buf_t out;

    buf_init(&out, buf, buf_size);

    for (i = 0; i < countof(keycodes); i++) {
        if (keycodes[i] == key) {
            buf_puts(&out, keystr[i]);
            return;
        }
    }
    if (key >= KEY_META(0) && key <= KEY_META(0xff)) {
        keytostr(buf1, sizeof(buf1), key & 0xff);
        buf_printf(&out, "M-%s", buf1);
    } else
    if (key >= KEY_CTRL('a') && key <= KEY_CTRL('z')) {
        buf_printf(&out, "C-%c", key + 'a' - 1);
    } else
#if 0
    /* Cannot do this because KEY_F1..KEY_F20 are not consecutive */
    if (key >= KEY_F1 && key <= KEY_F20) {
        buf_printf(&out, "f%d", key - KEY_F1 + 1);
    } else
#endif
    {
        buf_putc_utf8(&out, key);
    }
}

int to_hex(int key)
{
    /* Only ASCII supported */
    if (qe_isdigit(key))
        return key - '0';
    else
    if (qe_inrange(key | ('a' - 'A'), 'a', 'f'))
        return (key & 7) + 9;
    else
        return -1;
}

#if 1
/* Should move all this to display.c */

typedef struct ColorDef {
    const char *name;
    unsigned int color;
} ColorDef;

static ColorDef const default_colors[] = {
    /* From HTML 4.0 spec */
    { "black",   QERGB(0x00, 0x00, 0x00) },
    { "green",   QERGB(0x00, 0x80, 0x00) },
    { "silver",  QERGB(0xc0, 0xc0, 0xc0) },
    { "lime",    QERGB(0x00, 0xff, 0x00) },

    { "gray",    QERGB(0xbe, 0xbe, 0xbe) },
    { "olive",   QERGB(0x80, 0x80, 0x00) },
    { "white",   QERGB(0xff, 0xff, 0xff) },
    { "yellow",  QERGB(0xff, 0xff, 0x00) },

    { "maroon",  QERGB(0x80, 0x00, 0x00) },
    { "navy",    QERGB(0x00, 0x00, 0x80) },
    { "red",     QERGB(0xff, 0x00, 0x00) },
    { "blue",    QERGB(0x00, 0x00, 0xff) },

    { "purple",  QERGB(0x80, 0x00, 0x80) },
    { "teal",    QERGB(0x00, 0x80, 0x80) },
    { "fuchsia", QERGB(0xff, 0x00, 0xff) },
    { "aqua",    QERGB(0x00, 0xff, 0xff) },

    /* more colors */
    { "cyan",    QERGB(0x00, 0xff, 0xff) },
    { "magenta", QERGB(0xff, 0x00, 0xff) },
    { "grey",    QERGB(0xbe, 0xbe, 0xbe) },
    { "transparent", COLOR_TRANSPARENT },
};
#define nb_default_colors  countof(default_colors)

static ColorDef *qe_colors = (ColorDef *)default_colors;
static int nb_qe_colors = nb_default_colors;

void color_completion(CompleteState *cp)
{
    ColorDef const *def;
    int count;

    def = qe_colors;
    count = nb_qe_colors;
    while (count > 0) {
        if (strxstart(def->name, cp->current, NULL))
            add_string(&cp->cs, def->name);
        def++;
        count--;
    }
}

static int css_lookup_color(ColorDef const *def, int count,
                            const char *name)
{
    int i;

    for (i = 0; i < count; i++) {
        if (!strxcmp(def[i].name, name))
            return i;
    }
    return -1;
}

int css_define_color(const char *name, const char *value)
{
    ColorDef *def;
    QEColor color;
    int index;

    /* Check color validity */
    if (css_get_color(&color, value))
        return -1;

    /* First color definition: allocate modifiable array */
    if (qe_colors == default_colors) {
        qe_colors = qe_malloc_dup(default_colors, sizeof(default_colors));
    }

    /* Make room: reallocate table in chunks of 8 entries */
    if (((nb_qe_colors - nb_default_colors) & 7) == 0) {
        if (!qe_realloc(&qe_colors,
                        (nb_qe_colors + 8) * sizeof(ColorDef))) {
            return -1;
        }
    }
    /* Check for redefinition */
    index = css_lookup_color(qe_colors, nb_qe_colors, name);
    if (index >= 0) {
        qe_colors[index].color = color;
        return 0;
    }

    def = &qe_colors[nb_qe_colors];
    def->name = qe_strdup(name);
    def->color = color;
    nb_qe_colors++;

    return 0;
}

/* XXX: make HTML parsing optional ? */
int css_get_color(QEColor *color_ptr, const char *p)
{
    ColorDef const *def;
    int count, index, len, v, i, n;
    unsigned char rgba[4];

    /* search in tables */
    def = qe_colors;
    count = nb_qe_colors;
    index = css_lookup_color(def, count, p);
    if (index >= 0) {
        *color_ptr = def[index].color;
        return 0;
    }

    rgba[3] = 0xff;
    if (qe_isxdigit((unsigned char)*p)) {
        goto parse_num;
    } else
    if (*p == '#') {
        /* handle '#' notation */
        p++;
    parse_num:
        len = strlen(p);
        switch (len) {
        case 3:
            for (i = 0; i < 3; i++) {
                v = to_hex(*p++);
                rgba[i] = v | (v << 4);
            }
            break;
        case 6:
            for (i = 0; i < 3; i++) {
                v = to_hex(*p++) << 4;
                v |= to_hex(*p++);
                rgba[i] = v;
            }
            break;
        default:
            /* error */
            return -1;
        }
    } else
    if (strstart(p, "rgb(", &p)) {
        n = 3;
        goto parse_rgba;
    } else
    if (strstart(p, "rgba(", &p)) {
        /* extension for alpha */
        n = 4;
    parse_rgba:
        for (i = 0; i < n; i++) {
            /* XXX: floats ? */
            skip_spaces(&p);
            v = strtol(p, (char **)&p, 0);
            if (*p == '%') {
                v = (v * 255) / 100;
                p++;
            }
            rgba[i] = v;
            skip_spaces(&p);
            if (*p == ',')
                p++;
        }
    } else {
        return -1;
    }
    *color_ptr = (rgba[0] << 16) | (rgba[1] << 8) |
        (rgba[2]) | (rgba[3] << 24);
    return 0;
}

/* return 0 if unknown font */
int css_get_font_family(const char *str)
{
    int v;

    if (!strcasecmp(str, "serif") ||
        !strcasecmp(str, "times"))
        v = QE_FAMILY_SERIF;
    else
    if (!strcasecmp(str, "sans") ||
        !strcasecmp(str, "arial") ||
        !strcasecmp(str, "helvetica"))
        v = QE_FAMILY_SANS;
    else
    if (!strcasecmp(str, "fixed") ||
        !strcasecmp(str, "monospace") ||
        !strcasecmp(str, "courier"))
        v = QE_FAMILY_FIXED;
    else
        v = 0; /* inherit */
    return v;
}
#endif  /* style stuff */

/* a = a union b */
void css_union_rect(CSSRect *a, const CSSRect *b)
{
    if (css_is_null_rect(b))
        return;
    if (css_is_null_rect(a)) {
        *a = *b;
    } else {
        if (b->x1 < a->x1)
            a->x1 = b->x1;
        if (b->y1 < a->y1)
            a->y1 = b->y1;
        if (b->x2 > a->x2)
            a->x2 = b->x2;
        if (b->y2 > a->y2)
            a->y2 = b->y2;
    }
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

int get_clock_ms(void)
{
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

int get_clock_usec(void)
{
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

/* set one string. */
StringItem *set_string(StringArray *cs, int index, const char *str)
{
    StringItem *v;

    if (index >= cs->nb_items)
        return NULL;

    v = qe_malloc_hack(StringItem, strlen(str));
    if (!v)
        return NULL;
    v->selected = 0;
    strcpy(v->str, str);
    if (cs->items[index])
        qe_free(&cs->items[index]);
    cs->items[index] = v;
    return v;
}

/* make a generic array alloc */
StringItem *add_string(StringArray *cs, const char *str)
{
    int n;

    if (cs->nb_items >= cs->nb_allocated) {
        n = cs->nb_allocated + 32;
        if (!qe_realloc(&cs->items, n * sizeof(StringItem *)))
            return NULL;
        cs->nb_allocated = n;
    }
    cs->items[cs->nb_items++] = NULL;
    return set_string(cs, cs->nb_items - 1, str);
}

void free_strings(StringArray *cs)
{
    int i;

    for (i = 0; i < cs->nb_items; i++)
        qe_free(&cs->items[i]);
    qe_free(&cs->items);
    memset(cs, 0, sizeof(StringArray));
}

/**
 * Add a memory region to a dynamic string. In case of allocation
 * failure, the data is not added. The dynamic string is guaranteed to
 * be 0 terminated, although it can be longer if it contains zeros.
 *
 * @return 0 if OK, -1 if allocation error.
 */
int qmemcat(QString *q, const unsigned char *data1, int len1)
{
    int new_len, len, alloc_size;

    len = q->len;
    new_len = len + len1;
    /* see if we got a new power of two */
    /* NOTE: we got this trick from the excellent 'links' browser */
    if ((len ^ new_len) >= len) {
        /* find immediately bigger 2^n - 1 */
        alloc_size = new_len;
        alloc_size |= (alloc_size >> 1);
        alloc_size |= (alloc_size >> 2);
        alloc_size |= (alloc_size >> 4);
        alloc_size |= (alloc_size >> 8);
        alloc_size |= (alloc_size >> 16);
        /* allocate one more byte for end of string marker */
        if (!qe_realloc(&q->data, alloc_size + 1))
            return -1;
    }
    memcpy(q->data + len, data1, len1);
    q->data[new_len] = '\0'; /* we force a trailing '\0' */
    q->len = new_len;
    return 0;
}

/*
 * add a string to a dynamic string
 */
int qstrcat(QString *q, const char *str)
{
    return qmemcat(q, (unsigned char *)str, strlen(str));
}

/* XXX: we use a fixed size buffer */
int qprintf(QString *q, const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    int len, ret;

    va_start(ap, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, ap);
    /* avoid problems for non C99 snprintf() which can return -1 if overflow */
    if (len < 0)
        len = strlen(buf);
    ret = qmemcat(q, (unsigned char *)buf, len);
    va_end(ap);
    return ret;
}

int buf_write(buf_t *bp, const void *src, int size)
{
    int n = buf_avail(bp);

    if (n > size)
        n = size;
    memcpy(bp->buf + bp->len, src, n);
    bp->pos += size;
    bp->len += n;
    if (bp->len < bp->size)
        bp->buf[bp->len] = '\0';
    return n;
}

int buf_printf(buf_t *bp, const char *fmt, ...)
{
    va_list ap;
    int len;

    va_start(ap, fmt);
    len = vsnprintf(bp->buf + bp->len,
                    (bp->pos < bp->size) ? bp->size - bp->pos : 1, fmt, ap);
    va_end(ap);

    bp->pos += len;
    bp->len += len;
    if (bp->len >= bp->size) {
        bp->len = bp->size - 1;
        if (bp->len < 0)
            bp->len = 0;
    }

    return len;
}

int buf_putc_utf8(buf_t *bp, int c)
{
    if (c < 0x80) {
        bp->pos++;
        if (bp->pos < bp->size) {
            bp->buf[bp->len++] = c;
            bp->buf[bp->len] = '\0';
            return 1;
        } else {
            return 0;
        }
    } else {
        char buf[6];
        int len;

        len = utf8_encode(buf, c);

        if (buf_avail(bp) >= len) {
            memcpy(bp->buf + bp->len, buf, len);
            bp->buf[bp->len] = '\0';
            bp->pos += len;
            bp->len += len;
            return len;
        }
        bp->pos += len;
        return 0;
    }
}

int strsubst(char *buf, int buf_size, const char *from,
             const char *s1, const char *s2)
{
    const char *p, *q;
    buf_t out;

    buf_init(&out, buf, buf_size);

    p = from;
    while ((q = strstr(p, s1)) != NULL) {
        buf_write(&out, p, q - p);
        buf_puts(&out, s2);
        p = q + strlen(s1);
    }
    buf_puts(&out, p);

    return out.pos;
}

/*---------------- allocation routines ----------------*/

void *qe_malloc_bytes(size_t size)
{
    return malloc(size);
}

void *qe_mallocz_bytes(size_t size)
{
    void *p = malloc(size);
    if (p)
        memset(p, 0, size);
    return p;
}

void *qe_malloc_dup(const void *src, size_t size)
{
    void *p = malloc(size);
    if (p)
        memcpy(p, src, size);
    return p;
}

char *qe_strdup(const char *str)
{
    size_t size = strlen(str) + 1;
    char *p = malloc(size);

    if (p)
        memcpy(p, str, size);
    return p;
}

void *qe_realloc(void *pp, size_t size)
{
    void *p = realloc(*(void **)pp, size);
    if (p || !size)
        *(void **)pp = p;
    return p;
}
