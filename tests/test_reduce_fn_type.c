/*
** tests/test_reduce_fn_type.c -- compile-time check for the
** LimeReduceFn callback type (P0-2 scaffolding from
** Lime-Requests.txt).
**
** The machinery that actually invokes LimeReduceFn is not yet in
** place (it lands with the runtime apply_add_rule() implementation
** -- see P0-1), so this test does not exercise dispatch.  What it
** does verify:
**
**   1. A function matching the LimeReduceFn signature can be
**      declared and assigned to the type without warnings.
**   2. A GrammarModification can be constructed with the reduce and
**      reduce_user fields populated via designated initialisers.
**   3. Both of the following coexisting modification shapes
**      compile:
**         - reduce set, code NULL (runtime-dispatched)
**         - code set, reduce NULL (generator-time action text;
**           unchanged from the pre-P0-2 API)
**
** Future engineer: when dispatch wiring lands, extend this file to
** load an extension with a real reduce callback and verify it is
** invoked with the documented argument contract.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/extension.h"

static void sample_reduce(void *user_data,
                          void *extra_arg,
                          int nrhs,
                          const void *rhs_values,
                          const int *rhs_locs,
                          void *lhs_out)
{
    (void)user_data;
    (void)extra_arg;
    (void)nrhs;
    (void)rhs_values;
    (void)rhs_locs;
    (void)lhs_out;
}

int main(void)
{
    /* (1) Assignability. */
    LimeReduceFn fn = sample_reduce;
    if (fn == NULL) {
        fprintf(stderr, "LimeReduceFn assignment produced NULL?\n");
        return 1;
    }

    static const char *rhs_syms[] = { "a_expr", "AT_AT", "a_expr" };

    /* (2) Runtime-dispatched MOD_ADD_RULE. */
    GrammarModification runtime_mod = {
        .type = MOD_ADD_RULE,
        .description = "a_expr ::= a_expr AT_AT a_expr (runtime callback)",
        .u.add_rule = {
            .lhs         = "a_expr",
            .rhs         = rhs_syms,
            .nrhs        = 3,
            .reduce      = fn,
            .reduce_user = (void *)0xcafebabeUL,
            .code        = NULL,
            .precedence  = -1,
        },
    };

    /* (3) Generator-time MOD_ADD_RULE (the pre-P0-2 shape, still
    ** valid: an extension that ships with pre-generated parser tables
    ** or that registers rules before the generator runs). */
    GrammarModification static_mod = {
        .type = MOD_ADD_RULE,
        .description = "a_expr ::= a_expr AT_AT a_expr (generator-time)",
        .u.add_rule = {
            .lhs         = "a_expr",
            .rhs         = rhs_syms,
            .nrhs        = 3,
            .reduce      = NULL,
            .reduce_user = NULL,
            .code        = "{ A = jsonb_atat(B, C); }",
            .precedence  = -1,
        },
    };

    /* Light sanity: both shapes stored what we put in them. */
    if (runtime_mod.u.add_rule.reduce != fn ||
        runtime_mod.u.add_rule.reduce_user != (void *)0xcafebabeUL ||
        static_mod.u.add_rule.code == NULL ||
        strcmp(static_mod.u.add_rule.code,
               "{ A = jsonb_atat(B, C); }") != 0) {
        fprintf(stderr, "designated-initialiser round-trip mismatch\n");
        return 1;
    }

    printf("LimeReduceFn type check OK; both runtime and generator-time\n"
           "MOD_ADD_RULE shapes compiled and round-tripped.\n");
    return 0;
}
