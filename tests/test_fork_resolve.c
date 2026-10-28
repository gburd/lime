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
#include "parse_context.h"
#include "lime_compiler.h"
#include "snapshot.h"

/* Set when the real-snapshot sub-tests could not run because the
** in-process compiler was unavailable; main() reports exit 77. */
static int skipped_real_snapshot = 0;

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

    LimeStrategy strat = disambiguation_get_strategy(ctx);
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

/* Seeding variant: takes the stack-entry `stateno` layout so each
** fork resumes from the forked parser's captured stack instead of
** restarting at LALR state 0.  See test_seeded_resume_beats_state0. */
extern ParseForkSet *fork_resolve_create_forks_seedable(
    const ConflictPoint *conflict,
    const void *parser,
    size_t parser_size,
    size_t stack_entry_size,
    size_t inline_stack_offset,
    uint32_t inline_stack_count,
    size_t stack_field_offset,
    size_t tos_field_offset,
    size_t stack_end_offset,
    size_t stateno_offset,
    size_t stateno_size,
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

extern uint32_t fork_resolve_feed_tokens(ParseForkSet *fset,
                                         const int *tokens,
                                         uint32_t ntokens);

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
/*  Test: real LALR stepping -- wrong-grammar forks genuinely FAIL     */
/* ------------------------------------------------------------------ */

/*
** Two grammars that genuinely disagree about the token sequence
** "A B": G_AB accepts it, G_BA requires "B A" and rejects an A in the
** initial state outright.  Token codes are assigned in declaration
** order, identically in both grammars (A=1, B=2, EOF=0), so the same
** integer stream means different things to the two automata -- which
** is exactly the situation fork-resolve exists to arbitrate.
**
** With the old stub these sub-tests are impossible to pass: every
** fork accepted every token and every fork reported complete, so both
** forks would end up FORK_COMPLETED and the wrong-grammar fork would
** never fail.
*/
static const char *G_AB =
    "%name ForkResolveAB\n"
    "%token A B.\n"
    "%start_symbol s\n"
    "s ::= A B.\n";

static const char *G_BA =
    "%name ForkResolveBA\n"
    "%token A B.\n"
    "%start_symbol s\n"
    "s ::= B A.\n";

static ParserSnapshot *compile_snapshot(const char *text) {
    ParserSnapshot *snap = NULL;
    char *err = NULL;
    if (lime_compile_grammar_in_process(text, strlen(text), &snap, &err) != 0) {
        free(err);
        return NULL;
    }
    free(err);
    return snap;
}

/*
** Drive `tokens` through a two-fork set whose forks carry `snap_a` and
** `snap_b`, and report each fork's final status.  Returns false if the
** fork set could not be built.
*/
static bool run_two_fork_race(ParserSnapshot *snap_a, ParserSnapshot *snap_b,
                              const int *tokens, uint32_t ntokens,
                              ParseForkStatus *status_a,
                              ParseForkStatus *status_b) {
    MockParserFR parser;
    memset(&parser, 0, sizeof(parser));
    parser.yystack = parser.yystk0;
    parser.yytos = &parser.yystk0[0];
    parser.yystackEnd = &parser.yystk0[MOCK_STACK_DEPTH_FR - 1];
    parser.yyerrcnt = -1;

    ParserSnapshot *snaps[2] = { snap_a, snap_b };

    ConflictPoint cp;
    conflict_point_init(&cp, 1, 0, CONFLICT_LEVEL_TOKEN);

    /* Give the A-grammar the WORSE (higher) priority so a pass cannot
    ** be explained by priority ordering: only real token stepping can
    ** make it win. */
    LimeContext ca = {0};
    ca.ext_id = 1; ca.priority = 99; ca.grammar_name = "AB";
    conflict_point_add_context(&cp, &ca);

    LimeContext cb = {0};
    cb.ext_id = 2; cb.priority = 1; cb.grammar_name = "BA";
    conflict_point_add_context(&cp, &cb);

    ParseForkSet *fset = fork_resolve_create_forks(
        &cp, &parser,
        sizeof(MockParserFR), sizeof(MockStackEntryFR),
        offsetof(MockParserFR, yystk0), MOCK_STACK_DEPTH_FR,
        offsetof(MockParserFR, yystack),
        offsetof(MockParserFR, yytos),
        offsetof(MockParserFR, yystackEnd),
        snaps, 0);

    if (fset == NULL || fset->count != 2) {
        if (fset != NULL) parse_fork_set_destroy(fset);
        conflict_point_destroy(&cp);
        return false;
    }

    /* Record identities before feeding: failed forks get pruned (and
    ** freed) out of the set, so status must be read via the priority
    ** we assigned rather than by index afterwards. */
    fork_resolve_feed_tokens(fset, tokens, ntokens);

    *status_a = FORK_FAILED; /* absent from the set == pruned == failed */
    *status_b = FORK_FAILED;
    for (uint32_t i = 0; i < fset->count; i++) {
        if (fset->forks[i]->priority == 99) *status_a = fset->forks[i]->status;
        if (fset->forks[i]->priority == 1) *status_b = fset->forks[i]->status;
    }

    parse_fork_set_destroy(fset);
    conflict_point_destroy(&cp);
    return true;
}

static void test_real_stepping_wrong_grammar_fails(void) {
    printf("test_real_stepping_wrong_grammar_fails\n");

    ParserSnapshot *ab = compile_snapshot(G_AB);
    ParserSnapshot *ba = compile_snapshot(G_BA);
    if (ab == NULL || ba == NULL) {
        printf("  SKIP: in-process compile unavailable\n");
        skipped_real_snapshot = 1;
        if (ab) snapshot_release(ab);
        if (ba) snapshot_release(ba);
        return;
    }

    ASSERT(ab->yy_action != NULL && ab->yy_default != NULL &&
           ab->yy_rule_info_nrhs != NULL,
           "compiled snapshot should carry real action tables");

    /* "A B EOF": valid for G_AB, invalid for G_BA at the very first
    ** token. */
    const int toks_ab[3] = { 1, 2, 0 };
    ParseForkStatus sa, sb;
    ASSERT(run_two_fork_race(ab, ba, toks_ab, 3, &sa, &sb),
           "two-fork race should build");

    printf("  feeding 'A B EOF': AB-fork status=%d BA-fork status=%d\n",
           (int)sa, (int)sb);
    ASSERT(sa == FORK_COMPLETED,
           "the AB fork (right grammar) must COMPLETE on 'A B EOF'");
    ASSERT(sb == FORK_FAILED,
           "the BA fork (wrong grammar) must FAIL on 'A B EOF' -- "
           "impossible with the old accept-everything stub");

    /* Now the mirror image: "B A EOF" is valid for G_BA only.  Proves
    ** the outcome tracks the input, not a fixed fork ordering. */
    const int toks_ba[3] = { 2, 1, 0 };
    ASSERT(run_two_fork_race(ab, ba, toks_ba, 3, &sa, &sb),
           "mirrored two-fork race should build");

    printf("  feeding 'B A EOF': AB-fork status=%d BA-fork status=%d\n",
           (int)sa, (int)sb);
    ASSERT(sa == FORK_FAILED,
           "the AB fork (wrong grammar) must FAIL on 'B A EOF'");
    ASSERT(sb == FORK_COMPLETED,
           "the BA fork (right grammar) must COMPLETE on 'B A EOF'");

    snapshot_release(ab);
    snapshot_release(ba);
}

/*
** The surviving fork must be the one the tokens chose, even when the
** loser has the better priority number.  This is the property the stub
** silently destroyed: with every fork completing, fork_resolve_evaluate
** always fell through to a pure priority comparison.
*/
static void test_real_stepping_beats_priority(void) {
    printf("test_real_stepping_beats_priority\n");

    ParserSnapshot *ab = compile_snapshot(G_AB);
    ParserSnapshot *ba = compile_snapshot(G_BA);
    if (ab == NULL || ba == NULL) {
        printf("  SKIP: in-process compile unavailable\n");
        skipped_real_snapshot = 1;
        if (ab) snapshot_release(ab);
        if (ba) snapshot_release(ba);
        return;
    }

    MockParserFR parser;
    memset(&parser, 0, sizeof(parser));
    parser.yystack = parser.yystk0;
    parser.yytos = &parser.yystk0[0];
    parser.yystackEnd = &parser.yystk0[MOCK_STACK_DEPTH_FR - 1];
    parser.yyerrcnt = -1;

    ParserSnapshot *snaps[2] = { ab, ba };

    ConflictPoint cp;
    conflict_point_init(&cp, 1, 0, CONFLICT_LEVEL_TOKEN);

    LimeContext ca = {0};
    ca.ext_id = 1; ca.priority = 99; ca.grammar_name = "AB";
    conflict_point_add_context(&cp, &ca);

    LimeContext cb = {0};
    cb.ext_id = 2; cb.priority = 1; cb.grammar_name = "BA";
    conflict_point_add_context(&cp, &cb);

    ParseForkSet *fset = fork_resolve_create_forks(
        &cp, &parser,
        sizeof(MockParserFR), sizeof(MockStackEntryFR),
        offsetof(MockParserFR, yystk0), MOCK_STACK_DEPTH_FR,
        offsetof(MockParserFR, yystack),
        offsetof(MockParserFR, yytos),
        offsetof(MockParserFR, yystackEnd),
        snaps, 0);
    ASSERT(fset != NULL && fset->count == 2, "two forks should be created");
    if (fset == NULL) { conflict_point_destroy(&cp); goto done; }

    /* "A B EOF" -- only the AB grammar parses it, but the BA fork has
    ** the far better priority (1 vs 99). */
    const int toks[3] = { 1, 2, 0 };
    fork_resolve_feed_tokens(fset, toks, 3);

    int best = fork_resolve_evaluate(fset, TEST_TIEBREAK_PRIORITY);
    ASSERT(best >= 0, "a winner should be found");
    if (best >= 0) {
        printf("  winner priority=%d (loser had the better number)\n",
               fset->forks[best]->priority);
        ASSERT(fset->forks[best]->priority == 99,
               "the fork whose grammar actually parsed the input must win "
               "even though the other fork has the better priority -- "
               "with the old stub the priority-1 fork won unconditionally");
    }

    parse_fork_set_destroy(fset);
    conflict_point_destroy(&cp);

done:
    snapshot_release(ab);
    snapshot_release(ba);
}

/*
** Real-mode forks must not be declared complete before they reach
** accept.  Feeding only a valid PREFIX ("A", no B, no EOF) leaves the
** AB fork mid-parse.  fork_resolve_feed_tokens sweeps leftover RUNNING
** forks to COMPLETED at the end (its documented behaviour), so this
** checks the per-token predicate directly: the fork survives the
** prefix without failing, and did not reach accept from one token.
*/
static void test_real_stepping_prefix_not_accept(void) {
    printf("test_real_stepping_prefix_not_accept\n");

    ParserSnapshot *ab = compile_snapshot(G_AB);
    if (ab == NULL) {
        printf("  SKIP: in-process compile unavailable\n");
        skipped_real_snapshot = 1;
        return;
    }

    MockParserFR parser;
    memset(&parser, 0, sizeof(parser));
    parser.yystack = parser.yystk0;
    parser.yytos = &parser.yystk0[0];
    parser.yystackEnd = &parser.yystk0[MOCK_STACK_DEPTH_FR - 1];
    parser.yyerrcnt = -1;

    ParseFork *fork = fork_parser(&parser,
        sizeof(MockParserFR), sizeof(MockStackEntryFR),
        offsetof(MockParserFR, yystk0), MOCK_STACK_DEPTH_FR,
        offsetof(MockParserFR, yystack),
        offsetof(MockParserFR, yytos),
        offsetof(MockParserFR, yystackEnd),
        ab, 7);
    ASSERT(fork != NULL, "fork should be created");
    if (fork == NULL) { snapshot_release(ab); return; }

    ParseForkSet *fset = parse_fork_set_create(0);
    ASSERT(fset != NULL, "fork set should be created");
    if (fset == NULL) { free_parse_fork(fork); snapshot_release(ab); return; }
    ASSERT(parse_fork_set_add(fset, fork), "fork should be added");

    /* Only "A": a valid prefix but not a complete sentence. */
    const int prefix[1] = { 1 };
    fork_resolve_feed_tokens(fset, prefix, 1);

    ASSERT(fset->count == 1,
           "a valid prefix must not fail the fork (it would be pruned)");
    if (fset->count == 1) {
        ASSERT(fset->forks[0]->tokens_consumed == 1,
               "exactly one token should have been consumed");
        ASSERT(fset->forks[0]->status != FORK_FAILED,
               "valid prefix should leave the fork alive, not failed");
    }

    parse_fork_set_destroy(fset);
    snapshot_release(ab);
}

/*
** Static mode must stay permissive: table-less snapshots have nothing
** to step against, so every token is accepted and every fork that
** consumed a token reports complete.  This is what the pre-existing
** sub-tests above depend on; asserting it here makes the contract
** explicit rather than incidental.
*/
static void test_static_mode_still_permissive(void) {
    printf("test_static_mode_still_permissive\n");

    MockParserFR parser;
    memset(&parser, 0, sizeof(parser));
    parser.yystack = parser.yystk0;
    parser.yytos = &parser.yystk0[0];
    parser.yystackEnd = &parser.yystk0[MOCK_STACK_DEPTH_FR - 1];
    parser.yyerrcnt = -1;

    /* Table-less snapshots: exactly what create_test_snapshot() makes
    ** and what the lightweight path in fork_resolve_resolve() produces. */
    ParserSnapshot *snaps[2];
    snaps[0] = create_test_snapshot();
    snaps[1] = create_test_snapshot();

    ConflictPoint cp;
    conflict_point_init(&cp, 1, 0, CONFLICT_LEVEL_TOKEN);
    LimeContext c1 = {0};
    c1.ext_id = 1; c1.priority = 4; c1.grammar_name = "s1";
    conflict_point_add_context(&cp, &c1);
    LimeContext c2 = {0};
    c2.ext_id = 2; c2.priority = 9; c2.grammar_name = "s2";
    conflict_point_add_context(&cp, &c2);

    ParseForkSet *fset = fork_resolve_create_forks(
        &cp, &parser,
        sizeof(MockParserFR), sizeof(MockStackEntryFR),
        offsetof(MockParserFR, yystk0), MOCK_STACK_DEPTH_FR,
        offsetof(MockParserFR, yystack),
        offsetof(MockParserFR, yytos),
        offsetof(MockParserFR, yystackEnd),
        snaps, 0);
    ASSERT(fset != NULL && fset->count == 2, "two static forks created");

    if (fset != NULL) {
        /* Nonsense token codes that no grammar could accept. */
        const int junk[3] = { 4242, 7, 999 };
        fork_resolve_feed_tokens(fset, junk, 3);

        ASSERT(fset->count == 2,
               "static-mode forks must survive arbitrary tokens "
               "(nothing to validate against)");
        for (uint32_t i = 0; i < fset->count; i++) {
            ASSERT(fset->forks[i]->status == FORK_COMPLETED,
                   "static-mode forks should report complete");
            /* Static mode reports complete after the FIRST token, and
            ** fork_resolve_feed_tokens then skips already-completed
            ** forks -- so the remaining junk tokens are never offered.
            ** Pre-existing behaviour, preserved. */
            ASSERT(fset->forks[i]->tokens_consumed == 1,
                   "static-mode fork completes on its first token");
            ASSERT(fset->forks[i]->error_count == 0,
                   "static-mode forks never record errors");
        }

        int best = fork_resolve_evaluate(fset, TEST_TIEBREAK_PRIORITY);
        ASSERT(best >= 0 && fset->forks[best]->priority == 4,
               "static mode falls back to priority ordering");

        parse_fork_set_destroy(fset);
    }

    conflict_point_destroy(&cp);
    snapshot_release(snaps[0]);
    snapshot_release(snaps[1]);
}

/* ------------------------------------------------------------------ */
/*  Test: seeded resume from the conflict point != restart at state 0  */
/* ------------------------------------------------------------------ */

/*
** Two grammars that agree on the prefix "A B" and disagree only on
** what may follow it.  Token codes are assigned in declaration order,
** identically in both, so the same integer stream means different
** things to the two automata:
**
**   A=1 B=2 D=3 E=4, EOF=0
**
**   G_SEED_D:  s ::= t D.   t ::= A B.     -- "A B D"
**   G_SEED_E:  s ::= t E.   t ::= A B.     -- "A B E"
**
** The forked parser is placed mid-statement: its stack is exactly what
** a real parser holds after shifting A then B, i.e. [0, sA, sAB].  The
** lookahead fed to the forks is the single token "D" followed by EOF.
**
** That lookahead is:
**   - a valid CONTINUATION for G_SEED_D from the captured stack, and
**   - NOT a valid standalone parse from state 0 in EITHER grammar
**     (both require an A first).
**
** So the two behaviours give opposite verdicts, which is what makes
** this a real test of seeded resume rather than decoration:
**
**   restart-at-state-0 : D is rejected in state 0 -> BOTH forks fail
**                        -> no winner at all.
**   seeded resume      : the D-grammar fork continues and reaches
**                        accept; the E-grammar fork rejects D.
**
** Note the captured top-of-stack is a SHIFTREDUCE-encoded entry
** (t ::= A B is reduced as the second half of the B shift), so this
** also exercises carrying that encoding across the translation.
*/
static const char *G_SEED_D =
    "%name ForkResolveSeedD\n"
    "%token A B D E.\n"
    "%start_symbol s\n"
    "s ::= t D.\n"
    "t ::= A B.\n";

static const char *G_SEED_E =
    "%name ForkResolveSeedE\n"
    "%token A B D E.\n"
    "%start_symbol s\n"
    "s ::= t E.\n"
    "t ::= A B.\n";

/*
** Return the stack entry value a real generated parser would leave in
** `stateno` after shifting `token` from `from_state`: the raw action,
** re-encoded as a pending reduce when it is a SHIFTREDUCE (mirrors
** yy_shift() in limpar.c and parse_engine_step()'s own push).
*/
static uint16_t shifted_stateno(const ParserSnapshot *snap, uint16_t from_state, uint16_t token) {
    uint16_t a = snap_find_shift_action(snap, from_state, token);
    if (a > snap->yy_max_shift && a <= snap->yy_max_shiftreduce) {
        a = (uint16_t)(a + (snap->yy_min_reduce - snap->yy_min_shiftreduce));
    }
    return a;
}

/*
** Build a MockParserFR whose stack is genuinely mid-statement: the
** state-0 sentinel, then the states reached by shifting A and then B.
** Returns the resulting depth, or 0 if the grammar's tables don't give
** plain shift actions for that prefix (which would mean the fixture,
** not the code under test, is wrong).
*/
static uint32_t build_midstatement_parser(MockParserFR *p, const ParserSnapshot *snap) {
    memset(p, 0, sizeof(*p));
    p->yystack = p->yystk0;
    p->yystackEnd = &p->yystk0[MOCK_STACK_DEPTH_FR - 1];
    p->yyerrcnt = -1;

    uint16_t s_a = shifted_stateno(snap, 0, 1);  /* shift A from state 0 */
    if (s_a > snap->yy_max_shift) return 0;      /* expected a plain state */
    uint16_t s_ab = shifted_stateno(snap, s_a, 2); /* shift B from there  */

    p->yystk0[0].stateno = 0;
    p->yystk0[0].major = 0;
    p->yystk0[1].stateno = s_a;
    p->yystk0[1].major = 1;
    p->yystk0[2].stateno = s_ab;
    p->yystk0[2].major = 2;
    p->yytos = &p->yystk0[2];
    return 3;
}

static void test_seeded_resume_beats_state0(void) {
    printf("test_seeded_resume_beats_state0\n");

    ParserSnapshot *gd = compile_snapshot(G_SEED_D);
    ParserSnapshot *ge = compile_snapshot(G_SEED_E);
    if (gd == NULL || ge == NULL) {
        printf("  SKIP: in-process compile unavailable\n");
        skipped_real_snapshot = 1;
        if (gd) snapshot_release(gd);
        if (ge) snapshot_release(ge);
        return;
    }

    MockParserFR parser;
    uint32_t depth = build_midstatement_parser(&parser, gd);
    ASSERT(depth == 3, "mid-statement fixture stack should be 3 deep");
    if (depth != 3) goto done;

    printf("  captured mid-statement stack: [%u, %u, %u] "
           "(top %s a SHIFTREDUCE encoding, min_reduce=%u)\n",
           parser.yystk0[0].stateno, parser.yystk0[1].stateno, parser.yystk0[2].stateno,
           parser.yystk0[2].stateno >= gd->yy_min_reduce ? "IS" : "is not",
           gd->yy_min_reduce);

    /* Both grammars number states the same way here, so the same
    ** captured stack is meaningful to both forks. */
    ASSERT(shifted_stateno(ge, 0, 1) == parser.yystk0[1].stateno &&
           shifted_stateno(ge, shifted_stateno(ge, 0, 1), 2) == parser.yystk0[2].stateno,
           "both grammars must share the prefix's state numbering for the "
           "captured stack to mean the same thing to both forks");

    ParserSnapshot *snaps[2] = { gd, ge };

    ConflictPoint cp;
    conflict_point_init(&cp, 3, parser.yystk0[2].stateno, CONFLICT_LEVEL_TOKEN);

    /* The D-grammar gets the WORSE priority number, so a pass cannot be
    ** explained by priority ordering. */
    LimeContext cd = {0};
    cd.ext_id = 1; cd.priority = 77; cd.grammar_name = "seedD";
    conflict_point_add_context(&cp, &cd);

    LimeContext ce = {0};
    ce.ext_id = 2; ce.priority = 2; ce.grammar_name = "seedE";
    conflict_point_add_context(&cp, &ce);

    /* Continuation-only lookahead: "D EOF".  Meaningless as a parse
    ** from state 0 in either grammar. */
    const int toks[2] = { 3, 0 };

    /* --- Leg 1: the OLD behaviour, reproduced exactly ---------------
    ** fork_resolve_create_forks() supplies no entry layout, so the
    ** forks' stepping contexts start at state 0.  Recording it here
    ** rather than asserting from memory makes the comparison below a
    ** measurement, not an assumption. */
    ParseForkSet *unseeded = fork_resolve_create_forks(
        &cp, &parser,
        sizeof(MockParserFR), sizeof(MockStackEntryFR),
        offsetof(MockParserFR, yystk0), MOCK_STACK_DEPTH_FR,
        offsetof(MockParserFR, yystack),
        offsetof(MockParserFR, yytos),
        offsetof(MockParserFR, yystackEnd),
        snaps, 0);
    ASSERT(unseeded != NULL && unseeded->count == 2, "unseeded forks created");

    int unseeded_winner = -1;
    if (unseeded != NULL) {
        fork_resolve_feed_tokens(unseeded, toks, 2);
        unseeded_winner = fork_resolve_evaluate(unseeded, TEST_TIEBREAK_PRIORITY);
        printf("  restart-at-state-0: %u fork(s) survived, winner index %d\n",
               unseeded->count, unseeded_winner);
        ASSERT(unseeded_winner < 0,
               "restarting at state 0 must find NO winner: 'D' is not a valid "
               "first token in either grammar, so every fork fails");
        parse_fork_set_destroy(unseeded);
    }

    /* --- Leg 2: seeded resume from the captured stack --------------- */
    ParseForkSet *seeded = fork_resolve_create_forks_seedable(
        &cp, &parser,
        sizeof(MockParserFR), sizeof(MockStackEntryFR),
        offsetof(MockParserFR, yystk0), MOCK_STACK_DEPTH_FR,
        offsetof(MockParserFR, yystack),
        offsetof(MockParserFR, yytos),
        offsetof(MockParserFR, yystackEnd),
        offsetof(MockStackEntryFR, stateno),
        sizeof(((MockStackEntryFR *)0)->stateno),
        snaps, 0);
    ASSERT(seeded != NULL && seeded->count == 2, "seeded forks created");

    if (seeded != NULL) {
        fork_resolve_feed_tokens(seeded, toks, 2);

        ParseForkStatus sd = FORK_FAILED; /* pruned out of the set == failed */
        ParseForkStatus se = FORK_FAILED;
        for (uint32_t i = 0; i < seeded->count; i++) {
            if (seeded->forks[i]->priority == 77) sd = seeded->forks[i]->status;
            if (seeded->forks[i]->priority == 2) se = seeded->forks[i]->status;
        }
        printf("  seeded resume:      D-fork status=%d E-fork status=%d\n",
               (int)sd, (int)se);

        ASSERT(sd == FORK_COMPLETED,
               "seeded: the D-grammar fork must COMPLETE -- 'D EOF' is a valid "
               "CONTINUATION of the captured 'A B' stack, which is only "
               "reachable by resuming from that stack");
        ASSERT(se == FORK_FAILED,
               "seeded: the E-grammar fork must FAIL -- it wants E, not D");

        int best = fork_resolve_evaluate(seeded, TEST_TIEBREAK_PRIORITY);
        ASSERT(best >= 0,
               "seeded resume must find a winner where restart-at-state-0 "
               "found none -- this is the behavioural difference");
        if (best >= 0) {
            printf("  seeded winner priority=%d (loser had the better number)\n",
                   seeded->forks[best]->priority);
            ASSERT(seeded->forks[best]->priority == 77,
                   "the fork whose grammar continues the captured parse must "
                   "win despite the worse priority number");
        }

        parse_fork_set_destroy(seeded);
    }

    conflict_point_destroy(&cp);

done:
    snapshot_release(gd);
    snapshot_release(ge);
}

/*
** A seeded fork whose captured stack carries a state its snapshot
** cannot decode must be refused, not silently stepped against a bogus
** stack.  The strategy then leaves the context at state 0 (the old
** path), so the fork still behaves like a valid parser -- it just
** answers the from-scratch question.
*/
static void test_seed_rejects_untranslatable_stack(void) {
    printf("test_seed_rejects_untranslatable_stack\n");

    ParserSnapshot *gd = compile_snapshot(G_SEED_D);
    if (gd == NULL) {
        printf("  SKIP: in-process compile unavailable\n");
        skipped_real_snapshot = 1;
        return;
    }

    ParseContext *ctx = parse_begin(gd);
    ASSERT(ctx != NULL, "parse context should be created");
    if (ctx == NULL) { snapshot_release(gd); return; }

    /* A value that is neither a plain state (<= yy_max_shift) nor a
    ** decodable pending reduce (>= yy_min_reduce, ruleno < nrule). */
    uint16_t bogus[2] = { 0, (uint16_t)(gd->yy_min_reduce - 1) };
    ASSERT(bogus[1] > gd->yy_max_shift, "fixture value must not be a plain state");
    ASSERT(parse_engine_seed_stack(ctx, bogus, NULL, 2) != 0,
           "an undecodable stateno must be refused rather than installed");

    /* A reduce encoding past the end of the rule table. */
    uint16_t past_end[2] = { 0, (uint16_t)(gd->yy_min_reduce + gd->nrule) };
    ASSERT(parse_engine_seed_stack(ctx, past_end, NULL, 2) != 0,
           "a pending reduce for a nonexistent rule must be refused");

    /* Degenerate inputs. */
    uint16_t ok_stack[1] = { 0 };
    ASSERT(parse_engine_seed_stack(ctx, NULL, NULL, 1) != 0, "NULL states refused");
    ASSERT(parse_engine_seed_stack(ctx, ok_stack, NULL, 0) != 0, "zero-length refused");
    ASSERT(parse_engine_seed_stack(NULL, ok_stack, NULL, 1) != 0, "NULL ctx refused");

    /* Refusal left the context untouched, so a legitimate seed still
    ** works and stepping proceeds from it. */
    ASSERT(parse_engine_seed_stack(ctx, ok_stack, NULL, 1) == 0,
           "a valid seed must still succeed after refusals");
    ASSERT(parse_token(ctx, 1, NULL, LIME_LOC_UNKNOWN) == 0,
           "seeded-at-state-0 context should accept 'A'");
    /* Already stepping: a second seed must be refused. */
    ASSERT(parse_engine_seed_stack(ctx, ok_stack, NULL, 1) != 0,
           "re-seeding a context that has consumed a token must be refused");

    parse_end(ctx);
    snapshot_release(gd);
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
    test_real_stepping_wrong_grammar_fails();
    test_real_stepping_beats_priority();
    test_real_stepping_prefix_not_accept();
    test_static_mode_still_permissive();
    test_seeded_resume_beats_state0();
    test_seed_rejects_untranslatable_stack();

    printf("\n%d tests, %d failures\n", test_count, fail_count);
    if (fail_count > 0) return 1;
    /* Matching the suite convention: exit 77 when the real-snapshot
    ** coverage could not run because the in-process compiler is
    ** unavailable, so the result is reported as a skip, not a pass. */
    if (skipped_real_snapshot) return 77;
    return 0;
}
