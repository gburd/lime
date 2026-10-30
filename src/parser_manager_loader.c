/*
** parser_manager_loader.c -- OPT-IN plugin loader: parser_manager_load()
** opens a parser plugin from a shared library.
**
** Not part of liblime_parser or liblime_compiler.  Built only with
** `meson setup -Dplugin_loader=enabled` into its own archive,
** liblime_plugin_loader.a, so the default build cannot load code at
** runtime (tests/check_no_runtime_loader.sh enforces it on the default
** libraries).  Hosts that want shared-library plugins link it explicitly.
**
** Thin shim over the platform loader:
**   open   dlopen(path, RTLD_NOW|RTLD_LOCAL) -> LoadLibraryA(path)
**   entry  dlsym(h, LIME_PLUGIN_ENTRY_SYMBOL) -> GetProcAddress(h, ...)
**   close  dlclose(h)                         -> FreeLibrary((HMODULE)h)
** Failures collapse to PM_ERR_DLOPEN_FAILED / PM_ERR_NO_ENTRY_POINT.
*/
#include "parser_manager.h"
#include "parser_operations.h"

#include <stdarg.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define PM_PATH_SEP '\\'
#else
#include <dlfcn.h>
#define PM_PATH_SEP '/'
#endif

ParserManagerStatus parser_manager_register_loaded(ParserManager *mgr,
                                                   const LimeParserPlugin *plugin,
                                                   void *dl_handle, void (*close_fn)(void *),
                                                   char *library_path, void *user_data,
                                                   LimePluginHandle *handle_out);
const ParserManagerConfig *parser_manager_config(const ParserManager *mgr);

static void *pm_dl_open(const char *path) {
#ifdef _WIN32
    return (void *)LoadLibraryA(path);
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

static LimePluginEntryFn pm_dl_entry(void *handle) {
#ifdef _WIN32
    FARPROC sym = GetProcAddress((HMODULE)handle, LIME_PLUGIN_ENTRY_SYMBOL);
    if (sym == NULL) return NULL;
    LimePluginEntryFn fn; /* copy the bits: a direct cast trips -Wcast-function-type */
    memcpy(&fn, &sym, sizeof(fn));
    return fn;
#else
    return (LimePluginEntryFn)dlsym(handle, LIME_PLUGIN_ENTRY_SYMBOL);
#endif
}

static void pm_dl_close(void *handle) {
    if (handle == NULL) return;
#ifdef _WIN32
    FreeLibrary((HMODULE)handle);
#else
    dlclose(handle);
#endif
}

static char *dup_string(const char *s) {
    size_t n = strlen(s) + 1;
    char *r = malloc(n);
    if (r) memcpy(r, s, n);
    return r;
}

/* Absolute / explicitly relative paths are used as given; bare names are
** searched in config->plugin_search_paths, then used as given. */
static char *resolve_library_path(const ParserManagerConfig *config, const char *path) {
    if (path[0] == '/' || path[0] == '.') return dup_string(path);
#ifdef _WIN32
    if (path[0] == '\\') return dup_string(path);
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':')
        return dup_string(path);
#endif
    if (config != NULL && config->plugin_search_paths != NULL) {
        for (const char **dir = config->plugin_search_paths; *dir != NULL; dir++) {
            size_t dlen = strlen(*dir), plen = strlen(path);
            char *full = malloc(dlen + plen + 2);
            if (full == NULL) continue;
            memcpy(full, *dir, dlen);
            full[dlen] = PM_PATH_SEP;
            memcpy(full + dlen + 1, path, plen + 1);
            FILE *f = fopen(full, "r");
            if (f != NULL) {
                fclose(f);
                return full;
            }
            free(full);
        }
    }
    return dup_string(path);
}

ParserManagerStatus parser_manager_load(ParserManager *mgr, const char *path, void *user_data,
                                        LimePluginHandle *handle_out) {
    if (mgr == NULL || path == NULL || handle_out == NULL) return PM_ERR_INVALID_ARG;

    char *resolved = resolve_library_path(parser_manager_config(mgr), path);
    if (resolved == NULL) return PM_ERR_ALLOC;

    void *dl_handle = pm_dl_open(resolved);
    if (dl_handle == NULL) {
        free(resolved);
        return PM_ERR_DLOPEN_FAILED;
    }
    LimePluginEntryFn entry_fn = pm_dl_entry(dl_handle);
    const LimeParserPlugin *plugin = entry_fn ? entry_fn() : NULL;
    if (plugin == NULL) {
        pm_dl_close(dl_handle);
        free(resolved);
        return PM_ERR_NO_ENTRY_POINT;
    }
    ParserManagerStatus st = parser_manager_register_loaded(mgr, plugin, dl_handle, pm_dl_close,
                                                            resolved, user_data, handle_out);
    if (st != PM_OK) pm_dl_close(dl_handle);
    return st;
}

/* ================================================================== */
/*  parser_operations.h entry points that load a library              */
/* ================================================================== */

static ParserOpResult make_result(ParserManagerStatus status, LimePluginHandle handle,
                                  const char *fmt, ...) {
    ParserOpResult r = {status, handle, NULL};
    if (fmt != NULL) {
        va_list ap, ap2;
        va_start(ap, fmt);
        va_copy(ap2, ap);
        int n = vsnprintf(NULL, 0, fmt, ap2);
        va_end(ap2);
        if (n > 0 && (r.message = malloc((size_t)n + 1)) != NULL)
            vsnprintf(r.message, (size_t)n + 1, fmt, ap);
        va_end(ap);
    }
    return r;
}
static ParserOpResult ok_result(LimePluginHandle h) { return make_result(PM_OK, h, NULL); }
static ParserOpResult err_result(ParserManagerStatus st, LimePluginHandle h, const char *msg) {
    return make_result(st, h, "%s", msg);
}

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

ParserOpResult parser_op_update_from_library(ParserManager *mgr, const char *new_library_path,
                                             const char *grammar_file, bool version_check,
                                             void *user_data) {
    if (mgr == NULL) {
        return err_result(PM_ERR_INVALID_ARG, LIME_PLUGIN_HANDLE_INVALID,
                          "parser_op_update_from_library: NULL manager");
    }

    if (grammar_file == NULL) {
        return err_result(PM_ERR_INVALID_ARG, LIME_PLUGIN_HANDLE_INVALID,
                          "parser_op_update_from_library: grammar_file is required");
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

