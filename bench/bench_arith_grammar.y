/*
** Tiny arithmetic grammar used by bench_jit_real_parser to build a
** REAL Lime-generated parser with a non-trivial state machine.
**
** Grammar:
**   program -> expr
**   expr    -> term | expr '+' term | expr '-' term
**   term    -> factor | term '*' factor | term '/' factor
**   factor  -> NUM | '(' expr ')'
**
** The generated Parse() function is what every example/* in this repo
** actually drives.
*/

%name_prefix Arith
%token_prefix ARITH_
%token_type   { int }
%type program { int }
%type expr    { int }
%type term    { int }
%type factor  { int }
%extra_argument { int *result_out }
%start_symbol program

%token NUM PLUS MINUS STAR SLASH LP RP.

%include {
#include <assert.h>
}

program(P) ::= expr(E).            { P = E; *result_out = E; }

expr(E) ::= term(T).               { E = T; }
expr(E) ::= expr(L) PLUS  term(R). { E = L + R; }
expr(E) ::= expr(L) MINUS term(R). { E = L - R; }

term(T) ::= factor(F).               { T = F; }
term(T) ::= term(L) STAR  factor(R). { T = L * R; }
term(T) ::= term(L) SLASH factor(R). { T = (R != 0) ? L / R : 0; }

factor(F) ::= NUM(N).             { F = N; }
factor(F) ::= LP expr(E) RP.      { F = E; }
