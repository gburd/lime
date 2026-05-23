# Lime Parser Generator — packaging
#
# This directory holds package definitions for distributing Lime
# through common Linux/macOS package managers and as a tarball.
#
# What's here:
#   - lime.spec               -- RPM spec for Fedora / RHEL / openSUSE
#   - debian/                 -- Debian/Ubuntu source-package skeleton
#                                (control, rules, copyright, changelog)
#   - Formula/lime.rb         -- Homebrew formula (macOS / Linux)
#   - APKBUILD                -- Alpine Linux package build script
#   - PKGBUILD                -- Arch Linux package build script
#
# Nix is handled via the project root flake.nix.
#
# All package recipes assume the upstream tarball is produced by
# `make dist` (top-level Makefile) or `meson dist -C builddir`.
# Both produce lime-X.Y.Z.tar.gz with identical contents:
# everything tracked under `git ls-tree HEAD` minus build/CI cruft.
#
# To cut a release:
#
#   1. Bump VERSION in the top-level Makefile and meson.build.
#   2. `make dist` (or `meson dist -C builddir --no-tests`).
#   3. The tarball lands at ./build-dist/lime-X.Y.Z.tar.gz.
#   4. Update the source URL + SHA256 in each package recipe
#      below as needed.

This is documentation; the actual recipes are in the sibling files.
