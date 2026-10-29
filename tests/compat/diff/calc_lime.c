/* Lime side of the calc differential (see rpcalc_lime.c). */
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "calc.h"
/* calc.y uses api.value.type union: yylval.NUM is the double, NUM the code */
typedef union { double NUM; } YYSTYPE;
enum { NUM = 258 };
YYSTYPE yylval;
int yydebug;
int yylex(void);
void yyerror(char const *);
void *CalcAlloc(void *(*)(size_t)); void CalcFree(void *, void (*)(void *));
void Calc(void *, int, double);
#define PFX(x) Calc##x
#define LIME_VAL(v) ((v).NUM)
static int lime_map(int t) {
    switch (t) {
    case NUM: return TK_NUM; case '\n': return TK_NL; case '+': return TK_PLUS;
    case '-': return TK_MINUS; case '*': return TK_STAR; case '/': return TK_SLASH;
    case '(': return TK_LP; case ')': return TK_RP; default: return -1;
    }
}
#include "lime_yyparse.h"
#define yyparse lime_yyparse
#include "calc_epi.c"
