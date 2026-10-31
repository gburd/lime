%option noyywrap
%%
abc    { printf("1[%s]", yytext); REJECT; }
ab     { printf("2[%s]", yytext); REJECT; }
a      { printf("3[%s]", yytext); }
abcd   { printf("4[%s]", yytext); REJECT; }
[a-z]+ { printf("5[%s]", yytext); REJECT; }
.|\n   { printf("6[%s]", yytext); }
%%
int main(void){ yylex(); return 0; }
