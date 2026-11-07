#!/bin/sh
# Differential test: each FIXTURE_DIR/NAME.l is built with stock flex and with
# lime --lex; both scanners read NAME.in (or nothing), and their combined
# stdout+stderr (capped at 100 kB) and exit status must be identical.  A
# scanner still running after 10 s is a hang and counts as a difference.
# A line "flexdiff-flags: ..." in the fixture passes flags to both tools.
# SKIP (77) when stock flex or a C compiler is missing.
#   usage: test_flex_diff.sh LIME FIXTURE_DIR WORK_DIR [NAME...]
set -u
LIME=$1; DIR=$2; W=$3; shift 3
CC=${CC:-cc}
command -v flex >/dev/null 2>&1 || { echo "SKIP: stock flex not on PATH"; exit 77; }
command -v "$CC" >/dev/null 2>&1 || { echo "SKIP: no C compiler ($CC)"; exit 77; }
mkdir -p "$W" || exit 1
[ $# -eq 0 ] && set -- $(cd "$DIR" && ls *.l | sed 's/\.l$//')
# Bound every run (a looping scanner must FAIL, not hang).  macOS has no
# `timeout`; meson's per-test timeout still bounds the whole script there.
TO30=""; TO120=""; TO10=""
if command -v timeout >/dev/null 2>&1; then TO30="timeout 30"; TO120="timeout 120"; TO10="timeout 10"; fi
fail=0; n=0
for t in "$@"; do
  n=$((n + 1))
  in="$DIR/$t.in"; [ -f "$in" ] || in=/dev/null
  flags=$(sed -n 's/^.*flexdiff-flags: *\([^*]*\).*$/\1/p' "$DIR/$t.l" | head -1)
  rm -f "$W/$t".* "$W/$t.flex.d/$t.h" "$W/$t.lime.d/$t.h"
  if ! (cd "$W" && $TO30 flex $flags -o "$t.flex.c" "$DIR/$t.l") >"$W/$t.err" 2>&1; then
    echo "FAIL $t: stock flex rejected the fixture"; head -c 2000 "$W/$t.err"; fail=$((fail + 1)); continue
  fi
  [ -f "$W/$t.h" ] && mkdir -p "$W/$t.flex.d" && mv "$W/$t.h" "$W/$t.flex.d/$t.h"
  if ! (cd "$W" && $TO30 "$LIME" --lex $flags -o "$t.lime.c" "$DIR/$t.l") >"$W/$t.err" 2>&1; then
    echo "FAIL $t: lime --lex rejected it"; head -c 2000 "$W/$t.err"; fail=$((fail + 1)); continue
  fi
  [ -f "$W/$t.h" ] && mkdir -p "$W/$t.lime.d" && mv "$W/$t.h" "$W/$t.lime.d/$t.h"
  if [ -f "$DIR/$t.main.c" ] && [ ! -f "$W/$t.lime.d/$t.h" ]; then
    echo "FAIL $t: lime wrote no header $t.h"; fail=$((fail + 1)); continue
  fi
  for k in flex lime; do
    # NAME.main.c, if present, is a separate driver that includes the
    # scanner's header (%option header-file="NAME.h"), built per tool.
    main=
    if [ -f "$DIR/$t.main.c" ]; then
      main="$DIR/$t.main.c -I$W/$t.$k.d"
    fi
    if ! $TO120 "$CC" -w -o "$W/$t.$k" "$W/$t.$k.c" $main >"$W/$t.err" 2>&1; then
      echo "FAIL $t: $k output does not compile"; head -c 2000 "$W/$t.err"; fail=$((fail + 1)); continue 2
    fi
  done
  for k in flex lime; do
    # Cap inside the pipe: a runaway scanner must never fill the disk.
    { $TO10 "$W/$t.$k" < "$in" 2>&1; echo "rc=$?" > "$W/$t.$k.rc"; } | head -c 100000 > "$W/$t.$k.out"
    cat "$W/$t.$k.rc" >> "$W/$t.$k.out"
    grep -q '^rc=124$' "$W/$t.$k.rc" && echo "HANG $t ($k)"
  done
  if cmp -s "$W/$t.flex.out" "$W/$t.lime.out" && ! grep -q '^rc=124$' "$W/$t.lime.rc"; then
    echo "ok   $t"
  else
    echo "FAIL $t: output differs from flex"
    diff "$W/$t.flex.out" "$W/$t.lime.out" | head -c 2000
    fail=$((fail + 1))
  fi
done
# lime-lex by bare name through PATH, as `./configure LEX=lime-lex` runs it,
# with options a CLI rewrite would expand: PATH must survive (the scanner
# action's getenv sees it) and lex.yy.c must appear.
n=$((n + 1))
mkdir -p "$W/pathbin" "$W/pathrun" && ln -sf "$LIME" "$W/pathbin/lime-lex"
printf '%%%%\n.|\\n { }\n%%%%\nint yywrap(void){return 1;}\nint main(void){return 0;}\n' > "$W/pathrun/p.l"
rm -f "$W/pathrun/lex.yy.c"
if (cd "$W/pathrun" && PATH="$W/pathbin:$PATH" lime-lex -B -L --noline -Cf p.l && PATH="$W/pathbin:$PATH" lime-lex -t p.l | grep -q 'yylex') >"$W/path.err" 2>&1 \
   && test -s "$W/pathrun/lex.yy.c"; then
  echo "ok   bare lime-lex via PATH"
else
  echo "FAIL bare lime-lex via PATH"; head -c 2000 "$W/path.err"; fail=$((fail + 1))
fi
echo "$((n - fail))/$n identical to flex"
[ $fail -eq 0 ]
