/*
** Regression test for Letter 16: AOT codegen omitted explicit
** ERROR action-table entries from the per-state action loop.
**
** Trigger: multiple %nonassoc operators at the same precedence
** level.  After matching `expr CMP1 expr`, on lookahead CMP2,
** the parser has both shift CMP2 (continue with chained
** comparison) and reduce by `expr ::= expr CMP1 expr`.  Same
** precedence + NONE associativity hits the resolve_conflict
** branch at lime.c:1471 and demotes the shift to ERROR.
**
** This pattern is exactly how PostgreSQL's pgbench_expr.lime
** and the full gram.lime use %nonassoc on comparison
** operators (LT, GT, EQ, LE_OP, GE_OP, NE_OP).  PG's gram.lime
** generated 36 explicit ERROR action-table entries pre-fix
** that the AOT codegen silently dropped.
**
** Pre-fix the AOT-vs-table sweep diverges; post-fix silent.
*/

%name_prefix AotErr
%token_prefix AOTERR_
%token_type   { int }
%type expr    { int }
%extra_argument { int *result_out }
%start_symbol program

%token NUM PLUS LT GT EQ.
%left PLUS.
%nonassoc LT GT EQ.

program(P) ::= expr(E).            { P = E; *result_out = E; }

expr(E) ::= NUM(N).                { E = N; }
expr(E) ::= expr(L) PLUS expr(R).  { E = L + R; }
expr(E) ::= expr(L) LT   expr(R).  { E = (L <  R) ? 1 : 0; }
expr(E) ::= expr(L) GT   expr(R).  { E = (L >  R) ? 1 : 0; }
expr(E) ::= expr(L) EQ   expr(R).  { E = (L == R) ? 1 : 0; }
