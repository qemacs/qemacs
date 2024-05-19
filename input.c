/*
 * Input method handling for QEmacs.
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

static int default_input(qe__unused__ int *match_buf,
                         qe__unused__ int match_buf_size,
                         qe__unused__ int *match_len_ptr,
                         qe__unused__ const u8 *data,
                         qe__unused__ const char32_t *buf,
                         qe__unused__ int len)
{
    return INPUTMETHOD_NOMATCH;
}

static int unicode_input(int *match_buf,
                         int match_buf_size,
                         int *match_len_ptr,
                         qe__unused__ const u8 *data,
                         const char32_t *buf, int len)
{
    int i, h, c;
    if (buf[0] != 'x')
        return INPUTMETHOD_NOMATCH;

    c = 0;
    if (len > 5)
        len = 5;
    for (i = 1; i < len; i++) {
        h = qe_digit_value(buf[i]);
        if (h >= 16)
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

static void input_complete(CompleteState *cp, CompleteFunc enumerate) {
    QEmacsState *qs = cp->s->qe_state;
    InputMethod *m;

    for (m = qs->input_methods; m != NULL; m = m->next) {
        enumerate(cp, m->name, CT_IGLOB);
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

static CompletionDef input_completion = {
    "input", input_complete,
};

void input_methods_init(QEmacsState *qs)
{
    register_input_method(&default_input_method);
    register_input_method(&unicode_input_method);
    qe_register_completion(&input_completion);
}
