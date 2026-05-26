/*
** xml_plugin -- third test plugin used by tests/test_composition_e2e.c
** to verify that composing different inputs produces a different
** merkle hash.
**
**   nsymbol = 5, nterminal = 2, nrule = 4, nstate = 3, action_count = 6
**
** Distinct from json_plugin in every dimension so a composition that
** swaps json -> xml produces a verifiably different merkle root.
*/
#include "parser_manager.h"
#include "snapshot.h"
#include "snapshot_modify.h"

#include <stdlib.h>
#include <string.h>

static bool xp_init(void *user_data) {
    (void)user_data;
    return true;
}

static void xp_destroy(void) {
}

static const char *xp_get_name(void) {
    return "lime-xml";
}

static LimePluginVersion xp_get_version(void) {
    return (LimePluginVersion){ .major = 1, .minor = 0, .patch = 0 };
}

static uint16_t xp_get_abi_major(void) {
    return LIME_PLUGIN_ABI_VERSION_MAJOR;
}

static uint16_t xp_get_abi_minor(void) {
    return LIME_PLUGIN_ABI_VERSION_MINOR;
}

static uint32_t xp_get_capabilities(void) {
    return LIME_CAP_SNAPSHOT;
}

static ParserSnapshot *xp_create_snapshot(const char *grammar_file, char **error) {
    (void)grammar_file;

    ParserSnapshot *snap = clone_snapshot(NULL);
    if (!snap) {
        if (error) {
            const char msg[] = "xml_plugin: clone_snapshot failed";
            *error = malloc(sizeof(msg));
            if (*error) memcpy(*error, msg, sizeof(msg));
        }
        return NULL;
    }

    snap->nsymbol = 5;
    snap->nterminal = 2;
    snap->nrule = 4;
    snap->nstate = 3;
    snap->action_count = 6;
    snap->lookahead_count = 6;

    snap->yy_action = calloc(6, sizeof(uint16_t));
    snap->yy_lookahead = calloc(6, sizeof(uint16_t));
    snap->yy_shift_ofst = calloc(3, sizeof(int32_t));
    snap->yy_reduce_ofst = calloc(3, sizeof(int32_t));
    snap->yy_default = calloc(3, sizeof(uint16_t));

    if (!snap->yy_action || !snap->yy_lookahead || !snap->yy_shift_ofst
        || !snap->yy_reduce_ofst || !snap->yy_default) {
        snapshot_release(snap);
        if (error) {
            const char msg[] = "xml_plugin: calloc failed";
            *error = malloc(sizeof(msg));
            if (*error) memcpy(*error, msg, sizeof(msg));
        }
        return NULL;
    }

    for (uint32_t i = 0; i < 6; i++) {
        snap->yy_action[i] = (uint16_t)(i + 1000);
        snap->yy_lookahead[i] = (uint16_t)(i + 2000);
    }

    if (error) *error = NULL;
    return snap;
}

static const LimeParserPlugin xml_plugin = {
    .get_name         = xp_get_name,
    .get_version      = xp_get_version,
    .get_abi_major    = xp_get_abi_major,
    .get_abi_minor    = xp_get_abi_minor,
    .get_capabilities = xp_get_capabilities,
    .init             = xp_init,
    .destroy          = xp_destroy,
    .create_snapshot  = xp_create_snapshot,
    .validate_snapshot = NULL,
    .serialize_snapshot   = NULL,
    .deserialize_snapshot = NULL,
    ._reserved = {0},
};

LIME_PLUGIN_EXPORT
const LimeParserPlugin *lime_plugin_entry(void) {
    return &xml_plugin;
}
