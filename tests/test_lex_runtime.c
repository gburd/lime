/*
** tests/test_lex_runtime.c -- M3.3 integration test for the
** push-driven runtime.
**
** Driven by meson custom_target: lime -X
** test_lex_runtime_grammar.lex produces test_lex_runtime_grammar_lex.c
** and test_lex_runtime_grammar_lex.h, which this driver compiles
** against.  The whole pipeline (parse -> resolve -> compile ->
** emit -> cc -> run) is verified by this test passing.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_lex_runtime_grammar_lex.h"

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
    char   texts[MAX_TOKENS][64];
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

static int feed_and_capture(const char *input, struct capture *out) {
    TlxLexer *yyl = TlxLexAlloc(malloc);
    if (!yyl) return -1;
    out->n = 0;
    TlxLexResult r = TlxLexFeedBytes(yyl, input, strlen(input),
                                     emit_cb, out);
    if (r == TLX_LEX_OK) {
        TlxLexFeedEOF(yyl, emit_cb, out);
    }
    int rc = (r == TLX_LEX_OK) ? 0 : 1;
    TlxLexFree(yyl, free);
    return rc;
}

/* ----- sub-tests ----- */

static int test_simple_stream(void) {
    int saved = fails;
    struct capture c = {0};
    EXPECT(feed_and_capture("x+42 abc", &c) == 0, "feed failed");
    EXPECT(c.n == 5, "n_tokens=%d want 5", c.n);
    if (c.n >= 5) {
        EXPECT(c.rules[0] == TLX_RULE_IDENT && strcmp(c.texts[0], "x") == 0,
               "[0]: %d \"%s\"", c.rules[0], c.texts[0]);
        EXPECT(c.rules[1] == TLX_RULE_PLUS && strcmp(c.texts[1], "+") == 0,
               "[1]: %d \"%s\"", c.rules[1], c.texts[1]);
        EXPECT(c.rules[2] == TLX_RULE_NUM && strcmp(c.texts[2], "42") == 0,
               "[2]: %d \"%s\"", c.rules[2], c.texts[2]);
        EXPECT(c.rules[3] == TLX_RULE_WS && strcmp(c.texts[3], " ") == 0,
               "[3]: %d \"%s\"", c.rules[3], c.texts[3]);
        EXPECT(c.rules[4] == TLX_RULE_IDENT && strcmp(c.texts[4], "abc") == 0,
               "[4]: %d \"%s\"", c.rules[4], c.texts[4]);
    }
    if (fails == saved) printf("test_simple_stream: PASS\n");
    return fails - saved;
}

static int test_longest_match(void) {
    int saved = fails;
    /* Multi-digit num and multi-char ident.  Verify longest match. */
    struct capture c = {0};
    EXPECT(feed_and_capture("hello1234world", &c) == 0, "feed failed");
    /* "hello" is ident (5 bytes), then "1234" is num (4), then
    ** "world" is ident (5). */
    EXPECT(c.n == 3, "n_tokens=%d want 3", c.n);
    if (c.n >= 3) {
        EXPECT(c.rules[0] == TLX_RULE_IDENT && strcmp(c.texts[0], "hello") == 0,
               "[0]: \"%s\"", c.texts[0]);
        EXPECT(c.rules[1] == TLX_RULE_NUM && strcmp(c.texts[1], "1234") == 0,
               "[1]: \"%s\"", c.texts[1]);
        EXPECT(c.rules[2] == TLX_RULE_IDENT && strcmp(c.texts[2], "world") == 0,
               "[2]: \"%s\"", c.texts[2]);
    }
    if (fails == saved) printf("test_longest_match: PASS\n");
    return fails - saved;
}

static int test_unmatched_input(void) {
    int saved = fails;
    /* Capital letters aren't in any rule's pattern -- should
    ** fail with LEX_ERROR. */
    TlxLexer *yyl = TlxLexAlloc(malloc);
    EXPECT(yyl != NULL, "alloc returned NULL");
    if (!yyl) return fails - saved;

    struct capture c = {0};
    TlxLexResult r = TlxLexFeedBytes(yyl, "ABC", 3, emit_cb, &c);
    EXPECT(r == TLX_LEX_ERROR, "expected LEX_ERROR on uppercase, got %d", r);
    EXPECT(TlxLexErrorMessage(yyl) != NULL, "error message should be set");
    if (TlxLexErrorMessage(yyl)) {
        EXPECT(strstr(TlxLexErrorMessage(yyl), "unmatched") != NULL,
               "err msg: \"%s\"", TlxLexErrorMessage(yyl));
    }
    TlxLexFree(yyl, free);
    if (fails == saved) printf("test_unmatched_input: PASS\n");
    return fails - saved;
}

static int test_state_get_set(void) {
    int saved = fails;
    TlxLexer *yyl = TlxLexAlloc(malloc);
    EXPECT(yyl != NULL, "alloc returned NULL");
    if (!yyl) return fails - saved;

    EXPECT(TlxLexCurrentState(yyl) == TLX_STATE_INITIAL,
           "initial state should be INITIAL (0), got %d",
           TlxLexCurrentState(yyl));
    /* Set to a different value (none others declared, so set to
    ** something nonsensical and verify it round-trips). */
    TlxLexSetState(yyl, 42);
    EXPECT(TlxLexCurrentState(yyl) == 42,
           "after SetState(42), got %d", TlxLexCurrentState(yyl));
    TlxLexFree(yyl, free);
    if (fails == saved) printf("test_state_get_set: PASS\n");
    return fails - saved;
}

static int test_alloc_failure_safe(void) {
    int saved = fails;
    /* Pass NULL malloc -- TlxLexAlloc should return NULL. */
    TlxLexer *yyl = TlxLexAlloc(NULL);
    EXPECT(yyl == NULL, "alloc(NULL) should return NULL");
    /* Free of NULL is safe. */
    TlxLexFree(NULL, free);
    /* CurrentState of NULL returns -1. */
    EXPECT(TlxLexCurrentState(NULL) == -1,
           "CurrentState(NULL) should return -1");
    /* ErrorMessage of NULL returns NULL. */
    EXPECT(TlxLexErrorMessage(NULL) == NULL,
           "ErrorMessage(NULL) should return NULL");
    if (fails == saved) printf("test_alloc_failure_safe: PASS\n");
    return fails - saved;
}

int main(void) {
    test_simple_stream();
    test_longest_match();
    test_unmatched_input();
    test_state_get_set();
    test_alloc_failure_safe();
    if (fails == 0) {
        printf("\ntest_lex_runtime: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr, "\ntest_lex_runtime: %d sub-test failure(s)\n", fails);
    return 1;
}
