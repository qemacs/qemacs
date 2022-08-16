/*
 * QEmacs, tiny but powerful multimode editor
 *
 * Copyright (c) 2000-2001 Fabrice Bellard.
 * Copyright (c) 2000-2022 Charlie Gordon.
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

/*
 * default qemacs configuration
 */
static const CmdDef basic_commands[] = {

    /*---------------- Simple commands ----------------*/

    /* Character insertion */

    CMD2( "self-insert-command", "default",
          do_char, ESii,
          "*" "kp", "Insert the character you type")
    CMD2( "insert-char", "M-#",
          do_char, ESii,
          "*"
          "n{Insert char: }|charvalue|"
          "p", "Insert the character with a specific code")
    /* do_tab will not change read only buffer */
    CMD2( "tabulate", "TAB",
          do_tab, ESi, "p", "")
    //CMD2( "space", "SPC", do_space, "*p", "")
    CMD2( "quoted-insert", "C-q",
          do_quoted_insert, ESi, "*p",
          "Read next input character and insert it")
    CMD2( "newline", "C-j, RET",
          do_newline, ES, "*",
          "Insert a newline, and move to left margin of the new line")
    CMD2( "open-line", "C-o",
          do_open_line, ES, "*",
          "Insert a newline and leave point before it")

    CMD2( "overwrite-mode", "insert",
          do_overwrite_mode, ESi, "p", "")
    CMD3( "insert-mode", "",
          do_overwrite_mode, ESi, 0, "v", "")

#ifdef CONFIG_UNICODE_JOIN
    /* Insert combining accent: combine with letter if possible */
    CMD3( "combine-grave-accent", "M-`",
          do_combine_accent, ESi, 0x300, "*v", "")
    CMD3( "combine-acute-accent", "M-'",
          do_combine_accent, ESi, 0x301, "*v", "")
    CMD3( "combine-circumflex-accent", "M-^",
          do_combine_accent, ESi, 0x302, "*v", "")
    CMD3( "combine-diaeresis", "M-\"",
          do_combine_accent, ESi, 0x308, "*v", "")
    CMD3( "combine-tilde", "M-~",
          do_combine_accent, ESi, 0x303, "*v", "")
#endif

    /* Moving around */

    CMD3( "previous-line", "C-p, up",
          do_up_down, ESi, -1, "P", "Move to previous line")
    CMD3( "next-line", "C-n, down",
          do_up_down, ESi, +1, "P", "Move to next line")
    CMD3( "backward-char", "C-b, left",
          do_left_right, ESi, -1, "P", "")
    CMD3( "forward-char", "C-f, right",
          do_left_right, ESi, +1, "P", "")
    CMD3( "backward-word", "M-b, C-left",
          do_word_left_right, ESi, -1, "P", "")
    CMD3( "forward-word", "M-f, C-right",
          do_word_left_right, ESi, 1, "P", "")
    CMD1( "scroll-down", "M-v, pageup",
          do_scroll_up_down, -2 , "") /* u? */
    CMD1( "scroll-up", "C-v, pagedown",
          do_scroll_up_down, 2 , "") /* u? */
    CMD1( "scroll-down-one", "M-z",
          do_scroll_up_down, -1 , "") /* u? */
    CMD1( "scroll-up-one", "C-z",
          do_scroll_up_down, 1 , "") /* u? */
    CMD0( "beginning-of-line", "C-a, home",
          do_bol, "")
    CMD0( "end-of-line", "C-e, end",
          do_eol, "")
    CMD0( "beginning-of-buffer", "M-<, C-home",
          do_bof, "")
    CMD0( "end-of-buffer", "M->, C-end",
          do_eof, "")

    /*---------------- Region handling / Kill commands ----------------*/

    /* deletion commands should be allowed in read only buffers,
     * they should merely copy the data to the kill ring */
    CMD2( "delete-char", "C-d, delete",
          do_delete_char, ESi, "*p", "")
    CMD2( "backward-delete-char", "DEL",
          do_backspace, ESi, "*p", "")
    CMD0( "set-mark-command", "C-@",
          do_set_mark, "")
    CMD0( "exchange-point-and-mark", "C-x C-x",
          do_exchange_point_and_mark, "")
    CMD0( "mark-whole-buffer", "C-x h",
          do_mark_whole_buffer, "")
    CMD0( "append-next-kill", "M-C-w",
          do_append_next_kill, "")
    CMD2( "kill-line", "C-k",
          do_kill_line, ESi, "p" , "")
    CMD3( "kill-whole-line", "M-k", // should be C-S-Backspace
          do_kill_whole_line, ESi, 1, "P", "")
    CMD2( "kill-beginning-of-line", "",
          do_kill_beginning_of_line, ESi, "p" , "")
    CMD3( "backward-kill-word", "M-DEL, M-C-h",
          do_kill_word, ESi, -1, "P", "")
    CMD3( "kill-word", "M-d",
          do_kill_word, ESi, +1, "P", "")
    /* XXX: should take region as argument, implicit from keyboard */
    CMD1( "kill-region", "C-w",
          do_kill_region, 0, "")
    CMD1( "copy-region", "M-w",
          do_kill_region, 1, "")
    CMD2( "yank", "C-y",
          do_yank, ES, "*", "")
    CMD2( "yank-pop", "M-y",
          do_yank_pop, ES, "*", "")

    /*---------------- Buffer and file handling ----------------*/

    CMD3( "find-file", "C-x C-f",
          do_find_file, ESsi, 0,
          "s{Find file: }[file]|file|"
          "v", "") /* u? */
    CMD3( "find-file-other-window", "C-x M-f",
          do_find_file_other_window, ESsi, 0,
          "s{Find file: }[file]|file|"
          "v", "") /* u? */
    CMD3( "find-alternate-file", "C-x C-v",
          do_find_alternate_file, ESsi, 0,
          "s{Find alternate file: }[file]|file|"
          "v", "") /* u? */
    CMD3( "find-file-noselect", "",
          do_find_file_noselect, ESsi, 0,
          "s{Find file: }[file]|file|"
          "v", "") /* u? */
    CMD2( "insert-file", "C-x i",
          do_insert_file, ESs,
          "*s{Insert file: }[file]|file|", "") /* u? */
    CMD0( "save-buffer", "C-x C-s",
          do_save_buffer, "") /* u? */
    CMD2( "write-file", "C-x C-w",
          do_write_file, ESs,
          "s{Write file: }[file]|file|", "") /* u? */
    /* XXX: should take region as argument, implicit from keyboard */
    CMD2( "write-region", "C-x w",
          do_write_region, ESs,
          "s{Write region to file: }[file]|file|", "") /* u? */
    CMD2( "switch-to-buffer", "C-x b",
          do_switch_to_buffer, ESs,
          "s{Switch to buffer: }[buffer]|buffer|", "")
    CMD3( "kill-buffer", "C-x k",
          do_kill_buffer, ESsi, 0,
          "s{Kill buffer: }[buffer]|buffer|"
          "v", "")
    CMD0( "toggle-read-only", "C-x C-q, C-c ~",
          do_toggle_read_only, "")
    CMD2( "not-modified", "M-~",
          do_not_modified, ESi, "p", "")
    CMD2( "set-visited-file-name", "",
          do_set_visited_file_name, ESss,
          "s{Set visited file name: }[file]|file|"
          "s{Rename file? }|newname|", "")

    /*---------------- Case handling ----------------*/

    CMD3( "capitalize-word", "M-c",
          do_changecase_word, ESi, 2, "*v", "")
    CMD3( "downcase-word", "M-l",
          do_changecase_word, ESi, -1, "*v", "")
    CMD3( "upcase-word", "M-u",
          do_changecase_word, ESi, 1, "*v", "")
    /* XXX: should take region as argument, implicit from keyboard */
    CMD3( "capitalize-region", "M-C-c",
          do_changecase_region, ESi, 2, "*v", "")
    CMD3( "downcase-region", "C-x C-l",
          do_changecase_region, ESi, -1, "*v", "")
    CMD3( "upcase-region", "C-x C-u",
          do_changecase_region, ESi, 1, "*v", "")

    /*---------------- Command handling ----------------*/

    CMD2( "execute-command", "M-x",
          do_execute_command, ESsi,
          "s{Command: }[command]|command|"
          "p", "")
    /* M-0 thru M-9 also start numeric argument */
    CMD0( "numeric-argument",
          "C-u, M--, M-0, M-1, M-2, M-3, M-4, M-5, M-6, M-7, M-8, M-9",
          do_numeric_argument, "")
    CMD0( "keyboard-quit",
          "C-g, C-x C-g, C-c C-g, C-h C-g, ESC ESC ESC",
          do_keyboard_quit, "")
    CMD0( "start-kbd-macro", "C-x (",
          do_start_kbd_macro, "")
    CMD0( "end-kbd-macro", "C-x )",
          do_end_kbd_macro, "")
    CMD0( "call-last-kbd-macro", "C-x e, C-\\",
          do_call_last_kbd_macro, "")
    CMD2( "define-kbd-macro", "",
          do_define_kbd_macro, ESsss,
          "s{Macro name: }[command]"
          "s{Macro keys: }|macrokeys|"
          "s{Bind to key: }[key]", "")
    /* set/unset key? */
    CMD3( "global-set-key", "",
          do_set_key, ESssi, 0,
          "s{Set key globally: }[key]"
          "s{command: }[command]|command|"
          "v", "")
    CMD3( "local-set-key", "",
          do_set_key, ESssi, 1,
          "s{Set key locally: }[key]"
          "s{command: }[command]|command|"
          "v", "")

    /*---------------- Window handling ----------------*/

    /* should merge these functions */
    CMD0( "other-window", "C-x o",
          do_other_window, "")
    CMD0( "next-window", "C-x n",
          do_other_window, "")
    CMD0( "previous-window", "C-x p",
          do_previous_window, "")
#ifndef CONFIG_TINY
    CMD1( "center-cursor", "M-C-l",
          do_center_cursor, 1, "")
    CMD1( "find-window-up", "C-x up",
          do_find_window, KEY_UP, "")
    CMD1( "find-window-down", "C-x down",
          do_find_window, KEY_DOWN, "")
    CMD1( "find-window-left", "C-x left",
          do_find_window, KEY_LEFT, "")
    CMD1( "find-window-right", "C-x right",
          do_find_window, KEY_RIGHT, "")
    CMD3( "scroll-left", "M-(",
          do_scroll_left_right, ESi, -1, "P", "")
    CMD3( "scroll-right", "M-)",
          do_scroll_left_right, ESi, +1, "P", "")
    CMD1( "preview-mode", "",
          do_preview_mode, 1, "")
#endif
    CMD1( "delete-window", "C-x 0",
          do_delete_window, 0, "")
    CMD1( "delete-other-windows", "C-x 1",
          do_delete_other_windows, 0, "")
    CMD1( "delete-all-windows", "",
          do_delete_other_windows, 1, "")
    CMD1( "hide-window", "",
          do_hide_window, 1, "")
    CMD0( "delete-hidden-windows", "",
          do_delete_hidden_windows, "")
    CMD3( "split-window-vertically", "C-x 2",
          do_split_window, ESii,
          SW_STACKED, "vp", "")
    CMD3( "split-window-horizontally", "C-x 3",
          do_split_window, ESii,
          SW_SIDE_BY_SIDE, "vp", "")
    CMD0( "toggle-full-screen", "C-c f",
          do_toggle_full_screen, "")
    CMD0( "toggle-mode-line", "C-c m",
          do_toggle_mode_line, "")
    CMD2( "create-window", "",
          do_create_window, ESss,
          "s{Filename: }[file]|file|"
          "s{Layout: }|layout|", "")

    /*---------------- Help ----------------*/

    CMD0( "start-trace-mode", "C-h d",
          do_start_trace_mode, "")
    CMD2( "set-trace-options", "C-h t",
          do_set_trace_options, ESs,
          "s{Trace options: }|trace|", "")
    CMD0( "describe-key-briefly", "C-h c, C-h k",
          do_describe_key_briefly, "")
    CMD0( "help-for-help", "C-h C-h, f1",
          do_help_for_help, "")

    /*---------------- International ----------------*/

    CMD2( "set-buffer-file-coding-system", "C-x RET f, C-c c",
          do_set_buffer_file_coding_system, ESs,
          "s{Charset: }[charset]|charset|", "")
    CMD2( "convert-buffer-file-coding-system", "",
          do_convert_buffer_file_coding_system, ESs,
          "*" "s{Charset: }[charset]|charset|", "")
    CMD0( "toggle-bidir", "C-x RET b, C-c b",
          do_toggle_bidir, "")
    CMD2( "set-input-method", "C-x RET C-\\, C-c C-\\",
          do_set_input_method, ESs,
          "s{Input method: }[input]", "")
    CMD0( "switch-input-method", "C-x C-\\",
          do_switch_input_method, "")

    /*---------------- Styles & display ----------------*/

    CMD2( "define-color", "",
          do_define_color, ESss,
          "s{Color name: }[color]|color|"
          "s{Color value: }[color]|color|", "")
    CMD2( "set-style", "",
          do_set_style, ESsss,
          "s{Style: }[style]|style|"
          "s{CSS Property Name: }[style-property]|style-property|"
          "s{CSS Property Value: }|value|", "")
    CMD2( "set-display-size", "",
          do_set_display_size, ESii,
          "n{Width: }|width|"
          "n{Height: }|height|", "")
    CMD2( "set-system-font", "",
          do_set_system_font, ESss,
          "s{Font family: }|fontfamily|"
          "s{System fonts: }|fontnames|", "")
    CMD2( "set-window-style", "",
          do_set_window_style, ESs,
          "s{Style: }[style]|style|", "")

    /*---------------- Miscellaneous ----------------*/

    CMD2( "exit-qemacs", "C-x C-c",
          do_exit_qemacs, ESi, "p", "")
    CMD0( "refresh", "C-l",
          do_refresh_complete, "")
    CMD0( "undo", "C-x u, C-_",
          do_undo, "")
    CMD0( "redo", "C-x r, C-x C-_",
          do_redo, "")
    CMD3( "goto-line", "M-g",
          do_goto, ESsi, 'l',
          "s{Goto line: }"
          "v", "")
    CMD3( "goto-char", "C-x g",
          do_goto, ESsi, 'c',
          "s{Goto char: }"
          "v", "")
    CMD0( "count-lines", "C-x l",
          do_count_lines, "")
    CMD0( "what-cursor-position", "C-x =",
          do_what_cursor_position, "")

    /* non standard mappings */
    CMD0( "toggle-line-numbers", "C-x RET l, C-c l",
          do_toggle_line_numbers, "")
    CMD0( "toggle-truncate-lines", "C-x RET t, C-c t",
          do_toggle_truncate_lines, "")
    CMD0( "word-wrap", "C-x RET w, C-c w",
          do_word_wrap, "")
    CMD1( "toggle-control-h", "",
          do_toggle_control_h, 0, "")
    CMD2( "set-emulation", "",
          do_set_emulation, ESs,
          "s{Emulation mode: }|emulation|", "")
    CMD2( "cd", "",
          do_cd, ESs,
          "s{Change default directory: }[dir]|file|", "")
    CMD2( "set-mode", "",
          do_set_mode, ESs,
          "s{Set mode: }[mode]", "")
    CMD1( "set-auto-coding", "",
          do_set_auto_coding, 1, "")
    CMD1( "set-auto-mode", "",
          do_set_next_mode, 0, "")
    CMD3( "set-next-mode", "M-m",
          do_set_next_mode, ESi, +1, "P", "")
    CMD3( "set-previous-mode", "",
          do_set_next_mode, ESi, -1, "P", "")

    /* tab & indent */
    CMD2( "set-tab-width", "",
          do_set_tab_width, ESi,
          "p{Tab width: }", "")
    CMD2( "set-indent-width", "",
          do_set_indent_width, ESi,
          "p{Indent width: }", "")
    CMD2( "set-indent-tabs-mode", "",
          do_set_indent_tabs_mode, ESi,
          "p{Indent tabs mode (0 or 1): }", "")
    CMD2( "set-fill-column", "",
          do_set_fill_column, ESi,
          "p{Fill column: }", "")

    /* other stuff */
    CMD3( "load-file-from-path", "C-c C-f",
          do_load_file_from_path, ESsi, 0,
          "s{Load file from path: }[resource]|file|"
          "v", "")
    CMD2( "load-config-file", "",
          do_load_config_file, ESs,
          "s{Configuration file: }[resource]|file|", "")
    CMD2( "load-qerc", "",
          do_load_qerc, ESs,
          "s{path: }[file]|file|", "")
    CMD2( "add-resource-path", "",
          do_add_resource_path, ESs,
          "s{resource path: }[dir]|file|", "")
};
