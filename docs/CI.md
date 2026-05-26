# Continuous Integration

Lime runs CI on **two providers** -- GitHub Actions (the broad
multi-platform matrix) and Codeberg's hosted Forgejo Actions (the
authoritative Linux mirror).  Both fire on every `push` and
`pull_request` against `main`.

* `.github/workflows/ci.yml` -- Linux x86_64 + aarch64, macOS Intel +
  Apple Silicon, Windows 11 x86_64 + aarch64.  ~22 jobs.
* `.forgejo/workflows/ci.yml` -- Linux x86_64 only (Codeberg's
  hosted runners are Linux-only on the public free tier).  ~9 jobs.

A merge requires every job in both pipelines to be green.

## GitHub Actions matrix

### Build (Linux x86_64)

| Job                                       | Runner          | Compiler     | Notes |
|-------------------------------------------|-----------------|--------------|-------|
| `build (ubuntu-22.04, gcc)`               | `ubuntu-22.04`  | `gcc` (apt)  | LTS baseline |
| `build (ubuntu-22.04, clang)`             | `ubuntu-22.04`  | `clang` (apt)|       |
| `build (ubuntu-24.04, gcc)`               | `ubuntu-24.04`  | `gcc` (apt)  | LTS current |
| `build (ubuntu-24.04, clang)`             | `ubuntu-24.04`  | `clang` (apt)|       |

`ubuntu-latest` is intentionally avoided -- the previous
`ubuntu-latest, clang` job hit intermittent `ninja Build` failures
caused by the rolling-image substrate drifting under us.  The two
explicit LTS pins give us reproducibility and let us trace any
regression to the specific package set on each runner image.

### Build (Linux aarch64)

| Job                                       | Runner               | Compiler     |
|-------------------------------------------|----------------------|--------------|
| `build (ubuntu-24.04-arm, gcc)`           | `ubuntu-24.04-arm`   | `gcc` (apt)  |
| `build (ubuntu-24.04-arm, clang)`         | `ubuntu-24.04-arm`   | `clang` (apt)|

GitHub launched the free public ARM runners (`ubuntu-24.04-arm`) in
mid-2024.  Native aarch64 is the second-most-common deployment
target after x86_64, especially for PG hosts on Graviton / Ampere /
Snapdragon; covering it on every PR keeps the SIMD / atomic /
alignment paths honest.

### Build (macOS)

| Job                                       | Runner       | Compiler        | Arch     |
|-------------------------------------------|--------------|-----------------|----------|
| `build (macos-13, apple-clang)`           | `macos-13`   | Apple Clang     | x86_64   |
| `build (macos-14, apple-clang)`           | `macos-14`   | Apple Clang     | aarch64  |

We pin `-Dllvm=disabled` on macOS because the Xcode-bundled LLVM
doesn't expose the full `llvm-config` / Orc.h surface lime's JIT
needs.  Coverage of the JIT path stays on Linux + Nix devShell.
`macos-15` is intentionally skipped for now -- macos-13 +
macos-14 covers Intel + Apple Silicon and adding macos-15 only
re-tests Apple Silicon under a newer Xcode.

### Build (Windows 11)

| Job                                                 | Runner            | Toolchain         | Arch     |
|-----------------------------------------------------|-------------------|-------------------|----------|
| `build (windows-2022, msvc)`                        | `windows-2022`    | MSVC `cl.exe`     | x86_64   |
| `build (windows-2022, clang-cl)`                    | `windows-2022`    | LLVM `clang-cl`   | x86_64   |
| `build (windows-2022, mingw-gcc)`                   | `windows-2022`    | MinGW-GCC (UCRT64)| x86_64   |
| `build (windows-11-arm, msvc)`                      | `windows-11-arm`  | MSVC `cl.exe`     | aarch64  |
| `build (windows-11-arm, clang-cl)`                  | `windows-11-arm`  | LLVM `clang-cl`   | aarch64  |

The `ilammy/msvc-dev-cmd@v1` action seeds the VS environment for
both MSVC and Clang-CL; meson + ninja come in via `pip`.  MinGW
runs through `msys2/setup-msys2@v2` (UCRT64 subsystem) and uses
the MSYS2-bundled meson + ninja + pkgconf.

Lime's existing Windows portability layer (commits b50d03f,
aa91951, d91b3a1, 38aa7ef, 0e3fdc3, ...) handles the MSVC CRT
name mangling, `_Atomic` opt-in (`/experimental:c11atomics`), and
the pthread shim.  Several POSIX-only tests are
configure-time-skipped on Windows by `tests/meson.build` guards
keyed on `host_machine.system() != 'windows'`:

* `test_lex_emit` -- relies on `open_memstream(3)` (POSIX-only).
* `test_lex_lexer_include` -- relies on `pipe(2)` to capture stderr.
* `test_snapshot_create` -- relies on the `fork+exec+dlopen`
  pipeline, which is stubbed on Windows pending a `CreateProcess`
  + `LoadLibrary` rewrite.
* `test_extension_rebuild` -- same `fork+exec+dlopen` dependency.

These skips are expected and don't fail the suite; the rest of
the Windows test surface passes on every toolchain row.

ARM MinGW is intentionally skipped: `msys2/setup-msys2` doesn't
currently target Windows-on-ARM, and the cross-compile path
would add ~10 min of build time for marginal coverage.  The two
ARM-MSVC rows already exercise the native aarch64 codegen.

### Sanitizers

| Job                                       | Runner          | Mode |
|-------------------------------------------|-----------------|------|
| `sanitizers-linux (address)`              | `ubuntu-24.04`  | `b_sanitize=address` |
| `sanitizers-linux (thread)`               | `ubuntu-24.04`  | `b_sanitize=thread`  |
| `sanitizers-linux (undefined)`            | `ubuntu-24.04`  | `b_sanitize=undefined` |
| `sanitizers-macos (address)`              | `macos-14`      | `b_sanitize=address` |

Apple's bundled Clang has working AddressSanitizer but its
ThreadSanitizer + UndefinedBehaviorSanitizer parity is patchy
across macOS releases (TSan in particular requires a
`libclang_rt` Xcode ships intermittently for non-Intel hosts).
Address is the practical subset that runs reliably on every
macos-14 image vintage; we'll extend as Apple's TSan story
stabilizes.

### Other Linux jobs

| Job                  | Runner          | What it does |
|----------------------|-----------------|--------------|
| `valgrind`           | `ubuntu-24.04`  | `scripts/check_memory.sh` -- valgrind memcheck over the suite. |
| `coverage`           | `ubuntu-24.04`  | `scripts/check_coverage.sh` -- gcovr gate at LIME_COV_MIN_LINES=78 / LIME_COV_MIN_BRANCHES=60. |
| `version-consistency`| `ubuntu-24.04`  | Asserts `LIME_VERSION_STRING` (lime.c), `project(version: ...)` (meson.build), and `src/version.c` agree. |

### LLVM matrix

| Job                                 | Runner          | Args |
|-------------------------------------|-----------------|------|
| `llvm-matrix (shared)`              | `ubuntu-24.04`  | `-Dllvm=enabled` |
| `llvm-matrix (static)`              | `ubuntu-24.04`  | `-Dllvm=enabled -Dllvm-static=true` |
| `llvm-matrix (disabled)`            | `ubuntu-24.04`  | `-Dllvm=disabled` |

Each variant builds + tests + asserts the `tests/test_jit`
binary's `libLLVM` linkage count matches the requested config:
the shared row must link `libLLVM`, the static + disabled rows
must not.  Together with the
`include/jit_llvm_compat.h` shim's version split (LLVM 14-20 vs
LLVM 21+), this catches API-surface drift across LLVM majors.

## Codeberg / Forgejo matrix

Codeberg's hosted Forgejo Actions don't currently offer ARM,
macOS, or Windows runners on the public free tier, so this
pipeline mirrors the Linux portion of the GitHub matrix only.
When Codeberg adds hosted ARM (tracked at
[codeberg.org/actions/meta](https://codeberg.org/actions/meta)),
re-add the `ubuntu-24.04-arm` matrix entries.

| Job                                 | Tier             | Notes |
|-------------------------------------|------------------|-------|
| `build (ubuntu-22.04, gcc)`         | `codeberg-medium`| LTS baseline |
| `build (ubuntu-22.04, clang)`       | `codeberg-medium`|       |
| `build (ubuntu-24.04, gcc)`         | `codeberg-medium`| LTS current |
| `build (ubuntu-24.04, clang)`       | `codeberg-medium`|       |
| `sanitizers (address)`              | `codeberg-medium`| `b_sanitize=address`   |
| `sanitizers (thread)`               | `codeberg-medium`| `b_sanitize=thread`    |
| `sanitizers (undefined)`            | `codeberg-medium`| `b_sanitize=undefined` |
| `valgrind`                          | `codeberg-medium`| `scripts/check_memory.sh` |
| `coverage`                          | `codeberg-medium`| gcovr gate |
| `version-consistency`               | `codeberg-tiny`  | three-source agreement |
| `llvm-matrix (shared/static/disabled)`| `codeberg-medium` | linkage-asserted |

### `.forgejo/workflows/pages.yml`

Doxygen-generated API docs are deployed to
[gregburd.codeberg.page/lime/](https://gregburd.codeberg.page/lime/)
on every push that touches `include/`, `src/`, or `docs/`.  Runs
on `codeberg-small`.  Fails the deploy if doxygen prints any
warning -- combined with `WARN_AS_ERROR=YES` in `docs/Doxyfile`,
this means an undocumented public symbol or broken `\ref` is
caught at PR time.

### Codeberg runner tiers

| Label             | CPU | RAM | Runtime cap |
|-------------------|-----|-----|-------------|
| `codeberg-tiny`   | 1   | 2 G | 2 min       |
| `codeberg-small`  | 2   | 4 G | 5 min       |
| `codeberg-medium` | 4   | 8 G | 10 min      |

> Note: RAM allocation includes filesystem writes (with a 2 GB
> tempfile allowance).  Docker-in-docker is not officially
> supported.

All Lime jobs run on the default runner image
(`ghcr.io/catthehacker/ubuntu:act-latest`), an Ubuntu-based
image with standard build tools preinstalled.  Workflows do
**not** pin a `container:` -- pulling Debian explicitly added
latency and occasional pull failures on Codeberg's pool.  Each
job installs its own additional packages via `sudo apt-get
install`.

See [codeberg.org/actions/meta](https://codeberg.org/actions/meta)
for the canonical runner-label list and Codeberg's hosted-Actions
FAQ.

## Pre-merge expectations

Before a PR merges into `main`, every job in both pipelines must
succeed.  In practice that means:

* **Build clean** under GCC + Clang on Ubuntu 22.04 and 24.04 (x86_64),
  Ubuntu 24.04 (aarch64), Apple Clang on macOS 13 + 14, and MSVC +
  Clang-CL + MinGW-GCC on Windows 11.  Zero warnings on every row.
* **All tests pass** under each toolchain (Windows skips the four
  POSIX-gated tests listed above; that's expected).
* **Sanitizers clean**: address + thread + undefined on Linux,
  address on macOS.
* **`scripts/check_memory.sh`** runs cleanly under valgrind.
* **LLVM=enabled / enabled+static / disabled** all build, test, and
  link as advertised.
* **Doxygen** produces zero warnings.
* **Version-string** is consistent across `lime.c`, `meson.build`,
  and `src/version.c`.

## Local equivalents

The CI invokes the same scripts and meson commands a developer can
run locally.  Replicate any failing job with:

```sh
# build matrix (Linux only -- macOS / Windows need that platform)
nix develop --command bash -c \
  'meson setup build -Dllvm=disabled && ninja -C build && meson test -C build'

# version check
./scripts/check_version_consistency.sh

# valgrind
./scripts/check_memory.sh

# llvm matrix
nix develop --command bash -c \
  'meson setup build-llvm-shared   -Dllvm=enabled                    && ninja -C build-llvm-shared   && meson test -C build-llvm-shared'
nix develop --command bash -c \
  'meson setup build-llvm-static   -Dllvm=enabled -Dllvm-static=true && ninja -C build-llvm-static   && meson test -C build-llvm-static'
nix develop --command bash -c \
  'meson setup build-llvm-disabled -Dllvm=disabled                   && ninja -C build-llvm-disabled && meson test -C build-llvm-disabled'

# doxygen
nix develop --command bash -c 'cd docs && doxygen Doxyfile'

# sanitizers
nix develop --command bash -c \
  'meson setup build-asan -Dllvm=disabled -Db_sanitize=address      && ninja -C build-asan      && meson test -C build-asan'
nix develop --command bash -c \
  'meson setup build-tsan -Dllvm=disabled -Db_sanitize=thread       && ninja -C build-tsan      && meson test -C build-tsan'
nix develop --command bash -c \
  'meson setup build-ubsan -Dllvm=disabled -Db_sanitize=undefined   && ninja -C build-ubsan     && meson test -C build-ubsan'
```
