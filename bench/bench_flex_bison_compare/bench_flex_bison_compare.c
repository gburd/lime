/*
** bench_flex_bison_compare.c
**
** Apples-to-apples performance comparison of three implementations of
** an identical arithmetic grammar:
**
**   1. Lime + hand tokenizer  -- runtime parse_token engine driving
**                                ArithBuildSnapshot()'s tables.
**   2. Lime + JIT             -- same Lime parser with
**                                lime_jit_compile() called.  Lime's JIT
**                                presently accelerates the batch parse
**                                path; see docs/ROADMAP.md for the
**                                runtime-engine integration item.
**   3. Bison + Flex           -- the standard Yacc/Lex pipeline on
**                                the same grammar.
**
** Output is CSV-formatted: tool,trial,duration_ms,rss_kb,cpu_us
**
** Each tool parses the same input string a fixed number of times so
** mean/min/max can be derived externally if needed.  The summary at
** the bottom prints speedup ratios.
**
** Requires Flex and Bison; the meson custom_targets build the Bison
** parser and Flex lexer, and skip the binary entirely when either
** tool is missing.
*/

#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/resource.h>

#include "parser.h"
#include "parse_context.h"
#include "snapshot.h"
#include "snapshot_build.h"
#include "bench_arith_grammar.h"
#include "bench_arith.tab.h"

extern ParserSnapshot *ArithBuildSnapshot(void);

/* Bison parser entry. */
extern int bison_parse(void);
extern int bison_lex(void);

/* Flex lexer state.  yy_scan_string() is a flex-emitted helper that
** scans from a NUL-terminated string buffer; with %option prefix="bison_"
** it is renamed to bison__scan_string. */
struct yy_buffer_state;
extern struct yy_buffer_state *bison__scan_string(const char *str);
extern void bison__delete_buffer(struct yy_buffer_state *b);

/* Bison's %define api.prefix {bison_} renames the yylval object to
** bison_lval; the lexer assigns into it on NUM tokens. */
extern int bison_lval;

/* ------------------------------------------------------------------ */
/*  Timing helpers                                                      */
/* ------------------------------------------------------------------ */

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

typedef struct Sample {
    double ms;
    long rss_kb;
    long cpu_us;
} Sample;

static Sample sample_capture(uint64_t ns_elapsed) {
    Sample s = {0};
    s.ms = (double)ns_elapsed / 1e6;

    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
        /* Need <sys/resource.h> for ru_maxrss; on macOS it's in
        ** bytes, on Linux in KB; normalize to KB.  Some toolchains
        ** require _DARWIN_C_SOURCE / _GNU_SOURCE for the field to be
        ** visible -- _POSIX_C_SOURCE alone hides it. */
#if defined(__APPLE__)
        s.rss_kb = (long)((unsigned long)ru.ru_maxrss / 1024UL);
#else
        s.rss_kb = ru.ru_maxrss;
#endif
        s.cpu_us = ru.ru_utime.tv_sec * 1000000 + ru.ru_utime.tv_usec;
    }
    return s;
}

/* ------------------------------------------------------------------ */
/*  Lime runtime-engine driver                                          */
/* ------------------------------------------------------------------ */

/*
** Lime input uses the ARITH_* token codes from bench_arith_grammar.h;
** Bison input is an unparsed string the Flex lexer tokenises.  We
** therefore drive each parser with its native input format and
** ensure both observe the same logical sequence of tokens.
*/
typedef struct LimeTok {
    int code;
} LimeTok;

/* "(1 + 2) * (3 + 4) - 5" */
static const LimeTok lime_input[] = {
    {ARITH_LP},   {ARITH_NUM}, {ARITH_PLUS}, {ARITH_NUM},  {ARITH_RP},
    {ARITH_STAR}, {ARITH_LP},  {ARITH_NUM},  {ARITH_PLUS}, {ARITH_NUM},
    {ARITH_RP},   {ARITH_MINUS}, {ARITH_NUM},
};
static const int lime_input_n = (int)(sizeof(lime_input) / sizeof(lime_input[0]));

static double bench_lime(ParserSnapshot *snap, int iters) {
    uint64_t t0 = now_ns();
    int total = 0;
    for (int i = 0; i < iters; i++) {
        ParseContext *ctx = parse_begin(snap);
        int last = 0;
        for (int k = 0; k < lime_input_n; k++) {
            last = parse_token(ctx, lime_input[k].code, NULL, -1);
        }
        last = parse_token(ctx, 0, NULL, -1);
        total += last;
        parse_end(ctx);
    }
    uint64_t t1 = now_ns();
    __asm__ volatile("" : : "r"(total));
    return (double)(t1 - t0) / 1e6;
}

/* ------------------------------------------------------------------ */
/*  Bison driver                                                        */
/* ------------------------------------------------------------------ */

static const char *bison_input_str = "(1 + 2) * (3 + 4) - 5";

static double bench_bison(int iters) {
    uint64_t t0 = now_ns();
    int total = 0;
    for (int i = 0; i < iters; i++) {
        struct yy_buffer_state *buf = bison__scan_string(bison_input_str);
        total += bison_parse();
        bison__delete_buffer(buf);
    }
    uint64_t t1 = now_ns();
    __asm__ volatile("" : : "r"(total));
    return (double)(t1 - t0) / 1e6;
}

/* ------------------------------------------------------------------ */
/*  Main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    int iters = 100000;
    int trials = 5;
    if (argc > 1) iters = atoi(argv[1]);
    if (argc > 2) trials = atoi(argv[2]);

    printf("Lime vs Flex+Bison comparison\n");
    printf("=============================\n");
    printf("Input: \"%s\"\n", bison_input_str);
    printf("Iterations per trial: %d   Trials: %d\n\n", iters, trials);

    ParserSnapshot *snap = ArithBuildSnapshot();
    if (snap == NULL) {
        fprintf(stderr, "ArithBuildSnapshot returned NULL\n");
        return 1;
    }

    printf("tool,trial,duration_ms,rss_kb,cpu_us\n");

    double lime_min = 1e18, bison_min = 1e18;
    double lime_sum = 0,    bison_sum = 0;

    for (int t = 0; t < trials; t++) {
        double ms = bench_lime(snap, iters);
        Sample s = sample_capture((uint64_t)(ms * 1e6));
        printf("lime,%d,%.3f,%ld,%ld\n", t + 1, ms, s.rss_kb, s.cpu_us);
        if (ms < lime_min) lime_min = ms;
        lime_sum += ms;
    }

    for (int t = 0; t < trials; t++) {
        double ms = bench_bison(iters);
        Sample s = sample_capture((uint64_t)(ms * 1e6));
        printf("bison,%d,%.3f,%ld,%ld\n", t + 1, ms, s.rss_kb, s.cpu_us);
        if (ms < bison_min) bison_min = ms;
        bison_sum += ms;
    }

    /* JIT-armed Lime: compile and rerun. */
    int rc = lime_jit_compile(snap);
    double jit_min = 1e18, jit_sum = 0;
    if (rc == 0) {
        for (int t = 0; t < trials; t++) {
            double ms = bench_lime(snap, iters);
            Sample s = sample_capture((uint64_t)(ms * 1e6));
            printf("lime+jit,%d,%.3f,%ld,%ld\n", t + 1, ms, s.rss_kb, s.cpu_us);
            if (ms < jit_min) jit_min = ms;
            jit_sum += ms;
        }
    }

    printf("\n=== Summary (lower is better) ===\n");
    printf("  lime     min=%.2f ms mean=%.2f ms\n", lime_min,  lime_sum / trials);
    printf("  bison    min=%.2f ms mean=%.2f ms\n", bison_min, bison_sum / trials);
    if (rc == 0) {
        printf("  lime+jit min=%.2f ms mean=%.2f ms\n", jit_min, jit_sum / trials);
    }
    printf("\n  speedup vs bison (lime mean):     %.2fx\n",
           (bison_sum / trials) / (lime_sum / trials));
    if (rc == 0) {
        printf("  speedup vs bison (lime+jit mean): %.2fx\n",
               (bison_sum / trials) / (jit_sum / trials));
    }

    snapshot_release(snap);
    return 0;
}
