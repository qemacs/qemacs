/*
 * Module for handling variables in QEmacs
 *
 * Copyright (c) 2000-2008 Charlie Gordon.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "qe.h"
#include "variables.h"

static VarDef var_table[] = {

    S_VAR( "screen-width", width, VAR_NUMBER, VAR_RO )
    S_VAR( "screen-height", height, VAR_NUMBER, VAR_RO )
    S_VAR( "is-full-screen", is_full_screen, VAR_NUMBER, VAR_RO )
    S_VAR( "flag-split-window-change-focus", flag_split_window_change_focus, VAR_NUMBER, VAR_RW )
    S_VAR( "backspace-is-control-h", backspace_is_control_h, VAR_NUMBER, VAR_RW )
    S_VAR( "ungot-key", ungot_key, VAR_NUMBER, VAR_RW )
    S_VAR( "QEPATH", res_path, VAR_CHARS, VAR_RO )
    //S_VAR( "it", it, VAR_NUMBER, VAR_RW )
    S_VAR( "ignore-spaces", ignore_spaces, VAR_NUMBER, VAR_RW )
    S_VAR( "hilite-region", hilite_region, VAR_NUMBER, VAR_RW )
    S_VAR( "mmap-threshold", mmap_threshold, VAR_NUMBER, VAR_RW )
    S_VAR( "show-unicode", show_unicode, VAR_NUMBER, VAR_RW )


    //B_VAR( "screen-charset", charset, VAR_NUMBER, VAR_RW )

    B_VAR( "mark", mark, VAR_NUMBER, VAR_RW )
    B_VAR( "bufsize", total_size, VAR_NUMBER, VAR_RO )
    B_VAR( "bufname", name, VAR_CHARS, VAR_RO )
    B_VAR( "filename", filename, VAR_CHARS, VAR_RO )

    W_VAR( "point", offset, VAR_NUMBER, VAR_RW )
    W_VAR( "tab-size", tab_size, VAR_NUMBER, VAR_RW )
    W_VAR( "indent-size", indent_size, VAR_NUMBER, VAR_RW )
    W_VAR( "indent-tabs-mode", indent_tabs_mode, VAR_NUMBER, VAR_RW )
    W_VAR( "default-style", default_style, VAR_NUMBER, VAR_RW )
    W_VAR( "region-style", region_style, VAR_NUMBER, VAR_RW )
    W_VAR( "curline-style", curline_style, VAR_NUMBER, VAR_RW )
    W_VAR( "window-width", width, VAR_NUMBER, VAR_RW )
    W_VAR( "window_height", height, VAR_NUMBER, VAR_RW )
    W_VAR( "window-left", xleft, VAR_NUMBER, VAR_RW )
    W_VAR( "window-top", ytop, VAR_NUMBER, VAR_RW )
    W_VAR( "window-prompt", prompt, VAR_STRING, VAR_RW )

    M_VAR( "mode-name", name, VAR_CHARS, VAR_RO )

    /* more buffer fields: modified, readonly, binary, charset */
    /* more window fields: mode_line, disp_width, color, input_method...
     */

    //G_VAR( "text-mode-line", text_mode.mode_line, VAR_STRING, VAR_RW )
    //G_VAR( "ascii-mode-line", ascii_mode.mode_line, VAR_STRING, VAR_RW )
    //G_VAR( "hex-mode-line", hex_mode.mode_line, VAR_STRING, VAR_RW )
    //G_VAR( "unicode-mode-line", unihex_mode.mode_line, VAR_STRING, VAR_RW )

    //Dispatch these to the appropriate modules
    //G_VAR( "c-mode-extensions", c_mode_extensions, VAR_STRING, VAR_RW )
    //G_VAR( "c-mode-keywords", c_mode_keywords, VAR_STRING, VAR_RW )
    //G_VAR( "c-mode-types", c_mode_types, VAR_STRING, VAR_RW )
    //G_VAR( "html-mode-extensions", html_mode_extensions, VAR_STRING, VAR_RW )
    //G_VAR( "perl-mode-extensions", perl_mode_extensions, VAR_STRING, VAR_RW )
};

void qe_register_variables(VarDef *vars, int count)
{
    QEmacsState *qs = &qe_state;
    VarDef *vp;

    for (vp = vars; vp < vars + count - 1; vp++) {
        vp->next = vp + 1;
    }
    vp->next = qs->first_variable;
    qs->first_variable = vars;
}

VarDef *qe_find_variable(const char *name)
{
    QEmacsState *qs = &qe_state;
    VarDef *vp;

    for (vp = qs->first_variable; vp; vp = vp->next) {
        if (strequal(vp->name, name))
              return vp;
    }
    /* Should have a list of local variables for buffer/window/mode
     * instances
     */
    return NULL;
}

void qe_complete_variable(CompleteState *cp)
{
    QEmacsState *qs = cp->s->qe_state;
    const VarDef *vp;

    for (vp = qs->first_variable; vp; vp = vp->next) {
        complete_test(cp, vp->name);
    }
}

QVarType qe_get_variable(EditState *s, const char *name,
                         char *buf, int size, int *pnum)
{
    const VarDef *vp;
    int num = 0;
    const char *str = NULL;
    const void *ptr;

    vp = qe_find_variable(name);
    if (!vp) {
        /* Should enumerate user variables ? */

        /* Try environment */
        str = getenv(name);
        if (str) {
            pstrcpy(buf, size, str);
            return VAR_STRING;
        } else {
            if (size > 0)
                *buf = '\0';
            return VAR_UNKNOWN;
        }
    }
    switch (vp->domain) {
    case VAR_SELF:
        ptr = &vp->value;
        break;
    case VAR_GLOBAL:
        ptr = vp->value.ptr;
        break;
    case VAR_STATE:
        ptr = (const u8*)s->qe_state + vp->value.offset;
        break;
    case VAR_BUFFER:
        ptr = (const u8*)s->b + vp->value.offset;
        break;
    case VAR_WINDOW:
        ptr = (const u8*)s + vp->value.offset;
        break;
    case VAR_MODE:
        ptr = (const u8*)s->mode + vp->value.offset;
        break;
    default:
        if (size > 0)
            *buf = '\0';
        return VAR_UNKNOWN;
    }

    switch (vp->type) {
    case VAR_STRING:
        str = *(const char**)ptr;
        pstrcpy(buf, size, str);
        break;
    case VAR_CHARS:
        str = (const char*)ptr;
        pstrcpy(buf, size, str);
        break;
    case VAR_NUMBER:
        num = *(const int*)ptr;
        if (pnum)
            *pnum = num;
        else
            snprintf(buf, size, "%d", num);
        break;
    default:
        if (size > 0)
            *buf = '\0';
        return VAR_UNKNOWN;
    }
    return vp->type;
}

/* Ugly kludge to check for allocated data: pointers above end are
 * assumed to be allocated with qe_malloc
 */
extern u8 end[];

QVarType qe_set_variable(EditState *s, const char *name,
                         const char *value, int num)
{
    char buf[32];
    void *ptr;
    char **pstr;
    VarDef *vp;

    vp = qe_find_variable(name);
    if (!vp) {
        /* Create user variable (global/buffer/window/mode?) */
        vp = qe_mallocz(VarDef);
        vp->name = qe_strdup(name);
        vp->domain = VAR_SELF;
        vp->rw = VAR_RW;
        if (value) {
            vp->value.str = qe_strdup(value);
            vp->type = VAR_STRING;
        } else {
            vp->value.num = num;
            vp->type = VAR_NUMBER;
        }
        qe_register_variables(vp, 1);
        return vp->type;
    } else
    if (vp->rw == VAR_RO) {
        put_status(s, "Variable %s is read-only", name);
        return VAR_UNKNOWN;
    } else {
        switch (vp->domain) {
        case VAR_SELF:
            ptr = &vp->value;
            break;
        case VAR_GLOBAL:
            ptr = vp->value.ptr;
            break;
        case VAR_STATE:
            ptr = (u8*)s->qe_state + vp->value.offset;
            break;
        case VAR_BUFFER:
            ptr = (u8*)s->b + vp->value.offset;
            break;
        case VAR_WINDOW:
            ptr = (u8*)s + vp->value.offset;
            break;
        case VAR_MODE:
            ptr = (u8*)s->mode + vp->value.offset;
            break;
        default:
            return VAR_UNKNOWN;
        }

        switch (vp->type) {
        case VAR_STRING:
            if (!value) {
                sprintf(buf, "%d", num);
                value = buf;
            }
            pstr = (char **)ptr;
            if ((u8 *)*pstr > end)
                qe_free(*pstr);
            *pstr = qe_strdup(value);
            break;
        case VAR_CHARS:
            if (!value) {
                sprintf(buf, "%d", num);
                value = buf;
            }
            pstrcpy(ptr, vp->size, value);
            break;
        case VAR_NUMBER:
            if (!value)
                *(int*)ptr = num;
            else
                *(int*)ptr = strtol(value, NULL, 0);
            break;
        default:
            return VAR_UNKNOWN;
        }
        return vp->type;
    }
}

void do_show_variable(EditState *s, const char *name)
{
    char buf[MAX_FILENAME_SIZE];

    if (qe_get_variable(s, name, buf, sizeof(buf), NULL) == VAR_UNKNOWN)
        put_status(s, "No variable %s", name);
    else
        put_status(s, "%s -> %s", name, buf);
}

void do_set_variable(EditState *s, const char *name, const char *value)
{
    qe_set_variable(s, name, value, 0);
}

/* should register this as help function */
void qe_list_variables(EditState *s, EditBuffer *b)
{
    QEmacsState *qs = s->qe_state;
    char buf[MAX_FILENAME_SIZE];
    const VarDef *vp;

    eb_printf(b, "\n  variables:\n\n");
    for (vp = qs->first_variable; vp; vp = vp->next) {
        qe_get_variable(s, vp->name, buf, sizeof(buf), NULL);
        eb_printf(b, "    D%d T%d %s  %-14s  '%s'\n",
                  vp->domain, vp->type, vp->rw ? "rw" : "ro",
                  vp->name, buf);
    }
}

/*---------------- commands ----------------*/

static CmdDef var_commands[] = {
    CMD_( KEY_NONE, KEY_NONE,
          "show-variable", do_show_variable, ESs,
          "s{Show variable: }[var]|var|")
    CMD_( KEY_F8, KEY_NONE,
          "set-variable", do_set_variable, ESss,
          "s{Set variable: }[var]|var|s{to value: }|value|")
    CMD_DEF_END,
};

static int vars_init(void)
{
    qe_register_variables(var_table, countof(var_table));
    qe_register_cmd_table(var_commands, NULL);
    register_completion("var", qe_complete_variable);
    return 0;
}

qe_module_init(vars_init);
