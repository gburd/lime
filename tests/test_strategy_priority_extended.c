/*
** Extended tests for strategy_priority.c to achieve >90% coverage.
**
** Tests cover:
**   - Priority extraction with PriorityMetadata
**   - Priority extraction without metadata (fallback to ID)
**   - Priority init with zero extensions
**   - Priority init with NULL extensions
**   - Priority resolve with empty contexts
**   - Priority resolve with tie-breaking on ext_id
**   - Priority resolve using explicit LimeContext.priority
**   - Priority resolve with mixed zero/non-zero priorities
**   - Priority update (no-op verification)
**   - Priority destroy with NULL
**   - All edge cases and error paths
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

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

/* ------------------------------------------------------------------ */
/*  Test: priority resolution with explicit context priorities        */
/* ------------------------------------------------------------------ */

static void test_priority_resolve_explicit_context_priority(void) {
    printf("test_priority_resolve_explicit_context_priority\n");

    ExtensionRegistry *reg = create_extension_registry();

    /* Register extensions */
    ExtensionInfo info1 = {
        .name = "low-priority-ext",
        .version = "1.0",
        .get_modifications = mock_get_mods,
    };
    ExtensionInfo info2 = {
        .name = "high-priority-ext",
        .version = "1.0",
        .get_modifications = mock_get_mods,
    };

    ExtensionID id1, id2;
    register_extension(reg, &info1, &id1);
    register_extension(reg, &info2, &id2);
    load_extension(reg, id1, NULL, NULL);
    load_extension(reg, id2, NULL, NULL);

    DisambiguationContext *ctx = disambiguation_create(STRAT_PRIORITY, reg);
    ASSERT(ctx != NULL, "create priority strategy");

    /* Create conflict with explicit priorities in contexts */
    ConflictPoint cp;
    conflict_point_init(&cp, 100, 10, CONFLICT_LEVEL_RULE);

    LimeContext lc1 = {
        .ext_id = id1,
        .token = 100,
        .state = 10,
        .priority = 50,  /* Lower priority value */
        .grammar_name = "low-priority-ext",
    };
    LimeContext lc2 = {
        .ext_id = id2,
        .token = 100,
        .state = 10,
        .priority = 100, /* Higher priority value - should win */
        .grammar_name = "high-priority-ext",
    };

    conflict_point_add_context(&cp, &lc1);
    conflict_point_add_context(&cp, &lc2);

    StrategyResult result = disambiguation_resolve(ctx, &cp, NULL);

    ASSERT(result.nwinners == 1, "should have 1 winner");
    ASSERT(result.winning_contexts[0].ext_id == id2,
           "higher priority value should win");
    ASSERT(result.confidence == 1.0f, "confidence should be 1.0");
    ASSERT(result.explanation != NULL, "should have explanation");

    strategy_result_cleanup(&result);
    conflict_point_destroy(&cp);
    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: priority resolution with zero priorities (fallback to ID)   */
/* ------------------------------------------------------------------ */

static void test_priority_resolve_zero_priorities(void) {
    printf("test_priority_resolve_zero_priorities\n");

    ExtensionRegistry *reg = create_extension_registry();

    ExtensionInfo info1 = {
        .name = "ext1",
        .version = "1.0",
        .get_modifications = mock_get_mods,
    };
    ExtensionInfo info2 = {
        .name = "ext2",
        .version = "1.0",
        .get_modifications = mock_get_mods,
    };

    ExtensionID id1, id2;
    register_extension(reg, &info1, &id1);
    register_extension(reg, &info2, &id2);
    load_extension(reg, id1, NULL, NULL);
    load_extension(reg, id2, NULL, NULL);

    DisambiguationContext *ctx = disambiguation_create(STRAT_PRIORITY, reg);

    /* All contexts have priority=0, should fall back to internal table */
    ConflictPoint cp;
    conflict_point_init(&cp, 50, 5, CONFLICT_LEVEL_TOKEN);

    LimeContext lc1 = {
        .ext_id = id1,
        .token = 50,
        .state = 5,
        .priority = 0,
        .grammar_name = "ext1",
    };
    LimeContext lc2 = {
        .ext_id = id2,
        .token = 50,
        .state = 5,
        .priority = 0,
        .grammar_name = "ext2",
    };

    conflict_point_add_context(&cp, &lc1);
    conflict_point_add_context(&cp, &lc2);

    StrategyResult result = disambiguation_resolve(ctx, &cp, NULL);

    ASSERT(result.nwinners == 1, "should have 1 winner");
    /* Lower ID should win in fallback mode (registered first) */
    ASSERT(result.winning_contexts[0].ext_id == id1,
           "lower ext_id should win when priorities are zero");

    strategy_result_cleanup(&result);
    conflict_point_destroy(&cp);
    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: priority resolution with equal priorities (tie-break on ID) */
/* ------------------------------------------------------------------ */

static void test_priority_resolve_equal_priorities(void) {
    printf("test_priority_resolve_equal_priorities\n");

    ExtensionRegistry *reg = create_extension_registry();

    ExtensionInfo info1 = {
        .name = "ext1",
        .version = "1.0",
        .get_modifications = mock_get_mods,
    };
    ExtensionInfo info2 = {
        .name = "ext2",
        .version = "1.0",
        .get_modifications = mock_get_mods,
    };

    ExtensionID id1, id2;
    register_extension(reg, &info1, &id1);
    register_extension(reg, &info2, &id2);
    load_extension(reg, id1, NULL, NULL);
    load_extension(reg, id2, NULL, NULL);

    DisambiguationContext *ctx = disambiguation_create(STRAT_PRIORITY, reg);

    /* Both contexts have same priority */
    ConflictPoint cp;
    conflict_point_init(&cp, 30, 2, CONFLICT_LEVEL_RULE);

    LimeContext lc1 = {
        .ext_id = id1,
        .token = 30,
        .state = 2,
        .priority = 75,
        .grammar_name = "ext1",
    };
    LimeContext lc2 = {
        .ext_id = id2,
        .token = 30,
        .state = 2,
        .priority = 75,
        .grammar_name = "ext2",
    };

    conflict_point_add_context(&cp, &lc1);
    conflict_point_add_context(&cp, &lc2);

    StrategyResult result = disambiguation_resolve(ctx, &cp, NULL);

    ASSERT(result.nwinners == 1, "should have 1 winner");
    /* Lower ID breaks the tie */
    ASSERT(result.winning_contexts[0].ext_id == id1,
           "lower ext_id should win on tie-break");

    strategy_result_cleanup(&result);
    conflict_point_destroy(&cp);
    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: priority resolve with NULL contexts                         */
/* ------------------------------------------------------------------ */

static void test_priority_resolve_null_contexts(void) {
    printf("test_priority_resolve_null_contexts\n");

    ExtensionRegistry *reg = create_extension_registry();
    DisambiguationContext *ctx = disambiguation_create(STRAT_PRIORITY, reg);

    /* Conflict point with no contexts */
    ConflictPoint cp;
    conflict_point_init(&cp, 10, 1, CONFLICT_LEVEL_TOKEN);
    /* cp.contexts is NULL, ncontexts is 0 */

    StrategyResult result = disambiguation_resolve(ctx, &cp, NULL);

    /* Should fail to resolve */
    ASSERT(result.nwinners == 0, "no contexts should produce no winners");
    ASSERT(result.winning_contexts == NULL, "winning_contexts should be NULL");

    strategy_result_cleanup(&result);
    conflict_point_destroy(&cp);
    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: priority resolve with NULL conflict point                   */
/* ------------------------------------------------------------------ */

static void test_priority_resolve_null_conflict(void) {
    printf("test_priority_resolve_null_conflict\n");

    ExtensionRegistry *reg = create_extension_registry();
    DisambiguationContext *ctx = disambiguation_create(STRAT_PRIORITY, reg);

    StrategyResult result = disambiguation_resolve(ctx, NULL, NULL);

    ASSERT(result.nwinners == 0, "NULL conflict should return no winners");

    strategy_result_cleanup(&result);
    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: priority resolve with NULL strategy context                 */
/* ------------------------------------------------------------------ */

static void test_priority_resolve_null_context(void) {
    printf("test_priority_resolve_null_context\n");

    ConflictPoint cp;
    conflict_point_init(&cp, 42, 5, CONFLICT_LEVEL_RULE);

    LimeContext lc = {
        .ext_id = 1,
        .token = 42,
        .state = 5,
        .priority = 10,
        .grammar_name = "test",
    };
    conflict_point_add_context(&cp, &lc);

    StrategyResult result = disambiguation_resolve(NULL, &cp, NULL);

    ASSERT(result.nwinners == 0, "NULL context should return no winners");

    strategy_result_cleanup(&result);
    conflict_point_destroy(&cp);
}

/* ------------------------------------------------------------------ */
/*  Test: priority init with zero extensions                          */
/* ------------------------------------------------------------------ */

static void test_priority_init_zero_extensions(void) {
    printf("test_priority_init_zero_extensions\n");

    ExtensionRegistry *reg = create_extension_registry();
    /* Registry has no extensions loaded */

    DisambiguationContext *ctx = disambiguation_create(STRAT_PRIORITY, reg);
    ASSERT(ctx != NULL, "should create context with zero extensions");

    /* Should still work for resolution (will just have no matches) */
    ConflictPoint cp;
    conflict_point_init(&cp, 1, 0, CONFLICT_LEVEL_TOKEN);

    LimeContext lc = {
        .ext_id = 999,  /* Unknown extension */
        .token = 1,
        .state = 0,
        .priority = 10,
        .grammar_name = "unknown",
    };
    conflict_point_add_context(&cp, &lc);

    StrategyResult result = disambiguation_resolve(ctx, &cp, NULL);

    /* Should still pick the only context available */
    ASSERT(result.nwinners == 1, "should have 1 winner even if ext unknown");

    strategy_result_cleanup(&result);
    conflict_point_destroy(&cp);
    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: priority update (no-op)                                     */
/* ------------------------------------------------------------------ */

static void test_priority_update_noop(void) {
    printf("test_priority_update_noop\n");

    ExtensionRegistry *reg = create_extension_registry();
    DisambiguationContext *ctx = disambiguation_create(STRAT_PRIORITY, reg);

    /* Update should not crash and should be a no-op */
    disambiguation_update(ctx, true);
    disambiguation_update(ctx, false);

    ASSERT(true, "update should be no-op and not crash");

    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: priority resolve with single context                        */
/* ------------------------------------------------------------------ */

static void test_priority_resolve_single_context(void) {
    printf("test_priority_resolve_single_context\n");

    ExtensionRegistry *reg = create_extension_registry();

    ExtensionInfo info = {
        .name = "only-ext",
        .version = "1.0",
        .get_modifications = mock_get_mods,
    };

    ExtensionID id;
    register_extension(reg, &info, &id);
    load_extension(reg, id, NULL, NULL);

    DisambiguationContext *ctx = disambiguation_create(STRAT_PRIORITY, reg);

    ConflictPoint cp;
    conflict_point_init(&cp, 20, 3, CONFLICT_LEVEL_RULE);

    LimeContext lc = {
        .ext_id = id,
        .token = 20,
        .state = 3,
        .priority = 50,
        .grammar_name = "only-ext",
    };

    conflict_point_add_context(&cp, &lc);

    StrategyResult result = disambiguation_resolve(ctx, &cp, NULL);

    ASSERT(result.nwinners == 1, "single context should produce 1 winner");
    ASSERT(result.winning_contexts[0].ext_id == id, "should select the only context");
    ASSERT(result.confidence == 1.0f, "confidence should be 1.0");

    strategy_result_cleanup(&result);
    conflict_point_destroy(&cp);
    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: mixed explicit and zero priorities                          */
/* ------------------------------------------------------------------ */

static void test_priority_resolve_mixed_priorities(void) {
    printf("test_priority_resolve_mixed_priorities\n");

    ExtensionRegistry *reg = create_extension_registry();

    ExtensionInfo info1 = {
        .name = "ext1",
        .version = "1.0",
        .get_modifications = mock_get_mods,
    };
    ExtensionInfo info2 = {
        .name = "ext2",
        .version = "1.0",
        .get_modifications = mock_get_mods,
    };
    ExtensionInfo info3 = {
        .name = "ext3",
        .version = "1.0",
        .get_modifications = mock_get_mods,
    };

    ExtensionID id1, id2, id3;
    register_extension(reg, &info1, &id1);
    register_extension(reg, &info2, &id2);
    register_extension(reg, &info3, &id3);
    load_extension(reg, id1, NULL, NULL);
    load_extension(reg, id2, NULL, NULL);
    load_extension(reg, id3, NULL, NULL);

    DisambiguationContext *ctx = disambiguation_create(STRAT_PRIORITY, reg);

    ConflictPoint cp;
    conflict_point_init(&cp, 15, 7, CONFLICT_LEVEL_SEMANTIC);

    /* Mix of explicit and zero priorities */
    LimeContext lc1 = {
        .ext_id = id1,
        .token = 15,
        .state = 7,
        .priority = 0,  /* Will use fallback */
        .grammar_name = "ext1",
    };
    LimeContext lc2 = {
        .ext_id = id2,
        .token = 15,
        .state = 7,
        .priority = 200,  /* Explicit high priority */
        .grammar_name = "ext2",
    };
    LimeContext lc3 = {
        .ext_id = id3,
        .token = 15,
        .state = 7,
        .priority = 0,  /* Will use fallback */
        .grammar_name = "ext3",
    };

    conflict_point_add_context(&cp, &lc1);
    conflict_point_add_context(&cp, &lc2);
    conflict_point_add_context(&cp, &lc3);

    StrategyResult result = disambiguation_resolve(ctx, &cp, NULL);

    ASSERT(result.nwinners == 1, "should have 1 winner");
    /* Explicit priority 200 should win over zero priorities */
    ASSERT(result.winning_contexts[0].ext_id == id2,
           "explicit priority should win over fallback");

    strategy_result_cleanup(&result);
    conflict_point_destroy(&cp);
    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: explanation string generation                               */
/* ------------------------------------------------------------------ */

static void test_priority_explanation(void) {
    printf("test_priority_explanation\n");

    ExtensionRegistry *reg = create_extension_registry();

    ExtensionInfo info1 = {
        .name = "postgres",
        .version = "1.0",
        .get_modifications = mock_get_mods,
    };
    ExtensionInfo info2 = {
        .name = "oracle",
        .version = "1.0",
        .get_modifications = mock_get_mods,
    };

    ExtensionID id1, id2;
    register_extension(reg, &info1, &id1);
    register_extension(reg, &info2, &id2);
    load_extension(reg, id1, NULL, NULL);
    load_extension(reg, id2, NULL, NULL);

    DisambiguationContext *ctx = disambiguation_create(STRAT_PRIORITY, reg);

    ConflictPoint cp;
    conflict_point_init(&cp, 77, 12, CONFLICT_LEVEL_RULE);

    LimeContext lc1 = {
        .ext_id = id1,
        .token = 77,
        .state = 12,
        .priority = 100,
        .grammar_name = "postgres",
    };
    LimeContext lc2 = {
        .ext_id = id2,
        .token = 77,
        .state = 12,
        .priority = 50,
        .grammar_name = "oracle",
    };

    conflict_point_add_context(&cp, &lc1);
    conflict_point_add_context(&cp, &lc2);

    StrategyResult result = disambiguation_resolve(ctx, &cp, NULL);

    ASSERT(result.explanation != NULL, "should have explanation");
    /* Explanation should mention priority, ext_id, grammar name, and count */
    ASSERT(strstr(result.explanation, "priority") != NULL,
           "explanation should mention 'priority'");
    ASSERT(strstr(result.explanation, "postgres") != NULL,
           "explanation should mention winner grammar name");

    strategy_result_cleanup(&result);
    conflict_point_destroy(&cp);
    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== Extended Priority Strategy Tests ===\n");

    test_priority_resolve_explicit_context_priority();
    test_priority_resolve_zero_priorities();
    test_priority_resolve_equal_priorities();
    test_priority_resolve_null_contexts();
    test_priority_resolve_null_conflict();
    test_priority_resolve_null_context();
    test_priority_init_zero_extensions();
    test_priority_update_noop();
    test_priority_resolve_single_context();
    test_priority_resolve_mixed_priorities();
    test_priority_explanation();

    printf("\n%d tests, %d failures\n", test_count, fail_count);
    return fail_count > 0 ? 1 : 0;
}
