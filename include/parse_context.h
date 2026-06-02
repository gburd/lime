/*
** Parse context - manages parser state for a single parse session
**
** The parse context pins a snapshot for the duration of parsing,
** ensuring grammar stability even if extensions are modified.
*/
#ifndef PARSE_CONTEXT_H
#define PARSE_CONTEXT_H

#include "snapshot.h"
#include <stdbool.h>

/* Forward declaration (full definition in parser.h) */
typedef struct ParseContext ParseContext;

/**
 * @brief Per-parse-session state.
 *
 * Pins a snapshot for the duration of a parse, ensuring grammar
 * stability even if extensions are loaded or unloaded by other
 * threads while parsing is in flight.
 *
 * The `engine` field is owned by parse_engine.c (opaque to callers).
 * It holds the parse stack and the accept/error flags.  Embedding it
 * directly in ParseContext rather than in a side-table keyed by
 * `ParseContext *` is load-bearing for parse_token throughput: a
 * side-table lookup adds a pthread_mutex_lock + linear scan to every
 * parse_token call, which makes the runtime push parser
 * unnecessarily slower than its compiled-in counterpart in
 * bench/bench_flex_bison_compare.  Do not move it back out without
 * rerunning that benchmark.
 */
struct ParseContext {
    ParserSnapshot *snapshot;  /**< Snapshot pinned for this parse session */
    void *engine;              /**< Owned by parse_engine.c (opaque) */
    /**
     * Optional grammar-mode boundary detector.  When non-NULL, the
     * parse engine consults it on every token via the
     * context_switch.c API to spot embedded-grammar boundaries.  When
     * NULL (the common single-grammar case) the hook is a single
     * branch on the parse-engine hot path.
     *
     * Borrowed -- not owned by the ParseContext.  The caller owns
     * the stack and is responsible for destroying it after the
     * ParseContext.  Attach via parse_attach_context_stack().
     */
    struct GrammarContextStack *context_stack;

    /**
     * When true, this ParseContext was created via
     * parse_begin_borrowed() and the caller guarantees the snapshot
     * outlives the parse session.  parse_end will NOT call
     * snapshot_release on the snapshot pointer (which therefore was
     * never atomic_fetch_add'd in parse_begin_borrowed either).
     *
     * The flag survives the thread-local pool: parse_context_destroy
     * resets it to false before pooling.  parse_begin_borrowed sets it
     * to true after retrieving the pooled context.
     */
    bool borrowed_snapshot;
};

/*
** Create a parse context with the given snapshot.
** Acquires a reference to the snapshot.
*/
ParseContext *parse_context_create(ParserSnapshot *snap);

/*
** Borrowed-snapshot variant: same as parse_context_create except
** snap->refcount is NOT touched.  Caller guarantees snap outlives
** the returned ParseContext.  See parse_begin_borrowed() in the
** parser.h-style API section below.
*/
ParseContext *parse_context_create_borrowed(ParserSnapshot *snap);

/*
** Destroy a parse context, releasing the snapshot.
*/
void parse_context_destroy(ParseContext *ctx);

/*
** Drop the calling thread's pooled ParseContext (if any).
**
** Lime maintains a thread-local single-slot pool of recycled
** ParseContexts so that parse_begin / parse_end can avoid
** malloc/free + stack alloc/destroy on every parse.  The pool
** memory is reclaimed at process exit by default; this function
** is for callers who want to drop the cached context explicitly
** before joining a parser thread (test harnesses, leak hunters).
**
** Safe to call from a thread that has never called parse_begin
** (no-op).  Not thread-safe across threads -- each thread drains
** its own slot.
*/
void parse_context_pool_drain(void);

/* ------------------------------------------------------------------ */
/*  parser.h wrappers                                                  */
/* ------------------------------------------------------------------ */

ParseContext *parse_begin(ParserSnapshot *snap);
void parse_end(ParseContext *ctx);
ParserSnapshot *parse_get_snapshot(ParseContext *ctx);

/**
 * @brief Begin a parse with a BORROWED snapshot reference.
 *
 * Same as parse_begin() except the snapshot's atomic refcount is NOT
 * touched.  The caller MUST guarantee that *snap* lives for at least
 * as long as the returned ParseContext is in use (until parse_end
 * returns).  Typical use: a long-running parser server holds the
 * snapshot for hours while worker threads borrow it for milliseconds.
 *
 * Why this exists
 * ---------------
 * parse_begin / parse_end issue an atomic_fetch_add / fetch_sub on
 * snap->refcount.  At high concurrency these LOCK-prefixed RMW ops
 * serialise through the L3 cache coherence directory; on
 * bench/bench_parse_fanout we measure ~22%% scaling efficiency at 8
 * threads under the refcount-acquiring API.  Skipping the atomics
 * entirely is the only way to close that gap (cacheline-aligning the
 * field has no measurable effect; see
 * .agent/notes/perf-experiments-negative.md).
 *
 * Safety
 * ------
 * If the caller releases the snapshot while a borrowed parse is in
 * flight, parse_token will read freed memory.  Use this API only
 * when the snapshot's lifetime is statically known to dominate the
 * parse session's.  When in doubt, use parse_begin().
 *
 * @param snap  Snapshot to borrow.  Must NOT be NULL.
 * @return New ParseContext, or NULL on OOM.  Pass to parse_end().
 */
ParseContext *parse_begin_borrowed(ParserSnapshot *snap);

/*
** Feed a token to the push parser.
**
**   ctx         Active parse context.
**   token_code  Token code (0 for end-of-input).
**   token_value Semantic value (may be NULL).
**   location    Byte offset of the token in the source, or
**               LIME_LOC_UNKNOWN if the grammar does not declare
**               %locations or the caller does not track positions.
**
** Returns 0 on success, non-zero on parse error.
*/
int parse_token(ParseContext *ctx, int token_code, void *token_value, int location);

/*
** Sentinel value for `location` callers who do not track positions
** (or who cannot attribute a position to a given token, e.g. an
** injected end-of-input marker).  Guaranteed to be -1 so that
** existing code that passed an integer offset happens to be
** forward-compatible when offsets are always >= 0.
*/
#define LIME_LOC_UNKNOWN (-1)

/* ------------------------------------------------------------------ */
/*  Grammar-context binding (optional)                                 */
/* ------------------------------------------------------------------ */

/* Forward declaration; full type lives in include/grammar_context.h. */
typedef struct GrammarContextStack GrammarContextStack;

/**
 * @brief Attach a grammar-mode boundary detector to a parse context.
 *
 * When attached, the parse engine consults the stack via
 * context_switch_needed() / context_switch_detect_exit() on each
 * token; matching triggers swap @p ctx->snapshot to the embedded
 * grammar's snapshot for the duration of the embedded region.
 *
 * Pass NULL for @p stack to detach.  The stack is borrowed -- the
 * caller retains ownership and must destroy it after the
 * ParseContext.
 *
 * @param ctx   Parse context to bind.
 * @param stack Boundary detector, or NULL to detach.
 */
void parse_attach_context_stack(ParseContext *ctx, GrammarContextStack *stack);

/**
 * @brief Feed a token plus its lexeme.
 *
 * Behaves like parse_token() except that the supplied lexeme is also
 * fed through the attached GrammarContextStack (if any) for
 * trigger-lexeme matching.  Pass NULL for @p lexeme when no lexeme
 * text is available -- only token-code-based triggers will fire.
 *
 * Callers that haven't attached a context stack should keep using
 * parse_token() directly; this entry point is identical in cost
 * apart from the extra lexeme parameter on the no-stack fast path.
 */
int parse_token_lex(ParseContext *ctx, int token_code, void *token_value, const char *lexeme,
                    int location);

/* ------------------------------------------------------------------ */
/*  Snapshot action table lookup helpers                               */
/* ------------------------------------------------------------------ */

uint16_t snap_find_shift_action(const ParserSnapshot *snap, uint16_t stateno, uint16_t iLookAhead);
uint16_t snap_find_reduce_action(const ParserSnapshot *snap, uint16_t stateno, uint16_t iLookAhead);

#endif /* PARSE_CONTEXT_H */
