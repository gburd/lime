#!/bin/sh
# tests/compat/fetch_corpus.sh -- download the yacc/lex compatibility corpus
# into DIR (default /tmp/compat-dl), laid out as run_corpus.py expects.
# Not vendored: bison, gawk, bc and PostgreSQL are GPL/other licences and
# this repository is 0BSD.  Every download is checked against a pinned
# sha256; a mismatch stops the script.  Tests that need the corpus SKIP
# when it is absent.
#
# usage: fetch_corpus.sh [DIR]
set -eu
D=${1:-/tmp/compat-dl}
mkdir -p "$D"
cd "$D"

fetch() {  # file url sha256
  if [ ! -f "$1" ]; then
    echo "fetch $1"
    curl -fsSL -o "$1.part" "$2" && mv "$1.part" "$1"
  fi
  got=$(sha256sum "$1" | cut -d' ' -f1)
  if [ "$got" != "$3" ]; then
    echo "sha256 mismatch for $1: got $got want $3" >&2
    exit 1
  fi
}
unpack() {  # archive dir
  [ -d "$2" ] || tar xf "$1"
}

fetch bison-3.8.2.tar.xz https://ftp.gnu.org/gnu/bison/bison-3.8.2.tar.xz \
  9bba0214ccf7f1079c5d59210045227bcf619519840ebfa80cd3849cff5a5bf2
unpack bison-3.8.2.tar.xz bison-3.8.2
fetch flex-2.6.4.tar.gz https://github.com/westes/flex/releases/download/v2.6.4/flex-2.6.4.tar.gz \
  e87aae032bf07c26f85ac0ed3250998c37621d95f8bd748b31f15b33c45ee995
unpack flex-2.6.4.tar.gz flex-2.6.4
fetch byacc.tar.gz https://invisible-island.net/archives/byacc/byacc-20260126.tgz \
  b618c5fb44c2f5f048843db90f7d1b24f78f47b07913c8c7ba8c942d3eb24b00
unpack byacc.tar.gz byacc-20260126
fetch gawk.tar.xz https://ftp.gnu.org/gnu/gawk/gawk-5.3.1.tar.xz \
  694db764812a6236423d4ff40ceb7b6c4c441301b72ad502bb5c27e00cd56f78
unpack gawk.tar.xz gawk-5.3.1
fetch bc-1.07.1.tar.gz https://ftp.gnu.org/gnu/bc/bc-1.07.1.tar.gz \
  62adfca89b0a1c0164c2cdca59ca210c1d44c3ffc46daf9931cf4942664cb02a
unpack bc-1.07.1.tar.gz bc-1.07.1
fetch pg-REL_18_0.tar.gz https://github.com/postgres/postgres/archive/refs/tags/REL_18_0.tar.gz \
  d9071ab45c2c45a3c8371495539ea7d2ed4d8035f72de20cb7cddfe23081cb82
unpack pg-REL_18_0.tar.gz postgres-REL_18_0
# one-true-awk: the Debian orig tarball (the onetrue-awk GitHub tarball URL
# used earlier returned a 9-byte "Not Found").
fetch original-awk_2025-12-25.orig.tar.gz \
  https://deb.debian.org/debian/pool/main/o/original-awk/original-awk_2025-12-25.orig.tar.gz \
  5eb8bb449848b2860ecff1fba09a0429853f849277c7e8fa2825ba4a553af920
unpack original-awk_2025-12-25.orig.tar.gz original-awk-2025-12-25
fetch otawk-awkgram.y https://raw.githubusercontent.com/onetrue-awk/awk/2nd-edition/awkgram.y \
  f4d5579ad4223aa88409119851ff8ee215d1ec9b5a20674174fff1e2de0be416
[ -f freebsd-awkgram.y ] || cp otawk-awkgram.y freebsd-awkgram.y   # same file (sha256 equal)
# The C11 grammar and scanner (Jutta Degener's, as updated for C11).
fetch c11.y https://www.quut.com/c/ANSI-C-grammar-y-2011.html.y \
  04d95ad5296a7569daf68440aadef9e1e062f6be41ca76e21590304dae9b3584
fetch c11.l https://www.quut.com/c/ANSI-C-grammar-l-2011.html.l \
  22a80c75ce5d09c5b84d5890383a8882b37d67087916fcfa47e9eb3443df836b
# autoconf: the AC_PROG_LEX probe program (tests/test_ac_prog_lex.sh).
fetch autoconf-2.69.tar.xz https://ftp.gnu.org/gnu/autoconf/autoconf-2.69.tar.xz \
  64ebcec9f8ac5b2487125a86a7760d2591ac9e1d3dbd59489633f9de62a57684
unpack autoconf-2.69.tar.xz autoconf-2.69
fetch autoconf-2.73.tar.xz https://ftp.gnu.org/gnu/autoconf/autoconf-2.73.tar.xz \
  9fd672b1c8425fac2fa67fa0477b990987268b90ff36d5f016dae57be0d6b52e
unpack autoconf-2.73.tar.xz autoconf-2.73
echo "corpus ready in $D"
