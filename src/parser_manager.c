/*
** Parser Manager - Runtime parser plugin management implementation.
**
** Implements the API declared in include/parser_manager.h.
**
** The manager maintains a dynamic array of PluginEntry structs protected
** by a pthread_rwlock_t.  The active snapshot is stored as an atomic
** pointer for lock-free reads via parser_manager_get_snapshot().
**
** Dynamic plugin loading uses dlopen/dlsym/dlclose (POSIX).  On
** platforms without dlopen support, only static plugin registration
** is available.
*/
#include "parser_manager.h"
#include "parser.h"
#include "snapshot.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#include <pthread.h>

#ifndef _WIN32
#include <dlfcn.h>
#endif

/* ================================================================== */
/*  Internal data structures                                           */
/* ================================================================== */

#define PM_INITIAL_CAPACITY 8

/*
** A single entry in the plugin registry.
*/
typedef struct PluginEntry {
    LimePluginHandle        handle;
    const LimeParserPlugin *plugin;
    void                   *dl_handle;     /* dlopen handle, NULL for static */
    char                   *library_path;  /* strdup'd path, NULL for static */
    bool                    is_dynamic;
} PluginEntry;

/*
** The ParserManager struct.
*/
struct ParserManager {
    PluginEntry            *entries;
    uint32_t                count;
    uint32_t                capacity;
    LimePluginHandle        next_handle;

    LimePluginHandle        active_handle;
    _Atomic(ParserSnapshot *) active_snap;

    ParserManagerConfig     config;

    pthread_rwlock_t        lock;
};

/* ================================================================== */
/*  Internal helpers                                                   */
/* ================================================================== */

static char *dup_string(const char *s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy != NULL) memcpy(copy, s, len);
    return copy;
}

/*
** Find a plugin entry by handle.  Caller must hold at least a read lock.
*/
static PluginEntry *find_entry(ParserManager *mgr, LimePluginHandle handle) {
    for (uint32_t i = 0; i < mgr->count; i++) {
        if (mgr->entries[i].handle == handle) return &mgr->entries[i];
    }
    return NULL;
}

/*
** Find a plugin entry by name.  Caller must hold at least a read lock.
*/
static PluginEntry *find_entry_by_name(ParserManager *mgr, const char *name) {
    for (uint32_t i = 0; i < mgr->count; i++) {
        const LimeParserPlugin *p = mgr->entries[i].plugin;
        if (p != NULL && p->get_name != NULL) {
            const char *pname = p->get_name();
            if (pname != NULL && strcmp(pname, name) == 0) {
                return &mgr->entries[i];
            }
        }
    }
    return NULL;
}

/*
** Grow the entries array.  Caller must hold write lock.
*/
static bool grow_entries(ParserManager *mgr) {
    uint32_t new_cap = mgr->capacity * 2;
    if (new_cap == 0) new_cap = PM_INITIAL_CAPACITY;

    /* Respect max_plugins if set */
    if (mgr->config.max_plugins > 0 && mgr->count >= mgr->config.max_plugins) {
        return false;
    }

    PluginEntry *p = realloc(mgr->entries, new_cap * sizeof(PluginEntry));
    if (p == NULL) return false;

    memset(&p[mgr->capacity], 0,
           (new_cap - mgr->capacity) * sizeof(PluginEntry));
    mgr->entries = p;
    mgr->capacity = new_cap;
    return true;
}

/*
** Validate required callbacks in a plugin struct.
*/
static ParserManagerStatus validate_plugin(const LimeParserPlugin *plugin) {
    if (plugin == NULL) return PM_ERR_INVALID_ARG;
    if (plugin->get_name == NULL) return PM_ERR_INVALID_ARG;
    if (plugin->get_version == NULL) return PM_ERR_INVALID_ARG;
    if (plugin->get_abi_major == NULL) return PM_ERR_INVALID_ARG;
    if (plugin->get_abi_minor == NULL) return PM_ERR_INVALID_ARG;
    if (plugin->get_capabilities == NULL) return PM_ERR_INVALID_ARG;
    if (plugin->destroy == NULL) return PM_ERR_INVALID_ARG;

    /* ABI check */
    uint16_t abi_major = plugin->get_abi_major();
    if (abi_major != LIME_PLUGIN_ABI_VERSION_MAJOR) {
        return PM_ERR_ABI_MISMATCH;
    }

    return PM_OK;
}

/*
** Remove an entry from the array by index.  Caller must hold write lock.
** Shifts remaining entries down to fill the gap.
*/
static void remove_entry_at(ParserManager *mgr, uint32_t index) {
    if (index >= mgr->count) return;

    /* Shift entries after the removed one */
    uint32_t remaining = mgr->count - index - 1;
    if (remaining > 0) {
        memmove(&mgr->entries[index], &mgr->entries[index + 1],
                remaining * sizeof(PluginEntry));
    }

    mgr->count--;
    memset(&mgr->entries[mgr->count], 0, sizeof(PluginEntry));
}

/*
** Release the active snapshot (if any) and set the pointer to NULL.
** Caller must hold write lock.
*/
static void release_active_snapshot(ParserManager *mgr) {
    ParserSnapshot *old = atomic_exchange_explicit(
        &mgr->active_snap, NULL, memory_order_acq_rel);
    if (old != NULL) {
        snapshot_release(old);
    }
}

/*
** Try to resolve a plugin library path.  If the path is absolute or
** the file exists as given, return a strdup'd copy.  Otherwise search
** the configured search paths.  Returns NULL if not found.
*/
static char *resolve_library_path(const ParserManagerConfig *config,
                                  const char *path) {
    if (path == NULL) return NULL;

    /* If the path starts with '/' or '.', use it directly */
    if (path[0] == '/' || path[0] == '.') {
        return dup_string(path);
    }

    /* Try search paths */
    if (config->plugin_search_paths != NULL) {
        for (const char **dir = config->plugin_search_paths; *dir != NULL; dir++) {
            size_t dlen = strlen(*dir);
            size_t plen = strlen(path);
            /* +2 for '/' and NUL */
            char *full = malloc(dlen + plen + 2);
            if (full == NULL) continue;
            memcpy(full, *dir, dlen);
            full[dlen] = '/';
            memcpy(full + dlen + 1, path, plen + 1);

            /* Check if the file exists (best effort) */
            FILE *f = fopen(full, "r");
            if (f != NULL) {
                fclose(f);
                return full;
            }
            free(full);
        }
    }

    /* Fall back to the path as given */
    return dup_string(path);
}

/* ================================================================== */
/*  Status string                                                      */
/* ================================================================== */

const char *parser_manager_status_string(ParserManagerStatus status) {
    switch (status) {
    case PM_OK:                    return "success";
    case PM_ERR_INVALID_ARG:       return "invalid argument";
    case PM_ERR_ALLOC:             return "memory allocation failed";
    case PM_ERR_PLUGIN_NOT_FOUND:  return "plugin not found";
    case PM_ERR_DUPLICATE_NAME:    return "duplicate plugin name";
    case PM_ERR_ABI_MISMATCH:      return "ABI version mismatch";
    case PM_ERR_INIT_FAILED:       return "plugin init() failed";
    case PM_ERR_DLOPEN_FAILED:     return "dlopen() failed";
    case PM_ERR_NO_ENTRY_POINT:    return "missing lime_plugin_entry symbol";
    case PM_ERR_SNAPSHOT_FAILED:   return "snapshot creation failed";
    case PM_ERR_VALIDATION_FAILED: return "snapshot validation failed";
    case PM_ERR_PLUGIN_IN_USE:     return "plugin is in use";
    case PM_ERR_NO_ACTIVE_PLUGIN:  return "no active plugin";
    case PM_ERR_CAPABILITY_MISSING: return "required capability missing";
    }
    return "unknown error";
}

/* ================================================================== */
/*  Manager lifecycle                                                  */
/* ================================================================== */

ParserManager *parser_manager_create(const ParserManagerConfig *config) {
    ParserManager *mgr = calloc(1, sizeof(ParserManager));
    if (mgr == NULL) return NULL;

    if (pthread_rwlock_init(&mgr->lock, NULL) != 0) {
        free(mgr);
        return NULL;
    }

    mgr->entries = calloc(PM_INITIAL_CAPACITY, sizeof(PluginEntry));
    if (mgr->entries == NULL) {
        pthread_rwlock_destroy(&mgr->lock);
        free(mgr);
        return NULL;
    }

    mgr->capacity = PM_INITIAL_CAPACITY;
    mgr->count = 0;
    mgr->next_handle = 1;  /* 0 is LIME_PLUGIN_HANDLE_INVALID */
    mgr->active_handle = LIME_PLUGIN_HANDLE_INVALID;
    atomic_init(&mgr->active_snap, NULL);

    /* Copy config */
    if (config != NULL) {
        mgr->config = *config;
    } else {
        memset(&mgr->config, 0, sizeof(ParserManagerConfig));
    }

    return mgr;
}

void parser_manager_destroy(ParserManager *mgr) {
    if (mgr == NULL) return;

    pthread_rwlock_wrlock(&mgr->lock);

    /* Release active snapshot */
    ParserSnapshot *snap = atomic_exchange_explicit(
        &mgr->active_snap, NULL, memory_order_acq_rel);
    if (snap != NULL) {
        snapshot_release(snap);
    }
    mgr->active_handle = LIME_PLUGIN_HANDLE_INVALID;

    /* Destroy all loaded plugins */
    for (uint32_t i = 0; i < mgr->count; i++) {
        PluginEntry *e = &mgr->entries[i];
        if (e->plugin != NULL && e->plugin->destroy != NULL) {
            e->plugin->destroy();
        }
#ifndef _WIN32
        if (e->is_dynamic && e->dl_handle != NULL) {
            dlclose(e->dl_handle);
        }
#endif
        free(e->library_path);
    }

    free(mgr->entries);
    mgr->entries = NULL;
    mgr->count = 0;
    mgr->capacity = 0;

    pthread_rwlock_unlock(&mgr->lock);
    pthread_rwlock_destroy(&mgr->lock);
    free(mgr);
}

/* ================================================================== */
/*  Plugin registration (internal, shared by load and register)        */
/* ================================================================== */

/*
** Common registration logic.  Caller must hold write lock.
*/
static ParserManagerStatus register_plugin_locked(
    ParserManager *mgr,
    const LimeParserPlugin *plugin,
    void *dl_handle,
    char *library_path,
    bool is_dynamic,
    void *user_data,
    LimePluginHandle *handle_out
) {
    /* Validate plugin struct */
    ParserManagerStatus st = validate_plugin(plugin);
    if (st != PM_OK) {
        free(library_path);
        return st;
    }

    /* Check for duplicate name */
    const char *name = plugin->get_name();
    if (name != NULL && find_entry_by_name(mgr, name) != NULL) {
        free(library_path);
        return PM_ERR_DUPLICATE_NAME;
    }

    /* Check capacity */
    if (mgr->config.max_plugins > 0 && mgr->count >= mgr->config.max_plugins) {
        free(library_path);
        return PM_ERR_ALLOC;
    }

    if (mgr->count >= mgr->capacity) {
        if (!grow_entries(mgr)) {
            free(library_path);
            return PM_ERR_ALLOC;
        }
    }

    /* Call init if provided */
    if (plugin->init != NULL) {
        if (!plugin->init(user_data)) {
            free(library_path);
            return PM_ERR_INIT_FAILED;
        }
    }

    /* Add to registry */
    LimePluginHandle handle = mgr->next_handle++;
    PluginEntry *e = &mgr->entries[mgr->count];
    e->handle = handle;
    e->plugin = plugin;
    e->dl_handle = dl_handle;
    e->library_path = library_path;
    e->is_dynamic = is_dynamic;
    mgr->count++;

    *handle_out = handle;
    return PM_OK;
}

/* ================================================================== */
/*  Plugin loading                                                     */
/* ================================================================== */

ParserManagerStatus parser_manager_load(ParserManager *mgr,
                                        const char *path,
                                        void *user_data,
                                        LimePluginHandle *handle_out) {
    if (mgr == NULL || path == NULL || handle_out == NULL) {
        return PM_ERR_INVALID_ARG;
    }

#ifdef _WIN32
    /* Windows dynamic loading not implemented */
    (void)user_data;
    *handle_out = LIME_PLUGIN_HANDLE_INVALID;
    return PM_ERR_DLOPEN_FAILED;
#else
    /* Resolve the library path */
    char *resolved = resolve_library_path(&mgr->config, path);
    if (resolved == NULL) return PM_ERR_ALLOC;

    /* Open the shared library */
    void *dl_handle = dlopen(resolved, RTLD_NOW | RTLD_LOCAL);
    if (dl_handle == NULL) {
        free(resolved);
        return PM_ERR_DLOPEN_FAILED;
    }

    /* Look up entry point */
    dlerror(); /* clear */
    LimePluginEntryFn entry_fn =
        (LimePluginEntryFn)dlsym(dl_handle, LIME_PLUGIN_ENTRY_SYMBOL);
    if (entry_fn == NULL) {
        dlclose(dl_handle);
        free(resolved);
        return PM_ERR_NO_ENTRY_POINT;
    }

    /* Call entry point to get plugin struct */
    const LimeParserPlugin *plugin = entry_fn();
    if (plugin == NULL) {
        dlclose(dl_handle);
        free(resolved);
        return PM_ERR_NO_ENTRY_POINT;
    }

    /* Register under write lock */
    pthread_rwlock_wrlock(&mgr->lock);
    ParserManagerStatus st = register_plugin_locked(
        mgr, plugin, dl_handle, resolved, true, user_data, handle_out);
    pthread_rwlock_unlock(&mgr->lock);

    if (st != PM_OK) {
        /* On failure, close the library.  Note: register_plugin_locked
        ** already freed resolved on failure paths before the init call,
        ** but if init fails we still need to close dl_handle. */
        dlclose(dl_handle);
    }

    return st;
#endif
}

ParserManagerStatus parser_manager_register(ParserManager *mgr,
                                            const LimeParserPlugin *plugin,
                                            void *user_data,
                                            LimePluginHandle *handle_out) {
    if (mgr == NULL || plugin == NULL || handle_out == NULL) {
        return PM_ERR_INVALID_ARG;
    }

    pthread_rwlock_wrlock(&mgr->lock);
    ParserManagerStatus st = register_plugin_locked(
        mgr, plugin, NULL, NULL, false, user_data, handle_out);
    pthread_rwlock_unlock(&mgr->lock);

    return st;
}

ParserManagerStatus parser_manager_unload(ParserManager *mgr,
                                          LimePluginHandle handle) {
    if (mgr == NULL || handle == LIME_PLUGIN_HANDLE_INVALID) {
        return PM_ERR_INVALID_ARG;
    }

    pthread_rwlock_wrlock(&mgr->lock);

    /* Find the entry */
    uint32_t idx = UINT32_MAX;
    for (uint32_t i = 0; i < mgr->count; i++) {
        if (mgr->entries[i].handle == handle) {
            idx = i;
            break;
        }
    }

    if (idx == UINT32_MAX) {
        pthread_rwlock_unlock(&mgr->lock);
        return PM_ERR_PLUGIN_NOT_FOUND;
    }

    PluginEntry entry = mgr->entries[idx]; /* copy before removal */

    /* If this is the active plugin, deactivate */
    if (mgr->active_handle == handle) {
        release_active_snapshot(mgr);
        mgr->active_handle = LIME_PLUGIN_HANDLE_INVALID;
    }

    /* Remove from registry */
    remove_entry_at(mgr, idx);

    pthread_rwlock_unlock(&mgr->lock);

    /* Call destroy outside the lock to avoid deadlock if the plugin
    ** tries to do anything that acquires a lock. */
    if (entry.plugin != NULL && entry.plugin->destroy != NULL) {
        entry.plugin->destroy();
    }

#ifndef _WIN32
    if (entry.is_dynamic && entry.dl_handle != NULL) {
        dlclose(entry.dl_handle);
    }
#endif

    free(entry.library_path);

    return PM_OK;
}

/* ================================================================== */
/*  Active parser management                                           */
/* ================================================================== */

ParserManagerStatus parser_manager_set_active(ParserManager *mgr,
                                              LimePluginHandle handle,
                                              const char *grammar_file) {
    if (mgr == NULL || handle == LIME_PLUGIN_HANDLE_INVALID) {
        return PM_ERR_INVALID_ARG;
    }

    pthread_rwlock_wrlock(&mgr->lock);

    PluginEntry *e = find_entry(mgr, handle);
    if (e == NULL) {
        pthread_rwlock_unlock(&mgr->lock);
        return PM_ERR_PLUGIN_NOT_FOUND;
    }

    ParserSnapshot *new_snap = NULL;

    if (grammar_file != NULL) {
        /* Plugin must support snapshot creation */
        if (e->plugin->create_snapshot == NULL) {
            pthread_rwlock_unlock(&mgr->lock);
            return PM_ERR_CAPABILITY_MISSING;
        }

        char *error = NULL;
        new_snap = e->plugin->create_snapshot(grammar_file, &error);
        if (new_snap == NULL) {
            pthread_rwlock_unlock(&mgr->lock);
            free(error);
            return PM_ERR_SNAPSHOT_FAILED;
        }
        free(error);

        /* Optionally validate */
        if (mgr->config.validate_on_load && e->plugin->validate_snapshot != NULL) {
            char *val_error = NULL;
            if (!e->plugin->validate_snapshot(new_snap, &val_error)) {
                snapshot_release(new_snap);
                pthread_rwlock_unlock(&mgr->lock);
                free(val_error);
                return PM_ERR_VALIDATION_FAILED;
            }
            free(val_error);
        }

        /* Optionally JIT compile */
        if (mgr->config.auto_jit &&
            (e->plugin->get_capabilities() & LIME_CAP_JIT)) {
            lime_jit_compile(new_snap);
        }
    }

    /* Swap the active snapshot atomically */
    ParserSnapshot *old = atomic_exchange_explicit(
        &mgr->active_snap, new_snap, memory_order_acq_rel);
    if (old != NULL) {
        snapshot_release(old);
    }

    mgr->active_handle = handle;

    pthread_rwlock_unlock(&mgr->lock);
    return PM_OK;
}

LimePluginHandle parser_manager_get_active(const ParserManager *mgr) {
    if (mgr == NULL) return LIME_PLUGIN_HANDLE_INVALID;

    /* active_handle is only written under write lock; reading it
    ** under a read lock or even without a lock is safe for a uint32_t
    ** on all practical platforms. We use a read lock for consistency. */
    pthread_rwlock_rdlock((pthread_rwlock_t *)&mgr->lock);
    LimePluginHandle h = mgr->active_handle;
    pthread_rwlock_unlock((pthread_rwlock_t *)&mgr->lock);

    return h;
}

ParserManagerStatus parser_manager_set_snapshot(ParserManager *mgr,
                                                ParserSnapshot *snap) {
    if (mgr == NULL || snap == NULL) return PM_ERR_INVALID_ARG;

    /* Acquire a reference for the manager */
    snapshot_acquire(snap);

    /* Swap */
    ParserSnapshot *old = atomic_exchange_explicit(
        &mgr->active_snap, snap, memory_order_acq_rel);
    if (old != NULL) {
        snapshot_release(old);
    }

    return PM_OK;
}

ParserSnapshot *parser_manager_get_snapshot(ParserManager *mgr) {
    if (mgr == NULL) return NULL;

    /*
    ** We take a read lock to prevent a concurrent writer (set_active,
    ** hot_swap, unload) from releasing the active snapshot between our
    ** load and our acquire.  Without the lock, there is a TOCTOU race:
    **
    **   reader: snap = atomic_load(active_snap)   // snap refcount=1
    **   writer: old = atomic_exchange(active_snap) // old == snap
    **   writer: snapshot_release(old)              // refcount=0, freed
    **   reader: snapshot_acquire(snap)             // use-after-free
    **
    ** The read lock is shared, so multiple concurrent readers still
    ** proceed in parallel.  Only writers (which acquire the write lock)
    ** are serialized against readers.
    */
    pthread_rwlock_rdlock((pthread_rwlock_t *)&mgr->lock);

    ParserSnapshot *snap = atomic_load_explicit(
        &mgr->active_snap, memory_order_acquire);
    if (snap != NULL) {
        snapshot_acquire(snap);
    }

    pthread_rwlock_unlock((pthread_rwlock_t *)&mgr->lock);
    return snap;
}

/* ================================================================== */
/*  Plugin enumeration and introspection                               */
/* ================================================================== */

static void fill_plugin_info(const ParserManager *mgr,
                             const PluginEntry *e,
                             LimePluginInfo *info) {
    info->handle = e->handle;
    info->name = (e->plugin && e->plugin->get_name)
                     ? e->plugin->get_name() : NULL;
    info->version = (e->plugin && e->plugin->get_version)
                        ? e->plugin->get_version()
                        : (LimePluginVersion){0, 0, 0};
    info->capabilities = (e->plugin && e->plugin->get_capabilities)
                             ? e->plugin->get_capabilities() : 0;
    info->is_active = (e->handle == mgr->active_handle);
    info->is_dynamic = e->is_dynamic;
}

ParserManagerStatus parser_manager_get_plugin_info(const ParserManager *mgr,
                                                   LimePluginHandle handle,
                                                   LimePluginInfo *info) {
    if (mgr == NULL || info == NULL || handle == LIME_PLUGIN_HANDLE_INVALID) {
        return PM_ERR_INVALID_ARG;
    }

    pthread_rwlock_rdlock((pthread_rwlock_t *)&mgr->lock);

    PluginEntry *e = NULL;
    for (uint32_t i = 0; i < mgr->count; i++) {
        if (((ParserManager *)mgr)->entries[i].handle == handle) {
            e = &((ParserManager *)mgr)->entries[i];
            break;
        }
    }

    if (e == NULL) {
        pthread_rwlock_unlock((pthread_rwlock_t *)&mgr->lock);
        return PM_ERR_PLUGIN_NOT_FOUND;
    }

    fill_plugin_info(mgr, e, info);

    pthread_rwlock_unlock((pthread_rwlock_t *)&mgr->lock);
    return PM_OK;
}

ParserManagerStatus parser_manager_list_plugins(const ParserManager *mgr,
                                                LimePluginInfo *infos,
                                                uint32_t max_count,
                                                uint32_t *actual_count) {
    if (mgr == NULL || infos == NULL || actual_count == NULL) {
        return PM_ERR_INVALID_ARG;
    }

    pthread_rwlock_rdlock((pthread_rwlock_t *)&mgr->lock);

    uint32_t to_copy = mgr->count;
    if (to_copy > max_count) to_copy = max_count;

    for (uint32_t i = 0; i < to_copy; i++) {
        fill_plugin_info(mgr, &((ParserManager *)mgr)->entries[i], &infos[i]);
    }

    *actual_count = mgr->count;

    pthread_rwlock_unlock((pthread_rwlock_t *)&mgr->lock);
    return PM_OK;
}

LimePluginHandle parser_manager_find_by_name(const ParserManager *mgr,
                                             const char *name) {
    if (mgr == NULL || name == NULL) return LIME_PLUGIN_HANDLE_INVALID;

    pthread_rwlock_rdlock((pthread_rwlock_t *)&mgr->lock);

    PluginEntry *e = find_entry_by_name((ParserManager *)mgr, name);
    LimePluginHandle h = (e != NULL) ? e->handle : LIME_PLUGIN_HANDLE_INVALID;

    pthread_rwlock_unlock((pthread_rwlock_t *)&mgr->lock);
    return h;
}

uint32_t parser_manager_plugin_count(const ParserManager *mgr) {
    if (mgr == NULL) return 0;

    pthread_rwlock_rdlock((pthread_rwlock_t *)&mgr->lock);
    uint32_t n = mgr->count;
    pthread_rwlock_unlock((pthread_rwlock_t *)&mgr->lock);

    return n;
}

/* ================================================================== */
/*  Hot-swap support                                                   */
/* ================================================================== */

ParserManagerStatus parser_manager_hot_swap(ParserManager *mgr,
                                            LimePluginHandle new_handle,
                                            const char *grammar_file) {
    /*
    ** hot_swap has the same semantics as set_active but is explicitly
    ** documented to be atomic.  Because set_active already uses an
    ** atomic exchange for the snapshot pointer, the implementation is
    ** identical.
    */
    return parser_manager_set_active(mgr, new_handle, grammar_file);
}

/* ================================================================== */
/*  Version compatibility utilities                                    */
/* ================================================================== */

int lime_plugin_version_compare(LimePluginVersion a, LimePluginVersion b) {
    if (a.major != b.major) return (a.major > b.major) ? 1 : -1;
    if (a.minor != b.minor) return (a.minor > b.minor) ? 1 : -1;
    if (a.patch != b.patch) return (a.patch > b.patch) ? 1 : -1;
    return 0;
}

bool lime_plugin_version_satisfies(LimePluginVersion actual,
                                   LimePluginVersion required) {
    return lime_plugin_version_compare(actual, required) >= 0;
}

char *lime_plugin_version_string(LimePluginVersion v, char *buf, size_t buflen) {
    if (buf == NULL || buflen == 0) return buf;
    snprintf(buf, buflen, "%u.%u.%u",
             (unsigned)v.major, (unsigned)v.minor, (unsigned)v.patch);
    return buf;
}
