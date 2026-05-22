/*
** bench_arith.l -- Flex lexer for the bench_arith grammar.
**
** Drives the same arithmetic input as the Lime hand-written tokenizer
** so the comparison is apples-to-apples.  Bison's `api.prefix
** {bison_}` only renames yyparse/yylex/yylval (becoming bison_parse,
** bison_lex, bison_lval); the token enum names themselves stay the
** vanilla NUM/PLUS/etc. that the .y file declared.
*/

%option prefix="bison_"
%option noyywrap nodefault

%{
#include "bench_arith.tab.h"
%}

%%

[0-9]+      { bison_lval = atoi(yytext); return NUM; }
"+"         { return PLUS;  }
"-"         { return MINUS; }
"*"         { return STAR;  }
"/"         { return SLASH; }
"("         { return LP;    }
")"         { return RP;    }
[ \t\n]+    { /* skip whitespace */ }
.           { /* unknown -- yyerror */ }

%%
