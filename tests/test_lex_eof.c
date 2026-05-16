/*
** tests/test_lex_eof.c -- M3.6 integration test for <<EOF>>
** rule dispatch in the LexFeedBytes auto-pop branch.
**
** Driven by meson custom_targets: lime -X test_lex_eof_grammar.lex
** produces test_lex_eof_grammar_lex.{c,h}; lime -X
** test_lex_eof_none_grammar.lex produces the parallel files for
** the regression grammar.  Both pairs link into this driver.
**
** Sub-tests:
**   1. test_no_eof_rules_returns_ok   -- spec without <<EOF>>
**      rules: LexFeedBytes still returns LEX_OK on end-of-input
**      and no spurious tokens are emitted.  Verifies the
**      `any_eof == 0` codegen branch.
**   2. test_eof_initial_emits         -- single-state EOF rule
**      whose action calls LEX_EMIT(SOME_RULE) actually emits.
**   3. test_eof_state_qualified       -- <INITIAL><<EOF>> and
**      <XQ><<EOF>> both declared.  Use LEX_TRANSITION (and
**      LexSetState) to control which state is current at EOF;
**      verify the right rule fires.
**   4. test_eof_error_at              -- LEX_ERROR_AT inside an
**      EOF action body returns LEX_ERROR with the supplied msg.
**   5. test_eof_with_include          -- EOF rule fires on each
**      buffer-stack frame's auto-pop, including an inner
**      LexInclude'd buffer popping back to its parent.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_lex_eof_grammar_lex.h"
#include "test_lex_eof_none_grammar_lex.h"

static int fails = 0;

#define EXPECT(cond, ...) do {                                  \
    if (!(cond)) {                                              \
        fprintf(stderr, "  %s:%d: ", __func__, __LINE__);       \
        fprintf(stderr, __VA_ARGS__);                           \
        fprintf(stderr, "\n");                                  \
        fails++;                                                \
    }                                                           \
} while (0)

#define MAX_TOKENS 16

struct capture {
    int    n;
    int    rules[MAX_TOKENS];
    char   texts[MAX_TOKENS][16];
};

static void emit_cb(void *user, int rule,
                    const char *text, size_t len) {
    struct capture *c = user;
    if (c->n >= MAX_TOKENS) return;
    c->rules[c->n] = rule;
    if (len >= sizeof(c->texts[0])) len = sizeof(c->texts[0]) - 1;
    memcpy(c->texts[c->n], text, len);
    c->texts[c->n][len] = '\0';
    c->n++;
}

/* Capture used by test_eof_with_include: needs to push an
** include from inside the emit callback when rule A fires. */
struct include_capture {
    TleLexer *yyl;
    int       n;
    int       rules[MAX_TOKENS];
    int       depths[MAX_TOKENS];
    int       trigger_rule;
    const char *trigger_buf;
    size_t      trigger_len;
};

static void emit_include_cb(void *user, int rule,
                            const char *text, size_t len) {
    (void)text; (void)len;
    struct include_capture *c = user;
    if (c->n < MAX_TOKENS) {
        c->rules[c->n]  = rule;
        c->depths[c->n] = TleLexIncludeDepth(c->yyl);
        c->n++;
    }
    if (c->yyl && rule == c->trigger_rule && c->trigger_buf) {
        const char *buf = c->trigger_buf;
        size_t      bl  = c->trigger_len;
        c->trigger_buf  = NULL;       /* fire once */
        c->trigger_rule = -1;
        TleLexInclude(c->yyl, buf, bl);
    }
}

/* ----- sub-tests ------------------------------------------- */

/* (1) Spec with no <<EOF>> rules: LexFeedBytes returns LEX_OK on
** EOF, no implicit token emitted, no error message set.  This
** exercises the `any_eof == 0` codegen branch in lex_emit.c
** which omits the per-state EOF lookup table and the dispatch
** block in the auto-pop branch entirely. */
static int test_no_eof_rules_returns_ok(void) {
    int saved = fails;
    TlenLexer *yyl = TlenLexAlloc(malloc);
    EXPECT(yyl != NULL, "alloc");
    if (!yyl) return fails - saved;

    struct capture c = {0};
    TlenLexResult r = TlenLexFeedBytes(yyl, "zz", 2, emit_cb, &c);
    EXPECT(r == TLEN_LEX_OK, "feed result %d, expected LEX_OK", r);
    EXPECT(c.n == 2, "want 2 tokens, got %d", c.n);
    EXPECT(TlenLexErrorMessage(yyl) == NULL,
           "err_msg must be NULL after clean EOF, got \"%s\"",
           TlenLexErrorMessage(yyl));
    /* LexFeedEOF is a no-op in M3.6; the EOF rule fired (or
    ** didn't, here) inside LexFeedBytes' auto-pop. */
    r = TlenLexFeedEOF(yyl, emit_cb, &c);
    EXPECT(r == TLEN_LEX_OK, "FeedEOF result %d", r);
    EXPECT(c.n == 2, "FeedEOF must not emit anything new, got n=%d", c.n);
    TlenLexFree(yyl, free);
    if (fails == saved) printf("test_no_eof_rules_returns_ok: PASS\n");
    return fails - saved;
}

/* (2) Single-state EOF rule firing LEX_EMIT.  Feed "a" -- the A
** rule auto-emits, then the bottom buffer-stack frame hits EOF
** and the unqualified <<EOF>> rule fires its body, which calls
** LEX_EMIT(EOF_INIT). */
static int test_eof_initial_emits(void) {
    int saved = fails;
    TleLexer *yyl = TleLexAlloc(malloc);
    EXPECT(yyl != NULL, "alloc");
    if (!yyl) return fails - saved;

    struct capture c = {0};
    TleLexResult r = TleLexFeedBytes(yyl, "a", 1, emit_cb, &c);
    EXPECT(r == TLE_LEX_OK, "feed result %d, expected LEX_OK", r);
    EXPECT(c.n == 2, "want 2 tokens (A, EOF_INIT), got %d", c.n);
    if (c.n >= 2) {
        EXPECT(c.rules[0] == TLE_RULE_A &&
               strcmp(c.texts[0], "a") == 0,
               "[0]: rule=%d text=\"%s\"", c.rules[0], c.texts[0]);
        EXPECT(c.rules[1] == TLE_RULE_EOF_INIT &&
               strcmp(c.texts[1], "") == 0,
               "[1]: rule=%d text=\"%s\" "
               "(EOF action passes matched=\"\" matched_len=0)",
               c.rules[1], c.texts[1]);
    }
    EXPECT(TleLexErrorMessage(yyl) == NULL,
           "err_msg must be NULL after clean EOF, got \"%s\"",
           TleLexErrorMessage(yyl));
    TleLexFree(yyl, free);
    if (fails == saved) printf("test_eof_initial_emits: PASS\n");
    return fails - saved;
}

/* (3) State-qualified EOF: <XQ><<EOF>> fires only when current
** state is XQ at EOF, otherwise the unqualified <<EOF>> rule
** (which applies to INITIAL) fires.  Two sub-cases:
**   (a) State driven by an action body: feed "!" -- the bang
**       rule's LEX_TRANSITION puts us in XQ; EOF then fires
**       <XQ><<EOF>> which raises an error.
**   (b) State driven by LexSetState: alloc a fresh lexer, set
**       state to XQ, feed "y" -- Y auto-emits, EOF fires the
**       <XQ> rule -> error.
** Both confirm the per-state lookup table indexes by yyl->state. */
static int test_eof_state_qualified(void) {
    int saved = fails;

    /* (a) Action-driven transition into XQ. */
    {
        TleLexer *yyl = TleLexAlloc(malloc);
        EXPECT(yyl != NULL, "alloc");
        if (!yyl) return fails - saved;
        struct capture c = {0};
        TleLexResult r = TleLexFeedBytes(yyl, "!", 1, emit_cb, &c);
        EXPECT(r == TLE_LEX_ERROR,
               "feed should ERROR after !-induced XQ EOF, got %d", r);
        /* "!" auto-emits as RULE_B before the transition. */
        EXPECT(c.n == 1, "want 1 emit (B), got %d", c.n);
        if (c.n >= 1) {
            EXPECT(c.rules[0] == TLE_RULE_B,
                   "[0]: rule=%d want B=%d", c.rules[0], TLE_RULE_B);
        }
        EXPECT(TleLexCurrentState(yyl) == TLE_STATE_XQ,
               "after !, state=%d want XQ=%d",
               TleLexCurrentState(yyl), TLE_STATE_XQ);
        const char *msg = TleLexErrorMessage(yyl);
        EXPECT(msg && strcmp(msg, "unterminated string") == 0,
               "err_msg=\"%s\" want \"unterminated string\"",
               msg ? msg : "(null)");
        TleLexFree(yyl, free);
    }

    /* (b) LexSetState into XQ before any input. */
    {
        TleLexer *yyl = TleLexAlloc(malloc);
        EXPECT(yyl != NULL, "alloc");
        if (!yyl) return fails - saved;
        TleLexSetState(yyl, TLE_STATE_XQ);
        struct capture c = {0};
        TleLexResult r = TleLexFeedBytes(yyl, "y", 1, emit_cb, &c);
        EXPECT(r == TLE_LEX_ERROR,
               "feed should ERROR after XQ EOF, got %d", r);
        EXPECT(c.n == 1, "want 1 emit (Y), got %d", c.n);
        if (c.n >= 1) {
            EXPECT(c.rules[0] == TLE_RULE_Y,
                   "[0]: rule=%d want Y=%d", c.rules[0], TLE_RULE_Y);
        }
        const char *msg = TleLexErrorMessage(yyl);
        EXPECT(msg && strcmp(msg, "unterminated string") == 0,
               "err_msg=\"%s\"", msg ? msg : "(null)");
        TleLexFree(yyl, free);
    }

    /* (c) Symmetry check: in INITIAL we get the unqualified
    ** EOF rule's emit, not an error.  Already covered by
    ** test_eof_initial_emits but doing it here once more
    ** confirms the dispatch table is keyed by the ACTUAL
    ** runtime state, not a compile-time guess. */
    {
        TleLexer *yyl = TleLexAlloc(malloc);
        EXPECT(yyl != NULL, "alloc");
        if (!yyl) return fails - saved;
        struct capture c = {0};
        TleLexResult r = TleLexFeedBytes(yyl, "", 0, emit_cb, &c);
        EXPECT(r == TLE_LEX_OK, "empty feed in INITIAL: %d", r);
        EXPECT(c.n == 1, "want 1 emit (EOF_INIT), got %d", c.n);
        if (c.n >= 1) {
            EXPECT(c.rules[0] == TLE_RULE_EOF_INIT,
                   "[0]: rule=%d want EOF_INIT=%d",
                   c.rules[0], TLE_RULE_EOF_INIT);
        }
        TleLexFree(yyl, free);
    }

    if (fails == saved) printf("test_eof_state_qualified: PASS\n");
    return fails - saved;
}

/* (4) LEX_ERROR_AT inside an EOF action body returns LEX_ERROR
** with the supplied message stashed for LexErrorMessage.  Tokens
** emitted before the EOF rule fired are preserved.  Covered by
** test_eof_state_qualified case (a)+(b); this sub-test is the
** explicit standalone proof and the regression for the err_msg
** wiring (lex->err_msg = msg via the LEX_ERROR_AT macro). */
static int test_eof_error_at(void) {
    int saved = fails;
    TleLexer *yyl = TleLexAlloc(malloc);
    EXPECT(yyl != NULL, "alloc");
    if (!yyl) return fails - saved;
    /* Drive into XQ state via LexSetState so we don't depend on
    ** any preceding action body. */
    TleLexSetState(yyl, TLE_STATE_XQ);
    struct capture c = {0};
    TleLexResult r = TleLexFeedBytes(yyl, "", 0, emit_cb, &c);
    EXPECT(r == TLE_LEX_ERROR, "want LEX_ERROR on XQ EOF, got %d", r);
    EXPECT(c.n == 0, "want 0 emits before error, got %d", c.n);
    const char *msg = TleLexErrorMessage(yyl);
    EXPECT(msg != NULL, "err_msg must be set");
    if (msg) {
        EXPECT(strcmp(msg, "unterminated string") == 0,
               "err_msg=\"%s\"", msg);
    }
    TleLexFree(yyl, free);
    if (fails == saved) printf("test_eof_error_at: PASS\n");
    return fails - saved;
}

/* (5) EOF rule + LexInclude.  Per the M3.6 contract, the EOF
** rule for the current state fires once per buffer-stack frame
** as that frame hits end-of-input -- including an included
** buffer popping back to its parent.
**
** Pattern: feed "a" on the parent.  When the emit callback sees
** RULE_A, push "x" as an include.  Expected emit sequence:
**   - A      (rule A, depth 0; included buffer is then pushed)
**   - X      (rule X, depth 1, from inside the included buffer)
**   - EOF_INIT  (depth still 1 -- EOF rule fires from inside
**                the include frame, BEFORE the auto-pop)
**   - EOF_INIT  (depth 0 -- parent frame's auto-pop fires the
**                EOF rule once more)
** The two EOF_INIT emits prove the rule fires per frame. */
static int test_eof_with_include(void) {
    int saved = fails;
    TleLexer *yyl = TleLexAlloc(malloc);
    EXPECT(yyl != NULL, "alloc");
    if (!yyl) return fails - saved;

    struct include_capture c = {0};
    c.yyl          = yyl;
    c.trigger_rule = TLE_RULE_A;
    c.trigger_buf  = "x";
    c.trigger_len  = 1;

    TleLexResult r = TleLexFeedBytes(yyl, "a", 1, emit_include_cb, &c);
    EXPECT(r == TLE_LEX_OK, "feed result %d, expected LEX_OK", r);
    EXPECT(c.n == 4,
           "want 4 emits (A, X, EOF_INIT, EOF_INIT), got %d", c.n);
    if (c.n >= 4) {
        const int want_rules[4] = {
            TLE_RULE_A, TLE_RULE_X,
            TLE_RULE_EOF_INIT, TLE_RULE_EOF_INIT,
        };
        const int want_depths[4] = { 0, 1, 1, 0 };
        for (int i = 0; i < 4; i++) {
            EXPECT(c.rules[i] == want_rules[i],
                   "[%d] rule=%d want=%d", i, c.rules[i], want_rules[i]);
            EXPECT(c.depths[i] == want_depths[i],
                   "[%d] depth=%d want=%d",
                   i, c.depths[i], want_depths[i]);
        }
    }
    EXPECT(TleLexIncludeDepth(yyl) == 0, "fully unwound");
    TleLexFree(yyl, free);
    if (fails == saved) printf("test_eof_with_include: PASS\n");
    return fails - saved;
}

int main(void) {
    test_no_eof_rules_returns_ok();
    test_eof_initial_emits();
    test_eof_state_qualified();
    test_eof_error_at();
    test_eof_with_include();
    if (fails == 0) {
        printf("\ntest_lex_eof: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr, "\ntest_lex_eof: %d sub-test failure(s)\n", fails);
    return 1;
}
