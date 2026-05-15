/*
** src/lex/lex_compile.h -- per-state DFA compilation for the
** Lime .lex frontend (M2.5b).
**
** End-to-end pipeline driver: takes a parsed-and-resolved
** LimeLexSpec and produces one DFA per declared lexer state
** (plus the implicit INITIAL).  Each per-state DFA combines
** every rule whose state qualifier includes the state into a
** single NFA, runs subset construction, and minimizes.  Accept
** states are tagged with the originating rule's declaration
** index (compile-order priority on length ties).
**
** State semantics from docs/LEXER_DESIGN.md v0.2:
**   - INITIAL: implicit; default state.  Rules with no
**     qualifier fire here.
**   - %state X (inclusive): rules with no qualifier ALSO
**     fire in X, plus rules qualified <X>.
**   - %exclusive_state X: ONLY rules qualified <X> fire.
**   - Multi-state qualifier <a, b, c>: rule fires in each
**     listed state.
*/
#ifndef LIME_LEX_COMPILE_H
#define LIME_LEX_COMPILE_H

#include "lex_ast.h"
#include "lex_dfa.h"

#include <stddef.h>

typedef struct {
    char     *state_name;     /* "INITIAL" or a declared state name */
    int       exclusive;      /* 0 = inclusive (or INITIAL), 1 = exclusive */
    LimeDfa  *dfa;            /* compiled DFA for this state */
    int       n_rules;        /* number of rules included in the DFA */
    int      *rule_indices;   /* original spec-rule indices in compile order;
                               ** index = the accept_rule the DFA reports */
} LimeLexCompiledState;

typedef struct {
    LimeLexCompiledState *states;
    int                   n_states;
    int                   error_count;     /* nonzero if compile saw any errors */
} LimeLexCompiled;

/* Compile a parsed and pattern-resolved spec into per-state
** DFAs.  Caller must run lime_lex_resolve_patterns(spec) first;
** rules without expanded_pattern are compiled from rule->pattern
** directly (so a spec with no %pattern declarations works without
** a separate resolve call).
**
** Returns NULL only on alloc failure.  On compile errors (bad
** regex syntax in a rule, etc.) the result has error_count > 0
** and the offending state's dfa may be NULL.  Caller releases
** with lime_lex_compiled_free. */
LimeLexCompiled *lime_lex_compile(const LimeLexSpec *spec);

void lime_lex_compiled_free(LimeLexCompiled *c);

/* Find the compiled state by name; returns NULL if absent. */
const LimeLexCompiledState *lime_lex_compiled_find_state(
    const LimeLexCompiled *c, const char *state_name);

#endif /* LIME_LEX_COMPILE_H */
