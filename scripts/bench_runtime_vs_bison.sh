#!/usr/bin/env bash
#
# Runtime parse throughput: Bison vs Lime, same token stream.
#
# Usage: nix-shell -p bison --run ./scripts/bench_runtime_vs_bison.sh
#

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BENCH="$ROOT/bench"
TMPDIR="$(mktemp -d -t lime-runbench.XXXXXX)"
trap 'rm -rf "$TMPDIR"' EXIT

LIME="$ROOT/lime"
[ -x "$LIME" ] || (cd "$ROOT" && cc -O2 -o lime lime.c)

# Iterations
ITER=${ITER:-5000}

echo "=== Runtime parse throughput ==="
echo "Input: ~50-token SQL stream; $ITER iterations"
echo
printf "%-10s  %10s  %10s  %10s  %10s  %10s\n" \
       "tool" "median(ns)" "mean(ns)" "p95(ns)" "p99(ns)" "tokens"

# Build Bison version
mkdir -p "$TMPDIR/bison"
# Strip the grammar's epilogue that defines yylex/yyerror stubs;
# the harness provides its own.
awk '/^%%$/{n++; if(n==2) exit} {print}' "$BENCH/bench_grammar.y" > "$TMPDIR/bison/g.y"
echo "%%" >> "$TMPDIR/bison/g.y"
bison -d -o "$TMPDIR/bison/bison_out.c" "$TMPDIR/bison/g.y" 2>/dev/null
cc -O2 -w -DNDEBUG -DUSE_BISON \
    -I"$TMPDIR/bison" \
    -o "$TMPDIR/bench_bison" \
    "$BENCH/bench_runtime.c" \
    "$TMPDIR/bison/bison_out.c"
"$TMPDIR/bench_bison" "$ITER"

# Build Lime version
mkdir -p "$TMPDIR/lime"
cp "$BENCH/bench_grammar.lime" "$TMPDIR/lime/lime_out.lime"
(cd "$TMPDIR/lime" && "$LIME" -T"$ROOT/limpar.c" -PBenchParser -q lime_out.lime) 2>/dev/null
# lime outputs lime_out.h with "#define SELECT 1" etc.
cc -O2 -w -DNDEBUG -DUSE_LIME \
    -I"$TMPDIR/lime" \
    -o "$TMPDIR/bench_lime" \
    "$BENCH/bench_runtime.c" \
    "$TMPDIR/lime/lime_out.c"
"$TMPDIR/bench_lime" "$ITER"

echo
echo "(Lower is better. Numbers are ns per parse of a 55-token SQL stream.)"
