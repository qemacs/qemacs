/*
 * QEmacs, tiny but powerful multimode editor
 *
 * Copyright (c) 2000-2001 Fabrice Bellard.
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

/*
 * default qemacs configuration
 */
static const CmdDef basic_commands[] = {

    /*---------------- Simple commands ----------------*/

    /* Character insertion */

    CMD2( "self-insert-command", "default",
          "Insert the character you type",
          do_char, ESii, "*" "k" "p")
    CMD2( "insert-char", "M-#, C-x 8 RET",
          "Insert the character with a specific code or Unicode name",
          do_char, ESii, "*"
          "n{Insert char: }[charname]|charvalue|"
          "p")
    /* do_tab will not change read only buffer */
    CMD2( "tabulate", "TAB",
          "Insert a TAB or spaces according to indent-tabs-mode",
          do_tab, ESi, "p")
    //CMD2( "space", "SPC", "Insert a space", do_space, ESi, "*" "P")
    CMD2( "quoted-insert", "C-q",
          "Read next input character and insert it",
          do_quoted_insert, ESi, "*" "p")
    CMD2( "newline", "C-j, RET",
          "Insert a newline, and move to left margin of the new line",
          do_newline, ES, "*")
    CMD2( "open-line", "C-o",
          "Insert a newline and leave point before it",
          do_open_line, ES, "*")

    CMD2( "overwrite-mode", "insert, C-c o",
          "Toggle between overwrite mode and insert mode",
          do_overwrite_mode, ESi, "P")
    CMD3( "insert-mode", "C-c i",
          "Select insert mode",
          do_overwrite_mode, ESi, "v", 0)

#ifdef CONFIG_UNICODE_JOIN
    /* Insert combining accent: combine with letter if possible */
    CMD3( "combine-grave-accent", "M-`",
          "Combine the previous letter with a grave accent",
          do_combine_accent, ESi, "*" "v", 0x300)
    CMD3( "combine-acute-accent", "M-'",
          "Combine the previous letter with an acute accent",
          do_combine_accent, ESi, "*" "v", 0x301)
    CMD3( "combine-circumflex-accent", "M-^",
          "Combine the previous letter with a circumflex accent",
          do_combine_accent, ESi, "*" "v", 0x302)
    CMD3( "combine-diaeresis", "M-\"",
          "Combine the previous letter with a diaeresis (aka trema or umlaut)",
          do_combine_accent, ESi, "*" "v", 0x308)
    CMD3( "combine-tilde", "M-~",  // binding conflicts with not-modified
          "Combine the previous letter with a tilde",
          do_combine_accent, ESi, "*" "v", 0x303)
#endif

    /* Moving around */

    CMD2( "previous-line", "C-p, up, S-up",
          "Move to previous line",
          do_up_down, ESi, "q")
    CMD2( "next-line", "C-n, down, S-down",
          "Move to next line",
          do_up_down, ESi, "p")
    CMD2( "backward-char", "C-b, left, S-left",
          "Move to the previous character",
          do_left_right, ESi, "q")
    CMD2( "forward-char", "C-f, right, S-right",
          "Move to the next character",
          do_left_right, ESi, "p")
    CMD2( "backward-word", "M-b, C-left, M-left, C-S-left, M-S-left",
          "Move to the beginning of the word on or before point",
          do_word_left_right, ESi, "q")
    CMD2( "forward-word", "M-f, C-right, M-right, C-S-right, M-S-right",
          "Move to the end of the word on or after point",
          do_word_left_right, ESi, "p")
    CMD1( "scroll-down", "M-v, pageup, S-pageup",
          "Display the previous page",
          do_scroll_up_down, -2) /* u? */
    CMD1( "scroll-up", "C-v, pagedown, S-pagedown",
          "Display the next page",
          do_scroll_up_down, 2) /* u? */
    CMD1( "scroll-down-one", "M-z",
          "Move the window contents down one line",
          do_scroll_up_down, -1) /* u? */
    CMD1( "scroll-up-one", "C-z",
          "Move the window contents up one line",
          do_scroll_up_down, 1) /* u? */
    CMD0( "beginning-of-line", "C-a, home, S-home",
          "Move point to the beginning of the line",
          do_bol)
    CMD0( "end-of-line", "C-e, end, S-end",
          "Move point to the end of the line",
          do_eol)
    CMD0( "beginning-of-buffer", "M-<, C-home, C-S-home",
          "Move point to the beginning of the buffer",
          do_bof)
    CMD0( "end-of-buffer", "M->, C-end, C-S-end",
          "Move point to the end of the buffer",
          do_eof)

    /*---------------- Region handling / Kill commands ----------------*/

    /* deletion commands should be allowed in read only buffers,
     * they should merely copy the data to the kill ring */
    CMD2( "delete-char", "C-d, delete",
          "Delete the character at point",
          do_delete_char, ESi, "*" "P")
    CMD2( "backward-delete-char", "DEL",
          "Delete the character before point",
          do_backspace, ESi, "*" "P")
    CMD0( "set-mark-command", "C-@",
          "Set the buffer mark",
          do_set_mark)
    CMD0( "exchange-point-and-mark", "C-x C-x",
          "Exchange point and the buffer mark",
          do_exchange_point_and_mark)
    CMD0( "mark-whole-buffer", "C-x h",
          "Set the mark at the beginning and point at the end of the buffer",
          do_mark_whole_buffer)
    CMD0( "append-next-kill", "M-C-w",
          "Append the next kill to the current entry in the kill ring",
          do_append_next_kill)
    CMD2( "kill-line", "C-k",
          "Kill to the end of line",
          do_kill_line, ESi, "P")
    CMD2( "kill-whole-line", "M-k", // should be C-S-Backspace
          "Kill the line at point",
          do_kill_whole_line, ESi, "p")
    CMD2( "kill-beginning-of-line", "",
          "Kill to the beginning of the line",
          do_kill_beginning_of_line, ESi, "P")
    CMD2( "backward-kill-word", "M-DEL, M-C-h",
          "Kill to the beginning of the word at or before point",
          do_kill_word, ESi, "q")
    CMD2( "kill-word", "M-d",
          "Kill to the end of the word at or after point",
          do_kill_word, ESi, "p")
    /* XXX: should take region as argument, implicit from keyboard */
    CMD0( "kill-region", "C-w",
          "Kill the current region",
          do_kill_region)
    CMD0( "copy-region", "M-w",
          "Copy the current region to the kill ring",
          do_copy_region)
    CMD2( "yank", "C-y",
          "Insert the contents of the current entry in the kill ring",
          do_yank, ES, "*")
    CMD2( "yank-pop", "M-y",
          "Replace the last yanked data with the contents of the previous entry in the kill ring",
          do_yank_pop, ES, "*")

    /*---------------- Buffer and file handling ----------------*/

    CMD3( "find-file", "C-x C-f",
          "Load a file into a new buffer and/or display it in the current window",
          do_find_file, ESsi,
          "s{Find file: }[file]|file|"
          "v", 0) /* u? */
    CMD3( "find-file-other-window", "C-x M-f",
          "Load a file into a new buffer and/or display it in a new window",
          do_find_file_other_window, ESsi,
          "s{Find file: }[file]|file|"
          "v", 0) /* u? */
    CMD3( "find-alternate-file", "C-x C-v",
          "Load a new file into the current buffer and/or display it in the current window",
          do_find_alternate_file, ESsi,
          "s{Find alternate file: }[file]|file|"
          "v", 0) /* u? */
    CMD3( "find-file-noselect", "",
          "Load a file into a new buffer",
          do_find_file_noselect, ESsi,
          "s{Find file: }[file]|file|"
          "v", 0) /* u? */
    CMD2( "insert-file", "C-x i",
          "Insert the contents of a file at point",
          do_insert_file, ESs, "*"
          "s{Insert file: }[file]|file|") /* u? */
    CMD0( "save-buffer", "C-x C-s",
          "Save the buffer contents to the associated file if modified",
          do_save_buffer) /* u? */
    CMD2( "write-file", "C-x C-w",
          "Write the buffer contents to a specified file and associate it to the buffer",
          do_write_file, ESs,
          "s{Write file: }[file]|file|") /* u? */
    /* XXX: should take region as argument, implicit from keyboard */
    CMD2( "write-region", "C-x w",
          "Write the contents of the current region to a specified file",
          do_write_region, ESs,
          "s{Write region to file: }[file]|file|") /* u? */
    CMD2( "switch-to-buffer", "C-x b",
          "Change the buffer attached to the current window",
          do_switch_to_buffer, ESs,
          "s{Switch to buffer: }[buffer]|buffer|")
    CMD3( "kill-buffer", "C-x k",
          "Remove a named buffer",
          do_kill_buffer, ESsi,
          "s{Kill buffer: }[buffer]|buffer|"
          "v", 0)
    CMD3( "next-buffer", "C-x C-right",
          "Switch to the next buffer",
          do_buffer_navigation, ESii, "p" "v", 1)
    CMD3( "previous-buffer", "C-x C-left",
          "Switch to the previous buffer",
          do_buffer_navigation, ESii, "p" "v", -1)
    CMD0( "toggle-read-only", "C-x C-q, C-c %",
          "Toggle the read-only flag of the current buffer",
          do_toggle_read_only)
    CMD2( "not-modified", "M-~, C-c ~",
          "Toggle the modified flag of the current buffer",
          do_not_modified, ESi, "P")
    CMD2( "set-visited-file-name", "",
          "Change the name of file visited in current buffer",
          do_set_visited_file_name, ESss,
          "s{Set visited file name: }[file]|file|"
          "s{Rename file? }|newname|")

    /*---------------- Case handling ----------------*/

    CMD3( "capitalize-word", "M-c",
          "Upcase the first letter and downcase the rest of the word",
          do_changecase_word, ESi, "*" "v", 2)
    CMD3( "downcase-word", "M-l",
          "Downcase the rest of the word",
          do_changecase_word, ESi, "*" "v", -1)
    CMD3( "upcase-word", "M-u",
          "Upcase the rest of the word",
          do_changecase_word, ESi, "*" "v", 1)
    /* XXX: should take region as argument, implicit from keyboard */
    CMD3( "capitalize-region", "M-C-c",
          "Apply capital case to all words in the region",
          do_changecase_region, ESi, "*" "v", 2)
    CMD3( "downcase-region", "C-x C-l",
          "Downcase all words in the region",
          do_changecase_region, ESi, "*" "v", -1)
    CMD3( "upcase-region", "C-x C-u",
          "Upcase all words in the region",
          do_changecase_region, ESi, "*" "v", 1)

    /*---------------- Command handling ----------------*/

    CMD2( "execute-command", "M-x, f2",
          "Run a named command",
          do_execute_command, ESsi,
          "s{Command: }[command]|command|"
          "P")
    CMD2( "universal-argument",
          "C-u",
          "Set or multiply the numeric argument by 4",
          do_prefix_argument, ESi, "k")
    CMD2( "negative-argument",
          "M--",
          "Negate or set the numeric argument to -1",
          do_prefix_argument, ESi, "k")
    CMD2( "digit-argument",
          "M-0, M-1, M-2, M-3, M-4, M-5, M-6, M-7, M-8, M-9",
          "Set the numeric prefix argument",
          do_prefix_argument, ESi, "k")
    CMD0( "keyboard-quit",
          "C-g, C-x C-g, C-c C-g, C-h C-g, ESC ESC ESC",
          "Abort the current command",
          do_keyboard_quit)
    CMD0( "unknown-key",
          "none",
          "An unknown key was pressed",
          do_unknown_key)

    CMD0( "start-kbd-macro", "C-x (",
          "Start recording a keyboard macro",
          do_start_kbd_macro)
    CMD0( "end-kbd-macro", "C-x )",
          "End recording a keyboard macro",
          do_end_kbd_macro)
    CMD2( "call-last-kbd-macro", "C-x e, C-\\",
          "Run the last recorded keyboard macro",
          do_call_last_kbd_macro, ESi, "p")
    CMD2( "define-kbd-macro", "",
          "Define a named keyboard macro",
          do_define_kbd_macro, ESsss,
          "s{Macro name: }[command]"
          "s{Macro keys: }|macrokeys|"
          "s{Bind to key: }[key]")
#ifndef CONFIG_TINY
    CMD2( "edit-last-kbd-macro", "C-x *, C-x C-k C-e, C-x C-k e",
          "Edit the last keyboard macro",
          do_edit_last_kbd_macro, ESs,
          "s{Macro keys: }|macrokeys|")
    CMD2( "name-last-kbd-macro", "C-x C-k C-n, C-x C-k n",
          "Define a named command from the last keyboard macro",
          do_name_last_kbd_macro, ESs,
          "s{Macro name: }[command]")
    CMD2( "insert-kbd-macro", "C-x C-k i",
          "Insert in buffer the definition of kbd macro MACRONAME, as qescript code",
          do_insert_kbd_macro, ESs,
          "*s{Macro name: }[command]")
    CMD2( "read-kbd-macro", "C-x C-k r",
          "Read the region as a keyboard macro definition",
          do_read_kbd_macro, ESii, "m" "d")
    CMD2( "macro-add-counter", "C-x C-k C-a, C-x C-k a",
          "Add the value of numeric prefix arg (prompt if missing) to `macro-counter`",
          do_macro_add_counter, ESi,
          "N{Macro increment: }")
    CMD2( "macro-set-counter", "C-x C-k C-c, C-x C-k c",
          "Set the value of `macro-counter' to ARG, or prompt for value if no argument",
          do_macro_set_counter, ESi,
          "N{Macro counter: }")
    CMD2( "macro-insert-counter", "C-x C-k TAB, C-x C-k =",
          "Insert current value of `macro-counter`, then increment it by ARG",
          do_macro_insert_counter, ESi, "*p")
    CMD2( "macro-set-format", "C-x C-k C-f, C-x C-k f",
          "Set the printf-like format for `macro-insert-counter`",
          do_macro_set_format, ESs,
          "s{Format: }|macroformat|")
#endif
    /* set/unset key? */
    CMD3( "global-set-key", "f4",
          "Register a global key binding",
          do_set_key, ESssi,
          "s{Set key globally: }[key]"
          "s{command: }[command]|command|"
          "v", 0)
    CMD3( "local-set-key", "",
          "Register a key binding a given mode",
          do_set_key, ESssi,
          "s{Set key locally: }[key]"
          "s{command: }[command]|command|"
          "v", 1)

    /*---------------- Window handling ----------------*/

    /* should merge these functions */
    CMD0( "other-window", "C-x o",
          "Move the focus to another window",
          do_other_window)
    CMD0( "next-window", "C-x n",
          "Move the focus to the next window",
          do_other_window)
    CMD0( "previous-window", "C-x p",
          "Move the focus to the previous window",
          do_previous_window)
    CMD0( "window-swap-states", "C-x /",
          "Swap the states of the current and next windows",
          do_window_swap_states)
#ifndef CONFIG_TINY
    CMD1( "center-cursor", "M-C-l",
          "Center the window contents at point",
          do_center_cursor, 1)
    CMD1( "find-window-up", "C-x up",
          "Move the focus to the window above the current one",
          do_find_window, KEY_UP)
    CMD1( "find-window-down", "C-x down",
          "Move the focus to the window below the current one",
          do_find_window, KEY_DOWN)
    CMD1( "find-window-left", "C-x left",
          "Move the focus to the window to the left of the current one",
          do_find_window, KEY_LEFT)
    CMD1( "find-window-right", "C-x right",
          "Move the focus to the window to the right of the current one",
          do_find_window, KEY_RIGHT)
    CMD2( "scroll-left", "M-(",
          "Shift the window contents to the left",
          do_scroll_left_right, ESi, "q")
    CMD2( "scroll-right", "M-)",
          "Shift the window contents to the right",
          do_scroll_left_right, ESi, "p")
    CMD1( "preview-mode", "",
          "Enter preview mode: cursor movement keys cause window scrolling",
          do_preview_mode, 1)
#endif
    CMD1( "delete-window", "C-x 0",
          "Delete the current window",
          do_delete_window, 0)
    CMD1( "delete-other-windows", "C-x 1",
          "Delete all other windows",
          do_delete_other_windows, 0)
    CMD1( "delete-all-windows", "",
          "Delete all windows",
          do_delete_other_windows, 1)
    CMD1( "hide-window", "",
          "Hide the current window",
          do_hide_window, 1)
    CMD0( "delete-hidden-windows", "",
          "Delete the hidden windows",
          do_delete_hidden_windows)
    CMD3( "split-window-vertically", "C-x 2",
          "Split the current window top and bottom",
          do_split_window, ESii, "P" "v", SW_STACKED)
    CMD3( "split-window-horizontally", "C-x 3",
          "Split the current window side by side",
          do_split_window, ESii, "P" "v", SW_SIDE_BY_SIDE)
    CMD0( "toggle-full-screen", "C-c f",
          "Toggle full screen display (on graphics displays)",
          do_toggle_full_screen)
    CMD0( "toggle-mode-line", "C-c m",
          "Toggle mode-line display",
          do_toggle_mode_line)
#ifdef CONFIG_SESSION
    CMD2( "create-window", "",
          "Create a new window with a specified layout",
          do_create_window, ESss,
          "s{Filename: }[file]|file|"
          "s{Layout: }|layout|")
    CMD1( "save-session", "",
          "Save the current session in a .qesession file",
          do_save_session, 1)
#endif

    /*---------------- Help ----------------*/

    CMD2( "toggle-trace-mode", "C-h d",
          "Enable or disable trace mode: show the *Trace* buffer with debugging info",
          do_toggle_trace_mode, ESi, "P")
    CMD2( "set-trace-options", "C-h t",
         "Select the trace options: all, none, command, debug, emulate, shell, tty, pty",
          do_set_trace_options, ESs,
          "s{Trace options: }|trace|")
    CMD2( "describe-key-briefly", "C-h c, C-h k, f6",
          "Describe a key binding",
          do_describe_key_briefly, ESsi,
          "s{Describe key: }|keys|"
          "P")
    CMD0( "help-for-help", "C-h C-h, f1",
          "Show the qemacs help window",
          do_help_for_help)

    /*---------------- International ----------------*/

    CMD2( "set-buffer-file-coding-system", "C-x RET f, C-c c",
          "Set the buffer charset and encoding system",
          do_set_buffer_file_coding_system, ESs,
          "s{Charset: }[charset]|charset|")
    CMD2( "convert-buffer-file-coding-system", "",
          "Convert the buffer contents to a new charset and encoding system",
          do_convert_buffer_file_coding_system, ESs, "*"
          "s{Charset: }[charset]|charset|")
    CMD0( "toggle-bidir", "C-x RET b, C-c b",
          "",
          do_toggle_bidir)
    CMD2( "set-input-method", "C-x RET C-\\, C-c C-\\",
          "",
          do_set_input_method, ESs,
          "s{Input method: }[input]")
    CMD0( "switch-input-method", "C-x C-\\",
          "",
          do_switch_input_method)

    /*---------------- Styles & display ----------------*/

    CMD2( "define-color", "",
          "Define a named color",
          do_define_color, ESss,
          "s{Color name: }[color]|color|"
          "s{Color value: }[color]|color|")
    CMD2( "set-style", "",
          "Set a property for a named style",
          do_set_style, ESsss,
          "s{Style: }[style]|style|"
          "s{CSS Property Name: }[style-property]|style-property|"
          "s{CSS Property Value: }|value|")
    CMD2( "set-display-size", "",
          "Set the dimensions of the graphics screen",
          do_set_display_size, ESii,
          "n{Width: }|width|"
          "n{Height: }|height|")
    CMD2( "set-system-font", "",
          "Set the system font",
          do_set_system_font, ESss,
          "s{Font family: }|fontfamily|"
          "s{System fonts: }|fontnames|")
    CMD2( "set-window-style", "",
          "",
          do_set_window_style, ESs,
          "s{Style: }[style]|style|")

    /*---------------- Miscellaneous ----------------*/

    CMD2( "exit-qemacs", "C-x C-c",
          "Exit Quick Emacs",
          do_exit_qemacs, ESi, "P")
    CMD0( "refresh", "C-l",
          "Refresh the display, center the window contents at point",
          do_refresh_complete)
    CMD2( "repeat", "C-x z",
          "Repeat last command with same prefix argument",
          do_repeat, ESi, "p")
    CMD0( "undo", "C-x u, C-_, f9",
          "Undo the last change",
          do_undo)
    CMD0( "redo", "C-x r, C-x C-_, f10",
          "Redo the last change undone",
          do_redo)

    CMD3( "goto-line", "M-g g, M-g M-g, C-x g",
          "Go to a line number",
          do_goto, ESsi,
          "s{Goto line: }"
          "v", 'l')
    CMD3( "goto-char", "M-g c",
          "Go to a character number",
          do_goto, ESsi,
          "s{Goto char: }"
          "v", 'c')
    CMD0( "count-lines", "C-x l",
          "Count the lines in the current buffer and region",
          do_count_lines)
    CMD0( "what-cursor-position", "C-x =",
          "Show the current point and mark positions",
          do_what_cursor_position)

    /* non standard mappings */
    CMD2( "line-number-mode", "",
          "Control the display of line numbers in mode lines",
          do_line_number_mode, ESi, "P")
    CMD2( "column-number-mode", "",
          "Control the display of column numbers in mode lines",
          do_column_number_mode, ESi, "P")
    CMD2( "global-linum-mode", "",
          "Control the display of line numbers in the left gutter for all buffers",
          do_global_linum_mode, ESi, "P")
    CMD2( "linum-mode", "C-x RET l, C-c l",
          "Control the display of line numbers in the left gutter for the current buffer",
          do_linum_mode, ESi, "P")
    CMD2( "toggle-line-numbers", "",    /* for compatibility with previous versions */
          "Toggle the line number display",
          do_linum_mode, ESi, "P")
    CMD0( "toggle-truncate-lines", "C-x RET t, C-c t",
          "Toggle displaying long lines on multiple screen rows",
          do_toggle_truncate_lines)
    CMD0( "word-wrap", "C-x RET w, C-c w",
          "Toggle wrapping on a character or word basis",
          do_word_wrap)
    CMD1( "toggle-control-h", "",
          "Toggle backspace / DEL handling",
          do_toggle_control_h, 0)
    CMD2( "set-emulation", "",
          "Select emacs flavor emulation",
          do_set_emulation, ESs,
          "s{Emulation mode: }|emulation|")
    CMD2( "cd", "f7",
          "Change the current directory of the qemacs process",
          do_cd, ESs,
          "s{Change default directory: }[dir]|file|")
    CMD2( "set-mode", "",
          "Set an editing mode",
          do_set_mode, ESs,
          "s{Set mode: }[mode]")
    CMD1( "set-auto-coding", "",
          "",
          do_set_auto_coding, 1)
    CMD1( "set-auto-mode", "",
          "Select the best mode",
          do_set_next_mode, 0)
    CMD2( "set-next-mode", "M-m",
          "Select the next mode appropriate for the current buffer",
          do_set_next_mode, ESi, "p")
    CMD2( "set-previous-mode", "",
          "Select the previous mode appropriate for the current buffer",
          do_set_next_mode, ESi, "q")

    /* tab & indent */
    CMD2( "set-tab-width", "",
          "Set the TAB width for the current buffer",
          do_set_tab_width, ESi,
          "N{Tab width: }")
    CMD2( "set-indent-width", "",
          "Set the indentation width for the current window",
          do_set_indent_width, ESi,
          "N{Indent width: }")
    CMD2( "set-indent-tabs-mode", "",
          "Select whether to use TABs or spaces for indentation",
          do_set_indent_tabs_mode, ESi,
          "N{Indent tabs mode (0 or 1): }")
    CMD2( "set-fill-column", "",
          "Set the width for paragraph filling",
          do_set_fill_column, ESi,
          "N{Fill column: }")

    /* other stuff */
    CMD3( "load-file-from-path", "C-c C-f",
          "Load a resource file from the QEPATH",
          do_load_file_from_path, ESsi,
          "s{Load file from path: }[resource]|file|"
          "v", 0)
    CMD2( "load-config-file", "",
          "Load a configuration file from the QEPATH",
          do_load_config_file, ESs,
          "s{Configuration file: }[resource]|file|")
    CMD2( "load-qerc", "",
          "Load a local .qerc settings file",
          do_load_qerc, ESs,
          "s{path: }[file]|file|")
    CMD2( "add-resource-path", "",
          "Add a path to the resource path list",
          do_add_resource_path, ESs,
          "s{resource path: }[dir]|file|")
};
