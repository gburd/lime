/**
 * @file execution_policy.h
 * @brief Execution Policy Engine -- controls semantic action dispatch
 *        after disambiguation.
 *
 * When multiple grammar extensions can parse the same input, the
 * execution policy determines how their semantic actions are invoked.
 *
 * | Policy           | Description                                      |
 * |------------------|--------------------------------------------------|
 * | EXEC_FIRST_ONLY  | Only the highest-priority winner executes         |
 * | EXEC_ALL         | All winners execute independently                 |
 * | EXEC_CHAIN       | Winners execute in sequence, output chained       |
 * | EXEC_CONDITIONAL | Extension-provided callback decides               |
 *
 * @par Usage Example
 * @code
 *   ExecutionPolicyConfig config;
 *   execution_policy_config_init(&config);
 *   config.policy = EXEC_ALL;
 *   config.execute_fn = my_parser_execute;
 *
 *   int nresults = 0;
 *   ExecutionResult *results = execute_semantic_actions(
 *       &config, &strat_result, parsers, extensions, &nresults);
 *
 *   for (int i = 0; i < nresults; i++) {
 *       if (results[i].error) { ... handle error ... }
 *       else { ... use results[i].result ... }
 *   }
 *   execution_results_free(results, nresults);
 * @endcode
 *
 * @see disambiguation.h for StrategyResult.
 */
#ifndef EXECUTION_POLICY_H
#define EXECUTION_POLICY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Forward declarations                                               */
/* ------------------------------------------------------------------ */

struct StrategyResult;
struct LimeContext;

/* ------------------------------------------------------------------ */
/** @defgroup exec_ext_meta Grammar Extension Metadata (Execution)
 *  @brief Metadata bridging the extension registry and execution engine.
 *  @{
 */
/* ------------------------------------------------------------------ */

/**
 * @brief Metadata describing a grammar extension participant in the
 *        execution pipeline.
 *
 * Bridges the extension registry and the execution policy engine,
 * associating an extension ID with a parser instance and optional
 * execution callbacks.
 */
typedef struct GrammarExtensionMetadata {
    uint32_t extension_id; /**< Extension ID from the registry */
    const char *name;      /**< Extension name (borrowed pointer) */
    uint32_t priority;     /**< Higher = more preferred */
    void *user_data;       /**< Extension-specific data */

    /**
     * @brief Optional callback for EXEC_CONDITIONAL policy.
     *
     * Returns true if this extension's semantic actions should execute
     * given the current disambiguation result.
     *
     * @param self            This metadata struct.
     * @param strategy_result The full StrategyResult from disambiguation.
     * @retval true  Execute this extension's actions.
     * @retval false Skip this extension.
     *
     * @note May be NULL (treated as "always execute").
     */
    bool (*should_execute)(const struct GrammarExtensionMetadata *self,
                           const struct StrategyResult *strategy_result);
} GrammarExtensionMetadata;

/** @} */ /* end exec_ext_meta */

/* ------------------------------------------------------------------ */
/** @defgroup exec_policy_enum Execution Policy Enum
 *  @brief Available execution policies.
 *  @{
 */
/* ------------------------------------------------------------------ */

/**
 * @brief Available execution policies.
 *
 * Note: this is the runtime-side execution mode.  The user-facing
 * extension metadata in extension_registry.h uses a separate enum
 * (ExecutionPolicy with EXEC_SEQUENTIAL/PARALLEL/... values) that
 * describes scheduling preferences rather than the actual dispatch
 * mode.
 */
typedef enum LimeExecMode {
    EXEC_FIRST_ONLY = 0, /**< Only highest-priority winner executes */
    EXEC_ALL,            /**< All winners execute independently */
    EXEC_CHAIN,          /**< Winners execute in sequence, output chained */
    EXEC_CONDITIONAL,    /**< Extension-provided callback decides */
} LimeExecMode;

/** @} */ /* end exec_policy_enum */

/* ------------------------------------------------------------------ */
/** @defgroup exec_result Execution Result
 *  @brief Outcome of executing a single grammar's semantic actions.
 *  @{
 */
/* ------------------------------------------------------------------ */

/**
 * @brief Result of executing a single grammar's semantic actions.
 */
typedef struct ExecutionResult {
    uint32_t extension_id; /**< Which extension produced this result */
    void *result;          /**< Semantic action output (owned by caller) */
    char *error;           /**< NULL on success; malloc'd error on failure */
} ExecutionResult;

/** @} */ /* end exec_result */

/* ------------------------------------------------------------------ */
/** @defgroup exec_parser_handle Parser Handle
 *  @brief Opaque parser handle for the execution engine.
 *  @{
 */
/* ------------------------------------------------------------------ */

/**
 * @brief Opaque parser handle for the execution engine.
 *
 * In practice these are generated parser instances (yyParser from
 * limpar.c), but the policy engine does not depend on their internal
 * structure.
 */
typedef struct LimeParserHandle LimeParserHandle;

/** @} */ /* end exec_parser_handle */

/* ------------------------------------------------------------------ */
/** @defgroup exec_callback Parser Execution Callback
 *  @brief Callback type for running a parser's semantic actions.
 *  @{
 */
/* ------------------------------------------------------------------ */

/**
 * @brief Callback type for executing a parser's semantic actions.
 *
 * The execution policy engine calls this to run each winner's parser.
 *
 * @param parser  Opaque parser handle.
 * @param ext     Metadata for the grammar extension being executed.
 * @param input   Input data for chaining (NULL for non-chained execution).
 * @param[out] result On success, receives the semantic action output.
 * @param[out] error  On failure, receives a malloc'd error message.
 * @retval true  Execution succeeded.
 * @retval false Execution failed; see @p error.
 */
typedef bool (*ParserExecuteFn)(LimeParserHandle *parser, const GrammarExtensionMetadata *ext,
                                void *input, void **result, char **error);

/** @} */ /* end exec_callback */

/* ------------------------------------------------------------------ */
/** @defgroup exec_config Execution Policy Configuration
 *  @brief Configuration struct for the execution engine.
 *  @{
 */
/* ------------------------------------------------------------------ */

/**
 * @brief Configuration for the execution policy engine.
 */
typedef struct ExecutionPolicyConfig {
    LimeExecMode policy;        /**< Which policy to use */
    ParserExecuteFn execute_fn; /**< Callback to run a parser */

    /**
     * @brief If true, execution stops at the first error.
     *
     * If false, errors are collected and all parsers run.
     * Only meaningful for EXEC_ALL, EXEC_CHAIN, and EXEC_CONDITIONAL.
     */
    bool stop_on_error;

    /**
     * @brief Maximum number of parsers to execute.
     *
     * 0 means unlimited.  Useful as a safety limit for EXEC_ALL.
     */
    int max_executions;
} ExecutionPolicyConfig;

/** @} */ /* end exec_config */

/* ------------------------------------------------------------------ */
/** @defgroup exec_api Execution Policy API
 *  @brief Execute semantic actions according to a configured policy.
 *  @{
 */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize default execution policy configuration.
 *
 * Sets policy to EXEC_FIRST_ONLY with stop_on_error=true.
 *
 * @param config Configuration struct to initialize.
 */
void execution_policy_config_init(ExecutionPolicyConfig *config);

/**
 * @brief Execute semantic actions according to the configured policy.
 *
 * The StrategyResult (from disambiguation.h) provides the list of
 * winning contexts.  The @p parsers and @p extensions arrays are
 * parallel to the winning_contexts array.
 *
 * @param config        Execution policy configuration.
 * @param disambiguation Result from disambiguation strategy.
 * @param parsers       Array of parser handles, one per winner.
 * @param extensions    Array of extension metadata, one per winner.
 * @param[out] nresults_out Receives the number of results.
 * @return malloc'd array of ExecutionResult structs.  Caller must free
 *         using execution_results_free().  Returns NULL on allocation
 *         failure (nresults_out set to 0).
 *
 * @see execution_results_free()
 */
ExecutionResult *execute_semantic_actions(const ExecutionPolicyConfig *config,
                                          const struct StrategyResult *disambiguation,
                                          LimeParserHandle **parsers,
                                          GrammarExtensionMetadata **extensions, int *nresults_out);

/**
 * @brief Free an array of execution results.
 *
 * Frees each result's error string and the array itself.
 *
 * @param results Array returned by execute_semantic_actions().
 *                Passing NULL is safe.
 * @param nresults Number of entries in the array.
 *
 * @note The result->result pointers are NOT freed -- ownership of
 *       semantic action outputs belongs to the caller.
 */
void execution_results_free(ExecutionResult *results, int nresults);

/**
 * @brief Return a human-readable name for an execution policy.
 *
 * @param policy Policy to name.
 * @return Static string like "first_only", "all", "chain", etc.
 */
const char *execution_policy_name(LimeExecMode policy);

/**
 * @brief Convenience: execute with EXEC_FIRST_ONLY policy.
 *
 * @param execute_fn     Callback to run the parser.
 * @param disambiguation Result from disambiguation strategy.
 * @param parsers        Array of parser handles.
 * @param extensions     Array of extension metadata.
 * @param[out] nresults_out Receives 1 on success, 0 on failure.
 * @return Single-element result array, or NULL on failure.
 *
 * @see execute_semantic_actions()
 */
ExecutionResult *execute_first_only(ParserExecuteFn execute_fn,
                                    const struct StrategyResult *disambiguation,
                                    LimeParserHandle **parsers,
                                    GrammarExtensionMetadata **extensions, int *nresults_out);

/** @} */ /* end exec_api */

#ifdef __cplusplus
}
#endif

#endif /* EXECUTION_POLICY_H */
