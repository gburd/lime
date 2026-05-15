/*
** tests/test_lex_nfa.c -- M2.2 unit test for Thompson's NFA
** construction.
**
** Tests the NFA simulator (lime_lex_nfa_match) against a variety
** of regex patterns and inputs.  This is also a regression
** suite for the regex parser + NFA builder pipeline: any change
** to either that breaks well-known matches will fail here.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lex_nfa.h"
#include "lex_regex.h"

static int fails = 0;

#define CHECK(regex_src, input, expect) do {                       \
    char *err = NULL;                                              \
    LimeReNode *re = lime_lex_regex_parse((regex_src), &err);      \
    if (!re) {                                                     \
        fprintf(stderr, "  parse(\"%s\") failed: %s\n",            \
                (regex_src), err ? err : "(no msg)");              \
        free(err);                                                 \
        fails++;                                                   \
        break;                                                     \
    }                                                              \
    LimeNfa *nfa = lime_lex_nfa_from_regex(re);                    \
    if (!nfa) {                                                    \
        fprintf(stderr, "  nfa(\"%s\") returned NULL\n",           \
                (regex_src));                                      \
        lime_lex_regex_free(re);                                   \
        fails++;                                                   \
        break;                                                     \
    }                                                              \
    int got = lime_lex_nfa_match(nfa, (input), strlen(input));     \
    if (got != (expect)) {                                         \
        fprintf(stderr,                                            \
            "  /%s/ on \"%s\": got %d, want %d\n",                 \
            (regex_src), (input), got, (expect));                  \
        fails++;                                                   \
    }                                                              \
    lime_lex_nfa_free(nfa);                                        \
    lime_lex_regex_free(re);                                       \
} while (0)

/* ----- sub-tests ----- */

static int test_literal(void) {
    int saved = fails;
    CHECK("a",   "a",   1);
    CHECK("a",   "b",   0);
    CHECK("a",   "",    0);
    CHECK("a",   "aa",  0);
    CHECK("abc", "abc", 1);
    CHECK("abc", "abd", 0);
    CHECK("abc", "abcd",0);
    if (fails == saved) printf("test_literal: PASS\n");
    return fails - saved;
}

static int test_alt(void) {
    int saved = fails;
    CHECK("a|b", "a", 1);
    CHECK("a|b", "b", 1);
    CHECK("a|b", "c", 0);
    CHECK("ab|cd", "ab", 1);
    CHECK("ab|cd", "cd", 1);
    CHECK("ab|cd", "ac", 0);
    if (fails == saved) printf("test_alt: PASS\n");
    return fails - saved;
}

static int test_star_plus_question(void) {
    int saved = fails;
    /* a* */
    CHECK("a*", "",     1);
    CHECK("a*", "a",    1);
    CHECK("a*", "aaaa", 1);
    CHECK("a*", "b",    0);
    /* a+ */
    CHECK("a+", "",     0);
    CHECK("a+", "a",    1);
    CHECK("a+", "aaaa", 1);
    /* a? */
    CHECK("a?", "",  1);
    CHECK("a?", "a", 1);
    CHECK("a?", "aa", 0);
    /* (ab)* */
    CHECK("(ab)*", "",       1);
    CHECK("(ab)*", "ab",     1);
    CHECK("(ab)*", "abab",   1);
    CHECK("(ab)*", "ababab", 1);
    CHECK("(ab)*", "aba",    0);
    if (fails == saved) printf("test_star_plus_question: PASS\n");
    return fails - saved;
}

static int test_repeat(void) {
    int saved = fails;
    CHECK("a{3}",   "aa",   0);
    CHECK("a{3}",   "aaa",  1);
    CHECK("a{3}",   "aaaa", 0);
    CHECK("a{2,4}", "a",    0);
    CHECK("a{2,4}", "aa",   1);
    CHECK("a{2,4}", "aaa",  1);
    CHECK("a{2,4}", "aaaa", 1);
    CHECK("a{2,4}", "aaaaa",0);
    CHECK("a{2,}",  "aa",   1);
    CHECK("a{2,}",  "aaaa", 1);
    CHECK("a{2,}",  "a",    0);
    if (fails == saved) printf("test_repeat: PASS\n");
    return fails - saved;
}

static int test_char_class(void) {
    int saved = fails;
    CHECK("[a-z]", "g", 1);
    CHECK("[a-z]", "G", 0);
    CHECK("[a-z]", "0", 0);
    CHECK("[A-Za-z_]", "_", 1);
    CHECK("[A-Za-z_]", "Z", 1);
    CHECK("[A-Za-z_]", "1", 0);
    CHECK("[^0-9]", "x", 1);
    CHECK("[^0-9]", "5", 0);
    if (fails == saved) printf("test_char_class: PASS\n");
    return fails - saved;
}

static int test_wildcard(void) {
    int saved = fails;
    CHECK(".",   "x",  1);
    CHECK(".",   "\n", 0);   /* . excludes newline */
    CHECK(".+",  "abc", 1);
    CHECK(".+",  "",   0);
    if (fails == saved) printf("test_wildcard: PASS\n");
    return fails - saved;
}

static int test_realistic_patterns(void) {
    int saved = fails;
    /* Identifier */
    CHECK("[A-Za-z_][A-Za-z0-9_]*", "foo",      1);
    CHECK("[A-Za-z_][A-Za-z0-9_]*", "_bar23",   1);
    CHECK("[A-Za-z_][A-Za-z0-9_]*", "23foo",    0);
    CHECK("[A-Za-z_][A-Za-z0-9_]*", "",         0);
    CHECK("[A-Za-z_][A-Za-z0-9_]*", "fo o",     0);

    /* Hex literal */
    CHECK("0x[0-9A-Fa-f]+", "0xdeadBEEF", 1);
    CHECK("0x[0-9A-Fa-f]+", "0x",         0);
    CHECK("0x[0-9A-Fa-f]+", "0xZZ",       0);
    CHECK("0x[0-9A-Fa-f]+", "0x1g",       0);

    /* Decimal integer with optional sign */
    CHECK("[+-]?[0-9]+", "42",   1);
    CHECK("[+-]?[0-9]+", "-7",   1);
    CHECK("[+-]?[0-9]+", "+0",   1);
    CHECK("[+-]?[0-9]+", "abc",  0);
    CHECK("[+-]?[0-9]+", "--3",  0);

    /* Quoted string */
    CHECK("\"([^\"\\\\]|\\\\.)*\"", "\"hello\"",            1);
    CHECK("\"([^\"\\\\]|\\\\.)*\"", "\"with \\\"quote\\\"\"", 1);
    CHECK("\"([^\"\\\\]|\\\\.)*\"", "\"unterminated",       0);
    CHECK("\"([^\"\\\\]|\\\\.)*\"", "no quotes",            0);

    /* Whitespace run */
    CHECK("[ \\t\\r\\n]+", " \t",   1);
    CHECK("[ \\t\\r\\n]+", "x",     0);
    CHECK("[ \\t\\r\\n]+", "",      0);

    if (fails == saved) printf("test_realistic_patterns: PASS\n");
    return fails - saved;
}

static int test_alt_in_concat(void) {
    int saved = fails;
    CHECK("a(b|c)d", "abd", 1);
    CHECK("a(b|c)d", "acd", 1);
    CHECK("a(b|c)d", "ad",  0);
    CHECK("a(b|c)d", "abcd",0);
    if (fails == saved) printf("test_alt_in_concat: PASS\n");
    return fails - saved;
}

int main(void) {
    test_literal();
    test_alt();
    test_star_plus_question();
    test_repeat();
    test_char_class();
    test_wildcard();
    test_alt_in_concat();
    test_realistic_patterns();
    if (fails == 0) {
        printf("\ntest_lex_nfa: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr, "\ntest_lex_nfa: %d sub-test failure(s)\n", fails);
    return 1;
}
