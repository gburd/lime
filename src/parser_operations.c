/*
** Parser Operations - High-level runtime parser management operations.
**
** Implements the convenience API declared in include/parser_operations.h.
** These functions compose the low-level parser_manager_* primitives into
** production-ready workflows with error recovery and rollback.
*/
#include "parser_operations.h"
#include "parser.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ================================================================== */
/*  Internal helpers                                                   */
/* ================================================================== */

static ParserOpResult make_result(ParserManagerStatus status, LimePluginHandle handle,
                                  const char *fmt, ...) {
    ParserOpResult r;
    r.status = status;
    r.handle = handle;
    r.message = NULL;

    if (fmt != NULL) {
        va_list ap;
        va_start(ap, fmt);
        /* Measure needed size */
        va_list ap2;
        va_copy(ap2, ap);
        int n = vsnprintf(NULL, 0, fmt, ap2);
        va_end(ap2);

        if (n > 0) {
            r.message = malloc((size_t)n + 1);
            if (r.message != NULL) {
                vsnprintf(r.message, (size_t)n + 1, fmt, ap);
            }
        }
        va_end(ap);
    }

    return r;
}

static ParserOpResult ok_result(LimePluginHandle handle) {
    return make_result(PM_OK, handle, NULL);
}

static ParserOpResult err_result(ParserManagerStatus status, LimePluginHandle handle,
                                 const char *msg) {
    return make_result(status, handle, "%s", msg);
}

/* ================================================================== */
/*  ParserOpResult cleanup                                             */
/* ================================================================== */

void parser_op_result_cleanup(ParserOpResult *result) {
    if (result != NULL && result->message != NULL) {
        free(result->message);
        result->message = NULL;
    }
}

/* ================================================================== */
/*  Add operations                                                     */
/* ================================================================== */

ParserOpResult parser_op_add_dynamic(ParserManager *mgr, const char *library_path,
                                     const char *grammar_file, void *user_data) {
    if (mgr == NULL || library_path == NULL) {
        return err_result(PM_ERR_INVALID_ARG, LIME_PLUGIN_HANDLE_INVALID,
                          "parser_op_add_dynamic: NULL argument");
    }

    /* Step 1: Load the plugin */
    LimePluginHandle handle;
    ParserManagerStatus st = parser_manager_load(mgr, library_path, user_data, &handle);
    if (st != PM_OK) {
        return make_result(st, LIME_PLUGIN_HANDLE_INVALID, "failed to load plugin from '%s': %s",
                           library_path, parser_manager_status_string(st));
    }

    /* Step 2: Activate with grammar (if provided) */
    if (grammar_file != NULL) {
        st = parser_manager_set_active(mgr, handle, grammar_file);
        if (st != PM_OK) {
            /* Rollback: unload the plugin we just loaded */
            ParserManagerStatus unload_st = parser_manager_unload(mgr, handle);
            (void)unload_st; /* best effort */

            return make_result(st, LIME_PLUGIN_HANDLE_INVALID,
                               "loaded plugin from '%s' but activation "
                               "failed (rolled back): %s",
                               library_path, parser_manager_status_string(st));
        }
    }

    return ok_result(handle);
}

ParserOpResult parser_op_add_static(ParserManager *mgr, const LimeParserPlugin *plugin,
                                    const char *grammar_file, void *user_data) {
    if (mgr == NULL || plugin == NULL) {
        return err_result(PM_ERR_INVALID_ARG, LIME_PLUGIN_HANDLE_INVALID,
                          "parser_op_add_static: NULL argument");
    }

    /* Step 1: Register the plugin */
    LimePluginHandle handle;
    ParserManagerStatus st = parser_manager_register(mgr, plugin, user_data, &handle);
    if (st != PM_OK) {
        const char *name = plugin->get_name ? plugin->get_name() : "(unknown)";
        return make_result(st, LIME_PLUGIN_HANDLE_INVALID, "failed to register plugin '%s': %s",
                           name, parser_manager_status_string(st));
    }

    /* Step 2: Activate with grammar (if provided) */
    if (grammar_file != NULL) {
        st = parser_manager_set_active(mgr, handle, grammar_file);
        if (st != PM_OK) {
            parser_manager_unload(mgr, handle);

            const char *name = plugin->get_name ? plugin->get_name() : "(unknown)";
            return make_result(st, LIME_PLUGIN_HANDLE_INVALID,
                               "registered plugin '%s' but activation "
                               "failed (rolled back): %s",
                               name, parser_manager_status_string(st));
        }
    }

    return ok_result(handle);
}

/* ================================================================== */
/*  Remove operations                                                  */
/* ================================================================== */

ParserOpResult parser_op_remove(ParserManager *mgr, LimePluginHandle handle,
                                LimePluginHandle fallback_handle, const char *fallback_grammar) {
    if (mgr == NULL || handle == LIME_PLUGIN_HANDLE_INVALID) {
        return err_result(PM_ERR_INVALID_ARG, LIME_PLUGIN_HANDLE_INVALID,
                          "parser_op_remove: invalid argument");
    }

    /* Check if the plugin being removed is the active one */
    LimePluginHandle active = parser_manager_get_active(mgr);

    if (active == handle) {
        /* Need to switch to fallback (or deactivate) before removing */
        if (fallback_handle != LIME_PLUGIN_HANDLE_INVALID) {
            ParserManagerStatus st =
                parser_manager_set_active(mgr, fallback_handle, fallback_grammar);
            if (st != PM_OK) {
                return make_result(st, handle,
                                   "cannot remove active plugin: failed to "
                                   "activate fallback: %s",
                                   parser_manager_status_string(st));
            }
        }
        /* If no fallback, unload will deactivate via its own logic */
    }

    ParserManagerStatus st = parser_manager_unload(mgr, handle);
    if (st != PM_OK) {
        return make_result(st, handle, "unload failed: %s", parser_manager_status_string(st));
    }

    return ok_result(handle);
}

ParserOpResult parser_op_remove_by_name(ParserManager *mgr, const char *name,
                                        LimePluginHandle fallback_handle,
                                        const char *fallback_grammar) {
    if (mgr == NULL || name == NULL) {
        return err_result(PM_ERR_INVALID_ARG, LIME_PLUGIN_HANDLE_INVALID,
                          "parser_op_remove_by_name: NULL argument");
    }

    LimePluginHandle handle = parser_manager_find_by_name(mgr, name);
    if (handle == LIME_PLUGIN_HANDLE_INVALID) {
        return make_result(PM_ERR_PLUGIN_NOT_FOUND, LIME_PLUGIN_HANDLE_INVALID,
                           "no plugin named '%s' is loaded", name);
    }

    return parser_op_remove(mgr, handle, fallback_handle, fallback_grammar);
}

/* ================================================================== */
/*  Update operations                                                  */
/* ================================================================== */

ParserOpResult parser_op_update(ParserManager *mgr, const char *new_library_path,
                                const char *grammar_file, bool version_check, void *user_data) {
    if (mgr == NULL) {
        return err_result(PM_ERR_INVALID_ARG, LIME_PLUGIN_HANDLE_INVALID,
                          "parser_op_update: NULL manager");
    }

    if (grammar_file == NULL) {
        return err_result(PM_ERR_INVALID_ARG, LIME_PLUGIN_HANDLE_INVALID,
                          "parser_op_update: grammar_file is required");
    }

    LimePluginHandle old_handle = parser_manager_get_active(mgr);

    /*
    ** If new_library_path is NULL, reload with the current active plugin.
    */
    if (new_library_path == NULL) {
        if (old_handle == LIME_PLUGIN_HANDLE_INVALID) {
            return err_result(PM_ERR_NO_ACTIVE_PLUGIN, LIME_PLUGIN_HANDLE_INVALID,
                              "no active plugin to reload");
        }
        return parser_op_reload_grammar(mgr, grammar_file);
    }

    /* Get version info for the current active plugin (for version check) */
    LimePluginVersion old_version = { 0, 0, 0 };
    if (version_check && old_handle != LIME_PLUGIN_HANDLE_INVALID) {
        LimePluginInfo old_info;
        ParserManagerStatus st = parser_manager_get_plugin_info(mgr, old_handle, &old_info);
        if (st == PM_OK) {
            old_version = old_info.version;
        }
    }

    /* Step 1: Load the new plugin */
    LimePluginHandle new_handle;
    ParserManagerStatus st = parser_manager_load(mgr, new_library_path, user_data, &new_handle);
    if (st != PM_OK) {
        return make_result(st, LIME_PLUGIN_HANDLE_INVALID,
                           "failed to load new plugin from '%s': %s", new_library_path,
                           parser_manager_status_string(st));
    }

    /* Step 2: Version check (if requested) */
    if (version_check && old_handle != LIME_PLUGIN_HANDLE_INVALID) {
        LimePluginInfo new_info;
        st = parser_manager_get_plugin_info(mgr, new_handle, &new_info);
        if (st == PM_OK) {
            if (lime_plugin_version_compare(new_info.version, old_version) < 0) {
                /* Downgrade detected -- rollback */
                parser_manager_unload(mgr, new_handle);

                char old_vbuf[16], new_vbuf[16];
                lime_plugin_version_string(old_version, old_vbuf, sizeof(old_vbuf));
                lime_plugin_version_string(new_info.version, new_vbuf, sizeof(new_vbuf));

                return make_result(PM_ERR_ABI_MISMATCH, LIME_PLUGIN_HANDLE_INVALID,
                                   "version downgrade rejected: current=%s, "
                                   "new=%s (use version_check=false to force)",
                                   old_vbuf, new_vbuf);
            }
        }
    }

    /* Step 3: Hot-swap to the new plugin */
    st = parser_manager_hot_swap(mgr, new_handle, grammar_file);
    if (st != PM_OK) {
        /* Rollback: unload the new plugin, old remains active */
        parser_manager_unload(mgr, new_handle);

        return make_result(st, LIME_PLUGIN_HANDLE_INVALID,
                           "hot-swap to new plugin failed (rolled back): %s",
                           parser_manager_status_string(st));
    }

    /* Step 4: Unload the old plugin (if there was one) */
    if (old_handle != LIME_PLUGIN_HANDLE_INVALID) {
        parser_manager_unload(mgr, old_handle);
    }

    return ok_result(new_handle);
}

ParserOpResult parser_op_reload_grammar(ParserManager *mgr, const char *grammar_file) {
    if (mgr == NULL || grammar_file == NULL) {
        return err_result(PM_ERR_INVALID_ARG, LIME_PLUGIN_HANDLE_INVALID,
                          "parser_op_reload_grammar: NULL argument");
    }

    LimePluginHandle active = parser_manager_get_active(mgr);
    if (active == LIME_PLUGIN_HANDLE_INVALID) {
        return err_result(PM_ERR_NO_ACTIVE_PLUGIN, LIME_PLUGIN_HANDLE_INVALID,
                          "no active plugin to reload grammar for");
    }

    /*
    ** Use set_active with the same handle but a new grammar file.
    ** This creates a new snapshot and atomically replaces the old one.
    */
    ParserManagerStatus st = parser_manager_set_active(mgr, active, grammar_file);
    if (st != PM_OK) {
        return make_result(st, active, "grammar reload failed (old snapshot preserved): %s",
                           parser_manager_status_string(st));
    }

    return ok_result(active);
}

/* ================================================================== */
/*  Query operations                                                   */
/* ================================================================== */

ParserOpResult parser_op_get_stats(const ParserManager *mgr, ParserManagerStats *stats) {
    if (mgr == NULL || stats == NULL) {
        return err_result(PM_ERR_INVALID_ARG, LIME_PLUGIN_HANDLE_INVALID,
                          "parser_op_get_stats: NULL argument");
    }

    memset(stats, 0, sizeof(*stats));

    stats->total_plugins = parser_manager_plugin_count(mgr);
    stats->active_handle = parser_manager_get_active(mgr);
    stats->has_active = (stats->active_handle != LIME_PLUGIN_HANDLE_INVALID);

    /* Count dynamic vs static plugins */
    uint32_t max = stats->total_plugins;
    if (max > 0) {
        LimePluginInfo *infos = calloc(max, sizeof(LimePluginInfo));
        if (infos != NULL) {
            uint32_t actual;
            ParserManagerStatus st = parser_manager_list_plugins(mgr, infos, max, &actual);
            if (st == PM_OK) {
                for (uint32_t i = 0; i < actual && i < max; i++) {
                    if (infos[i].is_dynamic) {
                        stats->dynamic_plugins++;
                    } else {
                        stats->static_plugins++;
                    }
                }
            }
            free(infos);
        }
    }

    /* Active plugin details */
    if (stats->has_active) {
        LimePluginInfo active_info;
        ParserManagerStatus st =
            parser_manager_get_plugin_info(mgr, stats->active_handle, &active_info);
        if (st == PM_OK) {
            stats->active_name = active_info.name;
            stats->active_version = active_info.version;
            stats->active_capabilities = active_info.capabilities;
        }
    }

    /* Check if a snapshot is available */
    ParserSnapshot *snap = parser_manager_get_snapshot((ParserManager *)mgr);
    if (snap != NULL) {
        stats->snapshot_available = true;
        lemon_snapshot_release(snap);
    }

    return ok_result(stats->active_handle);
}

ParserOpResult parser_op_list_formatted(const ParserManager *mgr, FILE *out) {
    if (mgr == NULL || out == NULL) {
        return err_result(PM_ERR_INVALID_ARG, LIME_PLUGIN_HANDLE_INVALID,
                          "parser_op_list_formatted: NULL argument");
    }

    uint32_t count = parser_manager_plugin_count(mgr);
    if (count == 0) {
        fprintf(out, "(no plugins loaded)\n");
        return ok_result(LIME_PLUGIN_HANDLE_INVALID);
    }

    LimePluginInfo *infos = calloc(count, sizeof(LimePluginInfo));
    if (infos == NULL) {
        return err_result(PM_ERR_ALLOC, LIME_PLUGIN_HANDLE_INVALID, "allocation failed");
    }

    uint32_t actual;
    ParserManagerStatus st = parser_manager_list_plugins(mgr, infos, count, &actual);
    if (st != PM_OK) {
        free(infos);
        return err_result(st, LIME_PLUGIN_HANDLE_INVALID, "list_plugins failed");
    }

    uint32_t to_print = (actual < count) ? actual : count;
    for (uint32_t i = 0; i < to_print; i++) {
        char vbuf[16];
        lime_plugin_version_string(infos[i].version, vbuf, sizeof(vbuf));

        char capbuf[128];
        lime_plugin_capabilities_string(infos[i].capabilities, capbuf, sizeof(capbuf));

        fprintf(out, "  [%u] %s v%s%s%s%s\n", infos[i].handle,
                infos[i].name ? infos[i].name : "(null)", vbuf,
                infos[i].is_active ? " (active)" : "", infos[i].is_dynamic ? " (dynamic)" : "",
                capbuf[0] ? "" : "");

        if (capbuf[0]) {
            fprintf(out, "       capabilities: %s\n", capbuf);
        }
    }

    free(infos);
    return ok_result(LIME_PLUGIN_HANDLE_INVALID);
}

bool parser_op_is_loaded(const ParserManager *mgr, const char *name,
                         const LimePluginVersion *min_version) {
    if (mgr == NULL || name == NULL) return false;

    LimePluginHandle h = parser_manager_find_by_name(mgr, name);
    if (h == LIME_PLUGIN_HANDLE_INVALID) return false;

    if (min_version == NULL) return true;

    LimePluginInfo info;
    ParserManagerStatus st = parser_manager_get_plugin_info(mgr, h, &info);
    if (st != PM_OK) return false;

    return lime_plugin_version_satisfies(info.version, *min_version);
}
