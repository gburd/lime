/*
** test_diagnostics.c -- Test the RFC 0059 diagnostics API.
**
** Verifies:
**   - ParseTokenName returns correct strings and handles out-of-range
**   - ParseState returns 0 at start, valid state mid-parse, -1 for NULL
**   - ParseExpectedTokens returns correct counts and contents
**   - Two-call pattern (count then fill) works
**
** The generated parser comes from test_diagnostics_grammar.y via a
** meson custom_target.
*/
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_diagnostics_grammar.h"
#include "../../include/lime_error.h"

/* Declarations for the generated parser API (Lemon convention:
** user declares these in their driver). */
void *ParseAlloc(void *(*allocProc)(size_t));
void  ParseFree(void *p, void (*freeProc)(void *));
void  Parse(void *p, int major, int minor);
void  ParseTrace(FILE *f, char *prefix);

/* RFC 0059 API */
const char *ParseTokenName(int tokenCode);
int         ParseState(void *parser);
int         ParseExpectedTokens(int stateno, int *out, int max);

/* Keep the already-existing convenience */
char *ParseExpectedTokensString(void *parser);

static int tests_run    = 0;
static int tests_passed = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        tests_run++;                                                         \
        if (cond) {                                                          \
            tests_passed++;                                                  \
            printf("  %-55s PASS\n", msg);                                   \
        } else {                                                             \
            printf("  %-55s FAIL (%s:%d)\n", msg, __FILE__, __LINE__);       \
        }                                                                    \
    } while (0)

/* Helper: return 1 if token `code` appears in `arr` of length `n`. */
static int contains(const int *arr, int n, int code)
{
    for (int i = 0; i < n; i++) {
        if (arr[i] == code) return 1;
    }
    return 0;
}

static void test_token_name_basics(void)
{
    printf("\nToken name lookup:\n");
    /* The generated header defines symbol codes as e.g. PLUS, MINUS, etc.
    ** Each must map to its name. */
    const char *plus  = ParseTokenName(PLUS);
    const char *minus = ParseTokenName(MINUS);
    const char *integer_n = ParseTokenName(INTEGER);

    CHECK(plus    != NULL && strcmp(plus,    "PLUS")    == 0, "PLUS name lookup");
    CHECK(minus   != NULL && strcmp(minus,   "MINUS")   == 0, "MINUS name lookup");
    CHECK(integer_n != NULL && strcmp(integer_n, "INTEGER") == 0, "INTEGER name lookup");

    /* Edge cases */
    CHECK(ParseTokenName(-1)     == NULL, "ParseTokenName(-1) returns NULL");
    CHECK(ParseTokenName(-42)    == NULL, "ParseTokenName(-42) returns NULL");
    CHECK(ParseTokenName(100000) == NULL, "ParseTokenName(huge) returns NULL");
}

static void test_state_on_fresh_parser(void)
{
    printf("\nParseState on fresh parser:\n");
    void *p = ParseAlloc(malloc);
    int s = ParseState(p);
    CHECK(s == 0, "fresh parser state is 0 (initial state)");

    /* NULL handle */
    CHECK(ParseState(NULL) == -1, "ParseState(NULL) returns -1");

    ParseFree(p, free);
}

static void test_expected_tokens_initial_state(void)
{
    printf("\nExpected tokens at initial state:\n");
    void *p = ParseAlloc(malloc);
    int s = ParseState(p);

    /* Two-call pattern: first call sizes the buffer */
    int n = ParseExpectedTokens(s, NULL, 0);
    CHECK(n > 0, "initial state has at least one expected token");

    int *codes = malloc((size_t)n * sizeof(int));
    int n2 = ParseExpectedTokens(s, codes, n);
    CHECK(n == n2, "two-call count matches");

    /* A program starts with `expr`, which starts with INTEGER or LPAREN.
    ** The expected terminals should include both. */
    CHECK(contains(codes, n, INTEGER), "INTEGER is expected at start");
    CHECK(contains(codes, n, LPAREN),  "LPAREN is expected at start");
    CHECK(!contains(codes, n, PLUS),   "PLUS is NOT expected at start");

    free(codes);
    ParseFree(p, free);
}

static void test_expected_tokens_after_infix_operator(void)
{
    printf("\nExpected tokens after infix operator:\n");
    /* Feed `1 +` and check what's expected. */
    void *p = ParseAlloc(malloc);
    Parse(p, INTEGER, 1);
    Parse(p, PLUS, 0);

    int s = ParseState(p);
    CHECK(s >= 0, "state after `1 +` is valid");

    int n = ParseExpectedTokens(s, NULL, 0);
    int *codes = malloc((size_t)n * sizeof(int));
    ParseExpectedTokens(s, codes, n);

    /* After `1 +` the parser is mid-expression and expects the start of
    ** another expr -- INTEGER or LPAREN. */
    CHECK(contains(codes, n, INTEGER), "INTEGER is expected after `1 +`");
    CHECK(contains(codes, n, LPAREN),  "LPAREN is expected after `1 +`");
    CHECK(!contains(codes, n, PLUS),   "PLUS is NOT expected after `1 +`");

    free(codes);
    ParseFree(p, free);
}

static void test_buffer_truncation(void)
{
    printf("\nBuffer truncation:\n");
    void *p = ParseAlloc(malloc);
    int s = ParseState(p);

    int full_count = ParseExpectedTokens(s, NULL, 0);

    /* Request only 1 slot even though more are available */
    int buf[16];
    int written = ParseExpectedTokens(s, buf, 1);
    CHECK(written == full_count,
          "return value is total count even when buffer too small");
    /* First slot filled */
    CHECK(buf[0] >= 0, "first slot populated on truncated call");

    ParseFree(p, free);
}

static void test_string_helper_still_works(void)
{
    printf("\nBackward compatibility (ParseExpectedTokensString):\n");
    void *p = ParseAlloc(malloc);
    Parse(p, INTEGER, 1);
    Parse(p, PLUS, 0);

    char *s = ParseExpectedTokensString(p);
    CHECK(s != NULL, "ParseExpectedTokensString returns non-NULL mid-parse");
    if (s) {
        CHECK(strstr(s, "INTEGER") != NULL,
              "ParseExpectedTokensString contains INTEGER");
        free(s);
    }

    ParseFree(p, free);
}

static void test_invalid_state(void)
{
    printf("\nInvalid state handling:\n");
    int buf[4];
    CHECK(ParseExpectedTokens(-1, buf, 4)   == 0, "state -1 returns 0");
    CHECK(ParseExpectedTokens(-999, NULL, 0) == 0, "state -999 returns 0");
    /* Very large state is out of range */
    CHECK(ParseExpectedTokens(100000, NULL, 0) == 0, "state 100000 returns 0");
}

static void test_lime_error_helpers(void)
{
    printf("\nLimeError helpers:\n");
    LimeError *errs = NULL;
    CHECK(lime_error_count(NULL) == 0, "count(NULL) == 0");

    errs = lime_error_append(errs, "first error", "INTEGER, LPAREN",
                             1, 5, "test.sql");
    CHECK(errs != NULL, "append to empty list returns new head");
    CHECK(lime_error_count(errs) == 1, "count after first append == 1");
    CHECK(errs->line == 1 && errs->column == 5, "line/column stored");
    CHECK(strcmp(errs->message, "first error") == 0, "message duplicated");
    CHECK(strcmp(errs->expected, "INTEGER, LPAREN") == 0, "expected duplicated");
    CHECK(strcmp(errs->filename, "test.sql") == 0, "filename borrowed");

    errs = lime_error_append(errs, "second error", NULL, 2, 1, "test.sql");
    CHECK(lime_error_count(errs) == 2, "count after second append == 2");
    CHECK(errs->next->message != NULL, "second message allocated");
    CHECK(errs->next->expected == NULL, "NULL expected stays NULL");

    /* NULL message + NULL expected is fine */
    errs = lime_error_append(errs, NULL, NULL, 3, 1, NULL);
    CHECK(lime_error_count(errs) == 3, "count after third append == 3");

    lime_error_free(errs);
    /* double free guard: free(NULL) safe */
    lime_error_free(NULL);
    CHECK(1, "lime_error_free(NULL) did not crash");
}

int main(void)
{
    printf("RFC 0059 Diagnostics API Tests\n");
    printf("==============================\n");

    test_token_name_basics();
    test_state_on_fresh_parser();
    test_expected_tokens_initial_state();
    test_expected_tokens_after_infix_operator();
    test_buffer_truncation();
    test_string_helper_still_works();
    test_invalid_state();
    test_lime_error_helpers();

    printf("\n==============================\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
