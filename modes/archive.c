/*
 * Mode for viewing archive files for QEmacs.
 *
 * Copyright (c) 2002-2024 Charlie Gordon.
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

/*---------------- Archivers ----------------*/

typedef struct ArchiveType ArchiveType;

struct ArchiveType {
    const char *name;           /* name of archive format */
    const char *magic;          /* magic signature */
    int magic_size;
    const char *extensions;
    const char *list_cmd;       /* list archive contents to stdout */
    const char *extract_cmd;    /* extract archive element to stdout */
    int sf_flags;
    struct ArchiveType *next;
};

static ArchiveType archive_type_array[] = {
    { "tar", NULL, 0, "tar|tar.Z|tgz|tar.gz|tbz|tbz2|tar.bz2|tar.bzip2|"
            "txz|tar.xz|tlz|tar.lzma|taz", "tar tvf $1", NULL, 0, NULL },
    { "zip", "PK\003\004", 4, "zip|ZIP|jar|apk|bbb", "unzip -l $1", NULL, 0, NULL },
    { "rar", NULL, 0, "rar|RAR", "unrar l $1", NULL, 0, NULL },
    { "arj", NULL, 0, "arj|ARJ", "unarj l $1", NULL, 0, NULL },
    { "cab", NULL, 0, "cab", "cabextract -l $1", NULL, 0, NULL },
    { "7zip", NULL, 0, "7z", "7z l $1", NULL, 0, NULL },
    { "ar", NULL, 0, "a|ar", "ar -tv $1", NULL, 0, NULL },
    { "xar", NULL, 0, "xar|pkg", "xar -tvf $1", NULL, 0, NULL },
    { "zoo", NULL, 0, "zoo", "zoo l $1", NULL, 0, NULL },
    { "lha", NULL, 0, "lha", "lha -l $1", NULL, 0, NULL },
};

static ArchiveType *archive_types;

static ArchiveType *find_archive_type(const char *filename,
                                      const u8 *buf, int buf_size)
{
    char rname[MAX_FILENAME_SIZE];
    ArchiveType *atp;

    /* File extension based test */
    reduce_filename(rname, sizeof(rname), get_basename(filename));
    for (atp = archive_types; atp; atp = atp->next) {
        if (atp->magic_size && atp->magic_size <= buf_size
        &&  !memcmp(atp->magic, buf, atp->magic_size))
            return atp;
        if (match_extension(rname, atp->extensions))
            return atp;
    }
    return NULL;
}

static int archive_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    ArchiveType *atp = find_archive_type(p->filename, p->buf, p->buf_size);

    if (atp) {
        if (p->b && p->b->data_type == mode->data_type) {
            /* buffer loaded, re-selecting mode causes buffer reload */
            return 0;//9
        } else {
            /* buffer not yet loaded */
            return 85;//70
        }
    }

    return 0;
}

static int qe_shell_subst(char *buf, int size, const char *cmd,
                          const char *arg1, const char *arg2)
{
    buf_t outbuf, *out;

    out = buf_init(&outbuf, buf, size);
    while (*cmd) {
        if (*cmd == '$') {
            if (cmd[1] == '1' && arg1) {
                buf_put_byte(out, '\'');
                buf_puts(out, arg1);
                buf_put_byte(out, '\'');
                cmd += 2;
                continue;
            }
            if (cmd[1] == '2' && arg2) {
                buf_put_byte(out, '\'');
                buf_puts(out, arg2);
                buf_put_byte(out, '\'');
                cmd += 2;
                continue;
            }
        }
        buf_put_byte(out, *cmd++);
    }
    return out->len;
}

static int file_read_block(EditBuffer *b, FILE *f1, u8 *buf, int buf_size)
{
    FILE *f = f1;
    int nread = 0;

    if (!f)
        f = fopen(b->filename, "rb");
    if (f)
        nread = fread(buf, 1, buf_size, f);
    if (f && !f1)
        fclose(f);
    return nread;
}

static int archive_buffer_load(EditBuffer *b, FILE *f)
{
    /* Launch subprocess to list archive contents */
    char cmd[1024];
    ArchiveType *atp;
    u8 buf[256];
    int buf_size;

    buf_size = file_read_block(b, f, buf, sizeof(buf));
    atp = find_archive_type(b->filename, buf, buf_size);
    if (atp) {
        b->data_type_name = atp->name;
        eb_clear(b);
        // XXX: should use window caption
        eb_printf(b, "  Directory of %s archive %s\n",
                  atp->name, b->filename);
        qe_shell_subst(cmd, sizeof(cmd), atp->list_cmd, b->filename, NULL);
        qe_new_shell_buffer(b->qs, b, NULL, get_basename(b->filename), NULL,
                            NULL, cmd, atp->sf_flags | SF_INFINITE | SF_BUFED_MODE);

        /* XXX: should check for archiver error */
        /* XXX: should delay BF_SAVELOG until buffer is fully loaded */
        b->flags |= BF_READONLY;

        return 0;
    } else {
        eb_printf(b, "Cannot find archiver\n");
        return -1;
    }
}

static int archive_buffer_save(EditBuffer *b, int start, int end,
                               const char *filename)
{
    /* XXX: prevent saving parsed contents to archive file */
    return -1;
}

static void archive_buffer_close(EditBuffer *b)
{
    /* XXX: kill process? */
}

static EditBufferDataType archive_data_type = {
    "archive",
    archive_buffer_load,
    archive_buffer_save,
    archive_buffer_close,
    NULL, /* next */
};

static ModeDef archive_mode = {
    .name = "archive",
    .mode_probe = archive_mode_probe,
    .data_type = &archive_data_type,
};

static int archive_init(QEmacsState *qs)
{
    int i;

    /* copy and patch text_mode */
    // XXX: remove this mess
    memcpy(&archive_mode, &text_mode, offsetof(ModeDef, first_key));
    archive_mode.name = "archive";
    archive_mode.mode_probe = archive_mode_probe;
    archive_mode.data_type = &archive_data_type;

    for (i = 1; i < countof(archive_type_array); i++) {
        archive_type_array[i - 1].next = archive_type_array + i;
    }
    archive_types = archive_type_array;

    qe_register_data_type(qs, &archive_data_type);
    qe_register_mode(qs, &archive_mode, MODEF_DATATYPE | MODEF_SHELLPROC);

    return 0;
}

/*---------------- Compressors ----------------*/

typedef struct CompressType CompressType;

struct CompressType {
    const char *name;           /* name of compressed format */
    const char *magic;          /* magic signature */
    int magic_size;
    const char *extensions;
    const char *load_cmd;       /* uncompress file to stdout */
    const char *save_cmd;       /* compress to file from stdin */
    int sf_flags;
    struct CompressType *next;
};

static CompressType compress_type_array[] = {
    { "gzip", NULL, 0, "gz", "gunzip -c $1", "gzip > $1", 0, NULL },
    { "bzip2", NULL, 0, "bz2|bzip2", "bunzip2 -c $1", "bzip2 > $1", 0, NULL },
    { "compress", NULL, 0, "Z", "uncompress -c < $1", "compress > $1", 0, NULL },
    { "LZMA", NULL, 0, "lzma", "unlzma -c $1", "lzma > $1", 0, NULL },
    { "XZ", NULL, 0, "xz", "unxz -c $1", "xz > $1", 0, NULL },
    { "BinHex", NULL, 0, "hqx", "binhex decode -o /tmp/qe-$$ $1 && "
                       "cat /tmp/qe-$$ ; rm -f /tmp/qe-$$", NULL, 0, NULL },
    { "sqlite", "SQLite format 3\0", 16, NULL, "sqlite3 $1 .dump", NULL, 0, NULL },
    { "bplist", "bplist00", 8, "plist", "plutil -p $1", NULL, 0, NULL },
//    { "bplist", "bplist00", 8, "plist", "plutil -convert xml1 -o - $1", NULL, 0, NULL },
//    { "jpeg", NULL, 0, "jpg", "jp2a --height=35 --background=dark $1", NULL, SF_COLOR, NULL },
//    { "image", NULL, 0, "bmp", "img2txt -f utf8 $1", NULL, SF_COLOR, NULL  },
    { "pdf", NULL, 0, "pdf", "pstotext $1", NULL, 0, NULL },
    { "zdump", "TZif\0\0\0\0", 8, NULL, "zdump -v $1", NULL, 0, NULL },
#ifdef CONFIG_DARWIN
    { "dylib", NULL, 0, "dylib", "nm -n $1", NULL, 0, NULL },
#endif
};

static CompressType *compress_types;

static CompressType *find_compress_type(const char *filename,
                                        const u8 *buf, int buf_size)
{
    char rname[MAX_FILENAME_SIZE];
    CompressType *ctp;

    /* File extension based test */
    reduce_filename(rname, sizeof(rname), get_basename(filename));
    for (ctp = compress_types; ctp; ctp = ctp->next) {
        if (ctp->magic && ctp->magic_size && ctp->magic_size <= buf_size
        &&  !memcmp(ctp->magic, buf, ctp->magic_size))
            return ctp;
        if (match_extension(rname, ctp->extensions))
            return ctp;
    }
    return NULL;
}

static int compress_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    CompressType *ctp = find_compress_type(p->filename, p->buf, p->buf_size);

    if (ctp) {
        if (p->b && p->b->data_type == mode->data_type) {
            /* buffer loaded, re-selecting mode causes buffer reload */
            return 0;//9;
        } else {
            /* buffer not yet loaded */
            return 82;
        }
    }

    return 0;
}

static int compress_buffer_load(EditBuffer *b, FILE *f)
{
    /* Launch subprocess to expand compressed contents */
    char cmd[1024];
    CompressType *ctp;
    u8 buf[256];
    int buf_size;

    buf_size = file_read_block(b, f, buf, sizeof(buf));
    ctp = find_compress_type(b->filename, buf, buf_size);
    if (ctp) {
        b->data_type_name = ctp->name;
        eb_clear(b);
        qe_shell_subst(cmd, sizeof(cmd), ctp->load_cmd, b->filename, NULL);
        qe_new_shell_buffer(b->qs, b, NULL, get_basename(b->filename), NULL,
                            NULL, cmd,
                            ctp->sf_flags | SF_INFINITE | SF_AUTO_CODING | SF_AUTO_MODE);
        /* XXX: should check for archiver error */
        /* XXX: should delay BF_SAVELOG until buffer is fully loaded */
        b->flags |= BF_READONLY;

        return 0;
    } else {
        eb_printf(b, "cannot find compressor\n");
        return -1;
    }
}

static int compress_buffer_save(EditBuffer *b, int start, int end,
                               const char *filename)
{
    /* XXX: should recompress contents to compressed file */
    return -1;
}

static void compress_buffer_close(EditBuffer *b)
{
    /* XXX: kill process? */
}

static EditBufferDataType compress_data_type = {
    "compress",
    compress_buffer_load,
    compress_buffer_save,
    compress_buffer_close,
    NULL, /* next */
};

static ModeDef compress_mode = {
    .name = "compress",
    .mode_probe = compress_mode_probe,
    .data_type = &compress_data_type,
};

static int compress_init(QEmacsState *qs)
{
    int i;

    /* copy and patch text_mode */
    // XXX: remove this mess
    memcpy(&compress_mode, &text_mode, offsetof(ModeDef, first_key));
    compress_mode.name = "compress";
    compress_mode.mode_probe = compress_mode_probe;
    compress_mode.data_type = &compress_data_type;

    for (i = 1; i < countof(compress_type_array); i++) {
        compress_type_array[i - 1].next = compress_type_array + i;
    }
    compress_types = compress_type_array;

    qe_register_data_type(qs, &compress_data_type);
    qe_register_mode(qs, &compress_mode, MODEF_DATATYPE | MODEF_SHELLPROC);

    return 0;
}

/*---------------- Wget ----------------*/

static ModeDef wget_mode;

static int wget_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (strstart(p->real_filename, "http:", NULL)
    ||  strstart(p->real_filename, "https:", NULL)
    ||  strstart(p->real_filename, "ftp:", NULL)) {
        if (p->b && p->b->data_type == mode->data_type) {
            /* buffer loaded, re-selecting mode causes buffer reload */
            return 9;
        } else {
            /* buffer not yet loaded */
            return 90;
        }
    }

    return 0;
}

static int wget_buffer_load(EditBuffer *b, FILE *f)
{
    /* Launch wget subprocess to retrieve contents */
    char cmd[1024];

    eb_clear(b);
    qe_shell_subst(cmd, sizeof(cmd), "wget -q -O - $1", b->filename, NULL);
    qe_new_shell_buffer(b->qs, b, NULL, get_basename(b->filename), NULL,
                        NULL, cmd, SF_INFINITE | SF_AUTO_CODING | SF_AUTO_MODE);
    /* XXX: should refilter by content type */
    /* XXX: should have a way to keep http headers --save-headers */
    /* XXX: should check for wget error */
    /* XXX: should delay BF_SAVELOG until buffer is fully loaded */
    b->flags |= BF_READONLY;

    return 0;
}

static int wget_buffer_save(EditBuffer *b, int start, int end,
                               const char *filename)
{
    /* XXX: should put contents back to web server */
    return -1;
}

static void wget_buffer_close(EditBuffer *b)
{
    /* XXX: kill process? */
}

static EditBufferDataType wget_data_type = {
    "wget",
    wget_buffer_load,
    wget_buffer_save,
    wget_buffer_close,
    NULL, /* next */
};

static int wget_init(QEmacsState *qs)
{
    /* copy and patch text_mode */
    // XXX: remove this mess
    memcpy(&wget_mode, &text_mode, offsetof(ModeDef, first_key));
    wget_mode.name = "wget";
    wget_mode.mode_probe = wget_mode_probe;
    wget_mode.data_type = &wget_data_type;

    qe_register_data_type(qs, &wget_data_type);
    qe_register_mode(qs, &wget_mode, MODEF_DATATYPE | MODEF_SHELLPROC);

    return 0;
}

/*---------------- Manual pages ----------------*/

static ModeDef man_mode;

static int man_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    if (match_extension(p->real_filename,
                        "1.gz|1m.gz|1ssl.gz|1tcl.gz|2.gz|3.gz|3o.gz|3ssl.gz|"
                        "4.gz|5.gz|5ssl.gz|6.gz|7.gz|7ssl.gz|8.gz|9.gz")) {
        goto has_man;
    }

    if (match_extension(p->real_filename,
                        "1|1m|1ssl|1tcl|2|3|3o|3ssl|4|5|5ssl|6|7|7ssl|8|9|"
                        "n|ntcl|man|roff")
//    &&  !strchr(p->filename, '.')
    &&  (p->buf[0] == '.' ||
         (p->buf[0] == '\n' && p->buf[1] == '.') ||
         !memcmp(p->buf, "'\\\"", 3) ||
         !memcmp(p->buf, "'''", 3) ||
         !memcmp(p->buf, "\\\"", 2))) {
    has_man:
        if (match_extension(p->real_filename, "doc"))
            return 0;

        if (p->b && p->b->data_type == mode->data_type) {
            /* buffer loaded, re-selecting mode causes buffer reload */
            return 9;
        } else {
            /* buffer not yet loaded */
            return 90;
        }
    }

    if (!memcmp(p->buf, ".tr *\\(**", 9)
    ||  !memcmp(p->buf, ".\\\" ", 4))
        goto has_man;

    return 0;
}

static int man_buffer_load(EditBuffer *b, FILE *f)
{
    /* Launch man subprocess to format manual page */
    char cmd[1024];

    eb_clear(b);
    qe_shell_subst(cmd, sizeof(cmd), "man $1", b->filename, NULL);
    qe_new_shell_buffer(b->qs, b, NULL, get_basename(b->filename), NULL,
                        NULL, cmd, SF_COLOR | SF_INFINITE);
    /* XXX: should check for man error */
    /* XXX: should delay BF_SAVELOG until buffer is fully loaded */
    b->flags |= BF_READONLY;

    return 0;
}

static int man_buffer_save(EditBuffer *b, int start, int end,
                               const char *filename)
{
    /* XXX: should put contents back to web server */
    return -1;
}

static void man_buffer_close(EditBuffer *b)
{
    /* XXX: kill process? */
}

static EditBufferDataType man_data_type = {
    "man",
    man_buffer_load,
    man_buffer_save,
    man_buffer_close,
    NULL, /* next */
};

static int man_init(QEmacsState *qs)
{
    /* copy and patch text_mode */
    // XXX: remove this mess
    memcpy(&man_mode, &text_mode, offsetof(ModeDef, first_key));
    man_mode.name = "man";
    man_mode.mode_probe = man_mode_probe;
    man_mode.data_type = &man_data_type;

    qe_register_data_type(qs, &man_data_type);
    qe_register_mode(qs, &man_mode, MODEF_DATATYPE | MODEF_SHELLPROC);

    return 0;
}

/*---------------- Initialization ----------------*/

static int archive_compress_init(QEmacsState *qs)
{
    return archive_init(qs) ||
            compress_init(qs) ||
            wget_init(qs) ||
            man_init(qs);
}

qe_module_init(archive_compress_init);
