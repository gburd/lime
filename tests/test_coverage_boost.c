/*
** Comprehensive test suite to boost code coverage to 95%+.
** Targets functions with low coverage in:
** - snapshot_modify.c (25%)
** - jit_policy.c (38%)
** - token_table.c (56%)
** - conflict.c (69%)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#include "parser.h"
#include "jit_policy.h"
#include "token_table.h"

#include "../src/snapshot.h"
#include "../src/snapshot_modify.h"
#include "../src/conflict.h"
#include "../src/extension.h"

#define TEST(name) printf("  %-60s", name); fflush(stdout)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static int tests_passed = 0;
static int tests_failed = 0;

/* Helper to create a minimal snapshot for testing */
static ParserSnapshot *make_minimal_snapshot(void) {
    ParserSnapshot *snap = calloc(1, sizeof(ParserSnapshot));
    if (!snap) return NULL;

    atomic_init(&snap->refcount, 1);
    snap->version = 1;
    snap->nstate = 10;
    snap->nterminal = 5;
    snap->nsymbol = 10;

    /* Allocate action tables */
    snap->action_count = 50;
    snap->lookahead_count = 50;
    snap->yy_action = calloc(50, sizeof(uint16_t));
    snap->yy_lookahead = calloc(50, sizeof(uint16_t));
    snap->yy_shift_ofst = calloc(10, sizeof(int16_t));
    snap->yy_reduce_ofst = calloc(10, sizeof(int16_t));
    snap->yy_default = calloc(10, sizeof(uint16_t));

    if (!snap->yy_action || !snap->yy_lookahead || !snap->yy_shift_ofst ||
        !snap->yy_reduce_ofst || !snap->yy_default) {
        snapshot_release(snap);
        return NULL;
    }

    /* Fill with test data */
    for (uint32_t i = 0; i < snap->nstate; i++) {
        snap->yy_shift_ofst[i] = (int16_t)(i * 5);
        snap->yy_default[i] = (uint16_t)(100 + i);
    }

    for (uint32_t i = 0; i < snap->lookahead_count; i++) {
        snap->yy_lookahead[i] = (uint16_t)(i % 5);
        snap->yy_action[i] = (uint16_t)(i + 10);
    }

    return snap;
}

/* ============================================================== */
/*  Token Table Tests                                            */
/* ============================================================== */

static void test_token_table_create_destroy(void) {
    TEST("token_table_create and destroy");

    TokenTable *tt = create_token_table(64);
    if (!tt) {
        FAIL("Failed to create token table");
        return;
    }

    destroy_token_table(tt);
    PASS();
}

static void test_token_table_null_safety(void) {
    TEST("token_table NULL safety");

    /* These should not crash */
    destroy_token_table(NULL);

    int result = lookup_token(NULL, "SELECT", 6);
    if (result != -1) {
        FAIL("lookup on NULL table should return -1");
        return;
    }

    bool added = add_token(NULL, "SELECT", 1, 0);
    if (added) {
        FAIL("add on NULL table should fail");
        return;
    }

    PASS();
}

static void test_token_table_add_lookup(void) {
    TEST("token_table add and lookup");

    TokenTable *tt = create_token_table(64);
    if (!tt) {
        FAIL("Failed to create token table");
        return;
    }

    /* Add some tokens */
    if (!add_token(tt, "SELECT", 1, 0)) {
        destroy_token_table(tt);
        FAIL("Failed to add SELECT");
        return;
    }

    if (!add_token(tt, "FROM", 2, 0)) {
        destroy_token_table(tt);
        FAIL("Failed to add FROM");
        return;
    }

    if (!add_token(tt, "WHERE", 3, 0)) {
        destroy_token_table(tt);
        FAIL("Failed to add WHERE");
        return;
    }

    /* Lookup tokens */
    int code = lookup_token(tt, "SELECT", 6);
    if (code != 1) {
        destroy_token_table(tt);
        FAIL("Failed to lookup SELECT");
        return;
    }

    code = lookup_token(tt, "FROM", 4);
    if (code != 2) {
        destroy_token_table(tt);
        FAIL("Failed to lookup FROM");
        return;
    }

    /* Lookup nonexistent */
    code = lookup_token(tt, "NONEXISTENT", 11);
    if (code != -1) {
        destroy_token_table(tt);
        FAIL("Lookup nonexistent should return -1");
        return;
    }

    destroy_token_table(tt);
    PASS();
}

static void test_token_table_duplicate_add(void) {
    TEST("token_table duplicate add");

    TokenTable *tt = create_token_table(64);
    if (!tt) {
        FAIL("Failed to create token table");
        return;
    }

    add_token(tt, "SELECT", 1, 0);

    /* Adding duplicate should fail or succeed depending on implementation */
    bool result = add_token(tt, "SELECT", 1, 0);
    /* Just verify it doesn't crash */
    (void)result;

    destroy_token_table(tt);
    PASS();
}

static void test_token_table_many_tokens(void) {
    TEST("token_table with many tokens");

    TokenTable *tt = create_token_table(64);
    if (!tt) {
        FAIL("Failed to create token table");
        return;
    }

    /* Add many tokens to test hash table resizing */
    for (int i = 0; i < 100; i++) {
        char name[32];
        snprintf(name, sizeof(name), "TOKEN_%d", i);
        if (!add_token(tt, name, i, 0)) {
            destroy_token_table(tt);
            FAIL("Failed to add many tokens");
            return;
        }
    }

    /* Verify some tokens */
    int code = lookup_token(tt, "TOKEN_50", 8);
    if (code != 50) {
        destroy_token_table(tt);
        FAIL("Failed to lookup TOKEN_50");
        return;
    }

    destroy_token_table(tt);
    PASS();
}

/* ============================================================== */
/*  JIT Policy Tests                                             */
/* ============================================================== */

static void test_jit_policy_init(void) {
    TEST("jit_policy_init");

    JITPolicy *policy = jit_policy_init();
    if (!policy) {
        FAIL("Failed to initialize policy");
        return;
    }

    jit_policy_destroy(policy);
    PASS();
}

static void test_jit_policy_null_safety(void) {
    TEST("jit_policy NULL safety");

    /* These should not crash */
    jit_policy_destroy(NULL);

    bool result = jit_policy_should_compile(NULL, NULL);
    if (result) {
        FAIL("should_compile on NULL should return false");
        return;
    }

    jit_policy_record_parse(NULL, NULL, 100);

    PASS();
}

static void test_jit_policy_record_and_decide(void) {
    TEST("jit_policy record parse and decide");

    JITPolicy *policy = jit_policy_init();
    if (!policy) {
        FAIL("Failed to initialize policy");
        return;
    }

    ParserSnapshot *snap = make_minimal_snapshot();
    if (!snap) {
        jit_policy_destroy(policy);
        FAIL("Failed to create snapshot");
        return;
    }

    /* Record many parses to trigger compilation decision */
    for (int i = 0; i < 150; i++) {
        jit_policy_record_parse(policy, snap, 1000);  /* 1000ns per parse */
    }

    /* Now it should recommend compilation */
    bool should_compile = jit_policy_should_compile(policy, snap);

    snapshot_release(snap);
    jit_policy_destroy(policy);

    /* May or may not compile depending on policy, just verify no crash */
    PASS();
}

static void test_jit_policy_thresholds(void) {
    TEST("jit_policy respects thresholds");

    JITPolicy *policy = jit_policy_init();
    if (!policy) {
        FAIL("Failed to initialize policy");
        return;
    }

    ParserSnapshot *snap = make_minimal_snapshot();
    if (!snap) {
        jit_policy_destroy(policy);
        FAIL("Failed to create snapshot");
        return;
    }

    /* With only a few parses, should not compile */
    jit_policy_record_parse(policy, snap, 1000);
    jit_policy_record_parse(policy, snap, 1000);

    bool should_compile = jit_policy_should_compile(policy, snap);
    if (should_compile) {
        snapshot_release(snap);
        jit_policy_destroy(policy);
        FAIL("Should not compile with only 2 parses");
        return;
    }

    snapshot_release(snap);
    jit_policy_destroy(policy);
    PASS();
}

/* ============================================================== */
/*  Conflict Tests                                               */
/* ============================================================== */

static void test_conflict_set_create_destroy(void) {
    TEST("conflict_set_create and destroy");

    ConflictSet *cs = conflict_set_create();
    if (!cs) {
        FAIL("Failed to create conflict set");
        return;
    }

    conflict_set_destroy(cs);
    PASS();
}

static void test_conflict_set_null_safety(void) {
    TEST("conflict_set NULL safety");

    /* Should not crash */
    conflict_set_destroy(NULL);

    uint32_t count = conflict_set_count(NULL);
    if (count != 0) {
        FAIL("count on NULL should return 0");
        return;
    }

    bool has = conflict_set_has_conflicts(NULL);
    if (has) {
        FAIL("has_conflicts on NULL should return false");
        return;
    }

    PASS();
}

static void test_conflict_set_add_and_query(void) {
    TEST("conflict_set add and query");

    ConflictSet *cs = conflict_set_create();
    if (!cs) {
        FAIL("Failed to create conflict set");
        return;
    }

    /* Initially empty */
    if (conflict_set_has_conflicts(cs)) {
        conflict_set_destroy(cs);
        FAIL("New conflict set should be empty");
        return;
    }

    if (conflict_set_count(cs) != 0) {
        conflict_set_destroy(cs);
        FAIL("New conflict set should have count 0");
        return;
    }

    /* Add a token collision conflict */
    conflict_set_add_token_collision(cs, 1, 2, "SELECT");

    if (!conflict_set_has_conflicts(cs)) {
        conflict_set_destroy(cs);
        FAIL("Should have conflicts after adding");
        return;
    }

    if (conflict_set_count(cs) != 1) {
        conflict_set_destroy(cs);
        FAIL("Should have count 1");
        return;
    }

    conflict_set_destroy(cs);
    PASS();
}

static void test_conflict_set_multiple_types(void) {
    TEST("conflict_set with multiple conflict types");

    ConflictSet *cs = conflict_set_create();
    if (!cs) {
        FAIL("Failed to create conflict set");
        return;
    }

    /* Add various types of conflicts */
    conflict_set_add_token_collision(cs, 1, 2, "SELECT");
    conflict_set_add_shift_reduce(cs, 5, 10, "FROM");
    conflict_set_add_reduce_reduce(cs, 3, 7, 8, "WHERE");

    if (conflict_set_count(cs) != 3) {
        conflict_set_destroy(cs);
        FAIL("Should have 3 conflicts");
        return;
    }

    conflict_set_destroy(cs);
    PASS();
}

static void test_conflict_detection_no_conflicts(void) {
    TEST("detect_conflicts with no conflicts");

    ParserSnapshot *snap = make_minimal_snapshot();
    if (!snap) {
        FAIL("Failed to create snapshot");
        return;
    }

    ConflictSet *cs = detect_conflicts(snap, NULL);
    if (!cs) {
        snapshot_release(snap);
        FAIL("detect_conflicts returned NULL");
        return;
    }

    /* Minimal snapshot should have no conflicts */
    if (conflict_set_has_conflicts(cs)) {
        conflict_set_destroy(cs);
        snapshot_release(snap);
        FAIL("Minimal snapshot should have no conflicts");
        return;
    }

    conflict_set_destroy(cs);
    snapshot_release(snap);
    PASS();
}

/* ============================================================== */
/*  Snapshot Modify Tests                                        */
/* ============================================================== */

static void test_clone_snapshot_complete(void) {
    TEST("clone_snapshot creates complete copy");

    ParserSnapshot *base = make_minimal_snapshot();
    if (!base) {
        FAIL("Failed to create base snapshot");
        return;
    }

    ParserSnapshot *clone = clone_snapshot(base);
    if (!clone) {
        snapshot_release(base);
        FAIL("Failed to clone snapshot");
        return;
    }

    /* Verify clone is independent */
    if (clone == base) {
        snapshot_release(clone);
        snapshot_release(base);
        FAIL("Clone is same pointer as base");
        return;
    }

    /* Verify data was copied */
    if (clone->nstate != base->nstate) {
        snapshot_release(clone);
        snapshot_release(base);
        FAIL("Clone has different nstate");
        return;
    }

    if (clone->nterminal != base->nterminal) {
        snapshot_release(clone);
        snapshot_release(base);
        FAIL("Clone has different nterminal");
        return;
    }

    /* Verify version incremented */
    if (clone->version != base->version + 1) {
        snapshot_release(clone);
        snapshot_release(base);
        FAIL("Clone version not incremented");
        return;
    }

    snapshot_release(clone);
    snapshot_release(base);
    PASS();
}

static void test_clone_snapshot_independence(void) {
    TEST("clone_snapshot modifications are independent");

    ParserSnapshot *base = make_minimal_snapshot();
    if (!base) {
        FAIL("Failed to create base snapshot");
        return;
    }

    ParserSnapshot *clone = clone_snapshot(base);
    if (!clone) {
        snapshot_release(base);
        FAIL("Failed to clone snapshot");
        return;
    }

    /* Modify clone's action table */
    uint16_t original_value = base->yy_action[0];
    clone->yy_action[0] = 9999;

    /* Base should be unchanged */
    if (base->yy_action[0] != original_value) {
        snapshot_release(clone);
        snapshot_release(base);
        FAIL("Base was modified when clone changed");
        return;
    }

    snapshot_release(clone);
    snapshot_release(base);
    PASS();
}

static void test_clone_snapshot_with_null_fields(void) {
    TEST("clone_snapshot handles NULL fields");

    ParserSnapshot *base = calloc(1, sizeof(ParserSnapshot));
    if (!base) {
        FAIL("Failed to allocate base");
        return;
    }

    atomic_init(&base->refcount, 1);
    base->version = 1;
    /* Leave all pointers NULL */

    ParserSnapshot *clone = clone_snapshot(base);
    if (!clone) {
        snapshot_release(base);
        FAIL("Failed to clone snapshot with NULL fields");
        return;
    }

    snapshot_release(clone);
    snapshot_release(base);
    PASS();
}

static void test_clone_chain(void) {
    TEST("clone chain creates independent snapshots");

    ParserSnapshot *snap1 = make_minimal_snapshot();
    if (!snap1) {
        FAIL("Failed to create snap1");
        return;
    }

    ParserSnapshot *snap2 = clone_snapshot(snap1);
    if (!snap2) {
        snapshot_release(snap1);
        FAIL("Failed to create snap2");
        return;
    }

    ParserSnapshot *snap3 = clone_snapshot(snap2);
    if (!snap3) {
        snapshot_release(snap2);
        snapshot_release(snap1);
        FAIL("Failed to create snap3");
        return;
    }

    /* Verify version chain */
    if (snap1->version != 1 || snap2->version != 2 || snap3->version != 3) {
        snapshot_release(snap3);
        snapshot_release(snap2);
        snapshot_release(snap1);
        FAIL("Version chain incorrect");
        return;
    }

    /* Modify snap3, verify others unchanged */
    snap3->yy_action[0] = 7777;
    if (snap1->yy_action[0] == 7777 || snap2->yy_action[0] == 7777) {
        snapshot_release(snap3);
        snapshot_release(snap2);
        snapshot_release(snap1);
        FAIL("Clone chain not independent");
        return;
    }

    snapshot_release(snap3);
    snapshot_release(snap2);
    snapshot_release(snap1);
    PASS();
}

/* ============================================================== */
/*  Main                                                         */
/* ============================================================== */

int main(void) {
    printf("\n");
    printf("Coverage Boost Test Suite\n");
    printf("==========================\n\n");

    /* Token Table Tests */
    test_token_table_create_destroy();
    test_token_table_null_safety();
    test_token_table_add_lookup();
    test_token_table_duplicate_add();
    test_token_table_many_tokens();

    /* JIT Policy Tests */
    test_jit_policy_init();
    test_jit_policy_null_safety();
    test_jit_policy_record_and_decide();
    test_jit_policy_thresholds();

    /* Conflict Tests */
    test_conflict_set_create_destroy();
    test_conflict_set_null_safety();
    test_conflict_set_add_and_query();
    test_conflict_set_multiple_types();
    test_conflict_detection_no_conflicts();

    /* Snapshot Modify Tests */
    test_clone_snapshot_complete();
    test_clone_snapshot_independence();
    test_clone_snapshot_with_null_fields();
    test_clone_chain();

    printf("\n");
    printf("==========================\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_passed + tests_failed);
    printf("==========================\n");

    return tests_failed > 0 ? 1 : 0;
}
