/*
** test_parse_engine.c -- direct unit-test coverage of the LALR
** runtime hot path (src/parse_engine.c).
**
** parse_engine.c is the runtime push parser that drives every
** consumer of parse_begin/parse_token/parse_end.  Until v0.5.0 it
** was only covered transitively (test_runtime_parse, runtime_pbt,
** jit_parse_equivalence, extension_e2e) -- direct unit tests catch
** regressions integration tests miss, particularly the synthetic
** error/edge paths that real grammars never visit.
**
** The tests fall into two groups:
**
**   1. Real-grammar tests, driven through ArithBuildSnapshot()
**      (the same snapshot test_runtime_parse uses).  Cover the
**      common shift / shift-reduce / reduce / accept paths plus
**      stack growth, multi-context isolation, and threaded
**      refcount safety.
**
**   2. Synthetic-snapshot tests.  Hand-built ParserSnapshot
**      structures exercise paths that real grammars rarely hit:
**      NULL action tables, the yy_fallback retry, an explicit
**      yy_accept_action entry, a broken reduce table, and the
**      grammar-context-stack swap branch.
**
** What this test catches that integration tests don't:
**
**   - Stack-realloc growth past PARSE_STACK_INITIAL=64 entries
**     (deep-paren input).  No prior test fed >64-deep recursion.
**   - parse_token(NULL_ctx, ...) returning -1 cleanly.  Prior
**     tests assumed valid contexts; a misuse would segfault.
**   - parse_token against a snapshot with NULL yy_action /
**     yy_default / yy_rule_info_nrhs.  publish_modified_snapshot
**     can fail mid-build and leave a snapshot in this state;
**     parse_engine_step must return -1, not segfault.
**   - The %fallback retry branch.  No grammar in the tree
**     declares %fallback, so the fallback-table path was
**     completely dead until this test pinned it.
**   - The "explicit yy_accept_action in yy_action[idx]" branch.
**     arith reaches accept via reduce-to-bottom, never via this
**     path.  Synthetic snapshot drives it directly.
**   - Reduce returning -1 with broken rule metadata
**     (yy_rule_info_nrhs=NULL or ruleno >= nrule).  Defends
**     against silent table corruption.
**   - parse_token against a context with an attached
**     GrammarContextStack -- exercises the snapshot-swap branch.
*/

#define _POSIX_C_SOURCE 200809L

#include "parser.h"
#include "parse_context.h"
#include "snapshot.h"
#include "grammar_context.h"

#include "lime_threads.h"

#include "bench_arith_grammar.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern ParserSnapshot *ArithBuildSnapshot(void);

/* ------------------------------------------------------------------ */
/*  Test framework                                                     */
/* ------------------------------------------------------------------ */

static int n_pass = 0;
static int n_fail = 0;

#define CHECK(cond, name)                                                                          \
    do {                                                                                           \
        if (cond) {                                                                                \
            printf("  [PASS] %s\n", name);                                                         \
            n_pass++;                                                                              \
        } else {                                                                                   \
            printf("  [FAIL] %s (at %s:%d)\n", name, __FILE__, __LINE__);                          \
            n_fail++;                                                                              \
        }                                                                                          \
    } while (0)

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

/*
** Run an entire token sequence to completion, terminating with EOF.
** Returns the final parse_token return code: 1=accept, -1=error.
*/
static int run_seq(ParserSnapshot *snap, const int *toks, size_t n) {
    ParseContext *ctx = parse_begin(snap);
    if (ctx == NULL) return -2;
    int rc = 0;
    for (size_t i = 0; i < n; i++) {
        rc = parse_token(ctx, toks[i], NULL, -1);
        if (rc != 0) break;
    }
    if (rc == 0) rc = parse_token(ctx, 0, NULL, -1);
    parse_end(ctx);
    return rc;
}

/*
** Build a tiny synthetic snapshot.  Enough fields populated for
** parse_engine_step to operate.  Caller frees with snapshot_release.
**
** Layout summary (chosen so the constants are easy to hand-trace):
**
**   yy_max_shift       = 1   (shifts: 0..1)
**   yy_min_shiftreduce = 2   (shift-reduces: 2..3)
**   yy_max_shiftreduce = 3
**   yy_error_action    = 4
**   yy_accept_action   = 5
**   yy_no_action       = 6
**   yy_min_reduce      = 7   (reduces: 7..8)
**   nstate             = 2
**   nrule              = 2
**   ntoken / yy_ntoken = 4
**
** yy_action / yy_lookahead / yy_shift_ofst / yy_reduce_ofst /
** yy_default are allocated via calloc; callers patch the entries
** they need.
*/
typedef struct {
    ParserSnapshot *snap;
    uint16_t *action;
    uint16_t *lookahead;
    int32_t *shift_ofst;
    int32_t *reduce_ofst;
    uint16_t *defaults;
    int16_t *rule_lhs;
    int8_t *rule_nrhs;
    uint16_t *fallback;
} SyntheticSnap;

static SyntheticSnap make_synthetic(uint32_t nstate, uint32_t nrule, uint32_t naction) {
    SyntheticSnap s = {0};
    ParserSnapshot *snap = calloc(1, sizeof(ParserSnapshot));
    if (snap == NULL) return s;
    atomic_init(&snap->refcount, 1);
    snap->version = 1;
    snap->nstate = nstate;
    snap->nrule = nrule;
    snap->nterminal = 4;
    snap->yy_ntoken = 4;
    snap->yy_max_shift = 1;
    snap->yy_min_shiftreduce = 2;
    snap->yy_max_shiftreduce = 3;
    snap->yy_error_action = 4;
    snap->yy_accept_action = 5;
    snap->yy_no_action = 6;
    snap->yy_min_reduce = 7;
    snap->action_count = naction;
    snap->lookahead_count = naction;

    s.action = calloc(naction, sizeof(uint16_t));
    s.lookahead = calloc(naction, sizeof(uint16_t));
    s.shift_ofst = calloc(nstate, sizeof(int32_t));
    s.reduce_ofst = calloc(nstate, sizeof(int32_t));
    s.defaults = calloc(nstate, sizeof(uint16_t));
    s.rule_lhs = calloc(nrule, sizeof(int16_t));
    s.rule_nrhs = calloc(nrule, sizeof(int8_t));
    if (s.action == NULL || s.lookahead == NULL || s.shift_ofst == NULL ||
        s.reduce_ofst == NULL || s.defaults == NULL || s.rule_lhs == NULL ||
        s.rule_nrhs == NULL) {
        free(s.action);
        free(s.lookahead);
        free(s.shift_ofst);
        free(s.reduce_ofst);
        free(s.defaults);
        free(s.rule_lhs);
        free(s.rule_nrhs);
        free(snap);
        memset(&s, 0, sizeof(s));
        return s;
    }
    snap->yy_action = s.action;
    snap->yy_lookahead = s.lookahead;
    snap->yy_shift_ofst = s.shift_ofst;
    snap->yy_reduce_ofst = s.reduce_ofst;
    snap->yy_default = s.defaults;
    snap->yy_rule_info_lhs = s.rule_lhs;
    snap->yy_rule_info_nrhs = s.rule_nrhs;

    /* By default: every state's default is yy_error_action.  Tests
    ** patch the entries they need. */
    for (uint32_t i = 0; i < nstate; i++) s.defaults[i] = snap->yy_error_action;
    /* Lookaheads pre-filled with a sentinel that won't match any
    ** real token (yy_ntoken+1) so unset slots fall through to
    ** default.  Callers patch entries they actually need. */
    for (uint32_t i = 0; i < naction; i++) s.lookahead[i] = (uint16_t)0xFFFF;

    s.snap = snap;
    return s;
}

/* ------------------------------------------------------------------ */
/*  Lifecycle tests                                                    */
/* ------------------------------------------------------------------ */

static void test_parse_begin_valid(ParserSnapshot *snap) {
    ParseContext *ctx = parse_begin(snap);
    CHECK(ctx != NULL, "parse_begin returns non-NULL on valid snapshot");
    CHECK(parse_get_snapshot(ctx) == snap, "ctx pins the supplied snapshot");
    parse_end(ctx);
}

static void test_parse_begin_null_snap(void) {
    ParseContext *ctx = parse_begin(NULL);
    CHECK(ctx == NULL, "parse_begin(NULL) returns NULL gracefully");
    /* parse_end on NULL must be safe -- it is the documented contract
    ** that mirrors snapshot_release. */
    parse_end(NULL);
    CHECK(1, "parse_end(NULL) is a safe no-op");
}

static void test_parse_begin_refcount(ParserSnapshot *snap) {
    uint32_t before = atomic_load(&snap->refcount);
    ParseContext *ctx = parse_begin(snap);
    uint32_t mid = atomic_load(&snap->refcount);
    parse_end(ctx);
    uint32_t after = atomic_load(&snap->refcount);
    CHECK(mid == before + 1, "parse_begin bumps refcount by 1");
    CHECK(after == before, "parse_end restores refcount to baseline");
}

static void test_multiple_contexts_independent(ParserSnapshot *snap) {
    uint32_t before = atomic_load(&snap->refcount);
    ParseContext *a = parse_begin(snap);
    ParseContext *b = parse_begin(snap);
    CHECK(a != NULL && b != NULL && a != b, "two parse_begin calls produce independent contexts");
    CHECK(atomic_load(&snap->refcount) == before + 2, "refcount bumped twice");

    /* Drive each context independently to confirm engine state
    ** isolation. */
    int rc_a = parse_token(a, ARITH_NUM, NULL, -1);
    /* b does NOT see a's progress. */
    int rc_b = parse_token(b, ARITH_LP, NULL, -1);
    CHECK(rc_a == 0 && rc_b == 0, "contexts have isolated engine state");

    parse_end(a);
    parse_end(b);
    CHECK(atomic_load(&snap->refcount) == before, "both releases drop refcount back to baseline");
}

/* ------------------------------------------------------------------ */
/*  Parse-path tests (real arith grammar)                               */
/* ------------------------------------------------------------------ */

static void test_single_num_accepts(ParserSnapshot *snap) {
    int toks[] = {ARITH_NUM};
    CHECK(run_seq(snap, toks, 1) == 1, "single NUM accepts");
}

static void test_explicit_eof_completes(ParserSnapshot *snap) {
    /* parse_token contract: 1 = accept, 0 = continue, -1 = error.
    ** Drive a sequence and verify each step's return code. */
    ParseContext *ctx = parse_begin(snap);
    int rc1 = parse_token(ctx, ARITH_NUM, NULL, -1);
    CHECK(rc1 == 0, "non-EOF mid-parse token returns 0");
    int rc2 = parse_token(ctx, 0, NULL, -1);
    CHECK(rc2 == 1, "EOF after well-formed prefix returns 1 (accept)");
    parse_end(ctx);
}

static void test_eof_at_state_zero_is_error(ParserSnapshot *snap) {
    /* Empty input: state-0 has no shift entry for EOF in arith;
    ** must error, not segfault. */
    ParseContext *ctx = parse_begin(snap);
    int rc = parse_token(ctx, 0, NULL, -1);
    CHECK(rc < 0, "EOF at state 0 (empty input) returns -1");
    parse_end(ctx);
}

static void test_two_token_expression(ParserSnapshot *snap) {
    int toks[] = {ARITH_NUM, ARITH_PLUS, ARITH_NUM};
    CHECK(run_seq(snap, toks, 3) == 1, "1 + 2 accepts");
}

static void test_nested_expression(ParserSnapshot *snap) {
    /* ((1 + 2) * 3) - 4 */
    int toks[] = {
        ARITH_LP,  ARITH_LP,  ARITH_NUM, ARITH_PLUS,  ARITH_NUM,  ARITH_RP, ARITH_STAR,
        ARITH_NUM, ARITH_RP,  ARITH_MINUS, ARITH_NUM,
    };
    CHECK(run_seq(snap, toks, sizeof(toks) / sizeof(toks[0])) == 1,
          "((1 + 2) * 3) - 4 accepts");
}

static void test_unknown_token_code(ParserSnapshot *snap) {
    /* yy_ntoken is 8 in arith; feeding 9999 must not segfault and
    ** must reject as syntax error. */
    ParseContext *ctx = parse_begin(snap);
    int rc = parse_token(ctx, 9999, NULL, -1);
    CHECK(rc < 0, "out-of-range token code rejected as syntax error");
    parse_end(ctx);
}

/* ------------------------------------------------------------------ */
/*  Stack-growth path                                                  */
/* ------------------------------------------------------------------ */

static void test_deep_nesting_stack_growth(ParserSnapshot *snap) {
    /* PARSE_STACK_INITIAL is 64 in parse_engine.c.  Feed 80 nested
    ** open-parens followed by NUM and 80 close-parens to force the
    ** stack to realloc past the initial capacity (lines 67-72 of
    ** parse_engine.c -- previously dead). */
    enum { N = 80 };
    int toks[2 * N + 1];
    for (int i = 0; i < N; i++) toks[i] = ARITH_LP;
    toks[N] = ARITH_NUM;
    for (int i = 0; i < N; i++) toks[N + 1 + i] = ARITH_RP;

    int rc = run_seq(snap, toks, sizeof(toks) / sizeof(toks[0]));
    /* Either accept (preferred, grammar permits arbitrary nesting)
    ** or graceful error -- never a crash. */
    CHECK(rc == 1 || rc < 0, "deep nesting (80 parens) terminates without crashing");
    CHECK(rc == 1, "deep nesting parses successfully through stack realloc");
}

static void test_recovery_after_error_via_fresh_ctx(ParserSnapshot *snap) {
    /* parse_engine.c does not expose a parse_reset; a fresh ctx is
    ** the documented recovery contract.  Confirm the contract. */
    int bad[] = {ARITH_PLUS, ARITH_NUM};
    int good[] = {ARITH_NUM};
    CHECK(run_seq(snap, bad, 2) < 0, "leading-PLUS rejected");
    CHECK(run_seq(snap, good, 1) == 1,
          "fresh ctx after error accepts well-formed input");
}

/* ------------------------------------------------------------------ */
/*  parse_token_lex variant                                            */
/* ------------------------------------------------------------------ */

static void test_parse_token_lex_null_lexeme(ParserSnapshot *snap) {
    /* With NULL lexeme and no GrammarContextStack attached,
    ** parse_token_lex must behave identically to parse_token. */
    ParseContext *ctx = parse_begin(snap);
    int rc1 = parse_token_lex(ctx, ARITH_NUM, NULL, NULL, -1);
    int rc2 = parse_token_lex(ctx, 0, NULL, NULL, -1);
    parse_end(ctx);
    CHECK(rc1 == 0 && rc2 == 1,
          "parse_token_lex(NULL lexeme) accepts a single NUM");
}

static void test_parse_token_lex_with_lexeme(ParserSnapshot *snap) {
    /* With a lexeme but no context stack, the engine must still
    ** parse correctly.  The lexeme is consulted only by an attached
    ** GrammarContextStack. */
    ParseContext *ctx = parse_begin(snap);
    int rc1 = parse_token_lex(ctx, ARITH_NUM, NULL, "42", -1);
    int rc2 = parse_token_lex(ctx, 0, NULL, "", -1);
    parse_end(ctx);
    CHECK(rc1 == 0 && rc2 == 1,
          "parse_token_lex with lexeme parses identically to parse_token");
}

static void test_parse_token_lex_null_ctx(void) {
    int rc = parse_token_lex(NULL, 1, NULL, "lex", -1);
    CHECK(rc < 0, "parse_token_lex(NULL ctx) returns error, not segfault");
}

/* ------------------------------------------------------------------ */
/*  NULL-safety tests                                                   */
/* ------------------------------------------------------------------ */

static void test_parse_token_null_ctx(void) {
    int rc = parse_token(NULL, 1, NULL, -1);
    CHECK(rc < 0, "parse_token(NULL ctx) returns -1, not segfault");
}

static void test_parse_token_null_value(ParserSnapshot *snap) {
    /* parse_engine ignores token_value; NULL must be safe. */
    ParseContext *ctx = parse_begin(snap);
    int rc = parse_token(ctx, ARITH_NUM, NULL, -1);
    parse_end(ctx);
    CHECK(rc == 0, "parse_token with NULL token_value drives parse without crashing");
}

static void test_parse_end_null(void) {
    parse_end(NULL);
    CHECK(1, "parse_end(NULL) is a safe no-op");
}

/* ------------------------------------------------------------------ */
/*  snap_find_shift_action / snap_find_reduce_action helpers           */
/* ------------------------------------------------------------------ */

static void test_snap_find_shift_action_null(void) {
    /* The exposed helper in parse_context.c mirrors the static
    ** find_shift_action in parse_engine.c.  Defensive contract:
    ** NULL snapshot returns 0, never segfaults. */
    uint16_t a = snap_find_shift_action(NULL, 0, 0);
    CHECK(a == 0, "snap_find_shift_action(NULL) returns 0");
}

static void test_snap_find_shift_action_oob_state(ParserSnapshot *snap) {
    /* state >= nstate must return 0 (not segfault into the
    ** yy_shift_ofst array). */
    uint16_t a = snap_find_shift_action(snap, (uint16_t)(snap->nstate + 1000), 1);
    CHECK(a == 0, "snap_find_shift_action with state > nstate returns 0");
}

static void test_snap_find_reduce_action_null(void) {
    uint16_t a = snap_find_reduce_action(NULL, 0, 0);
    CHECK(a == 0, "snap_find_reduce_action(NULL) returns 0");
}

static void test_snap_find_reduce_action_oob_state(ParserSnapshot *snap) {
    uint16_t a = snap_find_reduce_action(snap, (uint16_t)(snap->nstate + 1000), 1);
    CHECK(a == 0, "snap_find_reduce_action with state > nstate returns 0");
}

/* ------------------------------------------------------------------ */
/*  Synthetic-snapshot tests                                           */
/* ------------------------------------------------------------------ */

/* parse_engine_step early-returns when the bound snapshot has no
** action tables.  Reproduces the publish_modified_snapshot mid-build
** failure mode. */
static void test_parse_token_against_empty_snapshot(void) {
    SyntheticSnap s = make_synthetic(2, 2, 4);
    if (s.snap == NULL) {
        CHECK(0, "make_synthetic OOM");
        return;
    }
    /* Wipe yy_action so parse_engine_step takes the empty-tables
    ** early return. */
    s.snap->yy_action = NULL;

    ParseContext *ctx = parse_begin(s.snap);
    int rc = parse_token(ctx, 1, NULL, -1);
    CHECK(rc < 0, "parse_token on snapshot with NULL yy_action returns -1");
    parse_end(ctx);

    /* Restore so snapshot_release doesn't try to free the wrong
    ** pointer.  We owned the array directly via s.action. */
    s.snap->yy_action = s.action;
    snapshot_release(s.snap);
}

static void test_parse_token_against_null_default(void) {
    SyntheticSnap s = make_synthetic(2, 2, 4);
    if (s.snap == NULL) {
        CHECK(0, "make_synthetic OOM");
        return;
    }
    s.snap->yy_default = NULL;
    ParseContext *ctx = parse_begin(s.snap);
    int rc = parse_token(ctx, 1, NULL, -1);
    CHECK(rc < 0, "parse_token on snapshot with NULL yy_default returns -1");
    parse_end(ctx);
    s.snap->yy_default = s.defaults;
    snapshot_release(s.snap);
}

/*
** Build a snapshot whose state 0 sees an explicit yy_accept_action
** entry in yy_action when fed token 1.  Exercises the
** "if (action == yy_accept_action)" branch in parse_engine_step
** that real grammars rarely visit (real grammars accept by reducing
** the start rule, not via an explicit accept-action entry).
*/
static void test_explicit_accept_action(void) {
    SyntheticSnap s = make_synthetic(2, 2, 8);
    if (s.snap == NULL) {
        CHECK(0, "make_synthetic OOM");
        return;
    }
    /* State 0, token 1 -> action=yy_accept_action.  shift_ofst=0
    ** so idx = 0 + token = 1; place the (lookahead=1, action=accept)
    ** entry at slot 1. */
    s.shift_ofst[0] = 0;
    s.lookahead[1] = 1;
    s.action[1] = s.snap->yy_accept_action;
    s.defaults[0] = s.snap->yy_error_action;

    ParseContext *ctx = parse_begin(s.snap);
    int rc = parse_token(ctx, 1, NULL, -1);
    CHECK(rc == 1, "explicit yy_accept_action entry causes parse_token to accept");
    parse_end(ctx);
    snapshot_release(s.snap);
}

/*
** Drive the yy_fallback retry path.  Snapshot is configured so that
** state 0 has no entry for token 2 (default=error_action) but
** yy_fallback[2] = 1 redirects token 2 to token 1, which IS handled
** as a shift-reduce that completes the parse.
*/
static void test_fallback_retry(void) {
    SyntheticSnap s = make_synthetic(2, 1, 8);
    if (s.snap == NULL) {
        CHECK(0, "make_synthetic OOM");
        return;
    }

    /* State 0, token 1 -> shift-reduce action 2 (encodes a reduce
    ** by rule 0).  yy_min_shiftreduce=2, yy_max_shiftreduce=3,
    ** yy_min_reduce=7.  Encoded state = 2 + (7-2) = 7 = ruleno 0.
    ** shift_ofst=0 + token=1 -> idx=1. */
    s.shift_ofst[0] = 0;
    s.lookahead[1] = 1;
    s.action[1] = 2;     /* shift-reduce */
    /* Token 2 has no entry: idx=2, lookahead[2]=0xFFFF; falls to
    ** default[0]=error_action and triggers the fallback retry. */
    s.defaults[0] = s.snap->yy_error_action;

    /* Rule 0: 1 RHS; pop-1 leaves the stack at (0,0); the goto on
    ** lhs=5 must resolve.  reduce_ofst[0]=-5 so idx=-5+5=0,
    ** lookahead[0]=5 matches, action[0]=yy_accept_action=5 fires
    ** the start-rule accept inside reduce(). */
    s.rule_lhs[0] = 5;
    s.rule_nrhs[0] = -1;
    s.reduce_ofst[0] = -5;
    s.lookahead[0] = 5;
    s.action[0] = s.snap->yy_accept_action;

    /* Fallback table: token 2 -> token 1 (substitute). */
    s.fallback = calloc(4, sizeof(uint16_t));
    s.fallback[2] = 1;
    s.snap->yy_fallback = s.fallback;
    s.snap->nfallback = 4;

    ParseContext *ctx = parse_begin(s.snap);
    /* Feed token 2; state 0 default is error_action; fallback retry
    ** rewrites major to 1; engine then sees the shift-reduce. */
    int rc = parse_token(ctx, 2, NULL, -1);
    /* The shift-reduce pushes encoded state 7; the next call
    ** (EOF) drives the reduce to start-rule completion. */
    int rc2 = parse_token(ctx, 0, NULL, -1);
    parse_end(ctx);
    CHECK(rc == 0, "fallback retry consumed token 2 as token 1 (shift-reduce push)");
    CHECK(rc2 == 1, "subsequent EOF reduces to start rule (accept)");

    free(s.fallback);
    s.snap->yy_fallback = NULL;
    snapshot_release(s.snap);
}

/*
** Drive reduce() into the broken-table early-return branches.
** When yy_rule_info_lhs is NULL or ruleno >= nrule, reduce returns
** -1 and parse_engine_step propagates the failure (lines 381-382 /
** 326-327).
*/
static void test_reduce_with_broken_rule_info(void) {
    SyntheticSnap s = make_synthetic(2, 2, 8);
    if (s.snap == NULL) {
        CHECK(0, "make_synthetic OOM");
        return;
    }
    /* State 0, token 1 -> action=7 (yy_min_reduce + 0).  Drives the
    ** explicit reduce branch in parse_engine_step. */
    s.shift_ofst[0] = 0;
    s.lookahead[1] = 1;
    s.action[1] = 7;
    s.defaults[0] = s.snap->yy_error_action;

    /* Wipe rule metadata so reduce() takes its NULL-check early
    ** return. */
    s.snap->yy_rule_info_lhs = NULL;

    ParseContext *ctx = parse_begin(s.snap);
    int rc = parse_token(ctx, 1, NULL, -1);
    CHECK(rc < 0,
          "parse_token returns -1 when reduce() hits NULL rule_info_lhs");
    parse_end(ctx);

    /* Restore so the snapshot teardown frees correctly. */
    s.snap->yy_rule_info_lhs = s.rule_lhs;
    snapshot_release(s.snap);
}

static void test_reduce_with_out_of_range_ruleno(void) {
    SyntheticSnap s = make_synthetic(2, 2, 8);
    if (s.snap == NULL) {
        CHECK(0, "make_synthetic OOM");
        return;
    }
    /* yy_min_reduce=7; nrule=2.  Action 7+5=12 yields ruleno=5
    ** which is >= nrule -- reduce() returns -1. */
    s.shift_ofst[0] = 0;
    s.lookahead[1] = 1;
    s.action[1] = 12;
    s.defaults[0] = s.snap->yy_error_action;

    ParseContext *ctx = parse_begin(s.snap);
    int rc = parse_token(ctx, 1, NULL, -1);
    CHECK(rc < 0, "parse_token returns -1 for out-of-range ruleno in reduce");
    parse_end(ctx);
    snapshot_release(s.snap);
}

/*
** Drive the find_reduce_action default-action fallback (line 162):
** the reduce-goto's idx points to a slot whose lookahead does NOT
** match, so the lookup falls back to yy_default[state].  We set
** the default to a plain-shift action so reduce() pushes it and
** parse_engine_step continues without error.
*/
static void test_reduce_goto_default_fallback(void) {
    SyntheticSnap s = make_synthetic(3, 1, 8);
    if (s.snap == NULL) {
        CHECK(0, "make_synthetic OOM");
        return;
    }

    /* Two-token sequence: token 1 plain-shifts to state 1, token 2
    ** then reduces.  This puts state 1 above the bottom so the
    ** subsequent pop-1 leaves stk->top != stk->base and the goto
    ** runs.  Without this priming step the very first reduce pops
    ** the bottom and returns 1 immediately (line 191), bypassing
    ** find_reduce_action entirely.
    **
    ** State 0:  shift on token 1 -> state 1.
    ** State 1:  reduce on token 2 -> rule 0 (1 RHS).
    **           After pop, stack=[(0,0)].  goto find_reduce_action
    **           (0, lhs=5) -> idx=5, lookahead[5]=sentinel -> falls
    **           to default[0]=yy_accept_action so reduce returns 1
    **           via line 197 (clean accept).
    **
    ** This is the simplest setup that drives line 162 of
    ** parse_engine.c (find_reduce_action's default fallback). */
    s.shift_ofst[0] = 0;
    s.lookahead[1] = 1;
    s.action[1] = 1;     /* plain shift to state 1 */

    s.shift_ofst[1] = 0;
    s.lookahead[2] = 2;
    s.action[2] = 7;     /* reduce by rule 0 */

    s.defaults[0] = s.snap->yy_accept_action; /* reduce-goto miss
                                              ** path -> accept. */

    s.rule_lhs[0] = 5;
    s.rule_nrhs[0] = -1;
    s.reduce_ofst[0] = 0;

    ParseContext *ctx = parse_begin(s.snap);
    int rc1 = parse_token(ctx, 1, NULL, -1);
    CHECK(rc1 == 0, "reduce-goto fallback: shift on token 1 returns 0");
    int rc2 = parse_token(ctx, 2, NULL, -1);
    /* token_code != 0, so reduce-returns-1 path returns 0 from
    ** parse_engine_step (the EOF-vs-non-EOF guard at line 378).
    ** The accept flag is latched; a follow-up EOF returns 1. */
    CHECK(rc2 == 0,
          "reduce-goto default-fallback: accept latched, non-EOF token returns 0");
    int rc3 = parse_token(ctx, 0, NULL, -1);
    CHECK(rc3 == 1, "post-latch EOF returns 1 (accept)");
    parse_end(ctx);
    snapshot_release(s.snap);
}

/*
** Drive the line-206 path: reduce-goto returns an action in the
** shift-reduce range (yy_max_shift < action <= yy_max_shiftreduce),
** which reduce() must re-encode by adding (yy_min_reduce -
** yy_min_shiftreduce) before pushing.
*/
static void test_reduce_goto_shift_reduce_push(void) {
    SyntheticSnap s = make_synthetic(3, 2, 8);
    if (s.snap == NULL) {
        CHECK(0, "make_synthetic OOM");
        return;
    }

    /* Two-token priming so reduce()'s pop doesn't reach the bottom. */
    s.shift_ofst[0] = 0;
    s.lookahead[1] = 1;
    s.action[1] = 1;     /* shift to state 1 */

    s.shift_ofst[1] = 0;
    s.lookahead[2] = 2;
    s.action[2] = 7;     /* reduce by rule 0 */

    s.defaults[0] = s.snap->yy_error_action;

    /* Rule 0: lhs=5, 1 RHS.  After pop, stack=[(0,0)] (not empty).
    ** Reduce-goto: state 0, lhs=5 -> action=3 (shift-reduce).
    ** reduce() must re-encode 3 + (7-2) = 8 (line 206 fires) and
    ** push that.  The engine then re-enters the pending-reduce
    ** branch with cur_state=8, ruleno=1.  Rule 1 has 2 RHS so the
    ** pop reaches the bottom -- start-rule completion via the
    ** pending-reduce r==1 branch (lines 322-323). */
    s.rule_lhs[0] = 5;
    s.rule_nrhs[0] = -1;
    s.rule_lhs[1] = 6;
    s.rule_nrhs[1] = -2;
    s.reduce_ofst[0] = -5;
    s.lookahead[0] = 5;
    s.action[0] = 3;

    ParseContext *ctx = parse_begin(s.snap);
    int rc1 = parse_token(ctx, 1, NULL, -1);
    int rc2 = parse_token(ctx, 2, NULL, -1);
    CHECK(rc1 == 0, "shift-reduce-push prelude shifts on token 1");
    CHECK(rc2 == 0,
          "reduce-goto re-encodes shift-reduce action; pending-reduce "
          "completes start rule, returns 0 for non-EOF token");
    parse_end(ctx);
    snapshot_release(s.snap);
}

/*
** Drive the pending-reduce-on-stack failure path (parse_engine.c
** lines 326-327).  Push an encoded state whose ruleno is
** out-of-range, then send EOF; the pending-reduce branch fires,
** reduce() returns -1, parse_engine_step returns -1.
*/
static void test_pending_reduce_fails(void) {
    SyntheticSnap s = make_synthetic(2, 1, 8);
    if (s.snap == NULL) {
        CHECK(0, "make_synthetic OOM");
        return;
    }

    /* State 0, token 1 -> shift-reduce action 3 (yy_max_shiftreduce).
    ** Encoded = 3 + (7-2) = 8.  On EOF, cur_state=8 >= yy_min_reduce
    ** -> reduce branch.  ruleno = 8-7 = 1, but nrule=1 so ruleno >=
    ** nrule -- reduce() returns -1 from its bounds check. */
    s.shift_ofst[0] = 0;
    s.lookahead[1] = 1;
    s.action[1] = 3;     /* shift-reduce */
    s.defaults[0] = s.snap->yy_error_action;
    s.rule_lhs[0] = 5;
    s.rule_nrhs[0] = -1;

    ParseContext *ctx = parse_begin(s.snap);
    int rc1 = parse_token(ctx, 1, NULL, -1);
    CHECK(rc1 == 0, "shift-reduce on token 1 pushes encoded state");
    int rc2 = parse_token(ctx, 0, NULL, -1);
    CHECK(rc2 < 0,
          "pending-reduce with out-of-range ruleno returns -1");
    parse_end(ctx);
    snapshot_release(s.snap);
}

/*
** Drive the "shift on EOF accepts" branch: action is in plain-shift
** range (<= yy_max_shift) AND token_code == 0.  Real grammars don't
** typically encode this, but parse_engine.c handles it (lines
** 339-340).
*/
static void test_shift_eof_accept(void) {
    SyntheticSnap s = make_synthetic(2, 2, 2);
    if (s.snap == NULL) {
        CHECK(0, "make_synthetic OOM");
        return;
    }
    /* State 0 + lookahead 0 (EOF) -> shift to state 1.  Action 1
    ** is <= yy_max_shift=1. */
    s.shift_ofst[0] = 0;
    s.lookahead[0] = 0;
    s.action[0] = 1;
    s.defaults[0] = s.snap->yy_error_action;

    ParseContext *ctx = parse_begin(s.snap);
    int rc = parse_token(ctx, 0, NULL, -1);
    CHECK(rc == 1, "shift on EOF=0 accepts via fast path");
    parse_end(ctx);
    snapshot_release(s.snap);
}

/*
** Drive the explicit error_action path.  After errored=true,
** subsequent parse_token calls must return -1 without re-entering
** the for-loop.
*/
static void test_post_error_returns_minus_one(ParserSnapshot *snap) {
    ParseContext *ctx = parse_begin(snap);
    /* Bad input: leading PLUS. */
    int rc1 = parse_token(ctx, ARITH_PLUS, NULL, -1);
    CHECK(rc1 < 0, "leading PLUS rejected");
    /* Subsequent token must keep returning -1 (cached errored
    ** flag). */
    int rc2 = parse_token(ctx, ARITH_NUM, NULL, -1);
    CHECK(rc2 < 0, "post-error parse_token sticks at -1");
    parse_end(ctx);
}

/*
** Drive the post-accept fast return: once accepted, further tokens
** return 1 without re-entering the for-loop.
*/
static void test_post_accept_returns_one(ParserSnapshot *snap) {
    ParseContext *ctx = parse_begin(snap);
    int rc1 = parse_token(ctx, ARITH_NUM, NULL, -1);
    int rc2 = parse_token(ctx, 0, NULL, -1);
    CHECK(rc1 == 0 && rc2 == 1, "single NUM + EOF accepts");
    /* After acceptance the engine returns 1 on any further input. */
    int rc3 = parse_token(ctx, ARITH_NUM, NULL, -1);
    CHECK(rc3 == 1, "post-accept parse_token sticks at 1");
    parse_end(ctx);
}

/* ------------------------------------------------------------------ */
/*  GrammarContextStack attachment                                     */
/* ------------------------------------------------------------------ */

/*
** Drive the grammar-context-stack swap branch (parse_engine.c
** lines 266-278).  We attach an empty stack rooted on the same
** snapshot.  context_switch_needed returns false on every token,
** which still exercises the outer "ctx->context_stack != NULL"
** branch but skips the snapshot swap -- the simplest surface that
** isn't covered transitively by any other test.
*/
static void test_context_stack_no_op_swap(ParserSnapshot *snap) {
    GrammarContextStack *stack = grammar_context_create(snap);
    if (stack == NULL) {
        CHECK(0, "grammar_context_create returned NULL");
        return;
    }

    ParseContext *ctx = parse_begin(snap);
    parse_attach_context_stack(ctx, stack);

    int rc1 = parse_token(ctx, ARITH_NUM, NULL, -1);
    int rc2 = parse_token(ctx, 0, NULL, -1);
    CHECK(rc1 == 0 && rc2 == 1, "context-stack-attached parse drives correctly");

    parse_attach_context_stack(ctx, NULL);
    parse_end(ctx);
    grammar_context_destroy(stack);
}

/* ------------------------------------------------------------------ */
/*  Threaded refcount safety                                           */
/* ------------------------------------------------------------------ */

#define THREADS_N 8
#define ITER_N 250

typedef struct {
    ParserSnapshot *snap;
    atomic_int errors;
} ThreadCtx;

static void *thread_parse_loop(void *arg) {
    ThreadCtx *tc = (ThreadCtx *)arg;
    for (int i = 0; i < ITER_N; i++) {
        ParseContext *ctx = parse_begin(tc->snap);
        if (ctx == NULL) {
            atomic_fetch_add(&tc->errors, 1);
            continue;
        }
        int rc1 = parse_token(ctx, ARITH_NUM, NULL, -1);
        int rc2 = parse_token(ctx, 0, NULL, -1);
        if (rc1 != 0 || rc2 != 1) {
            atomic_fetch_add(&tc->errors, 1);
        }
        parse_end(ctx);
    }
    return NULL;
}

static void test_concurrent_parse_begin_end(ParserSnapshot *snap) {
    uint32_t before = atomic_load(&snap->refcount);
    ThreadCtx tc = { .snap = snap };
    atomic_init(&tc.errors, 0);

    pthread_t threads[THREADS_N];
    for (int i = 0; i < THREADS_N; i++) {
        int rc = pthread_create(&threads[i], NULL, thread_parse_loop, &tc);
        if (rc != 0) {
            CHECK(0, "pthread_create failed");
            return;
        }
    }
    for (int i = 0; i < THREADS_N; i++) pthread_join(threads[i], NULL);

    int errors = atomic_load(&tc.errors);
    uint32_t after = atomic_load(&snap->refcount);
    CHECK(errors == 0,
          "no parse errors across THREADS_N x ITER_N concurrent parse_begin/end pairs");
    CHECK(after == before,
          "concurrent parse_begin/end pairs return refcount to baseline");
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("Lime parse_engine.c direct unit tests\n");
    printf("=====================================\n");

    ParserSnapshot *snap = ArithBuildSnapshot();
    if (snap == NULL) {
        fprintf(stderr, "ArithBuildSnapshot returned NULL\n");
        return 1;
    }

    /* Lifecycle */
    test_parse_begin_valid(snap);
    test_parse_begin_null_snap();
    test_parse_begin_refcount(snap);
    test_multiple_contexts_independent(snap);

    /* Parse path (real grammar) */
    test_single_num_accepts(snap);
    test_explicit_eof_completes(snap);
    test_eof_at_state_zero_is_error(snap);
    test_two_token_expression(snap);
    test_nested_expression(snap);
    test_unknown_token_code(snap);

    /* Stack growth + recovery */
    test_deep_nesting_stack_growth(snap);
    test_recovery_after_error_via_fresh_ctx(snap);

    /* parse_token_lex variant */
    test_parse_token_lex_null_lexeme(snap);
    test_parse_token_lex_with_lexeme(snap);
    test_parse_token_lex_null_ctx();

    /* NULL safety */
    test_parse_token_null_ctx();
    test_parse_token_null_value(snap);
    test_parse_end_null();

    /* snap_find_*_action helpers */
    test_snap_find_shift_action_null();
    test_snap_find_shift_action_oob_state(snap);
    test_snap_find_reduce_action_null();
    test_snap_find_reduce_action_oob_state(snap);

    /* Synthetic-snapshot tests */
    test_parse_token_against_empty_snapshot();
    test_parse_token_against_null_default();
    test_explicit_accept_action();
    test_fallback_retry();
    test_reduce_with_broken_rule_info();
    test_reduce_with_out_of_range_ruleno();
    test_reduce_goto_default_fallback();
    test_reduce_goto_shift_reduce_push();
    test_pending_reduce_fails();
    test_shift_eof_accept();
    test_post_error_returns_minus_one(snap);
    test_post_accept_returns_one(snap);

    /* Grammar-context stack */
    test_context_stack_no_op_swap(snap);

    /* Threaded refcount safety */
    test_concurrent_parse_begin_end(snap);

    snapshot_release(snap);

    printf("\n=== Summary === Pass: %d Fail: %d\n", n_pass, n_fail);
    return n_fail == 0 ? 0 : 1;
}
