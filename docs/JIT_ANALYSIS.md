# JIT Compilation Analysis for Lime Parser Generator

## Overview

JIT (Just-In-Time) compilation in Lime targets action table lookups during
parsing. While JIT can deliver a 2-5x speedup on the lookup step itself, action
table lookups account for only 10-20% of total SQL parse time. The dominant
bottleneck is tokenization, which consumes 40-60% of parse time. As a result,
JIT compilation of action tables alone yields a modest 1.1-1.5x overall
improvement. The highest-value JIT optimization is tokenizer JIT,
which targets the actual bottleneck and can deliver 1.8-2.8x overall speedup.

This document provides a detailed cost-benefit analysis to help users decide
when and how to apply JIT compilation in their Lime-based parsers.

---

## Parse Time Breakdown

Understanding where time is spent during parsing is essential for evaluating JIT
impact.

| Phase                | Share of Total Parse Time | Description                              |
|----------------------|---------------------------|------------------------------------------|
| Tokenization         | 40-60%                    | Lexical analysis, keyword classification |
| Semantic actions     | 20-30%                    | User-defined reduce actions, AST builds  |
| Action table lookup  | 10-20%                    | State transitions, shift/reduce dispatch |
| Stack operations     | 5-10%                     | Push/pop, state stack management         |

The action table lookup phase is the target of the core JIT optimization. It
involves looking up the correct parser action (shift, reduce, accept, or error)
given the current state and lookahead token.

---

## Measured result (v1.11.3): the JIT is slower on PostgreSQL's grammar

The cost-benefit section below is a model with *assumed* inputs ("if
action lookup is 15% of parse time and the JIT gives 3x on it...").
This section is the measurement that replaces it.

**Setup.** Debian 13, AWS c7i.8xlarge (Xeon Platinum 8488C, VM), gcc
14.2, LLVM 19.1.7, `--buildtype=release -Dllvm=enabled
-Dpg_grammar=true`.  The full PostgreSQL grammar (3,842 states, 557
terminals, 145k actions) -- the only grammar in the tree whose
`nstate x nterminal` (2.14M) clears the x86_64 cost-model threshold
(500k), so the only one where `lime_jit_compile` actually compiles
without `LIME_JIT=1`.  Two snapshots from the same tables, one
JIT-armed.  The 12-statement accept corpus from `tests/test_pg_grammar.c`
is pre-tokenised; each sample is 20,000 corpus passes (2.36M tokens,
~31 ms) timed with `CLOCK_PROCESS_CPUTIME_ID`; 21 rounds alternating
which path runs first; one warm-up round discarded; pinned to one core.
JIT and table produce identical accept/reject on every query first.

**Result, three independent runs:**

| Run | table ns/token | JIT ns/token | JIT speedup (median, min-max) |
|---|---:|---:|---:|
| 1 | 13.2 | 13.8 | 0.958x (0.951-0.967) |
| 2 | 13.0 | 13.8 | 0.942x (0.931-0.955) |
| 3 | 13.2 | 14.1 | 0.939x (0.932-0.943) |

The JIT is 4-6% **slower**, consistently (the spread never reaches
1.0x).  It costs 16-34 ms to compile, so it never breaks even.  On
every smaller grammar the cost model already declines to compile, for
the same reason stated in `src/jit_context.c`: on x86_64 the
C compiler's code for the table lookup beats the JIT's.

**What this does not cover:** aarch64, where the cost model assumes
the JIT wins (not measured here); other LLVM versions; grammars between
the tree's small ones and PostgreSQL's.

**Reproduce:** `LIME_PG_BENCH=21 tests/test_pg_grammar`.

**Note on `bench/bench_jit_real_parser`:** earlier releases cited it as
evidence of a 1.3x+ JIT win.  It cannot show one: both of its scenarios
time the same AOT-generated `Arith()`, and the JIT is armed on a
separate synthetic snapshot that `Arith()` never reads -- in the
measured run `lime_jit_compile` had in fact *declined* to compile
(`JIT_SKIPPED_BY_POLICY`).  Its verdict line now says so instead of
printing a speedup.

## Cost-Benefit Analysis

### Speedup from JIT on Action Table Lookups

JIT compilation replaces the interpreter-style table lookup (a sequence of
comparisons or a binary search over the compressed action table) with native
machine code that directly encodes state transitions as a computed jump.

- **JIT speedup on action lookups**: 2-5x
- **Share of total parse time affected**: 10-20%
- **Overall parse speedup**: 1.1-1.5x

The formula: if action lookup is 15% of total time and JIT gives a 3x speedup
on that portion, overall speedup is `1 / (0.85 + 0.15/3) = 1.11x`.

### Compilation Cost

JIT compilation itself has a cost that must be amortized over multiple parse
sessions.

| JIT Backend | Compilation Latency | Memory Overhead       |
|-------------|---------------------|-----------------------|
| MCJIT       | 10-50ms             | All states compiled   |
| OrcJIT      | Near-zero startup   | Only hot states held  |

### Break-Even Point

The break-even point is the number of parse sessions required before JIT
overhead is recovered through faster parsing.

| Backend | Break-Even (approx.)     | Notes                                  |
|---------|--------------------------|----------------------------------------|
| MCJIT   | ~100-500 parse sessions  | High upfront cost, full compilation    |
| OrcJIT  | ~10 parse sessions       | Lazy compilation, minimal startup cost |

For short-lived processes that parse only a handful of inputs, JIT overhead may
never be recovered. For long-running servers, the cost is negligible.

---

## When JIT Is Beneficial

JIT compilation provides meaningful value in the following scenarios:

- **Large grammars (500+ states)**: The action table is large enough that
  table-driven lookup becomes a measurable cost. JIT eliminates redundant
  comparisons and branch mispredictions in the lookup path.

- **Long-running servers processing many queries**: A database server or
  language server that parses thousands of SQL statements easily amortizes the
  one-time JIT compilation cost. After break-even, every subsequent parse is
  faster.

- **Repeated parsing of similar input patterns**: When the same grammar states
  are visited frequently, JIT-compiled code stays hot in the instruction cache.
  OrcJIT's lazy compilation naturally adapts to these patterns by only compiling
  frequently-visited states.

- **When combined with tokenizer JIT**: The combined effect of action
  table JIT and tokenizer JIT can yield 2-3x overall speedup, which is
  substantial enough to justify the added complexity.

## When JIT Is Not Beneficial

JIT compilation adds complexity and overhead with little payoff in these cases:

- **Small grammars (< 100 states)**: The action table is compact enough that
  table-driven lookup is already fast. The overhead of JIT compilation and the
  LLVM dependency are not justified.

- **One-shot CLI tools**: A command-line tool that parses a single file and
  exits will spend more time on JIT compilation than it saves. Use AOT
  compilation instead, or skip JIT entirely.

- **Short SQL queries (< 50 tokens)**: The total parse time is dominated by
  startup overhead (memory allocation, parser initialization). The action lookup
  phase is measured in microseconds and JIT cannot improve it meaningfully.

- **Memory-constrained environments**: LLVM JIT brings a significant memory
  footprint (tens of MB for the LLVM libraries and compiled code buffers). In
  embedded or resource-limited contexts, the memory cost outweighs the
  performance benefit.

---

## AOT vs Runtime JIT

Lime supports two strategies for compiled action tables: ahead-of-time (AOT)
compilation at parser generation time, and runtime JIT compilation during
parsing.

### AOT Compilation (`lime -j`)

AOT compilation generates native action table code at parser generation time and
embeds it directly in the output C file.

**Advantages**:
- Zero runtime compilation cost
- No LLVM runtime dependency in the generated parser
- Same performance as JIT-compiled code
- Deterministic behavior, no warmup period
- Suitable for all deployment environments

**Disadvantages**:
- Cannot adapt to workload patterns
- All states compiled regardless of whether they are hot
- Must regenerate parser to change optimization

### Runtime JIT

Runtime JIT compiles action table lookups during parser execution, potentially
adapting to observed usage patterns.

**Advantages**:
- Lazy compilation focuses resources on hot states
- Can specialize for observed token distributions
- No parser regeneration needed to change optimization strategy

**Disadvantages**:
- Requires LLVM as a runtime dependency
- Compilation latency during parsing (mitigated by OrcJIT lazy mode)
- Non-deterministic performance during warmup
- Higher memory footprint

### Recommendation

**Use AOT for most deployments.** AOT compilation provides the same steady-state
performance as runtime JIT with none of the runtime complexity. It eliminates
the LLVM dependency from the generated parser, simplifying deployment and
reducing the binary size.

**Use runtime JIT for adaptive scenarios** where workload patterns are unknown at
build time or vary significantly across deployments. This is primarily relevant
for general-purpose database engines that must handle diverse query patterns.

---

## MCJIT vs OrcJIT Migration

LLVM provides two JIT compilation frameworks. Lime initially targets MCJIT but
should migrate to OrcJIT for production use.

### MCJIT (Legacy)

MCJIT compiles an entire LLVM module at once. For Lime, this means all parser
states are compiled in a single batch.

| Metric              | MCJIT Behavior                          |
|---------------------|-----------------------------------------|
| Compilation model   | Whole-module, all states at once        |
| Startup latency     | 10-50ms (scales with grammar size)      |
| Memory usage        | All states resident in compiled code    |
| Granularity         | Cannot compile individual states        |

MCJIT is suitable for prototyping and testing the JIT pipeline but is not
recommended for production due to its high startup cost and all-or-nothing
compilation model.

### OrcJIT (Recommended)

OrcJIT (On-Request Compilation JIT) supports lazy, per-function compilation.
Each parser state can be compiled independently, on demand.

| Metric              | OrcJIT Behavior                           |
|---------------------|-------------------------------------------|
| Compilation model   | Per-state, on demand                      |
| Startup latency     | Near-zero (compile on first use)          |
| Memory usage        | Only hot states compiled and retained     |
| Granularity         | Individual state functions                |
| CPU savings         | 5-10x less compilation CPU vs MCJIT       |

**OrcJIT advantages over MCJIT**:

- **Lazy per-state compilation**: States are compiled only when first entered
  during parsing. Cold states (error recovery, rarely-used productions) are
  never compiled, saving both CPU and memory.

- **5-10x less compilation CPU**: Because only hot states are compiled, total
  compilation work is dramatically reduced for grammars with many states but
  concentrated hot paths.

- **Memory efficiency**: Compiled code for cold states can be freed or never
  generated. Only the working set of active states occupies memory.

- **Near-zero startup**: The parser begins executing immediately using the
  table-driven interpreter. JIT compilation happens in the background or on
  first access, so there is no user-visible startup delay.

### Migration Path

(Status as of Lime 0.2.4: items 1, 2, 3, 5 are done; item 4 is
deliberately not implemented because it is supplanted by the
size-threshold codegen path.)

1. ✅ Implement JIT pipeline using MCJIT for initial validation
2. ✅ Verify correctness against table-driven interpreter for all
   test grammars (see `tests/test_jit_parse_equivalence.c`,
   `tests/test_pg_grammar.c`)
3. ✅ Replace MCJIT with OrcJIT lazy compilation
4. **Skipped.**  Per-state compilation triggers were superseded by
   the size-class strategy: small grammars get an unrolled IR with
   constant-folded action returns, large grammars get a compact
   table-load IR.  Both paths compile in tens of milliseconds; the
   per-state granularity isn't a meaningful win.  See
   `src/jit_codegen.c::generate_find_shift_action_compact`.
5. ✅ Optional background compilation supported via
   `jit_maybe_compile()` policy hooks.

---

## Tokenizer JIT - Highest Value Optimization

The tokenizer JIT targets the actual parsing bottleneck and represents the
single highest-impact JIT optimization available in Lime.

### Why Tokenizer JIT Matters Most

Tokenization consumes 40-60% of total parse time. The primary cost within
tokenization is keyword classification: determining whether an identifier like
`SELECT`, `FROM`, or `WHERE` is a reserved keyword or a user identifier.

Traditional approaches use hash table lookups for keyword classification. While
hash tables provide O(1) average-case lookup, they involve:
- Hash computation over the full keyword string
- Potential hash collisions requiring string comparison
- Cache-unfriendly pointer chasing for chained hash tables

### Trie-Based Keyword Classifier

The tokenizer JIT replaces hash table keyword lookup with a JIT-compiled trie
(prefix tree) that maps directly to native branch instructions.

**How it works**:
1. At JIT compilation time, build a trie from the grammar's keyword set
2. Emit native code that walks the trie character-by-character using direct
   comparisons and jumps
3. Each path through the trie terminates at a leaf that returns the
   corresponding token ID
4. Non-matching paths fall through to the "identifier" token

**Advantages of the trie approach**:
- No hash computation needed
- No memory indirection (the trie is encoded as machine code branches)
- Highly predictable branch patterns for common keywords
- Instruction cache friendly (hot keywords = hot code paths)

### Expected Performance

| Metric                           | Value                     |
|----------------------------------|---------------------------|
| Speedup on tokenization          | 2-3x                      |
| Share of total time affected     | 40-60%                    |
| **Overall parse speedup**        | **1.8-2.8x**              |
| Compilation cost                 | < 5ms (small keyword set) |

The overall speedup is calculated as: if tokenization is 50% of total time and
trie JIT gives a 2.5x speedup, overall speedup is `1 / (0.50 + 0.50/2.5) =
1.67x`. Combined with action table JIT on the remaining 15% of time, the
compound speedup can reach 1.8-2.8x depending on grammar and query
characteristics.

### Tokenizer JIT vs Action Table JIT

| Aspect               | Action Table JIT       | Tokenizer JIT (5E)      |
|----------------------|------------------------|--------------------------|
| Target               | 10-20% of parse time   | 40-60% of parse time     |
| Speedup on target    | 2-5x                   | 2-3x                     |
| Overall impact       | 1.1-1.5x               | 1.8-2.8x                 |
| Complexity           | Moderate                | Moderate                 |
| Recommendation       | Implement second        | **Implement first**      |

---

## Recommended Configuration

### Default Thresholds

The following defaults provide a reasonable starting point for most deployments:

| Parameter                        | Default Value | Description                              |
|----------------------------------|---------------|------------------------------------------|
| `LIME_JIT_ENABLE`                | `0` (off)     | Enable runtime JIT compilation           |
| `LIME_JIT_MIN_STATES`            | `100`         | Minimum grammar states to trigger JIT    |
| `LIME_JIT_HOT_THRESHOLD`         | `10`          | State visits before lazy JIT fires       |
| `LIME_JIT_COMPILE_ALL`           | `0` (off)     | Force compilation of all states (MCJIT)  |
| `LIME_TOKENIZER_JIT`             | `0` (off)     | Enable tokenizer trie JIT               |

### When to Tune

- **Increase `LIME_JIT_MIN_STATES`** if JIT is triggering on small grammars
  where the overhead is not justified. Values of 200-500 are reasonable for
  conservative deployments.

- **Decrease `LIME_JIT_HOT_THRESHOLD`** for servers that need fast warmup on
  initial queries. A value of 1 compiles states on first visit (similar to AOT
  but at runtime).

- **Increase `LIME_JIT_HOT_THRESHOLD`** to reduce total compilation work when
  only a small fraction of states are truly hot. Values of 50-100 ensure only
  the most frequently visited states are compiled.

- **Enable `LIME_JIT_COMPILE_ALL`** only when using MCJIT and you want
  deterministic performance (no lazy compilation jitter). This is equivalent to
  AOT but performed at runtime.

- **Enable `LIME_TOKENIZER_JIT`** for any deployment where tokenization is the
  bottleneck (which is most SQL parsing workloads). This is the single most
  impactful optimization.

### Deployment Decision Tree

```
Is this a one-shot CLI tool?
  YES -> Skip JIT entirely, or use AOT (lime -j)
  NO  -> Continue

Is the grammar large (500+ states)?
  NO  -> Skip action table JIT; consider tokenizer JIT only
  YES -> Continue

Is LLVM available as a runtime dependency?
  NO  -> Use AOT (lime -j) for action tables
  YES -> Continue

Will the parser handle > 100 sessions?
  NO  -> Use AOT (lime -j)
  YES -> Enable runtime JIT with OrcJIT backend

Is tokenization the bottleneck? (typically yes for SQL)
  YES -> Enable LIME_TOKENIZER_JIT for highest impact
  NO  -> Action table JIT alone may suffice
```

## Recommendation (v1.11.3 review)

The two questions a reviewer would ask have answers now:

1. *Does the JIT win on the grammar it was built for?*  No: 4-6% slower
   than the table interpreter on PostgreSQL's grammar on x86_64
   (section above), and it never breaks even.
2. *Would cheaper dispatch tricks get the win instead?*  Already tried
   upstream: `parse_engine_step` uses direct-`goto` threaded dispatch;
   a computed-goto (`&&label`) jump table was measured and rejected as a
   ~12% regression on clang (see the comment above the dispatch loop in
   `src/parse_engine.c`).  The interpreter is not leaving an easy win on
   the table.

So: keep `-Dllvm=disabled` the default for any embedding (it already is
the CI default), and treat the JIT as an experiment until it is shown to
win somewhere -- aarch64 is the untested case the cost model bets on.
If an aarch64 measurement with the same harness
(`LIME_PG_BENCH=21 tests/test_pg_grammar`) also shows no win, the JIT
(four source files, the LLVM 14-21 compat shim, the cost-model policy,
three CI jobs, and a background compile thread that a PostgreSQL backend
cannot host) is a removal candidate.  That decision is the maintainer's;
this section only records the evidence.
