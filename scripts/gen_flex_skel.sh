#!/bin/sh
# Regenerate src/lex/flex_skel.inc from src/lex/flex_skel.txt (one C string
# literal per line).  Run after editing flex_skel.txt; commit both files.
#   usage: scripts/gen_flex_skel.sh [skel.txt] [out.inc]
set -eu
in=${1:-src/lex/flex_skel.txt}
out=${2:-src/lex/flex_skel.inc}
{ echo "/* Generated from the flex-mode skeleton; one string per line. */"
  sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/"/' -e 's/$/",/' "$in"; } > "$out"
