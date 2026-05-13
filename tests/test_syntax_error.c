/*
** tests/test_syntax_error.c -- P0-NEW-1 runtime check.
**
** See test_syntax_error_grammar.y for what the grammar accepts and
** how %syntax_error captures bindings into a SynErrorTrace.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lime_location.h"
#include "test_syntax_error_grammar.h"

/* The grammar's %include block defines the SynErrorTrace struct in
** the generated .c (so the parser body compiles).  We redefine the
** same shape here for the test driver.  Keep the two in sync. */
struct SynErrorTrace {
    int          fired;
    int          seen_yymajor;
    int          seen_yyminor;
    LimeLocation seen_yyloc;
};

/* Generated parser entry points. */
void *synAlloc(void *(*mallocProc)(size_t));
void  synFree(void *, void (*freeProc)(void *));
void  syn(void *yyp, int yymajor, int yyminor, struct SynErrorTrace *trace);
void  synLoc(void *yyp, int yymajor, int yyminor,
             LimeLocation yyloc, struct SynErrorTrace *trace);

static int failures = 0;

#define ASSERT_EQ_INT(actual, expected, label)                            \
    do {                                                                  \
        long _a = (long)(actual), _e = (long)(expected);                  \
        if (_a != _e) {                                                   \
            fprintf(stderr, "FAIL %s: expected %ld, got %ld\n",           \
                    (label), _e, _a);                                     \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static LimeLocation make_loc(uint32_t line, uint32_t col)
{
    LimeLocation l;
    l.first_line   = line;
    l.first_column = col;
    l.last_line    = line;
    l.last_column  = col;
    l.filename     = NULL;
    return l;
}

/* Case A: feed `A` then EOF.  Error must fire on EOF (yymajor==0).
** Before the fix, %syntax_error would read yytos->yyloc (the location
** of the previously-shifted A token, line 1).  After the fix, it
** reads yyLookaheadLoc which is the EOF marker's location -- here
** (3, 0), the explicit value passed for the EOF token. */
static void case_A_eof(void)
{
    struct SynErrorTrace trace = {0};
    void *p = synAlloc(malloc);

    synLoc(p, SYN_A, 0, make_loc(1, 1), &trace);
    /* Feed end-of-input with location (3, 0).  Parser fails here. */
    synLoc(p, 0,    0, make_loc(3, 0), &trace);
    synFree(p, free);

    ASSERT_EQ_INT(trace.fired,                  1, "Case A: %syntax_error must fire");
    ASSERT_EQ_INT(trace.seen_yymajor,           0, "Case A: yymajor must be 0 (EOF)");
    ASSERT_EQ_INT(trace.seen_yyloc.first_line,  3, "Case A: yyloc must be EOF marker's line, not A's");
    ASSERT_EQ_INT(trace.seen_yyloc.first_column,0, "Case A: yyloc col must be EOF marker's col");
}

/* Case B: feed `A C`.  Error fires on C.  yymajor must be SYN_C and
** yyloc must be C's location (2, 5), not A's (1, 1). */
static void case_B_unexpected_token(void)
{
    struct SynErrorTrace trace = {0};
    void *p = synAlloc(malloc);

    synLoc(p, SYN_A, 0, make_loc(1, 1), &trace);
    synLoc(p, SYN_C, 0, make_loc(2, 5), &trace);
    /* No EOF needed; %syntax_error fires synchronously on the bad
    ** token.  Drain anyway so synFree doesn't see pending state. */
    synLoc(p, 0,    0, make_loc(0, 0), &trace);
    synFree(p, free);

    ASSERT_EQ_INT(trace.fired,                  1,    "Case B: %syntax_error must fire");
    ASSERT_EQ_INT(trace.seen_yymajor,         SYN_C,  "Case B: yymajor must be SYN_C");
    ASSERT_EQ_INT(trace.seen_yyloc.first_line,  2,    "Case B: yyloc must be C's line");
    ASSERT_EQ_INT(trace.seen_yyloc.first_column,5,    "Case B: yyloc must be C's col");
}

/* Case C: feed `B` (wrong start token).  Error fires on the first
** input.  yymajor must be SYN_B; yyloc must be B's location. */
static void case_C_wrong_start(void)
{
    struct SynErrorTrace trace = {0};
    void *p = synAlloc(malloc);

    synLoc(p, SYN_B, 0, make_loc(7, 9), &trace);
    synLoc(p, 0,    0, make_loc(0, 0), &trace);
    synFree(p, free);

    ASSERT_EQ_INT(trace.fired,                  1,    "Case C: %syntax_error must fire");
    ASSERT_EQ_INT(trace.seen_yymajor,         SYN_B,  "Case C: yymajor must be SYN_B");
    ASSERT_EQ_INT(trace.seen_yyloc.first_line,  7,    "Case C: yyloc must be B's line");
    ASSERT_EQ_INT(trace.seen_yyloc.first_column,9,    "Case C: yyloc must be B's col");
}

int main(void)
{
    case_A_eof();
    case_B_unexpected_token();
    case_C_wrong_start();

    if (failures > 0) {
        fprintf(stderr, "test_syntax_error: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_syntax_error: all 3 cases distinguished correctly\n");
    return 0;
}
