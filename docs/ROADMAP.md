# Lime Roadmap

This file tracks features and limitations that the codebase currently
references as **under construction**.  Each entry describes what works
today, what is missing, what would be required to finish the feature,
and where the placeholder lives in the source tree.

Items are listed in rough order of impact.

---

## Releases shipped

- **v1.0.0** (June 2026) -- API stability commitment for v1.x line.
  Background diagnostics, did-you-mean linter suggestions, GDB +
  LLDB pretty-printers.
- **v1.1.0** (June 2026) -- Rust-target syntax-error introspection
  (`YY_TOKEN_NAMES`, `token_name`, `expected_tokens_in_state`).
- **v1.2.0** (June 2026) -- LSP fast-lint mode, formatter
  idempotence, LLDB pretty-printer test, Win32 thread shim.
- **v1.3.0 (LTS)** (June 2026) -- lalrpop-API Rust skin, formal
  24-month support commitment, customer-reported `%rust_action`
  alias-binding fix.
- **v1.3.1 (LTS patch)** (June 2026) -- alt-group `%rust_action`
  propagation fix (state-machine bug; see CHANGELOG.md), Rust-
  target API-stability documentation, CHANGELOG backfill, README
  LTS callout.

The LTS line is **v1.3.x** through June 2028.  See
[`docs/SUPPORT.md`](SUPPORT.md) for the backport policy.

---

## 1. In-process LALR(1) automaton rebuild library

**Status as of v0.10.0: COMPLETE.**

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
- `lime_compile_grammar_in_process(text, len, &snap, &err)` does it
  **without** the subprocess pipeline -- pure in-process LALR
  construction via `liblime_compiler.a`.  Available since v0.5.4
  (`include/lime_compiler.h`).  Linked via `pkg-config --libs
  lime-compiler` (pkg-config file shipped since v0.10.0).
  Documented in [docs/API.md](API.md#in-process-compiler-api).
- `lime_lint_grammar_in_process(text, len, &diags)` runs the same
  parse + `FindActions` + `lint_grammar` pipeline that `lime -L`
  runs, in-process, with no fork / exec / temp file.  Available
  since v0.10.0; used by `lime-lsp`'s diagnostic refresh path.
  ~10% / 200 ms saved per LSP request on PG's 21k-line `gram.lime`.
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
- **dlopen-handle cleanup.**  The subprocess pipeline's dlopen
  handles are released by `snapshot_dlopen_release` during
  `destroy_snapshot` (added in v0.6.x; see `src/snapshot.c:22`).
  Long-running daemons no longer accumulate mappings.

Requirements: for the *subprocess* path, `lime` and a C compiler
must be reachable (PATH or `LIME_BIN` / `LIME_CC`); the runtime
probes at `LIME_TEMPLATE` and `LIME_SNAPSHOT_BUILD_C` cover
source-tree and installed layouts.  The in-process path needs
neither.

**Why this was the prerequisite.**  This is the prerequisite for
composition (`compose_snapshots`,
[docs/COMPOSITION.md](COMPOSITION.md)) at production speed.
Without it, composition had to either fork+exec lime+cc to re-derive
LALR (~200ms per merge) or settle for snapshot-table merging only
(which doesn't recompute conflict resolutions across composed
grammars).  The in-process path collapses both paths to sub-
millisecond work, which is the threshold for PG-style daemon-startup
extension loading where `shared_preload_libraries` can ship
arbitrary mixes of grammar plugins (DuckDB-compat, EDB Oracle-compat,
pg_infer, pg_mentat, ... composed at backend startup).

Composition is the primary consumer of this work.

**Open follow-ups.**  All blocking items are closed.  The remaining
item is bookkeeping:

- **Thread-local active-context storage.**  The active
  `LimeCompilerContext` pointer is currently process-global, not
  `_Thread_local`, so two concurrent `lime_compile_grammar_in_process`
  calls in different threads race.  Sequential calls in one thread
  are fully isolated.  Thread-local promotion is tracked as phase 5;
  no consumer has reported a need yet.

**Source-tree references.**

- `src/snapshot_create.c::compile_grammar_file_to_snapshot,
  lime_compile_grammar_text` -- the working subprocess pipeline.
- `src/snapshot_create.c::lime_compile_grammar_in_process` (weak
  shim) and `lime.c::lime_compile_grammar_in_process` (strong
  in-process implementation, gated on `LIME_HAVE_SNAPSHOT_BUILD`).
- `lime.c::lime_lint_grammar_in_process` (v0.10.0; LSP-grade
  in-process linter).
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

**Status: COMPLETE.**

**What works today.**  `bench/bench_flex_bison_compare/` ships a
runnable harness that compares Lime against Flex+Bison on an
identical arithmetic grammar.  When `flex` and `bison` are not on
`$PATH`, the benchmark prints a diagnostic and exits cleanly so
unrelated builds are not blocked.  Numbers reproduced in
[BENCHMARKS_VS_BISON.md](BENCHMARKS_VS_BISON.md): Lime 1.81x faster
than bison+flex on the lex+parse end-to-end JSON benchmark.

Retained in this list as a reference entry; nothing remains to do.

---

## Removed

- *Stale "Task #3 dynamic-table support" references.*  The "Task #3"
  identifier referred to an internal tracker that has been folded
  into item 1 above.  All source comments now point at this file.
