/*
** tests/test_locations.c -- P0-NEW-2 runtime check.
**
** Exercises:
**   1. @<rhsalias> expands to the stack slot's yyloc field (not the
**      token's enum code).
**   2. @<lhsalias> and @$ expand to the post-reduce LHS location,
**      which physically reuses the first RHS's slot for non-empty
**      rules -- so the LHS location should equal the first RHS's.
**   3. YYLLOC_DEFAULT for empty productions: when nrhs==0 the LHS
**      slot's yyloc falls back to the lookahead's location.
**   4. %location_type {int} compiles -- this whole program treats
**      locations as scalar byte offsets, matching PostgreSQL's
**      YYLTYPE convention.  If the directive were ignored, the
**      test would not link (LimeLocation is a struct, not an int).
*/
#include <stdio.h>
#include <stdlib.h>

#include "test_locations.h"
#include "test_locations_grammar.h"

void *LocAlloc(void *(*mallocProc)(size_t));
void  LocFree(void *, void (*freeProc)(void *));
void  LocLoc(void *yyp, int yymajor, int yyminor, int yyloc,
             struct loc_capture *cap);

static int test_simple_token_locations(void)
{
    /* Token stream: A at offset 100, EOF at offset 999.
    ** Grammar: opt_sign(empty) A -> e -> s
    **   * @X (the A's yyloc) should be 100.
    **   * @SG (opt_sign empty) should fall back to lookahead loc.
    **     The lookahead at the moment opt_sign reduces is A (loc 100).
    **   * @E (the e LHS) should equal first RHS (opt_sign) location.
    **     opt_sign got its loc from the lookahead (100), so e_loc=100.
    **   * @S (the s LHS) should equal the e's location = 100. */
    struct loc_capture cap = { -1, -1, -1, -1 };
    void *parser = LocAlloc(malloc);
    if (!parser) return 1;
    LocLoc(parser, LOC_A, 42, 100, &cap);
    LocLoc(parser, 0,     0,  999, &cap);   /* EOF */
    LocFree(parser, free);

    int fails = 0;
    if (cap.a_loc != 100) {
        fprintf(stderr, "  a_loc=%d expected 100\n", cap.a_loc);
        fails++;
    }
    if (cap.sign_loc != 100) {
        fprintf(stderr, "  sign_loc=%d expected 100 "
                        "(lookahead-loc fallback for empty rule)\n",
                        cap.sign_loc);
        fails++;
    }
    if (cap.e_loc != 100) {
        fprintf(stderr, "  e_loc=%d expected 100 "
                        "(LHS reuses first-RHS slot)\n", cap.e_loc);
        fails++;
    }
    if (cap.s_loc != 100) {
        fprintf(stderr, "  s_loc=%d expected 100\n", cap.s_loc);
        fails++;
    }
    if (fails == 0) {
        printf("test_simple_token_locations: PASS "
               "(a=%d sign=%d e=%d s=%d)\n",
               cap.a_loc, cap.sign_loc, cap.e_loc, cap.s_loc);
    }
    return fails;
}

static int test_with_explicit_sign(void)
{
    /* Token stream: PLUS at 50, A at 100.
    ** Grammar: PLUS A -> opt_sign A -> e -> s
    **   * @X (A) should be 100.
    **   * @SG (opt_sign reduced from PLUS at 50) should be 50.
    **   * @E (first RHS = opt_sign at 50) should be 50.
    **   * @S (e at 50) should be 50. */
    struct loc_capture cap = { -1, -1, -1, -1 };
    void *parser = LocAlloc(malloc);
    if (!parser) return 1;
    LocLoc(parser, LOC_PLUS, 0, 50,  &cap);
    LocLoc(parser, LOC_A,    7, 100, &cap);
    LocLoc(parser, 0,        0, 999, &cap);
    LocFree(parser, free);

    int fails = 0;
    if (cap.a_loc != 100)   { fprintf(stderr,"  a_loc=%d != 100\n",   cap.a_loc);   fails++; }
    if (cap.sign_loc != 50) { fprintf(stderr,"  sign_loc=%d != 50\n", cap.sign_loc); fails++; }
    if (cap.e_loc != 50)    { fprintf(stderr,"  e_loc=%d != 50\n",    cap.e_loc);   fails++; }
    if (cap.s_loc != 50)    { fprintf(stderr,"  s_loc=%d != 50\n",    cap.s_loc);   fails++; }
    if (fails == 0) {
        printf("test_with_explicit_sign: PASS "
               "(a=%d sign=%d e=%d s=%d)\n",
               cap.a_loc, cap.sign_loc, cap.e_loc, cap.s_loc);
    }
    return fails;
}

int main(void)
{
    int fails = 0;
    fails += test_simple_token_locations();
    fails += test_with_explicit_sign();
    if (fails == 0) {
        printf("\ntest_locations: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr, "\ntest_locations: %d sub-test(s) FAILED\n", fails);
    return 1;
}
