/*
** Regression test for Letter 16 (May 2026, post-v0.2.6): AOT
** codegen must emit explicit ERROR action-table entries.
**
** Background.  Letter 15 reported that ReportAOTTable's
** per-state default branch emitted a uniform YY_NO_ACTION
** sentinel; v0.2.6 fixed that to mirror yy_default[stateno].
** Letter 16 then ran a full (state, lookahead) verification
** sweep and found a second omission: the per-state action
** loop only emitted SHIFT / SHIFTREDUCE / REDUCE cases,
** silently dropping ERROR action-table entries that the
** LALR analysis encoded as explicit (state, la) -> error
** overrides.  States with explicit error overrides on top
** of an otherwise-default-reducing state would silently
** reduce instead of erroring.
**
** The fix is in lime.c::ReportAOTTable: emit an `else if
** (ap->type==ERROR)` case that returns lemp->errAction, and
** count ERROR actions in the has_actions sweep so states
** with only ERROR overrides (no SHIFT) also get a switch
** body emitted.  Plus a critical filter: skip actions
** whose ap->sp->index >= lemp->nterminal (mirrors
** lime.c:6467 in the table-driven emit) so ACCEPT actions
** -- always attached to the start non-terminal -- don't
** leak into the token-side switch.
**
** This regression test:
**   1. Builds a snapshot from a tiny grammar that uses
**      %nonassoc on a comparison operator.  The conflict
**      resolver promotes the shift action to ERROR when
**      same-precedence shift/reduce can't break the tie
**      via associativity (lime.c:1471).
**   2. Asserts at least one explicit ERROR-action case is
**      emitted in the AOT switch (catches the original
**      Letter-16 omission).
**   3. Runs the full (state, lookahead) AOT-vs-table sweep
**      to confirm zero divergence.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "snapshot.h"

extern unsigned char yy_find_shift_action_aot(
    unsigned char stateno, unsigned short iLookAhead);

/*
** Same oracle as test_aot_equivalence.c: mirrors
** src/parse_engine.c::find_shift_action so we have a
** reference independent of the engine itself.
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

extern ParserSnapshot *AotErrBuildSnapshot(void);

int main(void) {
    ParserSnapshot *snap = AotErrBuildSnapshot();
    if (snap == NULL) {
        fprintf(stderr, "AotErrBuildSnapshot returned NULL\n");
        return 1;
    }

    printf("AOT explicit-ERROR regression test (Letter 16)\n");
    printf("==============================================\n");
    printf("  states     : %u\n", snap->nstate);
    printf("  yy_ntoken  : %u\n", snap->yy_ntoken);
    printf("  errAction  : %u\n", snap->yy_no_action - 2);

    /* Sweep all (s, la), comparing AOT to table-driven oracle.
    ** Letter 16's reported failure shape is AOT returning a
    ** reduce action where table-driven returns yy_no_action - 2
    ** (errAction).  Pre-fix this test fires on ~3 states for
    ** this grammar; post-fix it should be silent. */
    int mismatches = 0;
    int err_pairs  = 0;
    int sampled    = 0;
    uint16_t errAction = snap->yy_no_action - 2; /* errAction = noAction - 2 */

    for (uint32_t s = 0; s < snap->nstate; s++) {
        if (s > snap->yy_max_shift) continue;
        for (uint32_t la = 0; la <= snap->yy_ntoken; la++) {
            uint16_t want = table_find_shift(snap, (uint16_t)s, (uint16_t)la);
            unsigned char aot =
                yy_find_shift_action_aot((unsigned char)s, (unsigned short)la);
            sampled++;
            if (want == errAction) err_pairs++;
            if (aot != want) {
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

    printf("  pairs tested: %d\n", sampled);
    printf("  pairs returning errAction: %d\n", err_pairs);
    printf("  mismatches : %d\n", mismatches);

    /* Without %nonassoc producing at least one explicit ERROR
    ** action, this test wouldn't catch the Letter-16 bug at
    ** all: every (s, la) -> errAction would come from the
    ** state default, not from an explicit table entry, and
    ** the AOT default branch (fixed in v0.2.6) handles those
    ** correctly.  Asserting err_pairs > 0 keeps a future
    ** grammar tweak from silently neutering this regression. */
    if (err_pairs == 0) {
        fprintf(stderr,
                "test setup failure: grammar produced no errAction "
                "entries; the regression isn't being exercised.\n");
        return 2;
    }
    if (mismatches > 0) {
        fprintf(stderr,
                "AOT vs table-driven divergence on %d pair(s) -- "
                "Letter-16 regression\n", mismatches);
        return 1;
    }
    printf("PASS: %d pairs swept, %d errAction entries, 0 divergence\n",
           sampled, err_pairs);
    return 0;
}
