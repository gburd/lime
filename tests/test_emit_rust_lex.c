/*
 * tests/test_emit_rust_lex.c -- coverage exercise for the
 * --rustlex emit paths (default scalar, --rustlex-memchr,
 * --rustlex-simd).  Each variant produces a different .rs
 * file; we run lime once per variant and assert the output
 * file exists.
 *
 * The point of this test isn't to validate the runtime correctness
 * of each emit (the bench/rust_compare cargo drivers do that).  It's
 * to make sure the code paths in src/lex/emit_rust_lex.c are exercised
 * by the meson test suite so coverage reflects them.
 *
 * Skipped at runtime when LIME_BIN isn't on $PATH.
 */

#include "test_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *kLexFixture =
    "%name_prefix Cov.\n"
    "rule kw matches /[a-zA-Z_][a-zA-Z0-9_]*/ { /* */ }\n"
    "rule num matches /[0-9]+/ { /* */ }\n"
    "rule str matches /\"([^\"\\\\]|\\\\.)*\"/ { /* */ }\n"
    "rule ws matches /[ \\t\\n\\r]+/ { /* */ }\n"
    "rule lp matches /\\(/ { /* */ }\n"
    "rule rp matches /\\)/ { /* */ }\n";

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int run_one(const char *lime_bin, const char *flag,
                   const char *fixture_path) {
    /* argv slots: lime_bin, -X, -d., --rustlex, [optional flag],
    ** fixture_path, NULL terminator -- up to 7. */
    char *argv[7] = { (char *)lime_bin, "-X", "-d.", "--rustlex" };
    int n = 4;
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

int main(int argc, char **argv) {
    const char *lime_bin = (argc > 1) ? argv[1] : getenv("LIME_BIN");
    if (lime_bin == NULL || lime_bin[0] == 0) {
        fprintf(stderr, "SKIP: no LIME_BIN\n");
        return 77;
    }

    char tmpdir[256];
    if (test_compat_tmpdir("lime_emit_rust_lex", tmpdir, sizeof(tmpdir)) != 0) {
        fprintf(stderr, "FAIL: tmpdir\n");
        return 1;
    }
    if (chdir(tmpdir) != 0) { perror("chdir"); return 1; }

    /* Stage the fixture as cov.lex in cwd. */
    FILE *f = fopen("cov.lex", "wb");
    if (f == NULL) { perror("fopen"); return 1; }
    fputs(kLexFixture, f);
    fclose(f);

    int total = 0, pass = 0;
    struct { const char *name; const char *flag; const char *out; } variants[] = {
        { "default scalar",  NULL,                "cov_lex.rs" },
        { "memchr",          "--rustlex-memchr",  "cov_lex.rs" },
        { "simd",            "--rustlex-simd",    "cov_lex.rs" },
    };
    for (size_t i = 0; i < sizeof(variants)/sizeof(variants[0]); i++) {
        total++;
        unlink(variants[i].out);
        int rc = run_one(lime_bin, variants[i].flag, "cov.lex");
        if (rc == 0 && file_exists(variants[i].out)) {
            printf("  PASS: --rustlex %s emits %s\n",
                   variants[i].name, variants[i].out);
            pass++;
        } else {
            printf("  FAIL: --rustlex %s rc=%d output=%s\n",
                   variants[i].name, rc,
                   file_exists(variants[i].out) ? "present" : "missing");
        }
    }

    test_compat_chdir_temp();
    test_compat_rmdir_recursive(tmpdir);

    printf("\nResults: %d/%d passed\n", pass, total);
    return pass == total ? 0 : 1;
}
