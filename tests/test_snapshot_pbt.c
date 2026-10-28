/*
** test_snapshot_pbt.c -- property-based stateful test for
** snapshot reference counting.
**
** Target #2: "snapshot acquire/release refcount invariant.
** acquire(release(snap)) must not double-free.  Stateful test:
** model = simple counter; subject = the actual snapshot's atomic
** refcount; rules = acquire / release / read."
**
** Hand-rolled model loop:
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
**
** Ported from the old gburd/hegel-c socket API to libhegel's
** in-process C ABI; see tests/hegel_pbt.h for the mapping.  The
** properties are unchanged.
*/

#include "snapshot.h"
#include "parser.h"

#include "hegel_pbt.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

extern ParserSnapshot *ArithBuildSnapshot(void);

/*
** Read the atomic refcount.  snapshot.h's struct is C-only so
** atomic_load_explicit is fine here -- this file is C.
*/
static uint32_t read_refcount(const ParserSnapshot *snap) {
    return (uint32_t)atomic_load_explicit((atomic_uint_fast32_t *)&snap->refcount,
                                          memory_order_acquire);
}

static void prop_refcount_matches_model(hpbt_case *c, void *user_data) {
    (void)user_data;
    ParserSnapshot *snap;
    uint32_t model;
    int64_t n_ops, i;

    /* Build a fresh snapshot for each test case so we don't leak
    ** references across cases.  ArithBuildSnapshot is a one-time
    ** allocation; releasing it returns the storage. */
    snap = ArithBuildSnapshot();
    if (snap == NULL) {
        hpbt_assume(c, 0); /* OOM -- not the property under test */
        return;
    }

    /* Model: the count we believe the snapshot's atomic refcount
    ** holds.  ArithBuildSnapshot returns a snapshot with
    ** refcount = 1, so the model starts at 1. */
    model = read_refcount(snap);
    if (model != 1) {
        HPBT_FAIL(c, "fresh snapshot refcount = %u, expected 1", model);
        lime_snapshot_release(snap);
        return;
    }

    /* Draw 0..200 operations.  Drawing the size separately lets
    ** Hegel shrink to the minimal sequence that reveals a
    ** mismatch. */
    n_ops = hpbt_int(c, 0, 200);

    for (i = 0; i < n_ops && !hpbt_done(c); i++) {
        uint32_t got;

        /* Choose ACQUIRE (0) or RELEASE (1).  Hegel will explore
        ** both directions and shrink to the minimal sequence. */
        int64_t op = hpbt_int(c, 0, 1);
        if (hpbt_done(c)) break;

        if (op == 0) {
            /* ACQUIRE: subject and model both bump. */
            ParserSnapshot *acq = lime_snapshot_acquire(snap);
            if (acq != snap) {
                HPBT_FAIL(c, "lime_snapshot_acquire returned a different pointer at op %lld",
                          (long long)i);
                break;
            }
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

        got = read_refcount(snap);
        if (got != model) {
            HPBT_FAIL(c, "refcount drift after op %lld (%s): model=%u subject=%u", (long long)i,
                      op == 0 ? "acquire" : "release", model, got);
            break;
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
    const char *ver = hpbt_version();

    if (ver == NULL) {
        fprintf(stderr, "libhegel failed to initialise -- skipping PBT\n");
        return 77; /* meson skip */
    }
    printf("libhegel %s (in-process engine)\n", ver);

    return hpbt_run("prop_refcount_matches_model", prop_refcount_matches_model, NULL, 100);
}
