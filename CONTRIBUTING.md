# Contributing to Lime

Thanks for helping improve Lime. This file covers the essentials: how to
build, how to test, and the conventions a patch must satisfy before it
lands.

## License of contributions

Lime is public domain, distributed under the BSD Zero Clause License
(0BSD; see `LICENSE`). By submitting a contribution you agree that it is
released under the same terms and that you have the right to do so. There
is no CLA and no `Signed-off-by` / DCO requirement; keep it simple.

If your change adds or modifies a grammar derived from a third party (for
example PostgreSQL's `gram.y`), update `NOTICE` so the upstream attribution
stays accurate.

## Building and testing

Lime builds with Meson + Ninja. The reproducible development environment is
a Nix flake:

```sh
nix develop            # drops you into a shell with meson, ninja, llvm, etc.
meson setup build      # add -Dllvm=disabled to skip the JIT dependency
ninja -C build         # build the lime generator + runtime library
meson test -C build    # run the test suite
```

Without Nix, install `meson`, `ninja`, `pkg-config`, and a C11 compiler
(GCC or Clang), then run the same `meson setup` / `ninja` / `meson test`
commands.

Before opening a pull request:

- `meson test -C build` must pass.
- `scripts/check_version_consistency.sh` must pass (all version strings
  agree). Bump `lime.c`, `meson.build`, `src/version.c`, and
  `docs/Doxyfile` together when cutting a release.
- New runtime code should keep `scripts/check_coverage.sh` above its floor.

## Coding standard

- C11. Match the surrounding style; the tree is formatted with
  `clang-format` (see `.clang-format`).
- Warnings are errors in spirit: builds must be clean under
  `-Wall -Wextra`. Generated-code hygiene is gated by
  `tests/test_codegen_strict_warnings.c`.
- Public headers live in `include/lime/`; consumers include
  `<lime/...>`. Do not add unprefixed public symbols.
- No new runtime dependencies without justification.

## Reporting bugs and security issues

Open an issue for ordinary bugs. For security-sensitive reports (Lime is a
parser generator that can run inside PostgreSQL and other hosts), follow
`SECURITY.md` instead of filing a public issue.
