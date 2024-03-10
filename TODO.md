<!-- TODO list for qemacs -- Author: Charles Gordon -- Updated: 2023-02-21 -->

# QEmacs TODO list

## Documentation / Support

* improve **README.md**
* rework documentation:
 - document existing features
 - make documentation available inside qemacs
* move documentation to markdown
* rewrite **TODO.md** file with more sections and explanations
* migrate **coding-rules.html** to markdown
* use cooked markdown mode for help
* add command `qemacs-hello` on `C-h h` : load hello file for charset tests
* add command `qemacs-faq` on `C-h C-f`
* add command `qemacs-manual` on `C-h m`
* help: `data-directory`, `data-path` ?
* add command documentation in `describe-function`
* add command documentation in `describe-variable`
* show memory stats in `describe-buffer` and `about-qemacs`
* add function to add entry in **TODO.md**
* move mailing list to github or private server

## Core / Buffer / Input

* `describe-key-briefly`, `local-set-key`, etc should use a special input mode to read a string of keys via the minibuffer to remove the `qe_key_process` hack and use the same input behavior as emacs.
* in command specs, distinguish between interactive commands and non interactive functions
* use tabulation context for `text_screen_width`
* add method pointers in windows initialized from fallback chain
* remove redundant bindings along fallback chains
* share mmapped pages correctly
* check abort during long operations: bufferize input and check for `^G`
* optional 64-bit offsets on 64-bit systems, use typedef for buffer offsets
* disable messages from commands if non-interactive (eg: `set-variable`)
* add custom memory handling functions.
* `qe_realloc`: typed and clear reallocated area
* use failsafe memory allocator and `longjmp` recover.
* move `ungot_key` to `key_context`
* splitting pages should fall on 32 bit boundaries (difficult)
* handle broken charset sequences across page boundaries
* allow recursive main loop, and remove input callbacks
* synced virtual buffers with restricted range
* unsynced virtual buffers with restricted range and specific mode/charset
* bfs: built in file system for embedded extensions and files
   Jasspa bfs is way too complicated, make simpler system
* notes
* tiny: remove extra features
* tiny: make a really small version
* use Cursors cursors in command dispatchers:
    ```c
        struct QECursor {
            QEmacsState *qs;
            EditState *s;   /* points to the parent window */
            EditBuffer *b;
            int offset;
            // optional navigation data to accelerate buffer access:
            // getc(), peekc(), prevc()... etc.
        };
    ```
  an EditState would have an embedded QECursor that contains the buffer and offset
  an EditBuffer could also have an embedded QECursor with no EditState
* use hash tables for command and variable names
* sort key binding tables?
* `next-buffer` command on C-x C-right
* `previous-buffer` command on C-x C-left
* `save-some-buffers` command on C-x s
* standardize key prefixes: C-, M-, S- and possibly others
* use C-M- prefix instead of M-C- in source and documentation but support
both in `strtokey`
* redefine `KEY_Fx` to make them sequential
* rationalize KEY_xxx definitions and modifier bits to support all combinations
* add registrable escape sequences and key names
  ex: S-f5 = ^[[15;2~
* add registrable key translations for NON ASCII input
  ex: C-x 8 3 / 4	¾

## Charsets / Unicode / Bidir

* add default charset for new buffer creation, set that to utf8
* better display of invalid UTF-8 encodings
* change character detection API to handle cross page spanning
* fix `eb_prev_char` to handle non self-synchronizing charsets
* handle chinese encodings
* handle euc-kr
* add JIS missing encoding functions
* add JIS charset probing functions
* autodetect sjis, euc-jp...
* fix kana input method
* update cp directory from more recent unicode tables
* UTF-8 variants: CESU-8, Modified UTF-8, UTF-16
* UTF-1 obsolete standard encoding for Unicode
* handle `tty-width` to compute alignement in dired, bufed...
* limit number of combining marks to 20
* use `unichar`, `rune` and/or `u8` types (using `char32_t` at the moment)
* detect bad encoding and use `errno` to tell caller
* auto/mixed eol mode
* `set-eol-type` should take a string: auto/binary/dos/unix/mac/0/1/2...
* display `^L` as horizontal line and consider as linebreak character
* handle zero width codepoints:
  cp="200B" na="ZERO WIDTH SPACE" alias="ZWSP"
  cp="200C" na="ZERO WIDTH NON-JOINER" alias="ZWNJ"
  cp="200D" na="ZERO WIDTH JOINER" alias="ZWJ"
  cp="200E" na="LEFT-TO-RIGHT MARK" alias="LRM"
  cp="200F" na="RIGHT-TO-LEFT MARK" alias="RLM"
* `set_input_method()` and `set_buffer_file_coding_system()` in config file.
* test Hebrew keymap support.
* rewrite bidirectional algorithm and support
* use Unicode file hierarchy for code page files
* handle or remove extra code page files:
  CP1006.TXT CP1253.TXT CP1254.TXT CP1255.TXT CP1258.TXT
  CP775.TXT CP855.TXT CP856.TXT CP857.TXT CP860.TXT CP861.TXT
  CP862.TXT CP863.TXT CP864.TXT CP865.TXT CP869.TXT CP874.TXT CP932.TXT
  JIS0201.TXT SHIFTJIS.TXT euc-jis-2004-std.txt iso-2022-jp-2004-std.txt
  jisx0213-2004-std.txt sjis-0213-2004-std.txt
  MAC-CYRILLIC.TXT MAC-GREEK.TXT MAC-ICELAND.TXT MAC-TURKISH.TXT
  koi8_ru.cp APL-ISO-IR-68.TXT GSM0338.TXT SGML.TXT
* deal with accents in filenames (macOS uses combining accents encoded as UTF-8)
* rename `eb_putc` as it handles the full char32_t range

## Windowing / Display

* `set-display-size` unit issue: use extra argument or different function for pixels and characters
* always save window buffer properties to buffer upon detaching
* add global system to select default values for some window states
* fix current position when changing buffer attached to window
* fix default wrap setting mess
* display: add screen dump command and format
* colorize extra `^M` and `^Z` as preproc at end of line prior to calling the syntax highlighter (same as BOM)
* colorizer bug on **/comp/projects/fractal/fractint/ORGFORM/NOEL-2.FRM** (triple `^M`)
* display bug on **~/comp/projects/fractal/fractint/ORGFORM/BAILOUT.FRM** (double `^M`)
* default `display-width` of 0 is automatic, other values are shared between binary and hex modes
* minibuffer and popup windows should be in a separate lists
* `toggle-full-screen` should not put modeline on popup
* `toggle-full-screen` should work on popups
* kill buffer should delete popup and popleft window
* layout: check coordinate system to 1000 based with optional sidebars
* improve layout scheme for better scalability.
* display: API: use style cache in `DisplayState`
* display: API: remove screen argument in `release_font`, `glyph_width`
* display: API: add `create-style(name, properties)`
* display: use true colors on capable terminals
* basic: `frame-title-format` and `mode-line-format`
* basic: `transient-mark-mode` to highlight the current region
* basic: `delete-selection-mode` to delete the highlighted region on DEL and typing text
* modes: `header-line` format
* modes: `mode-line` format
* display filename relative to current directory instead of buffer name on `mode-line`
* window scrolling not emulated in tty (check `^Z` in recursive eps)
* multiple frames
* lingering windows
* cursor not found on **doc/256colors.raw** if `truncate-lines=1`
* `enlarge-window-interactively`
* `enlarge-window-horizontally`
* `enlarge-window`
* tab cursor displayed size
* improve speed of text renderer / improve truncate mode
* merge some good parts with CSS renderer ?.
* Suppress CRC hack (not reliable).
* fix crash bug on fragments longer than `MAX_SCREEN_WIDTH`.
* vertical scroll bar
* menu / context-menu / toolbars / dialogs
* scrolling by window size should position cursor differently
* emulation mode to use line-drawing characters for window borders
* Clean window deletion mess:
  * avoid problems with popups (`kill_buffer`, `delete_window`)
  * detach window from tree and keep attached to buffer if last
  * detach window from tree and put in delayed free tree otherwise

```c
edit_close(s)
do_delete_window(s)
  bufed_select(s) if vertical split
  dired_select(s) if vertical split
do_popup_exit(s)
do_delete_other_windows(s) deletes other windows (!)
do_minibuffer_exit(s) also deletes completion_popup
insert_window_left()  deletes some left-most windows
  do_list_buffers()
  do_dired()
```

* wrap long lines past line numbers column
* fix column computation based on display properties:
  (variable pitch, tabs, ^x and \uxxxx stuff -- emacs behaviour) ?

## X11 display / graphics

* handle X11 window manager close window event and exit cleanly
* clip display by popup size
* move `-nw` cmd line option to **tty.c** and make `term_probe` return better score
* remember X11 window positions and restore layout?
* faster video handling (generalize invalidate region system)
* integrate tinySVG renderer based on the new libraster.
* implement wheel mode in CSS display.
* fix configure for missing support: x11 xv png ...
* add `configure --disable-graphics`
* `dpy_open_font` should never return `NULL`, must have a system font.

## Files

* [BUG] check file date to detect asynchronous modifications on disk
* reload modified file upon change if untouched since load
* add hook on file change
* handle files starting with re:
* check file permissions.
* handle filenames with embedded spaces
* use trick for entering spaces in filename prompts without completion
* fix `s->offset` reset to 0 upon `C-x C-f newfile ENT C-x 2 C-x b ENT`
* insert-file: load via separate buffer with charset conversion
* `qe_load_file` should split screen evenly for `LF_SPLIT_SCREEN` flag
* [Idea] save file to non existent path -> create path.
* [Idea] find-file: gist:snippet
* Missing commands:
  * `reload-file` on `C-x C-r`
  * `find-file-existing`
  * `find-other-frame` on `C-x 5 f`, `C-x 5 C-f`
  * `find-other-window` on `C-x 4 f`, `C-x 4 C-f`
  * `find-other-read-only` on `C-x C-r`
  * `find-file-read-only-other-frame` on `C-x 5 r`
  * `find-file-read-only-other-window` on `C-x 4 r`
  * `save-modified-buffers` on `C-x s`
  * `find-file`: should support scp syntax for remote loading
* avoid error in new file
* actually load file in `find-file-noselect`
* should update symbolic links times when saving files
* improve speed: `C-x C-f ~/x2m RET A-r 20140101 RET 20140101 RET` -> 96s
* improve speed: `C-x C-f ~/x2m RET C-u 1000 C-n` -> 4s
* use a prefix to explore file in a popup window

## Moving / Editing / Navigation

* pass argval and pagewise to `do_scroll_up_down()` or split command
* files: fix `SPC` / `TAB` distinct behaviors on **~/comp/project/gnachman/**
* basic: update default settings to `indent_tabs_mode = 0`, `indent_width = 4`, my-colors
* basic: add property lists in buffer and window for default directory and similar properties (override)
* basic: backspace delete hacking tabs
* basic: enter should optionally remove whitespace at end of line
* basic: reset last command when executing macro
* basic: make `do_word_right`, `do_word_left`, `do_bol`, `do_eol`... return new offset
* basic: use visual movement for left, right, up, down and character based for `C-b C-f C-n C-p`
* basic: fix offset when exiting `s->hex_mode`
* `do_transpose` should take argval and swap distant fragments
* `do_transpose` has invalid behavior if left or right chunk is empty
* new flavor for GoogleClosureCompiler
* emacs bindings:
  * `M-g M-g` `goto-line` (with prefix argument)
  * `M-g TAB` `move-to-column` (with prefix argument)
* Missing commands:
  * `what-cursor-position` with universal prefix: show popup with long description
  * `compare-windows` should resync from the end of line.
  * `elastic-tabs`
  * `indent-rigidly`
  * `show-matching-delimiters`
  * blink-and-insert on `) } ] >`
  * `set-gosmacs-bindings` -> `set_emulation("gosmacs")`
  * `auto-fill-mode`
  * `auto-revert-mode`, `global-auto-revert-mode`, `auto-revert-tail-mode`
  * `next-buffer` on `C-x C-right, C-x >, f12` Move to the next buffer.
  * `previous-buffer` on `C-x C-left, C-x <, f11` Move to the next buffer.
* `toggle-full-screen`-> unsupported if `screen->dpy_full_screen` is `NULL`
* remote editing
* fix scroll up/down to move point if already at end
* move by paragraph on `M-[` and `M-]`
* `fill-paragraph` should default indentation for the second and subsequent
    lines to that of the first line
* scroll horizontally on `M-{` and `M-}`
* scroll up/down with argument should scroll by screen row.
* simplify `C-z` and `A-z` accordingly
* rectangular regions, cut/paste
* multi-line editing

## Macros

* `start-keyboard-macro` should clear current macro if called recursively
* allow redefining the keyboard macro embedding itself
* `show-macro`, `dump-macro` to ease macro debugging and timing
* fix macro slowliness in 6000 line buffer **junk/dirs/x**
* do not use visual movement inside macros (definition and execution)
* do not redisplay during macro execution, esp with prefix
* check for abort/failure during repeated command execution and macros
* improve `dump-macro` to convert macro to string
* fix `dump-macro` to save/restore `last-kbd-macro` to/from session
* do not store messages during repeated macro execution or limit buffer size
* `name-last-kbd-macro`
* fix source syntax issues
```lisp
   (define-macro "last-kbd-macro" "A-fA-fC-FC-FC-@C-EA-wC-AC-NC-XnC-YC-MC-Xp")
   (define-macro "last-kbd-macro" "A\-f")
   ---   - - -
   (define-macro "last-kbd-macro" "---   - - -C-M
   <Down><Up><Right><Left>")
   <><>(define-macro "last-kbd-macro" "\<>\<>")
```

## Minibuffer / Completion

* minibuf: spurious recursive edit cases.
* completion: fix electric behavior
* completion: add completion function to get the default value
* basic: fix fuzzy completion when single unanchored match
* completion: minibuffer completion: bad return on `C-x C-f . SPC qe SPC RET`
* minibuf: use more mode specific bindings
* minibuf: `minibuffer-electric-yank` in minibuffer to fix pathname
* basic: completion in `load-resource-file`
* completion: abbreviate lines in file completion list popup
* open file with fuzzy completion
* execute command with fuzzy completion
* completion with fuzzy matching

## Searching

* store incremental search strings in search history list
* incremental search: increase max repeat count
* improve search speed finally
* add low-level search accelerator function
* `hex-mode` search strings should mix hex ut8 strings and ASCII control char names
* isearch in hex should display search string in hex
* transfer case in `query-replace`
* regex replace with subexpressions
* query replace across multiple files
* query replace options: u -> undo last change
* add non selectable query-replace-mode for key bindings, cmd description and documentation
* extra: `grep`, `grep-buffer`, `grep-sources`, `grep-tree`...
* query_replace functions should be restricted to the highlighted region if any
* search: `count-words`, `wc`, `count-sloc`
* search: stats command for word count and mode specific stats
* isearch: `M-p` and `M-n` should select previous and next search pattern in history
* searching failure should abort macros

## Undo

* allow file save when undo clears file modified flag
* undo some cursor movements
* group undo entries to match command and or macro boundaries
* saving the file kills the redo stack!
* when undo resets the modified buffer flag, this prevents file save
* store cursor movements in undo records
* kill redo list when forking after undo
* undo should handle sequence of undo records upto tagged start.
* add disable-undo for tests and benchmarking
* limit size of undo buffers (`undo-outer-limit`, default 3000000)
* add undo records for styles, modes...
* disable undo for archive parse and uncompress phases
* compress logs and limit size
* mode for undo buffers

## Config / Sessions / Scripting

* config: reparse **.qerc** upon change
* session: register session store functions
* session: save previous answers, ...
* session: save preview mode, buffer modes and properties
* session: save process buffers?  non file-based buffers?  dired buffers?
* `qe_get_variable` should populate a QEValue
* `qe_set_variable` should take a QEValue pointer
* variables: add minimal version in tqe
* variables: set variable values via function pointer
* variables: add boolean and char types
* tack library of ancillary files at the end of the binary:
  - add protocol to read from library (lib:xxx)
  - add html.min.css and docbook.min.css to library
  - append lib: to default QE_PATH
* clean up ancillary files
* [Idea] dynamic project based settings, include, exclude patterns...
* commands: `parse_arg()` should handle default argument values
* commands: pass raw prefix argument `P` as combination of has_arg (flags) and argval (number)
* `eval-expression` should evaluate region if highlighted?

## Colors and styles

* [BUG] preserve static styles in `do_convert_buffer_file_coding_system()`
* optimize style transfer in `eb_insert_buffer_convert()`
* add color themes
* fix colors, default schemes...
* add style recent changes (`highlight-recent-changes`)
* make styles and log buffers read-only and display as binary
* fix overlong line coloring
* make `style-buffer-mode` and `log-buffer-mode`

## Modes

### Generic mode stuff

* basic: register modes recursively along fallback chain
* modes: `next-mode` should include buffer `default_mode` in list
* tags: remove tags for modified line
* mode inheritance
* basic: stop copying `ModeDef` structures to implement *inheritance*
* basic: fix mode setting mess
* modes: split modes into colorizers, interaction, io, charset, eoltype
* modes: major and minor modes
* rethink mode specific commands -> add inheritance for all commands ?
* add `mode_commands` and `global_commands` tables in `ModeDef` structure

### C mode

* allman and other styles
* epita style
* auto remove trailing spaces and add final newline
* colorizing bug: `static int (*qe__initcall_first)(void) qe__init_call = NULL;`
* align multi line comments leading * one space to the right
* fix C indentation inside comments
* fix C indentation inside struct, array and enum initializers
* automatic indentation detection
* `c-indent`
* `indent-with-tabs`
* stats command for `slcc`
* improve tag support:
  * missed tag: `static int (*qe__initcall_first)(void) qe__init_call = NULL;`
  * tag multiple files and buffers
  * tag project files recursively
  * handle multiple tag files
  * save tags to QTAGS or .qetags file
  * update tag files automatically
  * `list-tags` and `list-matching-lines` popups should be clickable and go to the match
    use locus properties?
  * `list-definitions` with hot load function
  * `show-definition` in popup
  * handle standard libraries with tag system
  * generate #include lines automatically
* autocomplete keyword, function, variable, member names
* `c-mode` descendants:
  * TAB at end of line or in space before \ should align on \ from previous line
  * preserve macro \ alignment when editing
  * see if java/javascript/c++ is OK.
  * `as-mode`: ActionStript files
  * `awk-mode`
  * C++ mode
  * `objc-mode`: Objective C
  * `csharp-mode`: C#
  * `d-mode`
  * `java-mode`
  * `javascript-mode`, `js-mode` -> javascript files
    * support for v8 natives syntax %ddd()
    * handle missing semicolons
  * `json-mode`
  * `scala-mode`
  * `yacc-mode`
  * `go-mode`
  * `idl-mode`
  * `typescript-mode`
  * `jspp-mode`: JS++
  * `vala-mode`
  * `cuda-mode`
* `electric-c-mode`
* parenthesis matching problems in comments and markdown and other modes

### Markdown modes

* recognise `~~~ {.c}` language tags
* cooked markdown mode for documentation
* use cooked markdown mode for help system
* `a_bb_c` should not detect `bb` as underlined
* `markdown-mode`: syntax modes for toml, bash (bash output), sh, text, txt

### Outline / Org mode

* outline styles
* implement hide / show regions

### Preview mode

* use minor mode keymap to exit preview mode with `KEY_RET`
* add auto skip feature at top and bottom of file to skip to the previous and/or next file in the preview mode
* typing text should `auto-search`
* typing text could ask for exiting preview-mode
* prevent edit in browse mode (currently called `preview-mode`)

### Shell mode

* [BUG] ^C does not work on OpenBSD
* `C-x RET RET` should switch to last process buffer selected and move to the end of buffer
* `C-x RET RET` should find another shell buffer if `*shell*` has been killed. Should enumerate all buffers.
* terminal emulation: when move inserts spaces, they should have default attributes:  add test cases
* terminal emulation: improve behavior based on Wikipedia page [ANSI_escape_code](https://en.wikipedia.org/wiki/ANSI_escape_code)
* `C-c C-c` should abort make process and other shell buffers
* support `:` as alternate escape sequence argument separator
* `start-shell` should kill popup window
* `A-x kill-buffer RET` -> hang
* turn on interactive mode on commands that move the cursor to EOB
* use auxiliary buffer to make process input asynchronous
* give commands a chance to execute for macros to behave correctly
* `A-y` at process prompt
* fix very long lines in shell buffer (not finished)
* fix screen size notifications, `SIGWINCH` signals and ioctl
* fix terminal size inside shell window ?
* other buffer modification functions in shell input region
* `tty_put_char` should convert charsets
* set current directory of new shell buffer to that of current window
* current path in compile set to current buffer path
* current path retrieved from shell prompt backwards from point
* fix bof/eof shell mode
* allow quoting of special keys: let `do_char` insert xterm/vt100
  key sequence to allow typing special keys into shell process
* cmdline arg to force lines and columns to test shell.
* toggling interactive shell mode is not automatic enough
* use target window for man and similar commands
* fix infinite scroll for man command
* man output bug on linux
* man pager -> more bindings, such as `RET` -> `push-button` (jump to map page)
* accented letter input in shell mode
* transcode between tty charset and shell buffer charset
* track unsupported escapes in shell buffer
* use colorized buffer for *trace* buffer to flag tty input, shell output, supported and unsupported escapes.
* `telnet-mode`: Connect to a remote computer using telnet
* ssh: should use the host name and get files via scp syntax
* fix tty emulation to run kilo inside the process window
* `shell-command-on-region` on `M-|`
   mg: Pipe text from current region to external command.
   emacs: Execute string COMMAND in inferior shell with region as input.
   Normally display output (if any) in temp buffer `*Shell Command Output*`;
   Prefix arg means replace the region with it.  Return the exit code of COMMAND.

### Dired

* dired view with outline and expand/collapse
* dired view with generalized file matcher
* dired: use window/buffer caption for directory and sizes description
* dired: display directory links as directories and links, group with directories
* dired: fork process and use asynchronous function to:
   - list directory contents
   - track directory contents file stats
   - compute subdirectory sizes
* keep dired current file upon: `RET C-x C-k RET`
* use buffer specific load functions
* separate buffer for each directory
* adjust dired gutter width for max name length
* snap dired left window horiz scroll
* make dired left window temporary popleft window
* improve dired (file commands, nicer display)
  * t -> `dired-touch`
  * | -> `dired-shell-command`
  * D -> `dired-mkdir`

### Bufed

* show current directory for shell buffers in buffer list view
* dired: use window/buffer caption for headings and sizes description

### XML / HTML

* [Idea] http request with headings
* merge xml and htmlsrc modes, add submodes for plist and other config files
* xml/htmlsrc: scan for `</script>` beyond end of very long line
* `&#x200c;` -> zero width causes missing chars at end of line
* html `mode_probe` fails on **junk/Books/881256329.epub/OEBPS/Attributes.xhtml** when cycling
* html: preview mode does not work
* html: checksum stuff does not work
* html/xml: fix colorizer for multi-line tags and attributes
* [BUG] xml: crash bug on **johnmacfarlane.net/texmath.xhtml**
* `html-mode`: support hex entities
* add syntax based wrapping mode for very long lines

* distribute libqhtml as a separate project
* OPTIMIZE `eb_nextc` et al or always duplicate box content (big speed improvement).
* polish end of line offset/cursor displacement support.
* handle implicit TR
* add file referencing (<?xml-stylesheet type="text/css" href="xxx"?>, <link>, etc...)
* fix LI numbering with VALUE attribute (cannot use CSS). Verify counter-reset semantics.
* (z-index) floats must be displayed after all other stuff.
* <NOBR> is sometimes incorrect.
* more font style synthesis in html2ppm.
* add xml CDATA parsing
* add auto-indent visual display for minified XML files

### TeX / latex

* `info-mode`: unix info mode
* improve `latex-mode`
  * mode for tek style sheets
  * mode for texi intermediary files
  * latex-mode: LaTeX documents.
  * bibtex-mode
  * tex-mode: TeX or LaTeX documents.
  * create tags

### Hex mode

* merge `hex-mode` and `binary-mode`
* `hex-mode` and `binary-mode` should have an initial skip value to align the display on any boundary
* extend `hex-mode` to support 16,32,64 bit words as little and big endian
* optimize display for very large `display-width` in binary and hex modes
* save `display-width` in binary and hex modes upon window change
* display alternate cursor in non active column in hex mode.

### Archive mode

* archive: issue with current directory
* archive: add API to register new file formats
* archive: use window/buffer caption for output description
* make archive mode use dired commands
* compress mode file save to compressed format

### Images / Video / Bitmaps

* fix ffmpeg support
* filtered scaling
* zoom, pan, rotate, describe, peek-color, histogram...
* multiview, wallpaper...
* use screen aspect-ratio...
* display info on modeline
* prevent display if not invalid
* improve image viewer on X11.

### Syntax modes

* modes: add language word lists: literals and builtins
* text based language modes: token patterns, word lists, indentation spec
* create tags in other languages: fds `_STYLE_FUNCTION`
* improve existing language modes:
  * `ada-mode`: create tags
  * `agena-mode`: create tags
  * `ats-mode`: create tags
  * `asm-mode`: handle various assembly styles
  * `calc-mode`: fix syntax, disable C++ comments
  * `cmake-mode`
  * `cobol-mode`
  * `coffee-mode`: create tags
  * `css-mode`
  * `elm-mode`: create tags
  * `elixir-mode`: create tags
  * `erlang-mode`: create tags
  * `fcl-mode`
  * `forth-mode`
  * `fortran-mode`: create tags
  * `groovy-mode`: create tags
  * `haskell-mode`: create tags
  * `icon-mode`: create tags
  * `ini-mode`: Windows .ini files.
  * `jai-mode`: create tags
  * `julia-mode`: create tags
  * `lisp-mode`
  * `lua-mode`: create tags
  * `makefile-mode`: Gnu and other makefiles.
  * `nim-mode`: create tags
  * `ocaml-mode`: create tags
  * `pascal-mode`: create tags
  * `perl-mode`
  * `php-mode`: improve coloring
  * `postscript-mode`: more restrictive match
  * `python-mode`: create tags
  * `r-mode`: create tags
  * `ruby-mode`: create tags
  * `rust-mode`: create tags
  * `scad-mode`: create tags
  * `scheme-mode`
  * `scilab-mode`
  * `sh-mode`: Handle here documents
  * `sh-mode`: Handle multiline strings
  * `sh-mode`: shell script files
  * `sql-mode`
  * `swift-mode`: create tags
  * `vbasic-mode`: more restrictive matcher because .cls files may be latex
  * `virgil-mode`: create tags
  * `vim-mode`: create tags
* missing languages:
  * `asp-mode`:
  * `automake-mode`:
  * `bat-mode`: DOS command.com batch files.
  * `bazel-mode` for build system (*.bzl, BUILD...)
  * `bennugd-mode`
  * `bluespec-mode`
  * `boo-mode`
  * `cg-mode`
  * `changelog-mode`
  * `chdr-mode`
  * `cmd-mode`: Windows cmd.exe command files.
  * `conf-mode`: configuration files.
  * `DCL-mode`
  * `def-mode`
  * `desktop-mode`
  * `diff-mode`
  * `doc-mode`
  * `docbook-mode`
  * `dosbatch-mode`
  * `dot-mode`
  * `dpatch-mode`
  * `dtd-mode`
  * `eiffel-mode`
  * `exelis-mode`
  * `fsharp-mode`
  * `gams-mode`: GAMS files.
  * `gap-mode`
  * `glsl-mode`
  * `gtkrc-mode`
  * `haddock-mode`
  * `imagej-mode`
  * `j-mode`
  * `language-mode`
  * `libtool-mode`
  * `literate-mode`
  * `log-mode`
  * `m4-mode`: M4 macro processor files
  * `maildrop-mode`: for **.mailfilter**
  * `mallard-mode`
  * `matlab-mode`
  * `mediawiki-mode`
  * `modelica-mode`
  * `mxml-mode`
  * `nemerle-mode`
  * `netrexx-mode`
  * `nroff-mode`
  * `nsis-mode`
  * `objj-mode`
  * `ocl-mode`
  * `octave-mode`
  * `ooc-mode`
  * `opal-mode`
  * `opencl-mode`
  * `patch-mode`
  * `pkgconfig-mode`
  * `po-mode`: translation files
  * `prolog-mode`
  * `protobuf-mode`
  * `puppet-mode`
  * `rpmspec-mode`
  * `sml-mode`
  * `sparql-mode`
  * `systemverilog-mode`
  * `t2t-mode`
  * `tcl-mode`: Tcl files.
  * `texinfo-mode`
  * `vbnet-mode`
  * `verilog-mode`
  * `vhdl-mode`: VHDL files.
  * `xslt-mode`
  * qmake, cmake, scons, ant, maven, bitC

### New modes

## `csv-mode`

* CSV database functions
  - `csv_find(string where)`
  - `csv_select(string field_list, string where, string destination)`
  - `csv_filter(string where)`
  - `csv_update(string what_list, string where)`
  - `csv_append_columns(string field_list)`
  - `csv_insert_columns(string field_list)`
  - `csv_delete_columns(string field_list)`
  - `csv_delete_lines(string where)`
  - `csv_append_lines(string field_list)`
  - `csv_insert_lines(string field_list)`
  - `csv_sort(string field_list)`

## `json-mode`

* auto-wrap and indent
* JSON database functions

## `xml-mode`

* auto-wrap and indent
* XML database functions

## Other modes

* `rst-mode`: support ReStructuredText (RST)
* `auto-compression-mode`
* minor modes with key override such as "preview" mode
* `visual-diff-mode`: Use color-coding to compare two buffers.
* calculator / spreadsheet mode (based on SC)
* calendar mode
* email reader mode: mail / rmail
* news reader mode
* irc client mode
* twitter
* rss
* wikipedia mode
* abbreviation mode
* ispell / spell checker
* printing support

## Fly zone

* kill eval result so it can be yanked where appropriate
* integrate qscript
