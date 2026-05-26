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

#ifdef __cplusplus
}
#endif

#endif /* LIME_COMPILER_H */
