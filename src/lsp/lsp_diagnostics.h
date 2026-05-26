/*
 * lsp_diagnostics.h -- shell out to `lime -L`, parse output,
 * surface as LSP Diagnostic[] via publishDiagnostics.
 *
 * The LSP reuses the existing `lime -L` binary rather than
 * embedding lime.c's parser.  This trades a fork+exec per
 * recompute (cheap at LSP timing scales -- hundreds of
 * milliseconds per debounced edit) for keeping the LSP a thin
 * shim with no parser-state duplication.  A future iteration may
 * link the parser library in directly.
 *
 * Output formats handled:
 *
 *   <path>:<line>:<col>: <severity>: <message>     (lint_grammar)
 *   <path>:<line>: <message>                       (ErrorMsg, parser stage)
 *
 * `severity` is one of {error, warning, info, hint}; anything
 * else gets demoted to "info".  Both stdout and stderr are
 * captured; lime sends lint output to stderr in current builds
 * but emits the "Linting <file>..." banner to stdout, which we
 * ignore.
 */
#ifndef LIME_LSP_DIAGNOSTICS_H
#define LIME_LSP_DIAGNOSTICS_H

#include "lsp_json.h"

#include <stddef.h>

/* Run `lime -L` against a text buffer.  The buffer is written to
 * a temp file under TMPDIR; the temp path is replaced in
 * diagnostic locations with the original document URI's path so
 * the editor reports against the right file.
 *
 * `lime_bin` is the path to the lime executable (resolved from
 * $LIME_BIN or the literal "lime" if unset).  Returns a freshly
 * allocated JSON array of LSP Diagnostic objects (possibly
 * empty); caller frees with json_free.  Returns NULL on hard
 * failure (couldn't fork, couldn't write temp file, etc.).
 */
json_value *lsp_diagnostics_run(const char *lime_bin,
                                const char *text, size_t text_len);

#endif /* LIME_LSP_DIAGNOSTICS_H */
