/*
** Unit tests for the fork-resolve disambiguation strategy.
**
** Tests the strategy_fork_resolve vtable implementation and its
** integration with the disambiguation framework.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "disambiguation.h"
#include "extension.h"
#include "conflict.h"
#include "parser_fork.h"
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
/*  Mock extension helpers                                             */
/* ------------------------------------------------------------------ */

static bool mock_get_mods(void *ud, const struct ParserSnapshot *base,
                           GrammarModification **out, uint32_t *nout) {
    (void)ud; (void)base;
    *out = NULL;
    *nout = 0;
    return true;
}

/*
** Create a minimal extension registry with N loaded extensions.
** Each extension gets ID = i+1, name = "ext_N".
*/
static ExtensionRegistry *create_mock_registry(uint32_t nextensions) {
    ExtensionRegistry *reg = create_extension_registry();
    if (reg == NULL) return NULL;

    for (uint32_t i = 0; i < nextensions; i++) {
        char name[32];
        snprintf(name, sizeof(name), "ext_%u", i + 1);

        ExtensionInfo info = {0};
        info.name = name;
        info.version = "1.0.0";
        info.get_modifications = mock_get_mods;

        ExtensionID id;
        bool ok = register_extension(reg, &info, &id);
        if (!ok) {
            destroy_extension_registry(reg);
            return NULL;
        }

        /* Load the extension so it's in EXT_LOADED state */
        char *error = NULL;
        load_extension(reg, id, NULL, &error);
        free(error);
    }

    return reg;
}

/* ------------------------------------------------------------------ */
/*  Test: strategy vtable via disambiguation_create                    */
/* ------------------------------------------------------------------ */

static void test_create_fork_resolve_context(void) {
    printf("test_create_fork_resolve_context\n");

    ExtensionRegistry *reg = create_mock_registry(3);
    ASSERT(reg != NULL, "mock registry should be created");

    DisambiguationContext *ctx = disambiguation_create(STRAT_FORK_RESOLVE, reg);
    ASSERT(ctx != NULL, "fork-resolve context should be created");

    DisambiguationStrategy strat = disambiguation_get_strategy(ctx);
    ASSERT(strat == STRAT_FORK_RESOLVE,
           "strategy should be STRAT_FORK_RESOLVE");

    const char *name = disambiguation_strategy_name(STRAT_FORK_RESOLVE);
    ASSERT(name != NULL, "strategy name should be non-NULL");
    ASSERT(strcmp(name, "fork-resolve") == 0,
           "strategy name should be 'fork-resolve'");

    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: create with empty registry                                   */
/* ------------------------------------------------------------------ */

static void test_create_empty_registry(void) {
    printf("test_create_empty_registry\n");

    ExtensionRegistry *reg = create_extension_registry();
    ASSERT(reg != NULL, "empty registry should be created");

    DisambiguationContext *ctx = disambiguation_create(STRAT_FORK_RESOLVE, reg);
    ASSERT(ctx != NULL,
           "fork-resolve context with empty registry should succeed");

    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: resolve single-context conflict (no fork needed)             */
/* ------------------------------------------------------------------ */

static void test_resolve_single_context(void) {
    printf("test_resolve_single_context\n");

    ExtensionRegistry *reg = create_mock_registry(1);
    DisambiguationContext *ctx = disambiguation_create(STRAT_FORK_RESOLVE, reg);
    ASSERT(ctx != NULL, "context should be created");

    /* Build a conflict with a single context */
    ConflictPoint cp;
    conflict_point_init(&cp, 42, 0, CONFLICT_LEVEL_TOKEN);

    LimeContext lctx = {0};
    lctx.ext_id = 1;
    lctx.token = 42;
    lctx.state = 0;
    lctx.priority = 10;
    lctx.grammar_name = "ext_1";
    conflict_point_add_context(&cp, &lctx);

    StrategyResult result = disambiguation_resolve(ctx, &cp, NULL);

    ASSERT(result.nwinners == 1, "should have exactly 1 winner");
    ASSERT(result.winning_contexts != NULL, "winners should be non-NULL");
    if (result.nwinners == 1 && result.winning_contexts != NULL) {
        ASSERT(result.winning_contexts[0].ext_id == 1,
               "winner should be ext_id 1");
    }
    ASSERT(result.confidence >= 0.99f,
           "single-context confidence should be ~1.0");
    ASSERT(result.explanation != NULL, "explanation should be set");

    strategy_result_cleanup(&result);
    conflict_point_destroy(&cp);
    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: resolve multi-context conflict by priority                   */
/* ------------------------------------------------------------------ */

static void test_resolve_multi_context_priority(void) {
    printf("test_resolve_multi_context_priority\n");

    ExtensionRegistry *reg = create_mock_registry(3);
    DisambiguationContext *ctx = disambiguation_create(STRAT_FORK_RESOLVE, reg);
    ASSERT(ctx != NULL, "context should be created");

    ConflictPoint cp;
    conflict_point_init(&cp, 10, 5, CONFLICT_LEVEL_RULE);

    /* Add three contexts with different priorities.
    ** Lower priority value = preferred. */
    LimeContext c1 = {0};
    c1.ext_id = 1; c1.token = 10; c1.state = 5;
    c1.priority = 30; c1.grammar_name = "ext_1";
    conflict_point_add_context(&cp, &c1);

    LimeContext c2 = {0};
    c2.ext_id = 2; c2.token = 10; c2.state = 5;
    c2.priority = 5;  /* Lowest priority value = highest priority */
    c2.grammar_name = "ext_2";
    conflict_point_add_context(&cp, &c2);

    LimeContext c3 = {0};
    c3.ext_id = 3; c3.token = 10; c3.state = 5;
    c3.priority = 20; c3.grammar_name = "ext_3";
    conflict_point_add_context(&cp, &c3);

    StrategyResult result = disambiguation_resolve(ctx, &cp, NULL);

    ASSERT(result.nwinners == 1, "should have 1 winner");
    if (result.nwinners == 1 && result.winning_contexts != NULL) {
        ASSERT(result.winning_contexts[0].ext_id == 2,
               "ext_2 should win (priority 5 is lowest/best)");
    }
    ASSERT(result.confidence > 0.0f && result.confidence < 1.0f,
           "multi-context confidence should be between 0 and 1");
    ASSERT(result.explanation != NULL, "explanation should be set");

    strategy_result_cleanup(&result);
    conflict_point_destroy(&cp);
    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: resolve with priority tie-breaking by ext_id                 */
/* ------------------------------------------------------------------ */

static void test_resolve_tiebreak_by_ext_id(void) {
    printf("test_resolve_tiebreak_by_ext_id\n");

    ExtensionRegistry *reg = create_mock_registry(2);
    DisambiguationContext *ctx = disambiguation_create(STRAT_FORK_RESOLVE, reg);
    ASSERT(ctx != NULL, "context should be created");

    ConflictPoint cp;
    conflict_point_init(&cp, 10, 5, CONFLICT_LEVEL_RULE);

    /* Same priority for both contexts */
    LimeContext c1 = {0};
    c1.ext_id = 2; c1.token = 10; c1.state = 5;
    c1.priority = 10; c1.grammar_name = "ext_2";
    conflict_point_add_context(&cp, &c1);

    LimeContext c2 = {0};
    c2.ext_id = 1; c2.token = 10; c2.state = 5;
    c2.priority = 10; c2.grammar_name = "ext_1";
    conflict_point_add_context(&cp, &c2);

    StrategyResult result = disambiguation_resolve(ctx, &cp, NULL);

    ASSERT(result.nwinners == 1, "should have 1 winner");
    if (result.nwinners == 1 && result.winning_contexts != NULL) {
        ASSERT(result.winning_contexts[0].ext_id == 1,
               "ext_1 should win (lower ext_id breaks tie)");
    }

    strategy_result_cleanup(&result);
    conflict_point_destroy(&cp);
    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: resolve with zero-priority contexts (fallback to ext_id)     */
/* ------------------------------------------------------------------ */

static void test_resolve_zero_priority(void) {
    printf("test_resolve_zero_priority\n");

    ExtensionRegistry *reg = create_mock_registry(2);
    DisambiguationContext *ctx = disambiguation_create(STRAT_FORK_RESOLVE, reg);
    ASSERT(ctx != NULL, "context should be created");

    ConflictPoint cp;
    conflict_point_init(&cp, 10, 0, CONFLICT_LEVEL_TOKEN);

    /* Both contexts have priority=0 (unset), so the strategy falls back
    ** to using the extension ID as priority. ext_id 1 < 2, so ext_1 wins. */
    LimeContext c1 = {0};
    c1.ext_id = 1; c1.token = 10; c1.priority = 0;
    c1.grammar_name = "ext_1";
    conflict_point_add_context(&cp, &c1);

    LimeContext c2 = {0};
    c2.ext_id = 2; c2.token = 10; c2.priority = 0;
    c2.grammar_name = "ext_2";
    conflict_point_add_context(&cp, &c2);

    StrategyResult result = disambiguation_resolve(ctx, &cp, NULL);

    ASSERT(result.nwinners == 1, "should have 1 winner");
    if (result.nwinners == 1 && result.winning_contexts != NULL) {
        /* When priority=0, the strategy uses the extension's registered
        ** priority from ForkExtEntry, which defaults to ext_id. */
        ASSERT(result.winning_contexts[0].ext_id == 1,
               "ext_1 should win (lower ext_id = higher default priority)");
    }

    strategy_result_cleanup(&result);
    conflict_point_destroy(&cp);
    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: resolve with empty conflict (no contexts)                    */
/* ------------------------------------------------------------------ */

static void test_resolve_empty_conflict(void) {
    printf("test_resolve_empty_conflict\n");

    ExtensionRegistry *reg = create_mock_registry(1);
    DisambiguationContext *ctx = disambiguation_create(STRAT_FORK_RESOLVE, reg);
    ASSERT(ctx != NULL, "context should be created");

    ConflictPoint cp;
    conflict_point_init(&cp, 10, 0, CONFLICT_LEVEL_TOKEN);
    /* Don't add any contexts */

    StrategyResult result = disambiguation_resolve(ctx, &cp, NULL);

    ASSERT(result.nwinners == 0, "should have no winners");
    ASSERT(result.winning_contexts == NULL, "winners should be NULL");

    strategy_result_cleanup(&result);
    conflict_point_destroy(&cp);
    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: resolve with NULL conflict                                   */
/* ------------------------------------------------------------------ */

static void test_resolve_null_conflict(void) {
    printf("test_resolve_null_conflict\n");

    ExtensionRegistry *reg = create_mock_registry(1);
    DisambiguationContext *ctx = disambiguation_create(STRAT_FORK_RESOLVE, reg);
    ASSERT(ctx != NULL, "context should be created");

    StrategyResult result = disambiguation_resolve(ctx, NULL, NULL);
    ASSERT(result.nwinners == 0, "should have no winners for NULL conflict");

    strategy_result_cleanup(&result);
    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: disambiguation_update (no-op for fork-resolve)               */
/* ------------------------------------------------------------------ */

static void test_update_noop(void) {
    printf("test_update_noop\n");

    ExtensionRegistry *reg = create_mock_registry(1);
    DisambiguationContext *ctx = disambiguation_create(STRAT_FORK_RESOLVE, reg);
    ASSERT(ctx != NULL, "context should be created");

    /* Should not crash */
    disambiguation_update(ctx, true);
    disambiguation_update(ctx, false);
    disambiguation_update(NULL, true);

    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: ParseForkSet integration with fork_resolve_create_forks      */
/* ------------------------------------------------------------------ */

/*
** Mock parser for testing fork_resolve_create_forks.
*/
#define MOCK_STACK_DEPTH_FR 8

typedef struct MockStackEntryFR {
    uint16_t stateno;
    uint16_t major;
    uint64_t minor;
} MockStackEntryFR;

typedef struct MockParserFR {
    MockStackEntryFR *yytos;
    int yyerrcnt;
    MockStackEntryFR *yystackEnd;
    MockStackEntryFR *yystack;
    MockStackEntryFR yystk0[MOCK_STACK_DEPTH_FR];
} MockParserFR;

/* Declared in strategy_fork_resolve.c */
extern ParseForkSet *fork_resolve_create_forks(
    const ConflictPoint *conflict,
    const void *parser,
    size_t parser_size,
    size_t stack_entry_size,
    size_t inline_stack_offset,
    uint32_t inline_stack_count,
    size_t stack_field_offset,
    size_t tos_field_offset,
    size_t stack_end_offset,
    struct ParserSnapshot **snapshots,
    uint32_t max_forks);

/* TiebreakRule enum from strategy_fork_resolve.c */
typedef enum {
    TEST_TIEBREAK_PRIORITY = 0,
    TEST_TIEBREAK_LONGEST_MATCH,
    TEST_TIEBREAK_FIRST_COMPLETE,
} TestTiebreakRule;

extern int fork_resolve_evaluate(const ParseForkSet *fset,
                                 TestTiebreakRule tiebreak);

static ParserSnapshot *create_test_snapshot(void) {
    ParserSnapshot *snap = calloc(1, sizeof(ParserSnapshot));
    if (snap == NULL) return NULL;
    atomic_init(&snap->refcount, 1);
    snap->version = 1;
    return snap;
}

static void test_create_forks_from_conflict(void) {
    printf("test_create_forks_from_conflict\n");

    MockParserFR parser;
    memset(&parser, 0, sizeof(parser));
    parser.yystack = parser.yystk0;
    parser.yytos = &parser.yystk0[0];
    parser.yystackEnd = &parser.yystk0[MOCK_STACK_DEPTH_FR - 1];
    parser.yyerrcnt = -1;
    parser.yystk0[0].stateno = 0;

    /* Create snapshots for each context */
    ParserSnapshot *snaps[3];
    snaps[0] = create_test_snapshot();
    snaps[1] = create_test_snapshot();
    snaps[2] = create_test_snapshot();

    ConflictPoint cp;
    conflict_point_init(&cp, 10, 5, CONFLICT_LEVEL_RULE);

    LimeContext c1 = {0};
    c1.ext_id = 1; c1.priority = 20; c1.grammar_name = "g1";
    conflict_point_add_context(&cp, &c1);

    LimeContext c2 = {0};
    c2.ext_id = 2; c2.priority = 5; c2.grammar_name = "g2";
    conflict_point_add_context(&cp, &c2);

    LimeContext c3 = {0};
    c3.ext_id = 3; c3.priority = 15; c3.grammar_name = "g3";
    conflict_point_add_context(&cp, &c3);

    ParseForkSet *fset = fork_resolve_create_forks(
        &cp, &parser,
        sizeof(MockParserFR), sizeof(MockStackEntryFR),
        offsetof(MockParserFR, yystk0), MOCK_STACK_DEPTH_FR,
        offsetof(MockParserFR, yystack),
        offsetof(MockParserFR, yytos),
        offsetof(MockParserFR, yystackEnd),
        snaps, 0);

    ASSERT(fset != NULL, "fork set should be created");
    ASSERT(fset->count == 3, "should have 3 forks");

    /* Mark the second fork (priority 5) as completed */
    if (fset->count >= 2) {
        parse_fork_complete(fset->forks[1], NULL, NULL);
    }

    int best_idx = fork_resolve_evaluate(fset, TEST_TIEBREAK_PRIORITY);
    /* The only completed fork is index 1 */
    ASSERT(best_idx == 1, "best fork should be index 1 (only completed)");

    /* Now complete all and check that priority 5 wins */
    for (uint32_t i = 0; i < fset->count; i++) {
        if (fset->forks[i]->status != FORK_COMPLETED) {
            parse_fork_complete(fset->forks[i], NULL, NULL);
        }
    }

    best_idx = fork_resolve_evaluate(fset, TEST_TIEBREAK_PRIORITY);
    ASSERT(best_idx >= 0, "should find a best fork");
    if (best_idx >= 0) {
        ASSERT(fset->forks[best_idx]->priority == 5,
               "best fork should have priority 5");
    }

    parse_fork_set_destroy(fset);
    conflict_point_destroy(&cp);

    snapshot_release(snaps[0]);
    snapshot_release(snaps[1]);
    snapshot_release(snaps[2]);
}

/* ------------------------------------------------------------------ */
/*  Test: fork_resolve_create_forks NULL safety                        */
/* ------------------------------------------------------------------ */

static void test_create_forks_null_safety(void) {
    printf("test_create_forks_null_safety\n");

    ASSERT(fork_resolve_create_forks(NULL, NULL, 0, 0, 0, 0, 0, 0, 0,
                                      NULL, 0) == NULL,
           "all-NULL should return NULL");

    /* NULL conflict */
    MockParserFR parser;
    memset(&parser, 0, sizeof(parser));
    ParserSnapshot *snap = create_test_snapshot();
    ParserSnapshot *snaps[1] = { snap };

    ASSERT(fork_resolve_create_forks(NULL, &parser,
                                      sizeof(parser), sizeof(MockStackEntryFR),
                                      0, 0, 0, 0, 0,
                                      snaps, 0) == NULL,
           "NULL conflict should return NULL");

    /* NULL parser */
    ConflictPoint cp;
    conflict_point_init(&cp, 10, 0, CONFLICT_LEVEL_TOKEN);
    LimeContext c = {0};
    c.ext_id = 1; c.priority = 1;
    conflict_point_add_context(&cp, &c);

    ASSERT(fork_resolve_create_forks(&cp, NULL,
                                      sizeof(parser), sizeof(MockStackEntryFR),
                                      0, 0, 0, 0, 0,
                                      snaps, 0) == NULL,
           "NULL parser should return NULL");

    /* fork_resolve_evaluate NULL */
    ASSERT(fork_resolve_evaluate(NULL, TEST_TIEBREAK_PRIORITY) == -1,
           "evaluate NULL should return -1");

    conflict_point_destroy(&cp);
    snapshot_release(snap);
}

/* ------------------------------------------------------------------ */
/*  Test: many contexts limited by max_forks                           */
/* ------------------------------------------------------------------ */

static void test_create_forks_max_limit(void) {
    printf("test_create_forks_max_limit\n");

    MockParserFR parser;
    memset(&parser, 0, sizeof(parser));
    parser.yystack = parser.yystk0;
    parser.yytos = &parser.yystk0[0];
    parser.yystackEnd = &parser.yystk0[MOCK_STACK_DEPTH_FR - 1];
    parser.yystk0[0].stateno = 0;

    ParserSnapshot *snaps[5];
    for (int i = 0; i < 5; i++) {
        snaps[i] = create_test_snapshot();
    }

    ConflictPoint cp;
    conflict_point_init(&cp, 10, 0, CONFLICT_LEVEL_TOKEN);

    for (int i = 0; i < 5; i++) {
        LimeContext c = {0};
        c.ext_id = (uint32_t)(i + 1);
        c.priority = i + 1;
        conflict_point_add_context(&cp, &c);
    }

    /* Limit to 3 forks */
    ParseForkSet *fset = fork_resolve_create_forks(
        &cp, &parser,
        sizeof(MockParserFR), sizeof(MockStackEntryFR),
        offsetof(MockParserFR, yystk0), MOCK_STACK_DEPTH_FR,
        offsetof(MockParserFR, yystack),
        offsetof(MockParserFR, yytos),
        offsetof(MockParserFR, yystackEnd),
        snaps, 3);

    ASSERT(fset != NULL, "fork set should be created");
    ASSERT(fset->count <= 3, "should have at most 3 forks");

    parse_fork_set_destroy(fset);
    conflict_point_destroy(&cp);

    for (int i = 0; i < 5; i++) {
        snapshot_release(snaps[i]);
    }
}

/* ------------------------------------------------------------------ */
/*  Test: resolve confidence scaling                                   */
/* ------------------------------------------------------------------ */

static void test_confidence_scaling(void) {
    printf("test_confidence_scaling\n");

    ExtensionRegistry *reg = create_mock_registry(5);
    DisambiguationContext *ctx = disambiguation_create(STRAT_FORK_RESOLVE, reg);
    ASSERT(ctx != NULL, "context should be created");

    /* Single context: confidence should be 1.0 */
    ConflictPoint cp1;
    conflict_point_init(&cp1, 10, 0, CONFLICT_LEVEL_TOKEN);
    LimeContext c1 = {0};
    c1.ext_id = 1; c1.priority = 1;
    conflict_point_add_context(&cp1, &c1);

    StrategyResult r1 = disambiguation_resolve(ctx, &cp1, NULL);
    ASSERT(r1.confidence >= 0.99f,
           "single context should have confidence ~1.0");
    strategy_result_cleanup(&r1);
    conflict_point_destroy(&cp1);

    /* Two contexts */
    ConflictPoint cp2;
    conflict_point_init(&cp2, 10, 0, CONFLICT_LEVEL_TOKEN);
    LimeContext c2a = {0}; c2a.ext_id = 1; c2a.priority = 1;
    LimeContext c2b = {0}; c2b.ext_id = 2; c2b.priority = 2;
    conflict_point_add_context(&cp2, &c2a);
    conflict_point_add_context(&cp2, &c2b);

    StrategyResult r2 = disambiguation_resolve(ctx, &cp2, NULL);
    ASSERT(r2.confidence > 0.0f && r2.confidence < 1.0f,
           "two-context confidence should be between 0 and 1");
    float conf2 = r2.confidence;
    strategy_result_cleanup(&r2);
    conflict_point_destroy(&cp2);

    /* Five contexts: confidence should be lower than two-context */
    ConflictPoint cp5;
    conflict_point_init(&cp5, 10, 0, CONFLICT_LEVEL_TOKEN);
    for (int i = 0; i < 5; i++) {
        LimeContext c = {0};
        c.ext_id = (uint32_t)(i + 1);
        c.priority = i + 1;
        conflict_point_add_context(&cp5, &c);
    }

    StrategyResult r5 = disambiguation_resolve(ctx, &cp5, NULL);
    ASSERT(r5.confidence > 0.0f && r5.confidence < 1.0f,
           "five-context confidence should be between 0 and 1");
    ASSERT(r5.confidence < conf2,
           "more contexts should give lower confidence");
    strategy_result_cleanup(&r5);
    conflict_point_destroy(&cp5);

    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== test_fork_resolve ===\n");

    test_create_fork_resolve_context();
    test_create_empty_registry();
    test_resolve_single_context();
    test_resolve_multi_context_priority();
    test_resolve_tiebreak_by_ext_id();
    test_resolve_zero_priority();
    test_resolve_empty_conflict();
    test_resolve_null_conflict();
    test_update_noop();
    test_create_forks_from_conflict();
    test_create_forks_null_safety();
    test_create_forks_max_limit();
    test_confidence_scaling();

    printf("\n%d tests, %d failures\n", test_count, fail_count);
    return fail_count > 0 ? 1 : 0;
}
