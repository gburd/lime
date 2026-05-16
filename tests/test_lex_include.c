/*
** tests/test_lex_include.c -- M3.5 integration test for the
** runtime buffer stack (LexInclude / LexIncludeDepth) and
** runtime pushback (LexPushback).
**
** Driven by meson custom_target: lime -X
** test_lex_include_grammar.lex produces test_lex_include_grammar_lex.c
** and test_lex_include_grammar_lex.h.  This driver compiles
** against the generated lexer and exercises:
**
**   1. single-buffer behavior (regression of M3.3 path)
**   2. one-level include from inside `emit`
**   3. auto-pop on EOF, parent resumes
**   4. nested includes (three buffers deep)
**   5. LexPushback rewind from inside `emit`
**   6. LexPushback bounds checks (empty stack and over-pushback)
**   7. LexIncludeDepth accessor across push/pop
**
** The generated grammar declares three single-byte rules
** (a/b/c) -- enough to drive every test below; the work under
** test is the runtime, not the DFA.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_lex_include_grammar_lex.h"

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

/* ----- shared capture struct ------------------------------- */

struct capture {
    TlxiLexer *yyl;          /* set by tests that need re-entrant calls */
    int        n;
    int        rules[MAX_TOKENS];
    char       texts[MAX_TOKENS][8];
    int        depths[MAX_TOKENS]; /* IncludeDepth at emit time */

    /* Optional: per-emit "splice" trigger.  When trigger_rule
    ** matches the rule being emitted, push trigger_buf as an
    ** include and clear the trigger so it only fires once. */
    int         trigger_rule;
    const char *trigger_buf;
    size_t      trigger_len;

    /* Optional: pushback trigger.  When pb_rule matches AND
    ** pb_remaining > 0, call LexPushback(pb_n) and decrement. */
    int    pb_rule;
    size_t pb_n;
    int    pb_remaining;

    /* Out: last LexPushback result (set whenever we trigger). */
    TlxiLexResult last_pb_result;
};

static void emit_cb(void *user, int rule,
                    const char *text, size_t len) {
    struct capture *c = user;
    if (c->n < MAX_TOKENS) {
        c->rules[c->n] = rule;
        size_t copy = len < sizeof(c->texts[0]) - 1
                        ? len : sizeof(c->texts[0]) - 1;
        memcpy(c->texts[c->n], text, copy);
        c->texts[c->n][copy] = '\0';
        c->depths[c->n] = c->yyl ? TlxiLexIncludeDepth(c->yyl) : -1;
        c->n++;
    }
    if (c->yyl && rule == c->trigger_rule && c->trigger_buf) {
        const char *buf = c->trigger_buf;
        size_t      bl  = c->trigger_len;
        c->trigger_buf  = NULL;       /* fire once */
        c->trigger_rule = -1;
        TlxiLexInclude(c->yyl, buf, bl);
    }
    if (c->yyl && rule == c->pb_rule && c->pb_remaining > 0) {
        c->pb_remaining--;
        c->last_pb_result = TlxiLexPushback(c->yyl, c->pb_n);
    }
}

/* ----- sub-tests ------------------------------------------- */

static int test_single_buffer_regression(void) {
    int saved = fails;
    TlxiLexer *yyl = TlxiLexAlloc(malloc);
    EXPECT(yyl != NULL, "alloc");
    if (!yyl) return fails - saved;

    EXPECT(TlxiLexIncludeDepth(yyl) == 0, "depth pre-feed");

    struct capture c = {0};
    c.yyl = yyl;
    TlxiLexResult r = TlxiLexFeedBytes(yyl, "abc", 3, emit_cb, &c);
    EXPECT(r == TLXI_LEX_OK, "feed result %d", r);
    EXPECT(c.n == 3, "n=%d want 3", c.n);
    if (c.n == 3) {
        EXPECT(c.rules[0] == TLXI_RULE_A, "[0] rule=%d", c.rules[0]);
        EXPECT(c.rules[1] == TLXI_RULE_B, "[1] rule=%d", c.rules[1]);
        EXPECT(c.rules[2] == TLXI_RULE_C, "[2] rule=%d", c.rules[2]);
        EXPECT(c.depths[0] == 0, "[0] depth=%d", c.depths[0]);
        EXPECT(c.depths[1] == 0, "[1] depth=%d", c.depths[1]);
        EXPECT(c.depths[2] == 0, "[2] depth=%d", c.depths[2]);
    }
    EXPECT(TlxiLexIncludeDepth(yyl) == 0, "depth post-feed");
    TlxiLexFree(yyl, free);
    if (fails == saved) printf("test_single_buffer_regression: PASS\n");
    return fails - saved;
}

static int test_one_level_include(void) {
    int saved = fails;
    TlxiLexer *yyl = TlxiLexAlloc(malloc);
    if (!yyl) { fails++; return 1; }

    /* Parent: "ab".  When emit sees `a`, splice in "cc".
    ** Expected emit order: a, c, c, b.  Depths: 0, 1, 1, 0. */
    struct capture c = {0};
    c.yyl           = yyl;
    c.trigger_rule  = TLXI_RULE_A;
    c.trigger_buf   = "cc";
    c.trigger_len   = 2;

    TlxiLexResult r = TlxiLexFeedBytes(yyl, "ab", 2, emit_cb, &c);
    EXPECT(r == TLXI_LEX_OK, "feed result %d", r);
    EXPECT(c.n == 4, "n=%d want 4", c.n);
    if (c.n == 4) {
        EXPECT(c.rules[0] == TLXI_RULE_A, "[0] %d", c.rules[0]);
        EXPECT(c.rules[1] == TLXI_RULE_C, "[1] %d", c.rules[1]);
        EXPECT(c.rules[2] == TLXI_RULE_C, "[2] %d", c.rules[2]);
        EXPECT(c.rules[3] == TLXI_RULE_B, "[3] %d", c.rules[3]);
        EXPECT(c.depths[0] == 0, "[0] depth=%d", c.depths[0]);
        EXPECT(c.depths[1] == 1, "[1] depth=%d", c.depths[1]);
        EXPECT(c.depths[2] == 1, "[2] depth=%d", c.depths[2]);
        EXPECT(c.depths[3] == 0, "[3] depth=%d after auto-pop", c.depths[3]);
    }
    EXPECT(TlxiLexIncludeDepth(yyl) == 0, "depth fully unwound");
    TlxiLexFree(yyl, free);
    if (fails == saved) printf("test_one_level_include: PASS\n");
    return fails - saved;
}

/* For the nested-include test we use a chained trigger: the
** first `a` splices "b", and when that `b` is emitted from the
** included buffer we splice "c" on top of it.  This exercises
** depth=0 -> 1 -> 2 and full unwind. */
struct chain_capture {
    TlxiLexer *yyl;
    int    n;
    int    rules[MAX_TOKENS];
    int    depths[MAX_TOKENS];
};

static void chain_emit(void *user, int rule,
                       const char *text, size_t len) {
    (void)text; (void)len;
    struct chain_capture *c = user;
    if (c->n < MAX_TOKENS) {
        c->rules[c->n]  = rule;
        c->depths[c->n] = TlxiLexIncludeDepth(c->yyl);
        c->n++;
    }
    if (rule == TLXI_RULE_A) {
        TlxiLexInclude(c->yyl, "b", 1);
    } else if (rule == TLXI_RULE_B && TlxiLexIncludeDepth(c->yyl) == 1) {
        /* Only the b that came from the first include splices c.
        ** Guard on depth to avoid an infinite chain. */
        TlxiLexInclude(c->yyl, "c", 1);
    }
}

static int test_nested_includes(void) {
    int saved = fails;
    TlxiLexer *yyl = TlxiLexAlloc(malloc);
    if (!yyl) { fails++; return 1; }

    struct chain_capture c = {0};
    c.yyl = yyl;

    /* Parent: "a".  After a -> push "b"; after b -> push "c".
    ** Expected: a (depth 0), b (depth 1), c (depth 2). */
    TlxiLexResult r = TlxiLexFeedBytes(yyl, "a", 1, chain_emit, &c);
    EXPECT(r == TLXI_LEX_OK, "feed result %d", r);
    EXPECT(c.n == 3, "n=%d want 3", c.n);
    if (c.n == 3) {
        EXPECT(c.rules[0] == TLXI_RULE_A && c.depths[0] == 0,
               "[0] rule=%d depth=%d", c.rules[0], c.depths[0]);
        EXPECT(c.rules[1] == TLXI_RULE_B && c.depths[1] == 1,
               "[1] rule=%d depth=%d", c.rules[1], c.depths[1]);
        EXPECT(c.rules[2] == TLXI_RULE_C && c.depths[2] == 2,
               "[2] rule=%d depth=%d", c.rules[2], c.depths[2]);
    }
    EXPECT(TlxiLexIncludeDepth(yyl) == 0, "depth fully unwound");
    TlxiLexFree(yyl, free);
    if (fails == saved) printf("test_nested_includes: PASS\n");
    return fails - saved;
}

static int test_pushback_rewind(void) {
    int saved = fails;
    TlxiLexer *yyl = TlxiLexAlloc(malloc);
    if (!yyl) { fails++; return 1; }

    /* Parent: "abcabc".  On the FIRST `c`, pushback 2.  That
    ** rewinds the top-of-stack frame's pos from 3 to 1, so the
    ** loop re-matches "bcabc" -> b, c, a, b, c on top of the
    ** original a, b, c.  Expected total: a,b,c,b,c,a,b,c (8). */
    struct capture c = {0};
    c.yyl          = yyl;
    c.pb_rule      = TLXI_RULE_C;
    c.pb_n         = 2;
    c.pb_remaining = 1;

    TlxiLexResult r = TlxiLexFeedBytes(yyl, "abcabc", 6, emit_cb, &c);
    EXPECT(r == TLXI_LEX_OK, "feed result %d", r);
    EXPECT(c.last_pb_result == TLXI_LEX_OK,
           "pushback result %d", c.last_pb_result);
    EXPECT(c.n == 8, "n=%d want 8", c.n);
    if (c.n == 8) {
        const int want[8] = {
            TLXI_RULE_A, TLXI_RULE_B, TLXI_RULE_C,
            TLXI_RULE_B, TLXI_RULE_C,
            TLXI_RULE_A, TLXI_RULE_B, TLXI_RULE_C,
        };
        for (int i = 0; i < 8; i++) {
            EXPECT(c.rules[i] == want[i],
                   "[%d] rule=%d want=%d", i, c.rules[i], want[i]);
        }
    }
    TlxiLexFree(yyl, free);
    if (fails == saved) printf("test_pushback_rewind: PASS\n");
    return fails - saved;
}

static int test_pushback_bounds(void) {
    int saved = fails;
    /* (a) empty-stack case: directly after Alloc the stack is
    ** empty, so any pushback must fail. */
    TlxiLexer *yyl = TlxiLexAlloc(malloc);
    if (!yyl) { fails++; return 1; }
    TlxiLexResult r = TlxiLexPushback(yyl, 1);
    EXPECT(r == TLXI_LEX_ERROR, "empty-stack pushback should fail");
    EXPECT(TlxiLexErrorMessage(yyl) != NULL, "err_msg set");
    if (TlxiLexErrorMessage(yyl)) {
        EXPECT(strstr(TlxiLexErrorMessage(yyl), "empty") != NULL,
               "err_msg=\"%s\"", TlxiLexErrorMessage(yyl));
    }

    /* (b) over-pushback: from inside emit on the first `a`,
    ** pos is 1; pushback(99) must fail with "exceeds buffered
    ** prefix".  The lex stream itself completes normally
    ** because LexPushback's failure is independent of the
    ** matching loop. */
    struct capture c = {0};
    c.yyl          = yyl;
    c.pb_rule      = TLXI_RULE_A;
    c.pb_n         = 99;
    c.pb_remaining = 1;
    r = TlxiLexFeedBytes(yyl, "ab", 2, emit_cb, &c);
    EXPECT(r == TLXI_LEX_OK, "feed result %d", r);
    EXPECT(c.n == 2, "n=%d want 2", c.n);
    EXPECT(c.last_pb_result == TLXI_LEX_ERROR,
           "over-pushback should fail, got %d", c.last_pb_result);
    EXPECT(TlxiLexErrorMessage(yyl) != NULL, "err_msg set");
    if (TlxiLexErrorMessage(yyl)) {
        EXPECT(strstr(TlxiLexErrorMessage(yyl), "exceeds") != NULL,
               "err_msg=\"%s\"", TlxiLexErrorMessage(yyl));
    }
    TlxiLexFree(yyl, free);
    if (fails == saved) printf("test_pushback_bounds: PASS\n");
    return fails - saved;
}

static int test_include_depth_accessor(void) {
    int saved = fails;
    /* IncludeDepth(NULL) returns 0. */
    EXPECT(TlxiLexIncludeDepth(NULL) == 0, "depth(NULL) should be 0");

    TlxiLexer *yyl = TlxiLexAlloc(malloc);
    if (!yyl) { fails++; return 1; }

    /* Direct LexInclude calls outside FeedBytes also push;
    ** the bottom frame is conceptually the "input being
    ** processed", so two LexInclude calls back-to-back take
    ** depth 0 -> 0 (first push, considered the bottom) -> 1
    ** (second push, an actual include on top). */
    EXPECT(TlxiLexIncludeDepth(yyl) == 0, "fresh");
    TlxiLexResult r = TlxiLexInclude(yyl, "x", 1);
    EXPECT(r == TLXI_LEX_OK, "first include %d", r);
    EXPECT(TlxiLexIncludeDepth(yyl) == 0,
           "after 1 push, depth=%d (bottom frame doesn't count)",
           TlxiLexIncludeDepth(yyl));
    r = TlxiLexInclude(yyl, "y", 1);
    EXPECT(r == TLXI_LEX_OK, "second include %d", r);
    EXPECT(TlxiLexIncludeDepth(yyl) == 1,
           "after 2 pushes, depth=%d", TlxiLexIncludeDepth(yyl));
    TlxiLexFree(yyl, free);
    if (fails == saved) printf("test_include_depth_accessor: PASS\n");
    return fails - saved;
}

int main(void) {
    test_single_buffer_regression();
    test_one_level_include();
    test_nested_includes();
    test_pushback_rewind();
    test_pushback_bounds();
    test_include_depth_accessor();
    if (fails == 0) {
        printf("\ntest_lex_include: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr, "\ntest_lex_include: %d sub-test failure(s)\n", fails);
    return 1;
}
