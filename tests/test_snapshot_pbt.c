/*
** test_snapshot_pbt.c -- property-based stateful test for
** snapshot reference counting.
**
** Target #2 from .agent/skills/hegel/references/c/reference.md:
** "snapshot acquire/release refcount invariant.  acquire(release(snap))
** must not double-free.  Stateful test: model = simple counter;
** subject = the actual snapshot's atomic refcount; rules = acquire /
** release / read."
**
** Hegel-c does not have a high-level stateful-testing API yet (per
** the C reference), so this hand-rolls the model loop:
**
**   - subject: a real ParserSnapshot
**   - model:   a uint32_t counter representing the ref count
**   - rules:   acquire (subject + model both incremented),
**              release (subject + model both decremented).  Once
**              the model hits zero, releasing again would double-
**              free; instead we treat that as the end of the test
**              episode and stop.
**   - invariant: at every step, the model counter must equal the
**              subject's refcount field.
**
** The point is to catch any mistake in clone_snapshot,
** snapshot_modify, or future refactors that quietly desync the
** atomic count from the underlying ownership.
*/

#include "snapshot.h"
#include "parser.h"

#include <hegel/hegel.h>
#include <hegel/generators.h>

#include <stdatomic.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

extern ParserSnapshot *ArithBuildSnapshot(void);

/*
** Read the atomic refcount.  snapshot.h's struct is C-only so
** atomic_load_explicit is fine here -- this file is C.
*/
static uint32_t read_refcount(const ParserSnapshot *snap) {
    return (uint32_t)atomic_load_explicit(
        (atomic_uint_fast32_t *)&snap->refcount, memory_order_acquire);
}

static void prop_refcount_matches_model(hegel_test_case *tc, void *user_data) {
    (void)user_data;

    /* Build a fresh snapshot for each test case so we don't leak
    ** references across cases.  ArithBuildSnapshot is a one-time
    ** allocation; releasing it returns the storage. */
    ParserSnapshot *snap = ArithBuildSnapshot();
    if (snap == NULL) {
        hegel_assume(0); /* OOM -- not the property under test */
        return;
    }

    /* Model: the count we believe the snapshot's atomic refcount
    ** holds.  ArithBuildSnapshot returns a snapshot with
    ** refcount = 1, so the model starts at 1. */
    uint32_t model = read_refcount(snap);
    assert(model == 1);

    /* Draw 0..200 operations.  Drawing the size separately lets
    ** Hegel shrink to the minimal sequence that reveals a
    ** mismatch. */
    int64_t n_ops = hegel_draw_int(tc, hegel_integers(0, 200));

    for (int64_t i = 0; i < n_ops; i++) {
        /* Choose ACQUIRE (0) or RELEASE (1).  Hegel will explore
        ** both directions and shrink to the minimal sequence. */
        int64_t op = hegel_draw_int(tc, hegel_integers(0, 1));

        if (op == 0) {
            /* ACQUIRE: subject and model both bump. */
            ParserSnapshot *got = lime_snapshot_acquire(snap);
            assert(got == snap);
            model++;
        } else {
            /* RELEASE.  If model is at 1, releasing would free
            ** the snapshot and subsequent invariant checks would
            ** dereference freed memory.  Stop the episode here
            ** and let Hegel record the partial sequence. */
            if (model <= 1) break;
            lime_snapshot_release(snap);
            model--;
        }

        uint32_t got = read_refcount(snap);
        if (got != model) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "refcount drift after op %lld: model=%u subject=%u",
                     (long long)i, model, got);
            hegel_note(msg);
            assert(got == model);
        }
    }

    /* Drain any remaining acquired references before final
    ** release.  Model > 1 means we did more acquires than
    ** releases. */
    while (model > 1) {
        lime_snapshot_release(snap);
        model--;
    }
    /* Final release frees the snapshot. */
    lime_snapshot_release(snap);
}

int main(void) {
    hegel_session *s = hegel_session_new();
    if (s == NULL) {
        fprintf(stderr, "hegel: server unavailable -- skipping PBT\n");
        return 77;
    }

    hegel_settings settings = HEGEL_DEFAULT_SETTINGS;
    settings.max_examples = 100;

    hegel_results r = hegel_run_test(s, prop_refcount_matches_model, NULL, &settings);
    printf("prop_refcount_matches_model: %s (%u valid / %u interesting)\n",
           r.passed ? "PASS" : "FAIL",
           r.valid_test_cases, r.interesting_test_cases);

    int rc = r.passed ? 0 : 1;
    hegel_results_free(&r);
    hegel_session_free(s);
    return rc;
}
