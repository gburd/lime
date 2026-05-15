/*
** Smoke test grammar for P0-NEW-8: opt-in eager default reduce
** drain.  Three-token grammar (A B C) with an action that
** appends the token text to a shared buffer at reduce-time.
**
** Each rule is a simple "default-reduce" cascade:
**   prog ::= a_stmt b_stmt c_stmt.
**   a_stmt ::= A.   { drain_emit(cap, "A", 1); }
**   b_stmt ::= B.   { drain_emit(cap, "B", 1); }
**   c_stmt ::= C.   { drain_emit(cap, "C", 1); }
**
** After shifting A and before lex-ing B, the LALR(1) state has
** only one action: reduce by `a_stmt ::= A`.  With drain, that
** reduce fires immediately, appending "A" to cap->buf.  Without
** drain, the reduce waits for B's push.
**
** The driver simulates ecpg's lexer interleaving by appending a
** space to cap->buf between Parse() calls.  With drain enabled
** the buffer reads "A B C "; without drain it reads " A BC" or
** similar lexer-vs-action races.
*/

%name_prefix Drain
%token_prefix DRAIN_
%token_type   { int }
%type prog    { int }
%type a_stmt  { int }
%type b_stmt  { int }
%type c_stmt  { int }
%extra_argument { struct drain_capture *cap }
%start_symbol prog
%token A B C.

%include {
#include "test_drain.h"
}

prog(P) ::= a_stmt(X) b_stmt(Y) c_stmt(Z). { (void)X; (void)Y; (void)Z; P = 0; }
a_stmt(L) ::= A(T). { (void)T; drain_emit(cap, "A", 1); L = 0; }
b_stmt(L) ::= B(T). { (void)T; drain_emit(cap, "B", 1); L = 0; }
c_stmt(L) ::= C(T). { (void)T; drain_emit(cap, "C", 1); L = 0; }
