#!/bin/sh
# Smoke test for scripts/lime-gdb.py.
#
# Builds a debug binary (if not already built), runs gdb with the
# pretty-printer script, breaks at a point where a ParserSnapshot
# is in scope, and verifies the pretty-printer + lime-snapshot
# command output looks right.
#
# Run from repo root:  ./scripts/test-lime-gdb.sh
#
# Returns 0 on success, non-zero on any check failure.  Uses the
# bench_parse_fanout benchmark binary (which has `snap` in scope
# from the start of run_trial) as the test fixture.

set -e

if ! command -v gdb >/dev/null 2>&1; then
    echo "test-lime-gdb: gdb not available; skipping" >&2
    exit 0
fi

if [ ! -d build-debug ]; then
    echo "test-lime-gdb: build-debug not found; setting up..." >&2
    meson setup build-debug --buildtype=debug >/dev/null 2>&1
fi
ninja -C build-debug bench/bench_parse_fanout >/dev/null 2>&1

# Run gdb non-interactively.
out=$(gdb -batch \
    -ex 'source scripts/lime-gdb.py' \
    -ex 'break bench_parse_fanout.c:run_trial' \
    -ex 'run 1' \
    -ex 'print *snap' \
    -ex 'lime-snapshot snap' \
    -ex 'continue' \
    ./build-debug/bench/bench_parse_fanout 2>&1)

# Required output lines.  If any are missing, the script is broken.
fails=0
for needle in \
    'lime-gdb: pretty-printers loaded' \
    'magic=LIME' \
    'abi_version:' \
    'refcount:' \
    'nstate:' \
    'yy_action:' \
    ; do
    if ! printf '%s\n' "$out" | grep -qF "$needle"; then
        echo "FAIL: missing '$needle' in gdb output"
        fails=$((fails + 1))
    fi
done

if [ "$fails" -gt 0 ]; then
    echo
    echo "----- gdb output -----"
    printf '%s\n' "$out"
    echo "----------------------"
    exit 1
fi

echo "test-lime-gdb: OK"
