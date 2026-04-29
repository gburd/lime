/*
** Comprehensive tests for grammar_context.c
**
** Tests cover:
**   - GrammarContextStack creation and destruction
**   - Mode registration
**   - Context detection by token and lexeme
**   - Exit detection
**   - Explicit push/pop
**   - Bracket tracking with auto-exit
**   - Switch callbacks
**   - Query functions (current mode, snapshot, depth, is_root_only)
**   - Edge cases (max depth, max modes, NULL safety)
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "grammar_context.h"
#include "snapshot.h"

/* ------------------------------------------------------------------ */
/*  Test infrastructure                                                */
/* ------------------------------------------------------------------ */

static int test_count = 0;
static int fail_count = 0;

#define ASSERT(cond, msg) do { \
    test_count++; \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL [%d]: %s (%s:%d)\n", \
                test_count, msg, __FILE__, __LINE__); \
        fail_count++; \
    } \
} while(0)

/* ------------------------------------------------------------------ */
/*  Mock snapshot creation                                             */
/* ------------------------------------------------------------------ */

static ParserSnapshot *create_mock_snapshot(int version) {
    ParserSnapshot *snap = calloc(1, sizeof(ParserSnapshot));
    if (snap == NULL) return NULL;
    atomic_init(&snap->refcount, 1);
    snap->version = version;
    return snap;
}

/* ------------------------------------------------------------------ */
/*  Test: create and destroy stack                                    */
/* ------------------------------------------------------------------ */

static void test_stack_lifecycle(void) {
    printf("test_stack_lifecycle\n");

    ParserSnapshot *root = create_mock_snapshot(1);
    ASSERT(root != NULL, "create root snapshot");

    GrammarContextStack *stack = grammar_context_create(root);
    ASSERT(stack != NULL, "create stack");
    ASSERT(grammar_context_depth(stack) == 0, "initial depth should be 0");
    ASSERT(grammar_context_is_root_only(stack), "should be root only");
    ASSERT(grammar_context_current_mode(stack) == MODE_SQL,
           "current mode should be MODE_SQL");

    ParserSnapshot *current = grammar_context_current_snapshot(stack);
    ASSERT(current == root, "current snapshot should be root");

    grammar_context_destroy(stack);
    snapshot_release(root);

    /* NULL snapshot should fail */
    stack = grammar_context_create(NULL);
    ASSERT(stack == NULL, "create with NULL snapshot should fail");

    /* Destroy NULL should be safe */
    grammar_context_destroy(NULL);
    ASSERT(true, "destroy(NULL) should be safe");
}

/* ------------------------------------------------------------------ */
/*  Test: mode registration                                            */
/* ------------------------------------------------------------------ */

static void test_mode_registration(void) {
    printf("test_mode_registration\n");

    ParserSnapshot *root = create_mock_snapshot(1);
    ParserSnapshot *xquery_snap = create_mock_snapshot(2);

    GrammarContextStack *stack = grammar_context_create(root);

    GrammarModeInfo xquery = {
        .mode = MODE_XQUERY,
        .name = "xquery",
        .trigger_token = 100,
        .trigger_lexeme = "$$",
        .exit_token = 101,
        .snapshot = xquery_snap,
    };

    bool ok = grammar_context_register_mode(stack, &xquery);
    ASSERT(ok, "register xquery mode");

    /* Duplicate registration should fail */
    ok = grammar_context_register_mode(stack, &xquery);
    ASSERT(!ok, "duplicate registration should fail");

    grammar_context_destroy(stack);
    snapshot_release(root);
    snapshot_release(xquery_snap);
}

/* ------------------------------------------------------------------ */
/*  Test: mode registration edge cases                                */
/* ------------------------------------------------------------------ */

static void test_mode_registration_edge_cases(void) {
    printf("test_mode_registration_edge_cases\n");

    ParserSnapshot *root = create_mock_snapshot(1);
    ParserSnapshot *mode_snap = create_mock_snapshot(2);

    GrammarContextStack *stack = grammar_context_create(root);

    /* NULL stack */
    GrammarModeInfo info = {
        .mode = MODE_XQUERY,
        .name = "xquery",
        .snapshot = mode_snap,
    };
    bool ok = grammar_context_register_mode(NULL, &info);
    ASSERT(!ok, "register with NULL stack should fail");

    /* NULL info */
    ok = grammar_context_register_mode(stack, NULL);
    ASSERT(!ok, "register with NULL info should fail");

    /* NULL snapshot in info */
    GrammarModeInfo bad_info = {
        .mode = MODE_XQUERY,
        .name = "xquery",
        .snapshot = NULL,
    };
    ok = grammar_context_register_mode(stack, &bad_info);
    ASSERT(!ok, "register with NULL snapshot should fail");

    grammar_context_destroy(stack);
    snapshot_release(root);
    snapshot_release(mode_snap);
}

/* ------------------------------------------------------------------ */
/*  Test: mode registration limit (max 16 modes)                      */
/* ------------------------------------------------------------------ */

static void test_mode_registration_limit(void) {
    printf("test_mode_registration_limit\n");

    ParserSnapshot *root = create_mock_snapshot(1);
    GrammarContextStack *stack = grammar_context_create(root);

    /* Register 16 modes (the max) */
    ParserSnapshot *snaps[16];
    for (int i = 0; i < 16; i++) {
        snaps[i] = create_mock_snapshot(100 + i);
        GrammarModeInfo info = {
            .mode = (GrammarMode)(MODE_SQL + i + 1),
            .name = "test-mode",
            .snapshot = snaps[i],
        };
        bool ok = grammar_context_register_mode(stack, &info);
        ASSERT(ok, "register mode within limit");
    }

    /* 17th should fail */
    ParserSnapshot *extra = create_mock_snapshot(999);
    GrammarModeInfo info = {
        .mode = MODE_XPATH,
        .name = "xpath",
        .snapshot = extra,
    };
    bool ok = grammar_context_register_mode(stack, &info);
    ASSERT(!ok, "register beyond limit should fail");

    grammar_context_destroy(stack);
    snapshot_release(root);
    for (int i = 0; i < 16; i++) {
        snapshot_release(snaps[i]);
    }
    snapshot_release(extra);
}

/* ------------------------------------------------------------------ */
/*  Test: detect switch by token                                      */
/* ------------------------------------------------------------------ */

static void test_detect_switch_by_token(void) {
    printf("test_detect_switch_by_token\n");

    ParserSnapshot *root = create_mock_snapshot(1);
    ParserSnapshot *xquery2_snap = create_mock_snapshot(2);

    GrammarContextStack *stack = grammar_context_create(root);

    GrammarModeInfo xquery = {
        .mode = MODE_XQUERY,
        .name = "xquery",
        .trigger_token = 42,
        .exit_token = 43,
        .snapshot = xquery2_snap,
    };
    grammar_context_register_mode(stack, &xquery);

    /* Detect switch to xquery */
    bool switched = grammar_context_detect_switch(stack, 42, NULL, 100);
    ASSERT(switched, "should detect switch by token 42");
    ASSERT(grammar_context_current_mode(stack) == MODE_XQUERY,
           "current mode should be PYTHON");
    ASSERT(grammar_context_depth(stack) == 1, "depth should be 1");
    ASSERT(!grammar_context_is_root_only(stack), "should not be root only");

    /* Current snapshot should be xquery2_snap */
    ParserSnapshot *current = grammar_context_current_snapshot(stack);
    ASSERT(current == xquery2_snap, "current snapshot should be xquery");

    /* Non-matching token should not switch */
    switched = grammar_context_detect_switch(stack, 99, NULL, 200);
    ASSERT(!switched, "non-matching token should not switch");

    grammar_context_destroy(stack);
    snapshot_release(root);
    snapshot_release(xquery2_snap);
}

/* ------------------------------------------------------------------ */
/*  Test: detect switch by lexeme                                     */
/* ------------------------------------------------------------------ */

static void test_detect_switch_by_lexeme(void) {
    printf("test_detect_switch_by_lexeme\n");

    ParserSnapshot *root = create_mock_snapshot(1);
    ParserSnapshot *xpath_snap = create_mock_snapshot(2);

    GrammarContextStack *stack = grammar_context_create(root);

    GrammarModeInfo js = {
        .mode = MODE_XPATH,
        .name = "xpath",
        .trigger_lexeme = "function",
        .exit_token = -1,
        .snapshot = xpath_snap,
    };
    grammar_context_register_mode(stack, &js);

    /* Detect switch by lexeme */
    bool switched = grammar_context_detect_switch(stack, -1, "function() {}", 50);
    ASSERT(switched, "should detect switch by lexeme 'function'");
    ASSERT(grammar_context_current_mode(stack) == MODE_XPATH,
           "current mode should be JAVASCRIPT");

    grammar_context_destroy(stack);
    snapshot_release(root);
    snapshot_release(xpath_snap);
}

/* ------------------------------------------------------------------ */
/*  Test: detect exit by token                                        */
/* ------------------------------------------------------------------ */

static void test_detect_exit_by_token(void) {
    printf("test_detect_exit_by_token\n");

    ParserSnapshot *root = create_mock_snapshot(1);
    ParserSnapshot *xquery_snap = create_mock_snapshot(2);

    GrammarContextStack *stack = grammar_context_create(root);

    GrammarModeInfo edn = {
        .mode = MODE_EDN,
        .name = "edn",
        .trigger_token = 100,
        .exit_token = 101,
        .snapshot = xquery_snap,
    };
    grammar_context_register_mode(stack, &edn);

    /* Enter edn */
    grammar_context_detect_switch(stack, 100, NULL, 0);
    ASSERT(grammar_context_current_mode(stack) == MODE_EDN,
           "should be in PLPGSQL mode");

    /* Detect exit */
    bool exited = grammar_context_detect_exit(stack, 101);
    ASSERT(exited, "should detect exit by token 101");
    ASSERT(grammar_context_current_mode(stack) == MODE_SQL,
           "should return to SQL mode");
    ASSERT(grammar_context_depth(stack) == 0, "depth should be 0");

    grammar_context_destroy(stack);
    snapshot_release(root);
    snapshot_release(xquery_snap);
}

/* ------------------------------------------------------------------ */
/*  Test: detect exit from root (should fail)                         */
/* ------------------------------------------------------------------ */

static void test_detect_exit_from_root(void) {
    printf("test_detect_exit_from_root\n");

    ParserSnapshot *root = create_mock_snapshot(1);
    GrammarContextStack *stack = grammar_context_create(root);

    /* Try to exit from root */
    bool exited = grammar_context_detect_exit(stack, 999);
    ASSERT(!exited, "cannot exit from root");
    ASSERT(grammar_context_is_root_only(stack), "should still be root only");

    grammar_context_destroy(stack);
    snapshot_release(root);
}

/* ------------------------------------------------------------------ */
/*  Test: explicit push                                                */
/* ------------------------------------------------------------------ */

static void test_explicit_push(void) {
    printf("test_explicit_push\n");

    ParserSnapshot *root = create_mock_snapshot(1);
    ParserSnapshot *xquery2_snap = create_mock_snapshot(2);

    GrammarContextStack *stack = grammar_context_create(root);

    GrammarModeInfo xquery = {
        .mode = MODE_XQUERY,
        .name = "xquery",
        .snapshot = xquery2_snap,
    };
    grammar_context_register_mode(stack, &xquery);

    /* Explicit push */
    bool ok = grammar_context_push(stack, MODE_XQUERY, 123);
    ASSERT(ok, "explicit push should succeed");
    ASSERT(grammar_context_current_mode(stack) == MODE_XQUERY,
           "current mode should be PYTHON");
    ASSERT(grammar_context_depth(stack) == 1, "depth should be 1");

    grammar_context_destroy(stack);
    snapshot_release(root);
    snapshot_release(xquery2_snap);
}

/* ------------------------------------------------------------------ */
/*  Test: explicit pop                                                 */
/* ------------------------------------------------------------------ */

static void test_explicit_pop(void) {
    printf("test_explicit_pop\n");

    ParserSnapshot *root = create_mock_snapshot(1);
    ParserSnapshot *xpath_snap = create_mock_snapshot(2);

    GrammarContextStack *stack = grammar_context_create(root);

    GrammarModeInfo js = {
        .mode = MODE_XPATH,
        .name = "xpath",
        .snapshot = xpath_snap,
    };
    grammar_context_register_mode(stack, &js);

    grammar_context_push(stack, MODE_XPATH, 0);
    ASSERT(grammar_context_depth(stack) == 1, "depth should be 1 after push");

    /* Explicit pop */
    bool ok = grammar_context_pop(stack);
    ASSERT(ok, "explicit pop should succeed");
    ASSERT(grammar_context_current_mode(stack) == MODE_SQL,
           "should return to SQL mode");
    ASSERT(grammar_context_depth(stack) == 0, "depth should be 0");

    /* Pop from root should fail */
    ok = grammar_context_pop(stack);
    ASSERT(!ok, "pop from root should fail");

    grammar_context_destroy(stack);
    snapshot_release(root);
    snapshot_release(xpath_snap);
}

/* ------------------------------------------------------------------ */
/*  Test: push depth limit (max 32 contexts)                          */
/* ------------------------------------------------------------------ */

static void test_push_depth_limit(void) {
    printf("test_push_depth_limit\n");

    ParserSnapshot *root = create_mock_snapshot(1);
    ParserSnapshot *mode_snap = create_mock_snapshot(2);

    GrammarContextStack *stack = grammar_context_create(root);

    GrammarModeInfo mode = {
        .mode = MODE_XQUERY,
        .name = "xquery",
        .snapshot = mode_snap,
    };
    grammar_context_register_mode(stack, &mode);

    /* Push 31 times (max depth is 32, root is at index 0) */
    for (int i = 0; i < 31; i++) {
        bool ok = grammar_context_push(stack, MODE_XQUERY, (uint32_t)i);
        ASSERT(ok, "push within limit should succeed");
    }

    ASSERT(grammar_context_depth(stack) == 31, "depth should be 31");

    /* 32nd push should fail */
    bool ok = grammar_context_push(stack, MODE_XQUERY, 999);
    ASSERT(!ok, "push beyond limit should fail");
    ASSERT(grammar_context_depth(stack) == 31, "depth should still be 31");

    grammar_context_destroy(stack);
    snapshot_release(root);
    snapshot_release(mode_snap);
}

/* ------------------------------------------------------------------ */
/*  Test: switch callback                                              */
/* ------------------------------------------------------------------ */

static GrammarMode switch_from, switch_to;
static int switch_call_count = 0;

static bool test_switch_callback(GrammarMode from, GrammarMode to, void *user_data) {
    (void)user_data;
    switch_from = from;
    switch_to = to;
    switch_call_count++;
    return true;  /* Allow switch */
}

static void test_switch_callback_basic(void) {
    printf("test_switch_callback_basic\n");

    ParserSnapshot *root = create_mock_snapshot(1);
    ParserSnapshot *xquery2_snap = create_mock_snapshot(2);

    GrammarContextStack *stack = grammar_context_create(root);

    switch_call_count = 0;
    grammar_context_set_switch_callback(stack, test_switch_callback, NULL);

    GrammarModeInfo xquery = {
        .mode = MODE_XQUERY,
        .name = "xquery",
        .snapshot = xquery2_snap,
    };
    grammar_context_register_mode(stack, &xquery);

    /* Push should trigger callback */
    grammar_context_push(stack, MODE_XQUERY, 0);
    ASSERT(switch_call_count == 1, "callback should be called once");
    ASSERT(switch_from == MODE_SQL, "from should be SQL");
    ASSERT(switch_to == MODE_XQUERY, "to should be PYTHON");

    /* Pop should also trigger callback */
    grammar_context_pop(stack);
    ASSERT(switch_call_count == 2, "callback should be called twice");
    ASSERT(switch_from == MODE_XQUERY, "from should be PYTHON");
    ASSERT(switch_to == MODE_SQL, "to should be SQL");

    grammar_context_destroy(stack);
    snapshot_release(root);
    snapshot_release(xquery2_snap);
}

/* ------------------------------------------------------------------ */
/*  Test: switch callback veto                                        */
/* ------------------------------------------------------------------ */

static bool veto_callback(GrammarMode from, GrammarMode to, void *user_data) {
    (void)from; (void)to; (void)user_data;
    return false;  /* Veto the switch */
}

static void test_switch_callback_veto(void) {
    printf("test_switch_callback_veto\n");

    ParserSnapshot *root = create_mock_snapshot(1);
    ParserSnapshot *xpath_snap = create_mock_snapshot(2);

    GrammarContextStack *stack = grammar_context_create(root);

    grammar_context_set_switch_callback(stack, veto_callback, NULL);

    GrammarModeInfo js = {
        .mode = MODE_XPATH,
        .name = "xpath",
        .snapshot = xpath_snap,
    };
    grammar_context_register_mode(stack, &js);

    /* Push should fail due to veto */
    bool ok = grammar_context_push(stack, MODE_XPATH, 0);
    ASSERT(!ok, "push should be vetoed");
    ASSERT(grammar_context_current_mode(stack) == MODE_SQL,
           "should remain in SQL mode");
    ASSERT(grammar_context_depth(stack) == 0, "depth should still be 0");

    grammar_context_destroy(stack);
    snapshot_release(root);
    snapshot_release(xpath_snap);
}

/* ------------------------------------------------------------------ */
/*  Test: bracket tracking                                             */
/* ------------------------------------------------------------------ */

static void test_bracket_tracking(void) {
    printf("test_bracket_tracking\n");

    ParserSnapshot *root = create_mock_snapshot(1);
    GrammarContextStack *stack = grammar_context_create(root);

    grammar_context_open_bracket(stack);
    grammar_context_open_bracket(stack);
    /* Bracket depth is now 2 (internal state, not exposed) */

    bool exited = grammar_context_close_bracket(stack);
    ASSERT(!exited, "closing bracket should not exit yet");

    exited = grammar_context_close_bracket(stack);
    ASSERT(!exited, "closing bracket should not exit yet");

    grammar_context_destroy(stack);
    snapshot_release(root);
}

/* ------------------------------------------------------------------ */
/*  Test: bracket-based auto-exit                                      */
/* ------------------------------------------------------------------ */

static void test_bracket_auto_exit(void) {
    printf("test_bracket_auto_exit\n");

    ParserSnapshot *root = create_mock_snapshot(1);
    ParserSnapshot *xquery2_snap = create_mock_snapshot(2);

    GrammarContextStack *stack = grammar_context_create(root);

    /* Mode with bracket-based exit (exit_token = -1) */
    GrammarModeInfo xquery = {
        .mode = MODE_XQUERY,
        .name = "xquery",
        .trigger_token = 50,
        .exit_token = -1,  /* Bracket-based exit */
        .snapshot = xquery2_snap,
    };
    grammar_context_register_mode(stack, &xquery);

    /* Simulate entering xquery mode via "xmlquery(...)" where the '(' increments depth */
    grammar_context_open_bracket(stack);  /* '(' from trigger */
    grammar_context_push(stack, MODE_XQUERY, 0);
    ASSERT(grammar_context_current_mode(stack) == MODE_XQUERY,
           "should be in XQUERY mode");

    /* When we close the matching ')', bracket depth drops below entry depth, triggering auto-exit */
    bool exited = grammar_context_close_bracket(stack);
    ASSERT(exited, "closing bracket should trigger auto-exit");
    ASSERT(grammar_context_current_mode(stack) == MODE_SQL,
           "should return to SQL mode");

    grammar_context_destroy(stack);
    snapshot_release(root);
    snapshot_release(xquery2_snap);
}

/* ------------------------------------------------------------------ */
/*  Test: NULL safety for bracket functions                           */
/* ------------------------------------------------------------------ */

static void test_bracket_null_safety(void) {
    printf("test_bracket_null_safety\n");

    grammar_context_open_bracket(NULL);
    bool exited = grammar_context_close_bracket(NULL);
    ASSERT(!exited, "close_bracket(NULL) should return false");
    /* Should not crash */
}

/* ------------------------------------------------------------------ */
/*  Test: query functions NULL safety                                 */
/* ------------------------------------------------------------------ */

static void test_query_null_safety(void) {
    printf("test_query_null_safety\n");

    ASSERT(grammar_context_current_snapshot(NULL) == NULL,
           "current_snapshot(NULL) should return NULL");
    ASSERT(grammar_context_current_mode(NULL) == MODE_SQL,
           "current_mode(NULL) should return MODE_SQL");
    ASSERT(grammar_context_depth(NULL) == 0,
           "depth(NULL) should return 0");
    ASSERT(grammar_context_is_root_only(NULL),
           "is_root_only(NULL) should return true");
}

/* ------------------------------------------------------------------ */
/*  Test: nested context switching                                    */
/* ------------------------------------------------------------------ */

static void test_nested_context_switching(void) {
    printf("test_nested_context_switching\n");

    ParserSnapshot *root = create_mock_snapshot(1);
    ParserSnapshot *xquery2_snap = create_mock_snapshot(2);
    ParserSnapshot *xpath_snap = create_mock_snapshot(3);

    GrammarContextStack *stack = grammar_context_create(root);

    GrammarModeInfo xquery = {
        .mode = MODE_XQUERY,
        .name = "xquery",
        .snapshot = xquery2_snap,
    };
    GrammarModeInfo js = {
        .mode = MODE_XPATH,
        .name = "xpath",
        .snapshot = xpath_snap,
    };

    grammar_context_register_mode(stack, &xquery);
    grammar_context_register_mode(stack, &js);

    /* SQL -> Python */
    grammar_context_push(stack, MODE_XQUERY, 0);
    ASSERT(grammar_context_current_mode(stack) == MODE_XQUERY, "mode: PYTHON");
    ASSERT(grammar_context_depth(stack) == 1, "depth: 1");

    /* Python -> JavaScript */
    grammar_context_push(stack, MODE_XPATH, 10);
    ASSERT(grammar_context_current_mode(stack) == MODE_XPATH, "mode: JS");
    ASSERT(grammar_context_depth(stack) == 2, "depth: 2");

    /* JavaScript -> Python */
    grammar_context_pop(stack);
    ASSERT(grammar_context_current_mode(stack) == MODE_XQUERY, "mode: PYTHON");
    ASSERT(grammar_context_depth(stack) == 1, "depth: 1");

    /* Python -> SQL */
    grammar_context_pop(stack);
    ASSERT(grammar_context_current_mode(stack) == MODE_SQL, "mode: SQL");
    ASSERT(grammar_context_depth(stack) == 0, "depth: 0");

    grammar_context_destroy(stack);
    snapshot_release(root);
    snapshot_release(xquery2_snap);
    snapshot_release(xpath_snap);
}

/* ------------------------------------------------------------------ */
/*  Test: push unregistered mode (should fail)                        */
/* ------------------------------------------------------------------ */

static void test_push_unregistered_mode(void) {
    printf("test_push_unregistered_mode\n");

    ParserSnapshot *root = create_mock_snapshot(1);
    GrammarContextStack *stack = grammar_context_create(root);

    /* Try to push a mode that was never registered */
    bool ok = grammar_context_push(stack, MODE_CUSTOM, 0);
    ASSERT(!ok, "push unregistered mode should fail");
    ASSERT(grammar_context_current_mode(stack) == MODE_SQL,
           "should remain in SQL mode");

    grammar_context_destroy(stack);
    snapshot_release(root);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== Grammar Context Tests ===\n");

    test_stack_lifecycle();
    test_mode_registration();
    test_mode_registration_edge_cases();
    test_mode_registration_limit();
    test_detect_switch_by_token();
    test_detect_switch_by_lexeme();
    test_detect_exit_by_token();
    test_detect_exit_from_root();
    test_explicit_push();
    test_explicit_pop();
    test_push_depth_limit();
    test_switch_callback_basic();
    test_switch_callback_veto();
    test_bracket_tracking();
    test_bracket_auto_exit();
    test_bracket_null_safety();
    test_query_null_safety();
    test_nested_context_switching();
    test_push_unregistered_mode();

    printf("\n%d tests, %d failures\n", test_count, fail_count);
    return fail_count > 0 ? 1 : 0;
}
