%option noyywrap yylineno
%%
a/b  |
c    { printf("[%s]", yytext); }
^d   ECHO;
x$   ECHO;
%%
int main(void){ yylex(); return 0; }
