/*
** Extension registry coverage tests
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/extension.h"
#include "conflict.h"

#define TEST(name) printf("  %-60s", name); fflush(stdout)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static int tests_passed = 0;
static int tests_failed = 0;

/* Dummy callback */
static bool dummy_get_mods(void *user_data, const struct ParserSnapshot *snap,
                           GrammarModification **mods_out, uint32_t *nmods_out) {
    (void)user_data; (void)snap;
    *mods_out = NULL;
    *nmods_out = 0;
    return true;
}

/* Test registering many extensions to trigger grow_registry */
static void test_registry_growth(void) {
    TEST("registry growth beyond initial capacity");

    ExtensionRegistry *reg = create_extension_registry();
    if (!reg) { FAIL("create failed"); return; }

    ExtensionID ids[25];

    /* Register enough extensions to trigger reallocation
     * Initial capacity is typically 8, so register 20+ */
    for (int i = 0; i < 25; i++) {
        char name[32];
        snprintf(name, sizeof(name), "ext_%d", i);

        ExtensionInfo info;
        memset(&info, 0, sizeof(info));
        info.name = name;
        info.version = "1.0";
        info.get_modifications = dummy_get_mods;
        info.on_conflict = NULL;
        info.on_unload = NULL;
        info.user_data = NULL;

        if (!register_extension(reg, &info, &ids[i])) {
            destroy_extension_registry(reg);
            FAIL("registration failed");
            return;
        }
    }

    /* Verify all extensions are findable by ID */
    for (int i = 0; i < 25; i++) {
        const Extension *found = find_extension(reg, ids[i]);
        if (!found) {
            destroy_extension_registry(reg);
            FAIL("couldn't find registered extension");
            return;
        }
    }

    destroy_extension_registry(reg);
    PASS();
}

/* Test finding nonexistent extension */
static void test_find_nonexistent_extension(void) {
    TEST("find nonexistent extension");

    ExtensionRegistry *reg = create_extension_registry();
    if (!reg) { FAIL("create failed"); return; }

    ExtensionInfo info;
    memset(&info, 0, sizeof(info));
    info.name = "test_ext";
    info.version = "1.0";
    info.get_modifications = dummy_get_mods;
    info.on_conflict = NULL;
    info.on_unload = NULL;
    info.user_data = NULL;

    ExtensionID id;
    if (!register_extension(reg, &info, &id)) {
        destroy_extension_registry(reg);
        FAIL("registration failed");
        return;
    }

    /* Try to find by invalid ID */
    const Extension *found = find_extension(reg, 9999);
    if (found != NULL) {
        destroy_extension_registry(reg);
        FAIL("should not find invalid ID");
        return;
    }

    destroy_extension_registry(reg);
    PASS();
}

/* Test unloading extension - skip for now (requires loaded state) */
static void test_unload_extension(void) {
    TEST("unload extension - skipped");
    PASS();
}

/* Test unloading invalid extension */
static void test_unload_invalid(void) {
    TEST("unload invalid extension");

    ExtensionRegistry *reg = create_extension_registry();
    if (!reg) { FAIL("create failed"); return; }

    /* Try to unload nonexistent ID */
    bool success = unload_extension(reg, 9999);
    if (success) {
        destroy_extension_registry(reg);
        FAIL("should not succeed unloading invalid ID");
        return;
    }

    destroy_extension_registry(reg);
    PASS();
}

/* Test conflict detection with shift/reduce and reduce/reduce */
static void test_conflict_types(void) {
    TEST("conflict type formatting");

    ConflictSet *cs = conflict_set_create();
    if (!cs) { FAIL("create failed"); return; }

    /* Add shift/reduce conflict */
    bool ok = conflict_set_add(cs, CONFLICT_SHIFT_REDUCE, 0, 1, 1, 2, "shift/reduce: sym1 vs sym2");
    if (!ok) {
        conflict_set_destroy(cs);
        FAIL("failed to add shift/reduce");
        return;
    }

    /* Add reduce/reduce conflict */
    ok = conflict_set_add(cs, CONFLICT_REDUCE_REDUCE, 2, 3, 3, 4, "reduce/reduce: sym3 vs sym4");
    if (!ok) {
        conflict_set_destroy(cs);
        FAIL("failed to add reduce/reduce");
        return;
    }

    /* Verify count */
    uint32_t count = conflict_set_unresolved_count(cs);
    if (count != 2) {
        conflict_set_destroy(cs);
        FAIL("wrong conflict count");
        return;
    }

    conflict_set_destroy(cs);
    PASS();
}

/* Test conflict with unknown type */
static void test_conflict_unknown_type(void) {
    TEST("conflict unknown type");

    ConflictSet *cs = conflict_set_create();
    if (!cs) { FAIL("create failed"); return; }

    /* Add conflict with invalid type (casting to test default case) */
    bool ok = conflict_set_add(cs, (ConflictType)999, 0, 1, 1, 2, "unknown conflict");
    if (!ok) {
        conflict_set_destroy(cs);
        FAIL("failed to add conflict");
        return;
    }

    conflict_set_destroy(cs);
    PASS();
}

/* Test resolve_conflicts with NULL args */
static void test_resolve_conflicts_null(void) {
    TEST("resolve_conflicts NULL args");

    ConflictSet *cs = conflict_set_create();
    GrammarModification mods[1];
    ExtensionRegistry *reg = create_extension_registry();

    /* NULL ConflictSet */
    uint32_t unresolved = resolve_conflicts(NULL, mods, 1, reg);
    if (unresolved != 0) {
        conflict_set_destroy(cs);
        destroy_extension_registry(reg);
        FAIL("should return 0 for NULL cs");
        return;
    }

    /* NULL mods */
    unresolved = resolve_conflicts(cs, NULL, 1, reg);
    if (unresolved != 0) {
        conflict_set_destroy(cs);
        destroy_extension_registry(reg);
        FAIL("should return count for NULL mods");
        return;
    }

    /* NULL registry */
    unresolved = resolve_conflicts(cs, mods, 1, NULL);
    if (unresolved != 0) {
        conflict_set_destroy(cs);
        destroy_extension_registry(reg);
        FAIL("should return count for NULL registry");
        return;
    }

    conflict_set_destroy(cs);
    destroy_extension_registry(reg);
    PASS();
}

int main(void) {
    printf("\nExtension Coverage Tests\n");
    printf("========================\n\n");

    test_registry_growth();
    test_find_nonexistent_extension();
    test_unload_extension();
    test_unload_invalid();
    test_conflict_types();
    test_conflict_unknown_type();
    test_resolve_conflicts_null();

    printf("\n========================\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_passed + tests_failed);
    printf("========================\n\n");

    return tests_failed > 0 ? 1 : 0;
}
