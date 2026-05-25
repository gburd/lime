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

/* ------------------------------------------------------------------ */
/** @defgroup ctx_switch Context-switch trigger registry
 *  @brief Runtime-registered grammar-mode boundary detection.
 *
 *  This is the layer above @ref grammar_context_register_mode that
 *  handles the boilerplate of building a GrammarModeInfo and assigning
 *  a fresh mode id for each runtime-registered embedded grammar.
 *
 *  The earlier (1506723^) version of this module had four hard-coded
 *  trigger lexemes (`xmlquery`, `xpath`, `{:`, `json`) baked into
 *  static const strings.  Real SQL doesn't say `xmlquery(...)` and
 *  PostgreSQL uses `XMLPARSE(DOCUMENT ...)`, so triggers are now
 *  registered at runtime by each host grammar.
 *
 *  @{
 */
/* ------------------------------------------------------------------ */

/**
 * @brief Sentinel returned by context_switch_classify_lexeme() when no
 *        registered trigger matches the lexeme.
 *
 * The sentinel maps to MODE_SQL (== 0); callers should compare against
 * this constant rather than against MODE_SQL directly to keep the
 * "no match" intent explicit at call sites.
 */
#define MODE_NONE ((GrammarMode)MODE_SQL)

/**
 * @brief Register a runtime trigger that switches into an embedded grammar.
 *
 * When the host grammar's lexer sees @p trigger_lexeme as a prefix of
 * its current lexeme, the parser switches into the embedded grammar
 * identified by @p embedded_snap.  The mode is assigned a fresh
 * GrammarMode id internally; the assignment is returned via the
 * `mode` field of the GrammarModeInfo registered against
 * @p stack.
 *
 * The trigger is bracket-depth-driven: the embedded mode exits when
 * the bracket depth returns to where it was at entry.  Callers that
 * need explicit-token exit semantics should use
 * grammar_context_register_mode() directly.
 *
 * @param stack          Context stack to register with.
 * @param trigger_lexeme Lexeme prefix that triggers the switch (borrowed,
 *                       must outlive @p stack).
 * @param embedded_snap  Snapshot for the embedded grammar.  Stack
 *                       acquires a reference.
 * @param mode_name      Human-readable name (borrowed, must outlive
 *                       @p stack); appears in callbacks.
 * @retval true  Trigger registered.
 * @retval false NULL inputs, registry is full, or duplicate trigger.
 */
bool context_switch_register_trigger(GrammarContextStack *stack,
                                     const char *trigger_lexeme,
                                     ParserSnapshot *embedded_snap,
                                     const char *mode_name);

/**
 * @brief Classify a lexeme against the registered triggers.
 *
 * Returns the GrammarMode of a matching trigger, or @ref MODE_NONE if
 * no trigger matches.  The classification is intentionally a prefix
 * test: a host-grammar lexer that emits multi-character keywords as
 * single lexemes will still be matched against shorter trigger
 * prefixes (for example, `json` matching against the lexeme `json`).
 *
 * @param stack   Context stack with the trigger registry.
 * @param lexeme  Lexeme text (may be NULL).
 * @return Mode the lexeme would trigger, or @ref MODE_NONE.
 */
GrammarMode context_switch_classify_lexeme(const GrammarContextStack *stack, const char *lexeme);

/**
 * @brief Fast-path predicate: could the current token trigger a switch?
 *
 * When no triggers are registered or the lexeme is empty, this returns
 * false in O(1) so the host parser pays a single load + branch on the
 * fast path.  When triggers are registered, callers can short-circuit
 * the heavier classify_lexeme / detect_switch path with this check.
 *
 * Inside an embedded context, this always returns true so callers will
 * still poll for the exit condition via context_switch_detect_exit().
 *
 * @param stack       Context stack with the trigger registry.
 * @param token_code  Token code just emitted by the host lexer.
 * @param lexeme      Lexeme text (may be NULL).
 * @retval true   A switch or exit may be possible.
 * @retval false  Definitively no switch needed.
 */
bool context_switch_needed(const GrammarContextStack *stack, int token_code, const char *lexeme);

/**
 * @brief Detect whether the current embedded context should exit.
 *
 * The original (1506723^) version of this function looked at the
 * lexeme to spot the EDN `:}` close marker.  The runtime-registered
 * design instead delegates exit detection to either an explicit exit
 * token (registered via GrammarModeInfo.exit_token) or to bracket
 * depth tracking.  This function is a thin wrapper for the explicit
 * exit-token case.
 *
 * @param stack       Context stack with an active embedded context.
 * @param token_code  Token code just emitted by the host lexer.
 * @param lexeme      Lexeme text (may be NULL; reserved for future
 *                    lexeme-based exit detection).
 * @retval true   Context was popped; previous mode is now active.
 * @retval false  No exit detected.
 */
bool context_switch_detect_exit(GrammarContextStack *stack, int token_code, const char *lexeme);

/** @} */ /* end ctx_switch */

/* ------------------------------------------------------------------ */
/** @defgroup grammar_ctx_introspect Context-stack introspection
 *  @brief Read-only accessors over the registered-mode table.
 *
 *  These are used by context_switch.c so it can implement
 *  classify_lexeme / register_trigger without re-deriving the
 *  GrammarContextStack layout (which is otherwise opaque).  Keep
 *  them in include/ rather than a private internal header so the
 *  symbols stay reachable across translation units in the runtime
 *  library.
 *
 *  @{
 */
/* ------------------------------------------------------------------ */

/**
 * @brief Number of registered modes.
 *
 * @param stack Context stack.
 * @return Count of registered modes (0 if @p stack is NULL).
 */
uint32_t grammar_context_mode_count(const GrammarContextStack *stack);

/**
 * @brief Read one entry from the registered-mode table.
 *
 * @param stack Context stack.
 * @param i     Index in the half-open range [0, grammar_context_mode_count(stack)).
 * @return Pointer to the entry, or NULL on out-of-range / NULL stack.
 *         The pointer is valid until the next register_mode call.
 */
const GrammarModeInfo *grammar_context_mode_at(const GrammarContextStack *stack, uint32_t i);

/** @} */ /* end grammar_ctx_introspect */

#endif /* GRAMMAR_CONTEXT_H */
