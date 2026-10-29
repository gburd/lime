# Benchmarking and Performance Testing

This document lists the benchmarks under `bench/`, what each one
measures, and how to run them so the numbers mean something.

## Setup

```bash
nix develop                       # meson, ninja, gcc/clang, flex, bison
meson setup builddir --buildtype=release
ninja -C builddir
```

Every benchmark below is built by `ninja`; none is run by
`meson test`.  The flex/bison and simdjson harnesses are skipped at
configure time when those tools are missing.

## The benchmarks

All binaries land in `builddir/bench/`.

| Executable | Measures | Arguments |
|------------|----------|-----------|
| `parser_bench` | Tokenizer throughput (SIMD vs scalar classify), table-driven action lookup, snapshot acquire/release and creation, token-table lookup.  CSV on stdout, commentary on stderr. | `[iters] [warmup]` |
| `extension_overhead` | Extension framework cost against a no-extension baseline: registry ops, conflict detection, disambiguation, execution policy, parser fork, grammar-context switching, and the full pipeline.  CSV on stdout. | `[iters] [warmup]` |
| `bench_pg_rebuild` | CPU time, wall time and peak-RSS growth of one in-process LALR(1) rebuild of a whole grammar (default: the PostgreSQL grammar).  The cost every extension-set change pays. | `<grammar.lime> [iterations]` |
| `bench_parse_fanout` | Parse throughput as parser threads scale 1..N over one shared `ParserSnapshot`; the false-sharing check for the snapshot layout.  POSIX only. | `[thread counts...]` or `THREADS=N` |
| `bench_simd_classify` | SIMD vs scalar character classification in `src/tokenize_simd.c`, ns per 32-byte block.  Backs the README's SIMD claim. | `[bytes] [iters]` |
| `glr_overhead` | The LALR push parser vs the GLR engine on the same unambiguous arithmetic input: the fixed cost of the GLR step machinery. | `[iters] [trials]` |
| `bench_flex_bison_compare` | Lime vs bison + flex (default `-Cem` tables) on an arithmetic grammar and a JSON document.  Both sides are checked to accept the input before timing. | `[iters] [trials]` |
| `bench_flex_bison_compare_cf` | The same harness with the JSON lexer built by `flex -CF` (full tables), flex's fastest mode. | `[iters] [trials]` |
| `bench_runtime_lime`, `bench_runtime_bison` | One pre-tokenised stream through a 250-rule SQL-shaped grammar (`bench_grammar.{lime,y}`), built once per tool.  Needs flex and bison. | `[iterations]` |
| `bench_simdjson_compare` | Lime (malloc, malloc-leak, arena) vs simdjson on JSON at steady state.  Orientation only: simdjson is not a general parser generator.  Needs simdjson. | none |
| `bench_lex_vec`, `bench_lex_novec` | A lime-generated C lexer emitted with `--lex-vectorize` (default) vs `--lex-no-vectorize`, on the same fixture. | `<fixture.json>` |
| `bench_lsc_small`, `bench_lsc_medium`, `bench_lsc_large` | Lexer throughput across DFA sizes (small JSON, medium JSON, PostgreSQL bootscanner), to find where transition-table memory starts to dominate. | none |

Also in `bench/` but not wired into meson (build recipes are in each
file's header comment): `bench_composed_vs_aot.c` and
`bench_composed_vs_aot_heavy.c` (composed snapshot vs AOT parser on
the same tokens, dispatch-bound and action-bound grammars), and
`rust_compare/` (Lime's Rust emit vs hand-written lexers; see its
README).

The PostgreSQL grammar parse benchmark lives in the test suite:
build with `-Dpg_grammar=true` and run
`LIME_PG_BENCH=21 builddir/tests/test_pg_grammar`.

## Measuring correctly

- **Check output before timing.**  The comparison harnesses assert
  that both sides accept (or compute the same result for) the input
  before they time anything.  Keep that property when you add one.
- **Use CPU time, repeat, report the median and the spread.**  A mean
  of noisy runs hides the noise.
- **Pin to one core** for single-thread benchmarks:
  `taskset -c 2 ./builddir/bench/bench_flex_bison_compare`.
- **Alternate A and B** rather than running all of A then all of B,
  so drift (thermal, frequency) hits both sides equally.

Low coefficient of variation (CV) means a usable number:

- **CV < 5%**: consistent
- **CV 5-10%**: usable with care
- **CV > 10%**: investigate system load or thermal throttling first

To reduce variability on Linux:

```bash
sudo cpupower frequency-set -g performance
echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo
taskset -c 2 ./builddir/bench/parser_bench
```

## Profiling

```bash
meson setup builddir-prof --buildtype=debugoptimized
ninja -C builddir-prof
perf record -g ./builddir-prof/bench/parser_bench
perf report
```

Heap profile:

```bash
valgrind --tool=massif --massif-out-file=massif.out \
    ./builddir/bench/bench_pg_rebuild examples/pg/pg_grammar.lime 1
ms_print massif.out
```

## Comparing builds

```bash
meson setup builddir-a --buildtype=release
meson setup builddir-b --buildtype=release   # with your change applied
ninja -C builddir-a && ninja -C builddir-b
for i in 1 2 3 4 5; do
    taskset -c 2 ./builddir-a/bench/parser_bench > a.$i.csv
    taskset -c 2 ./builddir-b/bench/parser_bench > b.$i.csv
done
```

## No JIT benchmarks

Earlier releases shipped `jit_comparison` and `bench_jit_real_parser`
for the LLVM JIT.  The JIT was removed after measurement showed it
slower than the table interpreter; see `docs/JIT_ANALYSIS.md`.

## References

- [SIMD Intrinsics Guide](https://www.intel.com/content/www/us/en/docs/intrinsics-guide/)
- [Linux perf Examples](https://www.brendangregg.com/perf.html)
