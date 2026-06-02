#!/usr/bin/env bash
# build-lto.sh - Configure a Lime build directory with LTO enabled.
#
# Why a wrapper?  GCC and Clang both produce LTO bytecode in their .o
# files when -flto is on (meson's -Db_lto=true).  Ordinary `ar` and
# `ranlib` do not load the LTO plugin when creating static archives,
# so the symbols from those LTO .o files are invisible to the linker
# when the archive is later consumed via -lwhatever.  This produces
# spurious "undefined reference to ..." errors at link time.
#
# The fix is well-known: use the compiler's plugin-aware archiver
# wrappers (gcc-ar / gcc-ranlib for GCC, llvm-ar / llvm-ranlib for
# Clang).  Meson honours the AR / RANLIB / NM environment variables
# at setup time and bakes those choices into the generated build
# graph, so they only need to be set when the build directory is
# first configured.
#
# Usage:
#   ./scripts/build-lto.sh build-lto                     # GCC LTO
#   CC=clang ./scripts/build-lto.sh build-lto-clang      # Clang LTO
#   ./scripts/build-lto.sh build-lto-pgo -Dlime_pgo=use  # LTO + PGO
#
# Any extra arguments are forwarded to `meson setup`, so the standard
# flags (-Dbuildtype=release, -Dllvm=disabled, etc.) compose normally.
#
# After setup, build and test as usual:
#   ninja -C build-lto
#   meson test -C build-lto

set -euo pipefail

if [ "$#" -lt 1 ]; then
    echo "usage: $0 <build-dir> [meson-setup-args...]" >&2
    exit 2
fi

build_dir=$1
shift

# Pick archiver + nm wrappers based on $CC.  We FORCE these (not
# conditional defaults) because nix and various distro environments
# pre-set AR=ar / RANLIB=ranlib in the shell, and those plain
# binutils tools are exactly the ones that don't load the LTO plugin.
# An empty/unset value falls through to the wrapper choice; a value
# that already names a wrapper (gcc-ar / llvm-ar) is preserved.
case "${CC:-cc}" in
    *clang*)
        case "${AR:-}" in *llvm-ar*) : ;; *) AR=llvm-ar ;; esac
        case "${RANLIB:-}" in *llvm-ranlib*) : ;; *) RANLIB=llvm-ranlib ;; esac
        case "${NM:-}" in *llvm-nm*) : ;; *) NM=llvm-nm ;; esac
        # Clang LTO additionally wants lld (or a binutils gold/bfd
        # ld with LLVMgold.so installed).  Honour any pre-set
        # LDFLAGS the caller supplied; otherwise nudge toward lld.
        if [ -z "${LDFLAGS:-}" ]; then
            if command -v ld.lld >/dev/null 2>&1; then
                export LDFLAGS="-fuse-ld=lld"
            fi
        fi
        ;;
    *)
        case "${AR:-}" in *gcc-ar*) : ;; *) AR=gcc-ar ;; esac
        case "${RANLIB:-}" in *gcc-ranlib*) : ;; *) RANLIB=gcc-ranlib ;; esac
        case "${NM:-}" in *gcc-nm*) : ;; *) NM=gcc-nm ;; esac
        ;;
esac

export AR RANLIB NM

echo "build-lto: CC=${CC:-cc} AR=$AR RANLIB=$RANLIB NM=$NM"
echo "build-lto: LDFLAGS=${LDFLAGS:-}"
echo "build-lto: meson setup $build_dir -Db_lto=true $*"

exec meson setup "$build_dir" -Db_lto=true "$@"
