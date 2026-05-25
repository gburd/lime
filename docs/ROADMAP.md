# Lime Roadmap

This file tracks features and limitations that the codebase currently
references as **under construction**.  Each entry describes what works
today, what is missing, what would be required to finish the feature,
and where the placeholder lives in the source tree.

Items are listed in rough order of impact.

---

## 1. In-process LALR(1) automaton rebuild library

**What works today.**  The full pipeline is end-to-end:

- `lime -n` emits a `<Prefix>BuildSnapshot()` builder AND embeds the
  original grammar source as a `static const unsigned char[]` so the
  runtime can rebuild against a merged grammar.
- `lime_snapshot_create("foo.y", &err)` runs the lime + cc
  subprocess pipeline on a grammar file and returns a populated
  snapshot.  See `src/snapshot_create.c` and
  `tests/test_snapshot_create.c` (8/8 PASS).
- `lime_compile_grammar_text(text, len, &err)` does the same for a
  grammar held in memory.
- `publish_modified_snapshot(reg, base, ...)` takes the subprocess
  path automatically when the base snapshot has `grammar_source`
  available.  It calls `lime_modifications_to_grammar_text()` to
  serialise the loaded extensions' modifications, concatenates them
  with the base text, and reruns the pipeline.  The resulting
  snapshot has fully recomputed action tables and the new tokens /
  rules are reachable through `parse_token()`.  Verified by
  `tests/test_extension_rebuild.c` (11/11 PASS): a postfix-factorial
  extension adds a token + rule, the merged grammar is rebuilt, and
  parses through the new rule succeed.

Requirements: `lime` and a C compiler must be reachable (PATH or
`LIME_BIN` / `LIME_CC`); the runtime probes at `LIME_TEMPLATE` and
`LIME_SNAPSHOT_BUILD_C` cover source-tree and installed layouts.

**What is missing.**  Two tracker items remain:

- **Cleanup of dlopen handles.**  The snapshot tables are
  deep-copied out of each dlopened `.so`, so the snapshot itself is
  independent of the `.so`.  But the `.so` stays mapped into the
  process for the lifetime of the snapshot.  Memory cost is small
  (~100 KB per loaded grammar) and acceptable for daemons that load
  grammars during init, but a long-running process that loads
  thousands of grammars over its lifetime would accumulate
  mappings.  Tracker: extend `ParserSnapshot` with a `dlopen` slot
  and `dlclose()` it during `destroy_snapshot`.
- **Pure in-process build.**  The current pipeline forks a `lime`
  subprocess and a C compiler, which adds ~200ms of latency and
  requires a compiler at runtime.  A pure in-process Build() --
  exposing `lime.c`'s `FindRulePrecedences` /  `FindFirstSets` /
  `FindStates` / `FindLinks` / `FindFollowSets` / `FindActions` /
  `CompressTables` / `ResortStates` as a callable library -- would
  remove that latency and let lime work in environments without a
  compiler installed.  The algorithms are all in `lime.c` already;
  the work is structural decoupling from file I/O and global state.

**Source-tree references.**

- `src/snapshot_create.c::compile_grammar_file_to_snapshot,
  lime_compile_grammar_text` -- the working subprocess pipeline.
- `src/extension.c::publish_modified_snapshot` -- chooses subprocess
  rebuild when the base has `grammar_source`, falls back to the
  metadata-only path otherwise.
- `lime.c::ReportSnapshotInit` -- emits `s_grammar_source[]` and
  the `grammar_source` field on `LimeParserTables`.

---

## 2. JIT-accelerated `parse_token`

**What works today.**  Two paths:

1. *Per-token JIT for the runtime push parser.*  `lime_jit_compile(snap)`
   now also emits a `jit_find_shift_action(state, lookahead) -> action`
   function (alongside the original monolithic batch function).
   `parse_engine_step()` checks `snap->jit_ctx` and dispatches through
   the JIT'd lookup instead of the table-driven `find_shift_action()`.
   Each parse_token call goes through native code: a fully-inlined
   nested switch that LLVM lowers to native jump tables, avoiding
   the indirect-load chain through `yy_shift_ofst` / `yy_lookahead`
   / `yy_action` / `yy_default`.  Verified by
   `tests/test_jit_parse_equivalence` (21/21 PASS) -- JIT and table
   paths produce identical outcomes on every well-formed and
   malformed input the test feeds them.

2. *Monolithic batch JIT.*  The original `jit_parse_sequence(tokens,
   count, state)` function continues to be compiled and is callable
   for callers that want batch processing.  `bench/jit_comparison`
   shows 2-4x speedup on the synthetic batch workload.

Performance signal from `bench/bench_flex_bison_compare` on
aarch64-darwin (500K iterations x 8 trials, identical arithmetic
grammar):

```
  lime     min=166.56 ms mean=175.00 ms
  bison    min=183.58 ms mean=194.05 ms
  lime+jit min=145.73 ms mean=150.25 ms
  speedup vs bison (lime mean):     1.11x
  speedup vs bison (lime+jit mean): 1.29x
```

**Implementation notes for future contributors.**

- The JIT'd `jit_find_shift_action` returns `i32` (not `i16`) so the
  aarch64 / x86_64 ABIs put a well-defined 32-bit value in the
  return register.  Returning LLVM `i16` directly leaves the upper
  16 bits undefined; the C ABI requires the *caller* to zero-extend
  small return types, so reading via a `uint16_t (*)(...)` function
  pointer would get garbage in the upper bits.  Use the
  `JITFindShiftActionFn` typedef in `include/jit_context.h` (uint32
  in / out) and let the engine mask down.  This is documented at
  the typedef and at `src/parse_engine.c::find_shift_action`.

**What is missing.**  A few smaller items:

- *Reduce dispatch via JIT.*  The current JIT only accelerates the
  shift action lookup; reduce / shift-reduce / accept dispatch
  still goes through C in `parse_engine_step`.  Extending the JIT
  to emit the full inner loop (similar to the existing
  `jit_parse_sequence`, but state-machine-correct) would give a
  further speedup, possibly into the 1.5-2x range vs Bison.

- *Tiered compilation.*  All states are compiled to the same
  optimisation level today.  A future tiered model could keep cold
  states on the table-driven path and JIT only the hot ones, which
  would reduce compile latency for very large grammars (the PG
  PG grammar at ~3,842 states currently takes ~19 ms to JIT via
  the compact path).

**Source-tree references.**

- `src/jit_codegen.c::generate_find_shift_action` -- IR for the
  per-token lookup function.
- `src/jit_context.c::jit_compile_snapshot` -- looks up both
  `jit_parse_sequence` and `jit_find_shift_action` after compile.
- `src/parse_engine.c::find_shift_action` -- the dispatch site that
  routes to the JIT'd function when `snap->jit_ctx != NULL`.
- `tests/test_jit_parse_equivalence.c` -- regression test asserting
  JIT and table paths produce identical parse outcomes.

---

## 3. Bayesian and LLM disambiguation strategies

**What works today.**  Priority-based, fork-resolve, and Bayesian
(Beta-Bernoulli) disambiguation strategies are fully implemented
and exercised by tests.  See `src/strategy_bayesian.c` and
`tests/test_strategy_bayesian.c` (4 tests / 20 assertions covering
cold-start, biased feedback, independence across conflict points,
and posterior-mean closed-form match).

**What is missing.**  `STRAT_LLM` still binds to the stub vtable.
Bayesian uses greedy posterior-mean selection; Thompson sampling
would be a natural exploration upgrade.

**What it would take.**  A generic LLM HTTP client honouring a
configurable model endpoint (the `examples/llm_oracle/` directory
has a sketch).  Thompson sampling for Bayesian is ~50 lines of
additional code in `strategy_bayesian.c`.

**Source-tree references.**

- `src/strategy_bayesian.c` -- working Beta-Bernoulli
  implementation.
- `src/disambiguation.c::stub_vtable` -- the stub binding that
  STRAT_LLM still uses.

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
