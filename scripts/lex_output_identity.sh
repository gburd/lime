#!/bin/sh
# scripts/lex_output_identity.sh OLD_LIME NEW_LIME [extra .lex ...]
#
# Byte-identity check for the .lex emitter: every tracked .lex in the
# tree (plus any extra files given) goes through both binaries, once
# natively (lime -X) and once with the flex skin (--target=c:flex),
# always written to the SAME directory so nothing path-dependent can
# differ.  Exit 0 iff every generated file, stderr and exit status
# match.  Used for Track B3 (%keyword_class / %operator_class must not
# change output for scanners that do not use them).
set -eu
OLD=$1
NEW=$2
shift 2
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=$(mktemp -d)
trap 'find "$WORK" -depth -delete' EXIT
mkdir "$WORK/in"
n=0
for f in $(cd "$ROOT" && git ls-files '*.lex') "$@"; do
    case $f in /*) src=$f ;; *) src=$ROOT/$f ;; esac
    # A scanner that uses %keyword_class / %operator_class / %runtime_rules is new output
    # by definition (an older lime rejects it); identity is about the rest.
    if grep -q '^%\(keyword_class\|operator_class\|runtime_rules\)' "$src"; then
        echo "skip (uses the class directives): $f"
        continue
    fi
    n=$((n + 1))
    cp "$src" "$WORK/in/${n}_$(basename "$src")"
done

gen() { # gen LIME TAG
    for f in "$WORK"/in/*.lex; do
        b=$(basename "$f" .lex)
        for mode in native flex; do
            mkdir "$WORK/out"
            rc=0
            if [ "$mode" = native ]; then
                "$1" -X -d"$WORK/out" "$f" >/dev/null 2>"$WORK/out/stderr" || rc=$?
            else
                "$1" -X --target=c:flex -d"$WORK/out" "$f" >/dev/null 2>"$WORK/out/stderr" || rc=$?
            fi
            echo "rc=$rc" >"$WORK/out/rc"
            mkdir -p "$WORK/$2/$b"
            mv "$WORK/out" "$WORK/$2/$b/$mode"
        done
    done
}
gen "$OLD" old
gen "$NEW" new
files=$(find "$WORK/old" -name '*.[ch]' | wc -l)
if diff -r "$WORK/old" "$WORK/new"; then
    echo "identical: $n .lex x {native, flex skin}, $files generated files"
else
    echo "DIFFERENT (see above)"
    exit 1
fi
