/* The trailing-context rule shapes flex's own src/scan.l uses (lines 531,
 * 538, 784, 822, 823, 828), plus x$, a `|` rule with context, fixed-|r|
 * with variable s, and context across a start condition change. */
%option noyywrap yylineno
NL	\r?\n
%x SC CCL
%%
"{"/[[:digit:]]		{ printf("BRACE[%s]%d ", yytext, yylineno); }
"$"/([[:blank:]]|{NL})	{ printf("DOLLAR "); }
"<"			{ BEGIN(SC); printf("< "); }
<SC>">"/^		{ BEGIN(INITIAL); printf("GT^ "); }
<SC>">"			{ BEGIN(INITIAL); printf("GT "); }
<SC>.|\n		{ printf("sc[%s] ", yytext); }
"["			{ BEGIN(CCL); printf("[ "); }
<CCL>"^"/[^-\]\n]	{ printf("CARET1 "); }
<CCL>"^"/("-"|"]")	{ printf("CARET2 "); }
<CCL>-/[^\]\n]		{ printf("DASH "); }
<CCL>"]"		{ BEGIN(INITIAL); printf("] "); }
<CCL>.|\n		{ printf("ccl[%s] ", yytext); }
end$			{ printf("END%d ", yylineno); }
ab/c+			{ printf("ABC[%s] ", yytext); }
q/r |
qq			{ printf("Q[%s] ", yytext); }
\n			{ printf("NL%d\n", yylineno); }
.			{ printf("<%s>", yytext); }
%%
int main(void){ yylex(); return 0; }
