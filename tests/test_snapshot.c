/*
** Unit tests for the snapshot system.
**
** Tests reference counting (acquire/release), NULL safety,
** manual snapshot population, and clone_snapshot().
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "parser.h"
#include "snapshot.h"
#include "snapshot_modify.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-50s ", name); \
} while(0)

#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)

/*
** Helper: create a minimal snapshot manually for testing acquire/release
** without depending on create_base_snapshot() being fully wired up.
*/
static ParserSnapshot *make_test_snapshot(void) {
    ParserSnapshot *snap = calloc(1, sizeof(ParserSnapshot));
    if (snap == NULL) return NULL;

    snap->version = 1;
    atomic_init(&snap->refcount, 1);

    snap->nsymbol = 4;
    snap->nterminal = 2;
    snap->nrule = 3;
    snap->nstate = 5;

    /* Allocate minimal action tables */
    snap->action_count = 3;
    snap->yy_action = calloc(3, sizeof(uint16_t));
    snap->yy_action[0] = 10;
    snap->yy_action[1] = 20;
    snap->yy_action[2] = 30;

    snap->lookahead_count = 3;
    snap->yy_lookahead = calloc(3, sizeof(uint16_t));
    snap->yy_lookahead[0] = 0;
    snap->yy_lookahead[1] = 1;
    snap->yy_lookahead[2] = 2;

    snap->yy_shift_ofst = calloc(5, sizeof(int32_t));
    snap->yy_reduce_ofst = calloc(5, sizeof(int32_t));
    snap->yy_default = calloc(5, sizeof(uint16_t));

    snap->symbols = NULL;
    snap->rules = NULL;
    snap->states = NULL;
    snap->jit_ctx = NULL;
    snap->create_time_ns = 0;

    return snap;
}

/* Test: snapshot_acquire(NULL) returns NULL */
static void test_acquire_null(void) {
    TEST("snapshot_acquire(NULL) returns NULL");
    ParserSnapshot *result = snapshot_acquire(NULL);
    if (result == NULL) {
        PASS();
    } else {
        FAIL("expected NULL");
    }
}

/* Test: snapshot_release(NULL) is a safe no-op */
static void test_release_null(void) {
    TEST("snapshot_release(NULL) is safe no-op");
    /* This should not crash */
    snapshot_release(NULL);
    PASS();
}

/* Test: create and release a manual snapshot */
static void test_create_and_release(void) {
    TEST("create + release manual snapshot");
    ParserSnapshot *snap = make_test_snapshot();
    if (snap == NULL) {
        FAIL("make_test_snapshot returned NULL");
        return;
    }
    /* Refcount should be 1 after creation */
    uint_fast32_t rc = atomic_load(&snap->refcount);
    if (rc != 1) {
        FAIL("initial refcount != 1");
        snapshot_release(snap);
        return;
    }
    /* Release should free the snapshot (no crash = success) */
    snapshot_release(snap);
    PASS();
}

/* Test: acquire increments refcount */
static void test_acquire_increments(void) {
    TEST("snapshot_acquire increments refcount");
    ParserSnapshot *snap = make_test_snapshot();
    if (snap == NULL) {
        FAIL("make_test_snapshot returned NULL");
        return;
    }
    ParserSnapshot *ref = snapshot_acquire(snap);
    if (ref != snap) {
        FAIL("acquire did not return same pointer");
        snapshot_release(snap);
        return;
    }
    uint_fast32_t rc = atomic_load(&snap->refcount);
    if (rc != 2) {
        char buf[64];
        snprintf(buf, sizeof(buf), "expected refcount 2, got %lu", (unsigned long)rc);
        FAIL(buf);
        snapshot_release(snap);
        snapshot_release(snap);
        return;
    }
    /* Release both references */
    snapshot_release(snap);
    /* After one release, refcount should be 1 */
    rc = atomic_load(&snap->refcount);
    if (rc != 1) {
        char buf[64];
        snprintf(buf, sizeof(buf), "expected refcount 1 after release, got %lu", (unsigned long)rc);
        FAIL(buf);
        snapshot_release(snap);
        return;
    }
    /* Final release frees the snapshot */
    snapshot_release(snap);
    PASS();
}

/* Test: multiple acquire/release cycles */
static void test_multiple_acquire_release(void) {
    TEST("multiple acquire/release cycles");
    ParserSnapshot *snap = make_test_snapshot();
    if (snap == NULL) {
        FAIL("make_test_snapshot returned NULL");
        return;
    }

    /* Acquire 10 additional references */
    for (int i = 0; i < 10; i++) {
        snapshot_acquire(snap);
    }
    uint_fast32_t rc = atomic_load(&snap->refcount);
    if (rc != 11) {
        char buf[64];
        snprintf(buf, sizeof(buf), "expected refcount 11, got %lu", (unsigned long)rc);
        FAIL(buf);
        /* Clean up */
        for (int i = 0; i < 11; i++) snapshot_release(snap);
        return;
    }

    /* Release 10 references */
    for (int i = 0; i < 10; i++) {
        snapshot_release(snap);
    }
    rc = atomic_load(&snap->refcount);
    if (rc != 1) {
        char buf[64];
        snprintf(buf, sizeof(buf), "expected refcount 1, got %lu", (unsigned long)rc);
        FAIL(buf);
        snapshot_release(snap);
        return;
    }

    /* Final release */
    snapshot_release(snap);
    PASS();
}

/* Test: snapshot field values survive acquire/release */
static void test_fields_preserved(void) {
    TEST("snapshot fields preserved across acquire/release");
    ParserSnapshot *snap = make_test_snapshot();
    if (snap == NULL) {
        FAIL("make_test_snapshot returned NULL");
        return;
    }

    snapshot_acquire(snap);

    /* Check fields */
    int ok = 1;
    if (snap->version != 1) { ok = 0; FAIL("version changed"); }
    else if (snap->nsymbol != 4) { ok = 0; FAIL("nsymbol changed"); }
    else if (snap->nterminal != 2) { ok = 0; FAIL("nterminal changed"); }
    else if (snap->nrule != 3) { ok = 0; FAIL("nrule changed"); }
    else if (snap->nstate != 5) { ok = 0; FAIL("nstate changed"); }
    else if (snap->action_count != 3) { ok = 0; FAIL("action_count changed"); }
    else if (snap->yy_action[0] != 10) { ok = 0; FAIL("yy_action[0] changed"); }
    else if (snap->yy_action[1] != 20) { ok = 0; FAIL("yy_action[1] changed"); }
    else if (snap->yy_action[2] != 30) { ok = 0; FAIL("yy_action[2] changed"); }

    snapshot_release(snap);
    snapshot_release(snap);

    if (ok) PASS();
}

/* Test: create_base_snapshot stub returns error */
static void test_create_base_snapshot_stub(void) {
    TEST("create_base_snapshot (stub) returns error");
    char *error = NULL;
    ParserSnapshot *snap = create_base_snapshot("dummy.y", &error);

    if (snap != NULL) {
        FAIL("expected NULL from stub");
        snapshot_release(snap);
        free(error);
        return;
    }
    if (error == NULL) {
        FAIL("expected error message from stub");
        return;
    }
    /* The stub should provide an error message */
    if (strlen(error) == 0) {
        FAIL("error message is empty");
        free(error);
        return;
    }
    free(error);
    PASS();
}

/* Test: create_base_snapshot with NULL error pointer */
static void test_create_base_snapshot_null_error(void) {
    TEST("create_base_snapshot with NULL error pointer");
    ParserSnapshot *snap = create_base_snapshot("dummy.y", NULL);
    if (snap != NULL) {
        FAIL("expected NULL from stub");
        snapshot_release(snap);
        return;
    }
    PASS();
}

/* Test: clone_snapshot produces independent copy */
static void test_clone_snapshot(void) {
    TEST("clone_snapshot produces independent copy");
    ParserSnapshot *orig = make_test_snapshot();
    if (orig == NULL) {
        FAIL("make_test_snapshot returned NULL");
        return;
    }

    ParserSnapshot *clone = clone_snapshot(orig);
    if (clone == NULL) {
        FAIL("clone_snapshot returned NULL");
        snapshot_release(orig);
        return;
    }

    /* Clone should be a different pointer */
    if (clone == orig) {
        FAIL("clone is same pointer as original");
        snapshot_release(orig);
        snapshot_release(clone);
        return;
    }

    /* Clone should have refcount 1 */
    uint_fast32_t rc = atomic_load(&clone->refcount);
    if (rc != 1) {
        char buf[64];
        snprintf(buf, sizeof(buf), "clone refcount expected 1, got %lu", (unsigned long)rc);
        FAIL(buf);
        snapshot_release(orig);
        snapshot_release(clone);
        return;
    }

    /* Clone version should be incremented by 1 */
    int ok = 1;
    if (clone->version != orig->version + 1) { ok = 0; FAIL("version not incremented"); }
    else if (clone->nsymbol != orig->nsymbol) { ok = 0; FAIL("nsymbol mismatch"); }
    else if (clone->nterminal != orig->nterminal) { ok = 0; FAIL("nterminal mismatch"); }
    else if (clone->nrule != orig->nrule) { ok = 0; FAIL("nrule mismatch"); }
    else if (clone->nstate != orig->nstate) { ok = 0; FAIL("nstate mismatch"); }
    else if (clone->action_count != orig->action_count) { ok = 0; FAIL("action_count mismatch"); }

    /* Clone action tables should have same values but different pointers */
    if (ok && clone->yy_action == orig->yy_action) {
        ok = 0;
        FAIL("yy_action not deep-copied");
    }
    if (ok && memcmp(clone->yy_action, orig->yy_action,
                     orig->action_count * sizeof(uint16_t)) != 0) {
        ok = 0;
        FAIL("yy_action values differ");
    }

    /* Releasing original should not affect clone */
    snapshot_release(orig);
    rc = atomic_load(&clone->refcount);
    if (ok && rc != 1) {
        char buf[64];
        snprintf(buf, sizeof(buf), "clone refcount changed after orig release: %lu", (unsigned long)rc);
        ok = 0;
        FAIL(buf);
    }

    snapshot_release(clone);
    if (ok) PASS();
}

/* Test: clone_snapshot(NULL) creates an empty snapshot */
static void test_clone_null(void) {
    TEST("clone_snapshot(NULL) creates empty snapshot");
    ParserSnapshot *clone = clone_snapshot(NULL);
    if (clone == NULL) {
        FAIL("expected non-NULL empty snapshot");
        return;
    }
    /* Empty snapshot should have version 0, refcount 1, and zero counts */
    int ok = 1;
    uint_fast32_t rc = atomic_load(&clone->refcount);
    if (rc != 1) { ok = 0; FAIL("empty snapshot refcount != 1"); }
    else if (clone->version != 0) { ok = 0; FAIL("empty snapshot version != 0"); }
    else if (clone->nsymbol != 0) { ok = 0; FAIL("empty snapshot nsymbol != 0"); }
    else if (clone->nstate != 0) { ok = 0; FAIL("empty snapshot nstate != 0"); }
    snapshot_release(clone);
    if (ok) PASS();
}

/* Test: lime_snapshot_* wrappers match underlying functions */
static void test_public_wrappers(void) {
    TEST("lime_snapshot_acquire/release wrappers");
    ParserSnapshot *snap = make_test_snapshot();
    if (snap == NULL) {
        FAIL("make_test_snapshot returned NULL");
        return;
    }

    /* Use the public wrapper API */
    ParserSnapshot *ref = lime_snapshot_acquire(snap);
    if (ref != snap) {
        FAIL("lime_snapshot_acquire did not return same pointer");
        snapshot_release(snap);
        return;
    }

    uint_fast32_t rc = atomic_load(&snap->refcount);
    if (rc != 2) {
        char buf[64];
        snprintf(buf, sizeof(buf), "expected refcount 2, got %lu", (unsigned long)rc);
        FAIL(buf);
        lime_snapshot_release(snap);
        lime_snapshot_release(snap);
        return;
    }

    lime_snapshot_release(snap);
    lime_snapshot_release(snap);
    PASS();
}

int main(void) {
    printf("Snapshot system unit tests\n");
    printf("==========================\n\n");

    test_acquire_null();
    test_release_null();
    test_create_and_release();
    test_acquire_increments();
    test_multiple_acquire_release();
    test_fields_preserved();
    test_create_base_snapshot_stub();
    test_create_base_snapshot_null_error();
    test_clone_snapshot();
    test_clone_null();
    test_public_wrappers();

    printf("\n==========================\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);
    printf("==========================\n");

    return (tests_passed == tests_run) ? 0 : 1;
}
