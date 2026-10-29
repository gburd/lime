/* Lime side of the rpcalc differential.  The epilogue of rpcalc.y (its
** yylex/main/yyerror) is extracted to rpcalc_epi.c by run_diff.sh and
** compiled here unchanged, so both parsers see the same token stream. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "rpcalc.h"
typedef double YYSTYPE;
YYSTYPE yylval;
int yylex(void);
void yyerror(char const *);
void *RpAlloc(void *(*)(size_t)); void RpFree(void *, void (*)(void *));
void Rp(void *, int, double);
#define PFX(x) Rp##x
#define LIME_VAL(v) (v)
#define NUM 258
#define YYEOF 0
static int lime_map(int t) {
    switch (t) {
    case NUM: return TK_NUM; case '\n': return TK_NL; case '+': return TK_PLUS;
    case '-': return TK_MINUS; case '*': return TK_STAR; case '/': return TK_SLASH;
    case '^': return TK_CARET; case 'n': return TK_N; default: return -1;
    }
}
#define LIME_ABORT_ON_ERROR 1   /* rpcalc.y has no error rule */
#include "lime_yyparse.h"
#define yyparse lime_yyparse
#include "rpcalc_epi.c"
