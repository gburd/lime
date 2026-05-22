/*
** Snapshot system core implementation.
**
** Provides atomic reference-counted snapshots of parser tables so that
** multiple reader threads can safely share a snapshot while a writer
** thread prepares a new one.
*/
#include "snapshot.h"
#include "parser.h"
#include "jit_context.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                    */
/* ------------------------------------------------------------------ */

/*
** Free all memory owned by *snap* and then free the snapshot struct
** itself.  This is called only when the reference count reaches zero,
** so no synchronisation is needed here.
*/
static void destroy_snapshot(ParserSnapshot *snap) {
    if (snap == NULL) return;

    /*
    ** Release every heap allocation owned by this snapshot.  Symbol,
    ** rule and state arrays are owned only when create_base_snapshot
    ** has populated them from a runtime-built grammar; until that
    ** path is wired (see docs/ROADMAP.md) snap->symbols / rules /
    ** states are typically NULL or shallow-shared with a generator
    ** registration, so freeing them is a no-op.  The action tables,
    ** rule metadata and fallback table are always owned by the
    ** snapshot via clone_snapshot / snapshot_build_from_tables.
    */
    free(snap->symbols);
    free(snap->rules);
    free(snap->states);

    /* Action tables */
    free(snap->yy_action);
    free(snap->yy_lookahead);
    free(snap->yy_shift_ofst);
    free(snap->yy_reduce_ofst);
    free(snap->yy_default);

    /* Rule metadata + fallback */
    free(snap->yy_rule_info_lhs);
    free(snap->yy_rule_info_nrhs);
    free(snap->yy_fallback);

    /* JIT context (if any). */
    jit_detach_from_snapshot(snap);

    free(snap);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

/*
** Atomically increment the reference count and return *snap* for
** convenience.  Passing NULL is a no-op that returns NULL.
*/
ParserSnapshot *snapshot_acquire(ParserSnapshot *snap) {
    if (snap == NULL) return NULL;
    atomic_fetch_add_explicit(&snap->refcount, 1, memory_order_relaxed);
    return snap;
}

/*
** Atomically decrement the reference count.  If it reaches zero the
** snapshot is destroyed.  Passing NULL is a safe no-op.
**
** The decrement uses acquire-release ordering: release so that all
** prior reads/writes in this thread are visible before another
** thread might observe the zero count, and acquire so that when we
** are the last releaser we synchronize-with all prior releases by
** other threads and can safely destroy the snapshot.
**
** memory_order_acq_rel on the atomic op is preferred over a
** memory_order_release decrement followed by a separate acquire
** fence in the prev==1 branch -- the combined ordering is more
** idiomatic, equivalently correct, and avoids a known
** ThreadSanitizer false-positive that the fence pattern can
** trigger.  See discussion in tests/test_parser_manager.c's
** test_concurrent_swap_and_read.
*/
void snapshot_release(ParserSnapshot *snap) {
    if (snap == NULL) return;

    uint_fast32_t prev = atomic_fetch_sub_explicit(&snap->refcount, 1, memory_order_acq_rel);

    if (prev == 1) {
        /* We were the last holder; safe to destroy. */
        destroy_snapshot(snap);
    }
}

/*
** create_base_snapshot is defined in src/snapshot_create.c.  It runs
** the lime parser generator as a subprocess on the grammar file,
** compiles the resulting *_snapshot.c into a shared library, and
** dlopen()s it to retrieve the populated ParserSnapshot.
**
** Removed the weak fallback that previously lived here: when both
** the weak default and the strong definition are in the same static
** archive (liblime_parser.a), the linker resolves to whichever it
** sees first inside snapshot.c.o, which silently shadows the strong
** def in snapshot_create.c.o.
*/

/* ------------------------------------------------------------------ */
/*  Public lemon_snapshot_* wrappers (declared in parser.h)            */
/* ------------------------------------------------------------------ */

ParserSnapshot *lemon_snapshot_acquire(ParserSnapshot *snap) {
    return snapshot_acquire(snap);
}

void lemon_snapshot_release(ParserSnapshot *snap) {
    snapshot_release(snap);
}

ParserSnapshot *lemon_snapshot_create(const char *grammar_file, char **error) {
    return create_base_snapshot(grammar_file, error);
}
