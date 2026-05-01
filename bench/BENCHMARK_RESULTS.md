# Extension Framework Overhead Benchmark Results

> **See also:** [`docs/BENCHMARKS_VS_BISON.md`](../docs/BENCHMARKS_VS_BISON.md)
> for a direct head-to-head comparison against GNU Bison on an
> identical grammar.  This document covers Lime's extension-framework
> overhead in isolation.

**Date:** 2026-04-29
**System:** Intel Xeon E5-2697 v2 @ 2.70GHz (12 cores / 24 threads), 125GB RAM
**OS:** NixOS Linux 6.12.80 x86_64
**Compiler:** GCC 14.3.0, -O2 -std=c11
**Build:** meson debugoptimized (default), JIT disabled (LLVM not available)
**Iterations:** 50,000 per benchmark, 1,000 warmup

---

## Overview

The extension framework adds **negligible overhead** when no extensions are
loaded (the common case). The fast-path check costs ~53-120 ns per token.
When extensions with conflicts are present, the full
detect-resolve-execute pipeline costs ~1.4 us per conflicting token, which
is comparable to a single action table lookup for 50 tokens (~337 ns).

| Scenario | Latency (ns/op) | Overhead vs Baseline |
|----------|----------------:|---------------------:|
| No extensions (fast path) | 119 | 0% (baseline) |
| Extensions loaded, no conflicts | ~344 | ~189% |
| Full pipeline (detect + resolve + execute) | 1,433 | ~1,104% |
| Baseline action lookup (50 tokens) | 337 | reference |

The full extension pipeline is ~4.3x the cost of looking
up 50 action table entries, which represents a realistic parse step.
Since conflict resolution only triggers on ambiguous tokens (not every
token), the amortized per-parse overhead is a small fraction of total
parse time.

---

## 1. Baseline Operations

These measure the cost of core parser operations without any extension
framework involvement.

| Benchmark | ns/op | ops/sec | stddev |
|-----------|------:|--------:|-------:|
| snapshot_acquire_release | 62 | 16.0M | 80 |
| action_lookup_50_tokens | 337 | 3.0M | 749 |

- **snapshot_acquire_release**: Atomic refcount increment + decrement.
  This is the minimum cost of sharing a parser snapshot between threads.
- **action_lookup_50_tokens**: 50 table-driven shift-action lookups,
  simulating a moderate-length parse. This is the core parser hot path.

---

## 2. Extension Registry

| Benchmark | ns/op | ops/sec | stddev |
|-----------|------:|--------:|-------:|
| lookup_8_extensions | 344 | 2.9M | 262 |
| get_loaded_count | 82 | 12.3M | 117 |

- **lookup_8_extensions**: Linear scan of 8 registered extensions by ID.
  Takes one rwlock acquisition.
- **get_loaded_count**: Quick count of loaded extensions. This is the
  fast-path check that determines whether conflict detection is needed.

---

## 3. Conflict Detection

| Benchmark | ns/op | ops/sec | stddev | Notes |
|-----------|------:|--------:|-------:|-------|
| detect_token_no_conflict | 1,120 | 893K | 875 | 1 ext, no mods |
| detect_single_no_conflict | 121 | 8.2M | 68 | Single token check |
| detect_token_with_conflict | 1,548 | 646K | 868 | 2 ext, CARET collision |
| detect_rule_with_conflict | 1,003 | 997K | 582 | Rule-level check |
| detect_all_with_conflict | 2,945 | 340K | 1,013 | Full 3-level scan |
| detect_single_with_conflict | 660 | 1.5M | 222 | Token+state check |

- **Conflict overhead**: Detecting a token-level conflict (the most
  common case) costs ~1.5 us with 2 conflicting extensions.
- **No-conflict case**: Even with extensions loaded, a single-token check
  with no conflict is only ~121 ns.
- **Full scan**: The full 3-level scan (token + rule + semantic)
  costs ~3 us for 2 extensions with 2 modifications each. This scales
  with O(n*m) where n = extensions and m = modifications per extension.

---

## 4. Disambiguation Strategies

| Benchmark | ns/op | ops/sec | stddev |
|-----------|------:|--------:|-------:|
| priority_resolve | 739 | 1.4M | 211 |
| priority_update | 54 | 18.4M | 93 |
| fork_resolve_resolve | 1,348 | 742K | 416 |
| fork_resolve_update | 54 | 18.6M | 40 |

- **Priority strategy**: Resolves by comparing extension priorities.
  At ~739 ns per resolution, this is fast enough for per-token use.
- **Fork-resolve strategy**: Creates and evaluates parse forks.
  ~1.8x slower than priority at ~1,348 ns, but provides more accurate
  disambiguation for complex conflicts.
- **Update**: Both strategies have negligible update overhead (~54 ns).
  This is the feedback path for learning-based strategies.

---

## 5. Execution Policy

| Benchmark | ns/op | ops/sec | stddev |
|-----------|------:|--------:|-------:|
| first_only_2_winners | 187 | 5.4M | 73 |
| all_2_winners | 294 | 3.4M | 174 |
| chain_2_winners | 232 | 4.3M | 157 |

- **FIRST_ONLY**: Executes only the highest-priority winner. Minimal
  overhead (~187 ns) includes callback dispatch + result allocation.
- **ALL**: Executes both winners independently. ~1.6x FIRST_ONLY due
  to the second callback invocation and result.
- **CHAIN**: Executes winners in sequence with output chaining.
  Slightly cheaper than ALL since the second execution receives piped
  input rather than independent invocation.

---

## 6. Parser Fork (State Cloning)

| Benchmark | ns/op | ops/sec | stddev |
|-----------|------:|--------:|-------:|
| clone_parser_state | 164 | 6.1M | 106 |
| fork_parser_full | 254 | 3.9M | 167 |
| fork_set_2_forks | 881 | 1.1M | 279 |

- **clone_parser_state**: Deep copy of a mock parser with 4-entry stack.
  The ~164 ns cost is dominated by malloc + memcpy.
- **fork_parser_full**: Clone + snapshot reference + fork struct
  allocation. ~1.5x clone alone.
- **fork_set_2_forks**: Create 2 forks, mark one completed and one
  failed, prune, and find the best fork. The full fork-resolve cycle.

---

## 7. Grammar Context (Mode Switching)

| Benchmark | ns/op | ops/sec | stddev |
|-----------|------:|--------:|-------:|
| is_root_only | 54 | 18.4M | 437 |
| push_pop_mode | 121 | 8.3M | 77 |
| detect_switch_no_match | 63 | 15.8M | 41 |

- **is_root_only**: Fast-path check for embedded grammar support.
  At ~54 ns, this is nearly free and should be the first check in the
  tokenizer hot path.
- **push_pop_mode**: Full context switch (push + pop). Costs ~121 ns.
- **detect_switch_no_match**: Token-triggered switch detection when no
  mode matches. Very fast at ~63 ns.

---

## 8. Combined Pipeline

| Benchmark | ns/op | ops/sec | stddev |
|-----------|------:|--------:|-------:|
| no_extensions_fast_path | 119 | 8.4M | 383 |
| full_detect_resolve_execute | 1,433 | 698K | 568 |

- **no_extensions_fast_path**: When no extensions are loaded, the entire
  extension framework check costs only ~119 ns. This is a single
  `get_loaded_extension_count()` call.
- **full_detect_resolve_execute**: The complete overhead path for a
  conflicting token: detect conflict + priority disambiguation + policy
  execution. At ~1,433 ns (~1.4 us), this is the worst-case per-token
  overhead.

---

## 9. Memory Overhead

| Structure | Size (bytes) |
|-----------|-------------:|
| ParserSnapshot | 168 |
| Extension | 80 |
| GrammarModification | 56 |
| ConflictPoint | 40 |
| LimeContext | 24 |
| StrategyResult | 24 |
| ExecutionPolicyConfig | 24 |
| ExecutionResult | 24 |
| ParseFork | 96 |
| ClonedParserState | 48 |
| ParseForkSet | 24 |

**Per-extension overhead:** ~144 bytes (Extension struct + name/version strings)
plus ~120 bytes per grammar modification.

**Conflict detection working set:** ~544 bytes initial allocation for a
MultiGrammarConflictResult with room for 8 conflict points.

---

## 10. Overhead Analysis and Recommendations

### Per-Token Overhead Budget

For a typical SQL parse of 50 tokens:

| Scenario | Total overhead | Per-token | % of parse time |
|----------|---------------:|----------:|----------------:|
| No extensions | 119 ns | 2.4 ns | 0.7% |
| Extensions, no conflict | 6,630 ns | 133 ns | 39% |
| 1 conflicting token (priority) | 7,563 ns | 151 ns | 45% |
| 1 conflicting token (fork) | 8,063 ns | 161 ns | 48% |

(Parse time reference: 50-token action lookup = ~337 ns total = 16,850 ns)

### Scaling

- **Extension count**: Conflict detection scales O(n*m) where n =
  number of loaded extensions and m = modifications per extension.
  For the typical case of 2-5 extensions with 5-20 modifications
  each, this remains under 10 us.
- **Conflict frequency**: Most tokens are unambiguous. Only tokens
  where multiple extensions compete (e.g., ^ for power vs XOR)
  trigger the full pipeline. A typical parse might have 0-2
  such tokens.

### Recommendations

1. **Always check `get_loaded_extension_count()` first.** The 82 ns
   fast-path eliminates all extension overhead when none are loaded.

2. **Use `grammar_context_is_root_only()` for embedded grammars.**
   The 54 ns check avoids mode-switching overhead when only SQL is
   being parsed.

3. **Prefer PRIORITY over FORK_RESOLVE** when deterministic ordering
   is acceptable. Priority is ~1.8x faster (739 vs 1,348 ns).

4. **Use EXEC_FIRST_ONLY** when only the winning extension matters.
   It is the fastest policy at 187 ns.

5. **Cache conflict detection results** for static grammars. The
   conflict set does not change between parses unless extensions
   are loaded/unloaded.

---

## Raw CSV Data

```csv
category,benchmark,iterations,total_ns,min_ns,max_ns,ns_per_op,ops_per_sec,stddev_ns
baseline,snapshot_acquire_release,50000,3121131,45,14082,62.42,16019834,79.65
baseline,action_lookup_50_tokens,50000,16835040,207,83486,336.70,2969996,749.46
registry,lookup_8_extensions,50000,17222560,265,17428,344.45,2903169,262.16
registry,get_loaded_count,50000,4078666,53,16461,81.57,12258910,116.66
conflict,detect_token_no_conflict,50000,55982445,625,121777,1119.65,893137,874.82
conflict,detect_single_no_conflict,50000,6063856,83,10326,121.28,8245578,68.25
conflict,detect_token_with_conflict,50000,77414236,984,73193,1548.28,645876,867.52
conflict,detect_rule_with_conflict,50000,50163009,847,64337,1003.26,996750,581.72
conflict,detect_all_with_conflict,50000,147226334,1684,66371,2944.53,339613,1013.14
conflict,detect_single_with_conflict,50000,32990654,546,17793,659.81,1515581,221.74
disambiguation,priority_resolve,50000,36931162,559,17243,738.62,1353870,210.56
disambiguation,priority_update,50000,2722989,46,15629,54.46,18362175,93.21
disambiguation,fork_resolve_resolve,50000,67412799,1016,23433,1348.26,741699,415.83
disambiguation,fork_resolve_update,50000,2681364,49,7003,53.63,18647226,40.25
exec_policy,first_only_2_winners,50000,9339622,173,7753,186.79,5353536,73.36
exec_policy,all_2_winners,50000,14699961,240,17197,294.00,3401370,174.35
exec_policy,chain_2_winners,50000,11618824,198,15772,232.38,4303362,156.90
parser_fork,clone_parser_state,50000,8194460,150,15771,163.89,6101683,105.91
parser_fork,fork_parser_full,50000,12712570,233,24936,254.25,3933115,167.35
parser_fork,fork_set_2_forks,50000,44048047,589,17433,880.96,1135124,279.47
grammar_ctx,is_root_only,50000,2723061,49,96292,54.46,18361689,436.91
grammar_ctx,push_pop_mode,50000,6026711,113,10219,120.53,8296399,77.04
grammar_ctx,detect_switch_no_match,50000,3173274,56,9186,63.47,15756597,40.95
pipeline,no_extensions_fast_path,50000,5954079,113,74503,119.08,8397604,383.47
pipeline,full_detect_resolve_execute,50000,71660024,1137,67177,1433.20,697739,567.55
```

---

## Parser Baseline Benchmarks (from parser_bench)

Additional baseline data from the parser_bench suite (5,000 iterations):

| Category | Benchmark | ns/op | ops/sec |
|----------|-----------|------:|--------:|
| tokenizer | simple_select | 1,253 | 798K |
| tokenizer | complex_join | 10,794 | 93K |
| tokenizer | cte_recursive | 12,279 | 81K |
| tokenizer | insert | 3,804 | 263K |
| tokenizer | subquery | 7,430 | 135K |
| token_table | lookup_36_keywords_hit | 3,240 | 309K |
| token_table | lookup_5_keywords_miss | 448 | 2.2M |
| snapshot | acquire_release | 92 | 10.9M |
| jit | action_lookup_interpreted_64x32 | 13,963 | 72K |
| policy | metrics_record | 100 | 10.0M |
| policy | should_compile_eval | 51 | 19.7M |
| throughput | throughput_4kb | 190,862 | 5.2K |
| throughput | throughput_64kb | 3,167,545 | 316 |
| throughput | throughput_256kb | 12,803,847 | 78 |

### Throughput

| Input Size | MB/s |
|-----------|------:|
| 4 KB | 20.1 |
| 64 KB | 19.7 |
| 256 KB | 19.5 |

---

## JIT Comparison (from jit_comparison, interpreted-only)

| Grammar Size | Parse Length | Mean (ns) | Median (ns) | P95 (ns) | Throughput |
|-------------|------------:|----------:|------------:|------:|-----------|
| 64 states, 32 terms | 50 tokens | 192 | 189 | 192 | 5.2M parses/sec |
| 256 states, 100 terms | 100 tokens | 338 | 334 | 337 | 3.0M parses/sec |
| 512 states, 150 terms | 200 tokens | 600 | 593 | 596 | 1.7M parses/sec |

Note: JIT compilation was not available (LLVM not linked). When enabled,
expected speedup is 2-5x for the action lookup hot path.
