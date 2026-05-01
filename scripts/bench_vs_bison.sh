#!/usr/bin/env bash
#
# Lime vs Bison: parser generation time + binary size comparison
#
# Runs each generator N times on the identical grammar, reports wall-clock
# median generation time and size of the generated C output.
#
# Usage:  nix-shell -p bison --run ./scripts/bench_vs_bison.sh
#

set -eu

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BENCH="$ROOT/bench"
TMPDIR="$(mktemp -d -t lime-bench.XXXXXX)"
trap 'rm -rf "$TMPDIR"' EXIT

LIME="$ROOT/lime"
if [ ! -x "$LIME" ]; then
    echo "Building lime..." >&2
    (cd "$ROOT" && cc -O2 -o lime lime.c)
fi

ITER=20

# ----- Generation time -----
echo "=== Parser generation time (median of $ITER runs) ==="
printf "%-10s %12s %12s %12s %14s\n" "tool" "median (ms)" "min (ms)" "max (ms)" "output bytes"

# Bison
BISON_TIMES=()
for _ in $(seq $ITER); do
    rm -rf "$TMPDIR/bison"
    mkdir -p "$TMPDIR/bison"
    start=$(python3 -c 'import time; print(int(time.monotonic_ns()))')
    bison -o "$TMPDIR/bison/out.c" "$BENCH/bench_grammar.y" 2>/dev/null || true
    end=$(python3 -c 'import time; print(int(time.monotonic_ns()))')
    BISON_TIMES+=($(( (end - start) / 1000000 )))
done
BISON_SIZE=$(wc -c < "$TMPDIR/bison/out.c" 2>/dev/null || echo 0)
BISON_SORTED=($(printf '%s\n' "${BISON_TIMES[@]}" | sort -n))
BISON_MEDIAN=${BISON_SORTED[$((ITER / 2))]}
BISON_MIN=${BISON_SORTED[0]}
BISON_MAX=${BISON_SORTED[$((ITER - 1))]}
printf "%-10s %12d %12d %12d %14d\n" "bison" "$BISON_MEDIAN" "$BISON_MIN" "$BISON_MAX" "$BISON_SIZE"

# Lime
LIME_TIMES=()
for _ in $(seq $ITER); do
    rm -rf "$TMPDIR/lime"
    mkdir -p "$TMPDIR/lime"
    # Lime reads the filename to derive output; copy .lime file into a clean dir
    cp "$BENCH/bench_grammar.lime" "$TMPDIR/lime/g.lime"
    start=$(python3 -c 'import time; print(int(time.monotonic_ns()))')
    # -PBenchParser matches the %name in the grammar so driver linking is easy
    (cd "$TMPDIR/lime" && "$LIME" -T"$ROOT/limpar.c" -PBenchParser -q g.lime) 2>/dev/null || true
    end=$(python3 -c 'import time; print(int(time.monotonic_ns()))')
    LIME_TIMES+=($(( (end - start) / 1000000 )))
done
# Lime outputs g.c + g.h next to the input file
LIME_SIZE=0
if [ -f "$TMPDIR/lime/g.c" ]; then
    LIME_SIZE=$(wc -c < "$TMPDIR/lime/g.c")
fi
LIME_SORTED=($(printf '%s\n' "${LIME_TIMES[@]}" | sort -n))
LIME_MEDIAN=${LIME_SORTED[$((ITER / 2))]}
LIME_MIN=${LIME_SORTED[0]}
LIME_MAX=${LIME_SORTED[$((ITER - 1))]}
printf "%-10s %12d %12d %12d %14d\n" "lime" "$LIME_MEDIAN" "$LIME_MIN" "$LIME_MAX" "$LIME_SIZE"

echo
echo "=== Compiled parser binary size ==="
printf "%-10s %14s\n" "tool" "compiled (bytes)"

# Compile each generated parser and measure
# Bison: the grammar file embeds yylex and yyerror in the %% epilogue,
# so the driver only needs main().
cat > "$TMPDIR/bison_main.c" <<'EOF'
int yyparse(void);
int main(void) { return yyparse(); }
EOF
if [ -f "$TMPDIR/bison/out.c" ]; then
    cc -O2 -w -o "$TMPDIR/bison_bin" "$TMPDIR/bison/out.c" "$TMPDIR/bison_main.c" 2>/dev/null || true
    if [ -f "$TMPDIR/bison_bin" ]; then
        BISON_BIN=$(wc -c < "$TMPDIR/bison_bin")
        # Strip to get pure code size
        strip "$TMPDIR/bison_bin" 2>/dev/null || true
        BISON_BIN_STRIPPED=$(wc -c < "$TMPDIR/bison_bin")
        printf "%-10s %14d %s\n" "bison" "$BISON_BIN_STRIPPED" "(stripped)"
    fi
fi

# Lime: similar minimal driver
cat > "$TMPDIR/lime_main.c" <<'EOF'
#include <stdlib.h>
void *BenchParserAlloc(void *(*f)(unsigned long));
void  BenchParserFree(void *p, void (*f)(void*));
void  BenchParser(void *p, int major, int minor);
int main(void) {
    void *p = BenchParserAlloc((void *(*)(unsigned long))malloc);
    BenchParser(p, 0, 0);
    BenchParserFree(p, free);
    return 0;
}
EOF
if [ -f "$TMPDIR/lime/g.c" ]; then
    cc -O2 -w -o "$TMPDIR/lime_bin" "$TMPDIR/lime/g.c" "$TMPDIR/lime_main.c" 2>/dev/null || true
    if [ -f "$TMPDIR/lime_bin" ]; then
        strip "$TMPDIR/lime_bin" 2>/dev/null || true
        LIME_BIN=$(wc -c < "$TMPDIR/lime_bin")
        printf "%-10s %14d %s\n" "lime" "$LIME_BIN" "(stripped)"
    fi
fi

echo
echo "=== Summary ==="
if [ "$LIME_MEDIAN" -gt 0 ] && [ "$BISON_MEDIAN" -gt 0 ]; then
    RATIO=$(python3 -c "print(f'{$BISON_MEDIAN / $LIME_MEDIAN:.2f}')")
    echo "Lime is ${RATIO}x faster at parser generation"
fi
if [ "$LIME_SIZE" -gt 0 ] && [ "$BISON_SIZE" -gt 0 ]; then
    if [ "$LIME_SIZE" -gt "$BISON_SIZE" ]; then
        RATIO=$(python3 -c "print(f'{$LIME_SIZE / $BISON_SIZE:.2f}')")
        echo "Lime's generated source is ${RATIO}x the size of Bison's ($LIME_SIZE vs $BISON_SIZE bytes)"
    else
        RATIO=$(python3 -c "print(f'{$BISON_SIZE / $LIME_SIZE:.2f}')")
        echo "Bison's generated source is ${RATIO}x the size of Lime's ($BISON_SIZE vs $LIME_SIZE bytes)"
    fi
fi
