/*
** Test grammar for default-reduce behaviour with empty productions.
** Combines the three shapes from /tmp/repro3*.y into one grammar so
** a single test driver can exercise all four cases.
*/

%name_prefix Dr
%token_prefix DR_

%token_type     { int }
%type s         { int }
%extra_argument { int *fired }
%start_symbol   s
%token SELECT ALL DISTINCT ICONST.

/* All three case-shapes converge on the same start symbol via
** alternation, so the runtime driver just feeds different token
** streams.  Lime's LALR construction handles this naturally. */
s ::= SELECT opt_all opt_distinct ICONST.

opt_all ::= ALL.
opt_all ::= .                /* empty */

opt_distinct ::= DISTINCT.
opt_distinct ::= .           /* empty */

%syntax_error {
  *fired = 1;
  (void)yymajor;
  (void)yyminor;
}
