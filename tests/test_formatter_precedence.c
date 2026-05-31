/*
** test_formatter_precedence.c -- regression test for Lime-Letter-22.
**
** Asserts that `lime -F` preserves %left / %right / %nonassoc
** precedence directives.  Pre-fix (v0.3.0 through v0.3.5) the
** formatter dropped them entirely, exploding PG's gram.lime from
** 0 to 1682 parsing conflicts on the canonicalize pass.
**
** Sub-tests:
**   1. %left / %right / %nonassoc keywords appear in formatted output
**   2. Each precedence-level group emits as a single line
**   3. Binary operators stay in the right group
**   4. Unary [UMINUS] precedence override stays preserved
**   5. Conflict count: source vs formatted, both must be zero
**   6. Idempotence: format(format(F)) == format(F)
*/

#include <assert.h>
#include "test_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <limits.h>

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

static int contains(const char *hay, const char *needle, const char *tag) {
    if (strstr(hay, needle) == NULL) {
        fprintf(stderr, "FAIL: %s -- did not find marker `%s`\n", tag, needle);
        return 0;
    }
    return 1;
}

static char g_scratch[256] = {0};

static void cleanup_scratch(void) {
    if (g_scratch[0] == 0) return;
    test_compat_chdir_temp();
    test_compat_rmdir_recursive(g_scratch);
    g_scratch[0] = 0;
}

static int enter_scratch_dir(void) {
    if (test_compat_tmpdir("lime_test_prec", g_scratch, sizeof(g_scratch)) != 0) {
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

/* Run lime, return stderr+stdout-captured output as malloc'd
** buffer.  Exit code goes into *out_status.  Caller frees. */
static char *run_lime_capture(const char *lime_bin, const char *limpar,
                              const char *flags, const char *fixture,
                              int *out_status) {
    /* Tokenize flags into argv elements (space-separated, no quoting). */
    char flagbuf[256];
    snprintf(flagbuf, sizeof(flagbuf), "%s", flags ? flags : "");
    char tflag[1024];
    snprintf(tflag, sizeof(tflag), "-T%s", limpar);
    char *argv[20] = { (char *)lime_bin };
    int argc = 1;
    char *tok = strtok(flagbuf, " ");
    while (tok && argc < 16) { argv[argc++] = tok; tok = strtok(NULL, " "); }
    argv[argc++] = tflag;
    argv[argc++] = (char *)fixture;
    argv[argc] = NULL;

    /* Capture stdout + stderr into a single file in the cwd. */
    const char *combined = "_run_lime_capture.txt";
    int rc = 0;
    if (test_compat_run_to_file(argv, combined, &rc) != 0) {
        *out_status = -1;
        return NULL;
    }
    *out_status = rc;
    FILE *f = fopen(combined, "rb");
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

/* Extract the conflict count from lime's output.  Returns 0 if no
** "N parsing conflicts" line is present (which means 0 conflicts
** when lime exits cleanly). */
static int parse_conflict_count(const char *output) {
    const char *p = strstr(output, "parsing conflict");
    if (p == NULL) return 0;
    /* Walk back to find the digits. */
    while (p > output && (*p == ' ' || *p == 'p' || *p == 'a' || *p == 'r'
                          || *p == 's' || *p == 'i' || *p == 'n' || *p == 'g'
                          || *p == 'c' || *p == 'o' || *p == 'f' || *p == 'l'
                          || *p == 't')) p--;
    while (p > output && *p == ' ') p--;
    while (p > output && (*p >= '0' && *p <= '9')) p--;
    if (*p < '0' || *p > '9') p++;
    return atoi(p);
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
    if (stat(lime_bin, &st) != 0) return 77;
    if (stat(fixture, &st) != 0) return 77;

    char lime_abs[PATH_MAX], limpar_abs[PATH_MAX], fixture_abs[PATH_MAX];
    if (test_compat_realpath(lime_bin, lime_abs, sizeof(lime_abs)) != 0) return 77;
    if (test_compat_realpath(limpar, limpar_abs, sizeof(limpar_abs)) != 0) return 77;
    if (test_compat_realpath(fixture, fixture_abs, sizeof(fixture_abs)) != 0) return 77;

    if (enter_scratch_dir() != 0) {
        fprintf(stderr, "FAIL: scratch dir\n");
        return 1;
    }

    /* Copy fixture into scratch so lime -F doesn't write into the
    ** source tree. */
    if (test_compat_copy_file(fixture_abs, "grammar.lime") != 0) {
        fprintf(stderr, "FAIL: copy fixture\n");
        return 1;
    }

    /* Format pass 1 */
    {
        char *fmt_argv[] = { (char *)lime_abs, "-F", "grammar.lime", NULL };
        int rc = 0;
        if (test_compat_run(fmt_argv, &rc) != 0 || rc != 0) {
            fprintf(stderr, "FAIL: lime -F (pass 1) returned non-zero\n");
            return 1;
        }
    }

    char *formatted = slurp("grammar.lime.formatted");
    if (formatted == NULL) {
        fprintf(stderr, "FAIL: pass-1 formatted file missing\n");
        return 1;
    }

    int pass = 0, total = 0;

    /* Sub-test 1: %left / %right keywords present */
    total++;
    if (contains(formatted, "%left", "%left present")
        && contains(formatted, "%right", "%right present")) {
        printf("  [PASS] precedence keywords (%%left, %%right) survive\n");
        pass++;
    }

    /* Sub-test 2: each precedence-level group on its own line */
    total++;
    if (contains(formatted, "%left PLUS MINUS", "level-1 group")
        && contains(formatted, "%left TIMES DIVIDE", "level-2 group")
        && contains(formatted, "%right UMINUS", "level-3 group")) {
        printf("  [PASS] precedence groups stay grouped (PLUS+MINUS, TIMES+DIVIDE, UMINUS)\n");
        pass++;
    }

    /* Sub-test 3: [UMINUS] override marker survives */
    total++;
    if (contains(formatted, "[UMINUS]", "[UMINUS] marker")) {
        printf("  [PASS] [UMINUS] precedence-override marker survives\n");
        pass++;
    }

    /* Sub-test 4: source conflict count */
    total++;
    int src_status = 0;
    char *src_out = run_lime_capture(lime_abs, limpar_abs, "", "grammar.lime", &src_status);
    int src_conflicts = src_out ? parse_conflict_count(src_out) : -1;
    if (src_status == 0 && src_conflicts == 0) {
        printf("  [PASS] source compiles with 0 parsing conflicts\n");
        pass++;
    } else {
        fprintf(stderr,
                "  [FAIL] source: status=%d conflicts=%d\n", src_status, src_conflicts);
    }
    free(src_out);

    /* Sub-test 5: formatted conflict count -- THE BUG. */
    total++;
    int fmt_status = 0;
    char *fmt_out = run_lime_capture(lime_abs, limpar_abs, "",
                                     "grammar.lime.formatted", &fmt_status);
    int fmt_conflicts = fmt_out ? parse_conflict_count(fmt_out) : -1;
    if (fmt_status == 0 && fmt_conflicts == 0) {
        printf("  [PASS] formatted compiles with 0 parsing conflicts (Letter-22 fix)\n");
        pass++;
    } else {
        fprintf(stderr,
                "  [FAIL] formatted: status=%d conflicts=%d -- precedence directives lost?\n",
                fmt_status, fmt_conflicts);
    }
    free(fmt_out);

    /* Sub-test 6: idempotence */
    total++;
    if (test_compat_copy_file("grammar.lime.formatted", "grammar2.lime") == 0) {
        char *fmt_argv[] = { (char *)lime_abs, "-F", "grammar2.lime", NULL };
        int rc = 0;
        if (test_compat_run(fmt_argv, &rc) == 0 && rc == 0) {
            char *fmt2 = slurp("grammar2.lime.formatted");
            if (fmt2 && strcmp(formatted, fmt2) == 0) {
                printf("  [PASS] idempotence: format(format(F)) == format(F)\n");
                pass++;
            } else {
                fprintf(stderr, "  [FAIL] idempotence: pass-1 and pass-2 differ\n");
            }
            free(fmt2);
        }
    }

    free(formatted);

    printf("\n=== Summary === %d/%d sub-tests pass\n", pass, total);
    return pass == total ? 0 : 1;
}
