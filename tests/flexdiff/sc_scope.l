/* Start conditions: inclusive (%s) and exclusive (%x), <SC>{ } scopes,
 * <*>, <A,B> lists, BEGIN/YY_START, yy_push_state/yy_pop_state, ^ at the
 * start of a line in and out of a scope, and <SC><<EOF>>. */
%option noyywrap stack
%s INC
%x EXC CMT
%%
"inc"		{ BEGIN(INC); printf("{inc}"); }
"exc"		{ BEGIN(EXC); printf("{exc}"); }
<INC,EXC>"end"	{ BEGIN(INITIAL); printf("{end}"); }
<INC>"only"	{ printf("{inc-only}"); }
<EXC>{
  "x"		{ printf("{X%d}", YY_START); }
  ^"#"		{ printf("{bol#}"); }
  "/*"		{ yy_push_state(CMT); printf("{push}"); }
  .|\n		{ printf("[%c]", yytext[0]); }
  <<EOF>>	{ printf("{exc-eof}"); return 0; }
}
<CMT>{
  "*/"		{ yy_pop_state(); printf("{pop%d}", YY_START); }
  [^*]+|"*"	{ printf("c"); }
}
^"#"		{ printf("{BOL#}"); }
<*>"@"		{ printf("{at%d}", YY_START); }
\n		{ printf("\n"); }
.		{ printf("<%c>", yytext[0]); }
<<EOF>>		{ printf("{eof%d}", YY_START); return 0; }
%%
int main(void){ yylex(); printf("\n"); return 0; }
