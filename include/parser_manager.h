/**
 * @file parser_manager.h
 * @brief Parser Manager -- runtime parser plugin management system.
 *
 * Provides a unified interface for dynamically loading, registering,
 * and switching between parser implementations at runtime.  Parsers
 * are packaged as plugins (shared libraries or statically linked
 * modules) that conform to the LimeParserPlugin interface.
 *
 * The ParserManager maintains a thread-safe registry of loaded parsers,
 * supports hot-swapping of parser versions, and provides session-level
 * isolation so that in-flight parse operations are never disrupted by
 * plugin changes.
 *
 * @par Architecture
 * @code
 *   +-------------------+
 *   | ParserManager     |  (singleton or per-application)
 *   |  - plugin registry|
 *   |  - active parser  |
 *   +--------+----------+
 *            |
 *   +--------v----------+     +------------------+
 *   | LimeParserPlugin  |<--->| ParserSnapshot   |
 *   |  (loaded .so or   |     | (action tables,  |
 *   |   static module)  |     |  symbols, rules) |
 *   +-------------------+     +------------------+
 * @endcode
 *
 * @par Thread Safety
 * The registry is protected by a pthread_rwlock_t.  Multiple threads
 * can look up and use plugins concurrently; mutations (register,
 * unregister, set_active) acquire exclusive access.
 *
 * Parse sessions (via parse_begin/parse_token/parse_end from parser.h)
 * pin a snapshot, so they are not affected by concurrent plugin changes.
 *
 * Plugin load/unload callbacks are called under the write lock.
 * Implementations must not call back into the ParserManager from
 * these callbacks (deadlock).
 *
 * @par Usage Example
 * @code
 *   ParserManager *mgr = parser_manager_create(NULL);
 *
 *   // Load a plugin from a shared library
 *   LimePluginHandle h;
 *   parser_manager_load(mgr, "/usr/lib/lime/sql_parser.so", NULL, &h);
 *
 *   // Or register a statically linked plugin
 *   parser_manager_register(mgr, &my_static_plugin, NULL, &h);
 *
 *   // Set it as the active parser
 *   parser_manager_set_active(mgr, h, "grammar.y");
 *
 *   // Get a snapshot for parsing
 *   ParserSnapshot *snap = parser_manager_get_snapshot(mgr);
 *   ParseContext *ctx = parse_begin(snap);
 *   // ... parse tokens ...
 *   parse_end(ctx);
 *   lime_snapshot_release(snap);
 *
 *   parser_manager_destroy(mgr);
 * @endcode
 *
 * @see parser.h for the core parse session API.
 * @see snapshot.h for ParserSnapshot internals.
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
/** @defgroup plugin_handle Plugin Handle
 *  @brief Opaque handle for identifying loaded parser plugins.
 *  @{
 */
/* ================================================================== */

/**
 * @brief Opaque handle identifying a loaded parser plugin within a manager.
 *
 * Zero is reserved and means "no plugin" / invalid.
 */
typedef uint32_t LimePluginHandle;

/** @brief Sentinel value representing an invalid or unset plugin handle. */
#define LIME_PLUGIN_HANDLE_INVALID ((LimePluginHandle)0)

/** @} */ /* end plugin_handle */

/* ================================================================== */
/** @defgroup plugin_caps Plugin Capability Flags
 *  @brief Bit flags describing what a plugin supports.
 *  @{
 */
/* ================================================================== */

/**
 * @brief Bit flags describing what a plugin supports.
 *
 * Returned by the plugin's get_capabilities callback.  The manager uses
 * these to validate compatibility and optimize dispatch.
 */
typedef enum LimePluginCaps {
    LIME_CAP_SNAPSHOT = (1u << 0),     /**< Plugin produces ParserSnapshot objects */
    LIME_CAP_EXTENSIBLE = (1u << 1),   /**< Plugin supports runtime grammar extension */
    LIME_CAP_JIT = (1u << 2),          /**< Plugin's snapshots are JIT-compilable */
    LIME_CAP_INCREMENTAL = (1u << 3),  /**< Plugin can produce incremental snapshots */
    LIME_CAP_SERIALIZABLE = (1u << 4), /**< Plugin supports serializing/deserializing state */
} LimePluginCaps;

/** @} */ /* end plugin_caps */

/* ================================================================== */
/** @defgroup plugin_version Plugin Version
 *  @brief Semantic versioning for plugins and ABI compatibility.
 *  @{
 */
/* ================================================================== */

/**
 * @brief Semantic version for a plugin.
 *
 * Used for compatibility checks and upgrade/downgrade decisions.
 */
typedef struct LimePluginVersion {
    uint16_t major; /**< Major version (breaking changes) */
    uint16_t minor; /**< Minor version (backwards-compatible additions) */
    uint16_t patch; /**< Patch version (bug fixes) */
} LimePluginVersion;

/**
 * @brief ABI major version of the plugin interface.
 *
 * Both the manager and the plugin must agree on this value.
 * Bump when making breaking changes to LimeParserPlugin.
 */
#define LIME_PLUGIN_ABI_VERSION_MAJOR 1

/** @brief ABI minor version of the plugin interface. */
#define LIME_PLUGIN_ABI_VERSION_MINOR 0

/** @} */ /* end plugin_version */

/* ================================================================== */
/** @defgroup plugin_interface Plugin Interface
 *  @brief The contract between the manager and a parser implementation.
 *  @{
 */
/* ================================================================== */

/**
 * @brief The plugin interface struct -- the contract between the manager
 *        and a parser implementation.
 *
 * Every plugin, whether loaded from a shared library or linked statically,
 * must provide one of these.
 *
 * @par Required callbacks
 * - get_name(), get_version(), get_capabilities()
 * - create_snapshot()
 * - destroy()
 *
 * @par Optional callbacks (set to NULL if not supported)
 * - init() -- one-time initialization after loading
 * - validate_snapshot() -- check snapshot integrity
 * - serialize_snapshot() / deserialize_snapshot() -- persistence
 *
 * @par Lifetime
 * The LimeParserPlugin struct must remain valid from the time it is
 * registered until destroy() is called.  For dynamically loaded
 * plugins, the shared library must stay loaded for that duration.
 *
 * @par Thread Safety
 * All callbacks except init() and destroy() may be called concurrently
 * from multiple threads.  Implementations that need internal
 * synchronization must provide it themselves.
 */
typedef struct LimeParserPlugin {
    /* -------------------------------------------------------------- */
    /*  Identity                                                       */
    /* -------------------------------------------------------------- */

    /**
     * @brief Return the plugin's human-readable name.
     * @return NUL-terminated name string.  Must remain valid for the
     *         lifetime of the plugin.
     */
    const char *(*get_name)(void);

    /**
     * @brief Return the plugin's semantic version.
     * @return A LimePluginVersion struct.
     */
    LimePluginVersion (*get_version)(void);

    /**
     * @brief Return the ABI major version this plugin was compiled against.
     *
     * The manager rejects plugins whose ABI major version does not match
     * LIME_PLUGIN_ABI_VERSION_MAJOR.
     * @return ABI major version number.
     */
    uint16_t (*get_abi_major)(void);

    /**
     * @brief Return the ABI minor version this plugin was compiled against.
     * @return ABI minor version number.
     */
    uint16_t (*get_abi_minor)(void);

    /**
     * @brief Return a bitmask of LimePluginCaps describing this plugin.
     * @return Bitwise OR of zero or more LimePluginCaps values.
     */
    uint32_t (*get_capabilities)(void);

    /* -------------------------------------------------------------- */
    /*  Lifecycle                                                       */
    /* -------------------------------------------------------------- */

    /**
     * @brief One-time initialization.
     *
     * Called after the plugin is loaded but before any other callbacks.
     * Receives the user_data pointer passed to parser_manager_load() or
     * parser_manager_register().
     *
     * @param user_data Opaque pointer from the load/register call.
     * @retval true  Initialization succeeded.
     * @retval false Initialization failed; plugin is not registered.
     *
     * @note Optional: set to NULL if no initialization is needed.
     */
    bool (*init)(void *user_data);

    /**
     * @brief Teardown callback.
     *
     * Called when the plugin is unloaded from the manager.  Must release
     * all resources owned by the plugin.  After this call the plugin struct
     * and any pointers it returned are invalid.
     *
     * @note The manager guarantees that no other callbacks are in flight
     *       when destroy() is called.
     */
    void (*destroy)(void);

    /* -------------------------------------------------------------- */
    /*  Snapshot production                                             */
    /* -------------------------------------------------------------- */

    /**
     * @brief Create a snapshot from a grammar file path.
     *
     * @param grammar_file Path to the grammar file.
     * @param[out] error   On failure, set to a malloc'd error message
     *                     the caller must free.  Set to NULL on success.
     * @return New ParserSnapshot with refcount == 1 on success, or NULL
     *         on failure.
     *
     * @note Required if LIME_CAP_SNAPSHOT is set in capabilities.
     */
    ParserSnapshot *(*create_snapshot)(const char *grammar_file, char **error);

    /**
     * @brief Validate that a snapshot is internally consistent.
     *
     * @param snap   Snapshot to validate.
     * @param[out] error On failure, set to a malloc'd diagnostic message.
     * @retval true  Snapshot is well-formed.
     * @retval false Snapshot is invalid.
     *
     * @note Optional: set to NULL to skip validation.
     */
    bool (*validate_snapshot)(const ParserSnapshot *snap, char **error);

    /* -------------------------------------------------------------- */
    /*  Serialization (optional, requires LIME_CAP_SERIALIZABLE)       */
    /* -------------------------------------------------------------- */

    /**
     * @brief Serialize a snapshot to a byte buffer.
     *
     * @param snap        Snapshot to serialize.
     * @param[out] buf_out On success, points to a malloc'd buffer the
     *                     caller must free.
     * @param[out] len_out On success, receives the buffer length in bytes.
     * @retval true  Serialization succeeded.
     * @retval false Serialization failed.
     */
    bool (*serialize_snapshot)(const ParserSnapshot *snap, uint8_t **buf_out, size_t *len_out);

    /**
     * @brief Deserialize a snapshot from a byte buffer.
     *
     * @param buf   Buffer previously produced by serialize_snapshot().
     * @param len   Length of the buffer in bytes.
     * @param[out] error On failure, set to a malloc'd error message.
     * @return New ParserSnapshot with refcount == 1 on success, or NULL
     *         on failure.
     */
    ParserSnapshot *(*deserialize_snapshot)(const uint8_t *buf, size_t len, char **error);

    /* -------------------------------------------------------------- */
    /*  Reserved for future expansion                                  */
    /* -------------------------------------------------------------- */

    void *_reserved[8]; /**< Reserved for ABI-compatible future additions. */

} LimeParserPlugin;

/** @} */ /* end plugin_interface */

/* ================================================================== */
/** @defgroup plugin_entry Dynamic Plugin Entry Point
 *  @brief Entry point convention for shared-library plugins.
 *  @{
 */
/* ================================================================== */

/**
 * @brief Function signature for the dynamic plugin entry point.
 *
 * Every shared library plugin must export a function named
 * "lime_plugin_entry" with this signature.  The manager calls it
 * after dlopen() to obtain the plugin's interface struct.
 *
 * @return Pointer to the plugin's LimeParserPlugin struct.  Must remain
 *         valid until the plugin's destroy() callback is called.
 *
 * @par Example
 * @code
 *   LIME_PLUGIN_EXPORT const LimeParserPlugin *lime_plugin_entry(void) {
 *       return &my_plugin;
 *   }
 * @endcode
 */
typedef const LimeParserPlugin *(*LimePluginEntryFn)(void);

/** @brief Symbol name looked up by the manager after dlopen(). */
#define LIME_PLUGIN_ENTRY_SYMBOL "lime_plugin_entry"

/**
 * @brief Macro for declaring the plugin entry point with proper visibility.
 *
 * @par Usage
 * @code
 *   LIME_PLUGIN_EXPORT const LimeParserPlugin *lime_plugin_entry(void) {
 *       return &my_plugin;
 *   }
 * @endcode
 */
#ifdef _WIN32
#define LIME_PLUGIN_EXPORT __declspec(dllexport)
#else
#define LIME_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

/** @} */ /* end plugin_entry */

/* ================================================================== */
/** @defgroup manager_config Manager Configuration
 *  @brief Configuration struct for creating a ParserManager.
 *  @{
 */
/* ================================================================== */

/**
 * @brief Configuration for creating a ParserManager.
 *
 * Pass NULL to parser_manager_create() for defaults.
 */
typedef struct ParserManagerConfig {
    /**
     * @brief Maximum number of plugins that can be registered simultaneously.
     *
     * 0 means unlimited (dynamic growth).  Default: 0.
     */
    uint32_t max_plugins;

    /**
     * @brief If true, validate snapshots produced by plugins before use.
     *
     * The manager calls validate_snapshot() (if available) before making
     * snapshots available.  Default: false.
     */
    bool validate_on_load;

    /**
     * @brief If true, attempt to JIT-compile snapshots from capable plugins.
     *
     * Applies to plugins that advertise LIME_CAP_JIT.  Default: false.
     */
    bool auto_jit;

    /**
     * @brief Search paths for plugin shared libraries.
     *
     * NULL-terminated array of directory paths.  The manager searches
     * these directories when parser_manager_load() is called with a
     * relative path.  NULL means search only the exact path given.
     */
    const char **plugin_search_paths;

} ParserManagerConfig;

/** @} */ /* end manager_config */

/* ================================================================== */
/** @defgroup manager_status Manager Status Codes
 *  @brief Return codes for ParserManager operations.
 *  @{
 */
/* ================================================================== */

/**
 * @brief Status codes returned by ParserManager operations.
 */
typedef enum ParserManagerStatus {
    PM_OK = 0,                 /**< Operation succeeded */
    PM_ERR_INVALID_ARG,        /**< NULL or invalid argument */
    PM_ERR_ALLOC,              /**< Memory allocation failed */
    PM_ERR_PLUGIN_NOT_FOUND,   /**< Handle does not refer to a loaded plugin */
    PM_ERR_DUPLICATE_NAME,     /**< A plugin with this name is already loaded */
    PM_ERR_ABI_MISMATCH,       /**< Plugin ABI version incompatible */
    PM_ERR_INIT_FAILED,        /**< Plugin init() callback returned false */
    PM_ERR_DLOPEN_FAILED,      /**< Could not open shared library */
    PM_ERR_NO_ENTRY_POINT,     /**< Shared library missing lime_plugin_entry */
    PM_ERR_SNAPSHOT_FAILED,    /**< Plugin failed to create a snapshot */
    PM_ERR_VALIDATION_FAILED,  /**< Snapshot validation failed */
    PM_ERR_PLUGIN_IN_USE,      /**< Cannot unload: active sessions reference it */
    PM_ERR_NO_ACTIVE_PLUGIN,   /**< No active plugin set */
    PM_ERR_CAPABILITY_MISSING, /**< Plugin lacks a required capability */
} ParserManagerStatus;

/**
 * @brief Return a human-readable string for a status code.
 *
 * @param status The status code to describe.
 * @return Pointer to a static string describing @p status.
 */
const char *parser_manager_status_string(ParserManagerStatus status);

/** @} */ /* end manager_status */

/* ================================================================== */
/** @defgroup manager_handle Manager Handle
 *  @brief Opaque ParserManager type.
 *  @{
 */
/* ================================================================== */

/** @brief Opaque parser manager handle. */
typedef struct ParserManager ParserManager;

/** @} */ /* end manager_handle */

/* ================================================================== */
/** @defgroup manager_lifecycle Manager Lifecycle
 *  @brief Create and destroy ParserManager instances.
 *  @{
 */
/* ================================================================== */

/**
 * @brief Create a new parser manager.
 *
 * @param config Configuration parameters.  May be NULL for defaults.
 *               The config struct is copied internally; the caller's
 *               copy is not retained.
 * @return New ParserManager, or NULL on allocation failure.
 *
 * @see parser_manager_destroy()
 */
ParserManager *parser_manager_create(const ParserManagerConfig *config);

/**
 * @brief Destroy a parser manager and release all resources.
 *
 * All loaded plugins receive their destroy() callback.  Active parse
 * sessions that have pinned snapshots continue to function -- the
 * snapshots are reference-counted and will be freed when the last
 * session releases them.
 *
 * @param mgr Manager to destroy.  Passing NULL is safe.
 *
 * @see parser_manager_create()
 */
void parser_manager_destroy(ParserManager *mgr);

/** @} */ /* end manager_lifecycle */

/* ================================================================== */
/** @defgroup plugin_loading Plugin Loading
 *  @brief Load, register, and unload parser plugins.
 *  @{
 */
/* ================================================================== */

/**
 * @brief Load a parser plugin from a shared library.
 *
 * The manager:
 *   1. dlopen()s the library
 *   2. Looks up the "lime_plugin_entry" symbol
 *   3. Calls it to get the LimeParserPlugin struct
 *   4. Validates ABI version compatibility
 *   5. Calls init(user_data) if provided
 *   6. Registers the plugin in the registry
 *
 * @param mgr        The parser manager.
 * @param path       Filesystem path to the shared library
 *                   (.so / .dylib / .dll).  If relative, the manager
 *                   searches plugin_search_paths.
 * @param user_data  Opaque pointer passed to the plugin's init()
 *                   callback.  May be NULL.
 * @param[out] handle_out On success, receives the handle for the
 *                        loaded plugin.
 * @return PM_OK on success, or an error code.
 *
 * @see parser_manager_register() for statically linked plugins.
 * @see parser_manager_unload()
 */
ParserManagerStatus parser_manager_load(ParserManager *mgr, const char *path, void *user_data,
                                        LimePluginHandle *handle_out);

/**
 * @brief Register a statically linked parser plugin.
 *
 * @param mgr        The parser manager.
 * @param plugin     Pointer to a LimeParserPlugin struct.  Must remain
 *                   valid until the plugin is unloaded.
 * @param user_data  Opaque pointer passed to the plugin's init() callback.
 * @param[out] handle_out On success, receives the handle for the
 *                        registered plugin.
 * @return PM_OK on success, or an error code.
 *
 * @see parser_manager_load() for shared library plugins.
 */
ParserManagerStatus parser_manager_register(ParserManager *mgr, const LimeParserPlugin *plugin,
                                            void *user_data, LimePluginHandle *handle_out);

/**
 * @brief Unload a previously loaded or registered plugin.
 *
 * Calls the plugin's destroy() callback and removes it from the
 * registry.  If the plugin was loaded from a shared library, the
 * library is dlclose()'d after destroy() returns.
 *
 * If the plugin is currently active, it is deactivated first.
 * In-flight parse sessions that pinned a snapshot from this plugin
 * are not affected (snapshots are refcounted).
 *
 * @param mgr    The parser manager.
 * @param handle Handle of the plugin to unload.
 * @return PM_OK on success, or an error code.
 */
ParserManagerStatus parser_manager_unload(ParserManager *mgr, LimePluginHandle handle);

/** @} */ /* end plugin_loading */

/* ================================================================== */
/** @defgroup active_parser Active Parser Management
 *  @brief Select and query the active parser plugin.
 *  @{
 */
/* ================================================================== */

/**
 * @brief Set the active parser plugin.
 *
 * The active plugin is the one whose snapshot is returned by
 * parser_manager_get_snapshot().  Only one plugin can be active
 * at a time.
 *
 * @param mgr          The parser manager.
 * @param handle       Handle of the plugin to activate.
 * @param grammar_file If non-NULL, the manager calls create_snapshot()
 *                     immediately.  If NULL, a snapshot must have been
 *                     previously set via parser_manager_set_snapshot().
 * @return PM_OK on success, or an error code.
 *
 * @see parser_manager_get_active()
 * @see parser_manager_hot_swap()
 */
ParserManagerStatus parser_manager_set_active(ParserManager *mgr, LimePluginHandle handle,
                                              const char *grammar_file);

/**
 * @brief Get the handle of the currently active plugin.
 *
 * @param mgr The parser manager.
 * @return Handle of the active plugin, or LIME_PLUGIN_HANDLE_INVALID
 *         if no plugin is active.
 */
LimePluginHandle parser_manager_get_active(const ParserManager *mgr);

/**
 * @brief Provide or replace the active snapshot directly.
 *
 * The manager acquires a reference to @p snap.  The previous active
 * snapshot (if any) is released.
 *
 * This is useful for loading pre-built or deserialized snapshots
 * without going through create_snapshot().
 *
 * @param mgr  The parser manager.
 * @param snap Snapshot to set as active.
 * @return PM_OK on success, or an error code.
 */
ParserManagerStatus parser_manager_set_snapshot(ParserManager *mgr, ParserSnapshot *snap);

/**
 * @brief Get the current active snapshot.
 *
 * Returns a snapshot with an additional reference -- the caller must call
 * lime_snapshot_release() when done.
 *
 * @param mgr The parser manager.
 * @return Active snapshot, or NULL if no active snapshot is available.
 *
 * @thread_safe Multiple threads can call this concurrently.
 */
ParserSnapshot *parser_manager_get_snapshot(ParserManager *mgr);

/** @} */ /* end active_parser */

/* ================================================================== */
/** @defgroup plugin_introspection Plugin Enumeration and Introspection
 *  @brief Query loaded plugin information.
 *  @{
 */
/* ================================================================== */

/**
 * @brief Plugin information returned by enumeration functions.
 *
 * All string pointers point into plugin-owned memory and are valid
 * only while the plugin remains loaded.
 */
typedef struct LimePluginInfo {
    LimePluginHandle handle;   /**< Plugin handle */
    const char *name;          /**< Human-readable plugin name */
    LimePluginVersion version; /**< Plugin semantic version */
    uint32_t capabilities;     /**< Bitmask of LimePluginCaps */
    bool is_active;            /**< True if this is the active plugin */
    bool is_dynamic;           /**< True if loaded from shared library */
} LimePluginInfo;

/**
 * @brief Get information about a specific plugin.
 *
 * @param mgr    The parser manager.
 * @param handle Handle of the plugin to query.
 * @param[out] info Filled with plugin information on success.
 * @return PM_OK on success, or PM_ERR_PLUGIN_NOT_FOUND.
 */
ParserManagerStatus parser_manager_get_plugin_info(const ParserManager *mgr,
                                                   LimePluginHandle handle, LimePluginInfo *info);

/**
 * @brief Enumerate all loaded plugins.
 *
 * Fills the @p infos array with up to @p max_count entries.
 *
 * @param mgr          The parser manager.
 * @param[out] infos   Array to fill with plugin information.
 * @param max_count    Maximum entries to write.
 * @param[out] actual_count Receives the total number of loaded plugins.
 *                     If greater than @p max_count, some entries were omitted.
 * @return PM_OK on success.
 */
ParserManagerStatus parser_manager_list_plugins(const ParserManager *mgr, LimePluginInfo *infos,
                                                uint32_t max_count, uint32_t *actual_count);

/**
 * @brief Look up a plugin by name.
 *
 * @param mgr  The parser manager.
 * @param name Plugin name to search for.
 * @return Handle of the matching plugin, or LIME_PLUGIN_HANDLE_INVALID
 *         if not found.
 */
LimePluginHandle parser_manager_find_by_name(const ParserManager *mgr, const char *name);

/**
 * @brief Get the number of loaded plugins.
 *
 * @param mgr The parser manager.
 * @return Number of plugins currently loaded.
 */
uint32_t parser_manager_plugin_count(const ParserManager *mgr);

/** @} */ /* end plugin_introspection */

/* ================================================================== */
/** @defgroup hot_swap Hot-Swap Support
 *  @brief Atomically replace the active plugin and snapshot.
 *  @{
 */
/* ================================================================== */

/**
 * @brief Atomically replace the active plugin and snapshot.
 *
 * Equivalent to parser_manager_set_active() but guarantees that the
 * transition is atomic from the perspective of parser_manager_get_snapshot()
 * callers.  There is no window where get_snapshot() returns NULL or an
 * intermediate state.
 *
 * The old snapshot is released once all references to it are dropped
 * (existing parse sessions keep their pinned references).
 *
 * @param mgr          The parser manager.
 * @param new_handle   Handle of the plugin to activate.
 * @param grammar_file Grammar file for create_snapshot().  May be NULL
 *                     if a snapshot is already available.
 * @return PM_OK on success, or an error code.
 *
 * @see parser_manager_set_active()
 */
ParserManagerStatus parser_manager_hot_swap(ParserManager *mgr, LimePluginHandle new_handle,
                                            const char *grammar_file);

/** @} */ /* end hot_swap */

/* ================================================================== */
/** @defgroup version_utils Version Compatibility Utilities
 *  @brief Compare and format plugin versions.
 *  @{
 */
/* ================================================================== */

/**
 * @brief Compare two plugin versions.
 *
 * @param a First version.
 * @param b Second version.
 * @return Negative if @p a < @p b, 0 if equal, positive if @p a > @p b.
 */
int lime_plugin_version_compare(LimePluginVersion a, LimePluginVersion b);

/**
 * @brief Check if a plugin version satisfies a minimum requirement.
 *
 * @param actual   The version to test.
 * @param required The minimum required version.
 * @retval true  @p actual >= @p required.
 * @retval false @p actual < @p required.
 */
bool lime_plugin_version_satisfies(LimePluginVersion actual, LimePluginVersion required);

/**
 * @brief Format a version as a string.
 *
 * Writes a "major.minor.patch" string into @p buf.
 *
 * @param v      Version to format.
 * @param buf    Output buffer.
 * @param buflen Size of @p buf in bytes (must be at least 16).
 * @return @p buf for convenience.
 */
char *lime_plugin_version_string(LimePluginVersion v, char *buf, size_t buflen);

/** @} */ /* end version_utils */

/* ================================================================== */
/** @defgroup plugin_validation Plugin Validation and Introspection Utilities
 *  @brief Validate plugin structs and query capabilities.
 *  @{
 */
/* ================================================================== */

/**
 * @brief Validate that a plugin struct has all required callbacks and
 *        a compatible ABI version.
 *
 * @param plugin Plugin struct to validate.
 * @return PM_OK on success, or an error code describing the first
 *         problem found.
 */
ParserManagerStatus lime_plugin_validate(const LimeParserPlugin *plugin);

/**
 * @brief Check if a plugin has a specific capability.
 *
 * @param plugin Plugin to check.
 * @param cap    Capability flag to test.
 * @retval true  The plugin advertises @p cap.
 * @retval false The plugin does not have @p cap.
 */
bool lime_plugin_has_capability(const LimeParserPlugin *plugin, LimePluginCaps cap);

/**
 * @brief Check if a plugin has all of the specified capabilities.
 *
 * @param plugin        Plugin to check.
 * @param required_caps Bitmask of required capabilities (bitwise AND test).
 * @retval true  The plugin has every capability in @p required_caps.
 * @retval false At least one required capability is missing.
 */
bool lime_plugin_has_all_capabilities(const LimeParserPlugin *plugin, uint32_t required_caps);

/**
 * @brief Return a human-readable name for a single capability flag.
 *
 * @param cap Capability flag to name.
 * @return Static string like "SNAPSHOT", "EXTENSIBLE", etc., or
 *         "unknown" for unrecognized flags.
 */
const char *lime_plugin_capability_name(LimePluginCaps cap);

/**
 * @brief Format a capability bitmask as a comma-separated string of names.
 *
 * @param caps   Bitmask of capabilities.
 * @param buf    Output buffer.
 * @param buflen Size of @p buf in bytes.
 * @return @p buf for convenience.
 */
char *lime_plugin_capabilities_string(uint32_t caps, char *buf, size_t buflen);

/**
 * @brief Print a diagnostic summary of a plugin to a file stream.
 *
 * Includes name, version, ABI, capabilities, and callback presence.
 *
 * @param plugin Plugin to describe.
 * @param out    Output stream (e.g., stdout or stderr).
 */
void lime_plugin_dump(const LimeParserPlugin *plugin, FILE *out);

/** @} */ /* end plugin_validation */

#ifdef __cplusplus
}
#endif

#endif /* PARSER_MANAGER_H */
