/* Shared pull driver for the differential: turns the bison example's own
** yylex() (char codes + NUM/VAR/FUN) into Lime push calls.  Included by a
** per-grammar shim that defines PFX(), LIME_VAL(), lime_map() and YYSTYPE. */
#include <stdlib.h>
int lime_syntax_errors, lime_accepted;  /* set by %syntax_error / %parse_accept */
static int lime_yyparse(void) {
    void *p = PFX(Alloc)(malloc);
    int t;
    while ((t = yylex()) > 0) {
        int k = lime_map(t);
        if (k < 0) { yyerror("syntax error"); lime_syntax_errors++; }  /* unmapped char */
        else PFX()(p, k, LIME_VAL(yylval));
#ifdef LIME_ABORT_ON_ERROR
        /* No `error` rule: bison's yyparse() returns 1 at the first syntax
        ** error, but Lemon-style recovery discards the bad token and keeps
        ** going (and can then ACCEPT).  Lime has no YYABORT, so the driver
        ** stops feeding. */
        if (lime_syntax_errors) { PFX(Free)(p, free); return 1; }
#endif
    }
    PFX()(p, 0, LIME_VAL(yylval));
    PFX(Free)(p, free);
    return lime_accepted ? 0 : 1;   /* bison: yyparse() == 0 iff the input was accepted */
}
