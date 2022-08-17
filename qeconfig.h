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
          do_char, ESii, "*" "kp",
         "Insert the character you type")
    CMD2( "insert-char", "M-#",
          do_char, ESii, "*"
          "n{Insert char: }|charvalue|"
          "p",
          "Insert the character with a specific code")
    /* do_tab will not change read only buffer */
    CMD2( "tabulate", "TAB",
          do_tab, ESi, "p",
          "Insert a TAB or spaces according to indent-tabs-mode")
    //CMD2( "space", "SPC", do_space, "*p", "Insert a space")
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
          do_overwrite_mode, ESi, "p",
          "Toggle between overwrite mode and insert mode")
    CMD3( "insert-mode", "",
          do_overwrite_mode, ESi, 0, "v",
          "Select insert mode")

#ifdef CONFIG_UNICODE_JOIN
    /* Insert combining accent: combine with letter if possible */
    CMD3( "combine-grave-accent", "M-`",
          do_combine_accent, ESi, 0x300, "*v",
          "Combine the previous letter with a grave accent")
    CMD3( "combine-acute-accent", "M-'",
          do_combine_accent, ESi, 0x301, "*v",
          "Combine the previous letter with an acute accent")
    CMD3( "combine-circumflex-accent", "M-^",
          do_combine_accent, ESi, 0x302, "*v",
          "Combine the previous letter with a circumflex accent")
    CMD3( "combine-diaeresis", "M-\"",
          do_combine_accent, ESi, 0x308, "*v",
          "Combine the previous letter with a diaeresis (aka trema or umlaut)")
    CMD3( "combine-tilde", "M-~",
          do_combine_accent, ESi, 0x303, "*v",
          "Combine the previous letter with a tilde")
#endif

    /* Moving around */

    CMD3( "previous-line", "C-p, up",
          do_up_down, ESi, -1, "P",
          "Move to previous line")
    CMD3( "next-line", "C-n, down",
          do_up_down, ESi, +1, "P",
          "Move to next line")
    CMD3( "backward-char", "C-b, left",
          do_left_right, ESi, -1, "P",
          "Move to the previous character")
    CMD3( "forward-char", "C-f, right",
          do_left_right, ESi, +1, "P",
          "Move to the next character")
    CMD3( "backward-word", "M-b, C-left",
          do_word_left_right, ESi, -1, "P",
          "Move to the beginning of the word on or before point")
    CMD3( "forward-word", "M-f, C-right",
          do_word_left_right, ESi, 1, "P",
          "Move to the end of the word on or after point")
    CMD1( "scroll-down", "M-v, pageup",
          do_scroll_up_down, -2,
          "Display the previous page") /* u? */
    CMD1( "scroll-up", "C-v, pagedown",
          do_scroll_up_down, 2,
          "Display the next page") /* u? */
    CMD1( "scroll-down-one", "M-z",
          do_scroll_up_down, -1,
          "Move the window contents down one line") /* u? */
    CMD1( "scroll-up-one", "C-z",
          do_scroll_up_down, 1,
          "Move the window contents up one line") /* u? */
    CMD0( "beginning-of-line", "C-a, home",
          do_bol,
          "Move point to the beginning of the line")
    CMD0( "end-of-line", "C-e, end",
          do_eol,
          "Move point to the end of the line")
    CMD0( "beginning-of-buffer", "M-<, C-home",
          do_bof,
          "Move point to the beginning of the buffer")
    CMD0( "end-of-buffer", "M->, C-end",
          do_eof,
          "Move point to the end of the buffer")

    /*---------------- Region handling / Kill commands ----------------*/

    /* deletion commands should be allowed in read only buffers,
     * they should merely copy the data to the kill ring */
    CMD2( "delete-char", "C-d, delete",
          do_delete_char, ESi, "*p",
          "Delete the character at point")
    CMD2( "backward-delete-char", "DEL",
          do_backspace, ESi, "*p",
          "Delete the character before point")
    CMD0( "set-mark-command", "C-@",
          do_set_mark,
          "Set the buffer mark")
    CMD0( "exchange-point-and-mark", "C-x C-x",
          do_exchange_point_and_mark,
          "Exchange point and the buffer mark")
    CMD0( "mark-whole-buffer", "C-x h",
          do_mark_whole_buffer,
          "Set the mark at the beginning and point at the end of the buffer")
    CMD0( "append-next-kill", "M-C-w",
          do_append_next_kill,
          "Append the next kill to the current entry in the kill ring")
    CMD2( "kill-line", "C-k",
          do_kill_line, ESi, "p",
          "Kill to the end of line")
    CMD3( "kill-whole-line", "M-k", // should be C-S-Backspace
          do_kill_whole_line, ESi, 1, "P",
          "Kill the line at point")
    CMD2( "kill-beginning-of-line", "",
          do_kill_beginning_of_line, ESi, "p",
          "Kill to the beginning of the line")
    CMD3( "backward-kill-word", "M-DEL, M-C-h",
          do_kill_word, ESi, -1, "P",
          "Kill to the beginning of the word at or before point")
    CMD3( "kill-word", "M-d",
          do_kill_word, ESi, +1, "P",
          "Kill to the end of the word at or after point")
    /* XXX: should take region as argument, implicit from keyboard */
    CMD1( "kill-region", "C-w",
          do_kill_region, 0,
          "Kill the current region")
    CMD1( "copy-region", "M-w",
          do_kill_region, 1,
          "Copy the current region to the kill ring")
    CMD2( "yank", "C-y",
          do_yank, ES, "*",
          "Insert the contents of the current entry in the kill ring")
    CMD2( "yank-pop", "M-y",
          do_yank_pop, ES, "*",
          "Replace the last yanked data with the contents of the previous entry in the kill ring")

    /*---------------- Buffer and file handling ----------------*/

    CMD3( "find-file", "C-x C-f",
          do_find_file, ESsi, 0,
          "s{Find file: }[file]|file|"
          "v",
          "Load a file into a new buffer and/or display it in the current window") /* u? */
    CMD3( "find-file-other-window", "C-x M-f",
          do_find_file_other_window, ESsi, 0,
          "s{Find file: }[file]|file|"
          "v",
          "Load a file into a new buffer and/or display it in a new window") /* u? */
    CMD3( "find-alternate-file", "C-x C-v",
          do_find_alternate_file, ESsi, 0,
          "s{Find alternate file: }[file]|file|"
          "v",
          "Load a new file into the current buffer and/or display it in the current window") /* u? */
    CMD3( "find-file-noselect", "",
          do_find_file_noselect, ESsi, 0,
          "s{Find file: }[file]|file|"
          "v",
          "Load a file into a new buffer") /* u? */
    CMD2( "insert-file", "C-x i",
          do_insert_file, ESs,
          "*s{Insert file: }[file]|file|",
          "Insert the contents of a file at point") /* u? */
    CMD0( "save-buffer", "C-x C-s",
          do_save_buffer,
          "Save the buffer contents to the associated file if modified") /* u? */
    CMD2( "write-file", "C-x C-w",
          do_write_file, ESs,
          "s{Write file: }[file]|file|",
          "Write the buffer contents to a specified file and associate it to the buffer") /* u? */
    /* XXX: should take region as argument, implicit from keyboard */
    CMD2( "write-region", "C-x w",
          do_write_region, ESs,
          "s{Write region to file: }[file]|file|",
          "Write the contents of the current region to a specified file") /* u? */
    CMD2( "switch-to-buffer", "C-x b",
          do_switch_to_buffer, ESs,
          "s{Switch to buffer: }[buffer]|buffer|",
          "Change the buffer attached to the current window")
    CMD3( "kill-buffer", "C-x k",
          do_kill_buffer, ESsi, 0,
          "s{Kill buffer: }[buffer]|buffer|"
          "v",
          "Remove a named buffer")
    CMD0( "toggle-read-only", "C-x C-q, C-c ~",
          do_toggle_read_only,
          "Toggle the read-only flag of the current buffer")
    CMD2( "not-modified", "M-~",
          do_not_modified, ESi, "p",
          "Toggle the modified flag of the current buffer")
    CMD2( "set-visited-file-name", "",
          do_set_visited_file_name, ESss,
          "s{Set visited file name: }[file]|file|"
          "s{Rename file? }|newname|",
          "")

    /*---------------- Case handling ----------------*/

    CMD3( "capitalize-word", "M-c",
          do_changecase_word, ESi, 2, "*v",
          "Upcase the first letter and downcase the rest of the word")
    CMD3( "downcase-word", "M-l",
          do_changecase_word, ESi, -1, "*v",
          "Downcase the rest of the word")
    CMD3( "upcase-word", "M-u",
          do_changecase_word, ESi, 1, "*v",
          "Upcase the rest of the word")
    /* XXX: should take region as argument, implicit from keyboard */
    CMD3( "capitalize-region", "M-C-c",
          do_changecase_region, ESi, 2, "*v",
          "Apply capital case to all words in the region")
    CMD3( "downcase-region", "C-x C-l",
          do_changecase_region, ESi, -1, "*v",
          "Downcase all words in the region")
    CMD3( "upcase-region", "C-x C-u",
          do_changecase_region, ESi, 1, "*v",
          "Upcase all words in the region")

    /*---------------- Command handling ----------------*/

    CMD2( "execute-command", "M-x",
          do_execute_command, ESsi,
          "s{Command: }[command]|command|"
          "p",
          "Run a named command")
    /* M-0 thru M-9 also start numeric argument */
    CMD0( "numeric-argument",
          "C-u, M--, M-0, M-1, M-2, M-3, M-4, M-5, M-6, M-7, M-8, M-9",
          do_numeric_argument,
          "Set the numeric argument for the next command")
    CMD0( "keyboard-quit",
          "C-g, C-x C-g, C-c C-g, C-h C-g, ESC ESC ESC",
          do_keyboard_quit,
          "Abort the current command")
    CMD0( "start-kbd-macro", "C-x (",
          do_start_kbd_macro,
          "Start recording a keyboard macro")
    CMD0( "end-kbd-macro", "C-x )",
          do_end_kbd_macro,
          "End recording a keyboard macro")
    CMD0( "call-last-kbd-macro", "C-x e, C-\\",
          do_call_last_kbd_macro,
          "Run the last recorded keyboard macro")
    CMD2( "define-kbd-macro", "",
          do_define_kbd_macro, ESsss,
          "s{Macro name: }[command]"
          "s{Macro keys: }|macrokeys|"
          "s{Bind to key: }[key]",
          "Define a named keyboard macro")
    /* set/unset key? */
    CMD3( "global-set-key", "",
          do_set_key, ESssi, 0,
          "s{Set key globally: }[key]"
          "s{command: }[command]|command|"
          "v",
          "Register a global key binding")
    CMD3( "local-set-key", "",
          do_set_key, ESssi, 1,
          "s{Set key locally: }[key]"
          "s{command: }[command]|command|"
          "v",
          "Register a key binding a given mode")

    /*---------------- Window handling ----------------*/

    /* should merge these functions */
    CMD0( "other-window", "C-x o",
          do_other_window,
          "Move the focus to another window")
    CMD0( "next-window", "C-x n",
          do_other_window,
          "Move the focus to the next window")
    CMD0( "previous-window", "C-x p",
          do_previous_window,
          "Move the focus to the previous window")
#ifndef CONFIG_TINY
    CMD1( "center-cursor", "M-C-l",
          do_center_cursor, 1,
          "Center the window contents at point")
    CMD1( "find-window-up", "C-x up",
          do_find_window, KEY_UP,
          "Move the focus to the window above the current one")
    CMD1( "find-window-down", "C-x down",
          do_find_window, KEY_DOWN,
          "Move the focus to the window below the current one")
    CMD1( "find-window-left", "C-x left",
          do_find_window, KEY_LEFT,
          "Move the focus to the window to the left of the current one")
    CMD1( "find-window-right", "C-x right",
          do_find_window, KEY_RIGHT,
          "Move the focus to the window to the right of the current one")
    CMD3( "scroll-left", "M-(",
          do_scroll_left_right, ESi, -1, "P",
          "Shift the window contents to the left")
    CMD3( "scroll-right", "M-)",
          do_scroll_left_right, ESi, +1, "P",
          "Shift the window contents to the right")
    CMD1( "preview-mode", "",
          do_preview_mode, 1,
          "Enter preview mode: cursor movement keys cause window scrolling")
#endif
    CMD1( "delete-window", "C-x 0",
          do_delete_window, 0,
          "Delete the current window")
    CMD1( "delete-other-windows", "C-x 1",
          do_delete_other_windows, 0,
          "Delete all other windows")
    CMD1( "delete-all-windows", "",
          do_delete_other_windows, 1,
          "Delete all windows")
    CMD1( "hide-window", "",
          do_hide_window, 1,
          "Hide the current window")
    CMD0( "delete-hidden-windows", "",
          do_delete_hidden_windows,
          "Delete the hidden windows")
    CMD3( "split-window-vertically", "C-x 2",
          do_split_window, ESii,
          SW_STACKED, "vp",
          "Split the current window top and bottom")
    CMD3( "split-window-horizontally", "C-x 3",
          do_split_window, ESii,
          SW_SIDE_BY_SIDE, "vp",
          "Split the current window side by side")
    CMD0( "toggle-full-screen", "C-c f",
          do_toggle_full_screen,
          "Toggle full screen display (on graphics displays)")
    CMD0( "toggle-mode-line", "C-c m",
          do_toggle_mode_line,
          "Toggle mode-line display")
    CMD2( "create-window", "",
          do_create_window, ESss,
          "s{Filename: }[file]|file|"
          "s{Layout: }|layout|",
          "Create a new window with a specified layout")

    /*---------------- Help ----------------*/

    CMD0( "start-trace-mode", "C-h d",
          do_start_trace_mode,
         "Start trace mode: show the *Trace* buffer with debugging info")
    CMD2( "set-trace-options", "C-h t",
          do_set_trace_options, ESs,
          "s{Trace options: }|trace|",
          "Set the trace options")
    CMD0( "describe-key-briefly", "C-h c, C-h k",
          do_describe_key_briefly,
          "Describe a key binding")
    CMD0( "help-for-help", "C-h C-h, f1",
          do_help_for_help,
          "Show the qemacs help window")

    /*---------------- International ----------------*/

    CMD2( "set-buffer-file-coding-system", "C-x RET f, C-c c",
          do_set_buffer_file_coding_system, ESs,
          "s{Charset: }[charset]|charset|",
          "Set the buffer charset and encoding system")
    CMD2( "convert-buffer-file-coding-system", "",
          do_convert_buffer_file_coding_system, ESs,
          "*" "s{Charset: }[charset]|charset|",
          "Convert the buffer contents to a new charset and encoding system")
    CMD0( "toggle-bidir", "C-x RET b, C-c b",
          do_toggle_bidir,
          "")
    CMD2( "set-input-method", "C-x RET C-\\, C-c C-\\",
          do_set_input_method, ESs,
          "s{Input method: }[input]",
          "")
    CMD0( "switch-input-method", "C-x C-\\",
          do_switch_input_method,
          "")

    /*---------------- Styles & display ----------------*/

    CMD2( "define-color", "",
          do_define_color, ESss,
          "s{Color name: }[color]|color|"
          "s{Color value: }[color]|color|",
          "Define a named color")
    CMD2( "set-style", "",
          do_set_style, ESsss,
          "s{Style: }[style]|style|"
          "s{CSS Property Name: }[style-property]|style-property|"
          "s{CSS Property Value: }|value|",
          "Set a property for a named style")
    CMD2( "set-display-size", "",
          do_set_display_size, ESii,
          "n{Width: }|width|"
          "n{Height: }|height|",
          "Set the dimensions of the graphics screen")
    CMD2( "set-system-font", "",
          do_set_system_font, ESss,
          "s{Font family: }|fontfamily|"
          "s{System fonts: }|fontnames|",
          "Set the system font")
    CMD2( "set-window-style", "",
          do_set_window_style, ESs,
          "s{Style: }[style]|style|",
          "")

    /*---------------- Miscellaneous ----------------*/

    CMD2( "exit-qemacs", "C-x C-c",
          do_exit_qemacs, ESi, "p",
          "Exit Quick Emacs")
    CMD0( "refresh", "C-l",
          do_refresh_complete,
          "Refresh the display, center the window contents at point")
    CMD0( "undo", "C-x u, C-_",
          do_undo,
          "Undo the last change")
    CMD0( "redo", "C-x r, C-x C-_",
          do_redo,
          "Redo the last change undone")
    CMD3( "goto-line", "M-g",
          do_goto, ESsi, 'l',
          "s{Goto line: }"
          "v",
          "Go to a line number")
    CMD3( "goto-char", "C-x g",
          do_goto, ESsi, 'c',
          "s{Goto char: }"
          "v",
          "Go to a character number")
    CMD0( "count-lines", "C-x l",
          do_count_lines,
          "Count the lines in the current buffer and region")
    CMD0( "what-cursor-position", "C-x =",
          do_what_cursor_position,
          "Show the current point and mark positions")

    /* non standard mappings */
    CMD0( "toggle-line-numbers", "C-x RET l, C-c l",
          do_toggle_line_numbers,
          "Toggle the line number display")
    CMD0( "toggle-truncate-lines", "C-x RET t, C-c t",
          do_toggle_truncate_lines,
          "Toggle displaying long lines on multiple screen rows")
    CMD0( "word-wrap", "C-x RET w, C-c w",
          do_word_wrap,
          "Toggle wrapping on a character or word basis")
    CMD1( "toggle-control-h", "",
          do_toggle_control_h, 0,
          "Toggle backspace / DEL handling")
    CMD2( "set-emulation", "",
          do_set_emulation, ESs,
          "s{Emulation mode: }|emulation|",
          "Select emacs flavor emulation")
    CMD2( "cd", "",
          do_cd, ESs,
          "s{Change default directory: }[dir]|file|",
          "Change the current directory of the qemacs process")
    CMD2( "set-mode", "",
          do_set_mode, ESs,
          "s{Set mode: }[mode]",
          "Set an editing mode")
    CMD1( "set-auto-coding", "",
          do_set_auto_coding, 1,
          "")
    CMD1( "set-auto-mode", "",
          do_set_next_mode, 0,
          "Select the best mode for the current buffer")
    CMD3( "set-next-mode", "M-m",
          do_set_next_mode, ESi, +1, "P",
          "Select the next mode appropriate for the current buffer")
    CMD3( "set-previous-mode", "",
          do_set_next_mode, ESi, -1, "P",
          "Select the previous mode appropriate for the current buffer")

    /* tab & indent */
    CMD2( "set-tab-width", "",
          do_set_tab_width, ESi,
          "p{Tab width: }",
          "Set the TAB width for the current buffer")
    CMD2( "set-indent-width", "",
          do_set_indent_width, ESi,
          "p{Indent width: }",
          "Set the indentation width for the current window")
    CMD2( "set-indent-tabs-mode", "",
          do_set_indent_tabs_mode, ESi,
          "p{Indent tabs mode (0 or 1): }",
          "Select whether to use TABs or spaces for indentation")
    CMD2( "set-fill-column", "",
          do_set_fill_column, ESi,
          "p{Fill column: }",
          "Set the width for paragraph filling")

    /* other stuff */
    CMD3( "load-file-from-path", "C-c C-f",
          do_load_file_from_path, ESsi, 0,
          "s{Load file from path: }[resource]|file|"
          "v",
          "Load a resource file from the QEPATH")
    CMD2( "load-config-file", "",
          do_load_config_file, ESs,
          "s{Configuration file: }[resource]|file|",
          "Load a configuration file from the QEPATH")
    CMD2( "load-qerc", "",
          do_load_qerc, ESs,
          "s{path: }[file]|file|",
          "Load a local .qerc settings file")
    CMD2( "add-resource-path", "",
          do_add_resource_path, ESs,
          "s{resource path: }[dir]|file|",
          "Add a path to the resource path list")
};
