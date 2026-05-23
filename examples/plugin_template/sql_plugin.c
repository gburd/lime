/*
** Example Parser Plugin: SQL Parser
**
** Demonstrates how to package a Lime-generated parser as a runtime
** plugin that can be dynamically loaded by the ParserManager.
**
** This example implements a basic SQL parser plugin that:
**   1. Produces snapshots from grammar files via lime_snapshot_create()
**   2. Optionally validates snapshots
**   3. Advertises extensibility and JIT support
**
** Build as a shared library:
**
**   cc -shared -fPIC -o sql_plugin.so sql_plugin.c \
**      -I../../include -L../../builddir/src -llime_parser -lpthread
**
** Or link statically by including this file in your application and
** calling parser_manager_register() with &sql_plugin.
**
** See also: include/parser_manager.h for the full plugin API.
*/

#include "parser_manager.h"
#include "parser.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ================================================================== */
/*  Plugin-private state                                               */
/* ================================================================== */

/*
** Plugins can maintain private state. For a dynamic plugin this is
** file-scoped static data. For a static plugin the application can
** pass state via the user_data parameter to parser_manager_register().
**
** This example tracks how many snapshots have been created (for
** demonstration purposes).
*/
static struct {
    int  initialized;
    int  snapshot_count;
    void *user_data;      /* from init() */
} g_state;

/* ================================================================== */
/*  Identity callbacks                                                 */
/* ================================================================== */

static const char *sql_get_name(void) {
    return "lime-sql-parser";
}

static LimePluginVersion sql_get_version(void) {
    return (LimePluginVersion){ .major = 1, .minor = 0, .patch = 0 };
}

static uint16_t sql_get_abi_major(void) {
    return LIME_PLUGIN_ABI_VERSION_MAJOR;
}

static uint16_t sql_get_abi_minor(void) {
    return LIME_PLUGIN_ABI_VERSION_MINOR;
}

static uint32_t sql_get_capabilities(void) {
    return LIME_CAP_SNAPSHOT
         | LIME_CAP_EXTENSIBLE
         | LIME_CAP_JIT;
}

/* ================================================================== */
/*  Lifecycle callbacks                                                */
/* ================================================================== */

/*
** init() is called once after the plugin is loaded, before any other
** callbacks. Use it for one-time setup: opening resources, reading
** configuration, etc.
**
** user_data is whatever the application passed to parser_manager_load()
** or parser_manager_register(). May be NULL.
**
** Return true on success. Returning false causes the load to fail
** and the plugin is not registered.
*/
static bool sql_init(void *user_data) {
    if (g_state.initialized) {
        /* Already initialized -- this is fine for static plugins
        ** that might be registered multiple times across test runs. */
        return true;
    }

    g_state.initialized = 1;
    g_state.snapshot_count = 0;
    g_state.user_data = user_data;

    /*
    ** Real plugins might initialize the extension registry, set up
    ** logging, or pre-load a default grammar here.
    */

    return true;
}

/*
** destroy() is called when the plugin is unloaded. Release all
** resources. After this returns, no other callbacks will be called.
*/
static void sql_destroy(void) {
    g_state.initialized = 0;
    g_state.snapshot_count = 0;
    g_state.user_data = NULL;
}

/* ================================================================== */
/*  Snapshot production                                                */
/* ================================================================== */

/*
** create_snapshot() builds a parser snapshot from a grammar file.
**
** This is the core callback. The manager calls it when
** parser_manager_set_active() or parser_manager_hot_swap() is
** invoked with a grammar file path.
**
** On success: return a new ParserSnapshot with refcount == 1,
**             set *error to NULL.
** On failure: return NULL, set *error to a malloc'd message.
**
** Thread safety: this callback may be called from multiple threads
** concurrently. The underlying lime_snapshot_create() handles its
** own synchronization.
*/
static ParserSnapshot *sql_create_snapshot(const char *grammar_file,
                                           char **error) {
    if (grammar_file == NULL) {
        if (error) {
            const char msg[] = "sql_plugin: grammar_file is NULL";
            *error = malloc(sizeof(msg));
            if (*error) memcpy(*error, msg, sizeof(msg));
        }
        return NULL;
    }

    /*
    ** Delegate to the Lime library's snapshot creation.
    ** A real plugin might add pre-processing (macro expansion,
    ** include resolution) or post-processing (optimization passes)
    ** around this call.
    */
    ParserSnapshot *snap = lime_snapshot_create(grammar_file, error);
    if (snap != NULL) {
        g_state.snapshot_count++;
    }
    return snap;
}

/*
** validate_snapshot() checks that a snapshot is internally consistent.
** Optional but recommended for production plugins.
**
** The manager calls this if config.validate_on_load is true.
*/
static bool sql_validate_snapshot(const ParserSnapshot *snap,
                                  char **error) {
    if (snap == NULL) {
        if (error) {
            const char msg[] = "sql_plugin: snapshot is NULL";
            *error = malloc(sizeof(msg));
            if (*error) memcpy(*error, msg, sizeof(msg));
        }
        return false;
    }

    /*
    ** A real validator would check:
    **   - Action table array sizes are consistent
    **   - All state offsets are within bounds
    **   - Default actions reference valid states or reduce rules
    **   - Symbol and rule arrays are well-formed
    **
    ** For this example, we do a minimal check.
    */

    /* Snapshot must have at least one state */
    /* (This accesses the snapshot's public fields via the struct
    **  definition in src/snapshot.h -- a production plugin would
    **  use accessor functions if available.) */

    if (error) *error = NULL;
    return true;
}

/* ================================================================== */
/*  Plugin descriptor                                                  */
/* ================================================================== */

/*
** The plugin struct. This is the contract with the ParserManager.
**
** For static plugins, the application passes a pointer to this struct
** to parser_manager_register().
**
** For dynamic plugins, lime_plugin_entry() returns a pointer to this.
**
** All function pointers for unsupported optional callbacks are NULL.
*/
static const LimeParserPlugin sql_plugin = {
    /* Identity */
    .get_name         = sql_get_name,
    .get_version      = sql_get_version,
    .get_abi_major    = sql_get_abi_major,
    .get_abi_minor    = sql_get_abi_minor,
    .get_capabilities = sql_get_capabilities,

    /* Lifecycle */
    .init             = sql_init,
    .destroy          = sql_destroy,

    /* Snapshot production */
    .create_snapshot  = sql_create_snapshot,
    .validate_snapshot = sql_validate_snapshot,

    /* Serialization -- not implemented in this example */
    .serialize_snapshot   = NULL,
    .deserialize_snapshot = NULL,

    /* Reserved */
    ._reserved = {0},
};

/* ================================================================== */
/*  Dynamic loading entry point                                        */
/* ================================================================== */

/*
** When this file is compiled as a shared library, the ParserManager
** calls this function via dlsym() to obtain the plugin interface.
**
** The LIME_PLUGIN_EXPORT macro ensures the symbol has default
** visibility (not hidden by -fvisibility=hidden).
*/
LIME_PLUGIN_EXPORT
const LimeParserPlugin *lime_plugin_entry(void) {
    return &sql_plugin;
}

/* ================================================================== */
/*  Static registration helper                                         */
/* ================================================================== */

/*
** Convenience function for applications that link the plugin
** statically rather than loading it at runtime.
**
** Usage:
**   LimePluginHandle handle;
**   ParserManagerStatus st = sql_plugin_register(mgr, NULL, &handle);
*/
ParserManagerStatus sql_plugin_register(ParserManager *mgr,
                                        void *user_data,
                                        LimePluginHandle *handle_out) {
    return parser_manager_register(mgr, &sql_plugin, user_data, handle_out);
}
