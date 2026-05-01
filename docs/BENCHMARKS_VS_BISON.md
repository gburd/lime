# Lime vs Bison: A Direct Comparison

A side-by-side measurement of Lime 0.1.0 against GNU Bison 3.8.2 on an
identical grammar, running on the same hardware with the same compiler.

**TL;DR:** Lime generates parsers 3.85× faster than Bison but runs
them 1.4× slower. Lime's generated source is 1.47× larger. Use Lime
when you need runtime grammar extension, faster iteration during
grammar development, or modern error diagnostics. Use Bison when raw
runtime throughput on a static grammar is the only thing that matters.

## Methodology

### Environment

- **Hardware:** Apple M-series, aarch64-darwin
- **Compiler:** clang 21.1.8, `-O2 -std=c11`
- **Bison:** 3.8.2 (via `nix-shell -p bison`)
- **Lime:** 0.1.0, commit `fa14b77` (the RFC 0059 diagnostics build)

### Grammar

Both tools run on the same bench grammar — an ~250-rule SQL-like
expression language with 27 tokens, representative of what a
database or query engine would build. The grammars are parallel:
`bench/bench_grammar.y` for Bison, `bench/bench_grammar.lime` for
Lime. Same productions, same precedence table, same token set.

### Measurements

1. **Parser generation time** — wall clock for the generator to read
   the grammar and emit C code. Median of 20 runs each.
2. **Generated source size** — `wc -c` of the `.c` output.
3. **Compiled binary size** — stripped, with a trivial `main()` driver
   that feeds one EOF token.
4. **Runtime parse throughput** — time to parse a 55-token
   pre-tokenized SQL stream (SELECT, INSERT, UPDATE, DELETE,
   WHERE/AND/OR predicates). 5000 iterations of 200-parse batches to
   defeat `clock_gettime` quantization. Same stream for both tools.
   Compiled with `-O2 -DNDEBUG`. Lime uses the parser-reuse pattern
   described in [INTEGRATION.md](INTEGRATION.md#parser-allocation-and-reuse)
   (one `ParseAlloc`, `ParseInit` between parses) — the same model
   Bison uses internally.

Reproduce with:

```bash
nix-shell -p bison --run ./scripts/bench_vs_bison.sh
nix-shell -p bison --run ./scripts/bench_runtime_vs_bison.sh
```

## Results

### Parser Generation

| Metric | Bison 3.8.2 | Lime 0.1.0 | Ratio |
|--------|------------:|-----------:|------:|
| Generation time (median) | 154 ms | 40 ms | **Lime 3.85× faster** |
| Generated `.c` source | 108,126 bytes | 158,916 bytes | Lime 1.47× larger |
| Stripped binary | 33,712 bytes | 50,416 bytes | Lime 1.49× larger |

Lime wins decisively on generation speed. The generated source is
larger because it includes Lime's runtime introspection helpers
(`ParseTokenName`, `ParseState`, `ParseExpectedTokens`,
`ParseCoverage`, `ParseFallback`) that Bison doesn't provide.

### Runtime Parse Throughput

On a 55-token SQL stream, built with `-O2 -DNDEBUG` and the recommended
reuse pattern (allocate parser once, `ParseInit` between parses):

| Tool | Median (ns) | Mean (ns) | P95 (ns) | P99 (ns) |
|------|------------:|----------:|---------:|---------:|
| Bison | 525 | 535 | 620 | 775 |
| Lime  | 675 | 680 | 750 | 865 |

**Bison is ~1.29× faster** at pure parse throughput on this workload.

Per-token cost:
- Bison: ~9.5 ns/token
- Lime: ~12.3 ns/token

### Why the Runtime Gap?

Lime's generated parser is a push-parser (you call `Parse(p, code, val)`
per token), while Bison generates a pull-parser (it calls your
`yylex()`). The push design is better for integration with external
lexers — the reason SQLite switched to Lemon in 2001 — but it adds
per-token function-call overhead that Bison avoids by inlining its
input dispatch.

Additionally, Lime's generated code carries instrumentation for
features Bison doesn't offer:
- Token-name table (for `ParseTokenName`)
- Full action table introspection (for `ParseExpectedTokens`)
- Location tracking infrastructure (even when unused)

With those disabled, the gap would narrow — but so would Lime's
diagnostic capabilities.

## Feature Comparison

Performance numbers in isolation are misleading. Here's what each tool
actually gives you:

| Feature | Bison 3.8.2 | Lime 0.1.0 |
|---------|:-----------:|:----------:|
| Parser generator in one file | ✗ | ✓ (single `lime.c`) |
| Public domain license | ✗ (GPL exception) | ✓ |
| Push-parser API | ✗ (pull via yylex) | ✓ |
| Reentrant by default | ✗ (opt-in) | ✓ |
| Runtime grammar extension | ✗ | ✓ |
| SIMD-accelerated tokenizer | ✗ | ✓ (AVX2/NEON) |
| LLVM JIT | ✗ | ✓ |
| Structured error diagnostics API | ✗ | ✓ (RFC 0059) |
| `ParseTokenName`, `ParseExpectedTokens` | ✗ | ✓ |
| Copy-on-write snapshots for thread safety | ✗ | ✓ |
| Error recovery with `error` nonterminal | ✓ | ✓ (inherited from Lemon) |
| GLR parsing | ✓ | partial |
| Generalized-LR with merge functions | ✓ | ✗ |
| IELR(1) / LALR(1) algorithm choice | ✓ | LALR(1) only |
| Mature, huge user base | ✓ | ✗ (new) |

## When to Pick Each

**Choose Bison if:**
- Your grammar is fixed at build time and will never change at runtime
- You need GLR with merge functions (Bison's `%glr-parser` with
  `%merge`)
- Maximum raw parse throughput matters above everything else
- You're maintaining legacy code that already uses Bison

**Choose Lime if:**
- You need to add/remove grammar productions at runtime (plugins,
  dialect switches, DSL-over-SQL)
- You want structured error diagnostics without writing your own
- Your integration model fits a push-parser better (streaming tokens,
  resumable parsing, fork-resolve for ambiguity handling)
- You're writing a database engine, language server, or extensible
  query processor
- You care about license simplicity (public domain vs GPL+exception)
- You prefer faster grammar iteration over absolute runtime throughput

## Caveats and Honest Limitations

This comparison measures a **single grammar on a single workload on a
single machine**. Results will shift with:

- **Larger grammars.** Lime's ~4× generation-time advantage grows with
  grammar size; Bison's runtime advantage shrinks as grammars get
  larger and table lookups dominate more.
- **x86_64 vs ARM.** Measurements here are on Apple Silicon. On
  x86_64, Lime's SIMD tokenizer (AVX2) shows stronger gains than its
  NEON path.
- **With JIT.** Lime's optional LLVM JIT delivers 1.9-2.6× speedup on
  action-table lookups. Bison has no equivalent. This benchmark did
  not enable JIT because warmup cost doesn't amortize on such small
  parses; see `docs/JIT_ANALYSIS.md`.
- **Real tokenization.** This benchmark fed pre-tokenized streams. If
  you include tokenization, Lime's SIMD tokenizer (when used) changes
  the picture significantly — but that's a separate comparison.

For a more rigorous measurement, see `bench/BENCHMARK_RESULTS.md`
(runs Lime's extension overhead benchmarks on a Linux machine).

## Reproducing These Numbers

Full scripts are in the repository:

- `scripts/bench_vs_bison.sh` — generation time and size
- `scripts/bench_runtime_vs_bison.sh` — runtime throughput
- `bench/bench_grammar.y` / `.lime` — the shared grammar
- `bench/bench_runtime.c` — the timing harness (compiled twice, once
  with `-DUSE_BISON` and once with `-DUSE_LIME`)

Requirements: a C compiler, `bison`, `python3` (for nanosecond
timing math), and `lime` itself (built with `cc -o lime lime.c`).

Run both scripts and post the numbers. Apples-to-apples, same box,
same compiler.
