/*
** test_context_switch_error.c -- error-path tests for the
** context-switch trigger registry and the multi-grammar driver.
**
** Cases:
**   - unclosed embedded JSON region (`json '...` without closing quote)
**   - malformed embedded JSON content
**   - trigger consulted but registry empty (no_trigger early-out)
*/
#include "multi.h"

#include "grammar_context.h"
#include "snapshot.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* ------------------------------------------------------------------ */
/*  test_unterminated_json                                             */
/* ------------------------------------------------------------------ */

static void test_unterminated_json(void) {
    printf("test_unterminated_json\n");

    SqlSelect *ast = NULL;
    MultiParseStatus s = multi_parse_sql(
        "SELECT json '{\"a\":1, FROM t WHERE id = 5;", true, &ast);
    ASSERT(s == MULTI_LEX_ERROR, "unterminated json region returns LEX_ERROR");
    ASSERT(ast == NULL, "AST not produced on lex error");
}

/* ------------------------------------------------------------------ */
/*  test_malformed_json                                                */
/* ------------------------------------------------------------------ */

static void test_malformed_json(void) {
    printf("test_malformed_json\n");

    SqlSelect *ast = NULL;
    MultiParseStatus s = multi_parse_sql(
        "SELECT json '{\"a\": ###}' FROM t WHERE id = 5;", true, &ast);
    ASSERT(s == MULTI_JSON_ERROR, "malformed json body returns JSON_ERROR");
    ASSERT(ast == NULL, "AST not produced on json parse error");
}

/* ------------------------------------------------------------------ */
/*  test_classify_with_no_registered_snapshot                          */
/* ------------------------------------------------------------------ */

static ParserSnapshot *make_tag(uint64_t v) {
    ParserSnapshot *s = calloc(1, sizeof(ParserSnapshot));
    if (s == NULL) return NULL;
    atomic_init(&s->refcount, 1);
    s->version = v;
    return s;
}

static void test_classify_with_no_registered_snapshot(void) {
    printf("test_classify_with_no_registered_snapshot\n");

    ParserSnapshot *root = make_tag(1);
    GrammarContextStack *stack = grammar_context_create(root);
    ASSERT(stack != NULL, "stack created");

    /* No triggers registered.  classify_lexeme should never hit a
    ** match and the fast-path needed() should bail. */
    ASSERT(context_switch_classify_lexeme(stack, "json") == MODE_NONE,
           "empty registry never matches");
    ASSERT(!context_switch_needed(stack, 0, "json"),
           "fast path returns false with no triggers");
    ASSERT(!context_switch_needed(stack, 0, NULL),
           "fast path returns false with no triggers and NULL lexeme");

    /* Attempt to register a NULL embedded snapshot -- should reject. */
    ASSERT(!context_switch_register_trigger(stack, "json", NULL, "json"),
           "NULL embedded snapshot is rejected");

    /* Confirm the rejected registration didn't corrupt state. */
    ASSERT(grammar_context_mode_count(stack) == 0,
           "rejected registration leaves count at 0");

    grammar_context_destroy(stack);
    snapshot_release(root);
}

/* ------------------------------------------------------------------ */
/*  test_detect_exit_at_root                                           */
/* ------------------------------------------------------------------ */

static void test_detect_exit_at_root(void) {
    printf("test_detect_exit_at_root\n");

    ParserSnapshot *root = make_tag(1);
    GrammarContextStack *stack = grammar_context_create(root);

    /* At the root (no embedded context active), detect_exit must
    ** never fire. */
    ASSERT(!context_switch_detect_exit(stack, 1, NULL), "no embedded => no exit");
    ASSERT(!context_switch_detect_exit(stack, 999, "anything"), "no embedded => no exit");
    ASSERT(!context_switch_detect_exit(NULL, 0, NULL), "NULL stack => no exit");

    grammar_context_destroy(stack);
    snapshot_release(root);
}

int main(void) {
    test_unterminated_json();
    test_malformed_json();
    test_classify_with_no_registered_snapshot();
    test_detect_exit_at_root();

    if (g_fail == 0) {
        printf("PASS  %d/%d\n", g_total, g_total);
        return 0;
    }
    printf("FAIL  %d/%d\n", g_total - g_fail, g_total);
    return 1;
}
