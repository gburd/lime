/*
** tests/test_lex_actions.c -- M3.4 integration test for
** action-body inlining + LEX_* macros.
**
** Driven by meson custom_target: lime -X test_lex_actions_grammar.lex
** produces test_lex_actions_grammar_lex.{c,h}; this driver compiles
** against the generated header and exercises each action primitive
** end-to-end (parse -> resolve -> compile -> emit -> cc -> run).
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_lex_actions_grammar_lex.h"

static int fails = 0;

#define EXPECT(cond, ...) do {                                  \
    if (!(cond)) {                                              \
        fprintf(stderr, "  %s:%d: ", __func__, __LINE__);       \
        fprintf(stderr, __VA_ARGS__);                           \
        fprintf(stderr, "\n");                                  \
        fails++;                                                \
    }                                                           \
} while (0)

#define MAX_TOKENS 32

struct capture {
    int    n;
    int    rules[MAX_TOKENS];
    char   texts[MAX_TOKENS][32];
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

/* ----- sub-tests ----- */

/* Empty action body: must still auto-emit per the M3.3 contract.
** This is the regression guard against M3.4 breaking existing
** grammars whose authors relied on auto-emit. */
static int test_auto_emit_empty_body(void) {
    int saved = fails;
    TlaLexer *yyl = TlaLexAlloc(malloc);
    EXPECT(yyl != NULL, "alloc returned NULL");
    if (!yyl) return fails - saved;

    struct capture c = {0};
    TlaLexResult r = TlaLexFeedBytes(yyl, "+", 1, emit_cb, &c);
    EXPECT(r == TLA_LEX_OK, "expected LEX_OK, got %d", r);
    EXPECT(c.n == 1, "want 1 token, got %d", c.n);
    if (c.n >= 1) {
        EXPECT(c.rules[0] == TLA_RULE_PLUS,
               "expected PLUS, got rule=%d", c.rules[0]);
        EXPECT(strcmp(c.texts[0], "+") == 0,
               "want \"+\", got \"%s\"", c.texts[0]);
    }
    TlaLexFree(yyl, free);
    if (fails == saved) printf("test_auto_emit_empty_body: PASS\n");
    return fails - saved;
}

/* LEX_SKIP() must suppress the auto-emit for the whitespace rule
** but still consume bytes and let neighbouring tokens emit. */
static int test_lex_skip_whitespace(void) {
    int saved = fails;
    TlaLexer *yyl = TlaLexAlloc(malloc);
    EXPECT(yyl != NULL, "alloc returned NULL");
    if (!yyl) return fails - saved;

    struct capture c = {0};
    /* "abc   xyz" -> ident + skipped ws + ident.  Capture should
    ** see exactly 2 tokens (both IDENT). */
    TlaLexResult r = TlaLexFeedBytes(yyl, "abc   xyz", 9, emit_cb, &c);
    EXPECT(r == TLA_LEX_OK, "expected LEX_OK, got %d", r);
    EXPECT(c.n == 2, "want 2 tokens (whitespace skipped), got %d", c.n);
    if (c.n >= 2) {
        EXPECT(c.rules[0] == TLA_RULE_IDENT &&
               strcmp(c.texts[0], "abc") == 0,
               "[0]: rule=%d \"%s\"", c.rules[0], c.texts[0]);
        EXPECT(c.rules[1] == TLA_RULE_IDENT &&
               strcmp(c.texts[1], "xyz") == 0,
               "[1]: rule=%d \"%s\"", c.rules[1], c.texts[1]);
    }
    TlaLexFree(yyl, free);
    if (fails == saved) printf("test_lex_skip_whitespace: PASS\n");
    return fails - saved;
}

/* LEX_EMIT(rule) must emit the supplied rule code, not the
** matched rule.  The kw rule matches "if" by length tie /
** declaration order over ident, but its action calls
** LEX_EMIT(TLA_RULE_IDENT), so the emitted token is IDENT.
** Confirms the keyword-lookup pattern from docs/LEXER_DESIGN.md. */
static int test_lex_emit_overrides_rule(void) {
    int saved = fails;
    TlaLexer *yyl = TlaLexAlloc(malloc);
    EXPECT(yyl != NULL, "alloc returned NULL");
    if (!yyl) return fails - saved;

    struct capture c = {0};
    TlaLexResult r = TlaLexFeedBytes(yyl, "if", 2, emit_cb, &c);
    EXPECT(r == TLA_LEX_OK, "expected LEX_OK, got %d", r);
    EXPECT(c.n == 1, "want 1 token, got %d", c.n);
    if (c.n >= 1) {
        EXPECT(c.rules[0] == TLA_RULE_IDENT,
               "expected IDENT (LEX_EMIT override), got rule=%d (KW=%d)",
               c.rules[0], TLA_RULE_KW);
        EXPECT(strcmp(c.texts[0], "if") == 0,
               "want \"if\", got \"%s\"", c.texts[0]);
    }
    TlaLexFree(yyl, free);
    if (fails == saved) printf("test_lex_emit_overrides_rule: PASS\n");
    return fails - saved;
}

/* LEX_TRANSITION() must change lex->state across iterations.
** "!" transitions to SAW_BANG (an exclusive state where only
** the <SAW_BANG> reset rule matches); "?" in SAW_BANG transitions
** back to INITIAL.  Verify both directions and the side-effect
** that, after just "!", the state is SAW_BANG. */
static int test_lex_transition(void) {
    int saved = fails;

    /* Halt halfway through the round-trip: feed "!", confirm
    ** state advanced. */
    {
        TlaLexer *yyl = TlaLexAlloc(malloc);
        EXPECT(yyl != NULL, "alloc returned NULL");
        if (!yyl) return fails - saved;
        EXPECT(TlaLexCurrentState(yyl) == TLA_STATE_INITIAL,
               "initial state want %d got %d",
               TLA_STATE_INITIAL, TlaLexCurrentState(yyl));
        struct capture c = {0};
        TlaLexResult r = TlaLexFeedBytes(yyl, "!", 1, emit_cb, &c);
        EXPECT(r == TLA_LEX_OK, "expected LEX_OK, got %d", r);
        EXPECT(c.n == 1, "want 1 token, got %d", c.n);
        EXPECT(TlaLexCurrentState(yyl) == TLA_STATE_SAW_BANG,
               "after '!', state want %d got %d",
               TLA_STATE_SAW_BANG, TlaLexCurrentState(yyl));
        TlaLexFree(yyl, free);
    }

    /* Full round-trip: "!?" leaves us back in INITIAL. */
    {
        TlaLexer *yyl = TlaLexAlloc(malloc);
        EXPECT(yyl != NULL, "alloc returned NULL");
        if (!yyl) return fails - saved;
        struct capture c = {0};
        TlaLexResult r = TlaLexFeedBytes(yyl, "!?", 2, emit_cb, &c);
        EXPECT(r == TLA_LEX_OK, "expected LEX_OK, got %d", r);
        EXPECT(c.n == 2, "want 2 tokens, got %d", c.n);
        if (c.n >= 2) {
            EXPECT(c.rules[0] == TLA_RULE_BANG,
                   "[0]: rule=%d want BANG=%d", c.rules[0], TLA_RULE_BANG);
            EXPECT(c.rules[1] == TLA_RULE_RESET,
                   "[1]: rule=%d want RESET=%d", c.rules[1], TLA_RULE_RESET);
        }
        EXPECT(TlaLexCurrentState(yyl) == TLA_STATE_INITIAL,
               "after '!?', state want INITIAL=%d got %d",
               TLA_STATE_INITIAL, TlaLexCurrentState(yyl));
        TlaLexFree(yyl, free);
    }

    if (fails == saved) printf("test_lex_transition: PASS\n");
    return fails - saved;
}

/* LEX_TERMINATE() must return LEX_OK with no further matches and
** stop the feed loop cleanly.  Feed "abc;def": the ";" rule fires
** terminate; "def" must be left unconsumed (no token captured). */
static int test_lex_terminate(void) {
    int saved = fails;
    TlaLexer *yyl = TlaLexAlloc(malloc);
    EXPECT(yyl != NULL, "alloc returned NULL");
    if (!yyl) return fails - saved;

    struct capture c = {0};
    TlaLexResult r = TlaLexFeedBytes(yyl, "abc;def", 7, emit_cb, &c);
    EXPECT(r == TLA_LEX_OK, "expected LEX_OK after terminate, got %d", r);
    /* Two tokens: "abc" (auto-emit IDENT), ";" (auto-emit STOP --
    ** LEX_TERMINATE() does not call LEX_EMIT/LEX_SKIP, so the
    ** auto-emit fallback fires for the ";" itself).  "def" is
    ** dropped because the loop exited before re-entering. */
    EXPECT(c.n == 2, "want 2 tokens (abc, ;), got %d", c.n);
    if (c.n >= 2) {
        EXPECT(c.rules[0] == TLA_RULE_IDENT &&
               strcmp(c.texts[0], "abc") == 0,
               "[0]: rule=%d \"%s\"", c.rules[0], c.texts[0]);
        EXPECT(c.rules[1] == TLA_RULE_STOP &&
               strcmp(c.texts[1], ";") == 0,
               "[1]: rule=%d \"%s\"", c.rules[1], c.texts[1]);
    }
    EXPECT(TlaLexErrorMessage(yyl) == NULL,
           "terminate must not set err_msg, got \"%s\"",
           TlaLexErrorMessage(yyl));
    TlaLexFree(yyl, free);
    if (fails == saved) printf("test_lex_terminate: PASS\n");
    return fails - saved;
}

/* LEX_ERROR_AT(msg) must return LEX_ERROR and stash msg for
** LexErrorMessage.  The "@" character has no other rule, so it
** unambiguously hits the err rule.  The IDENT before it must
** still emit (the error fires AFTER the matching iteration's
** action body, so prior iterations are unaffected). */
static int test_lex_error_at(void) {
    int saved = fails;
    TlaLexer *yyl = TlaLexAlloc(malloc);
    EXPECT(yyl != NULL, "alloc returned NULL");
    if (!yyl) return fails - saved;

    struct capture c = {0};
    TlaLexResult r = TlaLexFeedBytes(yyl, "abc@xyz", 7, emit_cb, &c);
    EXPECT(r == TLA_LEX_ERROR, "expected LEX_ERROR, got %d", r);
    /* "abc" auto-emits before the "@" rule fires its error. */
    EXPECT(c.n == 1, "want 1 token (abc) before error, got %d", c.n);
    if (c.n >= 1) {
        EXPECT(c.rules[0] == TLA_RULE_IDENT &&
               strcmp(c.texts[0], "abc") == 0,
               "[0]: rule=%d \"%s\"", c.rules[0], c.texts[0]);
    }
    const char *msg = TlaLexErrorMessage(yyl);
    EXPECT(msg != NULL, "err_msg must be set");
    if (msg) {
        EXPECT(strcmp(msg, "custom @ error") == 0,
               "err_msg want \"custom @ error\" got \"%s\"", msg);
    }
    TlaLexFree(yyl, free);
    if (fails == saved) printf("test_lex_error_at: PASS\n");
    return fails - saved;
}

int main(void) {
    test_auto_emit_empty_body();
    test_lex_skip_whitespace();
    test_lex_emit_overrides_rule();
    test_lex_transition();
    test_lex_terminate();
    test_lex_error_at();
    if (fails == 0) {
        printf("\ntest_lex_actions: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr, "\ntest_lex_actions: %d sub-test failure(s)\n", fails);
    return 1;
}
