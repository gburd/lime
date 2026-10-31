%option noyywrap
%%
x  { yymore(); }
y  { printf("[%s]%d ", yytext, yyleng); }
%%
int main(void){ while(yylex()); return 0; }
