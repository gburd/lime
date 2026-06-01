/*
** Round-trip test grammar for the bison-API skin (--target=c:bison).
**
** Two binaries, one grammar:
**
**   1. test_skin_bison_native -- drives the standard Lime parser API
**      directly (BisonCalc / BisonCalcAlloc / BisonCalcFree).
**   2. test_skin_bison_skinned -- includes the same grammar's
**      generated `_bison.h` and drives via yyparse_extra() through
**      the bison-API surface.
**
** Both binaries fold the same input expression and assert that the
** computed result matches the expected value.  The test itself
** drives a sequence of expressions through both APIs and verifies
** equivalence.
**
** The grammar deliberately calls yyerror() from %syntax_error so
** the bison skin produces a non-zero return on bad input -- mirrors
** how a real bison-port consumer would write the grammar.
*/
%name BisonCalc
%token_type {int}
%type expr {int}
%extra_argument {int *result}

%include {
#include <stdio.h>
}

%syntax_error {
    (void)yymajor;
    (void)yyminor;
    /* Bridge into bison-style error reporting.  The skin's yyparse
    ** does NOT auto-call yyerror -- the user's %syntax_error block
    ** owns that decision.  Here we route into yyerror so the skin
    ** consumer sees errors via the bison contract. */
    void yyerror(const char *);
    yyerror("syntax error");
    *result = -1;
}

%token PLUS MINUS TIMES DIVIDE LPAREN RPAREN INTEGER.
%left PLUS MINUS.
%left TIMES DIVIDE.

%start_symbol program

program ::= expr(A). { *result = A; }

expr(A) ::= expr(B) PLUS  expr(C). { A = B + C; }
expr(A) ::= expr(B) MINUS expr(C). { A = B - C; }
expr(A) ::= expr(B) TIMES expr(C). { A = B * C; }
expr(A) ::= expr(B) DIVIDE expr(C). {
    if (C == 0) { *result = -1; A = 0; }
    else        { A = B / C; }
}
expr(A) ::= LPAREN expr(B) RPAREN. { A = B; }
expr(A) ::= INTEGER(B). { A = B; }
