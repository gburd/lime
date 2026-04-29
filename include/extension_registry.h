/**
 * @file extension_registry.h
 * @brief Extension Registry -- manages grammar extensions with rich metadata.
 *
 * The extension registry manages grammar extensions with metadata
 * including disambiguation strategies, execution policies, dependency
 * tracking, and conflict declarations.  It builds on the existing
 * extension system (src/extension.h) by adding:
 *
 *   - Per-extension disambiguation strategy and priority
 *   - Execution policy (sequential, parallel, pipeline, etc.)
 *   - Oracle callbacks for runtime conflict resolution
 *   - Declarative dependency and conflict relationships
 *   - Topological ordering with cycle detection
 *   - Hash-table-backed O(1) lookups by name
 *
 * @par Usage Example
 * @code
 *   ExtensionRegistry *reg = extension_registry_create();
 *
 *   GrammarExtensionMetadata meta = {
 *       .name    = "jsonb",
 *       .version = "1.0.0",
 *       .strategy = DISAMBIG_PRIORITY,
 *       .priority = 100,
 *       .policy   = EXEC_SEQUENTIAL,
 *   };
 *   extension_registry_register(reg, &meta);
 *
 *   char *error = NULL;
 *   extension_registry_check_dependencies(reg, &error);
 *
 *   extension_registry_destroy(reg);
 * @endcode
 *
 * @see disambiguation.h for conflict resolution strategies.
 * @see execution_policy.h for execution policy details.
 */
#ifndef EXTENSION_REGISTRY_H
#define EXTENSION_REGISTRY_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/*  Forward declarations                                               */
/* ------------------------------------------------------------------ */

struct ParserSnapshot;
struct GrammarModification;

/* ------------------------------------------------------------------ */
/** @defgroup disambig_strategy Disambiguation Strategies
 *  @brief How an extension prefers to resolve ambiguities.
 *  @{
 */
/* ------------------------------------------------------------------ */

/**
 * @brief How an extension prefers to resolve ambiguities introduced by
 *        its grammar modifications.
 */
typedef enum DisambiguationStrategy {
    DISAMBIG_PRIORITY = 0,    /**< Resolve by numeric priority (higher wins) */
    DISAMBIG_FORK_RESOLVE,    /**< Fork the parse and resolve after lookahead */
    DISAMBIG_ORACLE,          /**< Delegate to a runtime oracle callback */
    DISAMBIG_CONTEXT,         /**< Use grammar context to disambiguate */
    DISAMBIG_NONE,            /**< No disambiguation; conflicts are errors */
} DisambiguationStrategy;

/** @} */ /* end disambig_strategy */

/* ------------------------------------------------------------------ */
/** @defgroup exec_policy Execution Policies
 *  @brief Controls how an extension's modifications are applied.
 *  @{
 */
/* ------------------------------------------------------------------ */

/**
 * @brief Controls how an extension's modifications are applied relative
 *        to other extensions.
 */
typedef enum ExecutionPolicy {
    EXEC_SEQUENTIAL = 0,      /**< Apply modifications one at a time */
    EXEC_PARALLEL,            /**< Apply concurrently where safe */
    EXEC_PIPELINE,            /**< Apply as a pipeline stage */
    EXEC_LAZY,                /**< Defer application until first use */
} ExecutionPolicy;

/** @} */ /* end exec_policy */

/* ------------------------------------------------------------------ */
/** @defgroup oracle_cb Oracle Callback
 *  @brief Runtime conflict resolution via user-supplied oracles.
 *  @{
 */
/* ------------------------------------------------------------------ */

/**
 * @brief Context passed to an oracle callback when the system needs a
 *        disambiguation decision.
 */
typedef struct OracleContext {
    const char *extension_name;         /**< Extension requesting resolution */
    const char *conflict_description;   /**< Human-readable conflict info */
    int candidate_count;                /**< Number of candidate resolutions */
    const char **candidate_labels;      /**< Labels for each candidate */
    void *user_data;                    /**< Extension's user_data pointer */
} OracleContext;

/**
 * @brief Oracle callback type.
 *
 * Called when the disambiguation system needs a runtime decision.
 *
 * @param ctx Context describing the conflict and candidates.
 * @return Zero-based index of the chosen candidate, or -1 if the
 *         oracle cannot decide.
 */
typedef int (*OracleCallback)(const OracleContext *ctx);

/** @} */ /* end oracle_cb */

/* ------------------------------------------------------------------ */
/** @defgroup ext_metadata Extension Metadata
 *  @brief Rich metadata for grammar extensions.
 *  @{
 */
/* ------------------------------------------------------------------ */

/**
 * @brief Rich metadata for a grammar extension.
 *
 * Passed to extension_registry_register(); the registry copies all
 * strings and arrays internally.
 */
typedef struct GrammarExtensionMetadata {
    /** @name Identity */
    /** @{ */
    const char *name;                   /**< Unique extension name (required) */
    const char *version;                /**< Semver string (required) */
    /** @} */

    /** @name Disambiguation */
    /** @{ */
    DisambiguationStrategy strategy;    /**< How to resolve ambiguities */
    int priority;                       /**< Priority for DISAMBIG_PRIORITY (higher wins) */
    /** @} */

    /** @name Execution */
    /** @{ */
    ExecutionPolicy policy;             /**< How to apply modifications */
    /** @} */

    /** @name Oracle */
    /** @{ */
    OracleCallback oracle;              /**< Callback for DISAMBIG_ORACLE (may be NULL) */
    /** @} */

    /**
     * @brief Conflict threshold.
     *
     * Fraction [0.0, 1.0] of modifications that may conflict before
     * the extension is rejected outright.  0.0 means no conflicts
     * tolerated; 1.0 means all conflicts are tolerable.
     */
    float conflict_threshold;

    /**
     * @brief Dependencies.
     *
     * NULL-terminated array of extension names that must be registered
     * before this extension can be activated.
     */
    const char **requires;

    /**
     * @brief Conflicts.
     *
     * NULL-terminated array of extension names that are incompatible
     * with this extension.
     */
    const char **conflicts_with;

    /**
     * @brief Grammar modifications (may be NULL if provided later via load).
     */
    struct GrammarModification *modifications;

    /** @brief Number of entries in the modifications array. */
    uint32_t nmodifications;
} GrammarExtensionMetadata;

/** @} */ /* end ext_metadata */

/* ------------------------------------------------------------------ */
/** @defgroup registry_handle Registry Handle
 *  @brief Opaque extension registry type.
 *  @{
 */
/* ------------------------------------------------------------------ */

/** @brief Opaque extension registry handle. */
typedef struct ExtensionRegistry ExtensionRegistry;

/** @} */ /* end registry_handle */

/* ------------------------------------------------------------------ */
/** @defgroup ext_order Topological Order Result
 *  @brief Result of dependency resolution.
 *  @{
 */
/* ------------------------------------------------------------------ */

/**
 * @brief Result of dependency resolution: a sorted array of extension
 *        names in load order (dependencies before dependents).
 */
typedef struct ExtensionOrder {
    char **names;               /**< malloc'd array of strdup'd names */
    uint32_t count;             /**< Number of entries */
} ExtensionOrder;

/**
 * @brief Free an ExtensionOrder and all owned strings.
 *
 * @param order Order to destroy.  Passing NULL is safe.
 */
void extension_order_destroy(ExtensionOrder *order);

/** @} */ /* end ext_order */

/* ------------------------------------------------------------------ */
/** @defgroup registry_api Registry API
 *  @brief Create, query, and manage the extension registry.
 *  @{
 */
/* ------------------------------------------------------------------ */

/**
 * @brief Create an empty extension registry.
 *
 * @return New registry, or NULL on allocation failure.
 *
 * @see extension_registry_destroy()
 */
ExtensionRegistry *extension_registry_create(void);

/**
 * @brief Register an extension with its metadata.
 *
 * The registry copies all strings and arrays from @p metadata.
 *
 * @param reg      The extension registry.
 * @param metadata Extension metadata to register.
 * @retval true  Registration succeeded.
 * @retval false Name is NULL, empty, or already registered.
 *
 * @see extension_registry_unregister()
 */
bool extension_registry_register(ExtensionRegistry *reg,
                                 const GrammarExtensionMetadata *metadata);

/**
 * @brief Look up an extension by name.
 *
 * @param reg  The extension registry.
 * @param name Extension name to search for.
 * @return Pointer to internal metadata, or NULL if not found.
 *
 * @warning The returned pointer is valid only until the registry is
 *          modified (register/unregister).
 */
const GrammarExtensionMetadata *extension_registry_find(
    ExtensionRegistry *reg,
    const char *name);

/**
 * @brief Validate all dependency and conflict declarations.
 *
 * Checks:
 *   1. Every name in 'requires' refers to a registered extension.
 *   2. No extension lists a registered extension in 'conflicts_with'.
 *   3. The dependency graph is acyclic (topological sort succeeds).
 *
 * @param reg The extension registry.
 * @param[out] error_out On failure, receives a malloc'd error message
 *                       (caller must free).  May be NULL.  Set to NULL
 *                       on success.
 * @retval true  All dependencies and conflicts are valid.
 * @retval false A problem was found; see @p error_out.
 *
 * @see extension_registry_get_order()
 */
bool extension_registry_check_dependencies(ExtensionRegistry *reg,
                                           char **error_out);

/**
 * @brief Produce a topological ordering of all registered extensions.
 *
 * Extensions are sorted so that dependencies come before dependents.
 *
 * @param reg The extension registry.
 * @param[out] order_out Filled with the sorted extension names on success.
 * @param[out] error_out On failure, receives a malloc'd error message.
 * @retval true  Ordering succeeded.
 * @retval false Cycle detected or missing dependency; see @p error_out.
 */
bool extension_registry_get_order(ExtensionRegistry *reg,
                                  ExtensionOrder *order_out,
                                  char **error_out);

/**
 * @brief Unregister an extension by name.
 *
 * @param reg  The extension registry.
 * @param name Extension name to remove.
 * @retval true  Extension was found and removed.
 * @retval false Extension was not found.
 */
bool extension_registry_unregister(ExtensionRegistry *reg,
                                   const char *name);

/**
 * @brief Return the number of registered extensions.
 *
 * @param reg The extension registry.
 * @return Count of registered extensions.
 */
uint32_t extension_registry_count(const ExtensionRegistry *reg);

/**
 * @brief Visitor callback for extension iteration.
 *
 * @param meta      Metadata for the current extension.
 * @param user_data User-provided context pointer.
 * @retval true  Continue iteration.
 * @retval false Stop iteration early.
 */
typedef bool (*ExtensionVisitorFn)(const GrammarExtensionMetadata *meta,
                                   void *user_data);

/**
 * @brief Iterate over all registered extensions.
 *
 * The callback receives each extension's metadata and the user-provided
 * context pointer.
 *
 * @param reg      The extension registry.
 * @param visitor  Callback function.
 * @param user_data Opaque pointer forwarded to @p visitor.
 */
void extension_registry_foreach(const ExtensionRegistry *reg,
                                ExtensionVisitorFn visitor,
                                void *user_data);

/**
 * @brief Destroy the registry and free all owned memory.
 *
 * @param reg Registry to destroy.  Passing NULL is safe.
 */
void extension_registry_destroy(ExtensionRegistry *reg);

/** @} */ /* end registry_api */

#endif /* EXTENSION_REGISTRY_H */
