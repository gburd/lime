#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Version information */
const char *lemon_parser_version(void);

/* --- Snapshot API -------------------------------------------------- */

/*
** Opaque snapshot handle.  See src/snapshot.h for the full definition.
*/
typedef struct ParserSnapshot ParserSnapshot;

/*
** Acquire a reference to a snapshot.  The caller must eventually call
** lemon_snapshot_release() to avoid leaking memory.  Returns the same
** pointer for convenience.  Passing NULL is safe and returns NULL.
*/
ParserSnapshot *lemon_snapshot_acquire(ParserSnapshot *snap);

/*
** Release a snapshot reference.  When the last reference is released
** the snapshot and all memory it owns are freed.  NULL is safe.
*/
void lemon_snapshot_release(ParserSnapshot *snap);

/*
** Create a base snapshot by parsing a grammar file through the Lemon
** parser generator.  On success returns a snapshot with refcount 1 and
** sets *error to NULL.  On failure returns NULL and sets *error to a
** malloc'd message the caller must free.
*/
ParserSnapshot *lemon_snapshot_create(const char *grammar_file, char **error);

/* --- Parse Context API ----------------------------------------------- */

/*
** Opaque parse context handle.  See include/parse_context.h for details.
*/
typedef struct ParseContext ParseContext;

/*
** Begin a parse session pinned to a snapshot.
** Acquires a reference to *snap*; returns NULL on failure.
*/
ParseContext *parse_begin(ParserSnapshot *snap);

/*
** Feed a token to the parser.  Pass token_code == 0 for end-of-input.
** Returns 0 on success, non-zero on error.
*/
int parse_token(ParseContext *ctx, int token_code, void *token_value);

/*
** End the parse session, releasing the snapshot reference and freeing
** all internal state.  Passing NULL is safe.
*/
void parse_end(ParseContext *ctx);

/*
** Return the snapshot pinned by this context.
*/
ParserSnapshot *parse_get_snapshot(ParseContext *ctx);

/* --- JIT Compilation API ----------------------------------------- */

/*
** Check whether JIT compilation support is available at runtime.
** Returns true if LLVM was linked and initialization succeeds.
*/
bool lime_jit_available(void);

/*
** Compile and attach JIT code to a snapshot's action tables.
** Returns 0 on success, non-zero on failure (no-op if already compiled
** or if LLVM is unavailable).
*/
int lime_jit_compile(ParserSnapshot *snap);

/* --- Extension API ------------------------------------------------ */

/*
** Initialize the extension registry.  Must be called before any
** extension functions.  Returns true on success.
*/
bool lemon_extension_registry_init(void);

/*
** Destroy the extension registry, unloading all extensions.
*/
void lemon_extension_registry_destroy(void);

#ifdef __cplusplus
}
#endif

#endif /* PARSER_H */
