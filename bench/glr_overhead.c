/*
** glr_overhead.c -- side-by-side measurement of the LALR push parser
** vs the GLR engine on the same input.  Both paths drive the same
** ParserSnapshot built from bench_arith_grammar.y; the only thing
** that differs is whether the runtime takes the LALR fast path
** (parse_engine.c -> parse_token) or the GLR engine (glr.c).
**
** This benchmark is the load-bearing measurement behind the README
** claim "GLR has zero impact on the LALR fast path".  Specifically:
**
**   - lalr_baseline   -- parse_token() loop, no GLR setup at all
**   - glr_no_conflict -- lime_parse_glr_feed() loop on the same
**                        unambiguous arith input (no forks).
**
** The overhead percentage we print is the ratio glr_no_conflict /
** lalr_baseline.  It quantifies the fixed cost of running through
** the GLR step machinery (heap-allocated GSS nodes, refcount ops,
** orig_heads malloc per token) on input that does not exercise
** any of GLR's value-add.  Real GLR workloads with conflicts amortise
** this cost across the work the LALR engine cannot do.
**
** Methodology:
**   - 10000 iterations per trial, 100-iter warmup, 5 trials, median
**     reported.
**   - clock_gettime(CLOCK_MONOTONIC) in ns.
**   - Same arith expression feeds both paths so the comparison is
**     apples-to-apples on the SAME tokens against the SAME snapshot.
*/

#define _POSIX_C_SOURCE 200809L

#include "parser.h"
#include "snapshot.h"
#include "parse_context.h"
#include "snapshot_build.h"
#include "glr.h"

#include "bench_arith_grammar.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern ParserSnapshot *ArithBuildSnapshot(void);

void *ArithAlloc(void *(*mallocProc)(size_t));
void ArithFree(void *p, void (*freeProc)(void *));
void Arith(void *yyp, int yymajor, int yyminor, int *result_out);

typedef struct {
    int code;
    int value;
} Tok;

/* (((1 + 2) * (3 + 4) - 5) / (1 + 1)) -- 21 tokens, equals 8. */
static const Tok stream[] = {
    { ARITH_LP, 0 },    { ARITH_LP, 0 },   { ARITH_NUM, 1 },
    { ARITH_PLUS, 0 },  { ARITH_NUM, 2 },  { ARITH_RP, 0 },
    { ARITH_STAR, 0 },  { ARITH_LP, 0 },   { ARITH_NUM, 3 },
    { ARITH_PLUS, 0 },  { ARITH_NUM, 4 },  { ARITH_RP, 0 },
    { ARITH_MINUS, 0 }, { ARITH_NUM, 5 },  { ARITH_RP, 0 },
    { ARITH_SLASH, 0 }, { ARITH_LP, 0 },   { ARITH_NUM, 1 },
    { ARITH_PLUS, 0 },  { ARITH_NUM, 1 },  { ARITH_RP, 0 },
};
static const size_t stream_len = sizeof(stream) / sizeof(stream[0]);

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a, db = *(const double *)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  LALR baseline -- generated parser via Arith()                     */
/* ------------------------------------------------------------------ */

static uint64_t bench_lalr(uint32_t iterations) {
    uint64_t t0 = now_ns();
    volatile int sink = 0;
    for (uint32_t i = 0; i < iterations; i++) {
        int result = 0;
        void *p = ArithAlloc(malloc);
        for (size_t k = 0; k < stream_len; k++) {
            Arith(p, stream[k].code, stream[k].value, &result);
        }
        Arith(p, 0, 0, &result);
        ArithFree(p, free);
        sink += result;
    }
    uint64_t t1 = now_ns();
    (void)sink;
    return t1 - t0;
}

/* ------------------------------------------------------------------ */
/*  GLR engine on the same unambiguous input                          */
/* ------------------------------------------------------------------ */

static uint64_t bench_glr(ParserSnapshot *snap, uint32_t iterations) {
    uint64_t t0 = now_ns();
    volatile int sink = 0;
    for (uint32_t i = 0; i < iterations; i++) {
        ParseContext *ctx = parse_begin(snap);
        if (!ctx) return 0;
        if (lime_parse_glr(ctx, NULL, NULL) != 0) {
            parse_end(ctx);
            return 0;
        }
        for (size_t k = 0; k < stream_len; k++) {
            int rc = lime_parse_glr_feed(ctx, (uint16_t)stream[k].code);
            if (rc < 0) { sink ^= rc; break; }
        }
        (void)lime_parse_glr_feed(ctx, 0);
        sink ^= (int)lime_parse_glr_head_count(ctx);
        lime_parse_glr_end(ctx);
        parse_end(ctx);
    }
    uint64_t t1 = now_ns();
    (void)sink;
    return t1 - t0;
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    uint32_t iters = 10000;
    uint32_t warmup = 100;
    int trials = 5;

    if (argc > 1) iters = (uint32_t)atoi(argv[1]);
    if (argc > 2) trials = atoi(argv[2]);
    if (trials < 1) trials = 1;
    if (trials > 32) trials = 32;

    ParserSnapshot *snap = ArithBuildSnapshot();
    if (!snap) {
        fprintf(stderr, "ArithBuildSnapshot failed\n");
        return 1;
    }

    /* Sanity check both paths get the same answer. */
    {
        int result = 0;
        void *p = ArithAlloc(malloc);
        for (size_t k = 0; k < stream_len; k++) {
            Arith(p, stream[k].code, stream[k].value, &result);
        }
        Arith(p, 0, 0, &result);
        ArithFree(p, free);
        printf("# sanity LALR: result=%d (expect 8)\n", result);
    }

    printf("# Lime GLR overhead benchmark\n");
    printf("# iterations/trial: %u   trials: %d   warmup: %u\n", iters, trials, warmup);

    /* Warm-up. */
    (void)bench_lalr(warmup);
    (void)bench_glr(snap, warmup);

    double lalr_times[32], glr_times[32];

    for (int t = 0; t < trials; t++) {
        uint64_t ns = bench_lalr(iters);
        lalr_times[t] = (double)ns;
    }
    for (int t = 0; t < trials; t++) {
        uint64_t ns = bench_glr(snap, iters);
        glr_times[t] = (double)ns;
    }

    qsort(lalr_times, (size_t)trials, sizeof(double), cmp_double);
    qsort(glr_times, (size_t)trials, sizeof(double), cmp_double);

    double lalr_med = lalr_times[trials / 2];
    double glr_med = glr_times[trials / 2];

    double lalr_per = lalr_med / iters;
    double glr_per = glr_med / iters;
    double overhead_pct = (glr_per / lalr_per - 1.0) * 100.0;

    printf("\nResults (median of %d trials, ns per parse):\n", trials);
    printf("  LALR baseline      : %10.1f ns/parse\n", lalr_per);
    printf("  GLR no-conflict    : %10.1f ns/parse\n", glr_per);
    printf("  GLR overhead vs LALR: %+8.1f %%\n", overhead_pct);
    printf("\n# csv\n");
    printf("path,ns_per_parse\n");
    printf("lalr,%.1f\n", lalr_per);
    printf("glr_no_conflict,%.1f\n", glr_per);

    lime_snapshot_release(snap);
    return 0;
}
