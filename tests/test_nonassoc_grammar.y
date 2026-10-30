/* test_nonassoc_grammar.y -- %nonassoc must reject chaining (a < b < c),
** as bison and POSIX yacc do.  See test_nonassoc.c.
** The bug only bit in a state with no SHIFT action, i.e. after `e OP e`
** where OP is the HIGHEST-precedence operator (CHAIN here); LT, below a
** %left PLUS, is the case that always worked. */
%name NA
%token_type {int}
%extra_argument {int *res}
%nonassoc LT.
%left PLUS.
%nonassoc CHAIN.
%syntax_error { *res = -1; }
%parse_accept { if (*res == 0) *res = 1; }
prog ::= e.
e ::= e CHAIN e.
e ::= e LT e.
e ::= e PLUS e.
e ::= N.
