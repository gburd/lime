/*
** Comprehensive JIT vs Interpreted Parser Benchmark
**
** This benchmark specifically compares the performance of LLVM JIT-compiled
** parser vs the interpreted table-driven parser. It includes:
**   - Proper warmup phase to ensure JIT compilation completes
**   - Multiple iterations with statistical analysis
**   - Various grammar sizes to test scalability
**   - Realistic parse workloads
**
** Expected results:
**   - Interpreted: ~1-10µs per parse (baseline)
**   - JIT (cold): Higher initial overhead due to compilation
**   - JIT (warm): 2-5x faster than interpreted after amortization
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lime_time.h"
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include "parser.h"
#include "jit_context.h"
#include "jit_policy.h"
#include "snapshot.h"

/* ------------------------------------------------------------------ */
/*  Timing utilities                                                  */
/* ------------------------------------------------------------------ */

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ------------------------------------------------------------------ */
/*  Statistics                                                        */
/* ------------------------------------------------------------------ */

typedef struct Stats {
    uint64_t n;
    double mean;
    double min;
    double max;
    double stddev;
    double median;
    double p95;
    double p99;
} Stats;

static int compare_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

static Stats compute_stats(double *values, uint64_t n) {
    Stats s = {0};
    s.n = n;

    if (n == 0) return s;

    /* Mean, min, max */
    s.mean = 0.0;
    s.min = values[0];
    s.max = values[0];

    for (uint64_t i = 0; i < n; i++) {
        s.mean += values[i];
        if (values[i] < s.min) s.min = values[i];
        if (values[i] > s.max) s.max = values[i];
    }
    s.mean /= n;

    /* Standard deviation */
    double variance = 0.0;
    for (uint64_t i = 0; i < n; i++) {
        double diff = values[i] - s.mean;
        variance += diff * diff;
    }
    s.stddev = sqrt(variance / n);

    /* Sort for percentiles */
    double *sorted = malloc(n * sizeof(double));
    memcpy(sorted, values, n * sizeof(double));
    qsort(sorted, n, sizeof(double), compare_double);

    s.median = sorted[n / 2];
    s.p95 = sorted[(size_t)(n * 0.95)];
    s.p99 = sorted[(size_t)(n * 0.99)];

    free(sorted);
    return s;
}

static void print_stats(const char *label, const Stats *s) {
    printf("%s:\n", label);
    printf("  Samples:  %lu\n", (unsigned long)s->n);
    printf("  Mean:     %.2f ns\n", s->mean);
    printf("  Median:   %.2f ns\n", s->median);
    printf("  Min:      %.2f ns\n", s->min);
    printf("  Max:      %.2f ns\n", s->max);
    printf("  StdDev:   %.2f ns (%.1f%%)\n", s->stddev,
           (s->mean > 0 ? 100.0 * s->stddev / s->mean : 0.0));
    printf("  P95:      %.2f ns\n", s->p95);
    printf("  P99:      %.2f ns\n", s->p99);
}

/* ------------------------------------------------------------------ */
/*  Snapshot creation helpers                                         */
/* ------------------------------------------------------------------ */

/*
** Create a realistic snapshot for benchmarking.
** Simulates a moderate SQL grammar with the given number of states
** and terminals.
*/
static ParserSnapshot *create_benchmark_snapshot(uint32_t nstates,
                                                  uint32_t nterminals,
                                                  const char *label) {
    ParserSnapshot *snap = calloc(1, sizeof(ParserSnapshot));
    if (!snap) return NULL;

    atomic_init(&snap->refcount, 1);
    snap->version = 1;
    snap->nstate = nstates;
    snap->nterminal = nterminals;
    snap->nsymbol = nterminals + 50; /* terminals + non-terminals */

    /* Allocate action tables */
    uint32_t table_size = nstates * nterminals;
    snap->action_count = table_size;
    snap->lookahead_count = table_size;

    snap->yy_action = calloc(table_size, sizeof(uint16_t));
    snap->yy_lookahead = calloc(table_size, sizeof(uint16_t));
    snap->yy_shift_ofst = calloc(nstates, sizeof(int16_t));
    snap->yy_reduce_ofst = calloc(nstates, sizeof(int16_t));
    snap->yy_default = calloc(nstates, sizeof(uint16_t));

    if (!snap->yy_action || !snap->yy_lookahead ||
        !snap->yy_shift_ofst || !snap->yy_reduce_ofst ||
        !snap->yy_default) {
        snapshot_release(snap);
        return NULL;
    }

    /* Fill with realistic patterns */
    for (uint32_t s = 0; s < nstates; s++) {
        /* 70% of states have shift actions, 30% default to reduce */
        if (s % 10 < 7) {
            snap->yy_shift_ofst[s] = (int16_t)(s * nterminals);

            /* Populate lookahead/action entries */
            for (uint32_t t = 0; t < nterminals; t++) {
                uint32_t idx = s * nterminals + t;

                /* 80% of lookaheads match, 20% default */
                if (t % 5 != 0) {
                    snap->yy_lookahead[idx] = (uint16_t)t;
                    snap->yy_action[idx] = (uint16_t)(s * 10 + t);
                } else {
                    snap->yy_lookahead[idx] = 0xFFFF; /* invalid */
                    snap->yy_action[idx] = 0;
                }
            }
        } else {
            snap->yy_shift_ofst[s] = -1; /* no shift table */
        }

        snap->yy_default[s] = (uint16_t)(1000 + s);
    }

    snap->jit_ctx = NULL;

    printf("\n[%s] Created snapshot: %u states, %u terminals\n",
           label, nstates, nterminals);
    printf("[%s] Action table size: %u entries (%.1f KB)\n",
           label, table_size, (table_size * sizeof(uint16_t) * 2) / 1024.0);

    return snap;
}

/* ------------------------------------------------------------------ */
/*  Parse simulation                                                  */
/* ------------------------------------------------------------------ */

/*
** Simulate a parse by performing action table lookups.
** Uses the batch API for maximum performance.
*/
static void simulate_parse(ParserSnapshot *snap,
                          uint16_t *lookaheads,
                          uint32_t nlookaheads) {
    uint16_t state = 0;

    /* Call batch parse function (uses JIT if available, interpreted otherwise) */
    jit_parse_batch(snap, lookaheads, nlookaheads, &state);
}

/*
** Generate a realistic sequence of lookahead tokens for parsing.
*/
static uint16_t *generate_lookahead_sequence(uint32_t nterminals,
                                             uint32_t length) {
    uint16_t *seq = malloc(length * sizeof(uint16_t));
    if (!seq) return NULL;

    /* Generate a mix of common and rare terminals */
    for (uint32_t i = 0; i < length; i++) {
        /* 80% common terminals (first 20%), 20% rare */
        if (i % 5 != 0) {
            seq[i] = (uint16_t)(i % (nterminals / 5));
        } else {
            seq[i] = (uint16_t)(nterminals / 2 + (i % (nterminals / 2)));
        }
    }

    return seq;
}

/* ------------------------------------------------------------------ */
/*  Benchmark runner                                                  */
/* ------------------------------------------------------------------ */

typedef struct BenchConfig {
    uint32_t nstates;
    uint32_t nterminals;
    uint32_t parse_length;     /* Number of tokens per parse */
    uint32_t warmup_parses;    /* Warmup iterations */
    uint32_t bench_parses;     /* Benchmark iterations */
} BenchConfig;

static void run_comparison_benchmark(const BenchConfig *cfg) {
    printf("\n");
    printf("=================================================================\n");
    printf("  Grammar: %u states, %u terminals\n",
           cfg->nstates, cfg->nterminals);
    printf("  Parse length: %u tokens\n", cfg->parse_length);
    printf("  Warmup: %u parses, Benchmark: %u parses\n",
           cfg->warmup_parses, cfg->bench_parses);
    printf("=================================================================\n");

    /* Create snapshot */
    ParserSnapshot *snap = create_benchmark_snapshot(
        cfg->nstates, cfg->nterminals, "SETUP");
    if (!snap) {
        printf("ERROR: Failed to create snapshot\n");
        return;
    }

    /* Generate parse input */
    uint16_t *lookaheads = generate_lookahead_sequence(
        cfg->nterminals, cfg->parse_length);
    if (!lookaheads) {
        printf("ERROR: Failed to generate lookaheads\n");
        snapshot_release(snap);
        return;
    }

    /* ============================================================== */
    /*  INTERPRETED (TABLE-DRIVEN) BASELINE                          */
    /* ============================================================== */

    printf("\n--- INTERPRETED (TABLE-DRIVEN) ---\n");

    /* Warmup */
    for (uint32_t i = 0; i < cfg->warmup_parses; i++) {
        simulate_parse(snap, lookaheads, cfg->parse_length);
    }

    /* Benchmark */
    double *interp_times = malloc(cfg->bench_parses * sizeof(double));
    uint64_t total_interp_ns = 0;

    for (uint32_t i = 0; i < cfg->bench_parses; i++) {
        uint64_t start = now_ns();
        simulate_parse(snap, lookaheads, cfg->parse_length);
        uint64_t elapsed = now_ns() - start;

        interp_times[i] = (double)elapsed;
        total_interp_ns += elapsed;
    }

    Stats interp_stats = compute_stats(interp_times, cfg->bench_parses);
    print_stats("Interpreted", &interp_stats);

    printf("\nInterpreted throughput: %.0f parses/sec\n",
           cfg->bench_parses / ((double)total_interp_ns / 1e9));

    /* ============================================================== */
    /*  JIT COMPILATION                                              */
    /* ============================================================== */

    printf("\n--- JIT COMPILATION ---\n");

    JITStatus jit_status = jit_attach_to_snapshot(snap);

    if (jit_status != JIT_OK) {
        printf("JIT compilation not available: %s\n",
               jit_status_string(jit_status));
        printf("Skipping JIT benchmark.\n");

        free(interp_times);
        free(lookaheads);
        snapshot_release(snap);
        return;
    }

    printf("JIT compilation successful!\n");

    if (snap->jit_ctx) {
        JITStats jit_stats = jit_get_stats((JITContext *)snap->jit_ctx);
        printf("  Compiled: %u/%u states\n",
               jit_stats.states_compiled, jit_stats.states_total);
        printf("  Compile time: %.2f ms\n",
               jit_stats.compile_time_ns / 1e6);
        printf("  Code size: %zu bytes\n", jit_stats.code_size_bytes);
    }

    /* Warmup with JIT */
    for (uint32_t i = 0; i < cfg->warmup_parses; i++) {
        simulate_parse(snap, lookaheads, cfg->parse_length);
    }

    /* Benchmark JIT */
    double *jit_times = malloc(cfg->bench_parses * sizeof(double));
    uint64_t total_jit_ns = 0;

    for (uint32_t i = 0; i < cfg->bench_parses; i++) {
        uint64_t start = now_ns();
        simulate_parse(snap, lookaheads, cfg->parse_length);
        uint64_t elapsed = now_ns() - start;

        jit_times[i] = (double)elapsed;
        total_jit_ns += elapsed;
    }

    Stats jit_stats = compute_stats(jit_times, cfg->bench_parses);
    print_stats("JIT-compiled", &jit_stats);

    printf("\nJIT throughput: %.0f parses/sec\n",
           cfg->bench_parses / ((double)total_jit_ns / 1e9));

    /* ============================================================== */
    /*  COMPARISON                                                   */
    /* ============================================================== */

    printf("\n--- COMPARISON ---\n");

    double speedup = interp_stats.mean / jit_stats.mean;
    printf("Speedup (mean): %.2fx\n", speedup);

    double speedup_median = interp_stats.median / jit_stats.median;
    printf("Speedup (median): %.2fx\n", speedup_median);

    double speedup_p95 = interp_stats.p95 / jit_stats.p95;
    printf("Speedup (P95): %.2fx\n", speedup_p95);

    if (speedup >= 2.0) {
        printf("\n✓ SUCCESS: JIT achieves target speedup (≥2x)\n");
    } else if (speedup >= 1.5) {
        printf("\n⚠ PARTIAL: JIT shows improvement but below target (%.2fx < 2.0x)\n", speedup);
    } else if (speedup >= 1.0) {
        printf("\n⚠ MARGINAL: JIT marginally faster (%.2fx)\n", speedup);
    } else {
        printf("\n✗ FAILURE: JIT slower than interpreted (%.2fx < 1.0x)\n", speedup);
    }

    /* Variability comparison */
    double interp_cv = interp_stats.stddev / interp_stats.mean;
    double jit_cv = jit_stats.stddev / jit_stats.mean;
    printf("\nCoefficient of variation:\n");
    printf("  Interpreted: %.1f%%\n", interp_cv * 100.0);
    printf("  JIT:         %.1f%%\n", jit_cv * 100.0);

    if (jit_cv < interp_cv) {
        printf("  → JIT more consistent\n");
    } else {
        printf("  → Interpreted more consistent\n");
    }

    /* Per-lookup cost */
    uint32_t total_lookups = cfg->parse_length;
    double interp_ns_per_lookup = interp_stats.mean / total_lookups;
    double jit_ns_per_lookup = jit_stats.mean / total_lookups;

    printf("\nPer-lookup cost:\n");
    printf("  Interpreted: %.2f ns/lookup\n", interp_ns_per_lookup);
    printf("  JIT:         %.2f ns/lookup\n", jit_ns_per_lookup);

    /* Cleanup */
    free(interp_times);
    free(jit_times);
    free(lookaheads);
    snapshot_release(snap);
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  JIT vs Interpreted Parser Benchmark                         ║\n");
    printf("║  Extensible SQL Parser for PostgreSQL                        ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");

    printf("\nJIT available: %s\n", jit_is_available() ? "YES" : "NO (stub mode)");

    if (!jit_is_available()) {
        printf("\n");
        printf("WARNING: LLVM JIT is not available. This benchmark will only\n");
        printf("measure interpreted performance. To enable JIT:\n");
        printf("  1. Ensure LLVM development libraries are installed\n");
        printf("  2. Rebuild with: meson setup builddir && ninja -C builddir\n");
        printf("  3. Verify with: pkg-config --modversion llvm\n");
        printf("\n");
    }

    /* ============================================================== */
    /*  SMALL GRAMMAR (typical SQL subset)                           */
    /* ============================================================== */

    BenchConfig small = {
        .nstates = 64,
        .nterminals = 32,
        .parse_length = 50,
        .warmup_parses = 1000,
        .bench_parses = 10000,
    };

    run_comparison_benchmark(&small);

    /* ============================================================== */
    /*  MEDIUM GRAMMAR (full SQL)                                    */
    /* ============================================================== */

    BenchConfig medium = {
        .nstates = 256,
        .nterminals = 100,
        .parse_length = 100,
        .warmup_parses = 500,
        .bench_parses = 5000,
    };

    run_comparison_benchmark(&medium);

    /* ============================================================== */
    /*  LARGE GRAMMAR (extended SQL with extensions)                 */
    /* ============================================================== */

    BenchConfig large = {
        .nstates = 512,
        .nterminals = 150,
        .parse_length = 200,
        .warmup_parses = 200,
        .bench_parses = 2000,
    };

    run_comparison_benchmark(&large);

    /* ============================================================== */
    /*  AMORTIZATION ANALYSIS                                        */
    /* ============================================================== */

    printf("\n");
    printf("=================================================================\n");
    printf("  AMORTIZATION ANALYSIS\n");
    printf("=================================================================\n");
    printf("\nThis analysis shows when JIT compilation cost is recovered.\n");

    ParserSnapshot *snap = create_benchmark_snapshot(128, 64, "AMORTIZE");
    if (snap) {
        uint16_t *lookaheads = generate_lookahead_sequence(64, 100);
        if (lookaheads) {
            /* Measure interpreted baseline */
            uint64_t interp_start = now_ns();
            for (int i = 0; i < 1000; i++) {
                simulate_parse(snap, lookaheads, 100);
            }
            uint64_t interp_elapsed = now_ns() - interp_start;
            double interp_per_parse = (double)interp_elapsed / 1000.0;

            printf("\nInterpreted: %.2f ns/parse\n", interp_per_parse);

            /* Measure JIT compilation + execution */
            uint64_t jit_compile_start = now_ns();
            JITStatus st = jit_attach_to_snapshot(snap);
            uint64_t jit_compile_end = now_ns();

            if (st == JIT_OK) {
                uint64_t compile_time = jit_compile_end - jit_compile_start;
                printf("JIT compilation: %.2f ms\n", compile_time / 1e6);

                uint64_t jit_start = now_ns();
                for (int i = 0; i < 1000; i++) {
                    simulate_parse(snap, lookaheads, 100);
                }
                uint64_t jit_elapsed = now_ns() - jit_start;
                double jit_per_parse = (double)jit_elapsed / 1000.0;

                printf("JIT: %.2f ns/parse (after warmup)\n", jit_per_parse);

                /* Calculate break-even */
                if (jit_per_parse < interp_per_parse) {
                    double savings_per_parse = interp_per_parse - jit_per_parse;
                    uint64_t breakeven_parses = (uint64_t)(compile_time / savings_per_parse);

                    printf("\nBreak-even: %lu parses\n", (unsigned long)breakeven_parses);
                    printf("  (After %lu parses, JIT has paid for itself)\n",
                           (unsigned long)breakeven_parses);

                    if (breakeven_parses < 100) {
                        printf("  ✓ Excellent: Very fast break-even\n");
                    } else if (breakeven_parses < 1000) {
                        printf("  ✓ Good: Reasonable break-even\n");
                    } else {
                        printf("  ⚠ High: May only benefit long-running processes\n");
                    }
                } else {
                    printf("\n✗ WARNING: JIT not faster than interpreted!\n");
                }
            }

            free(lookaheads);
        }
        snapshot_release(snap);
    }

    printf("\n");
    printf("=================================================================\n");
    printf("  BENCHMARK COMPLETE\n");
    printf("=================================================================\n");
    printf("\n");

    return 0;
}
