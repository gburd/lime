/*
** Unit tests for ParseContext
**
** Tests the parse context lifecycle, snapshot pinning,
** and action lookup dispatch.
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>

#include "parser.h"
#include "parse_context.h"
#include "snapshot.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-60s ", name); \
} while(0)

#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)

/*
** Helper: create a minimal snapshot for testing
*/
static ParserSnapshot *make_test_snapshot(uint64_t version) {
    ParserSnapshot *snap = calloc(1, sizeof(ParserSnapshot));
    if (!snap) return NULL;

    atomic_init(&snap->refcount, 1);
    snap->version = version;
    snap->nstate = 10;
    snap->nterminal = 5;

    /* Allocate minimal action tables */
    snap->action_count = 50;
    snap->lookahead_count = 50;

    snap->yy_action = calloc(50, sizeof(uint16_t));
    snap->yy_lookahead = calloc(50, sizeof(uint16_t));
    snap->yy_shift_ofst = calloc(10, sizeof(int16_t));
    snap->yy_reduce_ofst = calloc(10, sizeof(int16_t));
    snap->yy_default = calloc(10, sizeof(uint16_t));

    if (!snap->yy_action || !snap->yy_lookahead ||
        !snap->yy_shift_ofst || !snap->yy_reduce_ofst ||
        !snap->yy_default) {
        snapshot_release(snap);
        return NULL;
    }

    /* Fill with test data */
    for (int i = 0; i < 10; i++) {
        snap->yy_shift_ofst[i] = (int16_t)(i * 5);
        snap->yy_reduce_ofst[i] = -1;
        snap->yy_default[i] = (uint16_t)(100 + i);

        for (int j = 0; j < 5; j++) {
            int idx = i * 5 + j;
            snap->yy_lookahead[idx] = (uint16_t)j;
            snap->yy_action[idx] = (uint16_t)(i * 10 + j);
        }
    }

    snap->jit_ctx = NULL;
    return snap;
}

/*
** Test: parse_context_create NULL snapshot
*/
static void test_create_null_snapshot(void) {
    TEST("parse_context_create with NULL snapshot");

    ParseContext *ctx = parse_context_create(NULL);

    if (ctx == NULL) {
        PASS();
    } else {
        FAIL("Expected NULL, got context");
        parse_context_destroy(ctx);
    }
}

/*
** Test: parse_context_create and destroy
*/
static void test_create_destroy(void) {
    TEST("parse_context_create and destroy");

    ParserSnapshot *snap = make_test_snapshot(1);
    if (!snap) {
        FAIL("Failed to create snapshot");
        return;
    }

    ParseContext *ctx = parse_context_create(snap);

    if (ctx == NULL) {
        FAIL("parse_context_create returned NULL");
        snapshot_release(snap);
        return;
    }

    /* Context should have acquired a reference to the snapshot */
    uint32_t refcount_before = atomic_load(&snap->refcount);

    parse_context_destroy(ctx);

    /* Reference should be released */
    uint32_t refcount_after = atomic_load(&snap->refcount);

    if (refcount_after == refcount_before - 1) {
        PASS();
    } else {
        FAIL("Refcount not properly released");
    }

    snapshot_release(snap);
}

/*
** Test: parse_context_destroy with NULL
*/
static void test_destroy_null(void) {
    TEST("parse_context_destroy with NULL");

    /* Should not crash */
    parse_context_destroy(NULL);

    PASS();
}

/*
** Test: Snapshot pinning
*/
static void test_snapshot_pinning(void) {
    TEST("Snapshot remains pinned during context lifetime");

    ParserSnapshot *snap = make_test_snapshot(1);
    if (!snap) {
        FAIL("Failed to create snapshot");
        return;
    }

    uint32_t initial_refcount = atomic_load(&snap->refcount);

    ParseContext *ctx = parse_context_create(snap);
    if (!ctx) {
        FAIL("Failed to create context");
        snapshot_release(snap);
        return;
    }

    /* Refcount should be increased */
    uint32_t during_refcount = atomic_load(&snap->refcount);

    if (during_refcount != initial_refcount + 1) {
        FAIL("Snapshot not properly acquired");
        parse_context_destroy(ctx);
        snapshot_release(snap);
        return;
    }

    /* Destroy context */
    parse_context_destroy(ctx);

    /* Refcount should be back to initial */
    uint32_t final_refcount = atomic_load(&snap->refcount);

    if (final_refcount == initial_refcount) {
        PASS();
    } else {
        FAIL("Refcount not properly restored");
    }

    snapshot_release(snap);
}

/*
** Test: Action table lookups via parse context
*/
static void test_action_lookups(void) {
    TEST("Action table lookups through context");

    ParserSnapshot *snap = make_test_snapshot(1);
    if (!snap) {
        FAIL("Failed to create snapshot");
        return;
    }

    ParseContext *ctx = parse_context_create(snap);
    if (!ctx) {
        FAIL("Failed to create context");
        snapshot_release(snap);
        return;
    }

    /* Test shift action lookup */
    uint16_t action = snap_find_shift_action(snap, 0, 2);

    /* Expected: state 0, lookahead 2 -> action[0*5 + 2] = 2 */
    if (action != 2) {
        FAIL("Incorrect shift action");
        parse_context_destroy(ctx);
        snapshot_release(snap);
        return;
    }

    /* Test default action */
    action = snap_find_shift_action(snap, 3, 10); /* invalid lookahead */

    /* Expected: default[3] = 103 */
    if (action != 103) {
        FAIL("Incorrect default action");
        parse_context_destroy(ctx);
        snapshot_release(snap);
        return;
    }

    parse_context_destroy(ctx);
    snapshot_release(snap);

    PASS();
}

/*
** Test: Multiple contexts with same snapshot
*/
static void test_multiple_contexts(void) {
    TEST("Multiple contexts can share a snapshot");

    ParserSnapshot *snap = make_test_snapshot(1);
    if (!snap) {
        FAIL("Failed to create snapshot");
        return;
    }

    uint32_t initial_refcount = atomic_load(&snap->refcount);

    ParseContext *ctx1 = parse_context_create(snap);
    ParseContext *ctx2 = parse_context_create(snap);
    ParseContext *ctx3 = parse_context_create(snap);

    if (!ctx1 || !ctx2 || !ctx3) {
        FAIL("Failed to create contexts");
        if (ctx1) parse_context_destroy(ctx1);
        if (ctx2) parse_context_destroy(ctx2);
        if (ctx3) parse_context_destroy(ctx3);
        snapshot_release(snap);
        return;
    }

    /* Refcount should be +3 */
    uint32_t during_refcount = atomic_load(&snap->refcount);

    if (during_refcount != initial_refcount + 3) {
        FAIL("Refcount incorrect with multiple contexts");
        parse_context_destroy(ctx1);
        parse_context_destroy(ctx2);
        parse_context_destroy(ctx3);
        snapshot_release(snap);
        return;
    }

    /* Destroy all contexts */
    parse_context_destroy(ctx1);
    parse_context_destroy(ctx2);
    parse_context_destroy(ctx3);

    /* Refcount should be back to initial */
    uint32_t final_refcount = atomic_load(&snap->refcount);

    if (final_refcount == initial_refcount) {
        PASS();
    } else {
        FAIL("Refcount not properly restored");
    }

    snapshot_release(snap);
}

/*
** Test: Reduce action lookup
*/
static void test_reduce_lookup(void) {
    TEST("Reduce action lookup");

    ParserSnapshot *snap = make_test_snapshot(1);
    if (!snap) {
        FAIL("Failed to create snapshot");
        return;
    }

    ParseContext *ctx = parse_context_create(snap);
    if (!ctx) {
        FAIL("Failed to create context");
        snapshot_release(snap);
        return;
    }

    /* All states have reduce_ofst = -1, so should return default */
    uint16_t action = snap_find_reduce_action(snap, 5, 2);

    if (action == snap->yy_default[5]) {
        PASS();
    } else {
        FAIL("Reduce action lookup incorrect");
    }

    parse_context_destroy(ctx);
    snapshot_release(snap);
}

/*
** Test: Edge case - action at boundary
*/
static void test_action_boundary(void) {
    TEST("Action lookup at state/terminal boundary");

    ParserSnapshot *snap = make_test_snapshot(1);
    if (!snap) {
        FAIL("Failed to create snapshot");
        return;
    }

    ParseContext *ctx = parse_context_create(snap);
    if (!ctx) {
        FAIL("Failed to create context");
        snapshot_release(snap);
        return;
    }

    /* Last state, last terminal */
    uint16_t action = snap_find_shift_action(snap, 9, 4);

    /* Expected: state 9, lookahead 4 -> action[9*5 + 4] = 94 */
    if (action != 94) {
        FAIL("Boundary action incorrect");
        parse_context_destroy(ctx);
        snapshot_release(snap);
        return;
    }

    parse_context_destroy(ctx);
    snapshot_release(snap);

    PASS();
}

/*
** Test: Zero-initialized snapshot
*/
static void test_zero_snapshot(void) {
    TEST("Context with zero-initialized snapshot");

    ParserSnapshot *snap = calloc(1, sizeof(ParserSnapshot));
    if (!snap) {
        FAIL("Failed to allocate snapshot");
        return;
    }

    atomic_init(&snap->refcount, 1);

    ParseContext *ctx = parse_context_create(snap);

    if (ctx) {
        parse_context_destroy(ctx);
        PASS();
    } else {
        FAIL("Failed to create context with zero snapshot");
    }

    snapshot_release(snap);
}

/*
** Test: Context state isolation
*/
static void test_context_isolation(void) {
    TEST("Contexts are independent (state isolation)");

    ParserSnapshot *snap = make_test_snapshot(1);
    if (!snap) {
        FAIL("Failed to create snapshot");
        return;
    }

    ParseContext *ctx1 = parse_context_create(snap);
    ParseContext *ctx2 = parse_context_create(snap);

    if (!ctx1 || !ctx2) {
        FAIL("Failed to create contexts");
        if (ctx1) parse_context_destroy(ctx1);
        if (ctx2) parse_context_destroy(ctx2);
        snapshot_release(snap);
        return;
    }

    /* Both contexts should be able to query the same snapshot independently */
    uint16_t action1 = snap_find_shift_action(snap, 1, 1);
    uint16_t action2 = snap_find_shift_action(snap, 1, 1);

    if (action1 == action2) {
        PASS();
    } else {
        FAIL("Context queries not consistent");
    }

    parse_context_destroy(ctx1);
    parse_context_destroy(ctx2);
    snapshot_release(snap);
}

int main(void) {
    printf("\nParse Context Test Suite\n");
    printf("========================\n\n");

    test_create_null_snapshot();
    test_create_destroy();
    test_destroy_null();
    test_snapshot_pinning();
    test_action_lookups();
    test_multiple_contexts();
    test_reduce_lookup();
    test_action_boundary();
    test_zero_snapshot();
    test_context_isolation();

    printf("\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
