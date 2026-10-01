#!/bin/sh
# Regression test for a qescript parser hang on an unmatched closing token.
#
# A script such as `if (1) { } }` (one closing brace more than it should
# have) used to make qe_parse_script() loop forever: once the expression
# parser gave up on the stray '}' it called qe_cfg_skip_expr() to recover,
# but that function returned immediately without consuming the token when
# it found an unbalanced '}', ')', ']' or ':' right away. The statement
# loop in qe_parse_script() then kept calling qe_cfg_stmt() on that same
# token, re-reporting "invalid expression" through the status line (whose
# message starts with a literal bell character) without ever making
# progress.
#
# This feeds a few such inputs to the tiny console build and checks that
# each one produces only a small, bounded number of error bells rather
# than flooding the output, which is what happens when the parser spins.

set -u
cd "$(dirname "$0")/.."

BIN=./tqe
if [ ! -x "$BIN" ]; then
    make tqe >/dev/null 2>&1
fi

if [ ! -x "$BIN" ]; then
    echo "SKIP: could not build $BIN"
    exit 0
fi

fail=0

check() {
    script="$1"
    tmp=$(mktemp)
    "$BIN" -nw -q +eval "$script" </dev/null >"$tmp" 2>&1 &
    pid=$!
    sleep 2
    kill -9 "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
    bells=$(tr -cd '\007' <"$tmp" | wc -c | tr -d ' ')
    rm -f "$tmp"
    if [ "$bells" -gt 10 ]; then
        echo "FAIL: '$script' produced $bells error bells; parser looks stuck in a loop"
        fail=1
    else
        echo "ok: '$script' recovered ($bells error bell(s))"
    fi
}

check 'if (1) { } }'
check 'if ()) {}'
check '}'
check ':'

exit "$fail"
