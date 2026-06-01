/*
** bench/lex_vectorize/bench_lex_vectorize.c -- throughput
** comparison for the C emit's --lex-vectorize flag.
**
** The same source file is compiled twice via meson with different
** -D flags.  Each invocation parameterises the lexer header /
** symbol prefix:
**
**   -DBENCH_LABEL=...       label printed to stdout
**   -DBENCH_HEADER=...      relative path to the generated .h
**   -DBENCH_PREFIX=...      C-side prefix the lexer emits
**                           (Jvec for vectorised, Jnovec for not)
**
** Token-pasting macros mint the exact symbol names the bench
** loop calls (Jvec_match / JVEC_STATE_INITIAL or Jnovec_match /
** JNOVEC_STATE_INITIAL).
**
** Both binaries share the same fixture-loading path, harness loop,
** and reporting code.  Only the lexer underneath differs.
**
** Usage:  bench_lex_vec <fixture.json>     (default --lex-vectorize)
**         bench_lex_novec <fixture.json>   (--lex-no-vectorize)
**
** Exit codes:
**   0  normal completion
**   1  fixture I/O error
**   2  tokeniser error (un-tokenisable input)
**   3  bad CLI args
*/

#ifndef BENCH_LABEL
#error "BENCH_LABEL must be defined by the build system"
#endif
#ifndef BENCH_HEADER
#error "BENCH_HEADER must be defined by the build system"
#endif
#ifndef BENCH_PREFIX
#error "BENCH_PREFIX must be defined by the build system"
#endif
#ifndef BENCH_PREFIX_UPPER
#error "BENCH_PREFIX_UPPER must be defined by the build system"
#endif

#include BENCH_HEADER

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define HAVE_MMAP 1
#else
#define HAVE_MMAP 0
#endif

/* Token-paste helpers: BENCH_PREFIX(_match) -> Jvec_match etc.
** The two-step PASTE/JOIN is required so BENCH_PREFIX is
** macro-expanded before pasting (otherwise we'd literally get
** BENCH_PREFIX_match). */
#define JOIN_(a, b) a##b
#define JOIN(a, b) JOIN_(a, b)
#define LEX_MATCH JOIN(BENCH_PREFIX, _match)
#define LEX_STATE_INITIAL JOIN(BENCH_PREFIX_UPPER, _STATE_INITIAL)

/* nano-resolution monotonic clock; falls back to clock() if the
** POSIX one isn't available.  Returns seconds-since-epoch as a
** double. */
static double now_seconds(void) {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#else
    return (double)clock() / (double)CLOCKS_PER_SEC;
#endif
}

/* Read the fixture into a buffer.  Prefer mmap on POSIX so the OS
** can stream the file from disk only on the first pass; subsequent
** passes hit the page cache.  Caller frees via release_fixture(). */
static char *load_fixture(const char *path, size_t *out_len, int *is_mmap) {
    *is_mmap = 0;
#if HAVE_MMAP
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return NULL; }
    if (st.st_size == 0) { close(fd); *out_len = 0; return strdup(""); }
    void *map = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return NULL;
    *out_len = (size_t)st.st_size;
    *is_mmap = 1;
    return (char *)map;
#else
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)n);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf); fclose(f); return NULL;
    }
    fclose(f);
    *out_len = (size_t)n;
    return buf;
#endif
}

static void release_fixture(char *p, size_t len, int is_mmap) {
#if HAVE_MMAP
    if (is_mmap) { munmap(p, len); return; }
#endif
    (void)len; (void)is_mmap;
    free(p);
}

/* One full tokenization pass over `bytes`.  Counts tokens; ignores
** the matched bytes.  Returns 0 on success, non-zero on lex error. */
static int tokenize_one_pass(const char *bytes, size_t n, size_t *out_tokens) {
    size_t pos = 0;
    size_t tokens = 0;
    while (pos < n) {
        int rule = -1;
        size_t consumed = 0;
        int ok = LEX_MATCH(LEX_STATE_INITIAL, bytes + pos, n - pos,
                           &rule, &consumed);
        if (!ok || consumed == 0) {
            *out_tokens = tokens;
            return 1;
        }
        pos += consumed;
        tokens++;
    }
    *out_tokens = tokens;
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr,
                "usage: %s <fixture.json>\n"
                "  Tokenises the fixture in a tight loop and reports\n"
                "  throughput.  Build label: " BENCH_LABEL "\n",
                argv[0]);
        return 3;
    }
    const char *fixture_path = argv[1];

    size_t fixture_len = 0;
    int is_mmap = 0;
    char *fixture = load_fixture(fixture_path, &fixture_len, &is_mmap);
    if (!fixture) {
        fprintf(stderr, "%s: cannot load fixture %s: %s\n",
                argv[0], fixture_path, strerror(errno));
        return 1;
    }

    /* Sanity-check the lexer: do one pass, report token count. */
    size_t tokens = 0;
    if (tokenize_one_pass(fixture, fixture_len, &tokens) != 0) {
        fprintf(stderr, "%s: tokenizer hit unmatched input after %zu tokens\n",
                argv[0], tokens);
        release_fixture(fixture, fixture_len, is_mmap);
        return 2;
    }
    printf("// label=%s fixture=%s bytes=%zu tokens=%zu\n",
           BENCH_LABEL, fixture_path, fixture_len, tokens);

    /* Adaptive iteration count: target a few hundred ms per run for
    ** fixtures from 1 KB to 50 MB. */
    int iters = 200;
    if (fixture_len > 1024 * 1024) iters = 50;
    if (fixture_len > 5 * 1024 * 1024) iters = 20;
    if (fixture_len > 50 * 1024 * 1024) iters = 5;

    /* Warmup. */
    for (int i = 0; i < 10; i++) {
        size_t tk = 0;
        (void)tokenize_one_pass(fixture, fixture_len, &tk);
    }

    /* Best-of-3.  Empirically the third run is most stable on
    ** Linux; reporting the best of three is enough to mask warm-
    ** cache-vs-evicted variation without medianing across noisy
    ** scheduler-stutter runs. */
    double best = 1e30;
    for (int run = 0; run < 3; run++) {
        double t0 = now_seconds();
        for (int i = 0; i < iters; i++) {
            size_t tk = 0;
            (void)tokenize_one_pass(fixture, fixture_len, &tk);
        }
        double elapsed = now_seconds() - t0;
        if (elapsed < best) best = elapsed;
    }

    double per_us = best / (double)iters * 1e6;
    double mb_per_sec = ((double)fixture_len * (double)iters) /
                        (best * 1024.0 * 1024.0);
    printf("%-22s %9.3f us/parse %9.1f MB/s  (%d iter, best %.3fs)\n",
           BENCH_LABEL, per_us, mb_per_sec, iters, best);

    release_fixture(fixture, fixture_len, is_mmap);
    return 0;
}
