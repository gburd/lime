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

1. Implement JIT pipeline using MCJIT for initial validation
2. Verify correctness against table-driven interpreter for all test grammars
3. Replace MCJIT with OrcJIT lazy compilation stubs
4. Add per-state compilation triggers on first state entry
5. Add optional background compilation for predicted hot states

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
