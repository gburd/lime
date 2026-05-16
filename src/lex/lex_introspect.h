/*
** src/lex/lex_introspect.h -- compiled-lexer text serializer (M4.2).
**
** Round-trippable .lex-syntax dump of a LimeLexCompiled paired
** with the LimeLexSpec it came from.  The body is byte-identical
** to lime_lex_spec_to_text(spec); a header block of `//`
** comments augments it with per-state DFA statistics so callers
** can answer "why didn't this rule fire?" without spelunking the
** compile internals.
**
** Mirrors the parser side's lime_modifications_to_grammar_text:
** the output is a self-describing artifact suitable for diffs,
** debugging, and feeding back through lime_lex_parse +
** lime_lex_compile to verify equivalence.
**
** Round-trip property: parse(lime_lex_compiled_to_text(c, s)) ==
** s structurally.  The leading comment block does not survive
** round-trip (comments are stripped by the tokenizer), but it's
** not meant to: it's debugging surface, not part of the AST.
*/
#ifndef LIME_LEX_INTROSPECT_H
#define LIME_LEX_INTROSPECT_H

#include "lex_ast.h"
#include "lex_compile.h"

/* Serialize a compiled lexer to a heap-allocated, NUL-terminated
** string.  Caller frees with free().  Returns NULL on alloc
** failure or when both arguments are NULL.
**
** When `c` is NULL, the result is just lime_lex_spec_to_text(spec)
** (no stats header).  When `spec` is NULL, the result is just the
** stats header with no body.  When both are non-NULL the
** stats header precedes the spec body. */
char *lime_lex_compiled_to_text(const LimeLexCompiled *c,
                                const LimeLexSpec *spec);

#endif /* LIME_LEX_INTROSPECT_H */
