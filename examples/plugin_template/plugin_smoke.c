/*
** Plugin smoke-test driver.
**
** Wired from examples/plugin_template/meson.build as the meson test
** for the plugin_template example.  The job of this binary is narrow:
** dlopen() the built sql_plugin shared library, look up the
** lime_plugin_entry symbol, validate the plugin's identity callbacks
** (name / version / ABI / capabilities) match what sql_plugin.c
** advertises, and exit cleanly.
**
** This is the load-bearing guardrail for the runtime plugin-loading
** story that compose_snapshots() depends on.  If any of dlopen, entry
** lookup, ABI validation, or capability reporting regresses, this
** test catches it before the end-to-end composition test does.
**
** The full plugin_host binary in this directory exercises a richer
** path (create_snapshot, set_active, hot_swap) but requires a real
** grammar file to complete; this driver verifies just the metadata
** contract, which is enough to prove the plugin ABI is intact.
**
** Usage: plugin_smoke <path-to-plugin.so>
*/
#include "parser_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <plugin.so>\n", argv[0]);
        return 2;
    }

    const char *path = argv[1];

    /*
    ** Drive the load through ParserManager rather than calling
    ** dlopen() directly -- the manager is what production hosts
    ** (PostgreSQL with shared_preload_libraries, etc.) will use,
    ** so that's the contract under test.
    */
    ParserManagerConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    /* validate_on_load only exercises validate_snapshot, which we
    ** are not invoking here; leave it off. */

    ParserManager *mgr = parser_manager_create(&cfg);
    if (mgr == NULL) {
        fprintf(stderr, "FAIL: parser_manager_create returned NULL\n");
        return 1;
    }

    LimePluginHandle handle = LIME_PLUGIN_HANDLE_INVALID;
    ParserManagerStatus st = parser_manager_load(mgr, path, NULL, &handle);
    if (st != PM_OK) {
        fprintf(stderr, "FAIL: parser_manager_load(%s): %s\n",
                path, parser_manager_status_string(st));
        parser_manager_destroy(mgr);
        return 1;
    }
    if (handle == LIME_PLUGIN_HANDLE_INVALID) {
        fprintf(stderr, "FAIL: load returned PM_OK with invalid handle\n");
        parser_manager_destroy(mgr);
        return 1;
    }

    LimePluginInfo info;
    memset(&info, 0, sizeof(info));
    st = parser_manager_get_plugin_info(mgr, handle, &info);
    if (st != PM_OK) {
        fprintf(stderr, "FAIL: get_plugin_info: %s\n",
                parser_manager_status_string(st));
        parser_manager_destroy(mgr);
        return 1;
    }

    /* Identity contract: sql_plugin.c hard-codes these. */
    if (info.name == NULL || strcmp(info.name, "lime-sql-parser") != 0) {
        fprintf(stderr, "FAIL: name '%s' != 'lime-sql-parser'\n",
                info.name ? info.name : "(null)");
        parser_manager_destroy(mgr);
        return 1;
    }
    if (info.version.major != 1 || info.version.minor != 0 || info.version.patch != 0) {
        fprintf(stderr, "FAIL: version %u.%u.%u != 1.0.0\n",
                info.version.major, info.version.minor, info.version.patch);
        parser_manager_destroy(mgr);
        return 1;
    }
    if (!info.is_dynamic) {
        fprintf(stderr, "FAIL: dlopened plugin reports is_dynamic=false\n");
        parser_manager_destroy(mgr);
        return 1;
    }

    /* Capability contract: sql_plugin advertises SNAPSHOT | EXTENSIBLE | JIT. */
    uint32_t want = LIME_CAP_SNAPSHOT | LIME_CAP_EXTENSIBLE | LIME_CAP_JIT;
    if ((info.capabilities & want) != want) {
        fprintf(stderr, "FAIL: capabilities 0x%x missing bits from 0x%x\n",
                info.capabilities, want);
        parser_manager_destroy(mgr);
        return 1;
    }

    char vbuf[16];
    lime_plugin_version_string(info.version, vbuf, sizeof(vbuf));
    printf("OK: loaded '%s' v%s caps=0x%x handle=%u dynamic=yes\n",
           info.name, vbuf, info.capabilities, (unsigned)info.handle);

    /* Unload exercises destroy() + dlclose() in the manager. */
    st = parser_manager_unload(mgr, handle);
    if (st != PM_OK) {
        fprintf(stderr, "FAIL: parser_manager_unload: %s\n",
                parser_manager_status_string(st));
        parser_manager_destroy(mgr);
        return 1;
    }

    if (parser_manager_plugin_count(mgr) != 0) {
        fprintf(stderr, "FAIL: plugin_count != 0 after unload\n");
        parser_manager_destroy(mgr);
        return 1;
    }

    parser_manager_destroy(mgr);
    return 0;
}
