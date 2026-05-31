/*
** test_formatter_prec_comments.c -- regression test for the
** precedence-directive leading-comment preservation gap surfaced
** during the v0.4.4 perf-audit follow-up.
**
** Asserts that `lime -F` preserves comments immediately preceding
** %left / %right / %nonassoc directives.  Pre-fix every comment
** above a precedence directive was silently dropped, which would
** silently lose project-wide instructions PG places above their
** %left / %right blocks (the canonical pattern for documenting
** operator-precedence rationale).
**
** Sub-tests:
**   1. SECTION-A multi-line block above first %left survives
**   2. SECTION-B comment between two %left directives survives
**   3. SECTION-C comment above %right survives
**   4. Idempotence: format(format(F)) == format(F)
**   5. Comments emit immediately above their directive (not stranded)
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

/* Returns 1 if `before` appears earlier than `after` in the haystack. */
static int comes_before(const char *hay, const char *before, const char *after,
                        const char *tag) {
    const char *b = strstr(hay, before);
    const char *a = strstr(hay, after);
    if (b == NULL || a == NULL || b > a) {
        fprintf(stderr, "FAIL: %s -- expected `%s` to come before `%s`\n",
                tag, before, after);
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
    if (test_compat_tmpdir("lime_test_prec_comments", g_scratch, sizeof(g_scratch)) != 0) {
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

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <lime-binary> <fixture.lime>\n", argv[0]);
        return 2;
    }
    const char *lime_bin = argv[1];
    const char *fixture  = argv[2];

    struct stat st;
    if (stat(lime_bin, &st) != 0) return 77;
    if (stat(fixture, &st) != 0) return 77;

    char lime_abs[PATH_MAX], fixture_abs[PATH_MAX];
    if (test_compat_realpath(lime_bin, lime_abs, sizeof(lime_abs)) != 0) return 77;
    if (test_compat_realpath(fixture, fixture_abs, sizeof(fixture_abs)) != 0) return 77;

    if (enter_scratch_dir() != 0) {
        fprintf(stderr, "FAIL: scratch dir\n");
        return 1;
    }

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cp '%s' grammar.lime", fixture_abs);
    if (system(cmd) != 0) return 1;

    snprintf(cmd, sizeof(cmd), "'%s' -F grammar.lime > /dev/null 2>&1", lime_abs);
    if (system(cmd) != 0) {
        fprintf(stderr, "FAIL: lime -F (pass 1) returned non-zero\n");
        return 1;
    }

    char *formatted = slurp("grammar.lime.formatted");
    if (formatted == NULL) {
        fprintf(stderr, "FAIL: pass-1 formatted file missing\n");
        return 1;
    }

    int pass = 0, total = 0;

    /* Sub-test 1: SECTION-A above first %left */
    total++;
    if (contains(formatted, "SECTION-A: Lowest precedence operators",
                 "SECTION-A banner")) {
        printf("  [PASS] SECTION-A multi-line block above first %%left survives\n");
        pass++;
    }

    /* Sub-test 2: SECTION-B between two %left */
    total++;
    if (contains(formatted, "SECTION-B: Multiplicative operators",
                 "SECTION-B banner")) {
        printf("  [PASS] SECTION-B comment between two %%left directives survives\n");
        pass++;
    }

    /* Sub-test 3: SECTION-C above %right */
    total++;
    if (contains(formatted, "SECTION-C: Unary precedence override",
                 "SECTION-C banner")) {
        printf("  [PASS] SECTION-C comment above %%right survives\n");
        pass++;
    }

    /* Sub-test 4: idempotence */
    total++;
    snprintf(cmd, sizeof(cmd),
             "cp grammar.lime.formatted grammar2.lime && '%s' -F grammar2.lime > /dev/null 2>&1",
             lime_abs);
    if (system(cmd) == 0) {
        char *fmt2 = slurp("grammar2.lime.formatted");
        if (fmt2 && strcmp(formatted, fmt2) == 0) {
            printf("  [PASS] idempotence: format(format(F)) == format(F)\n");
            pass++;
        } else {
            fprintf(stderr, "  [FAIL] idempotence: pass-1 and pass-2 differ\n");
        }
        free(fmt2);
    }

    /* Sub-test 5: each banner emits immediately above its directive
    ** (verifies the comment isn't stranded somewhere unrelated). */
    total++;
    int ok = 1;
    if (!comes_before(formatted, "SECTION-A", "%left PLUS MINUS",
                      "SECTION-A precedes %left PLUS MINUS")) ok = 0;
    if (!comes_before(formatted, "%left PLUS MINUS", "SECTION-B",
                      "SECTION-B follows %left PLUS MINUS")) ok = 0;
    if (!comes_before(formatted, "SECTION-B", "%left STAR SLASH",
                      "SECTION-B precedes %left STAR SLASH")) ok = 0;
    if (!comes_before(formatted, "%left STAR SLASH", "SECTION-C",
                      "SECTION-C follows %left STAR SLASH")) ok = 0;
    if (!comes_before(formatted, "SECTION-C", "%right UMINUS",
                      "SECTION-C precedes %right UMINUS")) ok = 0;
    if (ok) {
        printf("  [PASS] each banner emits immediately above its directive\n");
        pass++;
    }

    free(formatted);

    printf("\n=== Summary === %d/%d sub-tests pass\n", pass, total);
    return pass == total ? 0 : 1;
}
