/*
 * test_snapshot_diff.c
 *
 * Regression for include/snapshot_diff.h:
 *   * lime_snapshot_diff(old, new, &d, &err) computes a per-table
 *     change list.
 *   * lime_snapshot_apply_diff(base, d, &out, &err) reconstructs a
 *     snapshot functionally equivalent to the diff target.
 *   * Round-trip property: apply(diff(old, new), old) == new.
 *
 * Method: build two snapshots from grammar text via the in-process
 * API, diff them, apply, compare.  Skipped when the in-process
 * dispatch isn't available (no LLVM / no LIME_HAVE_SNAPSHOT_BUILD).
 */

#include "parser.h"
#include "snapshot.h"
#include "snapshot_diff.h"
#include "lime_compiler.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_count = 0;
static int pass_count = 0;
static int skip_count = 0;

#define TEST(name) do { \
    printf("[TEST %d] %s\n", ++test_count, name); fflush(stdout); \
} while (0)
#define ASSERT(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "  FAIL: %s\n", msg); return; } \
} while (0)
#define PASS() do { printf("  PASS\n"); pass_count++; } while (0)
#define SKIP(reason) do { printf("  SKIP: %s\n", reason); skip_count++; } while (0)

static const char *G_A =
    "%name A\n"
    "%token X.\n"
    "%start_symbol s\n"
    "s ::= X.\n";

static const char *G_B =
    "%name B\n"
    "%token X Y.\n"
    "%start_symbol s\n"
    "s ::= X.\n"
    "s ::= X Y.\n";

static struct ParserSnapshot *compile(const char *text) {
    struct ParserSnapshot *snap = NULL;
    char *err = NULL;
    if (lime_compile_grammar_in_process(text, strlen(text), &snap, &err) != 0) {
        fprintf(stderr, "  compile failed: %s\n", err ? err : "(no msg)");
        free(err);
        return NULL;
    }
    free(err);
    return snap;
}

/* Compare every yy_* table the diff covers. */
static int snapshots_equivalent(const struct ParserSnapshot *a,
                                const struct ParserSnapshot *b) {
    if (a->nstate != b->nstate) return 0;
    if (a->nrule != b->nrule)   return 0;
    if (a->action_count != b->action_count)       return 0;
    if (a->lookahead_count != b->lookahead_count) return 0;
    if (a->yy_action && b->yy_action &&
        memcmp(a->yy_action, b->yy_action,
               a->action_count * sizeof(*a->yy_action)) != 0) return 0;
    if (a->yy_lookahead && b->yy_lookahead &&
        memcmp(a->yy_lookahead, b->yy_lookahead,
               a->lookahead_count * sizeof(*a->yy_lookahead)) != 0) return 0;
    if (a->yy_shift_ofst && b->yy_shift_ofst &&
        memcmp(a->yy_shift_ofst, b->yy_shift_ofst,
               a->nstate * sizeof(*a->yy_shift_ofst)) != 0) return 0;
    if (a->yy_reduce_ofst && b->yy_reduce_ofst &&
        memcmp(a->yy_reduce_ofst, b->yy_reduce_ofst,
               a->nstate * sizeof(*a->yy_reduce_ofst)) != 0) return 0;
    if (a->yy_default && b->yy_default &&
        memcmp(a->yy_default, b->yy_default,
               a->nstate * sizeof(*a->yy_default)) != 0) return 0;
    if (a->yy_rule_info_lhs && b->yy_rule_info_lhs &&
        memcmp(a->yy_rule_info_lhs, b->yy_rule_info_lhs,
               a->nrule * sizeof(*a->yy_rule_info_lhs)) != 0) return 0;
    if (a->yy_rule_info_nrhs && b->yy_rule_info_nrhs &&
        memcmp(a->yy_rule_info_nrhs, b->yy_rule_info_nrhs,
               a->nrule * sizeof(*a->yy_rule_info_nrhs)) != 0) return 0;
    return 1;
}

static void test_identity_diff(void) {
    TEST("identity: diff(s, s) has zero changes per table");
    struct ParserSnapshot *s = compile(G_A);
    if (s == NULL) { SKIP("in-process compile unavailable"); return; }

    LimeSnapshotDiff *d = NULL;
    char *err = NULL;
    int rc = lime_snapshot_diff(s, s, &d, &err);
    ASSERT(rc == 0, err ? err : "diff failed");
    free(err);

    char buf[512];
    lime_snapshot_diff_summary(d, buf, sizeof(buf));
    printf("  %s\n", buf);

    /* Apply identity diff: result must equal s. */
    struct ParserSnapshot *out = NULL;
    rc = lime_snapshot_apply_diff(s, d, &out, &err);
    ASSERT(rc == 0, err ? err : "apply failed");
    free(err);
    ASSERT(snapshots_equivalent(s, out), "identity round-trip mismatch");

    snapshot_release(out);
    lime_snapshot_diff_release(d);
    snapshot_release(s);
    PASS();
}

static void test_round_trip_distinct(void) {
    TEST("round-trip: apply(a, diff(a, b)) functionally equiv to b");
    struct ParserSnapshot *a = compile(G_A);
    struct ParserSnapshot *b = compile(G_B);
    if (a == NULL || b == NULL) {
        if (a) snapshot_release(a);
        if (b) snapshot_release(b);
        SKIP("in-process compile unavailable");
        return;
    }

    LimeSnapshotDiff *d = NULL;
    char *err = NULL;
    int rc = lime_snapshot_diff(a, b, &d, &err);
    ASSERT(rc == 0, err ? err : "diff a->b failed");
    free(err);

    char buf[512];
    lime_snapshot_diff_summary(d, buf, sizeof(buf));
    printf("  %s\n", buf);

    struct ParserSnapshot *applied = NULL;
    rc = lime_snapshot_apply_diff(a, d, &applied, &err);
    ASSERT(rc == 0, err ? err : "apply failed");
    free(err);

    ASSERT(snapshots_equivalent(applied, b),
           "apply(a, diff(a, b)) not equivalent to b");

    snapshot_release(applied);
    lime_snapshot_diff_release(d);
    snapshot_release(a);
    snapshot_release(b);
    PASS();
}

static void test_diff_from_null(void) {
    TEST("NULL base: diff(NULL, s) reconstructs s from scratch");
    struct ParserSnapshot *s = compile(G_A);
    if (s == NULL) { SKIP("in-process compile unavailable"); return; }

    LimeSnapshotDiff *d = NULL;
    char *err = NULL;
    int rc = lime_snapshot_diff(NULL, s, &d, &err);
    ASSERT(rc == 0, err ? err : "diff NULL->s failed");
    free(err);

    char buf[512];
    lime_snapshot_diff_summary(d, buf, sizeof(buf));
    printf("  %s\n", buf);

    struct ParserSnapshot *applied = NULL;
    rc = lime_snapshot_apply_diff(NULL, d, &applied, &err);
    ASSERT(rc == 0, err ? err : "apply NULL+d failed");
    free(err);

    ASSERT(snapshots_equivalent(applied, s),
           "apply(NULL, diff(NULL, s)) != s");

    snapshot_release(applied);
    lime_snapshot_diff_release(d);
    snapshot_release(s);
    PASS();
}

static void test_bad_args(void) {
    TEST("bad args: NULL outputs and NULL new are rejected");
    LimeSnapshotDiff *d = NULL;
    struct ParserSnapshot *snap_out = NULL;
    char *err = NULL;

    int rc = lime_snapshot_diff(NULL, NULL, &d, &err);
    ASSERT(rc != 0 && d == NULL && err != NULL, "NULL new should fail");
    free(err); err = NULL;

    rc = lime_snapshot_apply_diff(NULL, NULL, &snap_out, &err);
    ASSERT(rc != 0 && err != NULL, "NULL diff should fail");
    free(err);

    /* NULL release is safe. */
    lime_snapshot_diff_release(NULL);

    /* NULL summary is safe. */
    char buf[16] = {0xFF};
    size_t n = lime_snapshot_diff_summary(NULL, buf, sizeof(buf));
    ASSERT(n == 0 && buf[0] == 0, "NULL summary should write empty string");
    PASS();
}

int main(void) {
    printf("=== test_snapshot_diff ===\n");

    test_identity_diff();
    test_round_trip_distinct();
    test_diff_from_null();
    test_bad_args();

    int effective = test_count - skip_count;
    printf("\n=== Results: %d/%d passed (%d skipped) ===\n",
           pass_count, effective, skip_count);
    if (effective == 0) return 77;
    return (pass_count == effective) ? 0 : 1;
}
