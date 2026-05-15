/*
** tests/test_yylloc_default.c -- P0-NEW-6 runtime check.
**
** Exercises Lime honoring a user-defined YYLLOC_DEFAULT macro on
** every reduce, per Bison's signature
**
**    YYLLOC_DEFAULT(Current, Rhs, N)
**
** where Rhs[1..N] are the RHS locations and Rhs[0] is the slot
** below the rule.  This is the LAST blocker for ecpg's preproc.y
** (whose YYLLOC_DEFAULT concatenates string-typed locations across
** every reduce).
**
** Sub-tests:
**   1. Single-token list -- A only.  Spans collapse to the A's span.
**   2. Multi-token list -- A B C.  list LHS span must extend from
**      A.start to C.end; with the pre-P0-NEW-6 default it would
**      collapse to A.start..A.end.
**   3. Empty rule -- the override's N==0 branch fires; LHS span
**      collapses to Rhs[0].end (the slot below the empty rule,
**      which is the parser's stack sentinel = {0,0}).
*/
#include <stdio.h>
#include <stdlib.h>

#include "test_yylloc_default.h"
#include "test_yylloc_default_grammar.h"

void *YlldAlloc(void *(*mallocProc)(size_t));
void  YlldFree(void *, void (*freeProc)(void *));
void  YlldLoc(void *yyp, int yymajor, int yyminor,
              struct span yyloc, struct ylld_capture *cap);

static struct span mk(int s, int e) {
    struct span sp = { s, e };
    return sp;
}

static int span_eq(struct span got, int s, int e, const char *label) {
    if (got.start == s && got.end == e) return 0;
    fprintf(stderr, "  %s: got {%d,%d}, want {%d,%d}\n",
            label, got.start, got.end, s, e);
    return 1;
}

static int test_single_token(void) {
    /* Tokens: A[10..11) SEMI[11..12) EOF.
    ** Reductions in order:
    **   empty (N=0) -> empty_loc = {Rhs[0].end, Rhs[0].end} = {0,0}
    **                  (sentinel slot below; ParseInit zeros it).
    **   list ::= A    (N=1) -> list_loc placeholder
    **   e ::= empty list (N=2) -> e_loc spans empty.start .. list.end
    **   s ::= e SEMI  (N=2) -> s_loc spans e.start .. SEMI.end. */
    struct ylld_capture cap = {0};
    void *p = YlldAlloc(malloc);
    if (!p) return 1;
    YlldLoc(p, YLLD_A,    1, mk(10, 11), &cap);
    YlldLoc(p, YLLD_SEMI, 0, mk(11, 12), &cap);
    YlldLoc(p, 0,         0, mk(99, 99), &cap);
    YlldFree(p, free);

    int fails = 0;
    if (!cap.empty_seen) {
        fprintf(stderr, "  empty rule did not fire\n");
        fails++;
    }
    /* Empty rule's LHS came from Rhs[0].end.  Rhs[0] is yystack[0]
    ** which is the zero-initialised sentinel, so {0,0}.  This proves
    ** the override fired (the pre-P0-NEW-6 default would have
    ** assigned yyLookaheadLoc = {10,11} instead). */
    fails += span_eq(cap.empty_loc, 0, 0, "empty_loc (override fired)");
    fails += span_eq(cap.list_loc,  10, 11, "list_loc (single A)");
    fails += span_eq(cap.e_loc,     0,  11, "e_loc (empty.start..list.end)");
    fails += span_eq(cap.s_loc,     0,  12, "s_loc (e.start..SEMI.end)");
    if (cap.list_seen != 1) {
        fprintf(stderr, "  list_seen=%d expected 1\n", cap.list_seen);
        fails++;
    }
    if (fails == 0) printf("test_single_token: PASS\n");
    return fails;
}

static int test_multi_token_list(void) {
    /* Tokens: A[10..11) B[20..21) C[30..31) SEMI[31..32) EOF.
    ** list reductions march forward:
    **   list ::= A          -> list = {10,11}
    **   list ::= list B     -> list = {10,21}   <-- requires override
    **   list ::= list C     -> list = {10,31}   <-- requires override
    ** Without YYLLOC_DEFAULT honoring on each reduce, the 2-RHS
    ** reductions would keep list.end glued to A.end=11 (slot reuse).
    ** This sub-test is the discriminator. */
    struct ylld_capture cap = {0};
    void *p = YlldAlloc(malloc);
    if (!p) return 1;
    YlldLoc(p, YLLD_A,    1, mk(10, 11), &cap);
    YlldLoc(p, YLLD_B,    2, mk(20, 21), &cap);
    YlldLoc(p, YLLD_C,    3, mk(30, 31), &cap);
    YlldLoc(p, YLLD_SEMI, 0, mk(31, 32), &cap);
    YlldLoc(p, 0,         0, mk(99, 99), &cap);
    YlldFree(p, free);

    int fails = 0;
    fails += span_eq(cap.list_loc, 10, 31,
                     "list_loc (A.start..C.end -- THE override test)");
    fails += span_eq(cap.e_loc,    0,  31, "e_loc (empty.start..list.end)");
    fails += span_eq(cap.s_loc,    0,  32, "s_loc (e.start..SEMI.end)");
    if (cap.list_seen != 3) {
        fprintf(stderr, "  list_seen=%d expected 3 (A, B, C)\n",
                cap.list_seen);
        fails++;
    }
    if (fails == 0) printf("test_multi_token_list: PASS\n");
    return fails;
}

int main(void) {
    int fails = 0;
    fails += test_single_token();
    fails += test_multi_token_list();
    if (fails == 0) {
        printf("\ntest_yylloc_default: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr, "\ntest_yylloc_default: %d sub-test(s) FAILED\n",
            fails);
    return 1;
}
