/*
** tests/test_lex_dfa_min.c -- M2.4 unit test for DFA
** minimization.
**
** Asserts:
**   1. The minimized DFA accepts exactly the same language as
**      the original (semantic preservation).
**   2. The minimized DFA has at most as many states as the
**      original (no growth).
**   3. For grammars with known-redundant states, minimization
**      produces a strict reduction.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lex_dfa.h"
#include "lex_nfa.h"
#include "lex_regex.h"

static int fails = 0;

#define EXPECT(cond, ...) do {                                    \
    if (!(cond)) {                                                \
        fprintf(stderr, "  %s:%d: ", __func__, __LINE__);         \
        fprintf(stderr, __VA_ARGS__);                             \
        fprintf(stderr, "\n");                                    \
        fails++;                                                  \
    }                                                             \
} while (0)

static LimeDfa *build_dfa(const char *regex_src) {
    char *err = NULL;
    LimeReNode *re = lime_lex_regex_parse(regex_src, &err);
    free(err);
    if (!re) return NULL;
    LimeNfa *nfa = lime_lex_nfa_from_regex(re);
    LimeDfa *dfa = nfa ? lime_lex_nfa_to_dfa(nfa) : NULL;
    lime_lex_nfa_free(nfa);
    lime_lex_regex_free(re);
    return dfa;
}

struct case_t {
    const char *input;
    int         expect;
};

static int check_equiv(const char *label,
                       const char *regex_src,
                       const struct case_t *cases, int n_cases) {
    int saved = fails;
    LimeDfa *orig = build_dfa(regex_src);
    if (!orig) { fprintf(stderr, "  %s: build_dfa returned NULL\n", label);
                 fails++; return fails - saved; }
    LimeDfa *mini = lime_lex_dfa_minimize(orig);
    if (!mini) { fprintf(stderr, "  %s: minimize returned NULL\n", label);
                 lime_lex_dfa_free(orig); fails++; return fails - saved; }

    int orig_n = lime_lex_dfa_state_count(orig);
    int mini_n = lime_lex_dfa_state_count(mini);

    EXPECT(mini_n <= orig_n,
           "%s: minimize grew states (%d -> %d)", label, orig_n, mini_n);

    for (int i = 0; i < n_cases; i++) {
        int o = lime_lex_dfa_match(orig, cases[i].input, strlen(cases[i].input));
        int m = lime_lex_dfa_match(mini, cases[i].input, strlen(cases[i].input));
        EXPECT(o == m,
               "%s on \"%s\": orig=%d mini=%d", label, cases[i].input, o, m);
        EXPECT(m == cases[i].expect,
               "%s on \"%s\": got %d want %d",
               label, cases[i].input, m, cases[i].expect);
    }

    if (fails == saved) {
        printf("%s: PASS  (%d -> %d states)\n", label, orig_n, mini_n);
    }

    lime_lex_dfa_free(mini);
    lime_lex_dfa_free(orig);
    return fails - saved;
}

static int test_simple_literal(void) {
    struct case_t cases[] = {
        {"a", 1}, {"b", 0}, {"", 0}, {"aa", 0},
    };
    return check_equiv("simple_literal", "a",
                       cases, sizeof(cases)/sizeof(cases[0]));
}

static int test_alt_with_redundancy(void) {
    struct case_t cases[] = {
        {"a", 1}, {"aa", 1}, {"aaaa", 1}, {"", 0}, {"b", 0},
    };
    return check_equiv("alt_with_redundancy", "(a|a)+",
                       cases, sizeof(cases)/sizeof(cases[0]));
}

static int test_identifier(void) {
    struct case_t cases[] = {
        {"foo",     1}, {"_bar",   1}, {"a1b2c3", 1},
        {"123foo",  0}, {"",       0}, {"foo bar", 0},
    };
    return check_equiv("identifier", "[A-Za-z_][A-Za-z0-9_]*",
                       cases, sizeof(cases)/sizeof(cases[0]));
}

static int test_signed_int(void) {
    struct case_t cases[] = {
        {"42",   1}, {"-7",  1}, {"+0", 1},
        {"abc",  0}, {"--3", 0}, {"",   0},
    };
    return check_equiv("signed_int", "[+-]?[0-9]+",
                       cases, sizeof(cases)/sizeof(cases[0]));
}

static int test_quoted_string(void) {
    struct case_t cases[] = {
        {"\"hello\"",                 1},
        {"\"with \\\"quote\\\"\"",    1},
        {"\"\"",                      1},
        {"\"unterminated",            0},
        {"no quotes",                 0},
    };
    return check_equiv("quoted_string", "\"([^\"\\\\]|\\\\.)*\"",
                       cases, sizeof(cases)/sizeof(cases[0]));
}

static int test_repetition(void) {
    struct case_t cases[] = {
        {"aaa",   1}, {"aa",    0}, {"aaaa",  0},
    };
    return check_equiv("repetition_exact", "a{3}",
                       cases, sizeof(cases)/sizeof(cases[0]));
}

static int test_known_reduction(void) {
    /* `a|b` produces a DFA with two accept states (one reachable
    ** via 'a', one via 'b').  Both have no outgoing edges and
    ** the same accept status, so they're equivalent under
    ** minimization and should merge into one. */
    int saved = fails;
    LimeDfa *orig = build_dfa("a|b");
    LimeDfa *mini = lime_lex_dfa_minimize(orig);
    int o = lime_lex_dfa_state_count(orig);
    int m = lime_lex_dfa_state_count(mini);
    EXPECT(m < o, "a|b: orig=%d mini=%d (expected strict reduction "
                  "by merging the two accept states)", o, m);
    if (fails == saved) {
        printf("test_known_reduction: PASS  (%d -> %d states)\n", o, m);
    }
    lime_lex_dfa_free(mini);
    lime_lex_dfa_free(orig);
    return fails - saved;
}

int main(void) {
    test_simple_literal();
    test_alt_with_redundancy();
    test_identifier();
    test_signed_int();
    test_quoted_string();
    test_repetition();
    test_known_reduction();
    if (fails == 0) {
        printf("\ntest_lex_dfa_min: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr, "\ntest_lex_dfa_min: %d sub-test failure(s)\n", fails);
    return 1;
}
