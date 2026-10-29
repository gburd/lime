# Why Lime has no JIT

Lime used to ship an optional LLVM JIT that compiled the shift-action
lookup of a `ParserSnapshot` into native code.  It was removed in the
v1.11.3 review because, measured on the grammar it was built for, it
made parsing slower on both x86_64 and aarch64 and never paid back its
compile time.  This page records the measurement.

The ahead-of-time `lime -j` path is unaffected: it emits plain C at
generation time and needs no LLVM.

## Method

- **Grammar.** The full PostgreSQL grammar (3,842 states, 557
  terminals, 145k actions).  It was the only grammar in the tree whose
  `nstate x nterminal` (2.14M) cleared the x86_64 cost-model threshold
  (500k), so the only one the JIT actually compiled without forcing it.
- **Paths.** Two snapshots built from the same tables, one JIT-armed.
  Before timing, JIT and table produced identical accept/reject on every
  query.
- **Workload.** The 12-statement accept corpus from
  `tests/test_pg_grammar.c`, pre-tokenised.  Each sample is 20,000
  corpus passes (2.36M tokens, ~31 ms), timed with
  `CLOCK_PROCESS_CPUTIME_ID`.
- **Repetition.** 21 rounds alternating which path runs first; one
  warm-up round discarded; pinned to one core.  Three independent runs
  per machine.  Reported: median ns/token and the min-max of the
  per-round speedup.
- **Build.** `--buildtype=release -Dpg_grammar=true`, plus the
  then-existing `-Dllvm=enabled`; gcc 14.2, LLVM 19.1.7, Debian 13.

The harness is still in the tree as the table-only parse benchmark:
`LIME_PG_BENCH=21 tests/test_pg_grammar`.

## x86_64

AWS c7i.8xlarge (Xeon Platinum 8488C, VM).

| Run | table ns/token | JIT ns/token | JIT speedup (median, min-max) |
|---|---:|---:|---:|
| 1 | 13.2 | 13.8 | 0.958x (0.951-0.967) |
| 2 | 13.0 | 13.8 | 0.942x (0.931-0.955) |
| 3 | 13.2 | 14.1 | 0.939x (0.932-0.943) |

The JIT was 4-6% **slower** in every run; the spread never reached
1.0x.  It cost 16-34 ms to compile, so it never broke even.  On every
smaller grammar the cost model already declined to compile, because on
x86_64 the C compiler's code for the table lookup beat the JIT's.

## aarch64

The cost model JIT-compiled everything on aarch64, on the assumption
that the relationship reversed there.  Same harness on AWS c8g.4xlarge
(Graviton4, Neoverse-V2), Debian 13, LLVM 19, release, pinned core:

| Workload (aarch64) | table | JIT | JIT speedup |
|---|---:|---:|---:|
| PostgreSQL grammar, 3 runs (ns/token) | 16.5-17.0 | 18.1-18.3 | **0.911x / 0.921x / 0.934x** |
| JSON lex+parse, 100k docs x 5, 3 runs (min ms) | 196.4-197.8 | 213.0-216.2 | **~0.91x** |
| arithmetic, 500k parses x 5, 3 runs (min ms) | 170.7-171.5 | 165.5-166.8 | **1.03x** |

7-9% slower on the PostgreSQL grammar and on JSON; 3% faster only on
the 12-rule arithmetic grammar.  The aarch64 premise did not hold for
any realistic grammar measured.

**Not covered:** other LLVM versions; grammars between the tree's small
ones and PostgreSQL's; Apple silicon.

## Cheaper dispatch was already tried

`parse_engine_step` uses direct-`goto` threaded dispatch.  A
computed-goto (`&&label`) jump table was measured and rejected as a
~12% regression on clang (see the comment above the dispatch loop in
`src/parse_engine.c`).  The interpreter was not leaving an easy win for
the JIT to collect.

## What went with it

Four source files and their headers, the LLVM 14-21 compat shim, the
cost-model policy and its background compile thread (which a
PostgreSQL backend cannot host), the `llvm` / `llvm-static` meson
options, three CI jobs, the JIT tests and benches, and the two JIT
fields in `ParserSnapshot` (snapshot ABI version 3).

Earlier releases cited `bench/bench_jit_real_parser` as evidence of a
1.3x JIT win.  It could not show one: both of its scenarios timed the
same AOT-generated `Arith()`, and the JIT was armed on a separate
snapshot that `Arith()` never read.
