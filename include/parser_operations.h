/*
** Parser Operations - High-level runtime parser management operations.
**
** Builds on the ParserManager primitives to provide production-ready
** workflows for common parser management tasks:
**
**   - Add and activate a parser in one call
**   - Update a parser with automatic rollback on failure
**   - Safely remove a parser (deactivate-first semantics)
**   - Reload a parser's grammar without changing plugins
**   - Enumerate parsers with formatted output
**   - Collect runtime statistics
**
** These are convenience functions that compose the lower-level
** parser_manager_* API. They add error recovery, logging, and
** version-aware upgrade logic that applications would otherwise
** need to implement themselves.
**
** Thread safety: All functions are thread-safe. They acquire the
** appropriate locks internally via the ParserManager.
*/
#ifndef PARSER_OPERATIONS_H
#define PARSER_OPERATIONS_H

#include "parser_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*  Operation result with detail                                       */
/* ================================================================== */

/**
 * @brief Extended result from a high-level operation.
 *
 * Contains the ParserManagerStatus code plus an optional error
 * message with more context than the status code alone provides.
 */
typedef struct ParserOpResult {
    ParserManagerStatus status; /**< Operation status code */
    LimePluginHandle handle;    /**< Relevant handle, or INVALID */
    char *message;              /**< Malloc'd detail string, or NULL.  Caller frees. */
} ParserOpResult;

/*
** Free the message inside a ParserOpResult (if any).
** Does not free the result struct itself (it is stack-allocated).
*/
void parser_op_result_cleanup(ParserOpResult *result);

/* ================================================================== */
/*  Add operations                                                     */
/* ================================================================== */

/*
** Load a plugin from a shared library and activate it immediately.
**
** Equivalent to:
**   1. parser_manager_load(mgr, library_path, user_data, &handle)
**   2. parser_manager_set_active(mgr, handle, grammar_file)
**
** If step 2 fails, the plugin is automatically unloaded (rolled back).
**
** grammar_file may be NULL to load the plugin without activating a
** grammar (the plugin is registered but no snapshot is created).
*/
ParserOpResult parser_op_add_dynamic(ParserManager *mgr, const char *library_path,
                                     const char *grammar_file, void *user_data);

/*
** Register a static plugin and activate it immediately.
**
** Equivalent to:
**   1. parser_manager_register(mgr, plugin, user_data, &handle)
**   2. parser_manager_set_active(mgr, handle, grammar_file)
**
** If step 2 fails, the plugin is automatically unloaded.
*/
ParserOpResult parser_op_add_static(ParserManager *mgr, const LimeParserPlugin *plugin,
                                    const char *grammar_file, void *user_data);

/* ================================================================== */
/*  Remove operations                                                  */
/* ================================================================== */

/*
** Remove a parser by handle.
**
** If the parser is currently active:
**   - If fallback_handle is not LIME_PLUGIN_HANDLE_INVALID, activates
**     the fallback plugin (with fallback_grammar if non-NULL) before
**     removing the target.
**   - If fallback_handle is INVALID, deactivates the target and leaves
**     no active parser.
**
** Returns PM_OK on success.
*/
ParserOpResult parser_op_remove(ParserManager *mgr, LimePluginHandle handle,
                                LimePluginHandle fallback_handle, const char *fallback_grammar);

/*
** Remove a parser by name.
**
** Looks up the plugin by name and delegates to parser_op_remove().
*/
ParserOpResult parser_op_remove_by_name(ParserManager *mgr, const char *name,
                                        LimePluginHandle fallback_handle,
                                        const char *fallback_grammar);

/* ================================================================== */
/*  Update operations                                                  */
/* ================================================================== */

/*
** Update (replace) the active parser with a new plugin version.
**
** Loads the new plugin from new_library_path, creates a snapshot from
** grammar_file, validates it (if the plugin supports validation), and
** atomically swaps it in as the active parser via hot_swap.
**
** If any step fails, the old parser remains active and the new plugin
** is unloaded. The operation is all-or-nothing.
**
** If new_library_path is NULL, re-uses the currently active plugin
** (useful for reloading a grammar without changing the plugin binary).
**
** version_check: if true, the new plugin must have a version >= the
** current active plugin. Set to false to allow downgrades.
*/
ParserOpResult parser_op_update(ParserManager *mgr, const char *new_library_path,
                                const char *grammar_file, bool version_check, void *user_data);

/*
** Reload the grammar for the currently active plugin.
**
** Creates a new snapshot from grammar_file using the current active
** plugin and atomically replaces the active snapshot. The plugin
** itself is not unloaded or replaced.
**
** Useful for picking up grammar file changes at runtime.
*/
ParserOpResult parser_op_reload_grammar(ParserManager *mgr, const char *grammar_file);

/* ================================================================== */
/*  Query operations                                                   */
/* ================================================================== */

/**
 * @brief Runtime statistics for the parser manager.
 */
typedef struct ParserManagerStats {
    uint32_t total_plugins;           /**< Currently loaded plugins */
    uint32_t dynamic_plugins;         /**< Loaded from shared libraries */
    uint32_t static_plugins;          /**< Registered statically */
    bool has_active;                  /**< Whether an active plugin is set */
    LimePluginHandle active_handle;   /**< Handle of the active plugin */
    const char *active_name;          /**< Name of active plugin, or NULL */
    LimePluginVersion active_version; /**< Version of the active plugin */
    uint32_t active_capabilities;     /**< Bitmap of capabilities advertised by the active plugin */
    bool snapshot_available;          /**< Whether get_snapshot would return non-NULL */
} ParserManagerStats;

/*
** Collect runtime statistics about the parser manager.
**
** The returned stats struct contains pointers into plugin-owned
** memory (active_name). These are valid only while the plugin
** remains loaded. Copy the string if you need to keep it.
*/
ParserOpResult parser_op_get_stats(const ParserManager *mgr, ParserManagerStats *stats);

/*
** Write a formatted summary of all loaded plugins to the given stream.
**
** Output format:
**   [1] sql_parser v1.2.3 (active) [snapshot, extensible, jit]
**   [2] json_parser v2.0.0 [snapshot]
**
** Returns PM_OK on success.
*/
ParserOpResult parser_op_list_formatted(const ParserManager *mgr, FILE *out);

/*
** Check if a specific plugin version is loaded by name.
**
** If min_version is non-NULL, returns true only if a plugin with
** the given name is loaded AND its version >= *min_version.
** If min_version is NULL, returns true if any version is loaded.
*/
bool parser_op_is_loaded(const ParserManager *mgr, const char *name,
                         const LimePluginVersion *min_version);

#ifdef __cplusplus
}
#endif

#endif /* PARSER_OPERATIONS_H */
