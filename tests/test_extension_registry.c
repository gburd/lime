/*
** Comprehensive tests for extension_registry.c
**
** Tests cover:
**   - Registry creation and destruction
**   - Extension registration (success and duplicate detection)
**   - Extension find (hit and miss)
**   - Extension unregister
**   - Extension count
**   - Extension foreach iteration
**   - Dependency checking (missing deps, cycles)
**   - Conflict checking
**   - Topological sort / get_order
**   - Hash table operations (rehashing, collisions)
**   - Edge cases (NULL args, empty registry, large registries)
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "extension_registry.h"

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
/*  Test: registry creation and destruction                           */
/* ------------------------------------------------------------------ */

static void test_registry_lifecycle(void) {
    printf("test_registry_lifecycle\n");

    ExtensionRegistry *reg = extension_registry_create();
    ASSERT(reg != NULL, "create should succeed");
    ASSERT(extension_registry_count(reg) == 0, "new registry should be empty");

    extension_registry_destroy(reg);

    /* NULL destroy should be safe */
    extension_registry_destroy(NULL);
    ASSERT(true, "destroy(NULL) should be safe");
}

/* ------------------------------------------------------------------ */
/*  Test: register extension                                           */
/* ------------------------------------------------------------------ */

static void test_register_extension(void) {
    printf("test_register_extension\n");

    ExtensionRegistry *reg = extension_registry_create();

    const char *empty_requires[] = { NULL };

    GrammarExtensionMetadata meta = {
        .name = "test-ext",
        .version = "1.0",
        .strategy = DISAMBIG_PRIORITY,
        .priority = 10,
        .policy = EXEC_SEQUENTIAL,
        .oracle = NULL,
        .conflict_threshold = 0.5f,
        .requires = empty_requires,
        .conflicts_with = empty_requires,
        .modifications = NULL,
        .nmodifications = 0,
    };

    bool ok = extension_registry_register(reg, &meta);
    ASSERT(ok, "register should succeed");
    ASSERT(extension_registry_count(reg) == 1, "count should be 1");

    /* Duplicate registration should fail */
    ok = extension_registry_register(reg, &meta);
    ASSERT(!ok, "duplicate registration should fail");
    ASSERT(extension_registry_count(reg) == 1, "count should still be 1");

    extension_registry_destroy(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: register NULL or empty name (should fail)                   */
/* ------------------------------------------------------------------ */

static void test_register_invalid_name(void) {
    printf("test_register_invalid_name\n");

    ExtensionRegistry *reg = extension_registry_create();

    /* NULL metadata */
    bool ok = extension_registry_register(reg, NULL);
    ASSERT(!ok, "register NULL metadata should fail");

    /* NULL name */
    GrammarExtensionMetadata meta_null = {
        .name = NULL,
        .version = "1.0",
    };
    ok = extension_registry_register(reg, &meta_null);
    ASSERT(!ok, "register NULL name should fail");

    /* Empty name */
    GrammarExtensionMetadata meta_empty = {
        .name = "",
        .version = "1.0",
    };
    ok = extension_registry_register(reg, &meta_empty);
    ASSERT(!ok, "register empty name should fail");

    /* NULL registry */
    GrammarExtensionMetadata meta = {
        .name = "test",
        .version = "1.0",
    };
    ok = extension_registry_register(NULL, &meta);
    ASSERT(!ok, "register to NULL registry should fail");

    extension_registry_destroy(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: find extension                                               */
/* ------------------------------------------------------------------ */

static void test_find_extension(void) {
    printf("test_find_extension\n");

    ExtensionRegistry *reg = extension_registry_create();

    const char *empty[] = { NULL };
    GrammarExtensionMetadata meta = {
        .name = "findme",
        .version = "2.0",
        .priority = 42,
        .requires = empty,
        .conflicts_with = empty,
    };

    extension_registry_register(reg, &meta);

    /* Find existing */
    const GrammarExtensionMetadata *found =
        extension_registry_find(reg, "findme");
    ASSERT(found != NULL, "find should succeed");
    ASSERT(strcmp(found->name, "findme") == 0, "name should match");
    ASSERT(strcmp(found->version, "2.0") == 0, "version should match");
    ASSERT(found->priority == 42, "priority should match");

    /* Find non-existing */
    found = extension_registry_find(reg, "nothere");
    ASSERT(found == NULL, "find non-existing should return NULL");

    /* NULL args */
    found = extension_registry_find(NULL, "findme");
    ASSERT(found == NULL, "find in NULL registry should return NULL");

    found = extension_registry_find(reg, NULL);
    ASSERT(found == NULL, "find NULL name should return NULL");

    extension_registry_destroy(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: unregister extension                                         */
/* ------------------------------------------------------------------ */

static void test_unregister_extension(void) {
    printf("test_unregister_extension\n");

    ExtensionRegistry *reg = extension_registry_create();

    const char *empty[] = { NULL };
    GrammarExtensionMetadata meta1 = {
        .name = "ext1",
        .version = "1.0",
        .requires = empty,
        .conflicts_with = empty,
    };
    GrammarExtensionMetadata meta2 = {
        .name = "ext2",
        .version = "1.0",
        .requires = empty,
        .conflicts_with = empty,
    };

    extension_registry_register(reg, &meta1);
    extension_registry_register(reg, &meta2);
    ASSERT(extension_registry_count(reg) == 2, "count should be 2");

    /* Unregister ext1 */
    bool ok = extension_registry_unregister(reg, "ext1");
    ASSERT(ok, "unregister should succeed");
    ASSERT(extension_registry_count(reg) == 1, "count should be 1");
    ASSERT(extension_registry_find(reg, "ext1") == NULL, "ext1 should be gone");
    ASSERT(extension_registry_find(reg, "ext2") != NULL, "ext2 should remain");

    /* Unregister non-existing */
    ok = extension_registry_unregister(reg, "nothere");
    ASSERT(!ok, "unregister non-existing should fail");

    /* NULL args */
    ok = extension_registry_unregister(NULL, "ext2");
    ASSERT(!ok, "unregister from NULL registry should fail");

    ok = extension_registry_unregister(reg, NULL);
    ASSERT(!ok, "unregister NULL name should fail");

    extension_registry_destroy(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: foreach iteration                                            */
/* ------------------------------------------------------------------ */

static int foreach_count;
static bool foreach_visitor(const GrammarExtensionMetadata *meta,
                             void *user_data) {
    (void)meta;
    int *counter = (int *)user_data;
    (*counter)++;
    return true;  /* Continue */
}

static bool foreach_early_stop(const GrammarExtensionMetadata *meta,
                                void *user_data) {
    (void)meta;
    int *counter = (int *)user_data;
    (*counter)++;
    return false;  /* Stop after first */
}

static void test_foreach(void) {
    printf("test_foreach\n");

    ExtensionRegistry *reg = extension_registry_create();

    const char *empty[] = { NULL };
    for (int i = 0; i < 5; i++) {
        char name[32];
        snprintf(name, sizeof(name), "ext%d", i);
        GrammarExtensionMetadata meta = {
            .name = name,
            .version = "1.0",
            .requires = empty,
            .conflicts_with = empty,
        };
        extension_registry_register(reg, &meta);
    }

    /* Full iteration */
    foreach_count = 0;
    extension_registry_foreach(reg, foreach_visitor, &foreach_count);
    ASSERT(foreach_count == 5, "should visit all 5 extensions");

    /* Early stop */
    foreach_count = 0;
    extension_registry_foreach(reg, foreach_early_stop, &foreach_count);
    ASSERT(foreach_count == 1, "should stop after first");

    /* NULL safety */
    extension_registry_foreach(NULL, foreach_visitor, &foreach_count);
    extension_registry_foreach(reg, NULL, &foreach_count);
    ASSERT(true, "NULL args should be safe");

    extension_registry_destroy(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: dependency checking - missing dependency                    */
/* ------------------------------------------------------------------ */

static void test_check_dependencies_missing(void) {
    printf("test_check_dependencies_missing\n");

    ExtensionRegistry *reg = extension_registry_create();

    const char *empty[] = { NULL };
    const char *requires_missing[] = { "nonexistent", NULL };

    GrammarExtensionMetadata meta = {
        .name = "dependent",
        .version = "1.0",
        .requires = requires_missing,
        .conflicts_with = empty,
    };
    extension_registry_register(reg, &meta);

    char *error = NULL;
    bool ok = extension_registry_check_dependencies(reg, &error);
    ASSERT(!ok, "check should fail for missing dependency");
    ASSERT(error != NULL, "error message should be set");
    ASSERT(strstr(error, "nonexistent") != NULL,
           "error should mention missing dependency");

    free(error);
    extension_registry_destroy(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: dependency checking - cycle detection                       */
/* ------------------------------------------------------------------ */

static void test_check_dependencies_cycle(void) {
    printf("test_check_dependencies_cycle\n");

    ExtensionRegistry *reg = extension_registry_create();

    const char *empty[] = { NULL };
    const char *a_requires_b[] = { "ext-b", NULL };
    const char *b_requires_c[] = { "ext-c", NULL };
    const char *c_requires_a[] = { "ext-a", NULL };

    GrammarExtensionMetadata meta_a = {
        .name = "ext-a",
        .version = "1.0",
        .requires = a_requires_b,
        .conflicts_with = empty,
    };
    GrammarExtensionMetadata meta_b = {
        .name = "ext-b",
        .version = "1.0",
        .requires = b_requires_c,
        .conflicts_with = empty,
    };
    GrammarExtensionMetadata meta_c = {
        .name = "ext-c",
        .version = "1.0",
        .requires = c_requires_a,
        .conflicts_with = empty,
    };

    extension_registry_register(reg, &meta_a);
    extension_registry_register(reg, &meta_b);
    extension_registry_register(reg, &meta_c);

    char *error = NULL;
    bool ok = extension_registry_check_dependencies(reg, &error);
    ASSERT(!ok, "check should fail for dependency cycle");
    ASSERT(error != NULL, "error message should be set");
    ASSERT(strstr(error, "cycle") != NULL, "error should mention cycle");

    free(error);
    extension_registry_destroy(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: dependency checking - conflict detected                     */
/* ------------------------------------------------------------------ */

static void test_check_dependencies_conflict(void) {
    printf("test_check_dependencies_conflict\n");

    ExtensionRegistry *reg = extension_registry_create();

    const char *empty[] = { NULL };
    const char *conflicts[] = { "ext-b", NULL };

    GrammarExtensionMetadata meta_a = {
        .name = "ext-a",
        .version = "1.0",
        .requires = empty,
        .conflicts_with = conflicts,
    };
    GrammarExtensionMetadata meta_b = {
        .name = "ext-b",
        .version = "1.0",
        .requires = empty,
        .conflicts_with = empty,
    };

    extension_registry_register(reg, &meta_a);
    extension_registry_register(reg, &meta_b);

    char *error = NULL;
    bool ok = extension_registry_check_dependencies(reg, &error);
    ASSERT(!ok, "check should fail for conflict");
    ASSERT(error != NULL, "error message should be set");
    ASSERT(strstr(error, "conflicts") != NULL, "error should mention conflict");

    free(error);
    extension_registry_destroy(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: dependency checking - valid DAG                             */
/* ------------------------------------------------------------------ */

static void test_check_dependencies_valid(void) {
    printf("test_check_dependencies_valid\n");

    ExtensionRegistry *reg = extension_registry_create();

    const char *empty[] = { NULL };
    const char *requires_base[] = { "base", NULL };

    GrammarExtensionMetadata meta_base = {
        .name = "base",
        .version = "1.0",
        .requires = empty,
        .conflicts_with = empty,
    };
    GrammarExtensionMetadata meta_derived = {
        .name = "derived",
        .version = "1.0",
        .requires = requires_base,
        .conflicts_with = empty,
    };

    extension_registry_register(reg, &meta_base);
    extension_registry_register(reg, &meta_derived);

    char *error = NULL;
    bool ok = extension_registry_check_dependencies(reg, &error);
    ASSERT(ok, "check should succeed for valid DAG");
    ASSERT(error == NULL, "error should be NULL on success");

    extension_registry_destroy(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: get_order (topological sort)                                */
/* ------------------------------------------------------------------ */

static void test_get_order(void) {
    printf("test_get_order\n");

    ExtensionRegistry *reg = extension_registry_create();

    const char *empty[] = { NULL };
    const char *requires_a[] = { "ext-a", NULL };
    const char *requires_b[] = { "ext-b", NULL };

    /* ext-a (no deps), ext-b -> ext-a, ext-c -> ext-b */
    GrammarExtensionMetadata meta_a = {
        .name = "ext-a",
        .version = "1.0",
        .requires = empty,
        .conflicts_with = empty,
    };
    GrammarExtensionMetadata meta_b = {
        .name = "ext-b",
        .version = "1.0",
        .requires = requires_a,
        .conflicts_with = empty,
    };
    GrammarExtensionMetadata meta_c = {
        .name = "ext-c",
        .version = "1.0",
        .requires = requires_b,
        .conflicts_with = empty,
    };

    extension_registry_register(reg, &meta_a);
    extension_registry_register(reg, &meta_b);
    extension_registry_register(reg, &meta_c);

    ExtensionOrder order;
    char *error = NULL;
    bool ok = extension_registry_get_order(reg, &order, &error);
    ASSERT(ok, "get_order should succeed");
    ASSERT(order.count == 3, "should have 3 extensions");
    ASSERT(error == NULL, "error should be NULL on success");

    /* ext-a should come before ext-b, ext-b before ext-c */
    int idx_a = -1, idx_b = -1, idx_c = -1;
    for (uint32_t i = 0; i < order.count; i++) {
        if (strcmp(order.names[i], "ext-a") == 0) idx_a = (int)i;
        if (strcmp(order.names[i], "ext-b") == 0) idx_b = (int)i;
        if (strcmp(order.names[i], "ext-c") == 0) idx_c = (int)i;
    }

    ASSERT(idx_a >= 0 && idx_b >= 0 && idx_c >= 0, "all extensions in order");
    ASSERT(idx_a < idx_b, "ext-a should come before ext-b");
    ASSERT(idx_b < idx_c, "ext-b should come before ext-c");

    extension_order_destroy(&order);
    extension_registry_destroy(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: get_order with cycle (should fail)                          */
/* ------------------------------------------------------------------ */

static void test_get_order_cycle(void) {
    printf("test_get_order_cycle\n");

    ExtensionRegistry *reg = extension_registry_create();

    const char *empty[] = { NULL };
    const char *requires_b[] = { "ext-b", NULL };
    const char *requires_a[] = { "ext-a", NULL };

    /* ext-a -> ext-b, ext-b -> ext-a (cycle) */
    GrammarExtensionMetadata meta_a = {
        .name = "ext-a",
        .version = "1.0",
        .requires = requires_b,
        .conflicts_with = empty,
    };
    GrammarExtensionMetadata meta_b = {
        .name = "ext-b",
        .version = "1.0",
        .requires = requires_a,
        .conflicts_with = empty,
    };

    extension_registry_register(reg, &meta_a);
    extension_registry_register(reg, &meta_b);

    ExtensionOrder order;
    char *error = NULL;
    bool ok = extension_registry_get_order(reg, &order, &error);
    ASSERT(!ok, "get_order should fail for cycle");
    ASSERT(error != NULL, "error should be set");

    free(error);
    extension_registry_destroy(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: get_order NULL safety                                       */
/* ------------------------------------------------------------------ */

static void test_get_order_null_safety(void) {
    printf("test_get_order_null_safety\n");

    ExtensionRegistry *reg = extension_registry_create();
    ExtensionOrder order;
    char *error = NULL;

    /* NULL registry */
    bool ok = extension_registry_get_order(NULL, &order, &error);
    ASSERT(!ok, "get_order with NULL registry should fail");

    /* NULL order_out */
    ok = extension_registry_get_order(reg, NULL, &error);
    ASSERT(!ok, "get_order with NULL order_out should fail");

    extension_registry_destroy(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: extension_order_destroy NULL safety                         */
/* ------------------------------------------------------------------ */

static void test_order_destroy_null(void) {
    printf("test_order_destroy_null\n");

    extension_order_destroy(NULL);
    ASSERT(true, "destroy(NULL) should be safe");
}

/* ------------------------------------------------------------------ */
/*  Test: large registry (hash table growth)                          */
/* ------------------------------------------------------------------ */

static void test_large_registry(void) {
    printf("test_large_registry\n");

    ExtensionRegistry *reg = extension_registry_create();

    const char *empty[] = { NULL };

    /* Register 100 extensions to trigger hash table growth */
    for (int i = 0; i < 100; i++) {
        char name[32];
        snprintf(name, sizeof(name), "ext-%d", i);
        GrammarExtensionMetadata meta = {
            .name = name,
            .version = "1.0",
            .requires = empty,
            .conflicts_with = empty,
        };
        bool ok = extension_registry_register(reg, &meta);
        ASSERT(ok, "register should succeed");
    }

    ASSERT(extension_registry_count(reg) == 100, "should have 100 extensions");

    /* Verify all can be found */
    for (int i = 0; i < 100; i++) {
        char name[32];
        snprintf(name, sizeof(name), "ext-%d", i);
        const GrammarExtensionMetadata *found =
            extension_registry_find(reg, name);
        ASSERT(found != NULL, "should find registered extension");
        ASSERT(strcmp(found->name, name) == 0, "name should match");
    }

    extension_registry_destroy(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: unregister with compaction (moves last entry)               */
/* ------------------------------------------------------------------ */

static void test_unregister_compaction(void) {
    printf("test_unregister_compaction\n");

    ExtensionRegistry *reg = extension_registry_create();

    const char *empty[] = { NULL };
    const char *names[] = {"first", "middle", "last"};

    for (int i = 0; i < 3; i++) {
        GrammarExtensionMetadata meta = {
            .name = names[i],
            .version = "1.0",
            .requires = empty,
            .conflicts_with = empty,
        };
        extension_registry_register(reg, &meta);
    }

    /* Unregister middle */
    extension_registry_unregister(reg, "middle");
    ASSERT(extension_registry_count(reg) == 2, "count should be 2");

    /* First and last should still be findable */
    ASSERT(extension_registry_find(reg, "first") != NULL, "first should remain");
    ASSERT(extension_registry_find(reg, "last") != NULL, "last should remain");
    ASSERT(extension_registry_find(reg, "middle") == NULL, "middle should be gone");

    extension_registry_destroy(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: check_dependencies on empty registry                        */
/* ------------------------------------------------------------------ */

static void test_check_dependencies_empty(void) {
    printf("test_check_dependencies_empty\n");

    ExtensionRegistry *reg = extension_registry_create();

    char *error = NULL;
    bool ok = extension_registry_check_dependencies(reg, &error);
    ASSERT(ok, "empty registry should pass check");
    ASSERT(error == NULL, "error should be NULL");

    extension_registry_destroy(reg);
}

/* ------------------------------------------------------------------ */
/*  Test: check_dependencies NULL registry                            */
/* ------------------------------------------------------------------ */

static void test_check_dependencies_null(void) {
    printf("test_check_dependencies_null\n");

    char *error = NULL;
    bool ok = extension_registry_check_dependencies(NULL, &error);
    ASSERT(ok, "NULL registry should return true");
    ASSERT(error == NULL, "error should be NULL");
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== Extension Registry Tests ===\n");

    test_registry_lifecycle();
    test_register_extension();
    test_register_invalid_name();
    test_find_extension();
    test_unregister_extension();
    test_foreach();
    test_check_dependencies_missing();
    test_check_dependencies_cycle();
    test_check_dependencies_conflict();
    test_check_dependencies_valid();
    test_get_order();
    test_get_order_cycle();
    test_get_order_null_safety();
    test_order_destroy_null();
    test_large_registry();
    test_unregister_compaction();
    test_check_dependencies_empty();
    test_check_dependencies_null();

    printf("\n%d tests, %d failures\n", test_count, fail_count);
    return fail_count > 0 ? 1 : 0;
}
