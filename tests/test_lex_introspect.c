/*
** tests/test_lex_introspect.c -- M4.2 integration test.
**
** Verifies lime_lex_compiled_to_text:
**
**   1. NULL/NULL inputs return NULL.
**   2. spec-only invocation matches lime_lex_spec_to_text(spec).
**   3. compiled+spec invocation:
**      a. Body re-parses to a structurally equivalent spec.
**      b. Body re-compiles to a structurally equivalent
**         LimeLexCompiled (same n_states, same n_rules per state,
**         same rule_indices).
**      c. Stats header lists every compiled state with its DFA
**         shape and rule list.
**      d. The stats header is comments only, so re-parsing the
**         full output (header + body) ignores it.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lex_ast.h"
#include "lex_compile.h"
#include "lex_dfa.h"
#include "lex_introspect.h"
#include "lex_parse.h"
#include "lex_pretty.h"
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

#define EXPECT_STR_HAS(haystack, needle) do {                   \
    if (!strstr((haystack), (needle))) {                        \
        fprintf(stderr, "  %s:%d: expected substring '%s' in:\n%s\n", \
                __func__, __LINE__, (needle), (haystack));      \
        fails++;                                                \
    }                                                           \
} while (0)

static LimeLexCompiled *compile_source(const char *src,
                                        LimeLexSpec **spec_out) {
    LimeLexSpec *spec = lime_lex_parse("<test>", src, strlen(src));
    if (!spec || spec->error_count > 0) {
        if (spec) lime_lex_spec_free(spec);
        if (spec_out) *spec_out = NULL;
        return NULL;
    }
    if (lime_lex_resolve_patterns(spec) != 0) {
        lime_lex_spec_free(spec);
        if (spec_out) *spec_out = NULL;
        return NULL;
    }
    LimeLexCompiled *c = lime_lex_compile(spec);
    if (spec_out) *spec_out = spec;
    else lime_lex_spec_free(spec);
    return c;
}

/* ----- sub-tests ----- */

static int test_null_inputs(void) {
    int saved = fails;
    char *out = lime_lex_compiled_to_text(NULL, NULL);
    EXPECT(out == NULL, "NULL/NULL should return NULL");
    free(out);
    if (fails == saved) printf("test_null_inputs: PASS\n");
    return fails - saved;
}

static int test_spec_only_matches_pretty(void) {
    int saved = fails;
    const char *src =
        "%name_prefix Foo.\n"
        "rule a matches /a/ { /* */ }\n";
    LimeLexSpec *spec = lime_lex_parse("<test>", src, strlen(src));
    EXPECT(spec != NULL, "parse NULL");
    if (spec) {
        char *via_pretty = lime_lex_spec_to_text(spec);
        char *via_introspect = lime_lex_compiled_to_text(NULL, spec);
        EXPECT(via_pretty && via_introspect, "alloc failure");
        if (via_pretty && via_introspect) {
            EXPECT(strcmp(via_pretty, via_introspect) == 0,
                   "spec-only introspect should equal pretty:\n"
                   "  pretty: %s\n  introspect: %s",
                   via_pretty, via_introspect);
        }
        free(via_pretty);
        free(via_introspect);
        lime_lex_spec_free(spec);
    }
    if (fails == saved) printf("test_spec_only_matches_pretty: PASS\n");
    return fails - saved;
}

static int test_stats_header_shape(void) {
    int saved = fails;
    const char *src =
        "%name_prefix Foo.\n"
        "%exclusive_state QUOTED.\n"
        "rule kw_if matches /if/ { /* */ }\n"
        "rule ident matches /[a-z]+/ { /* */ }\n"
        "<QUOTED> rule body matches /[^\"]+/ { /* */ }\n";
    LimeLexSpec *spec = NULL;
    LimeLexCompiled *c = compile_source(src, &spec);
    EXPECT(c && spec, "compile failed");
    if (c && spec) {
        char *out = lime_lex_compiled_to_text(c, spec);
        EXPECT(out != NULL, "lime_lex_compiled_to_text returned NULL");
        if (out) {
            EXPECT_STR_HAS(out, "// lime_lex_compiled_to_text:");
            EXPECT_STR_HAS(out, "2 compiled state(s)");
            EXPECT_STR_HAS(out, "0 compile error(s)");
            EXPECT_STR_HAS(out, "// state INITIAL (inclusive):");
            EXPECT_STR_HAS(out, "// state QUOTED (exclusive):");
            /* Stats body lists DFA shape + rule names. */
            EXPECT_STR_HAS(out, "//   dfa: ");
            EXPECT_STR_HAS(out, "kw_if(idx=0)");
            EXPECT_STR_HAS(out, "ident(idx=1)");
            EXPECT_STR_HAS(out, "body(idx=2)");
            /* Body is the spec dump. */
            EXPECT_STR_HAS(out, "%name_prefix Foo.");
            EXPECT_STR_HAS(out, "rule kw_if matches /if/");
            free(out);
        }
        lime_lex_compiled_free(c);
        lime_lex_spec_free(spec);
    }
    if (fails == saved) printf("test_stats_header_shape: PASS\n");
    return fails - saved;
}

/* The full output (header + body) must re-parse to the same
** spec, because the `// ...` lines are comments and the
** tokenizer drops them. */
static int test_round_trip_through_introspect(void) {
    int saved = fails;
    const char *src =
        "%name_prefix Foo.\n"
        "%pattern d /[0-9]/.\n"
        "%state EXPR.\n"
        "rule num matches /{d}+/ { /* */ }\n"
        "<EXPR> rule plus matches /\\+/ { /* */ }\n"
        "%ruleset basic {\n"
        "    rule ws matches /[ \\t]+/ { /* */ }\n"
        "}.\n"
        "%lexer_include basic.\n";

    LimeLexSpec *spec_a = NULL;
    LimeLexCompiled *ca = compile_source(src, &spec_a);
    EXPECT(ca && spec_a, "first compile failed");
    if (!ca || !spec_a) goto cleanup;

    char *t1 = lime_lex_compiled_to_text(ca, spec_a);
    EXPECT(t1 != NULL, "introspect #1 NULL");
    if (!t1) goto cleanup;

    /* Re-parse the introspect output (header is comments, body
    ** is .lex syntax).  The parser's tokenizer drops `//` line
    ** comments; the resulting spec must structurally match. */
    LimeLexSpec *spec_b = lime_lex_parse("<roundtrip>", t1, strlen(t1));
    EXPECT(spec_b != NULL, "reparse NULL");
    EXPECT(spec_b && spec_b->error_count == 0,
           "reparse errors=%d", spec_b ? spec_b->error_count : -1);
    if (!spec_b) { free(t1); goto cleanup; }
    EXPECT(lime_lex_resolve_patterns(spec_b) == 0,
           "resolve on reparsed spec failed");

    /* Re-compile and compare structural shape. */
    LimeLexCompiled *cb = lime_lex_compile(spec_b);
    EXPECT(cb && cb->error_count == 0,
           "second compile failed (%d errors)",
           cb ? cb->error_count : -1);
    if (cb) {
        EXPECT(cb->n_states == ca->n_states,
               "n_states %d vs %d", cb->n_states, ca->n_states);
        int n = cb->n_states < ca->n_states ?
                cb->n_states : ca->n_states;
        for (int i = 0; i < n; i++) {
            const LimeLexCompiledState *a = &ca->states[i];
            const LimeLexCompiledState *b = &cb->states[i];
            EXPECT(strcmp(a->state_name, b->state_name) == 0,
                   "state[%d] name '%s' vs '%s'",
                   i, a->state_name, b->state_name);
            EXPECT(a->exclusive == b->exclusive,
                   "state[%d] exclusive %d vs %d",
                   i, a->exclusive, b->exclusive);
            EXPECT(a->n_rules == b->n_rules,
                   "state[%d] n_rules %d vs %d",
                   i, a->n_rules, b->n_rules);
            int rn = a->n_rules < b->n_rules ?
                     a->n_rules : b->n_rules;
            for (int j = 0; j < rn; j++) {
                EXPECT(a->rule_indices[j] == b->rule_indices[j],
                       "state[%d].rule[%d] gid %d vs %d",
                       i, j, a->rule_indices[j],
                       b->rule_indices[j]);
            }
            /* DFA size should match (minimization is
            ** deterministic on the same regex set). */
            int an = a->dfa ? a->dfa->n_states : 0;
            int bn = b->dfa ? b->dfa->n_states : 0;
            EXPECT(an == bn,
                   "state[%d] dfa.n_states %d vs %d", i, an, bn);
        }
        lime_lex_compiled_free(cb);
    }

    /* And the introspect output of the reparsed spec must
    ** match the original (idempotent). */
    char *t2 = lime_lex_compiled_to_text(ca, spec_b);
    EXPECT(t2 != NULL, "introspect #2 NULL");
    if (t2) {
        EXPECT(strcmp(t1, t2) == 0,
               "introspect not idempotent\n--- t1 ---\n%s\n--- t2 ---\n%s\n",
               t1, t2);
        free(t2);
    }

    lime_lex_spec_free(spec_b);
    free(t1);
cleanup:
    if (ca) lime_lex_compiled_free(ca);
    if (spec_a) lime_lex_spec_free(spec_a);
    if (fails == saved) printf("test_round_trip_through_introspect: PASS\n");
    return fails - saved;
}

/* Empty-DFA states (no applicable rules) must serialize without
** crashing and with a recognisable marker. */
static int test_empty_dfa_state(void) {
    int saved = fails;
    const char *src =
        "%exclusive_state UNUSED.\n"
        "rule a matches /a/ { /* */ }\n";
    LimeLexSpec *spec = NULL;
    LimeLexCompiled *c = compile_source(src, &spec);
    EXPECT(c && spec, "compile failed");
    if (c && spec) {
        char *out = lime_lex_compiled_to_text(c, spec);
        EXPECT(out != NULL, "introspect NULL");
        if (out) {
            EXPECT_STR_HAS(out, "// state UNUSED (exclusive):");
            EXPECT_STR_HAS(out, "//   dfa: <empty>");
            EXPECT_STR_HAS(out, "//   rules: <none>");
            free(out);
        }
        lime_lex_compiled_free(c);
        lime_lex_spec_free(spec);
    }
    if (fails == saved) printf("test_empty_dfa_state: PASS\n");
    return fails - saved;
}

int main(void) {
    test_null_inputs();
    test_spec_only_matches_pretty();
    test_stats_header_shape();
    test_round_trip_through_introspect();
    test_empty_dfa_state();
    if (fails == 0) {
        printf("\ntest_lex_introspect: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr,
            "\ntest_lex_introspect: %d sub-test failure(s)\n", fails);
    return 1;
}
