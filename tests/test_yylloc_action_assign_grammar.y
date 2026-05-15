/*
** Smoke test grammar for P0-NEW-7: action-body `@$ = expr`
** assignments survive the post-action LHS-yyloc commit.
**
** Mirrors the ecpg preproc.y idiom that uncovered the bug:
** %location_type is a heap-allocated string ("source-text
** location"), YYLLOC_DEFAULT defaults to Rhs[1], and rules
** override that default in their action bodies via expressions
** like `@$ = cat_str(N, ...)`.
**
** The test is the discriminator: with the pre-P0-NEW-7 ordering
** (YYLLOC_DEFAULT runs AFTER the action body), every action-body
** @$ assignment is silently overwritten by the default and the
** captured final_loc collapses to a single-token string.  With
** the new ordering, @$ assignments survive and final_loc holds
** the assembled multi-token source-text.
*/

%name_prefix Yact
%token_prefix YACT_
%locations
%location_type   { char * }

%token_type      { char * }
%type top        { char * }
%type s          { char * }
%type e          { char * }
%type list       { char * }

%extra_argument  { struct yact_capture *cap }
%start_symbol    top
%token A B C SEMI.

%include {
#include "test_yylloc_action_assign.h"

/* Bison-style YYLLOC_DEFAULT.  Default to Rhs[1] for non-empty
** rules (or empty string for empty rules).  This is the
** rules-with-action-body-overrides pattern -- the default is a
** placeholder that any rule can replace. */
#define YYLLOC_DEFAULT(Current, Rhs, N)             \
    do {                                            \
        if ((N) > 0)  (Current) = (Rhs)[1];         \
        else          (Current) = (char *) "";      \
    } while (0)
}

/* top ::= s -- captures the assembled location at the top level. */
top(T) ::= s(S). {
    cap->final_loc = @S;
    T = S;
}

/* s ::= e SEMI -- override @$ to concatenate e's loc with ";".
** This is the exact ecpg pattern: build the source-text by
** combining child locations in the action. */
s(S) ::= e(E) SEMI(T). {
    @S = yact_cat3(cap, @E, " ", @T);
    cap->e_loc = @E;
    S = E;
}

/* e ::= list -- pass-through; default YYLLOC_DEFAULT(Rhs[1])
** without action override is correct here, so we don't write
** @$ at all.  This sub-test exercises the case where the
** default fires unmodified. */
e(E) ::= list(L). {
    cap->list_loc = @L;
    E = L;
}

/* list ::= A           -- single-element. */
list(L) ::= A(X). {
    L = X;
    cap->list_seen++;
}

/* list ::= list B      -- override @$ with cat_str pattern. */
list(L) ::= list(P) B(X). {
    @L = yact_cat3(cap, @P, " ", @X);
    L = X;
    cap->list_seen++;
}

/* list ::= list C      -- override @$ with a different shape
** (5-arg cat_str), to exercise the more complex ecpg pattern. */
list(L) ::= list(P) C(X). {
    @L = yact_cat5(cap, @P, " ", @X, "", "");
    L = X;
    cap->list_seen++;
}
