/**
 * @file conflict.h
 * @brief Conflict detection for the extension system.
 *
 * When multiple extensions modify a grammar, conflicts can arise:
 *   - Token collisions: two extensions add the same token name
 *   - Rule duplicates: two extensions add identical production rules
 *   - Precedence conflicts: conflicting precedence/associativity assignments
 *   - Shift/reduce and reduce/reduce conflicts in the rebuilt automaton
 *
 * detect_conflicts() scans an array of modifications and reports all
 * conflicts found.  Extensions can resolve conflicts via their
 * on_conflict callback.
 *
 * @par Multi-Grammar Conflict Detection
 * When multiple grammar extensions are loaded simultaneously, ambiguity
 * can arise at three levels:
 *
 *   | Level    | Description                                          |
 *   |----------|------------------------------------------------------|
 *   | TOKEN    | Same lexeme maps to different tokens in different grammars |
 *   | RULE     | A token sequence can be parsed by rules from different grammars |
 *   | SEMANTIC | Same syntactic construct carries different semantic actions |
 *
 * @see disambiguation.h for conflict resolution strategies.
 * @see extension_registry.h for extension metadata.
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
/** @defgroup conflict_types Conflict Types
 *  @brief Types of conflicts that can occur between grammar extensions.
 *  @{
 */
/* ------------------------------------------------------------------ */

/**
 * @brief Types of conflicts that can occur between grammar extensions.
 */
typedef enum ConflictType {
    CONFLICT_TOKEN_COLLISION,     /**< Same token name from different extensions */
    CONFLICT_DUPLICATE_RULE,      /**< Identical production rule */
    CONFLICT_PRECEDENCE_CLASH,    /**< Conflicting precedence assignment */
    CONFLICT_SHIFT_REDUCE,        /**< Shift/reduce in rebuilt automaton */
    CONFLICT_REDUCE_REDUCE,       /**< Reduce/reduce in rebuilt automaton */
} ConflictType;

/**
 * @brief Description of a single conflict between modifications.
 */
typedef struct Conflict {
    ConflictType type;            /**< What kind of conflict */

    uint32_t mod_index_a;         /**< Index of first conflicting modification */
    uint32_t mod_index_b;         /**< Index of second conflicting modification */

    ExtensionID ext_id_a;         /**< Extension owning modification A */
    ExtensionID ext_id_b;         /**< Extension owning modification B */

    char *description;            /**< Human-readable description (malloc'd, owned by ConflictSet) */

    bool resolved;                /**< Whether this conflict was resolved */
} Conflict;

/** @} */ /* end conflict_types */

/* ------------------------------------------------------------------ */
/** @defgroup conflict_set Conflict Set
 *  @brief Collection of detected conflicts.
 *  @{
 */
/* ------------------------------------------------------------------ */

/**
 * @brief Collection of detected conflicts.
 */
typedef struct ConflictSet {
    Conflict *conflicts;          /**< Dynamic array of conflicts */
    uint32_t count;               /**< Number of conflicts in the set */
    uint32_t capacity;            /**< Allocated slots */
} ConflictSet;

/**
 * @brief Create an empty conflict set.
 *
 * @return New conflict set, or NULL on allocation failure.
 *
 * @see conflict_set_destroy()
 */
ConflictSet *conflict_set_create(void);

/**
 * @brief Destroy a conflict set and free all owned memory.
 *
 * @param cs Conflict set to destroy.  Passing NULL is safe.
 */
void conflict_set_destroy(ConflictSet *cs);

/**
 * @brief Add a conflict to the set.
 *
 * @param cs          Conflict set to add to.
 * @param type        Type of conflict.
 * @param mod_index_a Index of the first conflicting modification.
 * @param mod_index_b Index of the second conflicting modification.
 * @param ext_id_a    Extension ID owning modification A.
 * @param ext_id_b    Extension ID owning modification B.
 * @param description Human-readable description (copied internally).
 * @retval true  Conflict was added.
 * @retval false Allocation failure.
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

/**
 * @brief Return the number of unresolved conflicts in the set.
 *
 * @param cs Conflict set to query.
 * @return Count of conflicts where resolved == false.
 */
uint32_t conflict_set_unresolved_count(const ConflictSet *cs);

/** @} */ /* end conflict_set */

/* ------------------------------------------------------------------ */
/** @defgroup conflict_detect Conflict Detection
 *  @brief Scan modifications for conflicts.
 *  @{
 */
/* ------------------------------------------------------------------ */

/**
 * @brief Scan modifications for conflicts.
 *
 * Checks for:
 *   - Token name collisions between different extensions
 *   - Duplicate production rules
 *   - Conflicting precedence assignments
 *
 * Automaton-level conflicts (shift/reduce, reduce/reduce) are detected
 * later during rebuild_automaton() and added to the conflict set then.
 *
 * @param mods  Array of grammar modifications to check.
 * @param nmods Number of modifications.
 * @param cs    Conflict set to populate (must be pre-created).
 * @retval true  At least one conflict was detected.
 * @retval false No conflicts found.
 */
bool detect_conflicts(
    const struct GrammarModification *mods,
    uint32_t nmods,
    ConflictSet *cs
);

/**
 * @brief Attempt to resolve conflicts via extension callbacks.
 *
 * Calls each extension's on_conflict callback for the conflicts that
 * involve its modifications.  Marks conflicts as resolved when a
 * callback provides a resolution.
 *
 * @param cs       Conflict set with detected conflicts.
 * @param mods     The modifications array (for building ConflictInfo).
 * @param nmods    Number of modifications.
 * @param registry Extension registry (for looking up callbacks).
 * @return Number of conflicts that remain unresolved.
 */
uint32_t resolve_conflicts(
    ConflictSet *cs,
    const struct GrammarModification *mods,
    uint32_t nmods,
    struct ExtensionRegistry *registry
);

/** @} */ /* end conflict_detect */

/* ------------------------------------------------------------------ */
/** @defgroup multi_grammar Multi-Grammar Conflict Detection
 *  @brief Detect ambiguities across multiple loaded grammar extensions.
 *  @{
 */
/* ------------------------------------------------------------------ */

/**
 * @brief Conflict detection levels for multi-grammar scenarios.
 */
typedef enum ConflictLevel {
    CONFLICT_LEVEL_TOKEN    = 0,   /**< Lexer-level ambiguity */
    CONFLICT_LEVEL_RULE     = 1,   /**< Parser-level ambiguity */
    CONFLICT_LEVEL_SEMANTIC = 2,   /**< Semantic action ambiguity */
} ConflictLevel;

/**
 * @brief A grammar context -- one possible interpretation of a token
 *        within a particular grammar/extension.
 */
typedef struct LimeContext {
    ExtensionID ext_id;            /**< Extension providing this context */
    uint16_t token;                /**< Token code in this grammar's space */
    int state;                     /**< Parser state where this applies */
    int priority;                  /**< Higher = preferred (from extension) */
    const char *grammar_name;      /**< Human-readable grammar name (not owned) */
} LimeContext;

/**
 * @brief A specific ambiguity where multiple grammars can handle the
 *        same token in the same parser state.
 */
typedef struct ConflictPoint {
    uint16_t token;                /**< The token that triggers the conflict */
    int state;                     /**< The parser state (-1 if token-level) */
    ConflictLevel level;           /**< Which level of ambiguity */

    LimeContext *contexts;         /**< Array of valid interpretations */
    int ncontexts;                 /**< Number of entries in contexts[] */
    int capacity;                  /**< Allocated slots in contexts[] */

    char *description;             /**< Human-readable summary (owned) */
} ConflictPoint;

/**
 * @brief Result of a multi-grammar conflict scan.
 */
typedef struct MultiGrammarConflictResult {
    ConflictPoint *points;         /**< Array of detected conflict points */
    uint32_t npoints;              /**< Number of conflict points */
    uint32_t capacity;             /**< Allocated slots in points[] */

    uint32_t token_conflicts;      /**< Count of token-level conflicts */
    uint32_t rule_conflicts;       /**< Count of rule-level conflicts */
    uint32_t semantic_conflicts;   /**< Count of semantic-level conflicts */
} MultiGrammarConflictResult;

/** @} */ /* end multi_grammar (types) */

/* ------------------------------------------------------------------ */
/** @defgroup conflict_point_api ConflictPoint Lifecycle
 *  @brief Create, populate, and destroy conflict points.
 *  @{
 */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize a ConflictPoint.
 *
 * The caller owns the struct (typically stack- or array-allocated).
 *
 * @param cp    ConflictPoint to initialize.
 * @param token Token code.
 * @param state Parser state (-1 for token-level conflicts).
 * @param level Ambiguity level.
 */
void conflict_point_init(ConflictPoint *cp, uint16_t token, int state,
                         ConflictLevel level);

/**
 * @brief Free resources owned by a ConflictPoint.
 *
 * Frees the contexts array and description.  Does not free the
 * ConflictPoint struct itself.
 *
 * @param cp ConflictPoint to clean up.
 */
void conflict_point_destroy(ConflictPoint *cp);

/**
 * @brief Add a grammar context to a conflict point.
 *
 * @param cp  ConflictPoint to add to.
 * @param ctx Context describing one possible interpretation.
 * @retval true  Context was added.
 * @retval false Allocation failure.
 */
bool conflict_point_add_context(ConflictPoint *cp, const LimeContext *ctx);

/** @} */ /* end conflict_point_api */

/* ------------------------------------------------------------------ */
/** @defgroup multi_conflict_result MultiGrammarConflictResult Lifecycle
 *  @brief Create and destroy multi-grammar conflict result sets.
 *  @{
 */
/* ------------------------------------------------------------------ */

/**
 * @brief Create an empty result set.
 *
 * @return New result set, or NULL on allocation failure.
 *
 * @see multi_conflict_result_destroy()
 */
MultiGrammarConflictResult *multi_conflict_result_create(void);

/**
 * @brief Destroy a result set and free all owned memory.
 *
 * @param result Result set to destroy.  Passing NULL is safe.
 */
void multi_conflict_result_destroy(MultiGrammarConflictResult *result);

/** @} */ /* end multi_conflict_result */

/* ------------------------------------------------------------------ */
/** @defgroup multi_detect_api Multi-Grammar Conflict Detection API
 *  @brief Scan loaded extensions for token, rule, and semantic conflicts.
 *  @{
 */
/* ------------------------------------------------------------------ */

/**
 * @brief Detect token-level conflicts across all loaded extensions.
 *
 * Scans the extension registry for loaded extensions whose modifications
 * introduce tokens that collide (same name, different semantics).
 *
 * @param reg    Extension registry with loaded extensions.
 * @param result Result set to populate (must be pre-created).
 * @return Number of token-level conflicts found.
 */
uint32_t detect_token_conflicts(
    struct ExtensionRegistry *reg,
    MultiGrammarConflictResult *result
);

/**
 * @brief Detect rule-level conflicts for a given token and state.
 *
 * Checks whether multiple extensions provide rules that could handle
 * the specified token at the given state, creating a parse ambiguity.
 *
 * @param reg    Extension registry with loaded extensions.
 * @param token  The token code to check.
 * @param state  The parser state to check.
 * @param result Result set to populate (must be pre-created).
 * @return Number of rule-level conflicts found.
 */
uint32_t detect_rule_conflicts(
    struct ExtensionRegistry *reg,
    uint16_t token,
    int state,
    MultiGrammarConflictResult *result
);

/**
 * @brief Detect semantic-level conflicts for a given token and state.
 *
 * Checks whether multiple extensions provide different semantic actions
 * for the same syntactic construct.
 *
 * @param reg    Extension registry with loaded extensions.
 * @param token  The token code to check.
 * @param state  The parser state to check.
 * @param result Result set to populate (must be pre-created).
 * @return Number of semantic-level conflicts found.
 */
uint32_t detect_semantic_conflicts(
    struct ExtensionRegistry *reg,
    uint16_t token,
    int state,
    MultiGrammarConflictResult *result
);

/**
 * @brief Detect all levels of conflict for a specific token and state.
 *
 * Convenience function that runs token, rule, and semantic detection
 * and returns a single ConflictPoint summarizing all interpretations.
 * If ncontexts > 1, the token is ambiguous.
 *
 * @param reg   Extension registry with loaded extensions.
 * @param token The token code to check.
 * @param state The parser state to check (-1 for token-only check).
 * @return ConflictPoint (caller must call conflict_point_destroy()).
 */
ConflictPoint detect_conflict(
    struct ExtensionRegistry *reg,
    uint16_t token,
    int state
);

/**
 * @brief Run a full multi-grammar conflict scan across all loaded extensions.
 *
 * Walks every loaded extension's modifications and detects conflicts at
 * all three levels (token, rule, semantic).
 *
 * @param reg    Extension registry with loaded extensions.
 * @param result Result set to populate (must be pre-created).
 * @retval true  At least one conflict was detected.
 * @retval false No conflicts found.
 */
bool detect_all_multi_grammar_conflicts(
    struct ExtensionRegistry *reg,
    MultiGrammarConflictResult *result
);

/** @} */ /* end multi_detect_api */

#endif /* CONFLICT_H */
