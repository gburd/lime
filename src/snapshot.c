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

/* Weak hook called by destroy_snapshot to release a snapshot's
** dlopen handle (if any).  The strong definition lives in
** src/snapshot_create.c, alongside the registry that pairs each
** subprocess-built snapshot with its .so handle.  Builds that
** don't include snapshot_create.c (the dynamically-built .so
** consumer side, or static-parser-only consumers) get NULL via
** the weak reference and destroy_snapshot skips the dlclose.
** Added v0.6.x to fix the documented dlopen-handle leak.
*/
#if (defined(__GNUC__) || defined(__clang__)) && (!defined(_WIN32) || defined(__MINGW32__))
__attribute__((weak))
void snapshot_dlopen_release(ParserSnapshot *snap);

/* Weak stub definition.  Apple's ld errors on unresolved weak
** undefineds at static-link time; a weak stub keeps consumers that
** don't link snapshot_create.c (e.g. the dynamically-built .so for
** snapshot_create's subprocess pipeline) linkable.  When the strong
** definition in snapshot_create.c IS linked, it wins. */
__attribute__((weak))
void snapshot_dlopen_release(ParserSnapshot *snap) { (void)snap; }
#else
/* Windows: no .so / dlopen machinery; provide a strong no-op so
** destroy_snapshot's call site links cleanly.  When the day comes
** to port the subprocess + LoadLibrary path to Win32, replace this
** with a real implementation in snapshot_create.c (Win32 branch). */
static void snapshot_dlopen_release(ParserSnapshot *snap) { (void)snap; }
#endif

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
    free(snap->grammar_source);

    /* Token-name table (deep-copied strings + array). */
    if (snap->token_names != NULL) {
        uint32_t i;
        for (i = 0; i < snap->token_names_count; i++) free(snap->token_names[i]);
        free(snap->token_names);
    }

    /* JIT context (if any).  jit_detach_from_snapshot is declared
    ** weak in jit_context.h so this snapshot.c file can be bundled
    ** into the dynamically-built .so produced by lime_snapshot_create
    ** without dragging in the JIT library.  When the symbol is not
    ** linked (the .so case), the weak reference resolves to NULL and
    ** we skip the call. */
    if (jit_detach_from_snapshot != NULL) {
        jit_detach_from_snapshot(snap);
    }

    /* dlopen handle (if any).  When this snapshot was constructed by
    ** lime_snapshot_create()'s subprocess pipeline, it owns a dlopen
    ** handle to the .so the action tables were memcpy'd out of.
    ** snapshot_dlopen_release is defined as a strong symbol in
    ** src/snapshot_create.c; in builds that don't include that file
    ** (notably the .so produced by the subprocess pipeline itself)
    ** the weak reference resolves to NULL and we skip the call.
    ** Added v0.6.x; prior to that the handle was deliberately leaked
    ** at process scope. */
    /* On Windows snapshot_dlopen_release is a static no-op (no
    ** dlopen machinery); on POSIX it's a weak symbol that may
    ** resolve to NULL if snapshot_create.c isn't linked. */
#if defined(_WIN32) && !defined(__MINGW32__)
    snapshot_dlopen_release(snap);
#else
    if (snapshot_dlopen_release != NULL) {
        snapshot_dlopen_release(snap);
    }
#endif

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
/*  Public lime_snapshot_* wrappers (declared in parser.h)            */
/* ------------------------------------------------------------------ */

ParserSnapshot *lime_snapshot_acquire(ParserSnapshot *snap) {
    return snapshot_acquire(snap);
}

void lime_snapshot_release(ParserSnapshot *snap) {
    snapshot_release(snap);
}

/* PG Track B introspection: the %first_token offset baked into this
** snapshot.  External token codes are `internal_index + first_token`;
** parse_token subtracts it.  Returns 0 when the grammar declared no
** %first_token (external == internal).  Lets a consumer that composes
** snapshots verify the offset survived composition (the bug fixed in
** clone_snapshot) without reaching into the struct.  Pair with
** lime_token_admissible_in_state(snap, 0, external_code) to confirm a
** terminal's external code is unchanged across a rebuild. */
uint16_t lime_snapshot_first_token(const ParserSnapshot *snap) {
    return snap ? snap->yy_first_token : 0;
}

/* Map a token NAME to its EXTERNAL code in this snapshot, or -1 when
** the name is unknown / the snapshot carries no name table.
**
** The external code is what parse_token() expects and what a scanner
** emits: internal_index + first_token.  This is the missing piece for
** a scanner that wants an extension keyword to resolve to its token in
** a recompiled/composed snapshot -- extension token codes are assigned
** by the recompile, so the scanner cannot hard-code them.
**
** Matches both terminals and nonterminals by name (a scanner only
** cares about terminals); the offset is applied uniformly.  O(nsymbol)
** linear scan -- intended for one-time scanner setup (resolve each
** keyword once into a code), not per-token.  Returns int (not
** uint16_t) so the -1 "not found" sentinel is unambiguous. */
int lime_snapshot_token_code(const ParserSnapshot *snap, const char *name) {
    if (snap == NULL || name == NULL || snap->token_names == NULL) return -1;
    uint32_t i;
    for (i = 0; i < snap->token_names_count; i++) {
        if (snap->token_names[i] != NULL && strcmp(snap->token_names[i], name) == 0) {
            return (int)i + (int)snap->yy_first_token;
        }
    }
    return -1;
}

ParserSnapshot *lime_snapshot_create(const char *grammar_file, char **error) {
    return create_base_snapshot(grammar_file, error);
}
