/* Lime side of the mfcalc differential (see rpcalc_lime.c). */
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mfcalc.h"   /* Lime token defines (TK_*) */
#include "calc.h"
typedef union { double NUM; symrec *VAR; symrec *FUN; } YYSTYPE;
YYSTYPE yylval;
int yydebug;
int yylex(void);
void yyerror(char const *);
void *MfAlloc(void *(*)(size_t)); void MfFree(void *, void (*)(void *));
void Mf(void *, int, YYSTYPE);
#define PFX(x) Mf##x
#define LIME_VAL(v) (v)
enum { NUM = 258, VAR, FUN, YYEOF = 0 };
static int lime_map(int t) {
    switch (t) {
    case NUM: return TK_NUM; case VAR: return TK_VAR; case FUN: return TK_FUN;
    case '\n': return TK_NL; case '+': return TK_PLUS; case '-': return TK_MINUS;
    case '*': return TK_STAR; case '/': return TK_SLASH; case '^': return TK_CARET;
    case '=': return TK_EQ; case '(': return TK_LP; case ')': return TK_RP;
    default: return -1;
    }
}
#include "lime_yyparse.h"
#define yyparse lime_yyparse
#include "mfcalc_epi.c"
