/*
** Smoke test grammar for P0-NEW-6: user-defined YYLLOC_DEFAULT
** honored on every reduce.
**
** %location_type carries (start, end) byte offsets.  YYLLOC_DEFAULT
** is overridden to compute Bison's documented span behavior:
**
**   non-empty:  Current.start = Rhs[1].start
**               Current.end   = Rhs[N].end
**   empty:      Current.start = Current.end = Rhs[0].end
**
** This is the same shape ecpg's preproc grammar uses (string-typed
** locations concatenated across RHS).  We use struct{int,int} here
** to stay self-contained and asserterable.
**
** Capture convention.  Lime evaluates YYLLOC_DEFAULT *after* the
** action body (the @<alias> macros expand to stack-slot reads at
** action-body time, before the override fires).  So the only
** action-body @ reference that observes the post-override LHS
** location is one that reads a *previous* rule's LHS via @<rhsalias>
** in the parent rule.  This grammar uses a `top ::= s.` wrapper so
** s's location can be captured post-override; e and list are
** likewise captured from the parent rule's RHS view.
**
** If Lime ignored the override (the pre-P0-NEW-6 behavior), the
** multi-token list reduction's .end would stay glued to A.end (slot
** reuse) and the list_loc check would fail.  That is the
** discriminator sub-test below.
*/

%name_prefix Ylld
%token_prefix YLLD_
%locations
%location_type   { struct span }

%token_type      { int }
%type top        { int }
%type s          { int }
%type e          { int }
%type list       { int }
%type empty      { int }

%extra_argument  { struct ylld_capture *cap }
%start_symbol    top
%token A B C SEMI.

%include {
#include "test_yylloc_default.h"

/* Bison-style YYLLOC_DEFAULT.  Mirrors the canonical Bison default
** ((Current).first = Rhs[1].first, (Current).last = Rhs[N].last;
** for empty rules, collapse to Rhs[0].end).  This is the same
** signature ecpg's preproc.y wants Lime to honor on every reduce. */
#define YYLLOC_DEFAULT(Current, Rhs, N)                          \
    do {                                                         \
        if ((N) > 0) {                                           \
            (Current).start = (Rhs)[1].start;                    \
            (Current).end   = (Rhs)[(N)].end;                    \
        } else {                                                 \
            (Current).start = (Rhs)[0].end;                      \
            (Current).end   = (Rhs)[0].end;                      \
        }                                                        \
    } while (0)
}

/* top ::= s -- captures s's POST-override LHS location via @S. */
top(T) ::= s(S). {
    cap->s_loc = @S;
    T = S;
}

/* s ::= e SEMI -- captures e's POST-override LHS location via @E. */
s(S) ::= e(E) SEMI(T). {
    (void)T;
    cap->e_loc = @E;
    S = E;
}

/* e ::= empty list -- captures list's POST-override LHS via @L,
** and empty's POST-override LHS via @EM. */
e(E) ::= empty(EM) list(L). {
    cap->empty_loc = @EM;
    cap->list_loc  = @L;
    E = L;
}

/* list ::= A | list B | list C -- exercises 1-RHS, 2-RHS reduces.
** A multi-element list's .end should march forward to the last
** token's .end; if YYLLOC_DEFAULT is ignored, it would stay glued
** to the first A's .end.  This rule is the discriminator. */
list(L) ::= A(X).        { L = X; cap->list_seen++; }
list(L) ::= list(P) B(X). { (void)P; (void)X; L = 1; cap->list_seen++; }
list(L) ::= list(P) C(X). { (void)P; (void)X; L = 1; cap->list_seen++; }

/* empty ::= . -- N==0 branch of YYLLOC_DEFAULT. */
empty(EM) ::= . {
    EM = 0;
    cap->empty_seen = 1;
}
