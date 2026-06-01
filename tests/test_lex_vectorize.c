/*
** tests/test_lex_vectorize.c -- coverage + structural assertions
** for the --lex-vectorize / --lex-no-vectorize C-emit paths.
**
** What this test verifies:
**
**   1. Default `lime -X` (no flag) emits the multiversion-at-
**      tokenize SIMD architecture.  Asserts the emitted .c file
**      contains:
**        - per-state scan helpers (avx2 + scalar at minimum)
**        - per-kind <prefix>_match_avx2 / _scalar functions
**        - the public <prefix>_match dispatcher with
**          __builtin_cpu_supports("avx2")
**
**   2. `lime -X --lex-no-vectorize` emits the legacy single-
**      function scalar driver.  Asserts the emitted .c file does
**      NOT contain the multiversion symbols.
**
**   3. Both modes compile cleanly (we don't run the compiler in-
**      process; we just assert lime exits 0 and the .c file is
**      well-formed enough to grep, which is the same contract
**      tests/test_emit_rust_lex.c provides for the Rust emit).
**
** This test deliberately does NOT assert the legacy emit is byte-
** identical to the pre-v0.8.10 output -- the legacy path is
** preserved verbatim in lex_emit.c so that contract is enforced
** by code review, not test fixtures.
**
** Skipped at runtime when no LIME_BIN is available (matches the
** test_emit_rust_lex skip semantics).
*/

#include "test_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* JSON-shape grammar.  STR/string-body rule is the canonical
** fast-path candidate (>= 240 self-loop bytes, exit set is
** {", \\}).  Use this so the SIMD emit's output contains
** Json_scan_INITIAL_<S>_avx2 etc. */
static const char *kJsonLex =
    "%name_prefix Tlv.\n"
    "rule lbrace   matches /\\{/        { /* */ }\n"
    "rule rbrace   matches /\\}/        { /* */ }\n"
    "rule lbracket matches /\\[/        { /* */ }\n"
    "rule rbracket matches /\\]/        { /* */ }\n"
    "rule colon    matches /:/         { /* */ }\n"
    "rule comma    matches /,/         { /* */ }\n"
    "rule t        matches /true/      { /* */ }\n"
    "rule f        matches /false/     { /* */ }\n"
    "rule nv       matches /null/      { /* */ }\n"
    "rule num      matches /-?(0|[1-9][0-9]*)(\\.[0-9]+)?([eE][+-]?[0-9]+)?/ { /* */ }\n"
    "rule str      matches /\"([^\"\\\\]|\\\\.)*\"/ { /* */ }\n"
    "rule ws       matches /[ \\t\\n\\r]+/ { /* */ }\n";

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int run_lime(const char *lime_bin, const char *flag,
                    const char *fixture_path) {
    char *argv[7] = { (char *)lime_bin, "-X", "-d.", NULL, NULL, NULL, NULL };
    int n = 3;
    if (flag) argv[n++] = (char *)flag;
    argv[n++] = (char *)fixture_path;
    argv[n] = NULL;
    int rc = 0;
    if (test_compat_run(argv, &rc) != 0) {
        fprintf(stderr, "  spawn failed for flag=%s\n", flag ? flag : "(none)");
        return -1;
    }
    return rc;
}

/* Slurp file contents into a heap buffer (caller frees). */
static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = 0;
    fclose(f);
    return buf;
}

#define EXPECT_PRESENT(haystack, needle) do { \
    if (strstr((haystack), (needle)) == NULL) { \
        fprintf(stderr, "  FAIL %s:%d: emitted .c missing \"%s\"\n", \
                __func__, __LINE__, (needle)); \
        fails++; \
    } \
} while (0)

#define EXPECT_ABSENT(haystack, needle) do { \
    if (strstr((haystack), (needle)) != NULL) { \
        fprintf(stderr, "  FAIL %s:%d: emitted .c contains forbidden \"%s\"\n", \
                __func__, __LINE__, (needle)); \
        fails++; \
    } \
} while (0)

int main(int argc, char **argv) {
    const char *lime_bin = (argc > 1) ? argv[1] : getenv("LIME_BIN");
    if (lime_bin == NULL || lime_bin[0] == 0) {
        fprintf(stderr, "SKIP: no LIME_BIN\n");
        return 77;
    }

    char tmpdir[256];
    if (test_compat_tmpdir("lime_lex_vectorize", tmpdir, sizeof(tmpdir)) != 0) {
        fprintf(stderr, "FAIL: tmpdir\n");
        return 1;
    }
    if (chdir(tmpdir) != 0) { perror("chdir"); return 1; }

    FILE *f = fopen("tlv.lex", "wb");
    if (f == NULL) { perror("fopen"); return 1; }
    fputs(kJsonLex, f);
    fclose(f);

    int fails = 0;

    /* ---- 1. Default (--lex-vectorize is on by default). ---- */
    unlink("tlv_lex.c");
    unlink("tlv_lex.h");
    int rc1 = run_lime(lime_bin, NULL, "tlv.lex");
    if (rc1 != 0) {
        fprintf(stderr, "  FAIL: default lime exit %d\n", rc1);
        fails++;
    } else if (!file_exists("tlv_lex.c") || !file_exists("tlv_lex.h")) {
        fprintf(stderr, "  FAIL: default lime didn't emit tlv_lex.c/.h\n");
        fails++;
    } else {
        char *src = slurp("tlv_lex.c");
        if (!src) {
            fprintf(stderr, "  FAIL: can't slurp tlv_lex.c\n");
            fails++;
        } else {
            /* Multiversion architecture markers. */
            EXPECT_PRESENT(src, "Tlv_match_avx2");
            EXPECT_PRESENT(src, "Tlv_match_scalar");
            EXPECT_PRESENT(src, "__builtin_cpu_supports(\"avx2\")");
            EXPECT_PRESENT(src, "target(\"avx2\")");
            EXPECT_PRESENT(src, "<immintrin.h>");
            /* Fast-path scanner for the JSON string body (state \"
            ** -dominated self-loop, exits on \" or \\). */
            EXPECT_PRESENT(src, "_mm256_cmpeq_epi8");
            EXPECT_PRESENT(src, "_mm256_movemask_epi8");
            /* NEON path for aarch64 should also be present (guarded). */
            EXPECT_PRESENT(src, "<arm_neon.h>");
            EXPECT_PRESENT(src, "Tlv_match_neon");
            EXPECT_PRESENT(src, "vceqq_u8");
            /* Public dispatcher signature unchanged. */
            EXPECT_PRESENT(src, "int Tlv_match(int state");
            free(src);
        }
        if (fails == 0) {
            printf("  PASS: default emit ships multiversion SIMD architecture\n");
        }
    }

    /* ---- 2. --lex-no-vectorize emits legacy scalar driver. ---- */
    int saved_fails = fails;
    unlink("tlv_lex.c");
    unlink("tlv_lex.h");
    int rc2 = run_lime(lime_bin, "--lex-no-vectorize", "tlv.lex");
    if (rc2 != 0) {
        fprintf(stderr, "  FAIL: --lex-no-vectorize lime exit %d\n", rc2);
        fails++;
    } else if (!file_exists("tlv_lex.c") || !file_exists("tlv_lex.h")) {
        fprintf(stderr, "  FAIL: --lex-no-vectorize didn't emit tlv_lex.c/.h\n");
        fails++;
    } else {
        char *src = slurp("tlv_lex.c");
        if (!src) {
            fprintf(stderr, "  FAIL: can't slurp tlv_lex.c\n");
            fails++;
        } else {
            /* No multiversion architecture markers. */
            EXPECT_ABSENT(src, "Tlv_match_avx2");
            EXPECT_ABSENT(src, "Tlv_match_scalar");
            EXPECT_ABSENT(src, "Tlv_match_neon");
            EXPECT_ABSENT(src, "__builtin_cpu_supports");
            EXPECT_ABSENT(src, "target(\"avx2\")");
            EXPECT_ABSENT(src, "<immintrin.h>");
            EXPECT_ABSENT(src, "_mm256_cmpeq_epi8");
            EXPECT_ABSENT(src, "<arm_neon.h>");
            /* Public symbol still present. */
            EXPECT_PRESENT(src, "int Tlv_match(int state");
            free(src);
        }
        if (fails == saved_fails) {
            printf("  PASS: --lex-no-vectorize emits legacy scalar driver\n");
        }
    }

    /* ---- 3. Explicit --lex-vectorize is the same as default. ---- */
    saved_fails = fails;
    unlink("tlv_lex.c");
    unlink("tlv_lex.h");
    int rc3 = run_lime(lime_bin, "--lex-vectorize", "tlv.lex");
    if (rc3 != 0) {
        fprintf(stderr, "  FAIL: --lex-vectorize lime exit %d\n", rc3);
        fails++;
    } else if (!file_exists("tlv_lex.c")) {
        fprintf(stderr, "  FAIL: --lex-vectorize didn't emit tlv_lex.c\n");
        fails++;
    } else {
        char *src = slurp("tlv_lex.c");
        if (!src) {
            fprintf(stderr, "  FAIL: can't slurp tlv_lex.c\n");
            fails++;
        } else {
            EXPECT_PRESENT(src, "Tlv_match_avx2");
            EXPECT_PRESENT(src, "__builtin_cpu_supports(\"avx2\")");
            free(src);
        }
        if (fails == saved_fails) {
            printf("  PASS: --lex-vectorize matches default (multiversion)\n");
        }
    }

    test_compat_chdir_temp();
    test_compat_rmdir_recursive(tmpdir);

    printf("\nResults: %d failure(s)\n", fails);
    return fails == 0 ? 0 : 1;
}
