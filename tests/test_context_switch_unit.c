/*
** test_context_switch_unit.c -- unit tests for the runtime-registered
** context-switch trigger registry (src/context_switch.c).
**
** Covers:
**   - context_switch_register_trigger: success, duplicate rejection,
**     NULL-input rejection, registry-full rejection
**   - context_switch_classify_lexeme: matching & non-matching lexemes,
**     NULL safety, prefix matching, empty-stack behavior
**   - context_switch_needed: fast path (zero triggers), root + lexeme
**     match / mismatch, embedded-mode always-true
**   - context_switch_detect_exit: NULL safety, root short-circuit,
**     explicit-exit-token path
**
** Snapshots are mock objects -- the tests never drive a real parse.
** They exercise the trigger-registry plumbing only.
*/
#include "grammar_context.h"
#include "snapshot.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Test-runner helpers                                                */
/* ------------------------------------------------------------------ */

static int g_total = 0;
static int g_fail = 0;

#define ASSERT(cond, msg)                                                                          \
    do {                                                                                           \
        g_total++;                                                                                 \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg);                      \
            g_fail++;                                                                              \
        }                                                                                          \
    } while (0)

static ParserSnapshot *mock_snapshot(int version) {
    ParserSnapshot *snap = calloc(1, sizeof(ParserSnapshot));
    if (snap == NULL) return NULL;
    atomic_init(&snap->refcount, 1);
    snap->version = (uint64_t)version;
    return snap;
}

/* ------------------------------------------------------------------ */
/*  test_register_trigger_basic                                        */
/* ------------------------------------------------------------------ */

static void test_register_trigger_basic(void) {
    printf("test_register_trigger_basic\n");

    ParserSnapshot *root = mock_snapshot(1);
    ParserSnapshot *json_snap = mock_snapshot(2);
    GrammarContextStack *stack = grammar_context_create(root);
    ASSERT(stack != NULL, "stack created");

    bool ok = context_switch_register_trigger(stack, "json", json_snap, "json");
    ASSERT(ok, "first registration succeeds");

    /* Duplicate trigger lexeme -- should fail. */
    ParserSnapshot *json2 = mock_snapshot(3);
    ok = context_switch_register_trigger(stack, "json", json2, "json-2");
    ASSERT(!ok, "duplicate trigger lexeme rejected");

    /* Different trigger lexeme -- should succeed. */
    ok = context_switch_register_trigger(stack, "xpath", json2, "xpath");
    ASSERT(ok, "different trigger lexeme accepted");

    grammar_context_destroy(stack);
    snapshot_release(root);
    snapshot_release(json_snap);
    snapshot_release(json2);
}

/* ------------------------------------------------------------------ */
/*  test_register_trigger_null_inputs                                  */
/* ------------------------------------------------------------------ */

static void test_register_trigger_null_inputs(void) {
    printf("test_register_trigger_null_inputs\n");

    ParserSnapshot *root = mock_snapshot(1);
    ParserSnapshot *snap = mock_snapshot(2);
    GrammarContextStack *stack = grammar_context_create(root);

    ASSERT(!context_switch_register_trigger(NULL, "json", snap, "json"), "NULL stack");
    ASSERT(!context_switch_register_trigger(stack, NULL, snap, "json"), "NULL lexeme");
    ASSERT(!context_switch_register_trigger(stack, "json", NULL, "json"), "NULL snapshot");
    ASSERT(!context_switch_register_trigger(stack, "json", snap, NULL), "NULL mode_name");
    ASSERT(!context_switch_register_trigger(stack, "", snap, "json"), "empty lexeme rejected");

    /* Sanity: legitimate registration still works after the rejected calls. */
    ASSERT(context_switch_register_trigger(stack, "json", snap, "json"),
           "post-rejection registration succeeds");

    grammar_context_destroy(stack);
    snapshot_release(root);
    snapshot_release(snap);
}

/* ------------------------------------------------------------------ */
/*  test_classify_lexeme                                               */
/* ------------------------------------------------------------------ */

static void test_classify_lexeme(void) {
    printf("test_classify_lexeme\n");

    ParserSnapshot *root = mock_snapshot(1);
    ParserSnapshot *json_snap = mock_snapshot(2);
    ParserSnapshot *xq_snap = mock_snapshot(3);
    GrammarContextStack *stack = grammar_context_create(root);

    /* No triggers yet: every lexeme classifies to MODE_NONE. */
    ASSERT(context_switch_classify_lexeme(stack, "json") == MODE_NONE,
           "empty registry returns MODE_NONE");

    (void)context_switch_register_trigger(stack, "json", json_snap, "json");
    (void)context_switch_register_trigger(stack, "xpath", xq_snap, "xpath");

    GrammarMode m = context_switch_classify_lexeme(stack, "json");
    ASSERT(m != MODE_NONE, "json lexeme matches a registered mode");

    GrammarMode m2 = context_switch_classify_lexeme(stack, "xpath");
    ASSERT(m2 != MODE_NONE, "xpath lexeme matches a registered mode");
    ASSERT(m != m2, "json and xpath get distinct mode ids");

    /* Prefix match -- "jsonish" starts with "json", so the trigger fires. */
    ASSERT(context_switch_classify_lexeme(stack, "jsonish") == m, "prefix match works");

    /* Non-matching */
    ASSERT(context_switch_classify_lexeme(stack, "select") == MODE_NONE, "non-match");
    ASSERT(context_switch_classify_lexeme(stack, "from") == MODE_NONE, "non-match #2");

    /* NULL safety */
    ASSERT(context_switch_classify_lexeme(stack, NULL) == MODE_NONE, "NULL lexeme is MODE_NONE");
    ASSERT(context_switch_classify_lexeme(NULL, "json") == MODE_NONE, "NULL stack is MODE_NONE");
    ASSERT(context_switch_classify_lexeme(stack, "") == MODE_NONE, "empty lexeme is MODE_NONE");

    grammar_context_destroy(stack);
    snapshot_release(root);
    snapshot_release(json_snap);
    snapshot_release(xq_snap);
}

/* ------------------------------------------------------------------ */
/*  test_needed_fast_path                                              */
/* ------------------------------------------------------------------ */

static void test_needed_fast_path(void) {
    printf("test_needed_fast_path\n");

    ParserSnapshot *root = mock_snapshot(1);
    GrammarContextStack *stack = grammar_context_create(root);

    /* Zero triggers => false regardless of token / lexeme. */
    ASSERT(!context_switch_needed(stack, 42, "json"), "zero triggers + lexeme => false");
    ASSERT(!context_switch_needed(stack, 0, NULL), "zero triggers + NULL => false");
    ASSERT(!context_switch_needed(NULL, 0, NULL), "NULL stack => false");

    /* Add a trigger -- now lexeme matches behave as expected. */
    ParserSnapshot *snap = mock_snapshot(2);
    (void)context_switch_register_trigger(stack, "json", snap, "json");

    ASSERT(context_switch_needed(stack, 0, "json"), "matching lexeme => true");
    ASSERT(!context_switch_needed(stack, 0, "select"), "non-matching lexeme => false");
    ASSERT(!context_switch_needed(stack, 0, NULL), "NULL lexeme at root => false");
    ASSERT(!context_switch_needed(stack, 0, ""), "empty lexeme at root => false");

    grammar_context_destroy(stack);
    snapshot_release(root);
    snapshot_release(snap);
}

/* ------------------------------------------------------------------ */
/*  test_needed_inside_embedded                                        */
/* ------------------------------------------------------------------ */

static void test_needed_inside_embedded(void) {
    printf("test_needed_inside_embedded\n");

    ParserSnapshot *root = mock_snapshot(1);
    ParserSnapshot *snap = mock_snapshot(2);
    GrammarContextStack *stack = grammar_context_create(root);

    (void)context_switch_register_trigger(stack, "json", snap, "json");
    /* Push into the registered embedded mode so we leave the root. */
    GrammarMode m = context_switch_classify_lexeme(stack, "json");
    ASSERT(grammar_context_push(stack, m, 0), "push into embedded mode");
    ASSERT(!grammar_context_is_root_only(stack), "no longer root-only");

    /* Inside an embedded context, needed() always returns true so the
    ** caller polls for an exit. */
    ASSERT(context_switch_needed(stack, 0, NULL), "embedded => true even with NULL lexeme");
    ASSERT(context_switch_needed(stack, 99, "anything"), "embedded => true with arbitrary token");

    grammar_context_destroy(stack);
    snapshot_release(root);
    snapshot_release(snap);
}

/* ------------------------------------------------------------------ */
/*  test_detect_exit                                                   */
/* ------------------------------------------------------------------ */

static void test_detect_exit(void) {
    printf("test_detect_exit\n");

    ParserSnapshot *root = mock_snapshot(1);
    ParserSnapshot *snap = mock_snapshot(2);
    GrammarContextStack *stack = grammar_context_create(root);

    /* NULL safety + root short-circuit. */
    ASSERT(!context_switch_detect_exit(NULL, 0, NULL), "NULL stack => false");
    ASSERT(!context_switch_detect_exit(stack, 0, NULL), "root-only => false");

    /* Register a mode with an explicit exit token via the lower-level
    ** API so we can test the exit-token path through context_switch_detect_exit. */
    GrammarModeInfo info = {
        .mode = MODE_JSON,
        .name = "json",
        .snapshot = snap,
        .trigger_token = -1,
        .trigger_lexeme = "json",
        .exit_token = 99, /* explicit exit */
    };
    ASSERT(grammar_context_register_mode(stack, &info), "register exit-token mode");
    ASSERT(grammar_context_push(stack, MODE_JSON, 0), "push embedded mode");

    /* Wrong exit token -- no pop. */
    ASSERT(!context_switch_detect_exit(stack, 7, NULL), "wrong token => no pop");
    ASSERT(!grammar_context_is_root_only(stack), "still embedded after wrong token");

    /* Right exit token -- pop. */
    ASSERT(context_switch_detect_exit(stack, 99, NULL), "matching exit_token pops");
    ASSERT(grammar_context_is_root_only(stack), "back to root after pop");

    grammar_context_destroy(stack);
    snapshot_release(root);
    snapshot_release(snap);
}

/* ------------------------------------------------------------------ */
/*  test_register_trigger_full                                         */
/* ------------------------------------------------------------------ */

static void test_register_trigger_full(void) {
    printf("test_register_trigger_full\n");

    /* MAX_MODES inside grammar_context.c is 16.  Fill the table and
    ** confirm registration #17 fails cleanly. */
    ParserSnapshot *root = mock_snapshot(1);
    GrammarContextStack *stack = grammar_context_create(root);

    ParserSnapshot *snaps[16];
    char names[16][16];
    for (int i = 0; i < 16; i++) {
        snaps[i] = mock_snapshot(100 + i);
        snprintf(names[i], sizeof(names[i]), "trig%02d", i);
        bool ok = context_switch_register_trigger(stack, names[i], snaps[i], names[i]);
        ASSERT(ok, "register within capacity");
    }

    ParserSnapshot *extra = mock_snapshot(999);
    bool ok = context_switch_register_trigger(stack, "extra", extra, "extra");
    ASSERT(!ok, "register beyond capacity returns false");

    grammar_context_destroy(stack);
    snapshot_release(root);
    for (int i = 0; i < 16; i++) snapshot_release(snaps[i]);
    snapshot_release(extra);
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    test_register_trigger_basic();
    test_register_trigger_null_inputs();
    test_classify_lexeme();
    test_needed_fast_path();
    test_needed_inside_embedded();
    test_detect_exit();
    test_register_trigger_full();

    if (g_fail == 0) {
        printf("PASS  %d/%d\n", g_total, g_total);
        return 0;
    }
    printf("FAIL  %d/%d\n", g_total - g_fail, g_total);
    return 1;
}
