%option noyywrap
%%
ab     { int i; char *c = strdup(yytext); unput(')'); for (i = yyleng-1; i >= 0; --i) unput(c[i]); unput('('); free(c); printf("U"); }
"(ab)" { printf("P"); }
q      { unput('z'); unput('z'); printf("[%s]", yytext); }
z      { printf("Z"); }
.|\n   { printf("<%c>", yytext[0]); }
%%
int main(void){ yylex(); return 0; }
