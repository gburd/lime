/*
** Priority-based disambiguation strategy.
**
** Resolves conflicts between extensions by comparing a numeric
** priority value associated with each extension.  The extension
** with the highest priority wins (higher numeric value = preferred,
** matching the LimeContext.priority convention from conflict.h).
**
** Priority sources (checked in order):
**   1. The LimeContext.priority field from the ConflictPoint's
**      contexts array (set by the conflict detection system).
**   2. If an extension provides a PriorityMetadata struct via
**      user_data (identified by magic number), that explicit
**      priority is used.
**   3. Fall back to registration order: lower ExtensionID = higher
**      priority (registered first).
**
** This strategy is deterministic and always produces confidence = 1.0
** when a winner is found.
*/
#include "disambiguation.h"
#include "extension.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Priority metadata                                                   */
/* ------------------------------------------------------------------ */

/*
** Extensions may pass a pointer to this struct as their user_data
** to specify an explicit priority.  The magic field must be set to
** PRIORITY_METADATA_MAGIC for the strategy to recognize it.
*/
#define PRIORITY_METADATA_MAGIC 0x50524F49u /* "PROI" */

typedef struct PriorityMetadata {
    uint32_t magic;   /* Must be PRIORITY_METADATA_MAGIC            */
    int32_t priority; /* Higher value = higher priority             */
} PriorityMetadata;

/* ------------------------------------------------------------------ */
/*  Per-extension priority entry                                        */
/* ------------------------------------------------------------------ */

typedef struct PriorityEntry {
    uint32_t extension_id;
    int32_t priority;
} PriorityEntry;

/* ------------------------------------------------------------------ */
/*  Strategy context                                                    */
/* ------------------------------------------------------------------ */

typedef struct PriorityContext {
    PriorityEntry *entries; /* Array of extension priorities        */
    uint32_t nentries;      /* Number of entries                    */
    uint32_t capacity;      /* Allocated slots                      */
} PriorityContext;

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static char *dup_str(const char *s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy != NULL) memcpy(copy, s, len);
    return copy;
}

/*
** Extract the priority value for an extension.
**
** Checks whether user_data points to a PriorityMetadata struct
** (by verifying the magic number).  If so, returns the explicit
** priority.  Otherwise, uses a priority derived from the ExtensionID
** (lower ID = higher priority = larger derived value).
*/
static int32_t extract_priority(const Extension *ext) {
    if (ext == NULL) return INT32_MIN;

    /* Check for explicit priority metadata */
    if (ext->user_data != NULL) {
        const PriorityMetadata *pm = (const PriorityMetadata *)ext->user_data;
        if (pm->magic == PRIORITY_METADATA_MAGIC) {
            return pm->priority;
        }
    }

    /* Fall back to registration order: lower ID = higher priority.
    ** We invert so that id=1 gets highest numeric priority. */
    return (int32_t)(10000 - ext->id);
}

/*
** Look up the priority for a given extension ID.
** Returns INT32_MIN if the extension is not in the priority table.
*/
static int32_t lookup_priority(const PriorityContext *pc, uint32_t ext_id) {
    if (pc == NULL) return INT32_MIN;
    for (uint32_t i = 0; i < pc->nentries; i++) {
        if (pc->entries[i].extension_id == ext_id) {
            return pc->entries[i].priority;
        }
    }
    /* Unknown extension -- treat as lowest priority */
    return INT32_MIN;
}

/* ------------------------------------------------------------------ */
/*  VTable callbacks                                                    */
/* ------------------------------------------------------------------ */

static void *priority_init(const Extension *const *extensions, uint32_t nextensions) {
    PriorityContext *pc = calloc(1, sizeof(PriorityContext));
    if (pc == NULL) return NULL;

    if (nextensions == 0) {
        /* No extensions yet -- valid empty context */
        return pc;
    }

    pc->entries = calloc(nextensions, sizeof(PriorityEntry));
    if (pc->entries == NULL) {
        free(pc);
        return NULL;
    }
    pc->capacity = nextensions;

    for (uint32_t i = 0; i < nextensions; i++) {
        const Extension *ext = extensions[i];
        if (ext == NULL) continue;

        pc->entries[pc->nentries].extension_id = ext->id;
        pc->entries[pc->nentries].priority = extract_priority(ext);
        pc->nentries++;
    }

    return pc;
}

static bool priority_resolve(void *strategy_context, const ConflictPoint *conflict,
                             struct ParseContext *parse_ctx, int lookahead,
                             StrategyResult *result) {
    (void)parse_ctx;
    (void)lookahead;

    PriorityContext *pc = (PriorityContext *)strategy_context;
    if (pc == NULL || conflict == NULL || result == NULL) return false;

    strategy_result_init(result);

    if (conflict->ncontexts <= 0 || conflict->contexts == NULL) {
        /* No contexts available -- cannot resolve */
        return false;
    }

    /* Check whether any context has a non-zero priority set by the
    ** conflict detection system.  If all are zero, use our internal
    ** priority table (built from extension metadata / registration order). */
    bool has_explicit_priority = false;
    for (int i = 0; i < conflict->ncontexts; i++) {
        if (conflict->contexts[i].priority != 0) {
            has_explicit_priority = true;
            break;
        }
    }

    int best_idx = 0;
    int32_t best_prio;

    if (has_explicit_priority) {
        /* Use the priority field from the LimeContext directly */
        best_prio = conflict->contexts[0].priority;
        for (int i = 1; i < conflict->ncontexts; i++) {
            int32_t p = conflict->contexts[i].priority;
            if (p > best_prio) {
                best_prio = p;
                best_idx = i;
            } else if (p == best_prio) {
                /* Tie-break: prefer lower ext_id (registered first) */
                if (conflict->contexts[i].ext_id < conflict->contexts[best_idx].ext_id) {
                    best_idx = i;
                }
            }
        }
    } else {
        /* Fall back to our internal priority table */
        best_prio = lookup_priority(pc, conflict->contexts[0].ext_id);
        for (int i = 1; i < conflict->ncontexts; i++) {
            int32_t p = lookup_priority(pc, conflict->contexts[i].ext_id);
            if (p > best_prio) {
                best_prio = p;
                best_idx = i;
            } else if (p == best_prio) {
                if (conflict->contexts[i].ext_id < conflict->contexts[best_idx].ext_id) {
                    best_idx = i;
                }
            }
        }
    }

    const LimeContext *winner = &conflict->contexts[best_idx];

    result->winning_contexts = malloc(sizeof(LimeContext));
    if (result->winning_contexts == NULL) return false;

    result->winning_contexts[0] = *winner;
    result->nwinners = 1;
    result->confidence = 1.0f;

    char buf[256];
    snprintf(buf, sizeof(buf), "priority: ext %u (prio %d, grammar '%s') wins among %d candidates",
             (unsigned)winner->ext_id, (int)best_prio,
             winner->grammar_name ? winner->grammar_name : "(unknown)", conflict->ncontexts);
    result->explanation = dup_str(buf);
    return true;
}

static void priority_update(void *strategy_context, struct ExtensionRegistry *registry,
                            bool success) {
    /* Priority strategy is static -- no learning needed */
    (void)strategy_context;
    (void)registry;
    (void)success;
}

static void priority_destroy(void *strategy_context) {
    PriorityContext *pc = (PriorityContext *)strategy_context;
    if (pc == NULL) return;
    free(pc->entries);
    free(pc);
}

/* ------------------------------------------------------------------ */
/*  Exported vtable                                                     */
/* ------------------------------------------------------------------ */

const DisambiguationStrategyVTable strategy_priority_vtable = {
    .init = priority_init,
    .resolve = priority_resolve,
    .update = priority_update,
    .destroy = priority_destroy,
};
