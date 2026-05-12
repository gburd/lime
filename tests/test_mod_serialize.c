/*
** tests/test_mod_serialize.c
**
** Unit tests for lime_modifications_to_grammar_text().
**
** Covers each modification type plus the skip-reporting contract
** (runtime callback rules, remove-rule, malformed modify-precedence).
** Does NOT spawn `lime` to round-trip the output -- that would make
** the test depend on the lime binary's location at runtime and on
** the host having a working compiler.  A separate manual check in
** the subprocess-fallback integration point is the right place for
** end-to-end validation.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/mod_serialize.h"

/* ------------------------------------------------------------------ */
/*  Helpers                                                             */
/* ------------------------------------------------------------------ */

#define ASSERT(cond, msg)                                              \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "%s:%d: FAIL: %s  (assert: %s)\n",         \
                    __FILE__, __LINE__, (msg), #cond);                 \
            failures++;                                                \
        }                                                              \
    } while (0)

static int contains(const char *hay, const char *needle)
{
    if (hay == NULL || needle == NULL) return 0;
    return strstr(hay, needle) != NULL ? 1 : 0;
}

static int failures = 0;

/* ------------------------------------------------------------------ */
/*  Test cases                                                          */
/* ------------------------------------------------------------------ */

static void test_empty(void)
{
    uint32_t skipped = 42;
    char *err = (char *)0xdeadbeef;
    char *out = lime_modifications_to_grammar_text(NULL, 0, &skipped, &err);
    ASSERT(out != NULL, "empty serialization should succeed");
    ASSERT(skipped == 0, "empty serialization skipped count should be 0");
    ASSERT(err == NULL, "empty serialization should not set error");
    ASSERT(contains(out, "Auto-generated"),
           "empty output should still contain the header comment");
    free(out);
}

static void test_null_mods_nonzero_count(void)
{
    uint32_t skipped;
    char *err = NULL;
    char *out = lime_modifications_to_grammar_text(NULL, 5, &skipped, &err);
    ASSERT(out == NULL, "NULL mods with nmods>0 should fail");
    ASSERT(err != NULL, "error should be set");
    free(err);
}

static void test_add_token(void)
{
    GrammarModification mod = {
        .type = MOD_ADD_TOKEN,
        .description = "hstore @@@ operator",
        .u.add_token = {
            .name       = "ATAT_OP",
            .lexeme     = "@@@",
            .token_code = -1,
        },
    };
    uint32_t skipped = 99;
    char *out = lime_modifications_to_grammar_text(&mod, 1, &skipped, NULL);
    ASSERT(out != NULL, "add_token serialization should succeed");
    ASSERT(skipped == 0, "add_token should not be skipped");
    ASSERT(contains(out, "%token ATAT_OP."), "output should contain %token directive");
    ASSERT(contains(out, "hstore @@@ operator"),
           "description should appear as a comment");
    free(out);
}

static void test_add_rule_with_code(void)
{
    const char *rhs[] = { "a_expr", "ATAT_OP", "a_expr" };
    GrammarModification mod = {
        .type = MOD_ADD_RULE,
        .description = "a_expr @@@ a_expr",
        .u.add_rule = {
            .lhs        = "a_expr",
            .rhs        = rhs,
            .nrhs       = 3,
            .code       = "A = hstore_atat(B, C);",
            .precedence = -1,
        },
    };
    uint32_t skipped = 99;
    char *out = lime_modifications_to_grammar_text(&mod, 1, &skipped, NULL);
    ASSERT(out != NULL, "add_rule with code should succeed");
    ASSERT(skipped == 0, "add_rule with code should not be skipped");
    ASSERT(contains(out, "a_expr ::= a_expr ATAT_OP a_expr ."),
           "output should contain the BNF form");
    ASSERT(contains(out, "{ A = hstore_atat(B, C); }"),
           "action code should be embedded in braces");
    free(out);
}

/* The pointer value here is never dereferenced; we only need a non-NULL
** function pointer to populate .reduce and trigger the skip path. */
static void dummy_reduce(void *u, void *e, int n,
                         const void *v, const int *l, void *o)
{
    (void)u; (void)e; (void)n; (void)v; (void)l; (void)o;
}

static void test_add_rule_with_reduce_callback_skipped(void)
{
    const char *rhs[] = { "a_expr", "ATAT_OP", "a_expr" };
    GrammarModification mod = {
        .type = MOD_ADD_RULE,
        .description = "runtime-callback variant",
        .u.add_rule = {
            .lhs         = "a_expr",
            .rhs         = rhs,
            .nrhs        = 3,
            .reduce      = dummy_reduce,
            .reduce_user = NULL,
            .code        = NULL,
            .precedence  = -1,
        },
    };
    uint32_t skipped = 0;
    char *out = lime_modifications_to_grammar_text(&mod, 1, &skipped, NULL);
    ASSERT(out != NULL, "serialization should succeed");
    ASSERT(skipped == 1, "runtime-callback rule should be skipped");
    ASSERT(contains(out, "SKIPPED"), "output should note the skip");
    /* The rule MUST NOT appear in valid form; only as a skip comment. */
    ASSERT(!contains(out, "a_expr ::= a_expr ATAT_OP a_expr ."),
           "skipped rule should not appear as a real rule");
    free(out);
}

static void test_add_rule_reduce_plus_code_emits_code(void)
{
    /* When both .reduce and .code are set, the .code path wins for
    ** subprocess fallback: the text can round-trip, and the reduce
    ** callback remains valid for the in-process dispatch path when
    ** that eventually exists. */
    const char *rhs[] = { "X" };
    GrammarModification mod = {
        .type = MOD_ADD_RULE,
        .u.add_rule = {
            .lhs         = "y",
            .rhs         = rhs,
            .nrhs        = 1,
            .reduce      = dummy_reduce,
            .reduce_user = NULL,
            .code        = "A = B;",
            .precedence  = -1,
        },
    };
    uint32_t skipped = 99;
    char *out = lime_modifications_to_grammar_text(&mod, 1, &skipped, NULL);
    ASSERT(skipped == 0, "rule with both reduce and code should emit");
    ASSERT(contains(out, "y ::= X ."), "rule should emit normally");
    ASSERT(contains(out, "{ A = B; }"), "code should emit normally");
    free(out);
}

static void test_add_rule_precedence_note(void)
{
    const char *rhs[] = { "A" };
    GrammarModification mod = {
        .type = MOD_ADD_RULE,
        .u.add_rule = {
            .lhs        = "s",
            .rhs        = rhs,
            .nrhs       = 1,
            .code       = "X = 1;",
            .precedence = 7,
        },
    };
    char *out = lime_modifications_to_grammar_text(&mod, 1, NULL, NULL);
    ASSERT(contains(out, "precedence=7"),
           "output should note the non-expressible precedence override");
    free(out);
}

static void test_modify_precedence(void)
{
    GrammarModification mods[] = {
        {
            .type = MOD_MODIFY_PRECEDENCE,
            .u.modify_prec = { .symbol = "PLUS",  .new_assoc = 1 },  /* left */
        },
        {
            .type = MOD_MODIFY_PRECEDENCE,
            .u.modify_prec = { .symbol = "ARROW", .new_assoc = 2 },  /* right */
        },
        {
            .type = MOD_MODIFY_PRECEDENCE,
            .u.modify_prec = { .symbol = "EQ",    .new_assoc = 3 },  /* nonassoc */
        },
        {
            .type = MOD_MODIFY_PRECEDENCE,
            .u.modify_prec = { .symbol = "WEIRD", .new_assoc = 0 },  /* skipped */
        },
    };
    uint32_t skipped = 0;
    char *out = lime_modifications_to_grammar_text(mods, 4, &skipped, NULL);
    ASSERT(contains(out, "%left PLUS."),      "left-assoc directive emitted");
    ASSERT(contains(out, "%right ARROW."),    "right-assoc directive emitted");
    ASSERT(contains(out, "%nonassoc EQ."),    "nonassoc directive emitted");
    ASSERT(contains(out, "SKIPPED MOD_MODIFY_PRECEDENCE for WEIRD"),
           "new_assoc=0 should be skipped");
    /* new_assoc=0 not expressible reports as a comment, not in the
    ** skipped counter (it did still emit *something*). */
    free(out);
}

static void test_add_type(void)
{
    GrammarModification mod = {
        .type = MOD_ADD_TYPE,
        .u.add_type = { .name = "stmt", .datatype = "Node *" },
    };
    char *out = lime_modifications_to_grammar_text(&mod, 1, NULL, NULL);
    ASSERT(contains(out, "%type stmt {Node *}"), "type directive emitted");
    free(out);
}

static void test_remove_rule_skipped(void)
{
    GrammarModification mod = {
        .type = MOD_REMOVE_RULE,
        .u.remove_rule = { .lhs = "stmt", .rule_index = 3 },
    };
    uint32_t skipped = 0;
    char *out = lime_modifications_to_grammar_text(&mod, 1, &skipped, NULL);
    ASSERT(skipped == 1, "remove_rule should count as skipped");
    ASSERT(contains(out, "SKIPPED MOD_REMOVE_RULE"),
           "remove_rule should emit a skip comment");
    free(out);
}

static void test_mixed_sequence(void)
{
    const char *rhs1[] = { "A", "B" };
    const char *rhs2[] = { "C" };
    GrammarModification mods[] = {
        {
            .type = MOD_ADD_TOKEN,
            .u.add_token = { .name = "TK_NEW", .token_code = -1 },
        },
        {
            .type = MOD_ADD_TYPE,
            .u.add_type = { .name = "foo", .datatype = "int" },
        },
        {
            .type = MOD_ADD_RULE,
            .u.add_rule = {
                .lhs = "foo", .rhs = rhs1, .nrhs = 2,
                .code = "X = 1;", .precedence = -1,
            },
        },
        {
            .type = MOD_ADD_RULE,
            .u.add_rule = {
                .lhs = "foo", .rhs = rhs2, .nrhs = 1,
                .reduce = dummy_reduce, .reduce_user = NULL,
                .code = NULL, .precedence = -1,
            },
        },
    };
    uint32_t skipped = 0;
    char *out = lime_modifications_to_grammar_text(mods, 4, &skipped, NULL);
    ASSERT(out != NULL, "mixed sequence serializes");
    ASSERT(skipped == 1, "one skipped (runtime callback rule)");
    ASSERT(contains(out, "%token TK_NEW."),     "token emitted");
    ASSERT(contains(out, "%type foo {int}"),    "type emitted");
    ASSERT(contains(out, "foo ::= A B ."),      "first rule emitted");
    ASSERT(!contains(out, "foo ::= C ."),       "skipped rule NOT emitted");
    free(out);
}

int main(void)
{
    test_empty();
    test_null_mods_nonzero_count();
    test_add_token();
    test_add_rule_with_code();
    test_add_rule_with_reduce_callback_skipped();
    test_add_rule_reduce_plus_code_emits_code();
    test_add_rule_precedence_note();
    test_modify_precedence();
    test_add_type();
    test_remove_rule_skipped();
    test_mixed_sequence();

    if (failures > 0) {
        fprintf(stderr, "test_mod_serialize: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_mod_serialize: all cases passed\n");
    return 0;
}
