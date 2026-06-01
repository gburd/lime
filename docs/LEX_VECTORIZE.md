# Lime C-Emit `--lex-vectorize`: Multiversion-at-Tokenize SIMD

This document covers the C-emit's SIMD architecture, the parallel
of [`docs/RUST_BENCHMARK.md`](RUST_BENCHMARK.md) for the Rust
side's `--rustlex-simd` work.

## Summary

`lime -X --lex-vectorize` (default ON since v0.8.10) emits a
multiversion-at-tokenize SIMD architecture in the generated C
lexer: per-state fast-path scan helpers in AVX2 / NEON / scalar
variants, plus a public `<prefix>_match()` that runtime-dispatches
once via `__builtin_cpu_supports("avx2")` on x86_64.

`lime -X --lex-no-vectorize` falls back to the legacy single-
function scalar driver.  The public symbol is the same; the
public ABI is unchanged.

## Architecture

Mirror of the Rust `--rustlex-simd` chain (see
[`src/lex/emit_rust_lex.c::emit_simd_helpers`](../src/lex/emit_rust_lex.c)):

```
<prefix>_match()                        (public dispatcher)
  -> __builtin_cpu_supports("avx2") ?
       <prefix>_match_avx2(...)         (target("avx2"))
       : <prefix>_match_scalar(...)     (no attr)

<prefix>_match_avx2:
  for each input byte:
    <prefix>_fast_path_<NAME>_avx2()    (always_inline, target avx2)
      -> <prefix>_scan_<NAME>_<S>_avx2  (always_inline, target avx2)
         -> _mm256_loadu_si256
            _mm256_cmpeq_epi8
            _mm256_or_si256
            _mm256_movemask_epi8
            __builtin_ctz
```

Every helper in the AVX2 chain carries `always_inline` and the
chain is monotone-increasing in target features, so
`<prefix>_match_avx2`'s body ends up containing the AVX2
intrinsics directly without crossing a `target_feature` inlining
barrier.  Verified via `objdump -d` on the JSON grammar:
`vmovdqu` / `vpcmpeqb` / `vpor` / `vpmovmskb` appear inline in
`Json_match_avx2`'s body, while `Json_match_scalar` contains zero
AVX2 instructions.

## Fast-path classification

A DFA state qualifies for SIMD scan-until-exit when:

  - At least 240 of 256 byte values self-loop (stay in the state)
  - The exit set has 1, 2, or 3 distinct bytes leading elsewhere

These thresholds match the Rust emit.  The Rust side caps at 8
exit bytes via `memchr_iter`; the C side caps at 3 to keep the
AVX2 compare-or chain short and the scalar tail predicate
compact.  States outside this band fall through to the per-byte
step loop (still correct, just no SIMD acceleration).

For the JSON grammar the only qualifying state is the string-
body state (self-loops on every byte except `"` and `\`).  That
single state covers the bulk of real-world JSON tokenize time,
because long string runs let AVX2's 32-byte chunks fire many
times per token before hitting an exit byte.

## Measured numbers

Intel i9-12900H, GCC 15.2 (-O3), schema-driven 1MB and 5MB
fixtures from `bench/rust_compare/make_fixture_v2.py`.  Best of
3 runs after 10 warmup iterations.

### 1MB fixtures

| shape    | lex+simd MB/s | lex+scalar MB/s | ratio  |
|----------|--------------:|----------------:|-------:|
| prose    |        17,156 |             537 |  32.0x |
| product  |         1,477 |             445 |   3.3x |
| tweet    |           793 |             403 |   2.0x |
| api_log  |           769 |             402 |   1.9x |
| metric   |           376 |             384 |   0.98x |

### 5MB fixtures

| shape    | lex+simd MB/s | lex+scalar MB/s | ratio  |
|----------|--------------:|----------------:|-------:|
| prose    |        15,786 |             531 |  29.7x |
| product  |         1,433 |             426 |   3.4x |
| tweet    |           776 |             400 |   1.9x |
| api_log  |           762 |             397 |   1.9x |
| metric   |           368 |             379 |   0.97x |

## Comparison with the Rust side's wins

The Rust side's `--rustlex-simd` measured a 1.97x win on prose
(685 vs 348 MB/s) at 1MB; the C side measures 32x.  Why such a
different ratio?

The C side's scalar baseline is genuinely slow (~500 MB/s on
prose) because GCC -O3 does not auto-vectorise the unified DFA
loop.  The loop body is a state-indexed switch around a
`trans[s][byte]` table lookup; GCC's auto-vectoriser does not
recognise this as a vectorisable pattern.

The Rust side's scalar baseline is faster (~348 MB/s) because
LLVM occasionally manages to vectorise simpler patterns, and
because Rust's `iter().position(|b| b == X || b == Y)` lowers to
something less obviously serial than the C `for (i; i<n; i++)
switch` loop.

In both cases the SIMD path explicitly vectorises and gets
roughly the same absolute throughput.  The ratio differs because
the baselines differ.

The architectural finding stands either way: explicit SIMD via
the multiversion-at-tokenize emit BEATS what the C compiler
generates for the unified-DFA loop on prose-shape content.

### Negative finding: metric shape

The metric shape (heavy on numbers, sparse strings) shows a
~2-3% regression with SIMD on.  That's the per-iteration fast-
path-dispatch overhead with no string body to scan past.  The
loss is small enough that flipping `--lex-vectorize` to ON by
default is the right call, but consumers parsing exclusively
metric-shape JSON can opt out via `--lex-no-vectorize` if their
profile shows the lexer as the bottleneck.

## Reproducing

```bash
# Generate fixtures (gitignored; regenerate as needed)
mkdir -p /tmp/lex-vec-fixtures
for shape in prose product tweet api_log metric; do
    for size in 1 5; do
        python3 bench/rust_compare/make_fixture_v2.py \
            --shape $shape --size-mb $size \
            --out /tmp/lex-vec-fixtures/fixture_${shape}_${size}mb.json
    done
done

# Build (release flags, full optimisation)
unset TMPDIR
nix --extra-experimental-features 'nix-command flakes' develop \
    --command bash -c "
        meson setup --buildtype=release build-bench-rel
        ninja -C build-bench-rel \
            bench/lex_vectorize/bench_lex_vec \
            bench/lex_vectorize/bench_lex_novec
    "

# Bench
for shape in prose product tweet api_log metric; do
    for size in 1 5; do
        echo "=== shape=$shape size=${size}MB ==="
        build-bench-rel/bench/lex_vectorize/bench_lex_vec \
            /tmp/lex-vec-fixtures/fixture_${shape}_${size}mb.json
        build-bench-rel/bench/lex_vectorize/bench_lex_novec \
            /tmp/lex-vec-fixtures/fixture_${shape}_${size}mb.json
    done
done
```

## Cross-platform behaviour

  - **x86_64 + gcc/clang**: AVX2 path emitted, runtime-detected.
    Falls back to scalar at runtime when AVX2 is unavailable.
  - **aarch64 + gcc/clang**: NEON path emitted, used
    unconditionally (NEON is mandatory on aarch64).
  - **Anything else** (RISC-V, plain x86, MSVC, etc.): the
    `#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))`
    guards skip the SIMD blocks entirely; only `<prefix>_match_scalar`
    is emitted, and `<prefix>_match()` calls it directly.

The default ON is safe because the emitted code always compiles:
the `#ifdef` guards collapse the AVX2 / NEON code to nothing on
unsupported targets without a build error.

## Why the C compiler's auto-vectoriser doesn't close this gap

Three reasons, each independently sufficient:

1. **The DFA loop body is a switch + table lookup.**  GCC's
   auto-vectoriser doesn't unroll
   `for (i = 0; i < n; i++) switch (state) { case 0: next =
   T[s][bytes[i]]; ... }` into 32-byte parallel work.  The
   dependency on `s` (mutated each iteration) plus the indirect
   table load defeats the pattern matchers.

2. **The fast-path scan loop's predicate is an OR of byte
   equalities.**  GCC CAN sometimes vectorise this on 64-bit
   word loads, but the result is much slower than explicit AVX2
   (no `pmovmskb`-equivalent compress, no `_mm256_or_si256`
   stage).  The explicit emit measures 30x faster on prose
   precisely because it bypasses the auto-vectoriser's
   conservative output.

3. **`__attribute__((target("avx2")))` is a hard inlining
   barrier in gcc/clang** — a function carrying it cannot be
   inlined into a caller that doesn't.  The pre-v0.8.10 Rust
   emit (and any naive C-side approach that puts target("avx2")
   on the leaf scan helper without lifting it to the entire
   match function) ends up with every fast-path scan crossing a
   target_feature inlining barrier.  v0.8.10 lifts the AVX2
   target up to `<prefix>_match_avx2` so every always_inline
   helper down the chain inherits AVX2 and inlines without
   crossing the barrier.

## Disabling

```bash
# Per-invocation:
lime -X --lex-no-vectorize foo.lex

# In a meson custom_target:
custom_target('foo_lex',
  input   : ['foo.lex'],
  output  : ['foo_lex.c', 'foo_lex.h'],
  command : [lime_exe, '-X', '--lex-no-vectorize', '-d@OUTDIR@', '@INPUT@'],
)
```

The `--lex-no-vectorize` emit is a regression check: if a future
change breaks the vectorised emit on a platform you can't easily
reproduce on, falling back to the scalar emit is one CLI flag
away.
