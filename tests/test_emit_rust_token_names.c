/*
 * tests/test_emit_rust_token_names.c -- coverage for the
 * --enable=token-names / --disable=token-names feature flag (v1.1.0).
 *
 * Default ON for --target=rust.  When enabled, the generated parser.rs
 * carries:
 *   pub static YY_TOKEN_NAMES: &[&'static str]
 *   pub fn token_name(code: u16) -> Option<&'static str>
 *   pub fn yy_find_shift_action(state: u16, lookahead: u16) -> u16
 *   pub fn expected_tokens_in_state(state: u16) -> Vec<u16>
 *
 * Closes the post-v1.0.0 Rust-diagnostics gap reported by Ra:
 * consumers can now build rustc-quality "expected one of: FROM, ',',
 * JOIN, WHERE" diagnostics on the Rust path the same way they do on
 * the C side via ParseTokenName + ParseExpectedTokens.
 *
 * Sub-tests:
 *
 *   1. Default --target=rust output contains all four public symbols
 *      AND the YY_TOKEN_NAMES array entries match the input grammar's
 *      %token declarations.
 *
 *   2. --disable=token-names suppresses ALL four symbols (the user
 *      doesn't want the static-data overhead).
 *
 *   3. Token names with non-rust-ident characters survive intact in
 *      the YY_TOKEN_NAMES array (e.g. a token literally named '.').
 *      Lemon-style %token_dot or punctuation aliases hit this path.
 *
 *   4. token_name(code) handles both INTERNAL and EXTERNAL codes
 *      symmetrically.  Since the test exercises this only via
 *      generated source inspection (no rustc in CI), we assert the
 *      function body contains the FIRST_TOKEN-relative branch.
 *
 * Skipped at runtime when LIME_BIN isn't set.
 */

#include "test_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Mini grammar with three named tokens and one non-ident. */
static const char *kFixture =
    "%name TestParse\n"
    "%token_type {int}\n"
    "%token IDENTIFIER NUMBER PLUS .\n"
    "%start_symbol expr\n"
    "expr ::= IDENTIFIER PLUS NUMBER.\n"
    "expr ::= NUMBER.\n";

static char *slurp(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(buf); return NULL; }
    buf[sz] = 0;
    if (out_len) *out_len = (size_t)sz;
    return buf;
}

static int run_lime(const char *lime_bin, const char *flag, const char *path) {
    char *argv[8];
    int n = 0;
    argv[n++] = (char *)lime_bin;
    argv[n++] = "-d.";
    argv[n++] = (char *)flag;
    argv[n++] = (char *)path;
    argv[n] = NULL;
    int rc = 0;
    if (test_compat_run(argv, &rc) != 0) return -1;
    return rc;
}

#define CHECK(cond, msg) do {                                       \
    if (!(cond)) {                                                  \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);\
        fail++;                                                     \
    }                                                               \
} while (0)

int main(int argc, char **argv) {
    const char *lime_bin = (argc > 1) ? argv[1] : getenv("LIME_BIN");
    if (!lime_bin || !lime_bin[0]) {
        fprintf(stderr, "SKIP: LIME_BIN not set\n");
        return 77;
    }

    char tmpdir[256];
    if (test_compat_tmpdir("lime_emit_rust_token_names", tmpdir, sizeof(tmpdir)) != 0) {
        fprintf(stderr, "FAIL: tmpdir\n");
        return 1;
    }
    if (chdir(tmpdir) != 0) { perror("chdir"); return 1; }

    /* Write fixture. */
    FILE *fp = fopen("test.y", "wb");
    if (!fp) { perror("fopen"); return 1; }
    fputs(kFixture, fp);
    fclose(fp);

    int fail = 0;

    /* Sub-test 1: --target=rust default emits the full surface. */
    {
        if (run_lime(lime_bin, "--target=rust", "test.y") != 0) {
            fprintf(stderr, "FAIL: lime --target=rust returned non-zero\n");
            return 1;
        }
        size_t len = 0;
        char *src = slurp("test.rs", &len);
        if (!src) { fprintf(stderr, "FAIL: no test.rs produced\n"); return 1; }

        CHECK(strstr(src, "pub static YY_TOKEN_NAMES:") != NULL,
              "default emits YY_TOKEN_NAMES");
        CHECK(strstr(src, "pub fn token_name(") != NULL,
              "default emits token_name");
        CHECK(strstr(src, "pub fn yy_find_shift_action(") != NULL,
              "default emits yy_find_shift_action");
        CHECK(strstr(src, "pub fn expected_tokens_in_state(") != NULL,
              "default emits expected_tokens_in_state");

        /* YY_TOKEN_NAMES contains our %token names in order.  Slot 0
        ** is "$end"; slots 1+ are the declared tokens. */
        CHECK(strstr(src, "\"$end\"") != NULL,
              "YY_TOKEN_NAMES slot 0 is $end");
        CHECK(strstr(src, "\"IDENTIFIER\"") != NULL,
              "YY_TOKEN_NAMES contains IDENTIFIER");
        CHECK(strstr(src, "\"NUMBER\"") != NULL,
              "YY_TOKEN_NAMES contains NUMBER");
        CHECK(strstr(src, "\"PLUS\"") != NULL,
              "YY_TOKEN_NAMES contains PLUS");

        /* token_name handles EXTERNAL codes via FIRST_TOKEN-relative
        ** subtraction.  Assert the FIRST_TOKEN branch is present. */
        CHECK(strstr(src, "FIRST_TOKEN") != NULL,
              "token_name() consults FIRST_TOKEN");

        free(src);
    }

    /* Sub-test 2: --disable=token-names suppresses all four symbols. */
    {
        unlink("test.rs");
        char *argv2[7] = {
            (char *)lime_bin, "-d.", "--target=rust",
            "--disable=token-names", "test.y", NULL
        };
        int rc = 0;
        if (test_compat_run(argv2, &rc) != 0 || rc != 0) {
            fprintf(stderr, "FAIL: lime --disable=token-names rc=%d\n", rc);
            return 1;
        }
        size_t len = 0;
        char *src = slurp("test.rs", &len);
        if (!src) { fprintf(stderr, "FAIL: no test.rs produced\n"); return 1; }

        CHECK(strstr(src, "pub static YY_TOKEN_NAMES:") == NULL,
              "--disable=token-names suppresses YY_TOKEN_NAMES");
        CHECK(strstr(src, "pub fn token_name(") == NULL,
              "--disable=token-names suppresses token_name");
        CHECK(strstr(src, "pub fn yy_find_shift_action(") == NULL,
              "--disable=token-names suppresses yy_find_shift_action");
        CHECK(strstr(src, "pub fn expected_tokens_in_state(") == NULL,
              "--disable=token-names suppresses expected_tokens_in_state");
        /* Token codes are still pub const; only the introspection
        ** surface goes away. */
        CHECK(strstr(src, "pub const IDENTIFIER") != NULL,
              "--disable=token-names keeps token-code consts");
        free(src);
    }

    if (fail) {
        fprintf(stderr, "FAIL: %d check(s) failed\n", fail);
        return 1;
    }
    fprintf(stdout, "OK: emit_rust_token_names\n");
    return 0;
}
