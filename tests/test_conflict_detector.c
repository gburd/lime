/*
** Unit tests for the enhanced multi-grammar conflict detection system.
**
** Tests cover:
**   - ConflictPoint lifecycle (init, add_context, destroy)
**   - MultiGrammarConflictResult lifecycle
**   - Token-level conflict detection across extensions
**   - Rule-level conflict detection
**   - Semantic-level conflict detection
**   - Combined detect_conflict() convenience function
**   - Full scan via detect_all_multi_grammar_conflicts()
**   - Edge cases (NULL args, empty registries, single extension)
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "extension.h"
#include "conflict.h"
#include "snapshot.h"

/* ------------------------------------------------------------------ */
/*  Test infrastructure                                                */
/* ------------------------------------------------------------------ */

static int test_count = 0;
static int fail_count = 0;

#define ASSERT(cond, msg) do { \
    test_count++; \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL [%d]: %s (%s:%d)\n", \
                test_count, msg, __FILE__, __LINE__); \
        fail_count++; \
    } \
} while(0)

/* ------------------------------------------------------------------ */
/*  Mock extension callbacks                                           */
/* ------------------------------------------------------------------ */

typedef struct MockData {
    GrammarModification *mods;
    uint32_t nmods;
    bool fail;
} MockData;

static bool mock_get_mods(void *user_data,
                          const struct ParserSnapshot *base,
                          GrammarModification **mods_out,
                          uint32_t *nmods_out) {
    MockData *d = (MockData *)user_data;
    (void)base;
    if (d->fail) return false;
    *mods_out = d->mods;
    *nmods_out = d->nmods;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Helper: register and load a mock extension                         */
/* ------------------------------------------------------------------ */

static ExtensionID register_and_load(ExtensionRegistry *reg,
                                     const char *name,
                                     MockData *data) {
    ExtensionInfo info = {
        .name = name,
        .version = "1.0.0",
        .get_modifications = mock_get_mods,
        .on_conflict = NULL,
        .on_unload = NULL,
        .user_data = data,
    };
    ExtensionID id = 0;
    if (!register_extension(reg, &info, &id)) return 0;
    char *err = NULL;
    if (!load_extension(reg, id, NULL, &err)) {
        free(err);
        return 0;
    }
    return id;
}

/* ------------------------------------------------------------------ */
/*  Test: ConflictPoint init and destroy                               */
/* ------------------------------------------------------------------ */

static void test_conflict_point_lifecycle(void) {
    printf("test_conflict_point_lifecycle\n");

    ConflictPoint cp;
    conflict_point_init(&cp, 42, 7, CONFLICT_LEVEL_TOKEN);

    ASSERT(cp.token == 42, "token should be 42");
    ASSERT(cp.state == 7, "state should be 7");
    ASSERT(cp.level == CONFLICT_LEVEL_TOKEN, "level should be TOKEN");
    ASSERT(cp.ncontexts == 0, "initial ncontexts should be 0");
    ASSERT(cp.contexts == NULL, "initial contexts should be NULL");
    ASSERT(cp.description == NULL, "initial description should be NULL");

    /* Destroy empty point */
    conflict_point_destroy(&cp);
    ASSERT(cp.ncontexts == 0, "ncontexts should be 0 after destroy");
    ASSERT(cp.contexts == NULL, "contexts should be NULL after destroy");

    /* NULL should be safe */
    conflict_point_init(NULL, 0, 0, CONFLICT_LEVEL_TOKEN);
    conflict_point_destroy(NULL);
    ASSERT(true, "NULL operations should not crash");
}

/* ------------------------------------------------------------------ */
/*  Test: ConflictPoint add_context                                    */
/* ------------------------------------------------------------------ */

static void test_conflict_point_add_context(void) {
    printf("test_conflict_point_add_context\n");

    ConflictPoint cp;
    conflict_point_init(&cp, 10, 5, CONFLICT_LEVEL_RULE);

    LimeContext ctx1 = {
        .ext_id = 1,
        .token = 10,
        .state = 5,
        .priority = 100,
        .grammar_name = "postgres",
    };
    LimeContext ctx2 = {
        .ext_id = 2,
        .token = 10,
        .state = 5,
        .priority = 50,
        .grammar_name = "oracle",
    };

    bool ok = conflict_point_add_context(&cp, &ctx1);
    ASSERT(ok, "first add should succeed");
    ASSERT(cp.ncontexts == 1, "ncontexts should be 1");

    ok = conflict_point_add_context(&cp, &ctx2);
    ASSERT(ok, "second add should succeed");
    ASSERT(cp.ncontexts == 2, "ncontexts should be 2");

    ASSERT(cp.contexts[0].ext_id == 1, "first context ext_id");
    ASSERT(cp.contexts[0].priority == 100, "first context priority");
    ASSERT(strcmp(cp.contexts[0].grammar_name, "postgres") == 0,
           "first context grammar_name");

    ASSERT(cp.contexts[1].ext_id == 2, "second context ext_id");
    ASSERT(strcmp(cp.contexts[1].grammar_name, "oracle") == 0,
           "second context grammar_name");

    /* NULL args should fail gracefully */
    ok = conflict_point_add_context(NULL, &ctx1);
    ASSERT(!ok, "NULL cp should return false");
    ok = conflict_point_add_context(&cp, NULL);
    ASSERT(!ok, "NULL ctx should return false");

    conflict_point_destroy(&cp);
}

/* ------------------------------------------------------------------ */
/*  Test: ConflictPoint growth (exercise realloc path)                 */
/* ------------------------------------------------------------------ */

static void test_conflict_point_growth(void) {
    printf("test_conflict_point_growth\n");

    ConflictPoint cp;
    conflict_point_init(&cp, 1, -1, CONFLICT_LEVEL_TOKEN);

    /* Add more contexts than initial capacity (4) */
    for (int i = 0; i < 20; i++) {
        LimeContext ctx = {
            .ext_id = (uint32_t)(i + 1),
            .token = 1,
            .state = -1,
            .priority = i,
            .grammar_name = "test",
        };
        bool ok = conflict_point_add_context(&cp, &ctx);
        ASSERT(ok, "add context should succeed");
    }

    ASSERT(cp.ncontexts == 20, "should have 20 contexts");
    ASSERT(cp.capacity >= 20, "capacity should be at least 20");

    /* Verify first and last */
    ASSERT(cp.contexts[0].ext_id == 1, "first ext_id");
    ASSERT(cp.contexts[19].ext_id == 20, "last ext_id");

    conflict_point_destroy(&cp);
}

/* ------------------------------------------------------------------ */
/*  Test: MultiGrammarConflictResult lifecycle                         */
/* ------------------------------------------------------------------ */

static void test_multi_conflict_result_lifecycle(void) {
    printf("test_multi_conflict_result_lifecycle\n");

    MultiGrammarConflictResult *r = multi_conflict_result_create();
    ASSERT(r != NULL, "create should succeed");
    ASSERT(r->npoints == 0, "initial npoints should be 0");
    ASSERT(r->token_conflicts == 0, "initial token_conflicts should be 0");
    ASSERT(r->rule_conflicts == 0, "initial rule_conflicts should be 0");
    ASSERT(r->semantic_conflicts == 0, "initial semantic_conflicts should be 0");

    multi_conflict_result_destroy(r);
    /* Should not crash */
    ASSERT(true, "destroy should not crash");

    /* NULL destroy should be safe */
    multi_conflict_result_destroy(NULL);
    ASSERT(true, "destroy(NULL) should be safe");
}

/* ------------------------------------------------------------------ */
/*  Test: detect_token_conflicts with colliding tokens                 */
/* ------------------------------------------------------------------ */

static void test_detect_token_conflicts_collision(void) {
    printf("test_detect_token_conflicts_collision\n");

    ExtensionRegistry *reg = create_extension_registry();
    ASSERT(reg != NULL, "create registry");

    /* Extension A adds token ARRAY */
    GrammarModification mod_a = {
        .type = MOD_ADD_TOKEN,
        .description = "PostgreSQL ARRAY",
        .u.add_token = { .name = "ARRAY", .lexeme = "ARRAY", .token_code = 100 },
    };
    MockData data_a = { .mods = &mod_a, .nmods = 1 };

    /* Extension B also adds token ARRAY */
    GrammarModification mod_b = {
        .type = MOD_ADD_TOKEN,
        .description = "TypeScript ARRAY",
        .u.add_token = { .name = "ARRAY", .lexeme = "Array", .token_code = 200 },
    };
    MockData data_b = { .mods = &mod_b, .nmods = 1 };

    register_and_load(reg, "postgres", &data_a);
    register_and_load(reg, "typescript", &data_b);

    MultiGrammarConflictResult *result = multi_conflict_result_create();
    ASSERT(result != NULL, "create result");

    uint32_t n = detect_token_conflicts(reg, result);
    ASSERT(n > 0, "should detect token conflict");
    ASSERT(result->npoints > 0, "should have conflict points");
    ASSERT(result->token_conflicts > 0, "token_conflicts count should be > 0");

    if (result->npoints > 0) {
        ConflictPoint *cp = &result->points[0];
        ASSERT(cp->level == CONFLICT_LEVEL_TOKEN, "level should be TOKEN");
        ASSERT(cp->ncontexts >= 2, "should have at least 2 contexts");
        ASSERT(cp->description != NULL, "should have description");
    }

    multi_conflict_result_destroy(result);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: detect_token_conflicts with no collision                     */
/* ------------------------------------------------------------------ */

static void test_detect_token_conflicts_no_collision(void) {
    printf("test_detect_token_conflicts_no_collision\n");

    ExtensionRegistry *reg = create_extension_registry();

    GrammarModification mod_a = {
        .type = MOD_ADD_TOKEN,
        .u.add_token = { .name = "JSONB", .lexeme = "@>", .token_code = 100 },
    };
    MockData data_a = { .mods = &mod_a, .nmods = 1 };

    GrammarModification mod_b = {
        .type = MOD_ADD_TOKEN,
        .u.add_token = { .name = "ARROW", .lexeme = "->", .token_code = 200 },
    };
    MockData data_b = { .mods = &mod_b, .nmods = 1 };

    register_and_load(reg, "ext-a", &data_a);
    register_and_load(reg, "ext-b", &data_b);

    MultiGrammarConflictResult *result = multi_conflict_result_create();
    uint32_t n = detect_token_conflicts(reg, result);

    ASSERT(n == 0, "should not detect conflicts for different tokens");
    ASSERT(result->npoints == 0, "should have no conflict points");

    multi_conflict_result_destroy(result);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: detect_token_conflicts with NULL args                        */
/* ------------------------------------------------------------------ */

static void test_detect_token_conflicts_null(void) {
    printf("test_detect_token_conflicts_null\n");

    ExtensionRegistry *reg = create_extension_registry();
    MultiGrammarConflictResult *result = multi_conflict_result_create();

    uint32_t n1 = detect_token_conflicts(NULL, result);
    ASSERT(n1 == 0, "NULL registry should return 0");

    uint32_t n2 = detect_token_conflicts(reg, NULL);
    ASSERT(n2 == 0, "NULL result should return 0");

    multi_conflict_result_destroy(result);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: detect_token_conflicts with empty registry                   */
/* ------------------------------------------------------------------ */

static void test_detect_token_conflicts_empty(void) {
    printf("test_detect_token_conflicts_empty\n");

    ExtensionRegistry *reg = create_extension_registry();
    MultiGrammarConflictResult *result = multi_conflict_result_create();

    uint32_t n = detect_token_conflicts(reg, result);
    ASSERT(n == 0, "empty registry should have no conflicts");

    multi_conflict_result_destroy(result);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: detect_token_conflicts with single extension                 */
/* ------------------------------------------------------------------ */

static void test_detect_token_conflicts_single(void) {
    printf("test_detect_token_conflicts_single\n");

    ExtensionRegistry *reg = create_extension_registry();

    GrammarModification mod = {
        .type = MOD_ADD_TOKEN,
        .u.add_token = { .name = "JSONB", .lexeme = "@>", .token_code = 100 },
    };
    MockData data = { .mods = &mod, .nmods = 1 };
    register_and_load(reg, "single", &data);

    MultiGrammarConflictResult *result = multi_conflict_result_create();
    uint32_t n = detect_token_conflicts(reg, result);

    ASSERT(n == 0, "single extension should have no conflicts");

    multi_conflict_result_destroy(result);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: detect_rule_conflicts                                        */
/* ------------------------------------------------------------------ */

static void test_detect_rule_conflicts(void) {
    printf("test_detect_rule_conflicts\n");

    ExtensionRegistry *reg = create_extension_registry();

    /* Extension A: adds token FOO(code=50) and a rule using FOO */
    static const char *rhs_a[] = {"FOO", "BAR", NULL};
    GrammarModification mods_a[2];
    memset(mods_a, 0, sizeof(mods_a));
    mods_a[0].type = MOD_ADD_TOKEN;
    mods_a[0].u.add_token.name = "FOO";
    mods_a[0].u.add_token.token_code = 50;
    mods_a[1].type = MOD_ADD_RULE;
    mods_a[1].u.add_rule.lhs = "expr";
    mods_a[1].u.add_rule.rhs = rhs_a;
    mods_a[1].u.add_rule.nrhs = 2;
    mods_a[1].u.add_rule.code = "{ A = B + C; }";
    MockData data_a = { .mods = mods_a, .nmods = 2 };

    /* Extension B: same token FOO(code=50) and different rule using FOO */
    static const char *rhs_b[] = {"FOO", "BAZ", NULL};
    GrammarModification mods_b[2];
    memset(mods_b, 0, sizeof(mods_b));
    mods_b[0].type = MOD_ADD_TOKEN;
    mods_b[0].u.add_token.name = "FOO";
    mods_b[0].u.add_token.token_code = 50;
    mods_b[1].type = MOD_ADD_RULE;
    mods_b[1].u.add_rule.lhs = "stmt";
    mods_b[1].u.add_rule.rhs = rhs_b;
    mods_b[1].u.add_rule.nrhs = 2;
    mods_b[1].u.add_rule.code = "{ X = Y - Z; }";
    MockData data_b = { .mods = mods_b, .nmods = 2 };

    register_and_load(reg, "ext-a", &data_a);
    register_and_load(reg, "ext-b", &data_b);

    MultiGrammarConflictResult *result = multi_conflict_result_create();
    uint32_t n = detect_rule_conflicts(reg, 50, 3, result);

    ASSERT(n > 0, "should detect rule-level conflict");
    ASSERT(result->rule_conflicts > 0, "rule_conflicts count should be > 0");

    if (result->npoints > 0) {
        ConflictPoint *cp = &result->points[0];
        ASSERT(cp->level == CONFLICT_LEVEL_RULE, "level should be RULE");
        ASSERT(cp->token == 50, "token should be 50");
        ASSERT(cp->state == 3, "state should be 3");
        ASSERT(cp->ncontexts == 2, "should have 2 contexts");
    }

    multi_conflict_result_destroy(result);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: detect_rule_conflicts with no conflict                       */
/* ------------------------------------------------------------------ */

static void test_detect_rule_conflicts_no_conflict(void) {
    printf("test_detect_rule_conflicts_no_conflict\n");

    ExtensionRegistry *reg = create_extension_registry();

    GrammarModification mod_a = {
        .type = MOD_ADD_TOKEN,
        .u.add_token = { .name = "FOO", .token_code = 50 },
    };
    MockData data_a = { .mods = &mod_a, .nmods = 1 };

    /* Only one extension defines token 50 */
    register_and_load(reg, "ext-a", &data_a);

    MultiGrammarConflictResult *result = multi_conflict_result_create();
    uint32_t n = detect_rule_conflicts(reg, 50, 3, result);

    ASSERT(n == 0, "single extension should not have rule conflicts");

    multi_conflict_result_destroy(result);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: detect_semantic_conflicts                                    */
/* ------------------------------------------------------------------ */

static void test_detect_semantic_conflicts(void) {
    printf("test_detect_semantic_conflicts\n");

    ExtensionRegistry *reg = create_extension_registry();

    /* Extension A: rule expr -> A PLUS B with action code_a */
    static const char *rhs[] = {"A", "PLUS", "B", NULL};
    GrammarModification mod_a = {
        .type = MOD_ADD_RULE,
        .u.add_rule = {
            .lhs = "expr",
            .rhs = rhs,
            .nrhs = 3,
            .code = "{ result = a + b; }",
        },
    };
    MockData data_a = { .mods = &mod_a, .nmods = 1 };

    /* Extension B: same rule with different action code */
    GrammarModification mod_b = {
        .type = MOD_ADD_RULE,
        .u.add_rule = {
            .lhs = "expr",
            .rhs = rhs,
            .nrhs = 3,
            .code = "{ result = concatenate(a, b); }",
        },
    };
    MockData data_b = { .mods = &mod_b, .nmods = 1 };

    register_and_load(reg, "math-ext", &data_a);
    register_and_load(reg, "string-ext", &data_b);

    MultiGrammarConflictResult *result = multi_conflict_result_create();
    uint32_t n = detect_semantic_conflicts(reg, 0, -1, result);

    ASSERT(n > 0, "should detect semantic conflict");
    ASSERT(result->semantic_conflicts > 0, "semantic_conflicts count > 0");

    if (result->npoints > 0) {
        ConflictPoint *cp = &result->points[0];
        ASSERT(cp->level == CONFLICT_LEVEL_SEMANTIC, "level should be SEMANTIC");
        ASSERT(cp->ncontexts == 2, "should have 2 contexts");
    }

    multi_conflict_result_destroy(result);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: detect_semantic_conflicts with same code (no conflict)       */
/* ------------------------------------------------------------------ */

static void test_detect_semantic_conflicts_same_code(void) {
    printf("test_detect_semantic_conflicts_same_code\n");

    ExtensionRegistry *reg = create_extension_registry();

    static const char *rhs[] = {"X", "Y", NULL};
    GrammarModification mod_a = {
        .type = MOD_ADD_RULE,
        .u.add_rule = {
            .lhs = "thing",
            .rhs = rhs,
            .nrhs = 2,
            .code = "{ same_code(); }",
        },
    };
    MockData data_a = { .mods = &mod_a, .nmods = 1 };

    GrammarModification mod_b = {
        .type = MOD_ADD_RULE,
        .u.add_rule = {
            .lhs = "thing",
            .rhs = rhs,
            .nrhs = 2,
            .code = "{ same_code(); }",
        },
    };
    MockData data_b = { .mods = &mod_b, .nmods = 1 };

    register_and_load(reg, "ext-a", &data_a);
    register_and_load(reg, "ext-b", &data_b);

    MultiGrammarConflictResult *result = multi_conflict_result_create();
    uint32_t n = detect_semantic_conflicts(reg, 0, -1, result);

    ASSERT(n == 0, "identical code should not be a semantic conflict");

    multi_conflict_result_destroy(result);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: detect_semantic_conflicts with single extension              */
/* ------------------------------------------------------------------ */

static void test_detect_semantic_conflicts_single(void) {
    printf("test_detect_semantic_conflicts_single\n");

    ExtensionRegistry *reg = create_extension_registry();

    static const char *rhs[] = {"A", NULL};
    GrammarModification mod = {
        .type = MOD_ADD_RULE,
        .u.add_rule = { .lhs = "x", .rhs = rhs, .nrhs = 1, .code = "{ }" },
    };
    MockData data = { .mods = &mod, .nmods = 1 };
    register_and_load(reg, "only-one", &data);

    MultiGrammarConflictResult *result = multi_conflict_result_create();
    uint32_t n = detect_semantic_conflicts(reg, 0, -1, result);

    ASSERT(n == 0, "single extension cannot have semantic conflicts");

    multi_conflict_result_destroy(result);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: detect_conflict() convenience function                       */
/* ------------------------------------------------------------------ */

static void test_detect_conflict_convenience(void) {
    printf("test_detect_conflict_convenience\n");

    ExtensionRegistry *reg = create_extension_registry();

    /* Two extensions both define token 77 */
    GrammarModification mod_a = {
        .type = MOD_ADD_TOKEN,
        .u.add_token = { .name = "CAST", .token_code = 77 },
    };
    MockData data_a = { .mods = &mod_a, .nmods = 1 };

    GrammarModification mod_b = {
        .type = MOD_ADD_TOKEN,
        .u.add_token = { .name = "CAST", .token_code = 77 },
    };
    MockData data_b = { .mods = &mod_b, .nmods = 1 };

    register_and_load(reg, "pg-cast", &data_a);
    register_and_load(reg, "oracle-cast", &data_b);

    ConflictPoint cp = detect_conflict(reg, 77, -1);

    ASSERT(cp.ncontexts >= 2, "should find at least 2 contexts");
    ASSERT(cp.token == 77, "token should be 77");

    if (cp.ncontexts >= 2) {
        ASSERT(cp.description != NULL, "should have description");
    }

    conflict_point_destroy(&cp);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: detect_conflict() with no ambiguity                          */
/* ------------------------------------------------------------------ */

static void test_detect_conflict_no_ambiguity(void) {
    printf("test_detect_conflict_no_ambiguity\n");

    ExtensionRegistry *reg = create_extension_registry();

    GrammarModification mod = {
        .type = MOD_ADD_TOKEN,
        .u.add_token = { .name = "UNIQUE_TOKEN", .token_code = 99 },
    };
    MockData data = { .mods = &mod, .nmods = 1 };
    register_and_load(reg, "single-ext", &data);

    ConflictPoint cp = detect_conflict(reg, 99, -1);

    ASSERT(cp.ncontexts <= 1, "single extension should not be ambiguous");

    conflict_point_destroy(&cp);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: detect_conflict() with NULL registry                         */
/* ------------------------------------------------------------------ */

static void test_detect_conflict_null(void) {
    printf("test_detect_conflict_null\n");

    ConflictPoint cp = detect_conflict(NULL, 42, 0);
    ASSERT(cp.ncontexts == 0, "NULL registry should give 0 contexts");
    conflict_point_destroy(&cp);
}

/* ------------------------------------------------------------------ */
/*  Test: detect_all_multi_grammar_conflicts                           */
/* ------------------------------------------------------------------ */

static void test_detect_all_conflicts(void) {
    printf("test_detect_all_conflicts\n");

    ExtensionRegistry *reg = create_extension_registry();

    /* Extension A: token ARRAY + rule */
    static const char *rhs_a[] = {"ARRAY", "OF", "type", NULL};
    GrammarModification mods_a[2];
    memset(mods_a, 0, sizeof(mods_a));
    mods_a[0].type = MOD_ADD_TOKEN;
    mods_a[0].u.add_token.name = "ARRAY";
    mods_a[0].u.add_token.token_code = 60;
    mods_a[1].type = MOD_ADD_RULE;
    mods_a[1].u.add_rule.lhs = "type_decl";
    mods_a[1].u.add_rule.rhs = rhs_a;
    mods_a[1].u.add_rule.nrhs = 3;
    mods_a[1].u.add_rule.code = "{ /* pg array */ }";
    MockData data_a = { .mods = mods_a, .nmods = 2 };

    /* Extension B: same token ARRAY + same rule structure, different code */
    GrammarModification mods_b[2];
    memset(mods_b, 0, sizeof(mods_b));
    mods_b[0].type = MOD_ADD_TOKEN;
    mods_b[0].u.add_token.name = "ARRAY";
    mods_b[0].u.add_token.token_code = 60;
    mods_b[1].type = MOD_ADD_RULE;
    mods_b[1].u.add_rule.lhs = "type_decl";
    mods_b[1].u.add_rule.rhs = rhs_a;  /* same RHS */
    mods_b[1].u.add_rule.nrhs = 3;
    mods_b[1].u.add_rule.code = "{ /* ts array */ }";
    MockData data_b = { .mods = mods_b, .nmods = 2 };

    register_and_load(reg, "postgres-types", &data_a);
    register_and_load(reg, "typescript-types", &data_b);

    MultiGrammarConflictResult *result = multi_conflict_result_create();
    bool found = detect_all_multi_grammar_conflicts(reg, result);

    ASSERT(found, "should detect conflicts in full scan");
    ASSERT(result->npoints > 0, "should have conflict points");

    /* Should find at least token-level and semantic-level conflicts */
    ASSERT(result->token_conflicts > 0, "should find token conflicts");
    ASSERT(result->semantic_conflicts > 0, "should find semantic conflicts");

    multi_conflict_result_destroy(result);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: detect_all with empty registry                               */
/* ------------------------------------------------------------------ */

static void test_detect_all_empty(void) {
    printf("test_detect_all_empty\n");

    ExtensionRegistry *reg = create_extension_registry();
    MultiGrammarConflictResult *result = multi_conflict_result_create();

    bool found = detect_all_multi_grammar_conflicts(reg, result);
    ASSERT(!found, "empty registry should have no conflicts");

    multi_conflict_result_destroy(result);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: detect_all with NULL args                                    */
/* ------------------------------------------------------------------ */

static void test_detect_all_null(void) {
    printf("test_detect_all_null\n");

    ExtensionRegistry *reg = create_extension_registry();
    MultiGrammarConflictResult *result = multi_conflict_result_create();

    bool r1 = detect_all_multi_grammar_conflicts(NULL, result);
    ASSERT(!r1, "NULL registry should return false");

    bool r2 = detect_all_multi_grammar_conflicts(reg, NULL);
    ASSERT(!r2, "NULL result should return false");

    multi_conflict_result_destroy(result);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: three-way token conflict                                     */
/* ------------------------------------------------------------------ */

static void test_three_way_token_conflict(void) {
    printf("test_three_way_token_conflict\n");

    ExtensionRegistry *reg = create_extension_registry();

    GrammarModification mod_a = {
        .type = MOD_ADD_TOKEN,
        .u.add_token = { .name = "SCOPE", .token_code = 88 },
    };
    MockData data_a = { .mods = &mod_a, .nmods = 1 };

    GrammarModification mod_b = {
        .type = MOD_ADD_TOKEN,
        .u.add_token = { .name = "SCOPE", .token_code = 88 },
    };
    MockData data_b = { .mods = &mod_b, .nmods = 1 };

    GrammarModification mod_c = {
        .type = MOD_ADD_TOKEN,
        .u.add_token = { .name = "SCOPE", .token_code = 88 },
    };
    MockData data_c = { .mods = &mod_c, .nmods = 1 };

    register_and_load(reg, "ext-1", &data_a);
    register_and_load(reg, "ext-2", &data_b);
    register_and_load(reg, "ext-3", &data_c);

    MultiGrammarConflictResult *result = multi_conflict_result_create();
    uint32_t n = detect_token_conflicts(reg, result);

    ASSERT(n > 0, "should detect three-way conflict");
    if (result->npoints > 0) {
        ASSERT(result->points[0].ncontexts >= 3,
               "should have at least 3 contexts for three-way");
    }

    multi_conflict_result_destroy(result);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: mixed modification types (only tokens conflict)              */
/* ------------------------------------------------------------------ */

static void test_mixed_mod_types(void) {
    printf("test_mixed_mod_types\n");

    ExtensionRegistry *reg = create_extension_registry();

    /* Extension A: adds a token and a rule */
    static const char *rhs[] = {"X", NULL};
    GrammarModification mods_a[2];
    memset(mods_a, 0, sizeof(mods_a));
    mods_a[0].type = MOD_ADD_TOKEN;
    mods_a[0].u.add_token.name = "OVERLAP";
    mods_a[0].u.add_token.token_code = 42;
    mods_a[1].type = MOD_ADD_RULE;
    mods_a[1].u.add_rule.lhs = "stmt";
    mods_a[1].u.add_rule.rhs = rhs;
    mods_a[1].u.add_rule.nrhs = 1;
    MockData data_a = { .mods = mods_a, .nmods = 2 };

    /* Extension B: adds the same token name but different mod type mix */
    GrammarModification mod_b = {
        .type = MOD_ADD_TOKEN,
        .u.add_token = { .name = "OVERLAP", .token_code = 43 },
    };
    MockData data_b = { .mods = &mod_b, .nmods = 1 };

    register_and_load(reg, "ext-a", &data_a);
    register_and_load(reg, "ext-b", &data_b);

    MultiGrammarConflictResult *result = multi_conflict_result_create();
    uint32_t n = detect_token_conflicts(reg, result);

    ASSERT(n > 0, "should detect token conflict even with mixed mod types");

    multi_conflict_result_destroy(result);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: unloaded extensions are ignored                              */
/* ------------------------------------------------------------------ */

static void test_unloaded_extensions_ignored(void) {
    printf("test_unloaded_extensions_ignored\n");

    ExtensionRegistry *reg = create_extension_registry();

    GrammarModification mod_a = {
        .type = MOD_ADD_TOKEN,
        .u.add_token = { .name = "CONFLICT_TOKEN", .token_code = 55 },
    };
    MockData data_a = { .mods = &mod_a, .nmods = 1 };

    GrammarModification mod_b = {
        .type = MOD_ADD_TOKEN,
        .u.add_token = { .name = "CONFLICT_TOKEN", .token_code = 55 },
    };
    MockData data_b = { .mods = &mod_b, .nmods = 1 };

    ExtensionID id_a = register_and_load(reg, "ext-a", &data_a);
    ExtensionID id_b = register_and_load(reg, "ext-b", &data_b);
    ASSERT(id_a > 0 && id_b > 0, "both should load");

    /* Unload ext-b */
    unload_extension(reg, id_b);

    MultiGrammarConflictResult *result = multi_conflict_result_create();
    uint32_t n = detect_token_conflicts(reg, result);

    ASSERT(n == 0, "unloaded extension should not cause conflicts");

    multi_conflict_result_destroy(result);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== Multi-Grammar Conflict Detection Tests ===\n");

    /* ConflictPoint tests */
    test_conflict_point_lifecycle();
    test_conflict_point_add_context();
    test_conflict_point_growth();

    /* MultiGrammarConflictResult tests */
    test_multi_conflict_result_lifecycle();

    /* Token-level tests */
    test_detect_token_conflicts_collision();
    test_detect_token_conflicts_no_collision();
    test_detect_token_conflicts_null();
    test_detect_token_conflicts_empty();
    test_detect_token_conflicts_single();

    /* Rule-level tests */
    test_detect_rule_conflicts();
    test_detect_rule_conflicts_no_conflict();

    /* Semantic-level tests */
    test_detect_semantic_conflicts();
    test_detect_semantic_conflicts_same_code();
    test_detect_semantic_conflicts_single();

    /* Combined detect_conflict() */
    test_detect_conflict_convenience();
    test_detect_conflict_no_ambiguity();
    test_detect_conflict_null();

    /* Full scan */
    test_detect_all_conflicts();
    test_detect_all_empty();
    test_detect_all_null();

    /* Multi-way and edge cases */
    test_three_way_token_conflict();
    test_mixed_mod_types();
    test_unloaded_extensions_ignored();

    printf("\n%d tests, %d failures\n", test_count, fail_count);
    return fail_count > 0 ? 1 : 0;
}
