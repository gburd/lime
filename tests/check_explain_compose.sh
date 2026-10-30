#!/bin/sh
# check_explain_compose.sh LIME FIXTURES PGDIR
# Golden-text test for `lime --explain-compose` (C2): output and exit code.
#   exit 0 = nothing unresolved, 1 = unresolved conflicts, 2 = I/O/grammar error.
lime=$1; fx=$2; pg=$3
tmp=${TMPDIR:-/tmp}/lime_explain_$$
mkdir -p "$tmp" || exit 2
bad=0
run() {  # name expected_rc golden base ext...
  name=$1; want=$2; golden=$3; shift 3
  ( cd "$(dirname "$1")" && "$lime" --explain-compose "$@" ) > "$tmp/$name.out" 2> "$tmp/$name.err"
  rc=$?
  if [ "$rc" != "$want" ]; then echo "FAIL $name: exit $rc, want $want"; cat "$tmp/$name.err"; bad=1; return; fi
  if [ -n "$golden" ] && ! cmp -s "$tmp/$name.out" "$golden"; then
    echo "FAIL $name: output differs from $(basename "$golden")"; diff "$golden" "$tmp/$name.out" | head -20; bad=1; return
  fi
  echo "ok   $name (exit $rc)"
}
run tiny_conflict 1 "$fx/explain_tiny_conflict.golden" "$fx/tiny_base.lime" "$fx/tiny_conflict_ext.lime"
run tiny_clean    0 "$fx/explain_tiny_clean.golden"    "$fx/tiny_base.lime" "$fx/tiny_clean_ext.lime"
run missing_file  2 ""                                  "$fx/tiny_base.lime" "$fx/no_such_ext.lime"
if [ -f "$pg/pg_grammar.lime" ]; then
  run pg_grammar  1 "$fx/explain_pg_grammar.golden" "$pg/pg_grammar.lime" "$fx/dialect_ext.lime"
  run gram        1 "$fx/explain_gram.golden"       "$pg/gram.lime"       "$fx/dialect_ext.lime"
fi
rm -r "$tmp"
exit $bad
