<!-- TODO list for qemacs -- Author: Charles Gordon -- Updated: 2022-08-18 -->

# QEmacs TODO list

## Changes for Core modules

### Documentation

* improve **README.md**
* rework documentation:
 - document existing features
 - make documentation available inside qemacs
* move documentation to markdown
* doc: rewrite **TODO.md** file with more sections and explanations
* doc: migrate **coding-rules.html** to markdown
* help: info-mode
* help: `qemacs-faq` on `C-h C-f`
* help: `qemacs-manual` on `C-h m`
* help: add inline documentation for commands on `C-h C-f`
* help: `data-directory`, `data-path`...
* basic: show memory stats in `describe-buffer` and `about-qemacs`
* extra: add function to add entry in **TODO.md**
* add command help/description in declarations
* make command declaration macros standalone

### Core / Buffer / Input

* [BUG] ^C does not work on OpenBSD
* tiny: remove extra features
* basic: add method pointers in windows initialized from fallback chain
* basic: check binding lookup along fallback chain
* basic: share mmapped pages correctly
* basic: check abort during long operations: bufferize input and check for `^G`
* basic: optional 64-bit offsets on 64-bit systems, use typedef for buffer offsets
* basic: disable messages from commands if non-interactive (eg: `set-variable`)
* add custom memory handling functions.
* use failsafe memory allocator and `longjmp` recover.
* redefine `KEY_Fx` to make them sequential
* move `ungot_key` to `key_context`
* splitting pages should fall on 32 bit boundaries (difficult)
* add default charset for new buffer creation, set that to utf8
* handle broken charset sequences across page boundaries
* allow recursive main loop, and remove input callbacks
* synced virtual buffers with restricted range
* unsynced virtual buffers with restricted range and specific mode/charset
* bfs: built in file system for embedded extensions and files
   Jasspa bfs is way too complicated, make simpler system
* notes
* `C-x x next-buffer` ??? Move to the next buffer.
* `qe_realloc`: typed and clear reallocated area

### Charsets / Unicode / Bidir

* charset: better display of invalid utf-8 encodings
* charset: change character detection API to handle cross page spanning
* charset: fix `eb_prev_char` to handle non self-synchronizing charsets
* charset: handle chinese encodings
* charset: handle euc-kr
* charset: autodetect sjis, euc-jp...
* charset: update cp directory from more recent unicode tables
* charset: UTF-8 variants: CESU-8, Modified UTF-8, UTF-16
* charset: UTF-1 obsolete standard encoding for Unicode
* charset: handle `tty-width` to compute alignement in dired, bufed...
* charset: limit number of combining marks to 20
* charset: use `unichar`, `rune` and/or `u8` types
* charset: detect bad encoding and use `errno` to tell caller
* charset: auto/mixed eol mode
* charset: `set-eol-type` should take a string: auto/binary/dos/unix/mac/0/1/2...
* charset: display `^L` as horizontal line and consider as linebreak character
* charset: handle zero width codepoints:
  cp="200B" na="ZERO WIDTH SPACE" alias="ZWSP"
  cp="200C" na="ZERO WIDTH NON-JOINER" alias="ZWNJ"
  cp="200D" na="ZERO WIDTH JOINER" alias="ZWJ"
  cp="200E" na="LEFT-TO-RIGHT MARK" alias="LRM"
  cp="200F" na="RIGHT-TO-LEFT MARK" alias="RLM"
* `set_input_method()` and `set_buffer_file_coding_system()` in config file.
* fix kana input method
* charset: add JIS missing encoding functions
* add JIS charset probing functions
* test Hebrew keymap support.
* rewrite fribidi
* use Unicode file hierarchy for code page files
* handle or remove extra code page files:
  CP1006.TXT CP1253.TXT CP1254.TXT CP1255.TXT CP1258.TXT
  CP775.TXT CP855.TXT CP856.TXT CP857.TXT CP860.TXT CP861.TXT
  CP862.TXT CP863.TXT CP864.TXT CP865.TXT CP869.TXT CP874.TXT CP932.TXT
  JIS0201.TXT SHIFTJIS.TXT euc-jis-2004-std.txt iso-2022-jp-2004-std.txt
  jisx0213-2004-std.txt sjis-0213-2004-std.txt
  MAC-CYRILLIC.TXT MAC-GREEK.TXT MAC-ICELAND.TXT MAC-TURKISH.TXT
  koi8_ru.cp APL-ISO-IR-68.TXT GSM0338.TXT SGML.TXT
* accented letters on OS/X
* combining unicode glyphs produce bogus cursor positions
   example: `V M-'` this problem occurs if no combined glyph exists.
   qemacs does not take into account combination performed by the terminal.
   Terminal glyph width of 0 should be supported.
* deal with accents in filenames (OS/X uses combining accents encoded as UTF-8)
* fix backspace on combining glyphs

### Windowing / Display

* basic: always save window buffer properties to buffer upon detaching
* basic: fix current position when changing buffer attached to window
* display: add screen dump command and format
* display: colorize extra `^M` and `^Z` as preproc at end of line prior to calling the syntax highlighter (same as BOM)
* display: colorizer bug on **/comp/projects/fractal/fractint/ORGFORM/NOEL-2.FRM** (triple `^M`)
* display: default `display-width` of 0 is automatic, other values are shared between binary and hex modes
* display: display bug on **~/comp/projects/fractal/fractint/ORGFORM/BAILOUT.FRM** (double `^M`)
* display: minibuffer and popup windows should be in a separate lists
* display: `toggle-full-screen` should not put modeline on popup
* display: `toggle-full-screen` should work on popups
* layout: kill buffer should delete popup and popleft window
* basic: fix default wrap setting mess
* screen: check coordinate system to 1000 based with optional sidebars
* display: API: use style cache in `DisplayState`
* display: API: remove screen argument in `release_font`, `glyph_width`
* display: API: add `create-style(name, properties)`
* display: use true colors on capable terminals
* basic: `frame-title-format` and `mode-line-format`
* basic: `transient-mark-mode` to highlight the current region
* basic: `delete-selection-mode` to delete the highlighted region on DEL and typing text
* modes: `header-line` format
* modes: `mode-line` format
* modes: display filename relative to current directory instead of buffer name on `mode-line`
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
* improve layout scheme for better scalability.
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

* display: wrap long lines past line numbers column
* fix column computation based on display properties:
  (variable pitch, tabs, ^x and \uxxxx stuff -- emacs behaviour) ?

### X11 display / graphics

* x11: handle X11 window manager close window event and exit cleanly
* clip display by popup size
* move `-nw` cmd line option to **tty.c** and make `term_probe` return better score
* remember X11 window positions and restore layout?
* faster video handling (generalize invalidate region system)
* integrate tinySVG renderer based on the new libraster.
* implement wheel mode in CSS display.
* fix configure for missing support: x11 xv png ...
* add `configure --disable-graphics`
* `dpy_open_font` should never return `NULL`, must have a system font.

### Files

* [BUG] files: check file date to detect asynchronous modifications on disk
* files: reload modified file upon change if untouched since load
* files: add hook on file change
* files: handle files starting with re:
* files: check file permissions.
* files: use trick for entering spaces in filename prompts without completion
* files: fix `s->offset` reset to 0 upon `C-x C-f newfile ENT C-x 2 C-x b ENT`
* files: insert-file: load via separate buffer with charset conversion
* files: `reload-file` on `C-x C-r`
* files: `qe_load_file` should split screen evenly for `LF_SPLIT_SCREEN` flag
* [Idea] save file to non existent path -> create path.
* [Idea] find-file: gist:snippet
* Missing commands:
  * `find-file-existing`
  * `find-other-frame` on `C-x 5 f`, `C-x 5 C-f`
  * `find-other-window` on `C-x 4 f`, `C-x 4 C-f`
  * `find-other-read-only` on `C-x C-r`
  * `find-file-read-only-other-frame` on `C-x 5 r`
  * `find-file-read-only-other-window` on `C-x 4 r`
  * `save-modified-buffers` on `C-x s`
  * `find-file`: should support scp syntax for remote loading
* files: handle files with embedded spaces
* avoid error in new file
* files: actually load file in `find-file-noselect`
* files: should update symbolic links times when saving files
* basic: improve speed: `C-x C-f ~/x2m RET A-r 20140101 RET 20140101 RET` -> 96s
* basic: improve speed: `C-x C-f ~/x2m RET C-u 1000 C-n` -> 4s

### Moving / Editing

* files: fix `SPC` / `TAB` distinct behaviors on **~/comp/project/gnachman/**
* basic: update default settings to `indent_tabs_mode = 0`, `indent_width = 4`, my-colors
* basic: add property lists in buffer and window for default directory and similar properties (override)
* basic: backspace delete hacking tabs
* display: use a prefix to explore file in a popup window
* basic: enter should optionally remove whitespace at end of line
* basic: reset last command when executing macro
* basic: make `do_word_right`, `do_word_left`, `do_bol`, `do_eol`... return new offset
* basic: use visual movement for left, right, up, down and character based for `C-b C-f C-n C-p`
* basic: fix offset when exiting `s->hex_mode`
* extras: `do_transpose` should take argval and swap distant fragments
* basic: `elastic-tabs`
* basic: `indent-rigidly`
* [Idea] dynamic project based settings, include, exclude patterns...
* new flavor for GoogleClosureCompiler
* emacs-22 bindings:
  * `M-g M-g` `goto-line` (with prefix argument)
  * `M-g M-p` `previous-error`
  * `M-g M-n` `next-error`
* Missing commands:
  * `show-matching-delimiters`
  * `toggle-full-screen`-> unsupported if `screen->dpy_full_screen` is `NULL`
* `set-gosmacs-bindings`
* remote editing
* blink-and-insert on `) } ] >`
* fix scroll up/down to move point if already at end
* move by paragraph on `M-[` and `M-]`
* scroll horizontally on `M-{` and `M-}`
* scroll up/down with argument should scroll by screen row.
* simplify `C-z A-z` accordingly
* `auto-fill-mode`
* `auto-revert-mode`, `global-auto-revert-mode`, `auto-revert-tail-mode`
* rectangular regions, cut/paste
* multi-line editing

### Macros

* macros: allow redefining the keyboard macro embedding itself
* macros: `show-macro`, `dump-macro` to ease macro debugging and timing
* macros: fix macro slowliness in 6000 line buffer **junk/dirs/x**
* macros: do not use visual movement inside macros (definition and execution)
* macros: do not redisplay during macro execution, esp with prefix
* macros: check for abort during repeated command execution
* macros: check for failure during repeated command execution
* macros: improve `dump-macro` to convert macro to string
* macros: fix `dump-macro` to save/restore `last-kbd-macro` to/from session
* macros: do not store messages during repeated macro execution or limit buffer size
* macros: `name-last-kbd-macro`
* macros: fix source syntax issues
```lisp
   (define-macro "last-kbd-macro" "A-fA-fC-FC-FC-@C-EA-wC-AC-NC-XnC-YC-MC-Xp")
   (define-macro "last-kbd-macro" "A\-f")
   ---   - - -
   (define-macro "last-kbd-macro" "---   - - -C-M
   <Down><Up><Right><Left>")
   <><>(define-macro "last-kbd-macro" "\<>\<>")
```

### Minibuffer / Completion

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

### Searching

* incremental search: increase max repeat count
* search: improve speed finally
* search: add low-level accelerator function
* search: use `do_isearch` or similar to input string and options
   for other search commands: `do_search_string`, `do_query_replace`,
   `count-matches`, `delete-matching-lines` (might need recursive edit)
* search: add regex support
* search: `hex-mode` search strings should mix hex ut8 strings and ASCII control char names
* search: handle word and case toggles matches in `query-replace`
* search: `count-words`, `wc`
* search: stats command for word count and mode specific stats
* search: regex search/replace
* search: query replace across multiple files
* search: query replace options: u -> undo last change
* extra: `grep`, `grep-buffer`, `grep-sources`, `grep-tree`...

### Undo

* undo: allow file save when undo clears file modified flag
* undo: undo some cursor movements
* undo: group undo entries to match command and or macro boundaries
* undo: saving the file kills the redo stack!
* undo: when undo resets the modified buffer flag, this prevents file save
* undo: store cursor movements in undo records
* undo: kill redo list when forking after undo
* undo: undo should handle sequence of undo records upto tagged start.
* undo: add disable-undo for tests and benchmarking
* undo: limit size of undo buffers (`undo-outer-limit`, default 3000000)
* undo: add undo records for styles, modes...
* undo: disable undo for archive parse and uncompress phases
* undo: compress logs and limit size
* mode for undo buffers

### Config / Sessions

* config: reparse **.qerc** upon change
* session: register session store functions
* session: save previous answers, ...
* session: save preview mode, buffer modes and properties
* session: save process buffers?  non file-based buffers?  dired buffers?
* basic: scripting
* script: expression evaluator
* variables: set variable values via function pointer
* variables: add boolean variables
* tack library of ancillary files at the end of the binary:
  - add protocol to read from library (lib:xxx)
  - add html.min.css and docbook.min.css to library
  - append lib: to default QE_PATH
* clean up ancillary files

### Colors and styles

* [BUG] preserve static styles in `do_convert_buffer_file_coding_system()`
* optimize style transfer in `eb_insert_buffer_convert()`
* style: add color themes
* style: add style recent changes (`highlight-recent-changes`)
* basic: make styles and log buffers read-only and display as binary
* syntax: fix overlong line coloring
* basic: make `style-buffer-mode` and `log-buffer-mode`
* basic: fix colors, default schemes...

## Modes

### Generic mode stuff

* basic: make ModeDef structures read-only
* basic: add default bindings in ModeDef
* basic: register modes recursively along fallback chain
* modes: `next-mode` should include buffer `default_mode` in list
* tags: remove tags for modified line
* basic: fix mode setting mess
* modes: split modes into colorizers, interaction, io, charset, eoltype
* modes: major and minor modes
* rethink mode specific commands -> add inheritance for all commands ?
* mode inheritance

### C mode

* clang: allman and other styles
* clang: epita style
* clang: auto remove trailing spaces and add final newline
* clang: colorizing bug: `static int (*qe__initcall_first)(void) qe__init_call = NULL;`
* clang: missed tag: `static int (*qe__initcall_first)(void) qe__init_call = NULL;`
* clang: align multi line comments leading * one space to the right
* clang: fix C indentation inside comments
* clang: fix C indentation inside struct, array and enum initializers
* `c-indent`
* `indent-with-tabs`
* stats command for `slcc`
* improve tag support:
  * tag multiple files and buffers
  * tag project files recursively
  * save tags to QTAGS or .qetags file
  * handle multiple tag files
  * update tag files automatically
  * `list-definitions` with hot load function
  * `show-definition` in popup
  * handle standard libraries with tag system
  * generate #include lines automatically
* see if java/javascript/c++ is OK.
* autocomplete keyword, function, variable, member names
* automatic indentation detection
* `c-mode` descendants:
  * `as-mode`: ActionStript files
  * `awk-mode`
  * C++ mode
  * `objc-mode`: Objective C
  * `csharp-mode`: C#
  * `d-mode`
  * `java-mode`
  * `javascript-mode`, `js-mode` -> javascript files
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

### Markdown modes

* markdown: recognise `~~~ {.c}` language tags
* modes: cooked markdown mode for documentation
* help: use cooked markdown mode for help system
* markdown: `a_bb_c` should not detect `bb` as underlined
* `markdown-mode`: syntax modes for toml, bash (bash output), sh, text, txt

### Outline / Org mode

* outline styles
* implement hide / show regions

### Preview mode

* basic: use minor mode keymap to exit preview mode with `KEY_RET`
* preview: add auto skip feature at top and bottom of file to skip to the previous and/or next file in the preview mode
* preview-mode: typing text should `auto-search`
* prevent edit in browse mode (currently called `preview-mode`)

### Shell mode

* C-x Enter should find another shell buffer if `*shell*` has been killed. Should enumerate all buffers.
* terminal emulation: when move inserts spaces, they should have default attributes
* shell: `C-c C-c` should abort make process
* shell: support `:` as alternate escape sequence argument separator
* shell: use target window for man and similar commands
* shell: `start-shell` should kill popup window
* shell: `A-x kill-buffer RET` -> hang
* shell: turn on interactive mode on commands that move the cursor to EOB
* shell: asynchronous input buffer
* shell: give commands a chance to execute for macros to behave correctly
* shell: `A-y` at process prompt
* shell: fix very long lines in shell buffer
* shell: `C-x RET` should switch to last process buffer selected and move to the end of buffer
* shell: fix screen size notifications, `SIGWINCH` signals and ioctl
* shell: fix crash bug when invoking qemacs recursively in the terminal
* shell: set current directory of new shell buffer to that of current window
* shell: use auxiliary buffer to make process input asynchronous
* shell: other buffer modification functions in shell input region
* shell: `tty_put_char` should convert charsets
* shell: current path in compile set to current buffer path
* shell: current path retrieved from shell prompt backwards from point
* fix bof/eof shell mode
* allow quoting of special keys: let `do_char` insert xterm/vt100
  key sequence to allow typing special keys into shell process
* fix terminal size inside shell window ?
* cmdline arg to force lines and columns to test shell.
* toggling interactive shell mode is not automatic enough
* man output bug on linux
* man pager -> more bindings, such as `RET` -> `push-button` (jump to map page)
* accented letter input in shell mode
* transcode between tty charset and shell buffer charset
* track unsupported escapes in shell buffer
* use colorized buffer for *trace* buffer to flag tty input, shell output, supported and unsupported escapes.
* `telnet-mode`: Connect to a remote computer using telnet
* ssh: should use the host name and get files via scp syntax

### Dired

* dired view with outline and expand/collapse
* dired view with generalized file matcher
* dired: use window/buffer caption for directory and sizes description
* dired: display directory links as directories and links, group with directories
* dired: fork process and use asynchronous function to:
   - list directory contents
   - track directory contents file stats
   - compute subdirectory sizes
* dired: keep dired current file upon: `RET C-x C-k RET`
* dired: fork for directory scan, background inode tracking, dir size scan
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

### XML / HTML

* [Idea] http request with headings
* xml: merge xml and htmlsrc modes, add submodes for plist and other config files
* xml/htmlsrc: scan for `</script>` beyond end of very long line
* html: `&#x200c;` -> zero width causes missing chars at end of line
* html: `mode_probe` fails on **junk/Books/881256329.epub/OEBPS/Attributes.xhtml** when cycling
* html: preview mode does not work
* html: checksum stuff does not work
* html/xml: merge xml / htmlsrc modes
* html/xml: fix colorizer for multi-line tags and attributes
* [BUG] xml: crash bug on **johnmacfarlane.net/texmath.xhtml**
* `html-mode`: support hex entities

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

* display: `hex-mode` and `binary-mode` should have an initial skip value to align the display on any boundary
* display: `hex-mode` should optionally display chunks of 2, 4 or 8 bytes in big or little endian order
* display: optimize display for very large `display-width` in binary and hex modes
* display: save `display-width` in binary and hex modes upon window change
* hex: extend hex mode to support 16,32,64 bit words as little and big endian
* merge `hex-mode` and `binary-mode`
* display alternate cursor in non active column in hex mode.

### Archive mode

* archive: issue with current directory
* archive: add API to register new file formats
* archive: use window/buffer caption for output description
* make archive mode use dired commands
* compress mode file save to compressed format

### Images / Video / Bitmaps

* images: filtered scaling
* images: zoom, pan, rotate, describe, peek-color, histogram...
* images: multiview, wallpaper...
* images: use screen aspect-ratio...
* images: display info on modeline
* images: prevent display if not invalid
* improve image viewer on X11.

### Syntax modes

* modes: add language word lists: literals and builtins
* modes: text based language modes: token patterns, word lists, indentation spec
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
  * look at qmake, cmake, scons, ant, maven, bitC

### New modes

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
