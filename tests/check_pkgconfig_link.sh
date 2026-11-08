#!/bin/sh
# check_pkgconfig_link.sh CC BUILDDIR
#
# A consumer links what `pkg-config --libs lime-compiler` prints, without
# --static (docs/API.md, README).  Both libraries are static archives, so
# lime-compiler.pc must pull in -llime_parser through Requires, not
# Requires.private (only read with --static): with .private, a program
# that composes failed to link with undefined lime_parser_version /
# snapshot_build_from_tables.  Installs into a scratch DESTDIR and builds
# the program the way a consumer of the package would.
# Exit 0 pass, 1 fail, 77 skip (no pkg-config, meson or compiler).
cc=$1; b=$2
command -v pkg-config >/dev/null 2>&1 || { echo "SKIP: no pkg-config"; exit 77; }
command -v meson >/dev/null 2>&1 || { echo "SKIP: no meson on PATH"; exit 77; }
command -v "$cc" >/dev/null 2>&1 || { echo "SKIP: no C compiler"; exit 77; }
W=$(mktemp -d); trap 'find "$W" -mindepth 1 -delete; rmdir "$W"' EXIT
meson install -C "$b" --no-rebuild --destdir "$W/root" > "$W/install.log" 2>&1 \
    || { echo "FAIL: meson install"; head -c 2000 "$W/install.log"; exit 1; }
pcdir=$(find "$W/root" -name lime-compiler.pc -exec dirname {} \; | head -1)
[ -n "$pcdir" ] || { echo "FAIL: no lime-compiler.pc installed"; exit 1; }
PKG_CONFIG_LIBDIR=$pcdir; PKG_CONFIG_SYSROOT_DIR=$W/root
export PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR
cat > "$W/c.c" <<'C'
#include <lime/parser.h>
#include <lime/lime_compiler.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    const char *g = "%token A.\ns ::= A.\n";
    char *err = NULL;
    ParserSnapshot *s = lime_compile_grammar_text(g, strlen(g), &err);
    printf("lime %s, compile %s\n", lime_parser_version(), s ? "ok" : (err ? err : "failed"));
    if (s == NULL) { free(err); return 1; }
    lime_snapshot_release(s);
    return 0;
}
C
libs=$(pkg-config --libs lime-compiler); cflags=$(pkg-config --cflags lime-compiler)
echo "pkg-config --libs lime-compiler: $libs"
# shellcheck disable=SC2086
if ! "$cc" $cflags -o "$W/c" "$W/c.c" $libs > "$W/err" 2>&1; then
    echo "FAIL link with pkg-config --libs lime-compiler:"; head -c 2000 "$W/err"; exit 1
fi
"$W/c" || { echo "FAIL: the linked program did not compile a grammar"; exit 1; }
echo "ok   pkg-config --libs lime-compiler links a composing program"
