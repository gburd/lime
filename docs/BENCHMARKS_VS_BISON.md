# Lime vs Bison: A Direct Comparison

A side-by-side measurement of Lime against GNU Bison 3.8.2 on
identical grammars, running on the same hardware with the same
compiler.

**TL;DR:** On a fair lex+parse JSON benchmark Lime is **1.81×
faster** than Bison+Flex.  On a parser-only arithmetic benchmark
Lime is 1.15× faster, JIT-armed 1.25× faster.  Lime still
generates parsers measurably faster (~4× on a 250-rule grammar --
last measured on Lime 0.1.0; the gap is unlikely to have shrunk).
Generated source is larger (1.47× on the older benchmark) because
Lime carries more introspection metadata.  Pick Lime when you need
runtime grammar extension, the SIMD lexer, the LLVM JIT for hot
grammars, or modern error diagnostics.  Pick Bison when you need
GLR with merge functions, IELR(1), or its long production track
record.

This document is a working measurement, not a marketing page.
Numbers are reproducible and the methodology is below.

## Current numbers (Lime 0.2.4, May 2026)

### JSON benchmark (lex + parse, fair on both sides)

This is the headline number.  Both sides perform an honest
lex-and-parse cycle on the same 515-byte JSON document with nested
objects and arrays.  The Bison side runs Flex 2.6.4 +
`json__scan_string` + `json_parse`.  The Lime side runs a small
hand-rolled tokenizer in a loop, feeding tokens to `parse_token`
as it goes.  Same input bytes, same logical work, same harness.

100,000 iterations per trial, 5 trials, median:

| Tool | mean (ms) | per-doc | per-token | vs Bison |
|------|----------:|--------:|----------:|---------:|
| **bison + flex**  | 470 | 4.70 µs | ~50 ns | 1.00× |
| **lime**          | 260 | 2.60 µs | ~28 ns | **1.81×** |
| **lime + JIT**    | 267 | 2.67 µs | ~29 ns | 1.76× |

JIT does not pull ahead of the unrolled-switch interpreter on this
size of grammar (16 rules, 11 terminals).  The JIT compile cost
(750 ms class) is not amortised across only 100 K parses on a
micro-grammar.  Real wins for the JIT come on bigger grammars and
hotter loops -- see `bench/jit_comparison` numbers below.

### Arithmetic benchmark (parser-only on Lime side)

`bench/bench_flex_bison_compare/`: 12-rule arithmetic grammar,
parsing `(1 + 2) * (3 + 4) - 5` (13 tokens).  Median of 7 trials,
each = 5 inner trials of 100,000 parses.

| Tool | min (ms) | mean (ms) | per-parse | per-token | vs Bison |
|------|---------:|----------:|----------:|----------:|---------:|
| **bison**     | 45.3 | 49.2 | 492 ns | 38 ns | 1.00× |
| **lime**      | 39.8 | 42.6 | 426 ns | 33 ns | **1.15×** |
| **lime + JIT**| 34.7 | 39.3 | 393 ns | 30 ns | **1.25×** |

This benchmark hand-feeds Lime pre-tokenized input but routes
Bison through `flex` + `bison_parse`, so it slightly under-counts
Lime's end-to-end cost.  The JSON benchmark above is the honest
apples-to-apples number; this one is parser-internals only.

### JIT compile scaling

`tests/test_pg_grammar`: full PostgreSQL SQL grammar (3,842 LR
states, 3,584 rules, 557 terminals, 145,227 action-table entries).

| Codegen path | Compile time | Notes |
|--------------|-------------:|-------|
| Original (fully unrolled `state × lookahead` switch) | did not terminate within 5 minutes | LLVM `default<O2>` does not scale on ~146 K basic blocks |
| With O2 disabled, still unrolled | ~186 s | LLVM mandatory lowering still slow on that IR size |
| **Compact (`generate_find_shift_action_compact`)** | **~19 ms** | tables baked as constant globals, ~30 IR instructions |

The compact path kicks in above `IR_SIZE_THRESHOLD = 500_000`
(`nstate × nterminal`).  Below the threshold the unrolled path is
still used because its constant-folded action returns are faster
per call on small grammars.

### JIT vs interpreted speedup

`bench/jit_comparison`: synthetic 128-state grammar with 64
terminals, action table of 8,192 entries.

| Path | Per-parse |
|------|----------:|
| Interpreted (table walk in C) | 98 ns |
| JIT (after warmup) | 43 ns |

**JIT is 2.28× faster** than the interpreter on this grammar size.
JIT compile time on the same grammar is 750 ms; break-even after
~13.6 M parses.  This is the workload class where JIT shines: a
single grammar parsed many times in a long-running process (a
database, a language server, a query engine).

### SIMD-accelerated tokenization

`bench/parser_bench`: SIMD vs scalar character classification on
4 KB through 256 KB inputs, AVX2 on x86_64 / NEON on aarch64.

| Path | Time per 4 KB classify |
|------|----------------------:|
| Scalar | 3171 ns |
| SIMD (best available) | 1784 ns |

**SIMD is 1.78× faster** on classification.  End-to-end tokenizer
throughput holds steady at 99-101 MB/s across input sizes.

## Historical baseline (Lime 0.1.0)

The original publication of this document reported Bison 1.29×
*faster* than Lime at parse throughput on a 55-token SQL stream
(525 ns vs 675 ns median).  That measurement used the
`bench/bench_grammar.{y,lime}` 250-rule SQL-like grammar -- a
different benchmark than the arithmetic one above, so the numbers
are not directly comparable to the current table.  Several rounds
of work since 0.1.0 have closed and reversed the gap on the
arithmetic benchmark:

  * Push-parser engine inlined and rewritten to use the snapshot
    layout directly (eliminated one level of indirection).
  * Action table layout reorganised to match the
    `find_shift_action` access pattern, improving cache hit rate.
  * SIMD tokenizer integrated end-to-end.
  * LLVM JIT engine integrated with the runtime, not just batch-mode.
  * `int16_t` -> `int32_t` widening of the offset arrays
    (correctness fix; trivial perf impact on small grammars).

A re-run of the original 250-rule SQL benchmark on the current
codebase is on the to-do list; an apples-to-apples comparison
against the 0.1.0 baseline is the right way to verify the
turnaround on a larger grammar.

## Methodology

### Environment

  * **Hardware:** Apple M-series, aarch64-darwin
  * **Compiler:** clang 21.1.8, `-O2 -std=c11`
  * **Bison:** 3.8.2 (via `nix develop` -> `flake.nix`)
  * **Flex:** 2.6.4
  * **LLVM:** 21.1.8 (for the Lime JIT)
  * **Lime:** 0.2.4

### Reproduce

The Nix flake provides every tool used; no host install required.

```bash
nix develop --command bash -c '
    cd /path/to/lime
    ninja -C builddir
    # Arithmetic-grammar comparison (5 trials by default)
    ./builddir/bench/bench_flex_bison_compare/bench_flex_bison_compare

    # JIT vs interpreted on a synthetic medium grammar
    ./builddir/bench/jit_comparison

    # Tokenizer + JIT + miscellaneous microbenchmarks
    ./builddir/bench/parser_bench

    # PG-grammar JIT compile time + correctness
    LIME_PG_TEST_JIT=1 ./builddir/tests/test_pg_grammar
'
```

The arithmetic-grammar benchmark is wall-clock time across 5 inner
trials; on a quiet machine the variance run-to-run is
~10 %.  Take the median of several outer runs for a stable number.

### Caveats

  * **Single grammar, single input shape** for the headline number.
    Different grammars and inputs will show different ratios.
  * **Apple Silicon** for these numbers.  On x86_64 with AVX2 the
    SIMD speedup ratios tend to be larger; the JIT speedup ratios
    on small grammars tend to be larger as well.
  * **Push parser overhead:** Lime is push (caller drives
    `parse_token`); Bison is pull (parser calls `yylex()`).  The
    shape of the integration affects the measured numbers as much
    as the parser internals do.
  * **No semantic actions** in the arithmetic benchmark.  Real
    grammars do work in actions; that work dominates if it's
    non-trivial.

## Feature Comparison

Performance numbers in isolation are misleading.  Here is what each
tool actually gives you:

| Feature | Bison 3.8.2 | Lime 0.2.4 |
|---------|:-----------:|:----------:|
| Parser generator in one file | ✗ | ✓ (single `lime.c`) |
| Public domain license | ✗ (GPL exception) | ✓ |
| Push-parser API | ✗ (pull via yylex) | ✓ |
| Reentrant by default | ✗ (opt-in) | ✓ |
| Runtime grammar extension | ✗ | ✓ |
| SIMD-accelerated tokenizer | ✗ | ✓ (AVX2/NEON) |
| LLVM JIT | ✗ | ✓ (auto-tuned per grammar size) |
| `%symbol_prefix` for namespace isolation | ✗ | ✓ |
| Built-in lexer compiler (`-X` flag) | (separate Flex) | ✓ |
| Format and lint flags | ✗ | ✓ (`-F`, `-L`) |
| Structured error diagnostics | ✗ | ✓ (RFC 0059) |
| `ParseTokenName`, `ParseExpectedTokens` | ✗ | ✓ |
| Copy-on-write snapshots for thread safety | ✗ | ✓ |
| Error recovery with `error` nonterminal | ✓ | ✓ (inherited from Lemon) |
| GLR parsing | ✓ | partial |
| Generalized-LR with merge functions | ✓ | ✗ |
| IELR(1) / LALR(1) algorithm choice | ✓ | LALR(1) only |
| Mature, large user base | ✓ | ✗ (newer project) |

## When to Pick Each

**Choose Bison if:**

  * Your grammar is fixed at build time and will never change at
    runtime.
  * You need GLR with merge functions (Bison's `%glr-parser` with
    `%merge`).
  * You need IELR(1) over LALR(1) -- some grammars need it.
  * You want the longest production track record available.

**Choose Lime if:**

  * You need to load and unload grammar extensions at runtime
    without recompilation.
  * You want JIT-accelerated parsing of a hot, mostly-static grammar.
  * The SIMD-tokenizer throughput matters (large input streams).
  * You need ergonomic error diagnostics out of the box.
  * Public-domain licensing matters to you.
  * You want a single-file, no-dependency parser generator that you
    can vendor directly into your build.
