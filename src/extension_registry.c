/*
** Extension registry implementation.
**
** Manages grammar extensions with rich metadata, hash-table-backed
** O(1) lookups, dependency validation via topological sort, and
** conflict-graph checks.
**
** Internal data structures:
**   - A dynamic array of RegistryEntry structs (each owns a deep copy
**     of the GrammarExtensionMetadata plus resolved string arrays).
**   - A separate hash table mapping extension names to array indices
**     for O(1) lookups.
**   - Topological sort uses Kahn's algorithm with in-degree tracking.
*/
#include "extension_registry.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/*  Internal constants                                                 */
/* ------------------------------------------------------------------ */

#define INITIAL_CAPACITY 16
#define HASH_TABLE_FACTOR 2 /* hash table size = capacity * factor */

/* ------------------------------------------------------------------ */
/*  String helpers                                                     */
/* ------------------------------------------------------------------ */

static char *reg_strdup(const char *s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy != NULL) memcpy(copy, s, len);
    return copy;
}

/*
** Duplicate a NULL-terminated array of strings.  Returns a freshly
** allocated array where each element is a strdup'd copy.  Returns NULL
** if the input is NULL.  The output array is also NULL-terminated.
*/
static char **dup_string_array(const char **src) {
    if (src == NULL) return NULL;

    uint32_t count = 0;
    while (src[count] != NULL)
        count++;

    char **dst = calloc(count + 1, sizeof(char *));
    if (dst == NULL) return NULL;

    for (uint32_t i = 0; i < count; i++) {
        dst[i] = reg_strdup(src[i]);
        if (dst[i] == NULL) {
            for (uint32_t j = 0; j < i; j++)
                free(dst[j]);
            free(dst);
            return NULL;
        }
    }
    dst[count] = NULL;
    return dst;
}

static void free_string_array(char **arr) {
    if (arr == NULL) return;
    for (uint32_t i = 0; arr[i] != NULL; i++) {
        free(arr[i]);
    }
    free(arr);
}

static uint32_t string_array_len(const char **arr) {
    if (arr == NULL) return 0;
    uint32_t n = 0;
    while (arr[n] != NULL)
        n++;
    return n;
}

/* ------------------------------------------------------------------ */
/*  Hash table (open addressing, linear probing)                       */
/* ------------------------------------------------------------------ */

typedef struct HashEntry {
    char *key;      /* Borrowed pointer to RegistryEntry.meta.name */
    uint32_t index; /* Index into the entries array                */
    bool occupied;
} HashEntry;

typedef struct HashTable {
    HashEntry *buckets;
    uint32_t size;  /* Number of buckets (always power of 2)       */
    uint32_t count; /* Number of occupied entries                   */
} HashTable;

static uint32_t hash_string(const char *s) {
    /* FNV-1a 32-bit */
    uint32_t h = 2166136261u;
    for (const char *p = s; *p != '\0'; p++) {
        h ^= (uint8_t)*p;
        h *= 16777619u;
    }
    return h;
}

static bool ht_init(HashTable *ht, uint32_t size) {
    /* Round up to power of 2 */
    uint32_t actual = 16;
    while (actual < size)
        actual *= 2;
    ht->buckets = calloc(actual, sizeof(HashEntry));
    if (ht->buckets == NULL) return false;
    ht->size = actual;
    ht->count = 0;
    return true;
}

static void ht_destroy(HashTable *ht) {
    free(ht->buckets);
    ht->buckets = NULL;
    ht->size = 0;
    ht->count = 0;
}

static bool ht_insert(HashTable *ht, const char *key, uint32_t index) {
    if (ht->count * 4 >= ht->size * 3) {
        /* Load factor > 75%: rehash */
        uint32_t new_size = ht->size * 2;
        HashEntry *old = ht->buckets;
        uint32_t old_size = ht->size;

        ht->buckets = calloc(new_size, sizeof(HashEntry));
        if (ht->buckets == NULL) {
            ht->buckets = old;
            return false;
        }
        ht->size = new_size;
        ht->count = 0;

        for (uint32_t i = 0; i < old_size; i++) {
            if (old[i].occupied) {
                ht_insert(ht, old[i].key, old[i].index);
            }
        }
        free(old);
    }

    uint32_t mask = ht->size - 1;
    uint32_t slot = hash_string(key) & mask;

    while (ht->buckets[slot].occupied) {
        if (strcmp(ht->buckets[slot].key, key) == 0) {
            /* Key already exists -- update index */
            ht->buckets[slot].index = index;
            return true;
        }
        slot = (slot + 1) & mask;
    }

    ht->buckets[slot].key = (char *)key; /* borrowed pointer */
    ht->buckets[slot].index = index;
    ht->buckets[slot].occupied = true;
    ht->count++;
    return true;
}

/*
** Look up a key.  Returns true and sets *index_out if found.
*/
static bool ht_find(const HashTable *ht, const char *key, uint32_t *index_out) {
    if (ht->count == 0) return false;

    uint32_t mask = ht->size - 1;
    uint32_t slot = hash_string(key) & mask;

    while (ht->buckets[slot].occupied) {
        if (strcmp(ht->buckets[slot].key, key) == 0) {
            if (index_out) *index_out = ht->buckets[slot].index;
            return true;
        }
        slot = (slot + 1) & mask;
    }
    return false;
}

/*
** Remove a key.  Uses backward-shift deletion to maintain probe chains.
*/
static bool ht_remove(HashTable *ht, const char *key) {
    if (ht->count == 0) return false;

    uint32_t mask = ht->size - 1;
    uint32_t slot = hash_string(key) & mask;

    while (ht->buckets[slot].occupied) {
        if (strcmp(ht->buckets[slot].key, key) == 0) {
            /* Found -- remove with backward shift */
            ht->buckets[slot].occupied = false;
            ht->buckets[slot].key = NULL;
            ht->count--;

            /* Reinsert subsequent entries in the same probe chain */
            uint32_t next = (slot + 1) & mask;
            while (ht->buckets[next].occupied) {
                char *rekey = ht->buckets[next].key;
                uint32_t reindex = ht->buckets[next].index;
                ht->buckets[next].occupied = false;
                ht->buckets[next].key = NULL;
                ht->count--;
                ht_insert(ht, rekey, reindex);
                next = (next + 1) & mask;
            }
            return true;
        }
        slot = (slot + 1) & mask;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/*  Registry entry (owns a deep copy of metadata)                      */
/* ------------------------------------------------------------------ */

typedef struct RegistryEntry {
    GrammarExtensionMetadata meta; /* Deep-copied metadata            */
    /* Owned copies of string arrays stored inside meta.requires and
    ** meta.conflicts_with point to these allocations.                  */
} RegistryEntry;

static void entry_destroy(RegistryEntry *entry) {
    free((char *)entry->meta.name);
    free((char *)entry->meta.version);
    free_string_array((char **)entry->meta.requires);
    free_string_array((char **)entry->meta.conflicts_with);
    /* modifications are borrowed, not owned by the registry */
    memset(entry, 0, sizeof(*entry));
}

/* ------------------------------------------------------------------ */
/*  Registry struct                                                    */
/* ------------------------------------------------------------------ */

struct ExtensionRegistry {
    RegistryEntry *entries;
    uint32_t count;
    uint32_t capacity;
    HashTable ht;
};

/* ------------------------------------------------------------------ */
/*  ExtensionOrder cleanup                                             */
/* ------------------------------------------------------------------ */

void extension_order_destroy(ExtensionOrder *order) {
    if (order == NULL) return;
    for (uint32_t i = 0; i < order->count; i++) {
        free(order->names[i]);
    }
    free(order->names);
    order->names = NULL;
    order->count = 0;
}

/* ------------------------------------------------------------------ */
/*  Create / destroy                                                   */
/* ------------------------------------------------------------------ */

ExtensionRegistry *extension_registry_create(void) {
    ExtensionRegistry *reg = calloc(1, sizeof(ExtensionRegistry));
    if (reg == NULL) return NULL;

    reg->entries = calloc(INITIAL_CAPACITY, sizeof(RegistryEntry));
    if (reg->entries == NULL) {
        free(reg);
        return NULL;
    }
    reg->capacity = INITIAL_CAPACITY;
    reg->count = 0;

    if (!ht_init(&reg->ht, INITIAL_CAPACITY * HASH_TABLE_FACTOR)) {
        free(reg->entries);
        free(reg);
        return NULL;
    }

    return reg;
}

void extension_registry_destroy(ExtensionRegistry *reg) {
    if (reg == NULL) return;

    for (uint32_t i = 0; i < reg->count; i++) {
        entry_destroy(&reg->entries[i]);
    }
    free(reg->entries);
    ht_destroy(&reg->ht);
    free(reg);
}

/* ------------------------------------------------------------------ */
/*  Register                                                           */
/* ------------------------------------------------------------------ */

bool extension_registry_register(ExtensionRegistry *reg, const GrammarExtensionMetadata *metadata) {
    if (reg == NULL || metadata == NULL) return false;
    if (metadata->name == NULL || metadata->name[0] == '\0') return false;

    /* Check for duplicate */
    if (ht_find(&reg->ht, metadata->name, NULL)) return false;

    /* Grow if needed */
    if (reg->count >= reg->capacity) {
        uint32_t new_cap = reg->capacity * 2;
        RegistryEntry *p = realloc(reg->entries, new_cap * sizeof(RegistryEntry));
        if (p == NULL) return false;
        memset(&p[reg->capacity], 0, (new_cap - reg->capacity) * sizeof(RegistryEntry));
        reg->entries = p;
        reg->capacity = new_cap;

        /* Rehash: keys are borrowed pointers into entries, which may have
        ** moved due to realloc. Rebuild the hash table. */
        ht_destroy(&reg->ht);
        if (!ht_init(&reg->ht, new_cap * HASH_TABLE_FACTOR)) return false;
        for (uint32_t i = 0; i < reg->count; i++) {
            ht_insert(&reg->ht, reg->entries[i].meta.name, i);
        }
    }

    /* Deep-copy metadata into the new entry */
    uint32_t slot = reg->count;
    RegistryEntry *entry = &reg->entries[slot];
    memset(entry, 0, sizeof(*entry));

    entry->meta.name = reg_strdup(metadata->name);
    entry->meta.version = reg_strdup(metadata->version);
    if (entry->meta.name == NULL) {
        free((char *)entry->meta.version);
        memset(entry, 0, sizeof(*entry));
        return false;
    }

    entry->meta.strategy = metadata->strategy;
    entry->meta.priority = metadata->priority;
    entry->meta.policy = metadata->policy;
    entry->meta.oracle = metadata->oracle;
    entry->meta.conflict_threshold = metadata->conflict_threshold;

    entry->meta.requires = (const char **)dup_string_array(metadata->requires);
    entry->meta.conflicts_with = (const char **)dup_string_array(metadata->conflicts_with);

    /* Shallow copy modifications (ownership stays with caller) */
    entry->meta.modifications = metadata->modifications;
    entry->meta.nmodifications = metadata->nmodifications;

    /* Insert into hash table */
    if (!ht_insert(&reg->ht, entry->meta.name, slot)) {
        entry_destroy(entry);
        return false;
    }

    reg->count++;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Find                                                               */
/* ------------------------------------------------------------------ */

const GrammarExtensionMetadata *extension_registry_find(ExtensionRegistry *reg, const char *name) {
    if (reg == NULL || name == NULL) return NULL;

    uint32_t index;
    if (!ht_find(&reg->ht, name, &index)) return NULL;
    return &reg->entries[index].meta;
}

/* ------------------------------------------------------------------ */
/*  Unregister                                                         */
/* ------------------------------------------------------------------ */

bool extension_registry_unregister(ExtensionRegistry *reg, const char *name) {
    if (reg == NULL || name == NULL) return false;

    uint32_t index;
    if (!ht_find(&reg->ht, name, &index)) return false;

    /* Remove from hash table first (before we move entries around) */
    ht_remove(&reg->ht, name);

    /* Destroy the entry */
    entry_destroy(&reg->entries[index]);

    /* Compact: move the last entry into the vacated slot */
    uint32_t last = reg->count - 1;
    if (index != last) {
        reg->entries[index] = reg->entries[last];
        memset(&reg->entries[last], 0, sizeof(RegistryEntry));

        /* Update hash table for the moved entry */
        ht_remove(&reg->ht, reg->entries[index].meta.name);
        ht_insert(&reg->ht, reg->entries[index].meta.name, index);
    }

    reg->count--;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Count                                                              */
/* ------------------------------------------------------------------ */

uint32_t extension_registry_count(const ExtensionRegistry *reg) {
    if (reg == NULL) return 0;
    return reg->count;
}

/* ------------------------------------------------------------------ */
/*  Foreach                                                            */
/* ------------------------------------------------------------------ */

void extension_registry_foreach(const ExtensionRegistry *reg, ExtensionVisitorFn visitor,
                                void *user_data) {
    if (reg == NULL || visitor == NULL) return;
    for (uint32_t i = 0; i < reg->count; i++) {
        if (!visitor(&reg->entries[i].meta, user_data)) break;
    }
}

/* ------------------------------------------------------------------ */
/*  Dependency checking: topological sort (Kahn's algorithm)           */
/* ------------------------------------------------------------------ */

/*
** Build an adjacency list and in-degree array from the dependency graph.
** An edge from A to B means "A requires B" (B must come before A).
**
** Returns false and sets *error_out on missing dependencies.
*/
typedef struct AdjList {
    uint32_t *neighbors; /* Flat array of neighbor indices          */
    uint32_t *offsets;   /* offsets[i] = start of neighbors for i   */
    uint32_t *in_degree; /* in_degree[i] = number of incoming edges */
    uint32_t n;          /* Number of nodes                         */
} AdjList;

static void adjlist_destroy(AdjList *adj) {
    free(adj->neighbors);
    free(adj->offsets);
    free(adj->in_degree);
    memset(adj, 0, sizeof(*adj));
}

static bool build_adjacency(const ExtensionRegistry *reg, AdjList *adj, char **error_out) {
    uint32_t n = reg->count;
    adj->n = n;

    /* First pass: count edges per node */
    uint32_t *out_degree = calloc(n, sizeof(uint32_t));
    adj->in_degree = calloc(n, sizeof(uint32_t));
    adj->offsets = calloc(n + 1, sizeof(uint32_t));

    if (out_degree == NULL || adj->in_degree == NULL || adj->offsets == NULL) {
        free(out_degree);
        adjlist_destroy(adj);
        if (error_out) *error_out = reg_strdup("allocation failure");
        return false;
    }

    uint32_t total_edges = 0;

    for (uint32_t i = 0; i < n; i++) {
        const GrammarExtensionMetadata *meta = &reg->entries[i].meta;
        uint32_t ndeps = string_array_len(meta->requires);
        for (uint32_t d = 0; d < ndeps; d++) {
            uint32_t dep_index;
            if (!ht_find(&reg->ht, meta->requires[d], &dep_index)) {
                /* Missing dependency */
                if (error_out) {
                    char buf[512];
                    snprintf(buf, sizeof(buf),
                             "extension '%s' requires '%s', which is not registered", meta->name,
                             meta->requires[d]);
                    *error_out = reg_strdup(buf);
                }
                free(out_degree);
                adjlist_destroy(adj);
                return false;
            }
            out_degree[i]++;
            adj->in_degree[dep_index]++; /* Wait -- edge direction:
                i requires dep_index, so the edge is dep_index -> i
                in topological order.  Actually, for topo sort we want
                "dep before dependent", so edge from dep to dependent.
                That means in_degree for i (the dependent) increases. */
            total_edges++;
        }
    }

    /* Fix in_degree: an edge "i requires dep_index" means dep_index must
    ** come before i, so the edge is dep_index -> i.  That means i's
    ** in-degree should increase, not dep_index's. Recalculate. */
    memset(adj->in_degree, 0, n * sizeof(uint32_t));
    for (uint32_t i = 0; i < n; i++) {
        const GrammarExtensionMetadata *meta = &reg->entries[i].meta;
        uint32_t ndeps = string_array_len(meta->requires);
        adj->in_degree[i] += ndeps; /* i has ndeps incoming edges */
    }

    /* Build offsets and neighbor arrays.  We store the adjacency list
    ** keyed by *source* of the edge (dep_index) so we can traverse
    ** "who depends on dep_index" during BFS. */

    /* Count outgoing edges for each node (how many nodes depend on it) */
    uint32_t *out_count = calloc(n, sizeof(uint32_t));
    if (out_count == NULL) {
        free(out_degree);
        adjlist_destroy(adj);
        if (error_out) *error_out = reg_strdup("allocation failure");
        return false;
    }

    for (uint32_t i = 0; i < n; i++) {
        const GrammarExtensionMetadata *meta = &reg->entries[i].meta;
        uint32_t ndeps = string_array_len(meta->requires);
        for (uint32_t d = 0; d < ndeps; d++) {
            uint32_t dep_index;
            ht_find(&reg->ht, meta->requires[d], &dep_index);
            out_count[dep_index]++;
        }
    }

    /* Compute offsets (prefix sum) */
    adj->offsets[0] = 0;
    for (uint32_t i = 0; i < n; i++) {
        adj->offsets[i + 1] = adj->offsets[i] + out_count[i];
    }

    adj->neighbors = calloc(total_edges > 0 ? total_edges : 1, sizeof(uint32_t));
    if (adj->neighbors == NULL) {
        free(out_degree);
        free(out_count);
        adjlist_destroy(adj);
        if (error_out) *error_out = reg_strdup("allocation failure");
        return false;
    }

    /* Fill neighbors: for each edge dep_index -> i, store i in
    ** dep_index's neighbor list. */
    uint32_t *fill_pos = calloc(n, sizeof(uint32_t));
    if (fill_pos == NULL) {
        free(out_degree);
        free(out_count);
        adjlist_destroy(adj);
        if (error_out) *error_out = reg_strdup("allocation failure");
        return false;
    }

    for (uint32_t i = 0; i < n; i++) {
        const GrammarExtensionMetadata *meta = &reg->entries[i].meta;
        uint32_t ndeps = string_array_len(meta->requires);
        for (uint32_t d = 0; d < ndeps; d++) {
            uint32_t dep_index;
            ht_find(&reg->ht, meta->requires[d], &dep_index);
            uint32_t pos = adj->offsets[dep_index] + fill_pos[dep_index];
            adj->neighbors[pos] = i;
            fill_pos[dep_index]++;
        }
    }

    free(out_degree);
    free(out_count);
    free(fill_pos);
    return true;
}

/*
** Kahn's algorithm: produce a topological ordering.
** Returns true on success, false if a cycle is detected.
*/
static bool topological_sort(const AdjList *adj, uint32_t **order_out, uint32_t *norder_out,
                             char **error_out, const ExtensionRegistry *reg) {
    uint32_t n = adj->n;

    uint32_t *in_deg = malloc(n * sizeof(uint32_t));
    uint32_t *queue = malloc(n * sizeof(uint32_t));
    uint32_t *order = malloc(n * sizeof(uint32_t));

    if (in_deg == NULL || queue == NULL || order == NULL) {
        free(in_deg);
        free(queue);
        free(order);
        if (error_out) *error_out = reg_strdup("allocation failure");
        return false;
    }

    memcpy(in_deg, adj->in_degree, n * sizeof(uint32_t));

    /* Seed the queue with nodes that have no dependencies */
    uint32_t qfront = 0, qback = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (in_deg[i] == 0) {
            queue[qback++] = i;
        }
    }

    uint32_t sorted = 0;
    while (qfront < qback) {
        uint32_t node = queue[qfront++];
        order[sorted++] = node;

        /* For each neighbor (dependent of node) */
        for (uint32_t e = adj->offsets[node]; e < adj->offsets[node + 1]; e++) {
            uint32_t dep = adj->neighbors[e];
            in_deg[dep]--;
            if (in_deg[dep] == 0) {
                queue[qback++] = dep;
            }
        }
    }

    free(in_deg);
    free(queue);

    if (sorted < n) {
        /* Cycle detected -- find participating nodes */
        if (error_out) {
            /* Build a descriptive message listing cycle members */
            char buf[1024];
            int off = snprintf(buf, sizeof(buf), "dependency cycle detected among: ");
            bool first = true;
            /*
            ** Any node not in the sorted output is part of a cycle.
            ** We identify them by marking which indices were sorted.
            */
            bool *in_sorted = calloc(n, sizeof(bool));
            if (in_sorted) {
                for (uint32_t i = 0; i < sorted; i++) {
                    in_sorted[order[i]] = true;
                }
                for (uint32_t i = 0; i < n && off < (int)sizeof(buf) - 32; i++) {
                    if (!in_sorted[i]) {
                        if (!first) {
                            off += snprintf(buf + off, sizeof(buf) - off, ", ");
                        }
                        off += snprintf(buf + off, sizeof(buf) - off, "'%s'",
                                        reg->entries[i].meta.name);
                        first = false;
                    }
                }
                free(in_sorted);
            }
            *error_out = reg_strdup(buf);
        }
        free(order);
        return false;
    }

    *order_out = order;
    *norder_out = sorted;
    return true;
}

/* ------------------------------------------------------------------ */
/*  check_dependencies: full validation                                */
/* ------------------------------------------------------------------ */

bool extension_registry_check_dependencies(ExtensionRegistry *reg, char **error_out) {
    if (error_out) *error_out = NULL;
    if (reg == NULL) return true;
    if (reg->count == 0) return true;

    /* 1. Check that all 'conflicts_with' declarations are not violated */
    for (uint32_t i = 0; i < reg->count; i++) {
        const GrammarExtensionMetadata *meta = &reg->entries[i].meta;
        if (meta->conflicts_with == NULL) continue;

        for (uint32_t c = 0; meta->conflicts_with[c] != NULL; c++) {
            if (ht_find(&reg->ht, meta->conflicts_with[c], NULL)) {
                if (error_out) {
                    char buf[512];
                    snprintf(buf, sizeof(buf),
                             "extension '%s' conflicts with registered extension '%s'", meta->name,
                             meta->conflicts_with[c]);
                    *error_out = reg_strdup(buf);
                }
                return false;
            }
        }
    }

    /* 2. Build the dependency graph and run topological sort.
    **    build_adjacency checks for missing dependencies.
    **    topological_sort checks for cycles. */
    AdjList adj;
    memset(&adj, 0, sizeof(adj));

    if (!build_adjacency(reg, &adj, error_out)) {
        return false;
    }

    uint32_t *order = NULL;
    uint32_t norder = 0;
    bool ok = topological_sort(&adj, &order, &norder, error_out, reg);
    adjlist_destroy(&adj);
    free(order);

    return ok;
}

/* ------------------------------------------------------------------ */
/*  get_order: topological ordering                                    */
/* ------------------------------------------------------------------ */

bool extension_registry_get_order(ExtensionRegistry *reg, ExtensionOrder *order_out,
                                  char **error_out) {
    if (error_out) *error_out = NULL;
    if (order_out) {
        order_out->names = NULL;
        order_out->count = 0;
    }
    if (reg == NULL || order_out == NULL) return false;
    if (reg->count == 0) return true;

    AdjList adj;
    memset(&adj, 0, sizeof(adj));

    if (!build_adjacency(reg, &adj, error_out)) {
        return false;
    }

    uint32_t *indices = NULL;
    uint32_t nindices = 0;
    bool ok = topological_sort(&adj, &indices, &nindices, error_out, reg);
    adjlist_destroy(&adj);

    if (!ok) return false;

    /* Convert indices to names */
    char **names = calloc(nindices, sizeof(char *));
    if (names == NULL) {
        free(indices);
        if (error_out) *error_out = reg_strdup("allocation failure");
        return false;
    }

    for (uint32_t i = 0; i < nindices; i++) {
        names[i] = reg_strdup(reg->entries[indices[i]].meta.name);
        if (names[i] == NULL) {
            for (uint32_t j = 0; j < i; j++)
                free(names[j]);
            free(names);
            free(indices);
            if (error_out) *error_out = reg_strdup("allocation failure");
            return false;
        }
    }

    free(indices);
    order_out->names = names;
    order_out->count = nindices;
    return true;
}
