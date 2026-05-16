/*
** tests/test_lex_per_rule.c -- M4.3 integration test for the
** per-rule test entry points.
**
** Verifies:
**   1. matching input  -> LEX_OK, *out_consumed set correctly
**   2. wrong rule      -> LEX_ERROR, *out_consumed unchanged
**   3. empty input     -> LEX_ERROR (no rule matches empty)
**   4. NULL out_consumed pointer is tolerated
**   5. <<EOF>> rule has no wrapper emitted (greps the generated
**      .c file -- path injected via -DPER_RULE_GEN_C=...)
**   6. <STATE>-qualified rule's wrapper uses the qualifying
**      state, not INITIAL (`expr_plus` matches "+" only because
**      the wrapper hands FOO_STATE_EXPR to Foo_match).  This is
**      observable as: Pr_test_rule_expr_plus("+", ...) == LEX_OK.
**      The plain `Pr_test_rule_plus` (unqualified) ALSO matches
**      "+", but that's the orthogonal case 1 check; the EXPR
**      wrapper succeeding is the proof that state-qualified
**      wrappers don't default to INITIAL.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_lex_per_rule_grammar_lex.h"

#ifndef PER_RULE_GEN_C
#  error "PER_RULE_GEN_C must be defined to the generated .c path"
#endif

static int fails = 0;

#define EXPECT(cond, ...) do {                                  \
    if (!(cond)) {                                              \
        fprintf(stderr, "  %s:%d: ", __func__, __LINE__);       \
        fprintf(stderr, __VA_ARGS__);                           \
        fprintf(stderr, "\n");                                  \
        fails++;                                                \
    }                                                           \
} while (0)

/* ------ tests ------ */

static int test_matching_input(void) {
    int saved = fails;
    size_t n;

    n = 999;
    EXPECT(Pr_test_rule_plus("+", 1, &n) == PR_LEX_OK,
           "plus(\"+\") should match");
    EXPECT(n == 1, "consumed=%zu, want 1", n);

    n = 999;
    EXPECT(Pr_test_rule_num("42", 2, &n) == PR_LEX_OK,
           "num(\"42\") should match");
    EXPECT(n == 2, "consumed=%zu, want 2", n);

    /* Longest-match: "abc" matches ident; the wrapper should
    ** report the full 3 bytes consumed. */
    n = 999;
    EXPECT(Pr_test_rule_ident("abc", 3, &n) == PR_LEX_OK,
           "ident(\"abc\") should match");
    EXPECT(n == 3, "consumed=%zu, want 3", n);

    /* Whitespace rule. */
    n = 999;
    EXPECT(Pr_test_rule_ws("   ", 3, &n) == PR_LEX_OK,
           "ws(\"   \") should match");
    EXPECT(n == 3, "consumed=%zu, want 3", n);

    if (fails == saved) printf("test_matching_input: PASS\n");
    return fails - saved;
}

static int test_wrong_rule_does_not_match(void) {
    int saved = fails;
    size_t n;

    /* "abc" parses as ident, NOT as plus -- ensure plus says no
    ** and out_consumed is NOT touched. */
    n = 0xDEADBEEF;
    EXPECT(Pr_test_rule_plus("abc", 3, &n) == PR_LEX_ERROR,
           "plus(\"abc\") should fail");
    EXPECT(n == 0xDEADBEEF, "out_consumed must be untouched on ERROR (got %zu)", n);

    /* Same for ident("+") -- + is the plus rule, not ident. */
    n = 0xDEADBEEF;
    EXPECT(Pr_test_rule_ident("+", 1, &n) == PR_LEX_ERROR,
           "ident(\"+\") should fail");
    EXPECT(n == 0xDEADBEEF, "out_consumed must be untouched (got %zu)", n);

    /* num("ab") -- letters aren't digits. */
    n = 0xDEADBEEF;
    EXPECT(Pr_test_rule_num("ab", 2, &n) == PR_LEX_ERROR,
           "num(\"ab\") should fail");
    EXPECT(n == 0xDEADBEEF, "out_consumed must be untouched (got %zu)", n);

    if (fails == saved) printf("test_wrong_rule_does_not_match: PASS\n");
    return fails - saved;
}

static int test_empty_input(void) {
    int saved = fails;
    size_t n = 7;

    /* No rule in this grammar matches empty input.  Empty input
    ** must therefore return LEX_ERROR with out_consumed
    ** untouched. */
    EXPECT(Pr_test_rule_plus("", 0, &n) == PR_LEX_ERROR,
           "plus(\"\") should fail");
    EXPECT(n == 7, "out_consumed must be untouched (got %zu)", n);

    EXPECT(Pr_test_rule_ident("", 0, &n) == PR_LEX_ERROR,
           "ident(\"\") should fail");

    if (fails == saved) printf("test_empty_input: PASS\n");
    return fails - saved;
}

static int test_null_out_consumed(void) {
    int saved = fails;

    /* Match path: NULL out_consumed must NOT crash. */
    EXPECT(Pr_test_rule_plus("+", 1, NULL) == PR_LEX_OK,
           "plus(\"+\", NULL) should still report OK");
    EXPECT(Pr_test_rule_num("123", 3, NULL) == PR_LEX_OK,
           "num(\"123\", NULL) should still report OK");

    /* Error path: NULL out_consumed must also be safe. */
    EXPECT(Pr_test_rule_plus("xyz", 3, NULL) == PR_LEX_ERROR,
           "plus(\"xyz\", NULL) should report ERROR");

    if (fails == saved) printf("test_null_out_consumed: PASS\n");
    return fails - saved;
}

/* Verify the generated .c file does NOT contain a test wrapper
** for the EOF rule.  The wrapper, if emitted, would have the
** symbol Pr_test_rule_end_of_input -- so we open the .c file
** and assert the literal string is absent.  The .c PATH comes
** from a -D macro so meson can wire it in without hand-editing
** include paths. */
static int test_eof_rule_excluded(void) {
    int saved = fails;
    FILE *f = fopen(PER_RULE_GEN_C, "rb");
    EXPECT(f != NULL, "open generated .c (%s)", PER_RULE_GEN_C);
    if (!f) return fails - saved;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    EXPECT(sz > 0, "generated .c is empty");
    if (sz <= 0) { fclose(f); return fails - saved; }

    char *buf = malloc((size_t)sz + 1);
    EXPECT(buf != NULL, "malloc");
    if (!buf) { fclose(f); return fails - saved; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';

    /* Positive control: a non-EOF rule's wrapper IS present. */
    EXPECT(strstr(buf, "Pr_test_rule_plus") != NULL,
           "expected non-EOF rule wrapper Pr_test_rule_plus in .c");
    /* Negative: the EOF rule's wrapper is NOT present. */
    EXPECT(strstr(buf, "Pr_test_rule_end_of_input") == NULL,
           "EOF rule wrapper Pr_test_rule_end_of_input should not exist");

    free(buf);
    if (fails == saved) printf("test_eof_rule_excluded: PASS\n");
    return fails - saved;
}

static int test_state_qualified_rule(void) {
    int saved = fails;
    size_t n;

    /* expr_plus is <EXPR>-qualified.  Its wrapper hands
    ** PR_STATE_EXPR to Pr_match.  Foo_match in EXPR state
    ** has the rule registered (since EXPR was declared and
    ** the rule was attached to it), so "+" matches. */
    n = 999;
    EXPECT(Pr_test_rule_expr_plus("+", 1, &n) == PR_LEX_OK,
           "expr_plus(\"+\") should match in EXPR state");
    EXPECT(n == 1, "consumed=%zu, want 1", n);

    /* Sanity: input that doesn't match the EXPR pattern fails. */
    n = 0xDEADBEEF;
    EXPECT(Pr_test_rule_expr_plus("a", 1, &n) == PR_LEX_ERROR,
           "expr_plus(\"a\") should fail");
    EXPECT(n == 0xDEADBEEF, "out_consumed untouched (got %zu)", n);

    if (fails == saved) printf("test_state_qualified_rule: PASS\n");
    return fails - saved;
}

int main(void) {
    test_matching_input();
    test_wrong_rule_does_not_match();
    test_empty_input();
    test_null_out_consumed();
    test_eof_rule_excluded();
    test_state_qualified_rule();
    if (fails == 0) {
        printf("\ntest_lex_per_rule: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr, "\ntest_lex_per_rule: %d sub-test failure(s)\n", fails);
    return 1;
}
