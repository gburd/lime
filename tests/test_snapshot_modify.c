/*
** Unit tests for snapshot modification
**
** Tests grammar modification operations: applying modifications,
** cloning snapshots, and handling modifications correctly.
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
    printf("  %-60s ", name); \
} while(0)

#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)

/*
** Helper: create a simple base snapshot
*/
static ParserSnapshot *make_base_snapshot(void) {
    ParserSnapshot *snap = calloc(1, sizeof(ParserSnapshot));
    if (!snap) return NULL;

    atomic_init(&snap->refcount, 1);
    snap->version = 1;
    snap->nstate = 10;
    snap->nterminal = 5;
    snap->nsymbol = 10;
    snap->nrule = 8;

    snap->action_count = 50;
    snap->lookahead_count = 50;

    snap->yy_action = calloc(50, sizeof(uint16_t));
    snap->yy_lookahead = calloc(50, sizeof(uint16_t));
    snap->yy_shift_ofst = calloc(10, sizeof(int32_t));
    snap->yy_reduce_ofst = calloc(10, sizeof(int32_t));
    snap->yy_default = calloc(10, sizeof(uint16_t));

    if (!snap->yy_action || !snap->yy_lookahead ||
        !snap->yy_shift_ofst || !snap->yy_reduce_ofst ||
        !snap->yy_default) {
        snapshot_release(snap);
        return NULL;
    }

    /* Fill with test data */
    for (int i = 0; i < 10; i++) {
        snap->yy_shift_ofst[i] = (int32_t)(i * 5);
        snap->yy_default[i] = (uint16_t)(100 + i);
    }

    for (int i = 0; i < 50; i++) {
        snap->yy_lookahead[i] = (uint16_t)(i % 5);
        snap->yy_action[i] = (uint16_t)(i * 2);
    }

    return snap;
}

/*
** Test: clone_snapshot creates independent copy
*/
static void test_clone_independence(void) {
    TEST("clone_snapshot creates independent copy");

    ParserSnapshot *base = make_base_snapshot();
    if (!base) {
        FAIL("Failed to create base snapshot");
        return;
    }

    ParserSnapshot *clone = clone_snapshot(base);
    if (!clone) {
        FAIL("clone_snapshot returned NULL");
        snapshot_release(base);
        return;
    }

    /* Version should be incremented */
    if (clone->version != base->version + 1) {
        FAIL("Clone version not incremented");
        snapshot_release(base);
        snapshot_release(clone);
        return;
    }

    /* Dimensions should match */
    if (clone->nstate != base->nstate ||
        clone->nterminal != base->nterminal ||
        clone->nsymbol != base->nsymbol) {
        FAIL("Clone dimensions don't match");
        snapshot_release(base);
        snapshot_release(clone);
        return;
    }

    /* Modify clone - should not affect base */
    clone->yy_action[0] = 9999;

    if (base->yy_action[0] == 9999) {
        FAIL("Clone not independent (shared memory)");
        snapshot_release(base);
        snapshot_release(clone);
        return;
    }

    snapshot_release(base);
    snapshot_release(clone);

    PASS();
}

/*
** Test: clone_snapshot with NULL
*/
static void test_clone_null(void) {
    TEST("clone_snapshot with NULL input");

    ParserSnapshot *clone = clone_snapshot(NULL);

    /* Should return an empty snapshot, not NULL */
    if (clone == NULL) {
        FAIL("Expected empty snapshot, got NULL");
        return;
    }

    if (clone->version != 0) {
        FAIL("Empty snapshot should have version 0");
        snapshot_release(clone);
        return;
    }

    snapshot_release(clone);
    PASS();
}

/*
** Test: clone_snapshot preserves action table data
*/
static void test_clone_preserves_data(void) {
    TEST("clone_snapshot preserves action table data");

    ParserSnapshot *base = make_base_snapshot();
    if (!base) {
        FAIL("Failed to create base snapshot");
        return;
    }

    ParserSnapshot *clone = clone_snapshot(base);
    if (!clone) {
        FAIL("clone_snapshot returned NULL");
        snapshot_release(base);
        return;
    }

    /* Verify all action table entries match */
    bool mismatch = false;

    for (uint32_t i = 0; i < base->action_count; i++) {
        if (clone->yy_action[i] != base->yy_action[i]) {
            mismatch = true;
            break;
        }
    }

    if (mismatch) {
        FAIL("Action table data not preserved");
        snapshot_release(base);
        snapshot_release(clone);
        return;
    }

    /* Verify lookahead table */
    for (uint32_t i = 0; i < base->lookahead_count; i++) {
        if (clone->yy_lookahead[i] != base->yy_lookahead[i]) {
            mismatch = true;
            break;
        }
    }

    if (mismatch) {
        FAIL("Lookahead table data not preserved");
        snapshot_release(base);
        snapshot_release(clone);
        return;
    }

    snapshot_release(base);
    snapshot_release(clone);

    PASS();
}

/*
** Test: clone_snapshot with partial data
*/
static void test_clone_partial_data(void) {
    TEST("clone_snapshot with partially initialized snapshot");

    ParserSnapshot *base = calloc(1, sizeof(ParserSnapshot));
    if (!base) {
        FAIL("Failed to allocate snapshot");
        return;
    }

    atomic_init(&base->refcount, 1);
    base->version = 5;
    base->nstate = 3;
    base->nterminal = 2;

    /* Only allocate yy_action, leave others NULL */
    base->action_count = 6;
    base->yy_action = calloc(6, sizeof(uint16_t));

    ParserSnapshot *clone = clone_snapshot(base);

    if (!clone) {
        FAIL("clone_snapshot failed with partial data");
        snapshot_release(base);
        return;
    }

    /* Version should be incremented */
    if (clone->version != 6) {
        FAIL("Version not incremented");
        snapshot_release(base);
        snapshot_release(clone);
        return;
    }

    snapshot_release(base);
    snapshot_release(clone);

    PASS();
}

/*
** Test: Multiple sequential clones
*/
static void test_sequential_clones(void) {
    TEST("Multiple sequential clones increment version");

    ParserSnapshot *v1 = make_base_snapshot();
    if (!v1) {
        FAIL("Failed to create base");
        return;
    }

    ParserSnapshot *v2 = clone_snapshot(v1);
    ParserSnapshot *v3 = clone_snapshot(v2);
    ParserSnapshot *v4 = clone_snapshot(v3);

    if (!v2 || !v3 || !v4) {
        FAIL("Clone failed");
        if (v1) snapshot_release(v1);
        if (v2) snapshot_release(v2);
        if (v3) snapshot_release(v3);
        if (v4) snapshot_release(v4);
        return;
    }

    if (v1->version + 1 == v2->version &&
        v2->version + 1 == v3->version &&
        v3->version + 1 == v4->version) {
        PASS();
    } else {
        FAIL("Version sequence incorrect");
    }

    snapshot_release(v1);
    snapshot_release(v2);
    snapshot_release(v3);
    snapshot_release(v4);
}

/*
** Test: Clone with large action tables
*/
static void test_clone_large_tables(void) {
    TEST("clone_snapshot with large action tables");

    ParserSnapshot *base = calloc(1, sizeof(ParserSnapshot));
    if (!base) {
        FAIL("Failed to allocate");
        return;
    }

    atomic_init(&base->refcount, 1);
    base->version = 1;
    base->nstate = 500;
    base->nterminal = 150;

    uint32_t table_size = 500 * 150;
    base->action_count = table_size;
    base->lookahead_count = table_size;

    base->yy_action = calloc(table_size, sizeof(uint16_t));
    base->yy_lookahead = calloc(table_size, sizeof(uint16_t));
    base->yy_shift_ofst = calloc(500, sizeof(int32_t));
    base->yy_reduce_ofst = calloc(500, sizeof(int32_t));
    base->yy_default = calloc(500, sizeof(uint16_t));

    if (!base->yy_action || !base->yy_lookahead ||
        !base->yy_shift_ofst || !base->yy_reduce_ofst ||
        !base->yy_default) {
        snapshot_release(base);
        FAIL("Failed to allocate large tables");
        return;
    }

    /* Fill with pattern */
    for (uint32_t i = 0; i < table_size; i++) {
        base->yy_action[i] = (uint16_t)(i % 65536);
        base->yy_lookahead[i] = (uint16_t)(i % 150);
    }

    ParserSnapshot *clone = clone_snapshot(base);

    if (!clone) {
        FAIL("Failed to clone large snapshot");
        snapshot_release(base);
        return;
    }

    /* Verify a sample of entries */
    bool correct = true;
    for (uint32_t i = 0; i < table_size; i += 1000) {
        if (clone->yy_action[i] != base->yy_action[i]) {
            correct = false;
            break;
        }
    }

    if (correct) {
        PASS();
    } else {
        FAIL("Large table data not preserved");
    }

    snapshot_release(base);
    snapshot_release(clone);
}

/*
** Test: Clone and modify multiple times
*/
static void test_clone_modify_chain(void) {
    TEST("Clone, modify, clone chain preserves independence");

    ParserSnapshot *v1 = make_base_snapshot();
    if (!v1) {
        FAIL("Failed to create base");
        return;
    }

    uint16_t original_value = v1->yy_action[10];

    ParserSnapshot *v2 = clone_snapshot(v1);
    if (!v2) {
        FAIL("First clone failed");
        snapshot_release(v1);
        return;
    }

    /* Modify v2 */
    v2->yy_action[10] = original_value + 100;

    ParserSnapshot *v3 = clone_snapshot(v2);
    if (!v3) {
        FAIL("Second clone failed");
        snapshot_release(v1);
        snapshot_release(v2);
        return;
    }

    /* Verify values */
    if (v1->yy_action[10] != original_value ||
        v2->yy_action[10] != original_value + 100 ||
        v3->yy_action[10] != original_value + 100) {
        FAIL("Clone chain independence broken");
        snapshot_release(v1);
        snapshot_release(v2);
        snapshot_release(v3);
        return;
    }

    snapshot_release(v1);
    snapshot_release(v2);
    snapshot_release(v3);

    PASS();
}

/*
** Test: Clone preserves refcount independence
*/
static void test_clone_refcount_independence(void) {
    TEST("Clone has independent refcount");

    ParserSnapshot *base = make_base_snapshot();
    if (!base) {
        FAIL("Failed to create base");
        return;
    }

    uint32_t base_refcount_before = atomic_load(&base->refcount);

    ParserSnapshot *clone = clone_snapshot(base);
    if (!clone) {
        FAIL("Clone failed");
        snapshot_release(base);
        return;
    }

    /* Clone should have refcount = 1 */
    uint32_t clone_refcount = atomic_load(&clone->refcount);

    /* Base refcount should be unchanged */
    uint32_t base_refcount_after = atomic_load(&base->refcount);

    if (clone_refcount != 1) {
        FAIL("Clone refcount incorrect");
        snapshot_release(base);
        snapshot_release(clone);
        return;
    }

    if (base_refcount_after != base_refcount_before) {
        FAIL("Base refcount changed");
        snapshot_release(base);
        snapshot_release(clone);
        return;
    }

    snapshot_release(base);
    snapshot_release(clone);

    PASS();
}

int main(void) {
    printf("\nSnapshot Modification Test Suite\n");
    printf("=================================\n\n");

    test_clone_independence();
    test_clone_null();
    test_clone_preserves_data();
    test_clone_partial_data();
    test_sequential_clones();
    test_clone_large_tables();
    test_clone_modify_chain();
    test_clone_refcount_independence();

    printf("\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
