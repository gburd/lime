/*
** src/lex/lex_parse.h -- parser for Lime .lex source files.
**
** Stage 2 of the .lex frontend.  Consumes the token stream from
** lex_tokenize and produces a LimeLexSpec AST.  Does NOT resolve
** %pattern fragment interpolation (M1.3) or desugar <STATE>{...}
** rule-block syntax (M1.4); those are subsequent passes operating
** on the AST.
**
** Diagnostics policy: parse errors are emitted to stderr with
** filename:line: prefix and counted in spec->error_count.  The
** parser keeps going past errors when it can synchronise on a
** known directive boundary; this lets a single parse pass surface
** multiple errors per file.
*/
#ifndef LIME_LEX_PARSE_H
#define LIME_LEX_PARSE_H

#include "lex_ast.h"

#include <stddef.h>

/* Parse a .lex source buffer into a LimeLexSpec AST.  Returns
** the spec on success (with spec->error_count == 0) or with a
** non-zero error count if parse errors were detected.  Returns
** NULL only on alloc failure.
**
** The returned spec is owned by the caller; release with
** lime_lex_spec_free.  filename is borrowed for diagnostics
** (must outlive the parse call but not the returned spec). */
LimeLexSpec *lime_lex_parse(const char *filename, const char *source, size_t source_len);

#endif /* LIME_LEX_PARSE_H */
