/*
 * default qemacs configuration 
 */
CmdDef basic_commands[] = {
    CMD1( KEY_DEFAULT, KEY_NONE, "self-insert-command", do_char, ' ')
    CMD0( KEY_CTRL('o'), KEY_NONE, "open-line", do_open_line)
    CMD1( KEY_CTRL('p'), KEY_UP, "previous-line", do_up_down, -1 )
    CMD1( KEY_CTRL('n'), KEY_DOWN, "next-line", do_up_down, 1 )
    CMD1( KEY_CTRL('b'), KEY_LEFT, "backward-char", do_left_right, -1 )
    CMD1( KEY_CTRL('f'), KEY_RIGHT, "forward-char", do_left_right, 1 )
    CMD1( KEY_META('b'), KEY_CTRL_LEFT, "backward-word", do_word_right, -1 )
    CMD1( KEY_META('f'), KEY_CTRL_RIGHT, "forward-word", do_word_right, 1 )
    CMD1( KEY_META('v'), KEY_PAGEUP, "scroll-down", do_scroll_up_down, -1 )
    CMD1( KEY_CTRL('v'), KEY_PAGEDOWN, "scroll-up", do_scroll_up_down, 1 )
    CMD0( KEY_HOME, KEY_CTRL('a'), "beginning-of-line", do_bol)
    CMD0( KEY_END, KEY_CTRL('e'), "end-of-line", do_eol)
    CMD0( KEY_ESC1(2), KEY_NONE, "overwrite-mode", do_insert)
    CMD0( KEY_CTRL('d'), KEY_DELETE, "delete-char", do_delete_char)
    CMD0( 127, KEY_NONE, "backward-delete-char", do_backspace)
    CMD1( KEY_META(KEY_BACKSPACE) , KEY_NONE, 
          "backward-delete-word", do_delete_word, -1)
    CMD1( KEY_META('d') , KEY_NONE, "delete-word", do_delete_word, 1)
    CMD1( KEY_CTRL('k'), KEY_NONE, "kill-line", do_kill_region, 2 )
    CMD0( KEY_CTRL('@'), KEY_NONE, "set-mark-command", do_set_mark )
    CMD1( KEY_CTRL('w'), KEY_NONE, "kill-region", do_kill_region, 1 )
    CMD1( KEY_META('w'), KEY_NONE, "copy-region", do_kill_region, 0 )
    CMD0( KEY_META('<'), KEY_CTRL_HOME, "beginning-of-buffer", do_bof )
    CMD0( KEY_META('>'), KEY_CTRL_END, "end-of-buffer", do_eof )
    CMD( KEY_META('x'), KEY_NONE, "execute-extended-command\0s{Command: }[command]|command|i", do_execute_command )
    CMD0( KEY_CTRL('u'), KEY_NONE, "universal-argument", do_universal_argument )
    CMD0( KEY_CTRL('y'), KEY_NONE, "yank", do_yank)
    CMD0( KEY_META('y'), KEY_NONE, "yank-pop", do_yank_pop)
    CMD0( KEY_CTRL('i'), KEY_NONE, "tabulate", do_tab)
    CMD0( KEY_CTRL('q'), KEY_NONE, "quoted-insert", do_quote)
    CMD1( KEY_CTRLX(KEY_CTRL('s')), KEY_NONE, "save-buffer", do_save, 0 )
    CMD1( KEY_CTRLX(KEY_CTRL('w')), KEY_NONE, "write-file", do_save, 1 )
    CMD0( KEY_CTRLX(KEY_CTRL('c')), KEY_NONE, "suspend-emacs", do_quit )
    CMD( KEY_CTRLX(KEY_CTRL('f')), KEY_NONE, "find-file\0s{Find file: }[file]|file|", do_load)
    CMD( KEY_CTRLX(KEY_CTRL('v')), KEY_NONE, "find-alternate-file\0s{Find alternate file: }[file]|file|", 
         do_find_alternate_file)
    CMD( KEY_CTRLX('b'), KEY_NONE,
         "switch-to-buffer\0s{Switch to buffer: }[buffer]|buffer|", do_switch_to_buffer)
    CMD( KEY_CTRLX('k'), KEY_NONE, 
         "kill-buffer\0s{Kill buffer: }[buffer]|buffer|", do_kill_buffer)
    CMD( KEY_CTRLX('i'), KEY_NONE, "insert-file\0s{Insert file: }[file]|file|", 
         do_insert_file)
    CMD0( KEY_CTRL('g'), KEY_NONE, "abort", do_break)
    CMD0( KEY_NONE, KEY_NONE, "doctor", do_doctor)
    CMD1( KEY_CTRL('s'), KEY_NONE, "isearch-forward", do_isearch, 1 )
    CMD1( KEY_CTRL('r'), KEY_NONE, "isearch-backward", do_isearch, -1 )
    CMD( KEY_META('%'), KEY_NONE, "query-replace\0s{Query replace: }|search|s{With: }|replace|", do_query_replace )
    CMD0( KEY_CTRLX('u'), KEY_CTRL('_'), "undo", do_undo)
    CMD0( KEY_RET, KEY_NONE, "newline", do_return)
    CMD0( KEY_CTRL('l'), KEY_NONE, "refresh", do_refresh)
    CMD( KEY_META('g'), KEY_NONE, "goto-line\0i{Goto line: }", do_goto_line)
    CMDi( KEY_NONE, KEY_NONE, "goto-char\0i{Goto char: }", do_goto_char)
    CMD( KEY_NONE, KEY_NONE, "global-set-key\0s{Set key globally: }s{command: }[command]|command|", do_global_set_key)
    CMD0( KEY_CTRLX(KEY_CTRL('q')), KEY_NONE, "vc-toggle-read-only", 
          do_toggle_read_only)
    CMD0( KEY_META('q'), KEY_NONE, "fill-paragraph", do_fill_paragraph)
    CMD0( KEY_META('{'), KEY_NONE, "backward-paragraph", do_backward_paragraph)
    CMD0( KEY_META('}'), KEY_NONE, "forward-paragraph", do_forward_paragraph)
    CMD0( KEY_CTRLX(KEY_CTRL('x')), KEY_NONE, "exchange-point-and-mark", do_exchange_point_and_mark)
    CMD1( KEY_META('l'), KEY_NONE, "downcase-word", do_changecase_word, 0)
    CMD1( KEY_META('u'), KEY_NONE, "upcase-word", do_changecase_word, 1)
    CMD1( KEY_CTRLX(KEY_CTRL('l')), KEY_NONE, "downcase-region", 
          do_changecase_region, 0)
    CMD1( KEY_CTRLX(KEY_CTRL('u')), KEY_NONE, "upcase-region", 
          do_changecase_region, 1)

    /* keyboard macros */
    CMD0( KEY_CTRLX('('), KEY_NONE, "start-kbd-macro", do_start_macro)
    CMD0( KEY_CTRLX(')'), KEY_NONE, "end-kbd-macro", do_end_macro)
    CMD0( KEY_CTRLX('e'), KEY_NONE, "call-last-kbd-macro", do_call_macro)

    /* window handling */
    CMD0( KEY_CTRLX('o'), KEY_NONE, "other-window", do_other_window)
    CMD1( KEY_CTRLX('0'), KEY_NONE, "delete-window", do_delete_window, 0)
    CMD0( KEY_CTRLX('1'), KEY_NONE, "delete-other-windows", do_delete_other_windows)
    CMD1( KEY_CTRLX('2'), KEY_NONE, "split-window-vertically", do_split_window, 0)
    CMD1( KEY_CTRLX('3'), KEY_NONE, "split-window-horizontally", do_split_window, 1)
    
    /* help */
    CMD0( KEY_CTRLH(KEY_CTRL('h')), KEY_F1, "help-for-help", do_help_for_help)
    CMD0( KEY_CTRLH('b'), KEY_NONE, "describe-bindings", 
          do_describe_bindings)
    CMD0( KEY_CTRLH('c'), KEY_NONE, "describe-key-briefly", 
          do_describe_key_briefly)

    /* international */
    CMD( KEY_CTRLXRET('f'), KEY_NONE, "set-buffer-file-coding-system\0s{Charset: }[charset]", 
         do_set_buffer_file_coding_system)
    CMD( KEY_NONE, KEY_NONE, "convert-buffer-file-coding-system\0s{Charset: }[charset]",
         do_convert_buffer_file_coding_system)
    CMD0( KEY_CTRLXRET('b'), KEY_NONE, "toggle-bidir", do_toggle_bidir)
    CMD( KEY_CTRLXRET(KEY_CTRL('\\')), KEY_NONE, 
         "set-input-method\0s{Input method: }[input]", do_set_input_method)
    CMD0( KEY_CTRL('\\'), KEY_NONE, 
          "switch-input-method", do_switch_input_method)

    /* styles & display */
    CMD( KEY_NONE, KEY_NONE, "set-style\0s{Style: }[style]|style|s{CSS Property Name: }s{CSS Property Value: }", do_set_style)
    CMD( KEY_NONE, KEY_NONE, "set-display-size\0i{Width: }i{Height: }", do_set_display_size)
    CMD( KEY_NONE, KEY_NONE, "set-system-font\0ss", do_set_system_font)
    CMD0( KEY_CTRLX('f'), KEY_NONE, "toggle-full-screen", do_toggle_full_screen)
    CMD0( KEY_NONE, KEY_NONE, "toggle-mode-line", do_toggle_mode_line)
    
    /* non standard mappings */
    CMD0( KEY_CTRLXRET('l'), KEY_NONE, "toggle-line-numbers", do_line_numbers)
    CMD0( KEY_CTRLXRET('t'), KEY_NONE, "truncate-lines", do_line_truncate )
    CMD0( KEY_CTRLXRET('w'), KEY_NONE, "word-wrap", do_word_wrap)
    
    /* tab & indent */
    CMD( KEY_NONE, KEY_NONE, "set-tab-width\0i{Tab width: }", do_set_tab_width)
    CMD( KEY_NONE, KEY_NONE, "set-indent-width\0i{Indent width: }", 
         do_set_indent_width)
    CMD( KEY_NONE, KEY_NONE, "set-indent-tabs-mode\0i{Indent tabs mode (0 or 1): }",
         do_set_indent_tabs_mode)
    CMD_DEF_END,
};

CmdDef minibuffer_commands[] = {
    CMD1( KEY_RET, KEY_NONE, "minibuffer-exit", do_minibuffer_exit, 0)
    CMD1( KEY_CTRL('g'), KEY_NONE, "minibuffer-abort", do_minibuffer_exit, 1)
    CMD0( KEY_CTRL('i'), KEY_NONE, "minibuffer-complete", do_completion)
    CMD0( ' ', KEY_NONE, "minibuffer-complete-space", do_completion_space)
    CMD1( KEY_CTRL('p'), KEY_UP, "previous-history-element", do_history, -1 )
    CMD1( KEY_CTRL('n'), KEY_DOWN, "next-history-element", do_history, 1 )
    CMD_DEF_END,
};

CmdDef less_commands[] = {
    CMD0( 'q', KEY_CTRL('g'), "less-exit", do_less_quit)
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
