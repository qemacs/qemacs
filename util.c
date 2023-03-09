/*
 * Utilities for qemacs.
 *
 * Copyright (c) 2001 Fabrice Bellard.
 * Copyright (c) 2002-2023 Charlie Gordon.
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

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "config.h"     /* for CONFIG_WIN32 */
#include "util.h"

#ifdef CONFIG_WIN32

/* XXX: not sufficient, but OK for basic operations */
// XXX: should use own own function in all cases
int fnmatch(const char *pattern, const char *string, int flags) {
    char c;
    while ((c = *pattern++) != '\0') {
        if (c == '*') {
            if (*pattern == '\0')
                return 0;
            while (*string) {
                if (!fnmatch(pattern, string))
                    return 0;
                string++;
            }
            return FNM_NOMATCH;
        } else
        if (c == '?') {
            if (*string++ == '\0')
                return FNM_NOMATCH;
        } else
        if (c != *string++) {
            return FNM_NOMATCH;
        }
    }
    return *string ? FNM_NOMATCH : 0;
}

#else
#include <fnmatch.h>
#endif

#define MAX_FILENAME_SIZE    1024       /* Size for a filename buffer */

struct FindFileState {
    char path[MAX_FILENAME_SIZE];
    char dirpath[MAX_FILENAME_SIZE]; /* current dir path */
    char pattern[MAX_FILENAME_SIZE]; /* search pattern */
    const char *bufptr;
    int flags;
    int depth;
    DIR *dir;
    DIR *parent_dir[FF_DEPTH];
    size_t parent_len[FF_DEPTH];
};

FindFileState *find_file_open(const char *path, const char *pattern, int flags) {
    /*@API utils
       Start a directory enumeration.
       @argument `path` the initial directory for the enumeration.
       @argument `pattern` a file pattern using `?` and `*` with the classic
       semantics used by unix shells
       @return a pointer to an opaque FindFileState structure.
     */
    FindFileState *s;

    s = qe_mallocz(FindFileState);
    if (!s)
        return NULL;
    pstrcpy(s->path, sizeof(s->path), path);
    pstrcpy(s->pattern, sizeof(s->pattern), pattern);
    s->bufptr = s->path;
    s->dir = NULL;
    s->flags = flags;
    return s;
}

int find_file_next(FindFileState *s, char *filename, int filename_size_max) {
    /*@API utils
       Get the next match in a directory enumeration.
       @argument `filename` a valid pointer to an array for the file name.
       @argument `filename_size_max` the length if the `filename` destination
       array in bytes.
       @return `0` if there is a match, `-1` if no more files matche the pattern.
     */
    struct dirent *dirent;
    const char *p;

    for (;;) {
        if (!s->dir || (dirent = readdir(s->dir)) == NULL) {
            if (s->dir) {
                closedir(s->dir);
                s->dir = NULL;
            }
            if (s->depth) {
                s->depth--;
                s->dir = s->parent_dir[s->depth];
                s->parent_dir[s->depth] = NULL;
                s->dirpath[s->parent_len[s->depth]] = '\0';
                continue;
            }
            p = s->bufptr;
            if (*p == '\0')
                return -1;
            if (s->flags & FF_PATH)
                p += strcspn(p, ":");
            else
                p += strlen(p);
            pstrncpy(s->dirpath, sizeof(s->dirpath), s->bufptr, p - s->bufptr);
            if (*p == ':')
                p++;
            s->bufptr = p;
            s->dir = opendir(s->dirpath);
        } else {
            if (dirent->d_type == DT_DIR) {
                if (*dirent->d_name == '.'
                &&  (strequal(dirent->d_name, ".") || strequal(dirent->d_name, ".."))) {
                    if (s->flags & FF_NOXXDIR)
                        continue;
                } else {
                    if (s->depth < (s->flags & FF_DEPTH)) {
                        s->parent_dir[s->depth] = s->dir;
                        s->parent_len[s->depth] = strlen(s->dirpath);
                        s->depth++;
                        makepath(s->dirpath, sizeof(s->dirpath), s->dirpath, dirent->d_name);
                        s->dir = opendir(s->dirpath);
                        continue;
                    }
                }
                if (s->flags & FF_NODIR)
                    continue;
            } else {
                if (s->flags & FF_ONLYDIR)
                    continue;
            }
            // XXX: should use our own fnmatch version
            if (fnmatch(s->pattern, dirent->d_name, 0) == 0) {
                makepath(filename, filename_size_max,
                         s->dirpath, dirent->d_name);
                return 0;
            }
        }
    }
}

void find_file_close(FindFileState **sp) {
    /*@API utils
       Close a directory enumeration state `FindFileState`.
       @argument `sp` a valid pointer to a `FindFileState` pointer that
       will be closed.
       @note `FindFileState` state structures must be freed to avoid memory
       and resource leakage.
     */
    if (*sp) {
        FindFileState *s = *sp;

        while (s->depth) {
            closedir(s->parent_dir[--s->depth]);
        }
        if (s->dir)
            closedir(s->dir);
        qe_free(sp);
    }
}

int is_directory(const char *path) {
    /*@API utils
       Check if the string pointed to by `path` is the name of a
       directory.
       @argument `path` a valid pointer to a string.
       @return `true` if `path` is the name of a directory, `false` otherwise.
       @note this function uses `stat`, so it will return `true` for
       directories and symbolic links pointing to existing directories.
     */
    struct stat st;
    return (!stat(path, &st) && S_ISDIR(st.st_mode));
}

int is_filepattern(const char *filespec) {
    /*@API utils
       Check if the string pointed to by `filespec` is a file pattern.
       @argument `filespec` a valid pointer to a string.
       @return `true` if `filespec` contains wildcard characters.
       @note this function only recognises `?` and `*` wildcard characters
     */
    // XXX: should also accept character ranges and {} comprehensions
    int pos = strcspn(filespec, "*?");
    return filespec[pos] != '\0';
}

static void canonicalize_path1(char *buf, int buf_size, const char *path) {
    /* XXX: make it better */
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
            if ((q - file) < ssizeof(file) - 1)
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

void canonicalize_path(char *buf, int buf_size, const char *path) {
    /*@API utils
       Normalize a path, removing redundant `.`, `..` and `/` parts.
       @argument `buf` a pointer to the destination array
       @argument `buf_size` the length of the destination array in bytes
       @argument `path` a valid pointer to a string.
       @note this function accepts drive and protocol specifications.
       @note removing `..` may have adverse side effects if the parent
       directory specified is a symbolic link.
     */
    const char *p;

    /* check for URL protocol or windows drive */
    /* CG: should not skip '/' */
    /* XXX: bogus if filename contains ':' */
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

char *make_user_path(char *buf, int buf_size, const char *path) {
    /*@API utils
       Reduce a path relative to the user's homedir, using the `~`
       shell syntax.
       @argument `buf` a pointer to the destination array
       @argument `buf_size` the length of the destination array in bytes
       @argument `path` a valid pointer to a string.
       @return a pointer to the destination array.
       @note this function uses the `HOME` environment variable to
       determine the user's home directory
     */
    char *homedir = getenv("HOME");
    if (homedir) {
        int len = strlen(homedir);

        if (len && homedir[len - 1] == '/')
            len--;

        if (!memcmp(path, homedir, len) && (path[len] == '/' || path[len] == '\0')) {
            if (buf_size > 1) {
                *buf = '~';
                pstrcpy(buf + 1, buf_size - 1, path + len);
            } else {
                if (buf_size > 0)
                    *buf = '\0';
            }
            return buf;
        }
    }
    return pstrcpy(buf, buf_size, path);
}

char *reduce_filename(char *dest, int size, const char *filename)
{
    // XXX: Document this function
    const char *base = get_basename(filename);
    char *dbase, *ext, *p;

    /* Copy path unchanged */
    pstrncpy(dest, size, filename, base - filename);

    /* Strip cvs temp file prefix */
    if (base[0] == '.' && base[1] == '#' && base[2] != '\0')
        base += 2;

    pstrcat(dest, size, base);

    dbase = get_basename_nc(dest);

    /* Strip some numeric extensions (vcs version numbers) */
    for (;;) {
        /* get the last extension */
        ext = get_extension_nc(dbase);
        /* no extension */
        if (*ext != '.')
            break;
        /* keep non numeric extension */
        if (!qe_isdigit(ext[1]))
            break;
        /* keep the last extension */
        if (strchr(dbase, '.') == ext)
            break;
        /* only strip multidigit extensions */
        if (!qe_isdigit(ext[2]))
            break;
        *ext = '\0';
    }

    if (*ext == '.' && *dbase != '.' && (ext - dbase) <= 8) {
        /* This is an old KLUDGE for MS/DOS 8.3 filenames:
           Convert all upper case MS/DOS filenames with extension
           to lower case */
        for (p = dbase; *p; p++) {
            if ((*p & 0x80) || qe_islower(*p))
                break;
        }
        if (!*p && (p - dbase) <= 12) {
            qe_strtolower(dbase, dest + size - dbase, dbase);
        }
    }

    /* Strip backup file suffix or cvs temp file suffix */
    p = dbase + strlen(dbase);
    if (p > dbase + 1 && (p[-1] == '~' || p[-1] == '#'))
        *--p = '\0';

    return dest;
}

char *file_load(const char *filename, int max_size, int *sizep) {
    /*@API utils
       Load a file in memory, return allocated block and size.

       * fail if file cannot be opened for reading,
       * fail if file size is greater or equal to `max_size` (`errno` = `ERANGE`),
       * fail if memory cannot be allocated,
       * otherwise load the file contents into a block of memory,
         null terminate the block and return a pointer to allocated
         memory along with the number of bytes read.
       Error codes are returned in `errno`.
       Memory should be freed with `qe_free()`.
     */
    FILE *fp;
    long length;
    char *buf;

    /* use binary mode so ftell() returns byte offsets */
    fp = fopen(filename, "rb");
    if (!fp)
        return NULL;

    fseek(fp, 0, SEEK_END);
    length = ftell(fp);
    if (length >= max_size) {
        fclose(fp);
        errno = ERANGE;
        return NULL;
    }
    if (!(buf = qe_malloc_array(char, length + 1))) {
        fclose(fp);
        errno = ENOMEM;
        return NULL;
    }

    fseek(fp, 0, SEEK_SET);
    length = fread(buf, 1, length, fp);
    buf[length] = '\0';
    fclose(fp);
    if (sizep)
        *sizep = (int)length;
    return buf;
}

int match_extension(const char *filename, const char *extlist) {
    /*@API utils
       Return `true` iff the filename extension appears in `|` separated
       list pointed to by `extlist`.
     * Initial and final `|` do not match an empty extension, but `||` does.
     * Multiple tacked extensions may appear un extlist eg. `|tar.gz|`
     * Initial dots do not account as extension delimiters.
     * `.` and `..` do not have an empty extension, nor do they match `||`
     */
    const char *base, *p, *q;
    int len;

    if (!extlist)
        return 0;

    base = get_basename(filename);
    while (*base == '.')
        base++;
    len = strlen(base);
    if (len == 0)
        return 0;

    for (p = q = extlist;; p++) {
        int c = *p;
        if (c == '|' || c == '\0') {
            int len1 = p - q;
            if (len1 != 0 || (q != extlist && c != '\0')) {
                if (len > len1 && base[len - len1 - 1] == '.'
                &&  !qe_memicmp(base + (len - len1), q, len1)) {
                    return 1;
                }
            }
            if (c == '|')
                q = p + 1;
            else
                break;
        }
    }
    return 0;
}

int match_shell_handler(const char *p, const char *list) {
    /*@API utils
       Return `true` iff the command name invoked by the `#!` line pointed to by `p`
       matches one of the commands in `|` separated list pointed to by `list`.
     * both `#!/bin/perl` and `#!/bin/env perl` styles match list `"perl"`
     */
    const char *base;

    if (!list)
        return 0;

    if (p[0] == '#' && p[1] == '!') {
        for (p += 2; qe_isblank(*p); p++)
            continue;
        for (base = p; *p && !qe_isspace(*p); p++) {
            if (*p == '/')
                base = p + 1;
        }
        if (memfind(list, base, p - base))
            return 1;
        if (p - base == 3 && !memcmp(base, "env", 3)) {
            while (*p && *p != '\n') {
                for (; qe_isblank(*p); p++)
                    continue;
                base = p;
                for (; *p && !qe_isspace(*p); p++)
                    continue;
                if (*base != '-')
                    return memfind(list, base, p - base);
            }
        }
    }
    return 0;
}

int remove_slash(char *buf) {
    /*@API utils
       Remove the trailing slash from path, except for / directory.
       @return the updated path length.
     */
    // XXX: should we have windows specific behavior?
    // XXX: should we handle protocol prefixes specifically?
    int len = strlen(buf);
    if (len > 1 && buf[len - 1] == '/') {
        buf[--len] = '\0';
    }
    return len;
}

int append_slash(char *buf, int buf_size) {
    /*@API utils
       Append a trailing slash to a path if none there already.
       @return the updated path length.
       @note: truncation cannot be detected reliably
     */
    // XXX: should we have windows specific behavior?
    int len = strnlen(buf, buf_size);
    if (len > 0 && buf[len - 1] != '/' && len + 1 < buf_size) {
        buf[len++] = '/';
        buf[len] = '\0';
    }
    return len;
}

char *makepath(char *buf, int buf_size, const char *path, const char *filename) {
    /*@API utils
       Construct a path from a directory name and a filename into the
       array pointed to by `buf` of length `buf_size` bytes.
       @return a pointer to the destination array.
       @note: truncation cannot be detected reliably
     */
    if (buf != path)
        pstrcpy(buf, buf_size, path);
    append_slash(buf, buf_size);
    return pstrcat(buf, buf_size, filename);
}

void splitpath(char *dirname, int dirname_size,
               char *filename, int filename_size, const char *pathname)
{
    /*@API utils
       Split the path pointed to by `pathname` into a directory part and a
       filename part.
       @note `dirname` will receive an empty string if `pathname` contains
       just a filename.
     */
    size_t offset = get_basename_offset(pathname);
    if (dirname)
        pstrncpy(dirname, dirname_size, pathname, offset);
    if (filename)
        pstrcpy(filename, filename_size, pathname + offset);
}

/*---------------- Character classification functions ----------------*/

unsigned char const qe_digit_value__[128] = {
#define REPEAT16(x)  x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x
    REPEAT16(255), REPEAT16(255), REPEAT16(255),
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 255, 255, 255, 255, 255, 255,
    255, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
    25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 255, 255, 255, 255, 255,
    255, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
    25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 255, 255, 255, 255, 255,
};

int qe_skip_spaces(const char **pp) {
    /*@API utils
       Skip white space at the beginning of the string pointed to by `*pp`.
       @argument `pp` the address of a valid string pointer. The pointer will
       be updated to point after any initial white space.
       @return the character value after the white space as an `unsigned char`.
     */
    const char *p;
    unsigned char c;

    p = *pp;
    while (qe_isspace(c = *p))
        p++;
    *pp = p;
    return c;
}

int qe_strcollate(const char *s1, const char *s2) {
    /*@API utils
       Compare 2 strings using special rules:
     * use lexicographical order
     * collate sequences of digits in numerical order.
     * push `*` at the end.
     */
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
    if (c1 == '*')
        res = 1;
    else
    if (c2 == '*')
        res = -1;
    else
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

/* CG: need a local version of strcasecmp: qe_strcasecmp() */

int qe_strtobool(const char *s, int def) {
    /*@API utils
       Determine the boolean value of a response string.
       @argument `s` a possibly null pointer to a string,
       @argument `def` the default value if `s` is null or an empty string,
       @return `true` for `y`, `yes`, `t`, `true` and `1`, case independenty,
       return `false` for other non empty contents and return the default
       value `def` otherwise.
     */
    if (s && *s) {
        return strxfind("1|y|yes|t|true", s) ? 1 : 0;
    } else {
        return def;
    }
}

void qe_strtolower(char *buf, int size, const char *str) {
    /*@API utils
       Convert an ASCII string to lowercase using `qe_tolower7` for
       each byte.
       @argument `buf` a valid pointer to a destination char array.
       @argument `size` the length of the destination array in bytes,
       @argument `str` a valid pointer to a string to convert.
       @note this version only handles ASCII.
     */
    // XXX: Should have a UTF-8 version that handles Unicode case conversion.
    // XXX: should return int, length of converted string?
    unsigned char c;

    if (size > 0) {
        while ((c = *str++) != '\0' && size > 1) {
            *buf++ = qe_tolower(c);
            size--;
        }
        *buf = '\0';
    }
}

int memfind(const char *list, const char *s, int len) {
    /*@API utils
       Find a string fragment in a list of words separated by `|`.
       An initial or trailing `|` do not match the empty string, but `||` does.
       @argument `list` a string of words separated by `|` characters.
       @argument `s` a valid string pointer.
       @argument `len` the number of bytes to consider in `s`.
       @return 1 if there is a match, 0 otherwise.
     */
    const char *q = list, *start;
    int i, j;

    if (q) {
        if (!len) {
            /* match the empty string against || only */
            while (*q) {
                if (*q++ == '|' && *q == '|')
                    return 1;
            }
        } else {
            while (*q) {
                for (start = q, i = 0; *q && *q++ != '|'; i++)
                    continue;
                if (i == len) {
                    for (j = 0; j < i && start[j] == s[j]; j++)
                        continue;
                    if (j == i)
                        return 1;
                }
            }
        }
    }
    return 0;
}

int strfind(const char *keytable, const char *str) {
    /*@API utils
       Find a string in a list of words separated by `|`.
       An initial or trailing `|` do not match the empty string, but `||` does.
       @argument `list` a string of words separated by `|` characters.
       @argument `str` a valid string pointer.
       @return 1 if there is a match, 0 otherwise.
     */
    return memfind(keytable, str, strlen(str));
}

int strxfind(const char *list, const char *s) {
    /*@API utils
       Find a string in a list of words separated by `|`, ignoring case
       for ASCII and skipping `-` , `_` and spaces.
       An initial or trailing `|` do not match the empty string, but `||` does.
       @argument `list` a string of words separated by `|` characters.
       @argument `s` a valid string pointer for the string to search.
       @return 1 if there is a match, 0 otherwise.
       @note: this function only handles case insensitive matching for ASCII.
     */
    const u8 *p, *q;
    u8 c1, c2;

    q = (const u8*)list;
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
        p = (const u8*)s;
        for (;;) {
            do {
                c1 = qe_toupper(*p++);
            } while (c1 == '-' || c1 == '_' || c1 == ' ');
            do {
                c2 = qe_toupper(*q++);
            } while (c2 == '-' || c2 == '_' || c2 == ' ');
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

const char *strmem(const char *str, const void *mem, int size) {
    /*@API utils
       Find a chunk of characters inside a string.
       @argument `str` a valid string pointer in which to search for matches.
       @argument `mem` a pointer to a chunk of bytes to search.
       @argument `size` the length in bytes of the chuck to search.
       @return a pointer to the first character of the match if found,
       `NULL` otherwise.
     */
    char c;
    int len;
    const char *p, *str_max, *p1 = mem;

    if (size <= 0)
        return (size < 0) ? NULL : str;

    size--;
    c = *p1++;
    if (!c) {
        /* cannot find chunk with embedded nuls */
        return NULL;
    }

    // XXX: problem if match is a suffix
    len = strlen(str);
    if (size >= len)
        return NULL;

    str_max = str + len - size;
    for (p = str; p < str_max; p++) {
        if (*p == c && !memcmp(p + 1, p1, size))
            return p;
    }
    return NULL;
}

const void *memstr(const void *buf, int size, const char *str) {
    /*@API utils
       Find a string in a chunk of memory.
       @argument `buf` a valid pointer to the block of memory in which to
       search for matches.
       @argument `size` the length in bytes of the memory block.
       @argument `str` a valid string pointer for the string to search.
       @return a pointer to the first character of the match if found,
       `NULL` otherwise.
     */
    u8 c;
    int len;
    const u8 *p, *buf_max;

    c = *str++;
    if (!c) {
        /* empty string matches start of buffer */
        return buf;
    }

    // XXX: problem if match is a suffix
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

int qe_memicmp(const void *p1, const void *p2, size_t count) {
    /*@API utils
       Perform a case independent comparison of blocks of memory.
       @argument `p1` a valid pointer to the first block.
       @argument `p2` a valid pointer to the second block.
       @argument `count` the length in bytes of the blocks to compare.
       @return `0` is the blocks compare equal, ignoring case,
       @return a negative value if the first block compares below the second,
       @return a positive value if the first block compares above the second.
       @note this version only handles ASCII.
     */
    const u8 *s1 = (const u8 *)p1;
    const u8 *s2 = (const u8 *)p2;

    for (; count-- > 0; s1++, s2++) {
        if (*s1 != *s2) {
            u8 c1 = qe_toupper(*s1);
            u8 c2 = qe_toupper(*s2);
            if (c1 != c2)
                return (c2 < c1) - (c1 < c2);
        }
    }
    return 0;
}

const char *qe_stristr(const char *s1, const char *s2) {
    /*@API utils
       Find an ASCII string in another ASCII string, ignoring case.
       @argument `s1` a valid pointer to the string in which to
       search for matches.
       @argument `s2` a valid string pointer for the string to search.
       @return a pointer to the first character of the match if found,
       `NULL` otherwise.
       @note this version only handles ASCII.
     */
    u8 c, c1, c2;
    int len;

    len = strlen(s2);
    if (!len)
        return s1;

    c = *s2++;
    len--;
    c1 = qe_toupper(c);
    c2 = qe_tolower(c);

    while ((c = *s1++) != '\0') {
        if (c == c1 || c == c2) {
            // XXX: not strictly correct as s2 might be shorter than len
            if (!qe_memicmp(s1, s2, len))
                return s1 - 1;
        }
    }
    return NULL;
}

int stristart(const char *str, const char *val, const char **ptr) {
    /*@API utils
       Test if `val` is a prefix of `str` (case independent for ASCII).
       If there is a match, a pointer to the next character after the
       match in `str` is stored into `ptr` provided `ptr` is not null.
       @param `str` valid string pointer,
       @param `val` valid string pointer to the prefix to test,
       @param `ptr` a possibly null pointer to a `const char *` to set
       to point after the prefix in `str` in there is a match.
       @return `true` if there is a match, `false` otherwise.
     */
    const char *p, *q;

    p = str;
    q = val;
    while (*q != '\0') {
        if (qe_toupper((u8)*p) != qe_toupper((u8)*q)) {
            return 0;
        }
        p++;
        q++;
    }
    if (ptr)
        *ptr = p;
    return 1;
}

int strxstart(const char *str, const char *val, const char **ptr) {
    /*@API utils
       Test if `val` is a prefix of `str` (case independent for ASCII
       and ignoring `-`, `_` and spaces).  If there is a match, a pointer
       to the next character after the match in `str` is stored into `ptr`,
       provided `ptr` is not null.
       @param `str` valid string pointer,
       @param `val` valid string pointer to the prefix to test,
       @param `ptr` a possibly null pointer to a `const char *` to set
       to point after the prefix in `str` in there is a match.
       @return `true` if there is a match, `false` otherwise.
     */
    const char *p, *q;
    p = str;
    q = val;
    while (*q != '\0') {
        if (qe_toupper((u8)*p) != qe_toupper((u8)*q)) {
            if (*q == '-' || *q == '_' || *q == ' ') {
                q++;
                continue;
            }
            if (*p == '-' || *p == '_' || *p == ' ') {
                p++;
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

int strxcmp(const char *str1, const char *str2) {
    /*@API utils
       Compare strings case independently (for ASCII), also ignoring
       spaces, dashes and underscores.
       @param `str1` a valid string pointer for the left operand.
       @param `str2` a valid string pointer for the right operand.
       @return a negative, 0 or positive value reflecting the sign
       of `str1 <=> str2`
     */
    for (;;) {
        u8 c1 = *str1;
        u8 c2 = *str2;
        int d = qe_toupper(c1) - qe_toupper(c2);
        if (d) {
            if (c2 == '-' || c2 == '_' || c2 == ' ') {
                str2++;
                continue;
            }
            if (c1 == '-' || c1 == '_' || c1 == ' ') {
                str1++;
                continue;
            }
            return d < 0 ? -1 : +1;
        }
        if (!c1)
            break;
        str1++;
        str2++;
    }
    return 0;
}

int strmatchword(const char *str, const char *val, const char **ptr) {
    /*@API utils
       Check if `val` is a word prefix of `str`. In this case, return
       `true` and store a pointer to the first character after the prefix
       in `str` into `ptr` provided `ptr` is not a null pointer.

       If `val` is not a word prefix of `str`, return `false` and leave `*ptr`
       unchanged.

       @param `str` a valid string pointer.
       @param `val` a valid string pointer for the prefix to test.
       @param `ptr` updated with a pointer past the prefix in `str` if found.
       @return `true` if there is a match, `false` otherwise.
     */
    if (strstart(str, val, &str) && !qe_isword((u8)*str)) {
        if (ptr)
            *ptr = str;
        return 1;
    }
    return 0;
}

int strmatch_pat(const char *str, const char *pat, int start) {
    /*@API utils
       Check if the pattern `pat` matches `str` or a prefix of `str`.
       Patterns use only `*` as a wildcard, to match any sequence of
       characters.
       @param `str` a valid string pointer.
       @param `pat` a valid string pointer for the pattern to test.
       @param `start` a non zero integer if the function should return
       `1` for a partial match at the start of `str.
       @return `1` if there is a match, `0` otherwise.
     */
    u8 c1, c2;
    while (*pat) {
        c1 = *pat++;
        if (c1 == '*') {
            c1 = *pat++;
            if (c1 == '\0')
                return 1;
            while ((c2 = *str++) != '\0') {
                if (c1 == c2 && strmatch_pat(str, pat, start))
                    return 1;
            }
            return 0;
        } else {
            c2 = *str++;
            if (c1 != c2)
                return 0;
        }
    }
    return start || *str == '\0';
}

int utf8_strimatch_pat(const char *str, const char *pat, int start) {
    /*@API utils
       Check if the pattern `pat` matches `str` or a prefix of `str`,
       using a case insensitive comparison.  Patterns use only `*` as
       a wildcard, to match any sequence of characters.
       Accents are also ignored by this function.
       @param `str` a valid string pointer.
       @param `pat` a valid string pointer for the pattern to test.
       @param `start` a non zero integer if the function should return
       `1` for a partial match at the start of `str.
       @return `1` if there is a match, `0` otherwise.
     */
    char32_t c1, c2;
    while (*pat) {
        c1 = *pat++;
        if (c1 & 0x80) {
            pat--;
            do {
                c1 = utf8_decode(&pat);
            } while (qe_isaccent(c1));
            c1 = qe_unaccent(c1);
        }
        if (c1 == '*') {
            c1 = *pat++;
            if (c1 & 0x80) {
                pat--;
                do {
                    c1 = utf8_decode(&pat);
                } while (qe_isaccent(c1));
                c1 = qe_unaccent(c1);
            }
            if (c1 == '\0')
                return 1;
            while ((c2 = *str++) != '\0') {
                if (c2 & 0x80) {
                    str--;
                    do {
                        c2 = utf8_decode(&str);
                    } while (qe_isaccent(c2));
                    c2 = qe_unaccent(c2);
                }
                if ((c1 == c2 || qe_wtoupper(c1) == qe_wtoupper(c2))
                &&  utf8_strimatch_pat(str, pat, start))
                    return 1;
            }
            return 0;
        } else {
            c2 = *str++;
            if (c2 & 0x80) {
                str--;
                do {
                    c2 = utf8_decode(&str);
                } while (qe_isaccent(c2));
                c2 = qe_unaccent(c2);
            }
            if (c1 != c2 && qe_wtoupper(c1) != qe_wtoupper(c2))
                return 0;
        }
    }
    return start || *str == '\0';
}

/* used in libqhtml */
int get_str(const char **pp, char *buf, int buf_size, const char *stop) {
    /*@API utils
       Get a token from a string, stop on a set of characters and white-space.
       Skip spaces before and after the token. Return the token length.

       @param `pp` the address of a valid pointer to the current position
       in the source string
       @param `buf` a pointer to a destination array.
       @param `buf_size` the length of the destination array.
       @param `stop` a valid string pointer containing separator characters.
       @return the length of the token stored into buf.
       @note: token truncation cannot be easily detected.
     */
    const char *p;
    int i;

    qe_skip_spaces(pp);
    p = *pp;
    for (i = 0;;) {
        u8 c = *p;
        /* Stop on spaces and eat them */
        if (qe_isspace(c) || strchr(stop, c))
            break;
        if (i + 1 < buf_size)
            buf[i++] = c;
        p++;
    }
    if (i < buf_size)
        buf[i] = '\0';
    *pp = p;
    qe_skip_spaces(pp);
    return i;
}

/*---- Unicode string functions: null terminated arrays of code points ----*/

int ustrstart(const char32_t *str0, const char *val, int *lenp) {
    /*@API utils
       Test if `val` is a prefix of `str0`.

       If `val` is a prefix of `str`, the length of the prefix is stored into
       `*lenp`, provided `lenp` is not a null pointer, and return `1`.

       If `val` is not a prefix of `str`, return `0` and leave `*lenp`
       unchanged.

       @param `str0` input string, must be a valid pointer to a null terminated code point array.
       @param `val` prefix string, must be a valid string pointer.
       @param `lenp` updated with the length of the prefix if found.
       @return `true` if there is a match, `false` otherwise.
     */
    const char32_t *str = str0;

    for (; *val != '\0'; val++, str++) {
        /* assuming val is ASCII or Latin1 */
        if (*str != (u8)*val)
            return 0;
    }
    if (lenp)
        *lenp = str - str0;
    return 1;
}

const char32_t *ustrstr(const char32_t *str, const char *val) {
    /*@API utils
       Find a string of characters inside a string of code points.
       @argument `str` a valid wide string pointer in which to search for matches.
       @argument `val` a valid string pointer to a subtring to search for.
       @return a pointer to the first code point of the match if found,
       `NULL` otherwise.
     */
    char32_t c = val[0];

    for (; *str != '\0'; str++) {
        if (*str == c && ustrstart(str, val, NULL))
            return str;
    }
    return NULL;
}

int ustristart(const char32_t *str0, const char *val, int *lenp) {
    /*@API utils
       Test if `val` is a prefix of `str0`. Comparison is perform ignoring case.

       If `val` is a prefix of `str`, the length of the prefix is stored into
       `*lenp`, provided `lenp` is not a null pointer, and return `1`.

       If `val` is not a prefix of `str`, return `0` and leave `*lenp`
       unchanged.

       @param `str0` input string, must be a valid pointer to a null terminated code point array.
       @param `val` prefix string, must be a valid string pointer.
       @param `lenp` updated with the length of the prefix if found.
       @return `true` if there is a match, `false` otherwise.
       @note: val is assumed to be contain ASCII only.
     */
    const char32_t *str = str0;

    for (; *val != '\0'; val++, str++) {
        /* assuming val is ASCII or Latin1 */
        if (qe_toupper(*str) != qe_toupper((u8)*val))
            return 0;
    }
    if (lenp)
        *lenp = str - str0;
    return 1;
}

const char32_t *ustristr(const char32_t *str, const char *val) {
    /*@API utils
       Find a string of characters inside a string of code points ignoring case.
       @argument `str` a valid wide string pointer in which to search for matches.
       @argument `val` a valid string pointer to a subtring to search for.
       @return a pointer to the first code point of the match if found,
       `NULL` otherwise.
       @note: val is assumed to be contain ASCII only.
     */
    char32_t c = qe_toupper((u8)val[0]);

    for (; *str != '\0'; str++) {
        if (qe_toupper(*str) == c && ustristart(str, val, NULL))
            return str;
    }
    return NULL;
}

int umemcmp(const char32_t *s1, const char32_t *s2, size_t count) {
    /*@API utils
       Compare two blocks of code points and return an integer indicative of
       their relative order.
       @argument `s1` a valid wide string pointer.
       @argument `s2` a valid wide string pointer.
       @argument `count` the maximum number of code points to compare.
       @return `0` if the strings compare equal, a negative value if `s1` is
       lexicographically before `s2` and a positive number otherwise.
     */
    for (; count-- > 0; s1++, s2++) {
        if (*s1 != *s2) {
            return *s1 < *s2 ? -1 : 1;
        }
    }
    return 0;
}

int ustr_get_identifier(char *buf, int buf_size, char32_t c,
                        const char32_t *str, int i, int n)
{
    /*@API utils
       Extract an ASCII identifier from a wide string into a char array.
       @argument `buf` a valid pointer to a destination array.
       @argument `buf_size` the length of the destination array.
       @argument `c` the first code point to copy.
       @argument `str` a valid wide string pointer.
       @argument `i` the offset of the first code point to copy.
       @argument `n` the offset to the end of the wide string.
       @return the length of the identifier present in the source string.
       @note: the return value can be larger than the destination array length.
       In this case, the destination array contains a truncated string, null
       terminated unless buf_size is <= 0.
     */
    int len = 0, j;

    if (len + 1 < buf_size) {
        /* c is assumed to be an ASCII character */
        buf[len++] = c;
    }
    for (j = i; j < n; j++) {
        c = str[j];
        if (!qe_isalnum_(c))
            break;
        if (len + 1 < buf_size)
            buf[len++] = c;
    }
    if (len < buf_size) {
        buf[len] = '\0';
    }
    return j - i;
}

int ustr_get_identifier_lc(char *buf, int buf_size, char32_t c,
                           const char32_t *str, int i, int n)
{
    /*@API utils
       Extract an ASCII identifier from a wide string into a char array and
       convert it to lowercase.
       @argument `buf` a valid pointer to a destination array.
       @argument `buf_size` the length of the destination array.
       @argument `c` the first code point to copy.
       @argument `str` a valid wide string pointer.
       @argument `i` the offset of the first code point to copy.
       @argument `n` the offset to the end of the wide string.
       @return the length of the identifier present in the source string.
       @note: the return value can be larger than the destination array length.
       In this case, the destination array contains a truncated string, null
       terminated unless buf_size is <= 0.
     */
    int len = 0, j;

    if (len < buf_size) {
        /* c is assumed to be an ASCII character */
        buf[len++] = qe_tolower(c);
    }
    for (j = i; j < n; j++) {
        c = str[j];
        if (!qe_isalnum_(c))
            break;
        if (len < buf_size - 1)
            buf[len++] = qe_tolower(c);
    }
    if (len < buf_size) {
        buf[len] = '\0';
    }
    return j - i;
}

int utf8_get_word(char *buf, int buf_size, char32_t c,
                  const char32_t *str, int i, int n)
{
    /*@API utils
       Extract a word from a wide string into a char array.
       Non ASCII code points are UTF-8 encoded.
       @argument `buf` a valid pointer to a destination array.
       @argument `buf_size` the length of the destination array.
       @argument `c` the first code point to copy.
       @argument `str` a valid wide string pointer.
       @argument `i` the offset of the first code point to copy.
       @argument `n` the offset to the end of the wide string.
       @return the length of the identifier present in the source string.
       @note: the return value can be larger than the destination array length.
       In this case, the destination array contains a truncated string, null
       terminated unless buf_size is <= 0.
     */
    buf_t outbuf, *out;
    int j;

    out = buf_init(&outbuf, buf, buf_size);

    buf_putc_utf8(out, c);
    for (j = i; j < n; j++) {
        c = str[j];
        if (!qe_isword(c))
            break;
        buf_putc_utf8(out, c);
    }
    return j - i;
}

int ustr_match_keyword(const char32_t *buf, const char *str, int *lenp) {
    int i = 0;
    while (str[i]) {
        if (*buf++ != (u8)str[i++])
            return 0;
    }
    if (qe_isalnum_(*buf))
        return 0;
    if (lenp)
        *lenp = i;
    return 1;
}

/*---------------- Functions for handling keys ----------------*/

/* should move to a separate module */
static unsigned short const keycodes[] = {
    KEY_SPC, KEY_DEL, KEY_RET, KEY_LF, KEY_ESC, KEY_TAB, KEY_SHIFT_TAB,
    KEY_CTRL(' '), KEY_CTRL('@'), KEY_DEL, KEY_CTRL('\\'),
    KEY_CTRL(']'), KEY_CTRL('^'), KEY_CTRL('_'), KEY_CTRL('_'),
    KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN,
    KEY_HOME, KEY_END, KEY_PAGEUP, KEY_PAGEDOWN,
    KEY_CTRL_LEFT, KEY_CTRL_RIGHT, KEY_CTRL_UP, KEY_CTRL_DOWN,
    KEY_CTRL_HOME, KEY_CTRL_END, KEY_CTRL_PAGEUP, KEY_CTRL_PAGEDOWN,
    KEY_SHIFT_LEFT, KEY_SHIFT_RIGHT, KEY_SHIFT_UP, KEY_SHIFT_DOWN,
    KEY_SHIFT_HOME, KEY_SHIFT_END, KEY_SHIFT_PAGEUP, KEY_SHIFT_PAGEDOWN,
    KEY_CTRL_SHIFT_LEFT, KEY_CTRL_SHIFT_RIGHT, KEY_CTRL_SHIFT_UP, KEY_CTRL_SHIFT_DOWN,
    KEY_CTRL_SHIFT_HOME, KEY_CTRL_SHIFT_END, KEY_CTRL_SHIFT_PAGEUP, KEY_CTRL_SHIFT_PAGEDOWN,
    KEY_PAGEUP, KEY_PAGEDOWN, KEY_INSERT, KEY_DELETE,
    KEY_DEFAULT, KEY_NONE, KEY_UNKNOWN,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5,
    KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10,
    KEY_F11, KEY_F12, KEY_F13, KEY_F14, KEY_F15,
    KEY_F16, KEY_F17, KEY_F18, KEY_F19, KEY_F20,
    '{', '}', '|',
};

static const char * const keystr[countof(keycodes)] = {
    "SPC", "DEL", "RET", "LF", "ESC", "TAB", "S-TAB",
    "C-SPC", "C-@", "C-?", "C-\\", "C-]", "C-^", "C-_", "C-/",
    "left", "right", "up", "down",
    "home", "end", "pageup", "pagedown",
    "C-left", "C-right", "C-up", "C-down",
    "C-home", "C-end", "C-pageup", "C-pagedown",
    "S-left", "S-right", "S-up", "S-down",
    "S-home", "S-end", "S-pageup", "S-pagedown",
    "C-S-left", "C-S-right", "C-S-up", "C-S-down",
    "C-S-home", "C-S-end", "C-S-pageup", "C-S-pagedown",
    "prior", "next", "insert", "delete",
    "default", "none", "unknown",
    "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10",
    "f11", "f12", "f13", "f14", "f15", "f16", "f17", "f18", "f19", "f20",
    "LB", "RB", "VB",
};

int compose_keys(unsigned int *keys, int *nb_keys)
{
    unsigned int *keyp;

    if (*nb_keys < 2)
        return 0;

    /* compose KEY_ESC as META prefix */
    keyp = keys + *nb_keys - 2;
    if (keyp[0] == KEY_ESC && keyp[1] != KEY_ESC) {
        if (keyp[1] <= 0xff || KEY_IS_ESC1(keyp[1])) {
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
    for (p1 = p; *p1 && *p1 != ' ' && !(*p1 == ',' && p1[1] == ' '); p1++)
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
        *pp = p1;
    } else {
        key = utf8_decode(&p);
        // XXX: Should assume unknown function key if p != p1 */
        *pp = p;
    }
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
        // XXX: this only works for ASCII control keys, not for function keys
        /* Should pass buffer with C-xxx to strtokey1 */
        p += 4;
        key = KEY_META(KEY_CTRL(strtokey1(&p)));
    } else {
        key = strtokey1(&p);
    }
    *pp = p;
    return key;
}

int strtokeys(const char *str, unsigned int *keys,
              int max_keys, const char **endp) {
    int key, nb_keys;
    const char *p;

    p = str;
    nb_keys = 0;

    while (qe_skip_spaces(&p)) {
        key = strtokey(&p);
        keys[nb_keys++] = key;
        compose_keys(keys, &nb_keys);
        if (*p == ',' && p[1] == ' ') {
            p += 2;
            break;
        }
        if (nb_keys >= max_keys)
            break;
    }
    if (endp)
        *endp = p;
    return nb_keys;
}

/* Convert a key to a string representation. Recurse at most once */
int buf_put_key(buf_t *out, int key)
{
    int i, start = out->len;

    for (i = 0; i < countof(keycodes); i++) {
        if (keycodes[i] == key) {
            return buf_puts(out, keystr[i]);
        }
    }
    if (key >= KEY_META(0) && key <= KEY_META(0xff)) {
        buf_puts(out, "M-");
        buf_put_key(out, key & 0xff);
    } else
    if (key >= KEY_META(KEY_ESC1(0)) && key <= KEY_META(KEY_ESC1(0xff))) {
        buf_puts(out, "M-");
        buf_put_key(out, KEY_ESC1(key & 0xff));
    } else
    if (key >= KEY_CTRL('a') && key <= KEY_CTRL('z')) {
        buf_printf(out, "C-%c", key + 'a' - 1);
    } else {
        buf_putc_utf8(out, key);
    }
    return out->len - start;
}

int buf_put_keys(buf_t *out, unsigned int *keys, int nb_keys)
{
    int i, start = out->len;

    for (i = 0; i < nb_keys; i++) {
        if (i != 0)
            buf_put_byte(out, ' ');
        buf_put_key(out, keys[i]);
    }
    return out->len - start;
}

/*---- StringArray functions ----*/

/* set one string. */
StringItem *set_string(StringArray *cs, int index, const char *str, int group)
{
    StringItem *v;
    int len;

    // XXX: negative index from the end?
    if (!cs || index < 0 || index >= cs->nb_items)
        return NULL;

    len = strlen(str);
    v = qe_malloc_hack(StringItem, len);
    if (!v)
        return NULL;
    v->selected = 0;
    v->group = group;
    memcpy(v->str, str, len + 1);
    if (cs->items[index])
        qe_free(&cs->items[index]);
    cs->items[index] = v;
    return v;
}

/* make a generic array alloc */
StringItem *add_string(StringArray *cs, const char *str, int group) {
    if (!cs)
        return NULL;
    if (cs->nb_items >= cs->nb_allocated) {
        int n = cs->nb_allocated + 32;
        if (!qe_realloc(&cs->items, n * sizeof(StringItem *)))
            return NULL;
        cs->nb_allocated = n;
    }
    cs->items[cs->nb_items++] = NULL;
    return set_string(cs, cs->nb_items - 1, str, group);
}

int remove_string(StringArray *cs, const char *str) {
    int i, j, count = 0;
    if (cs) {
        for (i = j = 0; i < cs->nb_items; i++) {
            if (cs->items[i] && strcmp(cs->items[i]->str, str)) {
                cs->items[j++] = cs->items[i];
            } else {
                qe_free(&cs->items[i]);
                count++;
            }
        }
        cs->nb_items = j;
    }
    return count;
}

void free_strings(StringArray *cs) {
    int i;
    for (i = 0; i < cs->nb_items; i++)
        qe_free(&cs->items[i]);
    qe_free(&cs->items);
    memset(cs, 0, sizeof(StringArray));
}

/*---- Dynamic buffers with static allocation ----*/

int buf_write(buf_t *bp, const void *src, int size)
{
    int n = 0;

    if (bp->pos < bp->size) {
        n = bp->size - bp->pos - 1;
        if (n > size)
            n = size;
        memcpy(bp->buf + bp->len, src, n);
        bp->len += n;
        bp->buf[bp->len] = '\0';
    }
    bp->pos += size;
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

int buf_putc_utf8(buf_t *bp, char32_t c)
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
        /* avoid appending a partial UTF-8 sequence */
        if (bp->pos + len < bp->size) {
            memcpy(bp->buf + bp->len, buf, len);
            bp->pos += len;
            bp->len += len;
            bp->buf[bp->len] = '\0';
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
    buf_t outbuf, *out;

    out = buf_init(&outbuf, buf, buf_size);

    p = from;
    while ((q = strstr(p, s1)) != NULL) {
        buf_write(out, p, q - p);
        buf_puts(out, s2);
        p = q + strlen(s1);
    }
    buf_puts(out, p);

    return out->pos;
}

int byte_quote(char *dest, int size, unsigned char c) {
    buf_t buf[1];
    buf_init(buf, dest, size);
    return buf_encode_byte(buf, c);
}

int strquote(char *dest, int size, const char *str, int len) {
    buf_t out[1];
    buf_init(out, dest, size);
    if (str) {
        int i;
        if (len < 0)
            len = strlen(str);
        buf_put_byte(out, '"');
        for (i = 0; i < len; i++)
            buf_encode_byte(out, str[i]);
        buf_put_byte(out, '"');
        return out->pos;
    } else {
        return buf_puts(out, "null");
    }
}

#if 0
/* TODO */
int strunquote(char *dest, int size, const char *str, int len)
{
}
#endif

int buf_encode_byte(buf_t *out, unsigned char ch) {
    int c;
    if (((void)(c = 'n'), ch == '\n')
    ||  ((void)(c = 'r'), ch == '\r')
    ||  ((void)(c = 't'), ch == '\t')
    ||  ((void)(c = 'f'), ch == '\f')
    ||  ((void)(c = 'b'), ch == '\010')
    ||  ((void)(c = 'E'), ch == '\033')
    ||  ((void)(c = '\''), ch == '\'')      // XXX: need flag to make this optional
    ||  ((void)(c = '\"'), ch == '\"')      // XXX: need flag to make this optional
    ||  ((void)(c = '\\'), ch == '\\')) {
        return buf_printf(out, "\\%c", c);
    } else
    if (ch < 32) {
        //if (*p == '\e' && col > 9) {
        //    eb_write(b, b->total_size, "\n         ", 10);
        //    col = 9;
        //}
        return buf_printf(out, "\\^%c", (ch + '@') & 127);      // XXX: need flag to make this optional
    } else
    if (ch < 127) {
        return buf_put_byte(out, ch);
    } else {
        return buf_printf(out, "\\0x%02X", ch);      // XXX: need flag to make this optional
    }
}

/*---------------- allocation routines ----------------*/

void *qe_malloc_bytes(size_t size) {
    /*@API memory
       Allocate an uninitialized block of memory of a given size in
       bytes.
       @argument `size` the number of bytes to allocate.
       @return a pointer to allocated memory, aligned on the maximum
       alignment size.
     */
    return (malloc)(size);
}

void *qe_mallocz_bytes(size_t size) {
    /*@API memory
       Allocate a block of memory of a given size in bytes initialized
       to all bits zero.
       @argument `size` the number of bytes to allocate.
       @return a pointer to allocated memory, aligned on the maximum
       alignment size.
     */
    void *p = (malloc)(size);
    if (p)
        memset(p, 0, size);
    return p;
}

void *qe_malloc_dup(const void *src, size_t size) {
    /*@API memory
       Allocate a block of memory of a given size in bytes initialized
       as a copy of an existing object.
       @argument `src` a valid pointer to the object to duplicate.
       @argument `size` the number of bytes to allocate.
       @return a pointer to allocated memory, aligned on the maximum
       alignment size.
     */
    void *p = (malloc)(size);
    if (p)
        memcpy(p, src, size);
    return p;
}

char *qe_strdup(const char *str) {
    /*@API memory
       Allocate a copy of a string.
       @argument `src` a valid pointer to a string to duplicate.
       @return a pointer to allocated memory, aligned on the maximum
       alignment size.
     */
    size_t size = strlen(str) + 1;
    char *p = (malloc)(size);

    if (p)
        memcpy(p, str, size);
    return p;
}

void *qe_realloc(void *pp, size_t size) {
    /*@API memory
       reallocate a block of memory to a different size.
       @argument `pp` the address of a possibly null pointer to a
       block to reallocate. `pp` is updated with the new pointer
       if reallocation is successful.
       @argument `size` the new size for the object.
       @return a pointer to allocated memory, aligned on the maximum
       alignment size.
       @note this API makes it easier to check for success separately
       from modifying the existing pointer, which is unchanged if
       reallocation fails. This approach is not strictly conforming,
       it assumes all pointers have the same size and representation,
       which is mandated by POSIX.
       We use memcpy to avoid compiler optimisation issues with the
       syntax `*(void **)pp = p;` that violates the strict aliasing rule.
     */
    void *p;
    memcpy(&p, pp, sizeof(p));
    p = (realloc)(p, size);
    if (p || !size)
        memcpy(pp, &p, sizeof(p));
    return p;
}

/*---------------- bounded strings ----------------*/

/* get the n-th string from a `|` separated list */
bstr_t bstr_get_nth(const char *s, int n) {
    bstr_t bs;

    for (bs.s = s;; s++) {
        if (*s == '\0' || *s == '|') {
            if (n-- == 0) {
                bs.len = s - bs.s;
                break;
            }
            if (*s) {
                bs.s = s + 1;
            } else {
                bs.len = 0;
                bs.s = NULL;
                break;
            }
        }
    }
    return bs;
}

/* get the first string from a list and push pointer */
bstr_t bstr_token(const char *s, int sep, const char **pp) {
    bstr_t bs = { s, 0 };

    if (s) {
        /* XXX: should special case spaces? */
        for (; *s != '\0' && *s != sep; s++)
            continue;

        bs.len = s - bs.s;
        if (pp) {
            *pp = *s ? s + 1 : NULL;
        }
    }
    return bs;
}

/*---------------- qe_qsort_r ----------------*/

/* Our own implementation of qsort_r() since it is not available
 * on some targets, such as OpenBSD.
 */

typedef void (*exchange_f)(void *a, void *b, size_t size);

static void exchange_bytes(void *a, void *b, size_t size) {
    unsigned char t;
    unsigned char *ac = (unsigned char *)a;
    unsigned char *bc = (unsigned char *)b;

    while (size-- > 0) {
        t = *ac;
        *ac++ = *bc;
        *bc++ = t;
    }
}

static void exchange_ints(void *a, void *b, size_t size) {
    int *ai = (int *)a;
    int *bi = (int *)b;

    for (size /= sizeof(int); size-- != 0;) {
        int t = *ai;
        *ai++ = *bi;
        *bi++ = t;
    }
}

static void exchange_one_int(void *a, void *b, size_t size) {
    int *ai = (int *)a;
    int *bi = (int *)b;
    int t = *ai;
    *ai = *bi;
    *bi = t;
}

#if LONG_MAX != INT_MAX
static void exchange_longs(void *a, void *b, size_t size) {
    long *ai = (long *)a;
    long *bi = (long *)b;

    for (size /= sizeof(long); size-- != 0;) {
        long t = *ai;
        *ai++ = *bi;
        *bi++ = t;
    }
}

static void exchange_one_long(void *a, void *b, size_t size) {
    long *ai = (long *)a;
    long *bi = (long *)b;
    long t = *ai;
    *ai = *bi;
    *bi = t;
}
#endif

static inline exchange_f exchange_func(void *base, size_t size) {
    exchange_f exchange = exchange_bytes;
#if LONG_MAX != INT_MAX
    if ((((uintptr_t)base | (uintptr_t)size) & (sizeof(long) - 1)) == 0) {
        exchange = exchange_longs;
        if (size == sizeof(long))
            exchange = exchange_one_long;
    } else
#endif
    if ((((uintptr_t)base | (uintptr_t)size) & (sizeof(int) - 1)) == 0) {
        exchange = exchange_ints;
        if (size == sizeof(int))
            exchange = exchange_one_int;
    }
    return exchange;
}

static inline void *med3_r(void *a, void *b, void *c, void *thunk,
                           int (*compare)(void *, const void *, const void *))
{
    return compare(thunk, a, b) < 0 ?
        (compare(thunk, b, c) < 0 ? b : (compare(thunk, a, c) < 0 ? c : a )) :
        (compare(thunk, b, c) > 0 ? b : (compare(thunk, a, c) < 0 ? a : c ));
}

#define MAXSTACK 64

void qe_qsort_r(void *base, size_t nmemb, size_t size, void *thunk,
                int (*compare)(void *, const void *, const void *))
{
    /*@API utils
       Sort an array using a comparison function with an extra opaque
       argument.
       @argument `base` a valid pointer to an array of objects,
       @argument `nmemb` the number of elements in the array,
       @argument `size` the object size in bytes,
       @argument `thunk` the generic argument to pass to the comparison
       function,
       @argument `compare` a function pointer for a comparison function
       taking 3 arguments: the `thunk` argument and pointers to 2
       objects from the array, returning an integer whose sign indicates
       their relative position according to the sort order.
       @note this function behaves like OpenBSD's `qsort_r()`, the
       implementation is non recursive using a combination of quicksort
       and insertion sort for small chunks. The GNU lib C on linux also
       has a function `qsort_r()` with similar semantics but a different
       calling convention.
     */
    struct {
        unsigned char *base;
        size_t count;
    } stack[MAXSTACK], *sp;
    size_t m0, n;
    unsigned char *lb, *m, *i, *j;
    exchange_f exchange = exchange_func(base, size);

    if (nmemb < 2 || size <= 0)
        return;

    sp = stack;
    sp->base = base;
    sp->count = nmemb;
    sp++;
    while (sp-- > stack) {
        lb = sp->base;
        n = sp->count;

        while (n >= 7) {
            /* partition into two segments */
            i = lb + size;
            j = lb + (n - 1) * size;
            /* select pivot and exchange with 1st element */
            m0 = (n >> 2) * size;
            /* should use median of 3 or 9 */
            m = med3_r(lb + m0, lb + 2 * m0, lb + 3 * m0, thunk, compare);

            exchange(lb, m, size);

            m0 = n - 1;  /* m is the offset of j */
            for (;;) {
                while (i < j && compare(thunk, lb, i) > 0) {
                    i += size;
                }
                while (j >= i && compare(thunk, j, lb) > 0) {
                    j -= size;
                    m0--;
                }
                if (i >= j)
                    break;
                exchange(i, j, size);
                i += size;
                j -= size;
                m0--;
            }

            /* pivot belongs in A[j] */
            exchange(lb, j, size);

            /* keep processing smallest segment,
            * and stack largest */
            n = n - m0 - 1;
            if (m0 < n) {
                if (n > 1) {
                    sp->base = j + size;
                    sp->count = n;
                    sp++;
                }
                n = m0;
            } else {
                if (m0 > 1) {
                    sp->base = lb;
                    sp->count = m0;
                    sp++;
                }
                lb = j + size;
            }
        }
        /* Use insertion sort for small fragments */
        for (i = lb + size, j = lb + n * size; i < j; i += size) {
			for (m = i; m > lb && compare(thunk, m - size, m) > 0; m -= size)
                exchange(m, m - size, size);
        }
    }
}

/*---- Unicode and UTF-8 support ----*/

/* Compute tty width of unicode characters.  This is a modified
 * implementation of wcwidth() from Markus Kuhn. We handle most
 * non spacing and enclosing combining characters.
 * file generated by unicode_gen.c, defines:
 * int qe_wcwidth(char32_t ucs);
 * int qe_wcwidth_variant(char32_t ucs);
 */
#include "wcwidth.c"

#ifdef CONFIG_TINY
char32_t qe_unaccent(char32_t c) { return c; }
char32_t qe_wctolower(char32_t c) { return qe_tolower(c); }
char32_t qe_wctoupper(char32_t c) { return qe_toupper(c); }
#endif

/* UTF-8 specific tables */

#define REP2(x)    x, x
#define REP4(x)    x, x, x, x
#define REP8(x)    REP4(x), REP4(x)
#define REP16(x)   REP4(x), REP4(x), REP4(x), REP4(x)
#define REP32(x)   REP16(x), REP16(x)
#define REP64(x)   REP16(x), REP16(x), REP16(x), REP16(x)
#define REP128(x)  REP64(x), REP64(x)
#define REP256(x)  REP64(x), REP64(x), REP64(x), REP64(x)

unsigned char const utf8_length[256] = {
    REP128(1),  /* [0x00...0x7F] are self-encoding ASCII bytes */
    REP64(1),   /* [0x80...0xBF] are invalid prefix bytes, could use 0 */
    /* 0xC0 and 0xC1 are invalid leading bytes for strict parsing */
    REP32(2),   /* [0xC0...0xDF] leading bytes of 2 byte sequences */
    REP16(3),   /* [0xE0...0xEF] leading bytes of 3 byte sequences */
    REP8(4),    /* [0xF0...0xF7] leading bytes of 4 byte sequences */
    REP4(5),    /* [0xF8...0xFB] leading bytes of 5 byte sequences */
    REP2(6),    /* [0xFC...0xFD] leading bytes of 6 byte sequences */
    1,          /* 0xFE is invalid in UTF-8 encoding */
    1,          /* 0xFF is invalid in UTF-8 encoding */
};

static char32_t const utf8_min_code[7] = {
    0, 0, 0x80, 0x800, 0x10000, 0x00200000, 0x04000000,
};

char32_t utf8_decode_strict(const char **pp) {
    /*@API utils
       Return the UTF-8 encoded code point at `*pp` and increment `*pp`
       to point to the next code point.
       Strict decoding is performed, any encoding error returns INVALID_CHAR:
       - invalid lead bytes 0x80..0xC1, 0xF8..0xFF
       - overlong encodings
       - low and high surrogate codes
       - special codes 0xfffe and 0xffff
       - code points beyond CHARCODE_MAX
     */
    char32_t c, c1;
    const u8 *p;

    p = (const u8 *)*pp;
    c = *p++;
    if (c < 128) {
        /* fast case for ASCII */
    } else {
        int i, len = utf8_length[c];
        if (len < 2)
            goto fail; /* can only be multi byte code here */
        c &= (0x1f << 2) >> len;
        for (i = 1;;) {
            c1 = *p ^ 0x80;
            if (c1 > 0x3f)
                goto fail;
            p++;
            c = (c << 6) + c1;
            if (++i == len)
                break;
        }
        /* exclude non canonical sequences */
        // min bit length: 8, 6+6, 5+6+6, 4+6+6+6, 3+6+6+6+6, 2+6+6+6+6+6
        //                 7+5*(n-2)
        // if (c < 0x80U << (5 * (len - 2)))
        if (c < utf8_min_code[len])
            goto fail;
        /* exclude surrogate pairs */
        if (c >= 0xd800 && c <= 0xdfff)
            goto fail;
        /* exclude special codes and invalid code points */
        if (c == 0xfffe || c == 0xffff || c > 0x10ffff)
            goto fail;
    }
    *pp = (const char *)p;
    return c;
 fail:
    *pp = (const char *)p;
#define INVALID_CHAR 0xfffd
    return INVALID_CHAR;
}

static unsigned char const utf8x_length[0x100 - 0xC0] = {
    REP32(2),   /* [0xC0...0xDF] leading bytes of 2 byte sequences */
    REP16(3),   /* [0xE0...0xEF] leading bytes of 3 byte sequences */
    REP8(4),    /* [0xF0...0xF7] leading bytes of 4 byte sequences */
    REP4(5),    /* [0xF8...0xFB] leading bytes of 5 byte sequences */
    REP2(6),    /* [0xFC...0xFD] leading bytes of 6 byte sequences */
    REP2(6),    /* [0xFE...0xFF] leading bytes of 6 byte sequences */
};

#define RUN2(x)    (x)+0, (x)+1
#define RUN4(x)    (x)+0, (x)+1, (x)+2, (x)+3
#define RUN8(x)    RUN4(x), RUN4((x)+4)
#define RUN16(x)   RUN4(x), RUN4((x)+4), RUN4((x)+8), RUN4((x)+12)
#define RUN32(x)   RUN16(x), RUN16((x)+16)

static unsigned char const utf8x_bits[0x100 - 0xC0] = {
    RUN32(0),   /* [0xC0...0xDF] leading bytes of 2 byte sequences */
    RUN16(0),   /* [0xE0...0xEF] leading bytes of 3 byte sequences */
    RUN8(0),    /* [0xF0...0xF7] leading bytes of 4 byte sequences */
    RUN4(0),    /* [0xF8...0xFB] leading bytes of 5 byte sequences */
    RUN2(0),    /* [0xFC...0xFD] leading bytes of 6 byte sequences */
    RUN2(2),    /* [0xFE...0xFF] leading bytes of 6 byte sequences */
};

char32_t utf8_decode(const char **pp) {
    /*@API utils
       Return the UTF-8 encoded code point at `*pp` and increment `*pp`
       to point to the next code point.
       Lax decoding is performed:
       - stray trailing bytes 0x80..0xBF return a single byte
       - overlong encodings, surrogates and special codes are accepted
       - 32-bit codes are produced by 0xFE and 0xFF lead bytes if followed
       by 5 trailing bytes
     */
    const u8 *p = (const u8 *)*pp;
    char32_t c = *p++;
    if (c < 0xC0) {
        /* fast case for ASCII and trailing bytes */
    } else {
        int i = 1;
        int len = utf8x_length[c - 0xC0];
        c = utf8x_bits[c - 0xC0];
        for (;;) {
            char32_t c1 = *p++ ^ 0x80;
            if (c1 > 0x3f) {
                p -= i;
                c = p[-1];
                break;
            }
            c = (c << 6) + c1;
            if (++i == len)
                break;
        }
    }
    *pp = (const char *)p;
    return c;
}

int utf8_encode(char *q0, char32_t c) {
    /* note: the buffer must be at least 6 bytes long. Return number of
     * bytes copied. */
    char *q = q0;

    if (c < 0x80) {
        *q++ = c;
    } else {
        if (c < 0x800) {
            *q++ = (c >> 6) | 0xc0;
        } else {
            if (c < 0x10000) {
                *q++ = (c >> 12) | 0xe0;
            } else {
                if (c < 0x00200000) {
                    *q++ = (c >> 18) | 0xf0;
                } else {
                    if (c < 0x04000000) {
                        *q++ = (c >> 24) | 0xf8;
                    } else {
                        *q++ = (c >> 30) | 0xfc;
                        *q++ = ((c >> 24) & 0x3f) | 0x80;
                    }
                    *q++ = ((c >> 18) & 0x3f) | 0x80;
                }
                *q++ = ((c >> 12) & 0x3f) | 0x80;
            }
            *q++ = ((c >> 6) & 0x3f) | 0x80;
        }
        *q++ = (c & 0x3f) | 0x80;
    }
    return q - q0;
}

int utf8_to_char32(char32_t *dest, int dest_length, const char *str)
{
    const char *p;
    int pos;

    if (dest_length <= 0)
        return 0;

    p = str;
    for (pos = 0; pos + 1 < dest_length; pos++) {
        char32_t c = utf8_decode(&p);
        if (c == '\0')
            break;
        dest[pos] = c;
    }
    dest[pos] = 0;
    return pos;
}

int char32_to_utf8(char *dest, int dest_length, const char32_t *src, int src_length)
{
    buf_t outbuf, *out;
    int i;

    out = buf_init(&outbuf, dest, dest_length);

    for (i = 0; i < src_length; i++)
        buf_putc_utf8(out, src[i]);

    return out->pos;
}
