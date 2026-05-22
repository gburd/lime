/**
 * @file parser_fork.h
 * @brief Parser State Cloning for Fork-Resolve Disambiguation.
 *
 * Provides deep-copy mechanisms for parser state, enabling the
 * fork-resolve disambiguation strategy.  When the parser encounters
 * an ambiguity (e.g., multiple grammars could handle the current input),
 * the parser state is forked: each fork receives an independent copy of
 * the parser stack and continues parsing with a different grammar.
 * After all forks complete (or fail), the results are compared and the
 * best parse is selected.
 *
 * The yyParser struct (defined in limpar.c) contains:
 *   - yytos:      pointer to top of stack
 *   - yyerrcnt:   error recovery counter
 *   - yystack:    the parser stack (may be heap-allocated if grown)
 *   - yystk0:     initial inline stack storage
 *   - yystackEnd: pointer to last valid stack slot
 *   - extra_argument / extra_context: user-supplied data
 *
 * Cloning handles:
 *   1. Deep copy of the stack entries (state + semantic values)
 *   2. Correct pointer fixup (yytos, yystackEnd relative to new stack)
 *   3. Shared immutable tables are reference-counted via snapshots
 *   4. The inline yystk0[] buffer vs heap-allocated yystack distinction
 *
 * @par Thread Safety
 * ParseFork structs are not thread-safe.  Each fork should be used by
 * a single thread (or externally synchronized).  The underlying
 * ParserSnapshot is thread-safe (refcounted).
 *
 * @see disambiguation.h for the fork-resolve strategy.
 * @see snapshot.h for ParserSnapshot reference counting.
 */
#ifndef PARSER_FORK_H
#define PARSER_FORK_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct ParserSnapshot;

/* ================================================================== */
/** @defgroup fork_status Fork Status
 *  @brief Status tracking for forked parse attempts.
 *  @{
 */
/* ================================================================== */

/**
 * @brief Status of a forked parse attempt.
 */
typedef enum ParseForkStatus {
    FORK_PENDING = 0, /**< Fork created, not yet started/completed */
    FORK_RUNNING,     /**< Fork is actively consuming tokens */
    FORK_COMPLETED,   /**< Fork reached accept state */
    FORK_FAILED,      /**< Fork encountered an unrecoverable error */
    FORK_ABANDONED,   /**< Fork was pruned (lower priority or timeout) */
} ParseForkStatus;

/** @} */ /* end fork_status */

/* ================================================================== */
/** @defgroup cloned_state Cloned Parser State
 *  @brief Deep copy of a grammar-specific parser state.
 *  @{
 */
/* ================================================================== */

/**
 * @brief Deep copy of a grammar-specific parser state.
 *
 * The parser state internals are grammar-specific (the yyParser struct
 * is generated per-grammar), so the cloned state is stored as a raw
 * byte buffer plus metadata about its layout.
 */
typedef struct ClonedParserState {
    void *state_data;  /**< Deep copy of the yyParser struct (malloc'd) */
    size_t state_size; /**< sizeof(yyParser) for this grammar */

    void *stack_data;        /**< Separately allocated stack (if stack was on heap) */
    size_t stack_size;       /**< Number of bytes in the cloned stack */
    uint32_t stack_depth;    /**< Number of entries currently on the stack */
    uint32_t stack_capacity; /**< Total entry slots in the cloned stack */

    bool stack_is_inline; /**< True if stack lives inside state_data (yystk0) */
} ClonedParserState;

/** @} */ /* end cloned_state */

/* ================================================================== */
/** @defgroup parse_fork ParseFork
 *  @brief A forked parser instance for disambiguation.
 *  @{
 */
/* ================================================================== */

/**
 * @brief A forked parser instance.
 *
 * Created by fork_parser(), each fork holds an independent copy of the
 * parser state and a reference to the grammar snapshot it should parse
 * with.
 */
typedef struct ParseFork {
    ClonedParserState cloned_state; /**< Deep copy of parser state */

    struct ParserSnapshot *snapshot; /**< Grammar snapshot (ref acquired) */

    /**
     * @brief Fork priority.
     *
     * Lower value = higher priority.  When two forks both succeed,
     * the one with the lower priority number wins.
     */
    int priority;

    ParseForkStatus status; /**< Current status of this fork */

    /**
     * @brief Result produced by semantic actions.
     *
     * Interpretation is grammar-specific.  Owned by the fork; freed
     * on destroy.
     */
    void *semantic_result;

    /**
     * @brief Optional destructor for semantic_result.
     *
     * If NULL, free() is used.  Set this when the result requires
     * custom cleanup.
     */
    void (*free_result)(void *);

    uint32_t tokens_consumed; /**< Number of tokens fed to this fork */
    uint32_t error_count;     /**< Cumulative error count */

    uint64_t fork_id; /**< Unique identifier for tracing/debugging */
} ParseFork;

/** @} */ /* end parse_fork */

/* ================================================================== */
/** @defgroup fork_id Fork ID Generation
 *  @brief Unique identifier generation for fork tracing.
 *  @{
 */
/* ================================================================== */

/**
 * @brief Return a globally unique fork ID.
 *
 * IDs are monotonically increasing within a process.
 *
 * @return Unique fork identifier.
 *
 * @thread_safe This function is thread-safe.
 */
uint64_t parser_fork_next_id(void);

/** @} */ /* end fork_id */

/* ================================================================== */
/** @defgroup clone_api Cloning API
 *  @brief Deep-copy parser state for forking.
 *  @{
 */
/* ================================================================== */

/**
 * @brief Clone the parser state from an opaque parser handle.
 *
 * The caller must provide correct layout parameters derived from the
 * generated parser's compile-time constants.  Use the
 * PARSER_FORK_LAYOUT_PARAMS() macro for convenience.
 *
 * @param parser              Pointer to a yyParser (cast to void*).
 * @param parser_size         sizeof(yyParser) for this grammar.
 * @param stack_entry_size    sizeof(yyStackEntry) for this grammar.
 * @param inline_stack_offset offsetof(yyParser, yystk0).
 * @param inline_stack_count  YYSTACKDEPTH (number of inline stack entries).
 * @param stack_field_offset  offsetof(yyParser, yystack).
 * @param tos_field_offset    offsetof(yyParser, yytos).
 * @param stack_end_offset    offsetof(yyParser, yystackEnd).
 * @param[out] out            On success, filled with the cloned state.
 * @retval true  Cloning succeeded.
 * @retval false Allocation failure.
 *
 * @see PARSER_FORK_LAYOUT_PARAMS()
 * @see cloned_parser_state_destroy()
 */
bool clone_parser_state(const void *parser, size_t parser_size, size_t stack_entry_size,
                        size_t inline_stack_offset, uint32_t inline_stack_count,
                        size_t stack_field_offset, size_t tos_field_offset, size_t stack_end_offset,
                        ClonedParserState *out);

/**
 * @brief Free a cloned parser state.
 *
 * After this call, the ClonedParserState is zeroed and must not be used.
 *
 * @param cloned State to destroy.
 */
void cloned_parser_state_destroy(ClonedParserState *cloned);

/** @} */ /* end clone_api */

/* ================================================================== */
/** @defgroup fork_lifecycle Fork Lifecycle
 *  @brief Create and destroy forked parser instances.
 *  @{
 */
/* ================================================================== */

/**
 * @brief Create a new fork from a base parser state.
 *
 * Clones the parser state, acquires a reference to the snapshot, and
 * returns a heap-allocated ParseFork ready for token feeding.
 *
 * @param parser              Pointer to the base yyParser (void*).
 * @param parser_size         sizeof(yyParser).
 * @param stack_entry_size    sizeof(yyStackEntry).
 * @param inline_stack_offset offsetof(yyParser, yystk0).
 * @param inline_stack_count  YYSTACKDEPTH.
 * @param stack_field_offset  offsetof(yyParser, yystack).
 * @param tos_field_offset    offsetof(yyParser, yytos).
 * @param stack_end_offset    offsetof(yyParser, yystackEnd).
 * @param snapshot            Grammar snapshot for this fork.
 *                            A reference is acquired (refcount incremented).
 * @param priority            Priority for disambiguation (lower = preferred).
 * @return Heap-allocated ParseFork, or NULL on failure.
 *
 * @see free_parse_fork()
 * @see PARSER_FORK_LAYOUT_PARAMS()
 */
ParseFork *fork_parser(const void *parser, size_t parser_size, size_t stack_entry_size,
                       size_t inline_stack_offset, uint32_t inline_stack_count,
                       size_t stack_field_offset, size_t tos_field_offset, size_t stack_end_offset,
                       struct ParserSnapshot *snapshot, int priority);

/**
 * @brief Destroy a fork and release all resources.
 *
 * - The cloned parser state is freed.
 * - The snapshot reference is released.
 * - The semantic_result is freed via free_result (or free()).
 * - The ParseFork struct itself is freed.
 *
 * @param fork Fork to destroy.  Passing NULL is safe.
 */
void free_parse_fork(ParseFork *fork);

/** @} */ /* end fork_lifecycle */

/* ================================================================== */
/** @defgroup fork_access Fork State Access
 *  @brief Access the cloned parser state within a fork.
 *  @{
 */
/* ================================================================== */

/**
 * @brief Get a mutable pointer to the cloned yyParser inside a fork.
 *
 * The returned pointer is valid until free_parse_fork() is called.
 *
 * @param fork The fork to access.
 * @return Pointer to the cloned yyParser, or NULL if @p fork is NULL
 *         or has no cloned state.
 */
void *parse_fork_get_parser(ParseFork *fork);

/**
 * @brief Get the snapshot associated with a fork.
 *
 * @param fork The fork to query.
 * @return Snapshot pointer, valid for the fork's lifetime.
 */
struct ParserSnapshot *parse_fork_get_snapshot(const ParseFork *fork);

/**
 * @brief Mark a fork as completed with the given semantic result.
 *
 * Sets status to FORK_COMPLETED and stores the result pointer.
 *
 * @param fork    The fork to complete.
 * @param result  Semantic action output.
 * @param free_fn Destructor for @p result (NULL to use free()).
 */
void parse_fork_complete(ParseFork *fork, void *result, void (*free_fn)(void *));

/**
 * @brief Mark a fork as failed.
 *
 * @param fork The fork that failed.
 */
void parse_fork_fail(ParseFork *fork);

/**
 * @brief Mark a fork as abandoned (pruned).
 *
 * @param fork The fork to abandon.
 */
void parse_fork_abandon(ParseFork *fork);

/** @} */ /* end fork_access */

/* ================================================================== */
/** @defgroup fork_set Fork Set Management
 *  @brief Manage a collection of active forks for parallel evaluation.
 *  @{
 */
/* ================================================================== */

/**
 * @brief A set of active forks being evaluated in parallel.
 *
 * Used by the fork-resolve disambiguation strategy.
 */
typedef struct ParseForkSet {
    ParseFork **forks;  /**< Dynamic array of fork pointers */
    uint32_t count;     /**< Number of forks in the set */
    uint32_t capacity;  /**< Allocated slots */
    uint32_t max_forks; /**< Upper bound (0 = unlimited) */
} ParseForkSet;

/**
 * @brief Create a fork set with the given maximum fork count.
 *
 * @param max_forks Maximum number of forks allowed (0 = unlimited).
 * @return New fork set, or NULL on allocation failure.
 *
 * @see parse_fork_set_destroy()
 */
ParseForkSet *parse_fork_set_create(uint32_t max_forks);

/**
 * @brief Destroy a fork set and all forks it contains.
 *
 * @param set Fork set to destroy.  Passing NULL is safe.
 */
void parse_fork_set_destroy(ParseForkSet *set);

/**
 * @brief Add a fork to the set.
 *
 * The set takes ownership of the fork.
 *
 * @param set  Fork set to add to.
 * @param fork Fork to add.
 * @retval true  Fork was added.
 * @retval false Set is at capacity.
 */
bool parse_fork_set_add(ParseForkSet *set, ParseFork *fork);

/**
 * @brief Remove and destroy all failed or abandoned forks.
 *
 * Removes forks with FORK_FAILED or FORK_ABANDONED status.
 *
 * @param set Fork set to prune.
 * @return Number of forks pruned.
 */
uint32_t parse_fork_set_prune(ParseForkSet *set);

/**
 * @brief Find the best completed fork.
 *
 * Returns the fork with the lowest priority among those with
 * FORK_COMPLETED status.
 *
 * @param set Fork set to search.
 * @return Best completed fork, or NULL if no fork has completed.
 */
ParseFork *parse_fork_set_best(const ParseForkSet *set);

/**
 * @brief Return the number of forks still active.
 *
 * Counts forks in FORK_PENDING or FORK_RUNNING state.
 *
 * @param set Fork set to query.
 * @return Number of active forks.
 */
uint32_t parse_fork_set_active_count(const ParseForkSet *set);

/** @} */ /* end fork_set */

/* ================================================================== */
/** @defgroup fork_macro Convenience Macro for Generated Parsers
 *  @brief Helper macro to produce layout parameters for cloning.
 *  @{
 */
/* ================================================================== */

/**
 * @brief Produce the layout parameters expected by clone_parser_state()
 *        and fork_parser().
 *
 * Use inside generated parser code (or user code that has visibility
 * into yyParser and yyStackEntry).
 *
 * @param ParserType     The generated yyParser type.
 * @param StackEntryType The generated yyStackEntry type.
 *
 * @par Example
 * @code
 *   ParseFork *f = fork_parser(myParser,
 *                              PARSER_FORK_LAYOUT_PARAMS(yyParser,
 *                                                        yyStackEntry),
 *                              snapshot, priority);
 * @endcode
 */
#define PARSER_FORK_LAYOUT_PARAMS(ParserType, StackEntryType)                                      \
    sizeof(ParserType), sizeof(StackEntryType), offsetof(ParserType, yystk0),                      \
        (uint32_t)(sizeof(((ParserType *)0)->yystk0) / sizeof(StackEntryType)),                    \
        offsetof(ParserType, yystack), offsetof(ParserType, yytos),                                \
        offsetof(ParserType, yystackEnd)

/** @} */ /* end fork_macro */

#ifdef __cplusplus
}
#endif

#endif /* PARSER_FORK_H */
