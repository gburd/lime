/*
** Unit tests for the parser manager system.
**
** Tests plugin registration (static), registry operations, active parser
** management, enumeration, hot-swap, version utilities, and concurrent
** access patterns.
**
** Dynamic loading tests (dlopen) are skipped when no test plugin .so is
** available; the core registry logic is exercised via static plugins.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>

#include "parser_manager.h"
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

/*
** A minimal mock plugin used throughout the tests.
*/

static bool mock_init_called = false;
static bool mock_destroy_called = false;
static bool mock_init_should_fail = false;

static const char *mock_get_name(void) { return "mock_parser"; }

static LimePluginVersion mock_get_version(void) {
    return (LimePluginVersion){1, 2, 3};
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

/*
** Create a minimal snapshot for testing.
*/
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
    snap->yy_shift_ofst = calloc(3, sizeof(int16_t));
    snap->yy_reduce_ofst = calloc(3, sizeof(int16_t));
    snap->yy_default = calloc(3, sizeof(uint16_t));
    return snap;
}

static ParserSnapshot *mock_create_snapshot(const char *grammar_file,
                                            char **error) {
    (void)grammar_file;
    if (error) *error = NULL;
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

/* A second mock plugin with a different name */
static const char *mock2_get_name(void) { return "mock_parser_v2"; }

static LimePluginVersion mock2_get_version(void) {
    return (LimePluginVersion){2, 0, 0};
}

static void mock2_destroy(void) { /* no-op */ }

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

/* Mock plugin with wrong ABI */
static uint16_t bad_abi_get_major(void) { return 99; }

static const LimeParserPlugin mock_plugin_bad_abi = {
    .get_name         = mock_get_name,
    .get_version      = mock_get_version,
    .get_abi_major    = bad_abi_get_major,
    .get_abi_minor    = mock_get_abi_minor,
    .get_capabilities = mock_get_capabilities,
    .init             = NULL,
    .destroy          = mock_destroy,
    .create_snapshot  = mock_create_snapshot,
    .validate_snapshot = NULL,
    .serialize_snapshot = NULL,
    .deserialize_snapshot = NULL,
    ._reserved = {0},
};

/* Mock plugin whose create_snapshot always fails */
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

static void mock_fail_destroy(void) { /* no-op */ }

static const LimeParserPlugin mock_plugin_fail_snap = {
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

/* Reset mock state between tests */
static void reset_mock_state(void) {
    mock_init_called = false;
    mock_destroy_called = false;
    mock_init_should_fail = false;
}

/* ================================================================== */
/*  Manager lifecycle tests                                            */
/* ================================================================== */

static void test_create_destroy_null_config(void) {
    TEST("parser_manager_create(NULL) + destroy");
    ParserManager *mgr = parser_manager_create(NULL);
    if (mgr == NULL) {
        FAIL("create returned NULL");
        return;
    }
    if (parser_manager_plugin_count(mgr) != 0) {
        FAIL("expected 0 plugins");
        parser_manager_destroy(mgr);
        return;
    }
    parser_manager_destroy(mgr);
    PASS();
}

static void test_create_with_config(void) {
    TEST("parser_manager_create with config");
    ParserManagerConfig config = {
        .max_plugins = 4,
        .validate_on_load = true,
        .auto_jit = false,
        .plugin_search_paths = NULL,
    };
    ParserManager *mgr = parser_manager_create(&config);
    if (mgr == NULL) {
        FAIL("create returned NULL");
        return;
    }
    parser_manager_destroy(mgr);
    PASS();
}

static void test_destroy_null(void) {
    TEST("parser_manager_destroy(NULL) is safe");
    parser_manager_destroy(NULL);
    PASS();
}

/* ================================================================== */
/*  Plugin registration tests                                          */
/* ================================================================== */

static void test_register_static_plugin(void) {
    TEST("register static plugin");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h;
    ParserManagerStatus st = parser_manager_register(mgr, &mock_plugin, NULL, &h);
    if (st != PM_OK) {
        char buf[128];
        snprintf(buf, sizeof(buf), "register failed: %s",
                 parser_manager_status_string(st));
        FAIL(buf);
        parser_manager_destroy(mgr);
        return;
    }

    if (h == LIME_PLUGIN_HANDLE_INVALID) {
        FAIL("got invalid handle");
        parser_manager_destroy(mgr);
        return;
    }

    if (!mock_init_called) {
        FAIL("init not called");
        parser_manager_destroy(mgr);
        return;
    }

    if (parser_manager_plugin_count(mgr) != 1) {
        FAIL("expected 1 plugin");
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_destroy(mgr);
    PASS();
}

static void test_register_null_args(void) {
    TEST("register with NULL args returns error");
    ParserManager *mgr = parser_manager_create(NULL);
    LimePluginHandle h;

    if (parser_manager_register(NULL, &mock_plugin, NULL, &h) != PM_ERR_INVALID_ARG) {
        FAIL("expected INVALID_ARG for NULL mgr");
        parser_manager_destroy(mgr);
        return;
    }

    if (parser_manager_register(mgr, NULL, NULL, &h) != PM_ERR_INVALID_ARG) {
        FAIL("expected INVALID_ARG for NULL plugin");
        parser_manager_destroy(mgr);
        return;
    }

    if (parser_manager_register(mgr, &mock_plugin, NULL, NULL) != PM_ERR_INVALID_ARG) {
        FAIL("expected INVALID_ARG for NULL handle_out");
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_destroy(mgr);
    PASS();
}

static void test_register_abi_mismatch(void) {
    TEST("register plugin with wrong ABI version");
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h;
    ParserManagerStatus st = parser_manager_register(
        mgr, &mock_plugin_bad_abi, NULL, &h);

    if (st != PM_ERR_ABI_MISMATCH) {
        char buf[128];
        snprintf(buf, sizeof(buf), "expected ABI_MISMATCH, got %s",
                 parser_manager_status_string(st));
        FAIL(buf);
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_destroy(mgr);
    PASS();
}

static void test_register_duplicate_name(void) {
    TEST("register duplicate plugin name");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h1;
    parser_manager_register(mgr, &mock_plugin, NULL, &h1);

    LimePluginHandle h2;
    ParserManagerStatus st = parser_manager_register(
        mgr, &mock_plugin, NULL, &h2);

    if (st != PM_ERR_DUPLICATE_NAME) {
        char buf[128];
        snprintf(buf, sizeof(buf), "expected DUPLICATE_NAME, got %s",
                 parser_manager_status_string(st));
        FAIL(buf);
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_destroy(mgr);
    PASS();
}

static void test_register_init_failure(void) {
    TEST("register plugin whose init() fails");
    reset_mock_state();
    mock_init_should_fail = true;

    ParserManager *mgr = parser_manager_create(NULL);
    LimePluginHandle h;
    ParserManagerStatus st = parser_manager_register(mgr, &mock_plugin, NULL, &h);

    if (st != PM_ERR_INIT_FAILED) {
        char buf[128];
        snprintf(buf, sizeof(buf), "expected INIT_FAILED, got %s",
                 parser_manager_status_string(st));
        FAIL(buf);
        parser_manager_destroy(mgr);
        return;
    }

    if (parser_manager_plugin_count(mgr) != 0) {
        FAIL("plugin should not be registered on init failure");
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_destroy(mgr);
    PASS();
}

static void test_register_multiple_plugins(void) {
    TEST("register multiple distinct plugins");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h1, h2;
    parser_manager_register(mgr, &mock_plugin, NULL, &h1);
    parser_manager_register(mgr, &mock_plugin_v2, NULL, &h2);

    if (parser_manager_plugin_count(mgr) != 2) {
        FAIL("expected 2 plugins");
        parser_manager_destroy(mgr);
        return;
    }

    if (h1 == h2) {
        FAIL("handles should be unique");
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_destroy(mgr);
    PASS();
}

/* ================================================================== */
/*  Unload tests                                                       */
/* ================================================================== */

static void test_unload_plugin(void) {
    TEST("unload plugin");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h;
    parser_manager_register(mgr, &mock_plugin, NULL, &h);

    mock_destroy_called = false;
    ParserManagerStatus st = parser_manager_unload(mgr, h);
    if (st != PM_OK) {
        char buf[128];
        snprintf(buf, sizeof(buf), "unload failed: %s",
                 parser_manager_status_string(st));
        FAIL(buf);
        parser_manager_destroy(mgr);
        return;
    }

    if (!mock_destroy_called) {
        FAIL("destroy not called");
        parser_manager_destroy(mgr);
        return;
    }

    if (parser_manager_plugin_count(mgr) != 0) {
        FAIL("expected 0 plugins after unload");
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_destroy(mgr);
    PASS();
}

static void test_unload_invalid_handle(void) {
    TEST("unload with invalid handle");
    ParserManager *mgr = parser_manager_create(NULL);

    ParserManagerStatus st = parser_manager_unload(mgr, LIME_PLUGIN_HANDLE_INVALID);
    if (st != PM_ERR_INVALID_ARG) {
        FAIL("expected INVALID_ARG");
        parser_manager_destroy(mgr);
        return;
    }

    st = parser_manager_unload(mgr, 999);
    if (st != PM_ERR_PLUGIN_NOT_FOUND) {
        FAIL("expected PLUGIN_NOT_FOUND");
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_destroy(mgr);
    PASS();
}

static void test_unload_active_plugin(void) {
    TEST("unload the active plugin clears active state");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h;
    parser_manager_register(mgr, &mock_plugin, NULL, &h);
    parser_manager_set_active(mgr, h, "test.y");

    if (parser_manager_get_active(mgr) != h) {
        FAIL("plugin should be active");
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_unload(mgr, h);

    if (parser_manager_get_active(mgr) != LIME_PLUGIN_HANDLE_INVALID) {
        FAIL("active should be cleared after unloading active plugin");
        parser_manager_destroy(mgr);
        return;
    }

    ParserSnapshot *snap = parser_manager_get_snapshot(mgr);
    if (snap != NULL) {
        FAIL("snapshot should be NULL after unloading active plugin");
        lemon_snapshot_release(snap);
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_destroy(mgr);
    PASS();
}

/* ================================================================== */
/*  Active parser management tests                                     */
/* ================================================================== */

static void test_set_active_with_grammar(void) {
    TEST("set_active creates snapshot from grammar");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h;
    parser_manager_register(mgr, &mock_plugin, NULL, &h);

    ParserManagerStatus st = parser_manager_set_active(mgr, h, "test.y");
    if (st != PM_OK) {
        char buf[128];
        snprintf(buf, sizeof(buf), "set_active failed: %s",
                 parser_manager_status_string(st));
        FAIL(buf);
        parser_manager_destroy(mgr);
        return;
    }

    if (parser_manager_get_active(mgr) != h) {
        FAIL("active handle mismatch");
        parser_manager_destroy(mgr);
        return;
    }

    ParserSnapshot *snap = parser_manager_get_snapshot(mgr);
    if (snap == NULL) {
        FAIL("expected non-NULL snapshot");
        parser_manager_destroy(mgr);
        return;
    }

    lemon_snapshot_release(snap);
    parser_manager_destroy(mgr);
    PASS();
}

static void test_set_active_null_grammar(void) {
    TEST("set_active with NULL grammar does not create snapshot");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h;
    parser_manager_register(mgr, &mock_plugin, NULL, &h);

    ParserManagerStatus st = parser_manager_set_active(mgr, h, NULL);
    if (st != PM_OK) {
        char buf[128];
        snprintf(buf, sizeof(buf), "set_active(NULL) failed: %s",
                 parser_manager_status_string(st));
        FAIL(buf);
        parser_manager_destroy(mgr);
        return;
    }

    /* Active handle should be set but no snapshot */
    if (parser_manager_get_active(mgr) != h) {
        FAIL("active handle mismatch");
        parser_manager_destroy(mgr);
        return;
    }

    /* Snapshot may or may not be NULL depending on whether one was previously set.
    ** Since this is the first activation, it should be NULL. */
    ParserSnapshot *snap = parser_manager_get_snapshot(mgr);
    if (snap != NULL) {
        FAIL("expected NULL snapshot when grammar is NULL");
        lemon_snapshot_release(snap);
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_destroy(mgr);
    PASS();
}

static void test_set_active_invalid_handle(void) {
    TEST("set_active with invalid handle");
    ParserManager *mgr = parser_manager_create(NULL);

    ParserManagerStatus st = parser_manager_set_active(
        mgr, LIME_PLUGIN_HANDLE_INVALID, NULL);
    if (st != PM_ERR_INVALID_ARG) {
        FAIL("expected INVALID_ARG");
        parser_manager_destroy(mgr);
        return;
    }

    st = parser_manager_set_active(mgr, 999, NULL);
    if (st != PM_ERR_PLUGIN_NOT_FOUND) {
        FAIL("expected PLUGIN_NOT_FOUND");
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_destroy(mgr);
    PASS();
}

static void test_set_active_snapshot_failure(void) {
    TEST("set_active when create_snapshot fails");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h;
    parser_manager_register(mgr, &mock_plugin_fail_snap, NULL, &h);

    ParserManagerStatus st = parser_manager_set_active(mgr, h, "test.y");
    if (st != PM_ERR_SNAPSHOT_FAILED) {
        char buf[128];
        snprintf(buf, sizeof(buf), "expected SNAPSHOT_FAILED, got %s",
                 parser_manager_status_string(st));
        FAIL(buf);
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_destroy(mgr);
    PASS();
}

static void test_get_active_no_plugin(void) {
    TEST("get_active with no plugins");
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h = parser_manager_get_active(mgr);
    if (h != LIME_PLUGIN_HANDLE_INVALID) {
        FAIL("expected INVALID handle when no plugins");
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_destroy(mgr);
    PASS();
}

static void test_get_active_null_mgr(void) {
    TEST("get_active(NULL) returns INVALID");
    LimePluginHandle h = parser_manager_get_active(NULL);
    if (h != LIME_PLUGIN_HANDLE_INVALID) {
        FAIL("expected INVALID handle");
        return;
    }
    PASS();
}

/* ================================================================== */
/*  Snapshot management tests                                          */
/* ================================================================== */

static void test_set_snapshot_directly(void) {
    TEST("set_snapshot directly");
    ParserManager *mgr = parser_manager_create(NULL);

    ParserSnapshot *snap = make_mock_snapshot();
    if (snap == NULL) {
        FAIL("make_mock_snapshot returned NULL");
        parser_manager_destroy(mgr);
        return;
    }

    ParserManagerStatus st = parser_manager_set_snapshot(mgr, snap);
    if (st != PM_OK) {
        FAIL("set_snapshot failed");
        snapshot_release(snap);
        parser_manager_destroy(mgr);
        return;
    }

    /* The manager should have acquired a ref, so we can release ours */
    ParserSnapshot *retrieved = parser_manager_get_snapshot(mgr);
    if (retrieved == NULL) {
        FAIL("expected non-NULL snapshot");
        snapshot_release(snap);
        parser_manager_destroy(mgr);
        return;
    }

    /* Should be the same snapshot */
    if (retrieved != snap) {
        FAIL("retrieved snapshot differs from original");
        lemon_snapshot_release(retrieved);
        snapshot_release(snap);
        parser_manager_destroy(mgr);
        return;
    }

    lemon_snapshot_release(retrieved);
    snapshot_release(snap);
    parser_manager_destroy(mgr);
    PASS();
}

static void test_get_snapshot_null_mgr(void) {
    TEST("get_snapshot(NULL) returns NULL");
    ParserSnapshot *snap = parser_manager_get_snapshot(NULL);
    if (snap != NULL) {
        FAIL("expected NULL");
        lemon_snapshot_release(snap);
        return;
    }
    PASS();
}

static void test_snapshot_replaced_on_set_active(void) {
    TEST("set_active replaces previous snapshot");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h;
    parser_manager_register(mgr, &mock_plugin, NULL, &h);

    /* Set active with a grammar file to create first snapshot */
    parser_manager_set_active(mgr, h, "first.y");
    ParserSnapshot *snap1 = parser_manager_get_snapshot(mgr);

    /* Set active again to replace */
    parser_manager_set_active(mgr, h, "second.y");
    ParserSnapshot *snap2 = parser_manager_get_snapshot(mgr);

    if (snap1 == NULL || snap2 == NULL) {
        FAIL("expected non-NULL snapshots");
        if (snap1) lemon_snapshot_release(snap1);
        if (snap2) lemon_snapshot_release(snap2);
        parser_manager_destroy(mgr);
        return;
    }

    /* They should be different snapshot objects */
    if (snap1 == snap2) {
        FAIL("snapshots should be different objects");
        lemon_snapshot_release(snap1);
        lemon_snapshot_release(snap2);
        parser_manager_destroy(mgr);
        return;
    }

    lemon_snapshot_release(snap1);
    lemon_snapshot_release(snap2);
    parser_manager_destroy(mgr);
    PASS();
}

/* ================================================================== */
/*  Enumeration and introspection tests                                */
/* ================================================================== */

static void test_get_plugin_info(void) {
    TEST("get_plugin_info returns correct data");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h;
    parser_manager_register(mgr, &mock_plugin, NULL, &h);
    parser_manager_set_active(mgr, h, NULL);

    LimePluginInfo info;
    ParserManagerStatus st = parser_manager_get_plugin_info(mgr, h, &info);
    if (st != PM_OK) {
        FAIL("get_plugin_info failed");
        parser_manager_destroy(mgr);
        return;
    }

    int ok = 1;
    if (info.handle != h) { ok = 0; FAIL("handle mismatch"); }
    else if (info.name == NULL || strcmp(info.name, "mock_parser") != 0) {
        ok = 0; FAIL("name mismatch");
    }
    else if (info.version.major != 1 || info.version.minor != 2 ||
             info.version.patch != 3) {
        ok = 0; FAIL("version mismatch");
    }
    else if (!(info.capabilities & LIME_CAP_SNAPSHOT)) {
        ok = 0; FAIL("capabilities mismatch");
    }
    else if (!info.is_active) {
        ok = 0; FAIL("should be active");
    }
    else if (info.is_dynamic) {
        ok = 0; FAIL("should not be dynamic");
    }

    parser_manager_destroy(mgr);
    if (ok) PASS();
}

static void test_get_plugin_info_invalid(void) {
    TEST("get_plugin_info with invalid args");
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginInfo info;
    if (parser_manager_get_plugin_info(mgr, LIME_PLUGIN_HANDLE_INVALID, &info)
        != PM_ERR_INVALID_ARG) {
        FAIL("expected INVALID_ARG for invalid handle");
        parser_manager_destroy(mgr);
        return;
    }

    if (parser_manager_get_plugin_info(mgr, 999, &info)
        != PM_ERR_PLUGIN_NOT_FOUND) {
        FAIL("expected PLUGIN_NOT_FOUND");
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_destroy(mgr);
    PASS();
}

static void test_list_plugins(void) {
    TEST("list_plugins enumerates all loaded plugins");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h1, h2;
    parser_manager_register(mgr, &mock_plugin, NULL, &h1);
    parser_manager_register(mgr, &mock_plugin_v2, NULL, &h2);

    LimePluginInfo infos[4];
    uint32_t actual;
    ParserManagerStatus st = parser_manager_list_plugins(mgr, infos, 4, &actual);
    if (st != PM_OK) {
        FAIL("list_plugins failed");
        parser_manager_destroy(mgr);
        return;
    }

    if (actual != 2) {
        char buf[64];
        snprintf(buf, sizeof(buf), "expected 2 plugins, got %u", actual);
        FAIL(buf);
        parser_manager_destroy(mgr);
        return;
    }

    /* Verify we got both names */
    bool found_mock = false, found_v2 = false;
    for (uint32_t i = 0; i < actual; i++) {
        if (infos[i].name && strcmp(infos[i].name, "mock_parser") == 0)
            found_mock = true;
        if (infos[i].name && strcmp(infos[i].name, "mock_parser_v2") == 0)
            found_v2 = true;
    }

    if (!found_mock || !found_v2) {
        FAIL("missing one or both plugin names");
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_destroy(mgr);
    PASS();
}

static void test_list_plugins_small_buffer(void) {
    TEST("list_plugins with small buffer returns actual_count > max");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h1, h2;
    parser_manager_register(mgr, &mock_plugin, NULL, &h1);
    parser_manager_register(mgr, &mock_plugin_v2, NULL, &h2);

    LimePluginInfo infos[1];
    uint32_t actual;
    ParserManagerStatus st = parser_manager_list_plugins(mgr, infos, 1, &actual);
    if (st != PM_OK) {
        FAIL("list_plugins failed");
        parser_manager_destroy(mgr);
        return;
    }

    if (actual != 2) {
        char buf[64];
        snprintf(buf, sizeof(buf), "expected actual_count=2, got %u", actual);
        FAIL(buf);
        parser_manager_destroy(mgr);
        return;
    }

    /* Only 1 entry should have been filled */
    if (infos[0].handle == LIME_PLUGIN_HANDLE_INVALID) {
        FAIL("first entry not filled");
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_destroy(mgr);
    PASS();
}

static void test_find_by_name(void) {
    TEST("find_by_name returns correct handle");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h;
    parser_manager_register(mgr, &mock_plugin, NULL, &h);

    LimePluginHandle found = parser_manager_find_by_name(mgr, "mock_parser");
    if (found != h) {
        FAIL("handle mismatch");
        parser_manager_destroy(mgr);
        return;
    }

    found = parser_manager_find_by_name(mgr, "nonexistent");
    if (found != LIME_PLUGIN_HANDLE_INVALID) {
        FAIL("expected INVALID for nonexistent name");
        parser_manager_destroy(mgr);
        return;
    }

    found = parser_manager_find_by_name(mgr, NULL);
    if (found != LIME_PLUGIN_HANDLE_INVALID) {
        FAIL("expected INVALID for NULL name");
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_destroy(mgr);
    PASS();
}

static void test_plugin_count(void) {
    TEST("plugin_count tracks registrations");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    if (parser_manager_plugin_count(mgr) != 0) {
        FAIL("expected 0");
        parser_manager_destroy(mgr);
        return;
    }

    LimePluginHandle h1;
    parser_manager_register(mgr, &mock_plugin, NULL, &h1);
    if (parser_manager_plugin_count(mgr) != 1) {
        FAIL("expected 1");
        parser_manager_destroy(mgr);
        return;
    }

    LimePluginHandle h2;
    parser_manager_register(mgr, &mock_plugin_v2, NULL, &h2);
    if (parser_manager_plugin_count(mgr) != 2) {
        FAIL("expected 2");
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_unload(mgr, h1);
    if (parser_manager_plugin_count(mgr) != 1) {
        FAIL("expected 1 after unload");
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_destroy(mgr);
    PASS();
}

static void test_plugin_count_null(void) {
    TEST("plugin_count(NULL) returns 0");
    if (parser_manager_plugin_count(NULL) != 0) {
        FAIL("expected 0");
        return;
    }
    PASS();
}

/* ================================================================== */
/*  Hot-swap tests                                                     */
/* ================================================================== */

static void test_hot_swap(void) {
    TEST("hot_swap atomically replaces active plugin");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h1, h2;
    parser_manager_register(mgr, &mock_plugin, NULL, &h1);
    parser_manager_register(mgr, &mock_plugin_v2, NULL, &h2);

    parser_manager_set_active(mgr, h1, "old.y");
    ParserSnapshot *old_snap = parser_manager_get_snapshot(mgr);

    ParserManagerStatus st = parser_manager_hot_swap(mgr, h2, "new.y");
    if (st != PM_OK) {
        char buf[128];
        snprintf(buf, sizeof(buf), "hot_swap failed: %s",
                 parser_manager_status_string(st));
        FAIL(buf);
        if (old_snap) lemon_snapshot_release(old_snap);
        parser_manager_destroy(mgr);
        return;
    }

    if (parser_manager_get_active(mgr) != h2) {
        FAIL("active should be h2 after swap");
        if (old_snap) lemon_snapshot_release(old_snap);
        parser_manager_destroy(mgr);
        return;
    }

    ParserSnapshot *new_snap = parser_manager_get_snapshot(mgr);
    if (new_snap == NULL) {
        FAIL("expected non-NULL snapshot after swap");
        if (old_snap) lemon_snapshot_release(old_snap);
        parser_manager_destroy(mgr);
        return;
    }

    if (new_snap == old_snap) {
        FAIL("snapshot should have changed");
        lemon_snapshot_release(new_snap);
        lemon_snapshot_release(old_snap);
        parser_manager_destroy(mgr);
        return;
    }

    /* Old snapshot should still be valid (we hold a reference) */
    if (old_snap) lemon_snapshot_release(old_snap);
    lemon_snapshot_release(new_snap);
    parser_manager_destroy(mgr);
    PASS();
}

static void test_hot_swap_preserves_old_snapshot_refs(void) {
    TEST("hot_swap: old snapshot survives via held reference");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h1, h2;
    parser_manager_register(mgr, &mock_plugin, NULL, &h1);
    parser_manager_register(mgr, &mock_plugin_v2, NULL, &h2);

    parser_manager_set_active(mgr, h1, "grammar.y");

    /* Simulate a parse session pinning the snapshot */
    ParserSnapshot *pinned = parser_manager_get_snapshot(mgr);
    if (pinned == NULL) {
        FAIL("expected snapshot");
        parser_manager_destroy(mgr);
        return;
    }

    /* Hot-swap to a new plugin */
    parser_manager_hot_swap(mgr, h2, "new.y");

    /* The pinned snapshot should still be usable */
    if (pinned->version != 42) {
        /* Our mock always creates version 42 */
        FAIL("pinned snapshot data corrupted");
        lemon_snapshot_release(pinned);
        parser_manager_destroy(mgr);
        return;
    }

    /* Release the pinned snapshot; should not crash */
    lemon_snapshot_release(pinned);
    parser_manager_destroy(mgr);
    PASS();
}

/* ================================================================== */
/*  Version utility tests                                              */
/* ================================================================== */

static void test_version_compare_equal(void) {
    TEST("version_compare: equal versions");
    LimePluginVersion a = {1, 2, 3};
    LimePluginVersion b = {1, 2, 3};
    if (lime_plugin_version_compare(a, b) != 0) {
        FAIL("expected 0");
        return;
    }
    PASS();
}

static void test_version_compare_major(void) {
    TEST("version_compare: major difference");
    LimePluginVersion a = {2, 0, 0};
    LimePluginVersion b = {1, 9, 9};
    if (lime_plugin_version_compare(a, b) <= 0) {
        FAIL("expected a > b");
        return;
    }
    if (lime_plugin_version_compare(b, a) >= 0) {
        FAIL("expected b < a");
        return;
    }
    PASS();
}

static void test_version_compare_minor(void) {
    TEST("version_compare: minor difference");
    LimePluginVersion a = {1, 3, 0};
    LimePluginVersion b = {1, 2, 9};
    if (lime_plugin_version_compare(a, b) <= 0) {
        FAIL("expected a > b");
        return;
    }
    PASS();
}

static void test_version_compare_patch(void) {
    TEST("version_compare: patch difference");
    LimePluginVersion a = {1, 2, 4};
    LimePluginVersion b = {1, 2, 3};
    if (lime_plugin_version_compare(a, b) <= 0) {
        FAIL("expected a > b");
        return;
    }
    PASS();
}

static void test_version_satisfies(void) {
    TEST("version_satisfies");
    LimePluginVersion actual = {1, 3, 0};
    LimePluginVersion required = {1, 2, 0};

    if (!lime_plugin_version_satisfies(actual, required)) {
        FAIL("1.3.0 should satisfy 1.2.0");
        return;
    }

    if (lime_plugin_version_satisfies(required, actual)) {
        FAIL("1.2.0 should NOT satisfy 1.3.0");
        return;
    }

    /* Equal satisfies */
    if (!lime_plugin_version_satisfies(actual, actual)) {
        FAIL("1.3.0 should satisfy 1.3.0");
        return;
    }

    PASS();
}

static void test_version_string(void) {
    TEST("version_string formatting");
    char buf[16];
    LimePluginVersion v = {1, 2, 3};
    char *result = lime_plugin_version_string(v, buf, sizeof(buf));
    if (result != buf) {
        FAIL("should return buf");
        return;
    }
    if (strcmp(buf, "1.2.3") != 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected '1.2.3', got '%s'", buf);
        FAIL(msg);
        return;
    }
    PASS();
}

static void test_version_string_null_buf(void) {
    TEST("version_string with NULL buf");
    LimePluginVersion v = {1, 0, 0};
    char *result = lime_plugin_version_string(v, NULL, 0);
    if (result != NULL) {
        FAIL("expected NULL");
        return;
    }
    PASS();
}

/* ================================================================== */
/*  Status string tests                                                */
/* ================================================================== */

static void test_status_strings(void) {
    TEST("parser_manager_status_string for all codes");
    /* Verify all codes produce non-empty strings */
    ParserManagerStatus codes[] = {
        PM_OK, PM_ERR_INVALID_ARG, PM_ERR_ALLOC,
        PM_ERR_PLUGIN_NOT_FOUND, PM_ERR_DUPLICATE_NAME,
        PM_ERR_ABI_MISMATCH, PM_ERR_INIT_FAILED,
        PM_ERR_DLOPEN_FAILED, PM_ERR_NO_ENTRY_POINT,
        PM_ERR_SNAPSHOT_FAILED, PM_ERR_VALIDATION_FAILED,
        PM_ERR_PLUGIN_IN_USE, PM_ERR_NO_ACTIVE_PLUGIN,
        PM_ERR_CAPABILITY_MISSING,
    };
    int ncodes = sizeof(codes) / sizeof(codes[0]);

    for (int i = 0; i < ncodes; i++) {
        const char *s = parser_manager_status_string(codes[i]);
        if (s == NULL || strlen(s) == 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "empty string for code %d", codes[i]);
            FAIL(buf);
            return;
        }
    }
    PASS();
}

/* ================================================================== */
/*  Dynamic loading tests (limited - no actual .so in test env)        */
/* ================================================================== */

static void test_load_nonexistent_library(void) {
    TEST("load: nonexistent library path");
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h;
    ParserManagerStatus st = parser_manager_load(
        mgr, "/nonexistent/path/to/plugin.so", NULL, &h);

    if (st != PM_ERR_DLOPEN_FAILED) {
        char buf[128];
        snprintf(buf, sizeof(buf), "expected DLOPEN_FAILED, got %s",
                 parser_manager_status_string(st));
        FAIL(buf);
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_destroy(mgr);
    PASS();
}

static void test_load_null_args(void) {
    TEST("load: NULL arguments");
    ParserManager *mgr = parser_manager_create(NULL);
    LimePluginHandle h;

    if (parser_manager_load(NULL, "plugin.so", NULL, &h) != PM_ERR_INVALID_ARG) {
        FAIL("expected INVALID_ARG for NULL mgr");
        parser_manager_destroy(mgr);
        return;
    }

    if (parser_manager_load(mgr, NULL, NULL, &h) != PM_ERR_INVALID_ARG) {
        FAIL("expected INVALID_ARG for NULL path");
        parser_manager_destroy(mgr);
        return;
    }

    if (parser_manager_load(mgr, "plugin.so", NULL, NULL) != PM_ERR_INVALID_ARG) {
        FAIL("expected INVALID_ARG for NULL handle_out");
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_destroy(mgr);
    PASS();
}

/* ================================================================== */
/*  Concurrency tests                                                  */
/* ================================================================== */

#define NUM_READER_THREADS 8
#define READS_PER_THREAD 1000

typedef struct ReaderArg {
    ParserManager *mgr;
    int errors;
} ReaderArg;

static void *reader_thread_fn(void *arg) {
    ReaderArg *ra = (ReaderArg *)arg;
    ra->errors = 0;

    for (int i = 0; i < READS_PER_THREAD; i++) {
        ParserSnapshot *snap = parser_manager_get_snapshot(ra->mgr);
        if (snap == NULL) {
            ra->errors++;
            continue;
        }
        /* Verify the snapshot looks valid */
        if (snap->version == 0 && snap->nsymbol == 0) {
            /* Might be an empty snapshot, that's ok */
        }
        lemon_snapshot_release(snap);
    }

    return NULL;
}

static void test_concurrent_get_snapshot(void) {
    TEST("concurrent get_snapshot from multiple readers");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h;
    parser_manager_register(mgr, &mock_plugin, NULL, &h);
    parser_manager_set_active(mgr, h, "test.y");

    pthread_t threads[NUM_READER_THREADS];
    ReaderArg args[NUM_READER_THREADS];

    for (int i = 0; i < NUM_READER_THREADS; i++) {
        args[i].mgr = mgr;
        args[i].errors = 0;
        pthread_create(&threads[i], NULL, reader_thread_fn, &args[i]);
    }

    for (int i = 0; i < NUM_READER_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    int total_errors = 0;
    for (int i = 0; i < NUM_READER_THREADS; i++) {
        total_errors += args[i].errors;
    }

    if (total_errors > 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%d errors in %d reads",
                 total_errors, NUM_READER_THREADS * READS_PER_THREAD);
        FAIL(buf);
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_destroy(mgr);
    PASS();
}

typedef struct SwapArg {
    ParserManager *mgr;
    LimePluginHandle h1;
    LimePluginHandle h2;
    int swaps;
    int errors;
} SwapArg;

static void *swap_thread_fn(void *arg) {
    SwapArg *sa = (SwapArg *)arg;
    sa->errors = 0;

    for (int i = 0; i < sa->swaps; i++) {
        LimePluginHandle target = (i % 2 == 0) ? sa->h1 : sa->h2;
        ParserManagerStatus st = parser_manager_hot_swap(
            sa->mgr, target, "swap.y");
        if (st != PM_OK) {
            sa->errors++;
        }
    }

    return NULL;
}

static void test_concurrent_swap_and_read(void) {
    TEST("concurrent hot_swap + get_snapshot");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h1, h2;
    parser_manager_register(mgr, &mock_plugin, NULL, &h1);
    parser_manager_register(mgr, &mock_plugin_v2, NULL, &h2);
    parser_manager_set_active(mgr, h1, "initial.y");

    /* Launch reader threads */
    pthread_t readers[4];
    ReaderArg reader_args[4];
    for (int i = 0; i < 4; i++) {
        reader_args[i].mgr = mgr;
        reader_args[i].errors = 0;
        pthread_create(&readers[i], NULL, reader_thread_fn, &reader_args[i]);
    }

    /* Launch swapper threads */
    pthread_t swappers[2];
    SwapArg swap_args[2];
    for (int i = 0; i < 2; i++) {
        swap_args[i].mgr = mgr;
        swap_args[i].h1 = h1;
        swap_args[i].h2 = h2;
        swap_args[i].swaps = 50;
        swap_args[i].errors = 0;
        pthread_create(&swappers[i], NULL, swap_thread_fn, &swap_args[i]);
    }

    /* Wait for all threads */
    for (int i = 0; i < 2; i++) {
        pthread_join(swappers[i], NULL);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(readers[i], NULL);
    }

    int total_errors = 0;
    for (int i = 0; i < 4; i++) total_errors += reader_args[i].errors;
    for (int i = 0; i < 2; i++) total_errors += swap_args[i].errors;

    if (total_errors > 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%d errors during concurrent swap+read",
                 total_errors);
        FAIL(buf);
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_destroy(mgr);
    PASS();
}

static void *register_unload_thread_fn(void *arg) {
    ParserManager *mgr = (ParserManager *)arg;

    /* Register and immediately unload, several times */
    for (int i = 0; i < 20; i++) {
        LimePluginHandle h;
        ParserManagerStatus st = parser_manager_register(
            mgr, &mock_plugin_v2, NULL, &h);
        if (st == PM_OK) {
            parser_manager_unload(mgr, h);
        }
        /* DUPLICATE_NAME is expected if another thread registered the same name */
    }

    return NULL;
}

static void test_concurrent_register_unload(void) {
    TEST("concurrent register/unload");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    pthread_t threads[4];
    for (int i = 0; i < 4; i++) {
        pthread_create(&threads[i], NULL, register_unload_thread_fn, mgr);
    }

    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }

    /* Manager should be in a consistent state (0 or more plugins) */
    uint32_t count = parser_manager_plugin_count(mgr);
    /* We can't predict exactly how many due to race conditions,
    ** but the manager should not have crashed */
    (void)count;

    parser_manager_destroy(mgr);
    PASS();
}

/* ================================================================== */
/*  Handle monotonicity tests                                          */
/* ================================================================== */

static void test_handles_monotonic(void) {
    TEST("handles are monotonically increasing");
    reset_mock_state();
    ParserManager *mgr = parser_manager_create(NULL);

    LimePluginHandle h1, h2;
    parser_manager_register(mgr, &mock_plugin, NULL, &h1);
    parser_manager_register(mgr, &mock_plugin_v2, NULL, &h2);

    if (h2 <= h1) {
        FAIL("h2 should be > h1");
        parser_manager_destroy(mgr);
        return;
    }

    /* Unload h1 and register again -- new handle should be > h2 */
    parser_manager_unload(mgr, h1);

    /* Re-register (allowed since h1 was unloaded, clearing the name) */
    LimePluginHandle h3;
    parser_manager_register(mgr, &mock_plugin, NULL, &h3);
    if (h3 <= h2) {
        FAIL("h3 should be > h2 (handles never reused)");
        parser_manager_destroy(mgr);
        return;
    }

    parser_manager_destroy(mgr);
    PASS();
}

/* ================================================================== */
/*  Max plugins limit test                                             */
/* ================================================================== */

static void test_max_plugins_limit(void) {
    TEST("max_plugins config limits registrations");
    reset_mock_state();
    ParserManagerConfig config = {
        .max_plugins = 1,
        .validate_on_load = false,
        .auto_jit = false,
        .plugin_search_paths = NULL,
    };
    ParserManager *mgr = parser_manager_create(&config);

    LimePluginHandle h1;
    ParserManagerStatus st = parser_manager_register(mgr, &mock_plugin, NULL, &h1);
    if (st != PM_OK) {
        FAIL("first register should succeed");
        parser_manager_destroy(mgr);
        return;
    }

    LimePluginHandle h2;
    st = parser_manager_register(mgr, &mock_plugin_v2, NULL, &h2);
    if (st == PM_OK) {
        FAIL("second register should fail due to max_plugins=1");
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
    printf("Parser Manager unit tests\n");
    printf("=========================\n\n");

    printf("[Lifecycle]\n");
    test_create_destroy_null_config();
    test_create_with_config();
    test_destroy_null();

    printf("\n[Registration]\n");
    test_register_static_plugin();
    test_register_null_args();
    test_register_abi_mismatch();
    test_register_duplicate_name();
    test_register_init_failure();
    test_register_multiple_plugins();

    printf("\n[Unloading]\n");
    test_unload_plugin();
    test_unload_invalid_handle();
    test_unload_active_plugin();

    printf("\n[Active Parser]\n");
    test_set_active_with_grammar();
    test_set_active_null_grammar();
    test_set_active_invalid_handle();
    test_set_active_snapshot_failure();
    test_get_active_no_plugin();
    test_get_active_null_mgr();

    printf("\n[Snapshots]\n");
    test_set_snapshot_directly();
    test_get_snapshot_null_mgr();
    test_snapshot_replaced_on_set_active();

    printf("\n[Enumeration]\n");
    test_get_plugin_info();
    test_get_plugin_info_invalid();
    test_list_plugins();
    test_list_plugins_small_buffer();
    test_find_by_name();
    test_plugin_count();
    test_plugin_count_null();

    printf("\n[Hot-swap]\n");
    test_hot_swap();
    test_hot_swap_preserves_old_snapshot_refs();

    printf("\n[Version Utilities]\n");
    test_version_compare_equal();
    test_version_compare_major();
    test_version_compare_minor();
    test_version_compare_patch();
    test_version_satisfies();
    test_version_string();
    test_version_string_null_buf();

    printf("\n[Status Strings]\n");
    test_status_strings();

    printf("\n[Dynamic Loading]\n");
    test_load_nonexistent_library();
    test_load_null_args();

    printf("\n[Concurrency]\n");
    test_concurrent_get_snapshot();
    test_concurrent_swap_and_read();
    test_concurrent_register_unload();

    printf("\n[Handle Monotonicity]\n");
    test_handles_monotonic();

    printf("\n[Config Limits]\n");
    test_max_plugins_limit();

    printf("\n=========================\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);
    printf("=========================\n");

    return (tests_passed == tests_run) ? 0 : 1;
}
