%option noyywrap
%%
abcd   { printf("[%s]", yytext); yyless(2); printf("{%s,%d}", yytext, yyleng); }
cd     { printf("CD"); }
x+     { printf("X%d", yyleng); if (yyleng > 1) yyless(yyleng-1); }
.|\n   { printf("<%c>", yytext[0]); }
%%
int main(void){ yylex(); return 0; }
