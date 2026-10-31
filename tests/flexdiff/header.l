/* %option header-file with a prefix and start conditions; header.main.c
 * includes the header and drives the scanner through it. */
%option noyywrap yylineno prefix="hh" header-file="header.h"
%x STR
%%
\"		{ BEGIN(STR); }
<STR>\"		{ BEGIN(INITIAL); return 2; }
<STR>.|\n	{ }
[a-z]+		{ return 1; }
\n|.		{ }
%%
