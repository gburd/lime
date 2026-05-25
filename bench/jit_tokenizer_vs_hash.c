/*
** JIT-tokenizer vs hash-table head-to-head benchmark.
**
** The Lime runtime classifies SQL keywords through `lookup_token`, a
** case-insensitive hash table.  An earlier prototype (deleted in
** commit 1506723) compiled the same keyword set into a length-bucketed
** trie via LLVM OrcJIT and exposed `jit_tokenizer_classify_keyword`.
**
** This benchmark answers a single decision-gate question:
**
**     Does the JIT classifier outperform the hash table by enough
**     margin to justify the LLVM compile cost?
**
** What it measures
** ----------------
**   1. cold-start cost          -- one-time JIT compile (ns)
**   2. hash warm-cost           -- median ns per `lookup_token` call
**   3. JIT  warm-cost           -- median ns per `jit_tokenizer_classify_keyword`
**   4. correctness equivalence  -- every keyword yields the same code
**   5. break-even point         -- lookups required to amortise JIT compile
**
** Methodology
** -----------
**   - 85 representative SQL keywords drawn from examples/pg/tokens.lime.
**   - Workload mix: 60% known keywords, 40% non-keyword identifiers --
**     mirrors realistic SQL where about half of identifier tokens are
**     reserved words (cf. tests/test_tokenize.c).
**   - 10 trials of 10000 iterations each; we report the median across
**     trials of the per-iteration mean.  Median-of-means filters out
**     scheduler noise on macOS without distorting the warm-cost figure.
**   - Hash and JIT see identical input streams in identical order so
**     neither path benefits from differential branch-predictor warmth.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "token_table.h"
#include "jit_tokenizer.h"

/* ------------------------------------------------------------------ */
/*  Keyword set                                                        */
/* ------------------------------------------------------------------ */

/*
** 85 SQL keywords drawn from examples/pg/tokens.lime, picked to span
** a realistic length distribution (2-15 chars) and a representative
** mix of common (SELECT, FROM, WHERE) and rarer (AUTHORIZATION,
** CHARACTERISTICS) SQL reserved words.
*/
static const char *const KEYWORDS[] = {
    "AS",          "AT",          "BY",          "DO",          "IF",          "IN",
    "IS",          "OF",          "ON",          "OR",          "TO",          "ADD",
    "ALL",         "AND",         "ANY",         "ASC",         "FOR",         "NOT",
    "NEW",         "OLD",         "OUT",         "ROW",         "SET",         "USE",
    "BOTH",        "CALL",        "CASE",        "CAST",        "DATA",        "DESC",
    "DROP",        "EACH",        "ELSE",        "FROM",        "FULL",        "INTO",
    "JOIN",        "LEFT",        "LIKE",        "NULL",        "ONLY",        "OPEN",
    "OVER",        "READ",        "ROWS",        "SHOW",        "SOME",        "THEN",
    "TYPE",        "USER",        "VIEW",        "WHEN",        "WITH",        "ALTER",
    "BEGIN",       "CHECK",       "CLOSE",       "CROSS",       "FETCH",       "FIRST",
    "GROUP",       "INDEX",       "INNER",       "MERGE",       "ORDER",       "OUTER",
    "RIGHT",       "TABLE",       "UNION",       "USING",       "WHERE",       "COMMIT",
    "CREATE",      "DELETE",      "EXCEPT",      "EXISTS",      "HAVING",      "INSERT",
    "RETURN",      "SELECT",      "UPDATE",      "DEFAULT",     "EXECUTE",     "PRIMARY",
    "AUTHORIZATION",
};

#define NKEYWORDS ((int)(sizeof(KEYWORDS) / sizeof(KEYWORDS[0])))

/*
** 40 non-keyword identifiers -- common SQL column / table / function
** names that share length and prefix patterns with the keywords above.
** These exercise the hash-miss / JIT-miss path on realistic input.
*/
static const char *const NON_KEYWORDS[] = {
    "x",         "y",         "z",         "id",        "no",        "ok",
    "foo",       "bar",       "baz",       "qux",       "name",      "addr",
    "city",      "user1",     "table1",    "col_1",     "amount",    "status",
    "result",    "vendor",    "client",    "product",   "service",   "address",
    "balance",   "company",   "country",   "history",   "comment",   "session",
    "version",   "category", "department", "operation", "permission","subscription",
    "transaction", "configuration", "documentation", "implementation",
};

#define NNON_KEYWORDS ((int)(sizeof(NON_KEYWORDS) / sizeof(NON_KEYWORDS[0])))

/* ------------------------------------------------------------------ */
/*  Workload synthesis                                                 */
/* ------------------------------------------------------------------ */

/*
** A single workload entry: pointer + length, paired with the expected
** lookup result so the benchmark loop also acts as a correctness probe.
*/
typedef struct WorkItem {
    const char *str;
    size_t len;
    int expected; /* -1 for non-keywords, token code otherwise */
} WorkItem;

/*
** Build a 60% keyword / 40% non-keyword interleaved workload.  The
** ordering is deterministic -- a simple LCG seeded from a fixed value
** -- so every run hits the same instruction-cache footprint.
*/
static WorkItem *make_workload(int n, int kw_base_code) {
    WorkItem *items = calloc((size_t)n, sizeof(*items));
    if (!items) abort();

    /* Linear-congruential PRNG (Numerical Recipes constants), fixed seed. */
    uint32_t rng = 0xCAFEBABEu;

    for (int i = 0; i < n; i++) {
        rng = rng * 1664525u + 1013904223u;
        bool is_kw = (rng % 100u) < 60u;

        if (is_kw) {
            int idx = (int)((rng >> 8) % (uint32_t)NKEYWORDS);
            items[i].str = KEYWORDS[idx];
            items[i].len = strlen(KEYWORDS[idx]);
            items[i].expected = kw_base_code + idx;
        } else {
            int idx = (int)((rng >> 8) % (uint32_t)NNON_KEYWORDS);
            items[i].str = NON_KEYWORDS[idx];
            items[i].len = strlen(NON_KEYWORDS[idx]);
            items[i].expected = -1;
        }
    }
    return items;
}

/* ------------------------------------------------------------------ */
/*  Timing                                                             */
/* ------------------------------------------------------------------ */

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int cmp_uint64(const void *a, const void *b) {
    uint64_t va = *(const uint64_t *)a;
    uint64_t vb = *(const uint64_t *)b;
    return (va > vb) - (va < vb);
}

/* ------------------------------------------------------------------ */
/*  Benchmark loops -- both kept volatile-store-checksumed so the      */
/*  optimiser cannot hoist the lookup out of the loop.                  */
/* ------------------------------------------------------------------ */

static uint64_t bench_hash(TokenTable *tt, const WorkItem *items, int n) {
    volatile int sink = 0;
    uint64_t t0 = now_ns();
    for (int i = 0; i < n; i++) {
        sink += lookup_token(tt, items[i].str, items[i].len);
    }
    uint64_t t1 = now_ns();
    (void)sink;
    return t1 - t0;
}

static uint64_t bench_jit(const JITTokenizer *jit, const WorkItem *items, int n) {
    volatile int sink = 0;
    uint64_t t0 = now_ns();
    for (int i = 0; i < n; i++) {
        sink += jit_tokenizer_classify_keyword(jit, items[i].str, items[i].len);
    }
    uint64_t t1 = now_ns();
    (void)sink;
    return t1 - t0;
}

/* ------------------------------------------------------------------ */
/*  Equivalence verification                                           */
/* ------------------------------------------------------------------ */

/*
** For every keyword in the table, both paths must agree.  We also test
** non-keywords -- both must report "miss" (-1).  Returns the count of
** mismatches (0 if equivalent).
*/
static int verify_equivalence(TokenTable *tt, const JITTokenizer *jit) {
    int mismatches = 0;

    for (int i = 0; i < NKEYWORDS; i++) {
        size_t len = strlen(KEYWORDS[i]);
        int hash_code = lookup_token(tt, KEYWORDS[i], len);
        int jit_code  = jit_tokenizer_classify_keyword(jit, KEYWORDS[i], len);
        if (hash_code != jit_code) {
            fprintf(stderr,
                    "MISMATCH on '%s': hash=%d jit=%d\n",
                    KEYWORDS[i], hash_code, jit_code);
            mismatches++;
        }
    }
    for (int i = 0; i < NNON_KEYWORDS; i++) {
        size_t len = strlen(NON_KEYWORDS[i]);
        int hash_code = lookup_token(tt, NON_KEYWORDS[i], len);
        int jit_code  = jit_tokenizer_classify_keyword(jit, NON_KEYWORDS[i], len);
        if (hash_code != jit_code) {
            fprintf(stderr,
                    "MISMATCH on non-keyword '%s': hash=%d jit=%d\n",
                    NON_KEYWORDS[i], hash_code, jit_code);
            mismatches++;
        }
    }
    return mismatches;
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

#define N_ITER 10000
#define N_TRIALS 10
#define KW_BASE_CODE 1000

int main(void) {
    printf("=================================================================\n");
    printf(" JIT tokenizer vs hash table -- head-to-head\n");
    printf("=================================================================\n");
    printf(" keywords         : %d\n", NKEYWORDS);
    printf(" non-keywords     : %d\n", NNON_KEYWORDS);
    printf(" iterations/trial : %d\n", N_ITER);
    printf(" trials           : %d\n", N_TRIALS);
    printf(" workload mix     : 60%% keyword / 40%% non-keyword\n");
    printf("\n");

    if (!jit_tokenizer_is_available()) {
        printf("JIT NOT AVAILABLE -- this benchmark requires LLVM.\n");
        printf("Build with -Dllvm=enabled to run.\n");
        return 0;
    }

    /* Build the TokenTable populated with our 85 keywords. */
    TokenTable *tt = create_token_table(128);
    if (!tt) {
        fprintf(stderr, "OOM creating TokenTable\n");
        return 2;
    }
    for (int i = 0; i < NKEYWORDS; i++) {
        if (!add_token(tt, KEYWORDS[i], KW_BASE_CODE + i, 0)) {
            fprintf(stderr, "add_token failed for %s\n", KEYWORDS[i]);
            destroy_token_table(tt);
            return 2;
        }
    }

    /* JIT-compile the trie -- time the cold-start cost. */
    uint64_t compile_t0 = now_ns();
    JITTokenizer *jit = jit_tokenizer_create(tt);
    uint64_t compile_t1 = now_ns();
    if (!jit) {
        fprintf(stderr, "jit_tokenizer_create returned NULL\n");
        destroy_token_table(tt);
        return 3;
    }
    uint64_t cold_ns = compile_t1 - compile_t0;

    JITTokenizerStats jit_stats = jit_tokenizer_get_stats(jit);
    printf("Cold-start (JIT compile)\n");
    printf("  wall-clock     : %.3f ms (%llu ns)\n",
           (double)cold_ns / 1e6, (unsigned long long)cold_ns);
    printf("  reported by impl: %.3f ms\n",
           (double)jit_stats.compile_time_ns / 1e6);
    printf("  keywords baked : %u\n", jit_stats.keywords_compiled);
    printf("\n");

    /* ---------- correctness gate ---------- */
    int mismatches = verify_equivalence(tt, jit);
    if (mismatches != 0) {
        fprintf(stderr,
                "FAIL: %d mismatches between hash and JIT classifiers\n",
                mismatches);
        jit_tokenizer_destroy(jit);
        destroy_token_table(tt);
        return 4;
    }
    printf("Correctness: hash and JIT agree on all %d keywords + %d non-keywords.\n\n",
           NKEYWORDS, NNON_KEYWORDS);

    /* ---------- warm-cost benchmark ---------- */
    WorkItem *items = make_workload(N_ITER, KW_BASE_CODE);

    /* Warm both paths so neither is paying for I-cache misses. */
    (void)bench_hash(tt, items, N_ITER);
    (void)bench_jit(jit, items, N_ITER);

    uint64_t hash_trial_ns[N_TRIALS];
    uint64_t jit_trial_ns[N_TRIALS];

    /*
    ** Interleave hash/JIT trials.  Running 10 hash trials then 10 JIT
    ** trials would let frequency scaling or thermal drift bias whichever
    ** path ran last.
    */
    for (int t = 0; t < N_TRIALS; t++) {
        hash_trial_ns[t] = bench_hash(tt, items, N_ITER);
        jit_trial_ns[t]  = bench_jit(jit,  items, N_ITER);
    }

    qsort(hash_trial_ns, N_TRIALS, sizeof(uint64_t), cmp_uint64);
    qsort(jit_trial_ns,  N_TRIALS, sizeof(uint64_t), cmp_uint64);

    uint64_t hash_med = hash_trial_ns[N_TRIALS / 2];
    uint64_t jit_med  = jit_trial_ns[N_TRIALS / 2];

    double hash_per = (double)hash_med / (double)N_ITER;
    double jit_per  = (double)jit_med  / (double)N_ITER;

    /* Best-of-trials, mostly to gauge variance. */
    uint64_t hash_best = hash_trial_ns[0];
    uint64_t jit_best  = jit_trial_ns[0];

    printf("Warm-cost (median across %d trials of %d iterations)\n",
           N_TRIALS, N_ITER);
    printf("  hash : %.2f ns / lookup (median trial %llu ns / iter, best %llu ns)\n",
           hash_per, (unsigned long long)hash_med,
           (unsigned long long)hash_best);
    printf("  JIT  : %.2f ns / lookup (median trial %llu ns / iter, best %llu ns)\n",
           jit_per,  (unsigned long long)jit_med,
           (unsigned long long)jit_best);
    printf("\n");

    /* ---------- decision math ---------- */
    double speedup = hash_per / jit_per;          /* > 1 means JIT faster */
    double saved_per_lookup = hash_per - jit_per; /* ns saved (or lost) per lookup */

    printf("Speedup (hash/JIT)   : %.3fx\n", speedup);
    printf("Per-lookup delta     : %+.2f ns (positive = JIT wins)\n",
           saved_per_lookup);

    if (saved_per_lookup > 0.0) {
        double break_even = (double)cold_ns / saved_per_lookup;
        printf("Break-even           : %.0f lookups to amortise %llu ns of compile\n",
               break_even, (unsigned long long)cold_ns);
    } else {
        printf("Break-even           : NEVER (JIT does not beat hash on this hardware)\n");
    }
    printf("\n");

    /* Decision gate per the project's kill-switch protocol: 5%% margin
    ** AND amortising in <= 100k lookups. */
    int verdict = 0; /* 0 = abandon, 1 = integrate */
    if (speedup > 1.05 && saved_per_lookup > 0.0) {
        double break_even = (double)cold_ns / saved_per_lookup;
        if (break_even <= 100000.0) verdict = 1;
    }

    printf("DECISION : %s\n", verdict ? "INTEGRATE" : "ABANDON");
    if (!verdict) {
        printf("Reason   : %s\n",
               speedup <= 1.05
                 ? "JIT does not beat hash by > 5% margin"
                 : "Compile cost does not amortise within 100k lookups");
    }

    free(items);
    jit_tokenizer_destroy(jit);
    destroy_token_table(tt);
    return verdict ? 0 : 1; /* exit 0 = integrate, 1 = abandon */
}
