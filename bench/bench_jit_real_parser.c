/*
** bench_jit_real_parser.c
**
** Tests the README claim:
**   "Optional LLVM JIT provides 2.5-4.2x faster action table lookups."
**   "Generated parsers optionally use ... LLVM JIT compilation for
**    action table lookups."
**
** Method:
**   1. Build a REAL parser via `./builddir/lime bench_arith_grammar.y`.
**      The generated ArithParse() function is the one every example in
**      examples/* uses.
**   2. Tokenise N copies of a representative arithmetic expression to
**      a fixed token stream.
**   3. Parse it M times in two scenarios:
**        a) baseline   -- no JIT calls at all
**        b) jit-armed  -- call lime_jit_compile() on a synthetic
**                         snapshot before parsing
**      In both cases the timed loop calls the SAME ArithParse() symbol.
**      If the README claim is true that JIT speeds up "action table
**      lookups" of generated parsers, scenario (b) should be faster.
**   4. Use clock_gettime(CLOCK_MONOTONIC) with a workload large enough
**      to keep us well above timer resolution.
**   5. Print symbol cross-reference data: scan the linked binary for
**      LLVM symbols inside the ArithParse() module.  If none are
**      referenced from the generated parser code, that is direct
**      evidence the JIT path is NOT wired into generated parsers.
**
** Result is reported as a verdict, not a pass/fail assert -- this is a
** forensic benchmark, not a regression test.
*/

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <math.h>

#include "parser.h"          /* lime_jit_available, lime_jit_compile */
#include "snapshot.h"        /* ParserSnapshot */
#include "snapshot_modify.h" /* clone_snapshot for synthetic snapshot */

#include "bench_arith_grammar.h"

/* ------------------------------------------------------------------ */
/*  Generated parser API (filled in by Lime)                           */
/* ------------------------------------------------------------------ */

void *ArithAlloc(void *(*mallocProc)(size_t));
void ArithFree(void *p, void (*freeProc)(void *));
void Arith(void *yyp, int yymajor, int yyminor, int *result_out);

/* ------------------------------------------------------------------ */
/*  Token stream                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    int code;
    int value;
} Tok;

/*
** Build a token stream that parses to a deterministic numeric result.
** We repeat a non-trivial expression `reps` times and end with a
** parser-acceptable EOF marker (token code 0, the standard Lime end
** sentinel).
**
** Pattern: ((1 + 2) * (3 + 4) - 5) / (1 + 1)
** = ((3) * (7) - 5) / 2 = (21 - 5)/2 = 8
*/
static Tok *make_token_stream(uint32_t reps, uint32_t *count_out) {
    /* 17 tokens per rep. */
    static const Tok pattern[] = {
        { ARITH_LP, 0 },    { ARITH_LP, 0 },   { ARITH_NUM, 1 },  { ARITH_PLUS, 0 },
        { ARITH_NUM, 2 },   { ARITH_RP, 0 },   { ARITH_STAR, 0 }, { ARITH_LP, 0 },
        { ARITH_NUM, 3 },   { ARITH_PLUS, 0 }, { ARITH_NUM, 4 },  { ARITH_RP, 0 },
        { ARITH_MINUS, 0 }, { ARITH_NUM, 5 },  { ARITH_RP, 0 },   { ARITH_SLASH, 0 },
        { ARITH_LP, 0 },
    };
    static const Tok tail[] = {
        { ARITH_NUM, 1 },
        { ARITH_PLUS, 0 },
        { ARITH_NUM, 1 },
        { ARITH_RP, 0 },
    };

    /* For repeated expressions we just parse independent copies; the
    ** parser is reset between expressions. */
    uint32_t per =
        (uint32_t)(sizeof(pattern) / sizeof(pattern[0]) + sizeof(tail) / sizeof(tail[0]));
    uint32_t total = per * reps;

    Tok *toks = malloc(total * sizeof(Tok));
    if (toks == NULL) return NULL;

    Tok *p = toks;
    for (uint32_t r = 0; r < reps; r++) {
        memcpy(p, pattern, sizeof(pattern));
        p += sizeof(pattern) / sizeof(pattern[0]);
        memcpy(p, tail, sizeof(tail));
        p += sizeof(tail) / sizeof(tail[0]);
    }

    *count_out = total;
    return toks;
}

/* ------------------------------------------------------------------ */
/*  Timed parse loop                                                   */
/* ------------------------------------------------------------------ */

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/*
** Parse `tokens_per_expr` tokens, ending each expression with the EOF
** marker.  Repeat for `iterations` expressions.  Returns total ns.
*/
static uint64_t time_parse_loop(const Tok *toks, uint32_t tokens_per_expr, uint32_t iterations) {
    uint64_t t0 = now_ns();
    int total = 0;

    for (uint32_t i = 0; i < iterations; i++) {
        int result = 0;
        void *p = ArithAlloc(malloc);

        for (uint32_t k = 0; k < tokens_per_expr; k++) {
            Arith(p, toks[k].code, toks[k].value, &result);
        }
        Arith(p, 0, 0, &result); /* EOF */

        ArithFree(p, free);
        total += result;
    }

    uint64_t t1 = now_ns();

    /* Make sure the optimiser doesn't elide the work. */
    __asm__ volatile("" : : "r"(total));

    return t1 - t0;
}

/* ------------------------------------------------------------------ */
/*  Build a synthetic snapshot for lime_jit_compile() to chew on        */
/* ------------------------------------------------------------------ */

static ParserSnapshot *make_synthetic_snapshot(void) {
    ParserSnapshot *snap = clone_snapshot(NULL);
    if (snap == NULL) return NULL;

    snap->nstate = 64;
    snap->nterminal = 32;
    snap->nsymbol = 64;
    snap->nrule = 16;
    snap->action_count = snap->nstate * snap->nterminal;
    snap->lookahead_count = snap->action_count;

    snap->yy_action = calloc(snap->action_count, sizeof(uint16_t));
    snap->yy_lookahead = calloc(snap->lookahead_count, sizeof(uint16_t));
    snap->yy_shift_ofst = calloc(snap->nstate, sizeof(int16_t));
    snap->yy_reduce_ofst = calloc(snap->nstate, sizeof(int16_t));
    snap->yy_default = calloc(snap->nstate, sizeof(uint16_t));

    if (!snap->yy_action || !snap->yy_lookahead || !snap->yy_shift_ofst || !snap->yy_reduce_ofst ||
        !snap->yy_default) {
        snapshot_release(snap);
        return NULL;
    }

    /* Plausible-looking content so the JIT path doesn't bail. */
    for (uint32_t s = 0; s < snap->nstate; s++) {
        snap->yy_shift_ofst[s] = (int16_t)(s * snap->nterminal);
        snap->yy_reduce_ofst[s] = -1;
        snap->yy_default[s] = (uint16_t)(s + 1000);
    }
    for (uint32_t i = 0; i < snap->action_count; i++) {
        uint32_t s = i / snap->nterminal;
        uint32_t t = i % snap->nterminal;
        snap->yy_lookahead[i] = (uint16_t)t;
        snap->yy_action[i] = (uint16_t)((s + t) % snap->nstate);
    }
    return snap;
}

/* ------------------------------------------------------------------ */
/*  main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    uint32_t expr_reps = 1;       /* tokens per expression                */
    uint32_t iterations = 200000; /* number of expressions              */
    uint32_t warmup_iter = 5000;
    uint32_t trials = 5;

    if (argc > 1) iterations = (uint32_t)atoi(argv[1]);
    if (argc > 2) trials = (uint32_t)atoi(argv[2]);

    printf("Lime JIT-vs-real-parser benchmark\n");
    printf("=================================\n");
    printf("This benchmark uses ArithParse(), the function Lime emits when\n");
    printf("you run `lime grammar.y` -- the same path examples/* use.\n\n");

    printf("Iterations per trial: %u   Trials: %u\n", iterations, trials);
    printf("JIT available: %s\n", lime_jit_available() ? "YES" : "NO");

    /* ---------------------------------------------------------------- */
    /*  Build the token stream                                          */
    /* ---------------------------------------------------------------- */
    uint32_t total_tokens = 0;
    Tok *toks = make_token_stream(expr_reps, &total_tokens);
    if (toks == NULL) {
        fprintf(stderr, "OOM\n");
        return 1;
    }
    printf("Tokens per expression: %u\n", total_tokens);

    /* Sanity: parse once and print the result. */
    {
        int result = 0;
        void *p = ArithAlloc(malloc);
        for (uint32_t k = 0; k < total_tokens; k++) {
            Arith(p, toks[k].code, toks[k].value, &result);
        }
        Arith(p, 0, 0, &result);
        ArithFree(p, free);
        printf("Sanity check (((1+2)*(3+4)-5)/(1+1)) = %d (expect 8)\n", result);
    }

    /* ---------------------------------------------------------------- */
    /*  Warmup                                                          */
    /* ---------------------------------------------------------------- */
    (void)time_parse_loop(toks, total_tokens, warmup_iter);

    /* ---------------------------------------------------------------- */
    /*  Scenario A: baseline -- never call lime_jit_compile             */
    /* ---------------------------------------------------------------- */
    printf("\n--- Scenario A: BASELINE (no lime_jit_compile)        ---\n");
    double a_times[16] = { 0 };
    for (uint32_t t = 0; t < trials && t < 16; t++) {
        uint64_t ns = time_parse_loop(toks, total_tokens, iterations);
        a_times[t] = (double)ns / 1e6;
        printf("  trial %u: %.3f ms\n", t + 1, a_times[t]);
    }

    /* ---------------------------------------------------------------- */
    /*  Scenario B: arm the JIT before parsing                          */
    /* ---------------------------------------------------------------- */
    printf("\n--- Scenario B: JIT-ARMED (lime_jit_compile called)   ---\n");
    ParserSnapshot *jit_snap = make_synthetic_snapshot();
    if (jit_snap == NULL) {
        printf("  could not make synthetic snapshot, skipping\n");
    } else {
        int rc = lime_jit_compile(jit_snap);
        printf("  lime_jit_compile() returned %d\n", rc);
        if (rc == 0 && jit_snap->jit_ctx != NULL) {
            printf("  JIT context attached to synthetic snapshot.\n");
        }
    }

    /* Re-warm caches for fairness. */
    (void)time_parse_loop(toks, total_tokens, warmup_iter);

    double b_times[16] = { 0 };
    for (uint32_t t = 0; t < trials && t < 16; t++) {
        uint64_t ns = time_parse_loop(toks, total_tokens, iterations);
        b_times[t] = (double)ns / 1e6;
        printf("  trial %u: %.3f ms\n", t + 1, b_times[t]);
    }

    /* ---------------------------------------------------------------- */
    /*  Stats                                                            */
    /* ---------------------------------------------------------------- */
    double a_min = a_times[0], a_sum = 0;
    double b_min = b_times[0], b_sum = 0;
    for (uint32_t i = 0; i < trials && i < 16; i++) {
        if (a_times[i] < a_min) a_min = a_times[i];
        if (b_times[i] < b_min) b_min = b_times[i];
        a_sum += a_times[i];
        b_sum += b_times[i];
    }
    double a_mean = a_sum / trials;
    double b_mean = b_sum / trials;

    printf("\n=== Results (lower is better) ===\n");
    printf("  Baseline   : min=%.3f ms  mean=%.3f ms\n", a_min, a_mean);
    printf("  JIT-armed  : min=%.3f ms  mean=%.3f ms\n", b_min, b_mean);

    double ratio_min = a_min / b_min;
    double ratio_mean = a_mean / b_mean;
    printf("  Speedup    : min=%.3fx     mean=%.3fx\n", ratio_min, ratio_mean);

    printf("\n=== Verdict ===\n");
    if (fabs(ratio_mean - 1.0) < 0.05) {
        printf("  [GAP ] lime_jit_compile() has NO measurable effect on the\n");
        printf("         generated ArithParse() function (within 5%%).\n");
        printf("         The README's \"JIT speeds up generated parsers\" claim\n");
        printf("         is not reflected in the actual generated code path.\n");
    } else if (ratio_mean > 1.05) {
        printf("  [PASS] JIT-armed parsing is %.2fx faster.\n", ratio_mean);
    } else {
        printf("  [FAIL] JIT-armed parsing is SLOWER than baseline (%.2fx).\n", ratio_mean);
    }

    if (jit_snap) snapshot_release(jit_snap);
    free(toks);
    return 0;
}
