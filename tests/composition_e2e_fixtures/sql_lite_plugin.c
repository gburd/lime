/*
** sql_lite_plugin -- minimal "SQL-lite grammar" plugin used by
** tests/test_composition_e2e.c.
**
** The plugin's create_snapshot() ignores its grammar_file argument
** and produces a deterministic, identifiable synthetic snapshot
** with a known shape:
**
**   nsymbol = 8, nterminal = 4, nrule = 3, nstate = 2,
**   action_count = 5, yy_action[i] = (i + 1), yy_lookahead[i] = (i + 100)
**
** This is the same shape used by tests/test_shared_token_lifetime.c's
** make_plugin_snapshot helper, which the in-tree composition unit
** tests already validate against.  Producing it from a real .so
** plugin closes the loop: compose_snapshots() works against runtime-
** loaded plugins, not just in-test fixtures.
*/
#include "parser_manager.h"
#include "snapshot.h"
#include "snapshot_modify.h"

#include <stdlib.h>
#include <string.h>

static bool slp_init(void *user_data) {
    (void)user_data;
    return true;
}

static void slp_destroy(void) {
}

static const char *slp_get_name(void) {
    return "lime-sql-lite";
}

static LimePluginVersion slp_get_version(void) {
    return (LimePluginVersion){ .major = 1, .minor = 0, .patch = 0 };
}

static uint16_t slp_get_abi_major(void) {
    return LIME_PLUGIN_ABI_VERSION_MAJOR;
}

static uint16_t slp_get_abi_minor(void) {
    return LIME_PLUGIN_ABI_VERSION_MINOR;
}

static uint32_t slp_get_capabilities(void) {
    return LIME_CAP_SNAPSHOT;
}

static ParserSnapshot *slp_create_snapshot(const char *grammar_file, char **error) {
    (void)grammar_file;

    ParserSnapshot *snap = clone_snapshot(NULL);
    if (!snap) {
        if (error) {
            const char msg[] = "sql_lite_plugin: clone_snapshot failed";
            *error = malloc(sizeof(msg));
            if (*error) memcpy(*error, msg, sizeof(msg));
        }
        return NULL;
    }

    snap->nsymbol = 8;
    snap->nterminal = 4;
    snap->nrule = 3;
    snap->nstate = 2;
    snap->action_count = 5;
    snap->lookahead_count = 5;

    snap->yy_action = calloc(5, sizeof(uint16_t));
    snap->yy_lookahead = calloc(5, sizeof(uint16_t));
    snap->yy_shift_ofst = calloc(2, sizeof(int32_t));
    snap->yy_reduce_ofst = calloc(2, sizeof(int32_t));
    snap->yy_default = calloc(2, sizeof(uint16_t));

    if (!snap->yy_action || !snap->yy_lookahead || !snap->yy_shift_ofst
        || !snap->yy_reduce_ofst || !snap->yy_default) {
        snapshot_release(snap);
        if (error) {
            const char msg[] = "sql_lite_plugin: calloc failed";
            *error = malloc(sizeof(msg));
            if (*error) memcpy(*error, msg, sizeof(msg));
        }
        return NULL;
    }

    for (uint32_t i = 0; i < 5; i++) {
        snap->yy_action[i] = (uint16_t)(i + 1);
        snap->yy_lookahead[i] = (uint16_t)(i + 100);
    }

    if (error) *error = NULL;
    return snap;
}

static const LimeParserPlugin sql_lite_plugin = {
    .get_name         = slp_get_name,
    .get_version      = slp_get_version,
    .get_abi_major    = slp_get_abi_major,
    .get_abi_minor    = slp_get_abi_minor,
    .get_capabilities = slp_get_capabilities,
    .init             = slp_init,
    .destroy          = slp_destroy,
    .create_snapshot  = slp_create_snapshot,
    .validate_snapshot = NULL,
    .serialize_snapshot   = NULL,
    .deserialize_snapshot = NULL,
    ._reserved = {0},
};

LIME_PLUGIN_EXPORT
const LimeParserPlugin *lime_plugin_entry(void) {
    return &sql_lite_plugin;
}
