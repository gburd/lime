%option noyywrap
%%
[a-z]+ { return 1; }
.|\n { }
%%
