# JIT-compiled keyword tokenizer

## Status

**Experimental — feat/jit-tokenizer branch.  Decision: ABANDON
under the threshold the kill-switch was given, RECONSIDER if the
target workload justifies the cold-start cost.**

The kill-switch protocol asked: does the JIT-compiled trie
classifier beat the existing hash-based `TokenTable`?  The
benchmark (`bench/jit_tokenizer_vs_hash.c`) gives a clear answer:

| metric                          | value         |
|---------------------------------|--------------:|
| keywords compiled               | 85            |
| cold-start (JIT compile)        | **47.2 ms**   |
| hash warm cost                  | 14.4 ns       |
| JIT warm cost                   | 9.5 ns        |
| per-lookup speedup (hash / JIT) | **1.516×**    |
| per-lookup delta                | +4.90 ns      |
| break-even (lookups to amortise compile) | **9.6 M** |

The JIT IS faster per lookup — 1.5× on m3pro NEON.  But the 47 ms
cold-start is a 9.6-million-lookup amortisation cliff.  The
kill-switch threshold I gave was "amortise within 100k lookups";
at 9.6M lookups, that says abandon.

## Why the threshold of 100k may be wrong for some workloads

The 100k threshold assumes a CLI-style usage pattern: short-lived
process, parses a small number of inputs, exits.  For that
workload, hash table is unambiguously better.

For a long-lived database server parsing millions of queries per
day, the picture changes:

- A single SQL query touches 5-50 keyword lookups
- 9.6 M lookups / 25 lookups per query = ~380 k queries to break
  even on cold start
- A busy production DB hits 380 k queries in minutes to hours
- After break-even, the JIT saves ~5 ns per lookup forever

If Lime is targeting embedded / one-shot use (the current default
shape), abandon is correct.  If Lime is targeting long-lived
parser deployments (PG-style), the JIT may be worth it AS AN
OPT-IN, with the 47 ms cost paid at snapshot creation.

## Recommendation

**For v0.3.x: do not integrate.**  The hash table wins for the
common case (short workloads) and the LLVM dependency burden is
already significant for non-JIT users.  Carrying a tokenizer JIT
that doesn't pay back without 9.6 M lookups is hard to justify.

**Reconsider when:**

- A real consumer (PG team, others) reports a workload where
  break-even is reached in production.
- Cold-start cost drops (LLVM upgrades, snapshot caching of
  compiled functions across processes, etc.).
- Per-lookup cost matters more (tokenizer becomes the bottleneck;
  it is not today — see `docs/PERFORMANCE.md`).

If reconsidering, the integration shape is:

```c
/* compile once when the snapshot is built */
token_table_compile_jit(snap->token_table);

/* runtime: token_table_lookup() delegates to JIT if attached */
int code = token_table_lookup(t, name, len);
```

The unit tests in `tests/test_jit_tokenizer_unit.c` already
verify per-keyword equivalence, so the integration is small once
the decision flips.

## What's on this branch

| file | purpose |
|------|---------|
| `src/jit_tokenizer.c`        | restored from `1506723^` |
| `include/jit_tokenizer.h`    | restored from `1506723^` |
| `bench/jit_tokenizer_vs_hash.c` | head-to-head benchmark |
| `tests/test_jit_tokenizer_unit.c` | unit tests for create/destroy/classify/stats/availability |

The JIT tokenizer is **not wired into `TokenTable`** on this
branch.  It compiles, builds an LLVM IR module per call to
`jit_tokenizer_create()`, and classifies correctly.  Integration
into the runtime keyword lookup path is deferred per the
recommendation above.

## Reproducing the measurement

```bash
nix develop --command bash -c '
  meson setup builddir-release --buildtype=release
  ninja -C builddir-release
  ./builddir-release/bench/jit_tokenizer_vs_hash
'
```

Run on m3pro (Apple M3 Pro, NEON), 85 keywords, 60% keyword / 40%
non-keyword workload mix, 10 trials of 10000 iterations each.

## Decision input for the user

This branch is the data, not the decision.  Reasonable calls:

1. **Merge this branch as a benchmark + parking spot for the JIT
   code.**  Keep it building and tested under `feat/jit-tokenizer`
   indefinitely; pull into main when a consumer needs it.
2. **Delete the branch and the code.**  The "no speculative
   features" rule applies; we can always recover from
   `1506723^` again.
3. **Merge as opt-in feature with `token_table_compile_jit()`
   exposed but not called by default.**  Users with the right
   workload shape opt in.

The benchmark stays useful regardless of which path is chosen --
it lets us re-evaluate the call when the workload story changes.
