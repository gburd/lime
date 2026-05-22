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
- `lemon_snapshot_create("foo.y", &err)` runs the lime + cc
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
