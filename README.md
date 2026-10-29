# Lime Parser Generator

[![CI](https://github.com/gburd/lime/actions/workflows/ci.yml/badge.svg)](https://github.com/gburd/lime/actions)
[![Release](https://img.shields.io/github/v/release/gburd/lime)](https://github.com/gburd/lime/releases)

> **Canonical source:** <https://github.com/gburd/lime> — clone from here
> (`git clone https://github.com/gburd/lime`).  A mirror is kept at
> <https://codeberg.org/gregburd/lime>, but GitHub is authoritative and is
> where CI runs and releases are cut.  Automated tooling should use the
> GitHub URL: Codeberg serves anti-scraper decoy content to automated
> clients on some paths, so a bot fetching it may receive fabricated data.

> **Support policy:** this is a fast-moving, single-maintainer project;
> the current release line (v1.10.x) gets the fixes.  Bug-fix and security
> backports are best-effort on the most recent release branch, not a
> multi-year LTS guarantee.  See [`docs/SUPPORT.md`](docs/SUPPORT.md) for
> the backport policy and its limits.

## Overview

Lime is a runtime-extensible LALR(1) parser generator.  It reads a
context-free grammar and emits a C parser, like Yacc or Bison — but
unlike those tools, the generated parser can load and unload grammar
extensions at runtime without recompilation.

The generator itself compiles from a single C file with no dependencies.
Generated parsers optionally use SIMD-accelerated tokenization (AVX2/NEON).

## Motivation

Database engines, language servers, and extensible query processors need
parsers that can evolve without downtime.  Adding a new operator, a custom
type, or a dialect-specific clause traditionally means editing the grammar,
regenerating the parser, and restarting the process.

Lime eliminates that cycle.  Grammar extensions are shared libraries loaded
at runtime.  Conflict detection and disambiguation happen live.  The base
parser runs at full speed when no extensions are loaded — the extension
machinery has zero overhead until activated.

This design is driven by a single observation: runtime grammar
extension has been explored mostly in more permissive engines (Earley,
GLR, PEG) or via offline composition, not in deterministic in-process
LALR(1) parsers.  Lime targets that gap.

## Why Lime over Yacc/Bison?

- **Runtime extensibility** — Load and unload grammar extensions
  dynamically via a C API, with conflict detection and resolution callbacks.
  Runtime *load and unload* of extensions against a deterministic LALR(1)
  table, in-process, with copy-on-write snapshots, is Lime's differentiated
  niche; other systems reach runtime/modular composition through more
  permissive algorithms or offline steps.  See
  [docs/RELATED_WORK.md](docs/RELATED_WORK.md).
  The base reduce actions and extension reduce callbacks run at parse time,
  unified by the host-reduce router; see
  [docs/REDUCE_DISPATCH.md](docs/REDUCE_DISPATCH.md).
- **Performance** — SIMD-accelerated tokenization delivers a
  measured 1.5-2x speedup on the full tokenizer pipeline (the
  underlying character-classify primitive runs 4-8x on AVX2; the
  loop overhead and emit path absorb the rest).  These figures are
  from small friendly grammars; see
  [docs/PERFORMANCE.md](docs/PERFORMANCE.md),
  [docs/BENCHMARKS_VS_BISON.md](docs/BENCHMARKS_VS_BISON.md), and
  `bench/bench_simd_classify` for the baselines and their limits.
- **Thread-safe by design** — Copy-on-write snapshots with atomic reference
  counting allow concurrent parsing with zero shared mutable state.
- **Modern memory safety** — `%destructor` directives prevent semantic
  value leaks during error recovery.  All allocations tracked; zero leaks
  under Valgrind and ASan.
- **Public Domain** — No GPL, no attribution clauses.  Use it anywhere.
- **Single-file build** — The generator compiles from one C file.  Embed it
  directly in your build system.

For a detailed comparison with Yacc, Bison, ANTLR, and Menhir, see
**[docs/COMPARISON.md](docs/COMPARISON.md)**.  Migration guides:
**[from Bison](docs/MIGRATION_FROM_BISON.md)** ·
**[from Yacc](docs/MIGRATION_FROM_YACC.md)** ·
**[from Flex](docs/MIGRATION_FROM_FLEX.md)**.

## Tooling

Lime ships a complete grammar-author toolchain alongside the
generator itself.  All commands work on the same `.lime` files.

| Tool | What it does |
|------|--------------|
| `lime -L`            | Grammar-hygiene linter (E001-E005, W001-W009, S001-S002), `gcc` / `json` / human formats.  See [docs/LINT.md](docs/LINT.md). |
| `lime -F`            | Non-destructive formatter that preserves comments and indent. |
| `lime --diff-conflicts` | Symbolic LALR-conflict diff between two grammars (`base.lime ext.lime`).  Use in CI to catch dialect-overlay regressions.  See [docs/DIFF_CONFLICTS.md](docs/DIFF_CONFLICTS.md). |
| `lime-lsp`           | Language Server Protocol implementation for editors.  Diagnostics from the linter, hover, goto-definition.  In-process diagnostic refresh path runs ~10% / 200 ms faster than the subprocess fallback on large grammars (since v0.10.0).  See [docs/LSP.md](docs/LSP.md) and [editors/lime-lsp-config.md](editors/lime-lsp-config.md). |
| `--target=rust`      | Emit Rust output (since v0.8.0).  See above. |
| `--target=c:bison`   | Emit a `bison`-shaped C skin (since v0.9.1).  Minimises call-site churn when porting a `bison` consumer (`yyparse`, `YYSTYPE`, `%union`, `YYDEBUG`); it is a skin, not a drop-in for `bison -d` output.  See [docs/BISON_COMPAT.md](docs/BISON_COMPAT.md) for the supported/emulated/unsupported matrix. |
| `--target=c:flex`    | Emit a `flex`-shaped C scanner skin (since v0.9.3).  Exposes `yytext` / `yyleng` / `yylineno` / `yy_scan_string` for porting; it is a skin, not a drop-in for `lex.yy.c`.  See [docs/FLEX_COMPAT.md](docs/FLEX_COMPAT.md). |
| `--target=rust:logos` | Emit a `logos`-compatible Rust scanner (since v0.9.3). |

Grammar-composition directives:

| Directive | Purpose |
|-----------|---------|
| `%dialect NAME { ... }` | Generator-time conditional rule inclusion (since v0.4.0).  See [docs/DIALECT.md](docs/DIALECT.md). |
| `%extends "base.lime"` + `%override` / `%remove` / `%override_type` | File-level grammar inheritance with diamond resolution (since v0.4.1).  See [docs/EXTENDS.md](docs/EXTENDS.md). |
| `%embed lang TRIGGER 'lex' ENTRY_TOKEN TOKEN.` | Sugar over the context-switch trigger registry (since v0.4.4).  See [docs/EMBED.md](docs/EMBED.md). |
| `%action_c` / `%action_rust` | Carry both a C and a Rust action body for the same production (since v0.12.0).  Migrate production-by-production with a green build at every step.  See [docs/RUST_OUTPUT.md](docs/RUST_OUTPUT.md). |

Runtime APIs that complement the generator:

| API | What it does |
|-----|--------------|
| `parse_begin_borrowed(snap)` | Skip the snapshot atomic refcount.  3.4x throughput uplift at 8 threads on `bench/bench_parse_fanout` (since v0.10.0).  See [docs/API.md](docs/API.md#parse_begin_borrowed). |
| `lime_compile_grammar_in_process(text, len, &snap, &err)` | In-process LALR(1) construction.  No `fork` / `exec` / temp file (since v0.5.4).  Link via `pkg-config --libs lime-compiler`. |
| `lime_lint_grammar_in_process(text, len, &diags)` | In-process linter (since v0.10.0).  Backs `lime-lsp`'s diagnostic refresh path. |
| Generalized-LR parsing | `glr_parse(...)` for grammars where LALR(1) conflicts can't be resolved by precedence (since v0.3.4).  See [docs/GLR.md](docs/GLR.md). |
| Multi-grammar parsing | Runtime context-switch trigger registry for SQL-with-embedded-JSON / XML / JSONPath (since v0.3.0).  See [docs/CONTEXT_SWITCH.md](docs/CONTEXT_SWITCH.md). |

## Lexer subsystem

Since v0.2.0 Lime also generates lexers.  `lime -X foo.lex` produces
`foo_lex.c` and `foo_lex.h`; the generated pair compiles and links with
no Lime runtime dependency, the same way the parser side does.  The
emit callback signature matches `ParseLoc`, so the entire driver loop
for a paired lexer + parser collapses to one `LexFeedBytes` call.

The lexer is push-driven, reentrant, and zero-globals by construction
-- no `yytext` / `yyleng` / `yylineno` side channels.  Action bodies
see typed locals (`matched`, `matched_len`, `loc`, `lex`, `extra`,
`state`) and a small set of macros (`LEX_EMIT`, `LEX_TRANSITION`,
`LEX_PUSHBACK`, `LEX_TERMINATE`, `LEX_ERROR_AT`).  Exclusive states
carry typed local data; an include-buffer stack (`LexInclude`) handles
ecpg-style splice grammars without `yywrap`.  POSIX-extended regex
subset; no PCRE assertions, no captures, no REJECT, no `yymore` --
the PG flex audit (six scanners, ~5,300 lines) found zero uses of any
of those.

```sh
lime -X -d. parser.lex          # produces parser_lex.c + parser_lex.h
cc -c parser_lex.c              # no Lime runtime to link against
```

Reference docs:

- **[docs/LEXER_DESIGN.md](docs/LEXER_DESIGN.md)** -- design spec,
  API surface, worked examples.
- **[docs/MIGRATION_FROM_FLEX.md](docs/MIGRATION_FROM_FLEX.md)** --
  porting flex `.l` scanners to Lime `.lex`, with directive
  mapping, action-body translation, and a common-gotchas list.
- **`man/lime_lex(5)`** -- `.lex` file format and runtime API
  reference.
- **[examples/pg_bootscanner/](examples/pg_bootscanner/)** -- PG's
  `bootscanner.l` ported end-to-end as a worked example.

## Rust output

Since v0.8.0 Lime can emit Rust as well as C.  The `--target=rust`
flag adds a self-contained `.rs` parser alongside the existing
`.c` / `.h` output.  The `.lex` lexer side gets a Rust mirror with
`-X --target=rust`.  Both outputs are additive -- the C path is
unchanged.

```sh
lime --target=rust grammar.lime                       # writes grammar.rs
lime --target=rust --enable=crate grammar.lime        # also a Cargo crate skeleton
lime --target=rust --enable=nostd grammar.lime        # use alloc::vec::Vec; no std
lime --target=rust --enable=crate,nostd grammar.lime  # both at once
lime -X --target=rust tokenizer.lex                   # writes tokenizer_lex.rs
```

Short-form short flag also works: `lime -t rust grammar.lime` or
`-trust grammar.lime` (lime's traditional glued-short convention).
Features recognised by `--enable=` / `--disable=`: `simd` (default
ON), `memchr`, `per-token-dfa`, `vectorize` (default ON, C-side
SIMD/intrinsic emit), `crate`, `nostd`.  See
[docs/RUST_OUTPUT.md](docs/RUST_OUTPUT.md) for full semantics.

The pre-v0.8.6 flag spellings (`--rust`, `--rustcrate`,
`--rustnostd`, `--rustlex`, `--rustlex-simd`, `--rustlex-memchr`,
`--per-token-dfa`) continue to work as deprecation aliases that
print a one-line stderr warning suggesting the new form.

The Rust output is a single self-contained `.rs` module.  No Lime
runtime dependency: the file compiles with `rustc --crate-type lib`
in isolation.  Action tables, per-rule reduce callbacks, and the
LALR loop are all emitted; `%fallback`, `%first_token`, `%left` /
`%right` / `%nonassoc`, `%name`, and `%start_symbol` work
transparently.  Rust-specific directives:

- `%rust_action { ... }` -- per-rule Rust body override (parallel
  to the C action body; lets a grammar declare bodies in either
  language).
- `%rust_extra_argument {T}` -- threads a user value through every
  reduce callback (Rust analog of `%extra_argument`).
- `%rust_value_type {T}` -- override the semantic value type
  (default `i64`).
- `%rust_syntax_error / %rust_parse_accept / %rust_parse_failure /
  %rust_stack_overflow { ... }` -- Rust-side parse hooks.

Performance on a 226 KB JSON fixture (i9-12900H, --release fat
LTO):  Lime's `--target=rust` parse hits ~327 MB/s, ~1.71x slower
than lalrpop and ~1.19x faster than serde_json's AST build.  The
`-X --target=rust` lexer hits ~295 MB/s, ~1.63x slower than logos
(progressively closed from 1.88x in v0.8.1 via the v0.8.2 flat-
layout rewrite and v0.8.3's per-state SIMD-friendly fast-path
scans on self-loop-dominant DFA states).  Lime's table-driven
approach is the cost of supporting runtime composition (lalrpop
has no equivalent); it beats pest (PEG) and serde_json (AST) on
the same workload.

See **[docs/RUST_OUTPUT.md](docs/RUST_OUTPUT.md)** for the full
feature surface, **[docs/RUST_BENCHMARK.md](docs/RUST_BENCHMARK.md)**
for the head-to-head numbers vs logos / lalrpop / nom / pest /
serde_json, and **[examples/rust_calc/](examples/rust_calc/)** for
a working Cargo example.

## Quick Start

```bash
# Development environment (optional)
nix develop

# Single-file build (parser generator only, no .lex compiler)
cc -o lime lime.c

# Full build (parser generator with .lex compiler, runtime library,
# extensions, tests, benchmarks)
meson setup builddir
ninja -C builddir
ninja -C builddir test

# OR -- meson-free build via the hand-written Makefile
make            # builds bin/lime, lib/liblime_parser.a, tests
make test       # runs the runtime-engine tests
make LEX=1      # bundle the .lex compiler into the lime binary
make install PREFIX=/usr/local
```

The single-file build accepts every `.y` grammar.  The `-X` flag for
the `.lex` lexer compiler requires linking the lex sources -- if
invoked without them, lime prints a clear error directing you to
either `meson setup` or
`cc -o lime lime.c src/lex/*.c -Iinclude -Isrc/lex -DLIME_HAS_LEX_COMPILER`.

## Project Layout

The project root contains the parser generator itself.  The `src/`
directory contains the runtime push-parser, snapshot system, and
extension framework, all built into a single library.

```
lime/
├── lime.c                  # Parser generator (single-file)
├── limpar.c                # Parser driver template (filled by lime)
├── meson.build             # Build configuration
├── Makefile                # Convenience wrapper for meson
├── flake.nix               # Nix development environment
│
├── src/                    # Runtime + extension framework library
│   ├── snapshot.{c,h}      #   Copy-on-write grammar snapshots
│   ├── snapshot_build.c    #   Build a runtime snapshot from
│   │                       #   generated parser tables
│   ├── parse_engine.c      #   LALR(1) push-parser interpreter
│   ├── extension.{c,h}     #   Extension registry
│   ├── conflict.{c,h}      #   Conflict detection
│   ├── tokenize_simd.{c,h} #   SIMD tokenization (AVX2/NEON)
│   └── ...                 #   (35+ source files)
│
├── include/                # Public API headers (snapshot_build.h,
│                           # tokenize_simd.h, parser.h, ...)
├── tests/                  # Test suite (60+ files)
├── bench/                  # Benchmarks (vs bison, overhead, PG rebuild)
├── examples/               # Example grammars and extensions
├── contrib/                # SQL dialect extensions (Oracle, MySQL, ...)
├── docs/                   # Reference documentation
├── man/                    # Man pages: lime(1), lime_grammar(5), lime_lex(5)
├── scripts/                # Validation and coverage scripts
└── tools/                  # Composition and management utilities
```

## Documentation

See **[docs/README.md](docs/README.md)** for the full index, and
**[CHANGELOG.md](CHANGELOG.md)** for per-release detail.  Key documents:

| Document | Description |
|----------|-------------|
| [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md) | Build Lime, write your first grammar |
| [docs/CONCEPTS.md](docs/CONCEPTS.md) | Snapshots, extensions, conflicts |
| [docs/INTEGRATION.md](docs/INTEGRATION.md) | Embed Lime in your project |
| [docs/EXAMPLES.md](docs/EXAMPLES.md) | All examples explained |
| [docs/API.md](docs/API.md) | C API reference |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | System design |
| [docs/DIAGNOSTICS.md](docs/DIAGNOSTICS.md) | Parser error messages and recovery |
| [docs/EXTENSIONS.md](docs/EXTENSIONS.md) | Writing runtime extensions |
| [docs/CONTEXT_SWITCH.md](docs/CONTEXT_SWITCH.md) | Multi-grammar parsing (e.g. SQL with embedded JSON / XML / JSONPath) |
| [docs/ALGORITHM.md](docs/ALGORITHM.md) | LALR(1) theory and implementation |
| [docs/PERFORMANCE.md](docs/PERFORMANCE.md) | Performance tuning |
| [docs/BENCHMARKS_VS_BISON.md](docs/BENCHMARKS_VS_BISON.md) | Head-to-head comparison with Bison |
| [docs/BENCHMARK_RESULTS.md](docs/BENCHMARK_RESULTS.md) | Multi-host benchmark numbers (nuc + M3 Pro) with proper statistics |
| [docs/COMPARISON.md](docs/COMPARISON.md) | Comparison with Yacc, Bison, ANTLR |

### Doxygen

```bash
cd docs && make doxygen
open api/html/index.html
```

## Examples

Every example lives under [`examples/`](examples/) and builds standalone
(its own `Makefile` or `meson.build`).  See [docs/EXAMPLES.md](docs/EXAMPLES.md)
for a longer walkthrough of each.  Grouped quick reference:

### Tutorials and plugin demos

| Example | What it shows |
|---------|---------------|
| [`examples/calc/`](examples/calc/) | A four-operation calculator extended at runtime with shared-library plugins.  The canonical "hello world" for Lime's extension framework. |
| [`examples/plugin_template/`](examples/plugin_template/) | Minimal skeleton for packaging a Lime-generated parser as a runtime-loadable plugin (`sql_plugin.c`) and a host application that loads it via `ParserManager` (`plugin_host.c`). |
| [`examples/jsonb_extension.c`](examples/jsonb_extension.c) | Single-file walkthrough of `MOD_ADD_TOKEN` + `MOD_ADD_RULE` + `MOD_MODIFY_PRECEDENCE` adding PostgreSQL-style JSONB operators (`->`, `->>`, `@>`, `<@`, `?`) to an existing SQL parser. |
| [`examples/llm_oracle/`](examples/llm_oracle/) | Custom disambiguation strategy that consults an LLM when Lime's built-in strategies decline to resolve a conflict.  Illustrates the disambiguation callback API. |
| [`examples/multi_grammar_sql_json/`](examples/multi_grammar_sql_json/) | SQL host parser with an embedded JSON sub-grammar.  Shows the runtime trigger-registration API (`context_switch_register_trigger`) that lets a single parser handle multiple languages -- the canonical use case is PostgreSQL-style `SELECT json '{"a":1}' FROM t`.  See [docs/CONTEXT_SWITCH.md](docs/CONTEXT_SWITCH.md). |

### Non-SQL query languages

| Example | What it shows |
|---------|---------------|
| [`examples/datalog/`](examples/datalog/) | Datalog / EDN parser with a hand-rolled tokenizer driving Lime's push parser.  Demonstrates the "bring your own lexer" integration pattern. |
| [`examples/json/`](examples/json/) | Compact JSON parser that produces a tagged-union `JsonValue` AST.  Demonstrates `%token_type`, `%type`, `%extra_argument`, and the typed-alias action style.  Companion to the simdjson throughput benchmark below. |
| [`examples/jsonpath/`](examples/jsonpath/) | JSONPath parser converted from PostgreSQL's `jsonpath_gram.y` / `jsonpath_scan.l`.  Self-contained; does not link against PostgreSQL. |
| [`examples/xpath/`](examples/xpath/), [`examples/xquery/`](examples/xquery/) | XPath 1.0 and XQuery parsers, each with a standalone driver that reads expressions from stdin or argv and prints the AST. |
| [`examples/mongodb/`](examples/mongodb/) | MongoDB query-document parser for expressions like `{ "age": { "$gt": 25 } }`. |
| [`examples/cobol/`](examples/cobol/) | Substantial COBOL parser covering all four divisions (IDENTIFICATION, ENVIRONMENT, DATA, PROCEDURE) and ~30 verbs.  Demonstrates `%symbol_prefix CB_INTERNAL_` for namespace isolation. |

### PostgreSQL grammar conversions

These demonstrate Lime's ability to handle real production grammars by
porting PostgreSQL subsystem parsers.  They are **demos of Lime, not
dependencies on PostgreSQL** -- each is a self-contained standalone
parser.

| Example | What it shows |
|---------|---------------|
| [`examples/pg/`](examples/pg/) | Full PostgreSQL SQL grammar from `gram.y` (~21,000 lines in upstream) as a single Lime grammar. |
| [`examples/pg_modular/`](examples/pg_modular/) | The same PostgreSQL grammar decomposed into 35+ literate modules under `base/`, `ddl/`, `dml/`, `expr/`, `from_clause/`, `select_targets/`, `functions/`, `window/`, `cte/`, `transactions/`, `security/`, `utility/`.  Exercises Lime's `%module_name` / `%require` / `%import` composition directives. |
| [`examples/bootstrap/`](examples/bootstrap/) | PostgreSQL BKI (bootstrap) parser from `bootparse.y` + `bootscanner.l` -- the small grammar used during `initdb`. |
| [`examples/pgbench/`](examples/pgbench/) | `pgbench` expression-language parser. |
| [`examples/replication/`](examples/replication/) | Streaming-replication protocol parser from `repl_gram.y` + `repl_scanner.l` (`IDENTIFY_SYSTEM`, `START_REPLICATION`, etc.). |
| [`examples/syncrep/`](examples/syncrep/) | Synchronous-replication config-string parser (`synchronous_standby_names`). |
| [`examples/isolation/`](examples/isolation/) | Parser for the `.spec` files driving PostgreSQL's isolation test framework. |
| [`examples/lime_postgres/`](examples/lime_postgres/) | Integration notes specifically for embedding Lime inside PostgreSQL, including <a href="examples/lime_postgres/EXTENSION_AUTHORING.md"><code>EXTENSION_AUTHORING.md</code></a>, <a href="examples/lime_postgres/DIALECT_SUPPORT.md"><code>DIALECT_SUPPORT.md</code></a>, and <a href="examples/lime_postgres/EMBEDDED_LANGUAGES.md"><code>EMBEDDED_LANGUAGES.md</code></a>.  Documentation, not shipped code. |

### Literate grammar format

| Example | What it shows |
|---------|---------------|
| [`examples/literate/`](examples/literate/) | Two-file literate grammar (`tokens.md` + `grammar.md`) showing the `%module_name` / `%require` system driving a calculator.  Companion reading: [docs/LITERATE_FORMAT.md](docs/LITERATE_FORMAT.md) and <a href="docs/MODULE_FORMAT.md">docs/MODULE_FORMAT.md</a>. |

## Usage

Generate a parser from a grammar file:

```bash
./lime grammar.y
```

Key flags: `-d dir` (output directory), `-T template` (custom template),
`-s` (statistics), `-L` (lint), `-F` (format), `-X` (run as `.lex`
compiler), `--target=rust` (Rust output).  Pass any unrecognised flag
(e.g. `lime --help`) to get the full option list, or read `man lime`.
`lime -x` prints the version.

### Extension Development

```c
#include "extension.h"

ExtensionRegistry *reg = global_extension_registry();
ExtensionInfo info = {
    .name = "my-extension",
    .version = "1.0.0",
    .get_modifications = my_callback,
};

ExtensionID id;
register_extension(reg, &info, &id);
load_extension(reg, id, NULL);
```

See **[docs/EXTENSIONS.md](docs/EXTENSIONS.md)** and
`examples/jsonb_extension.c` for working examples.

## Performance

### vs Bison + Flex (lex + parse)

`bench/bench_flex_bison_compare/`: same 515-byte JSON document lexed
and parsed by Bison + flex and by Lime, both checked to accept it
before timing.  Re-measured in v1.11.3 against both flex table modes
(Debian 13, Xeon 8488C, one pinned core, 10 interleaved rounds of
100k documents, median):

| Baseline | bison + flex | lime | lime vs bison |
|----------|-------------:|-----:|--------------:|
| flex default (`-Cem`) | 258.6 ms | 203.7 ms | 1.27x faster |
| flex `-CF` (full tables) | 168.1 ms | 202.0 ms | **0.83x (17% slower)** |

Earlier versions of this README claimed 1.81x; that was measured only
against flex's slowest (default) table mode.  The Lime side also lexes
with a hand-written tokenizer, and neither side builds values, so this
is a small-grammar recognition benchmark, not a claim about real
parsers.  Details and caveats:
[docs/BENCHMARKS_VS_BISON.md](docs/BENCHMARKS_VS_BISON.md).

### Parser-only throughput

Same harness, arithmetic grammar.  Not symmetric: the Lime side is
fed pre-tokenised input while the bison side runs flex, so this
*favours* Lime -- and Lime is still slower on the re-measurement.
Debian 13, Xeon 8488C, release build, one pinned core, 500k parses x 5
trials, three runs:

| Tool | per-parse (min, 3 runs) | vs Bison |
|------|------------------------:|---------:|
| bison + flex | 245-251 ns | 1.00x |
| lime (runtime engine) | 286-287 ns | **0.86x** |

Earlier releases reported lime 1.15x on an i9-12900H; that
measurement has not reproduced here.

### No JIT

Earlier releases shipped an optional LLVM JIT for the shift-action
lookup.  It was removed: on the full PostgreSQL grammar (3,842
states) it was 4-6% slower than the table interpreter on x86_64
(Xeon 8488C) and 7-9% slower on aarch64 (Graviton4), so its 16-34 ms
compile never paid back.  The measurements and method are in
[docs/JIT_ANALYSIS.md](docs/JIT_ANALYSIS.md).

### SIMD tokenizer

`bench/parser_bench`, AVX2 on x86_64 / NEON on aarch64:

| Path | 4 KB classify |
|------|--------------:|
| Scalar | 3171 ns |
| SIMD (best available) | 1784 ns |

End-to-end tokenizer throughput holds at 99-101 MB/s across
4 KB-256 KB inputs (the scalar-path baseline for the same inputs is
not stated here; see
[docs/BENCHMARKS_VS_BISON.md](docs/BENCHMARKS_VS_BISON.md)).

### Extension framework overhead

`bench/extension_overhead` (numbers from `bench/BENCHMARK_RESULTS.md`):

| Scenario | ns/op |
|----------|------:|
| No extensions (fast path) | 119 |
| Token-level conflict detection | 1,548 |
| Priority disambiguation | 739 |
| Full detect+resolve+execute | 1,433 |

The fast-path check is a single `get_loaded_extension_count()` call.
The full pipeline only fires on conflicting tokens, so amortised
per-parse overhead is a small fraction of total parse time.  See
[docs/EXTENSION_PERFORMANCE.md](docs/EXTENSION_PERFORMANCE.md) and
[bench/BENCHMARK_RESULTS.md](bench/BENCHMARK_RESULTS.md).

## Testing

```bash
ninja -C builddir test                # all tests
./scripts/measure_coverage.sh         # coverage report
./scripts/check_coverage.sh           # coverage gate (CI uses this)
./scripts/check_memory.sh             # valgrind
```

The coverage gate enforces a floor on `src/` (the runtime library
that production users link against): line coverage ≥ 78%, branch
coverage ≥ 60%.  (The script quietly enforced only 72/55 from v0.8.7
until the v1.11.3 review restored the documented floor; measured then:
79.4% lines / 60.2% branches.)  `lime.c`, the generator itself, is not
gated -- it measured 73.9% lines / 62.0% branches taken.  Override
locally for ad-hoc checks:

```bash
LIME_COV_MIN_LINES=85 LIME_COV_MIN_BRANCHES=70 \
    ./scripts/check_coverage.sh
```

Floors should ratchet upward as coverage improves.

Sanitizer builds:

```bash
meson setup builddir-asan -Db_sanitize=address  && ninja -C builddir-asan test
meson setup builddir-tsan -Db_sanitize=thread   && ninja -C builddir-tsan test
meson setup builddir-ubsan -Db_sanitize=undefined && ninja -C builddir-ubsan test
```

## Dependencies

**Build:** GCC 13+ or Clang 15+, Meson 0.60+, Ninja, pkg-config.
**Optional:** lcov/gcovr (coverage), Valgrind, perf.
**Runtime:** pthreads, C11 standard library.

All provided by `nix develop` via `flake.nix`.

## Contributing

1. Create `tests/test_<name>.c`, add to `tests/meson.build`
2. Build and test: `ninja -C builddir && meson test -C builddir`
3. Run sanitizers before submitting
4. Measure coverage: `./scripts/measure_coverage.sh`

## Distribution and Packaging

Source releases are produced via the build system:

```bash
make dist                                    # ./build-dist/lime-parser-X.Y.Z.tar.gz
meson dist -C builddir --formats=gztar       # builddir/meson-dist/lime-parser-X.Y.Z.tar.gz
```

Both produce a reproducible tarball with everything tracked under
`git ls-tree HEAD`.

Package recipes for downstream distributions live under
[`packaging/`](packaging/):

| Recipe | Distribution |
|--------|--------------|
| [`packaging/lime.spec`](packaging/lime.spec)               | Fedora / RHEL / openSUSE (RPM) |
| [`packaging/debian/`](packaging/debian/)                   | Debian / Ubuntu (.deb source package) |
| [`packaging/Formula/lime.rb`](packaging/Formula/lime.rb)   | Homebrew (macOS / Linux) |
| [`packaging/PKGBUILD`](packaging/PKGBUILD)                 | Arch Linux |
| [`packaging/APKBUILD`](packaging/APKBUILD)                 | Alpine Linux |
| [`flake.nix`](flake.nix)                                   | Nix / NixOS (project root) |

See [`packaging/README.md`](packaging/README.md) for the release
checklist.

## Acknowledgements

Lime is derived from the **Lemon** parser generator by
[D. Richard Hipp](https://www.hwaci.com/drh/), originally developed as
part of the [SQLite](https://www.sqlite.org/) project.  Lemon is
released into the public domain.

We are grateful to Dr. Hipp and the SQLite team for creating and
maintaining Lemon, and for their commitment to public domain software.

## License

Public Domain

## References

- **Lemon Parser Generator**: http://www.hwaci.com/sw/lemon/
- **SQLite**: https://www.sqlite.org/
