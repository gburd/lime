/*
** bench_simd_classify.c -- micro-benchmark that proves the SIMD
** character-classification path actually delivers speedup over the
** scalar fallback.
**
** Both implementations live in src/tokenize_simd.c and consume the
** same input buffer.  This benchmark calls each one in tight loops
** over a representative SQL/source-text corpus and reports
** ns/32-char-block plus the SIMD-vs-scalar ratio.
**
** Why this benchmark exists: the README and docs/API.md document
** SIMD acceleration as a real feature, but the SIMD path is only
** wired into src/tokenize.c (the runtime SQL tokenizer) and the
** generated lexer pipeline.  Without this microbenchmark there is no
** repeatable signal that classify_simd_* is actually faster than
** classify_scalar; the broader parser-bench is dominated by parse
** logic and would mask the difference.
**
** This bench therefore documents the *floor* speedup the SIMD path
** delivers over the scalar fallback at the inner loop.  Real-world
** tokenizer wins are larger because tokenize.c amortises the
** classification call over identifier/whitespace runs.
*/

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "lime_time.h"

#include "tokenize_simd.h"

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/*
** Build a representative source-text corpus of size `n` bytes.  Mixes
** identifiers, keywords, numbers, whitespace, and operators -- the
** kind of input the runtime SQL tokenizer eats.
*/
static char *make_corpus(size_t n) {
    char *buf = malloc(n + 64);  /* +64 for SIMD over-read safety */
    if (buf == NULL) return NULL;

    static const char *snippets[] = {
        "SELECT * FROM ",  "WHERE id = ",     "JOIN users ON ",  "GROUP BY name",
        " AND active=1 ",  "INSERT INTO ",    "UPDATE t SET ",   "DELETE FROM t",
        "ORDER BY ",       "LIMIT 100 OFF ",  "CREATE TABLE ",   "DROP INDEX ",
        " PRIMARY KEY ",   " VARCHAR(255) ",  " 12345 67890 ",   " 0xCAFEBABE ",
    };

    size_t pos = 0;
    uint32_t r = 0xC001D00D;
    while (pos < n) {
        r = r * 1103515245u + 12345u;
        const char *s = snippets[r >> 28];
        size_t len = strlen(s);
        if (pos + len > n) len = n - pos;
        memcpy(buf + pos, s, len);
        pos += len;
    }
    /* Zero-fill the over-read region so SIMD reads past `n` are
    ** deterministic. */
    memset(buf + n, 0, 64);
    return buf;
}

/*
** Time the given classifier over `iters` passes of the corpus.
** Returns ns / 32-char-block.
*/
static double bench_classify(ClassifyFunc fn, const char *corpus, size_t n, int iters) {
    /* Each pass classifies floor(n / 32) blocks.  We accumulate the
    ** is_alpha_mask through volatile to defeat dead-code elimination
    ** without paying for it in the timed loop. */
    volatile uint32_t sink = 0;
    size_t blocks = n / 32;

    uint64_t t0 = now_ns();
    for (int i = 0; i < iters; i++) {
        uint32_t acc = 0;
        for (size_t b = 0; b < blocks; b++) {
            CharClassVector v = fn(corpus, b * 32);
            acc ^= v.is_alpha_mask ^ v.is_digit_mask ^ v.is_space_mask;
        }
        sink ^= acc;
    }
    uint64_t t1 = now_ns();
    (void)sink;

    double total_blocks = (double)blocks * (double)iters;
    return (double)(t1 - t0) / total_blocks;
}

int main(int argc, char **argv) {
    size_t n = 64 * 1024;  /* 64 KB */
    int iters = 2000;
    if (argc > 1) n = (size_t)atol(argv[1]);
    if (argc > 2) iters = atoi(argv[2]);

    printf("Lime SIMD vs scalar character-classification benchmark\n");
    printf("======================================================\n");
    printf("Corpus: %zu bytes,   Iterations: %d\n\n", n, iters);

    char *corpus = make_corpus(n);
    if (corpus == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    /* Warmup -- pull caches hot. */
    (void)bench_classify(classify_scalar, corpus, n, 100);

    /* Scalar */
    double scalar_ns = bench_classify(classify_scalar, corpus, n, iters);
    printf("  scalar : %7.2f ns / 32-byte block\n", scalar_ns);

    /* Best available SIMD path. */
    ClassifyFunc best = get_classify_func();
    const char *backend_name = "scalar";
    if (best == classify_scalar) {
        backend_name = "scalar (no SIMD on this host)";
    } else {
#if defined(__x86_64__) || defined(__i386__)
        backend_name = "AVX2";
#elif defined(__ARM_NEON)
        backend_name = "NEON";
#endif
    }

    double simd_ns = bench_classify(best, corpus, n, iters);
    printf("  %-7s: %7.2f ns / 32-byte block\n", backend_name, simd_ns);

    double speedup = scalar_ns / simd_ns;
    printf("\n  speedup: %.2fx (SIMD vs scalar)\n", speedup);

    /* Verdict */
    printf("\n=== Verdict ===\n");
    if (best == classify_scalar) {
        printf("  [INFO] No SIMD backend available on this host;\n");
        printf("         falling back to scalar.  Ratio is 1.00x by\n");
        printf("         construction.  Run on x86_64-with-AVX2 or\n");
        printf("         aarch64 to exercise the SIMD paths.\n");
    } else if (speedup >= 1.5) {
        printf("  [PASS] SIMD path delivers >=1.5x measurable speedup.\n");
    } else if (speedup >= 1.05) {
        printf("  [WARN] SIMD path is faster but below 1.5x;\n");
        printf("         likely memory-bandwidth-bound.\n");
    } else {
        printf("  [FAIL] SIMD path is not faster than scalar.\n");
        printf("         Investigate dispatch and inlining.\n");
    }

    free(corpus);
    return 0;
}
