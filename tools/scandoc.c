/*
 * SCANDOC, Extract documentation from C source files
 *
 * Copyright (c) 2022-2024 Charlie Gordon.
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

/* A simple utility to preprocess C source code to extract
   documentation comments. These comments start with an @ just after
   the 2 initial characters of the comment.
   They are ordered lexicographically.
   The line starting with @ is removed from the output.
   Macros are created with @MACRONAME=text till the enf of line
   Macros are expanded before sorting the sections
 */

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NAME "scandoc"
#define VERSION  "2024-05-20"

static int verbose;

__attribute__((format(printf, 2, 3)))
static void fatal(int err, const char *format, ...) {
    va_list ap;
    fprintf(stderr, "%s: ", NAME);
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    if (err)
        fprintf(stderr, ": %s", strerror(err));
    fprintf(stderr, "\n");
    exit(1);
}

__attribute__((format(printf, 2, 3)))
static void error(int err, const char *format, ...) {
    va_list ap;
    fprintf(stderr, "%s: ", NAME);
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    if (err)
        fprintf(stderr, ": %s", strerror(err));
    fprintf(stderr, "\n");
}

__attribute__((format(printf, 1, 2)))
static void warning(const char *format, ...) {
    va_list ap;
    fprintf(stderr, "%s: ", NAME);
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

static void *xmalloc(size_t size) {
    void *p = malloc(size);
    if (!p)
        fatal(0, "cannot allocate memory");
    return p;
}

static void *xrealloc(void *p, size_t size) {
    p = realloc(p, size);
    if (!p)
        fatal(0, "cannot reallocate memory");
    return p;
}

static int strstart(const char *str, const char *val, const char **ptr) {
    size_t i;
    for (i = 0; val[i] != '\0'; i++) {
        if (str[i] != val[i])
            return 0;
    }
    if (ptr)
        *ptr = &str[i];
    return 1;
}

static int match_annotation(const char *str, const char *val, const char **ptr) {
    if (*str == '@')
        str++;
    while (*val) {
        if (*str == '_' || *str == '-')
            str++;
        if (*str != *val)
            return 0;
        str++;
        val++;
    }
    if (isalnum((unsigned char)*str))
        return 0;
    if (*str == ':')
        str++;
    while (isspace((unsigned char)*str))
        str++;
    if (ptr)
        *ptr = str;
    return 1;
}

static char *single_line(const char *s) {
    /* convert the string `s` to a single line, replacing newlines and
       adjacent white space with a single space and trimming initial and
       trailing white space from the resulting string.
     */
    char *res = strdup(s);
    char *p = res;
    char *q = res;

    for (;;) {
        while (isspace((unsigned char)*p))
            p++;
        while (*p && *p != '\n')
            *q++ = *p++;
        while (q > s && isspace((unsigned char)q[-1]))
            *--q = '\0';
        if (*p)
            *q++ = ' ';
        else
            break;
    }
    *q = '\0';
    return res;
}

typedef struct abbrev_t {
    char *abbr;
    size_t abbr_len;
    char *rep;
    size_t rep_len;
    struct abbrev_t *next;
} abbrev_t;

static abbrev_t *abbrevs;

static abbrev_t *add_abbrev(const char *abbr, size_t abbr_len, const char *rep, size_t rep_len) {
    abbrev_t *ap = xmalloc(sizeof *ap);
    ap->abbr = strndup(abbr, abbr_len);
    ap->abbr_len = strlen(ap->abbr);
    ap->rep = strndup(rep, rep_len);
    ap->rep_len = strlen(ap->rep);
    ap->next = abbrevs;
    return abbrevs = ap;
}

static abbrev_t *find_abbrev(const char *abbr, size_t abbr_len) {
    abbrev_t *ap;
    for (ap = abbrevs; ap; ap = ap->next) {
        if (abbr_len == ap->abbr_len && !memcmp(abbr, ap->abbr, abbr_len))
            break;
    }
    return ap;
}

typedef struct doc_t {
    char *section;
    char *text;
    const char *filename;
    int lineno;
} doc_t;

static doc_t *docs;
static int docs_len;

static int doc_strcmp(const char *p1, const char *p2) {
    /* compare strings ignoring spaces and converting
       numbers to implement natural ordering */
    for (;;) {
        unsigned char c1, c2;
        while (isspace(c1 = *p1++))
            continue;
        while (isspace(c2 = *p2++))
            continue;
        if (isdigit(c1) && isdigit(c2)) {
            long v1 = strtol(p1 - 1, (char **)(intptr_t)&p1, 10);
            long v2 = strtol(p2 - 1, (char **)(intptr_t)&p2, 10);
            if (v1 != v2)
                return (v1 > v2) - (v2 > v1);
        } else {
            if (c1 != c2)
                return c1 - c2;
            if (c1 == '\0')
                return 0;
        }
    }
}

static int doc_compare(const void *a1, const void *a2) {
    const doc_t *dp1 = a1;
    const doc_t *dp2 = a2;
    int cmp = doc_strcmp(dp1->section, dp2->section);
    return cmp ? cmp : doc_strcmp(dp1->text, dp2->text);
}

static int flush_docs(const char *outname) {
    FILE *ft = stdout;
    int i;

    if (outname) {
        if ((ft = fopen(outname, "w")) == NULL) {
            error(errno, "cannot open output file %s", outname);
            return 1;
        }
    }
    qsort(docs, docs_len, sizeof(*docs), doc_compare);

    for (i = 0; i < docs_len; i++) {
        doc_t *dp = &docs[i];
        char *p = dp->text;
        int indent, len;

        /* remove initial indentation spaces from each line */
        indent = strspn(p, " ");
        if (p[indent] == '\0')
            continue;

        if (verbose) {
            //fprintf(ft, "\n[//] # (%s:%d:%s)\n",
            //        dp->filename, dp->lineno, dp->section);
            fprintf(ft, "\n<!---\n%s:%d:%s\n-->\n",
                    dp->filename, dp->lineno, dp->section);
        }
        fprintf(ft, "\n");
        for (;;) {
            int ii = strspn(p, " ");
            if (p[ii] == '\0')
                break;
            if (ii > indent)
                ii = indent;
            p += ii;
            len = strcspn(p, "\n");
            fprintf(ft, "%.*s\n", len, p);
            if (p[len] == '\n')
                len++;
            p += len;
        }
    }
    if (ft != stdout)
        fclose(ft);
    return 0;
}

static int rtrim(const char *s, int len) {
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        --len;
    return len;
}

static int skipwhite(const char *s) {
    int pos = 0;
    while (isspace((unsigned char)s[pos]))
        pos++;
    return pos;
}

static char *trimdup(const char *p) {
    p += skipwhite(p);
    return strndup(p, rtrim(p, strlen(p)));
}

static void freep(char **pp) {
    free(*pp);
    *pp = NULL;
}

static void concat(char **pp, const char *p2, int len2, int strip) {
    char *p1 = *pp;
    char *p3;
    int len1, len3, space = 0;
    len1 = p1 ? strlen(p1) : 0;
    if (len2 < 0)
        len2 = strlen(p2);
    if (strip) {
        len1 = rtrim(p1, len1);
        while (len2 > 0 && isspace((unsigned char)*p2)) {
            p2++;
            len2--;
        }
        len2 = rtrim(p2, len2);
        space = (len1 && len2);
    }
    len3 = len1 + space + len2;
    p3 = xmalloc(len3 + 1);
    memcpy(p3, p1, len1);
    if (space)
        p3[len1] = ' ';
    memcpy(p3 + len1 + space, p2, len2);
    p3[len3] = '\0';
    freep(pp);
    *pp = p3;
}

__attribute__((format(printf, 2, 0)))
static int vxprintf(char **strp, const char *format, va_list ap)
{
    char buf[1024];
    va_list arg;
    char *str;
    int len;

    if (!strp || !format) {
        errno = EINVAL;
        return -1;
    }

    va_copy(arg, ap);
    len = vsnprintf(buf, sizeof buf, format, arg);
    va_end(arg);

    if (len < 0 || (str = malloc(len + 1)) == NULL)
        return -1;

    if (len < (int)sizeof(buf)) {
        memcpy(str, buf, len + 1);
    } else {
        vsnprintf(str, len + 1, format, ap);
    }
    free(*strp);
    *strp = str;
    return len;
}

__attribute__((format(printf, 2, 3)))
static int xprintf(char **strp, const char *format, ...)
{
    va_list ap;
    int len;

    va_start(ap, format);
    len = vxprintf(strp, format, ap);
    va_end(ap);

    return len;
}

static int xconcat(char **strp, size_t n, const char *s1,
                   const char *s2, const char *s3)
{
    size_t len1 = strlen(s1);
    size_t len2 = strlen(s2);
    size_t len3 = strlen(s3);
    size_t len = n + len1 + len2 + len3;
    char *res = xmalloc(len + 1);
    if (n)
        memcpy(res, *strp, n);
    memcpy(res + n, s1, len1);
    memcpy(res + n + len1, s2, len2);
    memcpy(res + n + len1 + len2, s3, len3 + 1);
    free(*strp);
    *strp = res;
    return len;
}

static char *load_file(const char *filename) {
    char *contents;
    size_t size, len;
    int c;
    FILE *fp = fopen(filename, "r");
    if (fp == NULL)
        fatal(errno, "cannot include '%s'", filename);

    contents = xmalloc(32);
    size = 32;
    len = 0;

    while ((c = getc(fp)) != EOF) {
        if (len + 1 > size) {
            size += size / 2 + 32;
            contents = xrealloc(contents, size);
        }
        contents[len++] = (char)c;
    }
    contents[len] = '\0';
    fclose(fp);
    return contents;
}

static void add_doc(const char *filename,
                    const char *comment, int lineno,
                    const char *proto_arg, int proto_line)
{
    doc_t *dp;
    char *sec;
    char *text;
    abbrev_t *ap;
    int len = strlen(comment);
    int line_len;
    int abbr_len;
    int text_len;
    int sec_len;

    if (len < 3 || comment[2] != '@')
        return;

    /* strip comment marks */
    comment += 3;
    len -= 3;
    if (len > 1 && comment[len - 1] == '/' && comment[len - 2] == '*')
        len -= 2;

    /* strip boundary spaces */
    while (len > 0 && *comment == ' ') {
        comment++;
        len--;
    }
    len = rtrim(comment, len);

    for (line_len = 0; line_len < len && comment[line_len] != '\n'; line_len++)
        continue;

    /* extract section (first line) */
    sec_len = rtrim(comment, line_len);
    sec = strndup(comment, sec_len);

    if (line_len < len && comment[line_len] == '\n')
        line_len++;
    text_len = len - line_len;
    text = strndup(comment + line_len, text_len);

    if (verbose) {
        fprintf(stderr, "%s:%d:%s\n", filename, lineno, sec);
        if (verbose > 1) {
            fprintf(stderr, "%s\n", text);
        }
    }

    /* scan the first word */
    abbr_len = strcspn(sec, " =");
    if (sec[abbr_len] == '=') {
        /* macro definition */
        char *rep = sec + abbr_len + 1;
        /* store the expansion for the macro name */
        ap = add_abbrev(sec, abbr_len, rep, strlen(rep));
        /* remove the macro name and equal sign from the section line */
        xprintf(&sec, "%s", rep);
    } else
    if ((ap = find_abbrev(sec, abbr_len)) != NULL) {
        /* expand the macro inside in the section line */
        xprintf(&sec, "%s%s", ap->rep, sec + abbr_len);
    }
    /* synthesize extra data based on tag */
    if (ap) {
        if (!strcmp(ap->abbr, "API") && proto_arg) {
            int pos, epos, elen;
            char *proto;
            const char *p;

            /* skip storage class from prototype */
            strstart(proto_arg, "static ", &proto_arg);
            strstart(proto_arg, "inline ", &proto_arg);
            strstart(proto_arg, "extern ", &proto_arg);
            /* linearize the prototype */
            proto = single_line(proto_arg);

            /* extract function name */
            for (pos = epos = elen = 0; proto[pos]; pos++) {
                if (proto[pos] == '(') {
                    elen = pos - epos;
                    break;
                }
                if (proto[pos] == ' ' || proto[pos] == '*') {
                    epos = pos + 1;
                }
            }
            for (; proto[pos]; pos++) {
                if (proto[pos] == ';' || proto[pos] == '{') {
                    break;
                }
            }
            pos = rtrim(proto, pos);
            /* entry name starts at epos, length is elen */
            concat(&sec, proto + epos, elen, 1);
            xprintf(&text, "%*s### `%.*s;`\n\n%s",
                    skipwhite(text), "", pos, proto, text);

            /* TODO: should implement more elaborate text reformating */
            /* TODO: handle function macros */
            p = text;
            while ((p = strchr(p, '@')) != NULL) {
                int offset = p - text;
                if (match_annotation(p, "argument", &p) || match_annotation(p, "param", &p)) {
                    xprintf(&text, "%.*s\n* argument %s", offset, text, p);
                } else
                if (match_annotation(p, "returns", &p) || match_annotation(p, "return", &p)) {
                    xprintf(&text, "%.*s\nReturn %s", offset, text, p);
                } else
                if (match_annotation(p, "note", &p)) {
                    xprintf(&text, "%.*s\nNote: %s", offset, text, p);
                } else
                if (match_annotation(p, "seealso", &p)) {
                    xprintf(&text, "%.*s\nSee also: %s", offset, text, p);
                } else {
                    warning("unknown annotation: %s", p);
                }
                p = text + offset + 1;
            }
            free(proto);
        } else {
            const char *p = text;
            while ((p = strchr(p, '@')) != NULL) {
                int offset = p - text;
                if (match_annotation(p, "include", &p)) {
                    char *fname = trimdup(p);
                    char *contents = load_file(fname);
                    xconcat(&text, offset, contents, "", "");
                    free(fname);
                    free(contents);
                    break;
                } else {
                    warning("unknown annotation: %s", p);
                }
            }
        }
    }

    /* append section */
    docs = xrealloc(docs, (docs_len + 1) * sizeof(*docs));
    dp = &docs[docs_len++];
    dp->section = sec;
    dp->text = text;
    dp->filename = filename;
    dp->lineno = lineno;
}

static int scandoc(const char *filename, FILE *fp) {
    char buf[512];
    int len, lineno = 0;
    int in_comment = 0, comment_line = 0;
    int in_proto = 0, proto_line = 0;
    char *proto = NULL;
    char *comment = NULL;
    char *p;

    while (fgets(buf, sizeof buf, fp)) {
        if (in_proto == 2 && in_comment == 2) {
            add_doc(filename, comment, comment_line, proto, proto_line);
            freep(&comment);
            freep(&proto);
            in_comment = in_proto = 0;
        }
        lineno++;
        p = buf + skipwhite(buf);
        if (in_proto != 1) {
            char *p1 = buf;
            int p1_len;

            if (!strncmp(p, "/*@", 3)) {
                if (in_comment == 2) {
                    /* flush pending doc comment */
                    add_doc(filename, comment, comment_line, proto, proto_line);
                    freep(&comment);
                    freep(&proto);
                    in_comment = in_proto = 0;
                }
                freep(&comment);
                comment_line = lineno;
                in_comment = 1;
                p1 = p;
            }
            p1_len = strlen(p1);
            if (in_comment == 1) {
                char *e = strstr(p1, "*/");
                if (e) {
                    p1_len = e + 2 - p1;
                    in_comment = 2;
                }
                concat(&comment, p1, p1_len, 0);
                continue;
            }
            if (!*p) {
                /* blank line */
                continue;
            }
            if (p == buf && *p != '/' && *p != '#' && *p != '}' && *p != '*') {
                /* discard previous proto */
                freep(&proto);
                proto_line = lineno;
                in_proto = 1;
            }
        }
        if (in_proto == 1) {
            if (!*p) {
                /* blank line */
                continue;
            }
            len = strcspn(p, ";{");
            if (p[len] == '{' || p[len] == ';') {
                in_proto = 2;
            }
            concat(&proto, p, len, 1);
        }
    }
    if (in_comment == 2) {
        add_doc(filename, comment, comment_line, proto, proto_line);
    }
    freep(&comment);
    freep(&proto);
    return 0;
}

int main(int argc, char *argv[]) {
    char *filename = NULL;
    char *outname = NULL;
    int i;
    int args_done = 0;

    for (i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (!args_done && *arg == '-') {
            if (arg[1] == '\0') {
                filename = arg;
                scandoc("<stdin>", stdin);
                continue;
            }
            if (arg[1] == '-') {
                if (!strcmp(arg, "--")) {
                    args_done = 1;
                } else
                if (!strcmp(arg, "--help")) {
                    goto usage;
                } else
                if (!strcmp(arg, "--version")) {
                    printf("%s version %s\n", NAME, VERSION);
                    return 1;
                } else {
                    warning("bad option: -%s", arg);
                    return 1;
                }
            }
            while (*++arg) {
                switch (*arg) {
                case 'h':
                case '?':
                    goto usage;
                case 'o':
                    outname = arg[1] ? arg + 1 : argv[++i];
                    if (!outname) {
                        warning("missing filename for -o");
                        return 1;
                    }
                    break;
                case 'v':
                    verbose++;
                    continue;
                default:
                    warning("bad option: %s", arg);
                    return 1;
                }
                break;
            }
        } else {
            FILE *fp;
            filename = arg;
            if ((fp = fopen(filename, "r")) == NULL) {
                error(errno, "cannot open input file %s", filename);
                return 1;
            }
            scandoc(filename, fp);
            fclose(fp);
        }
    }
    if (!filename) {
    usage:
        printf("usage: %s [-v] [-o FILENAME] FILE ...\n", NAME);
        return 2;
    }
    /* output the documentation to outname or stdout */
    flush_docs(outname);
    return 0;
}
