/*
** bench_lex_state_count.c -- compare lexer throughput across DFAs
** of different sizes to evaluate when memory-side optimisations
** (prefetch hints, table-layout changes, hugepage backing) might
** help.
**
** Why this benchmark
** ------------------
** A single-state-per-token-class DFA's transition table fits in L1
** (~32 KB).  As keyword count grows, more states are emitted, the
** table grows, and L1 misses start to bite.  This bench lets us
** measure that knee in a controlled way -- if MB/s drops sharply
** between, say, 100 and 200 states on the same hardware, the
** transition-table is the bottleneck and prefetch / table-layout
** optimisations have headroom.
**
** Design
** ------
** Compile-time-parameterised over the lexer header + symbol prefix,
** mirroring the bench_lex_vectorize harness.  Each variant reports:
**
**   - Generated DFA state count (from a header constant)
**   - Lexer transition-table size (computed from state count)
**   - Throughput in MB/s on a 1 MB synthetic input
**
** Usage:
**   bench_lex_state_count <variant-name>
**
** The harness prints a single CSV-friendly line per run so a driver
** can sweep all variants and emit a comparison table.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "lime_time.h"

#ifndef BENCH_LABEL
#error "BENCH_LABEL must be defined by the build system"
#endif
#ifndef BENCH_HEADER
#error "BENCH_HEADER must be defined by the build system"
#endif
#ifndef BENCH_PREFIX
#error "BENCH_PREFIX must be defined by the build system"
#endif

#include BENCH_HEADER

/* Token-pasting helpers to mint the right symbol names. */
#define _PASTE(a, b) a##b
#define PASTE(a, b)  _PASTE(a, b)
/* The %name_prefix in the .lex file controls the C symbol prefix.
** State name macros are emitted in upper case (BENCH_PREFIX_UPPER),
** while function names use the case-preserved form. */
#define MATCH_FN     PASTE(BENCH_PREFIX, _match)
#define INITIAL_STATE_NAME  PASTE(BENCH_PREFIX_UPPER, _STATE_INITIAL)

/* Compute a corpus of ~size_kb KB by repeating the input pattern. */
static char *make_corpus(const char *seed, size_t seed_len, size_t size_kb) {
    size_t target = size_kb * 1024;
    char *buf = malloc(target + 64);
    if (!buf) return NULL;
    size_t p = 0;
    while (p < target) {
        size_t cp = (target - p < seed_len) ? (target - p) : seed_len;
        memcpy(buf + p, seed, cp);
        p += cp;
    }
    buf[p] = 0;
    return buf;
}

int main(int argc, char **argv) {
    /* Pick a workload appropriate to the lexer.  bootscanner is
    ** SQL-shaped so feed it SQL tokens; json variants get JSON
    ** tokens.  The seed pattern is small but the corpus is large
    ** so timer noise is negligible. */
    const char *seed;
    size_t seed_len;

    if (strcmp(BENCH_LABEL, "bootscanner") == 0) {
        seed = "CREATE TABLE foo ( id integer , name text ) ; "
               "INSERT INTO foo VALUES ( 1 , 'hello' ) ; "
               "SELECT * FROM foo WHERE id = 1 ; ";
        seed_len = strlen(seed);
    } else {
        seed = "{\"id\":1,\"name\":\"hello\",\"tags\":[\"a\",\"b\",\"c\"],"
               "\"meta\":{\"k\":\"v\"},\"vals\":[1.5,2.5,3.5,4.5]}\n";
        seed_len = strlen(seed);
    }

    const size_t corpus_kb = 1024;  /* 1 MB */
    char *corpus = make_corpus(seed, seed_len, corpus_kb);
    if (!corpus) { fprintf(stderr, "OOM\n"); return 1; }

    const size_t corpus_bytes = corpus_kb * 1024;

    /* Warmup */
    {
        const uint8_t *p = (const uint8_t *)corpus;
        size_t pos = 0;
        while (pos < corpus_bytes) {
            int rule = -1; size_t consumed = 0;
            int ok = MATCH_FN(INITIAL_STATE_NAME, p + pos,
                              corpus_bytes - pos, &rule, &consumed);
            if (!ok || consumed == 0) { pos++; continue; }
            pos += consumed;
        }
    }

    /* Timed runs: best of 5. */
    const int trials = 5;
    uint64_t best_ns = (uint64_t)-1;
    uint64_t total_tokens = 0;
    for (int t = 0; t < trials; t++) {
        const uint8_t *p = (const uint8_t *)corpus;
        size_t pos = 0;
        uint64_t tokens = 0;
        uint64_t t0 = lime_now_ns();
        while (pos < corpus_bytes) {
            int rule = -1; size_t consumed = 0;
            int ok = MATCH_FN(INITIAL_STATE_NAME, p + pos,
                              corpus_bytes - pos, &rule, &consumed);
            if (!ok || consumed == 0) { pos++; continue; }
            pos += consumed;
            tokens++;
        }
        uint64_t ns = lime_now_ns() - t0;
        if (ns < best_ns) best_ns = ns;
        total_tokens = tokens;
    }

    double mb_per_sec = (double)corpus_bytes / 1024.0 / 1024.0
                        * 1e9 / (double)best_ns;

    /* CSV-friendly output: variant, throughput_mbps, tokens, best_ns */
    printf("%s,%.1f,%llu,%llu\n",
           BENCH_LABEL, mb_per_sec,
           (unsigned long long)total_tokens,
           (unsigned long long)best_ns);

    free(corpus);
    return 0;
}
