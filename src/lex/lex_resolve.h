/*
** src/lex/lex_resolve.h -- pattern fragment resolution for the
** Lime .lex frontend.
**
** Stage 3 of the .lex pipeline (after tokenize and parse, before
** the M2 DFA compiler).  Resolves `%pattern name /regex/`
** declarations: substitutes `{name}` references in pattern bodies
** and rule patterns with the named pattern's regex source.
**
** Wraps each substitution in `(...)` so the splice doesn't bleed
** into the surrounding regex context (e.g., `{x}*` with x="ab"
** produces `(ab)*`, not `ab*`).
**
** Disambiguates `{NAME}` (interpolation, alphabetic first char)
** from `{N,M}` (POSIX repetition, digit first char).  Repetition
** forms pass through verbatim.
**
** Cycle detection: a transient visitor flag (`_resolve_visit`) on
** each LimeLexPattern detects recursive references.  Cycles are
** reported as parse errors (incrementing spec->error_count) and
** the offending pattern's expansion collapses to empty.
*/
#ifndef LIME_LEX_RESOLVE_H
#define LIME_LEX_RESOLVE_H

#include "lex_ast.h"

/* Resolve all %pattern fragments and substitute them into rule
** patterns.  Populates pattern->expanded_regex for every pattern,
** rule->expanded_pattern for every rule (top-level and inside
** rulesets).  EOF rules' expanded_pattern stays NULL.
**
** Returns 0 on success, non-zero if any cycle or undefined-
** reference error was detected (spec->error_count is also
** incremented for each).  The returned spec is fully resolved
** for the M2 DFA compiler regardless; failed expansions
** collapse to safe placeholders so downstream passes don't
** see NULL pointers. */
int lime_lex_resolve_patterns(LimeLexSpec *spec);

#endif /* LIME_LEX_RESOLVE_H */
