/**
 * @file grammar_context.h
 * @brief Grammar Context -- language switching for embedded grammars.
 *
 * Manages a stack of grammar contexts to support parsing inputs that
 * mix multiple languages (e.g., SQL with embedded XQuery expressions
 * or EDN literals).
 *
 * Each context represents an active grammar identified by a name and
 * backed by a ParserSnapshot.  When the parser encounters a boundary
 * token (e.g., "xmlquery(", "xpath(", "{:"), it pushes a new context
 * for the embedded language.  When the embedded region ends, the
 * context is popped and parsing resumes with the previous grammar.
 *
 * @par Supported Modes
 * | Mode         | Description                        |
 * |--------------|------------------------------------|
 * | MODE_SQL     | Standard SQL (default / root)      |
 * | MODE_XQUERY  | XQuery embedded in xmlquery(...)   |
 * | MODE_XPATH   | XPath embedded in xpath(...)       |
 * | MODE_EDN     | EDN embedded in {: ... :}          |
 * | MODE_JSON    | JSON embedded literal               |
 * | MODE_CUSTOM  | User-defined grammar mode          |
 *
 * @par Usage Example
 * @code
 *   GrammarContextStack *stack = grammar_context_create(sql_snap);
 *
 *   GrammarModeInfo xq_info = {
 *       .mode = MODE_XQUERY, .name = "xquery",
 *       .snapshot = xq_snap, .trigger_token = TK_XMLQUERY,
 *       .exit_token = -1,
 *   };
 *   grammar_context_register_mode(stack, &xq_info);
 *
 *   // During lexing:
 *   if (grammar_context_detect_switch(stack, token, lexeme, offset)) {
 *       // context was pushed, new snapshot is active
 *   }
 *
 *   grammar_context_destroy(stack);
 * @endcode
 *
 * @see parser.h for snapshot reference management.
 */
#ifndef GRAMMAR_CONTEXT_H
#define GRAMMAR_CONTEXT_H

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Forward declarations                                               */
/* ------------------------------------------------------------------ */

typedef struct ParserSnapshot ParserSnapshot;

/* ------------------------------------------------------------------ */
/** @defgroup grammar_modes Grammar Modes
 *  @brief Built-in and custom grammar mode identifiers.
 *  @{
 */
/* ------------------------------------------------------------------ */

/**
 * @brief Grammar mode identifiers.
 */
typedef enum GrammarMode {
    MODE_SQL = 0, /**< Standard SQL (root grammar) */
    MODE_XQUERY,  /**< XQuery embedded expression */
    MODE_XPATH,   /**< XPath embedded expression */
    MODE_EDN,     /**< EDN (Extensible Data Notation) */
    MODE_JSON,    /**< JSON embedded literal */
    MODE_CUSTOM,  /**< User-defined / extension grammar */
    MODE_COUNT_   /**< Sentinel (number of built-in modes) */
} GrammarMode;

/** @} */ /* end grammar_modes */

/* ------------------------------------------------------------------ */
/** @defgroup ctx_entry Context Entry
 *  @brief One level of the grammar context stack.
 *  @{
 */
/* ------------------------------------------------------------------ */

/**
 * @brief One level of the grammar context stack.
 */
typedef struct GrammarContextEntry {
    GrammarMode mode;         /**< Active grammar mode */
    const char *mode_name;    /**< Human-readable name (borrowed) */
    ParserSnapshot *snapshot; /**< Snapshot for this grammar */
    int depth;                /**< Nesting depth (for bracket matching) */
    uint32_t start_offset;    /**< Input offset where mode began */
} GrammarContextEntry;

/** @} */ /* end ctx_entry */

/* ------------------------------------------------------------------ */
/** @defgroup mode_info Mode Registration
 *  @brief Describes a grammar mode that can be entered during parsing.
 *  @{
 */
/* ------------------------------------------------------------------ */

/**
 * @brief Describes a grammar mode that can be entered during parsing.
 *
 * Registered with grammar_context_register_mode().
 */
typedef struct GrammarModeInfo {
    GrammarMode mode;         /**< Which mode this describes */
    const char *name;         /**< Human-readable name */
    ParserSnapshot *snapshot; /**< Snapshot to use (reference acquired) */

    /**
     * @brief Entry trigger: token code that initiates this mode.
     *
     * If -1, the mode is only entered explicitly via grammar_context_push().
     */
    int trigger_token;

    /**
     * @brief Entry trigger: lexeme prefix that initiates this mode.
     *
     * If NULL, only trigger_token is checked.
     */
    const char *trigger_lexeme;

    /**
     * @brief Exit trigger: token code that terminates this mode.
     *
     * -1 means the mode ends when bracket/paren depth returns to 0.
     */
    int exit_token;
} GrammarModeInfo;

/** @} */ /* end mode_info */

/* ------------------------------------------------------------------ */
/** @defgroup ctx_stack Context Stack Handle
 *  @brief Opaque grammar context stack type.
 *  @{
 */
/* ------------------------------------------------------------------ */

/** @brief Opaque grammar context stack handle. */
typedef struct GrammarContextStack GrammarContextStack;

/** @} */ /* end ctx_stack */

/* ------------------------------------------------------------------ */
/** @defgroup ctx_switch_cb Context Switch Callback
 *  @brief Notification callback for grammar context transitions.
 *  @{
 */
/* ------------------------------------------------------------------ */

/**
 * @brief Called when a context switch occurs.
 *
 * Allows the application to save/restore parser state across grammar
 * boundaries.
 *
 * @param prev_mode Mode being left.
 * @param new_mode  Mode being entered.
 * @param user_data Pointer registered with grammar_context_set_switch_callback().
 * @retval true  Allow the switch.
 * @retval false Veto the switch (context remains unchanged).
 */
typedef bool (*ContextSwitchCallback)(GrammarMode prev_mode, GrammarMode new_mode, void *user_data);

/** @} */ /* end ctx_switch_cb */

/* ------------------------------------------------------------------ */
/** @defgroup grammar_ctx_api Grammar Context API
 *  @brief Create, manage, and query the grammar context stack.
 *  @{
 */
/* ------------------------------------------------------------------ */

/**
 * @brief Create a context stack with the given root grammar snapshot.
 *
 * The root mode is MODE_SQL.  Acquires a reference to @p root_snapshot.
 *
 * @param root_snapshot Snapshot for the root (SQL) grammar.
 * @return New context stack, or NULL on allocation failure.
 *
 * @see grammar_context_destroy()
 */
GrammarContextStack *grammar_context_create(ParserSnapshot *root_snapshot);

/**
 * @brief Destroy the context stack and release all snapshot references.
 *
 * @param stack Stack to destroy.  Passing NULL is safe.
 */
void grammar_context_destroy(GrammarContextStack *stack);

/**
 * @brief Register a grammar mode.
 *
 * The stack acquires a reference to the mode's snapshot.
 *
 * @param stack Stack to register the mode with.
 * @param info  Mode descriptor.
 * @retval true  Registration succeeded.
 * @retval false Mode already registered or allocation failed.
 */
bool grammar_context_register_mode(GrammarContextStack *stack, const GrammarModeInfo *info);

/**
 * @brief Detect whether a context switch should occur.
 *
 * If a switch is detected, the appropriate context is pushed and the
 * function returns true.  The caller should then use
 * grammar_context_current_snapshot() for subsequent parsing.
 *
 * @param stack      The context stack.
 * @param token_code The token code just lexed.
 * @param lexeme     The lexeme text (may be NULL).
 * @param offset     Current offset in the input stream.
 * @retval true  A context switch occurred; new mode is active.
 * @retval false No switch; continue with the current mode.
 */
bool grammar_context_detect_switch(GrammarContextStack *stack, int token_code, const char *lexeme,
                                   uint32_t offset);

/**
 * @brief Detect whether the current embedded context should end.
 *
 * If so, the context is popped and the function returns true.  The
 * caller should switch back to grammar_context_current_snapshot().
 *
 * @param stack      The context stack.
 * @param token_code The token code just lexed.
 * @retval true  Context was popped; previous mode is now active.
 * @retval false No exit detected; continue in the current mode.
 */
bool grammar_context_detect_exit(GrammarContextStack *stack, int token_code);

/**
 * @brief Explicitly push a grammar mode onto the context stack.
 *
 * @param stack  The context stack.
 * @param mode   Grammar mode to push.
 * @param offset Current input offset.
 * @retval true  Push succeeded.
 * @retval false Mode not registered or allocation failed.
 */
bool grammar_context_push(GrammarContextStack *stack, GrammarMode mode, uint32_t offset);

/**
 * @brief Pop the current grammar context.
 *
 * @param stack The context stack.
 * @retval true  A context was popped.
 * @retval false Already at the root (nothing to pop).
 */
bool grammar_context_pop(GrammarContextStack *stack);

/**
 * @brief Return the snapshot for the current (topmost) grammar context.
 *
 * @param stack The context stack.
 * @return Current snapshot.  Valid until the next push/pop.
 */
ParserSnapshot *grammar_context_current_snapshot(const GrammarContextStack *stack);

/**
 * @brief Return the current grammar mode.
 *
 * @param stack The context stack.
 * @return Current GrammarMode value.
 */
GrammarMode grammar_context_current_mode(const GrammarContextStack *stack);

/**
 * @brief Return the current nesting depth (0 = root).
 *
 * @param stack The context stack.
 * @return Nesting depth.
 */
uint32_t grammar_context_depth(const GrammarContextStack *stack);

/**
 * @brief Register a callback to be invoked on context switches.
 *
 * @param stack     The context stack.
 * @param cb        Callback function (may be NULL to unregister).
 * @param user_data Opaque pointer forwarded to @p cb.
 */
void grammar_context_set_switch_callback(GrammarContextStack *stack, ContextSwitchCallback cb,
                                         void *user_data);

/**
 * @brief Notify the context stack that a bracket/parenthesis was opened.
 *
 * Used for depth tracking in modes that exit when their enclosing
 * brackets close.
 *
 * @param stack The context stack.
 */
void grammar_context_open_bracket(GrammarContextStack *stack);

/**
 * @brief Notify the context stack that a bracket/parenthesis was closed.
 *
 * May trigger an automatic context pop if the mode's depth returns
 * to its entry depth.
 *
 * @param stack The context stack.
 * @retval true  A context pop was triggered by bracket close.
 * @retval false No pop occurred.
 */
bool grammar_context_close_bracket(GrammarContextStack *stack);

/**
 * @brief Check if only the root grammar is active.
 *
 * This is the fast-path check: if true, the parser can skip all
 * mode-detection logic.
 *
 * @param stack The context stack.
 * @retval true  Only the root grammar is active (no embedded contexts).
 * @retval false One or more embedded contexts are active.
 */
bool grammar_context_is_root_only(const GrammarContextStack *stack);

/** @} */ /* end grammar_ctx_api */

#endif /* GRAMMAR_CONTEXT_H */
