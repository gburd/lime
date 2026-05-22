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

**What is missing.**  The runtime cannot today reconstruct an LALR(1)
automaton from a merged grammar at runtime.  Specifically:

- `apply_add_rule()` records new rules in the snapshot's parallel
  rule-metadata arrays but cannot derive new action-table entries
  for shift/reduce dispatch on those rules.
- `rebuild_automaton()` validates rule metadata and bumps the
  snapshot version, but does not recompute `yy_action`,
  `yy_lookahead`, `yy_shift_ofst`, `yy_reduce_ofst`, or
  `yy_default`.
- `create_base_snapshot("foo.y", &err)` returns a clear error
  pointing callers at `<Prefix>BuildSnapshot()` rather than parsing
  the grammar file in-process.

**What it would take.**  Refactor `lime.c`'s build phases
(`FindRulePrecedences`, `FindFirstSets`, `FindStates`, `FindLinks`,
`FindFollowSets`, `FindActions`, `CompressTables`, `ResortStates`)
into a public library function `lime_build_tables(symbols[], rules[],
LimeBuildResult *out)` that does not depend on global state or file
I/O.  Then wire `apply_*` and `rebuild_automaton` to call it on the
mutated grammar.  The algorithms exist; the work is purely structural
refactoring of `lime.c` to expose them.

**Source-tree references.**

- `src/snapshot.c::create_base_snapshot` -- weak default that emits
  the actionable error.
- `src/snapshot_modify.c::apply_add_token / apply_add_rule /
  rebuild_automaton` -- record metadata; defer table reconstruction.
- `src/parser_composition.c::compose_snapshots` -- composition
  pipeline reaches `rebuild_automaton` and inherits the same gap.

**Workaround.**  Run `lime` on the merged grammar text and load the
result via `<Prefix>BuildSnapshot()`.  This is what the `examples/`
directory does and what the v0.2 release ships.

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
