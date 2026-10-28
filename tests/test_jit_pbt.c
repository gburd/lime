/*
** test_jit_pbt.c -- property-based test for JIT vs interpreter
** equivalence.
**
** Target #3: "For any (state, lookahead) pair, the JIT'd
** find_shift_action must return the same action as the table-driven
** version.  This is the property-test analogue of
** tests/test_jit_parse_equivalence.c -- broaden it."
**
** test_jit_parse_equivalence.c uses a fixed token sequence; this
** PBT lets Hegel pick (state, lookahead) pairs uniformly across the
** snapshot's full state*lookahead space and asserts byte-equal
** action codes.  When the JIT codegen changes in ways that should
** be invisible (the compact path landed in v0.2.5, for example),
** this PBT catches divergence before users see it.
**
** Skipped if the JIT isn't available on the host (LIME_NO_JIT
** build, or the arch-conditional gate in lime_jit_compile declined
** to compile).
**
** Ported from the old gburd/hegel-c socket API to libhegel's
** in-process C ABI; see tests/hegel_pbt.h for the mapping.  The
** property is unchanged.
*/

#include "parser.h"
#include "parse_context.h"
#include "snapshot.h"

#include "hegel_pbt.h"

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
static uint16_t interp_find_shift(const ParserSnapshot *snap, uint16_t state, uint16_t lookahead) {
    int32_t ofst, idx;
    if (state > snap->yy_max_shift) return state;
    if (snap->yy_shift_ofst == NULL) return snap->yy_no_action;
    ofst = snap->yy_shift_ofst[state];
    idx = ofst + (int32_t)lookahead;
    if (idx >= 0 && (uint32_t)idx < snap->lookahead_count && snap->yy_lookahead[idx] == lookahead) {
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
extern uint16_t jit_find_shift_action(const ParserSnapshot *snap, uint16_t state,
                                      uint16_t lookahead);

static ParserSnapshot *g_snap;

static void prop_jit_matches_interpreter(hpbt_case *c, void *u) {
    (void)u;
    ParserSnapshot *snap = g_snap;
    int64_t state, lookahead;
    uint16_t want, got;

    state = hpbt_int(c, 0, (int64_t)snap->nstate - 1);
    lookahead = hpbt_int(c, 0, (int64_t)snap->yy_ntoken);
    if (hpbt_done(c)) return;

    want = interp_find_shift(snap, (uint16_t)state, (uint16_t)lookahead);
    got = jit_find_shift_action(snap, (uint16_t)state, (uint16_t)lookahead);

    if (got != want) {
        HPBT_FAIL(c, "divergence at state=%lld la=%lld: interp=%u jit=%u", (long long)state,
                  (long long)lookahead, (unsigned)want, (unsigned)got);
    }
}

int main(void) {
    const char *ver = hpbt_version();
    int jit_rc, rc;

    if (ver == NULL) {
        fprintf(stderr, "libhegel failed to initialise -- skipping PBT\n");
        return 77; /* meson skip */
    }
    printf("libhegel %s (in-process engine)\n", ver);

    g_snap = ArithBuildSnapshot();
    if (g_snap == NULL) {
        fprintf(stderr, "ArithBuildSnapshot returned NULL\n");
        return 1;
    }

    /* Compile JIT.  If the host has no LLVM (LIME_NO_JIT) or the
    ** arch-conditional gate skips, lime_jit_compile returns
    ** non-zero and the property under test is moot.
    **
    ** Note the x86_64 default in lime_jit_compile: it declines to
    ** JIT small grammars because the table-driven interpreter is
    ** faster there.  The arith grammar is small, so on x86_64 this
    ** test needs LIME_JIT=1 to have anything to compare against;
    ** the meson test sets it. */
    jit_rc = lime_jit_compile(g_snap);
    if (jit_rc != 0) {
        fprintf(stderr,
                "lime_jit_compile rc=%d: no JIT to compare against "
                "(LLVM disabled at build time, or lime_jit_compile's "
                "arch/size policy declined -- set LIME_JIT=1 to force). "
                "Skipping JIT-equivalence PBT; libhegel %s is available "
                "and working.\n",
                jit_rc, ver);
        snapshot_release(g_snap);
        return 77;
    }

    rc = hpbt_run("prop_jit_matches_interpreter", prop_jit_matches_interpreter, NULL, 1000);

    snapshot_release(g_snap);
    return rc;
}
