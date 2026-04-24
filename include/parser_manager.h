/*
** Parser Manager - Runtime parser plugin management system.
**
** Provides a unified interface for dynamically loading, registering,
** and switching between parser implementations at runtime. Parsers
** are packaged as plugins (shared libraries or statically linked
** modules) that conform to the LimeParserPlugin interface.
**
** The ParserManager maintains a thread-safe registry of loaded parsers,
** supports hot-swapping of parser versions, and provides session-level
** isolation so that in-flight parse operations are never disrupted by
** plugin changes.
**
** Architecture:
**
**   +-------------------+
**   | ParserManager     |  (singleton or per-application)
**   |  - plugin registry|
**   |  - active parser  |
**   +--------+----------+
**            |
**   +--------v----------+     +------------------+
**   | LimeParserPlugin  |<--->| ParserSnapshot   |
**   |  (loaded .so or   |     | (action tables,  |
**   |   static module)  |     |  symbols, rules) |
**   +-------------------+     +------------------+
**
** Thread safety:
**   - The registry is protected by a pthread_rwlock_t. Multiple threads
**     can look up and use plugins concurrently; mutations (register,
**     unregister, set_active) acquire exclusive access.
**   - Parse sessions (via parse_begin/parse_token/parse_end from
**     parser.h) pin a snapshot, so they are not affected by concurrent
**     plugin changes.
**   - Plugin load/unload callbacks are called under the write lock.
**     Implementations must not call back into the ParserManager from
**     these callbacks (deadlock).
**
** Usage:
**
**   ParserManager *mgr = parser_manager_create(NULL);
**
**   // Load a plugin from a shared library
**   LimePluginHandle h;
**   parser_manager_load(mgr, "/usr/lib/lime/sql_parser.so", &h);
**
**   // Or register a statically linked plugin
**   parser_manager_register(mgr, &my_static_plugin, &h);
**
**   // Set it as the active parser
**   parser_manager_set_active(mgr, h);
**
**   // Get a snapshot for parsing
**   ParserSnapshot *snap = parser_manager_get_snapshot(mgr);
**   ParseContext *ctx = parse_begin(snap);
**   // ... parse tokens ...
**   parse_end(ctx);
**   lemon_snapshot_release(snap);
**
**   parser_manager_destroy(mgr);
*/
#ifndef PARSER_MANAGER_H
#define PARSER_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct ParserSnapshot ParserSnapshot;
typedef struct ParseContext ParseContext;

/* ================================================================== */
/*  Plugin handle                                                      */
/* ================================================================== */

/*
** Opaque handle identifying a loaded parser plugin within a manager.
** Zero is reserved and means "no plugin" / invalid.
*/
typedef uint32_t LimePluginHandle;

#define LIME_PLUGIN_HANDLE_INVALID ((LimePluginHandle)0)

/* ================================================================== */
/*  Plugin capability flags                                            */
/* ================================================================== */

/*
** Bit flags describing what a plugin supports. Returned by the
** plugin's get_capabilities callback. The manager uses these to
** validate compatibility and optimize dispatch.
*/
typedef enum LimePluginCaps {
    /* Plugin produces ParserSnapshot objects */
    LIME_CAP_SNAPSHOT        = (1u << 0),

    /* Plugin supports runtime grammar extension via the extension API */
    LIME_CAP_EXTENSIBLE      = (1u << 1),

    /* Plugin's snapshots are JIT-compilable */
    LIME_CAP_JIT             = (1u << 2),

    /* Plugin can produce incremental snapshots (modify in place) */
    LIME_CAP_INCREMENTAL     = (1u << 3),

    /* Plugin supports serializing/deserializing its state */
    LIME_CAP_SERIALIZABLE    = (1u << 4),
} LimePluginCaps;

/* ================================================================== */
/*  Plugin version                                                     */
/* ================================================================== */

/*
** Semantic version for a plugin. Used for compatibility checks and
** upgrade/downgrade decisions.
*/
typedef struct LimePluginVersion {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
} LimePluginVersion;

/*
** ABI version of the plugin interface. Both the manager and the
** plugin must agree on this value. Bump the major component when
** making breaking changes to LimeParserPlugin.
*/
#define LIME_PLUGIN_ABI_VERSION_MAJOR 1
#define LIME_PLUGIN_ABI_VERSION_MINOR 0

/* ================================================================== */
/*  Plugin interface                                                   */
/* ================================================================== */

/*
** The LimeParserPlugin struct is the contract between the manager and
** a parser implementation. Every plugin -- whether loaded from a
** shared library or linked statically -- must provide one of these.
**
** Required callbacks:
**   - get_name, get_version, get_capabilities
**   - create_snapshot
**   - destroy (called when the plugin is unloaded)
**
** Optional callbacks (set to NULL if not supported):
**   - init            -- one-time initialization after loading
**   - create_snapshot_from_file -- build snapshot from a grammar file
**   - validate_snapshot -- check snapshot integrity
**   - serialize_snapshot / deserialize_snapshot -- persistence
**
** Lifetime:
**   The LimeParserPlugin struct must remain valid from the time it is
**   registered until destroy() is called. For dynamically loaded
**   plugins, the shared library must stay loaded for that duration.
**
** Thread safety:
**   All callbacks except init() and destroy() may be called
**   concurrently from multiple threads. Implementations that need
**   internal synchronization must provide it themselves.
*/
typedef struct LimeParserPlugin {
    /* -------------------------------------------------------------- */
    /*  Identity                                                       */
    /* -------------------------------------------------------------- */

    /*
    ** Return the plugin's human-readable name. The returned pointer
    ** must remain valid for the lifetime of the plugin.
    */
    const char *(*get_name)(void);

    /*
    ** Return the plugin's semantic version.
    */
    LimePluginVersion (*get_version)(void);

    /*
    ** Return the ABI version this plugin was compiled against.
    ** The manager will reject plugins whose ABI major version does
    ** not match LIME_PLUGIN_ABI_VERSION_MAJOR.
    */
    uint16_t (*get_abi_major)(void);
    uint16_t (*get_abi_minor)(void);

    /*
    ** Return a bitmask of LimePluginCaps describing this plugin's
    ** capabilities.
    */
    uint32_t (*get_capabilities)(void);

    /* -------------------------------------------------------------- */
    /*  Lifecycle                                                       */
    /* -------------------------------------------------------------- */

    /*
    ** One-time initialization. Called after the plugin is loaded but
    ** before any other callbacks. Receives the user_data pointer
    ** that was passed to parser_manager_load/register.
    **
    ** Returns true on success. On failure, the plugin is not
    ** registered and the load/register call fails.
    **
    ** Optional: set to NULL if no initialization is needed.
    */
    bool (*init)(void *user_data);

    /*
    ** Teardown. Called when the plugin is unloaded from the manager.
    ** Must release all resources owned by the plugin. After this
    ** call the plugin struct and any pointers it returned are invalid.
    **
    ** The manager guarantees that no other callbacks are in flight
    ** when destroy() is called.
    */
    void (*destroy)(void);

    /* -------------------------------------------------------------- */
    /*  Snapshot production                                             */
    /* -------------------------------------------------------------- */

    /*
    ** Create a snapshot from a grammar file path.
    **
    ** On success, returns a new ParserSnapshot with refcount == 1
    ** and sets *error to NULL. On failure, returns NULL and sets
    ** *error to a malloc'd message the caller must free.
    **
    ** Required if LIME_CAP_SNAPSHOT is set in capabilities.
    */
    ParserSnapshot *(*create_snapshot)(const char *grammar_file,
                                       char **error);

    /*
    ** Validate that a snapshot is internally consistent.
    **
    ** Returns true if the snapshot's action tables, symbols, and
    ** rules are well-formed. On failure, sets *error to a malloc'd
    ** diagnostic message.
    **
    ** Optional: set to NULL to skip validation.
    */
    bool (*validate_snapshot)(const ParserSnapshot *snap, char **error);

    /* -------------------------------------------------------------- */
    /*  Serialization (optional, requires LIME_CAP_SERIALIZABLE)       */
    /* -------------------------------------------------------------- */

    /*
    ** Serialize a snapshot to a byte buffer.
    **
    ** On success, *buf_out points to a malloc'd buffer of *len_out
    ** bytes that the caller must free. Returns true on success.
    */
    bool (*serialize_snapshot)(const ParserSnapshot *snap,
                               uint8_t **buf_out,
                               size_t *len_out);

    /*
    ** Deserialize a snapshot from a byte buffer previously produced
    ** by serialize_snapshot().
    **
    ** On success, returns a new snapshot with refcount == 1.
    ** On failure, returns NULL and sets *error.
    */
    ParserSnapshot *(*deserialize_snapshot)(const uint8_t *buf,
                                            size_t len,
                                            char **error);

    /* -------------------------------------------------------------- */
    /*  Reserved for future expansion                                  */
    /* -------------------------------------------------------------- */

    void *_reserved[8];

} LimeParserPlugin;

/* ================================================================== */
/*  Dynamic plugin entry point                                         */
/* ================================================================== */

/*
** Every shared library plugin must export a function with this
** signature and the name "lime_plugin_entry". The manager calls it
** after dlopen() to obtain the plugin's interface struct.
**
** The returned pointer must remain valid until the plugin's destroy()
** callback is called.
**
** Example:
**
**   LIME_PLUGIN_EXPORT const LimeParserPlugin *lime_plugin_entry(void) {
**       return &my_plugin;
**   }
*/
typedef const LimeParserPlugin *(*LimePluginEntryFn)(void);

#define LIME_PLUGIN_ENTRY_SYMBOL "lime_plugin_entry"

/*
** Macro for declaring the plugin entry point with proper visibility.
** Use in plugin source files:
**
**   LIME_PLUGIN_EXPORT const LimeParserPlugin *lime_plugin_entry(void) {
**       return &my_plugin;
**   }
*/
#ifdef _WIN32
  #define LIME_PLUGIN_EXPORT __declspec(dllexport)
#else
  #define LIME_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

/* ================================================================== */
/*  Manager configuration                                              */
/* ================================================================== */

/*
** Configuration for creating a ParserManager.
** Pass NULL to parser_manager_create() for defaults.
*/
typedef struct ParserManagerConfig {
    /*
    ** Maximum number of plugins that can be registered simultaneously.
    ** 0 means unlimited (dynamic growth). Default: 0.
    */
    uint32_t max_plugins;

    /*
    ** If true, the manager will validate snapshots produced by plugins
    ** (by calling validate_snapshot if available) before making them
    ** available. Default: false.
    */
    bool validate_on_load;

    /*
    ** If true, the manager will attempt to JIT-compile snapshots from
    ** plugins that advertise LIME_CAP_JIT. Default: false.
    */
    bool auto_jit;

    /*
    ** Search paths for plugin shared libraries. NULL-terminated array
    ** of directory paths. The manager will search these directories
    ** when parser_manager_load() is called with a relative path.
    ** NULL means search only the exact path given.
    */
    const char **plugin_search_paths;

} ParserManagerConfig;

/* ================================================================== */
/*  Manager status codes                                               */
/* ================================================================== */

typedef enum ParserManagerStatus {
    PM_OK = 0,                    /* Operation succeeded */
    PM_ERR_INVALID_ARG,           /* NULL or invalid argument */
    PM_ERR_ALLOC,                 /* Memory allocation failed */
    PM_ERR_PLUGIN_NOT_FOUND,      /* Handle does not refer to a loaded plugin */
    PM_ERR_DUPLICATE_NAME,        /* A plugin with this name is already loaded */
    PM_ERR_ABI_MISMATCH,          /* Plugin ABI version incompatible */
    PM_ERR_INIT_FAILED,           /* Plugin init() callback returned false */
    PM_ERR_DLOPEN_FAILED,         /* Could not open shared library */
    PM_ERR_NO_ENTRY_POINT,        /* Shared library missing lime_plugin_entry */
    PM_ERR_SNAPSHOT_FAILED,       /* Plugin failed to create a snapshot */
    PM_ERR_VALIDATION_FAILED,     /* Snapshot validation failed */
    PM_ERR_PLUGIN_IN_USE,         /* Cannot unload: active sessions reference it */
    PM_ERR_NO_ACTIVE_PLUGIN,      /* No active plugin set */
    PM_ERR_CAPABILITY_MISSING,    /* Plugin lacks a required capability */
} ParserManagerStatus;

/*
** Return a human-readable string for a status code.
** The returned pointer is to static storage.
*/
const char *parser_manager_status_string(ParserManagerStatus status);

/* ================================================================== */
/*  Opaque manager handle                                              */
/* ================================================================== */

typedef struct ParserManager ParserManager;

/* ================================================================== */
/*  Manager lifecycle                                                  */
/* ================================================================== */

/*
** Create a new parser manager.
**
** config may be NULL for default settings. The config struct is
** copied internally; the caller's copy is not retained.
**
** Returns NULL on allocation failure.
*/
ParserManager *parser_manager_create(const ParserManagerConfig *config);

/*
** Destroy a parser manager and release all resources.
**
** All loaded plugins receive their destroy() callback. Active parse
** sessions that have pinned snapshots continue to function -- the
** snapshots are reference-counted and will be freed when the last
** session releases them.
**
** Passing NULL is safe.
*/
void parser_manager_destroy(ParserManager *mgr);

/* ================================================================== */
/*  Plugin loading                                                     */
/* ================================================================== */

/*
** Load a parser plugin from a shared library.
**
** path: filesystem path to the shared library (.so / .dylib / .dll).
**       If relative, the manager searches plugin_search_paths.
** user_data: opaque pointer passed to the plugin's init() callback.
**            May be NULL.
** handle_out: on success, receives the handle for the loaded plugin.
**
** The manager:
**   1. dlopen()s the library
**   2. Looks up the "lime_plugin_entry" symbol
**   3. Calls it to get the LimeParserPlugin struct
**   4. Validates ABI version compatibility
**   5. Calls init(user_data) if provided
**   6. Registers the plugin in the registry
**
** Returns PM_OK on success.
*/
ParserManagerStatus parser_manager_load(ParserManager *mgr,
                                        const char *path,
                                        void *user_data,
                                        LimePluginHandle *handle_out);

/*
** Register a statically linked parser plugin.
**
** plugin: pointer to a LimeParserPlugin struct. The struct must remain
**         valid until the plugin is unloaded.
** user_data: opaque pointer passed to the plugin's init() callback.
** handle_out: on success, receives the handle for the registered plugin.
**
** Returns PM_OK on success.
*/
ParserManagerStatus parser_manager_register(ParserManager *mgr,
                                            const LimeParserPlugin *plugin,
                                            void *user_data,
                                            LimePluginHandle *handle_out);

/*
** Unload a previously loaded or registered plugin.
**
** Calls the plugin's destroy() callback and removes it from the
** registry. If the plugin was loaded from a shared library, the
** library is dlclose()'d after destroy() returns.
**
** If the plugin is currently active, it is deactivated first.
** In-flight parse sessions that pinned a snapshot from this plugin
** are not affected (snapshots are refcounted).
**
** Returns PM_OK on success.
*/
ParserManagerStatus parser_manager_unload(ParserManager *mgr,
                                          LimePluginHandle handle);

/* ================================================================== */
/*  Active parser management                                           */
/* ================================================================== */

/*
** Set the active parser plugin.
**
** The active plugin is the one whose snapshot is returned by
** parser_manager_get_snapshot(). Only one plugin can be active
** at a time.
**
** If grammar_file is non-NULL, the manager calls the plugin's
** create_snapshot() to produce a snapshot immediately. If NULL,
** the plugin must have been previously used to create a snapshot
** (or the caller must provide one via parser_manager_set_snapshot).
**
** Returns PM_OK on success.
*/
ParserManagerStatus parser_manager_set_active(ParserManager *mgr,
                                              LimePluginHandle handle,
                                              const char *grammar_file);

/*
** Get the handle of the currently active plugin.
** Returns LIME_PLUGIN_HANDLE_INVALID if no plugin is active.
*/
LimePluginHandle parser_manager_get_active(const ParserManager *mgr);

/*
** Provide or replace the active snapshot directly.
**
** The manager acquires a reference to snap. The previous active
** snapshot (if any) is released.
**
** This is useful for loading pre-built or deserialized snapshots
** without going through create_snapshot().
**
** Returns PM_OK on success.
*/
ParserManagerStatus parser_manager_set_snapshot(ParserManager *mgr,
                                                ParserSnapshot *snap);

/*
** Get the current active snapshot.
**
** Returns a snapshot with an additional reference (caller must call
** lemon_snapshot_release when done). Returns NULL if no active
** snapshot is available.
**
** Thread-safe: multiple threads can call this concurrently.
*/
ParserSnapshot *parser_manager_get_snapshot(ParserManager *mgr);

/* ================================================================== */
/*  Plugin enumeration and introspection                               */
/* ================================================================== */

/*
** Plugin information returned by enumeration functions.
** All string pointers point into plugin-owned memory and are valid
** only while the plugin remains loaded.
*/
typedef struct LimePluginInfo {
    LimePluginHandle handle;
    const char *name;
    LimePluginVersion version;
    uint32_t capabilities;
    bool is_active;
    bool is_dynamic;              /* true if loaded from shared library */
} LimePluginInfo;

/*
** Get information about a specific plugin.
** Returns PM_OK and fills *info on success.
*/
ParserManagerStatus parser_manager_get_plugin_info(const ParserManager *mgr,
                                                   LimePluginHandle handle,
                                                   LimePluginInfo *info);

/*
** Enumerate all loaded plugins.
**
** Fills the infos array with up to max_count entries. Sets
** *actual_count to the number written. If actual_count > max_count,
** the array was too small and some entries were omitted.
**
** Returns PM_OK on success.
*/
ParserManagerStatus parser_manager_list_plugins(const ParserManager *mgr,
                                                LimePluginInfo *infos,
                                                uint32_t max_count,
                                                uint32_t *actual_count);

/*
** Look up a plugin by name.
** Returns the handle, or LIME_PLUGIN_HANDLE_INVALID if not found.
*/
LimePluginHandle parser_manager_find_by_name(const ParserManager *mgr,
                                             const char *name);

/*
** Get the number of loaded plugins.
*/
uint32_t parser_manager_plugin_count(const ParserManager *mgr);

/* ================================================================== */
/*  Hot-swap support                                                   */
/* ================================================================== */

/*
** Atomically replace the active plugin and snapshot.
**
** This is a convenience function equivalent to:
**   parser_manager_set_active(mgr, new_handle, grammar_file)
** but guarantees that the transition is atomic from the perspective
** of parser_manager_get_snapshot() callers. There is no window where
** get_snapshot() returns NULL or an intermediate state.
**
** The old snapshot is released once all references to it are dropped
** (existing parse sessions keep their pinned references).
**
** Returns PM_OK on success.
*/
ParserManagerStatus parser_manager_hot_swap(ParserManager *mgr,
                                            LimePluginHandle new_handle,
                                            const char *grammar_file);

/* ================================================================== */
/*  Version compatibility utilities                                    */
/* ================================================================== */

/*
** Compare two plugin versions.
** Returns <0 if a < b, 0 if a == b, >0 if a > b.
*/
int lime_plugin_version_compare(LimePluginVersion a, LimePluginVersion b);

/*
** Check if a plugin version satisfies a minimum requirement.
** Returns true if actual >= required.
*/
bool lime_plugin_version_satisfies(LimePluginVersion actual,
                                   LimePluginVersion required);

/*
** Format a version as a string into buf (must be at least 16 bytes).
** Returns buf for convenience.
*/
char *lime_plugin_version_string(LimePluginVersion v, char *buf, size_t buflen);

/* ================================================================== */
/*  Plugin validation and introspection utilities                      */
/* ================================================================== */

/*
** Validate that a plugin struct has all required callbacks and a
** compatible ABI version. Returns PM_OK on success, or an error
** code describing the first problem found.
*/
ParserManagerStatus lime_plugin_validate(const LimeParserPlugin *plugin);

/*
** Check if a plugin has a specific capability.
*/
bool lime_plugin_has_capability(const LimeParserPlugin *plugin,
                                LimePluginCaps cap);

/*
** Check if a plugin has all of the specified capabilities (bitwise AND).
*/
bool lime_plugin_has_all_capabilities(const LimeParserPlugin *plugin,
                                      uint32_t required_caps);

/*
** Return a human-readable name for a single capability flag.
** Returns "unknown" for unrecognized flags.
*/
const char *lime_plugin_capability_name(LimePluginCaps cap);

/*
** Format a capability bitmask as a comma-separated string of names.
** Writes into buf (which must be at least buflen bytes).
** Returns buf for convenience.
*/
char *lime_plugin_capabilities_string(uint32_t caps, char *buf, size_t buflen);

/*
** Print a diagnostic summary of a plugin to the given FILE stream.
** Includes name, version, ABI, capabilities, and callback presence.
*/
void lime_plugin_dump(const LimeParserPlugin *plugin, FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* PARSER_MANAGER_H */
