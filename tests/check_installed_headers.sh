#!/bin/sh
# check_installed_headers.sh CC SRCDIR SYNTAX CFLAGS HEADER...
#   SYNTAX: meson's get_argument_syntax() -- 'msvc' (cl, clang-cl) or 'gcc'
#
# Every header meson installs under <prefix>/include/lime/ must compile on
# its own, the way a consumer includes it: #include <lime/NAME.h>, with
# what lime.pc gives a consumer (-I${includedir} -pthread) and C11 (plus
# /experimental:c11atomics on MSVC, which <stdatomic.h> needs there).
# Catches an installed
# header that #includes one that is not installed (lime_threads.h was
# missing from the list, so <lime/extension.h> did not compile).
#
# The headers are copied into a scratch include/lime/ exactly as the
# install list names them, so the test needs no `meson install`.
# Exit 0 clean, 1 failure, 77 skip (no compiler).
cc=$1; src=$2; syntax=$3; cflags=$4; shift 4
if [ -z "$cc" ] || ! command -v "$cc" >/dev/null 2>&1; then
  echo "SKIP: no C compiler"; exit 77
fi
W=$(mktemp -d)
trap 'find "$W" -mindepth 1 -delete; rmdir "$W"' EXIT
mkdir -p "$W/include/lime"
for h in "$@"; do cp "$src/$h" "$W/include/lime/"; done
bad=0
for h in "$W"/include/lime/*.h; do
  b=$(basename "$h")
  printf '#include <lime/%s>\nint main(void) { return 0; }\n' "$b" > "$W/t.c"
  if [ "$syntax" = msvc ]; then
    cmd="\"$cc\" /nologo /Zs $cflags /I\"$W/include\" \"$W/t.c\""
  else
    cmd="\"$cc\" -fsyntax-only -pthread $cflags -I\"$W/include\" \"$W/t.c\""
  fi
  # shellcheck disable=SC2086
  if eval "$cmd" > "$W/err" 2>&1; then
    echo "ok   <lime/$b>"
  else
    echo "FAIL <lime/$b>: $(grep -m1 'error' "$W/err")"; bad=1
  fi
done
exit $bad
