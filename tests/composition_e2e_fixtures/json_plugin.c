/*
** json_plugin -- second test plugin used by tests/test_composition_e2e.c.
**
** Same shape as sql_lite_plugin but with distinct counts so the
** composed result has predictable summed dimensions:
**
**   nsymbol = 6, nterminal = 3, nrule = 2, nstate = 2, action_count = 4
**
** Composed with sql_lite_plugin's snapshot, the union should have:
**   nrule = 3 + 2 = 5
**   action_count = 5 + 4 = 9
**   nstate = 2 + 2 = 4
*/
#include "parser_manager.h"
#include "snapshot.h"
#include "snapshot_modify.h"

#include <stdlib.h>
#include <string.h>

static bool jp_init(void *user_data) {
    (void)user_data;
    return true;
}

static void jp_destroy(void) {
}

static const char *jp_get_name(void) {
    return "lime-json";
}

static LimePluginVersion jp_get_version(void) {
    return (LimePluginVersion){ .major = 1, .minor = 0, .patch = 0 };
}

static uint16_t jp_get_abi_major(void) {
    return LIME_PLUGIN_ABI_VERSION_MAJOR;
}

static uint16_t jp_get_abi_minor(void) {
    return LIME_PLUGIN_ABI_VERSION_MINOR;
}

static uint32_t jp_get_capabilities(void) {
    return LIME_CAP_SNAPSHOT;
}

static ParserSnapshot *jp_create_snapshot(const char *grammar_file, char **error) {
    (void)grammar_file;

    ParserSnapshot *snap = clone_snapshot(NULL);
    if (!snap) {
        if (error) {
            const char msg[] = "json_plugin: clone_snapshot failed";
            *error = malloc(sizeof(msg));
            if (*error) memcpy(*error, msg, sizeof(msg));
        }
        return NULL;
    }

    snap->nsymbol = 6;
    snap->nterminal = 3;
    snap->nrule = 2;
    snap->nstate = 2;
    snap->action_count = 4;
    snap->lookahead_count = 4;

    snap->yy_action = calloc(4, sizeof(uint16_t));
    snap->yy_lookahead = calloc(4, sizeof(uint16_t));
    snap->yy_shift_ofst = calloc(2, sizeof(int32_t));
    snap->yy_reduce_ofst = calloc(2, sizeof(int32_t));
    snap->yy_default = calloc(2, sizeof(uint16_t));

    if (!snap->yy_action || !snap->yy_lookahead || !snap->yy_shift_ofst
        || !snap->yy_reduce_ofst || !snap->yy_default) {
        snapshot_release(snap);
        if (error) {
            const char msg[] = "json_plugin: calloc failed";
            *error = malloc(sizeof(msg));
            if (*error) memcpy(*error, msg, sizeof(msg));
        }
        return NULL;
    }

    for (uint32_t i = 0; i < 4; i++) {
        snap->yy_action[i] = (uint16_t)(i + 50);
        snap->yy_lookahead[i] = (uint16_t)(i + 200);
    }

    if (error) *error = NULL;
    return snap;
}

static const LimeParserPlugin json_plugin = {
    .get_name         = jp_get_name,
    .get_version      = jp_get_version,
    .get_abi_major    = jp_get_abi_major,
    .get_abi_minor    = jp_get_abi_minor,
    .get_capabilities = jp_get_capabilities,
    .init             = jp_init,
    .destroy          = jp_destroy,
    .create_snapshot  = jp_create_snapshot,
    .validate_snapshot = NULL,
    .serialize_snapshot   = NULL,
    .deserialize_snapshot = NULL,
    ._reserved = {0},
};

LIME_PLUGIN_EXPORT
const LimeParserPlugin *lime_plugin_entry(void) {
    return &json_plugin;
}
