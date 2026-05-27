/*
 * lsp_format.h -- textDocument/formatting helper.
 *
 * Available since v0.6.x.  Implementation in lsp_format.c shells
 * out to `lime -F <tmpfile>` and returns the result as an LSP
 * TextEdit[] (a single edit replacing the entire document) so the
 * editor applies it via its standard formatting pipeline.
 */

#ifndef LIME_LSP_FORMAT_H
#define LIME_LSP_FORMAT_H

#include "lsp_json.h"

#include <stddef.h>

/* Build a textDocument/formatting response.  Returns a JSON array of
 * TextEdit objects (always 0 or 1 entries: one whole-document
 * replacement on success, empty array on failure or empty input).
 * Caller takes ownership of the returned json_value and frees it
 * with json_free().
 *
 * `lime_bin` is the path to the lime CLI binary; pass NULL to use
 * "lime" from $PATH.  Same convention as lsp_diagnostics_run.
 */
json_value *lsp_format_run(const char *lime_bin,
                           const char *text, size_t text_len);

#endif
