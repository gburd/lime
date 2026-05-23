/*
** Unit tests for the extension system.
**
** Tests the extension registry, registration, loading/unloading,
** conflict detection, and snapshot modification pipeline.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "parser.h"
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
/*  Mock extension callbacks                                           */
/* ------------------------------------------------------------------ */

typedef struct MockExtData {
    bool get_mods_called;
    bool unload_called;
    GrammarModification *mods;
    uint32_t nmods;
    bool get_mods_fail;  /* If true, get_modifications returns false */
} MockExtData;

static bool mock_get_modifications(
    void *user_data,
    const struct ParserSnapshot *base,
    GrammarModification **mods_out,
    uint32_t *nmods_out
) {
    MockExtData *d = (MockExtData *)user_data;
    d->get_mods_called = true;
    (void)base;
    if (d->get_mods_fail) return false;
    *mods_out = d->mods;
    *nmods_out = d->nmods;
    return true;
}

static void mock_on_unload(void *user_data) {
    MockExtData *d = (MockExtData *)user_data;
    d->unload_called = true;
}

/* ------------------------------------------------------------------ */
/*  Test: registry create/destroy                                      */
/* ------------------------------------------------------------------ */

static void test_registry_lifecycle(void) {
    printf("test_registry_lifecycle\n");

    ExtensionRegistry *reg = create_extension_registry();
    ASSERT(reg != NULL, "create_extension_registry should succeed");

    uint32_t count = get_loaded_extension_count(reg);
    ASSERT(count == 0, "new registry should have 0 loaded extensions");

    destroy_extension_registry(reg);
    /* Should not crash */
    ASSERT(true, "destroy_extension_registry should not crash");

    /* Destroy NULL should be safe */
    destroy_extension_registry(NULL);
    ASSERT(true, "destroy_extension_registry(NULL) should be safe");
}

/* ------------------------------------------------------------------ */
/*  Test: register extension                                           */
/* ------------------------------------------------------------------ */

static void test_register_extension(void) {
    printf("test_register_extension\n");

    ExtensionRegistry *reg = create_extension_registry();
    ASSERT(reg != NULL, "create registry");

    MockExtData data = {0};

    ExtensionInfo info = {
        .name = "test-ext",
        .version = "1.0.0",
        .get_modifications = mock_get_modifications,
        .on_conflict = NULL,
        .on_unload = mock_on_unload,
        .user_data = &data,
    };

    ExtensionID id = 0;
    bool ok = register_extension(reg, &info, &id);
    ASSERT(ok, "register_extension should succeed");
    ASSERT(id > 0, "assigned ID should be > 0");

    /* Look it up */
    const Extension *ext = find_extension(reg, id);
    ASSERT(ext != NULL, "find_extension should find it");
    ASSERT(ext->id == id, "found extension should have correct ID");
    ASSERT(strcmp(ext->name, "test-ext") == 0, "name should match");
    ASSERT(ext->state == EXT_REGISTERED, "state should be REGISTERED");

    /* Duplicate name should fail */
    ExtensionID id2 = 0;
    ok = register_extension(reg, &info, &id2);
    ASSERT(!ok, "duplicate name should be rejected");

    /* NULL info should fail */
    ok = register_extension(reg, NULL, &id2);
    ASSERT(!ok, "NULL info should be rejected");

    /* Missing name should fail */
    ExtensionInfo bad_info = info;
    bad_info.name = NULL;
    ok = register_extension(reg, &bad_info, &id2);
    ASSERT(!ok, "NULL name should be rejected");

    /* Missing get_modifications should fail */
    bad_info = info;
    bad_info.name = "other";
    bad_info.get_modifications = NULL;
    ok = register_extension(reg, &bad_info, &id2);
    ASSERT(!ok, "NULL get_modifications should be rejected");

    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: load and unload extension                                    */
/* ------------------------------------------------------------------ */

static void test_load_unload(void) {
    printf("test_load_unload\n");

    ExtensionRegistry *reg = create_extension_registry();
    ASSERT(reg != NULL, "create registry");

    /* Set up a modification */
    GrammarModification mod = {
        .type = MOD_ADD_TOKEN,
        .description = "Add JSONB token",
        .u.add_token = {
            .name = "TK_JSONB",
            .lexeme = "@>",
            .token_code = -1,
        },
    };

    MockExtData data = {
        .mods = &mod,
        .nmods = 1,
    };

    ExtensionInfo info = {
        .name = "jsonb-ext",
        .version = "0.1.0",
        .get_modifications = mock_get_modifications,
        .on_unload = mock_on_unload,
        .user_data = &data,
    };

    ExtensionID id;
    register_extension(reg, &info, &id);

    /* Load it */
    char *error = NULL;
    bool ok = load_extension(reg, id, NULL, &error);
    ASSERT(ok, "load_extension should succeed");
    ASSERT(error == NULL, "error should be NULL on success");
    ASSERT(data.get_mods_called, "get_modifications should have been called");

    const Extension *ext = find_extension(reg, id);
    ASSERT(ext != NULL && ext->state == EXT_LOADED, "state should be LOADED");

    uint32_t count = get_loaded_extension_count(reg);
    ASSERT(count == 1, "should have 1 loaded extension");

    /* Loading again should fail (already loaded) */
    ok = load_extension(reg, id, NULL, &error);
    ASSERT(!ok, "loading already-loaded should fail");
    free(error);
    error = NULL;

    /* Unload it */
    ok = unload_extension(reg, id);
    ASSERT(ok, "unload_extension should succeed");
    ASSERT(data.unload_called, "on_unload should have been called");

    ext = find_extension(reg, id);
    ASSERT(ext != NULL && ext->state == EXT_UNLOADED, "state should be UNLOADED");

    count = get_loaded_extension_count(reg);
    ASSERT(count == 0, "should have 0 loaded extensions after unload");

    /* Can re-load after unload */
    data.get_mods_called = false;
    data.unload_called = false;
    ok = load_extension(reg, id, NULL, &error);
    ASSERT(ok, "re-loading after unload should succeed");
    ASSERT(data.get_mods_called, "get_modifications should be called again");

    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: load failure (get_modifications returns false)               */
/* ------------------------------------------------------------------ */

static void test_load_failure(void) {
    printf("test_load_failure\n");

    ExtensionRegistry *reg = create_extension_registry();

    MockExtData data = {
        .get_mods_fail = true,
    };

    ExtensionInfo info = {
        .name = "bad-ext",
        .version = "0.0.1",
        .get_modifications = mock_get_modifications,
        .user_data = &data,
    };

    ExtensionID id;
    register_extension(reg, &info, &id);

    char *error = NULL;
    bool ok = load_extension(reg, id, NULL, &error);
    ASSERT(!ok, "load should fail when get_modifications fails");
    ASSERT(error != NULL, "error message should be set");
    free(error);

    const Extension *ext = find_extension(reg, id);
    ASSERT(ext != NULL && ext->state == EXT_ERROR, "state should be ERROR");

    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: find nonexistent extension                                   */
/* ------------------------------------------------------------------ */

static void test_find_nonexistent(void) {
    printf("test_find_nonexistent\n");

    ExtensionRegistry *reg = create_extension_registry();

    const Extension *ext = find_extension(reg, 999);
    ASSERT(ext == NULL, "nonexistent ID should return NULL");

    ext = find_extension(reg, 0);
    ASSERT(ext == NULL, "ID 0 should return NULL");

    ext = find_extension(NULL, 1);
    ASSERT(ext == NULL, "NULL registry should return NULL");

    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: unload nonexistent / not loaded                              */
/* ------------------------------------------------------------------ */

static void test_unload_edge_cases(void) {
    printf("test_unload_edge_cases\n");

    ExtensionRegistry *reg = create_extension_registry();

    /* Unload nonexistent */
    bool ok = unload_extension(reg, 999);
    ASSERT(!ok, "unloading nonexistent should fail");

    /* Register but don't load, then try unload */
    MockExtData data = {0};
    ExtensionInfo info = {
        .name = "unloaded",
        .version = "1.0",
        .get_modifications = mock_get_modifications,
        .user_data = &data,
    };
    ExtensionID id;
    register_extension(reg, &info, &id);

    ok = unload_extension(reg, id);
    ASSERT(!ok, "unloading REGISTERED (not loaded) should fail");

    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: multiple extensions                                          */
/* ------------------------------------------------------------------ */

static void test_multiple_extensions(void) {
    printf("test_multiple_extensions\n");

    ExtensionRegistry *reg = create_extension_registry();

    MockExtData data1 = {0}, data2 = {0}, data3 = {0};

    ExtensionInfo info1 = {
        .name = "ext-alpha",
        .version = "1.0",
        .get_modifications = mock_get_modifications,
        .user_data = &data1,
    };
    ExtensionInfo info2 = {
        .name = "ext-beta",
        .version = "2.0",
        .get_modifications = mock_get_modifications,
        .user_data = &data2,
    };
    ExtensionInfo info3 = {
        .name = "ext-gamma",
        .version = "3.0",
        .get_modifications = mock_get_modifications,
        .user_data = &data3,
    };

    ExtensionID id1, id2, id3;
    register_extension(reg, &info1, &id1);
    register_extension(reg, &info2, &id2);
    register_extension(reg, &info3, &id3);

    ASSERT(id1 != id2 && id2 != id3 && id1 != id3,
           "all IDs should be unique");

    /* Load two of three */
    load_extension(reg, id1, NULL, NULL);
    load_extension(reg, id3, NULL, NULL);

    uint32_t count = get_loaded_extension_count(reg);
    ASSERT(count == 2, "should have 2 loaded extensions");

    /* Unload one */
    unload_extension(reg, id1);
    count = get_loaded_extension_count(reg);
    ASSERT(count == 1, "should have 1 loaded extension after unload");

    destroy_extension_registry(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: conflict detection - token collision                         */
/* ------------------------------------------------------------------ */

static void test_conflict_token_collision(void) {
    printf("test_conflict_token_collision\n");

    GrammarModification mods[2];
    memset(mods, 0, sizeof(mods));

    mods[0].type = MOD_ADD_TOKEN;
    mods[0].description = "ext1: add JSONB";
    mods[0].u.add_token.name = "TK_JSONB";
    mods[0].u.add_token.lexeme = "@>";
    mods[0].u.add_token.token_code = -1;

    mods[1].type = MOD_ADD_TOKEN;
    mods[1].description = "ext2: add JSONB (conflict)";
    mods[1].u.add_token.name = "TK_JSONB";
    mods[1].u.add_token.lexeme = "@>";
    mods[1].u.add_token.token_code = -1;

    ConflictSet *cs = conflict_set_create();
    ASSERT(cs != NULL, "conflict_set_create should succeed");

    bool found = detect_conflicts(mods, 2, cs);
    ASSERT(found, "should detect token collision");
    ASSERT(cs->count > 0, "conflict set should have entries");

    if (cs->count > 0) {
        ASSERT(cs->conflicts[0].type == CONFLICT_TOKEN_COLLISION,
               "conflict type should be TOKEN_COLLISION");
    }

    uint32_t unresolved = conflict_set_unresolved_count(cs);
    ASSERT(unresolved > 0, "conflicts should be unresolved");

    conflict_set_destroy(cs);
}

/* ------------------------------------------------------------------ */
/*  Test: conflict detection - no conflicts                            */
/* ------------------------------------------------------------------ */

static void test_conflict_no_conflict(void) {
    printf("test_conflict_no_conflict\n");

    GrammarModification mods[2];
    memset(mods, 0, sizeof(mods));

    mods[0].type = MOD_ADD_TOKEN;
    mods[0].u.add_token.name = "TK_JSONB";
    mods[0].u.add_token.lexeme = "@>";

    mods[1].type = MOD_ADD_TOKEN;
    mods[1].u.add_token.name = "TK_ARROW";
    mods[1].u.add_token.lexeme = "->";

    ConflictSet *cs = conflict_set_create();

    bool found = detect_conflicts(mods, 2, cs);
    ASSERT(!found, "should not detect conflicts for different tokens");
    ASSERT(cs->count == 0, "conflict set should be empty");

    conflict_set_destroy(cs);
}

/* ------------------------------------------------------------------ */
/*  Test: conflict set operations                                      */
/* ------------------------------------------------------------------ */

static void test_conflict_set_ops(void) {
    printf("test_conflict_set_ops\n");

    ConflictSet *cs = conflict_set_create();
    ASSERT(cs != NULL, "create should succeed");
    ASSERT(cs->count == 0, "initial count should be 0");

    bool ok = conflict_set_add(cs, CONFLICT_TOKEN_COLLISION,
                               0, 1, 1, 2, "test collision");
    ASSERT(ok, "add should succeed");
    ASSERT(cs->count == 1, "count should be 1 after add");

    ok = conflict_set_add(cs, CONFLICT_DUPLICATE_RULE,
                          2, 3, 1, 3, "test duplicate");
    ASSERT(ok, "second add should succeed");
    ASSERT(cs->count == 2, "count should be 2");

    uint32_t unresolved = conflict_set_unresolved_count(cs);
    ASSERT(unresolved == 2, "both should be unresolved");

    /* Mark one as resolved */
    cs->conflicts[0].resolved = true;
    unresolved = conflict_set_unresolved_count(cs);
    ASSERT(unresolved == 1, "one should remain unresolved");

    conflict_set_destroy(cs);

    /* NULL destroy should be safe */
    conflict_set_destroy(NULL);
    ASSERT(true, "destroy(NULL) should not crash");
}

/* ------------------------------------------------------------------ */
/*  Test: conflict detection - duplicate rule                          */
/* ------------------------------------------------------------------ */

static void test_conflict_duplicate_rule(void) {
    printf("test_conflict_duplicate_rule\n");

    static const char *rhs[] = {"A", "PLUS", "B", NULL};

    GrammarModification mods[2];
    memset(mods, 0, sizeof(mods));

    mods[0].type = MOD_ADD_RULE;
    mods[0].description = "ext1: expr -> A PLUS B";
    mods[0].u.add_rule.lhs = "expr";
    mods[0].u.add_rule.rhs = rhs;
    mods[0].u.add_rule.nrhs = 3;
    mods[0].u.add_rule.precedence = -1;

    mods[1].type = MOD_ADD_RULE;
    mods[1].description = "ext2: expr -> A PLUS B (duplicate)";
    mods[1].u.add_rule.lhs = "expr";
    mods[1].u.add_rule.rhs = rhs;
    mods[1].u.add_rule.nrhs = 3;
    mods[1].u.add_rule.precedence = -1;

    ConflictSet *cs = conflict_set_create();
    ASSERT(cs != NULL, "create conflict set");

    bool found = detect_conflicts(mods, 2, cs);
    ASSERT(found, "should detect duplicate rule");
    ASSERT(cs->count > 0, "should have conflicts");
    if (cs->count > 0) {
        ASSERT(cs->conflicts[0].type == CONFLICT_DUPLICATE_RULE,
               "type should be DUPLICATE_RULE");
    }

    conflict_set_destroy(cs);
}

/* ------------------------------------------------------------------ */
/*  Test: conflict detection - precedence clash                        */
/* ------------------------------------------------------------------ */

static void test_conflict_precedence_clash(void) {
    printf("test_conflict_precedence_clash\n");

    GrammarModification mods[2];
    memset(mods, 0, sizeof(mods));

    mods[0].type = MOD_MODIFY_PRECEDENCE;
    mods[0].description = "ext1: PLUS left prec 10";
    mods[0].u.modify_prec.symbol = "PLUS";
    mods[0].u.modify_prec.new_precedence = 10;
    mods[0].u.modify_prec.new_assoc = 1;  /* left */

    mods[1].type = MOD_MODIFY_PRECEDENCE;
    mods[1].description = "ext2: PLUS right prec 20";
    mods[1].u.modify_prec.symbol = "PLUS";
    mods[1].u.modify_prec.new_precedence = 20;
    mods[1].u.modify_prec.new_assoc = 2;  /* right */

    ConflictSet *cs = conflict_set_create();
    ASSERT(cs != NULL, "create conflict set");

    bool found = detect_conflicts(mods, 2, cs);
    ASSERT(found, "should detect precedence clash");
    ASSERT(cs->count > 0, "should have conflicts");
    if (cs->count > 0) {
        ASSERT(cs->conflicts[0].type == CONFLICT_PRECEDENCE_CLASH,
               "type should be PRECEDENCE_CLASH");
    }

    conflict_set_destroy(cs);
}

/* ------------------------------------------------------------------ */
/*  Test: detect_conflicts with NULL/invalid args                      */
/* ------------------------------------------------------------------ */

static void test_detect_conflicts_null_args(void) {
    printf("test_detect_conflicts_null_args\n");

    ConflictSet *cs = conflict_set_create();
    ASSERT(cs != NULL, "create conflict set");

    GrammarModification mod;
    memset(&mod, 0, sizeof(mod));

    /* All should return false without crashing */
    bool r1 = detect_conflicts(NULL, 1, cs);
    ASSERT(!r1, "NULL mods should return false");

    bool r2 = detect_conflicts(&mod, 0, cs);
    ASSERT(!r2, "0 nmods should return false");

    bool r3 = detect_conflicts(&mod, 1, NULL);
    ASSERT(!r3, "NULL cs should return false");

    conflict_set_destroy(cs);
}

/* ------------------------------------------------------------------ */
/*  Test: snapshot clone                                               */
/* ------------------------------------------------------------------ */

static void test_snapshot_clone(void) {
    printf("test_snapshot_clone\n");

    /* Clone of NULL creates an empty snapshot (version 0, refcount 1) */
    ParserSnapshot *cloned = clone_snapshot(NULL);
    ASSERT(cloned != NULL, "clone_snapshot(NULL) should create empty snapshot");
    if (cloned) {
        ASSERT(cloned->version == 0, "empty snapshot version should be 0");
        ASSERT(atomic_load(&cloned->refcount) == 1, "empty snapshot refcount should be 1");
        snapshot_release(cloned);
    }

    /* Create a minimal snapshot and clone it */
    ParserSnapshot *snap = calloc(1, sizeof(ParserSnapshot));
    ASSERT(snap != NULL, "allocate test snapshot");
    snap->version = 42;
    atomic_init(&snap->refcount, 1);
    snap->nsymbol = 5;
    snap->nterminal = 3;
    snap->nrule = 2;
    snap->nstate = 4;
    snap->action_count = 3;
    snap->lookahead_count = 3;

    snap->yy_action = malloc(3 * sizeof(uint16_t));
    snap->yy_lookahead = malloc(3 * sizeof(uint16_t));
    snap->yy_shift_ofst = malloc(4 * sizeof(int32_t));
    snap->yy_reduce_ofst = malloc(4 * sizeof(int32_t));
    snap->yy_default = malloc(4 * sizeof(uint16_t));

    if (snap->yy_action) { snap->yy_action[0] = 10; snap->yy_action[1] = 20; snap->yy_action[2] = 30; }
    if (snap->yy_lookahead) { snap->yy_lookahead[0] = 1; snap->yy_lookahead[1] = 2; snap->yy_lookahead[2] = 3; }
    if (snap->yy_shift_ofst) { snap->yy_shift_ofst[0] = -1; }
    if (snap->yy_reduce_ofst) { snap->yy_reduce_ofst[0] = -2; }
    if (snap->yy_default) { snap->yy_default[0] = 99; }

    cloned = clone_snapshot(snap);
    ASSERT(cloned != NULL, "clone should succeed");

    if (cloned) {
        ASSERT(cloned->version == snap->version + 1,
               "cloned version should be incremented");
        ASSERT(cloned->nsymbol == snap->nsymbol, "nsymbol should match");
        ASSERT(cloned->nterminal == snap->nterminal, "nterminal should match");
        ASSERT(cloned->nrule == snap->nrule, "nrule should match");
        ASSERT(cloned->nstate == snap->nstate, "nstate should match");
        ASSERT(cloned->action_count == snap->action_count, "action_count should match");

        /* Verify it's a deep copy, not the same pointers */
        ASSERT(cloned->yy_action != snap->yy_action,
               "yy_action should be a separate allocation");

        if (cloned->yy_action) {
            ASSERT(cloned->yy_action[0] == 10, "action data should be copied");
            ASSERT(cloned->yy_action[2] == 30, "action data should be copied (end)");
        }

        snapshot_release(cloned);
    }

    snapshot_release(snap);
}

/* ------------------------------------------------------------------ */
/*  Test: global registry wrappers                                     */
/* ------------------------------------------------------------------ */

static void test_global_registry(void) {
    printf("test_global_registry\n");

    bool ok = lemon_extension_registry_init();
    ASSERT(ok, "lemon_extension_registry_init should succeed");

    /* Double init should also succeed */
    ok = lemon_extension_registry_init();
    ASSERT(ok, "double init should be idempotent");

    lemon_extension_registry_destroy();
    /* Should not crash */
    ASSERT(true, "destroy should not crash");

    /* Double destroy should be safe */
    lemon_extension_registry_destroy();
    ASSERT(true, "double destroy should be safe");
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== Extension System Tests ===\n");

    test_registry_lifecycle();
    test_register_extension();
    test_load_unload();
    test_load_failure();
    test_find_nonexistent();
    test_unload_edge_cases();
    test_multiple_extensions();
    test_conflict_token_collision();
    test_conflict_no_conflict();
    test_conflict_duplicate_rule();
    test_conflict_precedence_clash();
    test_detect_conflicts_null_args();
    test_conflict_set_ops();
    test_snapshot_clone();
    test_global_registry();

    printf("\n%d tests, %d failures\n", test_count, fail_count);
    return fail_count > 0 ? 1 : 0;
}
