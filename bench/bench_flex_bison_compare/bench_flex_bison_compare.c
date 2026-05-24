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
#include "lime_time.h"
#include <time.h>
#include <sys/resource.h>

#include "parser.h"
#include "parse_context.h"
#include "snapshot.h"
#include "snapshot_build.h"
#include "bench_arith_grammar.h"
#include "bench_arith.tab.h"
#include "bench_json_grammar.h"
/* bench_json.tab.h is intentionally not included here -- it would
** conflict with bench_json_grammar.h (both define JSON_LBRACE etc.).
** The harness drives the Lime parser using the Lime token IDs from
** bench_json_grammar.h; the Bison side runs through json_parse +
** json_lex which reference bench_json.tab.h internally. */

extern ParserSnapshot *ArithBuildSnapshot(void);
extern ParserSnapshot *JsonParserBuildSnapshot(void);

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

/* JSON: parallel parser/lexer entry points (api.prefix {json_}). */
extern int json_parse(void);
extern int json_lex(void);
extern struct yy_buffer_state *json__scan_string(const char *str);
extern void json__delete_buffer(struct yy_buffer_state *b);

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
    LIME_USE(total);
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
    LIME_USE(total);
    return (double)(t1 - t0) / 1e6;
}

/* ------------------------------------------------------------------ */
/*  JSON benchmark: real-world workload with object/array nesting     */
/* ------------------------------------------------------------------ */
/*
** Both sides of the JSON benchmark perform an honest lex-and-parse
** cycle on the same input string.  The Bison side runs Flex through
** json__scan_string + json_parse.  The Lime side runs a small
** hand-rolled tokenizer in a loop, feeding tokens to parse_token
** as it goes.  This is structurally apples-to-apples: both pay for
** lex, both pay for parse.
**
** The arithmetic benchmark above is parser-only on the Lime side
** (the input is pre-tokenized) so it slightly under-counts Lime's
** end-to-end cost; the JSON benchmark is the more honest one.
*/

static const char *kJsonInput =
    "{\n"
    "  \"name\": \"acme\",\n"
    "  \"version\": 12,\n"
    "  \"active\": true,\n"
    "  \"deleted\": false,\n"
    "  \"parent\": null,\n"
    "  \"deps\": [\"libfoo\", \"libbar\", \"libbaz\"],\n"
    "  \"counts\": [1, 2, 3, 4, 5, 6, 7, 8, 9, 10],\n"
    "  \"meta\": {\n"
    "    \"author\": \"alice\",\n"
    "    \"reviewers\": [\"bob\", \"carol\", \"dave\"],\n"
    "    \"score\": 0.95,\n"
    "    \"flags\": {\n"
    "      \"verified\": true,\n"
    "      \"public\": true,\n"
    "      \"deprecated\": false\n"
    "    }\n"
    "  },\n"
    "  \"tags\": [\n"
    "    {\"key\": \"env\", \"value\": \"prod\"},\n"
    "    {\"key\": \"region\", \"value\": \"us-west-2\"},\n"
    "    {\"key\": \"team\", \"value\": \"platform\"}\n"
    "  ]\n"
    "}";

/*
** Hand-rolled JSON tokenizer for the Lime side.  Returns one of
** the JSON_* token codes (from bench_json_grammar.h) for each
** lexeme; advances *pp past the consumed input; returns 0 at end of
** string.  Returns -1 on lex error (silent: caller treats as EOF).
*/
static int lime_json_next_token(const char **pp) {
    const char *p = *pp;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p == '\0') {
        *pp = p;
        return 0;
    }
    int tok;
    switch (*p) {
        case '{': tok = JSON_LBRACE;   p++; break;
        case '}': tok = JSON_RBRACE;   p++; break;
        case '[': tok = JSON_LBRACKET; p++; break;
        case ']': tok = JSON_RBRACKET; p++; break;
        case ',': tok = JSON_COMMA;    p++; break;
        case ':': tok = JSON_COLON;    p++; break;
        case '"':
            p++;
            while (*p != '\0' && *p != '"') {
                if (*p == '\\' && p[1] != '\0') p += 2;
                else p++;
            }
            if (*p == '"') p++;
            tok = JSON_STRING;
            break;
        case 't':
            if (p[1] == 'r' && p[2] == 'u' && p[3] == 'e') {
                p += 4;
                tok = JSON_TRUE;
            } else {
                *pp = p + 1;
                return -1;
            }
            break;
        case 'f':
            if (p[1] == 'a' && p[2] == 'l' && p[3] == 's' && p[4] == 'e') {
                p += 5;
                tok = JSON_FALSE;
            } else {
                *pp = p + 1;
                return -1;
            }
            break;
        case 'n':
            if (p[1] == 'u' && p[2] == 'l' && p[3] == 'l') {
                p += 4;
                tok = JSON_NULL;
            } else {
                *pp = p + 1;
                return -1;
            }
            break;
        default:
            if ((*p >= '0' && *p <= '9') || *p == '-') {
                if (*p == '-') p++;
                while (*p >= '0' && *p <= '9') p++;
                if (*p == '.') {
                    p++;
                    while (*p >= '0' && *p <= '9') p++;
                }
                if (*p == 'e' || *p == 'E') {
                    p++;
                    if (*p == '+' || *p == '-') p++;
                    while (*p >= '0' && *p <= '9') p++;
                }
                tok = JSON_NUMBER;
            } else {
                *pp = p + 1;
                return -1;
            }
            break;
    }
    *pp = p;
    return tok;
}

static double bench_lime_json(ParserSnapshot *snap, int iters) {
    uint64_t t0 = now_ns();
    int total = 0;
    for (int i = 0; i < iters; i++) {
        ParseContext *ctx = parse_begin(snap);
        const char *p = kJsonInput;
        int last = 0;
        for (;;) {
            int tok = lime_json_next_token(&p);
            if (tok <= 0) break;
            last = parse_token(ctx, tok, NULL, -1);
        }
        last = parse_token(ctx, 0, NULL, -1);
        total += last;
        parse_end(ctx);
    }
    uint64_t t1 = now_ns();
    LIME_USE(total);
    return (double)(t1 - t0) / 1e6;
}

static double bench_bison_json(int iters) {
    uint64_t t0 = now_ns();
    int total = 0;
    for (int i = 0; i < iters; i++) {
        struct yy_buffer_state *buf = json__scan_string(kJsonInput);
        total += json_parse();
        json__delete_buffer(buf);
    }
    uint64_t t1 = now_ns();
    LIME_USE(total);
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

    /* ============================================================== */
    /*  JSON benchmark: same harness, real-world workload             */
    /* ============================================================== */
    printf("\n\n");
    printf("JSON benchmark (lex + parse, fair on both sides)\n");
    printf("================================================\n");
    printf("Input:  %zu-byte JSON document with nested objects/arrays\n",
           strlen(kJsonInput));
    printf("Iterations per trial: %d   Trials: %d\n\n", iters, trials);

    ParserSnapshot *jsnap = JsonParserBuildSnapshot();
    if (jsnap == NULL) {
        fprintf(stderr, "JsonParserBuildSnapshot returned NULL\n");
        return 1;
    }

    printf("tool,trial,duration_ms,rss_kb,cpu_us\n");

    double jl_min = 1e18, jb_min = 1e18, jj_min = 1e18;
    double jl_sum = 0, jb_sum = 0, jj_sum = 0;

    for (int t = 0; t < trials; t++) {
        double ms = bench_lime_json(jsnap, iters);
        Sample s = sample_capture((uint64_t)(ms * 1e6));
        printf("lime_json,%d,%.3f,%ld,%ld\n", t + 1, ms, s.rss_kb, s.cpu_us);
        if (ms < jl_min) jl_min = ms;
        jl_sum += ms;
    }
    for (int t = 0; t < trials; t++) {
        double ms = bench_bison_json(iters);
        Sample s = sample_capture((uint64_t)(ms * 1e6));
        printf("bison_json,%d,%.3f,%ld,%ld\n", t + 1, ms, s.rss_kb, s.cpu_us);
        if (ms < jb_min) jb_min = ms;
        jb_sum += ms;
    }

    int jit_rc = lime_jit_compile(jsnap);
    if (jit_rc == 0) {
        for (int t = 0; t < trials; t++) {
            double ms = bench_lime_json(jsnap, iters);
            Sample s = sample_capture((uint64_t)(ms * 1e6));
            printf("lime_json+jit,%d,%.3f,%ld,%ld\n", t + 1, ms, s.rss_kb, s.cpu_us);
            if (ms < jj_min) jj_min = ms;
            jj_sum += ms;
        }
    }

    printf("\n=== JSON Summary (lower is better) ===\n");
    printf("  lime_json     min=%.2f ms mean=%.2f ms\n", jl_min, jl_sum / trials);
    printf("  bison_json    min=%.2f ms mean=%.2f ms\n", jb_min, jb_sum / trials);
    if (jit_rc == 0) {
        printf("  lime_json+jit min=%.2f ms mean=%.2f ms\n", jj_min, jj_sum / trials);
    }
    printf("\n  speedup vs bison_json (lime_json mean):     %.2fx\n",
           (jb_sum / trials) / (jl_sum / trials));
    if (jit_rc == 0) {
        printf("  speedup vs bison_json (lime_json+jit mean): %.2fx\n",
               (jb_sum / trials) / (jj_sum / trials));
    }

    snapshot_release(jsnap);
    return 0;
}
