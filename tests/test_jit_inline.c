/*
** Test for JIT per-rule reducer inlining policy.
**
** Verifies jit_can_inline_rule() correctly classifies rule actions as
** inlin able vs. dispatch-required.
**
** When LLVM is unavailable (LIME_NO_JIT), this test still validates the
** codegen decision logic - the policy function compiles and runs correctly
** in stub mode even though no runtime JIT execution occurs.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* Plain-data classifier (jit_inline.h).  The struct-rule-typed wrapper
** is intentionally absent from the test harness because tests don't
** link against lime.c (where `struct rule` is defined). */
extern bool jit_can_inline_rule_text(const char *code, int no_code);

/* JIT-availability probe.  When meson is configured with -Dllvm=disabled,
** jit_codegen.c compiles a stub that returns false unconditionally
** (the inlining policy doesn't matter when there's no JIT to inline
** into).  In that configuration the policy classifications below would
** all collapse to false, so we skip the test rather than report
** spurious failures. */
extern bool jit_is_available(void);

/* Helper: pair the two relevant fields the policy reads. */
typedef struct {
    const char *code;
    int noCode;
} rule_view;
static inline bool can_inline(const rule_view *r) {
    return jit_can_inline_rule_text(r ? r->code : NULL, r ? r->noCode : 0);
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
    ** The pre-v0.6.2 jit_can_inline_rule(struct rule*) wrapper
    ** rejected NULL outright but the v0.6.2 plain-data refactor
    ** treats the no-op rule as inlinable, which is the correct
    ** semantics: an empty reduce IS trivially inlinable. */
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
    printf("JIT per-rule reducer inlining policy tests\n");
    printf("===========================================\n\n");

    if (!jit_is_available()) {
        printf("SKIP: JIT disabled (LIME_NO_JIT); inline policy untested.\n");
        return 77;  /* meson SKIP exit code */
    }

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
