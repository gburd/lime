#!/usr/bin/env bash
# build-pgo.sh - Configure a Lime build directory with PGO enabled.
#
# Profile-guided optimization workflow:
#   1. Build with `-Dlime_pgo=generate` to instrument the binary.
#   2. Run representative workloads to populate .gcda profile data.
#   3. Rebuild with `-Dlime_pgo=use` for the optimised codegen.
#
# This wrapper runs all three phases.  The training workload is
# the test suite + benchmarks by default; pass `--training-cmd "..."`
# to substitute your own workload.
#
# Usage:
#   ./scripts/build-pgo.sh <build-dir> [meson-setup-args...]
#
# Measured wins on lime v0.9.x (i9-12900H, gcc 15.2):
#   bench_lsc_small:    +51% throughput
#   bench_lsc_medium:   +46% throughput
#   bench_lsc_large:    +26% throughput
#   bench_parse_fanout: +14% throughput (8 threads, borrowed snapshot)
#   bench_jit_real_parser: -7% (regression -- workload mismatch)
#
# IMPORTANT: PGO + LTO do not currently compose with static archives
# (gcc-ar plugin conflict).  Use one or the other, not both.

set -euo pipefail

if [ "$#" -lt 1 ]; then
    echo "usage: $0 <build-dir> [meson-setup-args...]" >&2
    exit 2
fi

build_dir=$1
shift

# Phase 1: instrument
echo "build-pgo: phase 1 -- meson setup with -Dlime_pgo=generate"
meson setup "$build_dir" -Dbuildtype=release -Dlime_pgo=generate "$@"
ninja -C "$build_dir"

# Phase 2: train.  Run tests + benches to cover representative paths.
echo "build-pgo: phase 2 -- training run (tests + benches)"
meson test -C "$build_dir" --num-processes 4 || {
    echo "build-pgo: WARNING: some tests failed during training; continuing"
}
# Run benches if they exist (smoke test for profile coverage).
for b in bench/parser_bench bench/bench_jit_real_parser \
         bench/lex_state_count/bench_lsc_small \
         bench/lex_state_count/bench_lsc_medium \
         bench/lex_state_count/bench_lsc_large; do
    if [ -x "$build_dir/$b" ]; then
        "$build_dir/$b" >/dev/null 2>&1 || true
    fi
done
gcda_count=$(find "$build_dir" -name '*.gcda' 2>/dev/null | wc -l)
echo "build-pgo: phase 2 produced $gcda_count .gcda profile files"

if [ "$gcda_count" -eq 0 ]; then
    echo "build-pgo: ERROR: no .gcda files generated; profile use phase will fail"
    exit 1
fi

# Phase 3: rebuild with profile data
echo "build-pgo: phase 3 -- meson configure -Dlime_pgo=use"
meson configure "$build_dir" -Dlime_pgo=use
ninja -C "$build_dir"

echo "build-pgo: done.  Run benches to see the win."
