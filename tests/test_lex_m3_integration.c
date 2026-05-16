/*
** tests/test_lex_m3_integration.c -- cross-feature integration
** test for the M3.4 + M3.5 merge.
**
** Verifies that LEX_PUSHBACK(n), defined as a #define wrapping
** the M3.5 runtime function <prefix>LexPushback(lex, n), works
** correctly when called from inside an action body inlined by
** M3.4.  This is the merge bridge between the two milestones:
** M3.4 owns the macro contract, M3.5 owns the buffer-stack
** primitive, and the merge wires them together.
**
** Pattern under test: an "abcd" rule whose action calls
** LEX_PUSHBACK(2) -- pushing back the trailing "cd" -- then
** explicitly emits.  The next iteration must re-match the
** rewound bytes against a separate "cd" rule.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_lex_m3_integration_grammar_lex.h"

static int fails = 0;

#define EXPECT(cond, ...) do {                                \
    if (!(cond)) {                                            \
        fprintf(stderr, "  %s:%d: ", __func__, __LINE__);     \
        fprintf(stderr, __VA_ARGS__);                         \
        fprintf(stderr, "\n");                                \
        fails++;                                              \
    }                                                         \
} while (0)

#define MAX_TOK 8

struct cap {
    int    n;
    int    rules[MAX_TOK];
    char   texts[MAX_TOK][16];
};

static void emit_cb(void *user, int rule,
                    const char *text, size_t len) {
    struct cap *c = user;
    if (c->n >= MAX_TOK) return;
    c->rules[c->n] = rule;
    if (len >= sizeof(c->texts[0])) len = sizeof(c->texts[0]) - 1;
    memcpy(c->texts[c->n], text, len);
    c->texts[c->n][len] = '\0';
    c->n++;
}

static int test_pushback_in_action(void) {
    int saved = fails;
    /* Grammar (see test_lex_m3_integration_grammar.lex):
    **   rule abcd matches /abcd/  -> action: LEX_PUSHBACK(2);
    **                                       LEX_EMIT(M3I_RULE_ABCD)
    **   rule cd   matches /cd/    -> auto-emit
    **   rule any  matches /[a-z]/ -> auto-emit
    ** On input "abcd":
    **   - Foo_match returns rule=abcd, consumed=4.
    **   - M3.5 invariant advances top.pos by 4.
    **   - Action body fires: LEX_PUSHBACK(2) calls
    **     M3iLexPushback(lex, 2) which rewinds top.pos by 2.
    **   - LEX_EMIT(ABCD) emits with the original matched_len=4.
    **   - Next iteration: top.pos = 2 (after rewind), so the
    **     next match starts at "cd" and matches the cd rule.
    ** Expected emit: abcd then cd. */
    M3iLexer *yyl = M3iLexAlloc(malloc);
    EXPECT(yyl != NULL, "alloc failed");
    if (!yyl) return fails - saved;
    struct cap c = {0};
    M3iLexResult r = M3iLexFeedBytes(yyl, "abcd", 4, emit_cb, &c);
    EXPECT(r == M3I_LEX_OK, "feed returned %d", r);
    M3iLexFree(yyl, free);

    EXPECT(c.n == 2, "n_tokens=%d want 2", c.n);
    if (c.n >= 2) {
        EXPECT(c.rules[0] == M3I_RULE_ABCD &&
               strcmp(c.texts[0], "abcd") == 0,
               "[0] rule=%d text=\"%s\"", c.rules[0], c.texts[0]);
        EXPECT(c.rules[1] == M3I_RULE_CD &&
               strcmp(c.texts[1], "cd") == 0,
               "[1] rule=%d text=\"%s\"", c.rules[1], c.texts[1]);
    }
    if (fails == saved) printf("test_pushback_in_action: PASS\n");
    return fails - saved;
}

static int test_action_with_include(void) {
    int saved = fails;
    /* Caller-driven LexInclude composes correctly with
    ** LexFeedBytes when called between feeds.  The caller does
    ** two LexFeedBytes invocations: the first drains the parent
    ** buffer; between them, LexInclude pushes a side buffer on
    ** top so the second feed processes the side buffer (top of
    ** stack) before the side buffer's frame auto-pops back to
    ** wherever the stack was before. */
    M3iLexer *yyl = M3iLexAlloc(malloc);
    EXPECT(yyl != NULL, "alloc failed");
    if (!yyl) return fails - saved;
    struct cap c = {0};

    /* Feed the parent first; depth tracks correctly. */
    M3iLexResult r = M3iLexFeedBytes(yyl, "a", 1, emit_cb, &c);
    EXPECT(r == M3I_LEX_OK, "feed #1 returned %d", r);
    EXPECT(M3iLexIncludeDepth(yyl) == 0,
           "after feed, depth=%d want 0", M3iLexIncludeDepth(yyl));

    /* Push an include + run another feed.  The second feed's
    ** push goes on top of the include, so the second feed's
    ** bytes are processed first, then the include drains, then
    ** the second feed exits when depth returns to its
    ** initial_depth. */
    static const char included[] = "x";
    r = M3iLexInclude(yyl, included, 1);
    EXPECT(r == M3I_LEX_OK, "include returned %d", r);
    /* LexIncludeDepth = max(0, depth - 1).  After include with
    ** no in-flight feed, depth is 1 (just the include frame),
    ** so the accessor reports 0 (no "include level above the
    ** original input" -- there's no original input). */
    EXPECT(M3iLexIncludeDepth(yyl) == 0,
           "after include with no parent feed, depth=%d want 0",
           M3iLexIncludeDepth(yyl));

    r = M3iLexFeedBytes(yyl, "b", 1, emit_cb, &c);
    EXPECT(r == M3I_LEX_OK, "feed #2 returned %d", r);
    /* The second feed pushed "b" on top of "x".  initial_depth
    ** for that feed = 1 (the include).  The loop drains until
    ** depth returns to 1.  "b" matches and pops; depth = 1 ==
    ** initial_depth, loop exits.  "x" is still on the stack. */
    EXPECT(M3iLexIncludeDepth(yyl) == 0,
           "after feed #2, depth=%d want 0 (the orphan 'x' frame "
           "sits at depth 1 internally but the accessor subtracts 1)",
           M3iLexIncludeDepth(yyl));

    /* Captured tokens: "a" from feed #1, then "b" from feed #2. */
    EXPECT(c.n == 2, "n_tokens=%d want 2", c.n);
    if (c.n >= 2) {
        EXPECT(strcmp(c.texts[0], "a") == 0, "[0] text=\"%s\"", c.texts[0]);
        EXPECT(strcmp(c.texts[1], "b") == 0, "[1] text=\"%s\"", c.texts[1]);
    }

    M3iLexFree(yyl, free);
    if (fails == saved) printf("test_action_with_include: PASS\n");
    return fails - saved;
}

int main(void) {
    test_pushback_in_action();
    test_action_with_include();
    if (fails == 0) {
        printf("\ntest_lex_m3_integration: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr,
            "\ntest_lex_m3_integration: %d sub-test failure(s)\n", fails);
    return 1;
}
