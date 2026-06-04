/*
 * tests/test_flag_scheme.c -- regression for the v0.8.6 CLI flag
 * redesign (--target / --enable / --disable plus deprecation
 * aliases).
 *
 * The point of this test is to lock down the user-facing flag
 * surface so the deprecation aliases keep working AND the new
 * scheme produces the expected outputs.
 *
 * Skipped at runtime when LIME_BIN isn't reachable.
 */

#include "test_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static int dir_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static const char *kGrammar =
    "%name TR\n"
    "%token A B C.\n"
    "%first_token 257\n"
    "%start_symbol s\n"
    "s ::= A.\n"
    "s ::= B.\n"
    "s ::= C.\n";

static const char *kLexFixture =
    "%name_prefix Cov.\n"
    "rule kw matches /[a-zA-Z_][a-zA-Z0-9_]*/ { /* */ }\n"
    "rule num matches /[0-9]+/ { /* */ }\n"
    "rule ws matches /[ \\t\\n\\r]+/ { /* */ }\n";

/* Run lime with a given argv and capture stderr; on success the .rs
** file path is checked.  The lime_bin argument is unused -- argv[0]
** carries the binary path -- but it's threaded through for clarity
** at call sites. */
static int run_capture(const char *lime_bin, char *const argv[],
                       char *errbuf, size_t errbuf_size,
                       int *exit_code) {
    (void)lime_bin;
    return test_compat_run_capture_stderr(argv, errbuf, errbuf_size, exit_code);
}

/* Stage a grammar fixture in cwd. */
static int write_fixture(const char *path, const char *content) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) return -1;
    int ok = fputs(content, f) >= 0;
    if (fclose(f) != 0) ok = 0;
    return ok ? 0 : -1;
}

/* Locate limpar.c the same way the other rust tests do.  Returns
** a static buffer pointer. */
static const char *resolve_template(void) {
    static char buf[1024];
    const char *t = getenv("LIME_TEMPLATE");
    if (t && access(t, R_OK) == 0) return t;
    const char *root = getenv("LIME_PROJECT_ROOT");
    if (root) {
        snprintf(buf, sizeof(buf), "%s/limpar.c", root);
        if (access(buf, R_OK) == 0) return buf;
    }
    return NULL;
}

static int g_total = 0, g_pass = 0;

#define CHECK(cond, fmt, ...) do {                                           \
    g_total++;                                                               \
    if (cond) { g_pass++; printf("  PASS: " fmt "\n", ##__VA_ARGS__); }      \
    else      { printf("  FAIL: " fmt "\n", ##__VA_ARGS__); }                \
} while (0)

int main(int argc, char **argv) {
    const char *lime_bin = (argc > 1) ? argv[1] : getenv("LIME_BIN");
    if (lime_bin == NULL || lime_bin[0] == 0) {
        fprintf(stderr, "SKIP: no LIME_BIN\n");
        return 77;
    }
    const char *tmpl = resolve_template();
    if (tmpl == NULL) {
        fprintf(stderr, "SKIP: no template\n");
        return 77;
    }

    char tmpdir[256];
    if (test_compat_tmpdir("lime_flag_scheme", tmpdir, sizeof(tmpdir)) != 0) {
        fprintf(stderr, "FAIL: tmpdir\n");
        return 1;
    }
    if (chdir(tmpdir) != 0) { perror("chdir"); return 1; }

    if (write_fixture("g.lime", kGrammar) != 0) {
        fprintf(stderr, "FAIL: write fixture\n"); return 1;
    }
    if (write_fixture("cov.lex", kLexFixture) != 0) {
        fprintf(stderr, "FAIL: write lex fixture\n"); return 1;
    }

    char tflag[1024];
    snprintf(tflag, sizeof(tflag), "-T%s", tmpl);

    char errbuf[4096];
    int rc = 0;

    /* === 1. --target=rust triggers Rust output === */
    {
        unlink("g.rs");
        char *cargv[] = { (char *)lime_bin, tflag, "--target=rust",
                          "g.lime", NULL };
        run_capture(lime_bin, cargv, errbuf, sizeof(errbuf), &rc);
        CHECK(rc == 0 && file_exists("g.rs"),
              "--target=rust produces g.rs (rc=%d)", rc);
        /* Should NOT print a deprecation warning. */
        CHECK(strstr(errbuf, "deprecated") == NULL,
              "--target=rust does not warn about deprecation");
    }

    /* === 2. -trust short form (glued) === */
    {
        unlink("g.rs");
        char *cargv[] = { (char *)lime_bin, tflag, "-trust",
                          "g.lime", NULL };
        run_capture(lime_bin, cargv, errbuf, sizeof(errbuf), &rc);
        CHECK(rc == 0 && file_exists("g.rs"),
              "-trust (glued short) produces g.rs (rc=%d)", rc);
    }

    /* === 3. -t rust short form (separated) === */
    {
        unlink("g.rs");
        char *cargv[] = { (char *)lime_bin, tflag, "-t", "rust",
                          "g.lime", NULL };
        run_capture(lime_bin, cargv, errbuf, sizeof(errbuf), &rc);
        CHECK(rc == 0 && file_exists("g.rs"),
              "-t rust (separated short) produces g.rs (rc=%d)", rc);
    }

    /* === 4. --target=c is the default; no .rs produced === */
    {
        unlink("g.rs");
        char *cargv[] = { (char *)lime_bin, tflag, "--target=c",
                          "g.lime", NULL };
        run_capture(lime_bin, cargv, errbuf, sizeof(errbuf), &rc);
        CHECK(rc == 0 && !file_exists("g.rs"),
              "--target=c does not produce g.rs (rc=%d)", rc);
    }

    /* === 5. --enable=crate (with --target=rust) emits Cargo crate === */
    {
        unlink("g.rs");
        test_compat_rmdir_recursive("g_crate");
        char *cargv[] = { (char *)lime_bin, tflag, "--target=rust",
                          "--enable=crate", "g.lime", NULL };
        run_capture(lime_bin, cargv, errbuf, sizeof(errbuf), &rc);
        /* With --enable=crate, lime moves the .rs into the crate's
        ** src/parser.rs.  No bare g.rs remains in cwd. */
        CHECK(rc == 0 && dir_exists("g_crate")
              && file_exists("g_crate/src/parser.rs"),
              "--target=rust --enable=crate produces g_crate/src/parser.rs "
              "(rc=%d)", rc);
        CHECK(strstr(errbuf, "deprecated") == NULL,
              "--enable=crate does not warn about deprecation");
    }

    /* === 6. --rust-crate (deprecated) still works AND warns === */
    {
        unlink("g.rs");
        test_compat_rmdir_recursive("g_crate");
        char *cargv[] = { (char *)lime_bin, tflag, "--rust-crate",
                          "g.lime", NULL };
        run_capture(lime_bin, cargv, errbuf, sizeof(errbuf), &rc);
        CHECK(rc == 0 && dir_exists("g_crate")
              && file_exists("g_crate/src/parser.rs"),
              "--rust-crate still produces g_crate/src/parser.rs (rc=%d)",
              rc);
        CHECK(strstr(errbuf, "--rust-crate is deprecated") != NULL,
              "--rust-crate prints deprecation warning to stderr");
    }

    /* === 7. --rustcrate (older spelling) still works AND warns === */
    {
        unlink("g.rs");
        test_compat_rmdir_recursive("g_crate");
        char *cargv[] = { (char *)lime_bin, tflag, "--rustcrate",
                          "g.lime", NULL };
        run_capture(lime_bin, cargv, errbuf, sizeof(errbuf), &rc);
        CHECK(rc == 0 && dir_exists("g_crate")
              && file_exists("g_crate/src/parser.rs"),
              "--rustcrate (older spelling) still works (rc=%d)", rc);
        CHECK(strstr(errbuf, "--rustcrate is deprecated") != NULL,
              "--rustcrate prints deprecation warning");
    }

    /* === 8. --rust deprecation alias still emits .rs === */
    {
        unlink("g.rs");
        char *cargv[] = { (char *)lime_bin, tflag, "--rust",
                          "g.lime", NULL };
        run_capture(lime_bin, cargv, errbuf, sizeof(errbuf), &rc);
        CHECK(rc == 0 && file_exists("g.rs"),
              "--rust still produces g.rs (rc=%d)", rc);
        CHECK(strstr(errbuf, "--rust is deprecated") != NULL,
              "--rust prints deprecation warning");
    }

    /* === 9. --enable / --disable interact: later wins === */
    {
        unlink("g.rs");
        test_compat_rmdir_recursive("g_crate");
        char *cargv[] = { (char *)lime_bin, tflag, "--target=rust",
                          "--enable=crate", "--disable=crate",
                          "g.lime", NULL };
        run_capture(lime_bin, cargv, errbuf, sizeof(errbuf), &rc);
        /* With crate disabled, the bare g.rs remains in cwd and no
        ** g_crate/ is created. */
        CHECK(rc == 0 && file_exists("g.rs") && !dir_exists("g_crate"),
              "--enable=crate then --disable=crate: crate disabled (rc=%d)",
              rc);
    }

    /* === 10. --enable=crate without --target=rust warns === */
    {
        char *cargv[] = { (char *)lime_bin, tflag, "--enable=crate",
                          "g.lime", NULL };
        run_capture(lime_bin, cargv, errbuf, sizeof(errbuf), &rc);
        CHECK(rc == 0 &&
              strstr(errbuf,
                     "--enable=crate has no effect without --target=rust")
                  != NULL,
              "--enable=crate --target=c (default) prints rust-only warning");
    }

    /* === 11. --enable=foo (unknown feature) is a hard error === */
    {
        char *cargv[] = { (char *)lime_bin, tflag, "--enable=foo",
                          "g.lime", NULL };
        run_capture(lime_bin, cargv, errbuf, sizeof(errbuf), &rc);
        CHECK(rc != 0 && strstr(errbuf, "unknown feature name") != NULL,
              "--enable=foo (unknown feature) errors with explanation");
    }

    /* === 12. --target=foo (invalid value) is a hard error === */
    {
        char *cargv[] = { (char *)lime_bin, tflag, "--target=foo",
                          "g.lime", NULL };
        run_capture(lime_bin, cargv, errbuf, sizeof(errbuf), &rc);
        CHECK(rc != 0 &&
              strstr(errbuf, "must be 'c' or 'rust'") != NULL,
              "--target=foo (invalid value) errors");
    }

    /* === 13. --rustlex-simd deprecation alias still works === */
    {
        unlink("cov_lex.rs");
        char *cargv[] = { (char *)lime_bin, "-X", "-d.",
                          "--rustlex", "--rustlex-simd",
                          "cov.lex", NULL };
        run_capture(lime_bin, cargv, errbuf, sizeof(errbuf), &rc);
        CHECK(rc == 0 && file_exists("cov_lex.rs"),
              "-X --rustlex --rustlex-simd produces cov_lex.rs (rc=%d)", rc);
        CHECK(strstr(errbuf, "--rustlex-simd is deprecated") != NULL,
              "--rustlex-simd prints deprecation warning");
        CHECK(strstr(errbuf, "default since v0.8.6") != NULL,
              "--rustlex-simd warning notes default-since version");
    }

    /* === 14. --rustlex-memchr deprecation alias still works === */
    {
        unlink("cov_lex.rs");
        char *cargv[] = { (char *)lime_bin, "-X", "-d.",
                          "--rustlex", "--rustlex-memchr",
                          "cov.lex", NULL };
        run_capture(lime_bin, cargv, errbuf, sizeof(errbuf), &rc);
        CHECK(rc == 0 && file_exists("cov_lex.rs"),
              "--rustlex-memchr still works (rc=%d)", rc);
        CHECK(strstr(errbuf, "--rustlex-memchr is deprecated") != NULL,
              "--rustlex-memchr prints deprecation warning");
    }

    /* === 15. --per-token-dfa deprecation alias still works === */
    {
        unlink("cov_lex.rs");
        char *cargv[] = { (char *)lime_bin, "-X", "-d.",
                          "--per-token-dfa",
                          "cov.lex", NULL };
        run_capture(lime_bin, cargv, errbuf, sizeof(errbuf), &rc);
        CHECK(rc == 0,
              "--per-token-dfa still parses (rc=%d)", rc);
        CHECK(strstr(errbuf, "--per-token-dfa is deprecated") != NULL,
              "--per-token-dfa prints deprecation warning");
    }

    /* === 16. New form: -X --target=rust --enable=memchr === */
    {
        unlink("cov_lex.rs");
        char *cargv[] = { (char *)lime_bin, "-X", "-d.",
                          "--target=rust", "--enable=memchr",
                          "cov.lex", NULL };
        run_capture(lime_bin, cargv, errbuf, sizeof(errbuf), &rc);
        CHECK(rc == 0 && file_exists("cov_lex.rs"),
              "-X --target=rust --enable=memchr produces cov_lex.rs (rc=%d)",
              rc);
        CHECK(strstr(errbuf, "deprecated") == NULL,
              "new-scheme invocation has zero deprecation warnings");
    }

    /* === 17. --enable= multiple features comma-separated === */
    {
        unlink("g.rs");
        test_compat_rmdir_recursive("g_crate");
        char *cargv[] = { (char *)lime_bin, tflag, "--target=rust",
                          "--enable=crate,nostd", "g.lime", NULL };
        run_capture(lime_bin, cargv, errbuf, sizeof(errbuf), &rc);
        CHECK(rc == 0 && dir_exists("g_crate")
              && file_exists("g_crate/src/parser.rs"),
              "--enable=crate,nostd accepted (rc=%d)", rc);
    }

    /* === 18. -e short form --enable === */
    {
        unlink("g.rs");
        test_compat_rmdir_recursive("g_crate");
        char *cargv[] = { (char *)lime_bin, tflag, "-trust",
                          "-ecrate", "g.lime", NULL };
        run_capture(lime_bin, cargv, errbuf, sizeof(errbuf), &rc);
        CHECK(rc == 0 && dir_exists("g_crate")
              && file_exists("g_crate/src/parser.rs"),
              "-trust -ecrate (glued shorts) produces crate (rc=%d)", rc);
    }

    /* === 19. C-target build does NOT emit spurious 'safe has no
    ** effect' warning when no --enable=safe was given.  Regression
    ** for lime-v0.10-upgrade-blocker.md (Ra crates/ra-parser): the
    ** rust-only `safe` feature defaults to ON in g_features, and
    ** pre-v0.11 the warning loop fired any time a rust-only feature
    ** was non-zero, regardless of whether the user opted in.  Result:
    ** every C-target build emitted a spurious stderr line that broke
    ** downstream build systems classifying exit-1 outcomes
    ** (resolved-conflicts vs hard-error). === */
    {
        unlink("g.c"); unlink("g.h"); unlink("g.out");
        char *cargv[] = { (char *)lime_bin, tflag, "-q",
                          "g.lime", NULL };
        run_capture(lime_bin, cargv, errbuf, sizeof(errbuf), &rc);
        CHECK(rc == 0 && file_exists("g.c"),
              "C-target build succeeds (rc=%d)", rc);
        CHECK(strstr(errbuf, "--enable=safe has no effect") == NULL,
              "C-target build does NOT emit spurious safe warning");
        CHECK(strstr(errbuf, "has no effect without --target=rust") == NULL,
              "C-target build emits NO rust-only feature warnings");
    }

    /* === 20. EXPLICIT --enable=safe on C-target DOES emit warning ===
    ** This is the intended behaviour: when the user explicitly opts
    ** in to a rust-only feature on a non-rust target, lime tells
    ** them it's a no-op.  Verifies the warning still fires when it
    ** SHOULD. */
    {
        unlink("g.c"); unlink("g.h"); unlink("g.out");
        char *cargv[] = { (char *)lime_bin, tflag, "-q",
                          "--enable=safe", "g.lime", NULL };
        run_capture(lime_bin, cargv, errbuf, sizeof(errbuf), &rc);
        CHECK(rc == 0,
              "C-target with explicit --enable=safe still succeeds (rc=%d)", rc);
        CHECK(strstr(errbuf, "--enable=safe has no effect") != NULL,
              "explicit --enable=safe DOES emit the no-effect warning");
    }

    /* Cleanup. */
    unlink("g.rs");
    test_compat_rmdir_recursive("g_crate");
    unlink("cov_lex.rs");
    unlink("cov_lex.c");
    unlink("cov_lex.h");
    unlink("g.c");
    unlink("g.h");
    unlink("g.out");
    unlink("g.lime");
    unlink("cov.lex");
    test_compat_chdir_temp();
    test_compat_rmdir_recursive(tmpdir);

    printf("\n=== Results: %d/%d passed ===\n", g_pass, g_total);
    return (g_pass == g_total) ? 0 : 1;
}
