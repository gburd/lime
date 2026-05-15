/*
** tests/test_lex_compile.c -- M2.5b end-to-end integration test.
**
** Drives the full pipeline:
**    .lex source string
**       -> lime_lex_parse        (M1.2)
**       -> lime_lex_resolve_patterns (M1.3)
**       -> lime_lex_compile      (M2.5b -- this commit)
** Then drives each compiled DFA on sample inputs and asserts
** the matched rule.
**
** Tests:
**   1. INITIAL-only spec: simple rules, no states.
**   2. Inclusive state: rules in INITIAL also fire in inclusive
**      states.
**   3. Exclusive state: only qualified rules fire there.
**   4. Multi-state qualifier: rule appears in each listed state's
**      compiled DFA.
**   5. Rules from a %ruleset: also collected and compiled.
**   6. EOF rules: present in the spec but skipped during DFA
**      construction (DFA has no transition for EOF rules' rule
**      indices).
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lex_ast.h"
#include "lex_compile.h"
#include "lex_dfa.h"
#include "lex_parse.h"
#include "lex_resolve.h"

static int fails = 0;

#define EXPECT(cond, ...) do {                                  \
    if (!(cond)) {                                              \
        fprintf(stderr, "  %s:%d: ", __func__, __LINE__);       \
        fprintf(stderr, __VA_ARGS__);                           \
        fprintf(stderr, "\n");                                  \
        fails++;                                                \
    }                                                           \
} while (0)

/* Drive the compiled DFA and return either the global
** rule_index (mapped via cs->rule_indices), or -1 if the input
** isn't accepted. */
static int compile_match(const LimeLexCompiledState *cs,
                         const char *input, size_t n) {
    if (!cs || !cs->dfa) return -1;
    int s = cs->dfa->start;
    for (size_t i = 0; i < n; i++) {
        s = cs->dfa->states[s].trans[(unsigned char)input[i]];
        if (s < 0) return -1;
    }
    if (!cs->dfa->states[s].is_accept) return -1;
    int local_rule = cs->dfa->states[s].accept_rule;
    if (local_rule < 0 || local_rule >= cs->n_rules) return -1;
    return cs->rule_indices[local_rule];
}

static LimeLexCompiled *compile_source(const char *src) {
    LimeLexSpec *spec = lime_lex_parse("<test>", src, strlen(src));
    if (!spec || spec->error_count > 0) {
        if (spec) {
            fprintf(stderr, "  parse errors: %d\n", spec->error_count);
            lime_lex_spec_free(spec);
        }
        return NULL;
    }
    if (lime_lex_resolve_patterns(spec) != 0) {
        fprintf(stderr, "  resolve errors: %d\n", spec->error_count);
        lime_lex_spec_free(spec);
        return NULL;
    }
    LimeLexCompiled *c = lime_lex_compile(spec);
    lime_lex_spec_free(spec);
    return c;
}

/* ----- sub-tests ----- */

static int test_initial_only(void) {
    int saved = fails;
    const char *src =
        "rule kw_if matches /if/ { /* */ }\n"
        "rule ident matches /[A-Za-z_][A-Za-z0-9_]*/ { /* */ }\n"
        "rule num   matches /[0-9]+/ { /* */ }\n";
    LimeLexCompiled *c = compile_source(src);
    EXPECT(c != NULL, "compile NULL");
    if (c) {
        EXPECT(c->n_states == 1, "n_states=%d want 1", c->n_states);
        const LimeLexCompiledState *cs =
            lime_lex_compiled_find_state(c, "INITIAL");
        EXPECT(cs != NULL, "INITIAL state missing");
        if (cs) {
            EXPECT(cs->n_rules == 3, "n_rules=%d want 3", cs->n_rules);
            EXPECT(cs->dfa != NULL, "INITIAL dfa NULL");
            /* Priority resolution: 'if' matches kw_if (rule 0) AND
            ** ident (rule 1); rule 0 wins. */
            EXPECT(compile_match(cs, "if", 2)  == 0,
                   "if -> rule 0 (kw priority)");
            EXPECT(compile_match(cs, "foo", 3) == 1, "foo -> rule 1");
            EXPECT(compile_match(cs, "42", 2)  == 2, "42 -> rule 2");
            EXPECT(compile_match(cs, "i", 1)   == 1, "i -> rule 1");
        }
        lime_lex_compiled_free(c);
    }
    if (fails == saved) printf("test_initial_only: PASS\n");
    return fails - saved;
}

static int test_inclusive_state(void) {
    int saved = fails;
    /* %state EXPR is inclusive: rules in INITIAL also fire here.
    ** rule 'plus' has <EXPR> qualifier so it ONLY fires in EXPR. */
    const char *src =
        "%state EXPR.\n"
        "rule ident matches /[a-z]+/ { /* */ }\n"
        "<EXPR> rule plus matches /\\+/ { /* */ }\n";
    LimeLexCompiled *c = compile_source(src);
    EXPECT(c != NULL, "compile NULL");
    if (c) {
        EXPECT(c->n_states == 2, "n_states=%d want 2", c->n_states);
        const LimeLexCompiledState *init = lime_lex_compiled_find_state(c, "INITIAL");
        const LimeLexCompiledState *expr = lime_lex_compiled_find_state(c, "EXPR");
        EXPECT(init && expr, "states missing");
        if (init && expr) {
            /* INITIAL: only ident applies (the unqualified rule). */
            EXPECT(init->n_rules == 1, "INITIAL n_rules=%d want 1",
                   init->n_rules);
            /* EXPR (inclusive): both ident and plus apply. */
            EXPECT(expr->n_rules == 2, "EXPR n_rules=%d want 2",
                   expr->n_rules);
            EXPECT(compile_match(init, "foo", 3) == 0, "INITIAL foo -> rule 0");
            EXPECT(compile_match(init, "+", 1)   == -1, "INITIAL + -> reject");
            EXPECT(compile_match(expr, "foo", 3) == 0, "EXPR foo -> rule 0");
            EXPECT(compile_match(expr, "+", 1)   == 1, "EXPR + -> rule 1");
        }
        lime_lex_compiled_free(c);
    }
    if (fails == saved) printf("test_inclusive_state: PASS\n");
    return fails - saved;
}

static int test_exclusive_state(void) {
    int saved = fails;
    /* %exclusive_state QUOTED: rules in INITIAL do NOT fire here. */
    const char *src =
        "%exclusive_state QUOTED.\n"
        "rule open  matches /\"/ { /* */ }\n"
        "<QUOTED> rule body  matches /[^\"]+/ { /* */ }\n"
        "<QUOTED> rule close matches /\"/ { /* */ }\n";
    LimeLexCompiled *c = compile_source(src);
    EXPECT(c != NULL, "compile NULL");
    if (c) {
        EXPECT(c->n_states == 2, "n_states=%d want 2", c->n_states);
        const LimeLexCompiledState *init = lime_lex_compiled_find_state(c, "INITIAL");
        const LimeLexCompiledState *q    = lime_lex_compiled_find_state(c, "QUOTED");
        EXPECT(init && q, "states missing");
        if (init && q) {
            /* INITIAL: only `open` (unqualified). */
            EXPECT(init->n_rules == 1, "INITIAL n_rules=%d want 1",
                   init->n_rules);
            /* QUOTED (exclusive): only body and close. */
            EXPECT(q->n_rules == 2, "QUOTED n_rules=%d want 2",
                   q->n_rules);
            EXPECT(compile_match(init, "\"", 1)   == 0,
                   "INITIAL \" -> rule 0 (open)");
            EXPECT(compile_match(q,    "abc", 3)  == 1,
                   "QUOTED abc -> rule 1 (body)");
            EXPECT(compile_match(q,    "\"", 1)   == 2,
                   "QUOTED \" -> rule 2 (close)");
        }
        lime_lex_compiled_free(c);
    }
    if (fails == saved) printf("test_exclusive_state: PASS\n");
    return fails - saved;
}

static int test_multi_state_qualifier(void) {
    int saved = fails;
    /* Rule in two states; should appear in both compiled DFAs. */
    const char *src =
        "%exclusive_state A.\n"
        "%exclusive_state B.\n"
        "<A, B> rule shared matches /x/ { /* */ }\n"
        "<A>    rule onlyA  matches /a/ { /* */ }\n"
        "<B>    rule onlyB  matches /b/ { /* */ }\n";
    LimeLexCompiled *c = compile_source(src);
    EXPECT(c != NULL, "compile NULL");
    if (c) {
        const LimeLexCompiledState *a = lime_lex_compiled_find_state(c, "A");
        const LimeLexCompiledState *b = lime_lex_compiled_find_state(c, "B");
        EXPECT(a && b, "states missing");
        if (a && b) {
            EXPECT(a->n_rules == 2, "A n_rules=%d want 2", a->n_rules);
            EXPECT(b->n_rules == 2, "B n_rules=%d want 2", b->n_rules);
            EXPECT(compile_match(a, "x", 1) == 0, "A x -> rule 0 (shared)");
            EXPECT(compile_match(a, "a", 1) == 1, "A a -> rule 1 (onlyA)");
            EXPECT(compile_match(a, "b", 1) == -1, "A b -> reject");
            EXPECT(compile_match(b, "x", 1) == 0, "B x -> rule 0 (shared)");
            EXPECT(compile_match(b, "b", 1) == 2, "B b -> rule 2 (onlyB)");
            EXPECT(compile_match(b, "a", 1) == -1, "B a -> reject");
        }
        lime_lex_compiled_free(c);
    }
    if (fails == saved) printf("test_multi_state_qualifier: PASS\n");
    return fails - saved;
}

static int test_ruleset_rules_compiled(void) {
    int saved = fails;
    const char *src =
        "%ruleset basic {\n"
        "    rule a matches /a/ { /* */ }\n"
        "    rule b matches /b/ { /* */ }\n"
        "}.\n"
        "rule top matches /t/ { /* */ }\n";
    LimeLexCompiled *c = compile_source(src);
    EXPECT(c != NULL, "compile NULL");
    if (c) {
        const LimeLexCompiledState *cs =
            lime_lex_compiled_find_state(c, "INITIAL");
        EXPECT(cs && cs->n_rules == 3,
               "INITIAL n_rules=%d want 3 (top + ruleset's a, b)",
               cs ? cs->n_rules : -1);
        if (cs) {
            /* Top-level rule first (declaration order); then
            ** ruleset rules. */
            EXPECT(compile_match(cs, "t", 1) == 0, "t -> rule 0 (top)");
            EXPECT(compile_match(cs, "a", 1) == 1, "a -> rule 1 (ruleset)");
            EXPECT(compile_match(cs, "b", 1) == 2, "b -> rule 2 (ruleset)");
        }
        lime_lex_compiled_free(c);
    }
    if (fails == saved) printf("test_ruleset_rules_compiled: PASS\n");
    return fails - saved;
}

static int test_eof_rule_skipped(void) {
    int saved = fails;
    /* EOF rules don't appear in the DFA but the compiled state
    ** still tracks them in n_rules / rule_indices for
    ** LexFeedEOF dispatch. */
    const char *src =
        "rule body matches /a/ { /* */ }\n"
        "rule eof  matches <<EOF>> { /* */ }\n";
    LimeLexCompiled *c = compile_source(src);
    EXPECT(c != NULL, "compile NULL");
    if (c) {
        const LimeLexCompiledState *cs =
            lime_lex_compiled_find_state(c, "INITIAL");
        EXPECT(cs && cs->n_rules == 2,
               "INITIAL n_rules=%d want 2 (body + eof)",
               cs ? cs->n_rules : -1);
        if (cs) {
            /* DFA matches only 'a' (rule 0); eof is dispatched
            ** outside the DFA. */
            EXPECT(compile_match(cs, "a", 1) == 0, "a -> rule 0");
            EXPECT(compile_match(cs, "",  0) == -1, "empty -> reject");
        }
        lime_lex_compiled_free(c);
    }
    if (fails == saved) printf("test_eof_rule_skipped: PASS\n");
    return fails - saved;
}

static int test_pattern_resolution_propagates(void) {
    int saved = fails;
    /* %pattern + interpolation; verifies M1.3's resolved
    ** expanded_pattern is what gets compiled. */
    const char *src =
        "%pattern d /[0-9]/.\n"
        "rule num matches /{d}+/ { /* */ }\n";
    LimeLexCompiled *c = compile_source(src);
    EXPECT(c != NULL, "compile NULL");
    if (c) {
        const LimeLexCompiledState *cs =
            lime_lex_compiled_find_state(c, "INITIAL");
        EXPECT(cs && cs->dfa, "INITIAL dfa missing");
        if (cs && cs->dfa) {
            EXPECT(compile_match(cs, "42",  2) == 0, "42 -> rule 0");
            EXPECT(compile_match(cs, "abc", 3) == -1, "abc -> reject");
        }
        lime_lex_compiled_free(c);
    }
    if (fails == saved) printf("test_pattern_resolution_propagates: PASS\n");
    return fails - saved;
}

int main(void) {
    test_initial_only();
    test_inclusive_state();
    test_exclusive_state();
    test_multi_state_qualifier();
    test_ruleset_rules_compiled();
    test_eof_rule_skipped();
    test_pattern_resolution_propagates();
    if (fails == 0) {
        printf("\ntest_lex_compile: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr, "\ntest_lex_compile: %d sub-test failure(s)\n", fails);
    return 1;
}
