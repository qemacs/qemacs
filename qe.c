/*
 * QEmacs, tiny but powerful multimode editor
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard.
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
#include "qfribidi.h"
#ifdef CONFIG_DLL
#include <dlfcn.h>
#endif

/* each history list */
typedef struct HistoryEntry {
    struct HistoryEntry *next;
    StringArray history;
    char name[32];
} HistoryEntry;

static int (*__initcall_first)(void) __init_call = NULL;
static void (*__exitcall_first)(void) __exit_call = NULL;

static int get_line_height(QEditScreen *screen, int style_index);
void print_at_byte(QEditScreen *screen,
                   int x, int y, int width, int height,
                   const char *str, int style_index);
static void do_cmd_set_mode(EditState *s, const char *name);
void do_delete_window(EditState *s, int force);
void do_end_macro(EditState *s);
static void get_default_path(EditState *s, char *buf, int buf_size);
static EditBuffer *predict_switch_to_buffer(EditState *s);
static StringArray *get_history(const char *name);
static void qe_key_process(int key);

ModeSavedData *generic_mode_save_data(EditState *s);
void generic_text_display(EditState *s);
static void display1(DisplayState *s);
#ifndef CONFIG_TINY
static void save_selection(void);
#endif
static CompletionFunc find_completion(const char *name);

QEmacsState qe_state;
static ModeDef *first_mode = NULL;
static KeyDef *first_key = NULL;
static CmdDef *first_cmd = NULL;
static CompletionEntry *first_completion = NULL;
static HistoryEntry *first_history = NULL;
static QEditScreen global_screen;
static int screen_width = 0;
static int screen_height = 0;

/* mode handling */

void qe_register_mode(ModeDef *m)
{
    ModeDef **p;
    CmdDef *table;

    /* record mode in mode list */
    p = &first_mode;
    while (*p != NULL) p = &(*p)->next;
    m->next = NULL;
    *p = m;
    
    /* add missing functions */
    if (!m->display)
        m->display = generic_text_display;
    if (!m->mode_save_data)
        m->mode_save_data = generic_mode_save_data;
    if (!m->data_type)
        m->data_type = &raw_data_type;
    if (!m->mode_line)
        m->mode_line = text_mode_line;
    
    /* add a new command to switch to that mode */
    if (!(m->mode_flags & MODEF_NOCMD)) {
        char buf[64], *args, *name;
        int size;

        table = malloc(sizeof(CmdDef) * 2);
        memset(table, 0, sizeof(CmdDef) * 2);
        table->key = KEY_NONE;
        table->alt_key = KEY_NONE;

        /* lower case convert for C mode */
        pstrcpy(buf, sizeof(buf) - 10, m->name);
        css_strtolower(buf, sizeof(buf));
        pstrcat(buf, sizeof(buf) - 10, "-mode");
        args = buf + strlen(buf) + 1;
        strcpy(args, "S"); /* constant string parameter */
        size = strlen(args) + strlen(buf) + 2;
        name = malloc(size);
        memcpy(name, buf, size);
        table->name = name;
        table->action.func = (void*)do_cmd_set_mode;
        table->val = strdup(m->name);
        qe_register_cmd_table(table, NULL);
    }
}

static ModeDef *find_mode(const char *mode_name)
{
    ModeDef *p;
    p = first_mode;
    while (p != NULL) {
        if (!strcmp(mode_name, p->name))
            return p;
        p = p->next;
    }
    return NULL;
}

/* commands handling */

CmdDef *qe_find_cmd(const char *cmd_name)
{
    CmdDef *d;
    
    d = first_cmd;
    while (d != NULL) {
        while (d->name != NULL) {
            if (!strcmp(cmd_name, d->name))
                return d;
            d++;
        }
        d = d->action.next;
    }
    return NULL;
}

static int qe_register_binding1(unsigned int *keys, int nb_keys,
                                CmdDef *d, ModeDef *m)
{
    KeyDef **lp, *p;

    /* add key */
    p = malloc(sizeof(KeyDef) + (nb_keys - 1) * sizeof(unsigned int));
    if (!p)
        return -1;
    p->nb_keys = nb_keys;
    p->cmd = d;
    p->mode = m;
    memcpy(p->keys, keys, nb_keys * sizeof(unsigned int));
    /* find position : mode keys should be before generic keys */
    if (m == NULL) {
        lp = &first_key;
        while (*lp != NULL) lp = &(*lp)->next;
        *lp = p;
        p->next = NULL;
    } else {
        p->next = first_key;
        first_key = p;
    }
    return 0;
}

/* convert compressed mappings to real ones */
static void qe_register_binding2(int key,
                                 CmdDef *d, ModeDef *m)
{
    int nb_keys;
    unsigned int keys[3];
    nb_keys = 0;
    if (key >= KEY_CTRLX(0) && key <= KEY_CTRLX(0xff)) {
        keys[nb_keys++] = KEY_CTRL('x');
        keys[nb_keys++] = key & 0xff;
    } else if (key >= KEY_CTRLXRET(0) && key <= KEY_CTRLXRET(0xff)) {
        keys[nb_keys++] = KEY_CTRL('x');
        keys[nb_keys++] = KEY_RET;
        keys[nb_keys++] = key & 0xff;
    } else if (key >= KEY_CTRLH(0) && key <= KEY_CTRLH(0xff)) {
        keys[nb_keys++] = KEY_CTRL('h');
        keys[nb_keys++] = key & 0xff;
    } else {
        keys[nb_keys++] = key;
    }
    qe_register_binding1(keys, nb_keys, d, m);
}

/* if mode is non NULL, the defined keys are only active in this mode */
void qe_register_cmd_table(CmdDef *cmds, const char *mode)
{
    CmdDef **ld, *d;
    ModeDef *m;

    m = NULL;
    if (mode)
        m = find_mode(mode);

    /* find last command table */
    ld = &first_cmd;
    while (*ld != NULL) {
        d = *ld;
        while (d->name != NULL) {
            d++;
        }
        ld = &d->action.next;
    }
    /* add new command table */
    *ld = cmds;

    /* add default bindings */
    d = cmds;
    while (d->name != NULL) {
        if (d->key != KEY_NONE)
            qe_register_binding2(d->key, d, m);
        if (d->alt_key != KEY_NONE)
            qe_register_binding2(d->alt_key, d, m);
        d++;
    }
}

/* key binding handling */

void qe_register_binding(int key, const char *cmd_name, const char *mode_names)
{
    CmdDef *d;
    ModeDef *m;
    const char *p, *r;
    char mode_name[64];

    d = qe_find_cmd(cmd_name);
    if (!d)
        return;
    if (!mode_names || mode_names[0] == '\0') {
        qe_register_binding2(key, d, NULL);
    } else {
        p = mode_names;
        for(;;) {
            r = strchr(p, '|');
            /* XXX: overflows */
            if (!r) {
                strcpy(mode_name, p);
            } else {
                memcpy(mode_name, p, r - p);
                mode_name[r - p] = '\0';
            }
            m = find_mode(mode_name);
            if (m) {
                qe_register_binding2(key, d, m);
            }
            if (!r)
                break;
            p = r + 1;
        }
    }
}

void command_completion(StringArray *cs, const char *input)
{
    int count, len;
    CmdDef *d;
    count = 0;
    
    len = strlen(input);
    d = first_cmd;
    while (d != NULL) {
        while (d->name != NULL) {
            if (!strncmp(d->name, input, len))
                add_string(cs, d->name);
            d++;
        }
        d = d->action.next;
    }
}

#define MAX_KEYS 10

void do_global_set_key(EditState *s, const char *keystr, const char *cmd_name)
{
    int key, nb_keys;
    unsigned int keys[MAX_KEYS];
    const char *p;
    CmdDef *d;

    p = keystr;
    nb_keys = 0;
    for(;;) {
        skip_spaces(&p);
        if (*p == '\0')
            break;
        key = strtokey(&p);
        keys[nb_keys++] = key;
        if (nb_keys >= MAX_KEYS)
            break;
    }
    d = qe_find_cmd(cmd_name);
    if (!d)
        return;
    qe_register_binding1(keys, nb_keys, d, NULL);
}

/* basic editing functions */
void do_bof(EditState *s)
{
    s->offset = 0;
}

void do_eof(EditState *s)
{
    s->offset = s->b->total_size;
}

void do_bol(EditState *s)
{
    if (s->mode->move_bol)
        s->mode->move_bol(s);
}

void do_eol(EditState *s)
{
    if (s->mode->move_eol)
        s->mode->move_eol(s);
}

void do_word_right(EditState *s, int dir)
{
    if (s->mode->move_word_left_right) 
        s->mode->move_word_left_right(s, dir);
}

void text_move_bol(EditState *s)
{
    int c, offset1;

    for(;;) {
        if (s->offset <= 0)
            break;
        c = eb_prevc(s->b, s->offset, &offset1);
        if (c == '\n')
            break;
        s->offset = offset1;
    }
}

void text_move_eol(EditState *s)
{
    int c, offset1;

    for(;;) {
        c = eb_nextc(s->b, s->offset, &offset1);
        if (c == '\n')
            break;
        s->offset = offset1;
    }
}

int isword(int c)
{
    /* XXX: any unicode char >= 128 is considered as word. */
    return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        (c == '_') || (c >= 128);
}

static void word_right(EditState *s, int w)
{
    int c, offset1;

    for(;;) {
        if (s->offset >= s->b->total_size)
            break;
        c = eb_nextc(s->b, s->offset, &offset1);
        if (isword(c) == w)
            break;
        s->offset = offset1;
    }
}

static void word_left(EditState *s, int w)
{
    int c, offset1;

    for(;;) {
        if (s->offset == 0)
            break;
        c = eb_prevc(s->b, s->offset, &offset1);
        if (isword(c) == w)
            break;
        s->offset = offset1;
    }
}

void text_move_word_left_right(EditState *s, int dir)
{
    if (dir > 0) {
        word_right(s, 1);
        word_right(s, 0);
    } else {
        word_left(s, 1);
        word_left(s, 0);
    }
}

/* paragraph handling */

int eb_next_paragraph(EditBuffer *b, int offset)
{
    int text_found;

    offset = eb_goto_bol(b, offset);
    /* find end of paragraph */
    text_found = 0;
    for(;;) {
        if (offset >= b->total_size)
            break;
        if (eb_is_empty_line(b, offset)) {
            if (text_found)
                break;
        } else {
            text_found = 1;
        }
        offset = eb_next_line(b, offset);
    }
    return offset;
}

int eb_start_paragraph(EditBuffer *b, int offset)
{
    for(;;) {
        offset = eb_goto_bol(b, offset);
        if (offset <= 0)
            break;
        /* check if only spaces */
        if (eb_is_empty_line(b, offset)) {
            offset = eb_next_line(b, offset);
            break;
        }
        eb_prevc(b, offset, &offset);
    }
    return offset;
}

void do_backward_paragraph(EditState *s)
{
    int offset;

    offset = s->offset;
    /* skip empty lines */
    for(;;) {
        if (offset <= 0)
            break;
        offset = eb_goto_bol(s->b, offset);
        if (!eb_is_empty_line(s->b, offset))
            break;
        /* line just before */
        eb_prevc(s->b, offset, &offset);
    }

    offset = eb_start_paragraph(s->b, offset);

    /* line just before */
    eb_prevc(s->b, offset, &offset);
    offset = eb_goto_bol(s->b, offset);

    s->offset = offset;
}

void do_forward_paragraph(EditState *s)
{
    s->offset = eb_next_paragraph(s->b, s->offset);
}

#define PARAGRAPH_WIDTH 76

void do_fill_paragraph(EditState *s)
{
    int par_start, par_end, col;
    int offset, offset1, n, c, line_count, indent_size;
    int chunk_start, word_start, word_size, word_count, space_size;
    unsigned char buf[1];
    
    /* find start & end of paragraph */
    par_start = eb_start_paragraph(s->b, s->offset);
    par_end = eb_next_paragraph(s->b, par_start);
    
    /* compute indent size */
    indent_size = 0;
    offset = eb_next_line(s->b, par_start);
    if (!eb_is_empty_line(s->b, offset)) {
        while (offset < par_end) {
            c = eb_nextc(s->b, offset, &offset);
            if (!isspace(c))
                break;
            indent_size++;
        }
    }

    /* suppress any spaces in between */
    col = 0;
    offset = par_start;
    word_count = 0;
    line_count = 0;
    while (offset < par_end) {
        /* skip spaces */
        chunk_start = offset;
        space_size = 0;
        while (offset < par_end) {
            c = eb_nextc(s->b, offset, &offset1);
            if (!isspace(c))
                break;
            offset = offset1;
            space_size++;
        }
        /* skip word */
        word_start = offset;
        word_size = 0;
        while (offset < par_end) {
            c = eb_nextc(s->b, offset, &offset1);
            if (isspace(c))
                break;
            offset = offset1;
            word_size++;
        }
        
        if (word_count == 0) {
            /* first word: preserve spaces */
            col += space_size + word_size;
        } else {
            /* insert space single space then word */
            if (offset == par_end || 
                (col + 1 + word_size > PARAGRAPH_WIDTH)) {
                buf[0] = '\n';
                eb_write(s->b, chunk_start, buf, 1);
                chunk_start++;
                if (offset < par_end) {
                    /* indent */
                    buf[0] = ' ';
                    for(n = indent_size; n > 0; n--)
                        eb_insert(s->b, chunk_start, buf, 1);
                    chunk_start += indent_size;

                    word_start += indent_size;
                    offset += indent_size;
                    par_end += indent_size;
                }
                col = word_size + indent_size;
            } else {
                buf[0] = ' ';
                eb_write(s->b, chunk_start, buf, 1);
                chunk_start++;
                col += 1 + word_size;
            }

            /* remove all other spaces if needed */
            n = word_start - chunk_start;
            if (n > 0) {
                eb_delete(s->b, chunk_start, n);
                offset -= n;
                par_end -= n;
            }
        }
        word_count++;
    }
}

/* upper / lower case functions (XXX: use generic unicode
   function). Return next offset */
static int eb_changecase(EditBuffer *b, int offset, int up)
{
    int offset1, ch, len;
    char buf[MAX_CHAR_BYTES];

    ch = eb_nextc(b, offset, &offset1);
    if (ch < 128) {
        if (up)
            ch = toupper(ch);
        else
            ch = tolower(ch);
    }
    len = unicode_to_charset(buf, ch, b->charset);
    if (len == (offset1 - offset)) {
        eb_write(b, offset, buf, len);
    } else {
        eb_delete(b, offset, offset1 - offset);
        eb_insert(b, offset, buf, len);
    }
    return offset + len;
}

void do_changecase_word(EditState *s, int up)
{
    int c;
    
    word_right(s, 1);
    for(;;) {
        if (s->offset >= s->b->total_size)
            break;
        c = eb_nextc(s->b, s->offset, NULL);
        if (!isword(c))
            break;
        s->offset = eb_changecase(s->b, s->offset, up);
    }
}

void do_changecase_region(EditState *s, int up)
{
    int offset;

    /* WARNING: during case change, the region offsets can change, so
       it is not so simple ! */
    if (s->offset > s->b->mark) 
        offset = s->b->mark;
    else
        offset = s->offset;
    for(;;) {
        if (s->offset > s->b->mark) {
            if (offset >= s->offset)
                break;
        } else {
            if (offset >= s->b->mark)
                break;
        }
        offset = eb_changecase(s->b, offset, up);
    }
}

void do_delete_word(EditState *s, int dir)
{
    int start = s->offset;
    int end;

    if (s->b->flags & BF_READONLY)
        return;

    if (s->mode->move_word_left_right)
        s->mode->move_word_left_right(s, dir);
    end = s->offset;
    if (start < end)
      eb_delete(s->b, start, end-start);
    else
      eb_delete(s->b, end, start-end);
}

void do_delete_char(EditState *s)
{
    int offset1;

    eb_nextc(s->b, s->offset, &offset1);
    eb_delete(s->b, s->offset, offset1 - s->offset);
}

void do_backspace(EditState *s)
{
    int offset1;

    eb_prevc(s->b, s->offset, &offset1);
    if (offset1 < s->offset) {
        eb_delete(s->b, offset1, s->offset - offset1);
        s->offset = offset1;
        /* special case for composing */
        if (s->compose_len > 0)
            s->compose_len--;
    }
}

/* return the cursor position relative to the screen. Note that xc is
   given in pixel coordinates */
typedef struct {
    int linec;
    int yc;
    int xc;
    int offsetc;
    DirType basec; /* direction of the line */
    DirType dirc; /* direction of the char under the cursor */
    int cursor_width; /* can be negative depending on char orientation */
    int cursor_height;
} CursorContext;

int cursor_func(DisplayState *ds,
                int offset1, int offset2, int line_num,
                int x, int y, int w, int h, int hex_mode)
{
    CursorContext *m = ds->cursor_opaque;

    if (m->offsetc >= offset1 &&
        m->offsetc < offset2) {
        m->xc = x;
        m->yc = y;
        m->basec = ds->base;
        m->dirc = ds->base; /* XXX: do it */
        m->cursor_width = w; 
        m->cursor_height = h; 
        m->linec = line_num;
#if 0
        printf("cursor_func: xc=%d yc=%d linec=%d offset: %d<=%d<%d\n", 
               m->xc, m->yc, m->linec, offset1, m->offsetc, offset2);
#endif
        return -1;
    } else {
        return 0;
    }
}

void get_cursor_pos(EditState *s, CursorContext *m)
{
    DisplayState ds1, *ds = &ds1;
    
    display_init(ds, s, DISP_CURSOR);
    ds->cursor_opaque = m;
    ds->cursor_func = cursor_func;
    m->offsetc = s->offset;
    m->xc = m->yc = NO_CURSOR;
    display1(ds);
}

typedef struct {
    int yd;
    int xd;
    int xdmin;
    int offsetd;
} MoveContext;

/* called each time the cursor could be displayed */
static int down_cursor_func(DisplayState *ds,
                            int offset1, int offset2, int line_num,
                            int x, int y, int w, int h, int hex_mode)
{
    int d;
    MoveContext *m = ds->cursor_opaque;

    if (line_num == m->yd) {
        /* find the closest char */
        d = abs(x - m->xd);
        if (d < m->xdmin) {
            m->xdmin = d;
            m->offsetd = offset1;
        }
        return 0;
    } else if (line_num > m->yd) {
        /* no need to explore more chars */
        return -1;
    } else {
        return 0;
    }
}

void do_up_down(EditState *s, int dir)
{
    if (s->mode->move_up_down)
        s->mode->move_up_down(s, dir);
}

void do_left_right(EditState *s, int dir)
{
    if (s->mode->move_left_right)
        s->mode->move_left_right(s, dir);
}

static int up_down_last_x = -1;

void text_move_up_down(EditState *s, int dir)
{
    MoveContext m1, *m = &m1;
    DisplayState ds1, *ds = &ds1;
    CursorContext cm;

    if (s->qe_state->last_cmd_func != do_up_down)
        up_down_last_x = -1;

    get_cursor_pos(s, &cm);
    if (cm.xc == NO_CURSOR)
        return;

    if (up_down_last_x == -1)
        up_down_last_x = cm.xc;

    if (dir < 0) {
        /* difficult case: we need to go backward on displayed text */
        while (cm.linec <= 0) {
            if (s->offset_top <= 0)
                return; 
            s->offset_top = s->mode->text_backward_offset(s, s->offset_top - 1);

            /* adjust y_disp so that the cursor is at the same position */
            s->y_disp += cm.yc;
            get_cursor_pos(s, &cm);
            s->y_disp -= cm.yc;
        }
    }

    /* find cursor offset */
    m->yd = cm.linec + dir;
    m->xd = up_down_last_x;
    m->xdmin = 0x7fffffff;
    /* if no cursor position is found, we go to bof or eof according
       to dir */
    if (dir > 0)
        m->offsetd = s->b->total_size;
    else
        m->offsetd = 0;
    display_init(ds, s, DISP_CURSOR);
    ds->cursor_opaque = m;
    ds->cursor_func = down_cursor_func;
    display1(ds);
    s->offset = m->offsetd;
}

typedef struct {
    int y_found;
    int offset_found;
    int dir;
    int offsetc;
} ScrollContext;

/* called each time the cursor could be displayed */
static int scroll_cursor_func(DisplayState *ds,
                              int offset1, int offset2, int line_num,
                              int x, int y, int w, int h, int hex_mode)
{
    ScrollContext *m = ds->cursor_opaque;
    int y1;

    y1 = y + h;
    /* XXX: add bidir handling : position cursor on left / right */
    if (m->dir < 0) {
        if (y >= 0 && y < m->y_found) {
            m->y_found = y;
            m->offset_found = offset1;
        }
    } else {
        if (y1 <= ds->height && y1 > m->y_found) {
            m->y_found = y1;
            m->offset_found = offset1;
        }
    }
    if (m->offsetc >= offset1 && m->offsetc < offset2 &&
        y >= 0 && y1 <= ds->height) {
        m->offset_found = m->offsetc;
        m->y_found = 0x7fffffff * m->dir; /* ensure that no other
                                             position will be found */
        return -1;
    }
    return 0;
}

void do_scroll_up_down(EditState *s, int dir)
{
    if (s->mode->scroll_up_down)
        s->mode->scroll_up_down(s, dir);
}

void perform_scroll_up_down(EditState *s, int h)
{
    ScrollContext m1, *m = &m1;
    DisplayState ds1, *ds = &ds1;
    int dir;

    if (h < 0)
        dir = -1;
    else
        dir = 1;
        
    /* move display up/down */
    s->y_disp -= h;

    /* y_disp should not be > 0. So we update offset_top until we have
       it negative */
    if (s->y_disp > 0) {
        display_init(ds, s, DISP_CURSOR_SCREEN);
        do {
            if (s->offset_top <= 0) {
                /* cannot go back: we stay at the top of the screen and
                   exit loop */
                s->y_disp = 0;
            } else {
                s->offset_top = s->mode->text_backward_offset(s, s->offset_top - 1);
                ds->y = 0;
                s->mode->text_display(s, ds, s->offset_top);
                s->y_disp -= ds->y;
            }
        } while (s->y_disp > 0);
    }

    /* now update cursor position so that it is on screen */
    m->offsetc = s->offset;
    m->dir = -dir;
    m->y_found = 0x7fffffff * dir;
    m->offset_found = s->offset; /* default offset */
    display_init(ds, s, DISP_CURSOR_SCREEN);
    ds->cursor_opaque = m;
    ds->cursor_func = scroll_cursor_func;
    display1(ds);

    s->offset = m->offset_found;
}

void text_scroll_up_down(EditState *s, int dir)
{
    int h, line_height;
    /* try to round to a line height */
    line_height = get_line_height(s->screen, s->default_style);
    h = (s->height / line_height) - 1;
    if (h < 1)
        h = 1;
    h = h * line_height;
    
    perform_scroll_up_down(s, dir * h);
}

/* center the cursor in the window */
/* XXX: make it generic to all modes */
void center_cursor(EditState *s)
{
    CursorContext cm;

    /* only apply to text modes */
    if (!s->mode->text_display)
        return;

    get_cursor_pos(s, &cm);
    if (cm.xc == NO_CURSOR)
        return;

    /* try to center display */
    perform_scroll_up_down(s, -((s->height / 2) - cm.yc));
}

/* called each time the cursor could be displayed */
typedef struct {
    int yd;
    int xd;
    int xdmin;
    int offsetd;
    int dir;
    int after_found;
} LeftRightMoveContext;

static int left_right_cursor_func(DisplayState *ds,
                                  int offset1, int offset2, int line_num,
                                  int x, int y, int w, int h, int hex_mode)
{
    int d;
    LeftRightMoveContext *m = ds->cursor_opaque;

    if (line_num == m->yd && 
        ((m->dir < 0 && x < m->xd) || 
         (m->dir > 0 && x > m->xd))) {
        /* find the closest char in the correct direction */
        d = abs(x - m->xd);
        if (d < m->xdmin) {
            m->xdmin = d;
            m->offsetd = offset1;
        }
        return 0;
    } else if (line_num > m->yd) {
        m->after_found = 1;
        /* no need to explore more chars */
        return -1;
    } else {
        return 0;
    }
}

/* go to left or right in visual order */
void text_move_left_right_visual(EditState *s, int dir)
{
    LeftRightMoveContext m1, *m = &m1;
    DisplayState ds1, *ds = &ds1;
    int xc, yc, nextline;
    CursorContext cm;

    get_cursor_pos(s, &cm);
    xc = cm.xc;
    yc = cm.linec;

    nextline = 0;
    for(;;) {
        /* find cursor offset */
        m->yd = yc;
        if (!nextline) {
            m->xd = xc;
        } else {
            m->xd = -dir * 0x3fffffff;  /* not too big to avoid overflow */
        }
        m->xdmin = 0x7fffffff;
        m->offsetd = -1;
        m->dir = dir;
        m->after_found = 0;
        display_init(ds, s, DISP_CURSOR);
        ds->cursor_opaque = m;
        ds->cursor_func = left_right_cursor_func;
        display1(ds);
        if (m->offsetd >= 0) {
            /* position found : update and exit */
            s->offset = m->offsetd;
            break;
        } else {
            if (dir > 0) {
                /* no suitable position found : go to next line */
                /* if no char after, no need to continue */
                if (!m->after_found)
                   break;
            } else {
                /* no suitable position found : go to previous line */
                if (yc <= 0) {
                    if (s->offset_top <= 0)
                        break;
                    s->offset_top = s->mode->text_backward_offset(s, s->offset_top - 1);
                    /* adjust y_disp so that the cursor is at the same position */
                    s->y_disp += cm.yc;
                    get_cursor_pos(s, &cm);
                    s->y_disp -= cm.yc;
                    yc = cm.linec;
                }
            }
            yc += dir;
            nextline = 1;
        }
    }
}

/* mouse get cursor func */
#ifndef CONFIG_TINY

/* called each time the cursor could be displayed */
typedef struct {
    int yd;
    int xd;
    int dy_min;
    int dx_min;
    int offset_found;
    int hex_mode;
} MouseGotoContext;

/* distance from x to segment [x1,x2-1] */
static int seg_dist(int x, int x1, int x2)
{
    if (x <= x1)
        return x1 - x;
    else if (x >= x2)
        return x - x2 + 1;
    else
        return 0;
}

/* XXX: would need two pass in the general case (first search line,
   then colunm */
static int mouse_goto_func(DisplayState *ds,
                           int offset1, int offset2, int line_num,
                           int x, int y, int w, int h, int hex_mode)
{
    MouseGotoContext *m = ds->cursor_opaque;
    int dy, dx;

    dy = seg_dist(m->yd, y, y + h);
    if (dy < m->dy_min) {
        m->dy_min = dy;
        m->dx_min = 0x3fffffff;
    }
    if (dy == m->dy_min) {
        dx = seg_dist(m->xd, x, x + w);
        if (dx < m->dx_min) {
            m->dx_min = dx;
            m->offset_found = offset1;
            m->hex_mode = hex_mode;
            /* fast exit test */
            if (dy == 0 && dx == 0)
                return -1;
        }
    }
    return 0;
}

/* go to left or right in visual order. In hex mode, a side effect is
   to select the right column. */
void text_mouse_goto(EditState *s, int x, int y)
{
    QEmacsState *qs = s->qe_state;
    MouseGotoContext m1, *m = &m1;
    DisplayState ds1, *ds = &ds1;

    m->dx_min = 0x3fffffff;
    m->dy_min = 0x3fffffff;
    m->xd = x;
    m->yd = y;
    m->offset_found = s->offset; /* fail safe */
    m->hex_mode = s->hex_mode;
    
    display_init(ds, s, DISP_CURSOR_SCREEN);
    ds->hex_mode = -1; /* we select both hex chars and normal chars */
    ds->cursor_opaque = m;
    ds->cursor_func = mouse_goto_func;
    display1(ds);

    s->offset = m->offset_found;
    s->hex_mode = m->hex_mode;

    /* activate window (need more ideas for popups) */
    if (!(s->flags & WF_POPUP))
        qs->active_window = s;
    if (s->mouse_force_highlight)
        s->force_highlight = 1;
}
#else
void text_mouse_goto(EditState *s, int x, int y)
{
}
#endif

void do_char(EditState *s, int key)
{
    if (s->b->flags & BF_READONLY)
        return;
    if (s->mode->write_char)
        s->mode->write_char(s, key);
}

void text_write_char(EditState *s, int key)
{
    int cur_ch, len, cur_len, offset1, ret, insert;
    char buf[MAX_CHAR_BYTES];

    cur_ch = eb_nextc(s->b, s->offset, &offset1);
    cur_len = offset1 - s->offset;
    len = unicode_to_charset(buf, key, s->b->charset);
    insert = (s->insert || cur_ch == '\n');
        
    if (insert) {
        const InputMethod *m;
        int match_len, offset, i, offset1;
            
        /* use compose system only if insert mode */
        if (s->compose_len == 0) 
            s->compose_start_offset = s->offset;

        /* insert char */
        eb_insert(s->b, s->offset, buf, len);
        s->offset += len;

        s->compose_buf[s->compose_len++] = key;
        m = s->input_method;
        for(;;) {
            if (!m) {
                s->compose_len = 0;
                break;
            }
            ret = m->input_match(&match_len, m->data, s->compose_buf, 
                                 s->compose_len);
            if (ret == INPUTMETHOD_NOMATCH) {
                /* no match : reset compose state */
                    
                s->compose_len = 0;
                break;
            } else if (ret == INPUTMETHOD_MORECHARS) {
                /* more chars expected: do nothing and insert current key */
                break;
            } else {
                /* match : delete matched chars */
                key = ret;
                offset = s->compose_start_offset;
                offset1 = s->offset; /* save offset so that we are not disturb
                                        when it moves in eb_delete() */
                for(i=0;i<match_len;i++)
                    eb_nextc(s->b, offset, &offset);
                eb_delete(s->b, s->compose_start_offset, 
                          offset - s->compose_start_offset);
                s->compose_len -= match_len;
                umemmove(s->compose_buf, s->compose_buf + match_len,
                         s->compose_len);
                /* then insert match */
                len = unicode_to_charset(buf, key, s->b->charset);
                eb_insert(s->b, s->compose_start_offset, buf, len);
                s->offset = offset1 + len - (offset - s->compose_start_offset);
                s->compose_start_offset += len;
                /* if some compose chars are left, we iterate */
                if (s->compose_len == 0)
                    break;
            }
        }
    } else {
        if (cur_len == len) {
            eb_write(s->b, s->offset, buf, len);
        } else {
            eb_delete(s->b, s->offset, cur_len);
            eb_insert(s->b, s->offset, buf, len);
        }
        s->offset += len;
    }
}

/* XXX: may be better to move it into qe_key_process() */
static void quote_grab_key(void *opaque, int key)
{
    QEmacsState *qs = &qe_state;
    EditState *s;

    s = qs->active_window;
    if (!s)
        return;
    if (!KEY_SPECIAL(key) ||
        (key >= 0 && key <= 31)) {
        do_char(s, key);
        edit_display(qs);
        dpy_flush(&global_screen);
    }
    qe_ungrab_keys();
}

void do_quote(EditState *s)
{
    qe_grab_keys(quote_grab_key, NULL);
}

void do_insert(EditState *s)
{
    s->insert = !s->insert;
}

void do_tab(EditState *s)
{
    do_char(s, 9);
}

void do_open_line(EditState *s)
{
    u8 ch;

    if (s->b->flags & BF_READONLY)
        return;

    ch = '\n';
    eb_insert(s->b, s->offset, &ch, 1);
}

void do_return(EditState *s)
{
    do_open_line(s);
    s->offset++;
}

void do_break(EditState *s)
{
    /* well, currently nothing needs to be aborted in global context */
}

/* block functions */
void do_set_mark(EditState *s)
{
    s->b->mark = s->offset;
}

EditBuffer *new_yank_buffer(void)
{
    QEmacsState *qs = &qe_state;
    EditBuffer *b;

    if (++qs->yank_current == NB_YANK_BUFFERS)
        qs->yank_current = 0;
    b = qs->yank_buffers[qs->yank_current];
    if (b)
        eb_free(b);
    b = eb_new("*yank*", BF_SYSTEM);
    qs->yank_buffers[qs->yank_current] = b;
    return b;
}

void do_kill_region(EditState *s, int kill)
{
    int len, p1, p2, tmp, offset1;
    QEmacsState *qs = s->qe_state;
    EditBuffer *b;

    if (s->b->flags & BF_READONLY)
        return;

    p2 = s->offset;
    if (kill == 2) {
        /* kill line */
        if (eb_nextc(s->b, p2, &offset1) == '\n') {
            p1 = offset1;
        } else {
            p1 = p2;
            while (eb_nextc(s->b, p1, &offset1) != '\n') {
                p1 = offset1;
            }
        }
    } else {
        /* kill/copy region */
        p1 = s->b->mark;
    }

    if (p1 > p2) {
        tmp = p1;
        p1 = p2;
        p2 = tmp;
    }
    len = p2 - p1;
    b = new_yank_buffer();
    eb_insert_buffer(b, 0, s->b, p1, len);
    if (kill) {
        eb_delete(s->b, p1, len);
        s->offset = p1;
    }
    selection_activate(qs->screen);
}

void do_yank(EditState *s)
{
    int size;
    QEmacsState *qs = s->qe_state;
    EditBuffer *b;

    if (s->b->flags & BF_READONLY)
        return;

    /* if the GUI selection is used, it will be handled in the GUI code */
    selection_request(qs->screen);

    b = qs->yank_buffers[qs->yank_current];
    if (!b)
        return;
    size = b->total_size;
    if (size > 0) {
        eb_insert_buffer(s->b, s->offset, b, 0, size);
        s->offset += size;
    }
}

void do_yank_pop(EditState *s)
{
    QEmacsState *qs = s->qe_state;

    /* XXX: should verify if last command was a yank */
    do_undo(s);
    /* XXX: not strictly correct if the ring is not full */
    if (--qs->yank_current < 0)
        qs->yank_current = NB_YANK_BUFFERS - 1;
    do_yank(s);
}

void do_exchange_point_and_mark(EditState *s)
{
    int tmp;

    tmp = s->b->mark;
    s->b->mark = s->offset;
    s->offset = tmp;
}

static int reload_buffer(EditState *s, EditBuffer *b, FILE *f1)
{
    FILE *f;
    int ret, saved;

    /* if no file associated, cannot do anything */
    if (b->filename[0] == '\0')
        return 0;

    if (!f1) {
        f = fopen(b->filename, "r");
        if (!f)
            goto fail;
    } else {
        f = f1;
    }
    saved = b->save_log;
    b->save_log = 0;
    ret = b->data_type->buffer_load(b, f);
    b->modified = 0;
    b->save_log = saved;
    if (!f1)
        fclose(f);
    if (ret < 0) {
    fail:
        if (!f1) {
            put_status(s, "Could not load '%s'", b->filename);
        } else {
            put_status(s, "Error while reloading '%s'", b->filename);
        }
        return -1;
    } else {
        return 0;
    }
}


static void do_set_mode_file(EditState *s, ModeDef *m, 
                             ModeSavedData *saved_data, FILE *f1)
{
    int size, data_count;
    int saved_data_allocated = 0;
    EditState *e;
    EditBuffer *b;

    b = s->b;

    /* if a mode is already defined, try to close it */
    if (s->mode) {
        /* save mode data if necessary */
        if (!saved_data) {
            saved_data = s->mode->mode_save_data(s);
            if (saved_data)
                saved_data_allocated = 1;
        }
        s->mode->mode_close(s);
        free(s->mode_data);
        s->mode_data = NULL;
        s->mode = NULL;

        /* try to remove the raw or mode specific data if it is no
           longer used. */
        data_count = 0;
        for(e = qe_state.first_window; e != NULL; e = e->next_window) {
            if (e != s && e->b == b) {
                if (e->mode->data_type != &raw_data_type)
                    data_count++;
            }
        }
        /* we try to remove mode specific data if it is redundant with
           the buffer raw data */
        if (data_count == 0 && !b->modified) {
            /* close mode specific buffer representation because it is
               always redundant if it was not modified */
            if (b->data_type != &raw_data_type) {
                b->data_type->buffer_close(b);
                b->data = NULL;
                b->data_type = &raw_data_type;
            }
        }
    }
    /* if a new mode is wanted, open it */
    if (m) {
        size = m->instance_size;
        s->mode_data = NULL;
        if (m->data_type != &raw_data_type) {
            /* if a non raw data type is requested, we see if we can use it */
            if (b->data_type == &raw_data_type) {
                /* non raw data type: we must call a mode specific
                   load method */
                b->data_type = m->data_type;
                if (reload_buffer(s, b, f1) < 0) {
                    /* error: reset to text mode */
                    m = &text_mode;
                    b->data_type = &raw_data_type;
                }
            } else if (b->data_type != m->data_type) {
                /* non raw data type requested, but the the buffer has
                   a different type: we cannot switch mode, so we fall
                   back to text */
                m = &text_mode;
            } else {
                /* same data type: nothing more to do */
            }
        } else {
            /* if raw data and nothing loaded, we try to load */
            if (b->total_size == 0 && !b->modified)
                reload_buffer(s, b, f1);
        }
        if (size > 0) {
            s->mode_data = malloc(size);
            /* safe fall back: use text mode */
            if (!s->mode_data)
                m = &text_mode;
            else
                memset(s->mode_data, 0, size);
        }
        s->mode = m;
        
           /* init mode */
        m->mode_init(s, saved_data);
        /* modify offset_top so that its value is correct */
        if (s->mode->text_backward_offset)
            s->offset_top = s->mode->text_backward_offset(s, s->offset_top);
    }
    if (saved_data_allocated)
        free(saved_data);
}

void do_set_mode(EditState *s, ModeDef *m, ModeSavedData *saved_data)
{
    do_set_mode_file(s, m, saved_data, NULL);
}

static void do_cmd_set_mode(EditState *s, const char *name)
{
    ModeDef *m;

    m = find_mode(name);
    if (m)
        do_set_mode(s, m, NULL);
}

void charset_completion(StringArray *cs, const char *charset_str)
{
    QECharset *p;
    int len;

    len = strlen(charset_str);
    for(p = first_charset; p != NULL; p = p->next) {
        if (!strncmp(p->name, charset_str, len))
            add_string(cs, p->name);
    }
}

QECharset *read_charset(EditState *s, const char *charset_str)
{
    QECharset *charset;
    
    charset = find_charset(charset_str);
    if (!charset) {
        put_status(s, "Unknown charset '%s'", charset_str);
        return NULL;
    }
    return charset;
}

void do_set_buffer_file_coding_system(EditState *s, const char *charset_str)
{
    QECharset *charset;
    
    charset = read_charset(s, charset_str);
    if (!charset)
        return;
    eb_set_charset(s->b, charset);
}

/* convert the charset of a buffer to another charset */
void do_convert_buffer_file_coding_system(EditState *s, 
                                          const char *charset_str)
{
    QECharset *charset;
    EditBuffer *b1, *b;
    int offset, c, len;
    char buf[MAX_CHAR_BYTES];
    
    charset = read_charset(s, charset_str);
    if (!charset)
        return;

    b1 = eb_new("*tmp*", BF_SYSTEM);

    /* well, not very fast, but simple */
    b = s->b;
    for(offset = 0; offset < b->total_size;) {
        c = eb_nextc(b, offset, &offset);
        len = unicode_to_charset(buf, c, charset);
        eb_write(b1, b1->total_size, buf, len);
    }
    
    /* replace current buffer with convertion */
    eb_delete(b, 0, b->total_size);
    eb_insert_buffer(b, 0, b1, 0, b1->total_size);

    eb_free(b1);
    eb_set_charset(b, charset);
}

void do_toggle_bidir(EditState *s)
{
    s->bidir = !s->bidir;
}

void do_line_numbers(EditState *s)
{
    s->line_numbers = !s->line_numbers;
}

void do_line_truncate(EditState *s)
{
    if (s->wrap == WRAP_TRUNCATE)
        s->wrap = WRAP_LINE;
    else
        s->wrap = WRAP_TRUNCATE;
}

void do_word_wrap(EditState *s)
{
    if (s->wrap == WRAP_WORD)
        s->wrap = WRAP_LINE;
    else
        s->wrap = WRAP_WORD;
}

void do_goto_line(EditState *s, int line)
{
    if (line < 1)
        return;
    s->offset = eb_goto_pos(s->b, line - 1, 0);
}

void do_goto_char(EditState *s, int pos)
{
    if (pos < 0)
        return;
    s->offset = eb_goto_char(s->b, pos);
}

void do_set_tab_width(EditState *s, int tab_width)
{
    if (tab_width > 1)
        s->tab_size = tab_width;
}

void do_set_indent_width(EditState *s, int indent_width)
{
    if (indent_width > 1)
        s->indent_size = indent_width;
}

void do_set_indent_tabs_mode(EditState *s, int mode)
{
    s->indent_tabs_mode = (mode != 0);
}

void do_quit(EditState *s);
void do_load(EditState *s, const char *filename);
void do_switch_to_buffer(EditState *s, const char *bufname);
void do_break(EditState *s);
void do_insert_file(EditState *s, const char *filename);
void do_save(EditState *s, int save_as);
void do_isearch(EditState *s, int dir);
void do_refresh(EditState *s);

/* compute string for the first part of the mode line (flags,
   filename, modename) */
void basic_mode_line(EditState *s, char *buf, int buf_size, int c1)
{
    int mod, state;
    char *q;

    q = buf;
    mod = s->b->modified ? '*' : '-';
    if (s->b->flags & BF_LOADING)
        state = 'L';
    else if (s->b->flags & BF_SAVING)
            state = 'S';
    else if (s->busy)
        state = 'B';
    else
        state = '-';
    q += sprintf(q, "%c%c:%c%c  %-20s  (%s", 
                     c1,
                 state,
                 s->b->flags & BF_READONLY ? '%' : mod,
                 mod,
                 s->b->name,
                 s->mode->name);
    if (!s->insert)
        q += sprintf(q, " Ovwrt");
    if (s->interactive)
        q += sprintf(q, " Interactive");
    q += sprintf(q, ")--");
}

void text_mode_line(EditState *s, char *buf, int buf_size)
{
    int line_num, col_num, wrap_mode;
    int percent;
    char *q;

    wrap_mode = '-';
    if (!s->hex_mode) {
        if (s->wrap == WRAP_TRUNCATE)
            wrap_mode = 'T';
        else if (s->wrap == WRAP_WORD)
            wrap_mode = 'W';
    }
    basic_mode_line(s, buf, buf_size, wrap_mode);
    q = buf + strlen(buf);

    eb_get_pos(s->b, &line_num, &col_num, s->offset);
    q += sprintf(q, "L%d--C%d--%s", 
                 line_num + 1, col_num, s->b->charset->name);
    if (s->bidir) {
        q += sprintf(q, "--%s", s->cur_rtl ? "RTL" : "LTR");
    }
    if (s->input_method) {
        q += sprintf(q, "--%s", s->input_method->name);
    }
#if 0
    q += sprintf(q, "--[%d,%d]-[%d]", s->x_disp[0], s->x_disp[1], s->y_disp);
#endif
    percent = 0;
    if (s->b->total_size > 0)
        percent = (s->offset * 100) / s->b->total_size;
    q += sprintf(q, "--%d%%", percent);
    *q = '\0';
}


void display_mode_line(EditState *s)
{
    char buf[512];

    if (s->flags & WF_MODELINE) {
        s->mode->mode_line(s, buf, sizeof(buf));
        if (strcmp(buf, s->modeline_shadow) != 0) {
            print_at_byte(s->screen,
                          s->xleft, 
                          s->ytop + s->height, 
                          s->width,
                          s->qe_state->mode_line_height,
                          buf, QE_STYLE_MODE_LINE);
            strcpy(s->modeline_shadow, buf);
        }
    }
}

/* compute style */
static void apply_style(QEStyleDef *style, int style_index)
{
    QEStyleDef *s;
#ifndef WIN32
    if (style_index & QE_STYLE_TTY) {
        style->fg_color = tty_colors[TTY_GET_FG(style_index)];
        style->bg_color = tty_colors[TTY_GET_BG(style_index)];
    } else 
#endif
    {
        s = &qe_styles[style_index & ~QE_STYLE_SEL];
        if (s->fg_color != COLOR_TRANSPARENT)
            style->fg_color = s->fg_color;
        if (s->bg_color != COLOR_TRANSPARENT)
            style->bg_color = s->bg_color;
        if (s->font_style != 0)
            style->font_style = s->font_style;
        if (s->font_size != 0)
            style->font_size = s->font_size;
    }
    /* for selection, we need a special handling because only color is
           changed */
    if (style_index & QE_STYLE_SEL) {
        s = &qe_styles[QE_STYLE_SELECTION];
        style->fg_color = s->fg_color;
        style->bg_color = s->bg_color;
    }

}

void get_style(EditState *e, QEStyleDef *style, int style_index)
{
    /* get root default style */
    *style = qe_styles[0];

    /* apply window default style */
    if (e && e->default_style != 0)
        apply_style(style, e->default_style);

    /* apply specific style */
    if (style_index != 0)
        apply_style(style, style_index);
}

QEStyleDef *find_style(const char *name)
{
    int i;
    QEStyleDef *style;

    for(i=0;i<QE_STYLE_NB;i++) {
        style = &qe_styles[i];
        if (!strcmp(style->name, name))
            return style;
    }
    return NULL;
}

void style_completion(StringArray *cs, const char *input)
{
    QEStyleDef *style;
    int len, i;

    len = strlen(input);
    for(i=0;i<QE_STYLE_NB;i++) {
        style = &qe_styles[i];
        if (!strncmp(style->name, input, len))
            add_string(cs, style->name);
    }
}

/* Note: we use the same syntax as CSS styles to ease merging */
void do_set_style(EditState *e, const char *stylestr, 
                  const char *propstr, const char *value)
{
    QEStyleDef *style;
    int v, prop_index;

    style = find_style(stylestr);
    if (!style) {
        put_status(e, "Unknown style '%s'", stylestr);
        return;
    }

    prop_index = css_get_enum(propstr, 
                              "color,background-color,font-family,font-style,font-weight,font-size,text-decoration");
    if (prop_index < 0) {
        put_status(e, "Unknown property");
        return;
    }
    switch(prop_index) {
    case 0:
        css_get_color(&style->fg_color, value);
        break;
    case 1:
        css_get_color(&style->bg_color, value);
        break;
    case 2:
        v = css_get_font_family(value);
        style->font_style = (style->font_style & ~QE_FAMILY_MASK) | v;
        break;
    case 3:
        /* XXX: cannot handle inherit correctly */
        v = style->font_style;
        if (!strcmp(value, "italic")) {
            v |= QE_STYLE_ITALIC;
        } else if (!strcmp(value, "normal")) {
            v &= ~QE_STYLE_ITALIC;
        }
        style->font_style = v;
        break;
    case 4:
        /* XXX: cannot handle inherit correctly */
        v = style->font_style;
        if (!strcmp(value, "bold")) {
            v |= QE_STYLE_BOLD;
        } else if (!strcmp(value, "normal")) {
            v &= ~QE_STYLE_BOLD;
        }
        style->font_style = v;
        break;
    case 5:
        if (!strcmp(value, "inherit")) {
            style->font_size = 0;
        } else {
            style->font_size = strtol(value, NULL, 0);
        }
        break;
    case 6:
        /* XXX: cannot handle inherit correctly */
        if (!strcmp(value, "none")) {
            style->font_style &= ~QE_STYLE_UNDERLINE;
        } else if (!strcmp(value, "underline")) {
            style->font_style |= QE_STYLE_UNDERLINE;
        }
        break;
    }
}

void do_set_display_size(EditState *s, int w, int h)
{
    if (w != NO_ARG && h != NO_ARG) {
        screen_width = w;
        screen_height = h;
    }
}

/* NOTE: toggle-full-screen also hide the modeline of the current
   window and the status line */
void do_toggle_full_screen(EditState *s)
{
    QEditScreen *screen = s->screen;
    qe_state.is_full_screen = !qe_state.is_full_screen;
    if (screen->dpy.dpy_full_screen)
        screen->dpy.dpy_full_screen(screen, qe_state.is_full_screen);
    if (qe_state.is_full_screen)
        s->flags &= ~WF_MODELINE;
    else
        s->flags |= WF_MODELINE;
    qe_state.hide_status = qe_state.is_full_screen;
}

void do_toggle_mode_line(EditState *s)
{
    s->flags ^= WF_MODELINE;
    do_refresh(s);
}

void do_set_system_font(EditState *s, const char *qe_font_name, 
                        const char *system_fonts)
{
    int font_type;

    font_type = css_get_enum(qe_font_name, "fixed,serif,sans");
    if (font_type < 0) {
        put_status(s, "Invalid qemacs font");
        return;
    }
    pstrcpy(qe_state.system_fonts[font_type], sizeof(qe_state.system_fonts[0]),
            system_fonts);
}


void display_init(DisplayState *s, EditState *e, enum DisplayType do_disp)
{
    QEFont *font;
    QEStyleDef style;

    s->do_disp = do_disp;
    s->wrap = e->wrap;
    s->edit_state = e;
    /* select default values */
    get_style(e, &style, e->default_style);
    font = select_font(e->screen, style.font_style, style.font_size);
    s->eol_width = max(glyph_width(e->screen, font, '/'),
                       glyph_width(e->screen, font, '\\'));
    s->eol_width = max(s->eol_width, glyph_width(e->screen, font, '$'));
    s->default_line_height = font->ascent + font->descent;
    s->tab_width = glyph_width(e->screen, font, ' ') * e->tab_size;
    s->width = e->width - s->eol_width;
    s->height = e->height;
    s->hex_mode = e->hex_mode;
    s->cur_hex_mode = 0;
    s->y = e->y_disp;
    s->line_num = 0;
    s->eol_reached = 0;
    s->cursor_func = NULL;
    s->eod = 0;
}

void display_bol_bidir(DisplayState *s, DirType base, int embedding_level_max)
{
    s->base = base;
    s->x_disp = s->edit_state->x_disp[base];
    s->x = s->x_disp;
    s->fragment_index = 0;
    s->line_index = 0;
    s->nb_fragments = 0;
    s->word_index = 0;
    s->embedding_level_max = embedding_level_max;
    s->last_word_space = 0;
}

void display_bol(DisplayState *s)
{
    display_bol_bidir(s, DIR_LTR, 0);
}

void reverse_fragments(TextFragment *str, int len)
{
    int i, len2 = len / 2;
    
    for (i = 0; i < len2; i++) {
	TextFragment tmp = str[i];
	str[i] = str[len - 1 - i];
	str[len - 1 - i] = tmp;
    }
}

#define LINE_SHADOW_INCR 10

/* CRC to optimize redraw. */
/* XXX: is it safe enough ? */
static unsigned int compute_crc(unsigned char *data, int size, unsigned int sum)
{
    while (size >= 4) {
        sum += ((data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3]);
        data += 4;
        size -= 4;
    }
    while (size > 0) {
        sum += data[0] << (size * 8);
        data++;
        size--;
    }
    return sum;
}

static void flush_line(DisplayState *s, 
                       TextFragment *fragments, int nb_fragments,
                       int offset1, int offset2, int last)
{
    EditState *e = s->edit_state;
    QEditScreen *screen = e->screen;
    int level, pos, p, i, x_start, x, x1, y, baseline, line_height, max_descent;
    TextFragment *frag;
    QEFont *font;
    
    /* compute baseline and lineheight */
    baseline = 0;
    max_descent = 0;
    for(i=0;i<nb_fragments;i++) {
        if (fragments[i].ascent > baseline)
            baseline = fragments[i].ascent;
        if (fragments[i].descent > max_descent)
            max_descent = fragments[i].descent;
    }
    if (nb_fragments == 0) {
        /* if empty line, still needs a non zero line height */
        line_height = s->default_line_height;
    } else {
        line_height = baseline + max_descent;
    }
    
    /* swap according to embedding level */
    for (level = s->embedding_level_max; level > 0; level--) {
        pos = 0;
        while (pos < nb_fragments) {
            if (fragments[pos].embedding_level >= level) {
                /* find all chars >= level */
                for(p = pos + 1; p < nb_fragments && fragments[p].embedding_level >= level; p++);
                reverse_fragments(fragments + pos, p - pos);
                pos = p + 1;
            } else {
                pos++;
            }
        }
    }

    if (s->base == DIR_RTL) {
        x_start = e->width - s->x;
    } else {
        x_start = s->x_disp;
    }

    /* draw everything */
    if (s->do_disp == DISP_PRINT) {
        QEStyleDef style, default_style;
        QELineShadow *ls;
        unsigned int crc;

        /* test if display needed */
        crc = compute_crc((unsigned char *)fragments, 
                          sizeof(TextFragment) * nb_fragments, 0);
        crc = compute_crc((unsigned char *)s->line_chars, 
                          s->line_index * sizeof(int), crc);
        if (s->line_num >= e->shadow_nb_lines) {
            /* realloc shadow */
            int n = e->shadow_nb_lines;
            e->shadow_nb_lines = n + LINE_SHADOW_INCR;
            e->line_shadow = realloc(e->line_shadow, 
                                     e->shadow_nb_lines * sizeof(QELineShadow));
            /* put an impossible value so that we redraw */
            memset(&e->line_shadow[n], 0xff, 
                   LINE_SHADOW_INCR * sizeof(QELineShadow));
        }
        ls = &e->line_shadow[s->line_num];
        if (ls->y == s->y &&
            ls->x_start == x_start &&
            ls->height == line_height &&
            ls->crc == crc) {
            /* no display needed */
        } else {
#if 0
            printf("old=%d %d %d %d\n",
                   ls->y, ls->x_start, ls->height, ls->crc);
            printf("cur=%d %d %d %d\n",
                   s->y, x_start, line_height, crc);
#endif            
            /* init line shadow */
            ls->y = s->y;
            ls->x_start = x_start;
            ls->height = line_height;
            ls->crc = crc;

            /* display ! */

            get_style(e, &default_style, 0);
            x = e->xleft;
            y = e->ytop + s->y;

            /* first display background rectangles */
            if (x_start > 0) {
                fill_rectangle(screen, x, y, 
                               x_start, line_height, 
                               default_style.bg_color);
            }
            x += x_start;
            for(i=0;i<nb_fragments;i++) {
                frag = &fragments[i];
                get_style(e, &style, frag->style);
                fill_rectangle(screen, x, y, frag->width, line_height, 
                               style.bg_color);
                x += frag->width;
            }
            x1 = e->xleft + s->width + s->eol_width;
            if (x < x1) {
                fill_rectangle(screen, x, y, x1 - x, line_height, 
                               default_style.bg_color);
            }

            /* then display text */
            x = e->xleft;
            if (x_start > 0) {
                /* RTL eol mark */
                if (!last && s->base == DIR_RTL) {
                    /* XXX: optimize that ! */
                    int markbuf[1];
                
                    font = select_font(screen, 
                                       default_style.font_style, 
                                       default_style.font_size);
                    markbuf[0] = '/';
                    draw_text(screen, font, x, y + font->ascent, 
                              markbuf, 1, default_style.fg_color);
                }
            }
            x += x_start;
            for(i=0;i<nb_fragments;i++) {
                frag = &fragments[i];
                get_style(e, &style, frag->style);
                font = select_font(screen, 
                                   style.font_style, style.font_size);
                draw_text(screen, font, x, y + baseline, 
                          s->line_chars + frag->line_index,
                          frag->len, style.fg_color);
                x += frag->width;
            }
            x1 = e->xleft + s->width + s->eol_width;
            if (x < x1) {
                /* LTR eol mark */
                if (!last && s->base == DIR_LTR) {
                    /* XXX: optimize that ! */
                    int markbuf[1];
                
                    font = select_font(screen, 
                             default_style.font_style, default_style.font_size);
                    markbuf[0] = '\\';
                    draw_text(screen, font, 
                              e->xleft + s->width, y + font->ascent,
                              markbuf, 1, default_style.fg_color);
                }
            }
        }
    }
    
    /* call cursor callback */
    if (s->cursor_func) {

        x = x_start;
        /* mark eol */
        if (offset1 >= 0 && offset2 >= 0 && 
            s->base == DIR_RTL && 
            s->cursor_func(s, offset1, offset2, s->line_num,
                           x, s->y, -s->eol_width, line_height, e->hex_mode)) {
            s->eod = 1;
        }

        for(i=0;i<nb_fragments;i++) {
            int w, k, j, offset1, offset2;

            frag = &fragments[i];

            j = frag->line_index;
            for(k=0;k<frag->len;k++) {
                int hex_mode;
                offset1 = s->line_offsets[j][0];
                offset2 = s->line_offsets[j][1];
                hex_mode = s->line_hex_mode[j];
                w = s->line_char_widths[j];
                if (hex_mode == s->hex_mode || s->hex_mode == -1) {
                    if (s->base == DIR_RTL) {
                        if (offset1 >= 0 && offset2 >= 0 &&
                            s->cursor_func(s, offset1, offset2, s->line_num,
                                           x + w, s->y, -w, line_height, 
                                           hex_mode))
                            s->eod = 1;
                    } else {
                        if (offset1 >= 0 && offset2 >= 0 &&
                            s->cursor_func(s, offset1, offset2, s->line_num,
                                           x, s->y, w, line_height,
                                           hex_mode))
                            s->eod = 1;
                    }
                }
                x += w;
                j++;
            }
        }
        /* mark eol */
        if (offset1 >= 0 && offset2 >= 0 && 
            s->base == DIR_LTR && 
            s->cursor_func(s, offset1, offset2, s->line_num,
                           x, s->y, s->eol_width, line_height, e->hex_mode)) {
            s->eod = 1;
        }
    }
#if 0
    printf("y=%d line_num=%d line_height=%d baseline=%d\n", 
           s->y, s->line_num, line_height, baseline);
#endif
    s->y += line_height;
    s->line_num++;
}

/* keep 'n' line chars at the start of the line */
static void keep_line_chars(DisplayState *s, int n)
{
    int index;

    index = s->line_index - n;
    memmove(s->line_chars, s->line_chars + index, n * sizeof(unsigned int));
    memmove(s->line_offsets, s->line_offsets + index, n * 2 * sizeof(unsigned int));
    memmove(s->line_char_widths, s->line_char_widths + index, n * sizeof(short));
    s->line_index = n;
}
            
/* layout of a word fragment */
static void flush_fragment(DisplayState *s)
{
    int w, len, style_index, i, j;
    QEditScreen *screen = s->edit_state->screen;
    TextFragment *frag;
    QEStyleDef style;
    QEFont *font;
    unsigned int char_to_glyph_pos[MAX_WORD_SIZE];
    int nb_glyphs, dst_max_size, ascent, descent;

    if (s->fragment_index == 0)
        return;
    if (s->nb_fragments >= MAX_SCREEN_WIDTH) 
        goto the_end;

    /* update word start index if needed */
    if (s->nb_fragments >= 1 && s->last_word_space != s->last_space) {
        s->last_word_space = s->last_space;
        s->word_index = s->nb_fragments;
    }

    /* convert fragment to glyphs (currently font independent, but may
       change) */
    dst_max_size = MAX_SCREEN_WIDTH - s->line_index;
    if (dst_max_size <= 0)
        goto the_end;
    nb_glyphs = unicode_to_glyphs(s->line_chars + s->line_index, 
                                  char_to_glyph_pos, dst_max_size,
                                  s->fragment_chars, s->fragment_index, 
                                  s->last_embedding_level & 1);

    /* compute new offsets */
    j = s->line_index;
    for(i=0;i<nb_glyphs;i++) {
        s->line_offsets[j][0] = -1;
        s->line_offsets[j][1] = -1;
        j++;
    }
    for(i=0;i<s->fragment_index;i++) {
        int offset1, offset2;
        j = s->line_index + char_to_glyph_pos[i];
        offset1 = s->fragment_offsets[i][0];
        offset2 = s->fragment_offsets[i][1];
        s->line_hex_mode[j] = s->fragment_hex_mode[i];
        /* we suppose the the chars are contiguous */
        if (s->line_offsets[j][0] == -1 ||
            s->line_offsets[j][0] > offset1)
            s->line_offsets[j][0] = offset1;
        if (s->line_offsets[j][1] == -1 ||
            s->line_offsets[j][1] < offset2)
            s->line_offsets[j][1] = offset2;
    }

    style_index = s->last_style;
    if (style_index == QE_STYLE_DEFAULT)
        style_index = s->edit_state->default_style;
    get_style(s->edit_state, &style, style_index);
    /* select font according to current style */
    font = select_font(screen, 
                       style.font_style, style.font_size);
    j = s->line_index;
    ascent = font->ascent;
    descent = font->descent;
    if (s->line_chars[j] == '\t') {
        int x1;
        /* special case for TAB */
        x1 = (s->x - s->x_disp) % s->tab_width;
        w = s->tab_width - x1;
        /* display a single space */
        s->line_chars[j] = ' ';
        s->line_char_widths[j] = w;
    } else {
        /* XXX: use text metrics for full fragment */
        w = 0;
        for(i=0;i<nb_glyphs;i++) {
            QECharMetrics metrics;
            text_metrics(screen, font, &metrics, &s->line_chars[j], 1);
            if (metrics.font_ascent > ascent)
                ascent = metrics.font_ascent;
            if (metrics.font_descent > descent)
                descent = metrics.font_descent;
            s->line_char_widths[j] = metrics.width;
            w += s->line_char_widths[j];
            j++;
        }
    }
    
    /* add the fragment */
    frag = &s->fragments[s->nb_fragments++];
    frag->width = w;
    frag->line_index = s->line_index;
    frag->len = nb_glyphs;
    frag->embedding_level = s->last_embedding_level;
    frag->style = style_index;
    frag->ascent = ascent;
    frag->descent = descent;
    frag->dummy = 0;

    s->line_index += nb_glyphs;
    s->x += frag->width;

    switch(s->wrap) {
    case WRAP_TRUNCATE:
        break;
    case WRAP_LINE:
        while (s->x > s->width) {
            int len1, w1, ww, n;
            //            printf("x=%d maxw=%d len=%d\n", s->x, s->width, frag->len);
            frag = &s->fragments[s->nb_fragments - 1];
            /* find fragment truncation to fit the line */
            len = len1 = frag->len;
            w1 = s->x;
            while (s->x > s->width) {
                len--;
                ww = s->line_char_widths[frag->line_index + len];
                s->x -= ww;
                if (len == 0 && s->x == 0) {
                    /* avoid looping by putting at least one char per line */
                    len = 1;
                    s->x += ww;
                    break;
                }
            }
            len1 -= len;
            w1 -= s->x;
            frag->len = len;
            frag->width -= w1;
            //            printf("after: x=%d w1=%d\n", s->x, w1);
            n = s->nb_fragments;
            if (len == 0)
                n--;
            flush_line(s, s->fragments, n, -1, -1, 0);

            /* move the remaining fragment to next line */
            s->nb_fragments = 0;
            s->x = 0;
            if (len1 > 0) {
                memmove(s->fragments, frag, sizeof(TextFragment));
                frag = s->fragments;
                frag->width = w1;
                frag->line_index = 0;
                frag->len = len1;
                s->nb_fragments = 1;
                s->x = w1;
            }
            keep_line_chars(s, len1);
        }
        break;
    case WRAP_WORD:
        if (s->x > s->width) {
            int index;

            flush_line(s, s->fragments, s->word_index, -1, -1, 0);

            /* put words on next line */
            index = s->fragments[s->word_index].line_index;
            memmove(s->fragments, s->fragments + s->word_index,
                    (s->nb_fragments - s->word_index) * sizeof(TextFragment));
            s->nb_fragments -= s->word_index;
            s->x = 0;
            for(i=0;i<s->nb_fragments;i++) {
                s->fragments[i].line_index -= index;
                s->x += s->fragments[i].width;
            }
            keep_line_chars(s, s->line_index - index);
            s->word_index = 0;
        }
        break;
    }
 the_end:
    s->fragment_index = 0;
}

int display_char_bidir(DisplayState *s, int offset1, int offset2,
                       int embedding_level, int ch)
{
    int space, style, istab;
    EditState *e;

    style = (ch >> STYLE_SHIFT);

    /* special code to colorize block */
    e = s->edit_state;
    if (e->show_selection) {
        int mark = e->b->mark;
        int offset = e->offset;
        
        if ((offset1 >= offset && offset1 < mark) ||
            (offset1 >= mark && offset1 < offset))
            style |= QE_STYLE_SEL;
    }
    /* special patch for selection in hex mode */
    if (offset1 == offset2) {
        offset1 = -1;
        offset2 = -1;
    }
    
    ch = ch & ~STYLE_MASK;
    space = (ch == ' ');
    istab = (ch == '\t');
    /* a fragment is a part of word where style/embedding_level do not
       change. For TAB, only one fragment containing it is sent */
    if ((s->fragment_index >= MAX_WORD_SIZE) ||
        istab ||
        (s->fragment_index >= 1 && 
         (space != s->last_space ||
          style != s->last_style ||
          embedding_level != s->last_embedding_level))) { 
        /* flush the current fragment if needed */
        flush_fragment(s);
    }
    
    /* store the char and its embedding level */
    s->fragment_chars[s->fragment_index] = ch;
    s->fragment_offsets[s->fragment_index][0] = offset1;
    s->fragment_offsets[s->fragment_index][1] = offset2;
    s->fragment_hex_mode[s->fragment_index] = s->cur_hex_mode;
    s->fragment_index++;
    
    s->last_space = space;
    s->last_style = style;
    s->last_embedding_level = embedding_level;

    if (istab) {
        flush_fragment(s);
    }
    return 0;
}

void display_printhex(DisplayState *s, int offset1, int offset2, 
                      unsigned int h, int n)
{
    unsigned int i, v;
    EditState *e = s->edit_state;
    
    s->cur_hex_mode = 1;
    for(i=0;i<n;i++) {
        v = (h >> ((n - i - 1) * 4)) & 0xf;
        if (v >= 10)
            v += 'a' - 10;
        else
            v += '0';
        /* XXX: simplistic */
        if (e->hex_nibble == i) {
            display_char(s, offset1, offset2, v);
        } else {
            display_char(s, offset1, offset1, v);
        }
    }
    s->cur_hex_mode = 0;
}

void display_printf(DisplayState *ds, int offset1, int offset2,
                    const char *fmt, ...)
{
    char buf[256], *p;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    p = buf;
    if (*p) {
        display_char(ds, offset1, offset2, *p++);
        while (*p) {
            display_char(ds, -1, -1, *p++);
        }
    }
}

/* end of line */
void display_eol(DisplayState *s, int offset1, int offset2)
{
    flush_fragment(s);

    /* note: the line may be empty */
    flush_line(s, s->fragments, s->nb_fragments, offset1, offset2, 1);
}

/* temporary function for backward compatibility */
static void display1(DisplayState *s)
{
    EditState *e = s->edit_state;
    int offset;

    s->eod = 0;
    offset = e->offset_top;
    for(;;) {
        offset = e->mode->text_display(e, s, offset);
        /* EOF reached ? */
        if (offset < 0)
            break;
        
        switch(s->do_disp) {
        case DISP_CURSOR:
            if (s->eod)
                return;
            break;
        default:
        case DISP_PRINT:
            if (s->y >= s->height)
                return; /* end of screen */
            break;
        case DISP_CURSOR_SCREEN:
            if (s->eod || s->y >= s->height)
                return;
            break;
        }
    }
}

/******************************************************/
int text_backward_offset(EditState *s, int offset)
{
    int line, col;

    eb_get_pos(s->b, &line, &col, offset);
    return eb_goto_pos(s->b, line, 0);
}

#ifdef CONFIG_UNICODE_JOIN
/* max_size should be >= 2 */
static int bidir_compute_attributes(TypeLink *list_tab, int max_size, 
                                    EditBuffer *b, int offset)
{
    TypeLink *p;
    FriBidiCharType type, ltype;
    int left, offset1;
    unsigned int c;

    p = list_tab;
    /* Add the starting link */
    p->type = FRIBIDI_TYPE_SOT;
    p->len = 0;
    p->pos = 0;
    p++;
    left = max_size - 2;

    ltype = FRIBIDI_TYPE_SOT;

    for(;;) {
        offset1 = offset;
        c = eb_nextc(b, offset, &offset);
        if (c == '\n')
            break;
        type = fribidi_get_type(c);
        /* if not enough room, increment last link */
        if (type != ltype && left > 0) {
            p->type = type;
            p->pos = offset1;
            p->len = 1;
            p++;
            left--;
            ltype = type;
        } else {
            p[-1].len++;
        }
    }
    
    /* Add the ending link */
    p->type = FRIBIDI_TYPE_EOT;
    p->len = 0;
    p->pos = offset1;
    p++;

    return p - list_tab;
}
#endif

#ifndef CONFIG_TINY
/************************************************************/
/* colorization handling */
/* NOTE: only one colorization mode can be selected at a time for a
   buffer */

/* Gets the colorized line beginning at 'offset'. Its length
   excluding '\n' is returned */

#define COLORIZED_LINE_PREALLOC_SIZE 64

int get_colorized_line(EditState *s, unsigned int *buf, int buf_size,
                       int offset1, int line_num)
{
    int len, l, line, col, offset;
    int colorize_state;
    unsigned char *ptr;
    
    /* invalidate cache if needed */
    if (s->colorize_max_valid_offset != MAXINT) {
        eb_get_pos(s->b, &line, &col, s->colorize_max_valid_offset);
        line++;
        if (line < s->colorize_nb_valid_lines)
            s->colorize_nb_valid_lines = line;
        s->colorize_max_valid_offset = MAXINT;
    }

    /* realloc line buffer if needed */
    if ((line_num + 2) > s->colorize_nb_lines) {
        s->colorize_nb_lines = line_num + 2 + COLORIZED_LINE_PREALLOC_SIZE;
        ptr = realloc(s->colorize_states, s->colorize_nb_lines);
        if (!ptr)
            return 0;
        s->colorize_states = ptr;
    }

    /* propagate state if needed */
    if (line_num >= s->colorize_nb_valid_lines) {
        if (s->colorize_nb_valid_lines == 0) {
            s->colorize_states[0] = 0; /* initial state : zero */
            s->colorize_nb_valid_lines = 1;
        }
        offset = eb_goto_pos(s->b, s->colorize_nb_valid_lines - 1, 0);
        colorize_state = s->colorize_states[s->colorize_nb_valid_lines - 1];

        for(l = s->colorize_nb_valid_lines; l <= line_num; l++) {
            len = eb_get_line(s->b, buf, buf_size - 1, &offset);
            buf[len] = '\n';

            s->colorize_func(buf, len, &colorize_state, 1);
            
            s->colorize_states[l] = colorize_state;
        }
    }

    /* compute line color */
    len = eb_get_line(s->b, buf, buf_size - 1, &offset1);
    buf[len] = '\n';

    colorize_state = s->colorize_states[line_num];
    s->colorize_func(buf, len, &colorize_state, 0);
    
    s->colorize_states[line_num + 1] = colorize_state;

    s->colorize_nb_valid_lines = line_num + 2;
    return len;
}

/* invalidate the colorize data */
static void colorize_callback(EditBuffer *b,
                              void *opaque,
                              enum LogOperation op,
                              int offset,
                              int size)
{
    EditState *e = opaque;

    if (offset < e->colorize_max_valid_offset)
        e->colorize_max_valid_offset = offset;
}

void set_colorize_func(EditState *s, ColorizeFunc colorize_func)
{
    /* invalidate the previous states & free previous colorizer */
    eb_free_callback(s->b, colorize_callback, s);
    free(s->colorize_states);
    s->colorize_states = NULL;
    s->colorize_nb_lines = 0;
    s->colorize_nb_valid_lines = 0;
    s->colorize_max_valid_offset = MAXINT;
    s->get_colorized_line_func = NULL;
    s->colorize_func = NULL;
    
    if (colorize_func) {
        eb_add_callback(s->b, colorize_callback, s);
        s->get_colorized_line_func = get_colorized_line;
        s->colorize_func = colorize_func;
    }
}
                          
#else
void set_colorize_func(EditState *s, ColorizeFunc colorize_func)
{
}
#endif

#define RLE_EMBEDDINGS_SIZE    128
#define COLORED_MAX_LINE_SIZE  1024

int text_display(EditState *s, DisplayState *ds, int offset)
{
    int c;
    int offset0, offset1, line_num, col_num;
    TypeLink embeds[RLE_EMBEDDINGS_SIZE], *bd;
    int embedding_level, embedding_max_level;
    FriBidiCharType base;
    unsigned int colored_chars[COLORED_MAX_LINE_SIZE];
    int char_index, colored_nb_chars;

    line_num = 0; /* avoid warning */
    if (s->line_numbers || s->colorize_func) {
        eb_get_pos(s->b, &line_num, &col_num, offset);
    }

    offset1 = offset;
    
#ifdef CONFIG_UNICODE_JOIN
    if (s->bidir) {
        /* compute the embedding levels and rle encode them */
        if (bidir_compute_attributes(embeds, RLE_EMBEDDINGS_SIZE,
                                     s->b, offset) > 2) {
            base = FRIBIDI_TYPE_WL;
            fribidi_analyse_string(embeds, &base, &embedding_max_level);
            /* assure that base has only two possible values */
            if (base != FRIBIDI_TYPE_RTL)
                base = FRIBIDI_TYPE_LTR;
        } else {
            goto no_bidir;
        }
    } else 
#endif
    {
#ifdef CONFIG_UNICODE_JOIN
    no_bidir:
#endif
        /* all line is at embedding level 0 */
        embedding_max_level = 0;
        embeds[1].level = 0;
        embeds[2].pos = 0x7fffffff;
        base = FRIBIDI_TYPE_LTR;
    }
    
    display_bol_bidir(ds, base, embedding_max_level);

    /* line numbers */
    if (s->line_numbers) {
        display_printf(ds, -1, -1, "%6d  ", line_num + 1);
    }
    
    /* prompt display */
    if (s->prompt && offset1 == 0) {
        const char *p;
        p = s->prompt;
        while (*p) {
            display_char(ds, -1, -1, *p++);
        }
    }

    /* colorize */
    if (s->get_colorized_line_func) {
        colored_nb_chars = s->get_colorized_line_func(s, colored_chars, 
                                                      COLORED_MAX_LINE_SIZE, 
                                                      offset, line_num);
    } else {
        colored_nb_chars = 0;
    }
    
    bd = embeds + 1;
    char_index = 0;
    for(;;) {
        offset0 = offset;
        if (offset >= s->b->total_size) {
            display_eol(ds, offset0, offset0 + 1);
            offset = -1; /* signal end of text */
            break;
        } else {
            c = eb_nextc(s->b, offset, &offset);
            if (c == '\n') {
                display_eol(ds, offset0, offset);
                break;
            }
            
            /* compute embedding from RLE embedding list */
            if (offset0 >= bd[1].pos)
                bd++;
            embedding_level = bd[0].level;
            /* XXX: use embedding level for all cases ? */
            if (c < ' ' && c != '\t') {
                display_printf(ds, offset0, offset, "^%c", '@' + c);
            } else if (c >= 0x10000) {
                /* currently, we cannot display these chars */
                display_printf(ds, offset0, offset, "\\U%08x", c);
            } else if ((c >= 128 && c < 128 + 32) ||
                       (s->screen->charset != &charset_utf8 && c >= 256)) {
                display_printf(ds, offset0, offset, "\\u%04x", c);
            } else {
                if (char_index < colored_nb_chars)
                    c = colored_chars[char_index];
                display_char_bidir(ds, offset0, offset, embedding_level, c);
            }
            char_index++;
        }
    }
    return offset;
}

/* Generic display algorithm with automatic fit */
void generic_text_display(EditState *s)
{
    CursorContext m1, *m = &m1;
    DisplayState ds1, *ds = &ds1;
    int x1, xc, yc, offset;

    /* if the cursor is before the top of the display zone, we must
       resync backward */
    if (s->offset < s->offset_top) {
        s->offset_top = s->mode->text_backward_offset(s, s->offset);
    }

    if (s->display_invalid) {
        /* invalidate the line shadow buffer */
        free(s->line_shadow);
        s->line_shadow = NULL;
        s->shadow_nb_lines = 0;
        s->display_invalid = 0;
    }

    /* find cursor position with the current x_disp & y_disp and
       update y_disp so that we display only the needed lines */
    display_init(ds, s, DISP_CURSOR_SCREEN);
    ds->cursor_opaque = m;
    ds->cursor_func = cursor_func;
    m->offsetc = s->offset;
    m->xc = m->yc = NO_CURSOR;
    offset = s->offset_top;
    for(;;) {
        if (ds->y <= 0) {
            s->offset_top = offset;
            s->y_disp = ds->y;
        }
        offset = s->mode->text_display(s, ds, offset);
        if (offset < 0 || ds->y >= s->height || m->xc != NO_CURSOR)
            break;
    }
    //    printf("cursor: xc=%d yc=%d linec=%d\n", m->xc, m->yc, m->linec);
    if (m->xc == NO_CURSOR) {
        /* if no cursor found then we compute offset_top so that we
           have a chance to find the cursor in a small amount of time */
        display_init(ds, s, DISP_CURSOR_SCREEN);
        ds->cursor_opaque = m;
        ds->cursor_func = cursor_func;
        ds->y = 0;
        offset = s->mode->text_backward_offset(s, s->offset);
        s->mode->text_display(s, ds, offset);
        if (m->xc == NO_CURSOR) {
            /* XXX: should not happen */
            printf("ERROR: cursor not found\n");
            ds->y = 0;
        } else {
            ds->y = m->yc + m->cursor_height;
        }

        while (ds->y < s->height && offset > 0) {
            offset = s->mode->text_backward_offset(s, offset - 1);
            s->mode->text_display(s, ds, offset);
        }
        s->offset_top = offset;
        /* adjust y_disp so that the cursor is at the bottom of the
           screen */
        s->y_disp = s->height - ds->y;
    } else {
        yc = m->yc;
        if (yc < 0) {
            s->y_disp -= yc;
        } else if ((yc + m->cursor_height) >= s->height) {
            s->y_disp += s->height - (yc + m->cursor_height);
        }
    }

    /* update x cursor position if needed. Note that we distinguish
       between rtl and ltr margins. We try to have x_disp == 0 as much
       as possible */
    if (s->wrap == WRAP_TRUNCATE) {
        xc = m->xc;
        x1 = xc - s->x_disp[m->basec]; 
        if (x1 >= 0 && x1 < ds->width - ds->eol_width) {
            s->x_disp[m->basec] = 0;
        } else if (xc < 0) {
            s->x_disp[m->basec] -= xc;
        } else if (xc >= ds->width) {
            s->x_disp[m->basec] += ds->width - xc - ds->eol_width;
        }
    } else {
        s->x_disp[0] = 0;
        s->x_disp[1] = 0;
    }

    /* now we can display the text and get the real cursor position !  */

    display_init(ds, s, DISP_PRINT);
    ds->cursor_opaque = m;
    ds->cursor_func = cursor_func;
    m->offsetc = s->offset;
    m->xc = m->yc = NO_CURSOR;
    display1(ds);
    /* display the remaining region */
    if (ds->y < s->height) {
        QEStyleDef default_style;
        get_style(s, &default_style, 0);
        fill_rectangle(s->screen, s->xleft, s->ytop + ds->y, 
                       s->width, s->height - ds->y, 
                       default_style.bg_color);
        /* do not forget to erase the line shadow  */
        memset(&s->line_shadow[ds->line_num], 0xff, 
               (s->shadow_nb_lines - ds->line_num) * sizeof(QELineShadow));
    }
    xc = m->xc;
    yc = m->yc;

    if (s->qe_state->active_window == s) {
        int x, y, w, h;
        x = s->xleft + xc;
        y = s->ytop + yc;
        w = m->cursor_width;
        h = m->cursor_height;
        if (s->screen->dpy.dpy_cursor_at) {
            /* hardware cursor */
            s->screen->dpy.dpy_cursor_at(s->screen, x, y, w, h);
        } else {
            /* software cursor */
            if (w < 0) {
                x += w;
                w = -w;
            }
            fill_rectangle(s->screen, x, y, w, h, QECOLOR_XOR);
            /* invalidate line so that the cursor will be erased next time */
            memset(&s->line_shadow[m->linec], 0xff, 
                   sizeof(QELineShadow));
        }
    }
    s->cur_rtl = (m->dirc == DIR_RTL);
#if 0
    printf("cursor1: xc=%d yc=%d w=%d h=%d linec=%d\n", 
           m->xc, m->yc, m->cursor_width, m->cursor_height, m->linec);
#endif
}

enum CmdArgType {
    CMD_ARG_INT = 0,
    CMD_ARG_INTVAL,
    CMD_ARG_STRING,
    CMD_ARG_STRINGVAL,
    CMD_ARG_WINDOW,
};

#define MAX_CMD_ARGS 5

/* XXX: potentially non portable on weird architectures */
void call_func(void *func, int nb_args, void **args, 
               unsigned char *args_type)
{
    switch(nb_args) {
    case 0:
        ((void (*)())func)();
        break;
    case 1:
        ((void (*)())func)(args[0]);
        break;
    case 2:
        ((void (*)())func)(args[0], args[1]);
        break;
    case 3:
        ((void (*)())func)(args[0], args[1], args[2]);
        break;
    case 4:
        ((void (*)())func)(args[0], args[1], args[2], args[3]);
        break;
    case 5:
        ((void (*)())func)(args[0], args[1], args[2], args[3], args[4]);
        break;
    default:
        return;
    }
}

static void get_param(const char **pp, char *param, int param_size, int osep, int sep)
{
    const char *p;
    char *q;

    param_size--;
    p = *pp;
    if (*p == osep) {
        p++;
        if (param) {
            q = param;
            while (*p != sep && *p != '\0') {
                if ((q - param) < param_size)
                    *q++ = *p;
                p++;
            }
            *q = '\0';
        } else {
            while (*p != sep && *p != '\0')
                p++;
        }
        if (*p == sep)
            p++;
    } else {
        if (param)
            param[0] = '\0';
    }
    *pp = p;
}

/* return -1 if error, 0 if no more args, 1 if one arg parsed */
static int parse_arg(const char **pp, unsigned char *argtype, 
                     char *prompt, int prompt_size,
                     char *completion, int completion_size,
                     char *history, int history_size)
{
    int type;
    const char *p;

    p = *pp;
    type = *p;
    if (type == '\0')
        return 0;
    p++;
    get_param(&p, prompt, prompt_size, '{', '}');
    get_param(&p, completion, completion_size, '[', ']');
    get_param(&p, history, history_size, '|', '|');
    switch(type) {
    case 'v':
        *argtype = CMD_ARG_INTVAL;
        break;
    case 'i':
        *argtype = CMD_ARG_INT;
        break;
    case 's':
        *argtype = CMD_ARG_STRING;
        break;
    case 'S':
        *argtype = CMD_ARG_STRINGVAL;
        break;
    default:
        return -1;
    }
    *pp = p;
    return 1;
}

typedef struct ExecCmdState {
    EditState *s;
    CmdDef *d;
    int nb_args;
    int argval;
    const char *ptype;
    void *args[MAX_CMD_ARGS];
    unsigned char args_type[MAX_CMD_ARGS];
    char default_input[512]; /* default input if none given */
} ExecCmdState;

static void arg_edit_cb(void *opaque, char *str);
static void parse_args(ExecCmdState *es);
static void free_cmd(ExecCmdState *es);

void exec_command(EditState *s, CmdDef *d, int argval)
{
    ExecCmdState *es;
    
    es = malloc(sizeof(ExecCmdState));
    if (!es)
        return;

    es->s = s;
    es->d = d;
    es->argval= argval;
    es->nb_args = 0;

    /* first argument is always the window */
    es->args[es->nb_args] = (void *)s;
    es->args_type[es->nb_args] = CMD_ARG_WINDOW;
    es->nb_args++;
    es->ptype = d->name + strlen(d->name) + 1;

    parse_args(es);
}

/* parse as much arguments as possible. ask value to user if possible */
static void parse_args(ExecCmdState *es)
{
    EditState *s = es->s;
    CmdDef *d = es->d;
    char prompt[256];
    char completion_name[64];
    char history[32];
    unsigned char arg_type;
    int ret, rep_count, no_arg;

    for(;;) {
        ret = parse_arg(&es->ptype, &arg_type, 
                        prompt, sizeof(prompt),
                        completion_name, sizeof(completion_name),
                        history, sizeof(history));
        if (ret < 0)
            goto fail;
        if (ret == 0)
            break;
        if (es->nb_args >= MAX_CMD_ARGS)
            goto fail;
        es->args_type[es->nb_args] = arg_type;
        no_arg = 0;
        switch(arg_type) {
        case CMD_ARG_INTVAL:
        case CMD_ARG_STRINGVAL:
            es->args[es->nb_args] = (void *)d->val;
            break;
        case CMD_ARG_INT:
            if (es->argval != NO_ARG) {
                es->args[es->nb_args] = (void *)es->argval;
                es->argval = NO_ARG;
            } else {
                es->args[es->nb_args] = (void *)NO_ARG;
                no_arg = 1;
            }
            break;
        case CMD_ARG_STRING:
            es->args[es->nb_args] = (void *)NULL;
            no_arg = 1;
            break;
        }
        es->nb_args++;
        /* if no argument specified, try to ask it to the user */
        if (no_arg && prompt[0] != '\0') {
            char def_input[1024];

            /* XXX: currently, default input is handled non generically */
            def_input[0] = '\0';
            es->default_input[0] = '\0';
            if (!strcmp(completion_name, "file")) {
                get_default_path(s, def_input, sizeof(def_input));
            } else if (!strcmp(completion_name, "buffer")) {
                EditBuffer *b;
                if (d->action.func == (void *)do_switch_to_buffer)
                    b = predict_switch_to_buffer(s);
                else
                    b = s->b;
                pstrcpy(es->default_input, sizeof(es->default_input), b->name);
            }
            if (es->default_input[0] != '\0') {
                pstrcat(prompt, sizeof(prompt), "(default ");
                pstrcat(prompt, sizeof(prompt), es->default_input);
                pstrcat(prompt, sizeof(prompt), ") ");
            }
            minibuffer_edit(def_input, prompt, 
                            get_history(history), 
                            find_completion(completion_name),
                            arg_edit_cb, es);
            return;
        }
    }

    /* all arguments are parsed : we can now execute the command */
    /* argval is handled as repetition count if not taken as argument */
    if (es->argval != NO_ARG && es->argval > 1) {
        rep_count = es->argval;
    } else {
        rep_count = 1;
    }
    
    do {
        /* special case for hex mode */
        if (d->action.func != (void *)do_char) {
            s->hex_nibble = 0;
            /* special case for character composing */
            if (d->action.func != (void *)do_backspace)
                s->compose_len = 0;
        }
#ifndef CONFIG_TINY
        save_selection();
#endif
        call_func(d->action.func, es->nb_args, es->args, es->args_type);
    } while (--rep_count > 0);

    qe_state.last_cmd_func = d->action.func;
 fail:
    free_cmd(es);
}

static void free_cmd(ExecCmdState *es)
{
    int i;
    
    /* free allocated parameters */
    for(i = 0;i < es->nb_args; i++) {
        switch(es->args_type[i]) {
        case CMD_ARG_STRING:
            free(es->args[i]);
            break;
        }
    }
    free(es);
}

/* when the argument has been typed by the user, this callback is
   called */
static void arg_edit_cb(void *opaque, char *str)
{
    ExecCmdState *es = opaque;
    int index, val;
    char *p;

    if (!str) {
        /* command aborted */
    fail:
        free(str);
        free_cmd(es);
        return;
    }
    index = es->nb_args - 1;
    switch(es->args_type[index]) {
    case CMD_ARG_INT:
        val = strtol(str, &p, 0);
        if (*p != '\0') {
            put_status(NULL, "Invalid Number");
            goto fail;
        }
        es->args[index] = (void *)val;
        break;
    case CMD_ARG_STRING:
        if (str[0] == '\0' && es->default_input[0] != '\0') {
            free(str);
            str = strdup(es->default_input);
        }
        es->args[index] = (void *)str; /* will be freed at the of the command */
        break;
    }
    /* now we can parse the following arguments */
    parse_args(es);
}

void do_execute_command(EditState *s, const char *cmd, int argval)
{
    CmdDef *d;

    d = qe_find_cmd(cmd);
    if (!d) {
        put_status(s, "No match");
    } else {
        exec_command(s, d, argval);
    }
}

void window_display(EditState *s)
{
    CSSRect rect;

    /* set the clipping rectangle to the whole window */
    rect.x1 = s->xleft;
    rect.y1 = s->ytop;
    rect.x2 = rect.x1 + s->width;
    rect.y2 = rect.y1 + s->height;
    set_clip_rectangle(s->screen, &rect);

    s->mode->display(s);

    display_mode_line(s);
}

/* display all windows */
/* XXX: should use correct clipping to avoid popups display hacks */
void edit_display(QEmacsState *qs)
{
    EditState *s;
    int has_popups;
    
    /* first display popups and minibuf */
    has_popups = 0;
    for(s = qs->first_window; s != NULL; s = s->next_window) {
        if (s->flags & WF_POPUP) {
            window_display(s);
            has_popups = 1;
        } else if (s->minibuf) {
            window_display(s);
        }
    }

    if (!has_popups) {
        for(s = qs->first_window; s != NULL; s = s->next_window) {
            if (!s->minibuf)
                window_display(s);
        }
    }
}

void do_universal_argument(EditState *s)
{
    /* nothing is done there (see qe_key_process()) */
}

static const char *keys_to_str(char *buf, int buf_size, 
                               unsigned int *keys, int nb_keys)
{
    char buf1[64];
    int i;

    buf[0] = '\0';
    for(i=0;i<nb_keys;i++) {
        keytostr(buf1, sizeof(buf1), keys[i]);
        if (i != 0)
            pstrcat(buf, buf_size, " ");
        pstrcat(buf, buf_size, buf1);
    }
    return buf;
}

/* macros */

void do_start_macro(EditState *s)
{
    QEmacsState *qs = s->qe_state;

    if (qs->defining_macro) {
        qs->defining_macro = 0;
        put_status(s, "Already defining kbd macro");
        return;
    }
    qs->defining_macro = 1;
    free(qs->macro_keys);
    qs->macro_keys = NULL;
    qs->nb_macro_keys = 0;
    qs->macro_keys_size = 0;
    put_status(s, "Defining kbd macro...");
}

void do_end_macro(EditState *s)
{
    QEmacsState *qs = s->qe_state;

    /* if called inside a macro, it is last recorded keys, so ignore
       it */
    if (qs->macro_key_index != -1)
        return;

    if (!qs->defining_macro) {
        put_status(s, "Not defining kbd macro");
        return;
    }
    qs->defining_macro = 0;
    put_status(s, "Keyboard macro defined");
}

static void do_call_macro_bh(void *opaque)
{
    QEmacsState *qs = &qe_state;
    int key;

    /* XXX: what to do if asynchronous commands ? Command completion
       should be wait */
    for(qs->macro_key_index = 0; 
        qs->macro_key_index < qs->nb_macro_keys;
        qs->macro_key_index++) {
        key = qs->macro_keys[qs->macro_key_index];
        qe_key_process(key);
    }
    qs->macro_key_index = -1;
}

void do_call_macro(EditState *s)
{
    QEmacsState *qs = s->qe_state;

    if (qs->defining_macro) {
        qs->defining_macro = 0;
        put_status(s, "Can't execute macro while defining one");
        return;
    }

    if (qs->nb_macro_keys > 0) {
        register_bottom_half(do_call_macro_bh, NULL);
    }
}

#define MACRO_KEY_INCR 64

static void macro_add_key(int key)
{
    QEmacsState *qs = &qe_state;
    unsigned short *keys;
    int new_size;

    if (qs->nb_macro_keys >= qs->macro_keys_size) {
        new_size = qs->macro_keys_size + MACRO_KEY_INCR;
        keys = realloc(qs->macro_keys, new_size * sizeof(unsigned short));
        if (!keys)
            return;
        qs->macro_keys = keys;
        qs->macro_keys_size = new_size;
    }
    qs->macro_keys[qs->nb_macro_keys++] = key;
}

typedef struct QEKeyContext {
    int argval;
    int noargval;
    int sign;
    int is_universal_arg;
    int is_escape;
    int nb_keys;
    int describe_key; /* if true, the following command is only displayed */
    void (*grab_key_cb)(void *opaque, int key);
    void *grab_key_opaque;
    unsigned int keys[MAX_KEYS];
    char buf[128];
} QEKeyContext;

QEKeyContext key_ctx;

/*
 * All typed keys are sent to the callback. Previous grab is aborted
 */
void qe_grab_keys(void (*cb)(void *opaque, int key), void *opaque)
{
    QEKeyContext *c = &key_ctx;
    c->grab_key_cb = cb;
    c->grab_key_opaque = opaque;
}

/*
 * Abort key grabing
 */
void qe_ungrab_keys(void)
{
    QEKeyContext *c = &key_ctx;
    c->grab_key_cb = NULL;
    c->grab_key_opaque = NULL;
}

/* init qe key handling context */
static void qe_key_init(void)
{
    QEKeyContext *c = &key_ctx;

    c->is_universal_arg = 0;
    c->is_escape = 0;
    c->noargval = 1;
    c->argval = NO_ARG;
    c->sign = 1;
    c->nb_keys = 0;
    c->buf[0] = '\0';
}

static void qe_key_process(int key)
{
    QEmacsState *qs = &qe_state;
    QEKeyContext *c = &key_ctx;
    EditState *s;
    KeyDef *kd;
    CmdDef *d;
    char buf1[32];
    int len;

    if (qs->defining_macro) {
        macro_add_key(key);
    }
    
    if (c->grab_key_cb) {
        c->grab_key_cb(c->grab_key_opaque, key);
        return;
    }

    if (c->nb_keys >= MAX_KEYS) {
        qe_key_init();
        c->describe_key = 0;
        return;
    }
    c->keys[c->nb_keys++] = key;
    s = qs->active_window;
    if (!s->minibuf) {
        put_status(s, "");
        dpy_flush(&global_screen);
    }

    /* special case for escape : we transform it as meta so
       that unix users are happy ! */
    if (key == KEY_ESC) {
        c->is_escape = 1;
        goto next;
    } else if (c->is_escape) {
        if (c->nb_keys >= 2) {
            c->nb_keys--;
            c->keys[c->nb_keys - 1] = KEY_META(key);
        }
        c->is_escape = 0;
    }

    /* see if one command is found */
    for(kd = first_key; kd != NULL; kd = kd->next) {
        if (kd->nb_keys >= c->nb_keys) {
            if (!memcmp(kd->keys, c->keys, 
                        c->nb_keys * sizeof(unsigned int)) && 
                (kd->mode == NULL || kd->mode == s->mode)) {
                break;
            }
        }
    }
    if (!kd) {
        /* no key found */
        if (c->nb_keys == 1) {
            if (!KEY_SPECIAL(key)) {
                if (c->is_universal_arg) {
                    if (key >= '0' && key <= '9') {
                        if (c->argval == NO_ARG)
                            c->argval = 0;
                        c->argval = c->argval * 10 + (key - '0');
                        c->nb_keys = 0;
                        goto next;
                    } else if (key == '-') {
                        c->sign = -c->sign;
                        c->nb_keys = 0;
                        goto next;
                    }
                }
                for(kd = first_key; kd != NULL; kd = kd->next) {
                    if (kd->nb_keys == 1 &&
                        kd->keys[0] == KEY_DEFAULT &&
                        (kd->mode == NULL || kd->mode == s->mode)) {
                        break;
                    }
                }
                if (kd) {
                    kd->cmd->val = (void *)key;
                    goto exec_cmd;
                }
            }
        }
        qe_key_init();
        c->describe_key = 0;
    } else if (c->nb_keys == kd->nb_keys) {
    exec_cmd:
        d = kd->cmd;
        if (d->action.func == (void *)do_universal_argument && 
            !c->describe_key) {
            /* special handling for universal argument */
            c->is_universal_arg = 1;
            c->noargval = c->noargval * 4;
            c->nb_keys = 0;
        } else {
            if (c->is_universal_arg) {
                if (c->argval == NO_ARG) {
                    c->argval = c->noargval;
                } else {
                    c->argval = c->argval * c->sign;
                }
            }
            if (c->describe_key) {
                char buf1[128];
                put_status(s, "%s runs the command %s", 
                           keys_to_str(buf1, sizeof(buf1), c->keys, c->nb_keys),
                           d->name);
                c->describe_key = 0;
            } else {
                exec_command(s, d, c->argval);
            }
            qe_key_init();
            edit_display(qs);
            dpy_flush(&global_screen);
            return;
        }
    }
 next:
    /* display key pressed */
    if (!s->minibuf) {
        keytostr(buf1, sizeof(buf1), key);
        len = strlen(c->buf);
        if (len >= 1)
            c->buf[len-1] = ' ';
        strcat(c->buf, buf1);
        strcat(c->buf, "-");
        put_status(s, c->buf);
        dpy_flush(&global_screen);
    }
}

/* Print in latin 1 charset */
void print_at_byte(QEditScreen *screen,
                   int x, int y, int width, int height,
                   const char *str, int style_index)
{
    unsigned int ubuf[256];
    int len;
    QEStyleDef style;
    QEFont *font;
    CSSRect rect;

    len = utf8_to_unicode(ubuf, sizeof(ubuf) / sizeof(ubuf[0]), str);
    get_style(NULL, &style, style_index);

    /* clip rectangle */
    rect.x1 = x;
    rect.y1 = y;
    rect.x2 = rect.x1 + width;
    rect.y2 = rect.y1 + height;
    set_clip_rectangle(screen, &rect);

    /* start rectangle */
    fill_rectangle(screen, x, y, width, height, 
                   style.bg_color);
    font = select_font(screen, style.font_style, style.font_size);
    draw_text(screen, font, x, y + font->ascent, 
              ubuf, len, style.fg_color);
}

void put_status(EditState *s, const char *fmt, ...)
{
    char buf[MAX_SCREEN_WIDTH];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (strcmp(buf, qe_state.status_shadow) != 0) {
        print_at_byte(qe_state.screen,
                      0, qe_state.screen->height - qe_state.status_height,
                      qe_state.screen->width, qe_state.status_height,
                      buf, QE_STYLE_STATUS);
        strcpy(qe_state.status_shadow, buf);
    }
}

void switch_to_buffer(EditState *s, EditBuffer *b)
{
    QEmacsState *qs = &qe_state;
    EditBuffer *b1;
    EditState *e;
    ModeSavedData *saved_data, **psaved_data;
    ModeDef *mode;

    b1 = s->b;
    if (b1) {
        /* save old buffer data if no other window uses the buffer */
        for(e = qs->first_window; e != NULL; e = e->next_window) {
            if (e != s && e->b == b1)
                break;
        }
        if (!e) {
            /* if no more window uses the buffer, then save the data
               in the buffer */
            b1->saved_data = s->mode->mode_save_data(s);
        }
        /* now we can close the mode */
        do_set_mode(s, NULL, NULL);
    }
    
    /* now we can switch ! */
    s->b = b;
    
    if (b) {
        /* try to restore saved data from another window or from the
           buffer saved data */
        for(e = qs->first_window; e != NULL; e = e->next_window) {
            if (e != s && e->b == b)
                break;
        }
        if (!e) {
            psaved_data = &b->saved_data;
            saved_data = *psaved_data;
        } else {
            psaved_data = NULL;
            saved_data = e->mode->mode_save_data(e);
        }

        /* find the mode */
        if (saved_data)
            mode = saved_data->mode;
        else
            mode = &text_mode; /* default mode */        

        /* open it ! */
        do_set_mode(s, mode, saved_data);
    }
}

/* compute the client area from the window position */
static void compute_client_area(EditState *s)
{
    int x1, y1, x2, y2;
    QEmacsState *qs = &qe_state;

    x1 = s->x1;
    y1 = s->y1;
    x2 = s->x2;
    y2 = s->y2;
    if (s->flags & WF_MODELINE)
        y2 -= qs->mode_line_height;
    if (s->flags & WF_POPUP) {
        x1 += qs->border_width;
        x2 -= qs->border_width;
        y1 += qs->border_width;
        y2 -= qs->border_width;
    }
    if (s->flags & WF_RSEPARATOR)
        x2 -= qs->separator_width;
        
    s->xleft = x1;
    s->ytop = y1;
    s->width = x2 - x1;
    s->height = y2 - y1;
}

/* Create a new edit window, add it in the window list and sets it
 * active if none are active. The coordinates include the window
 * borders.  
 */
EditState *edit_new(EditBuffer *b,
                    int x1, int y1, int width, int height, int flags)
{
    EditState *s;
    QEmacsState *qs = &qe_state;
    
    s = malloc(sizeof(EditState));
    if (!s)
        return NULL;
    memset(s, 0, sizeof(EditState));
    s->qe_state = qs;
    s->screen = qs->screen;
    s->x1 = x1;
    s->y1 = y1;
    s->x2 = x1 + width;
    s->y2 = y1 + height;
    s->flags = flags;
    compute_client_area(s);
    s->next_window = qs->first_window;
    qs->first_window = s;
    if (!qs->active_window)
        qs->active_window = s;
    switch_to_buffer(s, b);
    return s;
}

/* close the edit window. If it is active, find another active
   window. If the buffer is only referenced by this window, then save
   in the buffer all the window state so that it can be recovered. */
void edit_close(EditState *s)
{
    QEmacsState *qs = &qe_state;
    EditState **ps;

    switch_to_buffer(s, NULL);

    /* free from window list */
    ps = &qs->first_window;
    while (*ps != NULL) {
        if (*ps == s)
            break;
        ps = &(*ps)->next_window;
    }
    *ps = (*ps)->next_window;

    /* if active window, select another active window */
    if (qs->active_window == s)
        qs->active_window = qs->first_window;

    free(s->line_shadow);
    free(s);
}

/* XXX: buffer overflows */
void file_completion(StringArray *cs, const char *input)
{
    FindFileState *ffs;
    char path[1024];
    char file[1024];
    char filename[1024];
    const char *p;
    int input_path_len;
    
    p = strrchr(input, '/'); 
    if (!p) {
        input_path_len = 0;
        pstrcpy(file, sizeof(file), input);
        strcpy(path, ".");
    } else {
        input_path_len = p - input + 1;
        memcpy(path, input, input_path_len);
        if (input_path_len > sizeof(path) - 1)
            input_path_len = sizeof(path) - 1;
        path[input_path_len] = '\0';
        pstrcpy(file, sizeof(file), p + 1);
    }
    strcat(file, "*");
    ffs = find_file_open(path, file);
    while (find_file_next(ffs, filename, sizeof(filename)) == 0) {
        struct stat sb;
        p = basename(filename);
        memcpy(file, input, input_path_len);
        strcpy(file + input_path_len, p);
        /* stat the file to find out if it's a directory.
         * In that case add a slash to speed up typing long paths
         */
        stat(file, &sb);
        if(S_ISDIR(sb.st_mode))
            strcat(file, "/");
        add_string(cs, file);
    }

    find_file_close(ffs);
}

void buffer_completion(StringArray *cs, const char *input)
{
    QEmacsState *qs = &qe_state;
    EditBuffer *b;
    int len;

    len = strlen(input);
    for(b = qs->first_buffer; b != NULL; b = b->next) {
        if (!(b->flags & BF_SYSTEM) && !strncmp(b->name, input, len))
            add_string(cs, b->name);
    }
}

/* register a new completion method */
void register_completion(const char *name, CompletionFunc completion_func)
{
    CompletionEntry **lp, *p;

    p = malloc(sizeof(CompletionEntry));
    if (!p)
        return;
    p->name = name;
    p->completion_func = completion_func;
    p->next = NULL;

    lp = &first_completion;
    while (*lp != NULL) lp = &(*lp)->next;
    *lp = p;
}

static CompletionFunc find_completion(const char *name)
{
    CompletionEntry *p;

    if (name[0] != '\0') {
        for(p = first_completion; p != NULL; p = p->next)
            if (!strcmp(name, p->name))
                return p->completion_func;
    }
    return NULL;
}

static int completion_sort_func(const void *p1, const void *p2)
{
    StringItem *item1 = *(StringItem **)p1;
    StringItem *item2 = *(StringItem **)p2;
    return strcmp(item1->str, item2->str);
}

static void (*minibuffer_cb)(void *opaque, char *buf);
static void *minibuffer_opaque;
static EditState *minibuffer_saved_active;

static EditState *completion_popup_window;
static CompletionFunc completion_function;

static StringArray *minibuffer_history;
static int minibuffer_history_index;
static int minibuffer_history_saved_offset;


/* XXX: utf8 ? */
void do_completion(EditState *s)
{
    QEmacsState *qs = &qe_state;
    char input[1024];
    int len, count, i, match_len, c;
    StringArray cs;
    StringItem **outputs;
    EditState *e;
    int w, h, h1, w1;

    if (!completion_function) {
        return;
    }

    len = eb_get_str(s->b, input, sizeof(input));
    memset(&cs, 0, sizeof(cs));
    completion_function(&cs, input);
    count = cs.nb_items;
    outputs = cs.items;
#if 0
    printf("count=%d\n", count);
    for(i=0;i<count;i++)
        printf("out[%d]=%s\n", i, outputs[i]->str);
#endif
    /* no completion ? */
    if (count == 0)
        goto the_end;
    /* compute the longest match len */
    match_len = len;
    for(;;) {
        c = outputs[0]->str[match_len];
        if (c == '\0')
            break;
        for(i=1;i<count;i++)
            if (outputs[i]->str[match_len] != c)
                goto no_match;
        match_len++;
    }
 no_match:
    if (match_len > len) {
        /* add the possible chars */
        eb_write(s->b, 0, outputs[0]->str, match_len);
        s->offset = match_len;
    } else {
        if (count > 1) {
            /* if more than one match, then display them in a new popup
               buffer */
            if (!completion_popup_window) {
                EditBuffer *b;
                b = eb_new("*completion*", BF_SYSTEM | BF_READONLY);
                w1 = qs->screen->width;
                h1 = qs->screen->height - qs->status_height;
                w = (w1 * 3) / 4;
                h = (h1 * 3) / 4;
                e = edit_new(b, (w1 - w) / 2, (h1 - h) / 2, w, h, 
                             WF_POPUP);
                /* set list mode */
                do_set_mode(e, &list_mode, NULL);
                do_refresh(e);
                completion_popup_window = e;
            }
        }
        if (completion_popup_window) {
            EditBuffer *b = completion_popup_window->b;
            /* modify the list with the current matches */
            qsort(outputs, count, sizeof(StringItem *), completion_sort_func);
            eb_delete(b, 0, b->total_size);
            for(i=0;i<count;i++) {
                eb_printf(b, " %s", outputs[i]->str);
                if (i != count - 1)
                    eb_printf(b, "\n");
            }
            completion_popup_window->mouse_force_highlight = 1;
            completion_popup_window->force_highlight = 0;
            completion_popup_window->offset = 0;
        }
    }
 the_end:
    free_strings(&cs);
}

/* space does completion only if a completion method is defined */
void do_completion_space(EditState *s)
{
    if (!completion_function) {
        do_char(s, ' ');
    } else {
        do_completion(s);
    }
}

/* scroll in completion popup */
void minibuf_complete_scroll_up_down(EditState *s, int dir)
{
    if (completion_popup_window) {
        completion_popup_window->force_highlight = 1;
        do_scroll_up_down(completion_popup_window, dir);
    }
}

static void set_minibuffer_str(EditState *s, const char *str)
{
    int len;
    eb_delete(s->b, 0, s->b->total_size);
    len = strlen(str);
    eb_write(s->b, 0, (u8 *)str, len);
    s->offset = len;
}

static StringArray *get_history(const char *name)
{
    HistoryEntry *p;

    if (name[0] == '\0')
        return NULL;
    for(p = first_history; p != NULL; p = p->next) {
        if (!strcmp(p->name, name))
            return &p->history;
    }
    /* not found: allocate history list */
    p = malloc(sizeof(HistoryEntry));
    if (!p)
        return NULL;
    memset(p, 0, sizeof(HistoryEntry));
    pstrcpy(p->name, sizeof(p->name), name);
    p->next = first_history;
    first_history = p;
    return &p->history;
}

void do_history(EditState *s, int dir)
{
    QEmacsState *qs = &qe_state;
    StringArray *hist = minibuffer_history;
    int index;
    char *str;
    char buf[1024];
    
    /* if completion visible, move in it */
    if (completion_popup_window) {
        completion_popup_window->force_highlight = 1;
        do_up_down(completion_popup_window, dir);
        return;
    }

    if (!hist)
        return;
    index = minibuffer_history_index + dir;
    if (index < 0 || index >= hist->nb_items)
        return;
    if (qs->last_cmd_func != do_history) {
        /* save currently edited line */
        eb_get_str(s->b, buf, sizeof(buf));
        set_string(hist, hist->nb_items - 1, buf);
        minibuffer_history_saved_offset = s->offset;
    }
    /* insert history text */
    minibuffer_history_index = index;
    str = hist->items[index]->str;
    set_minibuffer_str(s, str);
    if (index == hist->nb_items - 1) {
        s->offset = minibuffer_history_saved_offset;
    }
}

void do_minibuffer_exit(EditState *s, int abort)
{
    QEmacsState *qs = &qe_state;
    EditBuffer *b = s->b;
    StringArray *hist = minibuffer_history;
    static void (*cb)(void *opaque, char *buf);
    static void *opaque;
    char buf[4096], *retstr;
    int len;

    /* if completion is activated, then select current file only if
       the selection is highlighted */
    if (completion_popup_window && 
        completion_popup_window->force_highlight) {
        int offset;
        char buf[1024];
        offset = list_get_offset(completion_popup_window);
        eb_get_strline(completion_popup_window->b, buf, sizeof(buf), &offset);
        if (buf[0] != '\0')
            set_minibuffer_str(s, buf + 1);
    }

    /* remove completion popup if present */
    if (completion_popup_window) {
        EditBuffer *b = completion_popup_window->b;
        edit_close(completion_popup_window);
        eb_free(b);
        do_refresh(s);
    }

    len = eb_get_str(s->b, buf, sizeof(buf));
    if (hist && hist->nb_items > 0) {
        /* if null string, do not insert in history */
        hist->nb_items--;
        free(hist->items[hist->nb_items]);
        hist->items[hist->nb_items] = NULL;
        if (buf[0] != '\0')
            add_string(hist, buf);
    }

    /* free prompt */
    free(s->prompt);

    edit_close(s);
    eb_free(b);
    /* restore active window */
    qs->active_window = minibuffer_saved_active;

    /* force status update */
    strcpy(qe_state.status_shadow, " ");
    put_status(NULL, "");

    /* call the callback */
    cb = minibuffer_cb;
    opaque = minibuffer_opaque;
    minibuffer_cb = NULL;
    minibuffer_opaque = NULL;

    if (abort) {
        cb(opaque, NULL);
    } else {
        retstr = strdup(buf);
        cb(opaque, retstr);
    }
}

/* Start minibuffer editing. When editing is finished, the callback is
   called with an allocated string. If the string is null, it means
   editing was aborted. */
void minibuffer_edit(const char *input, const char *prompt, 
                     StringArray *hist, CompletionFunc completion_func,
                     void (*cb)(void *opaque, char *buf), void *opaque)
{

    EditState *s;
    QEmacsState *qs = &qe_state;
    EditBuffer *b;
    int len;
    
    /* check if already in minibuffer editing */
    if (minibuffer_cb) {
        put_status(NULL, "Already editing in minibuffer");
        cb(opaque, NULL);
        return;
    }
        
    minibuffer_cb = cb;
    minibuffer_opaque = opaque;

    b = eb_new("*minibuf*", BF_SYSTEM);

    s = edit_new (b, 0, qs->screen->height - qs->status_height,
                  qs->screen->width, qs->status_height, 0);
    do_set_mode(s, &minibuffer_mode, NULL);
    s->prompt = strdup(prompt);
    s->minibuf = 1;
    s->bidir = 0;
    s->default_style = QE_STYLE_MINIBUF;
    s->wrap = WRAP_TRUNCATE;

    /* add default input */
    if (input) {
        len = strlen(input);
        eb_write(b, 0, (u8 *)input, len);
        s->offset = len;
    }

    minibuffer_saved_active = qs->active_window;
    qs->active_window = s;

    completion_popup_window = NULL;
    completion_function = completion_func;
    minibuffer_history = hist;
    minibuffer_history_saved_offset = 0;
    if (hist) {
        minibuffer_history_index = hist->nb_items;
        add_string(hist, "");
    }
}

ModeDef less_mode;
/* XXX: incorrect to save it. Should use a safer method */
static EditState *popup_saved_active;

/* less like mode */
void do_less_quit(EditState *s)
{
    QEmacsState *qs = &qe_state;
    EditBuffer *b;

    qs->active_window = popup_saved_active;
    b = s->b;
    edit_close(s);
    eb_free(b);
}

/* show a popup on a readonly buffer */
void show_popup(EditBuffer *b)
{
    EditState *s;
    QEmacsState *qs = &qe_state;
    int w, h, w1, h1;
    
    /* XXX: generic function to open popup ? */
    w1 = qs->screen->width;
    h1 = qs->screen->height - qs->status_height;
    w = (w1 * 4) / 5;
    h = (h1 * 3) / 4;

    s = edit_new(b, (w1 - w) / 2, (h1 - h) / 2, w, h,
                 WF_POPUP);
    do_set_mode(s, &less_mode, NULL);
    s->wrap = WRAP_TRUNCATE;

    popup_saved_active = qs->active_window;
    qs->active_window = s;
    do_refresh(s);
}

#ifndef CONFIG_TINY
/* insert a window to the left. Close all windows which are totally
   under it (XXX: should try to move them first */
EditState *insert_window_left(EditBuffer *b, int width, int flags)
{
    QEmacsState *qs = &qe_state;
    EditState *e, *e_next, *e_new;

    for(e = qs->first_window; e != NULL; e = e_next) {
        e_next = e->next_window;
        if (e->minibuf) 
            continue;
        if (e->x2 <= width) {
            edit_close(e);
        } else if (e->x1 < width) {
            e->x1 = width;
        }
    }
     
    e_new = edit_new(b, 0, 0, width, qs->height - qs->status_height, 
                     flags | WF_RSEPARATOR);
    do_refresh(qs->first_window);
    return e_new;
}

/* return a window on the right of window 's' */
EditState *find_window_right(EditState *s)
{
    QEmacsState *qs = &qe_state;
    EditState *e;

    for(e = qs->first_window; e != NULL; e = e->next_window) {
        if (e->minibuf) 
            continue;
        if (e->x1 == s->x2)
            return e;
    }
    return NULL;
}
#endif

/* give a good guess to the user for the next buffer */
static EditBuffer *predict_switch_to_buffer(EditState *s)
{
    QEmacsState *qs = s->qe_state;
    EditState *e;
    EditBuffer *b;

    for(b = qs->first_buffer; b != NULL; b = b->next) {
        if (!(b->flags & BF_SYSTEM)) {
            for(e = qs->first_window; e != NULL; e = e->next_window) {
                if (e->b == b)
                    break;
            }
            if (!e) 
                goto found;
        }
    }
    /* select current buffer if none found */
    b = s->b;
 found:
    return b;
}

void do_switch_to_buffer(EditState *s, const char *bufname)
{
    EditBuffer *b;

    b = eb_find(bufname);
    if (!b)
        b = eb_new(bufname, BF_SAVELOG);
    if (b)
        switch_to_buffer(s, b);
}

void do_toggle_read_only(EditState *s)
{
    s->b->flags ^= BF_READONLY;
}

static void kill_buffer_confirm_cb(void *opaque, char *reply);
static void kill_buffer_noconfirm(EditBuffer *b);

void do_kill_buffer(EditState *s, const char *bufname)
{
    char buf[1024];
    EditBuffer *b;

    b = eb_find(bufname);
    if (!b) {
        put_status(s, "No match");
    } else {
        /* if associated to a filename, then ask */
        if (b->modified && b->filename[0] != '\0') {
            snprintf(buf, sizeof(buf), 
                     "Buffer %s modified; kill anyway? (yes or no) ", bufname);
            minibuffer_edit(NULL, buf, NULL, NULL,
                            kill_buffer_confirm_cb, b);
        } else {
            kill_buffer_noconfirm(b);
        }
    }
}

static void kill_buffer_confirm_cb(void *opaque, char *reply)
{
    int yes_replied;
    if (!reply)
        return;
    yes_replied = (strcmp(reply, "yes") == 0);
    free(reply);
    if (!yes_replied)
        return;
    kill_buffer_noconfirm(opaque);
}

static void kill_buffer_noconfirm(EditBuffer *b)
{
    QEmacsState *qs = &qe_state;
    EditState *e;
    EditBuffer *b1;
    
    /* find a new buffer to switch to */
    for(b1 = qs->first_buffer; b1 != NULL; b1 = b1->next) {
        if (b1 != b && !(b1->flags & BF_SYSTEM))
            break;
    }
    if (!b1) 
        b1 = eb_new("*scratch*", BF_SAVELOG);
    
    /* if the buffer remains because we cannot delete the main
       window, then switch to the scratch buffer */
    for(e = qs->first_window; e != NULL; e = e->next_window) {
        if (e->b == b) {
            switch_to_buffer(e, b1);
        }
    }
    
    /* now we can safely delete buffer */
    eb_free(b);
    
    do_refresh(qs->first_window);
}

/* compute default path for find/save buffer */
static void get_default_path(EditState *s, char *buf, int buf_size)
{
    EditBuffer *b = s->b;
    char buf1[1024];
    const char *filename;

    if ((b->flags & BF_SYSTEM) || b->name[0] == '*') {
        canonize_absolute_path(buf1, sizeof(buf1), "a");
        filename = buf1;
    } else {
        filename = s->b->filename;
    }
    pathname(buf, buf_size, filename);
}

static ModeDef *probe_mode(EditState *s, int mode, uint8_t *buf, int len)
{
    EditBuffer *b = s->b;
    ModeDef *m, *selected_mode;
    ModeProbeData probe_data;
    int best_probe_percent, percent;

    m = first_mode;
    selected_mode = NULL;
    best_probe_percent = 0;
    probe_data.buf = buf;
    probe_data.buf_size = len;
    probe_data.filename = b->filename;
    probe_data.mode = mode;

    while (m != 0) {
        if (m->mode_probe) {
            percent = m->mode_probe(&probe_data);
            if (percent > best_probe_percent) {
                selected_mode = m;
                best_probe_percent = percent;
            }
        }
        m = m->next;
    }
    return selected_mode;
}

static void do_load1(EditState *s, const char *filename1, int kill_buffer)
{
    char buf[1025];
    char filename[1024];
    int mode, buf_size;
    ModeDef *selected_mode;
    EditBuffer *b;
    EditBufferDataType *bdt;
    FILE *f;
    struct stat st;

    if (kill_buffer) {
        do_kill_buffer(s, s->b->name);
    }

    /* compute full name */
    canonize_absolute_path(filename, sizeof(filename), filename1);

    /* see if file is already edited */
    b = eb_find_file(filename);
    if (b) {
        switch_to_buffer(s, b);
        return;
    }

    /* create new buffer */
    b = eb_new("", BF_SAVELOG);
    set_filename(b, filename);

    /* switch to the newly created buffer */
    switch_to_buffer(s, b);

    s->offset = 0;
    s->wrap = WRAP_LINE;

    /* first we try to read the first bytes of the buffer to find the
       buffer data type */
    if (stat(filename, &st) < 0) {
        put_status(s, "(New file)");
	/* Try to determine the desired mode based on the filename.
	 * This avoids having to set c-mode for each new .c or .h file. */
	buf[0] = '\0';
	selected_mode = probe_mode(s, S_IFREG, buf, 0);
        /* XXX: avoid loading file */
	if (selected_mode)
	    do_set_mode(s, selected_mode, NULL);
        return;
    } else {
        mode = st.st_mode;
        buf_size = 0;
        if (S_ISREG(mode)) {
            f = fopen(filename, "r");
            if (!f) 
                goto fail;
            buf_size = fread(buf, 1, sizeof(buf) - 1, f);
            if (buf_size < 0) {
            fail1:
                fclose(f);
                goto fail;
            }
        } else {
            f = NULL;
        }
    }
    buf[buf_size] = '\0';
    selected_mode = probe_mode(s, mode, buf, buf_size);
    if (!selected_mode)
        goto fail1;
    bdt = selected_mode->data_type;

    /* autodetect buffer charset (could move it to raw buffer loader) */
    if (bdt == &raw_data_type) 
        eb_set_charset(b, detect_charset(buf, buf_size));

    /* now we can set the mode */
    do_set_mode_file(s, selected_mode, NULL, f);

    if (f) {
        fclose(f);
    }

    /* XXX: invalid place */
    edit_invalidate(s);
    return;
 fail:
    put_status(s, "Could not open '%s'", filename);
}

#if 0

static void load_progress_cb(void *opaque, int size)
{
    EditState *s = opaque;
    EditBuffer *b = s->b;
    if (size >= 1024 && !b->probed)
        probe_mode(s, S_IFREG);
}

static void load_completion_cb(void *opaque, int err)
{
    EditState *s = opaque;
    int mode;

    mode = S_IFREG;
    if (err == -ENOENT || err == -ENOTDIR) {
        put_status(s, "(New file)");
    } else if (err == -EISDIR) {
        mode = S_IFDIR;
    } else if (err < 0) {
        put_status(s, "Could not read file");
    }
    if (!s->b->probed)
        probe_mode(s, mode);
    edit_display(&qe_state);
    dpy_flush(&global_screen);
}
#endif

void do_load(EditState *s, const char *filename)
{
    do_load1(s, filename, 0);
}

void do_find_alternate_file(EditState *s, const char *filename)
{
    do_load1(s, filename, 1);
}

void do_insert_file(EditState *s, const char *filename)
{
    FILE *f;
    f = fopen(filename, "r");
    if (!f) {
        put_status(s, "Could not insert file '%s'", filename);
        return;
    }
    raw_load_buffer1(s->b, f, s->offset);
    fclose(f);
}

static void save_edit_cb(void *opaque, char *filename);
static void save_final(EditState *s);

void do_save(EditState *s, int save_as)
{
    char default_path[1024];

    if (!save_as && !s->b->modified) {
        put_status(s, "(No changes need to be saved)");
        return;
    }

    if (save_as || s->b->filename[0] == '\0') {
        get_default_path(s, default_path, sizeof(default_path));
        minibuffer_edit(default_path, 
                        "Write file: ", get_history("file"), file_completion, 
                        save_edit_cb, s);
    } else {
        save_final(s);
    }
}

static void save_edit_cb(void *opaque, char *filename)
{
    EditState *s = opaque;
    if (!filename)
        return;
    set_filename(s->b, filename);
    free(filename);
    save_final(s);
}

static void save_final(EditState *s)
{
    int ret;
    ret = save_buffer(s->b);
    if (ret == 0) {
        put_status(s, "Wrote %s", s->b->filename);
    } else {
        put_status(s, "Could not write %s", s->b->filename);
    }
}

typedef struct QuitState {
    enum {
        QS_ASK,
        QS_NOSAVE,
        QS_SAVE,
    } state;
    int modified;
    EditBuffer *b;
} QuitState;

static void quit_examine_buffers(QuitState *is);
static void quit_key(void *opaque, int ch);
static void quit_confirm_cb(void *opaque, char *reply);

void do_quit(EditState *s)
{
    QEmacsState *qs = s->qe_state;
    QuitState *is;

    is = malloc(sizeof(QuitState));
    if (!is)
        return;
    
    /* scan each buffer and ask to save it if it was modified */
    is->modified = 0;
    is->state = QS_ASK;
    is->b = qs->first_buffer;
    
    qe_grab_keys(quit_key, is);
    quit_examine_buffers(is);
}

/* analyse next buffer and ask question if needed */
static void quit_examine_buffers(QuitState *is)
{
    EditBuffer *b;
    
    while (is->b != NULL) {
        b = is->b;
        if (!(b->flags & BF_SYSTEM) && b->filename[0] != '\0' && b->modified) {
            switch(is->state) {
            case QS_ASK:
                /* XXX: display cursor */
                put_status(NULL, "Save file %s? (y, n, !, ., q) ", 
                           b->filename);
                dpy_flush(&global_screen);
                /* will wait for a key */
                return;
            case QS_NOSAVE:
                is->modified = 1;
                break;
            case QS_SAVE:
                save_buffer(b);
                break;
            }
        }
        is->b = is->b->next;
    }
    qe_ungrab_keys();

    /* now asks for confirmation or exit directly */
    if (is->modified) {
        minibuffer_edit(NULL, "Modified buffers exist; exit anyway? (yes or no) ", 
                        NULL, NULL,
                        quit_confirm_cb, NULL);
        edit_display(&qe_state);
        dpy_flush(&global_screen);
    } else {
        url_exit();
    }
}

static void quit_key(void *opaque, int ch)
{
    QuitState *is = opaque;
    EditBuffer *b;

    switch(ch) {
    case 'y':
    case ' ':
        /* save buffer */
        goto do_save;
    case 'n':
    case KEY_DELETE:
        is->modified = 1;
        break;
    case 'q':
    case KEY_RET:
        is->state = QS_NOSAVE;
        is->modified = 1;
        break;
    case '!':
        /* save all buffers */
        is->state = QS_SAVE;
        goto do_save;
    case '.':
        /* save current and exit */
        is->state = QS_NOSAVE;
    do_save:
        b = is->b;
        save_buffer(b);
        break;
    case KEY_CTRL('g'):
        /* abort */
        put_status(NULL, "Quit");
        dpy_flush(&global_screen);
        qe_ungrab_keys();
        return;
    default:
        /* get another key */
        return;
    }
    is->b = is->b->next;
    quit_examine_buffers(is);
}

static void quit_confirm_cb(void *opaque, char *reply)
{
    if (!reply)
        return;
    if (reply[0] == 'y' || reply[0] == 'Y')
        url_exit();
    free(reply);
}


#define SEARCH_FLAG_IGNORECASE 0x0001 
#define SEARCH_FLAG_SMARTCASE  0x0002 /* case sensitive if upper case present */
#define SEARCH_FLAG_WORD       0x0004

/* XXX: OPTIMIZE ! */
/* XXX: use UTF8 for words/chars ? */
int eb_search(EditBuffer *b, int offset, int dir, u8 *buf, int size, 
              int flags, CSSAbortFunc *abort_func, void *abort_opaque)
{
    int total_size = b->total_size;
    int i, c, lower_count, upper_count;
    unsigned char ch;
    u8 buf1[1024];

    if (size == 0 || size >= sizeof(buf1))
        return -1;
    
    /* analyse buffer if smart case */
    if (flags & SEARCH_FLAG_SMARTCASE) {
        upper_count = 0;
        lower_count = 0;
        for(i=0;i<size;i++) {
            c = buf[i];
            lower_count += islower(c);
            upper_count += isupper(c);
        }
        if (lower_count > 0 && upper_count == 0)
            flags |= SEARCH_FLAG_IGNORECASE;
    }

    /* copy buffer */
    for(i=0;i<size;i++) {
        c = buf[i];
        if (flags & SEARCH_FLAG_IGNORECASE) 
            buf1[i] = toupper(c);
        else
            buf1[i] = c;
    }

    if (dir < 0) {
        if (offset > (total_size - size))
            offset = total_size - size;
    } else {
        offset--;
    }
        
    for(;;) {
        offset += dir;
        if (offset < 0)
            return -1;
        if (offset > (total_size - size))
            return -1;
        /* search abort */
        if ((offset & 0xfff) == 0) {
            if (abort_func && abort_func(abort_opaque))
                return -1;
        }

        /* search start of word */
        if (flags & SEARCH_FLAG_WORD) {
            if (offset == 0)
                goto word_start_found;
            eb_read(b, offset - 1, &ch, 1);
            if (!isword(ch))
                goto word_start_found;
            else
                continue;
        }

    word_start_found:
        i = 0;
        for(;;) {
            eb_read(b, offset + i, &ch, 1);
            if (flags & SEARCH_FLAG_IGNORECASE) 
                ch = toupper(ch);
            if (ch != buf1[i])
                    break;
            i++;
            if (i == size) {
                /* check end of word */
                if (flags & SEARCH_FLAG_WORD) {
                    if (offset + size >= total_size)
                        goto word_end_found;
                    eb_read(b, offset + size, &ch, 1);
                    if (!isword(ch))
                        goto word_end_found;
                    break;
                }
            word_end_found:
                return offset;
            }
        }
    }
}

void usprintf(char **pp, const char *fmt, ...)
{
    char *q = *pp;
    int len;
    va_list ap;

    va_start(ap, fmt);
    len = vsprintf(q, fmt, ap);
    q += len;
    *pp = q; ;
    va_end(ap);
}

#define SEARCH_LENGTH 80
#define FOUND_TAG 0x80000000

/* store last searched string */
static unsigned int last_search_string[SEARCH_LENGTH];
static int last_search_string_len = 0;

int search_abort_func(void *opaque)
{
    return is_user_input_pending();
}

typedef struct ISearchState {
    EditState *s;
    int start_offset;
    int dir;
    int pos;
    int stack_ptr;
    int search_flags;
    int found_offset;
    unsigned int search_string[SEARCH_LENGTH];
} ISearchState;

static void isearch_display(ISearchState *is)
{
    EditState *s = is->s;
    char ubuf[256], *uq;
    u8 buf[2*SEARCH_LENGTH], *q; /* XXX: incorrect size */
    int i, len, hex_nibble, h;
    unsigned int v;
    int search_offset;
    int flags;
    
    /* prepare the search bytes */
    q = buf;
    search_offset = is->start_offset;
    hex_nibble = 0;
    for(i=0;i<is->pos;i++) {
        v = is->search_string[i];
        if (!(v & FOUND_TAG)) {
            if ((q - buf) < (sizeof(buf) - 10)) {
                if (s->hex_mode) {
                    h = to_hex(v);
                    if (h >= 0) {
                        if (hex_nibble == 0)
                            *q = h << 4;
                        else
                            *q++ |= h;
                        hex_nibble ^= 1;
                    }
                } else {
                    q += unicode_to_charset(q, v, s->b->charset);
                }
            }
        } else {
            search_offset = (v & ~FOUND_TAG) + is->dir;
        }
    }
    len = q - buf;
    if (len == 0) {
        s->offset = is->start_offset;
        is->found_offset = -1;
    } else {
        flags = is->search_flags;
        if (s->hex_mode)
            flags = 0;
        is->found_offset = eb_search(s->b, search_offset, is->dir, buf, len, 
                                     flags, search_abort_func, NULL);
        if (is->found_offset >= 0)
            s->offset = is->found_offset + len;
    }
            
    /* display search string */
    uq = ubuf;
    if (is->found_offset < 0 && len > 0)
        usprintf(&uq, "Failing ");
    if (s->hex_mode) {
        usprintf(&uq, "hex ");
    } else {
        if (is->search_flags & SEARCH_FLAG_WORD)
            usprintf(&uq, "word ");
        if (is->search_flags & SEARCH_FLAG_IGNORECASE)
            usprintf(&uq, "case-insensitive ");
        else if (!(is->search_flags & SEARCH_FLAG_SMARTCASE))
            usprintf(&uq, "case-sensitive ");
    }
    usprintf(&uq, "I-search");
    if (is->dir < 0)
        usprintf(&uq, " backward");
    usprintf(&uq, ": ");
    for(i=0;i<is->pos;i++) {
        v = is->search_string[i];
        if (!(v & FOUND_TAG)) {
            uq = utf8_encode(uq, v);
        }
    }
    *uq = '\0';

        /* display text */
    center_cursor(s);
    edit_display(s->qe_state);

    put_status(NULL, ubuf);

    dpy_flush(s->screen);
}

static void isearch_key(void *opaque, int ch)
{
    ISearchState *is = opaque;
    EditState *s = is->s;
    int i, j;

    switch(ch) {
    case KEY_BACKSPACE:
        if (is->pos > 0)
            is->pos--;
        break;
    case KEY_CTRL('g'):
        s->offset = is->start_offset;
        put_status(s, "Quit");
    the_end:
        /* save current searched string */
        if (is->pos > 0) {
            j = 0;
            for(i=0;i<is->pos;i++) {
                if (!(is->search_string[i] & FOUND_TAG))
                    last_search_string[j++] = is->search_string[i];
            }
            last_search_string_len = j;
        }
        qe_ungrab_keys();
        free(is);
        return;
    case KEY_CTRL('s'):
        is->dir = 1;
        goto addpos;
    case KEY_CTRL('r'):
        is->dir = -1;
    addpos:
        /* use last seached string if no input */
        if (is->pos == 0) {
            memcpy(is->search_string, last_search_string, 
                   last_search_string_len * sizeof(unsigned int));
            is->pos = last_search_string_len;
        } else {
            /* add the match position, if any */
            if (is->pos < SEARCH_LENGTH && is->found_offset >= 0)
                is->search_string[is->pos++] = FOUND_TAG | is->found_offset;
        }
        break;
#if 0
    case KEY_CTRL('q'):
        ch = get_key(s->screen);
        goto addch; 
    case KEY_CTRL('w'):
    case KEY_CTRL('y'):
        /* emacs compatibility: get word / line */
        break;
#endif
        /* case / word */
    case KEY_CTRL('w'):
        is->search_flags ^= SEARCH_FLAG_WORD;
        break;
    case KEY_CTRL('c'):
        is->search_flags ^= SEARCH_FLAG_IGNORECASE;
        is->search_flags &= ~SEARCH_FLAG_SMARTCASE;
        break;
    default:
        if (KEY_SPECIAL(ch)) {
            /* exit search mode */
            s->b->mark = is->start_offset;
            put_status(s, "Mark saved where search started");
            /* repost key */
            if (ch != KEY_RET)
                unget_key(ch);
            goto the_end;
        } else {
            //        addch:
            if (is->pos < SEARCH_LENGTH) {
                is->search_string[is->pos++] = ch;
            }
        }
        break;
    }
    isearch_display(is);
}

/* XXX: handle busy */
void do_isearch(EditState *s, int dir)
{
    ISearchState *is;

    is = malloc(sizeof(ISearchState));
    if (!is)
        return;
    is->s = s;
    is->start_offset = s->offset;
    is->dir = dir;
    is->pos = 0;
    is->stack_ptr = 0;
    is->search_flags = SEARCH_FLAG_SMARTCASE;
    
    qe_grab_keys(isearch_key, is);
    isearch_display(is);
}

static int to_bytes(EditState *s1, u8 *dst, int dst_size, const char *str)
{
    const char *s;
    int c, len, hex_nibble, h;
    u8 *d;

    d = dst;
    if (s1->hex_mode) {
        s = str;
        h = 0;
        hex_nibble = 0;
        for(;;) {
            c = *s++;
            if (c == '\0')
                break;
            c = to_hex(c);
            if (c >= 0) {
                h = (h << 4) | c;
                if (hex_nibble) {
                    if (d < dst + dst_size)
                        *d++ = h;
                    h = 0;
                }
                hex_nibble ^= 1;
            }
        }
        return d - dst;
    } else {
        /* XXX: potentially incorrect if charset change */
        len = strlen(str);
        if (len > dst_size)
            len = dst_size;
        memcpy(dst, str, len);
        return len;
    }
}

typedef struct QueryReplaceState {
    EditState *s;
    int nb_reps;
    int search_bytes_len, replace_bytes_len, found_offset;
    int replace_all;
    char search_str[SEARCH_LENGTH];
    char replace_str[SEARCH_LENGTH];
    u8 search_bytes[SEARCH_LENGTH];
    u8 replace_bytes[SEARCH_LENGTH];
} QueryReplaceState;

static void query_replace_abort(QueryReplaceState *is)
{
    EditState *s = is->s;

    qe_ungrab_keys();
    put_status(NULL, "Replaced %d occurrences", is->nb_reps);
    free(is);
    edit_display(s->qe_state);
    dpy_flush(&global_screen);
}

static void query_replace_replace(QueryReplaceState *is)
{
    EditState *s = is->s;

    eb_delete(s->b, is->found_offset, is->search_bytes_len);
    eb_insert(s->b, is->found_offset, is->replace_bytes, is->replace_bytes_len);
    is->found_offset += is->replace_bytes_len;
    is->nb_reps++;
}

static void query_replace_display(QueryReplaceState *is)
{
    EditState *s = is->s;

 redo:
    is->found_offset = eb_search(s->b, is->found_offset, 1, 
                                 is->search_bytes, is->search_bytes_len, 
                                 0, NULL, NULL);
    if (is->found_offset < 0) {
        query_replace_abort(is);
        return;
    }
    
    if (is->replace_all) {
        query_replace_replace(is);
        goto redo;
    }
    
    /* display text */
    s->offset = is->found_offset;
    center_cursor(s);
    edit_display(s->qe_state);
    
    put_status(NULL, "Query replace %s with %s: ", 
               is->search_str, is->replace_str);
    dpy_flush(&global_screen);
}

static void query_replace_key(void *opaque, int ch)
{
    QueryReplaceState *is = opaque;

    switch(ch) {
    case 'y':
    case ' ':
        query_replace_replace(is);
        break;
    case '!':
        is->replace_all = 1;
        break;
    case 'n':
    case KEY_DELETE:
        break;
    default:
        query_replace_abort(is);
        return;
    }
    query_replace_display(is);
}
    
static void do_query_replace(EditState *s, 
                             const char *search_str, const char *replace_str)
{
    QueryReplaceState *is;
    
    if (s->b->flags & BF_READONLY)
        return;

    is = malloc(sizeof(QueryReplaceState));
    if (!is)
        return;
    is->s = s;
    pstrcpy(is->search_str, sizeof(is->search_str), search_str);
    pstrcpy(is->replace_str, sizeof(is->replace_str), replace_str);

    is->search_bytes_len = to_bytes(s, is->search_bytes, sizeof(is->search_bytes), 
                                    search_str);
    is->replace_bytes_len = to_bytes(s, is->replace_bytes, sizeof(is->replace_bytes), 
                                     replace_str);
    is->nb_reps = 0;
    is->replace_all = 0;
    is->found_offset = s->offset;

    qe_grab_keys(query_replace_key, is);
    query_replace_display(is);
}

void do_doctor(EditState *s)
{
    put_status(s, "Hello, how are you ?");
}

static int get_line_height(QEditScreen *screen, int style_index)
{
    QEFont *font;
    QEStyleDef style;

    get_style(NULL, &style, style_index);
    font = select_font(screen, style.font_style, style.font_size);
    return font->ascent + font->descent;
}

void edit_invalidate(EditState *s)
{
    /* invalidate the modeline buffer */
    s->modeline_shadow[0] = '\0';
    s->display_invalid = 1;
}

void do_refresh(EditState *s1)
{
    QEmacsState *qs = &qe_state;
    EditState *e;
    int new_status_height, new_mode_line_height, content_height;
    int width, height;

    /* recompute various dimensions */
    if (qs->screen->media & CSS_MEDIA_TTY) {
        qs->separator_width = 1;
    } else {
        qs->separator_width = 4;
    }
    qs->border_width = 1; /* XXX: adapt to display type */
    
    width = qs->screen->width;
    height = qs->screen->height;
    new_status_height = get_line_height(qe_state.screen, 
                                        QE_STYLE_STATUS);
    new_mode_line_height = get_line_height(qe_state.screen, 
                                           QE_STYLE_MODE_LINE);
    content_height = height;
    if (!qs->hide_status) 
        content_height -= new_status_height;

    /* see if resize is necessary */
    if (qs->width != width ||
        qs->height != height ||
        qs->status_height != new_status_height ||
        qs->mode_line_height != new_mode_line_height ||
        qs->content_height != content_height) {
        
        /* do the resize */
        for(e = qs->first_window; e != NULL; e = e->next_window) {
            if (e->minibuf) {
                /* first resize minibuffer if present */
                e->x1 = 0;
                e->y1 = content_height;
                e->x2 = width;
                e->y2 = height;
            } else if (qs->height == 0) {
                /* needed only to init the window size for the first time */
                e->x1 = 0;
                e->y1 = 0;
                e->y2 = content_height;
                e->x2 = width;
            } else {
                /* NOTE: to ensure that no rounding errors are made,
                   we resize the absolute coordinates */
                e->x1 = (e->x1 * width) / qs->width;
                e->x2 = (e->x2 * width) / qs->width;
                e->y1 = (e->y1 * content_height) / qs->content_height;
                e->y2 = (e->y2 * content_height) / qs->content_height;
            }
        }
        
        qs->width = width;
        qs->height = height;
        qs->status_height = new_status_height;
        qs->mode_line_height = new_mode_line_height;
        qs->content_height = content_height;
    }
    /* compute client area */
    for(e = qs->first_window; e != NULL; e = e->next_window)
        compute_client_area(e);

    /* invalidate all the edit windows and draw borders */
    for(e = qs->first_window; e != NULL; e = e->next_window) {
        edit_invalidate(e);
        if (e->flags & (WF_POPUP | WF_RSEPARATOR)) {
            CSSRect rect;
            QEColor color;
            rect.x1 = 0;
            rect.y1 = 0;
            rect.x2 = qs->width;
            rect.y2 = qs->height;
            set_clip_rectangle(qs->screen, &rect);
            color = qe_styles[QE_STYLE_MODE_LINE].bg_color;
            if (e->flags & WF_POPUP) {
                fill_rectangle(qs->screen, 
                               e->x1, e->y1, 
                               qs->border_width, e->y2 - e->y1, color);
                fill_rectangle(qs->screen, 
                               e->x2 - qs->border_width, e->y1, 
                               qs->border_width, e->y2 - e->y1, color);
                fill_rectangle(qs->screen, 
                               e->x1, e->y1, 
                               e->x2 - e->x1, qs->border_width, color);
                fill_rectangle(qs->screen, 
                               e->x1, e->y2 - qs->border_width, 
                               e->x2 - e->x1, qs->border_width, color);
            }
            if (e->flags & WF_RSEPARATOR) {
                fill_rectangle(qs->screen, 
                               e->x2 - qs->separator_width, e->y1, 
                               qs->separator_width, e->y2 - e->y1, color);
            }
        }
    }
    /* invalidate status line */
    qs->status_shadow[0] = '\0';
    put_status(NULL, " ");
}

void do_other_window(EditState *s)
{
    QEmacsState *qs = s->qe_state;
    EditState *e;

    e = s->next_window;
    if (!e)
        e = qs->first_window;
    qs->active_window = e;
}

/* Delete a window and try to resize other window so that it get
   covered. If force is not true, do not accept to kill window if it
   is the only window or if it is the minibuffer window. */
void do_delete_window(EditState *s, int force)
{
    QEmacsState *qs = s->qe_state;
    EditState *e;
    int count, x1, y1, x2, y2;
    int ex1, ey1, ex2, ey2;

    count = 0;
    for(e = qs->first_window; e != NULL; e = e->next_window) {
        if (!e->minibuf)
            count++;
    }
    /* cannot close minibuf or if single window */
    if ((!s->minibuf && count > 1) || force) {
        /* try to merge the window with one adjacent window. If none
           found, just leave a hole */
        x1 = s->x1;
        x2 = s->x2;
        y1 = s->y1;
        y2 = s->y2;

        for(e = qs->first_window; e != NULL; e = e->next_window) {
            if (e->minibuf || e == s) 
                continue;
            ex1 = e->x1;
            ex2 = e->x2;
            ey1 = e->y1;
            ey2 = e->y2;
            
            if (x1 == ex2 && y1 == ey1 && y2 == ey2) {
                /* left border */
                e->x2 = x2;
                break;
            } else if (x2 == ex1 && y1 == ey1 && y2 == ey2) {
                /* right border */
                e->x1 = x1;
                break;
            } else if (y1 == ey2 && x1 == ex1 && x2 == ex2) {
                /* top border */
                e->y2 = y2;
                break;
            } else if (y2 == ey1 && x1 == ex1 && x2 == ex2) {
                /* bottom border */
                e->y1 = y1;
                break;
            }
        }
        compute_client_area(e);        
        edit_close(s);
        do_refresh(qs->first_window);
    }
}


void do_delete_other_windows(EditState *s)
{
    QEmacsState *qs = s->qe_state;
    EditState *e, *e1;
    
    for(e = qs->first_window; e != NULL; e = e1) {
        e1 = e->next_window;
        if (!e->minibuf && e != s)
            edit_close(e);
    }
    /* resize to whole screen */
    s->y1 = 0;
    s->x1 = 0;
    s->x2 = qs->width;
    s->y2 = qs->height - qs->status_height;
    s->flags &= ~WF_RSEPARATOR;
    compute_client_area(s);
    do_refresh(s);
}

/* XXX: add minimum size test and refuse to split if reached */
void do_split_window(EditState *s, int horiz)
{
    EditState *e;
    int x, y;

    /* cannot split minibuf */
    if (s->minibuf)
        return;
            
    if (horiz) {
        x = (s->x2 + s->x1) / 2;
        e = edit_new(s->b, x, s->y1,
                     s->x2 - x, s->y2 - s->y1, WF_MODELINE);
        s->x2 = x;
        s->flags |= WF_RSEPARATOR;
    } else {
        y = (s->y2 + s->y1) / 2;
        e = edit_new(s->b, s->x1, y,
                     s->x2 - s->x1, s->y2 - y, 
                     WF_MODELINE | (s->flags & WF_RSEPARATOR));
        s->y2 = y;
    }
    compute_client_area(s);
    do_refresh(s);
}

/* help */

static void print_bindings(EditBuffer *b, const char *title,
                           int type, ModeDef *mode)
{
    CmdDef *d;
    KeyDef *k;
    char buf[64];
    int found, gfound;

    d = first_cmd;
    gfound = 0;
    while (d != NULL) {
        while (d->name != NULL) {
            /* find each key mapping pointing to this command */
            found = 0;
            for(k = first_key; k != NULL; k = k->next) {
                if (k->cmd == d && k->mode == mode) {
                    if (!gfound)
                        eb_printf(b, "%s:\n\n", title);
                    if (found)
                        eb_printf(b, ",");
                    eb_printf(b, " %s", keys_to_str(buf, sizeof(buf), k->keys, k->nb_keys));
                    found = 1;
                    gfound = 1;
                }
            }
            if (found) {
                /* print associated command name */
                eb_line_pad(b, 25);
                eb_printf(b, ": %s\n", d->name);
            }
            d++;
        }
        d = d->action.next;
    }
}

EditBuffer *new_help_buffer(int *show_ptr)
{
    EditBuffer *b;

    *show_ptr = 0;
    b = eb_find("*Help*");
    if (b) {
        eb_delete(b, 0, b->total_size);
    } else {
        b = eb_new("*Help*", BF_SYSTEM);
        *show_ptr = 1;
    } 
    return b;
}

void do_describe_bindings(EditState *s)
{
    EditBuffer *b;
    char buf[64];
    int show;

    b = new_help_buffer(&show);
    if (!b)
        return;
    snprintf(buf, sizeof(buf), "%s mode bindings", s->mode->name);
    print_bindings(b, buf, 0, s->mode);

    print_bindings(b, "\nGlobal bindings", 0, NULL);
    
    b->flags |= BF_READONLY;
    if (show) {
        show_popup(b);
        eb_free(b);
    }
}

void do_describe_key_briefly(EditState *s)
{
    put_status(s, "Describe key briefly:");
    key_ctx.describe_key = 1;
}

void do_help_for_help(EditState *s)
{
    EditBuffer *b;
    int show;

    b = new_help_buffer(&show);
    if (!b)
        return;
    eb_printf(b, 
              "QEmacs help for help - Press q to quit:\n"
              "\n"
              "C-h C-h   Show this help\n"
              "C-h b     Display table of all key bindings\n"
              "C-h c     Describe key briefly\n"
              );
    b->flags |= BF_READONLY;
    if (show) {
        show_popup(b);
    }
}

#ifdef WIN32

void qe_event_init(void)
{
}

#else

/* we install a signal handler to set poll_flag to one so that we can
   avoid polling too often in some cases */

int __fast_test_event_poll_flag = 0;

static void poll_action(int sig)
{
    __fast_test_event_poll_flag = 1;
}

/* init event system */
void qe_event_init(void)
{
    struct sigaction sigact;
    struct itimerval itimer;

    sigact.sa_flags = SA_RESTART;
    sigact.sa_handler = poll_action;
    sigemptyset(&sigact.sa_mask);
    sigaction(SIGVTALRM, &sigact, NULL);

    itimer.it_interval.tv_sec = 0;
    itimer.it_interval.tv_usec = 20 * 1000; /* 50 times per second */
    itimer.it_value = itimer.it_interval;
    setitimer(ITIMER_VIRTUAL, &itimer, NULL);
}

/* see also qe_fast_test_event() */
int __is_user_input_pending(void)
{
    QEditScreen *s = &global_screen;
    return s->dpy.dpy_is_user_input_pending(s);
}

#endif

#ifndef CONFIG_TINY

void window_get_min_size(EditState *s, int *w_ptr, int *h_ptr)
{
    QEmacsState *qs = &qe_state;
    int w, h;

    /* XXX: currently, fixed height */
    w = 8;
    h = 8;
    if (s->flags & WF_MODELINE)
        h += qs->mode_line_height;
    *w_ptr = w;
    *h_ptr = h;
}

/* resize a window on bottom right edge */
void window_resize(EditState *s, int target_w, int target_h)
{
    QEmacsState *qs = &qe_state;
    EditState *e;
    int delta_y, delta_x, min_w, min_h, new_h, new_w;

    delta_x = target_w - (s->x2 - s->x1);
    delta_y = target_h - (s->y2 - s->y1);

    /* then see if we can resize without having too small windows */
    window_get_min_size(s, &min_w, &min_h);
    if (target_w < min_w ||
        target_h < min_h)
        return;
    /* check if moving would uncover empty regions */
    if ((s->x2 >= qs->screen->width && delta_x != 0) ||
        (s->y2 >= qs->screen->height - qs->status_height && delta_y != 0))
        return;

    for(e = qs->first_window; e != NULL; e = e->next_window) {
        if (e->minibuf || e == s)
            continue;
        window_get_min_size(e, &min_w, &min_h);
        if (e->y1 == s->y2) {
            new_h = e->y2 - e->y1 - delta_y;
            goto test_h;
        } else if (e->y2 == s->y2) {
            new_h = e->y2 - e->y1 + delta_y;
        test_h:
            if (new_h < min_h)
                return;
        }
        if (e->x1 == s->x2) {
            new_w = e->x2 - e->x1 - delta_x;
            goto test_w;
        } else if (e->x2 == s->x2) {
            new_w = e->x2 - e->x1 + delta_x;
        test_w:
            if (new_w < min_w)
                return;
        }
    }

    /* now everything is OK, we can resize all windows */
    for(e = qs->first_window; e != NULL; e = e->next_window) {
        if (e->minibuf || e == s)
            continue;
        if (e->y1 == s->y2)
            e->y1 += delta_y;
        else if (e->y2 == s->y2)
            e->y2 += delta_y;
        if (e->x1 == s->x2)
            e->x1 += delta_x;
        else if (e->x2 == s->x2)
            e->x2 += delta_x;
        compute_client_area(e);
    }
    s->x2 = s->x1 + target_w;
    s->y2 = s->y1 + target_h;
    compute_client_area(s);
}

/* mouse handling */

#define MOTION_NONE       0
#define MOTION_MODELINE   1
#define MOTION_RSEPARATOR 2
#define MOTION_TEXT       3

static int motion_type = MOTION_NONE;
static EditState *motion_target;
static int motion_x, motion_y;

int check_motion_target(EditState *s)
{
    QEmacsState *qs = &qe_state;
    EditState *e;
    /* first verify that window is always valid */
    for(e = qs->first_window; e != NULL; e = e->next_window) {
        if (e == motion_target)
            break;
    }
    return e != NULL;
}

/* remove temporary selection colorization and selection area */
static void save_selection(void)
{
    QEmacsState *qs = &qe_state;
    EditState *e;
    int selection_showed;

    selection_showed = 0;
    for(e = qs->first_window; e != NULL; e = e->next_window) {
        selection_showed |= e->show_selection;
        e->show_selection = 0;
    }
    if (selection_showed && motion_type == MOTION_TEXT) {
        motion_type = MOTION_NONE;
        e = motion_target;
        if (!check_motion_target(e))
            return;
        do_kill_region(e, 0);
    }
}

/* XXX: need a more general scheme for other modes such as HTML/image */
void wheel_scroll_up_down(EditState *s, int dir)
{
    int line_height;

    /* only apply to text modes */
    if (!s->mode->text_display)
        return;

    line_height = get_line_height(s->screen, s->default_style);
    perform_scroll_up_down(s, dir * WHEEL_SCROLL_STEP * line_height);
}

void mouse_event(QEEvent *ev)
{
    QEmacsState *qs = &qe_state;
    EditState *e;
    int mouse_x, mouse_y;
    mouse_x = ev->button_event.x;
    mouse_y = ev->button_event.y;

    switch(ev->type) {
    case QE_BUTTON_RELEASE_EVENT:
        save_selection();
        motion_type = MOTION_NONE;
        break;

    case QE_BUTTON_PRESS_EVENT:
        for(e = qs->first_window; e != NULL; e = e->next_window) {
            /* test if mouse is inside the text area */
            if (mouse_x >= e->xleft && mouse_x < e->xleft + e->width &&
                mouse_y >= e->ytop && mouse_y < e->ytop + e->height) {
                if (e->mode->mouse_goto) {
                    save_selection();
                    e->mode->mouse_goto(e, mouse_x - e->xleft,
                                        mouse_y - e->ytop);
                    switch(ev->button_event.button) {
                    case QE_BUTTON_LEFT:
                        motion_type = MOTION_TEXT;
                        motion_x = 0; /* indicate first move */
                        motion_target = e;
                        break;
                    case QE_BUTTON_MIDDLE:
                        do_yank(e);
                        break;
                    case QE_WHEEL_UP:
                        wheel_scroll_up_down(e, -1);
                        break;
                    case QE_WHEEL_DOWN:
                        wheel_scroll_up_down(e, 1);
                        break;
                    }
                    edit_display(qs);
                    dpy_flush(qs->screen);
                }
                break;
            }
            /* test if inside modeline */
            if ((e->flags & WF_MODELINE) &&
                mouse_x >= e->xleft && mouse_x < e->xleft + e->width &&
                mouse_y >= e->ytop + e->height && 
                mouse_y < e->ytop + e->height + qs->mode_line_height) {
                /* mark that motion can occur */
                motion_type = MOTION_MODELINE;
                motion_target = e;
                motion_y = e->ytop + e->height;
                break;
            }
            /* test if inside right window separator */
            if ((e->flags & WF_RSEPARATOR) &&
                mouse_x >= e->x2 - qs->separator_width && mouse_x < e->x2 &&
                mouse_y >= e->ytop && mouse_y < e->ytop + e->height) {
                /* mark that motion can occur */
                motion_type = MOTION_RSEPARATOR;
                motion_target = e;
                motion_x = e->x2 - qs->separator_width;
                break;
            }
        }
        break;
    case QE_MOTION_EVENT:
        switch(motion_type) {
        case MOTION_NONE:
        default:
            break;
        case MOTION_TEXT:
            {
                EditState *e = motion_target;
                if (!check_motion_target(e)) {
                    e->show_selection = 0;
                    motion_type = MOTION_NONE;
                } else {
                    /* put a mark if first move */
                    if (!motion_x) {
                        /* test needed for list mode */
                        if (e->b)
                            e->b->mark = e->offset;
                        motion_x = 1;
                    }
                    /* highlight selection */
                    e->show_selection = 1;
                    if (mouse_x >= e->xleft && mouse_x < e->xleft + e->width &&
                        mouse_y >= e->ytop && mouse_y < e->ytop + e->height) {
                            /* if inside the buffer, then update cursor 
                            position */
                            e->mode->mouse_goto(e, mouse_x - e->xleft,
                                                mouse_y - e->ytop);
                            edit_display(qs);
                            dpy_flush(qs->screen);
                     }
                }
            }
            break;
        case MOTION_MODELINE:
            if ((mouse_y / 8) != (motion_y / 8)) {
                if (!check_motion_target(motion_target)) {
                    motion_type = MOTION_NONE;
                } else {
                    motion_y = mouse_y;
                    window_resize(motion_target, 
                                  motion_target->x2 - motion_target->x1,
                                  motion_y - motion_target->y1);
                    do_refresh(qs->first_window);
                    edit_display(qs);
                    dpy_flush(qs->screen);
                }
            }
            break;
        case MOTION_RSEPARATOR:
            if ((mouse_x / 8) != (motion_x / 8)) {
                if (!check_motion_target(motion_target)) {
                    motion_type = MOTION_NONE;
                } else {
                    motion_x = mouse_x;
                    window_resize(motion_target, 
                                  motion_x - motion_target->x1,
                                  motion_target->y2 - motion_target->y1);
                    do_refresh(qs->first_window);
                    edit_display(qs);
                    dpy_flush(qs->screen);
                }
            }
            break;
        }
        break;
    default:
        break;
    }
}
#endif

/* put key in the unget buffer so that get_key() will return it */
void unget_key(int key)
{
    QEmacsState *qs = &qe_state;
    qs->ungot_key = key;
}


/* handle an event sent by the GUI */
void qe_handle_event(QEEvent *ev)
{
    QEmacsState *qs = &qe_state;

    switch(ev->type) {
    case QE_KEY_EVENT:
        qe_key_process(ev->key_event.key);
        break;
    case QE_EXPOSE_EVENT:
        do_refresh(qs->first_window);
        goto redraw;
    case QE_UPDATE_EVENT:
    redraw:
        edit_display(qs);
        dpy_flush(qs->screen);
        break;
#ifndef CONFIG_TINY
    case QE_BUTTON_PRESS_EVENT:
    case QE_BUTTON_RELEASE_EVENT:
    case QE_MOTION_EVENT:
        mouse_event(ev);
        break;
    case QE_SELECTION_CLEAR_EVENT:
        save_selection();
        goto redraw;
#endif
    default:
        break;
    }
}

static int text_mode_probe(ModeProbeData *p)
{
    return 10;
}

int text_mode_init(EditState *s, ModeSavedData *saved_data)
{
    set_colorize_func(s, NULL);
    eb_add_callback(s->b, eb_offset_callback, &s->offset);
    eb_add_callback(s->b, eb_offset_callback, &s->offset_top);
    if (!saved_data) {
        memset(s, 0, SAVED_DATA_SIZE);
        s->insert = 1;
        s->tab_size = 8;
        s->indent_size = 4;
        s->default_style = QE_STYLE_DEFAULT;
        s->wrap = WRAP_LINE;
    } else {
        memcpy(s, saved_data->generic_data, SAVED_DATA_SIZE);
    }
    s->hex_mode = 0;
    return 0;
}

/* generic save mode data (saves text presentation information) */
ModeSavedData *generic_mode_save_data(EditState *s)
{
    ModeSavedData *saved_data;
    saved_data = malloc(sizeof(ModeSavedData));
    if (!saved_data)
        return NULL;
    saved_data->mode = s->mode;
    memcpy(saved_data->generic_data, s, SAVED_DATA_SIZE);
    return saved_data;
}

void text_mode_close(EditState *s)
{
    /* free all callbacks or associated buffer data */
    set_colorize_func(s, NULL);
    eb_free_callback(s->b, eb_offset_callback, &s->offset);
    eb_free_callback(s->b, eb_offset_callback, &s->offset_top);
}

ModeDef text_mode = {
    "text", 
    instance_size: 0,
    mode_probe: text_mode_probe,
    mode_init: text_mode_init,
    mode_close: text_mode_close,

    text_display: text_display,
    text_backward_offset: text_backward_offset, 

    move_up_down: text_move_up_down,
    move_left_right: text_move_left_right_visual,
    move_bol: text_move_bol,
    move_eol: text_move_eol,
    move_word_left_right: text_move_word_left_right,
    scroll_up_down: text_scroll_up_down,
    write_char: text_write_char,
    mouse_goto: text_mouse_goto,
};

ModeDef minibuffer_mode;

#include "qeconfig.h"

/* find a resource file */
int find_resource_file(char *path, int path_size, const char *pattern)
{
    FindFileState *ffs;
    int ret;

    ffs = find_file_open(qe_state.res_path, pattern);
    if (!ffs)
        return -1;
    ret = find_file_next(ffs, path, path_size);

    find_file_close(ffs);

    return ret;
}

/******************************************************/
/* config file parsing */

int parse_config_file(EditState *s, const char *filename)
{
    FILE *f;
    char line[1024], str[1024];
    char cmd[128], *q, *str1;
    const char *p, *r;
    int err, line_num;
    CmdDef *d;
    int nb_args, c, i, first;
    void *args[MAX_CMD_ARGS];
    unsigned char args_type[MAX_CMD_ARGS];

    f = fopen(filename, "r");
    if (!f)
        return -1;
    err = 0;
    line_num = 0;
    for(;;) {
        if (fgets(line, sizeof(line), f) == NULL)
            break;
        line_num++;
        p = line;
        skip_spaces(&p);
        if (p[0] == '\0')
            continue;
        /* skip comments */
        if (p[0] == '/' && p[1] == '/')
            continue;
        if (p[0] == '#')
            continue;

        get_str(&p, cmd, sizeof(cmd), "(");
        /* transform '_' to '-' */
        q = cmd;
        while (*q) {
            if (*q == '_')
                *q = '-';
            q++;
        }
        /* search for command */
        d = qe_find_cmd(cmd);
        if (!d) {
            err = -1;
            fprintf(stderr, "%s:%d: unknown command '%s'\n", 
                    filename, line_num, cmd);
            continue;
        }
        skip_spaces(&p);

        nb_args = 0;
        c = '(';
        if (*p != c)
            goto expected;
        p++;

        /* first argument is always the window */
        args_type[nb_args++] = CMD_ARG_WINDOW;

        /* construct argument type list */
        r = d->name + strlen(d->name) + 1;
        for(;;) {
            unsigned char arg_type;
            int ret;

            ret = parse_arg(&r, &arg_type, NULL, 0, NULL, 0, NULL, 0);
            if (ret < 0)
                goto badcmd;
            if (ret == 0)
                break;
            if (nb_args >= MAX_CMD_ARGS) {
            badcmd:
                fprintf(stderr, "%s:%d: badly defined command '%s'\n", 
                        filename, line_num, cmd);
                goto fail;
            }
            args_type[nb_args++] = arg_type;
        }

        /* fill args to avoid problems if error */
        for(i=0;i<nb_args;i++)
            args[i] = NULL;

        if (*p == '(')
            p++;
        first = 1;
        for(i=0;i<nb_args;i++) {
            /* pseudo arguments: skip them */
            switch(args_type[i]) {
            case CMD_ARG_WINDOW:
                args[i] = (void *)s;
                continue;
            case CMD_ARG_INTVAL:
            case CMD_ARG_STRINGVAL:
                args[i] = (void *)d->val;
                continue;
            }
            
            skip_spaces(&p);
            if (!first) {
                c = ',';
                if (*p != c) 
                    goto expected;
                p++;
                skip_spaces(&p);
            }
            first = 0;
            
            switch(args_type[i]) {
            case CMD_ARG_INT:
                args[i] = (void *)strtol(p, (char**)&p, 0);
                break;
            case CMD_ARG_STRING:
                if (*p != '\"') {
                    fprintf(stderr, "%s:%d: string expected\n", 
                            filename, line_num);
                    goto fail;
                }
                p++;
                q = str;
                while (*p != '\"' && *p != '\0') {
                    c = *p++;
                    if (c == '\\') {
                        c = *p++;
                        switch(c) {
                        case 'n':
                            c = '\n';
                            break;
                        case 'r':
                            c = '\r';
                            break;
                        }
                    }
                    if ((q - str) < sizeof(str) - 1)
                        *q++ = c;
                }
                *q = '\0';
                if (*p == '\"')
                    p++;
                str1 = strdup(str);
                if (!str1)
                    goto fail;
                args[i] = (void *)str1;
                break;
            }
        }
        skip_spaces(&p);
        c = ')';
        if (*p != c) {
        expected:
            fprintf(stderr, "%s:%d: '%c' expected\n", 
                    filename, line_num, c);
            goto fail;
        }

        /* now we can execute the command */
        call_func(d->action.func, nb_args, args, args_type);

    fail:
        /* free the arguments */
        for(i=0;i<nb_args;i++) {
            if (args_type[i] == CMD_ARG_STRING) {
                free(args[i]);
            }
        }
    }
    fclose(f);
    return 0;
}

void parse_config(EditState *e)
{
    FindFileState *ffs;
    char filename[1024];

    ffs = find_file_open(qe_state.res_path, "config");
    if (!ffs)
        return;
    for(;;) {
        if (find_file_next(ffs, filename, sizeof(filename)) != 0)
            break;
        parse_config_file(e, filename);
    }
    find_file_close(ffs);
}

/******************************************************/
/* command line option handling */
static CmdOptionDef *first_cmd_options;

void qe_register_cmd_line_options(CmdOptionDef *table)
{
    CmdOptionDef **pp, *p;
    pp = &first_cmd_options;
    while (*pp != NULL) {
        p = *pp;
        while (p->name != NULL)
            p++;
        pp = &p->u.next;
    }
    *pp = table;
}

/******************************************************/

static void show_help(void)
{
    CmdOptionDef *p;
    printf("QEmacs version " QE_VERSION ", Copyright (c) 2000-2003 Fabrice Bellard\n");

    /* generate usage */
    printf("usage: qe");
    p = first_cmd_options;
    while (p != NULL) {
        while (p->name != NULL) {
            printf(" [-%s", p->name);
            if (p->flags & CMD_OPT_ARG)
                printf(" %s", p->argname);
            printf("]");
            p++;
        }
        p = p->u.next;
    }
    printf(" [filename...]\n\n");

    /* generate help */
    p = first_cmd_options;
    while (p != NULL) {
        while (p->name != NULL) {
            printf("-%s", p->name);
            if (p->flags & CMD_OPT_ARG)
                printf(" %s", p->argname);
            printf(": %s\n", p->help);
            p++;
        }
        p = p->u.next;
    }
    exit(1);
}

static CmdOptionDef cmd_options[] = {
    { "h", NULL, 0, "show help", 
      {func_noarg: show_help}},
    { NULL },
};

#if defined(__GNUC__) || defined(__TINYC__)
static inline void init_all_modules(void)
{
    int (*initcall)(void);
    void **ptr;
    
    ptr = (void **)&__initcall_first;
    for(;;) {
        /* NOTE: if bound checking is on, a '\0' is inserted between
           each initialized 'void *' */
#if defined(__BOUNDS_CHECKING_ON)
        ptr = (void **)((long)ptr + (2 * sizeof(void *)));
#else
        ptr++;
#endif
        initcall = *ptr;
        if (initcall == NULL)
            break;
        initcall();
    }
}
#else
/* cannot use elf sections, so we initialize the modules manually */
static inline void init_all_modules(void)
{
    x11_init();
    c_init();
}
#endif

#ifdef CONFIG_DLL

void load_all_modules(void)
{
    FindFileState *ffs;
    char filename[1024];
    void *h;
    int (*init_func)(void);
    
    ffs = find_file_open(qe_state.res_path, "*.so");
    if (!ffs)
        return;
    while (!find_file_next(ffs, filename, sizeof(filename))) {
        h = dlopen(filename, RTLD_LAZY);
        if (!h) {
			char *error = dlerror();
            fprintf(stderr, "Could not open module '%s': %s\n",
					filename, error);
            continue;
        }
        init_func = dlsym(h, "__qe_module_init");
        if (!init_func) {
            dlclose(h);
            fprintf(stderr, "Could not find qemacs initializer in module '%s'\n", 
                    filename);
            continue;
        }
        
        /* all is OK: we can init the module now */
        init_func();
    }
    find_file_close(ffs);
}

#endif

typedef struct QEArgs {
    int argc;
    char **argv;
} QEArgs;

/* init function */
void qe_init(void *opaque)
{
    QEArgs *args = opaque;
    int argc = args->argc;
    char **argv = args->argv;
    EditState *s;
    EditBuffer *b;
    QEDisplay *dpy;
    int i, optind, is_player;
    char *home_path;

    /* compute resources path */
    strcpy(qe_state.res_path, 
           CONFIG_QE_PREFIX "/share/qe:" CONFIG_QE_PREFIX "/lib/qe:"
           "/usr/share/qe:/usr/lib/qe");
    home_path = getenv("HOME");
    if (home_path) {
        pstrcat(qe_state.res_path, sizeof(qe_state.res_path), ":");
        pstrcat(qe_state.res_path, sizeof(qe_state.res_path), home_path);
        pstrcat(qe_state.res_path, sizeof(qe_state.res_path), "/.qe");
    }
    qe_state.macro_key_index = -1; /* no macro executing */
    qe_state.ungot_key = -1; /* no unget key */
    
    eb_init();
    charset_init();
    init_input_methods();
    load_ligatures();

    /* init basic modules */
    qe_register_mode(&text_mode);
    qe_register_cmd_table(basic_commands, NULL);

    register_completion("command", command_completion);
    register_completion("charset", charset_completion);
    register_completion("style", style_completion);
    register_completion("file", file_completion);
    register_completion("buffer", buffer_completion);
    
    /* minibuf mode inherits from text mode */
    memcpy(&minibuffer_mode, &text_mode, sizeof(ModeDef));
    minibuffer_mode.name = "minibuffer";
    minibuffer_mode.scroll_up_down = minibuf_complete_scroll_up_down;
    qe_register_mode(&minibuffer_mode);
    qe_register_cmd_table(minibuffer_commands, "minibuffer");
    
    /* less mode inherits from text mode */
    memcpy(&less_mode, &text_mode, sizeof(ModeDef));
    less_mode.name = "less";
    qe_register_mode(&less_mode);
    qe_register_cmd_table(less_commands, "less");
    
    qe_register_cmd_line_options(cmd_options);

    /* init all external modules in link order */
    init_all_modules();

#ifdef CONFIG_DLL
    /* load all dynamic modules */
    load_all_modules();
#endif

    /* see if invoked as player */
    {
        const char *p;
        p = argv[0];
        p = strrchr(p, '/');
        if (!p)
            p = argv[0];
        else
            p++;
        if (!strcmp(p, "ffplay"))
            is_player = 1;
        else
            is_player = 0;
    }

    /* handle options */
    optind = 1;
    for(;;) {
        const char *r, *optarg;
        CmdOptionDef *p;

        if (optind >= argc)
            break;
        r = argv[optind];
        if (r[0] != '-') 
            break;
        optind++;

        p = first_cmd_options;
        while (p != NULL) {
            while (p->name != NULL) {
                if (!strcmp(p->name, r + 1)) {
                    if (p->flags & CMD_OPT_ARG) {
                        if (optind >= argc) {
                            fprintf(stderr, "Argument expected -- %s\n", r);
                            goto next_cmd;
                        }
                        optarg = argv[optind++];
                    } else {
                        optarg = NULL;
                    }
                    if (p->flags & CMD_OPT_BOOL) {
                        *p->u.int_ptr = 1;
                    } else if (p->flags & CMD_OPT_STRING) {
                        *p->u.string_ptr = optarg;
                    } else if (p->flags & CMD_OPT_INT) {
                        *p->u.int_ptr = atoi(optarg);
                    } else if (p->flags & CMD_OPT_ARG) {
                        p->u.func_arg(optarg);
                    } else {
                        p->u.func_noarg();
                    }
                    goto next_cmd;
                }
                p++;
            }
            p = p->u.next;
        }
        fprintf(stderr, "Unknown option -- %s\n", r);
    next_cmd: ;
    }

    /* init of the editor state */
    qe_state.screen = &global_screen;

    /* create first buffer */
    b = eb_new("*scratch*", BF_SAVELOG);

    /* will be positionned by do_refresh() */
    s = edit_new(b, 0, 0, 0, 0, WF_MODELINE);
    
    /* at this stage, no screen is defined. Maybe should implement a
       dummy display driver to have a consistent state */
    parse_config(s);

    qe_key_init();

    /* select the suitable display manager */
    dpy = probe_display();
    if (!dpy) {
        fprintf(stderr, "No suitable display found, exiting\n");
        exit(1);
    }

    if (dpy->dpy_init(&global_screen, screen_width, screen_height) < 0) {
        fprintf(stderr, "Could not initialize display '%s', exiting\n", 
                dpy->name);
        exit(1);
    }

    qe_event_init();

    put_status(s, "QEmacs %s - Press F1 for help", QE_VERSION);

    do_refresh(s);

    /* load file(s) */
    for(i=optind;i<argc;i++) {
        do_load(s, argv[i]);
    }
    
    if (is_player && optind >= argc) {
        /* if player, go to directory mode by default if no file selected */
        do_dired(s);
    }

    edit_display(&qe_state);
    dpy_flush(&global_screen);
}


#ifdef CONFIG_WIN32
int main1(int argc, char **argv)
#else
int main(int argc, char **argv)
#endif
{
    QEArgs args;

    args.argc = argc;
    args.argv = argv;

    url_main_loop(qe_init, &args);

    close_input_methods();

    dpy_close(&global_screen);
    return 0;
}
