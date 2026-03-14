/*
 * Lisp Source mode for QEmacs.
 *
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

#include "qe.h"

/* TODO: lisp-indent = 2 */

#define LISP_LANG_LISP     1
#define LISP_LANG_ELISP    2
#define LISP_LANG_SCHEME   4
#define LISP_LANG_RACKET   8
#define LISP_LANG_CLOJURE  16
#define LISP_LANG_SANDBOX  32
#define LISP_LANG_XAOS     64
#define LISP_LANG_JANET    128

/*---------------- Lisp colors ----------------*/

static const char lisp_keywords[] = {
    "defun|let|let*|set|setq|prog1|progn|cond|if|unless|when|while|"
    "defsubst|remove|otherwise|dolist|incf|decf|boundp|"
    "and|or|not|case|eq|"
    "cons|list|concat|car|cdr|setcar|setcdr|nth|nthcdr|length|"
    "reverse|sort|"
    "caar|cadr|cdar|cddr|caddr|cadddr|"
    "lambda|"
    "\xCE\xBB|"  /* greek character lambda in UTF-8 */
    "mod|abs|max|min|log|logand|logior|logxor|ash|in|"
    "0+|1+|1-|<|>|<=|>=|-|+|*|/|=|<>|/=|"
};

static const char elisp_keywords[] = {
    /* elisp primitives */
    "eval|load|read|null|get|put|catch|throw|unwind-protect|atom|delete|"
    "dotimes|aset|aref|setplist|equal|fset|setq-default|pcase-let|"
    "consp|vectorp|listp|symbolp|stringp|numberp|zerop|functionp|integerp|"
    "assq|apply|funcall|mapatoms|mapc|mapcar|mapconcat|plist-get|plist-put|"
    "function|intern|intern-soft|copy-sequence|delete-dups|fboundp|"
    "push|pop|append|nconc|nreverse|memq|delq|remq|pcase|assoc|member|"
    "defalias|defgroup|defmacro|type-of|condition-case|declare-function|"
    "make-obsolete-variable|define-obsolete-variable-alias|set-default|"
    "default-boundp|default-value|car-safe|cdr-safe|"
    "make-variable-buffer-local|make-local-variable|local-variable-p|"
    "symbol-value|symbol-name|symbol-function|symbol-plist|"
    "string-match|downcase|upcase|string<|string=|format|substring|"
    "string-lessp|string-as-unibyte|"
    "format-time-string|current-time-string|"
    "string-to-number|number-to-string|read-from-string|char-to-string|"
    "make-string|string-to-char|string-equal|split-string|"
    "regexp-quote|"
    "make-vector|vector|vconcat|add-to-list|"
    /* emacs specific stuff */
    "eval-when-compile|assert|cl-assert|cl-pushnew|require|provide|"
    "interactive|save-excursion|save-restriction|error|message|sit-for|"
    "save-match-data|narrow-to-region|widen|"
    "call-interactively|run-hooks|add-hook|remove-hook|run-hook-with-args|"
    "defcustom|defvar|defconst|featurep|defvar-local|commandp|macrop|"
    "define-minor-mode|define-derived-mode|define-major-mode|"
    "define-key|make-keymap|make-sparse-keymap|key-binding|keymapp|lookup-key|"
    /* editing primitives */
    "marker-position|move-marker|copy-marker|set-marker|markerp|point-marker|"
    "mark|point|goto-char|char-after|preceding-char|following-char|"
    "current-column|"
    "move-beginning-of-line|move-end-of-line|beginning-of-line|end-of-line|"
    "count-lines|line-move|open-line|newline|"
    "region-beginning|region-end|line-beginning-position|line-end-position|"
    "line-beginning|line-end|bobp|eobp|bolp|eolp|"
    "forward-word|backward-word|forward-line|backward-line|"
    "forward-char|backward-char|skip-chars-forward|"
    "push-mark|point-min|point-max|exchange-point-and-mark|"
    "match-beginning|match-end|match-data|set-match-data|replace-match|"
    "search-forward|search-backward|re-search-forward|re-search-backward|"
    "looking-at|looking-back|"
    "display-buffer|erase-buffer|current-buffer|"
    "buffer-modified-p|set-buffer-modified-p|"
    "save-current-buffer|buffer-file-name|buffer-name|buffer-list|buffer-size|"
    "set-buffer|"
    "switch-to-buffer|get-buffer-create|kill-buffer|"
    "buffer-substring|buffer-substring-no-properties|"
    "set-text-properties|remove-text-properties|"
    "get-char-property|get-text-property|put-text-property|"
    "read-char|read-string|read-file-name|y-or-n-p|yes-or-no-p|"
    "completing-read|"
    "expand-file-name|file-name-directory|"
    "with-current-buffer|with-temp-buffer|"
    "with-syntax-table|syntax-table|standard-syntax-table|"
    "skip-syntax-forward|skip-syntax-backward|skip-chars-backward|"
    "insert|delete-char|delete-region|prin1|princ|terpri|indent-to|"
    "insert-file-contents|"
    "downcase-region|upcase-region|upcase-initials-region|"
    "delete-horizontal-space|kill-line|kill-region|yank|yank-pop|"
    "move-to-column|align|align-column|align-region|indent-region|"
    "write-region|undo-boundary|undo-in-progress|window-minibuffer-p|"
    "make-overlay|delete-overlay|remove-overlays|move-overlay|"
    "overlay-start|overlay-end|overlays-at|overlay-get|overlay-put|"
    "overlay-buffer|overlay-properties|"
    "define-abbrev|abbrev-get|abbrev-put|"
    "defface|make-face|set-face-property|facep|"
    "call-process|make-directory|delete-file|find-file|user-error|"
    "set-version-in-file|rx|submatch|read-directory-name|read-number|"
    "save-buffer|display-warning|file-readable-p|file-exists-p|"
    "file-directory-p|file-relative-name|make-text-button|"
    "string-prefix-p|sort-lines|write-file|pop-to-buffer|"
    "directory-files|default-directory|match-string|file-name-nondirectory|"
    "process-lines|emacs-major-version|emacs-minor-version|"
    "ignore-errors|define-button-type|button-get|find-file-noselect|"
    "eval-after-load|register-input-method|propertize|get-buffer|"
    "set-buffer-multibyte|current-time|read-event|noninteractive|"
    "frame-live-p|buffer-live-p|make-frame|selected-frame|select-frame|"
    "select-window|save-window-excursion|get-buffer-window|interactive-p|"
    "point-at-bol|load-file|locate-library|temp-directory|overlay|reparse-symbol|"
    "toggle-read-only|font-lock-mode|defimage|deftheme|defclass|defstruct|"
    "autoload|"
};

static const char scheme_keywords[] = {
    ""
};

static const char racket_keywords[] = {
    ""
};

static const char clojure_keywords[] = {
    ""
};

static const char sandbox_keywords[] = {
    ""
};

static const char lisp_types[] = {
    "nil|t|"
};

static const char elisp_types[] = {
    "nil|t|&optional|"
};

enum {
    IN_LISP_LEVEL    = 0x1F,    /* for IN_LISP_SCOMMENT */
    IN_LISP_COMMENT  = 0x20,
    IN_LISP_STRING   = 0x40,
    IN_LISP_SCOMMENT = 0x80,
};

enum {
    LISP_STYLE_TEXT       = QE_STYLE_DEFAULT,
    LISP_STYLE_COMMENT    = QE_STYLE_COMMENT,
    LISP_STYLE_SCOMMENT   = QE_STYLE_COMMENT,
    LISP_STYLE_NUMBER     = QE_STYLE_NUMBER,
    LISP_STYLE_STRING     = QE_STYLE_STRING,
    LISP_STYLE_CHARCONST  = QE_STYLE_STRING_Q,
    LISP_STYLE_KEYWORD    = QE_STYLE_KEYWORD,
    LISP_STYLE_TYPE       = QE_STYLE_TYPE,
    LISP_STYLE_QSYMBOL    = QE_STYLE_PREPROCESS,
    LISP_STYLE_MACRO      = QE_STYLE_TAG,
    LISP_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
};

static int lisp_get_symbol(char *buf, int buf_size, const char32_t *p)
{
    buf_t outbuf, *out;
    char32_t c;
    int i;

    out = buf_init(&outbuf, buf, buf_size);

    for (i = 0; (c = p[i]) != '\0'; i++) {
        if (qe_isblank(c) || qe_findchar(";(){}[]#'`,\"", c))
            break;
        buf_putc_utf8(out, c);
    }
    return i;
}

static int lisp_is_number(const char *str)
{
    int i;

    if (*str == 'b' && str[1]) {
        for (str++; qe_isbindigit(*str); str++)
            continue;
    } else
    if (*str == 'o' && str[1]) {
        for (str++; qe_isoctdigit(*str); str++)
            continue;
    } else
    if (*str == 'x' && str[1]) {
        for (str++; qe_isxdigit(*str); str++)
            continue;
    } else {
        if ((*str == '-' || *str == 'd') && str[1])
            str++;
        if (qe_isdigit(*str)) {
            for (; qe_isdigit(*str); str++)
                continue;
            if (*str == '.') {
                for (str++; qe_isdigit(*str); str++)
                    continue;
            }
            if (qe_tolower(*str) == 'e') {
                i = 1;
                if (str[i] == '+' || str[i] == '-')
                    i++;
                if (qe_isdigit(str[i])) {
                    for (str += i + 1; qe_isdigit(*str); str++)
                        continue;
                }
            }
        }
    }
    return (*str) ? 0 : 1;
}

static void lisp_colorize_line(QEColorizeContext *cp,
                               const char32_t *str, int n,
                               QETermStyle *sbuf, ModeDef *syn)
{
    int colstate = cp->colorize_state;
    int i = 0, start = i, len, level, style, style1, has_expr;
    int mode_flags = syn->colorize_flags;
    char kbuf[64];

    level = colstate & IN_LISP_LEVEL;
    style1 = style = 0;
    has_expr = 0;

    if (colstate & IN_LISP_SCOMMENT)
        style1 = LISP_STYLE_SCOMMENT;
    if (colstate & IN_LISP_STRING)
        goto parse_string;
    if (colstate & IN_LISP_COMMENT)
        goto parse_comment;

    while (i < n) {
        has_expr = 0;
        start = i;
        switch (str[i++]) {
        case ',':
            if (str[i] == '@')
                i++;
            FALLTHROUGH;
        case '`':
            style = LISP_STYLE_MACRO;
            break;
        case ';':
            if (mode_flags & LISP_LANG_JANET)
                goto regular;
            i = n;
            style = LISP_STYLE_COMMENT;
            break;
        case '(':
            if (colstate & IN_LISP_SCOMMENT)
                level++;
            break;
        case ')':
            if (colstate & IN_LISP_SCOMMENT) {
                if (level-- <= 1) {
                    SET_STYLE(sbuf, start, i - (level < 0), style1);
                    colstate &= ~IN_LISP_SCOMMENT;
                    level = 0;
                    style1 = 0;
                    continue;
                }
            }
            break;
        case '#':
            if (mode_flags & LISP_LANG_JANET) {
                i = n;
                style = LISP_STYLE_COMMENT;
                break;
            }
            if (str[i] == '|') {
                /* #| ... |# -> block comment */
                colstate |= IN_LISP_COMMENT;
                i++;
            parse_comment:
                for (; i < n; i++) {
                    if (str[i] == '|' && str[i + 1] == '#') {
                        i += 2;
                        colstate &= ~IN_LISP_COMMENT;
                        break;
                    }
                }
                style = LISP_STYLE_COMMENT;
                break;
            }
            if (str[i] == ';') {
                /* #; sexpr -> comment out sexpr */
                i++;
                colstate |= IN_LISP_SCOMMENT;
                style1 = LISP_STYLE_SCOMMENT;
                break;
            }
            if (str[i] == '"') {
                i++;
                colstate |= IN_LISP_STRING;
                goto parse_string;
            }
            if (str[i] == ':'
            &&  (str[i + 1] == '-' || qe_isalnum_(str[i + 1]))) {
                len = lisp_get_symbol(kbuf, sizeof(kbuf), str + i + 1);
                i += 1 + len;
                goto has_symbol;
            }
            if (qe_isalpha_(str[i])) {
                len = lisp_get_symbol(kbuf, sizeof(kbuf), str + i);
                i += len;
                if (strequal(kbuf, "t") || strequal(kbuf, "f")) {
                    /* #f -> false, #t -> true */
                    goto has_qsymbol;
                }
                if (mode_flags & LISP_LANG_RACKET) {
                    if (start == 0 && strequal(kbuf, "lang")) {
                        i = n;
                        style = LISP_STYLE_PREPROCESS;
                        break;
                    }
                    if (strequal(kbuf, "rx") || strequal(kbuf, "px")) {
                        if (str[i] == '"') {
                            /* #rx"regex" */
                            i += 1;
                            colstate |= IN_LISP_STRING;
                            goto parse_string;
                        }
                        if (str[i] == '#' && str[i + 1] == '"') {
                            /* #rx#"regex" */
                            i += 2;
                            colstate |= IN_LISP_STRING;
                            goto parse_string;
                        }
                    }
                }
                /* #b[01]+  -> binary constant */
                /* #o[0-7]+  -> octal constant */
                /* #d[0-9]+  -> decimal constant */
                /* #x[0-9a-fA-F]+  -> hex constant */
                goto has_symbol;
            }
            if (str[i] == '\\') {
                if (qe_isalnum_(str[i + 1])) {
                    /* #\x[0-9a-fA-F]+  -> hex char constant */
                    /* #\[a-zA-Z0-9]+  -> named char constant */
                    len = lisp_get_symbol(kbuf, sizeof(kbuf), str + i + 1);
                    i += 1 + len;
                    goto has_char_const;
                }
                if (i + 1 < n) {
                    i += 2;
                    goto has_char_const;
                }
            }
            {
                /* #( ... )  -> vector object */
                /* #! ... \n  -> line comment */
                /* # SPC  -> NIL ? */
            }
            break;
        case '"':
            /* parse string const */
            colstate |= IN_LISP_STRING;
        parse_string:
            while (i < n) {
                if (str[i] == '\\' && ++i < n) {
                    i++;
                } else
                if (str[i++] == '"') {
                    colstate &= ~IN_LISP_STRING;
                    has_expr = 1;
                    break;
                }
            }
            style = LISP_STYLE_STRING;
            break;
        case '?':
            /* parse char const */
            /* XXX: Should parse keys syntax */
            if (str[i] == '\\' && i + 1 < n) {
                i += 2;
            } else
            if (i < n) {
                i += 1;
            }
        has_char_const:
            has_expr = 1;
            style = LISP_STYLE_CHARCONST;
            break;
        case '\'':
            len = lisp_get_symbol(kbuf, sizeof(kbuf), str + i);
            if (len > 0) {
                i += len;
            has_qsymbol:
                has_expr = 1;
                style = LISP_STYLE_QSYMBOL;
                break;
            }
            break;
        default:
        regular:
            len = lisp_get_symbol(kbuf, sizeof(kbuf), str + i - 1);
            if (len > 0) {
                i += len - 1;
            has_symbol:
                has_expr = 1;
                if (lisp_is_number(kbuf)) {
                    style = LISP_STYLE_NUMBER;
                    break;
                }
                if (strfind(lisp_keywords, kbuf)
                ||  strfind(syn->keywords, kbuf)) {
                    style = LISP_STYLE_KEYWORD;
                    break;
                }
                if (strfind(syn->types, kbuf)) {
                    style = LISP_STYLE_TYPE;
                    break;
                }
                /* skip other symbol */
                break;
            }
            break;
        }
        if (style1) {
            style = style1;
            if (has_expr) {
                if ((colstate & IN_LISP_SCOMMENT) && level <= 0) {
                    colstate &= ~IN_LISP_SCOMMENT;
                    level = 0;
                    style1 = 0;
                }
            }
        }
        if (style) {
            SET_STYLE(sbuf, start, i, style);
            style = 0;
        }
    }
    colstate = (colstate & ~IN_LISP_LEVEL) | (level & IN_LISP_LEVEL);
    cp->colorize_state = colstate;
}

static int elisp_mode_probe(ModeDef *mode, ModeProbeData *p)
{
    /* check file name or extension */
    if (match_extension(p->filename, mode->extensions)
    ||  match_shell_handler(cs8(p->buf), mode->shell_handlers)
    ||  strstart(p->filename, ".emacs", NULL))
        return 80;

    return 1;
}

ModeDef lisp_mode = {
    .name = "Lisp",
    .extensions = "ll|li|lh|lo|lm|lisp|ls9",
    .keywords = NULL,
    .shell_handlers = "lisp",
    .types = lisp_types,
    .colorize_func = lisp_colorize_line,
    .colorize_flags = LISP_LANG_LISP,
};

static ModeDef elisp_mode = {
    .name = "ELisp",
    .extensions = "el",
    .keywords = elisp_keywords,
    .types = elisp_types,
    .mode_probe = elisp_mode_probe,
    .colorize_func = lisp_colorize_line,
    .colorize_flags = LISP_LANG_ELISP,
    .fallback = &lisp_mode,
};

static ModeDef scheme_mode = {
    .name = "Scheme",
    .extensions = "scm|sch|ss",
    .keywords = scheme_keywords,
    .types = lisp_types,
    .colorize_func = lisp_colorize_line,
    .colorize_flags = LISP_LANG_SCHEME,
    .fallback = &lisp_mode,
};

static ModeDef racket_mode = {
    .name = "Racket",
    .extensions = "rkt|rktd",
    .keywords = racket_keywords,
    .types = lisp_types,
    .colorize_func = lisp_colorize_line,
    .colorize_flags = LISP_LANG_RACKET,
    .fallback = &lisp_mode,
};

static ModeDef clojure_mode = {
    .name = "Clojure",
    .extensions = "clj|cljc",
    .keywords = clojure_keywords,
    .types = lisp_types,
    .colorize_func = lisp_colorize_line,
    .colorize_flags = LISP_LANG_CLOJURE,
    .fallback = &lisp_mode,
};

static ModeDef sandbox_mode = {
    .name = "Sandbox",  /* MacOS, Tinyscheme based? */
    .extensions = "sb",
    .keywords = sandbox_keywords,
    .types = lisp_types,
    .colorize_func = lisp_colorize_line,
    .colorize_flags = LISP_LANG_SANDBOX,
    .fallback = &lisp_mode,
};

static const char xaos_keywords[] = {
    ""
};

static ModeDef xaos_mode = {
    .name = "Xaos",  /* Xaos fractal generator */
    .extensions = "xhf|xaf|xpf",
    .keywords = xaos_keywords,
    .types = lisp_types,
    .colorize_func = lisp_colorize_line,
    .colorize_flags = LISP_LANG_XAOS,
    .fallback = &lisp_mode,
};

static const char janet_types[] = {
    "nil|t|false|true"
};

static const char janet_keywords[] = {
    /* from the documentation:
    % %= * *= *args* *current-file* *debug* *defdyn-prefix*
    *doc-color* *doc-width* *err* *err-color* *executable* *exit*
    *exit-value* *ffi-context* *flychecking* *lint-error*
    *lint-levels* *lint-warn* *macro-form* *macro-lints*
    *module-cache* *module-loaders* *module-loading* *module-make-env*
    *module-paths* *out* *peg-grammar* *pretty-format* *profilepath*
    *redef* *repl-prompt* *syspath* *task-id* + ++ += - -- -= -> ->>
    -?> -?>> / /= < <= = > >= abstract? accumulate accumulate2 all
    all-bindings all-dynamics and any? apply array array/clear
    array/concat array/ensure array/fill array/insert array/join
    array/new array/new-filled array/peek array/pop array/push
    array/remove array/slice array/trim array/weak array? as->
    as-macro as?-> asm assert assertf bad-compile bad-parse band
    blshift bnot boolean? bor brshift brushift buffer buffer/bit
    buffer/bit-clear buffer/bit-set buffer/bit-toggle buffer/blit
    buffer/clear buffer/fill buffer/format buffer/format-at
    buffer/from-bytes buffer/new buffer/new-filled buffer/popn
    buffer/push buffer/push-at buffer/push-byte buffer/push-float32
    buffer/push-float64 buffer/push-string buffer/push-uint16
    buffer/push-uint32 buffer/push-uint64 buffer/push-word
    buffer/slice buffer/trim buffer? bundle/add bundle/add-bin
    bundle/add-directory bundle/add-file bundle/add-manpage
    bundle/install bundle/installed? bundle/list bundle/manifest
    bundle/prune bundle/reinstall bundle/replace bundle/topolist
    bundle/uninstall bundle/update-all bundle/whois bxor bytes? cancel
    case catseq cfunction? chr cli-main cmp comment comp compare
    compare< compare<= compare= compare> compare>= compif compile
    complement comptime compwhen cond coro count curenv debug
    debug/arg-stack debug/break debug/fbreak debug/lineage debug/stack
    debug/stacktrace debug/step debug/unbreak debug/unfbreak debugger
    debugger-env debugger-on-status dec deep-not= deep= def- default
    default-peg-grammar defdyn defer defglobal defmacro defmacro- defn
    defn- delay describe dictionary? disasm distinct div doc doc*
    doc-format doc-of dofile drop drop-until drop-while dyn each eachk
    eachp edefer eflush empty? env-lookup eprin eprinf eprint eprintf
    error errorf ev/acquire-lock ev/acquire-rlock ev/acquire-wlock
    ev/all-tasks ev/call ev/cancel ev/capacity ev/chan ev/chan-close
    ev/chunk ev/close ev/count ev/deadline ev/do-thread ev/full
    ev/gather ev/give ev/give-supervisor ev/go ev/go-gather ev/lock
    ev/read ev/release-lock ev/release-rlock ev/release-wlock
    ev/rselect ev/rwlock ev/select ev/sleep ev/spawn ev/spawn-thread
    ev/take ev/thread ev/thread-chan ev/to-file ev/with-deadline
    ev/with-lock ev/with-rlock ev/with-wlock ev/write eval eval-string
    even? every? extreme false? ffi/align ffi/call
    ffi/calling-conventions ffi/close ffi/context ffi/defbind
    ffi/defbind-alias ffi/free ffi/jitfn ffi/lookup ffi/malloc
    ffi/native ffi/pointer-buffer ffi/pointer-cfunction ffi/read
    ffi/signature ffi/size ffi/struct ffi/trampoline ffi/write
    fiber-fn fiber/can-resume? fiber/current fiber/getenv
    fiber/last-value fiber/maxstack fiber/new fiber/root fiber/setenv
    fiber/setmaxstack fiber/status fiber? file/close file/flush
    file/lines file/open file/read file/seek file/tell file/temp
    file/write filewatch/add filewatch/listen filewatch/new
    filewatch/remove filewatch/unlisten filter find find-index first
    flatten flatten-into flush flycheck for forever forv freeze
    frequencies from-pairs function? gccollect gcinterval
    gcsetinterval generate gensym geomean get get-in getline getproto
    group-by has-key? has-value? hash idempotent? identity if-let
    if-not if-with import import* in inc index-of indexed? int/s64
    int/to-bytes int/to-number int/u64 int? interleave interpose
    invert janet/build janet/config-bits janet/version juxt juxt* keep
    keep-syntax keep-syntax! keys keyword keyword/slice keyword? kvs
    label last length lengthable? let load-image load-image-dict loop
    macex macex1 maclintf make-env make-image make-image-dict map
    mapcat marshal match math/-inf math/abs math/acos math/acosh
    math/asin math/asinh math/atan math/atan2 math/atanh math/cbrt
    math/ceil math/cos math/cosh math/e math/erf math/erfc math/exp
    math/exp2 math/expm1 math/floor math/frexp math/gamma math/gcd
    math/hypot math/inf math/int-max math/int-min math/int32-max
    math/int32-min math/lcm math/ldexp math/log math/log-gamma
    math/log10 math/log1p math/log2 math/nan math/next math/pi
    math/pow math/random math/rng math/rng-buffer math/rng-int
    math/rng-uniform math/round math/seedrandom math/sin math/sinh
    math/sqrt math/tan math/tanh math/trunc max max-of mean memcmp
    merge merge-into merge-module min min-of mod
    module/add-file-extension module/add-paths module/add-syspath
    module/cache module/expand-path module/find module/loaders
    module/loading module/paths module/value nan? nat? native neg?
    net/accept net/accept-loop net/address net/address-unpack
    net/chunk net/close net/connect net/flush net/listen net/localname
    net/peername net/read net/recv-from net/send-to net/server
    net/setsockopt net/shutdown net/socket net/write next nil? not
    not= number? odd? one? or os/arch os/cd os/chmod os/clock
    os/compiler os/cpu-count os/cryptorand os/cwd os/date os/dir
    os/environ os/execute os/exit os/getenv os/getpid os/isatty
    os/link os/lstat os/mkdir os/mktime os/open os/perm-int
    os/perm-string os/pipe os/posix-chroot os/posix-exec os/posix-fork
    os/proc-close os/proc-kill os/proc-wait os/readlink os/realpath
    os/rename os/rm os/rmdir os/setenv os/setlocale os/shell
    os/sigaction os/sleep os/spawn os/stat os/strftime os/symlink
    os/time os/touch os/umask os/which pairs parse parse-all
    parser/byte parser/clone parser/consume parser/eof parser/error
    parser/flush parser/has-more parser/insert parser/new
    parser/produce parser/state parser/status parser/where partial
    partition partition-by peg/compile peg/find peg/find-all peg/match
    peg/replace peg/replace-all pos? postwalk pp prewalk prin prinf
    print printf product prompt propagate protect put put-in quit
    range reduce reduce2 repeat repl require resume return reverse
    reverse! root-env run-context sandbox scan-number seq setdyn
    short-fn signal slice slurp some sort sort-by sorted sorted-by
    spit stderr stdin stdout string string/ascii-lower
    string/ascii-upper string/bytes string/check-set string/find
    string/find-all string/format string/from-bytes string/has-prefix?
    string/has-suffix? string/join string/repeat string/replace
    string/replace-all string/reverse string/slice string/split
    string/trim string/triml string/trimr string? struct
    struct/getproto struct/proto-flatten struct/rawget struct/to-table
    struct/with-proto struct? sum symbol symbol/slice symbol? table
    table/clear table/clone table/getproto table/new
    table/proto-flatten table/rawget table/setproto table/to-struct
    table/weak table/weak-keys table/weak-values table? tabseq take
    take-until take-while thaw thaw-keep-keys toggle trace tracev
    true? truthy? try tuple tuple/brackets tuple/join tuple/setmap
    tuple/slice tuple/sourcemap tuple/type tuple? type unless
    unmarshal untrace update update-in use values var- varfn varglobal
    walk warn-compile when when-let when-with with with-dyns with-env
    with-syms with-vars xprin xprinf xprint xprintf yield zero?
    zipcoll
     */

    "*=|"
    "++|"
    "+=|"
    "--|"
    "-=|"
    "/=|"
    ":close|"
    ":open|"
    ":read|"
    ":wait|"
    ":write|"
    "all|"
    "any?|"
    "apply|"
    "array?|"
    "array|"
    "as-macro|"
    "asm|"
    "assertf|"
    "assert|"
    "boolean?|"
    "break|"
    "buffer?|"
    "buffer|"
    "cfunction?|"
    "cmp|"
    "comment|"
    "compare<=|"
    "compare<|"
    "compare>=|"
    "compare>|"
    "compare|"
    "coro|"
    "count|"
    "debug|"
    "dec|"
    "deep-not=|"
    "deep=|"
    "def-|"
    "default|"
    "defdyn|"
    "defer|"
    "defglobal|"
    "defmacro-|"
    "defmacro|"
    "defn-|"
    "defn|"
    "def|"
    "disasm|"
    "do|"
    "each|"
    "eflush|"
    "empty?|"
    "eprinf|"
    "eprintf|"
    "eprint|"
    "errorf|"
    "error|"
    "even?|"
    "every?|"
    "false?|"
    "fiber?|"
    "flush|"
    "fn|"
    "forever|"
    "for|"
    "function?|"
    "gccollect|"
    "gensym|"
    "get|"
    "idempotent?|"
    "if-let|"
    "if-not|"
    "if|"
    "import|"
    "inc|"
    "keep|"
    "keyword?|"
    "loop|"
    "maclintf|"
    "map|"
    "nan?|"
    "nil?|"
    "not=|"
    "number?|"
    "odd?|"
    "pp|"
    "printf|"
    "print|"
    "prin|"
    "protect|"
    "put|"
    "quasiquote|"
    "quote|"
    "range|"
    "repeat|"
    "resume|"
    "seq|"
    "set|"
    "slice|"
    "some|"
    "splice|"
    "string?|"
    "string|"
    "struct?|"
    "struct|"
    "symbol?|"
    "table?|"
    "table|"
    "toggle|"
    "tracev|"
    "true?|"
    "truthy?|"
    "try|"
    "tuple?|"
    "tuple|"
    "unquote|"
    "upscope|"
    "var-|"
    "var|"
    "when-let|"
    "while|"
    "with|"
    "yield|"
    "zero?|"
};

static ModeDef janet_mode = {
    .name = "Janet",
    .extensions = "janet",
    .keywords = janet_keywords,
    .types = janet_types,
    .colorize_func = lisp_colorize_line,
    .colorize_flags = LISP_LANG_JANET,
    .fallback = &lisp_mode,
};

static int lisp_init(QEmacsState *qs)
{
    qe_register_mode(qs, &lisp_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &elisp_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &scheme_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &racket_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &clojure_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &sandbox_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &xaos_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &janet_mode, MODEF_SYNTAX);

    return 0;
}

qe_module_init(lisp_init);
