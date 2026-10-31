/* REJECT in a reentrant scanner, with start conditions and yylineno. */
%option reentrant noyywrap yylineno
%x Q
%%
<INITIAL,Q>[a-z]+\n?  { printf("W%d[%s]", yyget_lineno(yyscanner), yytext); REJECT; }
<INITIAL,Q>[a-z]      { printf("C[%s]", yytext); }
"\""                  { BEGIN(Q); printf("<Q>"); }
<Q>"\""               { BEGIN(INITIAL); printf("</Q>"); }
<Q>[a-z]{2}           { printf("P[%s]", yytext); REJECT; }
<*>.|\n               { printf("."); }
%%
int main(void){ yyscan_t s; yylex_init(&s); yylex(s); yylex_destroy(s); return 0; }
