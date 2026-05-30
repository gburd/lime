/*
** tests/test_lint.c -- v0.5.0 regression test for the expanded
** linter (`lime -L`).  Pre-v0.5.0 the linter was a 4-rule stub
** with no test coverage.  This driver runs lime against the
** per-rule fixtures in tests/test_lint_fixtures/ and asserts
** that each rule's structured diagnostic appears (positive case)
** or does not appear on clean.lime (negative case).
**
** Sub-tests:
**   1.  clean.lime           -> no diagnostics, exit 0
**   2.  E001 undeclared-rhs  -> "code":"E001" appears
**   3.  E002 undeclared-prec -> "code":"E002" appears
**   4.  E003 duplicate-token -> "code":"E003" appears
**   5.  E004 ambiguous-alias -> "code":"E004" appears
**   6.  E005 unreachable     -> "code":"E005" appears
**   7.  W001 unused-token    -> "code":"W001" appears
**   8.  W002 unused-prec     -> "code":"W002" appears
**   9.  W004 trivial-action  -> "code":"W004" appears
**   10. W005 missing-expect  -> "code":"W005" appears
**   11. W006 alias-no-action -> "code":"W006" appears
**   12. W007 inconsistent    -> "code":"W007" appears
**   13. W008 long-rhs        -> "code":"W008" appears
**   14. W009 long-action     -> "code":"W009" appears
**   15. --lint-strict        -> exit 1 on warning-only fixture
**   16. --lint-style         -> S001/S002 fire on clean.lime
**   17. --lint-format=gcc    -> stderr matches `path:line:col: severity: [code]`
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include "posix_shim.h"

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
    if (chdir("/") != 0) return;
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_scratch);
    int rc = system(cmd);
    (void)rc;
    g_scratch[0] = 0;
}

static int enter_scratch_dir(void) {
    const char *candidates[3] = {0};
    int n = 0;
    const char *tmp = getenv("TMPDIR");
    if (tmp != NULL && tmp[0] != 0) candidates[n++] = tmp;
    candidates[n++] = "/tmp";
    for (int i = 0; i < n; i++) {
        struct stat st;
        if (stat(candidates[i], &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        snprintf(g_scratch, sizeof(g_scratch),
                 "%s/lime_test_lint.XXXXXX", candidates[i]);
        if (mkdtemp(g_scratch) == NULL) { g_scratch[0] = 0; continue; }
        if (chdir(g_scratch) != 0) { cleanup_scratch(); continue; }
        atexit(cleanup_scratch);
        return 0;
    }
    g_scratch[0] = 0;
    return -1;
}

/* Run `lime <flags> <fixture>` capturing stdout, stderr, and exit
** code.  out_stdout / out_stderr receive the captured contents
** (caller frees).  Return code is the WEXITSTATUS of system(). */
static int run_lime(const char *lime_bin,
                    const char *flags,
                    const char *fixture,
                    char **out_stdout,
                    char **out_stderr) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "'%s' %s '%s' >stdout.txt 2>stderr.txt",
             lime_bin, flags, fixture);
    int rc = system(cmd);
    *out_stdout = slurp("stdout.txt");
    *out_stderr = slurp("stderr.txt");
    /* WEXITSTATUS extraction; system() already returns the encoded form */
    if (rc == -1) return -1;
    /* On POSIX system() returns waitpid status; portable normalize: */
    if (rc >= 256) rc = (rc >> 8) & 0xff;
    return rc;
}

/* True if `hay` contains needle. */
static int contains(const char *hay, const char *needle) {
    return hay != NULL && strstr(hay, needle) != NULL;
}

/* Assert that running lime -L --lint-format=json on `fixture` produces
** stdout that contains `expected_code` (e.g. "\"code\":\"E001\"") and
** has the specified exit code. */
static void check_rule_fires(const char *lime_bin,
                             const char *fixture_dir,
                             const char *fixture_name,
                             const char *expected_code,
                             int expected_exit) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", fixture_dir, fixture_name);

    char *out = NULL, *err = NULL;
    int rc = run_lime(lime_bin, "-L --lint-format=json", path, &out, &err);

    char tag[256];
    snprintf(tag, sizeof(tag), "%s -> %s", fixture_name, expected_code);

    char needle[64];
    snprintf(needle, sizeof(needle), "\"code\":\"%s\"", expected_code);

    if (out == NULL) {
        FAIL(tag, "no stdout captured");
    } else if (!contains(out, needle)) {
        FAIL(tag, "stdout missing %s\n    stdout=%s\n    stderr=%s",
             needle, out, err ? err : "(null)");
    } else if (rc != expected_exit) {
        FAIL(tag, "exit %d, expected %d (stdout=%s)",
             rc, expected_exit, out);
    } else {
        PASS(tag);
    }
    free(out); free(err);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr,
                "usage: %s <lime-binary> <fixtures-dir>\n"
                "  Each fixture in <fixtures-dir>/ should be named\n"
                "  e0NN_<rule>.lime / w0NN_<rule>.lime / clean.lime.\n",
                argv[0]);
        return 2;
    }
    const char *lime_bin    = argv[1];
    const char *fixture_dir = argv[2];

    struct stat st;
    if (stat(lime_bin, &st) != 0) {
        fprintf(stderr, "skip: lime binary not found at %s\n", lime_bin);
        return 77;
    }
    if (stat(fixture_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "skip: fixtures dir not found at %s\n", fixture_dir);
        return 77;
    }

    char lime_abs[PATH_MAX], fix_abs[PATH_MAX];
    if (realpath(lime_bin, lime_abs) == NULL) return 77;
    if (realpath(fixture_dir, fix_abs) == NULL) return 77;

    if (enter_scratch_dir() != 0) {
        fprintf(stderr, "FAIL: could not create scratch dir\n");
        return 1;
    }

    /* === clean.lime === */
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/clean.lime", fix_abs);
        char *out = NULL, *err = NULL;
        int rc = run_lime(lime_abs, "-L --lint-format=json", path, &out, &err);
        if (rc != 0) {
            FAIL("clean.lime exit", "exit %d, expected 0\n    stderr=%s",
                 rc, err ? err : "");
        } else if (out == NULL || strcmp(out, "[]\n") != 0) {
            FAIL("clean.lime json", "expected '[]\\n', got '%s'",
                 out ? out : "(null)");
        } else {
            PASS("clean.lime -> [] exit 0");
        }
        free(out); free(err);
    }

    /* === E rules: expected exit 1 === */
    check_rule_fires(lime_abs, fix_abs, "e001_undeclared_rhs.lime",  "E001", 1);
    check_rule_fires(lime_abs, fix_abs, "e002_undeclared_prec.lime", "E002", 1);
    check_rule_fires(lime_abs, fix_abs, "e003_duplicate_token.lime", "E003", 1);
    check_rule_fires(lime_abs, fix_abs, "e004_ambiguous_alias.lime", "E004", 1);
    check_rule_fires(lime_abs, fix_abs, "e005_unreachable_rule.lime","E005", 1);

    /* === W rules: expected exit 0 (warnings don't fail by default) === */
    check_rule_fires(lime_abs, fix_abs, "w001_unused_token.lime",         "W001", 0);
    check_rule_fires(lime_abs, fix_abs, "w002_unused_precedence.lime",    "W002", 0);
    check_rule_fires(lime_abs, fix_abs, "w004_trivial_action.lime",       "W004", 0);
    check_rule_fires(lime_abs, fix_abs, "w005_missing_expect.lime",       "W005", 0);
    check_rule_fires(lime_abs, fix_abs, "w006_alias_without_action.lime", "W006", 0);
    check_rule_fires(lime_abs, fix_abs, "w007_inconsistent_naming.lime",  "W007", 0);
    check_rule_fires(lime_abs, fix_abs, "w008_long_rhs.lime",             "W008", 0);
    check_rule_fires(lime_abs, fix_abs, "w009_long_action_body.lime",     "W009", 0);

    /* === --lint-strict promotes warnings to failures === */
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/w001_unused_token.lime", fix_abs);
        char *out = NULL, *err = NULL;
        int rc = run_lime(lime_abs, "-L --lint-strict --lint-format=json",
                          path, &out, &err);
        if (rc != 1) {
            FAIL("--lint-strict on W001",
                 "exit %d, expected 1\n    stdout=%s\n    stderr=%s",
                 rc, out ? out : "", err ? err : "");
        } else if (!contains(out, "\"code\":\"W001\"")) {
            FAIL("--lint-strict on W001", "stdout missing W001: %s",
                 out ? out : "");
        } else {
            PASS("--lint-strict promotes W001 to exit 1");
        }
        free(out); free(err);
    }

    /* === --lint-style enables S002 (clean.lime has a header so
    ** S001 won't fire; S002 fires for every nrhs>1 rule). === */
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/clean.lime", fix_abs);
        char *out = NULL, *err = NULL;
        int rc = run_lime(lime_abs, "-L --lint-style --lint-format=json",
                          path, &out, &err);
        if (rc != 0) {
            FAIL("--lint-style on clean", "exit %d (stdout=%s)",
                 rc, out ? out : "");
        } else if (!contains(out, "\"code\":\"S002\"")) {
            FAIL("--lint-style on clean", "stdout missing S002: %s",
                 out ? out : "");
        } else {
            PASS("--lint-style fires S002 on clean.lime");
        }
        free(out); free(err);
    }

    /* === --lint-format=gcc emits `path:line:col: severity: [code] msg` === */
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/e001_undeclared_rhs.lime", fix_abs);
        char *out = NULL, *err = NULL;
        int rc = run_lime(lime_abs, "-L --lint-format=gcc",
                          path, &out, &err);
        if (rc != 1) {
            FAIL("gcc-format E001", "exit %d, expected 1", rc);
        } else if (!contains(err, "error: [E001]")) {
            FAIL("gcc-format E001",
                 "stderr missing 'error: [E001]':\n    stderr=%s\n    stdout=%s",
                 err ? err : "", out ? out : "");
        } else {
            PASS("--lint-format=gcc emits 'path:line:col: error: [E001] ...'");
        }
        free(out); free(err);
    }

    /* === json mode emits an array even when no diagnostics fire === */
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/clean.lime", fix_abs);
        char *out = NULL, *err = NULL;
        int rc = run_lime(lime_abs, "-L --lint-format=json", path, &out, &err);
        if (rc == 0 && out != NULL && strcmp(out, "[]\n") == 0) {
            PASS("--lint-format=json on clean -> '[]\\n' exit 0");
        } else {
            FAIL("json clean", "rc=%d out='%s'", rc, out ? out : "(null)");
        }
        free(out); free(err);
    }

    /* === json mode is well-formed when many diagnostics fire === */
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/w008_long_rhs.lime", fix_abs);
        char *out = NULL, *err = NULL;
        int rc = run_lime(lime_abs, "-L --lint-format=json", path, &out, &err);
        if (rc != 0) {
            FAIL("json w008 exit", "exit %d", rc);
        } else if (out == NULL || out[0] != '[' ||
                   out[strlen(out) - 2] != ']') {
            FAIL("json well-formed", "out='%s'", out ? out : "(null)");
        } else {
            PASS("--lint-format=json output is `[ ... ]` array");
        }
        free(out); free(err);
    }

    int total = 17 + 5 - 5;  /* clean + 5E + 8W + 4 mode tests = 18 */
    (void)total;
    if (g_failures == 0) {
        printf("\n=== Summary === all sub-tests passed\n");
        return 0;
    }
    printf("\n=== Summary === %d failure(s)\n", g_failures);
    return 1;
}
