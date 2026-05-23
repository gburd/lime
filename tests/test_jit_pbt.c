/*
** test_jit_pbt.c -- property-based test for JIT vs interpreter
** equivalence.
**
** Target #3 from .agent/skills/hegel/references/c/reference.md:
** "For any (state, lookahead) pair, the JIT'd find_shift_action
** must return the same action as the table-driven version.  This
** is the property-test analogue of tests/test_jit_parse_equivalence.c
** -- broaden it."
**
** The existing test_jit_parse_equivalence.c uses a fixed token
** sequence; this PBT lets Hegel pick (state, lookahead) pairs
** uniformly across the snapshot's full state*lookahead space and
** asserts byte-equal action codes.  When the JIT codegen changes
** in ways that should be invisible (the compact path landed in
** v0.2.5, for example), this PBT catches divergence before users
** see it.
**
** Skipped if the JIT isn't available on the host (LIME_NO_JIT
** build, or aarch64-only-JIT gate disabled it).
*/

#include "parser.h"
#include "parse_context.h"
#include "snapshot.h"

#include <hegel/hegel.h>
#include <hegel/generators.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern ParserSnapshot *ArithBuildSnapshot(void);

/*
** The interpreter copy of find_shift_action.  Mirrors src/parse_engine.c
** so we have an oracle independent of the runtime engine itself.
** If the runtime engine ever drifts from this definition, the
** equivalence test below catches it.
*/
static uint16_t interp_find_shift(const ParserSnapshot *snap,
                                  uint16_t state, uint16_t lookahead) {
    if (state > snap->yy_max_shift) return state;
    if (snap->yy_shift_ofst == NULL) return snap->yy_no_action;
    int32_t ofst = snap->yy_shift_ofst[state];
    int32_t idx = ofst + (int32_t)lookahead;
    if (idx >= 0 && (uint32_t)idx < snap->lookahead_count
        && snap->yy_lookahead[idx] == lookahead) {
        return snap->yy_action[idx];
    }
    return snap->yy_default[state];
}

/*
** The JIT wrapper from src/jit_context.c.  Declared extern here
** so we can call it directly without going through parse_token's
** dispatch (which masks bugs that only show up on individual
** lookups).  Returns 0 if no JIT compiled.
*/
extern uint16_t jit_find_shift_action(const ParserSnapshot *snap,
                                      uint16_t state, uint16_t lookahead);

static ParserSnapshot *g_snap;

static void prop_jit_matches_interpreter(hegel_test_case *tc, void *u) {
    (void)u;
    ParserSnapshot *snap = g_snap;

    int64_t state     = hegel_draw_int(tc, hegel_integers(0, (int64_t)snap->nstate - 1));
    int64_t lookahead = hegel_draw_int(tc, hegel_integers(0, (int64_t)snap->yy_ntoken));

    uint16_t want = interp_find_shift(snap, (uint16_t)state, (uint16_t)lookahead);
    uint16_t got  = jit_find_shift_action(snap, (uint16_t)state, (uint16_t)lookahead);

    if (got != want) {
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "divergence at state=%lld la=%lld: interp=%u jit=%u",
                 (long long)state, (long long)lookahead, want, got);
        hegel_note(msg);
        assert(got == want);
    }
}

int main(void) {
    hegel_session *s = hegel_session_new();
    if (s == NULL) {
        fprintf(stderr, "hegel: server unavailable -- skipping PBT\n");
        return 77;
    }

    g_snap = ArithBuildSnapshot();
    if (g_snap == NULL) {
        fprintf(stderr, "ArithBuildSnapshot returned NULL\n");
        hegel_session_free(s);
        return 1;
    }

    /* Compile JIT.  If the host has no LLVM (LIME_NO_JIT) or the
    ** arch-conditional gate skips, lime_jit_compile returns
    ** non-zero and the property under test is moot. */
    int jit_rc = lime_jit_compile(g_snap);
    if (jit_rc != 0) {
        fprintf(stderr,
                "lime_jit_compile rc=%d (skip-on-arch or no-LLVM); "
                "skipping JIT-equivalence PBT\n",
                jit_rc);
        snapshot_release(g_snap);
        hegel_session_free(s);
        return 77;
    }

    hegel_settings settings = HEGEL_DEFAULT_SETTINGS;
    settings.max_examples = 1000;

    hegel_results r = hegel_run_test(s, prop_jit_matches_interpreter, NULL, &settings);
    printf("prop_jit_matches_interpreter: %s (%u valid / %u interesting)\n",
           r.passed ? "PASS" : "FAIL",
           r.valid_test_cases, r.interesting_test_cases);

    int rc = r.passed ? 0 : 1;
    hegel_results_free(&r);
    snapshot_release(g_snap);
    hegel_session_free(s);
    return rc;
}
