/*
** Snapshot system core implementation.
**
** Provides atomic reference-counted snapshots of parser tables so that
** multiple reader threads can safely share a snapshot while a writer
** thread prepares a new one.
*/
#include "snapshot.h"
#include "lexicon.h"
#include "parser.h"

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
    free(snap->grammar_source);

    /* Token-name table (deep-copied strings + array). */
    if (snap->token_names != NULL) {
        uint32_t i;
        for (i = 0; i < snap->token_names_count; i++) free(snap->token_names[i]);
        free(snap->token_names);
    }

    /* Rule-identity table (deep-copied strings + array). */
    if (snap->rule_ids != NULL) {
        uint32_t i;
        for (i = 0; i < snap->rule_ids_count; i++) free(snap->rule_ids[i]);
        free(snap->rule_ids);
    }

    /* Extension reduce bindings (array owned; callbacks/user_data are
    ** the extension's and are not freed here). */
    free(snap->ext_reduce);

    /* Lexer tables (keyword table / operator trie). */
    lime_lexicon_destroy(snap->lexicon);

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

/* Map a stable source-rule IDENTITY (its canonical rule text,
** "lhs ::= rhs1 rhs2 ...") to its ruleno in this snapshot, or -1 when
** the identity is unknown / the snapshot carries no rule-id table.
**
** This is the fix for fragile positional reduce dispatch: an embedder
** that supplies host/extension reduce callbacks (see
** lime_host_reduce_router) resolves each rule's ruleno ONCE by identity
** at setup, instead of computing (composed_ruleno - base_nrule), which
** breaks whenever the extension set, action presence, or unit-rule
** elimination renumbers rules.  The identity string is stable across
** all of those.  O(nrule) linear scan -- intended for one-time binding
** setup, not per-reduction.  Returns int so -1 is unambiguous. */
int lime_snapshot_rule_by_id(const ParserSnapshot *snap, const char *rule_id) {
    if (snap == NULL || rule_id == NULL || snap->rule_ids == NULL) return -1;
    uint32_t i;
    for (i = 0; i < snap->rule_ids_count; i++) {
        if (snap->rule_ids[i] != NULL && strcmp(snap->rule_ids[i], rule_id) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* Inverse of lime_snapshot_rule_by_id: return the canonical identity
** string for a given ruleno, or NULL when out of range / no table.
** For diagnostics ("which source rule is ruleno N in this composed
** snapshot?").  The returned pointer is owned by the snapshot; do not
** free it. */
const char *lime_snapshot_rule_id(const ParserSnapshot *snap, int ruleno) {
    if (snap == NULL || snap->rule_ids == NULL) return NULL;
    if (ruleno < 0 || (uint32_t)ruleno >= snap->rule_ids_count) return NULL;
    return snap->rule_ids[ruleno];
}

/* lime_snapshot_create / create_base_snapshot live in src/snapshot_create.c
** (liblime_compiler): building a snapshot from grammar text needs the
** in-process compiler. */
