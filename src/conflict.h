/*
** Conflict detection for the extension system.
**
** When multiple extensions modify a grammar, conflicts can arise:
**   - Token collisions: two extensions add the same token name
**   - Rule duplicates: two extensions add identical production rules
**   - Precedence conflicts: conflicting precedence/associativity assignments
**   - Shift/reduce and reduce/reduce conflicts in the rebuilt automaton
**
** detect_conflicts() scans an array of modifications and reports all
** conflicts found.  Extensions can resolve conflicts via their
** on_conflict callback.
*/
#ifndef CONFLICT_H
#define CONFLICT_H

#include <stdint.h>
#include <stdbool.h>

/* Forward declarations to avoid circular includes */
struct GrammarModification;
struct ExtensionRegistry;
struct ParserSnapshot;
typedef uint32_t ExtensionID;

/* ------------------------------------------------------------------ */
/*  Conflict types                                                     */
/* ------------------------------------------------------------------ */

typedef enum ConflictType {
    CONFLICT_TOKEN_COLLISION,     /* Same token name from different extensions */
    CONFLICT_DUPLICATE_RULE,      /* Identical production rule */
    CONFLICT_PRECEDENCE_CLASH,    /* Conflicting precedence assignment */
    CONFLICT_SHIFT_REDUCE,        /* Shift/reduce in rebuilt automaton */
    CONFLICT_REDUCE_REDUCE,       /* Reduce/reduce in rebuilt automaton */
} ConflictType;

/*
** Description of a single conflict between modifications.
*/
typedef struct Conflict {
    ConflictType type;

    /* The two modifications that conflict */
    uint32_t mod_index_a;         /* Index into the modifications array */
    uint32_t mod_index_b;         /* Index into the modifications array */

    /* Extension IDs that own the conflicting modifications */
    ExtensionID ext_id_a;
    ExtensionID ext_id_b;

    /* Human-readable description of the conflict */
    char *description;            /* malloc'd, owned by the ConflictSet */

    /* Whether this conflict was resolved */
    bool resolved;
} Conflict;

/* ------------------------------------------------------------------ */
/*  Conflict set                                                       */
/* ------------------------------------------------------------------ */

typedef struct ConflictSet {
    Conflict *conflicts;          /* Dynamic array */
    uint32_t count;
    uint32_t capacity;
} ConflictSet;

/*
** Create an empty conflict set.  Returns NULL on allocation failure.
*/
ConflictSet *conflict_set_create(void);

/*
** Destroy a conflict set and free all owned memory.
*/
void conflict_set_destroy(ConflictSet *cs);

/*
** Add a conflict to the set.  Returns true on success.
*/
bool conflict_set_add(
    ConflictSet *cs,
    ConflictType type,
    uint32_t mod_index_a,
    uint32_t mod_index_b,
    ExtensionID ext_id_a,
    ExtensionID ext_id_b,
    const char *description
);

/*
** Return the number of unresolved conflicts in the set.
*/
uint32_t conflict_set_unresolved_count(const ConflictSet *cs);

/* ------------------------------------------------------------------ */
/*  Conflict detection                                                 */
/* ------------------------------------------------------------------ */

/*
** Scan modifications for conflicts.  This checks for:
**   - Token name collisions between different extensions
**   - Duplicate production rules
**   - Conflicting precedence assignments
**
** Automaton-level conflicts (shift/reduce, reduce/reduce) are detected
** later during rebuild_automaton() and added to the conflict set then.
**
** Parameters:
**   mods     - Array of grammar modifications to check
**   nmods    - Number of modifications
**   cs       - Conflict set to populate (must be pre-created)
**
** Returns true if any conflicts were detected.
*/
bool detect_conflicts(
    const struct GrammarModification *mods,
    uint32_t nmods,
    ConflictSet *cs
);

/*
** Attempt to resolve conflicts by calling each extension's on_conflict
** callback.  Marks conflicts as resolved when a callback provides a
** resolution.
**
** Parameters:
**   cs        - Conflict set with detected conflicts
**   mods      - The modifications array (for building ConflictInfo)
**   nmods     - Number of modifications
**   registry  - Extension registry (for looking up callbacks)
**
** Returns the number of conflicts that remain unresolved.
*/
uint32_t resolve_conflicts(
    ConflictSet *cs,
    const struct GrammarModification *mods,
    uint32_t nmods,
    struct ExtensionRegistry *registry
);

#endif /* CONFLICT_H */
