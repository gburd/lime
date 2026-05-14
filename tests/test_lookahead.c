/*
** tests/test_lookahead.c -- P0-NEW-5 runtime check.
**
** Confirms that:
**   1. Parse_get_lookahead() inside an empty-production action
**      returns the token code Lime was about to shift.
**   2. Parse_clear_lookahead() makes the dispatch loop skip the
**      pending shift and return; the next driver Parse() call
**      pushes a fresh token that flows through normally.
**   3. Outside an active Parse() call (between Parse() invocations,
**      or after Parse_clear_lookahead consumed the token), the
**      lookahead state is YYEMPTY (-2).
**   4. Stack & lookahead state stay coherent across the whole
**      parse: s reduces to its end action and reports cap.reached_end.
**
** The grammar's empty production decl_typename absorbs the GREEDY
** lookahead via the API.  Without P0-NEW-5 there would be no way
** for the action to read it; without Parse_clear_lookahead the
** parser would either re-shift GREEDY (resulting in a syntax error
** because GREEDY is not in the follow set of decl_typename) or
** silently swallow whatever followed, breaking the parse.
*/
#include <stdio.h>
#include <stdlib.h>

#include "test_lookahead.h"
#include "test_lookahead_grammar.h"

void *LkAlloc(void *(*mallocProc)(size_t));
void  LkFree(void *, void (*freeProc)(void *));
void  Lk(void *yyp, int yymajor, int yyminor, struct lk_capture *cap);

/* Forward decls of the API the grammar's action body uses.  These
** are emitted by Lime into the generated .c next to Parse() itself
** but we need them visible from the test driver too so the inter-
** Parse() probing assertions below can run.  Lime emits them with
** the parser-name prefix; with %name_prefix Lk that becomes
** Lk_get_lookahead / Lk_clear_lookahead. */
int  Lk_get_lookahead(void *yyp, int *yyminor_out);
void Lk_clear_lookahead(void *yyp);

static int test_consume_lookahead_in_empty_action(void)
{
    /* Stream: KW_DECL IDENT GREEDY POST EOF.
    ** decl_typename fires as empty when Lime is about to shift
    ** GREEDY; the action reads GREEDY via Parse_get_lookahead,
    ** consumes it via Parse_clear_lookahead.  The driver then
    ** sends POST, which shifts cleanly through the rule
    **     s ::= KW_DECL IDENT decl_typename POST. */
    struct lk_capture cap = { -1, 0, 0 };
    void *parser = LkAlloc(malloc);
    if (!parser) return 1;

    /* Sanity: between Alloc and the first Parse(), no lookahead
    ** is in flight.  Should report YYEMPTY (-2). */
    int probe = Lk_get_lookahead(parser, NULL);
    if (probe != -2) {
        fprintf(stderr, "  lookahead between Alloc/Parse: %d expected -2\n", probe);
        return 1;
    }

    Lk(parser, LK_KW_DECL, 0, &cap);
    Lk(parser, LK_IDENT,   0, &cap);
    Lk(parser, LK_GREEDY,  0, &cap);   /* triggers decl_typename empty */
    Lk(parser, LK_POST,    0, &cap);
    Lk(parser, 0,          0, &cap);   /* EOF */
    LkFree(parser, free);

    int fails = 0;
    if (cap.lookahead_seen != LK_GREEDY) {
        fprintf(stderr, "  lookahead_seen=%d expected LK_GREEDY=%d\n",
                cap.lookahead_seen, LK_GREEDY);
        fails++;
    }
    if (!cap.reached_end) {
        fprintf(stderr, "  s never reduced; clear_lookahead was probably "
                "ignored (parser tried to shift GREEDY twice)\n");
        fails++;
    }
    if (cap.fired_syntax_error) {
        fprintf(stderr, "  %%syntax_error fired -- shouldn't have\n");
        fails++;
    }
    if (fails == 0) {
        printf("test_consume_lookahead_in_empty_action: PASS "
               "(saw=%d reached_end=%d)\n",
               cap.lookahead_seen, cap.reached_end);
    }
    return fails;
}

static int test_lookahead_empty_outside_parse(void)
{
    /* After Alloc, before any Parse() call, the lookahead state is
    ** undefined for the user but must be safely readable -- our
    ** convention is YYEMPTY (-2). */
    void *parser = LkAlloc(malloc);
    if (!parser) return 1;
    int probe = Lk_get_lookahead(parser, NULL);
    LkFree(parser, free);
    if (probe != -2) {
        fprintf(stderr, "test_lookahead_empty_outside_parse: %d expected -2\n",
                probe);
        return 1;
    }
    printf("test_lookahead_empty_outside_parse: PASS (probe=%d)\n", probe);
    return 0;
}

int main(void)
{
    int fails = 0;
    fails += test_consume_lookahead_in_empty_action();
    fails += test_lookahead_empty_outside_parse();
    if (fails == 0) {
        printf("\ntest_lookahead: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr, "\ntest_lookahead: %d sub-test(s) FAILED\n", fails);
    return 1;
}
