/*
** test_formatter_loc_directives.c -- regression test for Lime-Letter-23.
**
** Asserts that `lime -F` preserves %first_token, %locations, and
** %location_type directives.  Pre-fix all three were silently
** dropped from the formatted output, regenerating a parser whose
** action bodies referenced yyloc without the snapshot tracking it.
**
** Sub-tests:
**   1. %first_token N survives with the literal N value
**   2. %locations bare flag survives
**   3. %location_type {TYPE} survives with the literal TYPE
**   4. Idempotence: format(format(F)) == format(F)
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
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
    if (tmp && tmp[0]) candidates[n++] = tmp;
    candidates[n++] = "/tmp";
    for (int i = 0; i < n; i++) {
        struct stat st;
        if (stat(candidates[i], &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        snprintf(g_scratch, sizeof(g_scratch),
                 "%s/lime_test_loc.XXXXXX", candidates[i]);
        if (mkdtemp(g_scratch) == NULL) { g_scratch[0] = 0; continue; }
        if (chdir(g_scratch) != 0) { cleanup_scratch(); continue; }
        atexit(cleanup_scratch);
        return 0;
    }
    g_scratch[0] = 0;
    return -1;
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
    if (realpath(lime_bin, lime_abs) == NULL) return 77;
    if (realpath(fixture, fixture_abs) == NULL) return 77;

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

    /* Sub-test 1: %first_token */
    total++;
    if (contains(formatted, "%first_token 257", "%first_token 257")) {
        printf("  [PASS] %%first_token 257 survives\n");
        pass++;
    }

    /* Sub-test 2: %locations */
    total++;
    if (contains(formatted, "%locations", "%locations bare flag")) {
        printf("  [PASS] %%locations bare flag survives\n");
        pass++;
    }

    /* Sub-test 3: %location_type */
    total++;
    if (contains(formatted, "%location_type {YYLTYPE}", "%location_type {YYLTYPE}")) {
        printf("  [PASS] %%location_type {YYLTYPE} survives\n");
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
            free(fmt2);
        } else {
            fprintf(stderr, "  [FAIL] idempotence: pass-1 and pass-2 differ\n");
            free(fmt2);
        }
    }

    free(formatted);

    printf("\n=== Summary === %d/%d sub-tests pass\n", pass, total);
    return pass == total ? 0 : 1;
}
