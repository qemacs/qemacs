/*
 * Utilities for qemacs.
 * Copyright (c) 2001 Fabrice Bellard.
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

/* XXX: not suffisant, but OK for basic operations */
int fnmatch(const char *pattern, const char *string, int flags)
{
    if (pattern[0] == '*')
        return 0;
    else
        return strcmp(pattern, string) != 0;
}

#else
#include <fnmatch.h>
#endif

struct FindFileState {
    char path[1024];
    char dirpath[1024]; /* current dir path */
    char pattern[1024]; /* search pattern */
    const char *bufptr;
    DIR *dir;
};

FindFileState *find_file_open(const char *path, const char *pattern)
{
    FindFileState *s;

    s = malloc(sizeof(FindFileState));
    if (!s)
        return NULL;
    pstrcpy(s->path, sizeof(s->path), path);
    pstrcpy(s->pattern, sizeof(s->pattern), pattern);
    s->bufptr = s->path;
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

    for(;;) {
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
            q = s->dirpath;
            while (*p != ':' && *p != '\0') {
                if ((q - s->dirpath) < sizeof(s->dirpath) - 1)
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
                strcpy(filename, s->dirpath);
                strcat(filename, "/");
                strcat(filename, dirent->d_name);
                return 0;
            }
        }
    }
}

void find_file_close(FindFileState *s)
{
    if (s->dir) 
        closedir(s->dir);
}

#ifdef WIN32
/* convert '/' to '\' */
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
static void canonize_path1(char *buf, int buf_size, const char *path)
{
    const char *p;
    char *q, *q1;
    int c, abs_path;
    char file[1024];

    p = path;
    abs_path = (p[0] == '/');
    buf[0] = '\0';
    for(;;) {
        /* extract file */
        q = file;
        for(;;) {
            c = *p;
            if (c == '\0')
                break;
            p++;
            if (c == '/')
                break;
            if ((q - file) < sizeof(file) - 1)
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

void canonize_path(char *buf, int buf_size, const char *path)
{
    const char *p;
    /* check for URL protocol or windows drive */
    p = strchr(path, ':');
    if (p) {
        if ((p - path) == 1) {
            /* windows drive : we canonize only the following path */
            buf[0] = p[0];
            buf[1] = p[1];
            canonize_path1(buf + 2, buf_size - 2, p);
        } else {
            /* URL: it is already canonized */
            pstrcpy(buf, buf_size, path);
        }
    } else {
        /* simple unix path */
        canonize_path1(buf, buf_size, path);
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

/* canonize the path and make it absolute */
void canonize_absolute_path(char *buf, int buf_size, const char *path1)
{
    char path[1024];

    if (!is_abs_path(path1)) {
        /* XXX: should call it again */
        getcwd(path, sizeof(path));
#ifdef WIN32
        path_win_to_unix(path);
#endif
        pstrcat(path, sizeof(path), "/");
        pstrcat(path, sizeof(path), path1);
    } else {
        pstrcpy(path, sizeof(path), path1);
    }
    canonize_path(buf, buf_size, path);
}

/* last filename in a path */
const char *basename(const char *filename)
{
    const char *p;
    p = strrchr(filename, '/');
    if (!p) {
        return filename;
    } else {
        p++;
        return p;
    }
}

/* extract the pathname (and the trailing '/') */
const char *pathname(char *buf, int buf_size, const char *filename)
{
    const char *p;
    int len;

    p = strrchr(filename, '/');
    if (!p) {
        pstrcpy(buf, buf_size, filename);
    } else {
        len = p - filename + 1;
        if (len > buf_size - 1)
            len = buf_size - 1;
        memcpy(buf, filename, len);
        buf[len] = '\0';
    }
    return buf;
}

/* copy the nth first char of a string and truncate it. */
char *pstrncpy(char *buf, int buf_size, const char *s, int len)
{
    char *q, *q_end;
    int c;

    if (buf_size > 0) {
        q = buf;
        q_end = buf + buf_size - 1;
        while (q < q_end && len > 0) {
            c = *s++;
            if (c == '\0')
                break;
            *q++ = c;
            len--;
        }
        *q = '\0';
    }
    return buf;
}

void skip_spaces(const char **pp)
{
    const char *p;

    p = *pp;
    while (css_is_space(*p))
        p++;
    *pp = p;
}

/* need this for >= 256 */
static inline int utoupper(int c)
{
    if (c >= 'a' && c <= 'z')
        c += 'A' - 'a';
    return c;
}

int ustristart(const unsigned int *str, const char *val, const unsigned int **ptr)
{
    const unsigned int *p;
    const char *q;
    p = str;
    q = val;
    while (*q != '\0') {
        if (utoupper(*p) != utoupper(*q))
            return 0;
        p++;
        q++;
    }
    if (ptr)
        *ptr = p;
    return 1;
}

void get_str(const char **pp, char *buf, int buf_size, const char *stop)
{
    char *q;
    const char *p;
    int c;

    p = *pp;
    q = buf;
    for(;;) {
        c = *p;
        if (c == '\0' || strchr(stop, c))
            break;
        if ((q - buf) < buf_size - 1)
            *q++ = c;
        p++;
    }
    *q = '\0';
    *pp = p;
}

int css_get_enum(const char *str, const char *enum_str)
{
    int val, len;
    const char *s, *s1;

    s = enum_str;
    val = 0;
    len = strlen(str);
    for(;;) {
        s1 = strchr(s, ',');
        if (!s1) {
            if (!strcmp(s, str))
                return val;
            else
                break;
        } else {
            if (len == (s1 - s) && !memcmp(s, str, len))
                return val;
            s = s1 + 1;
        }
        val++;
    }
    return -1;
}

unsigned short keycodes[] = {
    KEY_LEFT,
    KEY_RIGHT,
    KEY_UP,
    KEY_DOWN,
    KEY_CTRL_LEFT,
    KEY_CTRL_RIGHT,
    KEY_CTRL_UP,
    KEY_CTRL_DOWN,
    KEY_CTRL_HOME,
    KEY_CTRL_END,
    KEY_CTRL('_'),
    KEY_CTRL(' '),
    KEY_CTRL('\\'),
    KEY_BACKSPACE,
    KEY_INSERT,
    KEY_DELETE, 
    KEY_PAGEUP,
    KEY_PAGEDOWN,
    KEY_HOME,
    KEY_END,
    ' ',
    KEY_RET,
    KEY_ESC,
    KEY_TAB,
    KEY_SHIFT_TAB,
    KEY_DEFAULT,
};

const char *keystr[] = {
    "left","right","up","down",
    "C-left","C-right","C-up","C-down", 
    "C-home", "C-end",
    "C-_", "C-space", "C-\\",
    "backspace","insert","delete","prior","next",
    "home","end",
    "SPC", "RET", "ESC",
    "TAB", "S-TAB",
    "default",
};

static int strtokey1(const char *p)
{
    int i, n;

    for(i=0;i<sizeof(keycodes)/sizeof(keycodes[0]);i++) {
        if (!strcmp(p, keystr[i]))
            return keycodes[i];
    }
    if (p[0] == 'f' && isdigit((unsigned char)p[1])) {
        p++;
        n = *p - '0';
        if (isdigit((unsigned char)p[1]))
            n = n * 10 + *p - '0';
        if (n == 0)
            n = 1;
        return KEY_F1 + n - 1;
    }
    return utf8_decode(&p);;
}

int strtokey(const char **pp)
{
    const char *p;
    int key;

    /* XXX: handle all cases */
    p = *pp;
    if (p[0] == 'C' && p[1] == '-') {
        /* control */
        p += 2;
        key = strtokey1(p);
        if (key >= 'a' && key <= 'z')
            key = KEY_CTRL(key);
    } else if (p[0] == 'M' && p[1] == '-') { 
        p += 2;
        key = strtokey1(p);
        if ((key >= 'a' && key <= 'z') ||
            key == KEY_BACKSPACE)
            key = KEY_META(key);
    } else {
        key = strtokey1(p);
    }
    while (*p != ' ' && *p != '\0')
        p++;
    *pp = p;
    return key;
}

void keytostr(char *buf, int buf_size, int key)
{
    int i;
    char buf1[32];
    
    for(i=0;i<sizeof(keycodes)/sizeof(keycodes[0]);i++) {
        if (keycodes[i] == key) {
            pstrcpy(buf, buf_size, keystr[i]);
            return;
        }
    }
    if (key >= KEY_META(' ') && key <= KEY_META(127)) {
        keytostr(buf1, sizeof(buf1), key & 0xff);
        snprintf(buf, buf_size, "M-%s", buf1);
    } else if (key >= 1 && key <= 31) {
        snprintf(buf, buf_size, "C-%c", key + 'a' - 1);
    } else if (key >= KEY_F1 && key <= KEY_F12) {
        snprintf(buf, buf_size, "F%d", key - KEY_F1 + 1);
    } else {
        char *q;
        q = utf8_encode(buf, key);
        *q = '\0';
    }
}

int to_hex(int key)
{
    if (key >= '0' && key <= '9')
        return key - '0';
    else if (key >= 'a' && key <= 'f')
        return key - 'a' + 10;
    else if (key >= 'A' && key <= 'F')
        return key - 'A' + 10;
    else
        return -1;
}

typedef struct ColorDef {
    char *name;
    unsigned int color;
} ColorDef;

static const ColorDef css_colors[] = {
    /*from HTML 4.0 spec */
    { "black", QERGB(0x00, 0x00, 0x00) },
    { "green", QERGB(0x00, 0x80, 0x00) },
    { "silver", QERGB(0xc0, 0xc0, 0xc0) },
    { "lime", QERGB(0x00, 0xff, 0x00) },

    { "gray", QERGB(0xbe, 0xbe, 0xbe) },
    { "olive", QERGB(0x80, 0x80, 0x00) },
    { "white", QERGB(0xff, 0xff, 0xff) },
    { "yellow", QERGB(0xff, 0xff, 0x00) },

    { "maroon", QERGB(0x80, 0x00, 0x00) },
    { "navy", QERGB(0x00, 0x00, 0x80) },
    { "red", QERGB(0xff, 0x00, 0x00) },
    { "blue", QERGB(0x00, 0x00, 0xff) },

    { "purple", QERGB(0x80, 0x00, 0x80) },
    { "teal", QERGB(0x00, 0x80, 0x80) },
    { "fuchsia", QERGB(0xff, 0x00, 0xff) },
    { "aqua", QERGB(0x00, 0xff, 0xff) },
    
    /* more colors */
    { "cyan", QERGB(0x00, 0xff, 0xff) },
    { "magenta", QERGB(0xff, 0x00, 0xff) },
    { "transparent", COLOR_TRANSPARENT },
};

/* XXX: make HTML parsing optional ? */
int css_get_color(int *color_ptr, const char *p)
{
    const ColorDef *def;
    int len, v, i, n;
    unsigned char rgba[4];

    /* search in table */
    def = css_colors;
    for(;;) {
        if (def >= css_colors + (sizeof(css_colors) / sizeof(css_colors[0])))
            break;
        if (!strcasecmp(p, def->name)) {
            *color_ptr = def->color;
            return 0;
        }
        def++;
    }
    
    rgba[3] = 0xff;
    if (isxdigit((unsigned char)*p)) {
        goto parse_num;
    } else if (*p == '#') {
        /* handle '#' notation */
        p++;
    parse_num:
        len = strlen(p);
        switch(len) {
        case 3:
            for(i=0;i<3;i++) {
                v = to_hex(*p++);
                rgba[i] = v | (v << 4);
            }
            break;
        case 6:
            for(i=0;i<3;i++) {
                v = to_hex(*p++) << 4;
                v |= to_hex(*p++);
                rgba[i] = v;
            }
            break;
        default:
            /* error */
            return -1;
        }
    } else if (strstart(p, "rgb(", &p)) {
        n = 3;
        goto parse_rgba;
    } else if (strstart(p, "rgba(", &p)) {
        /* extension for alpha */
        n = 4;
    parse_rgba:
        for(i=0;i<n;i++) {
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
    else if (!strcasecmp(str, "sans") ||
             !strcasecmp(str, "arial") ||
             !strcasecmp(str, "helvetica"))
        v = QE_FAMILY_SANS;
    else if (!strcasecmp(str, "fixed") ||
             !strcasecmp(str, "monospace") ||
             !strcasecmp(str, "courier"))
        v = QE_FAMILY_FIXED;
    else
        v = 0; /* inherit */
    return v;
}

/* a = a union b */
void css_union_rect(CSSRect *a, CSSRect *b)
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
int stat (__const char *__path,
          struct stat *__statbuf)
{
    return __xstat (_STAT_VER, __path, __statbuf);
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

/* set one string. */
StringItem *set_string(StringArray *cs, int index, const char *str)
{
    StringItem *v;
    if (index >= cs->nb_items)
        return NULL;

    v = malloc(sizeof(StringItem) + strlen(str));
    if (!v)
        return NULL;
    v->selected = 0;
    strcpy(v->str, str);
    if (cs->items[index])
        free(cs->items[index]);
    cs->items[index] = v;
    return v;
}

/* make a generic array alloc */
StringItem *add_string(StringArray *cs, const char *str)
{
    StringItem **tmp;
    int n;

    if (cs->nb_items >= cs->nb_allocated) {
        n = cs->nb_allocated + 32;
        tmp = realloc(cs->items, n * sizeof(StringItem *));
        if (!tmp)
            return NULL;
        cs->items = tmp;
        cs->nb_allocated = n;
    }
    cs->items[cs->nb_items++] = NULL;
    return set_string(cs, cs->nb_items - 1, str);
}

void free_strings(StringArray *cs)
{
    int i;

    for(i=0;i<cs->nb_items;i++)
        free(cs->items[i]);
    free(cs->items);
    memset(cs, 0, sizeof(StringArray));
}

void set_color(unsigned int *buf, int len, int style)
{
    int i;

    style <<= STYLE_SHIFT;
    for(i=0;i<len;i++)
        buf[i] |= style;
}

void css_strtolower(char *buf, int buf_size)
{
    int c;
    /* XXX: handle unicode */
    while (*buf) {
        c = tolower(*buf);
        *buf++ = c;
    }
}

void umemmove(unsigned int *dest, unsigned int *src, int len)
{
    memmove(dest, src, len * sizeof(unsigned int));
}

