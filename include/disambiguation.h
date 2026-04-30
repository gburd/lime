/**
 * @file disambiguation.h
 * @brief Disambiguation Strategy Interface -- pluggable conflict resolution.
 *
 * Provides a pluggable strategy system for resolving conflicts between
 * grammar extensions.  When multiple extensions contribute modifications
 * that conflict (e.g. shift/reduce, reduce/reduce, or duplicate rules),
 * a disambiguation strategy decides which extension "wins" or how to
 * reconcile them.
 *
 * Strategies are selected at configuration time and dispatched through
 * a vtable.  Built-in strategies include:
 *
 *   | Strategy          | Description                                    |
 *   |-------------------|------------------------------------------------|
 *   | STRAT_PRIORITY    | Resolve by static priority ordering            |
 *   | STRAT_FORK_RESOLVE| Fork parse state, try both, pick winner        |
 *   | STRAT_BAYESIAN    | Accumulate evidence, pick most likely          |
 *   | STRAT_LLM         | Query an LLM oracle for guidance               |
 *   | STRAT_CUSTOM      | User-supplied vtable                           |
 *
 * @par Usage Example
 * @code
 *   DisambiguationContext *ctx = disambiguation_create(STRAT_PRIORITY, reg);
 *   StrategyResult result = disambiguation_resolve(ctx, &conflict, parser);
 *   // ... use result.winning_contexts, result.confidence ...
 *   strategy_result_cleanup(&result);
 *   disambiguation_destroy(ctx);
 * @endcode
 *
 * Types LimeContext and ConflictPoint are defined in conflict.h, which
 * is the canonical owner of those types.
 *
 * @see conflict.h for LimeContext and ConflictPoint definitions.
 * @see extension_registry.h for extension metadata used by strategies.
 */
#ifndef DISAMBIGUATION_H
#define DISAMBIGUATION_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* LimeContext and ConflictPoint are defined here */
#include "conflict.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct ExtensionRegistry;
struct Extension;
struct ParseContext;

/* ================================================================== */
/** @defgroup strat_ids Strategy Identifiers
 *  @brief Enumeration of available disambiguation strategies.
 *  @{
 */
/* ================================================================== */

/**
 * @brief Enumeration of available disambiguation strategies.
 *
 * Note: this is the runtime-side strategy selector used by
 * disambiguation_create().  The user-facing extension metadata in
 * extension_registry.h uses a separate enum (DisambiguationStrategy
 * with DISAMBIG_* values) that describes an extension's preference
 * rather than the actual runtime strategy.
 */
typedef enum LimeStrategy {
    STRAT_PRIORITY      = 0,  /**< Resolve by extension priority metadata */
    STRAT_FORK_RESOLVE  = 1,  /**< Fork parse, try each, pick survivor */
    STRAT_BAYESIAN      = 2,  /**< Evidence accumulation (posterior probs) */
    STRAT_LLM           = 3,  /**< Query LLM oracle for resolution advice */
    STRAT_CUSTOM        = 4,  /**< User-provided vtable */
} LimeStrategy;

/** @} */ /* end strat_ids */

/* ================================================================== */
/** @defgroup strat_result Strategy Result
 *  @brief Outcome of a disambiguation resolution.
 *  @{
 */
/* ================================================================== */

/**
 * @brief The result of a disambiguation resolution.
 *
 * Contains the winning extension context(s) as LimeContext entries
 * (from conflict.h), a confidence score, and an optional textual
 * explanation.
 */
typedef struct StrategyResult {
    LimeContext *winning_contexts;    /**< Array of winners (malloc'd) */
    int nwinners;                     /**< Number of winners (usually 1) */
    float confidence;                 /**< Confidence: 0.0 = no idea, 1.0 = certain */
    char *explanation;                /**< Human-readable reason (malloc'd, may be NULL) */
} StrategyResult;

/**
 * @brief Initialize a StrategyResult to a safe empty state.
 *
 * @param result Result struct to initialize.
 */
void strategy_result_init(StrategyResult *result);

/**
 * @brief Free memory owned by a StrategyResult.
 *
 * Frees winning_contexts and explanation.  Does not free the
 * StrategyResult struct itself.
 *
 * @param result Result struct to clean up.
 */
void strategy_result_cleanup(StrategyResult *result);

/** @} */ /* end strat_result */

/* ================================================================== */
/** @defgroup strat_vtable Strategy VTable
 *  @brief Function pointer table for custom strategy implementations.
 *  @{
 */
/* ================================================================== */

/**
 * @brief Function pointer table for a disambiguation strategy implementation.
 *
 * Each strategy provides four callbacks.  The opaque @c strategy_context
 * pointer is strategy-specific state created by init() and freed by
 * destroy().
 */
typedef struct DisambiguationStrategyVTable {
    /**
     * @brief Initialize the strategy.
     *
     * Called once when the DisambiguationContext is created.  Receives
     * the loaded extensions so the strategy can precompute any needed
     * metadata (e.g. priority ordering).
     *
     * @param extensions  Array of loaded Extension pointers.
     * @param nextensions Count of extensions.
     * @return Opaque strategy context pointer, or NULL on failure.
     */
    void *(*init)(const struct Extension *const *extensions,
                  uint32_t nextensions);

    /**
     * @brief Resolve a single conflict.
     *
     * @param strategy_context Opaque pointer returned by init().
     * @param conflict         The conflict point to resolve.
     * @param parse_ctx        Current parse context (may be NULL if
     *                         resolution happens before parsing starts).
     * @param lookahead        Lookahead token code (-1 if not applicable).
     * @param[out] result      Filled with the resolution decision.
     * @retval true  Conflict was resolved.
     * @retval false Resolution failed.
     */
    bool (*resolve)(void *strategy_context,
                    const ConflictPoint *conflict,
                    struct ParseContext *parse_ctx,
                    int lookahead,
                    StrategyResult *result);

    /**
     * @brief Provide feedback after a parse completes.
     *
     * Strategies that learn (e.g. Bayesian) use this to update their
     * models.
     *
     * @param strategy_context Opaque pointer returned by init().
     * @param registry         Extension registry for metadata lookups.
     * @param success          True if the parse succeeded.
     */
    void (*update)(void *strategy_context,
                   struct ExtensionRegistry *registry,
                   bool success);

    /**
     * @brief Tear down the strategy and free all resources.
     *
     * @param strategy_context Opaque pointer returned by init().
     */
    void (*destroy)(void *strategy_context);

} DisambiguationStrategyVTable;

/** @} */ /* end strat_vtable */

/* ================================================================== */
/** @defgroup disambig_ctx Disambiguation Context
 *  @brief Opaque handle for a configured disambiguation session.
 *  @{
 */
/* ================================================================== */

/** @brief Opaque disambiguation context handle. */
typedef struct DisambiguationContext DisambiguationContext;

/** @} */ /* end disambig_ctx */

/* ================================================================== */
/** @defgroup disambig_api Disambiguation API
 *  @brief Create, use, and destroy disambiguation contexts.
 *  @{
 */
/* ================================================================== */

/**
 * @brief Create a disambiguation context using a built-in strategy.
 *
 * @param strategy Which strategy to use (STRAT_PRIORITY, etc.).
 * @param reg      The extension registry.  Must remain valid for the
 *                 lifetime of the context.
 * @return New context, or NULL on failure.
 *
 * @see disambiguation_create_custom() for user-supplied strategies.
 * @see disambiguation_destroy()
 */
DisambiguationContext *disambiguation_create(
    LimeStrategy strategy,
    struct ExtensionRegistry *reg);

/**
 * @brief Create a disambiguation context using a user-supplied vtable.
 *
 * @param vtable Strategy function pointers (copied internally).
 * @param reg    The extension registry.
 * @return New context, or NULL on failure.
 *
 * @see disambiguation_create() for built-in strategies.
 */
DisambiguationContext *disambiguation_create_custom(
    const DisambiguationStrategyVTable *vtable,
    struct ExtensionRegistry *reg);

/**
 * @brief Resolve a conflict using the configured strategy.
 *
 * @param ctx       The disambiguation context.
 * @param conflict  Description of the conflict to resolve.
 * @param parse_ctx Current parse context (may be NULL).
 * @return Resolution result.  Caller must call strategy_result_cleanup()
 *         on the returned struct when done.
 */
StrategyResult disambiguation_resolve(
    DisambiguationContext *ctx,
    const ConflictPoint *conflict,
    struct ParseContext *parse_ctx);

/**
 * @brief Provide feedback after a parse.
 *
 * Notifies the strategy so learning-based strategies can update their
 * models.
 *
 * @param ctx     The disambiguation context.
 * @param success True if the parse succeeded.
 */
void disambiguation_update(
    DisambiguationContext *ctx,
    bool success);

/**
 * @brief Get the strategy type used by this context.
 *
 * @param ctx The disambiguation context.
 * @return The LimeStrategy enum value.
 */
LimeStrategy disambiguation_get_strategy(
    const DisambiguationContext *ctx);

/**
 * @brief Get the name of a strategy as a string.
 *
 * @param strategy Strategy to name.
 * @return Static string like "priority", "fork-resolve", etc.
 */
const char *disambiguation_strategy_name(LimeStrategy strategy);

/**
 * @brief Destroy a disambiguation context and free all resources.
 *
 * @param ctx Context to destroy.  Passing NULL is safe.
 */
void disambiguation_destroy(DisambiguationContext *ctx);

/** @} */ /* end disambig_api */

#ifdef __cplusplus
}
#endif

#endif /* DISAMBIGUATION_H */
