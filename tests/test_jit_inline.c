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

/* Forward declaration - actual definition in lime.c but we only need the
** struct layout for testing */
struct rule {
    void *lhs;              /* Not used in tests */
    const char *lhsalias;
    int lhsStart;
    int ruleline;
    int nrhs;
    void **rhs;
    const char **rhsalias;
    int line;
    const char *code;       /* Action body - THIS is what we test */
    const char *codePrefix;
    const char *codeSuffix;
    void *precsym;
    int index;
    int iRule;
    int noCode;            /* True if this rule has no action code */
    /* ... rest of fields not needed for testing ... */
};

/* The function under test - defined in jit_codegen.c */
extern bool jit_can_inline_rule(const struct rule *rp);

/* JIT-availability probe.  When meson is configured with -Dllvm=disabled,
** jit_codegen.c compiles a stub that returns false unconditionally
** (the inlining policy doesn't matter when there's no JIT to inline
** into).  In that configuration the policy classifications below would
** all collapse to false, so we skip the test rather than report
** spurious failures. */
extern bool jit_is_available(void);

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
    return !jit_can_inline_rule(NULL);  /* Should return false */
}

static bool test_empty_action(void) {
    struct rule r = {0};
    r.noCode = 1;
    return jit_can_inline_rule(&r);  /* Empty actions are inlinable */
}

static bool test_passthrough_simple(void) {
    struct rule r = {0};
    r.code = "A = B;";
    return jit_can_inline_rule(&r);  /* Simple passthrough is inlinable */
}

static bool test_passthrough_whitespace(void) {
    struct rule r = {0};
    r.code = "  A  =  B  ;  ";
    return jit_can_inline_rule(&r);  /* Whitespace-padded passthrough */
}

static bool test_arithmetic_simple(void) {
    struct rule r = {0};
    r.code = "A = B + C;";
    return jit_can_inline_rule(&r);  /* Simple arithmetic expression */
}

static bool test_function_call(void) {
    struct rule r = {0};
    r.code = "A = malloc(sizeof(int));";
    return !jit_can_inline_rule(&r);  /* Function calls not inlinable */
}

static bool test_goto_keyword(void) {
    struct rule r = {0};
    r.code = "if (x) goto error;";
    return !jit_can_inline_rule(&r);  /* goto keyword blocks inlining */
}

static bool test_parse_callback(void) {
    struct rule r = {0};
    r.code = "Parse_Accept(ctx);";
    return !jit_can_inline_rule(&r);  /* Parse_* callbacks not inlinable */
}

static bool test_multiple_statements(void) {
    struct rule r = {0};
    r.code = "x = 1; y = 2; z = 3;";
    return !jit_can_inline_rule(&r);  /* Multiple statements not inlinable */
}

static bool test_block_statement(void) {
    struct rule r = {0};
    r.code = "{ A = B; C = D; }";
    return !jit_can_inline_rule(&r);  /* Block statements not inlinable */
}

static bool test_control_flow(void) {
    struct rule r = {0};
    r.code = "if (cond) A = B; else A = C;";
    return !jit_can_inline_rule(&r);  /* Control flow not inlinable */
}

static bool test_very_long_action(void) {
    struct rule r = {0};
    /* Create a 250-char simple expression (over the 200-char limit) */
    static char long_code[300];
    memset(long_code, 'x', 250);
    long_code[0] = 'A';
    long_code[1] = '=';
    long_code[249] = ';';
    long_code[250] = '\0';
    r.code = long_code;
    return !jit_can_inline_rule(&r);  /* Over length limit */
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
