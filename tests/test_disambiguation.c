/*
** Unit tests for the disambiguation strategy framework.
**
** Tests cover:
**   - StrategyResult init/cleanup
**   - disambiguation_create() with built-in strategies
**   - disambiguation_create_custom() with user vtable
**   - disambiguation_resolve() basic flow
**   - disambiguation_update() feedback
**   - disambiguation_get_strategy() and strategy_name()
**   - disambiguation_destroy() and NULL safety
**   - Priority strategy basic resolution
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "disambiguation.h"
#include "extension.h"
#include "conflict.h"

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
/*  Mock extension setup                                               */
/* ------------------------------------------------------------------ */

static bool mock_get_mods(void *user_data,
                          const struct ParserSnapshot *base,
                          GrammarModification **mods_out,
                          uint32_t *nmods_out) {
    (void)user_data; (void)base;
    *mods_out = NULL;
    *nmods_out = 0;
    return true;
}

static ExtensionRegistry *create_test_registry(void) {
    ExtensionRegistry *reg = create_extension_registry();
    if (!reg) return NULL;

    ExtensionInfo info1 = {
        .name = "ext-high",
        .version = "1.0",
        .get_modifications = mock_get_mods,
    };
    ExtensionInfo info2 = {
        .name = "ext-low",
        .version = "1.0",
        .get_modifications = mock_get_mods,
    };
    ExtensionID id1, id2;
    register_extension(reg, &info1, &id1);
    register_extension(reg, &info2, &id2);
    load_extension(reg, id1, NULL, NULL);
    load_extension(reg, id2, NULL, NULL);
    return reg;
}

/* ------------------------------------------------------------------ */
/*  Test: StrategyResult init/cleanup                                  */
/* ------------------------------------------------------------------ */

static void test_strategy_result_lifecycle(void) {
    printf("test_strategy_result_lifecycle\n");

    StrategyResult result;
    strategy_result_init(&result);

    ASSERT(result.winning_contexts == NULL, "initial winning_contexts NULL");
    ASSERT(result.nwinners == 0, "initial nwinners 0");
    ASSERT(result.confidence == 0.0f, "initial confidence 0.0");
    ASSERT(result.explanation == NULL, "initial explanation NULL");

    /* Cleanup of empty result should be safe */
    strategy_result_cleanup(&result);
    ASSERT(true, "cleanup empty should not crash");

    /* Cleanup with allocated data */
    StrategyResult r2;
    strategy_result_init(&r2);
    r2.winning_contexts = malloc(sizeof(LimeContext));
    r2.nwinners = 1;
    r2.explanation = strdup("test explanation");
    strategy_result_cleanup(&r2);
    ASSERT(r2.winning_contexts == NULL, "cleanup should NULL winning_contexts");
    ASSERT(r2.explanation == NULL, "cleanup should NULL explanation");

    /* NULL should be safe */
    strategy_result_init(NULL);
    strategy_result_cleanup(NULL);
    ASSERT(true, "NULL args should not crash");
}

/* ------------------------------------------------------------------ */
/*  Test: disambiguation_create with STRAT_PRIORITY                    */
/* ------------------------------------------------------------------ */

static void test_create_priority_strategy(void) {
    printf("test_create_priority_strategy\n");

    ExtensionRegistry *reg = create_test_registry();

    DisambiguationContext *ctx = disambiguation_create(STRAT_PRIORITY, reg);
    ASSERT(ctx != NULL, "create STRAT_PRIORITY should succeed");

    LimeStrategy s = disambiguation_get_strategy(ctx);
    ASSERT(s == STRAT_PRIORITY, "strategy should be STRAT_PRIORITY");

    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: disambiguation_create with stub strategies                   */
/* ------------------------------------------------------------------ */

static void test_create_stub_strategies(void) {
    printf("test_create_stub_strategies\n");

    ExtensionRegistry *reg = create_test_registry();

    /* Fork-resolve has its own implementation; LLM is still a stub.
    ** Bayesian is a real strategy now (see test_strategy_bayesian.c)
    ** -- here we just verify creation/destruction works. */
    DisambiguationContext *ctx_fork = disambiguation_create(STRAT_FORK_RESOLVE, reg);
    ASSERT(ctx_fork != NULL, "STRAT_FORK_RESOLVE should create");
    disambiguation_destroy(ctx_fork);

    DisambiguationContext *ctx_bayes = disambiguation_create(STRAT_BAYESIAN, reg);
    ASSERT(ctx_bayes != NULL, "STRAT_BAYESIAN should create");
    disambiguation_destroy(ctx_bayes);

    DisambiguationContext *ctx_llm = disambiguation_create(STRAT_LLM, reg);
    ASSERT(ctx_llm != NULL, "STRAT_LLM should create");
    disambiguation_destroy(ctx_llm);

    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: disambiguation_create with NULL registry                     */
/* ------------------------------------------------------------------ */

static void test_create_null_registry(void) {
    printf("test_create_null_registry\n");

    DisambiguationContext *ctx = disambiguation_create(STRAT_PRIORITY, NULL);
    /* Implementation may return NULL or create with empty state */
    /* Either behavior is acceptable; we just check it doesn't crash */
    if (ctx != NULL) {
        disambiguation_destroy(ctx);
    }
    ASSERT(true, "NULL registry should not crash");
}

/* ------------------------------------------------------------------ */
/*  Test: disambiguation_create_custom                                 */
/* ------------------------------------------------------------------ */

static int custom_init_called = 0;
static int custom_resolve_called = 0;
static int custom_destroy_called = 0;

static void *custom_init(const Extension *const *extensions, uint32_t n) {
    (void)extensions; (void)n;
    custom_init_called++;
    return (void *)(uintptr_t)42;
}

static bool custom_resolve(void *ctx, const ConflictPoint *conflict,
                           struct ParseContext *parse_ctx, int lookahead,
                           StrategyResult *result) {
    (void)ctx; (void)conflict; (void)parse_ctx; (void)lookahead;
    custom_resolve_called++;
    strategy_result_init(result);
    result->confidence = 0.99f;
    return true;
}

static void custom_destroy(void *ctx) {
    (void)ctx;
    custom_destroy_called++;
}

static void test_create_custom(void) {
    printf("test_create_custom\n");

    custom_init_called = 0;
    custom_resolve_called = 0;
    custom_destroy_called = 0;

    ExtensionRegistry *reg = create_test_registry();

    DisambiguationStrategyVTable vtable = {
        .init = custom_init,
        .resolve = custom_resolve,
        .update = NULL,
        .destroy = custom_destroy,
    };

    DisambiguationContext *ctx = disambiguation_create_custom(&vtable, reg);
    ASSERT(ctx != NULL, "custom create should succeed");
    ASSERT(custom_init_called == 1, "init should be called once");

    LimeStrategy s = disambiguation_get_strategy(ctx);
    ASSERT(s == STRAT_CUSTOM, "strategy should be STRAT_CUSTOM");

    disambiguation_destroy(ctx);
    ASSERT(custom_destroy_called == 1, "destroy should be called once");

    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: disambiguation_resolve with stub (returns unresolved)        */
/* ------------------------------------------------------------------ */

static void test_resolve_fork_resolve(void) {
    printf("test_resolve_fork_resolve\n");

    ExtensionRegistry *reg = create_test_registry();
    DisambiguationContext *ctx = disambiguation_create(STRAT_FORK_RESOLVE, reg);
    ASSERT(ctx != NULL, "create should succeed");

    /* Single context: fork-resolve should return it as the winner */
    ConflictPoint cp;
    conflict_point_init(&cp, 10, 5, CONFLICT_LEVEL_RULE);
    LimeContext lc = { .ext_id = 1, .token = 10, .state = 5, .priority = 0 };
    conflict_point_add_context(&cp, &lc);

    StrategyResult result = disambiguation_resolve(ctx, &cp, NULL);

    ASSERT(result.nwinners == 1, "fork-resolve with single context should have 1 winner");
    ASSERT(result.winning_contexts != NULL, "should have winning context");
    ASSERT(result.winning_contexts[0].ext_id == 1, "winner should be ext 1");
    ASSERT(result.confidence == 1.0f, "single context should have full confidence");

    strategy_result_cleanup(&result);
    conflict_point_destroy(&cp);
    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: disambiguation_resolve with custom strategy                  */
/* ------------------------------------------------------------------ */

static void test_resolve_custom(void) {
    printf("test_resolve_custom\n");

    custom_init_called = 0;
    custom_resolve_called = 0;
    custom_destroy_called = 0;

    ExtensionRegistry *reg = create_test_registry();

    DisambiguationStrategyVTable vtable = {
        .init = custom_init,
        .resolve = custom_resolve,
        .update = NULL,
        .destroy = custom_destroy,
    };

    DisambiguationContext *ctx = disambiguation_create_custom(&vtable, reg);

    ConflictPoint cp;
    conflict_point_init(&cp, 20, 3, CONFLICT_LEVEL_TOKEN);

    StrategyResult result = disambiguation_resolve(ctx, &cp, NULL);
    ASSERT(custom_resolve_called == 1, "resolve should be called");
    ASSERT(result.confidence == 0.99f, "confidence from custom strategy");

    strategy_result_cleanup(&result);
    conflict_point_destroy(&cp);
    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: disambiguation_update                                        */
/* ------------------------------------------------------------------ */

static void test_disambiguation_update(void) {
    printf("test_disambiguation_update\n");

    ExtensionRegistry *reg = create_test_registry();
    DisambiguationContext *ctx = disambiguation_create(STRAT_PRIORITY, reg);

    /* Update should not crash */
    disambiguation_update(ctx, true);
    disambiguation_update(ctx, false);
    ASSERT(true, "update should not crash");

    /* NULL context */
    disambiguation_update(NULL, true);
    ASSERT(true, "update(NULL) should be safe");

    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: disambiguation_strategy_name                                 */
/* ------------------------------------------------------------------ */

static void test_strategy_name(void) {
    printf("test_strategy_name\n");

    const char *n1 = disambiguation_strategy_name(STRAT_PRIORITY);
    ASSERT(n1 != NULL, "priority name should not be NULL");

    const char *n2 = disambiguation_strategy_name(STRAT_FORK_RESOLVE);
    ASSERT(n2 != NULL, "fork_resolve name should not be NULL");

    const char *n3 = disambiguation_strategy_name(STRAT_BAYESIAN);
    ASSERT(n3 != NULL, "bayesian name should not be NULL");

    const char *n4 = disambiguation_strategy_name(STRAT_LLM);
    ASSERT(n4 != NULL, "llm name should not be NULL");

    const char *n5 = disambiguation_strategy_name(STRAT_CUSTOM);
    ASSERT(n5 != NULL, "custom name should not be NULL");

    /* All names should be distinct */
    ASSERT(strcmp(n1, n2) != 0, "priority != fork_resolve");
    ASSERT(strcmp(n1, n3) != 0, "priority != bayesian");
}

/* ------------------------------------------------------------------ */
/*  Test: disambiguation_destroy NULL safety                           */
/* ------------------------------------------------------------------ */

static void test_destroy_null(void) {
    printf("test_destroy_null\n");

    disambiguation_destroy(NULL);
    ASSERT(true, "destroy(NULL) should be safe");
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== Disambiguation Strategy Tests ===\n");

    test_strategy_result_lifecycle();
    test_create_priority_strategy();
    test_create_stub_strategies();
    test_create_null_registry();
    test_create_custom();
    test_resolve_fork_resolve();
    test_resolve_custom();
    test_disambiguation_update();
    test_strategy_name();
    test_destroy_null();

    printf("\n%d tests, %d failures\n", test_count, fail_count);
    return fail_count > 0 ? 1 : 0;
}
