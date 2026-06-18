/*
** Extension system data structures and internal API.
**
** Extensions add grammar modifications (new tokens, rules, precedence
** changes) to the parser at runtime.  Each extension is identified by a
** unique ExtensionID and managed through a thread-safe registry.
**
** Lifecycle:
**   1. register_extension() -- define the extension and its callbacks
**   2. load_extension()     -- activate it (calls get_modifications)
**   3. unload_extension()   -- deactivate and remove its contributions
**
** The registry itself is protected by a pthread_rwlock so that lookups
** can proceed concurrently while registration/unregistration serialize.
*/
#ifndef EXTENSION_H
#define EXTENSION_H

#include <stdint.h>
#include <stdbool.h>
#include "lime_threads.h"

/* Re-use the ExtensionID type from the token table. */
typedef uint32_t ExtensionID;

/* Forward declarations */
struct ParserSnapshot;

/* ------------------------------------------------------------------ */
/*  Reduce action callback                                             */
/* ------------------------------------------------------------------ */

/*
** LimeReduceFn -- the callable an extension attaches to a MOD_ADD_RULE
** so its rule's reduction runs real code at parse time.
**
** Invoked by the parser when the rule reduces.  The extension is
** responsible for constructing the LHS value from the RHS slot values
** and writing it into *lhs_out.  The extension also owns the lifetime
** of any memory referenced from that LHS value; Lime treats `void *`
** stack slots as opaque payloads.
**
** Arguments:
**
**   user_data   The `reduce_user` pointer set on the GrammarModification.
**               Threaded through verbatim.
**
**   extra_arg   The parser's %extra_argument value, threaded through
**               parse_begin()->parse_token()->reduce unchanged.  NULL
**               when the grammar does not declare an %extra_argument.
**
**   nrhs        Number of right-hand-side symbols for this rule.  The
**               matching MOD_ADD_RULE stored nrhs at registration.
**
**   rhs_values  Array of `nrhs` symbol values in rule order (index 0 =
**               leftmost = $1).  Each element IS the symbol's value
**               by value -- a pointer-width payload -- NOT a pointer
**               to a slot holding the value: there is no extra
**               indirection.  For `%type {char *}` the element IS the
**               char*; for `%type {Node *}` it IS the Node*; read it
**               directly as `(Type)rhs_values[i]`, never
**               `*(Type *)rhs_values[i]`.  When the grammar's
**               %token_type is a *union* (e.g. PostgreSQL's
**               core_YYSTYPE), the element is the union's pointer-
**               width content (the active member's bits) -- a
**               terminal carrying `core_YYSTYPE.str` arrives as that
**               char* directly; do not reconstruct a `core_YYSTYPE *`
**               and dereference.  The extension must know its own
**               types.  (Semantic values must be pointer-
**               representable; see docs/HOST_REDUCE.md.)
**
**   rhs_locs    Array of `nrhs` byte-offset locations, one per RHS
**               symbol, or NULL if the grammar does not declare
**               %locations.  When present, each element is either a
**               non-negative byte offset or LIME_LOC_UNKNOWN (-1).
**
**   lhs_out     Writeback slot for the LHS value, laid out per the
**               LHS symbol's declared %type.  Symmetric with
**               rhs_values: store the value BY VALUE (the pointer-
**               width payload) at *lhs_out, e.g.
**               `*(Node **)lhs_out = node;` -- not via an extra slot
**               indirection.  The callback must fill
**               this slot before returning; on failure the extension
**               is responsible for zero-initialising it (Lime does
**               not inspect it but the next rule that consumes this
**               slot may).
**
** Contract:
**
**   - The callback runs on the parsing thread; it may not block or
**     longjmp out of the parser unless the allocator contract already
**     permits that.
**   - All pointers are valid for the duration of the call only.
**   - rhs_values and rhs_locs are read-only from the extension's
**     perspective; Lime may free or overwrite the backing storage
**     after the callback returns.
**
** NOTE: wiring from the parser template to this callback is not yet
** in place -- it lands together with the runtime apply_add_rule()
** implementation.  The type is declared here so extension code can be
** written against the final shape today.
*/
typedef void (*LimeReduceFn)(void *user_data, void *extra_arg, int nrhs, const void *rhs_values,
                             const int *rhs_locs, void *lhs_out);

/* ------------------------------------------------------------------ */
/*  Grammar modification types                                         */
/* ------------------------------------------------------------------ */

typedef enum GrammarModType {
    MOD_ADD_RULE,          /* Add a new production rule             */
    MOD_ADD_TOKEN,         /* Add a new terminal token              */
    MOD_MODIFY_PRECEDENCE, /* Change precedence of an existing rule */
    MOD_ADD_TYPE,          /* Add a new non-terminal type           */
    MOD_REMOVE_RULE,       /* Remove an existing rule               */
} GrammarModType;

/*
** A single grammar modification requested by an extension.
** The interpretation of the payload depends on `type`.
*/
typedef struct GrammarModification {
    GrammarModType type;

    /* Human-readable name for debugging / error messages */
    const char *description;

    union {
        /* MOD_ADD_RULE: a BNF-style rule in Lemon syntax */
        struct {
            const char *lhs;  /* Left-hand side non-terminal  */
            const char **rhs; /* NULL-terminated RHS symbols  */
            int nrhs;         /* Number of RHS symbols        */

            /*
            ** Reduce action, in order of precedence:
            **
            **   reduce != NULL
            **       The parser invokes reduce(reduce_user, ...) when
            **       this rule reduces.  See LimeReduceFn for the full
            **       calling convention.  This is the preferred path
            **       for runtime-loaded extensions because no runtime
            **       C compilation is required.
            **
            **   reduce == NULL && code != NULL
            **       `code` is a C statement block compiled into the
            **       parser's generated reduce() switch at build time.
            **       Applicable when the grammar is static (fed through
            **       the `lime` generator); not usable from extensions
            **       loaded at runtime, because the reduce() switch is
            **       already compiled.
            **
            **   both NULL
            **       No reduction action; the rule reduces but produces
            **       no value.
            */
            LimeReduceFn reduce; /* Callable action, or NULL     */
            void *reduce_user;   /* Opaque ptr for `reduce`      */
            const char *code;    /* Generator-time action text   */
            int precedence;      /* -1 if unset                  */
        } add_rule;

        /* MOD_ADD_TOKEN */
        struct {
            const char *name;   /* Token name (e.g. "TK_JSONB") */
            const char *lexeme; /* Literal text if applicable   */
            int token_code;     /* Assigned token code, or -1 for auto */
        } add_token;

        /* MOD_MODIFY_PRECEDENCE */
        struct {
            const char *symbol; /* Symbol whose precedence changes */
            int new_precedence;
            int new_assoc; /* 0=none, 1=left, 2=right, 3=nonassoc */
        } modify_prec;

        /* MOD_ADD_TYPE */
        struct {
            const char *name;     /* Non-terminal name */
            const char *datatype; /* C type for the value union   */
        } add_type;

        /* MOD_REMOVE_RULE */
        struct {
            const char *lhs;
            int rule_index; /* Index of rule to remove, or -1 for all */
        } remove_rule;
    } u;
} GrammarModification;

/* ------------------------------------------------------------------ */
/*  Conflict resolution                                                */
/* ------------------------------------------------------------------ */

typedef enum ConflictResolution {
    CONFLICT_UNRESOLVED = 0, /* No resolution provided            */
    CONFLICT_KEEP_EXISTING,  /* Keep the existing rule/token       */
    CONFLICT_USE_NEW,        /* Replace with the new modification  */
    CONFLICT_MERGE,          /* Extension provides merged result   */
} ConflictResolution;

typedef struct ConflictInfo {
    ExtensionID existing_ext; /* Extension that owns existing item */
    ExtensionID new_ext;      /* Extension proposing new item      */
    const GrammarModification *existing_mod;
    const GrammarModification *new_mod;
} ConflictInfo;

/* ------------------------------------------------------------------ */
/*  Extension callbacks                                                */
/* ------------------------------------------------------------------ */

/*
** get_modifications: Called when the extension is loaded.  The extension
** fills *mods_out with an array of modifications and sets *nmods_out to
** the count.  The array must remain valid until on_unload is called.
** Returns true on success.
*/
typedef bool (*ExtGetModificationsFn)(void *user_data, const struct ParserSnapshot *base_snapshot,
                                      GrammarModification **mods_out, uint32_t *nmods_out);

/*
** on_conflict: Called when a modification conflicts with another
** extension.  Returns a ConflictResolution value.
*/
typedef ConflictResolution (*ExtOnConflictFn)(void *user_data, const ConflictInfo *info);

/*
** on_unload: Called when the extension is being unloaded.  The extension
** should free any resources it allocated (including the modifications
** array returned by get_modifications).
*/
typedef void (*ExtOnUnloadFn)(void *user_data);

/* ------------------------------------------------------------------ */
/*  Extension descriptor                                               */
/* ------------------------------------------------------------------ */

typedef enum ExtensionState {
    EXT_REGISTERED, /* Registered but not yet loaded  */
    EXT_LOADED,     /* Active, modifications applied  */
    EXT_UNLOADED,   /* Was loaded, now removed        */
    EXT_ERROR,      /* Failed to load                 */
} ExtensionState;

typedef struct Extension {
    ExtensionID id;
    char *name;    /* Human-readable name (owned)   */
    char *version; /* Semver string (owned)         */

    ExtensionState state;

    /* Callbacks */
    ExtGetModificationsFn get_modifications;
    ExtOnConflictFn on_conflict; /* May be NULL (default: KEEP_EXISTING) */
    ExtOnUnloadFn on_unload;     /* May be NULL                   */
    void *user_data;             /* Opaque pointer passed to callbacks   */

    /* Cached modifications from last load */
    GrammarModification *modifications;
    uint32_t nmodifications;
} Extension;

/* ------------------------------------------------------------------ */
/*  Extension registration info (input to register_extension)          */
/* ------------------------------------------------------------------ */

typedef struct ExtensionInfo {
    const char *name;    /* Copied internally         */
    const char *version; /* Copied internally         */

    ExtGetModificationsFn get_modifications; /* Required           */
    ExtOnConflictFn on_conflict;             /* Optional (NULL ok) */
    ExtOnUnloadFn on_unload;                 /* Optional (NULL ok) */
    void *user_data;
} ExtensionInfo;

/* ------------------------------------------------------------------ */
/*  Extension registry                                                 */
/* ------------------------------------------------------------------ */

typedef struct ExtensionRegistry {
    Extension *extensions; /* Dynamic array of extensions   */
    uint32_t count;        /* Number of registered extensions */
    uint32_t capacity;     /* Allocated slots               */
    ExtensionID next_id;   /* Next ID to assign (starts at 1) */
    pthread_rwlock_t lock; /* Protects the registry         */
} ExtensionRegistry;

/* ------------------------------------------------------------------ */
/*  Internal API                                                       */
/* ------------------------------------------------------------------ */

/* Create / destroy the global extension registry */
ExtensionRegistry *create_extension_registry(void);
void destroy_extension_registry(ExtensionRegistry *reg);

/*
** Register an extension.  On success, *id_out receives the assigned
** ExtensionID and the function returns true.  The extension enters the
** EXT_REGISTERED state and must be explicitly loaded before its
** modifications take effect.
*/
bool register_extension(ExtensionRegistry *reg, const ExtensionInfo *info, ExtensionID *id_out);

/*
** Load a previously registered extension.  Calls the extension's
** get_modifications callback with the current base snapshot and
** transitions the extension to EXT_LOADED.  On failure sets *error
** to a malloc'd message and returns false.
*/
bool load_extension(ExtensionRegistry *reg, ExtensionID id,
                    const struct ParserSnapshot *base_snapshot, char **error);

/*
** Unload an extension.  Calls on_unload if provided, clears cached
** modifications, and transitions the extension to EXT_UNLOADED.
** Returns true on success, false if the extension was not found or
** not in a loadable state.
*/
bool unload_extension(ExtensionRegistry *reg, ExtensionID id);

/*
** Look up an extension by ID.  Returns a pointer to the Extension
** struct within the registry, or NULL if not found.  The caller must
** hold at least a read lock on the registry (or call this from within
** a registry operation).
*/
const Extension *find_extension(ExtensionRegistry *reg, ExtensionID id);

/*
** Get the number of currently loaded extensions.
*/
uint32_t get_loaded_extension_count(ExtensionRegistry *reg);

/* ------------------------------------------------------------------ */
/*  Snapshot publishing                                                */
/* ------------------------------------------------------------------ */

/*
** publish_modified_snapshot collects the modifications from every
** currently-loaded extension, runs them through
** create_modified_snapshot against *base*, and (on success) returns
** the resulting ParserSnapshot in *out_snap with refcount 1.  The
** caller owns the new snapshot reference.
**
** When at least one extension's modifications cause an unresolved
** conflict the function returns false, *error is filled with a
** human-readable message, and *out_conflicts (if non-NULL) receives
** the ConflictSet for inspection (the caller frees it via
** conflict_set_destroy()).
**
** This is the single entry point applications use to materialise the
** "active extended grammar" after loading or unloading extensions.
*/
struct ConflictSet;
bool publish_modified_snapshot(ExtensionRegistry *reg, const struct ParserSnapshot *base,
                               struct ParserSnapshot **out_snap,
                               struct ConflictSet **out_conflicts, char **error);

#endif /* EXTENSION_H */
