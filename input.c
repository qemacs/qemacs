/*
 * Input method handling for QEmacs.
 * Copyright (c) 2000 Fabrice Bellard.
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
#ifndef WIN32
#include <sys/mman.h>
#endif

InputMethod *input_methods;

static int default_input(int *match_len_ptr, 
                         const u8 *data, const unsigned int *buf, int len)
{
    return INPUTMETHOD_NOMATCH;
}

static int unicode_input(int *match_len_ptr, 
                         const u8 *data, const unsigned int *buf, int len)
{
    int i, h, c;
    if (buf[0] != 'x')
        return INPUTMETHOD_NOMATCH;

    c = 0;
    if (len > 5)
        len = 5;
    for(i=1;i<len;i++) {
        h = to_hex(buf[i]);
        if (h == -1)
            return INPUTMETHOD_NOMATCH;
        c = (c << 4) | h;
    }
    if (len == 5) {
        *match_len_ptr = len;
        return c;
    } else {
        return INPUTMETHOD_MORECHARS;
    }
}

static InputMethod default_input_method = { 
    "default", default_input, NULL,
};

static InputMethod unicode_input_method = { 
    "unicode", unicode_input, NULL,
};

void register_input_method(InputMethod *m)
{
    InputMethod **p;
    p = &input_methods;
    while (*p != NULL) p = &(*p)->next;
    m->next = NULL;
    *p = m;
}

#ifdef CONFIG_ALL_KMAPS

/* XXX: use an edit buffer to access the kmap !!!! */

/* parse the internal compressed input method format */
int kmap_input(int *match_len_ptr, 
               const u8 *data, const unsigned int *buf, int len)
{
    const u8 *p, *p1;
    int c, d, i, l, l1, match_len, match_char, match_count, match_real_len;
    int nb_prefixes, last_outputc, match, prefix_len, trailing_space;

    p = data;
    nb_prefixes = p[0] & 0x7f;
    trailing_space = p[0] & 0x80;
    p++;
    prefix_len = 0;
    if (nb_prefixes > 0) {
        p1 = p;
        for(i=0;i<nb_prefixes;i++) {
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
    for(;;) {
        match = 1;
        l1 = prefix_len; /* length of input pattern */
        for(;;) {
            c = *p++;
            d = c & 0x80;
            c = c & 0x7f;
            if (c == 0) {
                /* end of table */
                goto the_end;
            } else if (c >= 1 && c <= 0x1d) {
                /* delta */
                last_outputc += c;
                break;
            } else if (c == 0x1e) {
                /* explicit output */
                last_outputc = (p[0] << 8) | p[1];
                p += 2;
                break;
            } else if (c == 0x1f) {
                /* unicode value */
                c = (p[0] << 8) | p[1];
                p += 2;
            }
            if (l1 < len && c != buf[l1])
                match = 0;
            l1++;
            if (d) {
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
            } else if (l > match_len) {
                match_len = l;
                match_real_len = l1;
                match_char = last_outputc;
                match_count = 1;
            }
        }
    }
the_end:
    if (match_len == 0) {
        return INPUTMETHOD_NOMATCH;
    } else if (match_count > 1 || match_real_len > len) {
        return INPUTMETHOD_MORECHARS;
    } else {
        *match_len_ptr = match_len;
        return match_char;
    }
}

static int input_method_fd;

void load_input_methods(void)
{
    char buf[1024], *q;
    long file_size;
    int fd, offset;
    const unsigned char *file_ptr, *p;
    InputMethod *m;

    input_method_fd = -1;
    
    if (find_resource_file(buf, sizeof(buf), "kmaps") < 0)
        return;

    fd = open (buf, O_RDONLY);
    if (fd < 0)
        return;
    file_size = lseek(fd, 0, SEEK_END);
    file_ptr = mmap(NULL, file_size, PROT_READ, MAP_SHARED, fd, 0);
    if (file_ptr == MAP_FAILED) {
    fail:
        close(fd);
        return;
    }
    
    if (memcmp(file_ptr, "kmap", 4) != 0)
        goto fail;
    
    p = file_ptr + 4;
    for(;;) {
        offset = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
        p+= 4;
        if (offset == 0)
            break;
        m = malloc(sizeof(InputMethod));
        if (m) {
            m->data = file_ptr + offset;
            m->input_match = kmap_input;
            q = buf;
            while (*p != '\0') {
                if ((q - buf) < (sizeof(buf) - 1)) 
                    *q++ = *p;
                p++;
            }
            *q = '\0';
            p++;
            m->name =strdup(buf);
            register_input_method(m);
        }
    }

    input_method_fd = fd;
}

void unload_input_methods(void)
{
    if (input_method_fd >= 0) {
        close(input_method_fd);
        input_method_fd = -1;
    }
}
#endif

void input_completion(StringArray *cs, const char *input)
{
    InputMethod *m;
    int len;

    len = strlen(input);
    for(m = input_methods; m != NULL; m = m->next) {
        if (!strncmp(m->name, input, len))
            add_string(cs, m->name);
    }
}

void do_set_input_method(EditState *s, const char *input_str)
{
    InputMethod *m;

    m = input_methods;
    for(;;) {
        if (!m) {
            put_status(s, "'%s' not found", input_str);
            return;
        }
        if (!strcmp(input_str, m->name))
            break;
        m = m->next;
    }
    s->input_method = m;
    s->selected_input_method = m;
}

void do_switch_input_method(EditState *s)
{
    if (!s->input_method)
         s->input_method = s->selected_input_method;
    else
        s->input_method = NULL;
}

void init_input_methods(void)
{
    register_input_method(&default_input_method);
    register_input_method(&unicode_input_method);
    register_completion("input", input_completion);
#ifdef CONFIG_ALL_KMAPS
    load_input_methods();
#endif
}

void close_input_methods(void)
{
#ifdef CONFIG_ALL_KMAPS
    unload_input_methods();
#endif
}
