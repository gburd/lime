#!/bin/sh
# Byte-identity check for the Lime .lex dialect.
#
# usage: lex_golden_check.sh BASE_LIME NEW_LIME [SRCDIR]
#
# Runs every tracked *.lex file through both lime binaries with the same
# arguments and the same output path (generated files embed the output
# basename), in four modes: plain -X, -X --target=c:flex (the flex skin),
# -X --lex-no-vectorize, and -X --target=rust.  Every generated file and
# every exit status must be identical.  BASE_LIME is a lime built from the
# reference commit (35eefbc for Track E).  Exit 0 = identical, 1 = differs.
set -u
base=$1; new=$2; src=${3:-$(cd "$(dirname "$0")/../.." && pwd)}
tmp=$(mktemp -d)
cleanup() { find "$tmp" -depth -mindepth 1 -delete 2>/dev/null; rmdir "$tmp" 2>/dev/null; }
trap cleanup EXIT
nfiles=0; nout=0; bad=0
for f in $(cd "$src" && git ls-files '*.lex'); do
  nfiles=$((nfiles+1))
  for mode in plain flex novec rust; do
    case $mode in
      plain) args="-X";;
      flex)  args="-X --target=c:flex";;
      novec) args="-X --lex-no-vectorize";;
      rust)  args="-X --target=rust";;
    esac
    for who in base new; do
      mkdir -p "$tmp/$who/out"
      cp "$src/$f" "$tmp/$who/in.lex"
      eval bin=\$$who
      (cd "$tmp/$who" && "$bin" $args -dout in.lex >/dev/null 2>"$tmp/$who.err"; echo $? >"$tmp/$who.rc")
    done
    if ! cmp -s "$tmp/base.rc" "$tmp/new.rc"; then
      echo "DIFF exit status: $f ($mode)"; bad=$((bad+1))
    fi
    # Rust output lands next to the input, C output under out/.
    for who in base new; do
      (cd "$tmp/$who" && find . -type f ! -name in.lex | sort | xargs cat) >"$tmp/$who.all"
    done
    if ! cmp -s "$tmp/base.all" "$tmp/new.all"; then
      echo "DIFF output: $f ($mode)"; bad=$((bad+1))
    fi
    nout=$((nout + $(cd "$tmp/new" && find . -type f ! -name in.lex | wc -l)))
    find "$tmp" -depth -mindepth 1 -delete
  done
done
echo "lex golden: $nfiles .lex files x 4 modes, $nout generated files compared, $bad differences"
[ "$bad" -eq 0 ]
