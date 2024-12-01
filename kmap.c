/*
 * Kmap file based input method handling for QEmacs.
 *
 * Copyright (c) 2000 Fabrice Bellard.
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

#include "qe.h"
#ifdef CONFIG_MMAP
#include <sys/mman.h>
#endif

/* XXX: use an edit buffer to access the kmap !!!! */

/* parse the internal compressed input method format */
static int kmap_input(int *match_buf, int match_buf_size,
                      int *match_len_ptr, const u8 *data,
                      const char32_t *buf, int len)
{
    const u8 *p, *p1, *match_extra;
    int flag, i, l, l1, match_len, match_char, match_count, match_real_len;
    char32_t c;
    int match_olen;
    int nb_prefixes, last_outputc, match, prefix_len, trailing_space, olen;

    p = data;
    nb_prefixes = p[0] & 0x7f;
    trailing_space = p[0] & 0x80;
    p++;
    prefix_len = 0;
    if (nb_prefixes > 0) {
        p1 = p;
        for (i = 0; i < nb_prefixes; i++) {
            if (buf[0] == p1[0])
                goto prefix_match;
            p1 += 4;
        }
        return INPUTMETHOD_NOMATCH;
    prefix_match:
        p += nb_prefixes * 4 + (p1[1] << 16) + (p1[2] << 8) + p1[3];
        prefix_len = 1;
    }

    match_len = 0;
    match_real_len = 0;
    match_char = 0;
    match_count = 0;
    last_outputc = 0;
    match_olen = 0;
    match_extra = NULL;
    for (;;) {
        match = 1;
        olen = 1;
        l1 = prefix_len; /* length of input pattern */
        for (;;) {
            /* c = 0x00        end of table
             * c = 0x01..0x1d  delta unicode
             * c = 0x1e        unicode output mapping follows
             * c = 0x1f        unicode input char follows.
             * c = 0x20..0x7f  input character
             * c = 0x80        unused
             * c = 0x81        unused
             * c = 0x82..0x9d  extra unicode outputs follow
             * c = 0x9e        first unicode output mapping follows
             * c = 0x9f        last unicode input char follows and delta==1.
             * c = 0xa0..0xff  last input character and delta==1
             */
            c = *p++;
            flag = c & 0x80;
            c &= 0x7f;
            if (c == 0) {
                /* end of table / unused */
                goto the_end;
            } else
            if (c < 0x1e) {
                if (flag) {
                    /* extra output glyphs */
                    olen = c;
                } else {
                    /* delta */
                    last_outputc += c;
                }
                break;
            } else
            if (c == 0x1e) {
                /* explicit output */
                last_outputc = (p[0] << 8) | p[1];
                p += 2;
                if (flag)
                    continue;
                else
                    break;
            } else
            if (c == 0x1f) {
                /* unicode value */
                c = (p[0] << 8) | p[1];
                p += 2;
            }
            if (l1 < len && c != buf[l1])
                match = 0;
            l1++;
            if (flag) {
                /* delta = 1 */
                last_outputc++;
                break;
            }
        }
        if (trailing_space) {
            if (l1 < len && ' ' != buf[l1])
                match = 0;
            l1++;
        }

        if (match) {
            l = l1;
            if (l > len)
                l = len;
            if (l == match_len) {
                match_count++;
            } else
            if (l > match_len) {
                match_len = l;
                match_real_len = l1;
                match_char = last_outputc;
                match_count = 1;
                match_olen = olen;
                match_extra = p;
            }
        }
        p += (olen - 1) << 1;
    }
  the_end:
    if (match_len == 0) {
        return INPUTMETHOD_NOMATCH;
    } else
    if (match_count > 1 || match_real_len > len) {
        return INPUTMETHOD_MORECHARS;
    } else {
        *match_len_ptr = match_len;
        if (match_buf_size > 0) {
            match_buf[0] = match_char;
            p = match_extra;
            for (i = 1; i < match_olen && i < match_buf_size; i++, p += 2) {
                match_buf[i] = (p[0] << 8) + p[1];
            }
        }
        return match_olen;
    }
}

#ifdef CONFIG_MMAP
static void *input_method_map;
static size_t input_method_size;
#endif
static void *input_method_ptr;

int qe_load_input_methods(QEmacsState *qs, const char *filename)
{
    ssize_t file_size;
    int fd, offset;
    const unsigned char *file_ptr = NULL, *p;
    InputMethod *m;

    // O_BINARY? should just use fopen()
    fd = open(filename, O_RDONLY);
    if (fd < 0)
        return -1;

    file_size = lseek(fd, 0, SEEK_END);
    if (file_size <= 0)
        goto fail;

    lseek(fd, 0, SEEK_SET);

#ifdef CONFIG_MMAP
    input_method_map = mmap(NULL, file_size, PROT_READ, MAP_SHARED, fd, 0);
    if (input_method_map != (void*)MAP_FAILED) {
        file_ptr = input_method_map;
        input_method_size = file_size;
    }
#endif
    if (!file_ptr) {
        file_ptr = input_method_ptr = qe_malloc_array(char, file_size);
        if (input_method_ptr == NULL
        ||  read(fd, input_method_ptr, file_size) != file_size) {
            goto fail;
        }
    }

    if (memcmp(file_ptr, "kmap", 4) != 0)
        goto fail;

    p = file_ptr + 4;
    for (;;) {
        offset = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
        p += 4;
        if (offset == 0)
            break;
        /* Should add validation tests */
        m = qe_mallocz(InputMethod);
        if (m) {
            m->data = file_ptr + offset;
            m->input_match = kmap_input;
            m->name = (const char*)p;
            qe_register_input_method(qs, m);
        }
        p += strlen((const char *)p) + 1;
    }
    return 0;

  fail:
    qe_unload_input_methods(qs);
    close(fd);
    return -1;
}

void qe_unload_input_methods(QEmacsState *qs)
{
    /* Should unregister input methods, but this is only called upon exit */
#ifdef CONFIG_MMAP
    if (input_method_map && input_method_map != (void*)MAP_FAILED) {
        munmap(input_method_map, input_method_size);
        input_method_map = NULL;
        input_method_size = 0;
    }
#endif
    qe_free(&input_method_ptr);
}
