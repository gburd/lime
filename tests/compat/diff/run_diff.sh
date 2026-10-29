#!/bin/sh
# tests/compat/diff/run_diff.sh -- differential: bison-built vs Lime-built
# calc / rpcalc / mfcalc (bison-3.8.2/examples/c).  Both binaries use the
# example's own yylex() (its epilogue, compiled unchanged) so they see the
# same token stream; we compare stdout and the accept/reject exit status.
# stderr wording is not compared (bison's "unexpected X, expecting Y" vs
# Lime's %syntax_error text), only the NUMBER of syntax errors reported.
#
# usage: run_diff.sh LIME LIMPAR_C BISON_EXAMPLES_DIR
set -u
LIME=$1; TPL=$2; EX=$3
HERE=$(cd "$(dirname "$0")" && pwd)
abs() { case $1 in /*) printf '%s' "$1" ;; *) printf '%s/%s' "$PWD" "$1" ;; esac; }
LIME=$(abs "$LIME"); TPL=$(abs "$TPL"); EX=$(abs "$EX")
W=$(mktemp -d); trap 'find "$W" -mindepth 1 -delete; rmdir "$W"' EXIT
CC=${CC:-cc}
fails=0; cases=0

build() { # name dir file.y -- bison and lime each build in their own dir
    n=$1; d=$EX/$2; y=$d/$3; B=$W/b-$n; L=$W/l-$n
    mkdir -p "$B" "$L"
    cp "$y" "$B/$n.y"
    if [ -f "$d/calc.h" ]; then cp "$d/calc.h" "$B/"; cp "$d/calc.h" "$L/"; fi
    # calc.y sets api.header.include {"calc.h"}, so its header must be calc.h
    hdr=${n}_b.h; [ "$n" = calc ] && hdr=calc.h
    (cd "$B" && bison --header="$hdr" -o "${n}_b.c" "$n.y") || return 1
    $CC -w -I"$B" -o "$W/${n}_bison" "$B/${n}_b.c" -lm || return 1
    # epilogue = text after the second %% line, compiled unchanged on the lime side
    awk 'f==2{print} /^%%/{f++}' "$y" > "$L/${n}_epi.c"
    cp "$HERE/$n.lime" "$HERE/${n}_lime.c" "$HERE/lime_yyparse.h" "$L/"
    (cd "$L" && "$LIME" -q -T"$TPL" "$n.lime") || { echo "lime failed on $n.lime"; return 1; }
    $CC -w -I"$L" -o "$W/${n}_lime" "$L/$n.c" "$L/${n}_lime.c" -lm || return 1
}

check() { # name input
    cases=$((cases + 1))
    printf '%b' "$2" > "$W/in"
    "$W/$1_bison" < "$W/in" > "$W/b.out" 2> "$W/b.err"; brc=$?
    "$W/$1_lime"  < "$W/in" > "$W/l.out" 2> "$W/l.err"; lrc=$?
    be=$(grep -c 'syntax error' "$W/b.err"); le=$(grep -c 'syntax error' "$W/l.err")
    if cmp -s "$W/b.out" "$W/l.out" && [ "$brc" = "$lrc" ] && [ "$be" = "$le" ]; then
        echo "ok   $1 $(printf '%s' "$2" | head -c 40)"
    else
        echo "FAIL $1 input=$(printf '%s' "$2" | head -c 60)"
        echo "     bison rc=$brc errs=$be out=$(tr '\n' '|' < "$W/b.out")"
        echo "     lime  rc=$lrc errs=$le out=$(tr '\n' '|' < "$W/l.out")"
        fails=$((fails + 1))
    fi
}

for n in rpcalc calc mfcalc; do
    case $n in rpcalc) d=rpcalc f=rpcalc.y ;; calc) d=calc f=calc.y ;; mfcalc) d=mfcalc f=mfcalc.y ;; esac
    if ! build $n $d $f; then echo "FAIL $n: build"; fails=$((fails + 1)); continue; fi
done

# rpcalc: the six cases from rpcalc.test, plus multi-line and errors
for i in '1 2 3 * +\n' '1.1 2.2 3.3 * +\n' '1 2 + 3 *\n' '1 2 3 4 5 6 7 8 9 * * * * * * * *\n' \
         '3 7 + 3 4 5 * + - n\n' '3 4 ^\n' '\n\n1 1 +\n\n2 n\n' '' '1 +\n' '1 2\n' '+\n' '1 2 + 3\n4 5 *\n'; do
    check rpcalc "$i"
done
# calc: calc.test cases, precedence/associativity, error recovery via `error '\n'`
for i in '1+2*3\n' '1 - 2 - 3\n' '8 / 2 / 2\n' '(1+2) * 3\n' '1++2\n' '1+\n2*3\n' \
         '((((5))))\n' '2*(3+4)*5\n' '1/0\n' '\n\n7\n' ')\n1\n' '1 2\n3\n' '(1\n(2)\n' ''; do
    check calc "$i"
done
# mfcalc: mfcalc.test cases + functions, variables, unary minus, ^ right-assoc
for i in '1+2*3\n' '(1+2) * 3\n' '-2^2\n' '2^3^2\n' 'x = 3\nx * x\n' 'sin(0)\n' 'a = b = 4\na + b\n' \
         'sqrt(16) + ln(1)\n' '1 - - 1\n' '4 / 2 / 2\n' 'exp(0) = 2\nexp(0)\n' '1 +\n2\n' '(\n3\n' '= 1\n5\n'; do
    check mfcalc "$i"
done

echo "$cases cases, $fails failure(s)"
exit "$fails"
