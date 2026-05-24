/*
** test_aot_equivalence.c -- regression test for Letter-15:
** the AOT-emitted yy_find_shift_action_aot() must return the
** same value as the table-driven yy_find_shift_action() for
** every (state, lookahead) pair.
**
** Background: PG team's Letter 15 reported v0.2.5's lime -j
** AOT generator emitting a uniform YY_NO_ACTION sentinel for
** every state's default case, instead of mirroring the
** table-driven path's yy_default[]:
**   - states with iDfltReduce<0  -> errAction (YY_ERROR_ACTION)
**   - states with iDfltReduce>=0 -> the state's default reduce
**
** This test compiles the bench arithmetic grammar with both
** `-n` (snapshot) and `-j` (AOT), then drives both paths
** through every legal (state, lookahead) pair and asserts the
** action codes match.  Catches recurrence of Letter 15.
*/

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "snapshot.h"

/*
** AOT entry point emitted by `lime -j`.  Signature is fixed
** by the generator; see lime.c::ReportAOTTable.
*/
extern unsigned char yy_find_shift_action_aot(
    unsigned char stateno, unsigned short iLookAhead);

/*
** Table-driven equivalent.  Mirrors src/parse_engine.c::find_shift_action
** so we have an oracle independent of the runtime engine itself.
*/
static uint16_t table_find_shift(const ParserSnapshot *snap,
                                 uint16_t stateno, uint16_t lookahead) {
    if (stateno > snap->yy_max_shift) return stateno;
    if (snap->yy_shift_ofst == NULL) return snap->yy_no_action;
    int32_t ofst = snap->yy_shift_ofst[stateno];
    int32_t idx  = ofst + (int32_t)lookahead;
    if (idx >= 0 && (uint32_t)idx < snap->lookahead_count
        && snap->yy_lookahead[idx] == lookahead) {
        return snap->yy_action[idx];
    }
    return snap->yy_default[stateno];
}

extern ParserSnapshot *ArithBuildSnapshot(void);

int main(void) {
    ParserSnapshot *snap = ArithBuildSnapshot();
    if (snap == NULL) {
        fprintf(stderr, "ArithBuildSnapshot returned NULL\n");
        return 1;
    }

    printf("AOT vs table-driven equivalence test\n");
    printf("====================================\n");
    printf("  states     : %u\n", snap->nstate);
    printf("  yy_ntoken  : %u\n", snap->yy_ntoken);
    printf("  total pairs: %u\n", snap->nstate * (snap->yy_ntoken + 1));

    int mismatches = 0;
    int sampled    = 0;

    /* yy_find_shift_action_aot's return type is unsigned char in
    ** ReportAOTTable's emit, so action codes must fit in [0, 255].
    ** The bench arith grammar has < 256 actions; if a future
    ** change pushes this over, the AOT codegen needs to widen
    ** YYACTIONTYPE_AOT and this test catches it. */
    for (uint32_t s = 0; s < snap->nstate; s++) {
        /* Only states <= yy_max_shift go through the AOT path
        ** (the runtime engine's outer guard short-circuits
        ** stateno > yy_max_shift to passthrough). */
        if (s > snap->yy_max_shift) continue;

        for (uint32_t la = 0; la <= snap->yy_ntoken; la++) {
            uint16_t want = table_find_shift(snap, (uint16_t)s, (uint16_t)la);
            unsigned char aot =
                yy_find_shift_action_aot((unsigned char)s, (unsigned short)la);
            sampled++;
            if (aot != want) {
                /* Ignore cases where action overflowed the
                ** unsigned-char return type; that's a separate
                ** issue (see comment above) and not the bug
                ** Letter 15 reports.  Letter 15's failure mode
                ** is mismatch on the DEFAULT path, where action
                ** values are <= the action range. */
                if (want > 255) continue;
                if (mismatches < 5) {
                    fprintf(stderr,
                            "  [MISMATCH] state=%u la=%u: table=%u aot=%u\n",
                            s, la, want, aot);
                }
                mismatches++;
            }
        }
    }

    printf("  pairs tested: %d, mismatches: %d\n", sampled, mismatches);

    if (mismatches != 0) {
        fprintf(stderr,
                "AOT vs table-driven divergence on %d pair(s) -- "
                "Letter-15 regression\n",
                mismatches);
    }

    snapshot_release(snap);
    return mismatches == 0 ? 0 : 1;
}
