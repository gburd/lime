# Composed-snapshot parse performance (the honest state)

This is the answer to a recurring question: *"which existing Lime knob makes
a composed (in-process) snapshot parse as fast as the AOT parser?"*

**There isn't one.** No flag, env var, or build option shipped today closes
the gap between a composed snapshot and the statically generated
`base_yyparse`. This page says why, so nobody burns a day looking for a
setting that doesn't exist.

## The two parse paths

- **AOT (static):** `lime -n` emits a grammar-specific `base_yyparse` — a
  compiled switch-of-switches with the action/goto table bases hoisted into
  registers and every reduce inlined. The C compiler optimizes it as one
  function.
- **Composed (push / table-driven):** a `ParserSnapshot` is interpreted at
  runtime by the shared push engine (`parse_engine_step`), which walks the
  composed tables and calls back into the embedder's `host_reduce` per reduce.
  This is the path any loaded grammar extension takes.

The composed path is currently **~1.5–1.9x slower** per parse than AOT on
representative queries. That is structural, not a missing tuning knob.

## Where the time goes (after the W1 memset fix)

The letter-39 W1 fix removed the single largest cost — a per-reduce
`memset` of the whole `yyParser` (which embeds the ~1.6 KB initial parse
stack) in the generated `<Name>HostReduce` wrapper. It now zeroes only the
header fields the reduce actions read. That was measured on the PostgreSQL
port at ~53% of push-path parse self-time and is a pure win (no behavior
change).

What remains is inherent to interpreting composed tables at runtime:

1. **The interpreter loop.** `parse_engine_step`'s shift/reduce/goto `for(;;)`
   dispatch versus the AOT parser's inlined switch-of-switches.
2. **Reduce dispatch + the callback boundary.** Each reduce does an indirect
   `yy_rule_reduce_fn[ruleno]` call and crosses the `host_reduce` boundary back
   into the embedder.
3. **Per-token value-stack boxing.** A pointer-width `memcpy` per token to move
   the semantic value in and out of the generic stack.

None of these is removable by a memset shrink, and none is touched by the JIT.

## Why the LLVM JIT does not fix this

The only "fast path" ever shipped for composed snapshots is the optional LLVM
JIT (`LIME_JIT=1`). It accelerates **only the shift-action lookup** — the
nested `switch` on state and lookahead token in
`src/jit_codegen.c`. It does **not** touch the reduce dispatch, the
`host_reduce` callback boundary, or the per-token value-stack boxing, which is
where the residual time actually goes.

Worse, on x86_64 it loses to the C compiler outright. Lime's own arch-gate in
`lime_jit_compile()` (`src/jit_context.c`) skips the JIT for small grammars on
x86_64, because the compiler-generated static switch out-predicts the indirect
calls the JIT emits (`docs/BENCHMARK_RESULTS.md`: −8 to −12% on JSON/arith).
The PostgreSQL port measured the same: **0% speedup on x86_64, marginally
slower** (`SELECT 1` 786→813 ns; join 5539→5743 ns). On aarch64 the JIT does
win, which is the only reason it is still gated-on rather than removed.

## The real fix (planned, not shipped)

Closing the AOT/composed gap without LLVM means making the interpreter loop
itself competitive: a threaded-code / computed-goto dispatch for
`parse_engine_step` with the snapshot table bases hoisted into locals, plus a
persistent reducer context and value-stack changes to drop the per-reduce
setup and per-token copy. That is tracked as letter-39 W2/W3 and is not in this
release. Until it lands, the composed path is slower than AOT by the margins
above, and no existing knob changes that.

## See also

- `docs/REDUCE_DISPATCH.md` — how composed reduces are routed to base +
  extension actions.
- `docs/JIT_ANALYSIS.md` — the JIT's scope and measured results.
- `docs/HOST_REDUCE.md` — the host-reduce contract the W1 fix operates on.
