/* bison side of the C11 differential; c11.y's epilogue supplies yyerror.
** Counts tokens by wrapping yylex so the error position can be compared. */
#include <stdio.h>
int yyparse(void);
int c11_ntok, c11_last;
int main(void) { int r = yyparse(); if (r) printf("REJECT@%d\n", c11_last > 0 ? c11_ntok : c11_ntok + 1); else puts("ACCEPT"); return r; }
