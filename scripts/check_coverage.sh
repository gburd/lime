#!/usr/bin/env bash
#
# scripts/check_coverage.sh -- enforce a minimum coverage floor on the
# src/ runtime library.  Intended for CI; also runnable locally.
#
# Behaviour:
#   1. Builds with --coverage instrumentation in builddir-coverage.
#   2. Runs the test suite.
#   3. Computes src/ line and branch coverage via gcovr.
#   4. Compares against floors below.
#   5. Exits 0 if both >= floors, 1 otherwise.
#
# The floors are intentionally conservative versus the current
# measured numbers (~80% lines, ~64% branches as of v0.2.7) so a
# routine PR doesn't accidentally trip them, but a structural
# regression does.  Raise them as coverage improves; the goal is
# to ratchet upward, not to hold steady.
#
# Override floors locally for ad-hoc checks:
#   LIME_COV_MIN_LINES=85 LIME_COV_MIN_BRANCHES=70 \
#       scripts/check_coverage.sh
#

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

# Coverage floors for src/ (the runtime library that production users
# link against).  Tests/, lime.c (parser generator), and bench/ are
# excluded from the floor by --filter.
MIN_LINES="${LIME_COV_MIN_LINES:-72}"
MIN_BRANCHES="${LIME_COV_MIN_BRANCHES:-55}"

BUILDDIR="${BUILDDIR:-builddir-coverage}"

echo "================================================================"
echo "  Coverage gate"
echo "  floor: lines >= ${MIN_LINES}%   branches >= ${MIN_BRANCHES}%"
echo "  builddir: ${BUILDDIR}"
echo "================================================================"
echo

# 1. Configure + build under --coverage instrumentation.  Skip if the
#    builddir is already configured -- saves time on iterative local
#    runs; CI starts fresh.
if [ ! -f "${BUILDDIR}/build.ninja" ]; then
    echo "[1/4] Configuring ${BUILDDIR} with -Db_coverage=true..."
    meson setup "${BUILDDIR}" -Db_coverage=true
fi

echo "[2/4] Building..."
ninja -C "${BUILDDIR}"

echo "[3/4] Running tests..."
meson test -C "${BUILDDIR}" --no-rebuild

# 2. gcovr supports two gcov backends.  Prefer llvm-cov gcov on macOS
#    (where /usr/bin/gcov is the Xcode shim that doesn't read clang-
#    emitted notes) and plain gcov on Linux.
if [ "$(uname -s)" = "Darwin" ]; then
    GCOV_EXE="llvm-cov gcov"
else
    GCOV_EXE="gcov"
fi

# 3. Capture summary.  --filter restricts to src/ runtime; lime.c and
#    tests/ aren't held to this bar.
echo "[4/4] Computing coverage with gcovr (gcov: ${GCOV_EXE})..."
SUMMARY="$(gcovr --print-summary \
    --gcov-executable "${GCOV_EXE}" \
    -r . \
    --filter "src/" \
    "${BUILDDIR}" 2>&1 | tail -5)"

echo
echo "$SUMMARY"
echo

# 4. Parse the percentage out of "lines: NN.N% ..." / "branches: ...".
LINES_PCT="$(echo "$SUMMARY" | awk -F'[ %]' '/^lines:/ {print $2}')"
BRANCHES_PCT="$(echo "$SUMMARY" | awk -F'[ %]' '/^branches:/ {print $2}')"

if [ -z "$LINES_PCT" ] || [ -z "$BRANCHES_PCT" ]; then
    echo "ERROR: could not parse coverage percentages from gcovr output" >&2
    exit 1
fi

# 5. Compare.  awk handles the float comparison portably (no bc dep).
fail=0

cmp_lines="$(awk "BEGIN { print (${LINES_PCT} >= ${MIN_LINES}) ? 1 : 0 }")"
cmp_branches="$(awk "BEGIN { print (${BRANCHES_PCT} >= ${MIN_BRANCHES}) ? 1 : 0 }")"

if [ "$cmp_lines" != "1" ]; then
    echo "FAIL: lines coverage ${LINES_PCT}% < floor ${MIN_LINES}%" >&2
    fail=1
fi
if [ "$cmp_branches" != "1" ]; then
    echo "FAIL: branches coverage ${BRANCHES_PCT}% < floor ${MIN_BRANCHES}%" >&2
    fail=1
fi

if [ "$fail" -eq 0 ]; then
    echo "PASS: lines ${LINES_PCT}% >= ${MIN_LINES}%, branches ${BRANCHES_PCT}% >= ${MIN_BRANCHES}%"
    exit 0
fi

echo
echo "Coverage regression detected.  Either:" >&2
echo "  - Add tests for the new code, or" >&2
echo "  - Justify the regression and lower the floor in CI." >&2
exit 1
