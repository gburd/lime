/*
** tests/test_alternation.c -- P1-1 runtime check.
**
** Drives the generated p11Parse() parser with three different token
** streams, one per alternative of `s ::= e | A B | A B C .`.  Each
** run feeds a distinct A-token value and verifies that the shared
** trailing action (`R = X;`) wrote that value into the LHS slot,
** which is then surfaced to the test via the `%extra_argument`
** result-pointer.  If the generator did not propagate the action
** across all alternatives, at most one of the three runs would
** return the expected value.
*/
#include <stdio.h>
#include <stdlib.h>

#include "test_alternation_grammar.h"

/* Signatures emitted by the Lime-generated parser (with
** %name_prefix p11, %extra_argument {int *result_out}). */
void *p11Alloc(void *(*mallocProc)(size_t));
void  p11Free(void *, void (*freeProc)(void *));
void  p11(void *yyp, int yymajor, int yyminor, int *result_out);

static int drive(const int *tokens, const int *values, int ntok)
{
    int result = -1;
    void *parser = p11Alloc(malloc);
    if (parser == NULL) {
        return -1;
    }
    for (int i = 0; i < ntok; i++) {
        p11(parser, tokens[i], values[i], &result);
    }
    p11(parser, 0, 0, &result); /* end of input */
    p11Free(parser, free);
    return result;
}

int main(void)
{
    int failures = 0;

    /* Alternative 1: s ::= e, where e ::= A(X) with X=111.
    ** The e rule sets E = X; the s action sets R = X = 111. */
    {
        int toks[] = { P11_A };
        int vals[] = { 111 };
        int r = drive(toks, vals, 1);
        if (r != 111) {
            fprintf(stderr, "alt1 (s ::= e, A=111): expected 111, got %d\n", r);
            failures++;
        }
    }

    /* Alternative 2: s ::= A B, where A=222. */
    {
        int toks[] = { P11_A, P11_B };
        int vals[] = { 222, 0 };
        int r = drive(toks, vals, 2);
        if (r != 222) {
            fprintf(stderr, "alt2 (s ::= A B, A=222): expected 222, got %d\n", r);
            failures++;
        }
    }

    /* Alternative 3: s ::= A B C, where A=333. */
    {
        int toks[] = { P11_A, P11_B, P11_C };
        int vals[] = { 333, 0, 0 };
        int r = drive(toks, vals, 3);
        if (r != 333) {
            fprintf(stderr, "alt3 (s ::= A B C, A=333): expected 333, got %d\n", r);
            failures++;
        }
    }

    if (failures) {
        fprintf(stderr, "test_alternation: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_alternation: all 3 alternatives reduced, action propagated\n");
    return 0;
}
