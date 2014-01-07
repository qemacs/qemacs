/*
 * Org mode for QEmacs.
 *
 * Copyright (c) 2001-2002 Fabrice Bellard.
 * Copyright (c) 2002-2014 Charlie Gordon.
 * Copyright (c) 2014 Francois Revol.
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

ModeDef org_mode;

#define IN_BULLET       0x01
#define IN_ACTION       0x02
#define IN_TAG          0x04

#define MAX_BUF_SIZE    512


/* TODO: define specific styles */
static struct OrgTodoKeywords {
    const char *keyword;
    int style;
} OrgTodoKeywords [] = {
    { "TODO", QE_STYLE_STRING },
    { "DONE", QE_STYLE_TYPE },
};

/* TODO: define specific styles */
#define BULLET_STYLES 5
static int OrgBulletStyles[BULLET_STYLES] = {
    QE_STYLE_FUNCTION,
    QE_STYLE_VARIABLE,
    QE_STYLE_PREPROCESS,
    QE_STYLE_STRING,
    QE_STYLE_TYPE,
};

static int org_bullet_depth(unsigned int *str, int n)
{
    int i;

    for (i = 0; i < n && (str[i] & CHAR_MASK) == '*'; i++) {
        if ((str[i + 1] & CHAR_MASK) == ' ') {
            return i;
        }
    }
    return -1;
}

static int org_todo_keyword(unsigned int *str, int n)
{
    int i, c, klen;
    char kbuf[32];

    klen = 0;
    for (i = 0; i < n && qe_isalpha(c = (str[i] & CHAR_MASK)); i++) {
        if (klen < countof(kbuf) - 1)
            kbuf[klen++] = c;
        else
            break;
    }
    kbuf[klen] = '\0';
    if (klen > 0 && c == ' ') {
        int k;
        for (k = 0; k < countof(OrgTodoKeywords); k++) {
            if (!strcmp(kbuf, OrgTodoKeywords[k].keyword)) {
                set_color(str, str + i, OrgTodoKeywords[k].style);
                return k;
            }
        }
    }
    return -1;
}

static void org_colorize_line(unsigned int *str, int n, int *statep,
                              __unused__ int state_only)
{
    int colstate = *statep;
    int bullets = 0;
    int base_style = 0;
    int i = 0;
    int c;

    bullets = org_bullet_depth(str, n);
    if (bullets > -1) {
        colstate = IN_BULLET;
        base_style = OrgBulletStyles[bullets % BULLET_STYLES];
        set_color(str, str + bullets + 1, base_style);
        i = bullets + 2;
    }

    if (colstate & IN_BULLET) {
        int kw = org_todo_keyword(str + i, n - i);
        if (kw > -1) {
            int kwlen = strlen(OrgTodoKeywords[kw].keyword);
            set_color(str + i, str + i + kwlen, OrgTodoKeywords[kw].style);
            i += kwlen;
        }
    }

    while (i < n) {
        c = str[i] & CHAR_MASK;
        switch (c) {
        default:
            break;
        }
        set_color1(str + i, base_style);
        i++;
        continue;
    }

    colstate = 0;
    *statep = colstate;
}

static void do_org_todo(EditState *s)
{
    int offset, offsetl, line_num, col_num, bullets, len;
    unsigned int buf[MAX_BUF_SIZE];
    int kw;

    /* find start of line */
    eb_get_pos(s->b, &line_num, &col_num, s->offset);
    offset = eb_goto_bol(s->b, s->offset);
    do {
        offsetl = offset;
        len = eb_get_line(s->b, buf, countof(buf), &offsetl);
        bullets = org_bullet_depth(buf, len);
        if (bullets > -1) {
            break;
        }
        offsetl = offset;
        offset = eb_prev_line(s->b, offset);
    } while (offsetl > 0);

    if (bullets < 0)
        return;

    offset = eb_goto_bol(s->b, offset);
    kw = org_todo_keyword(buf + bullets + 2, len - bullets);

    if (kw > -1) {
        int kwlen = strlen(OrgTodoKeywords[kw].keyword);
        eb_delete(s->b, offset + bullets + 2, kwlen + 1);
    }

    kw++;

    if (kw < countof(OrgTodoKeywords)) {
        int kwlen = strlen(OrgTodoKeywords[kw].keyword);
        eb_insert(s->b, offset + bullets + 2, " ", 1);
        eb_insert(s->b, offset + bullets + 2, OrgTodoKeywords[kw].keyword, kwlen);
    }
}

static void do_org_meta_return(EditState *s)
{
    int offset, offsetl, line_num, col_num, bullets, len;
    unsigned int buf[MAX_BUF_SIZE];

    /* find start of line */
    eb_get_pos(s->b, &line_num, &col_num, s->offset);
    offset = eb_goto_bol(s->b, s->offset);
    offsetl = offset;
    len = eb_get_line(s->b, buf, countof(buf), &offsetl);
    bullets = org_bullet_depth(buf, len);
    if (bullets < 0)
        return;

    if (col_num > 0)
        offset = offsetl;

    eb_insert(s->b, offset, " \n", 2);
    do {
        eb_insert(s->b, offset, "*", 1);
    } while (bullets--);
    eb_goto_eol(s->b, offset);
    if (col_num > 0)
        text_move_up_down(s, 1);
    text_move_eol(s);
}

#if 0
static void do_org_insert_todo_heading(EditState *s)
{
    printf("M-S-RET\n");
    /* TODO */
}
#endif

static void do_org_promote(EditState *s, int dir)
{
    int offset, offsetl, line_num, col_num, len;
    int bullets;
    unsigned int buf[MAX_BUF_SIZE];

    /* find start of line */
    eb_get_pos(s->b, &line_num, &col_num, s->offset);
    offset = eb_goto_bol(s->b, s->offset);
    offsetl = offset;
    len = eb_get_line(s->b, buf, countof(buf), &offsetl);
    bullets = org_bullet_depth(buf, len);
    if (bullets < 0)
        return;

    if (dir > 0)
        eb_insert(s->b, offset, "*", 1);
    if (dir < 0 && bullets > 0)
        eb_delete(s->b, offset, 1);
}

static void do_org_promote_subtree(EditState *s, int dir)
{
    int offset, offsetl, offseti, line_num, col_num, len;
    int bullets, bullets1;
    unsigned int buf[MAX_BUF_SIZE];

    /* find start of line */
    eb_get_pos(s->b, &line_num, &col_num, s->offset);
    offset = eb_goto_bol(s->b, s->offset);
    offsetl = offset;
    len = eb_get_line(s->b, buf, countof(buf), &offsetl);
    bullets = org_bullet_depth(buf, len);
    if (bullets < 0)
        return;

    if (dir > 0)
        eb_insert(s->b, offset, "*", 1);
    else if (dir < 0 && bullets > 0)
        eb_delete(s->b, offset, 1);
    else
        return;

    bullets1 = bullets;
    for (;;) {
        offsetl = eb_next_line(s->b, offset);
        if (offsetl == offset)
            break;
        offset = offseti = offsetl;
        eb_get_pos(s->b, &line_num, &col_num, offseti);
        len = eb_get_line(s->b, buf, countof(buf), &offseti);
        bullets = org_bullet_depth(buf, len);
        if (bullets < 0)
            continue;
        if (bullets <= bullets1)
            break;

        if (dir > 0)
            eb_insert(s->b, offset, "*", 1);
        if (dir < 0 && bullets > 0)
            eb_delete(s->b, offset, 1);
    }
}

static void do_org_metaleft(EditState *s)
{
    do_org_promote(s, -1);
}

static void do_org_metaright(EditState *s)
{
    do_org_promote(s, 1);
}

static void do_org_shiftmetaleft(EditState *s)
{
    do_org_promote_subtree(s, -1);
}

static void do_org_shiftmetaright(EditState *s)
{
    do_org_promote_subtree(s, 1);
}

static int org_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    /* just check file extension */
    if (match_extension(p->filename, mode->extensions))
        return 80;

    return 0;
}

static int org_mode_init(EditState *s, __unused__ ModeSavedData *saved_data)
{
    int ret;

    ret = text_mode_init(s, saved_data);
    if (ret)
        return ret;

    s->wrap = WRAP_TRUNCATE;
    return 0;
}

/* Org mode specific commands */
static CmdDef org_commands[] = {
    CMD2( KEY_CTRLC(KEY_CTRL('t')), KEY_NONE,   /* C-c C-t */
    "org-todo", do_org_todo, ES, "*")
    CMD2( KEY_META(KEY_RET), KEY_NONE,
    "org-meta-return", do_org_meta_return, ES, "*")
    /*
    CMD2( KEY_META_S('\n'), KEY_NONE,
    "org-insert-todo-heading", do_org_insert_todo_heading, ES, "*")
*/
    /* actually M-left */
    CMD2( KEY_CTRL_LEFT, KEY_NONE,
    "org-meta-left", do_org_metaleft, ES, "*")
    /* actually M-right */
    CMD2( KEY_CTRL_RIGHT, KEY_NONE,
    "org-meta-right", do_org_metaright, ES, "*")
    /* actually M-S-left */
    CMD2( KEY_CTRLX('<'), KEY_NONE,
    "org-shift-meta-left", do_org_shiftmetaleft, ES, "*")
    /* actually M-S-right */
    CMD2( KEY_CTRLX('>'), KEY_NONE,
    "org-shift-meta-right", do_org_shiftmetaright, ES, "*")
    CMD_DEF_END,
};

static int org_init(void)
{
    memcpy(&org_mode, &text_mode, sizeof(ModeDef));
    org_mode.name = "org";
    org_mode.extensions = "org";
    org_mode.mode_probe = org_mode_probe;
    org_mode.mode_init = org_mode_init;
    org_mode.colorize_func = org_colorize_line;

    qe_register_mode(&org_mode);
    qe_register_cmd_table(org_commands, &org_mode);

    return 0;
}

qe_module_init(org_init);
