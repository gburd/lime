/*
** test_destructor_grammar.y -- exercises Lime's %destructor directive.
**
** Demonstrates that semantic values popped during error recovery
** have their destructors invoked, preventing the memory leaks that
** plain Yacc/Bison error tokens would cause.
**
** Grammar describes a tiny "function call list":
**     prog ::= calls.
**     calls ::= calls call.
**     calls ::= call.
**     call  ::= IDENT LP arg_list RP.
**     arg_list ::= arg_list COMMA STRLIT.
**     arg_list ::= STRLIT.
**
** When a syntax error occurs, the partially-constructed arg_list and
** its STRLITs need to be freed.  %destructor declarations attach
** cleanup code that the parser invokes on each popped stack slot,
** so the tracker counts pop-time vs. allocation-time and asserts
** equality.
*/

%name_prefix Dtor
%token_prefix DTOR_
%token_type   { char * }
%type prog       { int }
%type calls      { int }
%type call       { int }
%type arg_list   { int }
%extra_argument  { struct dtor_tracker *trk }
%start_symbol prog

%token IDENT STRLIT LP RP COMMA SEMI.

%include {
#include "test_destructor.h"
#include <stdlib.h>
#include <string.h>
}

/*
** Destructors fire whenever the parser pops a stack entry without
** having "consumed" its value through a normal reduction.  This
** happens during error recovery and at parse_end if there are
** unfinished items on the stack.  Each destructor tracks the free
** so the test harness can assert no leaks on error paths.
**
** Lemon supports per-non-terminal %destructor and a single catch-all
** %token_destructor for all terminal tokens.  Since IDENT and STRLIT
** are terminals, we register a single token_destructor that runs
** on any terminal popped from the stack.  Terminals without an
** allocated value (LP, RP, COMMA) get NULL passed in, so the free
** path checks for that.
*/

%token_destructor {
    if ($$ != NULL) {
        dtor_track_free(trk, $$);
        free($$);
    }
}

/* Rules ------------------------------------------------------------ */

prog(P)     ::= calls(C).               { (void)C; P = 0; }

calls(L)    ::= calls(A) call(B).       { (void)A; (void)B; L = 0; }
calls(L)    ::= call(B).                { (void)B; L = 0; }

call(C)     ::= IDENT(I) LP arg_list(A) RP. {
    /* Successful reduction consumes the IDENT name -- track its
    ** free so the destructor doesn't double-count. */
    dtor_track_free(trk, I);
    free(I);
    (void)A;
    C = 0;
}

arg_list(L) ::= arg_list(A) COMMA STRLIT(S). {
    (void)A;
    dtor_track_free(trk, S);
    free(S);
    L = 0;
}
arg_list(L) ::= STRLIT(S). {
    dtor_track_free(trk, S);
    free(S);
    L = 0;
}
