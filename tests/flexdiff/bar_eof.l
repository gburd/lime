/* `|` rules whose shared action belongs to an <<EOF>> rule, as in PG's
 * scan.l (<xqs>{quotecontinuefail} | <xqs>{other} | <xqs><<EOF>> {...}).
 * (yyless(0) inside an <<EOF>> action makes stock flex fail with "end of
 * buffer missed", so the action only calls it for a real match.) */
%option noyywrap
%x Q
%%
"'"		{ BEGIN(Q); printf("<q>"); }
<Q>"-"	|
<Q>.|\n	|
<Q><<EOF>>	{ if (yytext[0]) yyless(0); BEGIN(INITIAL); printf("</q:%d>", yyleng); }
.|\n		{ printf("[%s]", yytext); }
<<EOF>>		{ printf("{eof}\n"); return 0; }
%%
int main(void){ yylex(); return 0; }
