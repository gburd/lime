/*
** Smoke test grammar for P1-1: `|` alternation in rule RHS.
**
** Three alternatives for the start symbol that share a single
** trailing action.  The action writes the computed value into
** an %extra_argument pointer so the test harness can observe it.
*/

%name_prefix p11
%token_prefix P11_
%token_type      { int }
%type s          { int }
%type e          { int }
%extra_argument  { int *result_out }
%start_symbol    start
%token A B C.

start ::= s(S).                              { *result_out = S; }

e(E) ::= A(X).                               { E = X; }

/* Three alternatives, one shared trailing action referencing the
** LHS alias R and the positional alias X declared in each
** alternative's RHS.  Tests action propagation and per-alternative
** stack-slot resolution. */
s(R) ::= e(X) | A(X) B | A(X) B C .          { R = X; }
