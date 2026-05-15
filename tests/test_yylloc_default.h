/*
** tests/test_yylloc_default.h -- shared types for P0-NEW-6 test.
**
** The grammar uses %location_type {struct span} so a span carries
** both a start and an end byte offset, exercising YYLLOC_DEFAULT
** semantics where a non-trivial concatenation rule (start of first
** RHS, end of last RHS) actually matters -- the existing built-in
** default would only carry Rhs[1] forward.
*/
#ifndef TEST_YYLLOC_DEFAULT_H
#define TEST_YYLLOC_DEFAULT_H

struct span {
    int start;
    int end;
};

struct ylld_capture {
    /* LHS spans recorded during the parse. */
    struct span s_loc;          /* the start symbol's final span     */
    struct span e_loc;          /* the e nonterminal's span          */
    struct span list_loc;       /* the list nonterminal's span       */
    struct span empty_loc;      /* the empty production's LHS span   */
    int        empty_seen;      /* set when the empty action fires   */
    int        list_seen;       /* count of list reductions          */
};

#endif /* TEST_YYLLOC_DEFAULT_H */
