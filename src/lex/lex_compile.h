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
    char *state_name;  /* "INITIAL" or a declared state name */
    int exclusive;     /* 0 = inclusive (or INITIAL), 1 = exclusive */
    LimeDfa *dfa;      /* compiled DFA for this state */
    int n_rules;       /* number of rules included in the DFA */
    int *rule_indices; /* original spec-rule indices in compile order;
                               ** index = the accept_rule the DFA reports */
    /* v0.9 per-token DFA: one minimised DFA per non-EOF rule, in the
    ** same order as rule_indices[].  NULL entries correspond to EOF
    ** rules (no DFA, dispatched at LexFeedEOF time).  When non-NULL,
    ** per_rule_dfas[i] recognises exactly the language of the i-th
    ** rule -- useful for leading-byte dispatch where a single rule
    ** is the unambiguous match for a given starting byte.  The
    ** unified `dfa` above remains for ambiguous-leading-byte
    ** fallback. */
    LimeDfa **per_rule_dfas;
    int n_per_rule_dfas; /* equals n_rules; entries may be NULL for EOF rules */
} LimeLexCompiledState;

typedef struct {
    LimeLexCompiledState *states;
    int n_states;
    int error_count; /* nonzero if compile saw any errors */
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
const LimeLexCompiledState *lime_lex_compiled_find_state(const LimeLexCompiled *c,
                                                         const char *state_name);

/* Walk the spec's rules in compile order: top-level rules first,
** then -- subject to %lexer_include filtering -- the named
** rulesets in include order, or every ruleset in declaration
** order when no %lexer_include is present.
**
** This is the canonical order used to assign each rule its global
** compile index (the value `Foo_match` reports via *out_rule and
** the index emit_h writes into FOO_RULE_<NAME>).  Both rule-name
** and action-body collection in lex_emit.c walk in this order so
** the indices agree with what lime_lex_compile produced.
**
** The walker invokes `cb(rule, user)` for each rule.  Returning
** non-zero from `cb` aborts the walk and propagates the value.
** Undefined %lexer_include names are silently skipped here
** (they're diagnosed at compile time). */
typedef int (*LimeLexRuleVisitor)(const LimeLexRule *r, void *user);
int lime_lex_walk_rules(const LimeLexSpec *spec, LimeLexRuleVisitor cb, void *user);

#endif /* LIME_LEX_COMPILE_H */
