/*
** tests/test_locations.h -- struct loc_capture shared between the
** test driver and the grammar's %extra_argument.
*/
#ifndef TEST_LOCATIONS_H
#define TEST_LOCATIONS_H

struct loc_capture {
    int s_loc;       /* @<lhs> for s = the merged LHS location */
    int e_loc;       /* @<lhs> for the topmost e reduction */
    int a_loc;       /* @<rhsalias> for A in `e ::= opt_sign A` */
    int sign_loc;    /* @<rhsalias> for opt_sign (empty in test 1) */
};

#endif
