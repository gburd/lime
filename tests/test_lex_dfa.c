/*
** tests/test_lex_dfa.c -- M2.3 unit test for subset construction.
**
** Pipeline tested: regex -> AST (M2.1) -> NFA (M2.2) -> DFA
** (this commit).  Each test parses a regex, builds the DFA, and
** asserts the DFA matches the expected inputs.  DFA acceptance
** must agree with the NFA simulator's acceptance for every
** test in test_lex_nfa.c (regression by construction).
**
** Includes a state-count audit check: each DFA stays within
** the audit's "10x rule count" rule of thumb from
** docs/LEXER_DESIGN.md.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lex_dfa.h"
#include "lex_nfa.h"
#include "lex_regex.h"

static int fails = 0;

#define MATCH(regex, input, expect) do {                            \
    char *err = NULL;                                               \
    LimeReNode *re = lime_lex_regex_parse((regex), &err);           \
    if (!re) {                                                      \
        fprintf(stderr, "  parse(%s) failed: %s\n",                 \
                (regex), err ? err : "(no msg)");                   \
        free(err);                                                  \
        fails++; break;                                             \
    }                                                               \
    LimeNfa *nfa = lime_lex_nfa_from_regex(re);                     \
    if (!nfa) {                                                     \
        fprintf(stderr, "  nfa(%s) returned NULL\n", (regex));      \
        lime_lex_regex_free(re);                                    \
        fails++; break;                                             \
    }                                                               \
    LimeDfa *dfa = lime_lex_nfa_to_dfa(nfa);                        \
    if (!dfa) {                                                     \
        fprintf(stderr, "  dfa(%s) returned NULL\n", (regex));      \
        lime_lex_nfa_free(nfa);                                     \
        lime_lex_regex_free(re);                                    \
        fails++; break;                                             \
    }                                                               \
    int got = lime_lex_dfa_match(dfa, (input), strlen(input));      \
    if (got != (expect)) {                                          \
        fprintf(stderr, "  /%s/ on \"%s\": got %d, want %d\n",      \
                (regex), (input), got, (expect));                   \
        fails++;                                                    \
    }                                                               \
    lime_lex_dfa_free(dfa);                                         \
    lime_lex_nfa_free(nfa);                                         \
    lime_lex_regex_free(re);                                        \
} while (0)

static int test_basics(void) {
    int saved = fails;
    MATCH("a",   "a",   1);
    MATCH("a",   "b",   0);
    MATCH("a",   "",    0);
    MATCH("abc", "abc", 1);
    MATCH("abc", "abd", 0);
    MATCH("a*",  "",    1);
    MATCH("a*",  "aaa", 1);
    MATCH("a+",  "",    0);
    MATCH("a+",  "a",   1);
    if (fails == saved) printf("test_basics: PASS\n");
    return fails - saved;
}

static int test_alt(void) {
    int saved = fails;
    MATCH("a|b",      "a",    1);
    MATCH("a|b",      "b",    1);
    MATCH("a|b",      "c",    0);
    MATCH("(ab|cd)+", "abcd", 1);
    MATCH("(ab|cd)+", "ab",   1);
    MATCH("(ab|cd)+", "abab", 1);
    MATCH("(ab|cd)+", "ac",   0);
    if (fails == saved) printf("test_alt: PASS\n");
    return fails - saved;
}

static int test_classes(void) {
    int saved = fails;
    MATCH("[a-z]+",                 "hello", 1);
    MATCH("[a-z]+",                 "Hello", 0);
    MATCH("[A-Za-z_][A-Za-z0-9_]*", "foo23", 1);
    MATCH("[A-Za-z_][A-Za-z0-9_]*", "23foo", 0);
    MATCH("[^0-9]+",                "abc",   1);
    MATCH("[^0-9]+",                "ab1",   0);
    if (fails == saved) printf("test_classes: PASS\n");
    return fails - saved;
}

static int test_repetition(void) {
    int saved = fails;
    MATCH("a{3}",   "aaa",   1);
    MATCH("a{3}",   "aa",    0);
    MATCH("a{3}",   "aaaa",  0);
    MATCH("a{2,4}", "aa",    1);
    MATCH("a{2,4}", "aaaa",  1);
    MATCH("a{2,4}", "a",     0);
    MATCH("a{2,4}", "aaaaa", 0);
    MATCH("a{2,}",  "aa",    1);
    MATCH("a{2,}",  "a",     0);
    if (fails == saved) printf("test_repetition: PASS\n");
    return fails - saved;
}

static int test_realistic(void) {
    int saved = fails;
    MATCH("0x[0-9A-Fa-f]+",  "0xdeadBEEF", 1);
    MATCH("0x[0-9A-Fa-f]+",  "0x",         0);
    MATCH("0x[0-9A-Fa-f]+",  "0xZZ",       0);

    MATCH("[+-]?[0-9]+", "42",  1);
    MATCH("[+-]?[0-9]+", "-7",  1);
    MATCH("[+-]?[0-9]+", "+0",  1);
    MATCH("[+-]?[0-9]+", "abc", 0);

    MATCH("\"([^\"\\\\]|\\\\.)*\"",  "\"hello\"",            1);
    MATCH("\"([^\"\\\\]|\\\\.)*\"",  "\"with \\\"quote\\\"\"", 1);
    MATCH("\"([^\"\\\\]|\\\\.)*\"",  "\"unterminated",       0);

    MATCH("[ \\t\\r\\n]+", "  \t\n", 1);
    MATCH("[ \\t\\r\\n]+", "x",      0);
    if (fails == saved) printf("test_realistic: PASS\n");
    return fails - saved;
}

static int test_state_count_bounds(void) {
    int saved = fails;
    struct { const char *regex; int max_states; } cases[] = {
        { "a",                              4 },
        { "[A-Za-z_][A-Za-z0-9_]*",        16 },
        { "0x[0-9A-Fa-f]+",                16 },
        { "[+-]?[0-9]+",                   16 },
        { "\"([^\"\\\\]|\\\\.)*\"",        24 },
        { "(ab|cd|ef)+",                   16 },
    };
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        char *err = NULL;
        LimeReNode *re = lime_lex_regex_parse(cases[i].regex, &err);
        free(err);
        LimeNfa *nfa = lime_lex_nfa_from_regex(re);
        LimeDfa *dfa = lime_lex_nfa_to_dfa(nfa);
        int n = lime_lex_dfa_state_count(dfa);
        if (n > cases[i].max_states) {
            fprintf(stderr,
                "  /%s/: %d DFA states exceeds bound %d\n",
                cases[i].regex, n, cases[i].max_states);
            fails++;
        }
        lime_lex_dfa_free(dfa);
        lime_lex_nfa_free(nfa);
        lime_lex_regex_free(re);
    }
    if (fails == saved) printf("test_state_count_bounds: PASS\n");
    return fails - saved;
}

int main(void) {
    test_basics();
    test_alt();
    test_classes();
    test_repetition();
    test_realistic();
    test_state_count_bounds();
    if (fails == 0) {
        printf("\ntest_lex_dfa: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr, "\ntest_lex_dfa: %d sub-test failure(s)\n", fails);
    return 1;
}
