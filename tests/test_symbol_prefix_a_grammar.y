/*
** Tiny grammar A used by test_symbol_prefix to verify that two
** Lime-generated parsers compiled from different .y files with
** different %symbol_prefix values can be linked into the same
** translation unit without symbol collisions.
**
** Both this file and test_symbol_prefix_b.y have the same %name
** (so the public Parse / ParseAlloc / ParseFree symbols differ via
** %name), and both define internal YY_NTOKEN, YY_MAX_SHIFT, etc. --
** which would collide at preprocessor time if combined into one TU
** without %symbol_prefix.
*/

%name PrefixA
%token_prefix PA_
%symbol_prefix PA_INTERNAL_
%token_type   { int }
%type prog    { int }
%start_symbol prog

%token A B.

prog ::= A B. { (void)0; }
