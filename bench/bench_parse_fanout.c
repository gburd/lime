/*
** bench_parse_fanout.c -- multi-thread parse-server fan-out benchmark.
**
** Goal: measure parse throughput as the number of parallel parser
** threads scales from 1 to N.  All threads share a single
** ParserSnapshot (read-only, refcounted by the API).  Each thread
** has its own ParseContext / ParseStack; the only shared mutable
** state is the snapshot's atomic_uint refcount.
**
** Why this matters
** ----------------
** ParserSnapshot.refcount is the canonical false-sharing hazard for
** server workloads:
**
**   * 256-byte snapshot struct, refcount near the start;
**   * every parse_begin / parse_end on the same snapshot does an
**     atomic_fetch_add / fetch_sub on that one cacheline;
**   * read-only fields in the same cacheline (yy_action,
**     yy_lookahead, jit_find_shift_fn) get their cache line
**     invalidated on every parse_begin from any thread.
**
** If we measure ops/sec as thread count grows, perfect scaling is
** linear (8 threads = 8x throughput).  Any sub-linear scaling is
** the false-sharing tax.
**
** This benchmark is the measurement infrastructure for evaluating
** cacheline-alignment + field-reordering changes to ParserSnapshot.
** Without it, "audit said cacheline align refcount" is a theoretical
** improvement; with it, we can prove the win or prove there's no
** measurable win on this hardware.
**
** Output
** ------
** CSV-formatted rows per (threads, repeat) pair.  Run multiple
** trials and a Python harness (or jq+awk) can compute medians and
** scaling factors.  Also prints a verdict line summarizing the
** scaling efficiency at the highest tested thread count.
**
** Usage
** -----
**   ./bench_parse_fanout                # default: 1,2,4,8 threads
**   ./bench_parse_fanout 1 2 4 8 16     # explicit thread counts
**   THREADS=32 ./bench_parse_fanout     # via env
*/

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>
#include <assert.h>

#include "lime_time.h"
#include "lime_threads.h"

#include "parser.h"
#include "parse_context.h"
#include "snapshot.h"

#include "bench_arith_grammar.h"

/* ------------------------------------------------------------------ */
/*  Generated parser API (filled in by Lime)                           */
/* ------------------------------------------------------------------ */

extern void *ArithAlloc(void *(*mallocProc)(size_t));
extern void ArithFree(void *p, void (*freeProc)(void *));
extern void Arith(void *yyp, int yymajor, int yyminor, int *result_out);

/* Static snapshot accessor emitted by `lime -n`. */
extern ParserSnapshot *ArithBuildSnapshot(void);

/* ------------------------------------------------------------------ */
/*  Token stream (matches bench_jit_real_parser's pattern)             */
/* ------------------------------------------------------------------ */

typedef struct {
    int code;
    int value;
} Tok;

/* Build a single-expression token stream that parses to deterministic
** result 8 = ((1+2)*(3+4)-5)/(1+1).  Caller frees with free().  Note:
** the parser expects one expression per parse session terminated by
** an explicit Arith(parser, 0, 0, &result) EOF call.  We don't include
** the EOF in the stream itself -- the worker calls it separately
** after each expression. */
static Tok *make_token_stream(uint32_t *count_out) {
    static const Tok body[] = {
        { ARITH_LP, 0 },    { ARITH_LP, 0 },   { ARITH_NUM, 1 },  { ARITH_PLUS, 0 },
        { ARITH_NUM, 2 },   { ARITH_RP, 0 },   { ARITH_STAR, 0 }, { ARITH_LP, 0 },
        { ARITH_NUM, 3 },   { ARITH_PLUS, 0 }, { ARITH_NUM, 4 },  { ARITH_RP, 0 },
        { ARITH_MINUS, 0 }, { ARITH_NUM, 5 },  { ARITH_RP, 0 },   { ARITH_SLASH, 0 },
        { ARITH_LP, 0 },    { ARITH_NUM, 1 },  { ARITH_PLUS, 0 }, { ARITH_NUM, 1 },
        { ARITH_RP, 0 },
    };
    const uint32_t per = sizeof(body) / sizeof(body[0]);

    Tok *out = malloc(per * sizeof(Tok));
    if (!out) return NULL;
    memcpy(out, body, sizeof(body));

    *count_out = per;
    return out;
}

/* ------------------------------------------------------------------ */
/*  Per-thread driver                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    /* Inputs. */
    ParserSnapshot *snap;
    const Tok      *tokens;
    uint32_t        ntoken;
    uint32_t        parses_per_thread;

    /* Outputs. */
    uint64_t        ns_elapsed;
    uint64_t        parses_done;
    int             error;

    /* Sync. */
    atomic_uint    *start_gate;
} ThreadArgs;

static void *parse_worker(void *arg) {
    ThreadArgs *a = (ThreadArgs *)arg;

    /* Spin until the launcher releases the gate, so all threads start
    ** roughly together. */
    while (atomic_load_explicit(a->start_gate, memory_order_acquire) == 0) {
        /* spin */
    }

    uint64_t t0 = lime_now_ns();

    for (uint32_t p = 0; p < a->parses_per_thread; p++) {
        int result = -1;
        void *parser = ArithAlloc(malloc);
        if (!parser) { a->error = 1; return NULL; }

        for (uint32_t i = 0; i < a->ntoken; i++) {
            Arith(parser, a->tokens[i].code, a->tokens[i].value, &result);
        }
        Arith(parser, 0, 0, &result);  /* EOF */

        ArithFree(parser, free);

        if (result != 8) { a->error = 2; return NULL; }
        a->parses_done++;
    }

    a->ns_elapsed = lime_now_ns() - t0;
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Run one (nthreads, parses_per_thread) configuration                 */
/* ------------------------------------------------------------------ */

typedef struct {
    int    nthreads;
    int    trial;
    double total_ms;
    double per_thread_min_ms;
    double per_thread_max_ms;
    uint64_t total_parses;
    double  parses_per_sec;
} TrialResult;

static TrialResult run_trial(ParserSnapshot *snap,
                             const Tok *tokens, uint32_t ntoken,
                             int nthreads, uint32_t parses_per_thread,
                             int trial) {
    TrialResult r = { .nthreads = nthreads, .trial = trial };

    ThreadArgs *args = calloc((size_t)nthreads, sizeof(ThreadArgs));
    pthread_t *tids = calloc((size_t)nthreads, sizeof(pthread_t));
    atomic_uint gate;
    atomic_init(&gate, 0);

    if (!args || !tids) { fprintf(stderr, "OOM\n"); exit(1); }

    for (int i = 0; i < nthreads; i++) {
        args[i].snap = snap;
        args[i].tokens = tokens;
        args[i].ntoken = ntoken;
        args[i].parses_per_thread = parses_per_thread;
        args[i].start_gate = &gate;

        if (pthread_create(&tids[i], NULL, parse_worker, &args[i]) != 0) {
            fprintf(stderr, "pthread_create failed\n"); exit(1);
        }
    }

    /* Tiny grace period for threads to reach the spin loop. */
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 1 * 1000 * 1000 };
    nanosleep(&ts, NULL);

    uint64_t t0 = lime_now_ns();
    atomic_store_explicit(&gate, 1, memory_order_release);

    for (int i = 0; i < nthreads; i++) {
        pthread_join(tids[i], NULL);
    }
    uint64_t t1 = lime_now_ns();

    r.total_ms = (double)(t1 - t0) / 1e6;
    r.per_thread_min_ms = 1e18;
    r.per_thread_max_ms = 0;
    for (int i = 0; i < nthreads; i++) {
        if (args[i].error) {
            fprintf(stderr, "thread %d error %d\n", i, args[i].error);
            exit(1);
        }
        double ms = (double)args[i].ns_elapsed / 1e6;
        if (ms < r.per_thread_min_ms) r.per_thread_min_ms = ms;
        if (ms > r.per_thread_max_ms) r.per_thread_max_ms = ms;
        r.total_parses += args[i].parses_done;
    }
    r.parses_per_sec = (double)r.total_parses / (r.total_ms / 1e3);

    free(args);
    free(tids);
    return r;
}

/* ------------------------------------------------------------------ */
/*  Main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    /* Parse thread-count list from argv (or env, or default). */
    int   default_counts[] = { 1, 2, 4, 8 };
    int  *counts = default_counts;
    int   ncounts = 4;

    if (argc > 1) {
        ncounts = argc - 1;
        counts = malloc((size_t)ncounts * sizeof(int));
        for (int i = 0; i < ncounts; i++) {
            counts[i] = atoi(argv[i + 1]);
            if (counts[i] <= 0) {
                fprintf(stderr, "bad thread count %s\n", argv[i + 1]);
                return 1;
            }
        }
    } else if (getenv("THREADS")) {
        ncounts = 1;
        counts = malloc(sizeof(int));
        counts[0] = atoi(getenv("THREADS"));
    }

    /* Build the snapshot once.  Generated by `lime -n` from
    ** bench_arith_grammar.y -- the same one bench_jit_real_parser
    ** uses.  Refcount starts at 1; the workers don't increment
    ** since each parse_begin/parse_end pair is balanced. */
    ParserSnapshot *snap = ArithBuildSnapshot();
    if (!snap) { fprintf(stderr, "snapshot create failed\n"); return 1; }

    /* Build the token stream once -- shared read-only across threads. */
    uint32_t ntoken = 0;
    Tok *tokens = make_token_stream(&ntoken);
    if (!tokens) { fprintf(stderr, "token build failed\n"); return 1; }

    /* Sizing: aim for ~1 second per trial at 1 thread.  parses_per_thread
    ** is fixed; total work scales with nthreads (so total_parses grows
    ** but per-thread time stays in the seconds-not-milliseconds
    ** regime, where timer noise is negligible). */
    const uint32_t parses_per_thread = 50000;
    const int trials_per_count = 3;

    printf("# Lime parse fan-out benchmark\n");
    printf("# tokens/parse=%u  parses/thread=%u  trials=%d\n",
           ntoken, parses_per_thread, trials_per_count);
    printf("# Sanity: single-thread first, then scale.\n");
    printf("#\n");
    printf("nthreads,trial,total_ms,per_thread_min_ms,per_thread_max_ms,"
           "total_parses,parses_per_sec\n");

    /* Track the single-thread baseline for scaling-factor reporting. */
    double baseline_pps = 0.0;

    for (int c = 0; c < ncounts; c++) {
        int nt = counts[c];
        for (int t = 0; t < trials_per_count; t++) {
            TrialResult r = run_trial(snap, tokens, ntoken,
                                       nt, parses_per_thread, t);
            printf("%d,%d,%.3f,%.3f,%.3f,%llu,%.0f\n",
                   r.nthreads, r.trial, r.total_ms,
                   r.per_thread_min_ms, r.per_thread_max_ms,
                   (unsigned long long)r.total_parses, r.parses_per_sec);
            if (nt == 1 && r.parses_per_sec > baseline_pps) {
                baseline_pps = r.parses_per_sec;
            }
        }
    }

    /* Verdict: scaling efficiency at highest tested thread count. */
    if (baseline_pps > 0 && counts[ncounts - 1] > 1) {
        int top_nt = counts[ncounts - 1];
        /* Re-run one more measurement at top_nt for the verdict. */
        TrialResult final = run_trial(snap, tokens, ntoken,
                                       top_nt, parses_per_thread, 99);
        double expected_pps = baseline_pps * top_nt;
        double efficiency = final.parses_per_sec / expected_pps;
        printf("\n# Verdict\n");
        printf("# baseline (1 thr): %8.0f parses/sec\n", baseline_pps);
        printf("# top      (%d thr): %8.0f parses/sec\n",
               top_nt, final.parses_per_sec);
        printf("# expected linear:  %8.0f parses/sec\n", expected_pps);
        printf("# efficiency:       %5.1f%% (1.00 = perfect linear scaling)\n",
               efficiency * 100.0);
        if (efficiency >= 0.85) {
            printf("# scaling          : GOOD (false-sharing tax appears small)\n");
        } else if (efficiency >= 0.60) {
            printf("# scaling          : MODERATE (some cache contention measured)\n");
        } else {
            printf("# scaling          : POOR (likely false-sharing on the hot snapshot fields)\n");
        }
    }

    snapshot_release(snap);
    free(tokens);
    if (counts != default_counts) free(counts);
    return 0;
}
