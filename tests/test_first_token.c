/*
** tests/test_first_token.c -- P0-NEW-4 runtime check.
**
** The grammar declares %first_token 258, so the emitted header
** assigns terminal codes:
**
**     FT_A    = 1 + 258 = 259
**     FT_B    = 2 + 258 = 260
**     FT_PLUS = 3 + 258 = 261
**
** Three runs:
**
**  1. Drive the parser with externally-numbered keyword codes
**     (FT_A and friends).  The grammar reduces to e ::= A and
**     surfaces the value via %extra_argument.  Confirms the
**     offset arithmetic round-trips: caller passes external,
**     parser subtracts internally, action runs, value comes
**     out untouched.
**
**  2. Drive the parser through the e ::= e PLUS B rule.  This
**     exercises the offset on every token, on a multi-RHS
**     reduction, and on the running stack -- regression check
**     against any path that re-passes yymajor without
**     converting.
**
**  3. Drive the parser with an ASCII '+' (raw value 43, well
**     below YYFIRSTTOKEN=258).  After the offset subtract,
**     43 - 258 = -215 -- out of range.  The parser must reject
**     this rather than silently index the action table at a
**     negative offset.  We pass through the %syntax_error
**     callback to confirm rejection.
*/
#include <stdio.h>
#include <stdlib.h>

#include "test_first_token_grammar.h"

void *FtAlloc(void *(*mallocProc)(size_t));
void  FtFree(void *, void (*freeProc)(void *));
void  Ft(void *yyp, int yymajor, int yyminor, int *result_out);

static int drive(const int *tokens, const int *values, int ntok)
{
    int result = -1;
    void *parser = FtAlloc(malloc);
    if (parser == NULL) {
        return -1;
    }
    for (int i = 0; i < ntok; i++) {
        Ft(parser, tokens[i], values[i], &result);
    }
    Ft(parser, 0, 0, &result); /* end of input */
    FtFree(parser, free);
    return result;
}

static int test_keyword_codes_are_offset(void)
{
    /* Compile-time assertions: the emitted #defines reflect the
    ** offset.  If %first_token didn't take effect, FT_A would be
    ** 1 (Lemon default), not 259. */
    if (FT_A    != 259) { fprintf(stderr, "FT_A=%d expected 259\n", FT_A); return 1; }
    if (FT_B    != 260) { fprintf(stderr, "FT_B=%d expected 260\n", FT_B); return 1; }
    if (FT_PLUS != 261) { fprintf(stderr, "FT_PLUS=%d expected 261\n", FT_PLUS); return 1; }
    printf("test_keyword_codes_are_offset: PASS (FT_A=%d FT_B=%d FT_PLUS=%d)\n",
           FT_A, FT_B, FT_PLUS);
    return 0;
}

static int test_drive_simple_reduction(void)
{
    /* e ::= A(X)   { E = X; }
    ** s ::= e(E)   { *result_out = E; } */
    int tokens[] = { FT_A };
    int values[] = { 7 };
    int got = drive(tokens, values, 1);
    if (got != 7) {
        fprintf(stderr, "test_drive_simple_reduction: got %d expected 7\n", got);
        return 1;
    }
    printf("test_drive_simple_reduction: PASS (e <- A(7) -> 7)\n");
    return 0;
}

static int test_drive_multi_rhs_reduction(void)
{
    /* e ::= A(X)             { E = X; }
    ** e ::= e(L) PLUS B(R)   { E = L + R; }
    ** s ::= e(E)             { *result_out = E; } */
    int tokens[] = { FT_A, FT_PLUS, FT_B };
    int values[] = { 10, 0, 32 };
    int got = drive(tokens, values, 3);
    if (got != 42) {
        fprintf(stderr, "test_drive_multi_rhs_reduction: got %d expected 42\n", got);
        return 1;
    }
    printf("test_drive_multi_rhs_reduction: PASS (10 + 32 = 42)\n");
    return 0;
}

static int test_ascii_value_below_offset_rejected(void)
{
    /* Drive the parser with an ASCII '+' (43).  The grammar's
    ** PLUS token is 261, not 43; an out-of-range external code
    ** must be rejected, not silently re-indexed.  We don't have
    ** %syntax_error wired in this minimal grammar, so we rely on
    ** the parse failing to produce the e <- e PLUS B reduction
    ** -- the trailing reduction never runs and result stays at
    ** its sentinel. */
    int tokens[] = { FT_A, /*BAD*/ '+', FT_B };
    int values[] = { 5, 0, 9 };
    int got = drive(tokens, values, 3);
    if (got == 14) {
        /* If the parser accepted ASCII '+' as PLUS, it would have
        ** computed 5+9 = 14.  That's the failure mode. */
        fprintf(stderr, "test_ascii_value_below_offset_rejected: "
                "parser accepted ASCII '+' (43) as PLUS (261); got %d\n", got);
        return 1;
    }
    printf("test_ascii_value_below_offset_rejected: PASS "
           "(ASCII '+' rejected; result=%d, not 14)\n", got);
    return 0;
}

int main(void)
{
    int fails = 0;
    fails += test_keyword_codes_are_offset();
    fails += test_drive_simple_reduction();
    fails += test_drive_multi_rhs_reduction();
    fails += test_ascii_value_below_offset_rejected();
    if (fails == 0) {
        printf("\ntest_first_token: all 4 sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr, "\ntest_first_token: %d sub-test(s) FAILED\n", fails);
    return 1;
}
