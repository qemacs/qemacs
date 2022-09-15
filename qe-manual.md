
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

### `query-replace(string FROM-STRING, string TO-STRING,
               int DELIMITED=argval, int START=point, int END=end)`

Replace some occurrences of FROM-STRING with TO-STRING.
As each match is found, the user must type a character saying
what to do with it.  For directions, type '?' at that time.

Matching is independent of case if `case-fold-search` is non-zero and
FROM-STRING has no uppercase letters.  Replacement transfers the case
pattern of the old text to the new text, if `case-replace` and
`case-fold-search` are non-zero and FROM-STRING has no uppercase
letters.  (Transferring the case pattern means that if the old text
matched is all caps, or capitalized, then its replacement is upcased
or capitalized.)

Third arg DELIMITED (prefix arg if interactive), if non-zero, means
replace only matches surrounded by word boundaries.

Fourth and fifth arg START and END specify the region to operate on:
if these arguments are not provided, if the current region is
highlighted, operate on the contents of the region, otherwise,
operate from point to the end of the buffer.

To customize possible responses, change the "bindings" in
`query-replace-mode`.

### `replace-string(string FROM-STRING, string TO-STRING,
                int DELIMITED=argval, int START=point, int END=end)`

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

### `char *qe_strdup(const char *str);`

Allocate a copy of a string.

* argument `src` a valid pointer to a string to duplicate.

Return a pointer to allocated memory, aligned on the maximum
alignment size.

### `int append_slash(char *buf, int buf_size);`

Append a trailing slash to a path if none there already.

Return the updated path length.

### `void canonicalize_path(char *buf, int buf_size, const char *path);`

Normalize a path, removing redundant `.`, `..` and `/` parts.

* argument `buf` a pointer to the destination array

* argument `buf_size` the length of the destination array in bytes

* argument `path` a valid pointer to a string.

Note: this function accepts drive and protocol specifications.

Note: removing `..` may have adverse side effects if the parent
directory specified is a symbolic link.

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

Get the filename portion of a path.
Return the offset to the first character of the filename part of
the path pointed to by string argument `path`.

### `char *get_dirname(char *dest, int size, const char *file);`

Extract the directory portion of a path.
This leaves out the trailing slash if any.  The complete path is
obtained by catenating `dirname` + `"/"` + `basename`.
If the original path doesn't contain a directory name, `"."` is
copied to `dest`.

Return a pointer to the destination array.

Note: truncation cannot be detected reliably.

Note: the trailing slash is not removed if the directory is the
root directory: this make the behavior somewhat inconsistent,
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

### `char *pstrcpy(char *buf, int size, const char *str);`

Copy the string pointed by `str` to the destination array `buf`,
of length `size` bytes, truncating excess bytes.


* argument `buf` destination array, must be a valid pointer.

* argument `size` length of destination array.

* argument `str` pointer to a source string, must be a valid pointer.

Return a pointer to the destination array.

Note: truncation cannot be detected reliably.

Note: this function does what `strncpy` should have done to be
useful. **NEVER use `strncpy`**.

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

Find a string in another string, ignoring case.

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

Convert a string to lowercase using `qe_tolower` for each byte.

* argument `buf` a valid pointer to a destination array.

* argument `size` the length of the destination array in bytes,

* argument `str` a valid pointer to a string to convert.

Note: this version only handles ASCII.

### `int remove_slash(char *buf);`

Remove the trailing slash from path, except for / directory.

Return the updated path length.

### `void splitpath(char *dirname, int dirname_size, char *filename, int filename_size, const char *pathname);`

Split the path pointed to by `pathname` into a directory part and a
filename part.

Note: `dirname` will receive an empty string if `pathname` constains
just a filename.

### `int strend(const char *str, const char *val, const char **ptr);`

Check if `val` is a suffix of `str`. In this case, a
pointer to the first character of the suffix in `str` is stored
into `ptr` provided `ptr` is not a null pointer.


* argument `str` input string, must be a valid pointer.

* argument `val` suffix to test, must be a valid pointer.

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

Test if `val` is a prefix of `str` (case independent).
If there is a match, a pointer to the next character after the
match in `str` is stored into `ptr` provided `ptr` is not null.

* argument `str` valid string pointer,

* argument `val` valid string pointer to the prefix to test,

* argument `ptr` a possibly null pointer to a `const char *` to set
to point after the prefix in `str` in there is a match.

Return `true` if there is a match, `false` otherwise.

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

### `int strstart(const char *str, const char *val, const char **ptr);`

Check if `val` is a prefix of `str`. In this case, a
pointer to the first character after the prefix in `str` is
stored into `ptr` provided `ptr` is not a null pointer.

If `val` is not a prefix of `str`, return `0` and leave `*ptr`
unchanged.


* argument `str` input string, must be a valid pointer.

* argument `val` prefix to test, must be a valid pointer.

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

Compare strings case independently, also ignoring spaces, dashes
and underscores.

* argument `str1` a valid string pointer for the left operand.

* argument `str2` a valid string pointer for the right operand.

Return a negative, 0 or positive value reflecting the sign
of `str1 <=> str2`

### `int strxfind(const char *list, const char *s);`

Find a string in a list of words separated by `|`, ignoring case
and skipping `-` , `_` and spaces.
An initial or trailing `|` do not match the empty string, but `||` does.

* argument `list` a string of words separated by `|` characters.

* argument `s` a valid string pointer for the string to search.

Return 1 if there is a match, 0 otherwise.

### `int strxstart(const char *str, const char *val, const char **ptr);`

Test if `val` is a prefix of `str` (case independent and ignoring
`-`, `_` and spaces). If there is a match, a pointer to the next
character after the match in `str` is stored into `ptr`, provided
`ptr` is not null.

* argument `str` valid string pointer,

* argument `val` valid string pointer to the prefix to test,

* argument `ptr` a possibly null pointer to a `const char *` to set
to point after the prefix in `str` in there is a match.

Return `true` if there is a match, `false` otherwise.
