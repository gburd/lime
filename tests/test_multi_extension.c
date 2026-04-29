/*
** Integration tests for multi-extension grammar scenarios.
**
** These tests exercise the full pipeline from extension registration
** through conflict detection and disambiguation, simulating real-world
** scenarios where multiple grammar extensions interact.
**
** Scenarios tested:
**   1. PostgreSQL JSONB + Standard SQL (non-conflicting)
**   2. Two SQL dialects with token collision (ARRAY keyword)
**   3. Three extensions with cascading conflicts
**   4. Register, load, detect, unload, re-check cycle
**   5. Extension lifecycle with conflict resolution callbacks
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "extension.h"
#include "conflict.h"
#include "snapshot.h"
#include "snapshot_modify.h"

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
/*  Mock infrastructure                                                */
/* ------------------------------------------------------------------ */

typedef struct MockExtension {
    const char *name;
    GrammarModification *mods;
    uint32_t nmods;
    bool unloaded;
    int conflict_calls;
    ConflictResolution conflict_response;
} MockExtension;

static bool mock_get_mods(void *user_data,
                          const struct ParserSnapshot *base,
                          GrammarModification **mods_out,
                          uint32_t *nmods_out) {
    MockExtension *ext = (MockExtension *)user_data;
    (void)base;
    *mods_out = ext->mods;
    *nmods_out = ext->nmods;
    return true;
}

static void mock_on_unload(void *user_data) {
    MockExtension *ext = (MockExtension *)user_data;
    ext->unloaded = true;
}

static ConflictResolution mock_on_conflict(void *user_data,
                                            const ConflictInfo *info) {
    MockExtension *ext = (MockExtension *)user_data;
    (void)info;
    ext->conflict_calls++;
    return ext->conflict_response;
}

static ExtensionID setup_extension(ExtensionRegistry *reg,
                                   MockExtension *mock) {
    ExtensionInfo info = {
        .name = mock->name,
        .version = "1.0.0",
        .get_modifications = mock_get_mods,
        .on_conflict = mock_on_conflict,
        .on_unload = mock_on_unload,
        .user_data = mock,
    };
    ExtensionID id = 0;
    register_extension(reg, &info, &id);
    load_extension(reg, id, NULL, NULL);
    return id;
}

/* ------------------------------------------------------------------ */
/*  Scenario 1: Non-conflicting extensions (JSONB + standard SQL)      */
/* ------------------------------------------------------------------ */

static void test_non_conflicting_extensions(void) {
    printf("test_non_conflicting_extensions\n");

    ExtensionRegistry *reg = create_extension_registry();

    /* JSONB extension: adds TK_JSONB, TK_JSONB_ARROW */
    GrammarModification jsonb_mods[2];
    memset(jsonb_mods, 0, sizeof(jsonb_mods));
    jsonb_mods[0].type = MOD_ADD_TOKEN;
    jsonb_mods[0].u.add_token.name = "TK_JSONB";
    jsonb_mods[0].u.add_token.lexeme = "@>";
    jsonb_mods[0].u.add_token.token_code = 200;
    jsonb_mods[1].type = MOD_ADD_TOKEN;
    jsonb_mods[1].u.add_token.name = "TK_JSONB_ARROW";
    jsonb_mods[1].u.add_token.lexeme = "->>";
    jsonb_mods[1].u.add_token.token_code = 201;

    MockExtension jsonb_ext = {
        .name = "jsonb",
        .mods = jsonb_mods,
        .nmods = 2,
        .conflict_response = CONFLICT_UNRESOLVED,
    };

    /* Standard SQL: adds TK_BETWEEN, TK_EXISTS */
    GrammarModification sql_mods[2];
    memset(sql_mods, 0, sizeof(sql_mods));
    sql_mods[0].type = MOD_ADD_TOKEN;
    sql_mods[0].u.add_token.name = "TK_BETWEEN";
    sql_mods[0].u.add_token.lexeme = "BETWEEN";
    sql_mods[0].u.add_token.token_code = 300;
    sql_mods[1].type = MOD_ADD_TOKEN;
    sql_mods[1].u.add_token.name = "TK_EXISTS";
    sql_mods[1].u.add_token.lexeme = "EXISTS";
    sql_mods[1].u.add_token.token_code = 301;

    MockExtension sql_ext = {
        .name = "standard-sql",
        .mods = sql_mods,
        .nmods = 2,
        .conflict_response = CONFLICT_UNRESOLVED,
    };

    setup_extension(reg, &jsonb_ext);
    setup_extension(reg, &sql_ext);

    /* No token-level conflicts expected */
    MultiGrammarConflictResult *result = multi_conflict_result_create();
    uint32_t n = detect_token_conflicts(reg, result);
    ASSERT(n == 0, "non-conflicting extensions should have 0 token conflicts");

    /* No semantic conflicts either */
    uint32_t ns = detect_semantic_conflicts(reg, 0, -1, result);
    ASSERT(ns == 0, "should have 0 semantic conflicts");

    /* Full scan also clean */
    multi_conflict_result_destroy(result);
    result = multi_conflict_result_create();
    bool any = detect_all_multi_grammar_conflicts(reg, result);
    ASSERT(!any, "full scan should find no conflicts");

    multi_conflict_result_destroy(result);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Scenario 2: Token collision (ARRAY keyword)                        */
/* ------------------------------------------------------------------ */

static void test_array_token_collision(void) {
    printf("test_array_token_collision\n");

    ExtensionRegistry *reg = create_extension_registry();

    GrammarModification pg_mod = {
        .type = MOD_ADD_TOKEN,
        .u.add_token = { .name = "ARRAY", .lexeme = "ARRAY", .token_code = 150 },
    };
    MockExtension pg_ext = {
        .name = "postgres",
        .mods = &pg_mod,
        .nmods = 1,
        .conflict_response = CONFLICT_KEEP_EXISTING,
    };

    GrammarModification ts_mod = {
        .type = MOD_ADD_TOKEN,
        .u.add_token = { .name = "ARRAY", .lexeme = "Array", .token_code = 250 },
    };
    MockExtension ts_ext = {
        .name = "typescript",
        .mods = &ts_mod,
        .nmods = 1,
        .conflict_response = CONFLICT_USE_NEW,
    };

    setup_extension(reg, &pg_ext);
    setup_extension(reg, &ts_ext);

    /* Should detect the collision */
    MultiGrammarConflictResult *result = multi_conflict_result_create();
    uint32_t n = detect_token_conflicts(reg, result);
    ASSERT(n > 0, "should detect ARRAY token collision");
    ASSERT(result->token_conflicts == 1, "exactly 1 token conflict");

    /* Also check via the basic conflict detection path */
    GrammarModification all_mods[2] = { pg_mod, ts_mod };
    ConflictSet *cs = conflict_set_create();
    bool found = detect_conflicts(all_mods, 2, cs);
    ASSERT(found, "basic detect_conflicts should also find it");
    ASSERT(cs->count == 1, "should have exactly 1 conflict");
    if (cs->count > 0) {
        ASSERT(cs->conflicts[0].type == CONFLICT_TOKEN_COLLISION,
               "type should be TOKEN_COLLISION");
    }

    conflict_set_destroy(cs);
    multi_conflict_result_destroy(result);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Scenario 3: Three extensions with cascading conflicts              */
/* ------------------------------------------------------------------ */

static void test_three_extension_cascade(void) {
    printf("test_three_extension_cascade\n");

    ExtensionRegistry *reg = create_extension_registry();

    /* All three define the CAST token and a rule for cast_expr */
    static const char *rhs[] = {"expr", "CAST", "type", NULL};

    GrammarModification mods_pg[2];
    memset(mods_pg, 0, sizeof(mods_pg));
    mods_pg[0].type = MOD_ADD_TOKEN;
    mods_pg[0].u.add_token.name = "CAST";
    mods_pg[0].u.add_token.token_code = 80;
    mods_pg[1].type = MOD_ADD_RULE;
    mods_pg[1].u.add_rule.lhs = "cast_expr";
    mods_pg[1].u.add_rule.rhs = rhs;
    mods_pg[1].u.add_rule.nrhs = 3;
    mods_pg[1].u.add_rule.code = "{ pg_cast(a, t); }";

    GrammarModification mods_oracle[2];
    memset(mods_oracle, 0, sizeof(mods_oracle));
    mods_oracle[0].type = MOD_ADD_TOKEN;
    mods_oracle[0].u.add_token.name = "CAST";
    mods_oracle[0].u.add_token.token_code = 80;
    mods_oracle[1].type = MOD_ADD_RULE;
    mods_oracle[1].u.add_rule.lhs = "cast_expr";
    mods_oracle[1].u.add_rule.rhs = rhs;
    mods_oracle[1].u.add_rule.nrhs = 3;
    mods_oracle[1].u.add_rule.code = "{ oracle_cast(a, t); }";

    GrammarModification mods_mysql[2];
    memset(mods_mysql, 0, sizeof(mods_mysql));
    mods_mysql[0].type = MOD_ADD_TOKEN;
    mods_mysql[0].u.add_token.name = "CAST";
    mods_mysql[0].u.add_token.token_code = 80;
    mods_mysql[1].type = MOD_ADD_RULE;
    mods_mysql[1].u.add_rule.lhs = "cast_expr";
    mods_mysql[1].u.add_rule.rhs = rhs;
    mods_mysql[1].u.add_rule.nrhs = 3;
    mods_mysql[1].u.add_rule.code = "{ mysql_cast(a, t); }";

    MockExtension ext_pg = {
        .name = "postgres-cast", .mods = mods_pg, .nmods = 2,
        .conflict_response = CONFLICT_KEEP_EXISTING,
    };
    MockExtension ext_oracle = {
        .name = "oracle-cast", .mods = mods_oracle, .nmods = 2,
        .conflict_response = CONFLICT_KEEP_EXISTING,
    };
    MockExtension ext_mysql = {
        .name = "mysql-cast", .mods = mods_mysql, .nmods = 2,
        .conflict_response = CONFLICT_KEEP_EXISTING,
    };

    setup_extension(reg, &ext_pg);
    setup_extension(reg, &ext_oracle);
    setup_extension(reg, &ext_mysql);

    MultiGrammarConflictResult *result = multi_conflict_result_create();
    bool found = detect_all_multi_grammar_conflicts(reg, result);

    ASSERT(found, "three-way cascade should detect conflicts");
    ASSERT(result->token_conflicts > 0, "should have token conflicts");
    ASSERT(result->semantic_conflicts > 0, "should have semantic conflicts");

    /* Total conflict points should reflect the three-way interaction */
    ASSERT(result->npoints >= 2, "should have at least 2 conflict points");

    multi_conflict_result_destroy(result);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Scenario 4: Load/unload cycle                                      */
/* ------------------------------------------------------------------ */

static void test_load_unload_cycle(void) {
    printf("test_load_unload_cycle\n");

    ExtensionRegistry *reg = create_extension_registry();

    GrammarModification mod_a = {
        .type = MOD_ADD_TOKEN,
        .u.add_token = { .name = "OVERLAPPING", .token_code = 99 },
    };
    MockExtension ext_a = {
        .name = "ext-a", .mods = &mod_a, .nmods = 1,
    };

    GrammarModification mod_b = {
        .type = MOD_ADD_TOKEN,
        .u.add_token = { .name = "OVERLAPPING", .token_code = 99 },
    };
    MockExtension ext_b = {
        .name = "ext-b", .mods = &mod_b, .nmods = 1,
    };

    ExtensionID id_a = setup_extension(reg, &ext_a);
    ExtensionID id_b = setup_extension(reg, &ext_b);

    /* Phase 1: both loaded -- should have conflicts */
    MultiGrammarConflictResult *result = multi_conflict_result_create();
    uint32_t n = detect_token_conflicts(reg, result);
    ASSERT(n > 0, "phase 1: should have conflicts with both loaded");
    multi_conflict_result_destroy(result);

    /* Phase 2: unload one -- conflicts should disappear */
    unload_extension(reg, id_b);
    result = multi_conflict_result_create();
    n = detect_token_conflicts(reg, result);
    ASSERT(n == 0, "phase 2: should have no conflicts after unload");
    ASSERT(ext_b.unloaded, "ext_b on_unload should have been called");
    multi_conflict_result_destroy(result);

    /* Phase 3: re-load -- conflicts return */
    ext_b.unloaded = false;
    load_extension(reg, id_b, NULL, NULL);
    result = multi_conflict_result_create();
    n = detect_token_conflicts(reg, result);
    ASSERT(n > 0, "phase 3: conflicts should return after re-load");
    multi_conflict_result_destroy(result);

    /* Phase 4: unload both -- no conflicts */
    unload_extension(reg, id_a);
    unload_extension(reg, id_b);
    result = multi_conflict_result_create();
    n = detect_token_conflicts(reg, result);
    ASSERT(n == 0, "phase 4: no conflicts with both unloaded");
    multi_conflict_result_destroy(result);

    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Scenario 5: Conflict resolution via callbacks                      */
/* ------------------------------------------------------------------ */

static void test_conflict_resolution_callbacks(void) {
    printf("test_conflict_resolution_callbacks\n");

    ExtensionRegistry *reg = create_extension_registry();

    GrammarModification mod_a = {
        .type = MOD_ADD_TOKEN,
        .u.add_token = { .name = "DUPE_TOKEN", .token_code = 77 },
    };
    MockExtension ext_a = {
        .name = "resolver-a",
        .mods = &mod_a,
        .nmods = 1,
        .conflict_response = CONFLICT_KEEP_EXISTING,
    };

    GrammarModification mod_b = {
        .type = MOD_ADD_TOKEN,
        .u.add_token = { .name = "DUPE_TOKEN", .token_code = 77 },
    };
    MockExtension ext_b = {
        .name = "resolver-b",
        .mods = &mod_b,
        .nmods = 1,
        .conflict_response = CONFLICT_UNRESOLVED,
    };

    ExtensionID id_a = setup_extension(reg, &ext_a);
    ExtensionID id_b = setup_extension(reg, &ext_b);

    /* Build the combined mods array for basic conflict detection */
    GrammarModification combined[2] = { mod_a, mod_b };
    /* Manually set ext IDs */
    ConflictSet *cs = conflict_set_create();
    conflict_set_add(cs, CONFLICT_TOKEN_COLLISION, 0, 1,
                     id_a, id_b, "DUPE_TOKEN collision");

    /* Try resolution */
    uint32_t unresolved = resolve_conflicts(cs, combined, 2, reg);

    /* ext_a should resolve (KEEP_EXISTING) */
    ASSERT(ext_a.conflict_calls > 0, "ext_a on_conflict should be called");
    ASSERT(unresolved == 0, "conflict should be resolved by ext_a");

    conflict_set_destroy(cs);
    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Scenario 6: Large extension count (stress test)                    */
/* ------------------------------------------------------------------ */

static void test_many_extensions(void) {
    printf("test_many_extensions\n");

    ExtensionRegistry *reg = create_extension_registry();

    /* Register 50 extensions, all adding the same token */
    #define N_STRESS 50
    GrammarModification stress_mods[N_STRESS];
    MockExtension stress_exts[N_STRESS];
    char names[N_STRESS][32];

    for (int i = 0; i < N_STRESS; i++) {
        memset(&stress_mods[i], 0, sizeof(GrammarModification));
        stress_mods[i].type = MOD_ADD_TOKEN;
        stress_mods[i].u.add_token.name = "SHARED_TOKEN";
        stress_mods[i].u.add_token.token_code = 500;

        snprintf(names[i], sizeof(names[i]), "stress-ext-%d", i);
        stress_exts[i].name = names[i];
        stress_exts[i].mods = &stress_mods[i];
        stress_exts[i].nmods = 1;
        stress_exts[i].conflict_response = CONFLICT_UNRESOLVED;

        setup_extension(reg, &stress_exts[i]);
    }

    uint32_t loaded = get_loaded_extension_count(reg);
    ASSERT(loaded == N_STRESS, "should have all extensions loaded");

    MultiGrammarConflictResult *result = multi_conflict_result_create();
    uint32_t n = detect_token_conflicts(reg, result);

    ASSERT(n > 0, "should detect token conflict among many extensions");
    if (result->npoints > 0) {
        /* The internal TokenEntry collector has a fixed-size array (16),
        ** so not all 50 extensions will be recorded.  Just verify we
        ** detected the conflict with multiple contexts. */
        ASSERT(result->points[0].ncontexts >= 2,
               "should have multiple extensions as contexts");
    }

    multi_conflict_result_destroy(result);
    destroy_extension_registry(reg);
    #undef N_STRESS
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== Multi-Extension Integration Tests ===\n");

    test_non_conflicting_extensions();
    test_array_token_collision();
    test_three_extension_cascade();
    test_load_unload_cycle();
    test_conflict_resolution_callbacks();
    test_many_extensions();

    printf("\n%d tests, %d failures\n", test_count, fail_count);
    return fail_count > 0 ? 1 : 0;
}
