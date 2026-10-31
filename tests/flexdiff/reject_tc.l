%option noyywrap yylineno
%%
ab/c   { printf("1[%s]%d", yytext, yylineno); REJECT; }
a\n?b  { printf("2[%s]%d", yytext, yylineno); REJECT; }
a      { printf("3[%s]%d", yytext, yylineno); yymore(); }
abc    { printf("4[%s]", yytext); }
.|\n   { printf("6[%s]", yytext); }
%%
int main(void){ yylex(); return 0; }
