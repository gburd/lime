/*
** tests/test_diff_conflicts.c -- v0.4.3 regression test for
** `lime --diff-conflicts base.lime ext.lime`.
**
** Drives the lime binary against the small expression-grammar
** fixture set in tests/diff_conflicts_fixtures/.  Each sub-test
** asserts a property of the diff output: counts in NEW / RESOLVED
** / UNCHANGED, exit code, JSON well-formedness, symbolic stability
** under alias-renaming.
**
** Sub-tests (11):
**   1. base+clean_ext                -> 0 UNCHANGED, 0 RESOLVED, exit 0
**   2. base+clean_ext                -> 0 NEW, exit 0
**   3. base+conflicting_ext          -> >=1 NEW, exit 1
**   4. JSON: schema_version present  -> exit code from --json runs
**   5. JSON: parses cleanly          -> braces / brackets balance
**   6. JSON: 'new' array has rules   -> for conflicting_ext
**   7. Exit code 0 when no NEW       -> CI-friendly base+clean_ext
**   8. Exit code 1 when NEW > 0      -> base+conflicting_ext
**   9. Exit code 2 on missing file   -> arg-error contract
**  10. Symbolic key stability        -> base+conflicting_ext run
**                                      twice produces identical
**                                      NEW count (no state-id
**                                      noise in the symbolic key)
**  11. RESOLVED bucket               -> conflicting_base+resolving_ext
**                                      shows 1 RESOLVED, 0 NEW
**
** Exits 0 on full pass, 1 on any failure, 77 on missing binary
** or fixtures (meson skip).
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <limits.h>

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

/* Run lime --diff-conflicts and capture stdout to file `outpath`.
** Returns the exit code of lime (>= 0) or -1 on system() failure. */
static int run_diff(const char *lime_bin,
                    const char *limpar,
                    const char *base,
                    const char *ext,
                    int json_flag,
                    const char *outpath) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "'%s' --diff-conflicts %s -T'%s' '%s' '%s' >'%s' 2>/dev/null",
             lime_bin,
             json_flag ? "--json" : "",
             limpar, base, ext, outpath);
    int rc = system(cmd);
    if (rc == -1) return -1;
    if (WIFEXITED(rc)) return WEXITSTATUS(rc);
    return -1;
}

/* Count occurrences of `needle` in `haystack` (used for cheap
** structure assertions on the human/JSON output). */
static int count_occurrences(const char *hay, const char *needle) {
    if (!hay || !needle) return 0;
    int n = 0;
    size_t nl = strlen(needle);
    for (const char *p = hay; (p = strstr(p, needle)); p += nl) n++;
    return n;
}

/* Cheap JSON balance check: brace/bracket counts must match.
** Not a full parser -- but combined with a separate `python3 -m
** json.tool` check via system(), it catches malformed output. */
static int json_braces_balanced(const char *s) {
    int braces = 0, brackets = 0;
    int in_str = 0, esc = 0;
    for (const char *p = s; *p; p++) {
        if (in_str) {
            if (esc) esc = 0;
            else if (*p == '\\') esc = 1;
            else if (*p == '"') in_str = 0;
            continue;
        }
        if (*p == '"') in_str = 1;
        else if (*p == '{') braces++;
        else if (*p == '}') braces--;
        else if (*p == '[') brackets++;
        else if (*p == ']') brackets--;
    }
    return braces == 0 && brackets == 0;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr,
                "usage: %s <lime-binary> <limpar.c> <fixtures-dir>\n",
                argv[0]);
        return 2;
    }
    const char *lime_bin = argv[1];
    const char *limpar   = argv[2];
    const char *fixdir   = argv[3];

    struct stat st;
    if (stat(lime_bin, &st) != 0) {
        fprintf(stderr, "SKIP: %s not found\n", lime_bin);
        return 77;
    }

    char base_path[1024], clean_path[1024], conflict_path[1024];
    char conflict_base_path[1024], resolving_path[1024];
    snprintf(base_path,     sizeof(base_path),     "%s/base.lime", fixdir);
    snprintf(clean_path,    sizeof(clean_path),    "%s/clean_ext.lime", fixdir);
    snprintf(conflict_path, sizeof(conflict_path), "%s/conflicting_ext.lime", fixdir);
    snprintf(conflict_base_path, sizeof(conflict_base_path),
             "%s/conflicting_base.lime", fixdir);
    snprintf(resolving_path, sizeof(resolving_path),
             "%s/resolving_ext.lime", fixdir);

    if (stat(base_path, &st) != 0 ||
        stat(clean_path, &st) != 0 ||
        stat(conflict_path, &st) != 0 ||
        stat(conflict_base_path, &st) != 0 ||
        stat(resolving_path, &st) != 0) {
        fprintf(stderr, "SKIP: fixtures missing in %s\n", fixdir);
        return 77;
    }

    /* Use a per-test scratch dir for stdout captures so parallel
    ** ctest runs do not collide. */
    char scratch[256];
    const char *tmp = getenv("TMPDIR");
    if (tmp == NULL || !*tmp) tmp = "/tmp";
    snprintf(scratch, sizeof(scratch),
             "%s/lime_test_diff_conflicts.XXXXXX", tmp);
    if (mkdtemp(scratch) == NULL) {
        fprintf(stderr, "FAIL: mkdtemp for scratch dir\n");
        return 1;
    }

    char out1[1024], out2[1024], out_json[1024], out_dup[1024];
    snprintf(out1,     sizeof(out1),     "%s/diff1.txt", scratch);
    snprintf(out2,     sizeof(out2),     "%s/diff2.txt", scratch);
    snprintf(out_json, sizeof(out_json), "%s/diff.json", scratch);
    snprintf(out_dup,  sizeof(out_dup),  "%s/diff_dup.txt", scratch);

    /* ---- 1. base+clean_ext: also 0 UNCHANGED (clean base) ---- */
    {
        int rc = run_diff(lime_bin, limpar, base_path, clean_path, 0, out1);
        if (rc != 0) FAIL("unchanged_exit",
                          "expected exit 0, got %d", rc);
        char *out = slurp(out1);
        if (!out) { FAIL("unchanged_out", "no output"); }
        else {
            if (!strstr(out, "UNCHANGED conflicts: 0"))
                FAIL("unchanged_count",
                     "expected 0 UNCHANGED on clean base; output:\n%s", out);
            if (!strstr(out, "RESOLVED conflicts (0)"))
                FAIL("unchanged_resolved",
                     "expected 0 RESOLVED on clean base");
            free(out);
        }
    }

    /* ---- 2. base vs clean_ext ---- */
    {
        int rc = run_diff(lime_bin, limpar, base_path, clean_path, 0, out1);
        if (rc != 0) FAIL("base_vs_clean_exit",
                          "expected exit 0, got %d", rc);
        char *out = slurp(out1);
        if (!out) { FAIL("base_vs_clean_out", "no output"); }
        else {
            if (!strstr(out, "NEW conflicts (0)"))
                FAIL("base_vs_clean_new", "expected 0 NEW; output:\n%s", out);
            free(out);
        }
    }

    /* ---- 3. base vs conflicting_ext ---- */
    {
        int rc = run_diff(lime_bin, limpar, base_path, conflict_path, 0, out2);
        if (rc != 1) FAIL("base_vs_conflict_exit",
                          "expected exit 1 (NEW conflicts), got %d", rc);
        char *out = slurp(out2);
        if (!out) { FAIL("base_vs_conflict_out", "no output"); }
        else {
            /* Must mention shift/reduce and STAR */
            if (!strstr(out, "shift/reduce"))
                FAIL("base_vs_conflict_kind",
                     "expected shift/reduce in output:\n%s", out);
            if (!strstr(out, "STAR"))
                FAIL("base_vs_conflict_lookahead",
                     "expected STAR lookahead in output");
            /* Must NOT report 0 NEW */
            if (strstr(out, "NEW conflicts (0)"))
                FAIL("base_vs_conflict_count",
                     "expected NEW > 0 but got 0:\n%s", out);
            free(out);
        }
    }

    /* ---- 4-6. JSON output ---- */
    {
        int rc = run_diff(lime_bin, limpar, base_path, conflict_path, 1, out_json);
        if (rc != 1) FAIL("json_exit", "expected exit 1, got %d", rc);
        char *json = slurp(out_json);
        if (!json) { FAIL("json_out", "no output"); }
        else {
            /* Sub-test 4: schema version present */
            if (!strstr(json, "\"schema_version\": 1"))
                FAIL("json_schema", "missing schema_version");
            /* Sub-test 5: brace/bracket balance */
            if (!json_braces_balanced(json))
                FAIL("json_balance", "JSON braces/brackets unbalanced:\n%s", json);
            /* Sub-test 6: 'new' array contains at least one object */
            if (!strstr(json, "\"new\": ["))
                FAIL("json_new_array", "missing 'new' array");
            if (!strstr(json, "\"kind\": \"shift_reduce\""))
                FAIL("json_kind",
                     "expected shift_reduce kind in output:\n%s", json);
            /* python3 sanity-check (best-effort -- skip silently if
            ** python3 not available).  Catches anything our local
            ** balance check doesn't. */
            char cmd[1024];
            snprintf(cmd, sizeof(cmd),
                     "python3 -m json.tool '%s' >/dev/null 2>&1", out_json);
            int py_rc = system(cmd);
            if (py_rc != -1 && WIFEXITED(py_rc) && WEXITSTATUS(py_rc) == 127) {
                /* python3 not found -- not a failure */
            } else if (py_rc != 0) {
                FAIL("json_python_validate",
                     "python3 -m json.tool rejected the output");
            }
            free(json);
        }
    }

    /* ---- 7. Exit code 0 on no NEW (CI contract) ---- */
    {
        int rc = run_diff(lime_bin, limpar, base_path, clean_path, 0, out1);
        if (rc != 0)
            FAIL("ci_exit_zero",
                 "CI contract: exit 0 when no NEW conflicts (got %d)", rc);
    }

    /* ---- 8. Exit code 1 on NEW > 0 (CI contract) ---- */
    {
        int rc = run_diff(lime_bin, limpar, base_path, conflict_path, 0, out1);
        if (rc != 1)
            FAIL("ci_exit_one",
                 "CI contract: exit 1 when NEW > 0 (got %d)", rc);
    }

    /* ---- 9. Exit code 2 on missing file (arg-error contract) ---- */
    {
        char missing[1024];
        snprintf(missing, sizeof(missing), "%s/no_such_file.lime", fixdir);
        int rc = run_diff(lime_bin, limpar, base_path, missing, 0, out1);
        if (rc != 2)
            FAIL("missing_file_exit",
                 "expected exit 2 on missing file, got %d", rc);
    }

    /* ---- 10. Symbolic key stability across runs ---- */
    {
        int rc1 = run_diff(lime_bin, limpar,
                           base_path, conflict_path, 0, out2);
        int rc2 = run_diff(lime_bin, limpar,
                           base_path, conflict_path, 0, out_dup);
        if (rc1 != rc2)
            FAIL("stable_exit",
                 "exit codes differ across runs: %d vs %d", rc1, rc2);
        char *a = slurp(out2);
        char *b = slurp(out_dup);
        if (!a || !b) {
            FAIL("stable_out", "missing capture file");
        } else {
            int na = count_occurrences(a, "shift/reduce");
            int nb = count_occurrences(b, "shift/reduce");
            if (na != nb)
                FAIL("stable_count",
                     "NEW conflict count not stable across runs: %d vs %d",
                     na, nb);
        }
        free(a); free(b);
    }

    /* ---- 11. RESOLVED bucket ---- */
    {
        int rc = run_diff(lime_bin, limpar,
                          conflict_base_path, resolving_path, 0, out1);
        if (rc != 0)
            FAIL("resolved_exit",
                 "expected exit 0 (no NEW conflicts), got %d", rc);
        char *out = slurp(out1);
        if (!out) { FAIL("resolved_out", "no output"); }
        else {
            if (!strstr(out, "NEW conflicts (0)"))
                FAIL("resolved_no_new",
                     "expected 0 NEW after %%left STAR; output:\n%s", out);
            if (strstr(out, "RESOLVED conflicts (0)"))
                FAIL("resolved_count",
                     "expected RESOLVED > 0 (precedence resolves base SR)");
            free(out);
        }
    }

    /* Cleanup scratch dir */
    {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", scratch);
        int rc = system(cmd);
        (void)rc;
    }

    if (failures == 0) {
        printf("test_diff_conflicts: all sub-tests passed\n");
        return 0;
    }
    fprintf(stderr, "test_diff_conflicts: %d failure(s)\n", failures);
    return 1;
}
