# Quick Emacs (QEmacs)

Welcome to QEmacs! A small but powerful UNIX editor with many features
that even big editors lack.

## Quick Description

QEmacs is a small text editor targetted at embedded systems or debugging.
Although it is very small, it has some very interesting features that
even big editors lack:

- Full screen editor with an Emacs look and feel with all Emacs common
features: multi-buffer, multi-window, command mode, universal argument,
keyboard macros, config file with C like syntax, minibuffer with com-
pletion and history.

- Can edit huge files (hundreds of megabytes) without delay, using a
highly optimized internal representation and memory mapping for large files.

- Full Unicode support, including multi charset handling
  (8859-x, UTF8, SJIS, EUC-JP, ...) and bidirectional editing respecting
  the Unicode bidi algorithm. Arabic and Indic scripts handling (in
  progress). Automatic end of line detection.

- C mode: coloring with immediate update, auto-indent, automatic tags.

- Shell mode: full color VT100 emulation so that your shell works exactly
as you expect. Compile mode with colorized error messages,
automatic error message parser jumps to next/prev error, works with grep too.
The shell buffer is a fully functional terminal: you can run qemacs,
vim or even emacs recursively!

- Input methods for most languages, including Chinese (input methods
come from the Yudit editor).

- Hexadecimal editing mode with insertion and block commands. Unicode
hexa editing of UTF-8 files also supported. Can patch binary files,
preserving every byte outside the modified areas.

- Works on any VT100 terminals without termcap. UTF-8 VT100 support
included with double width glyphs.

- X11 support. Support multiple proportionnal fonts at the same time
(as XEmacs). X Input methods supported. Xft extension supported for
anti aliased font display.

- Bitmap images are displayed on graphics displays and as ASCII colored text
on terminals, which is handy when browing files over an ssh terminal connection.
(QEmacs use the public domain `stb_image` package for image conversions.
Source: https://github.com/nothings/stb/blob/master/stb_image.h )

## Building QEmacs

* Launch the configure tool `./configure`. You can list the
 possible options by typing `./configure --help`.

* Type `make` to compile qemacs and its associated tools.

* Type `make install` as root to install it in /usr/local.

## QEmacs Documentation

Read the file [qe-doc.html](qe-doc.html).

## Licensing

QEmacs is released under the GNU Lesser General Public License
(read the accompagning [COPYING](COPYING) file).

QEmacs is not a GNU project, it does not use code from GNU Emacs.

## Contributing to QEmacs

Please contact the qemacs-devel mailing list.

## Authors

QEmacs was started in 2000. The initial version was developped by
Fabrice Bellard and Charlie Gordon, who since then, has been maintaining
and extending it.
