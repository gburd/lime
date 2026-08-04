#!/usr/bin/env bash
#
# scripts/check_contrib_grammars.sh -- smoke-test every contrib/ SQL
# dialect grammar through the lime generator.
#
# The contrib/ dialects (oracle/mysql/sqlite/duckdb/xml/edn) are the
# Goal B reference artifacts but were not referenced by CI or the root
# meson build, so they could silently rot.  This is the guard: it runs
# each `contrib/**/*.lime` through `lime` and asserts that lime emits a
# non-empty generated `.c`.
#
# Scope is deliberately a *compile smoke test*, not a conformance run:
#   - lime exits non-zero on grammars that still have LALR conflicts or
#     unreducible rules, so we do NOT gate on lime's exit status; we
#     gate on "did a non-empty .c get produced".  Conflict counts are
#     reported for visibility but do not fail the build.
#   - We do not build/link the generated C or run the dialect tests.
#
# Exits non-zero if any grammar produces no output (a real regression:
# lime can no longer parse that grammar at all).
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

LIME_BIN="${LIME_BIN:-./lime}"
TEMPLATE="${LIME_TEMPLATE:-$PROJECT_ROOT/limpar.c}"

if [ ! -x "$LIME_BIN" ]; then
    echo "Building standalone lime binary ($LIME_BIN not found)..."
    "${CC:-cc}" -O2 -o "$PROJECT_ROOT/lime" "$PROJECT_ROOT/lime.c"
    LIME_BIN="$PROJECT_ROOT/lime"
fi

if [ ! -f "$TEMPLATE" ]; then
    echo "ERROR: lime template not found at $TEMPLATE" >&2
    exit 2
fi

workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT

echo "================================================================"
echo "  contrib grammar smoke test"
echo "  lime: $LIME_BIN   template: $TEMPLATE"
echo "================================================================"

fail=0
found=0
while IFS= read -r grammar; do
    found=1
    base="$(basename "${grammar%.lime}")"
    out="$workdir/$base"
    mkdir -p "$out"
    # lime may exit non-zero on conflicts; capture the log but judge on
    # whether it produced a non-empty generated .c.
    "$LIME_BIN" -T"$TEMPLATE" -d"$out" "$grammar" >"$out/log.txt" 2>&1 || true
    if [ -s "$out/$base.c" ]; then
        lines="$(wc -l <"$out/$base.c")"
        conflicts="$(grep -cE 'parsing conflict|can not be reduced|has no rules' "$out/log.txt" || true)"
        printf 'OK    %-52s %6s lines  (%s conflict/warn lines)\n' \
            "$grammar" "$lines" "$conflicts"
    else
        printf 'FAIL  %-52s no generated .c\n' "$grammar"
        sed 's/^/        /' "$out/log.txt" | head -5
        fail=1
    fi
done < <(find contrib -name '*.lime' | sort)

if [ "$found" -eq 0 ]; then
    echo "ERROR: no contrib/**/*.lime grammars found" >&2
    exit 2
fi

echo
if [ "$fail" -eq 0 ]; then
    echo "PASS: every contrib grammar compiles through lime."
    echo "NOTE: several dialects still emit LALR conflicts / unreducible"
    echo "      rules (grammar-quality debt, not a lime regression)."
    exit 0
fi

echo "FAIL: at least one contrib grammar no longer compiles through lime." >&2
exit 1
