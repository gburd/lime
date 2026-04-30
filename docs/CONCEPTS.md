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
ParserSnapshot *snap = lemon_snapshot_create("sql.y", &err);
lemon_snapshot_acquire(snap);   /* +1 refcount */
lemon_snapshot_release(snap);   /* -1 refcount, freed at 0 */
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
classify characters in parallel — 32 bytes at a time on AVX2.  This
delivers 5-10x faster lexing compared to scalar character-by-character
scanning.  A scalar fallback is always available.

## JIT Compilation

The optional LLVM JIT compiles parser action table lookups into native
machine code at runtime.  This replaces the table-driven interpreter
loop with direct jumps, yielding 2.5-4.2x speedup on the action lookup
phase.  JIT is most beneficial for large grammars (500+ states) in
long-running processes.  See [JIT_ANALYSIS.md](JIT_ANALYSIS.md) for
a detailed cost-benefit analysis.

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
[MODULE_FORMAT.md](MODULE_FORMAT.md).

## Further Reading

- [API Reference](API.md) — function-level documentation
- [Architecture](ARCHITECTURE.md) — component diagram and data flow
- [Extensions](EXTENSIONS.md) — writing extensions step by step
- [Algorithm](ALGORITHM.md) — LALR(1) theory and Lime's implementation
