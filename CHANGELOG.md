# Changelog

All notable changes to the Lime parser generator are documented here.

The format is loosely based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Pre-1.0 minor bumps may include source- or ABI-level changes; see the entry
text for each release for details.

For exhaustive per-release detail (rationale, benchmark numbers, letter
references, files touched), read the annotated tag message:

```sh
git tag --list 'v*'
git show v0.10.0
```

## [Unreleased]

### Added
- `CHANGELOG.md` (this file).

### Documentation
- `docs/API.md` now covers `parse_begin_borrowed`,
  `lime_lint_grammar_in_process`, and the `lime-compiler.pc` pkg-config
  contract added in v0.10.0.
- `docs/RUST_OUTPUT.md` now covers the symmetric `%action_c` /
  `%action_rust` directive pair added in v0.12.0.

## [0.12.0] - 2026-06-04

`--target=rust` correctness: action bodies, exit code, per-target
dispatch.

### Added
- `%action_rust` directive (alias for the existing `%rust_action`) and
  the symmetric `%action_c` no-op alias for the inline brace body. The
  pair lets a grammar carry both a C and a Rust action body for the
  same production and migrate production-by-production.

### Fixed
- Multi-line action bodies on the Rust target are no longer silently
  replaced with `// empty action`. The empty-body check now walks past
  leading whitespace and only treats the body as empty when no
  non-whitespace remains.
- `lime --target=rust` no longer runs the C-emit pipeline, which
  previously failed to open `limpar.c` and caused exit 1 even though
  the `.rs` was written successfully. The C pipeline now runs only
  when a C skin (`--skin=bison` / `--skin=flex`) is also requested.

ABI: no change. All v0.10.x / v0.11.x APIs unchanged.

## [0.11.0] - 2026-06-04

Bug-fix release.

### Fixed
- Removed the spurious `--enable=safe has no effect without
  --target=rust` warning that fired on every C-target build since
  v0.9.3. The feature-explicit tracker now distinguishes user-set
  values from defaults, so the warning fires only when the user
  explicitly opts in.

ABI: no change.

## [0.10.0] - 2026-06-02

Borrowed snapshot, in-process LSP, performance infrastructure.

### Added
- `parse_begin_borrowed(snap)` public API. Skips the atomic refcount
  on `snapshot_acquire` / `snapshot_release` for callers that can
  guarantee the snapshot outlives the parse session. Measured 3.4x
  throughput uplift at 8 threads on `bench/bench_parse_fanout`
  (4M -> 13M parses/sec).
- `lime_lint_grammar_in_process(text, len, &diags)` public API. Runs
  the same parse + `FindActions` + `lint_grammar` pipeline as
  `lime -L`, in-process, with no fork/exec/temp-file. Used by
  `lime-lsp`'s diagnostic refresh path (default ON via the new
  `-Dlime_lsp_in_process=enabled` meson option). Measured ~10% /
  200ms saved per LSP request on PG's 21k-line `gram.lime`.
- `lime-compiler.pc` pkg-config file. Stable link contract for
  downstream consumers needing `lime_compile_grammar_in_process`.
  `liblime_compiler.a` is now installable.
- Honest one-shot warning when `lime_compile_grammar_text` falls back
  to the subprocess pipeline. Names the missing link
  (`-llime-compiler`). `LIME_FORCE_SUBPROCESS=1` suppresses.
- `scripts/build-lto.sh` and `scripts/build-pgo.sh` wrappers for the
  production LTO + PGO build recipes.
- `bench/bench_parse_fanout` (multi-thread parse-server scaling) and
  `bench/lex_state_count` (DFA-size lexer throughput).
- LTO CI job (gcc + clang) on Codeberg + GitHub mirror.
- Thread-local `ParseContext` pool (POSIX). ~5% single-thread uplift,
  eliminates 3 mallocs + 3 frees per parse.

### Changed
- `ParserSnapshot` field layout reordered: hot read fields in the
  first two cachelines, refcount/magic/version on cacheline 2, cold
  setup fields after. ABI version bumps from 1 to 2. No source-level
  break (struct fields accessed by name everywhere in tree).

### Performance
- `bench_parse_fanout` 8-thread borrowed: 3.4x (4M -> 13M parses/sec)
- `bench_jit_real_parser` hot path: +3-5% (LTO release)
- `bench_lsc_small` (32-state lex): +51% (PGO=use)
- `bench_lsc_large` (122-state lex): +26% (PGO=use)
- Binary size (`parser_bench`): -19% (LTO release)

### Tests
- 125 / 125 stock + ASan + UBSan + TSan, 24 CI jobs green across Linux
  x86_64/arm64, macOS-14, Windows (msvc + mingw-gcc + clang-cl) and
  Windows-arm64.

## [0.9.3] - 2026-06-01

Safe Rust by default; flex skin; logos skin; tagged tokens.

## [0.9.2] - 2026-06-01

Bison skin gains `%union` and `YYDEBUG`.

## [0.9.1] - 2026-06-01

Per-token DFA, C-side SIMD, bison skin, flag redesign
(`--enable=` / `--disable=` / `--target=`).

## [0.8.9] - 2026-06-01

Multiversion-at-tokenize SIMD architecture.

## [0.8.8] - 2026-05-31

CI quality bump; all platforms green.

## [0.8.7] - 2026-05-31

Final pre-existing cross-platform issue resolved.

## [0.8.6] - 2026-05-30

All Windows test failures resolved.

## [0.8.5] - 2026-05-30

Windows LSP support; POSIX-test refactor.

## [0.8.4] - 2026-05-30

Opt-in `--rustlex-memchr` / `--rustlex-simd` flags.

## [0.8.3] - 2026-05-30

Per-state SIMD-friendly fast-path scans.

## [0.8.2] - 2026-05-30

Lexer emit rewrite; +18% tokenize on JSON.

## [0.8.1] - 2026-05-30

Documentation pass; Rust runtime perf tuning.

## [0.8.0] - 2026-05-29

### Added
- Rust output target. `--target=rust` emits a self-contained `.rs`
  parser alongside the existing C output. `-X --target=rust` does the
  same for `.lex` lexers. Both are additive; the C path is unchanged.
  See [`docs/RUST_OUTPUT.md`](docs/RUST_OUTPUT.md).

## [0.7.0] - 2026-05-29

ABI cleanup: snapshot magic value; simplified `parse_token` signature.

## [0.6.4] - 2026-05-29

`parse_token()` honours `%first_token` (Lime-Letter-25).

## [0.6.3] - 2026-05-27

Six deferred items closed.

## [0.6.2] - 2026-05-27

Hotfix: `struct rule` visibility in `jit_codegen.c`.

## [0.6.1] - 2026-05-27

PG-team queued-fixes batch.

## [0.6.0] - 2026-05-26

### Changed
- Generator output shape change.

## [0.5.5] - 2026-05-26

ROADMAP-1 complete (in-process LALR rebuild).

## [0.5.4] - 2026-05-26

In-process LALR rebuild API (ROADMAP-1 phases 2+3).

## [0.5.3] - 2026-05-26

`LimeCompilerContext` (ROADMAP-1 phase 1 of 5).

## [0.5.2] - 2026-05-26

Composition wired; Letter-23 fix.

## [0.5.1] - 2026-05-26

`parse_engine` test coverage; man-page sync.

## [0.5.0] - 2026-05-26

### Added
- Linter (`lime -L`). See [`docs/LINT.md`](docs/LINT.md).
- LSP (`lime-lsp`). See [`docs/LSP.md`](docs/LSP.md).
- Formatter completeness.
- CI matrix expansion.

## [0.4.4] - 2026-05-25

`%embed lang` directive; closes the v0.4 dialect-authoring arc.
See [`docs/EMBED.md`](docs/EMBED.md).

## [0.4.3] - 2026-05-25

`lime --diff-conflicts`. See [`docs/DIFF_CONFLICTS.md`](docs/DIFF_CONFLICTS.md).

## [0.4.2] - 2026-05-25

Emergency formatter precedence fix.

## [0.4.1] - 2026-05-25

`%extends` with diamond inheritance.
See [`docs/EXTENDS.md`](docs/EXTENDS.md).

## [0.4.0] - 2026-05-25

`%dialect` Model A directive; first of the v0.4 dialect-authoring arc.
See [`docs/DIALECT.md`](docs/DIALECT.md).

## [0.3.5] - 2026-05-25

Per-token-group / per-type-group comment preservation.

## [0.3.4] - 2026-05-25

### Added
- Generalized-LR parser support.
  See [`docs/GLR.md`](docs/GLR.md).

## [0.3.3] - 2026-05-25

Letter-20 formatter fix; cross-platform stabilization.

## [0.3.2] - 2026-05-25

Formatter comment + indent preservation; ASan-clean.

## [0.3.1] - 2026-05-25

Non-destructive `lime -F` formatter (Letter 18).

## [0.3.0] - 2026-05-24

### Added
- Multi-grammar parsing via runtime context-switch trigger registry.
  See [`docs/CONTEXT_SWITCH.md`](docs/CONTEXT_SWITCH.md).

## [0.2.7] - 2026-05-24

AOT explicit-ERROR action emission (Letter 16).

## [0.2.6] - 2026-05-24

AOT codegen state-default fix (Letter 15).

## [0.2.5] - 2026-05-23

Letter-14 fixes; multi-platform expansion.

## [0.2.4] - 2026-05-21

P0-NEW-11; EOF matched-pointer.

## [0.2.3] - 2026-05-17

P0-NEW-13; P0-NEW-12.

## [0.2.2] - 2026-05-16

M3.7; P0-NEW-9.

## [0.2.1] - 2026-05-16

Post-v0.2.0 cleanup.

## [0.2.0] - 2026-05-16

### Added
- Lexer subsystem. `lime -X foo.lex` produces `foo_lex.c` and
  `foo_lex.h`; the generated pair compiles and links with no Lime
  runtime dependency. Push-driven, reentrant, zero-globals; POSIX
  extended-regex subset. See [`docs/LEXER_DESIGN.md`](docs/LEXER_DESIGN.md).
- Bison-compat parser surface (M1-M5).

## Earlier releases

For releases prior to v0.2.0, consult the git history:

```sh
git log --oneline --tags --decorate
```
