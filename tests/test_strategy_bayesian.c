/*
** test_strategy_bayesian.c -- focused tests for STRAT_BAYESIAN.
**
** Verifies the Beta-Bernoulli posterior actually learns from
** disambiguation_update() feedback:
**
**   1. Cold start (no evidence): both arms have equal posterior
**      (mean = 0.5).  resolve() picks deterministically by ext_id
**      tiebreak.
**
**   2. After biased feedback: the arm fed only successes pulls
**      ahead; the arm fed only failures falls behind.  resolve()
**      switches to the better arm.
**
**   3. Independence across (token, state, ext_id): updates on one
**      conflict point do not bleed into a different one.
**
**   4. Confidence value: matches posterior mean = alpha / (alpha + beta).
**
** Mocking strategy: fabricate ConflictPoints by hand.  We do not
** drive a real parse; we drive disambiguation_resolve() +
** disambiguation_update() directly, asserting the in-memory
** posterior evolves as expected.
*/

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "conflict.h"
#include "disambiguation.h"
#include "extension.h"

static int test_count = 0;
static int fail_count = 0;

#define ASSERT(cond, msg)                                                                          \
    do {                                                                                           \
        test_count++;                                                                              \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "  FAIL [%d]: %s (%s:%d)\n", test_count, msg, __FILE__, __LINE__);     \
            fail_count++;                                                                          \
        }                                                                                          \
    } while (0)

/* Soft equality for float means (3 d.p.). */
static bool near(float a, float b) {
    return fabsf(a - b) < 0.001f;
}

static bool mock_get_mods(void *u, const struct ParserSnapshot *b, GrammarModification **m,
                          uint32_t *n) {
    (void)u;
    (void)b;
    *m = NULL;
    *n = 0;
    return true;
}

static ExtensionRegistry *make_registry(ExtensionID *id_a, ExtensionID *id_b) {
    ExtensionRegistry *reg = create_extension_registry();
    ExtensionInfo info_a = {.name = "ext-a", .version = "1", .get_modifications = mock_get_mods};
    ExtensionInfo info_b = {.name = "ext-b", .version = "1", .get_modifications = mock_get_mods};
    register_extension(reg, &info_a, id_a);
    register_extension(reg, &info_b, id_b);
    load_extension(reg, *id_a, NULL, NULL);
    load_extension(reg, *id_b, NULL, NULL);
    return reg;
}

/* Build a binary-conflict point: token T, state S, two contexts. */
static ConflictPoint make_conflict(uint16_t token, int state, ExtensionID a, ExtensionID b) {
    static LimeContext storage[8][2];
    static int slot = 0;
    LimeContext *cx = storage[slot++ % 8];
    cx[0].ext_id = a;
    cx[0].token = token;
    cx[0].state = state;
    cx[0].priority = 0;
    cx[0].grammar_name = "ext-a";
    cx[1].ext_id = b;
    cx[1].token = token;
    cx[1].state = state;
    cx[1].priority = 0;
    cx[1].grammar_name = "ext-b";
    ConflictPoint cp = {0};
    cp.token = token;
    cp.state = state;
    cp.level = CONFLICT_LEVEL_RULE;
    cp.contexts = cx;
    cp.ncontexts = 2;
    cp.capacity = 2;
    cp.description = NULL;
    return cp;
}

/* ------------------------------------------------------------------ */
/*  Test 1: cold start has uniform posterior (mean = 0.5)              */
/* ------------------------------------------------------------------ */

static void test_cold_start(void) {
    printf("test_cold_start\n");
    ExtensionID a, b;
    ExtensionRegistry *reg = make_registry(&a, &b);

    DisambiguationContext *ctx = disambiguation_create(STRAT_BAYESIAN, reg);
    ASSERT(ctx != NULL, "create STRAT_BAYESIAN");

    ConflictPoint cp = make_conflict(/*token=*/100, /*state=*/5, a, b);
    StrategyResult r = disambiguation_resolve(ctx, &cp, NULL);
    ASSERT(r.nwinners == 1, "single winner");
    ASSERT(near(r.confidence, 0.5f), "cold-start confidence = 0.5");
    strategy_result_cleanup(&r);
    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test 2: feedback updates the posterior mean as expected            */
/* ------------------------------------------------------------------ */

static void test_biased_feedback(void) {
    printf("test_biased_feedback\n");
    ExtensionID a, b;
    ExtensionRegistry *reg = make_registry(&a, &b);
    DisambiguationContext *ctx = disambiguation_create(STRAT_BAYESIAN, reg);

    ConflictPoint cp = make_conflict(100, 5, a, b);

    /* Feed 10 successes to whichever arm wins each round.  Both arms
    ** start at posterior 0.5; ties break by ext_id (lower wins).
    ** ext-a is registered first, so ext-a wins all 10 rounds and
    ** absorbs all 10 success credits. */
    for (int i = 0; i < 10; i++) {
        StrategyResult r = disambiguation_resolve(ctx, &cp, NULL);
        ASSERT(r.winning_contexts[0].ext_id == a, "ext-a wins by tiebreak");
        strategy_result_cleanup(&r);
        disambiguation_update(ctx, true);
    }

    /* After 10 successes the chosen arm has alpha = 1 + 10 = 11,
    ** beta = 1.  Posterior mean = 11/12 = 0.9166... */
    StrategyResult r = disambiguation_resolve(ctx, &cp, NULL);
    ASSERT(r.winning_contexts[0].ext_id == a, "ext-a still wins after success run");
    ASSERT(near(r.confidence, 11.0f / 12.0f), "ext-a confidence = 11/12 after 10 successes");
    strategy_result_cleanup(&r);

    /* Now feed 11 failures.  Each round, ext-a is still ahead of
    ** ext-b's prior 0.5 (until alpha_a / (alpha_a + beta_a) drops
    ** below 0.5), so each failure credits ext-a.  After enough
    ** failures ext-a's mean drops below ext-b's prior, the winner
    ** flips to ext-b, and *then* failures start crediting ext-b.
    **
    ** After 11 failures: ext-a alpha=11, beta=12, mean = 11/23 ~= 0.478.
    ** ext-b alpha=1, beta=1, mean = 0.5.  ext-b just edges ahead. */
    for (int i = 0; i < 11; i++) {
        StrategyResult r2 = disambiguation_resolve(ctx, &cp, NULL);
        strategy_result_cleanup(&r2);
        disambiguation_update(ctx, false);
    }

    StrategyResult r3 = disambiguation_resolve(ctx, &cp, NULL);
    ASSERT(r3.winning_contexts[0].ext_id == b, "ext-b takes over once ext-a falls below 0.5");
    ASSERT(near(r3.confidence, 0.5f), "ext-b confidence still at the 0.5 prior");
    strategy_result_cleanup(&r3);

    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test 3: posteriors are per-(token, state, ext_id), not global      */
/* ------------------------------------------------------------------ */

static void test_independence(void) {
    printf("test_independence\n");
    ExtensionID a, b;
    ExtensionRegistry *reg = make_registry(&a, &b);
    DisambiguationContext *ctx = disambiguation_create(STRAT_BAYESIAN, reg);

    ConflictPoint cp1 = make_conflict(100, 5, a, b);
    ConflictPoint cp2 = make_conflict(200, 7, a, b); /* different (token, state) */

    /* Train ext-a hard on cp1. */
    for (int i = 0; i < 10; i++) {
        StrategyResult r = disambiguation_resolve(ctx, &cp1, NULL);
        strategy_result_cleanup(&r);
        disambiguation_update(ctx, true);
    }

    /* cp2 should still be cold (mean 0.5). */
    StrategyResult r = disambiguation_resolve(ctx, &cp2, NULL);
    ASSERT(near(r.confidence, 0.5f), "cp2 remains cold despite cp1 training");
    strategy_result_cleanup(&r);

    /* And cp1 should remain trained. */
    StrategyResult r2 = disambiguation_resolve(ctx, &cp1, NULL);
    ASSERT(r2.confidence > 0.9f, "cp1 still strongly favors ext-a");
    strategy_result_cleanup(&r2);

    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test 4: posterior mean matches alpha / (alpha + beta)              */
/* ------------------------------------------------------------------ */

static void test_posterior_mean(void) {
    printf("test_posterior_mean\n");
    ExtensionID a, b;
    ExtensionRegistry *reg = make_registry(&a, &b);
    DisambiguationContext *ctx = disambiguation_create(STRAT_BAYESIAN, reg);

    ConflictPoint cp = make_conflict(100, 5, a, b);

    /* 3 successes, 1 failure on ext-a.  alpha = 1+3 = 4, beta = 1+1 = 2.
    ** mean = 4 / 6 = 0.6667. */
    for (int i = 0; i < 3; i++) {
        StrategyResult r = disambiguation_resolve(ctx, &cp, NULL);
        strategy_result_cleanup(&r);
        disambiguation_update(ctx, true);
    }
    {
        StrategyResult r = disambiguation_resolve(ctx, &cp, NULL);
        strategy_result_cleanup(&r);
        disambiguation_update(ctx, false);
    }

    StrategyResult r = disambiguation_resolve(ctx, &cp, NULL);
    ASSERT(near(r.confidence, 4.0f / 6.0f), "confidence matches alpha / (alpha + beta)");
    strategy_result_cleanup(&r);

    disambiguation_destroy(ctx);
    destroy_extension_registry(reg);
}

int main(void) {
    test_cold_start();
    test_biased_feedback();
    test_independence();
    test_posterior_mean();
    printf("\nbayesian: %d tests, %d failures\n", test_count, fail_count);
    return fail_count == 0 ? 0 : 1;
}
