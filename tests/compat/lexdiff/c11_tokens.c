#include <stdio.h>
extern int yylex(void);
extern char *yytext;
extern int yyleng;
extern int yylineno;
extern FILE *yyin;
void yyerror(const char *s) { printf("yyerror: %s\n", s); }
int main(int argc, char **argv) {
	int t;
	for (int i = 1; i < argc; i++) {
		yyin = fopen(argv[i], "r");
		if (!yyin) continue;
		while ((t = yylex()) != 0) {
			printf("%d\t%d\t", t, yylineno);
			fwrite(yytext, 1, (size_t) yyleng, stdout);
			printf("\n");
		}
		fclose(yyin);
	}
	return 0;
}
