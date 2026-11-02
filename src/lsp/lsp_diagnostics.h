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

/* Lint a grammar buffer in process (liblime_compiler) and return a
 * freshly allocated JSON array of LSP Diagnostic objects, possibly empty;
 * caller frees with json_free.  Returns NULL only if the array itself
 * cannot be allocated.
 */
json_value *lsp_diagnostics_run(const char *text, size_t text_len);

#endif /* LIME_LSP_DIAGNOSTICS_H */
