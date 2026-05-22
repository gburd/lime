/*
** bench_arith.y -- the same arithmetic grammar bench/bench_arith_grammar.y
** uses, written for GNU Bison 3.x.
**
** Tokens NUM PLUS MINUS STAR SLASH LP RP match the Lime version's
** numeric codes so the same input stream can drive either parser
** (the comparison harness adds an offset where required).
*/

%{
#include <stdio.h>
#include <stdlib.h>
int bison_lex(void);
void bison_error(const char *s);
extern int g_result;
%}

%define api.prefix {bison_}

%token NUM
%token PLUS MINUS STAR SLASH LP RP

%left PLUS MINUS
%left STAR SLASH

%%

program
    : expr                              { g_result = $1; }
    ;

expr
    : expr PLUS  expr                   { $$ = $1 + $3; }
    | expr MINUS expr                   { $$ = $1 - $3; }
    | expr STAR  expr                   { $$ = $1 * $3; }
    | expr SLASH expr                   { $$ = $3 != 0 ? $1 / $3 : 0; }
    | LP expr RP                        { $$ = $2; }
    | NUM                               { $$ = $1; }
    ;

%%

int g_result;

void bison_error(const char *s) {
    (void)s;
}
