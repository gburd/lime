/*
** Parser composition operations for combining parser modules.
**
** Provides union and merge operations over ParserSnapshots so that
** independently defined grammar modules can be combined into a single
** parser.  The composition pipeline is:
**
**   1. Pre-flight validation (dependency satisfaction, symbol checks)
**   2. Symbol unification across snapshots
**   3. Rule merging with conflict detection
**   4. State recomputation via rebuild_automaton()
**   5. Action table generation
**   6. Merkle tree computation for content hashing
**
** Two primary operations are exposed:
**
**   compose_snapshots() -- union of N snapshots into one.  All symbols
**     and rules from every snapshot are included.  Conflicts are
**     detected and reported.
**
**   merge_snapshots() -- merge an extension snapshot into a base
**     snapshot.  The extension's symbols and rules extend (or
**     override, subject to conflict resolution) the base.
*/
#ifndef PARSER_COMPOSITION_H
#define PARSER_COMPOSITION_H

#include "snapshot.h"
#include "conflict.h"
#include "dependency_resolver.h"
#include "merkle_tree.h"

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Result codes                                                       */
/* ------------------------------------------------------------------ */

typedef enum CompositionResult {
    COMPOSE_OK = 0,                 /* Composition succeeded              */
    COMPOSE_ERR_ALLOC,              /* Memory allocation failure          */
    COMPOSE_ERR_INVALID_INPUT,      /* NULL or otherwise invalid input    */
    COMPOSE_ERR_SYMBOL_COLLISION,   /* Unresolved symbol name collision   */
    COMPOSE_ERR_RULE_CONFLICT,      /* Unresolved rule conflict           */
    COMPOSE_ERR_DEPENDENCY,         /* Dependency not satisfied           */
    COMPOSE_ERR_BUILD,              /* LALR(1) automaton rebuild failed   */
    COMPOSE_ERR_CONFLICT,           /* Unresolved conflicts remain        */
} CompositionResult;

/* ------------------------------------------------------------------ */
/*  Composition options                                                */
/* ------------------------------------------------------------------ */

/*
** Flags controlling the composition behaviour.
*/
typedef enum CompositionFlags {
    COMPOSE_FLAG_NONE             = 0,

    /* When set, duplicate rules (same LHS and RHS) across snapshots
    ** are silently deduplicated rather than flagged as conflicts. */
    COMPOSE_FLAG_DEDUP_RULES      = (1 << 0),

    /* When set, if two snapshots define the same terminal symbol the
    ** later snapshot's definition wins (last-writer-wins). */
    COMPOSE_FLAG_LAST_WINS        = (1 << 1),

    /* When set, skip the dependency validation step.  Useful when
    ** composing raw snapshots that don't carry module metadata. */
    COMPOSE_FLAG_SKIP_DEPS        = (1 << 2),

    /* When set, compute a merkle tree over the composed result and
    ** store the root hash in the output snapshot's merkle_root field. */
    COMPOSE_FLAG_COMPUTE_MERKLE   = (1 << 3),
} CompositionFlags;

typedef struct CompositionOptions {
    CompositionFlags flags;

    /* Optional: modules corresponding to each snapshot.  When non-NULL,
    ** dependency validation is performed unless SKIP_DEPS is set.
    ** The array length must match the snapshot count. */
    ParserModule **modules;
    uint32_t nmodules;
} CompositionOptions;

/* ------------------------------------------------------------------ */
/*  Composition diagnostics                                            */
/* ------------------------------------------------------------------ */

/*
** Detailed information about a symbol unification event.
*/
typedef struct SymbolMapping {
    char *name;                     /* Symbol name (owned)                */
    uint32_t source_snapshot;       /* Index of the originating snapshot  */
    uint32_t original_index;        /* Symbol index in the source         */
    uint32_t unified_index;         /* Symbol index in the composed result*/
} SymbolMapping;

/*
** Diagnostic output from a composition operation.
*/
typedef struct CompositionDiagnostics {
    ConflictSet *conflicts;         /* Detected conflicts (owned, may be NULL) */

    SymbolMapping *symbol_map;      /* Symbol unification details         */
    uint32_t nsymbol_mappings;

    MerkleTree *merkle;             /* Content hash tree (owned, may be NULL) */

    char *error;                    /* Error message (owned, may be NULL) */
} CompositionDiagnostics;

/*
** Free all resources owned by a CompositionDiagnostics.  Does not free
** the struct itself (it is typically stack-allocated).
*/
void composition_diagnostics_destroy(CompositionDiagnostics *diag);

/* ------------------------------------------------------------------ */
/*  Validation                                                         */
/* ------------------------------------------------------------------ */

/*
** Pre-flight validation of a set of snapshots before composition.
** Checks:
**   - All snapshot pointers are non-NULL
**   - If modules are provided, dependency graph is acyclic and all
**     version constraints are satisfied
**   - Imported symbols are exported by some dependency
**
** Returns COMPOSE_OK if validation passes.  On failure, *diag is
** populated with details.
*/
CompositionResult validate_composition_inputs(
    ParserSnapshot **snapshots,
    uint32_t nsnapshots,
    const CompositionOptions *opts,
    CompositionDiagnostics *diag
);

/* ------------------------------------------------------------------ */
/*  Composition operations                                             */
/* ------------------------------------------------------------------ */

/*
** Compose (union) N snapshots into a single new snapshot.
**
** All symbols and rules from every snapshot are included in the result.
** Symbol names that appear in multiple snapshots are unified: if two
** snapshots define the same terminal, a SYMBOL_COLLISION conflict is
** raised (unless LAST_WINS or DEDUP_RULES flags are set).
**
** Parameters:
**   snapshots  - Array of snapshot pointers to compose
**   nsnapshots - Number of snapshots (must be >= 1)
**   opts       - Composition options (may be NULL for defaults)
**   out        - On success, receives a new snapshot with refcount == 1
**   diag       - On failure, receives diagnostic details (may be NULL)
**
** Returns COMPOSE_OK on success.
*/
CompositionResult compose_snapshots(
    ParserSnapshot **snapshots,
    uint32_t nsnapshots,
    const CompositionOptions *opts,
    ParserSnapshot **out,
    CompositionDiagnostics *diag
);

/*
** Merge an extension snapshot into a base snapshot.
**
** This is a specialised two-snapshot composition where the base snapshot
** is the primary grammar and the extension adds to or overrides it.
**
** Parameters:
**   base       - The base snapshot (not modified)
**   extension  - The extension snapshot to merge in
**   opts       - Composition options (may be NULL for defaults)
**   out        - On success, receives a new snapshot with refcount == 1
**   diag       - On failure, receives diagnostic details (may be NULL)
**
** Returns COMPOSE_OK on success.
*/
CompositionResult merge_snapshots(
    const ParserSnapshot *base,
    const ParserSnapshot *extension,
    const CompositionOptions *opts,
    ParserSnapshot **out,
    CompositionDiagnostics *diag
);

/* ------------------------------------------------------------------ */
/*  Utility                                                            */
/* ------------------------------------------------------------------ */

/*
** Compute a merkle tree from a snapshot's grammar data.  Creates leaf
** nodes for symbols, rules, states, and action tables, then builds
** a tree from the bottom up.
**
** Returns NULL on failure.  The caller owns the returned tree and must
** free it with merkle_free_tree().
*/
MerkleTree *compute_snapshot_merkle(const ParserSnapshot *snap);

#ifdef __cplusplus
}
#endif

#endif /* PARSER_COMPOSITION_H */
