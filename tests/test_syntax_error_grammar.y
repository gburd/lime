/*
** Smoke test grammar for P0-NEW-1: %syntax_error sees the offending
** lookahead's token code, value, and location.
**
** Grammar accepts only `A B`.  The test driver exercises three cases:
**
**   (Case A) feed `A` then EOF      -> error fires on EOF
**            -> %syntax_error must see yymajor==0, yyloc==(0,0,...)
**   (Case B) feed `A C`              -> error fires on C
**            -> %syntax_error must see yymajor==C's code, yyloc==C's
**               threaded location, NOT A's stack location.
**   (Case C) feed `B`                -> error fires on B (wrong start)
**            -> %syntax_error must see yymajor==B's code, yyloc==B's
**               threaded location.
**
** The fix's whole point is that Case A and Case B were previously
** indistinguishable when the user inspected yytos->yyloc -- both
** would have reported the location of the last successfully-shifted
** symbol.  Now %syntax_error reads from yypParser->yyLookaheadLoc
** which is the offending lookahead's location, threaded by ParseLoc().
*/
%name_prefix syn
%token_prefix SYN_
%token_type   { int }
%type s        { int }
%locations
%extra_argument { struct SynErrorTrace *trace }
%start_symbol  s
%token A B C.

%include {
#include "lime_location.h"
struct SynErrorTrace;
/* Forward-declared here so the parser can compile against an
** opaque pointer; the test driver supplies the full definition. */
struct SynErrorTrace {
    int          fired;
    int          seen_yymajor;
    int          seen_yyminor;
    LimeLocation seen_yyloc;
};
}

s ::= A B.

%syntax_error {
    /* yymajor, yyminor, yyloc, yypParser are all in scope.
    ** Record what we see so the test harness can assert. */
    trace->seen_yymajor = yymajor;
    trace->seen_yyminor = yyminor;
    trace->seen_yyloc   = yyloc;
    trace->fired        = 1;
}
