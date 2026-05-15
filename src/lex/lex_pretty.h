/*
** src/lex/lex_pretty.h -- pretty-printer for LimeLexSpec ASTs.
**
** Stage 4 of the .lex pipeline (after tokenize/parse/resolve).
** Emits a `.lex`-syntax text representation of an AST suitable
** for diffing, debugging, or feeding back through the parser
** (round-trip).
**
** Round-trip property: parse(t) -> A; pretty(A) -> t';
** parse(t') -> A'; pretty(A') -> t''.  The byte-equivalence
** t' == t'' holds (pretty-print is idempotent).  The structural
** equivalence A == A' also holds (modulo block-form rules,
** which the M1.4 parser desugars into per-rule qualifiers --
** the pretty-printer emits per-rule qualifiers, never the
** block shorthand).
*/
#ifndef LIME_LEX_PRETTY_H
#define LIME_LEX_PRETTY_H

#include "lex_ast.h"

/* Pretty-print spec into a freshly heap-allocated, NUL-terminated
** string.  Caller owns the result and must free() it.  Returns
** NULL only on alloc failure (NOT on AST shape errors -- the
** pretty-printer is total and emits something for any well-formed
** AST). */
char *lime_lex_spec_to_text(const LimeLexSpec *spec);

#endif /* LIME_LEX_PRETTY_H */
