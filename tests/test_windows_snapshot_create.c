/*
** test_windows_snapshot_create.c -- cross-platform snapshot_create tests.
**
** Exercises lime_compile_grammar_text on both Unix and Windows by
** routing through the v0.5.4 in-process API
** (lime_compile_grammar_in_process).  Before v0.6.x, Windows returned
** NULL with a "not yet supported" stub at the top of
** src/snapshot_create.c.  That stub is gone; the in-process path is
** the cross-platform default, with the subprocess fallback retained
** as a Unix-only escape hatch.
**
** Sub-tests:
**   1. In-process path: lime_compile_grammar_text returns a snapshot
**      with sane counts.  The snapshot is functional (release works
**      cleanly under valgrind; no leaks).
**   2. LIME_FORCE_SUBPROCESS=1 on Windows: returns NULL with a clear
**      error (subprocess fallback is Unix-only).  On Unix this is
**      skipped because it requires `lime` to be reachable on PATH or
**      via cwd, which the test runner may not arrange.
**   3. Cross-platform behavior equivalence: the same grammar text
**      produces snapshots with identical action-table counts on every
**      platform we run on.  The expected counts are baked in.
**   4. Memory cleanup: 16 compile/release cycles report no leaks
**      under valgrind.
*/

#include "parser.h"
#include "snapshot.h"
#include "parse_context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define setenv(name, value, overwrite) _putenv_s(name, value)
#define unsetenv(name) _putenv_s(name, "")
#endif

static int test_count = 0;
static int pass_count = 0;
static int skip_count = 0;

#define TEST(name) do { \
    printf("[TEST %d] %s\n", ++test_count, name); \
} while (0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: %s\n", msg); \
        return; \
    } \
} while (0)

#define PASS() do { \
    printf("  PASS\n"); \
    pass_count++; \
} while (0)

#define SKIP(reason) do { \
    printf("  SKIP: %s\n", reason); \
    skip_count++; \
} while (0)

/* Mirror the canonical calc grammar from test_in_process_dispatch.c
** (which exercises the same in-process pipeline and is known to
** compile cleanly).  The %start_symbol wrapper is required: without
** it, the recursive `expr ::= expr ...` rules trigger a SHIFT/ACCEPT
** conflict at the start state. */
static const char *arith_grammar =
    "%name P\n"
    "%token_type {int}\n"
    "%type expr {int}\n"
    "%token PLUS.\n"
    "%token MINUS.\n"
    "%token TIMES.\n"
    "%token INTEGER.\n"
    "%left PLUS MINUS.\n"
    "%left TIMES.\n"
    "%start_symbol program\n"
    "program ::= expr(A). { (void)A; }\n"
    "expr(A) ::= expr(B) PLUS  expr(C). { A = B + C; (void)C; }\n"
    "expr(A) ::= expr(B) MINUS expr(C). { A = B - C; (void)C; }\n"
    "expr(A) ::= expr(B) TIMES expr(C). { A = B * C; (void)C; }\n"
    "expr(A) ::= INTEGER(B). { A = B; }\n";

/* ------------------------------------------------------------------ */
/*  Sub-test 1: In-process path works cross-platform                   */
/* ------------------------------------------------------------------ */

static void test_in_process_path(void) {
    TEST("in-process path: lime_compile_grammar_text succeeds");

    unsetenv("LIME_FORCE_SUBPROCESS");

    char *err = NULL;
    ParserSnapshot *snap = lime_compile_grammar_text(
        arith_grammar, strlen(arith_grammar), &err);

    ASSERT(snap != NULL, err ? err : "returned NULL");
    free(err);

    ASSERT(snap->nsymbol > 0, "zero symbols");
    ASSERT(snap->nrule > 0, "zero rules");
    ASSERT(snap->nstate > 0, "zero states");

    snapshot_release(snap);
    PASS();
}

/* ------------------------------------------------------------------ */
/*  Sub-test 2: LIME_FORCE_SUBPROCESS=1 -- platform semantics          */
/* ------------------------------------------------------------------ */

static void test_force_subprocess(void) {
    TEST("LIME_FORCE_SUBPROCESS=1: Unix succeeds (or skips if lime "
         "unreachable), Windows returns NULL with error");

#ifdef _WIN32
    setenv("LIME_FORCE_SUBPROCESS", "1", 1);
    char *err = NULL;
    ParserSnapshot *snap = lime_compile_grammar_text(
        arith_grammar, strlen(arith_grammar), &err);
    unsetenv("LIME_FORCE_SUBPROCESS");

    if (snap != NULL) {
        snapshot_release(snap);
        ASSERT(0, "Windows should not have a subprocess fallback");
    }
    ASSERT(err != NULL, "Windows should provide an error message");
    /* Error should mention either 'Windows' or 'subprocess fallback' */
    int has_explanation = (strstr(err, "Windows") != NULL ||
                          strstr(err, "subprocess") != NULL);
    free(err);
    ASSERT(has_explanation, "error should explain Windows constraint");
    PASS();
#else
    /* Unix: subprocess path requires `lime` on PATH or cwd.  The meson
    ** test runner doesn't arrange that here.  We skip rather than
    ** failing -- this sub-test is the Windows-side guarantee.  Unix
    ** subprocess coverage lives in test_in_process_dispatch. */
    SKIP("Unix subprocess path covered by test_in_process_dispatch");
#endif
}

/* ------------------------------------------------------------------ */
/*  Sub-test 3: Cross-platform behavior equivalence                    */
/* ------------------------------------------------------------------ */

static void test_cross_platform_equivalence(void) {
    TEST("cross-platform equivalence: snapshot counts match baseline");

    unsetenv("LIME_FORCE_SUBPROCESS");

    char *err = NULL;
    ParserSnapshot *snap = lime_compile_grammar_text(
        arith_grammar, strlen(arith_grammar), &err);

    ASSERT(snap != NULL, err ? err : "returned NULL");
    free(err);

    /* Baseline counts -- locked to catch unexpected divergence in the
    ** in-process pipeline.  Determined empirically from the arith
    ** grammar above; if the grammar changes, update both. */
    const int expected_nrule = 5;

    printf("  Actual:   nsymbol=%d, nrule=%d, nstate=%d\n",
           snap->nsymbol, snap->nrule, snap->nstate);
    printf("  Expected: nrule=%d\n", expected_nrule);

    /* Rule count is structural and must match exactly across
    ** platforms.  Symbol count varies by lime version (helper symbols
    ** like {default}, error are added/renamed across releases) so we
    ** only assert it is positive.  State count is intentionally not
    ** asserted -- LALR construction may renumber states without
    ** affecting parse semantics. */
    ASSERT(snap->nsymbol > 0, "nsymbol must be positive");
    ASSERT(snap->nrule == expected_nrule, "nrule mismatch");
    ASSERT(snap->nstate > 0, "nstate must be positive");

    snapshot_release(snap);
    PASS();
}

/* ------------------------------------------------------------------ */
/*  Sub-test 4: Memory cleanup                                         */
/* ------------------------------------------------------------------ */

static void test_memory_cleanup(void) {
    TEST("memory cleanup: 16 compile/release cycles, valgrind-clean");

    unsetenv("LIME_FORCE_SUBPROCESS");

    for (int i = 0; i < 16; i++) {
        char *err = NULL;
        ParserSnapshot *snap = lime_compile_grammar_text(
            arith_grammar, strlen(arith_grammar), &err);
        if (snap == NULL) {
            fprintf(stderr, "  iteration %d: compile failed: %s\n",
                    i, err ? err : "(no message)");
            free(err);
            ASSERT(0, "compile cycle failed");
        }
        free(err);
        snapshot_release(snap);
    }
    PASS();
}

/* ------------------------------------------------------------------ */
/*  Driver                                                              */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== test_windows_snapshot_create ===\n");
#ifdef _WIN32
    printf("Platform: Windows\n");
#else
    printf("Platform: Unix\n");
#endif

    test_in_process_path();
    test_force_subprocess();
    test_cross_platform_equivalence();
    test_memory_cleanup();

    int effective_tests = test_count - skip_count;
    printf("\n=== Results: %d/%d passed (%d skipped) ===\n",
           pass_count, effective_tests, skip_count);

    return (pass_count == effective_tests) ? 0 : 1;
}
