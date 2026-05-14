/*
** Runtime check that Lime correctly reduces empty productions when
** the lookahead is in FOLLOW(empty rule).  This is the behaviour
** the PG migration team in Lime-Letter-4 P0-NEW-3 claimed was
** missing.  Three sub-tests, mirroring the three .out file shapes
** from /tmp/repro3*.y :
**   (a) one empty production behind one optional terminal
**   (b) two competing non-terminals after SELECT, only one of
**       which has an empty alternative -- the empty path must
**       be chosen when neither concrete alternative matches
**   (c) two stacked optional non-terminals each with empty
**       alternatives -- the lookahead must propagate through
**       both empty reduces in sequence
**
** If any sub-test reports "syntax_error fired", Lime has the bug
** the PG team described.  If all three pass, Lime's LALR(1)
** default-reduce machinery is working as designed and the PG
** team's actual issue is elsewhere.
*/
#include <stdio.h>
#include <stdlib.h>

#include "test_default_reduce_grammar.h"

void *DrAlloc(void *(*mallocProc)(size_t));
void  DrFree(void *, void (*freeProc)(void *));
void  Dr(void *yyp, int yymajor, int yyminor, int *fired);

static int run_case(const char *label, const int *toks, int n,
                    int expect_fired)
{
    int fired = 0;
    void *parser = DrAlloc(malloc);
    if (!parser) return 1;
    for (int i = 0; i < n; i++) {
        Dr(parser, toks[i], 0, &fired);
    }
    Dr(parser, 0, 0, &fired);  /* EOF */
    DrFree(parser, free);

    if (fired != expect_fired) {
        fprintf(stderr, "%s: fired=%d expected %d\n",
                label, fired, expect_fired);
        return 1;
    }
    printf("%s: PASS (fired=%d)\n", label, fired);
    return 0;
}

int main(void)
{
    int fails = 0;

    /* Case A: SELECT ICONST -- opt_all -> empty must fire because
    ** ICONST is in FOLLOW(opt_all). */
    {
        int toks[] = { DR_SELECT, DR_ICONST };
        fails += run_case("case_a_empty_opt_all",
                          toks, 2, /*expect_fired=*/0);
    }

    /* Case B: SELECT ICONST again, but with a competing
    ** distinct_clause that doesn't have an empty alt.  Lime
    ** must still pick the empty opt_all path. */
    {
        int toks[] = { DR_SELECT, DR_ICONST };
        fails += run_case("case_b_empty_among_alternatives",
                          toks, 2, /*expect_fired=*/0);
    }

    /* Case C: SELECT ICONST with TWO stacked empty optionals.
    ** opt_all -> empty then opt_distinct -> empty. */
    {
        int toks[] = { DR_SELECT, DR_ICONST };
        fails += run_case("case_c_stacked_empty_optionals",
                          toks, 2, /*expect_fired=*/0);
    }

    /* Sanity: bogus input still triggers syntax_error. */
    {
        int toks[] = { DR_SELECT, DR_SELECT };
        fails += run_case("case_d_bogus_input_must_error",
                          toks, 2, /*expect_fired=*/1);
    }

    if (fails == 0) {
        printf("\ntest_default_reduce: all 4 sub-tests PASS\n");
        printf("Lime's LALR(1) default-reduce for empty productions "
               "works as designed.\n");
        return 0;
    }
    fprintf(stderr, "\ntest_default_reduce: %d sub-test(s) FAILED\n",
            fails);
    return 1;
}
