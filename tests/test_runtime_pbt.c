/*
** test_runtime_pbt.c -- property-based tests for the runtime
** push parser engine.
**
** Targets two of the five Lime-specific Hegel targets identified
** in .agent/skills/hegel/references/c/reference.md:
**
**   1. Parser-table roundtrip: parse_engine_step driven against
**      randomly generated token sequences over a snapshot's
**      alphabet should never crash and should always return one
**      of {accept (>0), reject (<0), continue (0)}.
**
**   4. int32 offset arithmetic on large grammars: the
**      yy_shift_ofst[s] + lookahead computation must not overflow
**      and must produce indices that pass the bounds check or
**      cleanly fall back to yy_default[s].
**
** Skipped at runtime when libhegel or its server binary is not
** available -- exit code 77 is the standard meson "skip" code.
*/

#include "parser.h"
#include "parse_context.h"
#include "snapshot.h"

#include <hegel/hegel.h>
#include <hegel/generators.h>

#include <inttypes.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern ParserSnapshot *ArithBuildSnapshot(void);

/* Shared by both properties; simpler than threading user_data. */
static ParserSnapshot *g_snap;

/* ------------------------------------------------------------------ */
/*  Property 1: parser-table roundtrip                                */
/* ------------------------------------------------------------------ */
/*
** Drive parse_token with a random sequence of token codes drawn
** uniformly from the snapshot's terminal alphabet (codes 0 through
** snap->yy_ntoken - 1).  After each call, the return value must
** be in {-1, 0, 1, ..., yy_ntoken+nrule}.  parse_token must not
** crash, hang, or assert -- it must reach parse_end cleanly even
** when the input is garbage.
*/
static void prop_parser_no_crash(hegel_test_case *tc, void *user_data) {
    (void)user_data;
    ParserSnapshot *snap = g_snap;

    /* Draw token-stream length up to 1000.  Drawing the size
    ** separately gives Hegel a knob to shrink down to the
    ** smallest stream that exhibits a failure. */
    int64_t n = hegel_draw_int(tc, hegel_integers(0, 1000));

    ParseContext *ctx = parse_begin(snap);
    if (ctx == NULL) {
        /* OOM during begin: not the property under test.  Mark
        ** the test case as invalid so Hegel discards it but
        ** doesn't count it as a failure. */
        hegel_assume(0);
        return;
    }

    for (int64_t i = 0; i < n; i++) {
        /* Draw a random token in the snapshot's terminal range.
        ** Token 0 is reserved for EOF; allow it (parser must
        ** handle premature EOF gracefully). */
        int64_t tok = hegel_draw_int(tc, hegel_integers(0, snap->yy_ntoken));
        int rc = parse_token(ctx, (int)tok, NULL, -1);
        /* The contract: rc in {-1, 0, >=1}.  -1 means error and
        ** the parser is expected to refuse further tokens; 0
        ** means continue; >=1 means accepted (start symbol
        ** reduced).  Anything else is a runtime-engine bug. */
        if (rc < -1 || rc > (int)(snap->yy_ntoken + snap->nrule + 4)) {
            hegel_note("parse_token returned out-of-range rc");
            assert(0 && "parse_token rc out of range");
        }
        if (rc < 0) {
            /* Once errored, further tokens should not crash. */
            continue;
        }
        if (rc > 0) {
            /* Accepted -- further tokens should fall through
            ** without crashing.  Don't break; keep feeding. */
            continue;
        }
    }

    /* Final EOF.  Must not crash regardless of what happened
    ** above. */
    (void)parse_token(ctx, 0, NULL, -1);
    parse_end(ctx);
}

/* ------------------------------------------------------------------ */
/*  Property 4: int32 offset arithmetic doesn't overflow              */
/* ------------------------------------------------------------------ */
/*
** For every (state, lookahead) pair drawn from the snapshot's
** legal range, compute (yy_shift_ofst[state] + lookahead) and
** assert it stays in int64_t bounds (it always should -- the
** types are int32_t + uint16_t whose sum fits in int64_t with
** room to spare).  This is the direct shrinkable analogue of
** the int16->int32 widening fix landed in the multi-platform
** round.
**
** A pure arithmetic property -- doesn't drive parse_token -- so
** it's fast and shrinks aggressively to the minimal pair that
** fails (or passes for every drawable pair).
*/
static void prop_offset_arithmetic(hegel_test_case *tc, void *user_data) {
    (void)user_data;
    ParserSnapshot *snap = g_snap;

    int64_t state     = hegel_draw_int(tc, hegel_integers(0, (int64_t)snap->nstate - 1));
    int64_t lookahead = hegel_draw_int(tc, hegel_integers(0, (int64_t)snap->yy_ntoken));

    int32_t ofst = snap->yy_shift_ofst[state];
    /* Use int64_t for the sum so overflow in the test code itself
    ** can't mask a real overflow in the runtime engine. */
    int64_t sum = (int64_t)ofst + (int64_t)lookahead;

    /* The runtime engine treats this as int32_t in find_shift_action.
    ** Assert the value would survive the int32 arithmetic correctly:
    ** either it's a valid index into yy_lookahead, or it's negative
    ** and the bounds check catches it, or it's beyond lookahead_count
    ** and the bounds check catches it. */
    if (sum >= 0 && sum < (int64_t)snap->lookahead_count) {
        /* In-range case: yy_lookahead[sum] is a legal load. */
        uint16_t at = snap->yy_lookahead[sum];
        /* The lookahead value at that index is bounded by
        ** yy_ntoken (terminals + accept/error sentinels). */
        assert(at <= snap->yy_ntoken + 4);
    } else {
        /* Out-of-range case.  find_shift_action will fall back to
        ** yy_default[state].  Just assert that fallback is itself
        ** a valid action code. */
        uint16_t def = snap->yy_default[state];
        assert(def <= snap->nstate + snap->nrule + 4);
    }
}

/* ------------------------------------------------------------------ */
/*  Driver                                                             */
/* ------------------------------------------------------------------ */

int main(void) {
    hegel_session *s = hegel_session_new();
    if (s == NULL) {
        fprintf(stderr, "hegel: server unavailable -- skipping PBT\n");
        return 77; /* meson skip */
    }

    g_snap = ArithBuildSnapshot();
    if (g_snap == NULL) {
        fprintf(stderr, "ArithBuildSnapshot returned NULL\n");
        hegel_session_free(s);
        return 1;
    }

    int rc = 0;
    {
        hegel_settings settings = HEGEL_DEFAULT_SETTINGS;
        settings.max_examples = 200;
        hegel_results r = hegel_run_test(s, prop_parser_no_crash, NULL, &settings);
        printf("prop_parser_no_crash: %s (%u valid / %u interesting)\n",
               r.passed ? "PASS" : "FAIL",
               r.valid_test_cases, r.interesting_test_cases);
        if (!r.passed) rc = 1;
        hegel_results_free(&r);
    }
    {
        hegel_settings settings = HEGEL_DEFAULT_SETTINGS;
        settings.max_examples = 1000; /* pure arithmetic; cheap */
        hegel_results r = hegel_run_test(s, prop_offset_arithmetic, NULL, &settings);
        printf("prop_offset_arithmetic: %s (%u valid / %u interesting)\n",
               r.passed ? "PASS" : "FAIL",
               r.valid_test_cases, r.interesting_test_cases);
        if (!r.passed) rc = 1;
        hegel_results_free(&r);
    }

    snapshot_release(g_snap);
    hegel_session_free(s);
    return rc;
}
