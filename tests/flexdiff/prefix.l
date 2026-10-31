/* %option prefix renames the API; the scanner is driven through it. */
%option prefix="zz" noyywrap yylineno
%%
[a-z]+	{ printf("W[%s]%d ", zztext, zzleng); }
\n	{ printf("NL%d ", zzlineno); }
.	{ }
%%
int main(void){ zzin = stdin; while (zzlex()) ; printf("lines=%d\n", zzget_lineno()); zzlex_destroy(); return 0; }
