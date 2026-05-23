/* Tiny grammar B for test_symbol_prefix.  See test_symbol_prefix_a.y. */

%name PrefixB
%token_prefix PB_
%symbol_prefix PB_INTERNAL_
%token_type   { int }
%type prog    { int }
%start_symbol prog

%token C D.

prog ::= C D. { (void)0; }
