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

- **`%yystype_header "NAME"` directive / `--type-header` flag.**  Emit
  a standalone, makeheaders-free header containing the token
  `#define`s plus the `YYSTYPE` typedef (from `%union` or
  `%token_type`) so a separately-generated `lime -X` lexer can
  `#include` it instead of relying on a hand-written `_yytype.h`
  bridge.  The typedef is guarded by `YYSTYPE_IS_DECLARED`, so it
  composes with the parser `.c` without redefinition.  The directive
  chooses the header basename; the flag uses the default
  `<stem>_yytype.h`.  `lime -F` round-trips the directive.

## [1.4.0] -- 2026-06-07

Feature release on `main` (the v1.3.x LTS line is unaffected and
continues to receive bug-fix/security backports through June 2028).
Additive only -- no existing public symbol changed; the prior Rust
output and tuple-based lalrpop `parse()` path are byte-for-byte
unchanged.

### Added

- **Rust API-compatibility skins: `nom`, `pest`, `chumsky`.**
  `--target=rust:nom`, `--target=rust:pest`, `--target=rust:chumsky`
  now emit a self-contained wrapper next to the standard parser
  (`<stem>_nom.rs` etc.) presenting that crate's familiar surface
  (`nom`-style `IResult`, `pest`-style `Pairs`, `chumsky`-style
  `Result<_, Vec<Simple>>`).  No external crate dependency.  All
  five reserved Rust skins (lalrpop, nom, pest, chumsky -- plus the
  logos lexer skin) now ship; previously these three parsed the
  flag and rejected it as "reserved."
- **lalrpop skin: strongly-typed `Token` struct.**  The skin emits
  `Token { start: usize, code: u16, end: usize, value: Value }` with
  bidirectional `From` conversions to/from the
  `(usize, u16, usize, Value)` quadruple `parse()` consumes.  Build
  a typed token stream and `.map(Into::into)` it into `parse()`.
  The tuple form keeps working unchanged.
- **Generated parser: `pub fn current_state(&self) -> u16`.**  A
  public accessor for the LALR(1) state at the top of the parse
  stack, part of the documented introspection surface.

### Changed

- **lalrpop skin: `ParseError.expected` is now populated.**  Was
  always empty in v1.3.0.  `UnrecognizedToken` / `UnrecognizedEof`
  now carry the names of the tokens that would have been legal in
  the current state, computed via `current_state()` +
  `expected_tokens_in_state()` + `token_name()` (emitted when
  token-names are enabled, the default).
- **Internal: post-parse setup consolidated.**  Split
  `lime_post_parse_setup` into `lime_index_symbols` +
  `lime_number_rules`, giving a single source of truth for the
  `(rp->code || rp->rust_code)` rule-numbering predicate that two
  earlier customer bugs had to patch in three duplicated copies.
  No behavioural change.

### Fixed

- **`lime_rust_ident_used()` false positives.**  The alias-usage
  scan that decides whether a `%rust_action` references its named
  RHS symbols was a word-boundary `strstr`; it now walks Rust lexer
  state, correctly ignoring matches inside string/char/comment and
  raw-string contexts (and disambiguating lifetimes from char
  literals).
- **Grammar-fragment diagnostic.**  A `%token`-only fragment
  (`Symbol_count() > 1` but no rules) now reports that it looks
  like an `%include`-able fragment rather than the bare "Empty
  grammar."; genuinely empty input still reports "Empty grammar."

### Tests

- `test_emit_rust_skins_v14` (5 cargo sub-tests): nom/pest/chumsky
  each parse `1+2+3` to `6`.
- `test_lalrpop_enrich` (4 cargo sub-tests): parse via tuple, parse
  via `Token` struct, `Token`<->tuple round-trip, and
  `expected`-populated-on-error.
- `test_rust_ident_scan`, `test_fragment_error`.
- 137/137 stock + UBSan + TSan; standalone single-TU build links.

## [1.3.1] -- 2026-06-07

First LTS patch release.  Pure bug fix; no API changes.

### Fixed

- `%rust_action` body silently dropped at runtime when applied to
  the head alternative of an alt-group (`e ::= a | b. %rust_action {...}`).
  The `WAITING_FOR_DECL_OR_RULE` state machine cleared `alt_group_head`
  pre-emptively when it saw the `%` token, so `propagate_alt_group_attach`
  early-returned and the head alternative kept the default `lhs = rhs0`
  body while the tail alternative got the user's body.  Drop the
  over-eager state-machine clear; rule-start cleanup at line 7344 is
  sufficient.
- `%rust_action`-only rules (no inline `{...}` C body) misclassified
  as noCode passthrough, triggering SHIFTREDUCE table collapse that
  skipped the user's reduce dispatch even on single-rule cases.  Set
  `noCode = 0` when parsing the body and update the `iRule` numbering
  predicate (3 sites, kept in sync) to recognise `rust_code` as
  reduce code.
- `test_lint_fast` flaked on parallel CI runs at the 1.10x ratio
  margin.  Bumped to 2.0x; still catches a 2x regression on the 16-
  rule test grammar, which is what the assertion is for.

### Tests

- New runtime regression test `test_rust_action_dispatch` (cargo-
  driven) covering both single-rule and alt-group cases.  Three sub-
  tests asserting `final_value` is the user's intended computation.
  SKIPs cleanly when cargo not on PATH.

### Documented

- `docs/RUST_OUTPUT.md` gains an "API stability" section enumerating
  the 6 stable Rust-target symbols (`<Name>Parser`, `new`, `push`,
  `finalize`, `ParseError`, `Value`) and explicitly disclaiming
  `YY_*`, `yy_*`, `ReduceCtx`, `UserArg`, and the per-rule reduce
  dispatch tables as internal -- subject to change in any release.
- `docs/API.md` pins the gcc-style diagnostic format used by
  `lime_lint_grammar_in_process` / `_fast_in_process` and marks
  `lime_post_parse_setup` as internal-only.
- `docs/SKINS.md` gains a real `--target=rust:lalrpop` section with
  the `(usize, u16, usize, Value)` quadruple convention, a worked
  example, and the v1.4.0 deferred items (Token-enum auto-emit,
  `expected: Vec<String>` enrichment).
- `README.md` gains a top-of-file LTS callout pointing at
  `docs/SUPPORT.md`.
- `docs/ROADMAP.md` records v1.3.0 LTS landed and v1.3.1 patch.

## [1.3.0] -- 2026-06-06 (LTS)

First Long-Term Support release.  Backports through June 2028.  See
`docs/SUPPORT.md` for the full policy.

### Added

- **lalrpop-API-compatibility Rust skin** (`--target=rust:lalrpop`).
  Emits a sibling `<stem>_lalrpop.rs` that wraps the standard
  generated parser in a shape mimicking lalrpop's public API:
  `<PascalName>Parser::new().parse(tokens) -> Result<Value, ParseError>`
  with `ParseError<L,T,E>` mirroring `lalrpop_util::ParseError`'s
  variants (`InvalidToken`, `UnrecognizedEof`, `UnrecognizedToken`,
  `ExtraToken`, `User`).  See `docs/SKINS.md`.
- **`docs/SUPPORT.md`** -- formal LTS support policy with backport
  criteria (demonstrated bug, safe, small, regression test).

### Fixed

- Customer-reported `%rust_action` RHS-alias underscore-prefixing
  (commit `c0d68d0`): the alias-usage scanner only inspected the
  inline C body and missed identifiers used in the `%rust_action`
  body, so `expr(L) ::= ... %rust_action { R = L; }` got `_L`
  emitted in the binding and rustc rejected the .rs with E0425.
  New helper `lime_rust_ident_used()` scans both bodies.

### Tests

- `test_emit_rust_skin_lalrpop` -- shape + rustc compile-check.
- `test_rust_action_alias_usage` -- rustc compiles output without
  E0425 on `%rust_action`-only rules.

## [1.2.0] -- 2026-06-06

Maintenance + polish release.  Five items.

### Added

- **Parse-only fast lint mode** (`lime_lint_grammar_fast_in_process`).
  Skips LALR(1) construction (FindStates / FindFollowSets /
  FindActions) and runs only ParseText + lint_grammar.  Drops LSP
  diagnostic latency from ~2s to ~150ms on PG's `gram.lime`.
  `lime-lsp` `didChange` path uses fast-lint by default;
  `LIME_LSP_FULL_LINT=1` forces the full LALR pass.
- **`lime_post_parse_setup` helper.**  Hoists what was duplicated
  inline in three places (later v1.3.1 finds main + dc_child still
  inlined the iRule loop and patches the predicate in-place rather
  than completing the routing -- queued for v1.4.0).
- **LLDB pretty-printer smoke test.**  Mirrors v1.1.0's
  gdb_pretty_printers test.  Renamed `scripts/lime-lldb.py` ->
  `scripts/lime_lldb.py` because Python's `command script import`
  rejects dashes in module names.
- **Win32 thread shim for async diagnostics.**  Extends
  `lime_threads.h` with `pthread_cond_*` shimmed over Win32
  `CONDITION_VARIABLE`.  `lsp_diagnostics_async.c` no longer needs
  the `#if !defined(_WIN32)` Windows-stub branch.

### Fixed

- Formatter idempotence: header-comment scanner greedily merged
  two consecutive comment blocks separated by a blank line.  Stop
  at first blank line after seeing one block.  Idempotent grammars:
  52/64 -> 57/64.

## [1.1.0] -- 2026-06-05

### Added

- **Rust-target syntax-error introspection** (closes Lime-Letter-27).
  New API: `pub static YY_TOKEN_NAMES`, `pub fn token_name(code) ->
  Option<&'static str>`, `pub fn yy_find_shift_action(state,
  lookahead) -> u16`, `pub fn expected_tokens_in_state(state) ->
  Vec<u16>`.  Default ON via `--enable=token-names`; opt-out
  `--disable=token-names`.

### Documented

- `docs/RUST_OUTPUT.md` gains 3 new sections (token-name introspection
  example, `include!`-vs-crate-root inner-attr strip, `*mut State`
  workaround for nested reduction calls).

## [1.0.0] -- 2026-06-04

API stability commitment for v1.x.

### Added

- **Background diagnostics** for `lime-lsp` (`src/lsp/lsp_diagnostics_async.{h,c}`).
  ~5ms `didOpen` handler-return vs ~2s sync.  Per-URI generation
  counter; in-flight workers drop stale results when a newer
  request supersedes.  Out-stream mutex serialises LSP framing.
  POSIX-only initially (Windows added in v1.2.0).
- **"Did you mean" linter suggestions** on E001 / E002 / M003
  (case-insensitive Levenshtein distance).
- **GDB + LLDB pretty-printers** (`scripts/lime-{gdb,lldb}.py` +
  `scripts/test-lime-gdb.sh`).  Commands: `lime-snapshot`,
  `lime-stack`, `lime-actions`.
- **`CHANGELOG.md`** (this file -- though it took until v1.3.1 to
  actually backfill it; the announcement was incomplete on
  publication and that's a maintainer hygiene failure called out
  in v1.3.0's audit).
- **`docs/DEBUGGING.md`**.

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
