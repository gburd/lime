/*
** Test grammar for v0.6.0's per-rule reduce-callback dispatch.
**
** The grammar is a tiny arithmetic expression language with:
**   - rules that have user-action code,
**   - rules without code (default $$=$1 semantics),
**   - rules that share identical action bodies (PLUS / MINUS in
**     the second cluster),
**   - a destructor on a non-terminal (exercises codePrefix path),
** so the per-rule emit logic in lime.c gets all four shapes.
**
** The action bodies append a single character to the trace string
** so the driver can compare rule-firing sequences.  This lets the
** test verify functional equivalence at a much finer grain than
** "did the parse succeed" -- it asserts the EXACT order in which
** rules fired.
*/

%name_prefix Prr
%token_prefix PRR_

%token_type     { int }
%type expr      { int }
%type term      { int }
%type maybe_uminus { int }
%extra_argument { struct prr_ctx *ctx }

%include {
#include "test_per_rule_reduce.h"
}


%token PLUS MINUS TIMES INTEGER LPAREN RPAREN.

%left  PLUS MINUS.
%left  TIMES.

program ::= expr(E).             { ctx->result = E; prr_log(ctx, 'P'); }

/* Rule with user code that uses yylhsminor (rc==1 path). */
expr(A) ::= expr(B) PLUS  expr(C). { A = B + C; prr_log(ctx, '+'); }
/* Identical-body rule: linker may fold these into one .text */
expr(A) ::= expr(B) MINUS expr(C). { A = B - C; prr_log(ctx, '-'); }
expr(A) ::= expr(B) TIMES expr(C). { A = B * C; prr_log(ctx, '*'); }

/* Direct-LHS-write rule (rc==0). */
expr(A) ::= LPAREN expr(B) RPAREN. { A = B; prr_log(ctx, 'g'); }

/* Default $$=$1 semantics -- no user code, exercises noCode path
** in the per-rule emit. */
expr ::= term.

term(A) ::= INTEGER(I). { A = I; prr_log(ctx, 'i'); }

/* Destructor on a non-terminal -- exercises codePrefix path
** even though we don't actually trigger it in normal parses. */
%destructor expr {
    (void)yypParser;
    (void)$$;
}
