# Performance ideas — concrete optimizations with effort/impact estimates

This is a working list of measurable performance improvements that
would actually help.  Each item has an effort estimate (rough
person-days), expected impact (measured or estimated), and known
risk.  Items are ordered roughly by impact-per-effort, easiest
wins at the top.

## 1. Inline `parse_token` into `parse_engine_step` for the hot path

  * **Effort:** 0.5 day
  * **Impact:** ~5-10 % on per-token cost (one fewer function call
    per token; LTO helps but only sometimes).
  * **Risk:** low.  `parse_token` is currently a thin wrapper in
    `src/parse_context.c`; the engine is in `src/parse_engine.c`.
    Move the engine's hot loop into `parse_token` directly with
    `static inline` for `find_shift_action` /
    `find_reduce_action`.
  * **Verify with:** `bench/bench_flex_bison_compare` JSON
    benchmark; expect 5-10 % drop in `lime_json` mean time.

## 2. Action-table layout: pack frequently-used states first

  * **Effort:** 2-3 days
  * **Impact:** ~10-20 % on big-grammar parse throughput
    (PG-class).  Cache footprint reduction; the few states that
    handle 80 % of tokens fit in fewer cache lines.
  * **Risk:** medium.  Requires representative profiling input to
    decide hot-state ordering; risks pessimising any workload that
    differs from the profile.
  * **How:** add a `lime --pgo=trace.bin <grammar>` mode that
    reads a `parse_token`-trace from a real workload and re-orders
    states in the emitted snapshot accordingly.  Trace format:
    just a flat list of `state` values written from a custom
    extension hook.

## 3. Prefetch yy_action[] one or two iterations ahead

  * **Effort:** 1 day
  * **Impact:** 5-15 % on grammars with action tables that don't
    fit in L1 (PG: 145K * 2 = 290 KB action table; L1 is typically
    32-64 KB).
  * **Risk:** low.  `__builtin_prefetch` is portable and never
    breaks correctness.
  * **How:** in `find_shift_action`, after computing `idx`,
    prefetch `yy_action[idx + 1]` and `yy_lookahead[idx + 1]`
    (best-effort speculation that the next token will hit nearby).

## 4. Compact JIT codegen: tighter IR than the current six-block form

  * **Effort:** 1-2 days
  * **Impact:** 10-30 % on JIT'd large-grammar parse throughput.
    The current compact path emits two loads + two compares + two
    branches.  Combining the bounds check with the lookahead
    match into a single subtract-and-test could save one branch.
  * **Risk:** medium.  Subtle in IR; changes that look right on
    paper sometimes regress.
  * **How:** experiment with branchless variants in
    `src/jit_codegen.c::generate_find_shift_action_compact`.
    Verify correctness with `tests/test_pg_grammar` (the JIT vs
    table cross-check already in place catches regressions).

## 5. Specialise the engine for snapshots with no extensions loaded

  * **Effort:** 1 day
  * **Impact:** 5-10 % on the no-extension fast path.
  * **Risk:** low.  Two-tier engine: one inner loop without the
    extension-conflict check, used when
    `get_loaded_extension_count(snap->registry) == 0`; the
    existing engine for everything else.
  * **How:** branch in `parse_engine_step` on the extension count;
    short-circuit to the simpler loop.  See
    `bench/extension_overhead.c` for the per-call cost we'd elide.

## 6. SIMD action-table lookup for very small grammars

  * **Effort:** 3-5 days
  * **Impact:** 20-50 % on grammars with `nterminal <= 32`
    (calculator-class).
  * **Risk:** medium.  Architecture-specific intrinsics; needs an
    AVX2 path on x86_64 and a NEON path on aarch64, and a portable
    fallback for the rest.
  * **How:** for grammars where `nterminal` fits in a 256-bit
    register, a single SIMD compare against `yy_lookahead[ofst..]`
    finds the match position in one instruction, replacing the
    current load + compare branch.  Worth doing only if profiling
    shows small-grammar workloads dominate -- which they don't for
    most production users.

## 7. Reduce snapshot allocation traffic with arena pool

  * **Effort:** 2 days
  * **Impact:** unclear; depends on allocation pattern.  Could be
    significant for short-lived parse jobs (per-request parsing in
    a server) where the allocation cost is comparable to the parse
    cost.
  * **Risk:** medium.  Memory ownership semantics get more complex.
  * **How:** replace `malloc`/`free` calls in `snapshot_build.c`
    with a pool allocator scoped to the snapshot's lifetime.

## 8. Persist Bayesian disambiguation posterior to disk

  * **Effort:** 2 days
  * **Impact:** quality-of-life, not throughput.  Avoids cold-start
    re-training for every process invocation.
  * **Risk:** low.  Format is `(token, state, ext_id, alpha,
    beta)` quadruples; serialise as a fixed-size record file.
  * **How:** add `bayesian_save(path)` /
    `bayesian_load(path)` in `src/strategy_bayesian.c`.

## 9. Token-stream prefetching from the lexer

  * **Effort:** 3-5 days
  * **Impact:** 5-15 % end-to-end on lex+parse benchmarks
    (depending on token cost).
  * **Risk:** medium.  Needs tokenizer and parser to share a
    pipelined buffer; the current API treats them as independent.
  * **How:** lexer fills a small ring buffer of tokens ahead of
    the parser's current position so the parser never blocks on
    lex.  Useful only when lex cost is non-trivial relative to
    parse cost (true on the JSON benchmark; less true on
    arithmetic).

## 10. Co-locate snapshot tables in a single allocation

  * **Effort:** 1 day
  * **Impact:** 2-5 %.  Cache-line affinity: `yy_shift_ofst`,
    `yy_action`, `yy_lookahead`, `yy_default` all hit close
    together during a parse step; making them adjacent in memory
    improves cache utilisation.
  * **Risk:** low.  Allocation pattern change; ownership stays
    the same.
  * **How:** in `snapshot_build_from_tables`, single `malloc` of
    `total_bytes`; carve up into the four arrays via
    pointer arithmetic.

## 11. JIT compile in a background thread

  * **Effort:** 1-2 days
  * **Impact:** wall-clock latency-to-first-parse, not steady-state
    throughput.  For a server that's about to handle a flood of
    parse jobs on a never-before-seen grammar, this is meaningful.
  * **Risk:** medium.  Thread-safety of LLVM contexts in OrcJIT.
  * **How:** spawn a dedicated worker that owns the LLVM context;
    submit `lime_jit_compile_async(snap)` and the caller continues
    on the interpreted path until the JIT is ready.

## What would make things much faster but is out of scope here

  * **GLR with shared packed forest:** would let Lime accept the
    same family of grammars Bison's `%glr-parser` does, at the
    cost of substantially more engine complexity.
  * **JITed action callbacks:** today the JIT compiles only the
    shift/reduce dispatch.  Compiling the user-supplied action
    bodies via Clang at runtime would give end-to-end speedups
    on grammars where the action work dominates.  This is a major
    project (clang-as-library, header dependency tracking, ABI
    questions).
  * **Incremental parsing:** edit-tree-style reparse on small
    text edits.  Tree-sitter does this; Lime doesn't have the
    machinery for it.

## Methodology for picking the next item

For any item above, the rule is:

  1. Capture a baseline with the current head.
  2. Implement the change in isolation.
  3. Re-measure with the same harness, same input.
  4. Reject if the change does not improve the median by more
     than 2× the run-to-run variance.

`bench/bench_flex_bison_compare` is the right harness for items
that affect the parser inner loop.  `bench/jit_comparison` is
right for JIT codegen changes.  `bench/parser_bench` is right for
SIMD tokenizer changes.  Profile first, optimise second.
