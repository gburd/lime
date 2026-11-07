#!/bin/sh
# AC_PROG_LEX gate: autoconf's lex probe (the conftest.l that every autoconf
# configure script feeds $LEX), taken verbatim from programs.m4 of autoconf
# 2.69 and 2.73, must go through lime-lex the way it goes through flex:
# lex.yy.c appears, it links with the same LEXLIB choice ('' / -lfl / -ll),
# the YYTEXT_POINTER variant links, and the program runs.  The probe text
# is GPL, so it is read from the corpus (tests/compat/fetch_corpus.sh), not
# vendored.  SKIP (77) when the corpus or cc is absent.
#   usage: test_ac_prog_lex.sh LIME WORK_DIR   (corpus: $LIME_COMPAT_CORPUS)
set -u
LIME=$1; W=$2
D=${LIME_COMPAT_CORPUS:-/tmp/compat-dl}
CC=${CC:-cc}
command -v "$CC" >/dev/null 2>&1 || { echo "SKIP: no C compiler"; exit 77; }
mkdir -p "$W" || exit 1
W=$(mktemp -d "$W/run.XXXXXX") || exit 1
mkdir -p "$W/bin" || exit 1
ln -sf "$LIME" "$W/bin/lime-lex"
have_flex=0; command -v flex >/dev/null 2>&1 && have_flex=1
TO() { if command -v timeout >/dev/null 2>&1; then timeout "$@"; else shift; "$@"; fi; }

# The probe: from "cat >conftest.l <<_ACEOF[" to "]_ACEOF" in the macro.
extract() { awk '/_AC_PROG_LEX_YYTEXT_DECL\]/{f=1} f&&/^\]_ACEOF/{exit} f&&p{print} f&&/cat >conftest.l <<_ACEOF\[/{p=1}' "$1"; }

# probe LEX DIR: prints "lib=<LEXLIB> ptr=<yes|no> rc=<exit> out=<bytes>"
probe() {
  ( cd "$2" || exit 1
    TO 30 $1 conftest.l > lex.log 2>&1 || { echo "lex-failed: $(head -c 300 lex.log)"; exit 0; }
    [ -f lex.yy.c ] || { echo "no-lex.yy.c"; exit 0; }
    lib=none
    for l in '' -lfl -ll; do
      if TO 60 "$CC" -o conftest lex.yy.c $l > link.log 2>&1; then lib="'$l'"; break; fi
    done
    { echo '#define YYTEXT_POINTER 1'; cat lex.yy.c; } > ptr.c
    ptr=no
    l=$(echo "$lib" | tr -d "'"); [ "$l" = none ] && l=
    TO 60 "$CC" -o conftest_p ptr.c $l > ptr.log 2>&1 && ptr=yes
    rc=-; out=-
    if [ -x conftest ]; then
      printf 'abcdef\n' | TO 10 ./conftest > run.out 2>&1; rc=$?
      out=$(head -c 200 run.out | od -An -c | tr -s ' \n' ' ')
    fi
    echo "lib=$lib ptr=$ptr rc=$rc out=$out" )
}

fail=0; n=0
for v in 2.69 2.73; do
  m4=$D/autoconf-$v/lib/autoconf/programs.m4
  [ -f "$m4" ] || { echo "SKIP: $m4 absent (run tests/compat/fetch_corpus.sh)"; exit 77; }
  n=$((n + 1))
  for k in lime flex; do
    mkdir -p "$W/$v-$k"
    extract "$m4" > "$W/$v-$k/conftest.l"
  done
  grep -q 'b { REJECT; }' "$W/$v-lime/conftest.l" || { echo "FAIL $v: probe not found in $m4"; fail=$((fail + 1)); continue; }
  got=$(probe "$W/bin/lime-lex" "$W/$v-lime")
  echo "autoconf $v lime-lex: $got"
  case $got in
    lib=none*|lex-failed*|no-lex*|*ptr=no*) echo "FAIL $v: probe does not pass"; fail=$((fail + 1)); continue ;;
  esac
  case $got in *rc=-*|*rc=124*) echo "FAIL $v: probe program did not run"; fail=$((fail + 1)); continue ;; esac
  if [ $have_flex = 1 ]; then
    want=$(probe flex "$W/$v-flex")
    echo "autoconf $v flex:     $want"
    [ "$got" = "$want" ] || { echo "FAIL $v: differs from flex"; fail=$((fail + 1)); }
  fi
done
echo "$((n - fail))/$n autoconf AC_PROG_LEX probes pass"
[ $fail -eq 0 ]
