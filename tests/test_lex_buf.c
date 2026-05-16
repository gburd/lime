/*
** tests/test_lex_buf.c -- M3.7 (%literal_buffer runtime
** emission) + P0-NEW-9 (%include block emission) integration
** test driver.
**
** Driven by meson custom_target: lime -X
** test_lex_buf_grammar.lex produces test_lex_buf_grammar_lex.c
** + .h, which this driver compiles against.  Verifies:
**   - Buffers initialise to NULL/0 in LexAlloc.
**   - LEX_BUF_START / APPEND / APPEND_CH accumulate correctly.
**   - LEX_BUF_LEN and LEX_BUF_PEEK observe the same bytes.
**   - LEX_BUF_TAKE returns a NUL-terminated heap copy and
**     resets the buffer.
**   - The grow path runs on inputs that exceed `initial`.
**   - Two %literal_buffer declarations operate independently.
**   - The %include block's static helper is callable from
**     an action body (passthrough proves the linker resolved
**     the symbol -- the test would not build otherwise).
**   - LexFree cleans up a non-empty buffer when the caller
**     never invoked LEX_BUF_TAKE (no leak).
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_lex_buf_grammar_lex.h"

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
    char   texts[MAX_TOKENS][256];
    size_t lens[MAX_TOKENS];
};

static void emit_cb(void *user, int rule,
                    const char *text, size_t len) {
    struct capture *c = user;
    if (c->n >= MAX_TOKENS) return;
    c->rules[c->n] = rule;
    size_t copy = len;
    if (copy >= sizeof(c->texts[0])) copy = sizeof(c->texts[0]) - 1;
    memcpy(c->texts[c->n], text, copy);
    c->texts[c->n][copy] = '\0';
    c->lens[c->n] = len;
    c->n++;
}

static int feed(const char *input, struct capture *out) {
    BufLexer *yyl = BufLexAlloc(malloc);
    if (!yyl) return -1;
    out->n = 0;
    BufLexResult r = BufLexFeedBytes(yyl, input, strlen(input),
                                     emit_cb, out);
    if (r == BUF_LEX_OK) {
        BufLexFeedEOF(yyl, emit_cb, out);
    }
    int rc = (r == BUF_LEX_OK) ? 0 : 1;
    BufLexFree(yyl, free);
    return rc;
}

/* ----- sub-tests ----- */

/* (1) Simple accumulation: feed `"hello"`; expect a STR_CLOSE
** with buffer contents == "hello". */
static int test_simple_accum(void) {
    int saved = fails;
    struct capture c = {0};
    EXPECT(feed("\"hello\"", &c) == 0, "feed failed");
    EXPECT(c.n == 1, "n_tokens=%d want 1", c.n);
    if (c.n >= 1) {
        EXPECT(c.rules[0] == BUF_RULE_STR_CLOSE,
               "rule[0]=%d want STR_CLOSE", c.rules[0]);
        EXPECT(c.lens[0] == 5, "len[0]=%zu want 5", c.lens[0]);
        EXPECT(strcmp(c.texts[0], "hello") == 0,
               "text[0]=\"%s\"", c.texts[0]);
    }
    if (fails == saved) printf("test_simple_accum: PASS\n");
    return fails - saved;
}

/* (3) + (5): growth past `initial=4` plus implicit LEN/PEEK
** validation -- the str_close action calls LEX_BUF_LEN +
** LEX_BUF_PEEK before LEX_BUF_TAKE.  If either disagrees with
** the post-take contents, the emitted text would mismatch. */
static int test_growth(void) {
    int saved = fails;
    /* 80 'x' chars between quotes -- well past initial=4 with
    ** *2 policy; the helper grows 4 -> 8 -> 16 -> 32 -> 64 ->
    ** 128.  Use 80 to land mid-doubling for variety. */
    char input[100];
    input[0] = '"';
    memset(input + 1, 'x', 80);
    input[81] = '"';
    input[82] = '\0';
    char expected[81];
    memset(expected, 'x', 80);
    expected[80] = '\0';

    struct capture c = {0};
    EXPECT(feed(input, &c) == 0, "feed failed");
    EXPECT(c.n == 1, "n_tokens=%d want 1", c.n);
    if (c.n >= 1) {
        EXPECT(c.lens[0] == 80, "len[0]=%zu want 80", c.lens[0]);
        EXPECT(strcmp(c.texts[0], expected) == 0,
               "text mismatch: got \"%.16s...\"", c.texts[0]);
    }
    if (fails == saved) printf("test_growth: PASS\n");
    return fails - saved;
}

/* (4) %include helper reachability: the str_close action body
** calls buf_checksum() defined in the %include block.  If the
** include block were not emitted to the .c file, the action
** body would fail to compile -- this test running at all proves
** P0-NEW-9 is wired.  Additional dynamic check: the IDA path
** that uses LEX_BUF_APPEND (memcpy) round-trips a multi-byte
** payload, exercising additive grow + APPEND macro. */
static int test_include_helper_and_append(void) {
    int saved = fails;
    struct capture c = {0};
    EXPECT(feed("<world>", &c) == 0, "feed failed");
    EXPECT(c.n == 1, "n_tokens=%d want 1", c.n);
    if (c.n >= 1) {
        EXPECT(c.rules[0] == BUF_RULE_ID_CLOSE,
               "rule[0]=%d want ID_CLOSE", c.rules[0]);
        EXPECT(c.lens[0] == 5, "len[0]=%zu want 5", c.lens[0]);
        EXPECT(strcmp(c.texts[0], "world") == 0,
               "text[0]=\"%s\"", c.texts[0]);
    }
    /* Drive additive grow past initial=8: feed 30 chars. */
    char input[64] = "<";
    for (int i = 1; i < 31; i++) input[i] = (char)('a' + (i % 26));
    input[31] = '>';
    input[32] = '\0';
    struct capture c2 = {0};
    EXPECT(feed(input, &c2) == 0, "additive feed failed");
    EXPECT(c2.n == 1, "n_tokens=%d want 1", c2.n);
    if (c2.n >= 1) {
        EXPECT(c2.lens[0] == 30, "len=%zu want 30", c2.lens[0]);
    }
    if (fails == saved) printf("test_include_helper_and_append: PASS\n");
    return fails - saved;
}

/* (6) Multi-buffer: feed STR then IDA back-to-back; verify
** both produce the right close events with their own buffers
** and don't trample each other. */
static int test_multi_buffer(void) {
    int saved = fails;
    struct capture c = {0};
    EXPECT(feed("\"hi\"<there>", &c) == 0, "feed failed");
    EXPECT(c.n == 2, "n_tokens=%d want 2", c.n);
    if (c.n >= 2) {
        EXPECT(c.rules[0] == BUF_RULE_STR_CLOSE,
               "rule[0]=%d want STR_CLOSE", c.rules[0]);
        EXPECT(strcmp(c.texts[0], "hi") == 0,
               "text[0]=\"%s\"", c.texts[0]);
        EXPECT(c.rules[1] == BUF_RULE_ID_CLOSE,
               "rule[1]=%d want ID_CLOSE", c.rules[1]);
        EXPECT(strcmp(c.texts[1], "there") == 0,
               "text[1]=\"%s\"", c.texts[1]);
    }
    /* Reverse order -- IDA first, STR second. */
    struct capture c2 = {0};
    EXPECT(feed("<one>\"two\"", &c2) == 0, "feed failed (rev)");
    EXPECT(c2.n == 2, "n_tokens=%d want 2", c2.n);
    if (c2.n >= 2) {
        EXPECT(c2.rules[0] == BUF_RULE_ID_CLOSE,
               "rule[0]=%d want ID_CLOSE", c2.rules[0]);
        EXPECT(strcmp(c2.texts[0], "one") == 0,
               "text[0]=\"%s\"", c2.texts[0]);
        EXPECT(c2.rules[1] == BUF_RULE_STR_CLOSE,
               "rule[1]=%d want STR_CLOSE", c2.rules[1]);
        EXPECT(strcmp(c2.texts[1], "two") == 0,
               "text[1]=\"%s\"", c2.texts[1]);
    }
    if (fails == saved) printf("test_multi_buffer: PASS\n");
    return fails - saved;
}

/* (7) NULL-safe LexFree: leak-free cleanup of a buffer whose
** contents were never taken.  We open `<abc` and never feed
** the closing `>`, so on LexFeedBytes return the IDA buffer
** still owns 3 bytes.  LexFree must call free(scanid_buf).
** Memory cleanliness is verified externally (valgrind, ASan).
**
** Discriminator A: alloc -> accumulate -> take -> alloc again
** -> accumulate -> drop without take.  Verifies that LEX_BUF_
** TAKE's reset enables a second clean accumulation cycle.
**
** Discriminator B: lex never accumulated anything; LexFree on
** an idle lexer must not crash. */
static int test_lexfree_cleanup(void) {
    int saved = fails;

    /* (7a) -- complete cycle then dangling accumulation. */
    BufLexer *yyl = BufLexAlloc(malloc);
    EXPECT(yyl != NULL, "alloc failed");
    if (yyl) {
        struct capture c = {0};
        BufLexResult r = BufLexFeedBytes(yyl, "\"x\"", 3, emit_cb, &c);
        EXPECT(r == BUF_LEX_OK, "first feed should succeed");
        /* Buffer was taken inside str_close.  Now feed an
        ** unclosed IDA -> buffer is left dangling. */
        r = BufLexFeedBytes(yyl, "<abc", 4, emit_cb, &c);
        EXPECT(r == BUF_LEX_OK, "second feed should succeed");
        BufLexFree(yyl, free);   /* must not leak. */
    }

    /* (7b) -- alloc + immediate free. */
    BufLexer *idle = BufLexAlloc(malloc);
    EXPECT(idle != NULL, "idle alloc failed");
    BufLexFree(idle, free);

    /* (7c) -- defensive double-free guard via the public API
    ** contract: LexFree(NULL, free) is a no-op. */
    BufLexFree(NULL, free);
    BufLexFree(NULL, NULL);

    if (fails == saved) printf("test_lexfree_cleanup: PASS\n");
    return fails - saved;
}

int main(void) {
    test_simple_accum();
    test_growth();
    test_include_helper_and_append();
    test_multi_buffer();
    test_lexfree_cleanup();
    if (fails == 0) {
        printf("\ntest_lex_buf: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr, "\ntest_lex_buf: %d sub-test failure(s)\n", fails);
    return 1;
}
