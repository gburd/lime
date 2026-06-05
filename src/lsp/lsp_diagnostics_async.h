/*
 * lsp_diagnostics_async.h -- background diagnostic worker pool.
 *
 * Replaces the synchronous lsp_diagnostics_run() call in
 * publish_diagnostics() with an async worker that runs the lint
 * pipeline on a separate thread.  Subsequent didChange events on
 * the same document cancel any in-flight worker and start a new
 * one with the latest text.
 *
 * Key design points:
 *
 *   - Each document URI gets at most ONE in-flight worker.  A
 *     monotonic generation counter per worker lets us discard
 *     stale results: when the worker finishes, it publishes
 *     diagnostics only if its generation matches the current
 *     pending generation for that URI.
 *
 *   - Workers communicate back via the LSP server's stdout pipe,
 *     guarded by a single mutex (the LSP framing wraps every
 *     message with Content-Length + body; partial writes from
 *     concurrent workers would corrupt the JSON-RPC stream).
 *
 *   - When the editor types and emits didChange every keystroke,
 *     each new edit bumps the generation; in-flight worker(s)
 *     check the generation when they finish and silently drop
 *     their result if they're stale.  This delivers diagnostics
 *     for the LATEST edit only, after a brief debounce.
 *
 *   - Worker uses lime_lint_grammar_in_process() (the in-process
 *     fast path added in v0.10.0) when available, falling back
 *     to the subprocess path the same way lsp_diagnostics_run()
 *     does.
 *
 * Thread safety:
 *
 *   - lsp_diagnostics_async_init / _shutdown: must be called on
 *     the main LSP thread, no workers running.
 *   - lsp_diagnostics_async_request: called from the main LSP
 *     thread (didOpen/didChange/didSave handlers).  Internally
 *     thread-safe.
 *
 * POSIX-only.  Windows can fall through to the synchronous path
 * the same way lime_lsp_in_process is gated on POSIX in the
 * meson build (lime_threads.h doesn't shim pthread_create today;
 * the LSP server is a developer tool that runs fine under WSL).
 */
#ifndef LIME_LSP_DIAGNOSTICS_ASYNC_H
#define LIME_LSP_DIAGNOSTICS_ASYNC_H

#include <stddef.h>
#include <stdio.h>

/* Forward decl -- the implementation owns the queue + thread state. */
typedef struct lsp_diagnostics_async lsp_diagnostics_async;

/*
** Initialise the async pool.  `out` is the LSP server's stdout
** stream; the implementation takes a borrowed reference and
** synchronises writes with an internal mutex.  `lime_bin` is the
** path to the lime binary used for the subprocess fallback;
** borrowed (caller must outlive the pool).
**
** Returns NULL on POSIX systems where pthread_create fails (very
** rare), or on Windows where the implementation is a no-op stub.
** Callers MUST check for NULL and fall back to the synchronous
** path when this returns NULL.
*/
lsp_diagnostics_async *lsp_diagnostics_async_create(FILE *out,
                                                    const char *lime_bin);

/*
** Request a diagnostic refresh for `uri`.  `text` is copied into
** the worker's queue; the caller may free it after this returns.
**
** If a worker is already in flight for this URI, its generation
** is bumped (so its result, when it eventually finishes, will be
** silently discarded).  A new worker starts with the new text.
**
** Calling this with text == NULL or text_len == 0 cancels the
** in-flight worker and publishes an empty diagnostics array
** (used by didClose).
*/
void lsp_diagnostics_async_request(lsp_diagnostics_async *p,
                                    const char *uri,
                                    const char *text, size_t text_len);

/*
** Wait for any in-flight workers to finish, then tear down the
** pool.  Called from lsp_server_free.  Safe to call with NULL.
*/
void lsp_diagnostics_async_destroy(lsp_diagnostics_async *p);

#endif /* LIME_LSP_DIAGNOSTICS_ASYNC_H */
