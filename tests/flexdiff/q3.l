%option noyywrap yylineno
%%
a\n/\n  { printf("A%d[%d] ", yylineno, yyleng); }
b/\n\n  { printf("B%d ", yylineno); }
c       { int c1 = input(); int c2 = input(); printf("C%d,%d,%d ", c1, c2, yylineno); }
\n      { printf("N%d ", yylineno); }
.       { printf("O[%s] ", yytext); }
<<EOF>> { printf("EOF\n"); return 0; }
%%
int main(void){ while (yylex()); return 0; }
