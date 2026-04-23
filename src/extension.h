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
#include <pthread.h>

/* Re-use the ExtensionID type from the token table. */
typedef uint32_t ExtensionID;

/* Forward declarations */
struct ParserSnapshot;

/* ------------------------------------------------------------------ */
/*  Grammar modification types                                         */
/* ------------------------------------------------------------------ */

typedef enum GrammarModType {
    MOD_ADD_RULE,           /* Add a new production rule             */
    MOD_ADD_TOKEN,          /* Add a new terminal token              */
    MOD_MODIFY_PRECEDENCE,  /* Change precedence of an existing rule */
    MOD_ADD_TYPE,           /* Add a new non-terminal type           */
    MOD_REMOVE_RULE,        /* Remove an existing rule               */
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
            const char *lhs;         /* Left-hand side non-terminal  */
            const char **rhs;        /* NULL-terminated RHS symbols  */
            int nrhs;                /* Number of RHS symbols        */
            const char *code;        /* Reduction action (C code)    */
            int precedence;          /* -1 if unset                  */
        } add_rule;

        /* MOD_ADD_TOKEN */
        struct {
            const char *name;        /* Token name (e.g. "TK_JSONB") */
            const char *lexeme;      /* Literal text if applicable   */
            int token_code;          /* Assigned token code, or -1 for auto */
        } add_token;

        /* MOD_MODIFY_PRECEDENCE */
        struct {
            const char *symbol;      /* Symbol whose precedence changes */
            int new_precedence;
            int new_assoc;           /* 0=none, 1=left, 2=right, 3=nonassoc */
        } modify_prec;

        /* MOD_ADD_TYPE */
        struct {
            const char *name;        /* Non-terminal name */
            const char *datatype;    /* C type for the value union   */
        } add_type;

        /* MOD_REMOVE_RULE */
        struct {
            const char *lhs;
            int rule_index;          /* Index of rule to remove, or -1 for all */
        } remove_rule;
    } u;
} GrammarModification;

/* ------------------------------------------------------------------ */
/*  Conflict resolution                                                */
/* ------------------------------------------------------------------ */

typedef enum ConflictResolution {
    CONFLICT_UNRESOLVED = 0,  /* No resolution provided            */
    CONFLICT_KEEP_EXISTING,   /* Keep the existing rule/token       */
    CONFLICT_USE_NEW,         /* Replace with the new modification  */
    CONFLICT_MERGE,           /* Extension provides merged result   */
} ConflictResolution;

typedef struct ConflictInfo {
    ExtensionID existing_ext;         /* Extension that owns existing item */
    ExtensionID new_ext;              /* Extension proposing new item      */
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
typedef bool (*ExtGetModificationsFn)(
    void *user_data,
    const struct ParserSnapshot *base_snapshot,
    GrammarModification **mods_out,
    uint32_t *nmods_out
);

/*
** on_conflict: Called when a modification conflicts with another
** extension.  Returns a ConflictResolution value.
*/
typedef ConflictResolution (*ExtOnConflictFn)(
    void *user_data,
    const ConflictInfo *info
);

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
    EXT_REGISTERED,    /* Registered but not yet loaded  */
    EXT_LOADED,        /* Active, modifications applied  */
    EXT_UNLOADED,      /* Was loaded, now removed        */
    EXT_ERROR,         /* Failed to load                 */
} ExtensionState;

typedef struct Extension {
    ExtensionID id;
    char *name;                        /* Human-readable name (owned)   */
    char *version;                     /* Semver string (owned)         */

    ExtensionState state;

    /* Callbacks */
    ExtGetModificationsFn get_modifications;
    ExtOnConflictFn on_conflict;       /* May be NULL (default: KEEP_EXISTING) */
    ExtOnUnloadFn on_unload;           /* May be NULL                   */
    void *user_data;                   /* Opaque pointer passed to callbacks   */

    /* Cached modifications from last load */
    GrammarModification *modifications;
    uint32_t nmodifications;
} Extension;

/* ------------------------------------------------------------------ */
/*  Extension registration info (input to register_extension)          */
/* ------------------------------------------------------------------ */

typedef struct ExtensionInfo {
    const char *name;                  /* Copied internally         */
    const char *version;               /* Copied internally         */

    ExtGetModificationsFn get_modifications;  /* Required           */
    ExtOnConflictFn on_conflict;              /* Optional (NULL ok) */
    ExtOnUnloadFn on_unload;                  /* Optional (NULL ok) */
    void *user_data;
} ExtensionInfo;

/* ------------------------------------------------------------------ */
/*  Extension registry                                                 */
/* ------------------------------------------------------------------ */

typedef struct ExtensionRegistry {
    Extension *extensions;             /* Dynamic array of extensions   */
    uint32_t count;                    /* Number of registered extensions */
    uint32_t capacity;                 /* Allocated slots               */
    ExtensionID next_id;               /* Next ID to assign (starts at 1) */
    pthread_rwlock_t lock;             /* Protects the registry         */
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
bool register_extension(ExtensionRegistry *reg,
                        const ExtensionInfo *info,
                        ExtensionID *id_out);

/*
** Load a previously registered extension.  Calls the extension's
** get_modifications callback with the current base snapshot and
** transitions the extension to EXT_LOADED.  On failure sets *error
** to a malloc'd message and returns false.
*/
bool load_extension(ExtensionRegistry *reg,
                    ExtensionID id,
                    const struct ParserSnapshot *base_snapshot,
                    char **error);

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

#endif /* EXTENSION_H */
