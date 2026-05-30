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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include "posix_shim.h"

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
    int n_cand = 0;
    const char *tmp = getenv("TMPDIR");
    if (tmp != NULL && tmp[0] != 0) candidates[n_cand++] = tmp;
    candidates[n_cand++] = "/tmp";
    for (int i = 0; i < n_cand; i++) {
        struct stat st;
        if (stat(candidates[i], &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        int n = snprintf(g_scratch, sizeof(g_scratch),
                         "%s/lime_test_prec.XXXXXX", candidates[i]);
        if (n < 0 || (size_t)n >= sizeof(g_scratch)) continue;
        if (mkdtemp(g_scratch) == NULL) { g_scratch[0] = 0; continue; }
        if (chdir(g_scratch) != 0) { cleanup_scratch(); continue; }
        atexit(cleanup_scratch);
        return 0;
    }
    g_scratch[0] = 0;
    return -1;
}

/* Run lime, return stderr-captured output as malloc'd buffer.  Exit
** code goes into *out_status.  Caller frees the buffer. */
static char *run_lime_capture(const char *lime_bin, const char *limpar,
                              const char *flags, const char *fixture,
                              int *out_status) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "'%s' %s -T'%s' '%s' 2>&1",
             lime_bin, flags, limpar, fixture);
    FILE *p = popen(cmd, "r");
    if (p == NULL) { *out_status = -1; return NULL; }
    size_t cap = 4096, len = 0;
    char *buf = (char *)malloc(cap);
    if (buf == NULL) { pclose(p); *out_status = -1; return NULL; }
    int c;
    while ((c = fgetc(p)) != EOF) {
        if (len + 1 >= cap) {
            cap *= 2;
            char *n = (char *)realloc(buf, cap);
            if (n == NULL) { free(buf); pclose(p); *out_status = -1; return NULL; }
            buf = n;
        }
        buf[len++] = (char)c;
    }
    buf[len] = 0;
    int rc = pclose(p);
    *out_status = rc;
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
    if (realpath(lime_bin, lime_abs) == NULL) return 77;
    if (realpath(limpar, limpar_abs) == NULL) return 77;
    if (realpath(fixture, fixture_abs) == NULL) return 77;

    if (enter_scratch_dir() != 0) {
        fprintf(stderr, "FAIL: scratch dir\n");
        return 1;
    }

    /* Copy fixture into scratch so lime -F doesn't write into the
    ** source tree. */
    {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "cp '%s' grammar.lime", fixture_abs);
        if (system(cmd) != 0) return 1;
    }

    /* Format pass 1 */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "'%s' -F grammar.lime > /dev/null 2>&1", lime_abs);
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

    free(formatted);

    printf("\n=== Summary === %d/%d sub-tests pass\n", pass, total);
    return pass == total ? 0 : 1;
}
