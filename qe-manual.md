
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

# Concepts

# Buffers

# Windows

# Modes

# Commands

### `isearch-repeat-backward()`
Search for the next match backward.
Retrieve the last search string if search string is empty.

### `isearch-repeat-forward()`
Search for the next match forward.
Retrieve the last search string if search string is empty.

### `isearch-yank-char()`
Extract the current character from the buffer and append it
to the search string.

### `isearch-yank-kill()`
Append the contents of the last kill to the search string.

### `isearch-yank-line()`
Extract the current line from the buffer and append it to the
search string.

### `isearch-yank-word-or-char()`
Extract the current character or word from the buffer and append it
to the search string.

### `overwrite-mode(argval)`

Toggle overwrite mode.

With a prefix argument, turn overwrite mode on if the argument
is positive, otherwise select insert mode.
In overwrite mode, characters entered into a buffer replace
existing text without moving the rest of the line, rather than
shifting it to the right.  Characters typed before a newline
extend the line.  Space created by TAB characters is filled until
the tabulation stop is reached.
Backspace erases the previous character and sets point to it.
C-q still inserts characters in overwrite mode as a convenient way
to insert characters when necessary.

### `query-replace(string FROM-STRING, string TO-STRING, int DELIMITED=argval, int START=point, int END=end)`

Replace some occurrences of FROM-STRING with TO-STRING.
As each match is found, the user must type a character saying
what to do with it.  For directions, type '?' at that time.

FROM-STRING is analyzed for search flag names with determine how
matches are found.  Supported flags are [UniHex], [Hex], [Folding],
[Exact], [Regex] and [Word].

If case matching is either Folding or Smart, replacement transfers
the case pattern of the old text to the new text.  For example
if the old text matched is all caps, or capitalized, then its
replacement is upcased or capitalized.

Third arg DELIMITED (prefix arg if interactive), if non-zero, means
replace only matches surrounded by word boundaries.

Fourth and fifth arg START and END specify the region to operate on:
if these arguments are not provided, if the current region is
highlighted, operate on the contents of the region, otherwise,
operate from point to the end of the buffer.

To customize possible responses, change the "bindings" in
`query-replace-mode`.

### `replace-string(string FROM-STRING, string TO-STRING, int DELIMITED=argval, int START=point, int END=end)`

Replace occurrences of FROM-STRING with TO-STRING.
Preserve case in each match if `case-replace' and `case-fold-search'
are non-zero and FROM-STRING has no uppercase letters.
(Preserving case means that if the string matched is all caps, or
capitalized, then its replacement is upcased or capitalized.)

Third arg DELIMITED (prefix arg if interactive), if non-zero, means
replace only matches surrounded by word boundaries.

Fourth and fifth arg START and END specify the region to operate on:
if these arguments are not provided, if the current region is
highlighted, operate on the contents of the region, otherwise,
operate from point to the end of the buffer.

This function is usually the wrong thing to use in a qscript program.
What you probably want is a loop like this:

    while (search_forward(FROM-STRING))
        replace_match(TO-STRING);

which will run faster and will not set the mark or print anything.
The loop will not work if FROM-STRING can match the null string
and TO-STRING is also null

### `set-visited-file-name(string FILENAME, string RENAMEFILE)`

Change the name of file visited in current buffer to FILENAME.
This also renames the buffer to correspond to the new file. The
next time the buffer is saved it will go in the newly specified
file. If FILENAME is an empty string, mark the buffer as not
visiting any file.

If the RENAMEFILE argument is not null and starts with 'y', an
attempt is made to rename the old visited file to the new name
FILENAME.

# Implementation

# Structures

# C functions

### `struct buf_t;`

Fixed length character array handling
All output functions return the number of bytes actually written to the
output buffer and set a null terminator after any output.

### `buf_t *buf_attach(buf_t *bp, char *buf, int size, int pos);`

Initialize a `buf_t` to output to a fixed length array at a given position.

* argument `bp` a valid pointer to fixed length buffer

* argument `buf` a valid pointer to a destination array of bytes

* argument `size` the length of the destination array

* argument `pos` the initial position for output.

Return the `buf_t` argument.

Note: `size` must be strictly positive and `pos` must be in range: `0 <= pos < size`

Note: this function does not set a null terminator at offset `pos`.

### `int buf_avail(buf_t *bp);`

Compute the number of bytes available in the destination array

* argument `bp` a valid pointer to fixed length buffer

Return the number of bytes, or `0` if the buffer is full.

### `buf_t *buf_init(buf_t *bp, char *buf, int size);`

Initialize a `buf_t` to output to a fixed length array.

* argument `bp` a valid pointer to fixed length buffer

* argument `buf` a valid pointer to a destination array of bytes

* argument `size` the length of the destination array

Return the `buf_t` argument.

### `int buf_printf(buf_t *bp, const char *fmt, ...);`

Format contents at the end of a fixed length buffer.

* argument `bp` a valid pointer to fixed length buffer

* argument `fmt` a valid pointer to a format string

Return the number of bytes actually written.

### `int buf_put_byte(buf_t *bp, unsigned char ch);`

Append a byte to a fixed length buffer.

* argument `bp` a valid pointer to fixed length buffer

* argument `ch` a byte

Return the number of bytes actually written.

### `int buf_put_key(buf_t *out, int key);`

Encode a key as a qemacs string into a fixed length buffer

* argument `bp` a valid pointer to fixed length buffer

* argument `key` a key value

Return the number of bytes produced in the destination array,
not counting the null terminator.

Note: Recurse at most once for meta keys.

### `int buf_put_keys(buf_t *out, unsigned int *keys, int nb_keys);`

Encode a sequence of keys as a qemacs strings into a fixed length buffer

* argument `bp` a valid pointer to fixed length buffer

* argument `keys` a valid pointer to an array of keys

* argument `nb_keys` the number of keys to encode.

Return the number of bytes produced in the destination array,
not counting the null terminator.

### `int buf_putc_utf8(buf_t *bp, char32_t c);`

Encode a codepoint in UTF-8 at the end of a fixed length buffer.

* argument `bp` a valid pointer to fixed length buffer

* argument `c` a valid codepoint to encode

Return the number of bytes actually written.

Note: if the conversion does not fit in the destination, the
`len` field is not updated to avoid partial UTF-8 sequences.

### `int buf_puts(buf_t *bp, const char *str);`

Append a string to a fixed length buffer.

* argument `bp` a valid pointer to fixed length buffer

* argument `str` a valid pointer to a C string

Return the number of bytes actually written.

### `int buf_quote_byte(buf_t *bp, unsigned char ch);`

Encode a byte as a source code escape sequence into a fixed length buffer

* argument `bp` a valid pointer to fixed length buffer

* argument `ch` a byte value to encode as source

Return the number of bytes produced in the destination array,
not counting the null terminator

### `int buf_write(buf_t *bp, const void *src, int size);`

Write an array of bytes to a fixed length buffer.

* argument `bp` a valid pointer to fixed length buffer

* argument `src` a valid pointer to an array of bytes

* argument `size` the number of bytes to write.

Return the number of bytes actually written.

Note: content is truncated if it does not fit in the available
space in the destination buffer.

### `int qe_digit_value(char32_t c);`

Get the numerical value associated with a codepoint

* argument `c` a codepoint value

Return the corresponding numerical value, or 255 for none
ie: `'0'` -> `0`, `'1'` -> `1`, `'a'` -> 10, `'Z'` -> 35

### `int qe_findchar(const char *str, char32_t c);`

Test if a codepoint value is part of a set of ASCII characters

* argument `str` a valid pointer to a C string

* argument `c` a codepoint value

Return a boolean success value: `1` if the codepoint was found in
the string, `0` if `c` is `0` or non-ASCII or was not found in the set.

Note: only ASCII characters are supported

### `int qe_indexof(const char *str, char32_t c);`

Find the index of a codepoint value in a set of ASCII characters

* argument `str` a valid pointer to a C string

* argument `c` a codepoint value

Return the offset of `c` in `str` if found or `-1` if `c` is not
an ASCII character or was not found in the set.

Note: only non null ASCII characters are supported.
Contrary to `strchr`, `'\0'` is never found in the set.

### `int qe_inrange(char32_t c, char32_t a, char32_t b);`

Range test for codepoint values

* argument `c` a codepoint value

* argument `a` the minimum codepoint value for the range

* argument `b` the maximum codepoint value for the range

Return a boolean value indicating if the codepoint is inside the range

### `int qe_isalnum(char32_t c);`

Test if a codepoint represents a letter or a digit

* argument `c` a codepoint value

Return a boolean success value

Note: only ASCII letters and digits are supported

### `int qe_isalnum_(char32_t c);`

Test if a codepoint represents a letter, a digit or an underscore

* argument `c` a codepoint value

Return a boolean success value

Note: only ASCII letters and digits are supported

### `int qe_isalpha(char32_t c);`

Test if a codepoint represents a letter

* argument `c` a codepoint value

Return a boolean success value

Note: only ASCII letters are supported

### `int qe_isalpha_(char32_t c);`

Test if a codepoint represents a letter or an underscore

* argument `c` a codepoint value

Return a boolean success value

Note: only ASCII letters are supported

### `int qe_isbindigit(char32_t c);`

Test if a codepoint represents a binary digit

* argument `c` a codepoint value

Return a boolean success value

Note: only ASCII digits are supported

### `int qe_isbindigit_(char32_t c);`

Test if a codepoint represents a binary digit or an underscore

* argument `c` a codepoint value

Return a boolean success value

Note: only ASCII digits are supported

### `int qe_isblank(char32_t c);`

Test if a codepoint represents blank space

* argument `c` a codepoint value

Return a boolean value indicating if the codepoint is blank space

Note: only ASCII blanks and non-breaking-space are supported

### `int qe_isdigit(char32_t c);`

Test if a codepoint represents a digit

* argument `c` a codepoint value

Return a boolean value indicating if the codepoint is an ASCII digit

Note: only ASCII digits are supported

### `int qe_isdigit_(char32_t c);`

Test if a codepoint represents a digit or an underscore

* argument `c` a codepoint value

Return a boolean success value

Note: only ASCII digits are supported

### `int qe_islower(char32_t c);`

Test if a codepoint represents a lowercase letter

* argument `c` a codepoint value

Return a boolean success value

Note: only ASCII lowercase letters are supported

### `int qe_islower_(char32_t c);`

Test if a codepoint represents a lowercase letter or an underscore

* argument `c` a codepoint value

Return a boolean success value

Note: only ASCII lowercase letters are supported

### `int qe_isoctdigit(char32_t c);`

Test if a codepoint represents an octal digit

* argument `c` a codepoint value

Return a boolean success value

Note: only ASCII digits are supported

### `int qe_isoctdigit_(char32_t c);`

Test if a codepoint represents an octal digit or an underscore

* argument `c` a codepoint value

Return a boolean success value

Note: only ASCII digits are supported

### `int qe_isspace(char32_t c);`

Test if a codepoint represents white space

* argument `c` a codepoint value

Return a boolean value indicating if the codepoint is white space

Note: only ASCII whitespace and non-breaking-space are supported

### `int qe_isupper(char32_t c);`

Test if a codepoint represents an uppercase letter

* argument `c` a codepoint value

Return a boolean success value

Note: only ASCII uppercase letters are supported

### `int qe_isupper_(char32_t c);`

Test if a codepoint represents an uppercase letter or an underscore

* argument `c` a codepoint value

Return a boolean success value

Note: only ASCII uppercase letters are supported

### `int qe_isword(char32_t c);`

Test if a codepoint value is part of a _word_

* argument `c` a codepoint value

Return a boolean success value

Note: _word_ characters are letters, digits, underscore and any
non ASCII codepoints. This is oversimplistic, we should use tables for
better Unicode support.  The definition of _word_ characters should
depend on the current mode.

### `int qe_isxdigit(char32_t c);`

Test if a codepoint represents a hexadecimal digit

* argument `c` a codepoint value

Return a boolean success value

Note: only ASCII digits and letters are supported

### `int qe_isxdigit_(char32_t c);`

Test if a codepoint represents a hexadecimal digit or an underscore

* argument `c` a codepoint value

Return a boolean success value

Note: only ASCII digits and letters are supported

### `int qe_match2(char32_t c, char32_t c1, char32_t c2);`

Test if a codepoint value is one of 2 specified values

* argument `c` a codepoint value

* argument `c1` a codepoint value

* argument `c2` a codepoint value

Return a boolean success value

### `char32_t qe_tolower(char32_t c);`

Convert an uppercase letter to the corresponding lowercase letter

* argument `c` a codepoint value

Return the converted letter or `c` if it is not an uppercase letter

Note: only ASCII letters are supported

### `char32_t qe_toupper(char32_t c);`

Convert a lowercase letter to the corresponding uppercase letter

* argument `c` a codepoint value

Return the converted letter or `c` if it is not a lowercase letter

Note: only ASCII letters are supported

### `void qe_free(T **pp);`

Free the allocated memory pointed to by a pointer whose address is passed.

* argument `pp` the address of a possibly null pointer. This pointer is set
to `NULL` after freeing the memory. If the pointer memory is null,
nothing happens.

* argument `n` the number of bytes to allocate in addition to the size of type `T`.

Note: this function is implemented as a macro.

### `T *qe_malloc(type T);`

Allocate memory for an object of type `T`.

* argument `T` the type of the object to allocate.

Note: this function is implemented as a macro.

### `T *qe_malloc_array(type T, size_t n);`

Allocate memory for an array of objects of type `T`.

* argument `T` the type of the object to allocate.

* argument `n` the number of elements to allocate.

Note: this function is implemented as a macro.

### `void *qe_malloc_bytes(size_t size);`

Allocate an uninitialized block of memory of a given size in
bytes.

* argument `size` the number of bytes to allocate.

Return a pointer to allocated memory, aligned on the maximum
alignment size.

### `void *qe_malloc_dup(const void *src, size_t size);`

Allocate a block of memory of a given size in bytes initialized
as a copy of an existing object.

* argument `src` a valid pointer to the object to duplicate.

* argument `size` the number of bytes to allocate.

Return a pointer to allocated memory, aligned on the maximum
alignment size.

### `T *qe_malloc_hack(type T, size_t n);`

Allocate memory for an object of type `T` with `n` extra bytes.

* argument `T` the type of the object to allocate.

* argument `n` the number of bytes to allocate in addition to the size of type `T`.

Note: this function is implemented as a macro.

### `T *qe_mallocz(type T);`

Allocate memory for an object of type `T`.
The object is initialized to all bits zero.

* argument `T` the type of the object to allocate.

Note: this function is implemented as a macro.

### `T *qe_mallocz_array(type T, size_t n);`

Allocate memory for an array of objects of type `T`.
The objects are initialized to all bits zero.

* argument `T` the type of the object to allocate.

* argument `n` the number of elements to allocate.

Note: this function is implemented as a macro.

### `T *qe_mallocz_array(type T, size_t n);`

Allocate memory for an object of type `T` with `n` extra bytes.
The object and the extra space is initialized to all bits zero.

* argument `T` the type of the object to allocate.

* argument `n` the number of bytes to allocate in addition to the size of type `T`.

Note: this function is implemented as a macro.

### `void *qe_mallocz_bytes(size_t size);`

Allocate a block of memory of a given size in bytes initialized
to all bits zero.

* argument `size` the number of bytes to allocate.

Return a pointer to allocated memory, aligned on the maximum
alignment size.

### `void *qe_realloc(void *pp, size_t size);`

reallocate a block of memory to a different size.

* argument `pp` the address of a possibly null pointer to a
block to reallocate. `pp` is updated with the new pointer
if reallocation is successful.

* argument `size` the new size for the object.

Return a pointer to allocated memory, aligned on the maximum
alignment size.

Note: this API makes it easier to check for success separately
from modifying the existing pointer, which is unchanged if
reallocation fails. This approach is not strictly conforming,
it assumes all pointers have the same size and representation,
which is mandated by POSIX.
We use memcpy to avoid compiler optimisation issues with the
syntax `*(void **)pp = p;` that violates the strict aliasing rule.

### `char *qe_strdup(const char *str);`

Allocate a copy of a string.

* argument `src` a valid pointer to a string to duplicate.

Return a pointer to allocated memory, aligned on the maximum
alignment size.

### `int eb_search(EditBuffer *b, int dir, int flags, int start_offset, int end_offset, const char32_t *buf, int len, CSSAbortFunc *abort_func, void *abort_opaque, int *found_offset, int *found_end);`

Search a buffer for contents. Return true if contents was found.

* argument `b` a valid EditBuffer pointer

* argument `dir` search direction: -1 for backward, 1 for forward

* argument `flags` a combination of SEARCH_FLAG_xxx values

* argument `start_offset` the starting offset in buffer

* argument `end_offset` the maximum offset in buffer

* argument `buf` a valid pointer to an array of `char32_t`

* argument `len` the length of the array `buf`

* argument `abort_func` a function pointer to test for abort request

* argument `abort_opaque` an opaque argument for `abort_func`

* argument `found_offset` a valid pointer to store the match
  starting offset

* argument `found_end` a valid pointer to store the match
  ending offset

Return non zero if the search was successful. Match starting and
ending offsets are stored to `start_offset` and `end_offset`.
Return `0` if search failed or `len` is zero.
Return `-1` if search was aborted.

### `int append_slash(char *buf, int buf_size);`

Append a trailing slash to a path if none there already.

Return the updated path length.

Note: truncation cannot be detected reliably

### `int byte_quote(char *dest, int size, unsigned char ch);`

Encode a byte as a source code escape sequence

* argument `dest` a valid pointer to an array of bytes

* argument `size` the length of the destination array

* argument `ch` a byte value to encode as source

Return the number of bytes produced in the destination array,
not counting the null terminator

### `void canonicalize_path(char *buf, int buf_size, const char *path);`

Normalize a path, removing redundant `.`, `..` and `/` parts.

* argument `buf` a pointer to the destination array

* argument `buf_size` the length of the destination array in bytes

* argument `path` a valid pointer to a string.

Note: this function accepts drive and protocol specifications.

Note: removing `..` may have adverse side effects if the parent
directory specified is a symbolic link.

### `int check_fcall(const char32_t *str, int i);`

Test if a parenthesis follows optional white space

* argument `str` a valid pointer to an array of codepoints

* argument `i` the index of the current codepoint

Return a boolean success value

### `int cp_match_keywords(const char32_t *str, int n, int start, const char *s, int *end);`

Match a sequence of words from a | separated list of phrases.
A space in the string matches a non empty white space sequence in the source array.
Phrases are delimited by `|` characters.

* argument `str` a valid pointer to an array of codepoints

* argument `start` the index to the next codepoint

* argument `n` the length of the codepoint array

* argument `s` a valid pointer to a string containing phrases delimited by `|`.

* argument `end` a valid pointer to store the index of the codepoint after the end of a match.

Return a boolean success indicator.

### `int cp_skip_blanks(const char32_t *str, int i, int n);`

Skip blank codepoints.

* argument `str` a valid pointer to an array of codepoints

* argument `i` the index to the next codepoint

* argument `n` the length of the codepoint array

Return the index to the next non blank codepoint or `n` if none are found.

### `char *file_load(const char *filename, int max_size, int *sizep);`

Load a file in memory, return allocated block and size.

* fail if file cannot be opened for reading,
* fail if file size is greater or equal to `max_size` (`errno` = `ERANGE`),
* fail if memory cannot be allocated,
* otherwise load the file contents into a block of memory,
  null terminate the block and return a pointer to allocated
  memory along with the number of bytes read.
Error codes are returned in `errno`.
Memory should be freed with `qe_free()`.

### `void find_file_close(FindFileState **sp);`

Close a directory enumeration state `FindFileState`.

* argument `sp` a valid pointer to a `FindFileState` pointer that
will be closed.

Note: `FindFileState` state structures must be freed to avoid memory
and resource leakage.

### `int find_file_next(FindFileState *s, char *filename, int filename_size_max);`

Get the next match in a directory enumeration.

* argument `filename` a valid pointer to an array for the file name.

* argument `filename_size_max` the length if the `filename` destination
array in bytes.

Return `0` if there is a match, `-1` if no more files matche the pattern.

### `FindFileState *find_file_open(const char *path, const char *pattern, int flags);`

Start a directory enumeration.

* argument `path` the initial directory for the enumeration.

* argument `pattern` a file pattern using `?` and `*` with the classic
semantics used by unix shells

Return a pointer to an opaque FindFileState structure.

### `const char *get_basename(const char *filename);`

Get the filename portion of a path.
Return a pointer to the first character of the filename part of
the path pointed to by string argument `path`.

Note: call this function for a constant string.

### `char *get_basename_nc(char *filename);`

Get the filename portion of a path.
Return a pointer to the first character of the filename part of
the path pointed to by string argument `path`.

Note: call this function for a modifiable string.

### `size_t get_basename_offset(const char *path);`

Get the offset of the filename component of a path.
Return the offset to the first character of the filename part of
the path pointed to by string argument `path`.

### `int get_c_identifier(char *buf, int buf_size, const char32_t *p, int flavor);`

Grab a C ASCII identifier from a char32_t buffer for a given flavor.

* argument `buf` a pointer to the destination array

* argument `buf_size` the length of the destination array in bytes

* argument `p` a valid pointer to an array of codepoints

* argument `flavor` the language variant for identifier syntax

Return the number of codepoints used in the source array.

Note: `dest` can be a null pointer if `size` is `0`.

Note: non-ASCII codepoints are accepted for CLANG_RUST but are not UTF-8 encoded

### `char *get_dirname(char *dest, int size, const char *file);`

Extract the directory portion of a path.
This leaves out the trailing slash if any.  The complete path is
obtained by concatenating `dirname` + `"/"` + `basename`.
If the original path doesn't contain a directory name, `"."` is
copied to `dest`.

Return a pointer to the destination array.

Note: truncation cannot be detected reliably.

Note: the trailing slash is not removed if the directory is the
root directory: this makes the behavior somewhat inconsistent,
requiring more tests when reconstructing the full path.

### `const char *get_extension(const char *filename);`

Get the filename extension portion of a path.
Return a pointer to the first character of the last extension of
the filename part of the path pointed to by string argument `path`.
If there is no extension, return a pointer to the null terminator
and the end of path.
Leading dots are skipped, they are not considered part of an extension.

Note: call this function for a constant string.

### `char *get_extension_nc(char *filename);`

Get the filename extension portion of a path.
Return a pointer to the first character of the last extension of
the filename part of the path pointed to by string argument `path`.
If there is no extension, return a pointer to the null terminator
and the end of path.
Leading dots are skipped, they are not considered part of an extension.

Note: call this function for a modifiable string.

### `size_t get_extension_offset(const char *path);`

Get the filename extension portion of a path.
Return the offset to the first character of the last extension of
the filename part of the path pointed to by string argument `path`.
If there is no extension, return a pointer to the null terminator
and the end of path.
Leading dots are skipped, they are not considered part of an extension.

### `int get_js_identifier(char *dest, int size, char32_t c, const char32_t *str, int i, int n);`

Grab an identifier from a char32_t buffer, accept non-ASCII identifiers
and encode in UTF-8.

* argument `dest` a pointer to the destination array

* argument `size` the length of the destination array in bytes

* argument `c` the initial code point or `0` if none

* argument `str` a valid pointer to an array of codepoints

* argument `i` the index to the next codepoint

* argument `n` the length of the codepoint array

Return the number of codepoints used in the source array.

Note: `dest` can be a null pointer if `size` is `0`.

### `int get_str(const char **pp, char *buf, int buf_size, const char *stop);`

Get a token from a string, stop on a set of characters and white-space.
Skip spaces before and after the token. Return the token length.


* argument `pp` the address of a valid pointer to the current position
in the source string

* argument `buf` a pointer to a destination array.

* argument `buf_size` the length of the destination array.

* argument `stop` a valid string pointer containing separator characters.

Return the length of the token stored into buf.

Note: token truncation cannot be easily detected.

### `int is_directory(const char *path);`

Check if the string pointed to by `path` is the name of a
directory.

* argument `path` a valid pointer to a string.

Return `true` if `path` is the name of a directory, `false` otherwise.

Note: this function uses `stat`, so it will return `true` for
directories and symbolic links pointing to existing directories.

### `int is_filepattern(const char *filespec);`

Check if the string pointed to by `filespec` is a file pattern.

* argument `filespec` a valid pointer to a string.

Return `true` if `filespec` contains wildcard characters.

Note: this function only recognises `?` and `*` wildcard characters

### `char *make_user_path(char *buf, int buf_size, const char *path);`

Reduce a path relative to the user's homedir, using the `~`
shell syntax.

* argument `buf` a pointer to the destination array

* argument `buf_size` the length of the destination array in bytes

* argument `path` a valid pointer to a string.

Return a pointer to the destination array.

Note: this function uses the `HOME` environment variable to
determine the user's home directory

### `char *makepath(char *buf, int buf_size, const char *path, const char *filename);`

Construct a path from a directory name and a filename into the
array pointed to by `buf` of length `buf_size` bytes.

Return a pointer to the destination array.

Note: truncation cannot be detected reliably

### `int match_extension(const char *filename, const char *extlist);`

Return `true` iff the filename extension appears in `|` separated
list pointed to by `extlist`.
* Initial and final `|` do not match an empty extension, but `||` does.
* Multiple tacked extensions may appear un extlist eg. `|tar.gz|`
* Initial dots do not account as extension delimiters.
* `.` and `..` do not have an empty extension, nor do they match `||`

### `int match_shell_handler(const char *p, const char *list);`

Return `true` iff the command name invoked by the `#!` line pointed to by `p`
matches one of the commands in `|` separated list pointed to by `list`.
* both `#!/bin/perl` and `#!/bin/env perl` styles match list `"perl"`

### `int match_strings(const char *s1, const char *s2, int len);`

Find the length of the common prefix, only count complete UTF-8
sequences.

* argument `s1` a valid string pointer

* argument `s2` a valid string pointer

* argument `len` the maximum number of bytes to compare. This count
is assumed to only include complete UTF-8 sequences.

Return the length of the common prefix, between `0` and `len`.

### `int memfind(const char *list, const char *s, int len);`

Find a string fragment in a list of words separated by `|`.
An initial or trailing `|` do not match the empty string, but `||` does.

* argument `list` a string of words separated by `|` characters.

* argument `s` a valid string pointer.

* argument `len` the number of bytes to consider in `s`.

Return 1 if there is a match, 0 otherwise.

### `const void *memstr(const void *buf, int size, const char *str);`

Find a string in a chunk of memory.

* argument `buf` a valid pointer to the block of memory in which to
search for matches.

* argument `size` the length in bytes of the memory block.

* argument `str` a valid string pointer for the string to search.

Return a pointer to the first character of the match if found,
`NULL` otherwise.

### `char *pstrcat(char *buf, int size, const char *s);`

Copy the string pointed by `s` at the end of the string contained
in the destination array `buf`, of length `size` bytes,
truncating excess bytes.

Return a pointer to the destination array.

Note: truncation cannot be detected reliably.

Note: `strncat` has different semantics and does not check
for potential overflow of the destination array.

### `char *pstrcpy(char *buf, int size, const char *str);`

Copy the string pointed by `str` to the destination array `buf`,
of length `size` bytes, truncating excess bytes.


* argument `buf` destination array, must be a valid pointer.

* argument `size` length of destination array in bytes.

* argument `str` pointer to a source string, must be a valid pointer.

Return a pointer to the destination array.

Note: truncation cannot be detected reliably.

Note: this function does what many programmers wrongly expect
`strncpy` to do. `strncpy` has different semantics and does not
null terminate the destination array in case of excess bytes.
**NEVER use `strncpy`**.

### `char *pstrncat(char *buf, int size, const char *s, int slen);`

Copy at most `len` bytes from the string pointed by `s` at the end
of the string contained in the destination array `buf`, of length
`size` bytes, truncating excess bytes.

Return a pointer to the destination array.

Note: truncation cannot be detected reliably.

### `char *pstrncpy(char *buf, int size, const char *s, int len);`

Copy at most `len` bytes from the string pointed by `s` to the
destination array `buf`, of length `size` bytes, truncating
excess bytes.

Return a pointer to the destination array.

Note: truncation cannot be detected reliably.

### `int qe_haslower(const char *str);`

Check if a C string contains has ASCII lowercase letters.

* argument `str` a valid pointer to a C string

Return a boolean value, non zero if and only if the string contains
ASCII lowercase letters.

### `int qe_memicmp(const void *p1, const void *p2, size_t count);`

Perform a case independent comparison of blocks of memory.

* argument `p1` a valid pointer to the first block.

* argument `p2` a valid pointer to the second block.

* argument `count` the length in bytes of the blocks to compare.

Return `0` is the blocks compare equal, ignoring case,

Return a negative value if the first block compares below the second,

Return a positive value if the first block compares above the second.

Note: this version only handles ASCII.

### `void qe_qsort_r(void *base, size_t nmemb, size_t size, void *thunk, int (*compare)(void *, const void *, const void *));`

Sort an array using a comparison function with an extra opaque
argument.

* argument `base` a valid pointer to an array of objects,

* argument `nmemb` the number of elements in the array,

* argument `size` the object size in bytes,

* argument `thunk` the generic argument to pass to the comparison
function,

* argument `compare` a function pointer for a comparison function
taking 3 arguments: the `thunk` argument and pointers to 2
objects from the array, returning an integer whose sign indicates
their relative position according to the sort order.

Note: this function behaves like OpenBSD's `qsort_r()`, the
implementation is non recursive using a combination of quicksort
and insertion sort for small chunks. The GNU lib C on linux also
has a function `qsort_r()` with similar semantics but a different
calling convention.

### `int qe_skip_spaces(const char **pp);`

Skip white space at the beginning of the string pointed to by `*pp`.

* argument `pp` the address of a valid string pointer. The pointer will
be updated to point after any initial white space.

Return the character value after the white space as an `unsigned char`.

### `int qe_strcollate(const char *s1, const char *s2);`

Compare 2 strings using special rules:
* use lexicographical order
* collate sequences of digits in numerical order.
* push `*` at the end.

### `const char *qe_stristr(const char *s1, const char *s2);`

Find an ASCII string in another ASCII string, ignoring case.

* argument `s1` a valid pointer to the string in which to
search for matches.

* argument `s2` a valid string pointer for the string to search.

Return a pointer to the first character of the match if found,
`NULL` otherwise.

Note: this version only handles ASCII.

### `int qe_strtobool(const char *s, int def);`

Determine the boolean value of a response string.

* argument `s` a possibly null pointer to a string,

* argument `def` the default value if `s` is null or an empty string,

Return `true` for `y`, `yes`, `t`, `true` and `1`, case independenty,
return `false` for other non empty contents and return the default
value `def` otherwise.

### `void qe_strtolower(char *buf, int size, const char *str);`

Convert an ASCII string to lowercase using `qe_tolower7` for
each byte.

* argument `buf` a valid pointer to a destination char array.

* argument `size` the length of the destination array in bytes,

* argument `str` a valid pointer to a string to convert.

Note: this version only handles ASCII.

### `int remove_slash(char *buf);`

Remove the trailing slash from path, except for / directory.

Return the updated path length.

### `void splitpath(char *dirname, int dirname_size, char *filename, int filename_size, const char *pathname);`

Split the path pointed to by `pathname` into a directory part and a
filename part.

Note: `dirname` will receive an empty string if `pathname` contains
just a filename.

### `int strend(const char *str, const char *val, const char **ptr);`

Test if `val` is a suffix of `str`.

if `val` is a suffix of `str`, a pointer to the first character
of the suffix in `str` is stored into `ptr` provided `ptr` is
not a null pointer.


* argument `str` input string, must be a valid pointer.

* argument `val` suffix string, must be a valid pointer.

* argument `ptr` updated to the suffix in `str` if there is a match.

Return `true` if there is a match, `false` otherwise.

### `int strfind(const char *keytable, const char *str);`

Find a string in a list of words separated by `|`.
An initial or trailing `|` do not match the empty string, but `||` does.

* argument `list` a string of words separated by `|` characters.

* argument `str` a valid string pointer.

Return 1 if there is a match, 0 otherwise.

### `void strip_extension(char *filename);`

Strip the filename extension portion of a path.
Leading dots are skipped, they are not considered part of an extension.

### `int stristart(const char *str, const char *val, const char **ptr);`

Test if `val` is a prefix of `str` (case independent for ASCII).
If there is a match, a pointer to the next character after the
match in `str` is stored into `ptr` provided `ptr` is not null.

* argument `str` valid string pointer,

* argument `val` valid string pointer to the prefix to test,

* argument `ptr` a possibly null pointer to a `const char *` to set
to point after the prefix in `str` in there is a match.

Return `true` if there is a match, `false` otherwise.

### `int strmatch_pat(const char *str, const char *pat, int start);`

Check if the pattern `pat` matches `str` or a prefix of `str`.
Patterns use only `*` as a wildcard, to match any sequence of
characters.

* argument `str` a valid string pointer.

* argument `pat` a valid string pointer for the pattern to test.

* argument `start` a non zero integer if the function should return
`1` for a partial match at the start of `str.

Return `1` if there is a match, `0` otherwise.

### `int strmatchword(const char *str, const char *val, const char **ptr);`

Check if `val` is a word prefix of `str`. In this case, return
`true` and store a pointer to the first character after the prefix
in `str` into `ptr` provided `ptr` is not a null pointer.

If `val` is not a word prefix of `str`, return `false` and leave `*ptr`
unchanged.


* argument `str` a valid string pointer.

* argument `val` a valid string pointer for the prefix to test.

* argument `ptr` updated with a pointer past the prefix in `str` if found.

Return `true` if there is a match, `false` otherwise.

### `const char *strmem(const char *str, const void *mem, int size);`

Find a chunk of characters inside a string.

* argument `str` a valid string pointer in which to search for matches.

* argument `mem` a pointer to a chunk of bytes to search.

* argument `size` the length in bytes of the chuck to search.

Return a pointer to the first character of the match if found,
`NULL` otherwise.

### `int strquote(char *dest, int size, const char *str, int len);`

Encode a string using source code escape sequences

* argument `dest` a valid pointer to an array of bytes

* argument `size` the length of the destination array

* argument `src` a pointer to a string to encode

* argument `len` the number of bytes to encode

Return the length of the converted string, not counting the null
terminator, possibly longer than the destination array length.

Note: if `src` is a null pointer, the string `null` is output
otherwise a double quoted string is produced.

### `int strstart(const char *str, const char *val, const char **ptr);`

Test if `val` is a prefix of `str`.

If `val` is a prefix of `str`, a pointer to the first character
after the prefix in `str` is stored into `ptr` provided `ptr`
is not a null pointer.

If `val` is not a prefix of `str`, return `0` and leave `*ptr`
unchanged.


* argument `str` input string, must be a valid pointer.

* argument `val` prefix string, must be a valid pointer.

* argument `ptr` updated with a pointer past the prefix if found.

Return `true` if there is a match, `false` otherwise.

### `double strtod_c(const char *str, const char **endptr);`

Convert the number in the string pointed to by `str` as a `double`.
Call this function with a constant string and the address of a `const char *`.

### `long strtol_c(const char *str, const char **endptr, int base);`

Convert the number in the string pointed to by `str` as a `long`.
Call this function with a constant string and the address of a `const char *`.

### `long double strtold_c(const char *str, const char **endptr);`

Convert the number in the string pointed to by `str` as a `long double`.
Call this function with a constant string and the address of a `const char *`.

### `long strtoll_c(const char *str, const char **endptr, int base);`

Convert the number in the string pointed to by `str` as a `long long`.
Call this function with a constant string and the address of a `const char *`.

### `int strxcmp(const char *str1, const char *str2);`

Compare strings case independently (for ASCII), also ignoring
spaces, dashes and underscores.

* argument `str1` a valid string pointer for the left operand.

* argument `str2` a valid string pointer for the right operand.

Return a negative, 0 or positive value reflecting the sign
of `str1 <=> str2`

### `int strxfind(const char *list, const char *s);`

Find a string in a list of words separated by `|`, ignoring case
for ASCII and skipping `-` , `_` and spaces.
An initial or trailing `|` do not match the empty string, but `||` does.

* argument `list` a string of words separated by `|` characters.

* argument `s` a valid string pointer for the string to search.

Return 1 if there is a match, 0 otherwise.

Note: this function only handles case insensitive matching for ASCII.

### `int strxstart(const char *str, const char *val, const char **ptr);`

Test if `val` is a prefix of `str` (case independent for ASCII
and ignoring `-`, `_` and spaces).  If there is a match, a pointer
to the next character after the match in `str` is stored into `ptr`,
provided `ptr` is not null.

* argument `str` valid string pointer,

* argument `val` valid string pointer to the prefix to test,

* argument `ptr` a possibly null pointer to a `const char *` to set
to point after the prefix in `str` in there is a match.

Return `true` if there is a match, `false` otherwise.

### `int umemcmp(const char32_t *s1, const char32_t *s2, size_t count);`

Compare two blocks of code points and return an integer indicative of
their relative order.

* argument `s1` a valid wide string pointer.

* argument `s2` a valid wide string pointer.

* argument `count` the maximum number of code points to compare.

Return `0` if the strings compare equal, a negative value if `s1` is
lexicographically before `s2` and a positive number otherwise.

### `int ustr_get_identifier(char *dest, int size, char32_t c, const char32_t *str, int i, int n);`

Extract an ASCII identifier from a wide string into a char array.

* argument `dest` a valid pointer to a destination array.

* argument `size` the length of the destination array.

* argument `c` the first codepoint to copy.

* argument `str` a valid wide string pointer.

* argument `i` the offset of the first codepoint to copy.

* argument `n` the offset to the end of the wide string.

Return the length of the identifier present in the source string.

Note: the return value can be larger than the destination array length.
In this case, the destination array contains a truncated string, null
terminated unless `size <= 0`.

### `int ustr_get_identifier_lc(char *dest, int size, char32_t c, const char32_t *str, int i, int n);`

Extract an ASCII identifier from a wide string into a char array and
convert it to lowercase.

* argument `dest` a valid pointer to a destination array.

* argument `size` the length of the destination array.

* argument `c` the first code point to copy.

* argument `str` a valid wide string pointer.

* argument `i` the offset of the first code point to copy.

* argument `n` the offset to the end of the wide string.

Return the length of the identifier present in the source string.

Note: the return value can be larger than the destination array length.
In this case, the destination array contains a truncated string, null
terminated unless `size <= 0`.

### `int ustr_get_identifier_x(char *dest, int size, char32_t c, const char32_t *str, int i, int n, char32_t c1);`

Extract an ASCII identifier from a wide string into a char array.

* argument `dest` a valid pointer to a destination array.

* argument `size` the length of the destination array.

* argument `c` the first codepoint to copy.

* argument `str` a valid wide string pointer.

* argument `i` the offset of the first codepoint to copy.

* argument `n` the offset to the end of the wide string.

* argument `c1` a codepoint value to match in addition to `isalnum_`

Return the length of the identifier present in the source string.

Note: the return value can be larger than the destination array length.
In this case, the destination array contains a truncated string, null
terminated unless `size <= 0`.

### `int ustr_match_keyword(const char32_t *str, const char *keyword, int *lenp);`

Match a keyword in a wide string.

* argument `str` a valid wide string pointer.

* argument `keyword` a valid string pointer.

* argument `lenp` a pointer to store the length if matched.

Return a boolean success value.

Note: the keyword is assumed to contain only ASCII characters.
A match requires a string match not followed by a valid ASCII identifier
character.

### `int ustristart(const char32_t *str0, const char *val, int *lenp);`

Test if `val` is a prefix of `str0`. Comparison is perform ignoring case.

If `val` is a prefix of `str`, the length of the prefix is stored into
`*lenp`, provided `lenp` is not a null pointer, and return `1`.

If `val` is not a prefix of `str`, return `0` and leave `*lenp`
unchanged.


* argument `str0` input string, must be a valid pointer to a null terminated code point array.

* argument `val` prefix string, must be a valid string pointer.

* argument `lenp` updated with the length of the prefix if found.

Return `true` if there is a match, `false` otherwise.

Note: val is assumed to be contain ASCII only.

### `const char32_t *ustristr(const char32_t *str, const char *val);`

Find a string of characters inside a string of code points ignoring case.

* argument `str` a valid wide string pointer in which to search for matches.

* argument `val` a valid string pointer to a subtring to search for.

Return a pointer to the first code point of the match if found,
`NULL` otherwise.

Note: val is assumed to be contain ASCII only.

### `int ustrstart(const char32_t *str0, const char *val, int *lenp);`

Test if `val` is a prefix of `str0`.

If `val` is a prefix of `str`, the length of the prefix is stored into
`*lenp`, provided `lenp` is not a null pointer, and return `1`.

If `val` is not a prefix of `str`, return `0` and leave `*lenp`
unchanged.


* argument `str0` input string, must be a valid pointer to a null terminated code point array.

* argument `val` prefix string, must be a valid string pointer.

* argument `lenp` updated with the length of the prefix if found.

Return `true` if there is a match, `false` otherwise.

### `const char32_t *ustrstr(const char32_t *str, const char *val);`

Find a string of characters inside a string of code points.

* argument `str` a valid wide string pointer in which to search for matches.

* argument `val` a valid string pointer to a subtring to search for.

Return a pointer to the first code point of the match if found,
`NULL` otherwise.

### `char32_t utf8_decode(const char **pp);`

Return the UTF-8 encoded code point at `*pp` and increment `*pp`
to point to the next code point.
Lax decoding is performed:
- stray trailing bytes 0x80..0xBF return a single byte
- overlong encodings, surrogates and special codes are accepted
- 32-bit codes are produced by 0xFE and 0xFF lead bytes if followed
by 5 trailing bytes

### `char32_t utf8_decode_strict(const char **pp);`

Return the UTF-8 encoded code point at `*pp` and increment `*pp`
to point to the next code point.
Strict decoding is performed, any encoding error returns INVALID_CHAR:
- invalid lead bytes 0x80..0xC1, 0xF8..0xFF
- overlong encodings
- low and high surrogate codes
- special codes 0xfffe and 0xffff
- code points beyond CHARCODE_MAX

### `int utf8_get_word(char *dest, int size, char32_t c, const char32_t *str, int i, int n);`

Extract a word from a wide string into a char array.
Non ASCII code points are UTF-8 encoded.

* argument `dest` a valid pointer to a destination array.

* argument `size` the length of the destination array.

* argument `c` the first code point to copy.

* argument `str` a valid wide string pointer.

* argument `i` the offset of the first code point to copy.

* argument `n` the offset to the end of the wide string.

Return the length of the identifier present in the source string.

Note: the return value can be larger than the destination array length.
In this case, the destination array contains a truncated string, null
terminated unless `size <= 0`.

### `int utf8_prefix_len(const char *str1, const char *str2);`

Return the length in bytes of an intial common prefix of `str1` and `str2`.


* argument `str1` must be a valid UTF-8 string pointer.

* argument `str2` must be a valid UTF-8 string pointer.

### `int utf8_strimatch_pat(const char *str, const char *pat, int start);`

Check if the pattern `pat` matches `str` or a prefix of `str`,
using a case insensitive comparison.  Patterns use only `*` as
a wildcard, to match any sequence of characters.
Accents are also ignored by this function.

* argument `str` a valid string pointer.

* argument `pat` a valid string pointer for the pattern to test.

* argument `start` a non zero integer if the function should return
`1` for a partial match at the start of `str.

Return `1` if there is a match, `0` otherwise.

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
