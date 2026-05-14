/*
** tests/test_lookahead.h -- shared types between the test driver
** and the generated parser.
*/
#ifndef TEST_LOOKAHEAD_H
#define TEST_LOOKAHEAD_H

struct lk_capture {
    int lookahead_seen;       /* What Parse_get_lookahead returned */
    int reached_end;          /* True if s reduced cleanly */
    int fired_syntax_error;   /* True if %syntax_error fired */
};

/* Parse_get_lookahead / Parse_clear_lookahead are emitted by the
** generated parser into the .c file; we don't need to forward-
** declare them here because the action body in the .y is compiled
** alongside them. */

#endif
