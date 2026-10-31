/* %option reentrant bison-bridge bison-locations, yyextra, yyget_text,
 * two scanners interleaved, yy_scan_string. */
%option reentrant bison-bridge bison-locations noyywrap yylineno
%option extra-type="int *"
%{
typedef union { int n; } YYSTYPE;
typedef struct { int first_line, first_column, last_line, last_column; } YYLTYPE;
%}
%%
[0-9]+	{ yylval->n = atoi(yytext); yylloc->first_line = yylineno; (*yyextra)++; return 1; }
[a-z]+	{ yylval->n = -(int) yyleng; yylloc->first_line = yylineno; (*yyextra)++; return 2; }
\n|.	{ }
%%
int main(void){
	yyscan_t a, b; int ca = 0, cb = 0; YYSTYPE v; YYLTYPE l; int t;
	yylex_init_extra(&ca, &a); yylex_init(&b); yyset_extra(&cb, b);
	yy_scan_string("12 ab\n345 cde 6", a);
	yy_scan_string("x 7\n8 yy", b);
	/* flex 2.6.4's yy_scan_buffer leaves yylineno uninitialised. */
	yyset_lineno(1, a); yyset_lineno(1, b);
	for (;;) {
		int ta = yylex(&v, &l, a);
		if (ta) printf("A%d:%d@%d[%s] ", ta, v.n, l.first_line, yyget_text(a));
		t = yylex(&v, &l, b);
		if (t) printf("B%d:%d@%d ", t, v.n, l.first_line);
		if (!ta && !t) break;
	}
	printf("\nca=%d cb=%d\n", ca, cb);
	yylex_destroy(a); yylex_destroy(b);
	return 0;
}
