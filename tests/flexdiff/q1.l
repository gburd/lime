%option noyywrap yylineno
%%
a*      { printf("A[%s]%d ", yytext, yyleng); }
x/\n    { printf("X%d ", yylineno); }
\n      { printf("NL%d ", yylineno); }
.       { printf("O[%s] ", yytext); }
<<EOF>> { printf("EOF yyleng=%d yytext0=%d\n", yyleng, yytext[0]); return 0; }
%%
int main(void){ while (yylex()); return 0; }
