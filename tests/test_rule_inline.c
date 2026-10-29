/*
** Test for the per-rule action-body inlining classifier
** (src/rule_inline.c), used by the Rust emitter.
**
** Verifies rule_can_inline_text() correctly classifies rule actions as
** inlinable vs. dispatch-required.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "rule_inline.h"

/* Helper: pair the two relevant fields the policy reads. */
typedef struct {
    const char *code;
    int noCode;
} rule_view;
static inline bool can_inline(const rule_view *r) {
    return rule_can_inline_text(r ? r->code : NULL, r ? r->noCode : 0);
}

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-60s ", #name); \
    fflush(stdout); \
    if (test_##name()) { \
        tests_passed++; \
        printf("PASS\n"); \
    } else { \
        printf("FAIL\n"); \
    } \
} while (0)

/* ------------------------------------------------------------------ */
/*  Test cases                                                         */
/* ------------------------------------------------------------------ */

static bool test_null_rule(void) {
    /* NULL rule semantics: a NULL rule_view (or one with NULL code
    ** and noCode==0) classifies as "no action body" -- inlinable
    ** as a no-op.  This matches the empty_action case below.
    ** An empty reduce IS trivially inlinable. */
    return can_inline(NULL);
}

static bool test_empty_action(void) {
    rule_view r = {0};
    r.noCode = 1;
    return can_inline(&r);  /* Empty actions are inlinable */
}

static bool test_passthrough_simple(void) {
    rule_view r = {0};
    r.code = "A = B;";
    return can_inline(&r);  /* Simple passthrough is inlinable */
}

static bool test_passthrough_whitespace(void) {
    rule_view r = {0};
    r.code = "  A  =  B  ;  ";
    return can_inline(&r);  /* Whitespace-padded passthrough */
}

static bool test_arithmetic_simple(void) {
    rule_view r = {0};
    r.code = "A = B + C;";
    return can_inline(&r);  /* Simple arithmetic expression */
}

static bool test_function_call(void) {
    rule_view r = {0};
    r.code = "A = malloc(sizeof(int));";
    return !can_inline(&r);  /* Function calls not inlinable */
}

static bool test_goto_keyword(void) {
    rule_view r = {0};
    r.code = "if (x) goto error;";
    return !can_inline(&r);  /* goto keyword blocks inlining */
}

static bool test_parse_callback(void) {
    rule_view r = {0};
    r.code = "Parse_Accept(ctx);";
    return !can_inline(&r);  /* Parse_* callbacks not inlinable */
}

static bool test_multiple_statements(void) {
    rule_view r = {0};
    r.code = "x = 1; y = 2; z = 3;";
    return !can_inline(&r);  /* Multiple statements not inlinable */
}

static bool test_block_statement(void) {
    rule_view r = {0};
    r.code = "{ A = B; C = D; }";
    return !can_inline(&r);  /* Block statements not inlinable */
}

static bool test_control_flow(void) {
    rule_view r = {0};
    r.code = "if (cond) A = B; else A = C;";
    return !can_inline(&r);  /* Control flow not inlinable */
}

static bool test_very_long_action(void) {
    rule_view r = {0};
    /* Create a 250-char simple expression (over the 200-char limit) */
    static char long_code[300];
    memset(long_code, 'x', 250);
    long_code[0] = 'A';
    long_code[1] = '=';
    long_code[249] = ';';
    long_code[250] = '\0';
    r.code = long_code;
    return !can_inline(&r);  /* Over length limit */
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("Per-rule action-body inlining classifier tests\n");
    printf("==============================================\n\n");

    TEST(null_rule);
    TEST(empty_action);
    TEST(passthrough_simple);
    TEST(passthrough_whitespace);
    TEST(arithmetic_simple);
    TEST(function_call);
    TEST(goto_keyword);
    TEST(parse_callback);
    TEST(multiple_statements);
    TEST(block_statement);
    TEST(control_flow);
    TEST(very_long_action);

    printf("\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
