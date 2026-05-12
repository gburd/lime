#!/usr/bin/env bash
#
# check_version_consistency.sh
#
# Lime's version string lives in three places:
#
#   1. lime.c                    #define LIME_VERSION_STRING "X.Y.Z"
#   2. meson.build               project(... version : 'X.Y.Z' ...)
#   3. src/version.c             LIME_VERSION_STRING "X.Y.Z" fallback
#      (also mirrors the generator's macro)
#
# CI runs this check to catch accidental drift when someone bumps
# one location and forgets the others.  Exits non-zero and prints a
# helpful diff on mismatch.
#
# Safe to run anywhere in the worktree; only uses the repository
# root as a reference point.
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

extract_lime_c() {
    # #define LIME_VERSION_STRING "0.1.0"
    grep -E '^#define\s+LIME_VERSION_STRING\s+"[^"]+"' lime.c \
        | head -1 \
        | sed -E 's/.*"([^"]+)".*/\1/'
}

extract_meson_build() {
    # project('lime-parser', 'c', 'cpp',
    #   version : '0.1.0',
    grep -E "^\s*version\s*:\s*'[^']+'" meson.build \
        | head -1 \
        | sed -E "s/.*'([^']+)'.*/\1/"
}

extract_version_c() {
    # #define LIME_VERSION_STRING "0.1.0"   (fallback inside src/version.c)
    grep -E '^#define\s+LIME_VERSION_STRING\s+"[^"]+"' src/version.c \
        | head -1 \
        | sed -E 's/.*"([^"]+)".*/\1/'
}

lime_c_version=$(extract_lime_c)
meson_version=$(extract_meson_build)
version_c_version=$(extract_version_c)

if [[ -z "$lime_c_version"    ]]; then echo "ERROR: could not extract LIME_VERSION_STRING from lime.c"    >&2; exit 2; fi
if [[ -z "$meson_version"     ]]; then echo "ERROR: could not extract project version from meson.build"   >&2; exit 2; fi
if [[ -z "$version_c_version" ]]; then echo "ERROR: could not extract LIME_VERSION_STRING from src/version.c" >&2; exit 2; fi

echo "lime.c            : $lime_c_version"
echo "meson.build       : $meson_version"
echo "src/version.c     : $version_c_version"

if [[ "$lime_c_version" == "$meson_version" && "$meson_version" == "$version_c_version" ]]; then
    echo
    echo "OK: all three sources agree on version $lime_c_version"
    exit 0
fi

cat <<EOF >&2

ERROR: version strings disagree across the three sources of truth.

    lime.c            : $lime_c_version
    meson.build       : $meson_version
    src/version.c     : $version_c_version

Bump all three together when cutting a release.  The canonical
locations are:

    lime.c           line containing  #define LIME_VERSION_STRING "X.Y.Z"
    meson.build      line containing  version : 'X.Y.Z',
    src/version.c    line containing  #define LIME_VERSION_STRING "X.Y.Z"

EOF
exit 1
