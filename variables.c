/*
 * Module for handling variables in QEmacs
 *
 * Copyright (c) 2000-2024 Charlie Gordon.
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
#include "variables.h"

static QVarType qe_variable_set_value_offset(EditState *s, VarDef *vp, void *ptr,
                                             const char *value, int num);
static QVarType qe_variable_set_value_generic(EditState *s, VarDef *vp, void *ptr,
                                              const char *value, int num);

const char * const var_domain[] = {
    "global",   /* VAR_GLOBAL */
    "state",    /* VAR_STATE */
    "buffer",   /* VAR_BUFFER */
    "window",   /* VAR_WINDOW */
    "mode",     /* VAR_MODE */
    "self",     /* VAR_SELF */
};

static int use_full_version = 1;

static VarDef var_table[] = {

    S_VAR( "screen-width", width, VAR_NUMBER, VAR_RO,
           "Number of columns available for display on screen." )
    S_VAR( "screen-height", height, VAR_NUMBER, VAR_RO,
           "Number of lines available for display on screen." )
    S_VAR( "is-full-screen", is_full_screen, VAR_NUMBER, VAR_RO,
           "Set if this window is displayed in full screen (without borders)." )
    S_VAR( "flag-split-window-change-focus", flag_split_window_change_focus, VAR_NUMBER, VAR_RW_SAVE,
           "Set if `split-window` should set focus to the new window." )
    // XXX: need set_value function to perform side effect
    S_VAR( "backspace-is-control-h", backspace_is_control_h, VAR_NUMBER, VAR_RW_SAVE,
           "Set if the Delete key sends a control-H." )
    S_VAR( "ungot-key", ungot_key, VAR_NUMBER, VAR_RW, NULL )   // XXX: need set_value function
    S_VAR( "QEPATH", res_path, VAR_CHARS, VAR_RO,
           "List of directories to search for standard files to load." )
    //S_VAR( "it", it, VAR_NUMBER, VAR_RW, NULL )
    S_VAR( "ignore-spaces", ignore_spaces, VAR_NUMBER, VAR_RW_SAVE,
           "Set to ignore spaces in compare-windows." )
    S_VAR( "ignore-comments", ignore_comments, VAR_NUMBER, VAR_RW_SAVE,
           "Set to ignore comments in compare-windows." )
    S_VAR( "ignore-case", ignore_case, VAR_NUMBER, VAR_RW_SAVE,
           "Set to ignore case in compare-windows." )
    S_VAR( "ignore-preproc", ignore_preproc, VAR_NUMBER, VAR_RW_SAVE,
           "Set to ignore preprocessing directives in compare-windows." )
    S_VAR( "ignore-equivalent", ignore_equivalent, VAR_NUMBER, VAR_RW_SAVE,
           "Set to ignore equivalent strings defined by define-equivalent." )
    S_VAR( "hilite-region", hilite_region, VAR_NUMBER, VAR_RW_SAVE,
           "Set to highlight the region after setting the mark." )
    S_VAR( "mmap-threshold", mmap_threshold, VAR_NUMBER, VAR_RW_SAVE,   // XXX: need set_value function
           "Size from which files are mmapped instead of loaded in memory." )
    S_VAR( "max-load-size", max_load_size, VAR_NUMBER, VAR_RW_SAVE,   // XXX: need set_value function
           "Maximum size for files to be loaded or mmapped into a buffer." )
    S_VAR( "show-unicode", show_unicode, VAR_NUMBER, VAR_RW_SAVE,   // XXX: need set_value function
           "Set to show non-ASCII characters as unicode escape sequences." )
    S_VAR( "default-tab-width", default_tab_width, VAR_NUMBER, VAR_RW_SAVE,   // XXX: need set_value function
           "Default value of `tab-width` for buffers that do not override it." )
    S_VAR( "default-fill-column", default_fill_column, VAR_NUMBER, VAR_RW_SAVE,   // XXX: need set_value function
           "Default value of `fill-column` for buffers that do not override it" )
    S_VAR( "backup-inhibited", backup_inhibited, VAR_NUMBER, VAR_RW_SAVE,
           "Set to prevent automatic backups of modified files" )
    S_VAR( "c-label-indent", c_label_indent, VAR_NUMBER, VAR_RW_SAVE,
           "Number of columns to adjust indentation of C labels." )
    S_VAR( "macro-counter", macro_counter, VAR_NUMBER, VAR_RW_SAVE,
           "Macro counter: insert with C-x C-k TAB, set with C-x C-k C-c." )

    //B_VAR( "screen-charset", charset, VAR_NUMBER, VAR_RW, NULL )

    B_VAR_F( "mark", mark, VAR_NUMBER, VAR_RW, qe_variable_set_value_offset,
           "The position of the beginning of the current region." )
    B_VAR( "bufsize", total_size, VAR_NUMBER, VAR_RO,
           "The number of bytes in the current buffer." )
    B_VAR( "bufname", name, VAR_CHARS, VAR_RO,
           "The name of the current buffer." )
    B_VAR( "filename", filename, VAR_CHARS, VAR_RO,
           "The name of the file associated with the current buffer." )
    B_VAR( "tab-width", tab_width, VAR_NUMBER, VAR_RW,   // XXX: need set_value function
           "Distance between tab stops (for display of tab characters), in columns." )
    B_VAR( "fill-column", fill_column, VAR_NUMBER, VAR_RW,   // XXX: need set_value function
           "Column beyond which automatic line-wrapping should happen." )

    W_VAR_F( "point", offset, VAR_NUMBER, VAR_RW, qe_variable_set_value_offset,    /* should be window-point */
           "Current value of point in this window." )
    W_VAR( "indent-width", indent_width, VAR_NUMBER, VAR_RW,   // XXX: need set_value function
           "Number of columns to indent by for a syntactic level." )
    W_VAR( "indent-tabs-mode", indent_tabs_mode, VAR_NUMBER, VAR_RW,
           "Set if indentation can insert tabs." )
    W_VAR( "default-style", default_style, VAR_NUMBER, VAR_RW,   // XXX: need set_value function
           "Default text style for this window." )
    W_VAR( "region-style", region_style, VAR_NUMBER, VAR_RW,   // XXX: need set_value function
           "Text style for the current region in this window." )
    W_VAR( "curline-style", curline_style, VAR_NUMBER, VAR_RW,   // XXX: need set_value function
           "Text style for the current line in this window." )
    W_VAR( "window-width", width, VAR_NUMBER, VAR_RW,   // XXX: need set_value function
           "Number of display columns in this window." )
    W_VAR( "window-height", height, VAR_NUMBER, VAR_RW,   // XXX: need set_value function
           "Number of display lines in this window." )
    W_VAR( "window-left", xleft, VAR_NUMBER, VAR_RW,   // XXX: need set_value function
           "Display column of the left edge of this window." )
    W_VAR( "window-top", ytop, VAR_NUMBER, VAR_RW,   // XXX: need set_value function
           "Display line of the top edge of this window." )
    W_VAR( "window-prompt", prompt, VAR_STRING, VAR_RW,
           "Prompt string to show for this window." )
    W_VAR( "dump-width", dump_width, VAR_NUMBER, VAR_RW, NULL )   // XXX: need set_value function

    M_VAR( "mode-name", name, VAR_STRING, VAR_RO,
           "Name of the current major mode." )
    M_VAR( "auto-indent", auto_indent, VAR_NUMBER, VAR_RW,
           "Set for automatic indentation on new lines." )
#ifdef CONFIG_SESSION
    G_VAR( "use-session-file", use_session_file, VAR_NUMBER, VAR_RW, NULL )
#endif
    G_VAR( "force-tty", force_tty, VAR_NUMBER, VAR_RW,
           "Set to prevent graphics display." )
    G_VAR( "disable-crc", disable_crc, VAR_NUMBER, VAR_RW_SAVE,
           "Set to prevent CRC based display cache." )
    G_VAR( "use-html", use_html, VAR_NUMBER, VAR_RW, NULL )
    G_VAR( "is-player", is_player, VAR_NUMBER, VAR_RW, NULL )
    G_VAR( "full-version", use_full_version, VAR_NUMBER, VAR_RW, NULL )

    /* more buffer fields: modified, readonly, binary, charset */

    /* more window fields: mode_line, color, input_method...
     */

    //G_VAR( "text-mode-line", text_mode.mode_line, VAR_STRING, VAR_RW, NULL )
    //G_VAR( "binary-mode-line", binary_mode.mode_line, VAR_STRING, VAR_RW, NULL )
    //G_VAR( "hex-mode-line", hex_mode.mode_line, VAR_STRING, VAR_RW, NULL )
    //G_VAR( "unicode-mode-line", unihex_mode.mode_line, VAR_STRING, VAR_RW, NULL )

    //Dispatch these to the appropriate modules
    //G_VAR( "c-mode-extensions", c_mode.extensions, VAR_STRING, VAR_RW, NULL )
    //G_VAR( "c-mode-keywords", c_mode.keywords, VAR_STRING, VAR_RW, NULL )
    //G_VAR( "c-mode-types", c_mode.types, VAR_STRING, VAR_RW, NULL )
    //G_VAR( "html-src-mode-extensions", htmlsrc_mode.extensions, VAR_STRING, VAR_RW, NULL )
    //G_VAR( "html-mode-extensions", html_mode.extensions, VAR_STRING, VAR_RW, NULL )
    //G_VAR( "perl-mode-extensions", perl_mode.extensions, VAR_STRING, VAR_RW, NULL )
};

static VarDef *qe_find_variable(QEmacsState *qs, const char *name)
{
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

void variable_complete(CompleteState *cp, CompleteFunc enumerate) {
    QEmacsState *qs = cp->s->qs;
    const VarDef *vp;

    for (vp = qs->first_variable; vp; vp = vp->next) {
        enumerate(cp, vp->name, CT_STRX);
    }
}

QVarType qe_get_variable(EditState *s, const char *name,
                         char *buf, int size, int *pnum, int as_source)
{
    const VarDef *vp;
    int num = 0;
    const char *str = NULL;
    const void *ptr;
    const char *endp;

    /* find standard variable and user variables */
    // XXX: should also have window, buffer, mode properties?
    vp = qe_find_variable(s->qs, name);
    if (!vp) {
        /* Try environment */
        str = getenv(name);
        if (!str) {
            if (size > 0)
                *buf = '\0';
            return VAR_UNKNOWN;
        }
        num = strtol_c(str, &endp, 0);
        if (pnum && endp != str && *endp == '\0') {
            *pnum = num;
            return VAR_NUMBER;
        }
        if (as_source)
            strquote(buf, size, str, -1);
        else
            pstrcpy(buf, size, str);
        return VAR_STRING;
    }
    switch (vp->domain) {
    case VAR_SELF:
        ptr = &vp->value;
        break;
    case VAR_GLOBAL:
        ptr = vp->value.ptr;
        break;
    case VAR_STATE:
        ptr = (const u8*)s->qs + vp->value.offset;
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
        memcpy(&str, ptr, sizeof(str));
        if (as_source)
            strquote(buf, size, str, -1);
        else
            pstrcpy(buf, size, str ? str : "");
        break;
    case VAR_CHARS:
        str = (const char*)ptr;
        if (as_source)
            strquote(buf, size, str, -1);
        else
            pstrcpy(buf, size, str);
        break;
    case VAR_NUMBER:
        memcpy(&num, ptr, sizeof(num));
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

static QVarType qe_variable_set_value_offset(EditState *s, VarDef *vp, void *ptr,
                                             const char *value, int num)
{
    if (value) {
        /* XXX: should have default, min and max values */
        return VAR_INVALID;
    } else {
        int *pnum = (int *)ptr;
        *pnum = clamp_offset(num, 0, s->b->total_size);
        return VAR_NUMBER;
    }
}

static QVarType qe_variable_set_value_generic(EditState *s, VarDef *vp, void *ptr,
                                              const char *value, int num)
{
    char buf[32];
    char **pstr;
    int *pnum;

    switch (vp->type) {
    case VAR_STRING:
        if (!value) {
            snprintf(buf, sizeof(buf), "%d", num);
            value = buf;
        }
        pstr = (char **)ptr;
        if (!strequal(*pstr, value)) {
            if (vp->str_alloc)
                qe_free(pstr);
            *pstr = qe_strdup(value);
            vp->str_alloc = 1;
            vp->modified = 1;
        }
        break;
    case VAR_CHARS:
        if (!value) {
            snprintf(buf, sizeof(buf), "%d", num);
            value = buf;
        }
        if (!strequal(ptr, value)) {
            pstrcpy(ptr, vp->size, value);
            vp->modified = 1;
        }
        break;
    case VAR_NUMBER:
        if (!value) {
            pnum = (int *)ptr;
            if (*pnum != num) {
                *pnum = num;
                vp->modified = 1;
            }
        } else {
            /* XXX: should have default, min and max values */
            return VAR_INVALID;
        }
        break;
    default:
        return VAR_UNKNOWN;
    }
    return vp->type;
}

QVarType qe_set_variable(EditState *s, const char *name,
                         const char *value, int num)
{
    QEmacsState *qs = s->qs;
    void *ptr;
    VarDef *vp;

    vp = qe_find_variable(qs, name);
    if (!vp) {
        /* Create user variable (global/buffer/window/mode?) */
        vp = qe_mallocz(VarDef);
        vp->name = qe_strdup(name);
        vp->var_alloc = 1;
        vp->modified = 1;
        vp->domain = VAR_SELF;
        vp->rw = VAR_RW_SAVE;
        if (value) {
            vp->str_alloc = 1;
            vp->value.str = qe_strdup(value);
            vp->type = VAR_STRING;
        } else {
            vp->value.num = num;
            vp->type = VAR_NUMBER;
        }
        qe_register_variables(qs, vp, 1);
        return vp->type;
    } else
    if (vp->rw == VAR_RO) {
        return VAR_READONLY;
    } else {
        switch (vp->domain) {
        case VAR_SELF:
            ptr = &vp->value;
            break;
        case VAR_GLOBAL:
            ptr = vp->value.ptr;
            break;
        case VAR_STATE:
            ptr = (u8*)qs + vp->value.offset;
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
        if (vp->type == VAR_NUMBER && value) {
            const char *p;
            num = strtol_c(value, &p, 0);
            if (!*p)
                value = NULL;
        }
        return vp->set_value(s, vp, ptr, value, num);
    }
}

void do_show_variable(EditState *s, const char *name)
{
    char buf[MAX_FILENAME_SIZE];

    if (qe_get_variable(s, name, buf, sizeof(buf), NULL, 1) == VAR_UNKNOWN)
        put_error(s, "No variable %s", name);
    else
        put_status(s, "%s -> %s", name, buf);
}

void do_set_variable(EditState *s, const char *name, const char *value)
{
    switch (qe_set_variable(s, name, value, 0)) {
    case VAR_UNKNOWN:
        put_error(s, "Variable %s is invalid", name);
        break;
    case VAR_READONLY:
        put_error(s, "Variable %s is read-only", name);
        break;
    case VAR_INVALID:
        put_error(s, "Invalid value for variable %s: %s", name, value);
        break;
    default:
        do_show_variable(s, name);
        break;
    }
}

static void do_describe_variable(EditState *s, const char *name) {
    EditBuffer *b;
    VarDef *vp;

    if ((vp = qe_find_variable(s->qs, name)) == NULL) {
        put_error(s, "No variable %s", name);
        return;
    }
    b = new_help_buffer(s);
    if (!b)
        return;

    eb_putc(b, '\n');
    /* print name, class, current value and description */
    eb_variable_print_entry(b, vp, s);
    eb_putc(b, '\n');
    if (vp->desc && *vp->desc) {
        /* print short description */
        eb_printf(b, "  %s\n", vp->desc);
    }
    // XXX: should look up markdown documentation
    show_popup(s, b, "Help");
}

void qe_register_variables(QEmacsState *qs, VarDef *vars, int count)
{
    VarDef *vp;

    for (vp = vars; vp < vars + count; vp++) {
        if (!vp->set_value)
            vp->set_value = qe_variable_set_value_generic;
        vp->next = vp + 1;
    }
    vp[-1].next = qs->first_variable;
    qs->first_variable = vars;
}

/* should register this as help function */
void qe_list_variables(EditState *s, EditBuffer *b)
{
    char buf[MAX_FILENAME_SIZE];
    char typebuf[32];
    const char *type;
    const VarDef *vp;

    eb_puts(b, "\n  variables:\n\n");
    for (vp = s->qs->first_variable; vp; vp = vp->next) {
        /* XXX: merge with variable_print_entry() */
        switch (vp->type) {
        case VAR_NUMBER:
            type = "int";
            break;
        case VAR_STRING:
            type = "string";
            break;
        case VAR_CHARS:
            type = typebuf;
            snprintf(typebuf, sizeof(typebuf), "char[%d]", vp->size);
            break;
        default:
            type = "var";
            break;
        }
        qe_get_variable(s, vp->name, buf, sizeof(buf), NULL, 1);
        eb_printf(b, "    %s %s %s%s -> %s\n",
                  var_domain[vp->domain], type,
                  vp->rw ? "" : "read-only ",
                  vp->name, buf);
    }
}

void qe_save_variables(EditState *s, EditBuffer *b)
{
    char buf[MAX_FILENAME_SIZE];
    char varname[32], *p;
    const VarDef *vp;

    eb_puts(b, "// variables:\n");
    /* Only save customized variables */
    for (vp = s->qs->first_variable; vp; vp = vp->next) {
        if (vp->rw != VAR_RW_SAVE || !vp->modified)
            continue;
        pstrcpy(varname, countof(varname), vp->name);
        for (p = varname; *p; p++) {
            if (*p == '-')
                *p = '_';
        }
        qe_get_variable(s, vp->name, buf, sizeof(buf), NULL, 1);
        eb_printf(b, "%s = %s;\n", varname, buf);
    }
    eb_putc(b, '\n');
}

int eb_variable_print_entry(EditBuffer *b, VarDef *vp, EditState *s) {
    char buf[256];
    char typebuf[32];
    const char *type = typebuf;
    int len;

    if (!vp)
        return 0;

    *typebuf = '\0';
    switch (vp->type) {
    case VAR_NUMBER:
        type = "int";
        break;
    case VAR_STRING:
        type = "string";
        break;
    case VAR_CHARS:
        type = "char";
        snprintf(typebuf, sizeof(typebuf), "[%d]", vp->size);
        break;
    default:
        type = "var";
        break;
    }
    b->cur_style = QE_STYLE_VARIABLE;
    len = eb_puts(b, vp->name);
    b->cur_style = QE_STYLE_DEFAULT;
    len += eb_puts(b, " = ");
    qe_get_variable(s, vp->name, buf, sizeof(buf), NULL, 1);
    if (*buf == '\"')
        b->cur_style = QE_STYLE_STRING;
    else
        b->cur_style = QE_STYLE_NUMBER;
    len += eb_puts(b, buf);
    b->cur_style = QE_STYLE_COMMENT;
    if (len + 1 < 40) {
        b->tab_width = max_int(len + 1, b->tab_width);
        len += eb_putc(b, '\t');
    } else {
        b->tab_width = 40;
    }
    len += eb_printf(b, "  %s%s", vp->rw ? "" : "read-only ",
                     var_domain[vp->domain]);
    b->cur_style = QE_STYLE_TYPE;
    len += eb_printf(b, " %s%s", type, typebuf);
    b->cur_style = QE_STYLE_DEFAULT;
    return len;
}

int variable_print_entry(CompleteState *cp, EditState *s, const char *name) {
    VarDef *vp = qe_find_variable(s->qs, name);
    if (vp) {
        // XXX: should pass the target window
        return eb_variable_print_entry(s->b, vp, s);
    } else {
        return eb_puts(s->b, name);
    }
}

static CompletionDef variable_completion = {
    .name = "variable",
    .enumerate = variable_complete,
    .print_entry = variable_print_entry,
};

/*---------------- commands ----------------*/

static const CmdDef var_commands[] = {
    CMD2( "show-variable", "",
          "Show the value of a given variable",
          do_show_variable, ESs,
          "s{Show variable: }[variable]|variable|")
    CMD2( "set-variable", "f8",
          "Set the value of a variable",
          do_set_variable, ESss,
          "s{Set variable: }[variable]|variable|s{to value: }|value|")
    CMD2( "describe-variable", "C-h v",
          "Show information for a variable",
          do_describe_variable, ESs,
          "s{Describe variable: }[variable]|variable|")
};

static int variables_init(QEmacsState *qs) {
    qe_register_variables(qs, var_table, countof(var_table));
    qe_register_commands(qs, NULL, var_commands, countof(var_commands));
    qe_register_completion(qs, &variable_completion);
    return 0;
}

qe_module_init(variables_init);
