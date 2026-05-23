# Lime Parser Generator

[![CI](https://codeberg.org/gregburd/lime/actions/workflows/ci.yml/badge.svg)](https://codeberg.org/gregburd/lime/actions)
[![Pages](https://codeberg.org/gregburd/lime/actions/workflows/pages.yml/badge.svg)](https://gregburd.codeberg.page/lime/)

## Overview

Lime is a runtime-extensible LALR(1) parser generator.  It reads a
context-free grammar and emits a C parser, like Yacc or Bison — but
unlike those tools, the generated parser can load and unload grammar
extensions at runtime without recompilation.

The generator itself compiles from a single C file with no dependencies.
Generated parsers optionally use SIMD-accelerated tokenization (AVX2/NEON)
and LLVM JIT compilation for action table lookups.

## Motivation

Database engines, language servers, and extensible query processors need
parsers that can evolve without downtime.  Adding a new operator, a custom
type, or a dialect-specific clause traditionally means editing the grammar,
regenerating the parser, and restarting the process.

Lime eliminates that cycle.  Grammar extensions are shared libraries loaded
at runtime.  Conflict detection and disambiguation happen live.  The base
parser runs at full speed when no extensions are loaded — the extension
machinery has zero overhead until activated.

This design is driven by a single observation: no existing parser
generator supports runtime grammar modification.  Lime fills that gap.

## Why Lime over Yacc/Bison?

- **Runtime extensibility** — Load and unload grammar extensions
  dynamically via a C API, with conflict detection and resolution callbacks.
  No other parser generator offers this.
- **Performance** — SIMD-accelerated tokenization delivers 5-10x faster
  lexing.  Optional LLVM JIT provides 2.5-4.2x faster action table lookups.
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
make JIT=0      # build without LLVM JIT (stubs)
make install PREFIX=/usr/local
```

The single-file build accepts every `.y` grammar.  The `-X` flag for
the `.lex` lexer compiler requires linking the lex sources -- if
invoked without them, lime prints a clear error directing you to
either `meson setup` or
`cc -o lime lime.c src/lex/*.c -Iinclude -Isrc/lex -DLIME_HAS_LEX_COMPILER`.

Build options:

```sh
# LLVM JIT feature: auto (default), enabled, or disabled
meson setup builddir -Dllvm=enabled    # hard-require LLVM
meson setup builddir -Dllvm=disabled   # force stub mode, no libLLVM link
meson setup builddir -Dllvm-static=true -Dllvm=enabled  # statically link LLVM
meson configure builddir -Dllvm=disabled   # toggle after the fact
```

With `-Dllvm=disabled` the resulting binaries have zero references to
`libLLVM.so`; `jit_is_available()` returns false and JIT call sites
fall through to the interpreter.

With `-Dllvm-static=true` meson invokes `llvm-config --link-static` and
links the LLVM component archives directly into the final binary,
removing the runtime dependency on `libLLVM.so`. Expect a 50-80 MB
binary size increase and slower link; useful when shipping to hosts
that do not have a matching LLVM SONAME installed.

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
│   ├── jit_context.{c,h}   #   LLVM JIT compilation
│   └── ...                 #   (35+ source files)
│
├── include/                # Public API headers (snapshot_build.h,
│                           # tokenize_simd.h, parser.h, ...)
├── tests/                  # Test suite (60+ files)
├── bench/                  # Benchmarks (JIT, overhead, real parser)
├── examples/               # Example grammars and extensions
├── contrib/                # SQL dialect extensions (Oracle, MySQL, ...)
├── docs/                   # Reference documentation
├── man/                    # Man pages: lime(1), lime_grammar(5), lime_lex(5)
├── scripts/                # Validation and coverage scripts
└── tools/                  # Composition and management utilities
```

## Documentation

See **[docs/README.md](docs/README.md)** for the full index.  Key documents:

| Document | Description |
|----------|-------------|
| [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md) | Build Lime, write your first grammar |
| [docs/CONCEPTS.md](docs/CONCEPTS.md) | Snapshots, extensions, conflicts, JIT |
| [docs/INTEGRATION.md](docs/INTEGRATION.md) | Embed Lime in your project |
| [docs/EXAMPLES.md](docs/EXAMPLES.md) | All examples explained |
| [docs/API.md](docs/API.md) | C API reference |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | System design |
| [docs/DIAGNOSTICS.md](docs/DIAGNOSTICS.md) | Parser error messages and recovery |
| [docs/EXTENSIONS.md](docs/EXTENSIONS.md) | Writing runtime extensions |
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
`-s` (statistics), `-L` (lint), `-F` (format).  See `man lime` or
`lime -x` for the full list.

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

### vs Bison + Flex (lex + parse, fair)

`bench/bench_flex_bison_compare/`: same JSON document parsed
through Flex+Bison and Lime in the same harness.  100k iterations
per trial, 5 trials, median:

| Tool | mean (ms) | per-doc | vs Bison |
|------|----------:|--------:|---------:|
| bison + flex | 470 | 4.70 µs | 1.00× |
| lime         | 260 | 2.60 µs | **1.81×** |

This is the honest end-to-end comparison: both sides include
lexing.  See [docs/BENCHMARKS_VS_BISON.md](docs/BENCHMARKS_VS_BISON.md)
for methodology and the parser-only arithmetic benchmark.

### Parser-only throughput

Same harness, arithmetic grammar with pre-tokenized Lime input:

| Tool | per-parse | per-token | vs Bison |
|------|----------:|----------:|---------:|
| bison       | 492 ns | 38 ns | 1.00× |
| lime        | 426 ns | 33 ns | **1.15×** |
| lime + JIT  | 393 ns | 30 ns | **1.25×** |

### JIT speedup vs interpreted

`bench/jit_comparison`, synthetic 128-state grammar:

| Path | Per-parse |
|------|----------:|
| Interpreted | 98 ns |
| JIT (warm) | 43 ns |

JIT compile time on this grammar is 750 ms; break-even after ~14 M
parses.  Useful for long-running processes that hit the same
grammar over and over (databases, language servers, query engines).

### JIT compile scaling

The JIT codegen has two paths:

- **Unrolled** (default below `nstate × nterminal = 500,000`): every
  state is compiled to a basic block with a constant-action switch,
  yielding the fastest per-call performance.
- **Compact** (above the threshold): the snapshot's action tables
  are baked in as LLVM constant globals and the lookup is done via
  ~30 IR instructions.  This is what makes the JIT scale to
  PG-class grammars.

On the full PostgreSQL SQL grammar (3,842 states, 145 K-entry
action table), the compact path JITs in **~19 ms**.  An earlier
unrolled-only path either timed out or took 186 s on the same input.

### SIMD tokenizer

`bench/parser_bench`, AVX2 on x86_64 / NEON on aarch64:

| Path | 4 KB classify |
|------|--------------:|
| Scalar | 3171 ns |
| SIMD (best available) | 1784 ns |

End-to-end tokenizer throughput holds at 99-101 MB/s across
4 KB-256 KB inputs.

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
./scripts/check_memory.sh             # valgrind
```

Sanitizer builds:

```bash
meson setup builddir-asan -Db_sanitize=address  && ninja -C builddir-asan test
meson setup builddir-tsan -Db_sanitize=thread   && ninja -C builddir-tsan test
meson setup builddir-ubsan -Db_sanitize=undefined && ninja -C builddir-ubsan test
```

## Dependencies

**Build:** GCC 13+ or Clang 15+, Meson 0.60+, Ninja, pkg-config.
**Optional:** LLVM 14-21 (JIT; verified on 14.0.6 and 21.1.8, expected
to build on every release in between via the compat shim in
`include/jit_llvm_compat.h`).  lcov/gcovr (coverage), Valgrind, perf.
**Runtime:** pthreads, C11 standard library.  LLVM if JIT enabled.
**Runtime:** pthreads, C11 standard library.  LLVM if JIT enabled.

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
- **LLVM ORC JIT**: https://llvm.org/docs/ORCv2.html
