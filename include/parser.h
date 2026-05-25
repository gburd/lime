/**
 * @file parser.h
 * @brief Core public API for the Lime Parser library.
 *
 * This is the main header for consumers of the Lime runtime library.
 * It provides:
 *   - Snapshot lifecycle (create, acquire, release)
 *   - Parse session management (begin, token, end)
 *   - JIT compilation control
 *   - Extension registry initialization
 *
 * @par Basic Usage
 * @code
 *   // Create a snapshot from a grammar file
 *   char *error = NULL;
 *   ParserSnapshot *snap = lime_snapshot_create("sql.y", &error);
 *   if (!snap) { fprintf(stderr, "%s\n", error); free(error); return 1; }
 *
 *   // Begin a parse session
 *   ParseContext *ctx = parse_begin(snap);
 *
 *   // Feed tokens
 *   parse_token(ctx, TK_SELECT, NULL, LIME_LOC_UNKNOWN);
 *   parse_token(ctx, TK_STAR,   NULL, LIME_LOC_UNKNOWN);
 *   parse_token(ctx, 0,         NULL, LIME_LOC_UNKNOWN);  // end-of-input
 *
 *   // End the session
 *   parse_end(ctx);
 *   lime_snapshot_release(snap);
 * @endcode
 *
 * @see parser_manager.h for plugin-based parser management.
 * @see snapshot.h for the full ParserSnapshot definition.
 */
#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Return the Lime parser library version string.
 *
 * @return Static NUL-terminated version string (e.g. "0.1.0").
 */
const char *lime_parser_version(void);

/* --- Snapshot API -------------------------------------------------- */

/** @defgroup snapshot_api Snapshot API
 *  @brief Create, share, and release parser snapshots.
 *  @{
 */

/**
 * @brief Opaque snapshot handle.
 *
 * See src/snapshot.h for the full definition.
 */
typedef struct ParserSnapshot ParserSnapshot;

/**
 * @brief Acquire a reference to a snapshot.
 *
 * The caller must eventually call lime_snapshot_release() to avoid
 * leaking memory.
 *
 * @param snap Snapshot to acquire.  Passing NULL is safe and returns NULL.
 * @return Same pointer as @p snap, for convenience.
 *
 * @see lime_snapshot_release()
 */
ParserSnapshot *lime_snapshot_acquire(ParserSnapshot *snap);

/**
 * @brief Release a snapshot reference.
 *
 * When the last reference is released the snapshot and all memory it
 * owns are freed.
 *
 * @param snap Snapshot to release.  Passing NULL is safe.
 */
void lime_snapshot_release(ParserSnapshot *snap);

/**
 * @brief Create a base snapshot by parsing a grammar file.
 *
 * Runs the Lime parser generator on @p grammar_file and produces a
 * snapshot containing the compiled action tables.
 *
 * @param grammar_file Path to the grammar file.
 * @param[out] error   On failure, set to a malloc'd message the caller
 *                     must free.  Set to NULL on success.
 * @return New snapshot with refcount 1, or NULL on failure.
 */
ParserSnapshot *lime_snapshot_create(const char *grammar_file, char **error);

/**
 * @brief Build a base snapshot from in-memory grammar text.
 *
 * Same pipeline as lime_snapshot_create() but takes the grammar
 * source as a buffer rather than a file path.  Internally writes
 * the buffer to a temp file and runs lime + cc on it.  Used by
 * lime_snapshot_extend() and by callers that hold the grammar
 * text in memory (e.g. embedded in a config blob).
 *
 * @param grammar_text  Pointer to the grammar source bytes.
 * @param len           Byte length (NOT including any NUL).
 * @param[out] error    On failure, malloc'd message; NULL on success.
 * @return New snapshot with refcount 1, or NULL on failure.
 */
ParserSnapshot *lime_compile_grammar_text(const char *grammar_text, size_t len, char **error);

/** @} */ /* end snapshot_api */

/* --- Parse Context API ----------------------------------------------- */

/** @defgroup parse_ctx_api Parse Context API
 *  @brief Manage parse sessions pinned to a snapshot.
 *  @{
 */

/**
 * @brief Opaque parse context handle.
 *
 * See include/parse_context.h for details.
 */
typedef struct ParseContext ParseContext;

/**
 * @brief Begin a parse session pinned to a snapshot.
 *
 * Acquires a reference to @p snap.
 *
 * @param snap Snapshot to pin.
 * @return New parse context, or NULL on failure.
 *
 * @see parse_end()
 */
ParseContext *parse_begin(ParserSnapshot *snap);

/**
 * @brief Feed a token to the parser.
 *
 * @param ctx         Active parse context.
 * @param token_code  Token code (0 for end-of-input).
 * @param token_value Semantic value associated with the token (may be NULL).
 * @param location    Byte offset of the token in the original source, or
 *                    LIME_LOC_UNKNOWN if the grammar does not declare
 *                    %locations or the caller does not track positions.
 *                    Currently stored but not yet propagated into reduce
 *                    actions (that plumbing lands with the push-parser
 *                    full implementation); callers should pass real
 *                    locations anyway so they are ready for it.
 * @retval 0     Success.
 * @retval non-zero Parse error.
 */
int parse_token(ParseContext *ctx, int token_code, void *token_value, int location);

/**
 * @brief End the parse session.
 *
 * Releases the snapshot reference and frees all internal state.
 *
 * @param ctx Parse context to end.  Passing NULL is safe.
 */
void parse_end(ParseContext *ctx);

/**
 * @brief Return the snapshot pinned by this context.
 *
 * @param ctx Active parse context.
 * @return The pinned ParserSnapshot.
 */
ParserSnapshot *parse_get_snapshot(ParseContext *ctx);

/** @} */ /* end parse_ctx_api */

/* --- JIT Compilation API ----------------------------------------- */

/** @defgroup jit_api JIT Compilation API
 *  @brief Control LLVM JIT compilation of parser action tables.
 *  @{
 */

/**
 * @brief Check whether JIT compilation support is available at runtime.
 *
 * @retval true  LLVM was linked and initialization succeeded.
 * @retval false JIT is not available.
 */
bool lime_jit_available(void);

/**
 * @brief Compile and attach JIT code to a snapshot's action tables.
 *
 * @param snap Snapshot to JIT-compile.
 * @retval 0     Success (or already compiled, or LLVM unavailable).
 * @retval non-zero Compilation failure.
 */
int lime_jit_compile(ParserSnapshot *snap);

/** @} */ /* end jit_api */

/* --- Extension API ------------------------------------------------ */

/** @defgroup ext_init Extension Initialization API
 *  @brief Initialize and tear down the global extension registry.
 *  @{
 */

/**
 * @brief Initialize the global extension registry.
 *
 * Must be called before any extension functions.
 *
 * @retval true  Initialization succeeded.
 * @retval false Initialization failed.
 *
 * @see lime_extension_registry_destroy()
 */
bool lime_extension_registry_init(void);

/**
 * @brief Destroy the global extension registry.
 *
 * Unloads all extensions and frees registry resources.
 */
void lime_extension_registry_destroy(void);

/** @} */ /* end ext_init */

/* --- GLR Parsing API ---------------------------------------------- */

/** @defgroup glr_api GLR Parsing API
 *  @brief Generalized LR parsing -- forks the stack on conflicts.
 *
 *  Generalized LR (GLR) parsing handles grammars that LALR(1) cannot
 *  parse by exploring multiple parse stack heads in parallel.  A
 *  shift/reduce or reduce/reduce conflict at runtime causes a fork
 *  rather than a syntax error.  Heads that converge to the same
 *  parser state are merged; if two reductions produce the same
 *  nonterminal at the same position the user-provided
 *  GLRDisambiguateFn is consulted.
 *
 *  GLR has zero impact on the LALR fast path: parse_token() does not
 *  enter GLR code at all.  Callers must explicitly switch a
 *  ParseContext into GLR mode via lime_parse_glr() before driving it
 *  with lime_parse_glr_feed().
 *
 *  @see docs/GLR.md, include/glr.h, tests/test_glr_no_conflict.c,
 *       tests/test_glr_ambiguous.c
 *  @{
 */

#include "glr.h" /* GLRDisambiguateFn, GLRParser */

/**
 * @brief Switch a parse context into GLR mode.
 *
 * Allocates a Graph-Structured Stack and binds it to the context.
 * Subsequent calls to lime_parse_glr_feed() drive the GLR engine
 * against the context's pinned snapshot.
 *
 * @param ctx        Active parse context (must have a snapshot pinned).
 * @param disambig   Disambiguation callback (NULL = strict mode: any
 *                   merge is reported as -1 unresolvable ambiguity).
 * @param user_data  Opaque pointer passed back into @p disambig.
 *
 * @retval  0 Success (context now in GLR mode, or already was; the
 *            disambiguation callback is updated either way).
 * @retval -1 Unused for setup (reserved for symmetry with feed).
 * @retval -2 Allocation failure or invalid arguments.
 */
int lime_parse_glr(ParseContext *ctx, GLRDisambiguateFn disambig, void *user_data);

/**
 * @brief Feed one terminal to the GLR engine.
 *
 * @param ctx    Parse context previously switched into GLR mode.
 * @param token  Terminal symbol code.
 *
 * @retval  0 Success.
 * @retval -1 Unresolvable ambiguity (no callback or callback returned 0).
 * @retval -2 All heads died (syntax error on every active path) or
 *            the context is not in GLR mode.
 */
int lime_parse_glr_feed(ParseContext *ctx, uint16_t token);

/**
 * @brief Test whether the GLR parse has accepted.
 *
 * @return true iff exactly one live head sits in the snapshot's
 *         accept state.  Always false for contexts that are not in
 *         GLR mode.
 */
bool lime_parse_glr_accepted(ParseContext *ctx);

/**
 * @brief Return the number of active GLR parse heads.
 *
 * @return 1 for unambiguous parses, >1 while exploring an ambiguous
 *         prefix, 0 once all heads have died or the context is not
 *         in GLR mode.
 */
uint32_t lime_parse_glr_head_count(ParseContext *ctx);

/**
 * @brief Tear down GLR mode on a context, freeing the GSS.
 *
 * Idempotent and safe to call on a context that was never in GLR
 * mode.  The context itself remains valid for further parse_token()
 * calls (which are unaffected by GLR mode).
 */
void lime_parse_glr_end(ParseContext *ctx);

/** @} */ /* end glr_api */

#ifdef __cplusplus
}
#endif

#endif /* PARSER_H */
