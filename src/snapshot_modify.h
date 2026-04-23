/*
** Snapshot modification - applying grammar modifications to snapshots.
**
** Given a base ParserSnapshot and an array of GrammarModifications from
** loaded extensions, create_modified_snapshot() produces a new snapshot
** with the modifications applied.  The base snapshot is not mutated.
**
** The pipeline is:
**   1. Clone the base snapshot (deep copy)
**   2. Apply each modification (add rules, tokens, precedence changes)
**   3. Rebuild the LALR(1) automaton (states + action tables)
**   4. Return the new snapshot with refcount == 1
*/
#ifndef SNAPSHOT_MODIFY_H
#define SNAPSHOT_MODIFY_H

#include "snapshot.h"
#include "extension.h"
#include "conflict.h"

#include <stdbool.h>

/* ------------------------------------------------------------------ */
/*  Modification result                                                */
/* ------------------------------------------------------------------ */

typedef enum ModifyResult {
    MODIFY_OK = 0,           /* Snapshot created successfully */
    MODIFY_ERR_ALLOC,        /* Memory allocation failure */
    MODIFY_ERR_INVALID_MOD,  /* Invalid modification (bad type, missing fields) */
    MODIFY_ERR_CONFLICT,     /* Unresolved conflicts detected */
    MODIFY_ERR_BUILD,        /* LALR(1) automaton rebuild failed */
} ModifyResult;

/* ------------------------------------------------------------------ */
/*  API                                                                */
/* ------------------------------------------------------------------ */

/*
** Create a new snapshot by applying modifications to a base snapshot.
**
** Parameters:
**   base       - The base snapshot to clone (not modified, may be NULL
**                for an empty grammar)
**   mods       - Array of grammar modifications to apply
**   nmods      - Number of modifications in the array
**   registry   - Extension registry (used for conflict resolution callbacks)
**   out        - On success, receives a pointer to the new snapshot
**                with refcount == 1
**   conflicts  - On MODIFY_ERR_CONFLICT, receives a ConflictSet describing
**                the unresolved conflicts.  Caller must free with
**                conflict_set_destroy().  May be NULL if not needed.
**   error      - On failure, receives a malloc'd error message.
**                Caller must free().  May be NULL.
**
** Returns MODIFY_OK on success.
*/
ModifyResult create_modified_snapshot(
    const ParserSnapshot *base,
    const GrammarModification *mods,
    uint32_t nmods,
    ExtensionRegistry *registry,
    ParserSnapshot **out,
    ConflictSet **conflicts,
    char **error
);

/*
** Clone a snapshot.  Returns a new snapshot with refcount == 1 whose
** grammar data and action tables are deep copies of the original.
** Returns NULL on allocation failure.
*/
ParserSnapshot *clone_snapshot(const ParserSnapshot *base);

/*
** Apply a single modification to a mutable snapshot (one that was
** just cloned and has not yet been published).  Returns true on
** success, false on invalid modification.
*/
bool apply_modification(
    ParserSnapshot *snap,
    const GrammarModification *mod,
    char **error
);

/*
** Rebuild the LALR(1) automaton for a snapshot after modifications
** have been applied.  This recomputes states and action tables.
** Returns true on success.
**
** NOTE: This is currently a stub.  Full implementation requires
** the Lemon dynamic-table support from Task #3.
*/
bool rebuild_automaton(ParserSnapshot *snap, char **error);

#endif /* SNAPSHOT_MODIFY_H */
