%option noyywrap
%{
#define YY_INPUT(buf,result,max) { int c = getchar(); fprintf(stderr, "R"); result = (c == EOF) ? 0 : (buf[0] = (char)c, 1); }
%}
%%
x    { fprintf(stderr, "X"); }
xy   { fprintf(stderr, "Y"); }
.|\n { fprintf(stderr, "."); }
%%
int main(void){ while(yylex()); fprintf(stderr, "\n"); return 0; }
