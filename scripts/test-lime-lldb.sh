#!/bin/sh
# Smoke test for scripts/lime_lldb.py.
#
# Mirrors scripts/test-lime-gdb.sh: builds debug, runs lldb non-
# interactively against bench_parse_fanout (which has `snap` in scope
# at the start of run_trial), and verifies the type summaries +
# lime-snapshot command produce expected output.
#
# Skipped when lldb is unavailable on the host.  macOS-specific
# detail: lldb requires codesigning to attach to debug binaries.
# In CI we provide a freshly-built binary in the test runner's own
# user namespace, so codesign is unnecessary.
#
# Run from repo root:  ./scripts/test-lime-lldb.sh
#
# Returns 0 on success, non-zero on any check failure.

set -e

if ! command -v lldb >/dev/null 2>&1; then
    echo "test-lime-lldb: lldb not available; skipping" >&2
    exit 0
fi

if [ ! -d build-debug ]; then
    echo "test-lime-lldb: build-debug not found; setting up..." >&2
    meson setup build-debug --buildtype=debug >/dev/null 2>&1
fi
ninja -C build-debug bench/bench_parse_fanout >/dev/null 2>&1

# Run lldb non-interactively.  -b runs in batch mode; we feed
# commands via -o (one per command).
out=$(lldb -b \
    -o "command script import scripts/lime_lldb.py" \
    -o "breakpoint set --file bench_parse_fanout.c --name run_trial" \
    -o "run 1" \
    -o "frame variable snap" \
    -o "lime-snapshot snap" \
    -o "continue" \
    -o "quit" \
    ./build-debug/bench/bench_parse_fanout 2>&1)

# Required output lines.  If any are missing, the script is broken.
fails=0
for needle in \
    'lime-lldb: summaries registered' \
    'magic=LIME' \
    'abi_version' \
    'refcount' \
    'nstate' \
    ; do
    if ! printf '%s\n' "$out" | grep -qF "$needle"; then
        echo "FAIL: missing '$needle' in lldb output"
        fails=$((fails + 1))
    fi
done

if [ "$fails" -gt 0 ]; then
    echo
    echo "----- lldb output -----"
    printf '%s\n' "$out"
    echo "-----------------------"
    exit 1
fi

echo "test-lime-lldb: OK"
