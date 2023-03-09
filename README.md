# Quick Emacs (QEmacs)

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

## Building QEmacs

* Launch the custom configuration script `./configure`. You can list the
available options by typing `./configure --help`.

* Type `make` to compile qemacs and its associated tools.

* Type `make install` as root to install it in **/usr/local**.

## QEmacs Documentation

Read the file [qe-doc.html](qe-doc.html).

## Licensing

QEmacs is released under the MIT license.
(read the accompagning [LICENCE](LICENCE) file).

## Contributing to QEmacs

Please contact the [qemacs-devel](https://lists.nongnu.org/mailman/listinfo/qemacs-devel) mailing list.

## Authors

QEmacs was started in 2000. The initial version was developped by
Fabrice Bellard and Charlie Gordon, who since then, has been maintaining
and extending it.
