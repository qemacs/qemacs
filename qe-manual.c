/*
 * QEmacs, tiny but powerful multimode editor
 *
 * Copyright (c) 2000-2002 Fabrice Bellard.
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
   This file contains the main framework for the Quick Emacs manual.

   The manual is composed by scandoc from all source files including this one.
   The sections are extracted from scandoc comments, multiline comments
   starting with a @ and an ordering string. All sections are concatenated
   in natural order of the ordering strings.
 */

/*@ INTRO=0 - introduction
   # Introduction

   Welcome to QEmacs! A small but powerful UNIX editor with many features
   that even big editors lack.

   ## Quick Description

   QEmacs is a small text editor targeted at embedded systems or debugging.
   Although it is very small, it has some very interesting features that
   even big editors lack:

   - Full screen editor with an Emacs look and feel with all common Emacs
   features: multi-buffer, multi-window, command mode, universal argument,
   keyboard macros, config file with C-like syntax, minibuffer with
   completion and history.

   - Can edit huge files (hundreds of megabytes) without delay, using a
   highly optimized internal representation and memory mapping for large
   files.

   - Full Unicode support, including multi charset handling
   (8859-x, UTF8, SJIS, EUC-JP, ...) and bidirectional editing respecting
   the Unicode bidi algorithm. Arabic and Indic scripts handling (in
   progress). Automatic end of line detection.

   - C mode: coloring with immediate update, auto-indent, automatic tags.

   - Shell mode: full color VT100 terminal emulation so your shell works
   exactly as you expect. Compile mode with colorized error messages,
   automatic error message parser jumps to next/previous error, works
   with grep too. The shell buffer is a fully functional terminal: you
   can run qemacs, vim or even emacs recursively!

   - Input methods for most languages, including Chinese (input methods
   descriptions come from the Yudit editor).

   - Binary and hexadecimal in place editing mode with insertion and
   block commands. Unicode hexa editing of UTF-8 files also supported.
   Can patch binary files, preserving every byte outside the modified
   areas.

   - Works on any VT100 terminal without termcap. UTF-8 VT100 support
   included with double width glyphs.

   - X11 support. Supports multiple proportional fonts at the same time
   (like XEmacs). X Input methods supported. Xft extension supported for
   anti-aliased font display.

   - Bitmap images are displayed on graphics displays and as ASCII colored text
   on text terminals, which is handy when browsing files over an ssh connection.
   (QEmacs uses the public domain [`stb_image`](https://github.com/nothings/stb/blob/master/stb_image.h)
   package for image parsing.

 */

/*@ CONCEPTS=1 - concepts
   # Concepts
 */

/*@ BUFFERS=2 - buffers
   # Buffers
 */

/*@ WINDOWS=3 - windows
   # Windows
 */

/*@ MODES=4 - modes
   # Modes
 */

/*@ CMD=5 - commands
   # Commands
 */

/*@ IMPL=6 - implementation
   # Implementation
 */

/*@ STRUCT=7 - structures
   # Structures
 */

/*@ API=8 - functions
   # C functions
 */

/*@ EPILOG=9 - epilog
   ## Building QEmacs

 * Get the source code from github or an archive.
 * Launch the custom configuration script `./configure`. You can list the
available options by typing `./configure --help`.
 * Type `make` to compile qemacs and its associated tools.
 * Type `make install` as root to install it in ** /usr/local **.

   ## Authors

   QEmacs was started in 2000. The initial version was developped by
   Fabrice Bellard and Charlie Gordon, who since then, has been maintaining
   and extending it.

   ## Licensing

   QEmacs is released under the MIT license.
   (read the accompanying [LICENCE](LICENCE) file).

   ## Contributing to QEmacs

   The QEmacs project is hosted on [github](https://github.com/qemacs/qemacs/)
   Please file an issue for any questions or feature requests. Patch requests are welcome.

 */
