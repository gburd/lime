/*
** Extension system implementation.
**
** Implements the internal API declared in src/extension.h.  The registry
** uses a dynamic array protected by a pthread_rwlock so lookups can
** proceed concurrently while mutations serialize.
*/
#include "extension.h"
#include "parser.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                    */
/* ------------------------------------------------------------------ */

#define INITIAL_CAPACITY 16

static char *dup_string(const char *s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy != NULL) memcpy(copy, s, len);
    return copy;
}

/*
** Find an extension by ID.  Caller must hold at least a read lock.
*/
static Extension *find_entry(ExtensionRegistry *reg, ExtensionID id) {
    if (reg == NULL || id == 0) return NULL;
    for (uint32_t i = 0; i < reg->count; i++) {
        if (reg->extensions[i].id == id) return &reg->extensions[i];
    }
    return NULL;
}

static bool grow_registry(ExtensionRegistry *reg) {
    uint32_t new_cap = reg->capacity * 2;
    if (new_cap == 0) new_cap = INITIAL_CAPACITY;
    Extension *p = realloc(reg->extensions, new_cap * sizeof(Extension));
    if (p == NULL) return false;
    memset(&p[reg->capacity], 0,
           (new_cap - reg->capacity) * sizeof(Extension));
    reg->extensions = p;
    reg->capacity = new_cap;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Registry lifecycle                                                 */
/* ------------------------------------------------------------------ */

ExtensionRegistry *create_extension_registry(void) {
    ExtensionRegistry *reg = calloc(1, sizeof(ExtensionRegistry));
    if (reg == NULL) return NULL;

    if (pthread_rwlock_init(&reg->lock, NULL) != 0) {
        free(reg);
        return NULL;
    }

    reg->extensions = calloc(INITIAL_CAPACITY, sizeof(Extension));
    if (reg->extensions == NULL) {
        pthread_rwlock_destroy(&reg->lock);
        free(reg);
        return NULL;
    }
    reg->capacity = INITIAL_CAPACITY;
    reg->count = 0;
    reg->next_id = 1;  /* IDs start at 1; 0 means "base / none" */
    return reg;
}

void destroy_extension_registry(ExtensionRegistry *reg) {
    if (reg == NULL) return;

    pthread_rwlock_wrlock(&reg->lock);

    for (uint32_t i = 0; i < reg->count; i++) {
        Extension *e = &reg->extensions[i];
        if (e->state == EXT_LOADED && e->on_unload != NULL) {
            e->on_unload(e->user_data);
        }
        free(e->name);
        free(e->version);
        /* modifications array is owned by the extension (freed in on_unload) */
    }

    free(reg->extensions);
    reg->extensions = NULL;
    reg->count = 0;
    reg->capacity = 0;

    pthread_rwlock_unlock(&reg->lock);
    pthread_rwlock_destroy(&reg->lock);
    free(reg);
}

/* ------------------------------------------------------------------ */
/*  Registration                                                       */
/* ------------------------------------------------------------------ */

bool register_extension(ExtensionRegistry *reg,
                        const ExtensionInfo *info,
                        ExtensionID *id_out) {
    if (reg == NULL || info == NULL || id_out == NULL) return false;
    if (info->name == NULL || info->get_modifications == NULL) return false;

    pthread_rwlock_wrlock(&reg->lock);

    /* Check for duplicate name */
    for (uint32_t i = 0; i < reg->count; i++) {
        if (reg->extensions[i].name != NULL &&
            strcmp(reg->extensions[i].name, info->name) == 0) {
            pthread_rwlock_unlock(&reg->lock);
            return false;
        }
    }

    /* Grow if needed */
    if (reg->count >= reg->capacity) {
        if (!grow_registry(reg)) {
            pthread_rwlock_unlock(&reg->lock);
            return false;
        }
    }

    uint32_t slot = reg->count;
    ExtensionID id = reg->next_id++;

    Extension *e = &reg->extensions[slot];
    memset(e, 0, sizeof(*e));
    e->id = id;
    e->name = dup_string(info->name);
    e->version = dup_string(info->version);
    e->state = EXT_REGISTERED;
    e->get_modifications = info->get_modifications;
    e->on_conflict = info->on_conflict;
    e->on_unload = info->on_unload;
    e->user_data = info->user_data;
    e->modifications = NULL;
    e->nmodifications = 0;

    if (e->name == NULL) {
        /* Allocation failure -- roll back */
        free(e->version);
        memset(e, 0, sizeof(*e));
        pthread_rwlock_unlock(&reg->lock);
        return false;
    }

    reg->count++;
    *id_out = id;

    pthread_rwlock_unlock(&reg->lock);
    return true;
}

/* ------------------------------------------------------------------ */
/*  Load / unload                                                      */
/* ------------------------------------------------------------------ */

bool load_extension(ExtensionRegistry *reg,
                    ExtensionID id,
                    const struct ParserSnapshot *base_snapshot,
                    char **error) {
    if (reg == NULL) return false;
    if (error != NULL) *error = NULL;

    pthread_rwlock_wrlock(&reg->lock);
    Extension *e = find_entry(reg, id);
    if (e == NULL) {
        pthread_rwlock_unlock(&reg->lock);
        if (error != NULL) *error = dup_string("extension not found");
        return false;
    }
    if (e->state != EXT_REGISTERED && e->state != EXT_UNLOADED) {
        pthread_rwlock_unlock(&reg->lock);
        if (error != NULL) *error = dup_string("extension not in loadable state");
        return false;
    }

    /* Call get_modifications to collect the extension's grammar changes */
    GrammarModification *mods = NULL;
    uint32_t nmods = 0;
    bool ok = e->get_modifications(e->user_data, base_snapshot, &mods, &nmods);
    if (!ok) {
        e->state = EXT_ERROR;
        pthread_rwlock_unlock(&reg->lock);
        if (error != NULL) *error = dup_string("get_modifications failed");
        return false;
    }

    e->modifications = mods;
    e->nmodifications = nmods;
    e->state = EXT_LOADED;

    pthread_rwlock_unlock(&reg->lock);
    return true;
}

bool unload_extension(ExtensionRegistry *reg, ExtensionID id) {
    if (reg == NULL) return false;

    pthread_rwlock_wrlock(&reg->lock);
    Extension *e = find_entry(reg, id);
    if (e == NULL) {
        pthread_rwlock_unlock(&reg->lock);
        return false;
    }
    if (e->state != EXT_LOADED) {
        pthread_rwlock_unlock(&reg->lock);
        return false;
    }

    if (e->on_unload != NULL) {
        e->on_unload(e->user_data);
    }

    e->modifications = NULL;
    e->nmodifications = 0;
    e->state = EXT_UNLOADED;

    pthread_rwlock_unlock(&reg->lock);
    return true;
}

/* ------------------------------------------------------------------ */
/*  Lookups                                                            */
/* ------------------------------------------------------------------ */

const Extension *find_extension(ExtensionRegistry *reg, ExtensionID id) {
    if (reg == NULL) return NULL;
    /* Note: caller should hold a lock, but for simple lookups we take one */
    pthread_rwlock_rdlock(&reg->lock);
    Extension *e = find_entry(reg, id);
    pthread_rwlock_unlock(&reg->lock);
    return e;
}

uint32_t get_loaded_extension_count(ExtensionRegistry *reg) {
    if (reg == NULL) return 0;

    pthread_rwlock_rdlock(&reg->lock);
    uint32_t loaded = 0;
    for (uint32_t i = 0; i < reg->count; i++) {
        if (reg->extensions[i].state == EXT_LOADED) loaded++;
    }
    pthread_rwlock_unlock(&reg->lock);
    return loaded;
}

/* ------------------------------------------------------------------ */
/*  Public lemon_extension_* wrappers (declared in parser.h)           */
/* ------------------------------------------------------------------ */

/*
** Global registry instance used by the public API.
*/
static ExtensionRegistry *g_registry = NULL;

bool lemon_extension_registry_init(void) {
    if (g_registry != NULL) return true;
    g_registry = create_extension_registry();
    return g_registry != NULL;
}

void lemon_extension_registry_destroy(void) {
    if (g_registry == NULL) return;
    destroy_extension_registry(g_registry);
    g_registry = NULL;
}
