/*
** Smoke test grammar for P0-NEW-4: %first_token N directive.
**
** Sets %first_token 258 (Bison parity).  The generated header
** should emit:
**
**   #define FT_A   258    -- internal index 1, external 1+258
**   #define FT_B   259    -- internal index 2, external 2+258
**   #define FT_PLUS  260    -- internal index 3, external 3+258
**
** The driver calls FtParse() with these external values; the
** generated parser internally subtracts 258 to index its action
** table.  An ASCII '+' (43) passed as a token MUST be rejected
** (treated as YYNOCODE -> syntax error), confirming that the
** offset really separates ASCII from keyword codes.
*/

%name_prefix Ft
%token_prefix FT_
%first_token 258

%token_type     { int }
%type s         { int }
%type e         { int }
%extra_argument { int *result_out }
%start_symbol   s
%token A B PLUS.

s ::= e(E).                  { *result_out = E; }

e(E) ::= A(X).               { E = X; }
e(E) ::= e(L) PLUS B(R).     { E = L + R; }
