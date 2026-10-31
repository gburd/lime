#!/bin/sh
# Token-stream differential on real scanners, flex vs lime-lex.
#   c11.l   over every .c/.h of the flex, bison, bc and PG parser sources
#           (token, yylineno, yytext);
#   PG REL_18 src/backend/parser/scan.l over src/test/regress/sql/*.sql
#           (token, yylloc, line, semantic value, and each ereport), built
#           against a configured PG tree (PGTREE: ./configure, then
#           make -C src/backend generated-headers, make -C src/common,
#           make -C src/port).
# usage: run.sh LIME CORPUS_DIR WORK_DIR [PGTREE]
# Exit 0 = every stream identical; 77 = flex/bison/corpus missing.
set -u
LIME=$1; D=$2; W=$3; PGT=${4:-}
H=$(cd "$(dirname "$0")" && pwd)
for t in flex bison cc; do command -v $t >/dev/null 2>&1 || { echo "SKIP: no $t"; exit 77; }; done
[ -f "$D/c11.l" ] || { echo "SKIP: corpus absent"; exit 77; }
mkdir -p "$W" && cd "$W" || exit 1
fail=0
cp "$D/c11.l" "$D/c11.y" . && bison -d -o y.tab.c c11.y 2>/dev/null
flex --yylineno -o c11.flex.c c11.l && "$LIME" --lex --yylineno -o c11.lime.c c11.l || exit 1
cc -w -O1 -o c11.flex "$H/c11_tokens.c" c11.flex.c && cc -w -O1 -o c11.lime "$H/c11_tokens.c" c11.lime.c || exit 1
F=$(find "$D/flex-2.6.4/src" "$D/bison-3.8.2/src" "$D/bc-1.07.1" "$D/postgres-REL_18_0/src/backend/parser" \( -name '*.c' -o -name '*.h' \) | sort)
for k in flex lime; do timeout 300 ./c11.$k $F 2>&1 | head -c 500000000 > c11.$k.tok; done
if cmp -s c11.flex.tok c11.lime.tok; then echo "ok   c11.l: $(wc -l < c11.lime.tok) tokens identical"
else echo "FAIL c11.l"; diff c11.flex.tok c11.lime.tok | head -10; fail=1; fi
if [ -n "$PGT" ] && [ -f "$PGT/src/backend/parser/gram.h" ]; then
  I="-I$PGT/src/include -I$PGT/src/backend/parser"
  L="$PGT/src/common/libpgcommon_srv.a $PGT/src/port/libpgport_srv.a"
  cp "$PGT/src/backend/parser/scan.l" pgscan.l
  flex -o pgscan.flex.c pgscan.l && "$LIME" --lex -o pgscan.lime.c pgscan.l || exit 1
  for k in flex lime; do cc -O1 -w $I -o pg.$k "$H/pg_scan_tokens.c" pgscan.$k.c $L || exit 1; done
  S=$(ls "$D"/postgres-REL_18_0/src/test/regress/sql/*.sql)
  for k in flex lime; do timeout 300 ./pg.$k $S 2>&1 | head -c 500000000 > pg.$k.tok; done
  if cmp -s pg.flex.tok pg.lime.tok; then echo "ok   PG scan.l: $(grep -c '^[0-9]' pg.lime.tok) tokens, $(grep -c '^ERROR' pg.lime.tok) errors identical"
  else echo "FAIL PG scan.l"; diff pg.flex.tok pg.lime.tok | head -10; fail=1; fi
else
  echo "skip PG scan.l (no configured PGTREE given)"
fi
exit $fail
