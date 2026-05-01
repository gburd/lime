/*
** Simple expression grammar used by test_diagnostics.c.
**
** Intentionally tiny so the expected-token sets are easy to
** reason about.
*/
%token_type { int }
%type expr { int }
%left PLUS MINUS.
%left TIMES.

program ::= expr(A). { (void)A; }

expr(A) ::= expr(B) PLUS  expr(C). { A = B + C; }
expr(A) ::= expr(B) MINUS expr(C). { A = B - C; }
expr(A) ::= expr(B) TIMES expr(C). { A = B * C; }
expr(A) ::= LPAREN expr(B) RPAREN. { A = B; }
expr(A) ::= INTEGER(B). { A = B; }
