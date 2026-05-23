/*
** Unit tests for the high-level parser operations API.
**
** Tests add/remove/update/reload/query operations, including error
** recovery, rollback, version checking, and concurrent scenarios.
**
** Uses the same mock plugin infrastructure as test_parser_manager.c
** but exercises the parser_operations.h convenience layer.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>

#include "parser_operations.h"
#include "parser.h"
#include "snapshot.h"

/* ================================================================== */
/*  Test framework                                                     */
/* ================================================================== */

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-60s ", name); \
} while(0)

#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)

/* ================================================================== */
/*  Mock plugin infrastructure                                         */
/* ================================================================== */

static bool mock_init_called = false;
static bool mock_destroy_called = false;
static bool mock_init_should_fail = false;
static int mock_snapshot_count = 0;

static const char *mock_get_name(void) { return "mock_parser"; }

static LimePluginVersion mock_get_version(void) {
    return (LimePluginVersion){1, 0, 0};
}

static uint16_t mock_get_abi_major(void) {
    return LIME_PLUGIN_ABI_VERSION_MAJOR;
}

static uint16_t mock_get_abi_minor(void) {
    return LIME_PLUGIN_ABI_VERSION_MINOR;
}

static uint32_t mock_get_capabilities(void) {
    return LIME_CAP_SNAPSHOT;
}

static bool mock_init(void *user_data) {
    mock_init_called = true;
    (void)user_data;
    return !mock_init_should_fail;
}

static void mock_destroy(void) {
    mock_destroy_called = true;
}

static ParserSnapshot *make_mock_snapshot(void) {
    ParserSnapshot *snap = calloc(1, sizeof(ParserSnapshot));
    if (snap == NULL) return NULL;
    snap->version = 42;
    atomic_init(&snap->refcount, 1);
    snap->nsymbol = 2;
    snap->nterminal = 1;
    snap->nrule = 1;
    snap->nstate = 3;
    snap->action_count = 2;
    snap->yy_action = calloc(2, sizeof(uint16_t));
    snap->yy_lookahead = calloc(2, sizeof(uint16_t));
    snap->lookahead_count = 2;
    snap->yy_shift_ofst = calloc(3, sizeof(int32_t));
    snap->yy_reduce_ofst = calloc(3, sizeof(int32_t));
    snap->yy_default = calloc(3, sizeof(uint16_t));
    return snap;
}

static ParserSnapshot *mock_create_snapshot(const char *grammar_file,
                                            char **error) {
    (void)grammar_file;
    if (error) *error = NULL;
    mock_snapshot_count++;
    return make_mock_snapshot();
}

static bool mock_validate_snapshot(const ParserSnapshot *snap, char **error) {
    if (error) *error = NULL;
    return snap != NULL;
}

static const LimeParserPlugin mock_plugin = {
    .get_name         = mock_get_name,
    .get_version      = mock_get_version,
    .get_abi_major    = mock_get_abi_major,
    .get_abi_minor    = mock_get_abi_minor,
    .get_capabilities = mock_get_capabilities,
    .init             = mock_init,
    .destroy          = mock_destroy,
    .create_snapshot  = mock_create_snapshot,
    .validate_snapshot = mock_validate_snapshot,
    .serialize_snapshot = NULL,
    .deserialize_snapshot = NULL,
    ._reserved = {0},
};

/* Second mock plugin (v2) */
static const char *mock2_get_name(void) { return "mock_parser_v2"; }
static LimePluginVersion mock2_get_version(void) {
    return (LimePluginVersion){2, 0, 0};
}
static void mock2_destroy(void) {}

static const LimeParserPlugin mock_plugin_v2 = {
    .get_name         = mock2_get_name,
    .get_version      = mock2_get_version,
    .get_abi_major    = mock_get_abi_major,
    .get_abi_minor    = mock_get_abi_minor,
    .get_capabilities = mock_get_capabilities,
    .init             = NULL,
    .destroy          = mock2_destroy,
    .create_snapshot  = mock_create_snapshot,
    .validate_snapshot = NULL,
    .serialize_snapshot = NULL,
    .deserialize_snapshot = NULL,
    ._reserved = {0},
};

/* Failing mock plugin */
static const char *mock_fail_get_name(void) { return "failing_parser"; }

static ParserSnapshot *mock_fail_create_snapshot(const char *grammar_file,
                                                 char **error) {
    (void)grammar_file;
    if (error) {
        const char msg[] = "mock snapshot creation failure";
        *error = malloc(sizeof(msg));
        if (*error) memcpy(*error, msg, sizeof(msg));
    }
    return NULL;
}

static void mock_fail_destroy(void) {}

static const LimeParserPlugin mock_plugin_fail = {
    .get_name         = mock_fail_get_name,
    .get_version      = mock_get_version,
    .get_abi_major    = mock_get_abi_major,
    .get_abi_minor    = mock_get_abi_minor,
    .get_capabilities = mock_get_capabilities,
    .init             = NULL,
    .destroy          = mock_fail_destroy,
    .create_snapshot  = mock_fail_create_snapshot,
    .validate_snapshot = NULL,
    .serialize_snapshot = NULL,
    .deserialize_snapshot = NULL,
    ._reserved = {0},
};

static void reset_mock_state(void) {
    mock_init_called = false;
    mock_destroy_called = false;
    mock_init_should_fail = false;
    mock_snapshot_count = 0;
}

/* ================================================================== */
/*  Add operation tests                                                */
/* ================================================================== */

static void test_add_static_and_activate(void) {
    TEST("add_static: register and activate");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    ParserOpResult r = parser_op_add_static(mgr, &mock_plugin,
                                            "grammar.y", NULL);
    if (r.status != PM_OK) {
        char buf[256];
        snprintf(buf, sizeof(buf), "failed: %s (%s)",
                 parser_manager_status_string(r.status),
                 r.message ? r.message : "");
        FAIL(buf);
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    if (r.handle == LIME_PLUGIN_HANDLE_INVALID) {
        FAIL("got invalid handle");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    /* Should be active */
    if (parser_manager_get_active(mgr) != r.handle) {
        FAIL("not active");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    /* Snapshot should be available */
    ParserSnapshot *snap = parser_manager_get_snapshot(mgr);
    if (snap == NULL) {
        FAIL("no snapshot");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    lime_snapshot_release(snap);
    parser_op_result_cleanup(&r);
    parser_manager_destroy(mgr);
    PASS();
}

static void test_add_static_null_grammar(void) {
    TEST("add_static: register without grammar");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    ParserOpResult r = parser_op_add_static(mgr, &mock_plugin, NULL, NULL);
    if (r.status != PM_OK) {
        FAIL("should succeed");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    /* Plugin registered but no snapshot */
    if (parser_manager_plugin_count(mgr) != 1) {
        FAIL("expected 1 plugin");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    parser_op_result_cleanup(&r);
    parser_manager_destroy(mgr);
    PASS();
}

static void test_add_static_activation_failure_rollback(void) {
    TEST("add_static: rollback on activation failure");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    ParserOpResult r = parser_op_add_static(mgr, &mock_plugin_fail,
                                            "grammar.y", NULL);
    if (r.status == PM_OK) {
        FAIL("should have failed");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    /* Plugin should have been rolled back (unloaded) */
    if (parser_manager_plugin_count(mgr) != 0) {
        FAIL("plugin should have been unloaded on failure");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    /* Should have an error message */
    if (r.message == NULL) {
        FAIL("expected error message");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    parser_op_result_cleanup(&r);
    parser_manager_destroy(mgr);
    PASS();
}

static void test_add_static_null_args(void) {
    TEST("add_static: NULL arguments");
    ParserOpResult r = parser_op_add_static(NULL, &mock_plugin, NULL, NULL);
    if (r.status != PM_ERR_INVALID_ARG) {
        FAIL("expected INVALID_ARG");
        parser_op_result_cleanup(&r);
        return;
    }
    parser_op_result_cleanup(&r);

    ParserManager *mgr = parser_manager_create(NULL);
    r = parser_op_add_static(mgr, NULL, NULL, NULL);
    if (r.status != PM_ERR_INVALID_ARG) {
        FAIL("expected INVALID_ARG for NULL plugin");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    parser_op_result_cleanup(&r);
    parser_manager_destroy(mgr);
    PASS();
}

static void test_add_dynamic_null_args(void) {
    TEST("add_dynamic: NULL arguments");
    ParserOpResult r = parser_op_add_dynamic(NULL, "/foo.so", NULL, NULL);
    if (r.status != PM_ERR_INVALID_ARG) {
        FAIL("expected INVALID_ARG");
        parser_op_result_cleanup(&r);
        return;
    }
    parser_op_result_cleanup(&r);

    ParserManager *mgr = parser_manager_create(NULL);
    r = parser_op_add_dynamic(mgr, NULL, NULL, NULL);
    if (r.status != PM_ERR_INVALID_ARG) {
        FAIL("expected INVALID_ARG for NULL path");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    parser_op_result_cleanup(&r);
    parser_manager_destroy(mgr);
    PASS();
}

static void test_add_dynamic_nonexistent(void) {
    TEST("add_dynamic: nonexistent library");
    ParserManager *mgr = parser_manager_create(NULL);

    ParserOpResult r = parser_op_add_dynamic(mgr, "/nonexistent/plugin.so",
                                             NULL, NULL);
    if (r.status == PM_OK) {
        FAIL("should have failed");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    if (r.message == NULL) {
        FAIL("expected error message");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    parser_op_result_cleanup(&r);
    parser_manager_destroy(mgr);
    PASS();
}

/* ================================================================== */
/*  Remove operation tests                                             */
/* ================================================================== */

static void test_remove_inactive_plugin(void) {
    TEST("remove: unload inactive plugin");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h;
    parser_manager_register(mgr, &mock_plugin, NULL, &h);

    ParserOpResult r = parser_op_remove(mgr, h,
                                        LIME_PLUGIN_HANDLE_INVALID, NULL);
    if (r.status != PM_OK) {
        FAIL("remove failed");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    if (parser_manager_plugin_count(mgr) != 0) {
        FAIL("expected 0 plugins");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    parser_op_result_cleanup(&r);
    parser_manager_destroy(mgr);
    PASS();
}

static void test_remove_active_with_fallback(void) {
    TEST("remove: active plugin with fallback");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h1, h2;
    parser_manager_register(mgr, &mock_plugin, NULL, &h1);
    parser_manager_register(mgr, &mock_plugin_v2, NULL, &h2);
    parser_manager_set_active(mgr, h1, "grammar.y");

    /* Remove h1, falling back to h2 */
    ParserOpResult r = parser_op_remove(mgr, h1, h2, "fallback.y");
    if (r.status != PM_OK) {
        char buf[256];
        snprintf(buf, sizeof(buf), "remove failed: %s",
                 r.message ? r.message : "");
        FAIL(buf);
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    /* h2 should now be active */
    if (parser_manager_get_active(mgr) != h2) {
        FAIL("fallback should be active");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    /* Only 1 plugin should remain */
    if (parser_manager_plugin_count(mgr) != 1) {
        FAIL("expected 1 plugin");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    parser_op_result_cleanup(&r);
    parser_manager_destroy(mgr);
    PASS();
}

static void test_remove_active_no_fallback(void) {
    TEST("remove: active plugin without fallback");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h;
    parser_manager_register(mgr, &mock_plugin, NULL, &h);
    parser_manager_set_active(mgr, h, "grammar.y");

    ParserOpResult r = parser_op_remove(mgr, h,
                                        LIME_PLUGIN_HANDLE_INVALID, NULL);
    if (r.status != PM_OK) {
        FAIL("remove should succeed");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    /* No active plugin */
    if (parser_manager_get_active(mgr) != LIME_PLUGIN_HANDLE_INVALID) {
        FAIL("should have no active plugin");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    parser_op_result_cleanup(&r);
    parser_manager_destroy(mgr);
    PASS();
}

static void test_remove_by_name(void) {
    TEST("remove_by_name");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h;
    parser_manager_register(mgr, &mock_plugin, NULL, &h);

    ParserOpResult r = parser_op_remove_by_name(mgr, "mock_parser",
                                                LIME_PLUGIN_HANDLE_INVALID,
                                                NULL);
    if (r.status != PM_OK) {
        FAIL("remove_by_name failed");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    if (parser_manager_plugin_count(mgr) != 0) {
        FAIL("expected 0 plugins");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    parser_op_result_cleanup(&r);
    parser_manager_destroy(mgr);
    PASS();
}

static void test_remove_by_name_not_found(void) {
    TEST("remove_by_name: nonexistent name");
    ParserManager *mgr = parser_manager_create(NULL);

    ParserOpResult r = parser_op_remove_by_name(mgr, "nonexistent",
                                                LIME_PLUGIN_HANDLE_INVALID,
                                                NULL);
    if (r.status != PM_ERR_PLUGIN_NOT_FOUND) {
        FAIL("expected PLUGIN_NOT_FOUND");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    if (r.message == NULL || strstr(r.message, "nonexistent") == NULL) {
        FAIL("error message should mention the name");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    parser_op_result_cleanup(&r);
    parser_manager_destroy(mgr);
    PASS();
}

static void test_remove_invalid_args(void) {
    TEST("remove: invalid arguments");
    ParserOpResult r = parser_op_remove(NULL, 1,
                                        LIME_PLUGIN_HANDLE_INVALID, NULL);
    if (r.status != PM_ERR_INVALID_ARG) {
        FAIL("expected INVALID_ARG");
        parser_op_result_cleanup(&r);
        return;
    }
    parser_op_result_cleanup(&r);

    ParserManager *mgr = parser_manager_create(NULL);
    r = parser_op_remove(mgr, LIME_PLUGIN_HANDLE_INVALID,
                         LIME_PLUGIN_HANDLE_INVALID, NULL);
    if (r.status != PM_ERR_INVALID_ARG) {
        FAIL("expected INVALID_ARG for invalid handle");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    parser_op_result_cleanup(&r);
    parser_manager_destroy(mgr);
    PASS();
}

/* ================================================================== */
/*  Reload grammar tests                                               */
/* ================================================================== */

static void test_reload_grammar(void) {
    TEST("reload_grammar: replaces snapshot");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h;
    parser_manager_register(mgr, &mock_plugin, NULL, &h);
    parser_manager_set_active(mgr, h, "old.y");

    ParserSnapshot *old_snap = parser_manager_get_snapshot(mgr);

    ParserOpResult r = parser_op_reload_grammar(mgr, "new.y");
    if (r.status != PM_OK) {
        FAIL("reload failed");
        if (old_snap) lime_snapshot_release(old_snap);
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    ParserSnapshot *new_snap = parser_manager_get_snapshot(mgr);
    if (new_snap == old_snap) {
        FAIL("snapshot should have changed");
        if (old_snap) lime_snapshot_release(old_snap);
        if (new_snap) lime_snapshot_release(new_snap);
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    /* Same plugin should still be active */
    if (parser_manager_get_active(mgr) != h) {
        FAIL("active plugin should not change");
        if (old_snap) lime_snapshot_release(old_snap);
        if (new_snap) lime_snapshot_release(new_snap);
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    if (old_snap) lime_snapshot_release(old_snap);
    if (new_snap) lime_snapshot_release(new_snap);
    parser_op_result_cleanup(&r);
    parser_manager_destroy(mgr);
    PASS();
}

static void test_reload_grammar_no_active(void) {
    TEST("reload_grammar: no active plugin");
    ParserManager *mgr = parser_manager_create(NULL);

    ParserOpResult r = parser_op_reload_grammar(mgr, "grammar.y");
    if (r.status != PM_ERR_NO_ACTIVE_PLUGIN) {
        FAIL("expected NO_ACTIVE_PLUGIN");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    parser_op_result_cleanup(&r);
    parser_manager_destroy(mgr);
    PASS();
}

static void test_reload_grammar_null_args(void) {
    TEST("reload_grammar: NULL arguments");
    ParserOpResult r = parser_op_reload_grammar(NULL, "grammar.y");
    if (r.status != PM_ERR_INVALID_ARG) {
        FAIL("expected INVALID_ARG");
        parser_op_result_cleanup(&r);
        return;
    }
    /* Cleanup the first result before reassigning -- otherwise the
    ** malloc'd error message from the NULL-mgr call is leaked. */
    parser_op_result_cleanup(&r);

    ParserManager *mgr = parser_manager_create(NULL);
    r = parser_op_reload_grammar(mgr, NULL);
    if (r.status != PM_ERR_INVALID_ARG) {
        FAIL("expected INVALID_ARG for NULL grammar");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    parser_op_result_cleanup(&r);
    parser_manager_destroy(mgr);
    PASS();
}

/* ================================================================== */
/*  Update operation tests                                             */
/* ================================================================== */

static void test_update_reload_same_plugin(void) {
    TEST("update: NULL path reloads grammar via current plugin");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h;
    parser_manager_register(mgr, &mock_plugin, NULL, &h);
    parser_manager_set_active(mgr, h, "old.y");

    int count_before = mock_snapshot_count;
    ParserOpResult r = parser_op_update(mgr, NULL, "new.y", false, NULL);
    if (r.status != PM_OK) {
        FAIL("update failed");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    /* Should have created one more snapshot */
    if (mock_snapshot_count <= count_before) {
        FAIL("expected new snapshot");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    parser_op_result_cleanup(&r);
    parser_manager_destroy(mgr);
    PASS();
}

static void test_update_no_active_no_path(void) {
    TEST("update: no active plugin and NULL path");
    ParserManager *mgr = parser_manager_create(NULL);

    ParserOpResult r = parser_op_update(mgr, NULL, "grammar.y", false, NULL);
    if (r.status != PM_ERR_NO_ACTIVE_PLUGIN) {
        FAIL("expected NO_ACTIVE_PLUGIN");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    parser_op_result_cleanup(&r);
    parser_manager_destroy(mgr);
    PASS();
}

static void test_update_null_grammar(void) {
    TEST("update: NULL grammar_file");
    ParserManager *mgr = parser_manager_create(NULL);

    ParserOpResult r = parser_op_update(mgr, "/foo.so", NULL, false, NULL);
    if (r.status != PM_ERR_INVALID_ARG) {
        FAIL("expected INVALID_ARG");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    parser_op_result_cleanup(&r);
    parser_manager_destroy(mgr);
    PASS();
}

/* ================================================================== */
/*  Query operation tests                                              */
/* ================================================================== */

static void test_get_stats_empty(void) {
    TEST("get_stats: empty manager");
    ParserManager *mgr = parser_manager_create(NULL);
    ParserManagerStats stats;

    ParserOpResult r = parser_op_get_stats(mgr, &stats);
    if (r.status != PM_OK) {
        FAIL("get_stats failed");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    if (stats.total_plugins != 0) {
        FAIL("expected 0 plugins");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    if (stats.has_active) {
        FAIL("should not have active plugin");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    if (stats.snapshot_available) {
        FAIL("should not have snapshot");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    parser_op_result_cleanup(&r);
    parser_manager_destroy(mgr);
    PASS();
}

static void test_get_stats_with_plugins(void) {
    TEST("get_stats: with active plugin");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h1, h2;
    parser_manager_register(mgr, &mock_plugin, NULL, &h1);
    parser_manager_register(mgr, &mock_plugin_v2, NULL, &h2);
    parser_manager_set_active(mgr, h1, "grammar.y");

    ParserManagerStats stats;
    ParserOpResult r = parser_op_get_stats(mgr, &stats);
    if (r.status != PM_OK) {
        FAIL("get_stats failed");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    int ok = 1;
    if (stats.total_plugins != 2) {
        ok = 0; FAIL("expected 2 plugins");
    } else if (stats.static_plugins != 2) {
        ok = 0; FAIL("expected 2 static plugins");
    } else if (stats.dynamic_plugins != 0) {
        ok = 0; FAIL("expected 0 dynamic plugins");
    } else if (!stats.has_active) {
        ok = 0; FAIL("should have active");
    } else if (stats.active_handle != h1) {
        ok = 0; FAIL("wrong active handle");
    } else if (stats.active_name == NULL ||
               strcmp(stats.active_name, "mock_parser") != 0) {
        ok = 0; FAIL("wrong active name");
    } else if (stats.active_version.major != 1) {
        ok = 0; FAIL("wrong active version");
    } else if (!stats.snapshot_available) {
        ok = 0; FAIL("snapshot should be available");
    }

    parser_op_result_cleanup(&r);
    parser_manager_destroy(mgr);
    if (ok) PASS();
}

static void test_get_stats_null_args(void) {
    TEST("get_stats: NULL arguments");
    ParserManagerStats stats;
    ParserOpResult r = parser_op_get_stats(NULL, &stats);
    if (r.status != PM_ERR_INVALID_ARG) {
        FAIL("expected INVALID_ARG");
        parser_op_result_cleanup(&r);
        return;
    }
    parser_op_result_cleanup(&r);

    ParserManager *mgr = parser_manager_create(NULL);
    r = parser_op_get_stats(mgr, NULL);
    if (r.status != PM_ERR_INVALID_ARG) {
        FAIL("expected INVALID_ARG for NULL stats");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    parser_op_result_cleanup(&r);
    parser_manager_destroy(mgr);
    PASS();
}

static void test_list_formatted_empty(void) {
    TEST("list_formatted: no plugins");
    ParserManager *mgr = parser_manager_create(NULL);

    /* Write to /dev/null to avoid cluttering test output */
    FILE *f = fopen("/dev/null", "w");
    if (f == NULL) {
        FAIL("cannot open /dev/null");
        parser_manager_destroy(mgr);
        return;
    }

    ParserOpResult r = parser_op_list_formatted(mgr, f);
    fclose(f);

    if (r.status != PM_OK) {
        FAIL("list_formatted failed");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    parser_op_result_cleanup(&r);
    parser_manager_destroy(mgr);
    PASS();
}

static void test_list_formatted_with_plugins(void) {
    TEST("list_formatted: with plugins");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h1, h2;
    parser_manager_register(mgr, &mock_plugin, NULL, &h1);
    parser_manager_register(mgr, &mock_plugin_v2, NULL, &h2);
    parser_manager_set_active(mgr, h1, "grammar.y");

    FILE *f = fopen("/dev/null", "w");
    if (f == NULL) {
        FAIL("cannot open /dev/null");
        parser_manager_destroy(mgr);
        return;
    }

    ParserOpResult r = parser_op_list_formatted(mgr, f);
    fclose(f);

    if (r.status != PM_OK) {
        FAIL("list_formatted failed");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    parser_op_result_cleanup(&r);
    parser_manager_destroy(mgr);
    PASS();
}

static void test_list_formatted_null_args(void) {
    TEST("list_formatted: NULL arguments");
    ParserOpResult r = parser_op_list_formatted(NULL, stdout);
    if (r.status != PM_ERR_INVALID_ARG) {
        FAIL("expected INVALID_ARG");
        parser_op_result_cleanup(&r);
        return;
    }
    parser_op_result_cleanup(&r);

    ParserManager *mgr = parser_manager_create(NULL);
    r = parser_op_list_formatted(mgr, NULL);
    if (r.status != PM_ERR_INVALID_ARG) {
        FAIL("expected INVALID_ARG for NULL out");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }

    parser_op_result_cleanup(&r);
    parser_manager_destroy(mgr);
    PASS();
}

/* ================================================================== */
/*  is_loaded tests                                                    */
/* ================================================================== */

static void test_is_loaded(void) {
    TEST("is_loaded: basic checks");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    if (parser_op_is_loaded(mgr, "mock_parser", NULL)) {
        FAIL("should not be loaded");
        parser_manager_destroy(mgr);
        return;
    }

    LimePluginHandle h;
    parser_manager_register(mgr, &mock_plugin, NULL, &h);

    if (!parser_op_is_loaded(mgr, "mock_parser", NULL)) {
        FAIL("should be loaded");
        parser_manager_destroy(mgr);
        return;
    }

    if (parser_op_is_loaded(mgr, "nonexistent", NULL)) {
        FAIL("nonexistent should not be loaded");
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_destroy(mgr);
    PASS();
}

static void test_is_loaded_with_version(void) {
    TEST("is_loaded: with version requirement");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h;
    parser_manager_register(mgr, &mock_plugin, NULL, &h);

    /* mock_plugin is v1.0.0 */
    LimePluginVersion v1 = {1, 0, 0};
    if (!parser_op_is_loaded(mgr, "mock_parser", &v1)) {
        FAIL("1.0.0 should satisfy 1.0.0");
        parser_manager_destroy(mgr);
        return;
    }

    LimePluginVersion v09 = {0, 9, 0};
    if (!parser_op_is_loaded(mgr, "mock_parser", &v09)) {
        FAIL("1.0.0 should satisfy 0.9.0");
        parser_manager_destroy(mgr);
        return;
    }

    LimePluginVersion v2 = {2, 0, 0};
    if (parser_op_is_loaded(mgr, "mock_parser", &v2)) {
        FAIL("1.0.0 should NOT satisfy 2.0.0");
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_destroy(mgr);
    PASS();
}

static void test_is_loaded_null_args(void) {
    TEST("is_loaded: NULL arguments");
    if (parser_op_is_loaded(NULL, "foo", NULL)) {
        FAIL("NULL mgr should return false");
        return;
    }
    ParserManager *mgr = parser_manager_create(NULL);
    if (parser_op_is_loaded(mgr, NULL, NULL)) {
        FAIL("NULL name should return false");
        parser_manager_destroy(mgr);
        return;
    }
    parser_manager_destroy(mgr);
    PASS();
}

/* ================================================================== */
/*  Result cleanup tests                                               */
/* ================================================================== */

static void test_result_cleanup(void) {
    TEST("result_cleanup: frees message");
    ParserOpResult r;
    r.status = PM_OK;
    r.handle = LIME_PLUGIN_HANDLE_INVALID;
    r.message = malloc(32);
    if (r.message == NULL) {
        FAIL("malloc failed");
        return;
    }
    strcpy(r.message, "test message");

    parser_op_result_cleanup(&r);

    if (r.message != NULL) {
        FAIL("message should be NULL after cleanup");
        return;
    }

    /* Cleanup of NULL result is safe */
    parser_op_result_cleanup(NULL);

    PASS();
}

static void test_result_cleanup_null_message(void) {
    TEST("result_cleanup: NULL message is safe");
    ParserOpResult r;
    r.status = PM_OK;
    r.handle = LIME_PLUGIN_HANDLE_INVALID;
    r.message = NULL;

    parser_op_result_cleanup(&r);
    PASS();
}

/* ================================================================== */
/*  Full workflow test                                                  */
/* ================================================================== */

static void test_full_lifecycle(void) {
    TEST("full lifecycle: add -> use -> reload -> replace -> remove");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    /* 1. Add and activate first plugin */
    ParserOpResult r = parser_op_add_static(mgr, &mock_plugin,
                                            "grammar_v1.y", NULL);
    if (r.status != PM_OK) {
        FAIL("add v1 failed");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }
    LimePluginHandle h1 = r.handle;
    parser_op_result_cleanup(&r);

    /* 2. Verify snapshot */
    ParserSnapshot *snap = parser_manager_get_snapshot(mgr);
    if (snap == NULL) {
        FAIL("no snapshot after add");
        parser_manager_destroy(mgr);
        return;
    }
    lime_snapshot_release(snap);

    /* 3. Reload grammar */
    r = parser_op_reload_grammar(mgr, "grammar_v1_updated.y");
    if (r.status != PM_OK) {
        FAIL("reload failed");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }
    parser_op_result_cleanup(&r);

    /* 4. Add second plugin alongside */
    LimePluginHandle h2;
    parser_manager_register(mgr, &mock_plugin_v2, NULL, &h2);

    /* 5. Hot-swap to v2 */
    r.status = parser_manager_hot_swap(mgr, h2, "grammar_v2.y");
    if (r.status != PM_OK) {
        FAIL("hot-swap failed");
        parser_manager_destroy(mgr);
        return;
    }

    if (parser_manager_get_active(mgr) != h2) {
        FAIL("v2 should be active");
        parser_manager_destroy(mgr);
        return;
    }

    /* 6. Remove old plugin */
    r = parser_op_remove(mgr, h1, LIME_PLUGIN_HANDLE_INVALID, NULL);
    if (r.status != PM_OK) {
        FAIL("remove v1 failed");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }
    parser_op_result_cleanup(&r);

    /* 7. Check stats */
    ParserManagerStats stats;
    r = parser_op_get_stats(mgr, &stats);
    if (r.status != PM_OK || stats.total_plugins != 1 || !stats.has_active) {
        FAIL("stats incorrect");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }
    parser_op_result_cleanup(&r);

    /* 8. Remove final plugin */
    r = parser_op_remove(mgr, h2, LIME_PLUGIN_HANDLE_INVALID, NULL);
    if (r.status != PM_OK) {
        FAIL("remove v2 failed");
        parser_op_result_cleanup(&r);
        parser_manager_destroy(mgr);
        return;
    }
    parser_op_result_cleanup(&r);

    if (parser_manager_plugin_count(mgr) != 0) {
        FAIL("expected 0 plugins");
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_destroy(mgr);
    PASS();
}

/* ================================================================== */
/*  Concurrent operations test                                         */
/* ================================================================== */

#define NUM_WORKER_THREADS 4
#define OPS_PER_THREAD 100

typedef struct WorkerArg {
    ParserManager *mgr;
    int errors;
} WorkerArg;

static void *worker_thread_fn(void *arg) {
    WorkerArg *wa = (WorkerArg *)arg;
    wa->errors = 0;

    for (int i = 0; i < OPS_PER_THREAD; i++) {
        /* Alternate between querying stats and getting snapshot */
        if (i % 2 == 0) {
            ParserManagerStats stats;
            ParserOpResult r = parser_op_get_stats(wa->mgr, &stats);
            if (r.status != PM_OK) wa->errors++;
            parser_op_result_cleanup(&r);
        } else {
            ParserSnapshot *snap = parser_manager_get_snapshot(wa->mgr);
            if (snap != NULL) {
                lime_snapshot_release(snap);
            }
        }

        /* Check is_loaded */
        parser_op_is_loaded(wa->mgr, "mock_parser", NULL);
    }

    return NULL;
}

static void test_concurrent_operations(void) {
    TEST("concurrent: stats + snapshot + is_loaded");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h;
    parser_manager_register(mgr, &mock_plugin, NULL, &h);
    parser_manager_set_active(mgr, h, "grammar.y");

    pthread_t threads[NUM_WORKER_THREADS];
    WorkerArg args[NUM_WORKER_THREADS];

    for (int i = 0; i < NUM_WORKER_THREADS; i++) {
        args[i].mgr = mgr;
        args[i].errors = 0;
        pthread_create(&threads[i], NULL, worker_thread_fn, &args[i]);
    }

    for (int i = 0; i < NUM_WORKER_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    int total_errors = 0;
    for (int i = 0; i < NUM_WORKER_THREADS; i++) {
        total_errors += args[i].errors;
    }

    if (total_errors > 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%d errors", total_errors);
        FAIL(buf);
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_destroy(mgr);
    PASS();
}

/* ================================================================== */
/*  Main                                                               */
/* ================================================================== */

int main(void) {
    printf("Runtime Management unit tests\n");
    printf("=============================\n\n");

    printf("[Add Operations]\n");
    test_add_static_and_activate();
    test_add_static_null_grammar();
    test_add_static_activation_failure_rollback();
    test_add_static_null_args();
    test_add_dynamic_null_args();
    test_add_dynamic_nonexistent();

    printf("\n[Remove Operations]\n");
    test_remove_inactive_plugin();
    test_remove_active_with_fallback();
    test_remove_active_no_fallback();
    test_remove_by_name();
    test_remove_by_name_not_found();
    test_remove_invalid_args();

    printf("\n[Reload Grammar]\n");
    test_reload_grammar();
    test_reload_grammar_no_active();
    test_reload_grammar_null_args();

    printf("\n[Update Operations]\n");
    test_update_reload_same_plugin();
    test_update_no_active_no_path();
    test_update_null_grammar();

    printf("\n[Query Operations]\n");
    test_get_stats_empty();
    test_get_stats_with_plugins();
    test_get_stats_null_args();
    test_list_formatted_empty();
    test_list_formatted_with_plugins();
    test_list_formatted_null_args();

    printf("\n[is_loaded]\n");
    test_is_loaded();
    test_is_loaded_with_version();
    test_is_loaded_null_args();

    printf("\n[Result Cleanup]\n");
    test_result_cleanup();
    test_result_cleanup_null_message();

    printf("\n[Full Lifecycle]\n");
    test_full_lifecycle();

    printf("\n[Concurrency]\n");
    test_concurrent_operations();

    printf("\n=============================\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);
    printf("=============================\n");

    return (tests_passed == tests_run) ? 0 : 1;
}
