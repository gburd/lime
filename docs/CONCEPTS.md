# Concepts

This document explains the key ideas behind Lime's runtime extension
system.  For the basic parser generator (grammar → C parser), see
[Getting Started](GETTING_STARTED.md).

## Grammars and Parsers

Lime reads a `.y` grammar file containing production rules and emits a C
source file implementing an LALR(1) push-parser.  The generated parser
exposes three functions:

- `ParseAlloc()` — allocate parser state
- `Parse(parser, token_code, token_value)` — feed one token
- `ParseFree()` — release parser state

This is the same model as Lemon.  Everything below is what Lime adds.

## Snapshots

A **snapshot** is an immutable, reference-counted capture of a parser's
state tables (action table, goto table, rule table, symbol table).
Snapshots are the unit of concurrency: multiple threads can parse
simultaneously by acquiring references to the same snapshot.

```
ParserSnapshot *snap = lime_snapshot_create("sql.y", &err);
lime_snapshot_acquire(snap);   /* +1 refcount */
lime_snapshot_release(snap);   /* -1 refcount, freed at 0 */
```

Snapshots are created from a grammar file or by modifying an existing
snapshot (see Extensions below).  They are never mutated in place —
modification always produces a new snapshot.

## Extensions

An **extension** is a set of grammar modifications (new tokens, new
rules, precedence changes) packaged with metadata (name, version,
dependencies).  Extensions are registered, then loaded:

```
ExtensionInfo info = {
    .name = "jsonb_operators",
    .version = "1.0.0",
    .get_modifications = my_callback,
};
ExtensionID id;
register_extension(registry, &info, &id);   /* declare */
load_extension(registry, id, snapshot, &err); /* activate */
```

Loading an extension applies its modifications to the current snapshot,
producing a new snapshot.  The old snapshot remains valid for any
in-flight parses.  Unloading reverses the process.

### Extension Lifecycle

```
register_extension()  →  EXT_REGISTERED  (declared, inactive)
load_extension()      →  EXT_LOADED      (modifications applied)
unload_extension()    →  EXT_UNLOADED    (modifications removed)
```

## Conflict Detection

When two extensions modify the same part of the grammar, a **conflict**
arises.  Lime detects conflicts at three levels:

1. **Token conflicts** — two extensions define the same token code with
   different semantics (e.g., `^` as exponentiation vs. XOR).
2. **Rule conflicts** — two extensions add rules for the same
   non-terminal with overlapping patterns.
3. **Semantic conflicts** — rules have the same structure but different
   reduction actions.

Conflicts are detected automatically when extensions are loaded.  They
do not prevent loading — they are reported and can be resolved by a
disambiguation strategy.

## Disambiguation Strategies

A **disambiguation strategy** decides which extension "wins" when a
conflict occurs during parsing.  Lime provides several built-in
strategies and supports custom ones:

| Strategy | How it works |
|----------|-------------|
| **Priority** | Each extension has a numeric priority; highest wins. Fast (~740 ns). |
| **Fork-resolve** | Clone the parser state, try both interpretations, pick the one that succeeds. More accurate but slower (~1.3 µs). |
| **LLM oracle** | Query an external LLM API to decide. See `examples/llm_oracle/`. |
| **Custom** | Implement the `DisambiguationStrategyVTable` interface. |

Strategies are pluggable per-extension or per-registry.

## Execution Policies

After disambiguation selects a winner, the **execution policy** controls
how semantic actions are dispatched:

| Policy | Behavior |
|--------|----------|
| `EXEC_FIRST_ONLY` | Run only the winning extension's action. |
| `EXEC_ALL` | Run all extensions' actions independently. |
| `EXEC_CHAIN` | Run actions in sequence, piping output. |

## Copy-on-Write and Thread Safety

Lime's extension system is thread-safe by design:

- Snapshots are immutable and reference-counted with atomic operations.
- The extension registry uses read-write locks.
- Loading/unloading an extension creates a new snapshot; existing
  parsers continue using the old one undisturbed.
- No shared mutable state between concurrent parse sessions.

This means you can load an extension on one thread while other threads
are actively parsing with the previous snapshot.

## SIMD Tokenization

Lime's tokenizer uses SIMD instructions (AVX2 on x86, NEON on ARM) to
classify characters in parallel — 32 bytes at a time on AVX2.  The
classification primitive itself runs 4-8x faster than scalar
character-by-character scanning.  At the full-tokenizer level the
speedup is more modest -- typically 1.5-2x -- because the loop
overhead and emit path absorb the rest.  See
[bench/bench_simd_classify](../bench/bench_simd_classify.c) and
[PERFORMANCE.md](PERFORMANCE.md) for measured numbers.  A scalar
fallback is always available.

## JIT Compilation

The optional LLVM JIT compiles parser action table lookups into native
machine code at runtime.  This replaces the table-driven interpreter
loop with direct jumps, yielding ~2.3x speedup on the lookup step
itself (per `bench/jit_comparison`).  Action lookup is roughly 15% of
total parse time on the arithmetic benchmark, so the *overall* parse
speedup is in the 1.04-1.10x range on x86 and Apple Silicon (per
`docs/BENCHMARK_RESULTS.md`); on x86 small grammars JIT can actually
be a 5-10% regression because compile latency outweighs runtime
gain.  JIT is most beneficial for large grammars (500+ states) in
long-running processes.  See [JIT_ANALYSIS.md](JIT_ANALYSIS.md) for
the detailed cost-benefit analysis.

## Module System

Lime supports modular grammars via directives:

```
%module_name    "json_support"
%module_version "1.0.0"
%require        base_sql ">=1.0.0".
%export         json_expr json_value.
%import         expr from base_sql.
```

Modules can be composed with the `lime-compose` tool, which resolves
dependencies via topological sort and merges grammars.  See
<a href="MODULE_FORMAT.md">MODULE_FORMAT.md</a>.

## Multi-Grammar Parsing

A single Lime parser can handle inputs that mix multiple
languages.  The host grammar (e.g. SQL) registers trigger lexemes
that, when seen in the token stream, switch the parser into a
sub-grammar (e.g. JSON, JSONPath, XML).  When the sub-grammar's
scope closes — bracket depth returns to zero or an explicit exit
token fires — the parser resumes in the host grammar.

The mechanism is purely runtime: there are no built-in triggers,
and the host grammar is unchanged.  Triggers are registered
against a `GrammarContextStack` that the parser carries alongside
its `ParserSnapshot`:

```c
context_switch_register_trigger(stack, "json", json_snap, "json");
```

After registration, the parse-engine hook (`parse_engine_step`)
checks the trigger registry on each token; if no triggers are
registered the cost is one load + statically-predicted-not-taken
branch per token.  Single-grammar parsers pay nothing for the
multi-grammar machinery being present.

See [CONTEXT_SWITCH.md](CONTEXT_SWITCH.md) for the API and
`examples/multi_grammar_sql_json/` for a worked example
(SQL host + JSON embedded).  The v0.4.4 `%embed` directive (see
[EMBED.md](EMBED.md)) is sugar over the same registry: it generates
the wire-up boilerplate at codegen time so grammar authors do not
hand-write `register_trigger` calls.

## Dialect-Conditional Grammars (`%dialect`)

v0.4.0 adds the `%dialect NAME { ... }` directive: a generator-time
conditional rule block included only when the macro `dialect_NAME`
is defined (typically via the `-Ddialect=NAME` shorthand).  One
source file builds one `.c` / `.h` per dialect combination, with
no runtime switch.  The intended use is the dialect-overlay
workflow — `pg_oracle`, `pg_mysql`, `pg_duckdb` extensions of a
shared SQL grammar.  See [DIALECT.md](DIALECT.md).

## File-Level Grammar Inheritance (`%extends`)

v0.4.1 adds `%extends "base.lime"`, which loads and merges another
grammar at parser-generation time.  Companion directives —
`%override` (replace a base rule's body), `%remove` (drop a base
rule), `%override_type` (widen a base type) — give the deriving
file fine-grained control over what it keeps, replaces, and drops.
Diamond patterns (`unified.lime` extends both `oracle.lime` and
`mysql.lime`, both of which extend `ansi.lime`) are explicitly
supported with ten locked design rules covering identity matching,
sibling conflicts, and depth-based winner resolution.  See
[EXTENDS.md](EXTENDS.md).

`%dialect` and `%extends` are complementary: `%dialect` keeps one
file and builds many flavours via `-D`; `%extends` keeps one
flavour per file and merges them at codegen.  The runtime
extension framework (above) covers the third axis: changing the
grammar after the binary is built.

## Generalized-LR (GLR) Mode

v0.3.4 adds an opt-in GLR engine for grammars LALR(1) cannot
handle.  GLR forks the parse stack on conflicts, runs all
alternatives in parallel through a Graph-Structured Stack, and
asks a user-supplied disambiguation callback to pick a winner
when two reductions converge on the same nonterminal at the same
position.  The entry point is `lime_parse_glr()`; the LALR fast
path is byte-identical to the pre-v0.3.4 tree, so callers that
never opt in pay zero cost.  Measured GLR overhead on unambiguous
input is 5–8× the LALR fast path.  See [GLR.md](GLR.md).

## Tooling

Lime ships several developer-facing tools alongside the parser
generator:

- **`lime -L` linter** (v0.5.0 expansion) — opinionated
  grammar-hygiene checker with ~16 rules across errors
  (E001-E005), warnings (W001-W009), opt-in style suggestions
  (S001-S002), and module-composition errors (M001-M003).
  Output formats: `human`, `gcc`, `json`.  CI integration via
  `--lint-strict`.  See [LINT.md](LINT.md).
- **`lime --diff-conflicts`** (v0.4.3) — symbolic LALR conflict
  diff between a base grammar and an overlay.  Designed for
  dialect-extension review and CI gates: exits 0 / 1 / 2 for
  no-new-conflicts / new-conflicts / pipeline-error.  Has a
  `--json` mode with a stable v1 schema.  See
  [DIFF_CONFLICTS.md](DIFF_CONFLICTS.md).
- **`lime -F` formatter** — canonical pretty-printer.
  `format(format(F)) == format(F)` is a tested invariant.
- **`lime-lsp`** (v0.5.0) — Language Server Protocol companion
  for `.lime` files.  Speaks LSP 3.17 (subset) over stdio:
  diagnostics (by exec'ing `lime -L`), go-to-definition, hover,
  document outline.  See [LSP.md](LSP.md) and
  `editors/lime-lsp-config.md`.

## Further Reading

- [API Reference](API.md) — function-level documentation
- [Architecture](ARCHITECTURE.md) — component diagram and data flow
- [Extensions](EXTENSIONS.md) — writing extensions step by step
- [Context Switch](CONTEXT_SWITCH.md) — multi-grammar parsing
- [Dialect](DIALECT.md) — `%dialect` generator-time conditional rules
- [Extends](EXTENDS.md) — `%extends` file-level grammar inheritance
- [Embed](EMBED.md) — `%embed` directive sugar over context switch
- [GLR](GLR.md) — Generalized-LR engine, when to use, performance
- [Lint](LINT.md) — `lime -L` linter rule catalog and CI integration
- [Diff Conflicts](DIFF_CONFLICTS.md) — `lime --diff-conflicts` for overlays
- [LSP](LSP.md) — `lime-lsp` Language Server
- [Algorithm](ALGORITHM.md) — LALR(1) theory and Lime's implementation
