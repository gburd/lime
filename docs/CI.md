# Continuous Integration

Lime's authoritative CI runs on **Codeberg's hosted Forgejo Actions**.
Configuration lives under [`.forgejo/workflows/`](../.forgejo/workflows/).

## Pipelines

### `.forgejo/workflows/ci.yml`

Runs on every `push` and `pull_request` against `main`.  Five jobs:

| Job                  | Tier             | What it does |
|----------------------|------------------|--------------|
| `build`              | `codeberg-medium`| `gcc` and `clang` matrix.  Builds the standalone generator and the meson project; runs the full test suite. |
| `sanitizers`         | `codeberg-medium`| Address, thread, undefined sanitizer matrix.  Build + test under each. |
| `valgrind`           | `codeberg-medium`| `scripts/check_memory.sh` -- valgrind over the test binaries, checks for leaks and invalid reads. |
| `version-consistency`| `codeberg-tiny`  | Asserts that `LIME_VERSION_STRING` in `lime.c`, `project(version: ...)` in `meson.build`, and the literal in `src/version.c` agree.  Cheap; runs on every push. |
| `llvm-matrix`        | `codeberg-medium`| Three variants -- `-Dllvm=enabled`, `-Dllvm=enabled -Dllvm-static=true`, `-Dllvm=disabled`.  Each builds and runs the test suite, then asserts the resulting `tests/test_jit` binary's `libLLVM` linkage count matches the requested config. |

### `.forgejo/workflows/pages.yml`

Doxygen-generated API docs are deployed to
[gregburd.codeberg.page/lime/](https://gregburd.codeberg.page/lime/) on
every push that touches `include/`, `src/`, or `docs/`.  Runs on
`codeberg-small`.

The pages workflow captures `doxygen` stderr and prints the warning
count.  Combined with `WARN_AS_ERROR=YES` in `docs/Doxyfile`, this
means: if anyone introduces an undocumented public symbol, a broken
`\ref`, or other doxygen error, the docs deploy fails and the warning
is visible in the CI log.

## Codeberg runner tiers

Codeberg's hosted runners advertise three labels:

| Label             | CPU | RAM | Runtime cap |
|-------------------|-----|-----|-------------|
| `codeberg-tiny`   | 1   | 2 G | 2 min       |
| `codeberg-small`  | 2   | 4 G | 5 min       |
| `codeberg-medium` | 4   | 8 G | 10 min      |

> Note: RAM allocation includes filesystem writes (with a 2 GB
> tempfile allowance).  Docker daemon inside runners is not officially
> supported.

See [codeberg.org/actions/meta](https://codeberg.org/actions/meta) for
the canonical list and Codeberg's hosted-Actions FAQ.

## Other directories

* `.github/workflows/` -- inert at the moment because pushes go to
  Codeberg only.  Kept as a portability fallback in case anyone needs
  to mirror Lime to GitHub for additional execution coverage.

* `.woodpecker/ci.yml` -- removed.  Codeberg's Woodpecker CI is a
  separate offering with closed-testing access; we don't currently use
  it.  The Forgejo Actions pipeline above covers the same ground.

## Pre-merge expectations

Before a PR is merged into `main`, all five `ci.yml` jobs and the
`pages.yml` build must succeed.  In practice that means:

* Build clean under `gcc` and `clang`, zero warnings.
* All 34+ tests pass under no-sanitizer, address, thread, and
  undefined sanitizers.
* `scripts/check_memory.sh` runs cleanly under valgrind.
* `LLVM=enabled`/`enabled+static`/`disabled` all build and test.
* Doxygen produces zero warnings.
* The version string is consistent across the three sources of truth.

## Local equivalents

The CI invokes the same scripts and meson commands a developer can run
locally.  Replicate any failing job with:

```sh
# build matrix
nix develop --command bash -c 'meson setup build && ninja -C build && meson test -C build'

# version check
./scripts/check_version_consistency.sh

# valgrind
./scripts/check_memory.sh

# llvm matrix
nix develop --command bash -c 'meson setup build-llvm-shared   -Dllvm=enabled                   && ninja -C build-llvm-shared   && meson test -C build-llvm-shared'
nix develop --command bash -c 'meson setup build-llvm-static   -Dllvm=enabled -Dllvm-static=true && ninja -C build-llvm-static   && meson test -C build-llvm-static'
nix develop --command bash -c 'meson setup build-llvm-disabled -Dllvm=disabled                  && ninja -C build-llvm-disabled && meson test -C build-llvm-disabled'

# doxygen
nix develop --command bash -c 'cd docs && doxygen Doxyfile'
```
