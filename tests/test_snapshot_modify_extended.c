/*
** Extended snapshot modification tests for better coverage
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "../src/snapshot.h"
#include "../src/snapshot_modify.h"
#include "../src/conflict.h"

#define TEST(name) printf("  %-60s", name); fflush(stdout)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static int tests_passed = 0;
static int tests_failed = 0;

/* Helper to create a minimal snapshot */
static ParserSnapshot *make_test_snapshot(void) {
    ParserSnapshot *snap = calloc(1, sizeof(ParserSnapshot));
    if (!snap) return NULL;

    atomic_init(&snap->refcount, 1);
    snap->version = 1;
    snap->nstate = 5;
    snap->nterminal = 3;

    /* Minimal action tables */
    snap->action_count = 10;
    snap->yy_action = calloc(10, sizeof(uint16_t));
    snap->yy_lookahead = calloc(10, sizeof(uint16_t));
    snap->yy_shift_ofst = calloc(5, sizeof(int16_t));
    snap->yy_reduce_ofst = calloc(5, sizeof(int16_t));
    snap->yy_default = calloc(5, sizeof(uint16_t));

    return snap;
}

/* Test apply_modification with NULL args */
static void test_apply_mod_null_args(void) {
    TEST("apply_modification NULL args");

    ParserSnapshot *snap = make_test_snapshot();
    GrammarModification mod = {.type = MOD_ADD_TOKEN};
    char *error = NULL;

    /* NULL snapshot */
    if (apply_modification(NULL, &mod, &error)) {
        snapshot_release(snap);
        FAIL("should reject NULL snapshot");
        return;
    }

    /* NULL modification */
    if (apply_modification(snap, NULL, &error)) {
        snapshot_release(snap);
        FAIL("should reject NULL modification");
        return;
    }

    snapshot_release(snap);
    PASS();
}

/* Test apply_modification with invalid token */
static void test_apply_add_token_invalid(void) {
    TEST("apply_modification ADD_TOKEN invalid");

    ParserSnapshot *snap = make_test_snapshot();
    GrammarModification mod;
    mod.type = MOD_ADD_TOKEN;
    mod.u.add_token.name = NULL;  /* Invalid: NULL name */
    mod.u.add_token.token_code = 100;

    char *error = NULL;
    if (apply_modification(snap, &mod, &error)) {
        snapshot_release(snap);
        if (error) free(error);
        FAIL("should reject NULL token name");
        return;
    }

    if (error == NULL) {
        snapshot_release(snap);
        FAIL("should set error message");
        return;
    }

    free(error);
    snapshot_release(snap);
    PASS();
}

/* Test apply_modification with invalid rule */
static void test_apply_add_rule_invalid(void) {
    TEST("apply_modification ADD_RULE invalid");

    ParserSnapshot *snap = make_test_snapshot();
    GrammarModification mod;
    mod.type = MOD_ADD_RULE;
    mod.u.add_rule.lhs = NULL;  /* Invalid: NULL LHS */
    mod.u.add_rule.rhs = NULL;
    mod.u.add_rule.nrhs = 0;

    char *error = NULL;
    if (apply_modification(snap, &mod, &error)) {
        snapshot_release(snap);
        if (error) free(error);
        FAIL("should reject NULL LHS");
        return;
    }

    if (error == NULL) {
        snapshot_release(snap);
        FAIL("should set error message");
        return;
    }

    free(error);
    snapshot_release(snap);
    PASS();
}

/* Test apply_modification remove rule invalid */
static void test_apply_remove_rule_invalid(void) {
    TEST("apply_modification REMOVE_RULE invalid");

    ParserSnapshot *snap = make_test_snapshot();
    GrammarModification mod;
    mod.type = MOD_REMOVE_RULE;
    mod.u.add_rule.lhs = NULL;  /* Invalid - reusing add_rule struct */
    mod.u.add_rule.rhs = NULL;
    mod.u.add_rule.nrhs = 0;

    char *error = NULL;
    if (apply_modification(snap, &mod, &error)) {
        snapshot_release(snap);
        if (error) free(error);
        FAIL("should reject NULL LHS");
        return;
    }

    free(error);
    snapshot_release(snap);
    PASS();
}

/* Test apply_modification modify precedence invalid */
static void test_apply_modify_precedence_invalid(void) {
    TEST("apply_modification MODIFY_PRECEDENCE invalid");

    ParserSnapshot *snap = make_test_snapshot();
    GrammarModification mod;
    mod.type = MOD_MODIFY_PRECEDENCE;
    mod.u.modify_prec.symbol = NULL;  /* Invalid */
    mod.u.modify_prec.new_precedence = 5;
    mod.u.modify_prec.new_assoc = 0;

    char *error = NULL;
    if (apply_modification(snap, &mod, &error)) {
        snapshot_release(snap);
        if (error) free(error);
        FAIL("should reject NULL symbol");
        return;
    }

    free(error);
    snapshot_release(snap);
    PASS();
}

/* Test apply_modification add type invalid */
static void test_apply_add_type_invalid(void) {
    TEST("apply_modification ADD_TYPE invalid");

    ParserSnapshot *snap = make_test_snapshot();
    GrammarModification mod;
    mod.type = MOD_ADD_TYPE;
    mod.u.add_type.name = NULL;  /* Invalid */
    mod.u.add_type.datatype = "int";

    char *error = NULL;
    if (apply_modification(snap, &mod, &error)) {
        snapshot_release(snap);
        if (error) free(error);
        FAIL("should reject NULL name");
        return;
    }

    free(error);
    snapshot_release(snap);
    PASS();
}

/* Test rebuild_automaton with NULL */
static void test_rebuild_automaton_null(void) {
    TEST("rebuild_automaton NULL");

    char *error = NULL;
    if (rebuild_automaton(NULL, &error)) {
        FAIL("should reject NULL snapshot");
        return;
    }

    PASS();
}

/* Test create_modified_snapshot with NULL */
static void test_create_modified_null(void) {
    TEST("create_modified_snapshot NULL args");

    ParserSnapshot *snap = make_test_snapshot();
    GrammarModification mod = {.type = MOD_ADD_TOKEN};
    char *error = NULL;
    ParserSnapshot *out = NULL;
    ConflictSet *conflicts = NULL;

    /* NULL out parameter */
    ModifyResult result = create_modified_snapshot(snap, &mod, 1, NULL, NULL, &conflicts, &error);
    if (result != MODIFY_ERR_ALLOC) {
        snapshot_release(snap);
        FAIL("should return MODIFY_ERR_ALLOC for NULL out");
        return;
    }
    free(error); error = NULL;

    /* NULL base */
    result = create_modified_snapshot(NULL, &mod, 1, NULL, &out, &conflicts, &error);
    if (result == MODIFY_OK) {
        if (out) snapshot_release(out);
        snapshot_release(snap);
        FAIL("should fail for NULL base");
        return;
    }
    free(error); error = NULL;

    /* NULL modifications */
    result = create_modified_snapshot(snap, NULL, 1, NULL, &out, &conflicts, &error);
    if (result == MODIFY_OK) {
        if (out) snapshot_release(out);
        snapshot_release(snap);
        FAIL("should fail for NULL mods");
        return;
    }
    free(error); error = NULL;

    /* Zero count - might succeed as a simple clone */
    out = NULL;
    result = create_modified_snapshot(snap, &mod, 0, NULL, &out, &conflicts, &error);
    /* Accept either success (clone) or failure (invalid arg) */
    if (out != NULL) {
        snapshot_release(out);
    }
    free(error); error = NULL;

    snapshot_release(snap);
    PASS();
}

int main(void) {
    printf("\nSnapshot Modification Extended Tests\n");
    printf("====================================\n\n");

    test_apply_mod_null_args();
    test_apply_add_token_invalid();
    test_apply_add_rule_invalid();
    test_apply_remove_rule_invalid();
    test_apply_modify_precedence_invalid();
    test_apply_add_type_invalid();
    test_rebuild_automaton_null();
    test_create_modified_null();

    printf("\n====================================\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_passed + tests_failed);
    printf("====================================\n\n");

    return tests_failed > 0 ? 1 : 0;
}
