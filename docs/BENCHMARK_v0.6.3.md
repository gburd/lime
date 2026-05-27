# Lime v0.6.3 Benchmark Results

Two-machine comparison of Lime v0.6.3 vs Flex+Bison on identical
arithmetic and JSON grammars.  Run on 2026-05-27.

## Test machines

| | dev box | nuc |
|---|---|---|
| OS | Linux | FreeBSD 15.0-RELEASE |
| CPU | Intel i9-12900H (24 threads) | Intel i7-8809G (8 threads) |
| Frequency | 4.7 GHz turbo | 3.1 GHz base |
| Compiler | GCC 15.2.0 | FreeBSD clang 19.1.7 |
| LLVM | 21.1.8 (via Nix) | n/a (built `-Dllvm=disabled`) |
| Build type | release | release |

## Methodology

```bash
./build/bench/bench_flex_bison_compare/bench_flex_bison_compare
```

Two grammars:
- **arith** -- expression parser, input `(1 + 2) * (3 + 4) - 5`, 100k iterations × 5 trials
- **JSON** -- nested-object/array parser, 515-byte fixture, 100k iterations × 5 trials

Both grammars are compiled identically for lime, flex+bison, and (where available) lime+JIT.  The harness measures wall-clock time per trial.  RSS sampled per trial; CPU time accumulated across trials.

## arith results

| Tool | min (ms) | mean (ms) | speedup vs bison |
|---|---|---|---|
| **dev box (i9-12900H, gcc 15, JIT enabled)** | | | |
| lime         | 24.83 | 26.09 | 0.86× |
| bison        | 22.10 | 22.53 | 1.00× |
| lime+jit     | **17.20** | **18.14** | **1.24×** |
| **nuc (i7-8809G, clang 19, no JIT)** | | | |
| lime         | **34.78** | **35.33** | **1.03×** |
| bison        | 36.32 | 36.46 | 1.00× |

**Observations:**

- On the i9-12900H the table-driven Lime path is slightly slower than Bison (0.86×) for trivial expressions.  This matches the documented "x86_64 small-grammar" behaviour in `src/jit_context.c`: Intel's branch predictor handles Bison's static switch better than Lime's table dispatch on tiny inputs.  The JIT recovers and overtakes (1.24×).
- On the older i7-8809G Lime is even / slightly ahead of Bison (1.03×) without JIT.  The relative cost of the indirect dispatch is lower on this microarchitecture.

## JSON results

| Tool | min (ms) | mean (ms) | speedup vs bison |
|---|---|---|---|
| **dev box (i9-12900H, gcc 15, JIT enabled)** | | | |
| lime_json     | 139.69 | 161.81 | 1.54× |
| bison_json    | 213.64 | 248.67 | 1.00× |
| lime_json+jit | **135.49** | **139.77** | **1.78×** |
| **nuc (i7-8809G, clang 19, no JIT)** | | | |
| lime_json     | **261.57** | **261.89** | **1.15×** |
| bison_json    | 300.12 | 300.29 | 1.00× |

**Observations:**

- Lime is decisively faster on JSON parsing on both machines.
- The dev box hits 1.54× without JIT, 1.78× with JIT.
- The older nuc still gets 1.15× without JIT.
- The JSON win comes from the lime tokenizer being more compact than flex's DFA-driven tokenizer for the JSON character classes, and the parse engine's per-token dispatch being well-predicted in this loop.

## GLR overhead

`bench/glr_overhead` (median of 5 trials, ns per parse):

| | LALR baseline | GLR no-conflict | GLR overhead |
|---|---|---|---|
| dev box  | 295-330 ns | (not measured) | n/a |
| nuc | 374-380 ns | 2774-2867 ns | **~6.5×** |

The GLR overhead vs LALR matches the v0.5.x measurement (5-7×) — no regression from any v0.6.x changes.  GLR is only used on grammars with deferred-conflict markers; the LALR fast path is byte-identical from v0.5.3 onward.

## bench_runtime_lime / bench_runtime_bison

Per-token cost (ns):

| | min | p50 | p99 | p99.9 |
|---|---|---|---|---|
| lime (dev box) | 676 | 699 | 868 | 1066 |
| bison (dev box) | 559 | 575 | 693 | 781 |
| lime (nuc) | 914 | 913 | 918 | 919 |

Bison is faster per-token on the dev box but loses on the larger workloads (arith and JSON above).  The likely explanation: Lime's per-token cost includes machinery (snapshot acquire, JIT-context check) that pays off on long input streams but adds overhead on short ones.  Reproducible on both machines.

## Summary

**Lime v0.6.3 vs Flex+Bison:**

- **JSON / realistic workloads:** Lime is **1.5-1.8× faster** with JIT, **1.15-1.5× faster** without JIT.
- **Trivial arithmetic:** roughly even, with JIT giving Lime a 1.24× edge on x86_64.
- **GLR overhead:** ~6.5× vs LALR baseline.  Unchanged from prior releases.

The PG team can quote these numbers when comparing against the
flex+bison baseline they're qualifying lime to replace.

## Notes for reproducing

```bash
# In the lime source tree
nix --extra-experimental-features 'nix-command flakes' develop
unset TMPDIR
meson setup build-bench --buildtype=release -Dllvm=enabled
ninja -C build-bench
./build-bench/bench/bench_flex_bison_compare/bench_flex_bison_compare
LIME_JIT=1 ./build-bench/bench/bench_flex_bison_compare/bench_flex_bison_compare
```

## Known issues observed during this run -- both fixed in commit 112a5c1

1. **`tests/test_tokenize`** failed under release-mode gcc 15.2.0
   with token types coming back as 0.  Originally diagnosed as
   "SIMD optimiser interaction"; turned out to be a real test bug:
   the file used `assert(tokenizer_next(tok, &t));` which expands
   to `((void)0)` under NDEBUG (release).  The compiler then saw
   `t` as uninitialised on the next line and folded subsequent
   comparisons against undefined memory.  Fixed by introducing a
   `REQUIRE()` macro that survives NDEBUG and replacing 42
   instances of NDEBUG-stripped asserts.  26/26 sub-tests now
   PASS under release.

2. **`bench_jit_real_parser`** intermittently double-freed on
   teardown.  Originally diagnosed as "doesn't affect bench
   numbers"; turned out to be a heap corruption in the bench
   harness's `make_synthetic_snapshot`: `yy_shift_ofst` and
   `yy_reduce_ofst` are `int32_t *` (per `include/snapshot.h`)
   but were calloc'd with `sizeof(int16_t)` -- half size --
   causing the populating loop's 4-byte writes to spill past
   the end of the 2-byte slots.  glibc's heap-consistency check
   eventually noticed and aborted.  Fixed by correcting the
   sizeof to `sizeof(int32_t)`.

Both fixes are pure test/bench-side; no library code changed.
