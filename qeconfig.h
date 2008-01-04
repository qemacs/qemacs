/*
 * QEmacs, tiny but powerful multimode editor
 *
 * Copyright (c) 2000,2001 Fabrice Bellard.
 * Copyright (c) 2002-2007 Charlie Gordon
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
static CmdDef basic_commands[] = {

    /*---------------- Simple commands ----------------*/

    CMDV( KEY_DEFAULT, KEY_NONE, "self-insert-command", do_char, ESii, ' ',
          "*" "v" "ui")
    CMD_( KEY_META('#'), KEY_NONE, "insert-char", do_char, ESii,
          "*" "i{Insert char: }" "ui")
    CMD1( KEY_CTRL('p'), KEY_UP, "previous-line", do_up_down, -1 )
    CMD1( KEY_CTRL('n'), KEY_DOWN, "next-line", do_up_down, 1 )
    CMD1( KEY_CTRL('b'), KEY_LEFT, "backward-char", do_left_right, -1 )
    CMD1( KEY_CTRL('f'), KEY_RIGHT, "forward-char", do_left_right, 1 )
    CMD1( KEY_META('b'), KEY_CTRL_LEFT, "backward-word", do_word_right, -1 )
    CMD1( KEY_META('f'), KEY_CTRL_RIGHT, "forward-word", do_word_right, 1 )
    CMD1( KEY_META('v'), KEY_PAGEUP, "scroll-down", do_scroll_up_down, -2 ) /* u? */
    CMD1( KEY_CTRL('v'), KEY_PAGEDOWN, "scroll-up", do_scroll_up_down, 2 ) /* u? */
    CMD1( KEY_META('z'), KEY_NONE, "scroll-down-one", do_scroll_up_down, -1 ) /* u? */
    CMD1( KEY_CTRL('z'), KEY_NONE, "scroll-up-one", do_scroll_up_down, 1 ) /* u? */
    CMD0( KEY_CTRL('a'), KEY_HOME, "beginning-of-line", do_bol)
    CMD0( KEY_CTRL('e'), KEY_END, "end-of-line", do_eol)
    CMD0( KEY_INSERT, KEY_NONE, "overwrite-mode", do_insert)
    /* deletion commands should be allowed in read only buffers,
     * they should merely copy the data to the kill ring */
    CMD_( KEY_CTRL('d'), KEY_DELETE, "delete-char", do_delete_char, ESi, "*ui")
    CMD_( 127, KEY_NONE, "backward-delete-char", do_backspace, ESi, "*ui")
    CMD0( KEY_META('<'), KEY_CTRL_HOME, "beginning-of-buffer", do_bof )
    CMD0( KEY_META('>'), KEY_CTRL_END, "end-of-buffer", do_eof )
    /* do_tab will not change read only buffer */
    CMD_( KEY_CTRL('i'), KEY_NONE, "tabulate", do_tab, ESi, "ui")
    //CMD_( KEY_SPC, KEY_NONE, "space", do_space, "*ui")
    CMD_( KEY_CTRL('q'), KEY_NONE, "quoted-insert", do_quote, ESi, "*ui")
    CMDV( KEY_CTRL('j'), KEY_RET, "newline", do_return, ESi, 1, "*v")
    CMDV( KEY_CTRL('o'), KEY_NONE, "open-line", do_return, ESi, 0, "*v")

    /*---------------- Region handling / Kill commands ----------------*/

    CMD0( KEY_CTRL('@'), KEY_NONE, "set-mark-command", do_set_mark )
    CMD0( KEY_CTRLX(KEY_CTRL('x')), KEY_NONE, "exchange-point-and-mark",
          do_exchange_point_and_mark)
    CMD0( KEY_CTRLX('h'), KEY_NONE, "mark-whole-buffer", do_mark_whole_buffer)
    CMD0( KEY_META(KEY_CTRL('w')) , KEY_NONE,
          "append-next-kill", do_append_next_kill)
    CMDV( KEY_CTRL('k'), KEY_NONE, "kill-line", do_kill_line, ESi, 1, "*v" )
    CMDV( KEY_NONE, KEY_NONE,
          "kill-beginning-of-line", do_kill_line, ESi, -1, "*v" )
    CMDV( KEY_META(KEY_DEL) , KEY_META(KEY_BS),
          "backward-kill-word", do_kill_word, ESi, -1, "*v" )
    CMDV( KEY_META('d') , KEY_NONE, "kill-word", do_kill_word, ESi, 1, "*v" )
    CMDV( KEY_CTRL('w'), KEY_NONE, "kill-region", do_kill_region, ESi, 1, "*v" )
    CMD1( KEY_META('w'), KEY_NONE, "copy-region", do_kill_region, 0 )
    CMD_( KEY_CTRL('y'), KEY_NONE, "yank", do_yank, ES, "*")
    CMD_( KEY_META('y'), KEY_NONE, "yank-pop", do_yank_pop, ES, "*")

    /*---------------- Buffer and file handling ----------------*/

    CMD_( KEY_CTRLX(KEY_CTRL('f')), KEY_NONE, "find-file", do_find_file, ESs,
          "s{Find file: }[file]|file|") /* u? */
    CMD_( KEY_CTRLX(KEY_CTRL('v')), KEY_NONE, "find-alternate-file",
          do_find_alternate_file, ESs,
          "s{Find alternate file: }[file]|file|") /* u? */
    CMD_( KEY_CTRLX('i'), KEY_NONE, "insert-file", do_insert_file, ESs,
          "*s{Insert file: }[file]|file|") /* u? */
    CMD0( KEY_CTRLX(KEY_CTRL('s')), KEY_NONE, "save-buffer", do_save_buffer) /* u? */
    CMD_( KEY_CTRLX(KEY_CTRL('w')), KEY_NONE, "write-file", do_write_file, ESs,
          "s{Write file: }[file]|file|") /* u? */
    CMD_( KEY_CTRLX('w'), KEY_NONE, "write-region", do_write_region, ESs,
          "s{Write region to file: }[file]|file|") /* u? */
    CMD_( KEY_CTRLX('b'), KEY_NONE, "switch-to-buffer", do_switch_to_buffer, ESs,
          "s{Switch to buffer: }[buffer]|buffer|")
    CMD_( KEY_CTRLX('k'), KEY_NONE, "kill-buffer", do_kill_buffer, ESs,
          "s{Kill buffer: }[buffer]|buffer|")
    CMD0( KEY_CTRLX(KEY_CTRL('q')), KEY_NONE, "toggle-read-only",
          do_toggle_read_only)
    CMD_( KEY_META('~'), KEY_NONE, "not-modified", do_not_modified, ESi, "ui")
    CMD_( KEY_NONE, KEY_NONE, "set-visited-file-name",
          do_set_visited_file_name, ESss,
	  "s{Set visited file name: }[file]|file|"
	  "s{Rename file? }")

    /*---------------- Search and replace ----------------*/

    CMDV( KEY_META('S'), KEY_NONE, "search-forward", do_search_string, ESsi, 1,
	  "s{Search forward: }|search|"
	  "v")
    CMDV( KEY_META('R'), KEY_NONE, "search-backward", do_search_string, ESsi, -1,
	  "s{Search backward: }|search|"
	  "v")
    /* passing argument should switch to regex incremental search */
    CMD1( KEY_CTRL('r'), KEY_NONE, "isearch-backward", do_isearch, -1 )
    CMD1( KEY_CTRL('s'), KEY_NONE, "isearch-forward", do_isearch, 1 )
    CMD_( KEY_META('%'), KEY_NONE, "query-replace", do_query_replace, ESss,
	  "*" "s{Query replace: }|search|"
	  "s{With: }|replace|")
    /* passing argument restricts replace to word matches */
    CMD_( KEY_META('r'), KEY_NONE, "replace-string", do_replace_string, ESssi,
	  "*" "s{Replace String: }|search|"
	  "s{With: }|replace|"
	  "ui")

    /*---------------- Paragraph / case handling ----------------*/

    CMD0( KEY_META('{'), KEY_NONE, "backward-paragraph", do_backward_paragraph)
    CMD0( KEY_META('}'), KEY_NONE, "forward-paragraph", do_forward_paragraph)
    CMD_( KEY_META('q'), KEY_NONE, "fill-paragraph", do_fill_paragraph, ES, "*")
    CMDV( KEY_NONE, KEY_NONE, "kill-paragraph", do_kill_paragraph, ESi, 1, "*v")

    CMDV( KEY_META('c'), KEY_NONE, "capitalize-word", do_changecase_word, ESi, 2, "*v")
    CMDV( KEY_META('l'), KEY_NONE, "downcase-word", do_changecase_word, ESi, -1, "*v")
    CMDV( KEY_META('u'), KEY_NONE, "upcase-word", do_changecase_word, ESi, 1, "*v")
    CMDV( KEY_META(KEY_CTRL('c')), KEY_NONE,
          "capitalize-region", do_changecase_region, ESi, 2, "*v")
    CMDV( KEY_CTRLX(KEY_CTRL('l')), KEY_NONE,
          "downcase-region", do_changecase_region, ESi, -1, "*v")
    CMDV( KEY_CTRLX(KEY_CTRL('u')), KEY_NONE,
          "upcase-region", do_changecase_region, ESi, 1, "*v")

    /*---------------- Command handling ----------------*/

    CMD_( KEY_META('x'), KEY_NONE, "execute-command", do_execute_command, ESsi,
	  "s{Command: }[command]|command|"
	  "ui")
    /* M-0 thru M-9 should start universal argument */
    CMD0( KEY_CTRL('u'), KEY_META('-'), "universal-argument",
          do_universal_argument)
    CMD0( KEY_CTRL('g'), KEY_CTRLX(KEY_CTRL('g')), "abort", do_break)
    CMD0( KEY_CTRLX('('), KEY_NONE, "start-kbd-macro", do_start_macro)
    CMD0( KEY_CTRLX(')'), KEY_NONE, "end-kbd-macro", do_end_macro)
    CMD0( KEY_CTRLX('e'), KEY_CTRL('\\'), "call-last-kbd-macro", do_call_macro)
    CMD_( KEY_NONE, KEY_NONE, "define-kbd-macro", do_define_kbd_macro, ESsss,
	  "s{Macro name: }[command]"
	  "s{Macro keys: }"
	  "s{Bind to key: }[key]")
    /* set/unset key? */
    CMDV( KEY_NONE, KEY_NONE, "global-set-key", do_set_key, ESssi, 0,
          "s{Set key globally: }[key]"
	  "s{command: }[command]|command|"
	  "v")
    CMDV( KEY_NONE, KEY_NONE, "local-set-key", do_set_key, ESssi, 1,
          "s{Set key locally: }[key]"
	  "s{command: }[command]|command|"
	  "v")

    /*---------------- Window handling ----------------*/

    /* should merge these functions */
    CMD0( KEY_CTRLX('o'), KEY_NONE, "other-window", do_other_window)
    CMD0( KEY_CTRLX('n'), KEY_NONE, "next-window", do_other_window)
    CMD0( KEY_CTRLX('p'), KEY_NONE, "previous-window", do_previous_window)
#ifndef CONFIG_TINY
    CMD0( KEY_META(KEY_CTRL('l')), KEY_NONE, "center-cursor", do_center_cursor)
    CMD1( KEY_CTRL('x'), KEY_UP, "find-window-up", do_find_window,
          KEY_UP)
    CMD1( KEY_CTRL('x'), KEY_DOWN, "find-window-down", do_find_window,
          KEY_DOWN)
    CMD1( KEY_CTRL('x'), KEY_LEFT, "find-window-left", do_find_window,
          KEY_LEFT)
    CMD1( KEY_CTRL('x'), KEY_RIGHT, "find-window-right", do_find_window,
          KEY_RIGHT)
#endif
    CMD1( KEY_CTRLX('0'), KEY_NONE, "delete-window", do_delete_window, 0)
    CMD0( KEY_CTRLX('1'), KEY_NONE, "delete-other-windows",
          do_delete_other_windows)
    CMD1( KEY_CTRLX('2'), KEY_NONE, "split-window-vertically",
          do_split_window, 0) /* u? */
    CMD1( KEY_CTRLX('3'), KEY_NONE, "split-window-horizontally",
          do_split_window, 1) /* u? */
    CMD0( KEY_CTRLX('f'), KEY_NONE, "toggle-full-screen",
          do_toggle_full_screen)
    CMD0( KEY_NONE, KEY_NONE, "toggle-mode-line", do_toggle_mode_line)

    /*---------------- Help ----------------*/

    CMD0( KEY_CTRLH(KEY_CTRL('h')), KEY_F1, "help-for-help", do_help_for_help)
    CMD0( KEY_CTRLH('b'), KEY_NONE, "describe-bindings", do_describe_bindings)
    CMD0( KEY_CTRLH('c'), KEY_CTRLH('k'), "describe-key-briefly",
          do_describe_key_briefly)

    /*---------------- International ----------------*/

    CMD_( KEY_CTRLXRET('f'), KEY_NONE, "set-buffer-file-coding-system",
          do_set_buffer_file_coding_system, ESs,
          "s{Charset: }[charset]")
    CMD_( KEY_NONE, KEY_NONE, "convert-buffer-file-coding-system",
          do_convert_buffer_file_coding_system, ESs,
          "*" "s{Charset: }[charset]")
    CMD0( KEY_CTRLXRET('b'), KEY_NONE, "toggle-bidir", do_toggle_bidir)
    CMD_( KEY_CTRLXRET(KEY_CTRL('\\')), KEY_NONE, "set-input-method",
          do_set_input_method, ESs,
          "s{Input method: }[input]")
    CMD0( KEY_CTRLX(KEY_CTRL('\\')), KEY_NONE,
          "switch-input-method", do_switch_input_method)

    /*---------------- Styles & display ----------------*/
    CMD_( KEY_NONE, KEY_NONE, "define-color", do_define_color, ESss,
	  "s{Color name: }[color]|color|"
	  "s{Color value: }[color]|color|")
    CMD_( KEY_NONE, KEY_NONE, "set-style", do_set_style, ESsss,
          "s{Style: }[style]|style|"
          "s{CSS Property Name: }"
          "s{CSS Property Value: }")
    CMD_( KEY_NONE, KEY_NONE, "set-display-size", do_set_display_size, ESii,
	  "i{Width: }"
	  "i{Height: }")
    CMD_( KEY_NONE, KEY_NONE, "set-system-font", do_set_system_font, ESss,
	  "s{Font family: }"
	  "s{System fonts: }")

    /*---------------- Miscellaneous ----------------*/

    CMD_( KEY_CTRLX(KEY_CTRL('c')), KEY_NONE, "exit-qemacs",
          do_exit_qemacs, ESi, "ui")
    CMD0( KEY_CTRL('l'), KEY_NONE, "refresh", do_refresh_complete)
    CMD0( KEY_NONE, KEY_NONE, "doctor", do_doctor)
    CMD0( KEY_CTRLX('u'), KEY_CTRL('_'), "undo", do_undo)
    CMDV( KEY_META('g'), KEY_NONE, "goto-line", do_goto, ESsi, 'l',
          "us{Goto line: }" "v")
    CMDV( KEY_CTRLX('g'), KEY_NONE, "goto-char", do_goto, ESsi, 'c',
          "us{Goto char: }" "v")
    CMD0( KEY_CTRLX('l'), KEY_NONE, "count-lines", do_count_lines)
    CMD0( KEY_CTRLX('='), KEY_NONE, "what-cursor-position",
          do_what_cursor_position)

    /* non standard mappings */
    CMD0( KEY_CTRLXRET('l'), KEY_NONE, "toggle-line-numbers",
          do_toggle_line_numbers)
    CMD0( KEY_CTRLXRET('t'), KEY_NONE, "toggle-truncate-lines",
          do_toggle_truncate_lines)
    CMD0( KEY_CTRLXRET('w'), KEY_NONE, "word-wrap", do_word_wrap)
    CMD1( KEY_NONE, KEY_NONE, "toggle-control-h", do_toggle_control_h, 0)
    CMD_( KEY_NONE, KEY_NONE, "set-emulation", do_set_emulation, ESs,
          "s{Emulation mode: }")
    CMD0( KEY_NONE, KEY_NONE, "set-trace", do_set_trace)
    CMD_( KEY_NONE, KEY_NONE, "cd", do_cd, ESs,
          "s{Change default directory: }[file]|file|")
    CMD_( KEY_NONE, KEY_NONE, "set-mode", do_set_mode, ESs,
          "s{Set mode: }[mode]")

    /* tab & indent */
    CMD_( KEY_NONE, KEY_NONE, "set-tab-width", do_set_tab_width, ESi,
          "ui{Tab width: }")
    CMD_( KEY_NONE, KEY_NONE, "set-indent-width", do_set_indent_width, ESi,
          "ui{Indent width: }")
    CMD_( KEY_NONE, KEY_NONE, "set-indent-tabs-mode", do_set_indent_tabs_mode, ESi,
          "ui{Indent tabs mode (0 or 1): }")

    /* other stuff */
    CMD_( KEY_NONE, KEY_NONE, "load-file-from-path", do_load_file_from_path, ESs,
          "s{Load file from path: }|file|")
    CMD_( KEY_NONE, KEY_NONE, "load-config-file", do_load_config_file, ESs,
          "s{Configuration file: }[file]|file|")
    CMD_( KEY_NONE, KEY_NONE, "load-qerc", do_load_qerc, ESs,
          "s{path: }[file]|file|")

    CMD_DEF_END,
};

CmdDef minibuffer_commands[] = {
    CMD1( KEY_RET, KEY_NONE, "minibuffer-exit", do_minibuffer_exit, 0)
    CMD1( KEY_CTRL('g'), KEY_NONE, "minibuffer-abort", do_minibuffer_exit, 1)
    CMD0( KEY_CTRL('i'), KEY_NONE, "minibuffer-complete", do_completion)
    /* should take numeric prefix to specify word size */
    CMD0( KEY_META('='), KEY_NONE, "minibuffer-get-binary", do_minibuffer_get_binary)
    CMD0( ' ', KEY_NONE, "minibuffer-complete-space", do_completion_space)
    CMD1( KEY_CTRL('p'), KEY_UP, "previous-history-element", do_history, -1 )
    CMD1( KEY_CTRL('n'), KEY_DOWN, "next-history-element", do_history, 1 )
    CMD_DEF_END,
};

CmdDef less_commands[] = {
    CMD0( 'q', KEY_CTRL('g'), "less-exit", do_less_exit)
    CMD1( '/', KEY_NONE, "less-isearch", do_isearch, 1)
    CMD_DEF_END,
};


QEStyleDef qe_styles[QE_STYLE_NB] = {

#define STYLE_DEF(constant, name, fg_color, bg_color, \
                  font_style, font_size) \
{ name, fg_color, bg_color, font_style, font_size },

#include "qestyles.h"

#undef STYLE_DEF
};
