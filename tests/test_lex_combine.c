/*
** tests/test_lex_combine.c -- M2.5a unit test for multi-rule
** NFA combination + accept-rule tagging.
**
** Builds individual rule NFAs, tags each with a rule id,
** combines them, subset-constructs into a DFA, and verifies:
**   - The DFA accepts the same languages as the union of inputs.
**   - On overlap, the DFA's accept_rule is the LOWEST rule id
**     (declaration-order priority).
**   - States that accept exactly one rule report that rule.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lex_dfa.h"
#include "lex_nfa.h"
#include "lex_regex.h"

static int fails = 0;

#define EXPECT(cond, ...) do {                                  \
    if (!(cond)) {                                              \
        fprintf(stderr, "  %s:%d: ", __func__, __LINE__);       \
        fprintf(stderr, __VA_ARGS__);                           \
        fprintf(stderr, "\n");                                  \
        fails++;                                                \
    }                                                           \
} while (0)

/* Build a single-rule NFA tagged with rule_id. */
static LimeNfa *build_rule_nfa(const char *regex_src, int rule_id) {
    char *err = NULL;
    LimeReNode *re = lime_lex_regex_parse(regex_src, &err);
    free(err);
    if (!re) return NULL;
    LimeNfa *nfa = lime_lex_nfa_from_regex(re);
    lime_lex_regex_free(re);
    if (nfa) nfa->states[nfa->accept].accept_rule = rule_id;
    return nfa;
}

/* Drive the DFA over an input and return either the accept_rule
** of the final state, or -1 if the input was not accepted. */
static int dfa_match_rule(const LimeDfa *dfa,
                          const char *bytes, size_t n) {
    int s = dfa->start;
    for (size_t i = 0; i < n; i++) {
        s = dfa->states[s].trans[(unsigned char)bytes[i]];
        if (s < 0) return -1;
    }
    return dfa->states[s].is_accept ? dfa->states[s].accept_rule : -1;
}

/* ----- sub-tests ----- */

static int test_two_disjoint_rules(void) {
    int saved = fails;
    /* Rule 0: identifier.  Rule 1: integer literal.  They
    ** match disjoint inputs, so accept_rule unambiguously
    ** identifies which rule wins. */
    LimeNfa *r0 = build_rule_nfa("[A-Za-z_][A-Za-z0-9_]*", 0);
    LimeNfa *r1 = build_rule_nfa("[0-9]+", 1);
    LimeNfa *inputs[] = { r0, r1 };
    LimeNfa *combined = lime_lex_nfa_combine(inputs, 2);
    EXPECT(combined != NULL, "combine returned NULL");
    if (!combined) return fails - saved;
    LimeDfa *dfa = lime_lex_nfa_to_dfa(combined);
    EXPECT(dfa != NULL, "subset construction returned NULL");
    if (!dfa) { lime_lex_nfa_free(combined); return fails - saved; }

    EXPECT(dfa_match_rule(dfa, "foo", 3)   == 0, "foo should match rule 0");
    EXPECT(dfa_match_rule(dfa, "_bar", 4)  == 0, "_bar should match rule 0");
    EXPECT(dfa_match_rule(dfa, "42", 2)    == 1, "42 should match rule 1");
    EXPECT(dfa_match_rule(dfa, "0", 1)     == 1, "0 should match rule 1");
    EXPECT(dfa_match_rule(dfa, "", 0)      == -1, "empty should reject");
    EXPECT(dfa_match_rule(dfa, "1foo", 4)  == -1, "1foo should reject");

    lime_lex_dfa_free(dfa);
    lime_lex_nfa_free(combined);
    if (fails == saved) printf("test_two_disjoint_rules: PASS\n");
    return fails - saved;
}

static int test_overlap_priority(void) {
    int saved = fails;
    /* Rule 0: keyword "if".  Rule 1: identifier (which also matches "if").
    ** On the input "if", BOTH rules match.  Rule 0 (declared
    ** first) must win. */
    LimeNfa *r0 = build_rule_nfa("if",                       0);
    LimeNfa *r1 = build_rule_nfa("[A-Za-z_][A-Za-z0-9_]*",   1);
    LimeNfa *inputs[] = { r0, r1 };
    LimeNfa *combined = lime_lex_nfa_combine(inputs, 2);
    EXPECT(combined != NULL, "combine NULL");
    if (!combined) return fails - saved;
    LimeDfa *dfa = lime_lex_nfa_to_dfa(combined);
    EXPECT(dfa != NULL, "subset NULL");
    if (!dfa) { lime_lex_nfa_free(combined); return fails - saved; }

    /* "if" matches both -> rule 0 (priority). */
    EXPECT(dfa_match_rule(dfa, "if", 2)  == 0,
           "if should match rule 0 (keyword priority)");
    /* "i" matches only rule 1 (identifier). */
    EXPECT(dfa_match_rule(dfa, "i", 1)   == 1,
           "i should match rule 1 (identifier only)");
    /* "ifs" matches only rule 1 (identifier). */
    EXPECT(dfa_match_rule(dfa, "ifs", 3) == 1,
           "ifs should match rule 1");

    lime_lex_dfa_free(dfa);
    lime_lex_nfa_free(combined);
    if (fails == saved) printf("test_overlap_priority: PASS\n");
    return fails - saved;
}

static int test_three_rules(void) {
    int saved = fails;
    /* Rule 0: keyword "while".  Rule 1: keyword "if".
    ** Rule 2: identifier. */
    LimeNfa *r0 = build_rule_nfa("while",                    0);
    LimeNfa *r1 = build_rule_nfa("if",                       1);
    LimeNfa *r2 = build_rule_nfa("[A-Za-z_][A-Za-z0-9_]*",   2);
    LimeNfa *inputs[] = { r0, r1, r2 };
    LimeNfa *combined = lime_lex_nfa_combine(inputs, 3);
    EXPECT(combined != NULL, "combine NULL");
    if (!combined) return fails - saved;
    LimeDfa *dfa = lime_lex_nfa_to_dfa(combined);
    EXPECT(dfa != NULL, "subset NULL");
    if (!dfa) { lime_lex_nfa_free(combined); return fails - saved; }

    EXPECT(dfa_match_rule(dfa, "while", 5)  == 0, "while -> rule 0");
    EXPECT(dfa_match_rule(dfa, "if",    2)  == 1, "if -> rule 1");
    EXPECT(dfa_match_rule(dfa, "x",     1)  == 2, "x -> rule 2");
    EXPECT(dfa_match_rule(dfa, "whilex", 6) == 2, "whilex -> rule 2");

    lime_lex_dfa_free(dfa);
    lime_lex_nfa_free(combined);
    if (fails == saved) printf("test_three_rules: PASS\n");
    return fails - saved;
}

int main(void) {
    test_two_disjoint_rules();
    test_overlap_priority();
    test_three_rules();
    if (fails == 0) {
        printf("\ntest_lex_combine: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr, "\ntest_lex_combine: %d sub-test failure(s)\n", fails);
    return 1;
}
