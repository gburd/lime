/*
** tests/test_dialect.c -- v0.4.0 regression test for `%dialect NAME { ... }`.
**
** Runs the lime binary on tests/test_dialect_grammar.lime under
** several -D configurations, slurps the generated .c/.h, and
** asserts the build fingerprint (presence/absence of dialect
** tokens) matches the expected inclusion semantics.  Sub-tests:
**
**   1. No -D flag        -> oracle and mysql tokens absent.
**   2. -Ddialect=oracle  -> oracle tokens present, mysql absent.
**   3. -Ddialect=mysql   -> mysql token present, oracle absent.
**   4. Both              -> all dialect tokens present (shared
**                          namespace, single token table).
**   5. -Ddialect=oracle vs -Ddialect_oracle (legacy form):
**                          generated outputs are byte-identical.
**   6. Idempotence       -> running -Ddialect=oracle twice on the
**                          same input produces byte-identical .c/.h.
**   7. Empty body        -> %dialect never_defined { } is legal
**                          even when its macro is not defined.
**   8. String/char `}` literal in body does not close the block
**                          (covered by the fixture itself; build
**                          succeeding under -Ddialect=oracle is
**                          the assertion).
**   9. Unterminated body -> lime exits non-zero with a diagnostic
**                          mentioning the directive's line.
**  10. Nested %dialect    -> lime exits non-zero with a clear
**                          diagnostic.
**  11. Bad -Ddialect=     -> lime rejects empty name and names
**                          starting with a digit.
**
** This test is grammar-only; it does not compile or drive the
** generated parser.  Its job is to prove the desugarer + CLI
** shorthand produce the right artifacts.  Skips at runtime
** (exit 77) when the lime binary is not findable.
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int failures = 0;

#define FAIL(tag, fmt, ...) do {                                        \
    fprintf(stderr, "FAIL: %s -- " fmt "\n", (tag), ##__VA_ARGS__);     \
    failures++;                                                         \
} while (0)

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    if (n < 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)n + 1);
    if (buf == NULL) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = 0;
    return buf;
}

/* mkdir -p equivalent for test scratch dirs. */
static void ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return;
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", path);
    int rc = system(cmd);
    (void)rc;
}

/* Run lime <flags> -d<outdir> <fixture>; return exit status. */
static int run_lime(const char *lime_bin,
                    const char *limpar,
                    const char *flags,
                    const char *outdir,
                    const char *fixture) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "'%s' %s -T'%s' -d'%s' '%s' >/dev/null 2>&1",
             lime_bin, flags, limpar, outdir, fixture);
    return system(cmd);
}

/* Run lime expecting failure; capture stderr to a buffer for
** diagnostic-string assertions. */
static int run_lime_capture_stderr(const char *lime_bin,
                                   const char *limpar,
                                   const char *flags,
                                   const char *outdir,
                                   const char *fixture,
                                   char *err_out,
                                   size_t err_cap) {
    char cmd[4096];
    char errfile[1024];
    snprintf(errfile, sizeof(errfile), "%s/stderr.txt", outdir);
    snprintf(cmd, sizeof(cmd),
             "'%s' %s -T'%s' -d'%s' '%s' >/dev/null 2>'%s'",
             lime_bin, flags, limpar, outdir, fixture, errfile);
    int rc = system(cmd);
    char *err = slurp(errfile);
    if (err != NULL) {
        snprintf(err_out, err_cap, "%s", err);
        free(err);
    } else {
        err_out[0] = 0;
    }
    return rc;
}

static int contains(const char *hay, const char *needle) {
    return strstr(hay, needle) != NULL;
}

/* Returns 1 if the generated header for `outdir`/test_dialect_grammar.h
** contains the macro `#define <token>`.  Otherwise 0. */
static int has_token(const char *outdir, const char *token) {
    char hpath[1024];
    snprintf(hpath, sizeof(hpath),
             "%s/test_dialect_grammar.h", outdir);
    char *h = slurp(hpath);
    if (h == NULL) return 0;
    char needle[256];
    snprintf(needle, sizeof(needle), "#define %s", token);
    int found = contains(h, needle);
    free(h);
    return found;
}

/* Returns 1 if the .c and .h files in two directories are byte-
** identical for the test fixture.  Embedded #line directives that
** include the output directory name make us write to the SAME
** outdir twice (different runs) when comparing. */
static int files_equal(const char *path_a, const char *path_b) {
    char *a = slurp(path_a);
    char *b = slurp(path_b);
    int ok = 0;
    if (a != NULL && b != NULL) {
        ok = (strcmp(a, b) == 0);
    }
    free(a);
    free(b);
    return ok;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr,
                "usage: %s <lime-binary> <limpar.c> <fixture.lime>\n",
                argv[0]);
        return 2;
    }
    const char *lime_bin = argv[1];
    const char *limpar   = argv[2];
    const char *fixture  = argv[3];

    struct stat st;
    if (stat(lime_bin, &st) != 0) {
        fprintf(stderr, "SKIP: %s not found\n", lime_bin);
        return 77;
    }
    if (stat(fixture, &st) != 0) {
        fprintf(stderr, "SKIP: fixture %s not found\n", fixture);
        return 77;
    }

    /* All scratch dirs live under the test's CWD (meson sets one
    ** per test).  Names mirror the build configuration. */
    const char *d_none      = "out_none";
    const char *d_oracle    = "out_oracle";
    const char *d_mysql     = "out_mysql";
    const char *d_both      = "out_both";
    const char *d_legacy    = "out_legacy";
    const char *d_idem_a    = "out_idem_a";
    const char *d_idem_b    = "out_idem_b";
    const char *d_err       = "out_err";

    ensure_dir(d_none);
    ensure_dir(d_oracle);
    ensure_dir(d_mysql);
    ensure_dir(d_both);
    ensure_dir(d_legacy);
    ensure_dir(d_idem_a);
    ensure_dir(d_idem_b);
    ensure_dir(d_err);

    /* ---- 1. No -D ---- */
    if (run_lime(lime_bin, limpar, "", d_none, fixture) != 0) {
        FAIL("none", "lime exited non-zero with no -D");
    } else {
        if (has_token(d_none, "ROWNUM")) {
            FAIL("none", "ROWNUM should be absent without -Ddialect=oracle");
        }
        if (has_token(d_none, "BACKTICK_IDENT")) {
            FAIL("none", "BACKTICK_IDENT should be absent without -Ddialect=mysql");
        }
        if (!has_token(d_none, "NUMBER") || !has_token(d_none, "PLUS")) {
            FAIL("none", "always-active tokens missing (NUMBER/PLUS)");
        }
    }

    /* ---- 2. -Ddialect=oracle ---- */
    if (run_lime(lime_bin, limpar, "-Ddialect=oracle", d_oracle, fixture) != 0) {
        FAIL("oracle", "lime exited non-zero with -Ddialect=oracle");
    } else {
        if (!has_token(d_oracle, "ROWNUM")) {
            FAIL("oracle", "ROWNUM should be present with -Ddialect=oracle");
        }
        if (!has_token(d_oracle, "CONNECT_BY")) {
            FAIL("oracle", "CONNECT_BY should be present with -Ddialect=oracle");
        }
        if (!has_token(d_oracle, "QUOTED_BRACE")) {
            FAIL("oracle", "QUOTED_BRACE should be present "
                           "(brace-in-string scan)");
        }
        if (has_token(d_oracle, "BACKTICK_IDENT")) {
            FAIL("oracle", "BACKTICK_IDENT must be absent without -Ddialect=mysql");
        }
    }

    /* ---- 3. -Ddialect=mysql ---- */
    if (run_lime(lime_bin, limpar, "-Ddialect=mysql", d_mysql, fixture) != 0) {
        FAIL("mysql", "lime exited non-zero with -Ddialect=mysql");
    } else {
        if (!has_token(d_mysql, "BACKTICK_IDENT")) {
            FAIL("mysql", "BACKTICK_IDENT should be present");
        }
        if (has_token(d_mysql, "ROWNUM")) {
            FAIL("mysql", "ROWNUM must be absent without -Ddialect=oracle");
        }
    }

    /* ---- 4. Both -Ddialect=oracle -Ddialect=mysql ---- */
    if (run_lime(lime_bin, limpar,
                 "-Ddialect=oracle -Ddialect=mysql",
                 d_both, fixture) != 0) {
        FAIL("both", "lime exited non-zero with two dialects");
    } else {
        if (!has_token(d_both, "ROWNUM")) {
            FAIL("both", "ROWNUM missing in combined build");
        }
        if (!has_token(d_both, "BACKTICK_IDENT")) {
            FAIL("both", "BACKTICK_IDENT missing in combined build");
        }
        if (!has_token(d_both, "CONNECT_BY")) {
            FAIL("both", "CONNECT_BY missing in combined build");
        }
    }

    /* ---- 5. Shorthand -Ddialect=oracle vs legacy -Ddialect_oracle.
    ** Run twice into the SAME output dir so embedded #line strings
    ** referencing the dir name don't artificially differ. ---- */
    {
        const char *shared = "out_shared";
        ensure_dir(shared);
        if (run_lime(lime_bin, limpar,
                     "-Ddialect=oracle", shared, fixture) != 0) {
            FAIL("shorthand_eq_legacy",
                 "first run (shorthand) exited non-zero");
        } else {
            char src_c[1024], src_h[1024];
            snprintf(src_c, sizeof(src_c),
                     "%s/test_dialect_grammar.c", shared);
            snprintf(src_h, sizeof(src_h),
                     "%s/test_dialect_grammar.h", shared);
            char snap_c[1024], snap_h[1024];
            snprintf(snap_c, sizeof(snap_c), "%s/snapshot.c", d_legacy);
            snprintf(snap_h, sizeof(snap_h), "%s/snapshot.h", d_legacy);
            char cp_cmd[4096];
            snprintf(cp_cmd, sizeof(cp_cmd),
                     "cp '%s' '%s' && cp '%s' '%s'",
                     src_c, snap_c, src_h, snap_h);
            if (system(cp_cmd) != 0) {
                FAIL("shorthand_eq_legacy", "snapshot copy failed");
            } else if (run_lime(lime_bin, limpar,
                                "-Ddialect_oracle",
                                shared, fixture) != 0) {
                FAIL("shorthand_eq_legacy",
                     "second run (legacy) exited non-zero");
            } else {
                if (!files_equal(snap_c, src_c)) {
                    FAIL("shorthand_eq_legacy",
                         ".c output differs between -Ddialect=oracle "
                         "and -Ddialect_oracle");
                }
                if (!files_equal(snap_h, src_h)) {
                    FAIL("shorthand_eq_legacy",
                         ".h output differs between -Ddialect=oracle "
                         "and -Ddialect_oracle");
                }
            }
        }
    }

    /* ---- 6. Idempotence: same -D twice -> identical output. ---- */
    if (run_lime(lime_bin, limpar, "-Ddialect=oracle", d_idem_a, fixture) != 0
     || run_lime(lime_bin, limpar, "-Ddialect=oracle", d_idem_a, fixture) != 0) {
        FAIL("idempotence", "lime failed on repeated invocation");
    } else {
        char snap_c[1024], snap_h[1024];
        snprintf(snap_c, sizeof(snap_c),
                 "%s/test_dialect_grammar.c", d_idem_a);
        snprintf(snap_h, sizeof(snap_h),
                 "%s/test_dialect_grammar.h", d_idem_a);
        char copy_c[1024], copy_h[1024];
        snprintf(copy_c, sizeof(copy_c), "%s/copy.c", d_idem_b);
        snprintf(copy_h, sizeof(copy_h), "%s/copy.h", d_idem_b);
        char cp_cmd[4096];
        snprintf(cp_cmd, sizeof(cp_cmd),
                 "cp '%s' '%s' && cp '%s' '%s'",
                 snap_c, copy_c, snap_h, copy_h);
        if (system(cp_cmd) != 0) {
            FAIL("idempotence", "snapshot copy failed");
        } else {
            run_lime(lime_bin, limpar, "-Ddialect=oracle",
                     d_idem_a, fixture);
            if (!files_equal(snap_c, copy_c)) {
                FAIL("idempotence",
                     ".c output not byte-equal across runs");
            }
            if (!files_equal(snap_h, copy_h)) {
                FAIL("idempotence",
                     ".h output not byte-equal across runs");
            }
        }
    }

    /* ---- 7. Empty body -- the fixture's `%dialect never_defined { }`
    ** must not break the no-D build (already exercised by sub-test 1).
    ** Also build with -Ddialect=never_defined to verify the empty
    ** body produces a parser identical (modulo nothing) to no-D
    ** in token coverage. ---- */
    {
        const char *d_empty = "out_empty";
        ensure_dir(d_empty);
        if (run_lime(lime_bin, limpar, "-Ddialect=never_defined",
                     d_empty, fixture) != 0) {
            FAIL("empty_body",
                 "-Ddialect=never_defined (empty body) exited non-zero");
        } else if (has_token(d_empty, "ROWNUM")
                || has_token(d_empty, "BACKTICK_IDENT")) {
            FAIL("empty_body",
                 "empty-body dialect must not bring in other dialects' tokens");
        }
    }

    /* ---- 8. Brace-in-string scan: covered by sub-test 2 above
    ** (QUOTED_BRACE rule's action contains a `}` inside both a
    ** string literal and a char literal; if the desugarer's brace
    ** scan didn't track string state it would close the dialect
    ** block prematurely and the build would error). ---- */

    /* ---- 9. Unterminated %dialect ---- */
    {
        FILE *f = fopen("unterminated.lime", "wb");
        if (f == NULL) {
            FAIL("unterminated", "could not write fixture");
        } else {
            fputs("%name X\n%token A.\n%dialect foo {\n%token B.\n",
                  f);
            fclose(f);
            char err[2048];
            int rc = run_lime_capture_stderr(
                lime_bin, limpar, "-Ddialect=foo",
                d_err, "unterminated.lime", err, sizeof(err));
            if (rc == 0) {
                FAIL("unterminated",
                     "expected lime to fail; exit was zero");
            } else if (!contains(err, "unterminated")) {
                FAIL("unterminated",
                     "diagnostic missing 'unterminated': %s", err);
            } else if (!contains(err, "foo")) {
                FAIL("unterminated",
                     "diagnostic missing dialect name 'foo': %s", err);
            }
        }
    }

    /* ---- 10. Nested %dialect ---- */
    {
        FILE *f = fopen("nested.lime", "wb");
        if (f == NULL) {
            FAIL("nested", "could not write fixture");
        } else {
            fputs("%name X\n%token A.\n"
                  "%dialect foo {\n"
                  "%dialect bar {\n"
                  "}\n"
                  "}\n", f);
            fclose(f);
            char err[2048];
            int rc = run_lime_capture_stderr(
                lime_bin, limpar, "-Ddialect=foo -Ddialect=bar",
                d_err, "nested.lime", err, sizeof(err));
            if (rc == 0) {
                FAIL("nested",
                     "expected lime to fail; exit was zero");
            } else if (!contains(err, "nested")) {
                FAIL("nested",
                     "diagnostic missing 'nested': %s", err);
            }
        }
    }

    /* ---- 11. Bad -Ddialect= argument ---- */
    {
        char err[2048];
        int rc = run_lime_capture_stderr(
            lime_bin, limpar, "-Ddialect=",
            d_err, fixture, err, sizeof(err));
        if (rc == 0) {
            FAIL("empty_dialect_arg",
                 "expected lime to reject -Ddialect=");
        } else if (!contains(err, "requires a name")) {
            FAIL("empty_dialect_arg",
                 "diagnostic missing 'requires a name': %s", err);
        }

        rc = run_lime_capture_stderr(
            lime_bin, limpar, "-Ddialect=9bad",
            d_err, fixture, err, sizeof(err));
        if (rc == 0) {
            FAIL("bad_dialect_name",
                 "expected lime to reject -Ddialect=9bad");
        } else if (!contains(err, "must start with")) {
            FAIL("bad_dialect_name",
                 "diagnostic missing 'must start with': %s", err);
        }
    }

    if (failures > 0) {
        fprintf(stderr, "test_dialect: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_dialect: 11 sub-tests passed\n");
    return 0;
}
