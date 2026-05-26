# Lime vs Bison: A Direct Comparison

A side-by-side measurement of Lime against GNU Bison 3.8.2 on
identical grammars, running on the same hardware with the same
compiler.

**TL;DR:** On a fair lex+parse JSON benchmark Lime is **1.69×
faster** than Bison+Flex (consistent across v0.2.7-v0.4.4 on
x86_64 i9-12900H).  On a parser-only arithmetic benchmark Lime
runs at **0.81× of Bison** -- Bison's tight-loop arith parser
edges Lime by ~20%, a result that has held steady through every
release in the v0.2 / v0.3 / v0.4 series (verified by
bisecting v0.2.7 / v0.3.3 / v0.4.4 -- all measured 0.81-0.85×).

Lime still generates parsers measurably faster (~4× on a 250-rule
grammar -- last measured on Lime 0.1.0; the gap is unlikely to
have shrunk).  Generated source is larger (1.47× on the older
benchmark) because Lime carries more introspection metadata.
Pick Lime when you need runtime grammar extension, the SIMD
lexer, the LLVM JIT for hot grammars, or modern error
diagnostics.  Pick Bison when you need IELR(1) or its long
production track record.

This document is a working measurement, not a marketing page.
Numbers are reproducible and the methodology is below.

## Current numbers (Lime 0.4.4, May 2026)

Reproducibility: x86_64 i9-12900H Linux, gcc 15.2.0, glibc 2.42,
bison 3.8.2 + flex 2.6.4 from nix store.  `meson setup
--buildtype=debugoptimized -Dllvm=disabled`.  Run `bench/
bench_flex_bison_compare/bench_flex_bison_compare`.

### JSON benchmark (lex + parse, fair on both sides)

This is the headline number.  Both sides perform an honest
lex-and-parse cycle on the same 515-byte JSON document with nested
objects and arrays.  The Bison side runs Flex 2.6.4 +
`json__scan_string` + `json_parse`.  The Lime side runs a small
hand-rolled tokenizer in a loop, feeding tokens to `parse_token`
as it goes.  Same input bytes, same logical work, same harness.

100,000 iterations per trial, 5 trials, median (3-trial sweep):

| Tool | mean (ms) | per-doc | per-token | vs Bison |
|------|----------:|--------:|----------:|---------:|
| **bison + flex** | 263.0 | 2.63 µs | ~28 ns | 1.00× |
| **lime**         | 155.6 | 1.56 µs | ~16 ns | **1.69×** |

Lime's lexer (hand-rolled per-call inner loop) outpaces Flex's
table-driven scanner.  This is the user-visible number for
shipped consumers (PG team, etc.).

### Arithmetic benchmark (parser-only on Lime side)

`bench/bench_flex_bison_compare/`: 12-rule arithmetic grammar,
parsing `(1 + 2) * (3 + 4) - 5` (13 tokens).  100,000 iterations
per trial, 3-trial sweep:

| Tool | min (ms) | mean (ms) | per-parse | per-token | vs Bison |
|------|---------:|----------:|----------:|----------:|---------:|
| **bison** | 23.7 | 24.2 | 242 ns | 19 ns | 1.00× |
| **lime**  | 27.4 | 29.8 | 298 ns | 23 ns | **0.81×** |

The arith bench is **parser-internals only**: pre-tokenized
input fed to Lime's `parse_token` push API, vs Bison's pull-mode
`bison_parse` driving its own static state machine.  Bison's
generated arith parser fits in L1 cache and beats Lime's runtime
push engine on this micro-workload.

When the workload includes lexing (the JSON benchmark above), the
order reverses by ~2×.  Real applications combine lex+parse, so
the JSON number is the practical comparison.

This number has been stable across the v0.2 / v0.3 / v0.4 series
(measured at v0.2.7: 0.85×, v0.3.3 pre-GLR: 0.81×, v0.4.4 current:
0.81×).  The GLR engine merged in v0.3.4 has zero impact on this
result -- LALR fast-path files are byte-identical pre/post merge.

The earlier doc claim of 1.15× faster on this bench was from
m3pro hardware with different bison + glibc; corrected to the
stable x86_64 measurement during the v0.4.4 perf-audit pass.

### JIT scaling on PG grammar (informational)

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

## Lime+JIT vs simdjson (orientation, not a fair fight)

simdjson is a SIMD-accelerated JSON-only parser; Lime is a generic
LALR(1) parser generator that happens to be hosting a JSON
grammar.  simdjson wins; the question is by how much, and how
much of the gap is allocation cost vs parsing cost.

`bench/bench_simdjson_compare/` runs Lime in three allocator modes
and simdjson in its standard ondemand mode.  Median of 5 trials,
100 K iterations each, on a 515-byte JSON document (Apple M1,
clang 21.1.8 -O2, simdjson 4.6.4):

| Tool | mean (ms) | MB/s | docs/s | gap to simdjson |
|------|----------:|-----:|-------:|----------------:|
| lime+jit malloc (full free)        | 622 |   79 | 161 K | 20.3× |
| lime+jit malloc-leak (no free)     | 561 |   88 | 178 K | 18.3× |
| lime+jit arena (zero alloc steady) | 383 |  128 | 261 K | 12.5× |
| **simdjson ondemand**              |  31 | 1603 | 3.3 M | 1.0× |

Cost breakdown:

| Component | ms | % of malloc total |
|-----------|---:|------------------:|
| `free()` walk        |  61 | 10 % |
| `malloc()` per node  | 178 | 29 % |
| Parse machinery      | 383 | 61 % |
| Total                | 622 | 100 % |

So:

  * The allocator overhead is real (~39 % of total) but it's not
    the dominant cost.
  * Even with **zero allocations in steady state** (arena reset
    between iterations, mirroring simdjson's own model), Lime is
    12.5× slower.
  * That remaining gap is the inherent cost of a token-at-a-time
    push parser vs a SIMD-vectorised structural decoder operating
    on 32-byte chunks at a time.
  * Lime is the right tool when you want a generic parser
    generator (any language, runtime extensions, JIT for hot
    grammars).  simdjson is the right tool when "JSON, fast"
    *is* the requirement.

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
| Generalized-LR with merge functions | ✓ | ✓ (since v0.3.4; opt-in via `lime_parse_glr()`, ~5–8× slower than LALR fast path on unambiguous input -- see [GLR.md](GLR.md)) |
| IELR(1) / LALR(1) algorithm choice | ✓ | LALR(1) only |
| Mature, large user base | ✓ | ✗ (newer project) |

## When to Pick Each

**Choose Bison if:**

  * Your grammar is fixed at build time and will never change at
    runtime.
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
