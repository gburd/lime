/*
** tests/test_lex_extra_arg.c -- P0-NEW-12 regression test.
**
** Verifies that %lexer_extra_argument threads a user-declared
** parameter through LexFeedBytes() into action bodies without
** any file-scope globals.  Driver allocates its own struct,
** passes it by pointer, and confirms the action body sees and
** mutates it once per matched token.
**
** Independence from emit_cb's user_ctx is verified explicitly:
** user_ctx and the extra arg are different pointers, both
** observable inside their respective contexts.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The lex header forward-declares `struct test_extra;` because the
** %lexer_extra_argument type begins with `struct ...`.  The driver
** supplies the full definition so it can construct and inspect
** instances.  The .lex's %include block defines the same type for
** the generated .c TU; the two definitions are independent (C
** struct tags have file scope per TU). */
struct test_extra { int counter; };

#include "test_lex_extra_arg_grammar_lex.h"

static int fails = 0;

#define EXPECT(cond, ...) do {                                  \
    if (!(cond)) {                                              \
        fprintf(stderr, "  %s:%d: ", __func__, __LINE__);       \
        fprintf(stderr, __VA_ARGS__);                           \
        fprintf(stderr, "\n");                                  \
        fails++;                                                \
    }                                                           \
} while (0)

struct emit_ctx {
    int n_tokens;
    int last_rule;
};

static void emit_cb(void *user, int rule,
                    const char *text, size_t len) {
    (void)text; (void)len;
    struct emit_ctx *c = user;
    c->n_tokens++;
    c->last_rule = rule;
}

/* Single-call: extra is read+mutated once per matched WORD,
** user_ctx is independently tracked. */
static int test_extra_threaded_basic(void) {
    int saved = fails;
    ExaLexer *yyl = ExaLexAlloc(malloc);
    EXPECT(yyl != NULL, "alloc returned NULL");
    if (!yyl) return fails - saved;

    struct test_extra extra = { .counter = 0 };
    struct emit_ctx  ec    = { .n_tokens = 0, .last_rule = -1 };

    /* Three WORD tokens separated by whitespace (which is skipped). */
    ExaLexResult r = ExaLexFeedBytes(yyl, "abc def ghi", 11,
                                     emit_cb, &ec, &extra);
    EXPECT(r == EXA_LEX_OK, "expected LEX_OK, got %d", r);
    EXPECT(extra.counter == 3,
           "extra.counter=%d, want 3 (action-body mutation)",
           extra.counter);
    EXPECT(ec.n_tokens == 3,
           "ec.n_tokens=%d, want 3 (emit_cb fired per WORD)",
           ec.n_tokens);
    EXPECT(ec.last_rule == EXA_RULE_WORD,
           "ec.last_rule=%d, want EXA_RULE_WORD=%d",
           ec.last_rule, EXA_RULE_WORD);

    ExaLexFree(yyl, free);
    if (fails == saved) printf("test_extra_threaded_basic: PASS\n");
    return fails - saved;
}

/* Per-instance isolation: two lexers with separate extra structs
** maintain separate counters.  Confirms thread-safe shape (no
** file-scope state). */
static int test_extra_per_instance_isolation(void) {
    int saved = fails;
    ExaLexer *yy1 = ExaLexAlloc(malloc);
    ExaLexer *yy2 = ExaLexAlloc(malloc);
    EXPECT(yy1 != NULL && yy2 != NULL, "alloc returned NULL");
    if (!yy1 || !yy2) {
        if (yy1) ExaLexFree(yy1, free);
        if (yy2) ExaLexFree(yy2, free);
        return fails - saved;
    }

    struct test_extra e1 = { .counter = 0 };
    struct test_extra e2 = { .counter = 0 };
    struct emit_ctx  ec1 = { 0 };
    struct emit_ctx  ec2 = { 0 };

    ExaLexResult r1 = ExaLexFeedBytes(yy1, "aa bb", 5,
                                      emit_cb, &ec1, &e1);
    ExaLexResult r2 = ExaLexFeedBytes(yy2, "x", 1,
                                      emit_cb, &ec2, &e2);
    EXPECT(r1 == EXA_LEX_OK && r2 == EXA_LEX_OK,
           "feed returned r1=%d r2=%d", r1, r2);
    EXPECT(e1.counter == 2, "e1.counter=%d, want 2", e1.counter);
    EXPECT(e2.counter == 1, "e2.counter=%d, want 1", e2.counter);

    ExaLexFree(yy1, free);
    ExaLexFree(yy2, free);
    if (fails == saved) printf("test_extra_per_instance_isolation: PASS\n");
    return fails - saved;
}

int main(void) {
    test_extra_threaded_basic();
    test_extra_per_instance_isolation();
    if (fails == 0) {
        printf("test_lex_extra_arg: PASS\n");
        return 0;
    }
    printf("test_lex_extra_arg: %d FAIL\n", fails);
    return 1;
}
