# Lime Roadmap

This file tracks features and limitations that the codebase currently
references as **under construction**.  Each entry describes what works
today, what is missing, what would be required to finish the feature,
and where the placeholder lives in the source tree.

Items are listed in rough order of impact.

---

## 1. In-process LALR(1) automaton rebuild library

**What works today.**  The `lime` generator (in `lime.c`) implements
the full LALR(1) construction (`Build()` / `ReportTable()`) and emits
both a static parser via `limpar.c` and a runtime-friendly
`<Prefix>BuildSnapshot()` builder via `lime -n`.  Applications can
register pre-built parsers with the runtime through
`snapshot_build_from_tables()` (see `include/snapshot_build.h`) and
drive them via `parse_begin / parse_token / parse_end` against the
runtime push parser in `src/parse_engine.c`.

`lemon_snapshot_create("foo.y", &err)` is **fully implemented** via
the subprocess pipeline in `src/snapshot_create.c`: it runs `lime
-n` on the grammar file, compiles the resulting `*_snapshot.c` to a
shared library, `dlopen()`s it, and resolves the generic
`lime_snapshot_entry` symbol the snapshot file emits.  The host
process gets back a populated `ParserSnapshot` ready for
`parse_begin()`.  See `tests/test_snapshot_create.c` for the
end-to-end exercise (8/8 assertions).  Requirements: `lime` and a C
compiler must be reachable (PATH or `LIME_BIN` / `LIME_CC`); the
runtime probe at compile time (`LIME_TEMPLATE`,
`LIME_SNAPSHOT_BUILD_C`) covers source-tree and installed layouts.

**What is missing.**  Two things:

- **Cleanup of dlopen handles.**  The snapshot tables we get back
  are deep-copied out of the .so, so the snapshot itself is
  independent of the .so once built; but the .so stays mapped into
  the process for the lifetime of the snapshot.  Memory cost is
  small (~100 KB per loaded grammar) and acceptable for daemons
  that load grammars during init, but a long-running process that
  loads thousands of grammars over its lifetime would accumulate
  mappings.  Tracker: extend `ParserSnapshot` with a `dlopen` slot
  and dlclose during `destroy_snapshot`.
- **In-process LALR rebuild from a base snapshot + extension
  modifications.**  `apply_add_rule()` records new rules in the
  snapshot's parallel rule-metadata arrays but cannot derive new
  action-table entries for shift/reduce dispatch on those rules
  without re-running the LALR build over the merged grammar.  The
  subprocess path above only handles "fresh grammar from a file";
  it does not yet handle "this snapshot plus these mods".

**What it would take.**  For the in-process rebuild, two options:

  1. *Subprocess from a serialised grammar.*  Use
     `lime_modifications_to_grammar_text()` (already exists in
     `src/mod_serialize.c`) to turn the mod list into a `.y`
     fragment, concatenate it after the base grammar text, and run
     the same subprocess pipeline as `lemon_snapshot_create`.
     Requires keeping the original grammar text alongside the
     snapshot (a getter like `<Prefix>GetGrammarSource()` could be
     emitted by `lime -n`).
  2. *Refactor `lime.c` Build() phases into a pure library.*  The
     algorithms are all in `lime.c` already; the work is structural
     decoupling from file I/O and global state.

Option 1 is the smaller of the two and a reasonable interim path.
Option 2 is the long-term answer.

**Source-tree references.**

- `src/snapshot.c::create_base_snapshot` -- weak default that emits
  the actionable error; superseded by `snapshot_create.c`.
- `src/snapshot_create.c` -- subprocess pipeline; the working v1.
- `src/snapshot_modify.c::apply_add_rule / rebuild_automaton` --
  metadata-only rebuild, awaits Option 1 or 2.
- `src/parser_composition.c::compose_snapshots` -- composition
  pipeline reaches `rebuild_automaton` and inherits the same gap.

**Workaround.**  Build the merged grammar via `lime` directly and
load via `lemon_snapshot_create("merged.y", &err)`.

---

## 2. JIT-accelerated `parse_token`

**What works today.**  `lime_jit_compile(snap)` compiles a monolithic
batch parse function using LLVM ORC for the snapshot.  Numbers from
`bench/jit_comparison` show 2-4x speedup on the synthetic batch
workload.  `bench/bench_jit_real_parser` runs a real generated
arithmetic parser before and after `lime_jit_compile()` and
documents that the static `Arith()` function is unaffected.

**What is missing.**  The runtime push parser
(`parse_engine_step`) does not dispatch through the JIT-compiled
function.  Calling `lime_jit_compile()` therefore accelerates the
batch path but not the typical token-by-token interactive parse.

**What it would take.**  Either (a) extend `jit_codegen.c` to emit a
per-state action-lookup function `(state, lookahead) -> action` and
have `parse_engine_step` call it instead of `find_shift_action()`,
or (b) batch tokens at the parse_token boundary so the existing
`jit_parse_sequence` is reachable.  Option (a) is the cleaner of the
two and matches what most JIT'd parser drivers do.

**Source-tree references.**

- `src/jit_codegen.c::generate_monolithic_parser` -- only emits a
  batch function.
- `src/jit_context.c::jit_find_shift_action` -- comment notes "JIT
  path disabled for per-token lookups (too much overhead)".
- `src/parse_engine.c::find_shift_action` -- pure table lookup; no
  JIT dispatch.

---

## 3. Bayesian and LLM disambiguation strategies

**What works today.**  Priority-based and fork-resolve disambiguation
strategies are fully implemented and exercised by tests.

**What is missing.**  `STRAT_BAYESIAN` and `STRAT_LLM` are accepted by
`disambiguation_create()` but currently bind to a stub vtable that
returns "unresolved" for every conflict.

**What it would take.**  A Bayesian scorer over the conflict-history
state plus a generic LLM HTTP client honouring a configurable model
endpoint (the `examples/llm_oracle/` directory has a sketch).

**Source-tree references.**

- `src/disambiguation.c::stub_vtable` -- the stub binding that
  STRAT_BAYESIAN / STRAT_LLM use.

---

## 4. `examples/calc/` running on the runtime extension API

**What works today.**  `examples/calc/main.c` demonstrates loading a
`.so` plugin and calling its parser via a function pointer.

**What is missing.**  The example uses a homegrown plugin scheme
rather than the `register_extension` / `load_extension` /
`create_modified_snapshot` flow.  Until item 1 lands the example
cannot demonstrate runtime grammar mutation through the framework.

**What it would take.**  Once item 1 is finished, rewrite the calc
example as a registered extension that adds the `^` operator at
runtime and have the host re-parse the same input through the
modified snapshot.

---

## 5. Flex / Bison comparison benchmark

**What works today.**  `bench/bench_flex_bison_compare/` ships a
runnable harness that compares Lime against Flex+Bison on an
identical arithmetic grammar.  When `flex` and `bison` are not on
`$PATH`, the benchmark prints a diagnostic and exits cleanly so
unrelated builds are not blocked.

**What is missing.**  Nothing in the benchmark itself; it is gated on
the host having Flex/Bison installed.

---

## Removed

- *Stale "Task #3 dynamic-table support" references.*  The "Task #3"
  identifier referred to an internal tracker that has been folded
  into item 1 above.  All source comments now point at this file.
