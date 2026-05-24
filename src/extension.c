/*
** Extension system implementation.
**
** Implements the internal API declared in src/extension.h.  The registry
** uses a dynamic array protected by a pthread_rwlock so lookups can
** proceed concurrently while mutations serialize.
*/
#include "extension.h"
#include "parser.h"

#include <stdio.h>
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
    memset(&p[reg->capacity], 0, (new_cap - reg->capacity) * sizeof(Extension));
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
    reg->next_id = 1; /* IDs start at 1; 0 means "base / none" */
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

    LIME_RWLOCK_WRUNLOCK(&reg->lock);
    pthread_rwlock_destroy(&reg->lock);
    free(reg);
}

/* ------------------------------------------------------------------ */
/*  Registration                                                       */
/* ------------------------------------------------------------------ */

bool register_extension(ExtensionRegistry *reg, const ExtensionInfo *info, ExtensionID *id_out) {
    if (reg == NULL || info == NULL || id_out == NULL) return false;
    if (info->name == NULL || info->get_modifications == NULL) return false;

    pthread_rwlock_wrlock(&reg->lock);

    /* Check for duplicate name */
    for (uint32_t i = 0; i < reg->count; i++) {
        if (reg->extensions[i].name != NULL && strcmp(reg->extensions[i].name, info->name) == 0) {
            LIME_RWLOCK_WRUNLOCK(&reg->lock);
            return false;
        }
    }

    /* Grow if needed */
    if (reg->count >= reg->capacity) {
        if (!grow_registry(reg)) {
            LIME_RWLOCK_WRUNLOCK(&reg->lock);
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
        LIME_RWLOCK_WRUNLOCK(&reg->lock);
        return false;
    }

    reg->count++;
    *id_out = id;

    LIME_RWLOCK_WRUNLOCK(&reg->lock);
    return true;
}

/* ------------------------------------------------------------------ */
/*  Load / unload                                                      */
/* ------------------------------------------------------------------ */

bool load_extension(ExtensionRegistry *reg, ExtensionID id,
                    const struct ParserSnapshot *base_snapshot, char **error) {
    if (reg == NULL) return false;
    if (error != NULL) *error = NULL;

    pthread_rwlock_wrlock(&reg->lock);
    Extension *e = find_entry(reg, id);
    if (e == NULL) {
        LIME_RWLOCK_WRUNLOCK(&reg->lock);
        if (error != NULL) *error = dup_string("extension not found");
        return false;
    }
    if (e->state != EXT_REGISTERED && e->state != EXT_UNLOADED) {
        LIME_RWLOCK_WRUNLOCK(&reg->lock);
        if (error != NULL) *error = dup_string("extension not in loadable state");
        return false;
    }

    /* Call get_modifications to collect the extension's grammar changes */
    GrammarModification *mods = NULL;
    uint32_t nmods = 0;
    bool ok = e->get_modifications(e->user_data, base_snapshot, &mods, &nmods);
    if (!ok) {
        e->state = EXT_ERROR;
        LIME_RWLOCK_WRUNLOCK(&reg->lock);
        if (error != NULL) *error = dup_string("get_modifications failed");
        return false;
    }

    e->modifications = mods;
    e->nmodifications = nmods;
    e->state = EXT_LOADED;

    LIME_RWLOCK_WRUNLOCK(&reg->lock);
    return true;
}

bool unload_extension(ExtensionRegistry *reg, ExtensionID id) {
    if (reg == NULL) return false;

    pthread_rwlock_wrlock(&reg->lock);
    Extension *e = find_entry(reg, id);
    if (e == NULL) {
        LIME_RWLOCK_WRUNLOCK(&reg->lock);
        return false;
    }
    if (e->state != EXT_LOADED) {
        LIME_RWLOCK_WRUNLOCK(&reg->lock);
        return false;
    }

    if (e->on_unload != NULL) {
        e->on_unload(e->user_data);
    }

    e->modifications = NULL;
    e->nmodifications = 0;
    e->state = EXT_UNLOADED;

    LIME_RWLOCK_WRUNLOCK(&reg->lock);
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
    LIME_RWLOCK_RDUNLOCK(&reg->lock);
    return e;
}

uint32_t get_loaded_extension_count(ExtensionRegistry *reg) {
    if (reg == NULL) return 0;

    pthread_rwlock_rdlock(&reg->lock);
    uint32_t loaded = 0;
    for (uint32_t i = 0; i < reg->count; i++) {
        if (reg->extensions[i].state == EXT_LOADED) loaded++;
    }
    LIME_RWLOCK_RDUNLOCK(&reg->lock);
    return loaded;
}

/* ------------------------------------------------------------------ */
/*  Snapshot publishing                                                */
/* ------------------------------------------------------------------ */

#include "snapshot_modify.h"
#include "mod_serialize.h"

bool publish_modified_snapshot(ExtensionRegistry *reg, const struct ParserSnapshot *base,
                               struct ParserSnapshot **out_snap,
                               struct ConflictSet **out_conflicts, char **error) {
    if (out_snap == NULL) {
        if (error) *error = strdup("out_snap is required");
        return false;
    }
    *out_snap = NULL;
    if (out_conflicts) *out_conflicts = NULL;
    if (error) *error = NULL;

    if (reg == NULL) {
        if (error) *error = strdup("registry is required");
        return false;
    }

    /* Collect every loaded extension's modifications under the lock,
    ** along with a parallel ext_id_per_mod[] array so we can resolve
    ** conflicts back to the owning extension when an on_conflict
    ** callback is needed. */
    pthread_rwlock_rdlock(&reg->lock);
    uint32_t total = 0;
    for (uint32_t i = 0; i < reg->count; i++) {
        if (reg->extensions[i].state == EXT_LOADED) {
            total += reg->extensions[i].nmodifications;
        }
    }

    GrammarModification *combined = NULL;
    ExtensionID *ext_id_per_mod = NULL;
    if (total > 0) {
        combined = malloc(total * sizeof(GrammarModification));
        ext_id_per_mod = malloc(total * sizeof(ExtensionID));
        if (combined == NULL || ext_id_per_mod == NULL) {
            free(combined);
            free(ext_id_per_mod);
            LIME_RWLOCK_RDUNLOCK(&reg->lock);
            if (error) *error = strdup("publish_modified_snapshot: out of memory");
            return false;
        }
        uint32_t k = 0;
        for (uint32_t i = 0; i < reg->count; i++) {
            const Extension *e = &reg->extensions[i];
            if (e->state != EXT_LOADED) continue;
            for (uint32_t m = 0; m < e->nmodifications; m++) {
                combined[k] = e->modifications[m];
                ext_id_per_mod[k] = e->id;
                k++;
            }
        }
    }
    LIME_RWLOCK_RDUNLOCK(&reg->lock);

    /* Detect and resolve conflicts ourselves so we have access to the
    ** ext_id_per_mod[] mapping when invoking on_conflict callbacks. */
    ConflictSet *cs = conflict_set_create();
    if (cs == NULL) {
        free(combined);
        free(ext_id_per_mod);
        if (error) *error = strdup("publish_modified_snapshot: conflict_set_create failed");
        return false;
    }
    detect_conflicts(combined, total, cs);

    /* Walk every conflict and ask the owning extensions to resolve. */
    for (uint32_t i = 0; i < cs->count; i++) {
        Conflict *c = &cs->conflicts[i];
        if (c->resolved) continue;

        ExtensionID id_a = (c->mod_index_a < total) ? ext_id_per_mod[c->mod_index_a] : 0;
        ExtensionID id_b = (c->mod_index_b < total) ? ext_id_per_mod[c->mod_index_b] : 0;
        c->ext_id_a = id_a;
        c->ext_id_b = id_b;

        const Extension *ea = find_extension(reg, id_a);
        if (ea && ea->on_conflict) {
            ConflictInfo info = {
                .existing_ext = id_a,
                .new_ext = id_b,
                .existing_mod = &combined[c->mod_index_a],
                .new_mod = &combined[c->mod_index_b],
            };
            if (ea->on_conflict(ea->user_data, &info) != CONFLICT_UNRESOLVED) {
                c->resolved = true;
                continue;
            }
        }

        const Extension *eb = find_extension(reg, id_b);
        if (eb && eb->on_conflict) {
            ConflictInfo info = {
                .existing_ext = id_a,
                .new_ext = id_b,
                .existing_mod = &combined[c->mod_index_a],
                .new_mod = &combined[c->mod_index_b],
            };
            if (eb->on_conflict(eb->user_data, &info) != CONFLICT_UNRESOLVED) {
                c->resolved = true;
                continue;
            }
        }
    }

    uint32_t unresolved = conflict_set_unresolved_count(cs);
    if (unresolved > 0) {
        if (out_conflicts != NULL) {
            *out_conflicts = cs;
        } else {
            conflict_set_destroy(cs);
        }
        if (error) {
            char buf[128];
            snprintf(buf, sizeof(buf), "%u unresolved conflict(s) detected", unresolved);
            *error = strdup(buf);
        }
        free(combined);
        free(ext_id_per_mod);
        return false;
    }
    conflict_set_destroy(cs);

    /* Build the modified snapshot.
    **
    ** Two paths:
    **
    **   A) Subprocess rebuild (real LALR(1) reconstruction).  When
    **      the base snapshot carries its original grammar source
    **      (set by `lime -n`), we can serialise the modifications
    **      to a .y fragment via lime_modifications_to_grammar_text(),
    **      concatenate the two, and rerun the lime + cc subprocess
    **      pipeline on the merged grammar.  The resulting snapshot
    **      has fully recomputed action tables and the new tokens /
    **      rules are reachable through parse_token().
    **
    **   B) Metadata-only rebuild (legacy / no-source fallback).
    **      When grammar_source is unavailable we fall back to
    **      create_modified_snapshot(), which clones the snapshot
    **      and applies the modifications as metadata (counters
    **      grow, rule-info arrays extend) but does not re-derive
    **      action tables.  This keeps existing test paths working
    **      and is honest about what it can do.
    **
    ** Path A activates whenever the base has source AND the
    ** environment can run the subprocess pipeline (lime + cc on
    ** PATH).  Otherwise B.  When A fails for a reason other than
    ** missing tools (e.g. the merged grammar contains a real
    ** parser-construction conflict), we surface the error rather
    ** than silently downgrading to B. */
    ParserSnapshot *new_snap = NULL;
    char *cm_err = NULL;

    if (base != NULL && base->grammar_source != NULL && base->grammar_source_len > 0) {
        uint32_t skipped = 0;
        char *frag_err = NULL;
        char *frag = lime_modifications_to_grammar_text(combined, total, &skipped, &frag_err);
        if (frag != NULL) {
            size_t base_len = base->grammar_source_len;
            size_t frag_len = strlen(frag);
            char *merged = malloc(base_len + 1 + frag_len + 1);
            if (merged != NULL) {
                memcpy(merged, base->grammar_source, base_len);
                merged[base_len] = '\n';
                memcpy(merged + base_len + 1, frag, frag_len);
                merged[base_len + 1 + frag_len] = '\0';

                new_snap = lime_compile_grammar_text(merged, base_len + 1 + frag_len, &cm_err);
                free(merged);
            } else {
                cm_err = strdup("publish_modified_snapshot: out of memory merging grammar");
            }
            free(frag);
        } else {
            cm_err = frag_err ? frag_err
                              : strdup("lime_modifications_to_grammar_text returned NULL");
        }
    }

    if (new_snap == NULL) {
        /* Fall back to the metadata-only path. */
        ConflictSet *cs_inner = NULL;
        free(cm_err);
        cm_err = NULL;
        ModifyResult mr =
            create_modified_snapshot(base, combined, total, NULL, &new_snap, &cs_inner, &cm_err);
        free(combined);
        free(ext_id_per_mod);

        if (mr != MODIFY_OK) {
            if (error) {
                *error = cm_err ? cm_err : strdup("create_modified_snapshot failed");
            } else {
                free(cm_err);
            }
            if (out_conflicts && cs_inner != NULL) {
                *out_conflicts = cs_inner;
            } else if (cs_inner != NULL) {
                conflict_set_destroy(cs_inner);
            }
            return false;
        }
        free(cm_err);
        if (cs_inner != NULL) conflict_set_destroy(cs_inner);
    } else {
        free(combined);
        free(ext_id_per_mod);
        free(cm_err);
    }

    *out_snap = new_snap;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Public lemon_extension_* wrappers (declared in parser.h)           */
/* ------------------------------------------------------------------ */

/*
** Global registry instance used by the public API.
*/
static ExtensionRegistry *g_registry = NULL;

bool lime_extension_registry_init(void) {
    if (g_registry != NULL) return true;
    g_registry = create_extension_registry();
    return g_registry != NULL;
}

void lime_extension_registry_destroy(void) {
    if (g_registry == NULL) return;
    destroy_extension_registry(g_registry);
    g_registry = NULL;
}
