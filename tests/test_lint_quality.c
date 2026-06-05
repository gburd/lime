/*
** tests/test_lint_quality.c -- regression tests for the lint output
** quality improvements landed in audit/linter-quality:
**
**   1. E001 emits "; did you mean 'NAME'?" on near-typos of declared
**      symbols (case-insensitive Levenshtein, kind-aware preference).
**   2. E002 emits the same suggestion when a [PRECSYM] override
**      misspells a declared %left/%right/%nonassoc symbol.
**   3. M003 emits the same suggestion when an %export name misspells
**      a defined non-terminal.
**   4. The suggestion does NOT fire when the name is far from any
**      declared symbol -- avoid the "did you mean X?" noise from
**      large grammars where every undeclared identifier accidentally
**      lands within distance 4 of something.
**   5. --lint-explain CODE prints rule documentation and exits 0.
**   6. --lint-explain accepts lowercase codes ("e001" == "E001").
**   7. --lint-explain on an unknown / empty code exits 1.
**
** Builds and runs alongside tests/test_lint.c; the existing suite
** there covers per-rule firing, this one covers UX polish.
*/

#include <assert.h>
#include "test_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <limits.h>

static int g_failures = 0;

#define FAIL(tag, fmt, ...) do {                                          \
    fprintf(stderr, "  [FAIL] %s -- " fmt "\n", (tag), ##__VA_ARGS__);    \
    g_failures++;                                                         \
} while (0)

#define PASS(tag) printf("  [PASS] %s\n", (tag))

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

static char g_scratch[256] = {0};

static void cleanup_scratch(void) {
    if (g_scratch[0] == 0) return;
    test_compat_chdir_temp();
    test_compat_rmdir_recursive(g_scratch);
    g_scratch[0] = 0;
}

static int enter_scratch_dir(void) {
    if (test_compat_tmpdir("lime_test_lint_q", g_scratch,
                           sizeof(g_scratch)) != 0) {
        g_scratch[0] = 0;
        return -1;
    }
    if (chdir(g_scratch) != 0) {
        cleanup_scratch();
        return -1;
    }
    atexit(cleanup_scratch);
    return 0;
}

/* Run `lime <flags> [<fixture>]` capturing stdout, stderr, and exit
** code.  When fixture is NULL, only flags are passed -- used for
** --lint-explain which doesn't take a grammar argument. */
static int run_lime(const char *lime_bin,
                    const char *flags,
                    const char *fixture,
                    char **out_stdout,
                    char **out_stderr) {
    char flagbuf[256];
    snprintf(flagbuf, sizeof(flagbuf), "%s", flags);
    char *argv[20] = { (char *)lime_bin };
    int argc = 1;
    char *tok = strtok(flagbuf, " ");
    while (tok && argc < 18) { argv[argc++] = tok; tok = strtok(NULL, " "); }
    if (fixture) argv[argc++] = (char *)fixture;
    argv[argc] = NULL;
    int rc = 0;
    if (test_compat_run_to_files(argv, "stdout.txt", "stderr.txt", &rc) != 0) {
        return -1;
    }
    *out_stdout = slurp("stdout.txt");
    *out_stderr = slurp("stderr.txt");
    return rc;
}

static int contains(const char *hay, const char *needle) {
    return hay != NULL && strstr(hay, needle) != NULL;
}

/* Write a fixture file in the scratch dir and return its path
** (caller-owned static buffer; do not concurrently call). */
static const char *write_fixture(const char *name, const char *body) {
    static char path[512];
    snprintf(path, sizeof(path), "%s/%s", g_scratch, name);
    FILE *f = fopen(path, "wb");
    if (!f) return NULL;
    fputs(body, f);
    fclose(f);
    return path;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <lime-binary>\n", argv[0]);
        return 2;
    }
    const char *lime_bin = argv[1];

    struct stat st;
    if (stat(lime_bin, &st) != 0) {
        fprintf(stderr, "skip: lime binary not found at %s\n", lime_bin);
        return 77;
    }

    char lime_abs[PATH_MAX];
    if (test_compat_realpath(lime_bin, lime_abs, sizeof(lime_abs)) != 0)
        return 77;

    if (enter_scratch_dir() != 0) {
        fprintf(stderr, "FAIL: could not create scratch dir\n");
        return 1;
    }

    /* === E001 with did-you-mean: case-insensitive match across kind === */
    {
        const char *fix = write_fixture("e001_dym.lime",
            "%token PLUS NUM IDENTIFIER.\n"
            "start ::= expr.\n"
            "expr ::= NUM PLUS identifyer.\n");
        char *out = NULL, *err = NULL;
        int rc = run_lime(lime_abs, "-L --lint-format=json", fix, &out, &err);
        if (rc != 1) {
            FAIL("E001 dym exit", "rc=%d expected 1\n    out=%s\n    err=%s",
                 rc, out ? out : "", err ? err : "");
        } else if (!contains(out, "\"code\":\"E001\"")) {
            FAIL("E001 dym present", "missing E001 in %s",
                 out ? out : "(null)");
        } else if (!contains(out, "did you mean 'IDENTIFIER'")) {
            FAIL("E001 dym suggestion",
                 "stdout missing 'did you mean IDENTIFIER':\n    out=%s",
                 out ? out : "(null)");
        } else {
            PASS("E001 suggests 'IDENTIFIER' for typo 'identifyer'");
        }
        free(out); free(err);
    }

    /* === E001 distance budget: rejects unrelated names === */
    {
        const char *fix = write_fixture("e001_no_dym.lime",
            "%token A_VERY_LONG_NAME B_VERY_LONG_NAME.\n"
            "start ::= expr.\n"
            "expr ::= ZQ.\n");
        char *out = NULL, *err = NULL;
        int rc = run_lime(lime_abs, "-L --lint-format=json", fix, &out, &err);
        if (rc != 1) {
            FAIL("E001 no-dym exit", "rc=%d expected 1", rc);
        } else if (!contains(out, "\"code\":\"E001\"")) {
            FAIL("E001 no-dym present", "missing E001 in %s",
                 out ? out : "(null)");
        } else if (contains(out, "did you mean")) {
            FAIL("E001 no-dym false-positive",
                 "unexpected suggestion:\n    out=%s", out);
        } else {
            PASS("E001 omits suggestion when nothing is close");
        }
        free(out); free(err);
    }

    /* === E002 with did-you-mean on prec symbol === */
    {
        const char *fix = write_fixture("e002_dym.lime",
            "%token PLUS NUM MINUS.\n"
            "%left PLUS MINUS.\n"
            "start ::= expr.\n"
            "expr ::= NUM.\n"
            "expr ::= NUM PLUS NUM. [UMINOS]\n");
        char *out = NULL, *err = NULL;
        int rc = run_lime(lime_abs, "-L --lint-format=json", fix, &out, &err);
        if (rc != 1) {
            FAIL("E002 dym exit", "rc=%d expected 1\n    out=%s",
                 rc, out ? out : "");
        } else if (!contains(out, "\"code\":\"E002\"")) {
            FAIL("E002 dym present", "missing E002 in %s",
                 out ? out : "(null)");
        } else if (!contains(out, "did you mean 'MINUS'")) {
            FAIL("E002 dym suggestion",
                 "stdout missing 'did you mean MINUS':\n    out=%s", out);
        } else {
            PASS("E002 suggests 'MINUS' for typo 'UMINOS'");
        }
        free(out); free(err);
    }

    /* === M003 with did-you-mean on export === */
    {
        const char *fix = write_fixture("m003_dym.lime",
            "%module_name foo\n"
            "%module_version \"1.0.0\"\n"
            "\n"
            "%token PLUS NUM.\n"
            "%export progam.\n"
            "\n"
            "program ::= expr.\n"
            "expr ::= NUM PLUS NUM.\n");
        char *out = NULL, *err = NULL;
        int rc = run_lime(lime_abs, "-L --lint-format=json", fix, &out, &err);
        if (rc != 1) {
            FAIL("M003 dym exit", "rc=%d expected 1\n    out=%s",
                 rc, out ? out : "");
        } else if (!contains(out, "\"code\":\"M003\"")) {
            FAIL("M003 dym present", "missing M003 in %s",
                 out ? out : "(null)");
        } else if (!contains(out, "did you mean 'program'")) {
            FAIL("M003 dym suggestion",
                 "stdout missing 'did you mean program':\n    out=%s", out);
        } else {
            PASS("M003 suggests 'program' for export typo 'progam'");
        }
        free(out); free(err);
    }

    /* === --lint-explain=E001 prints docs to stdout, exits 0 === */
    {
        char *out = NULL, *err = NULL;
        int rc = run_lime(lime_abs, "--lint-explain=E001", NULL, &out, &err);
        if (rc != 0) {
            FAIL("lint-explain E001 exit", "rc=%d expected 0\n    err=%s",
                 rc, err ? err : "");
        } else if (!contains(out, "E001 -- undeclared-rhs-symbol")) {
            FAIL("lint-explain E001 body",
                 "stdout missing rule banner:\n    out=%s",
                 out ? out : "(null)");
        } else if (!contains(out, "Fix:")) {
            FAIL("lint-explain E001 fix",
                 "stdout missing 'Fix:' section:\n    out=%s", out);
        } else {
            PASS("--lint-explain=E001 prints docs to stdout, exits 0");
        }
        free(out); free(err);
    }

    /* === --lint-explain accepts lowercase === */
    {
        char *out = NULL, *err = NULL;
        int rc = run_lime(lime_abs, "--lint-explain=w005", NULL, &out, &err);
        if (rc != 0) {
            FAIL("lint-explain lowercase exit", "rc=%d expected 0", rc);
        } else if (!contains(out, "W005 -- missing-expect")) {
            FAIL("lint-explain lowercase body",
                 "stdout missing W005 banner:\n    out=%s",
                 out ? out : "(null)");
        } else {
            PASS("--lint-explain=w005 normalises to W005 (case-insensitive)");
        }
        free(out); free(err);
    }

    /* === --lint-explain unknown code: exit 1, stderr lists known codes === */
    {
        char *out = NULL, *err = NULL;
        int rc = run_lime(lime_abs, "--lint-explain=Z999", NULL, &out, &err);
        if (rc != 1) {
            FAIL("lint-explain unknown exit",
                 "rc=%d expected 1\n    out=%s\n    err=%s",
                 rc, out ? out : "", err ? err : "");
        } else if (!contains(err, "unrecognised lint code 'Z999'")) {
            FAIL("lint-explain unknown msg",
                 "stderr missing unrecognised-code message:\n    err=%s",
                 err ? err : "(null)");
        } else if (!contains(err, "E001-E005")) {
            FAIL("lint-explain unknown known-list",
                 "stderr missing known-codes hint:\n    err=%s", err);
        } else {
            PASS("--lint-explain=Z999 exits 1 with known-codes hint");
        }
        free(out); free(err);
    }

    /* === E001 dym is also visible in human format (the field is part
    ** of the message string, not a separate JSON field, so it shows up
    ** in every output mode for free) === */
    {
        const char *fix = write_fixture("e001_human.lime",
            "%token NUMBER PLUS.\n"
            "start ::= expr.\n"
            "expr ::= NUMBER PLUS numbar.\n");
        char *out = NULL, *err = NULL;
        int rc = run_lime(lime_abs, "-L --lint-format=gcc", fix, &out, &err);
        if (rc != 1) {
            FAIL("E001 gcc dym exit", "rc=%d expected 1", rc);
        } else if (!contains(err, "[E001]")) {
            FAIL("E001 gcc dym present", "stderr missing E001:\n    err=%s",
                 err ? err : "(null)");
        } else if (!contains(err, "did you mean")) {
            FAIL("E001 gcc dym suggestion",
                 "stderr missing 'did you mean':\n    err=%s", err);
        } else {
            PASS("E001 suggestion appears in --lint-format=gcc output");
        }
        free(out); free(err);
    }

    /* === W007 anchors to first-use line, not always 1:1 === */
    {
        const char *fix = write_fixture("w007_line.lime",
            "%token PLUS NUM.\n"
            "\n"
            "start ::= myExpr.\n"
            "myExpr ::= NUM PLUS NUM.\n");
        char *out = NULL, *err = NULL;
        int rc = run_lime(lime_abs, "-L --lint-format=json", fix, &out, &err);
        if (rc != 0) {
            FAIL("W007 line exit", "rc=%d expected 0 (W007 is warn)\n    out=%s",
                 rc, out ? out : "");
        } else if (!contains(out, "\"code\":\"W007\"")) {
            FAIL("W007 line present", "missing W007:\n    out=%s",
                 out ? out : "(null)");
        } else if (!contains(out, "\"line\":3,")) {
            /* myExpr first appears on line 3 (`start ::= myExpr.`).
            ** Pre-refactor, every W007 said "line":1 unconditionally. */
            FAIL("W007 line=3",
                 "expected line:3 (first-use of myExpr), got:\n    out=%s", out);
        } else {
            PASS("W007 anchors to first-use line (3) instead of 1");
        }
        free(out); free(err);
    }

    int total = 9;
    (void)total;
    if (g_failures == 0) {
        printf("\n=== Summary === all sub-tests passed\n");
        return 0;
    }
    printf("\n=== Summary === %d failure(s)\n", g_failures);
    return 1;
}
