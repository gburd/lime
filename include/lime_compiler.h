/**
 * @file lime_compiler.h
 * @brief In-process Lime grammar compilation.
 *
 * Replaces the subprocess pipeline (lime + cc + dlopen) used by
 * `lime_compile_grammar_text` in src/snapshot_create.c with a
 * direct in-process call.  Eliminates the ~200ms fork+exec
 * latency and the runtime C-compiler dependency, making
 * runtime grammar composition viable for daemon-startup
 * extension loading.
 *
 * Available since: v0.5.4 (ROADMAP item 1, phase 3).
 */

#ifndef LIME_COMPILER_H
#define LIME_COMPILER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ParserSnapshot;

/**
 * @brief Compile grammar text into a runtime ParserSnapshot, in-process.
 *
 * @param grammar_text  NUL-terminated grammar source.  Must outlive
 *                      this call only -- the function copies what
 *                      it needs.
 * @param len           Length of grammar_text in bytes (NOT including
 *                      NUL).
 * @param out_snapshot  On success, *out_snapshot points at a heap-
 *                      allocated ParserSnapshot.  Caller calls
 *                      snapshot_release() to free.
 * @param error         On failure, *error points at a heap-allocated
 *                      error message describing the parse error,
 *                      conflict, or LALR construction failure.
 *                      Caller free()s.  May be NULL on success.
 *
 * @retval 0 on success; *out_snapshot is non-NULL.
 * @retval non-zero on error; *out_snapshot is NULL, *error explains.
 *
 * Thread safety: each call must use a fresh LimeCompilerContext
 * internally.  At present (v0.5.4) the active-context pointer is
 * not __thread-local, so two concurrent calls in different threads
 * race.  Phase 5 will add thread-local storage when concurrent
 * compilation is required.  Sequential calls in the same thread
 * are fully isolated.
 */
int lime_compile_grammar_in_process(const char *grammar_text,
                                    size_t len,
                                    struct ParserSnapshot **out_snapshot,
                                    char **error);

/**
 * @brief Like lime_compile_grammar_in_process, but also reports the
 *        number of LALR conflicts the compose resolved (Letter 35 Q1).
 *
 * lemon resolves reduce/reduce conflicts (keep the earlier rule) and
 * shift/reduce conflicts (keep the shift) SILENTLY: they do not fail
 * the build.  So when a composed fragment contributes a rule that
 * shadows an already-loaded dialect's rule -- same RHS reducing to the
 * same LHS, or a shift/reduce overlap -- the snapshot builds and one
 * meaning is chosen by table-build order, not the author's intent.
 *
 * @param out_nconflict  On success, receives the count of conflicts
 *                       resolved during table construction.  0 = clean
 *                       compose.  >0 = at least one fragment rule
 *                       collided and lemon picked a winner
 *                       deterministically (keep-first / keep-shift);
 *                       the caller should treat this as "this grammar
 *                       conflicts with an already-loaded dialect" and
 *                       refuse the load or warn the author rather than
 *                       ship a snapshot that silently mis-parses.
 *                       May be NULL.
 *
 * Same return/ownership contract: 0 on success (*out_snapshot
 * non-NULL), non-zero on a hard error (parse error, empty grammar,
 * unreducible rule, LALR construction failure -- distinct from a
 * resolved conflict, which still returns 0).
 */
int lime_compile_grammar_in_process_ex(const char *grammar_text,
                                       size_t len,
                                       struct ParserSnapshot **out_snapshot,
                                       char **error,
                                       int *out_nconflict);

#ifdef __cplusplus
}
#endif

#endif /* LIME_COMPILER_H */
