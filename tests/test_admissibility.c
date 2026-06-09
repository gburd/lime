/*
** tests/test_admissibility.c -- regression for the context-sensitive
** token-admissibility oracle that backs multi-grammar keyword
** disambiguation (lime_token_admissible_in_state +
** parse_context_current_state).
**
** The arithmetic grammar (bench_arith_grammar.y) is a clean state-
** gating bed: a token's admissibility flips with parser state, which
** is the exact property QUEL/SQL keyword collision resolution relies
** on.  At the start state an expression can begin with NUM or '(' but
** NOT with a binary operator; after shifting a NUM the situation
** inverts -- a binary operator is now admissible and a second NUM is
** not.  We assert the oracle reports both.
**
** Sub-tests:
**   1. Start state: NUM / LP admissible; PLUS / STAR / RP not.
**   2. After shifting NUM: PLUS / STAR admissible; NUM / LP not.
**   3. EOF (code 0) and accept classification.
**   4. Out-of-range / NULL / LIME_NO_STATE edge cases.
**   5. parse_context_current_state: LIME_NO_STATE pre-parse, state 0
**      on first token, changes after a shift.
*/
#include "parser.h"
#include "parse_context.h"
#include "snapshot.h"

#include "bench_arith_grammar.h"

#include <stdio.h>
#include <stdlib.h>

extern ParserSnapshot *ArithBuildSnapshot(void);

static int n_pass = 0;
static int n_fail = 0;
#define CHECK(cond, name)                                              \
    do {                                                               \
        if (cond) { printf("  [PASS] %s\n", name); n_pass++; }         \
        else { printf("  [FAIL] %s (%s:%d)\n", name, __FILE__,         \
                       __LINE__); n_fail++; }                          \
    } while (0)

static int admissible(LimeTokenAdmissibility a) {
    return a != LIME_TOK_NONE;
}

int main(void) {
    ParserSnapshot *snap = ArithBuildSnapshot();
    if (snap == NULL) {
        fprintf(stderr, "ArithBuildSnapshot returned NULL\n");
        return 2;
    }

    /* --- 1. Start state (the LR start state is 0) ---------------- */
    uint16_t s0 = 0;
    CHECK(admissible(lime_token_admissible_in_state(snap, s0, ARITH_NUM)),
          "start: NUM admissible");
    CHECK(admissible(lime_token_admissible_in_state(snap, s0, ARITH_LP)),
          "start: LP admissible");
    CHECK(!admissible(lime_token_admissible_in_state(snap, s0, ARITH_PLUS)),
          "start: PLUS NOT admissible");
    CHECK(!admissible(lime_token_admissible_in_state(snap, s0, ARITH_STAR)),
          "start: STAR NOT admissible");
    CHECK(!admissible(lime_token_admissible_in_state(snap, s0, ARITH_RP)),
          "start: RP NOT admissible");

    /* --- 2. After shifting a NUM, query via the lookahead-correct
    **        oracle (parse_context_token_admissible).  After NUM the
    **        raw stack top is a pending shift-reduce, so we must NOT
    **        hand-interpret current_state -- the engine-replaying
    **        oracle resolves it. ----------------------------------- */
    {
        ParseContext *ctx = parse_begin(snap);
        CHECK(ctx != NULL, "parse_begin");

        /* Before any token: no live state -> treat-as-admissible. */
        CHECK(parse_context_current_state(ctx) == LIME_NO_STATE,
              "pre-parse: current_state == LIME_NO_STATE");
        CHECK(parse_context_token_admissible(ctx, ARITH_NUM) == LIME_TOK_SHIFT,
              "pre-parse: token admissible (treat-as-shift)");

        int rc = parse_token(ctx, ARITH_NUM, NULL, -1);
        CHECK(rc == 0, "shift NUM ok");

        /* In the post-NUM configuration a binary operator is
        ** admissible (NUM reduces to factor/term/expr, then the
        ** operator shifts) but a second NUM or LP is not -- the
        ** admissibility has flipped, exactly the QUEL/SQL collision
        ** behaviour. */
        CHECK(admissible(parse_context_token_admissible(ctx, ARITH_PLUS)),
              "post-NUM: PLUS admissible");
        CHECK(admissible(parse_context_token_admissible(ctx, ARITH_STAR)),
              "post-NUM: STAR admissible");
        CHECK(!admissible(parse_context_token_admissible(ctx, ARITH_NUM)),
              "post-NUM: second NUM NOT admissible");
        CHECK(!admissible(parse_context_token_admissible(ctx, ARITH_LP)),
              "post-NUM: LP NOT admissible");
        /* EOF reduces NUM all the way up and accepts. */
        CHECK(admissible(parse_context_token_admissible(ctx, 0)),
              "post-NUM: EOF admissible (reduce/accept path exists)");
        /* Out-of-range token code is inadmissible. */
        CHECK(parse_context_token_admissible(ctx, 0x7fff) == LIME_TOK_NONE,
              "post-NUM: out-of-range token NOT admissible");

        /* The oracle agrees with the engine: feeding PLUS succeeds. */
        rc = parse_token(ctx, ARITH_PLUS, NULL, -1);
        CHECK(rc == 0, "engine agrees: PLUS shifts after NUM");

        /* After PLUS, a NUM/LP is admissible again (RHS of operator)
        ** but a second operator is not. */
        CHECK(admissible(parse_context_token_admissible(ctx, ARITH_NUM)),
              "post-PLUS: NUM admissible again");
        CHECK(!admissible(parse_context_token_admissible(ctx, ARITH_PLUS)),
              "post-PLUS: second PLUS NOT admissible");

        parse_end(ctx);
    }

    /* --- 3. Edge cases ------------------------------------------- */
    CHECK(parse_context_token_admissible(NULL, ARITH_NUM) == LIME_TOK_SHIFT,
          "token_admissible(NULL ctx) -> SHIFT (no veto)");
    CHECK(lime_token_admissible_in_state(NULL, s0, ARITH_NUM) == LIME_TOK_NONE,
          "NULL snapshot -> NONE");
    CHECK(lime_token_admissible_in_state(snap, LIME_NO_STATE, ARITH_NUM) == LIME_TOK_SHIFT,
          "LIME_NO_STATE -> SHIFT (treat-as-admissible)");
    CHECK(lime_token_admissible_in_state(snap, (uint16_t)(snap->nstate + 100), ARITH_NUM)
              == LIME_TOK_NONE,
          "out-of-range state -> NONE");
    CHECK(lime_token_admissible_in_state(snap, s0, 0x7fff) == LIME_TOK_NONE,
          "out-of-range token code -> NONE");
    CHECK(parse_context_current_state(NULL) == LIME_NO_STATE,
          "current_state(NULL) -> LIME_NO_STATE");

    snapshot_release(snap);

    printf("\n%d passed, %d failed\n", n_pass, n_fail);
    return n_fail == 0 ? 0 : 1;
}
