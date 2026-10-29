# Performance Characteristics

This document describes the performance characteristics of the extensible SQL
parser system, including baseline measurements, optimization strategies, and
tuning guidance.

All measurements in this document were collected on a Linux x86_64 system
(GCC 14.3, c11, -O2) using the `parser_bench` benchmark suite
(`bench/parser_bench.c`). Results are from 5000 iterations with 200 warmup
iterations. Your numbers will vary by hardware.

## Production build recipe

The meson defaults (`buildtype=debugoptimized`, no LTO, no PGO) are
optimised for development feedback, not for shipping binaries.  Users
building lime for production throughput should turn on the perf
knobs explicitly:

```bash
# Step 1: build with LTO + release mode.  Use the bundled wrapper
# script -- LTO requires gcc-ar / llvm-ar (the plugin-aware archiver
# wrappers) so that the static-archive symbol table sees the LTO
# bytecode.  Plain `ar` does not, and you'll see spurious
# "undefined reference" errors at link time without the wrapper.
#
# Measured on lime v0.9.3 (i9-12900H, gcc 15.2.0):
#   * bench_jit_real_parser (since removed; it timed the AOT
#     Arith() parser): ~3-5% faster on hot path (51.3 vs 53.0 ms)
#   * binary size:           ~50% smaller (same binary:
#                            84 KB -> 41 KB)
#   * Workload-dependent:    streaming throughput cases see larger
#                            wins (+30-50%); some micro-benchmarks
#                            show flat or slightly slower numbers.
#                            Measure on YOUR workload before promising.
./scripts/build-lto.sh build-prod -Dbuildtype=release -Db_ndebug=true
meson compile -C build-prod

# Or, if you prefer to invoke meson directly, set the archiver
# wrappers explicitly:
#   AR=gcc-ar RANLIB=gcc-ranlib NM=gcc-nm \
#       meson setup build-prod -Dbuildtype=release -Db_lto=true -Db_ndebug=true
# (For clang, substitute llvm-ar / llvm-ranlib / llvm-nm; you may
# also need LDFLAGS=-fuse-ld=lld unless your binutils ld has the
# LLVMgold.so plugin installed.)

# Step 2 (optional): profile-guided optimisation.  Workload-dependent;
# measured on lime v0.9.x (i9-12900H, gcc 15.2):
#
#   bench_lsc_small (32-state lexer):    +51%% throughput
#   bench_lsc_medium (32-state lexer):   +46%% throughput
#   bench_lsc_large (122-state lexer):   +26%% throughput
#   bench_parse_fanout (8 thr borrowed): +14%% throughput
#   bench_jit_real_parser hot path:      -7%%   (regression!; bench since removed)
#
# Lex-heavy workloads see substantial wins (training data is
# representative; gcc's PGO chooses good inlining + branch ordering).
# The parser-engine hot path can regress when PGO's profile-driven
# code layout doesn't match the test invocation pattern -- if you
# care most about parse_token throughput, train the profile on a
# parse-heavy workload (e.g. bench_parse_fanout), then measure.
#
# CAVEAT: PGO + LTO together do not currently compose with our static
# library setup (gcc-ar plugin can't handle the dual bytecode).  Use
# one or the other.  PGO alone is the safer choice for lex-heavy
# workloads; LTO alone is the safer choice for general code-size
# reduction and modest hot-path wins.
meson configure build-prod -Dlime_pgo=generate
meson compile -C build-prod

# Run a representative workload (your own grammar / fixtures, NOT
# the test suite).  This populates `build-prod/.../*.gcda` profile
# data files that PGO consumes in the next step.  More diverse
# workloads = better profile = better codegen.  Recommended minimum:
# at least one full lex pass over a representative input AND one
# full parse session.
./build-prod/lime <your-grammar>
./build-prod/bench/parser_bench   # or any representative driver

# Step 3: rebuild with the collected profile.
meson configure build-prod -Dlime_pgo=use
meson compile -C build-prod
```

`-march=native` is NOT enabled by default — it ties the binary to
the build host's CPU.  Add `-Dc_args=-march=native -Dcpp_args=-march=native`
if you control the deployment target.  The SIMD scan helpers
emitted by `--enable=simd` (Rust output) and `--enable=vectorize`
(C output, default ON) already runtime-detect AVX2 / NEON, so
they work on any CPU without `-march=native`.

## Tokenizer Throughput

The tokenizer converts raw SQL text into a stream of typed tokens. Performance
depends on query length, keyword density, and whether a keyword lookup table
is provided.

| Query Type           | Length | ns/op  | Tokens/sec |
|---------------------|--------|--------|------------|
| Simple SELECT        | 47 B   | ~975   | ~1.0M      |
| INSERT with JSON     | 95 B   | ~2,650 | ~377K      |
| Subquery             | 162 B  | ~4,330 | ~231K      |
| Complex JOIN (5 tables) | 275 B | ~9,230 | ~108K   |
| Recursive CTE        | 289 B  | ~10,890| ~92K       |

**Keyword lookup overhead**: Tokenizing the complex JOIN query without a
keyword table takes ~12,330 ns/op (vs ~9,230 ns/op with keywords). The
keyword table adds a hash lookup per identifier but allows early
classification, which reduces downstream parser work. The net effect is
approximately 25% faster tokenization with keyword recognition enabled.

### Token Table Performance

Lock-free keyword lookup uses FNV-1a hashing with open addressing:

| Operation                    | ns/op | ops/sec |
|-----------------------------|-------|---------|
| Lookup 36 keywords (all hit) | ~1,860 | ~538K  |
| Lookup 5 keywords (all miss) | ~211   | ~4.7M  |

Misses are fast because the hash chain terminates quickly on empty slots.
Hit lookups average ~52 ns per keyword.

## Snapshot Operations

Snapshots use atomic reference counting for lock-free concurrent access.

| Operation          | ns/op | ops/sec |
|-------------------|-------|---------|
| Acquire + Release | ~71   | ~14.0M  |

The acquire/release cycle is a single `atomic_fetch_add` followed by an
`atomic_fetch_sub` with release semantics. This is the cost incurred each
time a parser thread pins a snapshot for use.

### Snapshot Memory

The `ParserSnapshot` struct is 128 bytes plus heap-allocated action tables:

| Component            | Size Formula                         | Example (500 states, 150 terminals) |
|---------------------|--------------------------------------|--------------------------------------|
| `yy_action`         | `action_count * 2 bytes`             | 150,000 B                           |
| `yy_lookahead`      | `lookahead_count * 2 bytes`          | 150,000 B                           |
| `yy_shift_ofst`     | `nstate * 2 bytes`                   | 1,000 B                            |
| `yy_reduce_ofst`    | `nstate * 2 bytes`                   | 1,000 B                            |
| `yy_default`        | `nstate * 2 bytes`                   | 1,000 B                            |
| **Total**           |                                      | **~296 KB**                         |

For a typical SQL grammar (500 states, 150 terminals), action tables consume
approximately 296 KB. Each snapshot holds an independent copy, so concurrent
snapshots multiply this cost.

## SIMD Character Classification

Character classification determines whether each byte is alphabetic, digit,
or whitespace. The classifier processes 32 bytes at a time (AVX2) or falls
back to scalar.

| Implementation     | ns/op (4 KB) | Throughput    |
|-------------------|-------------|---------------|
| Scalar             | ~11,655     | ~343 MB/s     |
| Best available     | ~14,260     | ~280 MB/s     |

On this test system AVX2 was not detected at runtime, so both rows use the
scalar path. On AVX2-capable hardware, the SIMD path processes 32 bytes per
instruction vs 1 byte for scalar, yielding a typical 4-8x speedup for
classification. The overall tokenizer speedup is lower (typically 1.5-2x)
because classification is only part of the tokenization pipeline.

### When SIMD Helps Most

- Long identifier-heavy queries (many consecutive alphabetic characters)
- Queries with large whitespace regions (comments, formatting)
- Batch tokenization of large SQL files

### When SIMD Helps Least

- Very short queries (< 32 bytes, cannot fill a SIMD register)
- Operator-heavy queries (single-character tokens bypass classification)

## Action Table Lookup

### Table-Driven Baseline

| Workload                      | ns/op   | ops/sec |
|------------------------------|---------|---------|
| 64 states x 32 terminals     | ~24,080 | ~41.5K  |

This is the cost of looking up all 2048 state+terminal combinations using
the standard `yy_shift_ofst` + `yy_lookahead` + `yy_action` table walk.

There is no JIT.  The LLVM JIT that earlier releases offered was 4-6%
slower than this table walk on x86_64 and 7-9% slower on aarch64 on the
PostgreSQL grammar, and was removed; see [JIT_ANALYSIS.md](JIT_ANALYSIS.md).
`lime -j` still emits an ahead-of-time C switch for the shift lookup.

## Extension Overhead

Extensions modify the grammar at runtime by adding tokens, rules, or
precedence changes. The overhead comes from several components:

### Loading Costs (One-Time)

| Cost Category         | Typical Range  | Trigger                          |
|-----------------------|----------------|----------------------------------|
| Snapshot cloning      | ~100-500 us    | Deep-copy of action tables (~296 KB) |
| Table rebuilding      | ~500 us - 5 ms | Recomputing LALR tables post-modification |
| Token table updates   | ~50-200 ns/token | Adding extension tokens (O(1) per token) |
| Conflict detection    | ~50-650 us     | Scanning for token/rule/semantic conflicts |
| Dependency resolution | ~5-60 us       | Topological sort of extension graph |

### Per-Token Runtime Costs

| Scenario                        | Per-Token Overhead | Notes                     |
|---------------------------------|-------------------|---------------------------|
| No extensions loaded            | 0                 | Zero overhead by design   |
| Extensions loaded, no conflicts | < 1 ns            | Same table format         |
| Priority disambiguation         | ~2-6 ns (amortized) | Invoked only on conflict states |
| Fork-resolve disambiguation     | ~40-1000 ns (amortized) | Parser state cloning |

The amortized per-token numbers assume conflicts occur on ~5% of tokens
in a typical extension-heavy workload. Most tokens pass through the
standard action table lookup with no extension overhead.

### Disambiguation Strategies

Two built-in strategies resolve conflicts between extensions:

- **Priority** (`STRAT_PRIORITY`): Compares numeric priority values.
  Cost: ~100-300 ns per conflict. Deterministic, always confidence 1.0.
  Best when you know which extension should win.

- **Fork-resolve** (`STRAT_FORK_RESOLVE`): Clones parser state for each
  candidate interpretation, feeds lookahead tokens, picks the survivor.
  Cost: ~2-50 us per conflict (depends on fork count and stack depth).
  Creates up to 16 forks (configurable), default 3-token lookahead.

### Memory Overhead Per Extension

| Component                    | Per-Extension   | Notes                      |
|------------------------------|-----------------|----------------------------|
| Registry entry + metadata    | ~350-700 B      | Deep-copied strings        |
| Action table growth          | ~2-10 KB        | Additional states/terminals|
| Fork set (peak, transient)   | ~4-640 KB       | Up to 16 forks * 40 KB    |

For comprehensive analysis including scaling behavior (1-50 extensions),
conflict type performance comparison, and tuning
recommendations, see **[EXTENSION_PERFORMANCE.md](EXTENSION_PERFORMANCE.md)**.

### Recommendations

- Load all extensions at startup before parsing begins, to avoid mid-stream
  snapshot transitions and repeated table rebuilds.
- Use priority strategy for known conflicts (100-1000x faster than fork-resolve).
- Declare `conflicts_with` in extension metadata to reject incompatible
  combinations early, before expensive conflict detection runs.

## Scaling Behavior

### Concurrent Parsers

Multiple parser threads can share a single snapshot via `snapshot_acquire`.
The only contention point is the atomic reference count, which uses
`memory_order_relaxed` for acquire and `memory_order_release` for release.
On modern x86_64 hardware this scales linearly to at least 8-16 threads.

Each parser thread holds its own `ParseContext` with a private stack, so
there is no shared mutable state during parsing.

### Multiple Extensions

Extensions are registered in a global `ExtensionRegistry` protected by a
`pthread_rwlock`. Lookups (read lock) proceed concurrently; only
registration/unregistration (write lock) serializes. In practice, extensions
are loaded at startup and the read lock path dominates.

Each loaded extension adds its modifications to a new snapshot. The cost
scales linearly with the number of extensions for most operations.
Semantic conflict detection is O(N^2) in the number of extensions but
is a one-time cost at load time.

| Extensions | Registry Memory | Conflict Detection | Snapshot Growth |
|-----------|-----------------|-------------------|-----------------|
| 1         | ~700 B          | ~5 us             | ~0%             |
| 5         | ~3.5 KB         | ~50-200 us        | +3-15%          |
| 10        | ~7 KB           | ~100-500 us       | +7-30%          |
| 50        | ~35 KB          | ~1-5 ms           | +30-100%        |

## Memory Budget Guide

| Component                      | Typical Size   | Scaling Factor       |
|-------------------------------|----------------|---------------------|
| `ParserSnapshot` (struct)     | 128 B          | Per snapshot         |
| Action tables                 | 150-300 KB     | Per snapshot         |
| `TokenTable` (36 keywords)    | ~3 KB          | Per table            |
| `ParseContext` (parser stack) | 2-8 KB         | Per concurrent parser|
| `Tokenizer` state             | ~100 B         | Per concurrent tokenizer |

**Total for a typical deployment** (1 snapshot, 4 parser
threads): approximately 500 KB - 1 MB.

## Performance Tuning

### Quick Wins

1. **Enable SIMD**: Ensure the binary runs on AVX2-capable hardware for
   automatic 1.5-2x tokenizer speedup.
2. **Populate keyword table**: Always provide a `TokenTable` to the tokenizer.
   The hash lookup cost is more than offset by faster downstream processing.
3. **Pin snapshots**: Acquire a snapshot reference at the start of a batch
   and release it at the end, rather than acquiring/releasing per query.

### Memory Optimization

4. **Limit concurrent snapshots**: Each snapshot holds a full copy of the
   action tables. If extensions are loaded infrequently, release old
   snapshots promptly.
5. **Share snapshots across threads**: Use `snapshot_acquire` rather than
   creating duplicate snapshots for each thread.

## Running Benchmarks

```
# Build
meson setup builddir
ninja -C builddir

# Run with default settings (10000 iterations, 100 warmup)
./builddir/bench/parser_bench

# Custom iteration count
./builddir/bench/parser_bench 5000 200

# Save CSV output, diagnostics to stderr
./builddir/bench/parser_bench 10000 500 > results.csv 2> diag.txt
```

The benchmark outputs CSV on stdout and diagnostic messages on stderr.
