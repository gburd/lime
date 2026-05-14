/*
** Smoke test grammar for P0-NEW-5: action-time lookahead access.
**
** Exercises the bison-yychar / yyclearin equivalent: an empty
** production whose action reads the parser's pending lookahead via
** Parse_get_lookahead() and signals consumption via
** Parse_clear_lookahead().  Mirrors plpgsql's decl_datatype pattern:
**
**     decl_datatype : empty { $$ = read_datatype(yychar, ...); yyclearin; }
**
** Token stream:  KW_DECL  IDENT  GREEDY  POST  EOF
** Grammar:       s ::= KW_DECL IDENT decl_typename POST.
**                decl_typename ::= empty.
** decl_typename's action absorbs GREEDY (the lookahead at the
** moment the empty rule fires) and reports the value via
** %extra_argument.  The driver continues with POST and EOF; if
** Parse_clear_lookahead worked correctly, POST shifts cleanly --
** if it didn't, the parser would try to shift GREEDY again and
** fail.
*/

%name_prefix Lk
%token_prefix LK_
%token_type     { int }
%type s         { int }
%extra_argument { struct lk_capture *cap }
%start_symbol   s
%token KW_DECL IDENT GREEDY POST.

%include {
#include "test_lookahead.h"
}

s ::= KW_DECL IDENT decl_typename POST.   { cap->reached_end = 1; }

decl_typename ::= .                       {
    /* Read the parser's pending lookahead -- this is the bison
    ** yychar pattern.  Outside an active Parse() call the API
    ** returns YYEMPTY (-2); inside a reduce action it returns
    ** the externally-visible token code Lime is about to shift.
    **
    ** Note: the function name Parse_get_lookahead is renamed by
    ** Lime to <prefix>_get_lookahead per the %name_prefix
    ** directive (same machinery that renames Parse -> <prefix>).
    ** With %name_prefix Lk, the symbol is Lk_get_lookahead.  This
    ** matches the LkAlloc / LkFree / Lk naming convention that
    ** the rest of the generated parser uses. */
    int t = Lk_get_lookahead(yypParser, NULL);
    cap->lookahead_seen = t;
    /* "Consume" it -- in real plpgsql this would be where
    ** read_datatype() would pull more tokens from the lexer. */
    Lk_clear_lookahead(yypParser);
}

%syntax_error {
    cap->fired_syntax_error = 1;
    (void)yymajor;
    (void)yyminor;
}
