#!/bin/sh
# tests/compat/diff/run_c11.sh -- C11 differential.  One flex scanner (c11.l)
# feeds (a) bison's parser for c11.y and (b) Lime's parser for c11.y run
# through tests/compat/y2lime_min.py (actions dropped).  Compare
# accept/reject on every input file.
# usage: run_c11.sh LIME LIMPAR_C C11_Y C11_L INPUT...
set -u
LIME=$1; TPL=$2; CY=$3; CL=$4; shift 4
HERE=$(cd "$(dirname "$0")" && pwd)
abs() { case $1 in /*) printf '%s' "$1" ;; *) printf '%s/%s' "$PWD" "$1" ;; esac; }
LIME=$(abs "$LIME"); TPL=$(abs "$TPL"); CY=$(abs "$CY"); CL=$(abs "$CL")
set -- $(for f in "$@"; do abs "$f"; echo; done)   # inputs are read after cd
W=$(mktemp -d); trap 'find "$W" -mindepth 1 -delete; rmdir "$W"' EXIT
CC=${CC:-cc}
cp "$CY" "$W/c11.y"; cp "$CL" "$W/c11.l"; cp "$HERE"/c11_lime.c "$HERE"/c11_bison_main.c "$W/"
cd "$W" || exit 1
bison -Wnone -d -o y.tab.c c11.y && flex -o lex.yy.c c11.l || exit 1
# bison side: count tokens yylex hands to yyparse (wrapper renames the scanner)
printf 'int c11_real_yylex(void);\nextern int c11_ntok, c11_last;\nint yylex(void){ c11_last = c11_real_yylex(); if (c11_last > 0) c11_ntok++; return c11_last; }\n' > wrap.c
$CC -w -Wno-implicit-function-declaration -Dyylex=c11_real_yylex -c -o lex_b.o lex.yy.c || exit 1
$CC -w -Wno-implicit-function-declaration -o c11_bison y.tab.c lex_b.o wrap.c c11_bison_main.c || exit 1
python3 "$HERE/../y2lime_min.py" c11.y > c11g.lime 2> map.txt || exit 1
[ -n "${C11_MUTATE:-}" ] && sed -i "$C11_MUTATE" c11g.lime   # negative control hook
"$LIME" -q -T"$TPL" c11g.lime; echo "lime: c11 conflicts rc=$? (bison also reports 2 s/r)"
{ echo '#include "c11g.h"'; echo 'static int lime_map(int t) { switch (t) {'
  awk '$1=="CHARMAP"{printf "case %s: return TK_%s;\n",$2,$3} $1=="TOKMAP"{printf "case %s: return TK_%s;\n",$2,$3}' map.txt
  echo 'default: return -1; } }'; } > c11_map.h
$CC -w -o c11_lime c11g.c c11_lime.c lex.yy.c || exit 1
fails=0; n=0
for f in "$@"; do
    n=$((n + 1))
    [ -f "$f" ] || { echo "FAIL missing input $f"; fails=$((fails + 1)); continue; }
    b=$(./c11_bison < "$f" 2>/dev/null | tail -1); l=$(./c11_lime < "$f" 2>/dev/null | tail -1)
    if [ "$b" = "$l" ]; then echo "ok   $b $f"; else echo "FAIL bison=$b lime=$l $f"; fails=$((fails + 1)); fi
done
echo "$n files, $fails mismatch(es)"
exit "$fails"
