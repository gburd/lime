/*
** Smoke test grammar for P0-NEW-2: proper @N location threading.
**
** Bison-compatible @<rhsalias> and @<lhsalias> / @$ semantics.
** Uses %location_type {int} so locations are byte offsets, matching
** PostgreSQL's YYLTYPE convention.
**
** The grammar parses an arithmetic expression and the action body
** captures @N values for the operands; the test driver checks that
** each captured location matches what the driver passed to ParseLoc().
**
** Includes an empty production (`opt_sign : empty`) so YYLLOC_DEFAULT
** for empty-rule reductions is exercised: the new LHS slot's yyloc
** should fall back to the lookahead's location.
*/

%name_prefix Loc
%token_prefix LOC_
%locations
%location_type   { int }

%token_type      { int }
%type s          { int }
%type e          { int }
%type opt_sign   { int }
%extra_argument  { struct loc_capture *cap }
%start_symbol    s
%token A B PLUS MINUS.

%include {
#include "test_locations.h"  /* struct loc_capture from the test driver */
}

s(S) ::= e(E).               {
    cap->s_loc = @S;
    cap->e_loc = @E;
    S = E;
}

e(E) ::= opt_sign(SG) A(X).  {
    cap->a_loc = @X;
    cap->sign_loc = @SG;   /* empty rule -> lookahead loc fallback */
    E = X;
}

opt_sign(O) ::= .            { O = 0; }
opt_sign(O) ::= PLUS.        { O = 1; }
opt_sign(O) ::= MINUS.       { O = -1; }
