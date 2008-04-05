/*
 * Input method handling for QEmacs.
 *
 * Copyright (c) 2000 Fabrice Bellard.
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

static int default_input(__unused__ int *match_buf,
                         __unused__ int match_buf_size,
                         __unused__ int *match_len_ptr,
                         __unused__ const u8 *data,
                         __unused__ const unsigned int *buf,
                         __unused__ int len)
{
    return INPUTMETHOD_NOMATCH;
}

static int unicode_input(int *match_buf,
                         int match_buf_size,
                         int *match_len_ptr,
                         __unused__ const u8 *data,
                         const unsigned int *buf, int len)
{
    int i, h, c;
    if (buf[0] != 'x')
        return INPUTMETHOD_NOMATCH;

    c = 0;
    if (len > 5)
        len = 5;
    for (i = 1; i < len; i++) {
        h = to_hex(buf[i]);
        if (h == -1)
            return INPUTMETHOD_NOMATCH;
        c = (c << 4) | h;
    }
    if (len == 5) {
        *match_len_ptr = len;
        match_buf[0] = c;
        return 1;
    } else {
        return INPUTMETHOD_MORECHARS;
    }
}

static InputMethod default_input_method = {
    "default", default_input, NULL, NULL,
};

static InputMethod unicode_input_method = {
    "unicode", unicode_input, NULL, NULL,
};

void register_input_method(InputMethod *m)
{
    QEmacsState *qs = &qe_state;
    InputMethod **p;

    p = &qs->input_methods;
    while (*p != NULL) {
        p = &(*p)->next;
    }
    m->next = NULL;
    *p = m;
}

static void input_completion(CompleteState *cp)
{
    QEmacsState *qs = cp->s->qe_state;
    InputMethod *m;

    for (m = qs->input_methods; m != NULL; m = m->next) {
        complete_test(cp, m->name);
    }
}

static InputMethod *find_input_method(const char *name)
{
    QEmacsState *qs = &qe_state;
    InputMethod *m;

    for (m = qs->input_methods; m != NULL; m = m->next) {
        if (strequal(m->name, name))
            return m;
    }
    return NULL;
}

void do_set_input_method(EditState *s, const char *name)
{
    InputMethod *m = find_input_method(name);

    if (m) {
        s->input_method = m;
        s->selected_input_method = m;
    } else {
        put_status(s, "'%s' not found", name);
    }
}

void do_switch_input_method(EditState *s)
{
    if (s->input_method)
        s->input_method = NULL;
    else
        s->input_method = s->selected_input_method;
}

void init_input_methods(void)
{
    register_input_method(&default_input_method);
    register_input_method(&unicode_input_method);
    register_completion("input", input_completion);
}
