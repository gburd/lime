/*
** Dependency resolver for parser module composition.
**
** Implements topological sorting of module dependencies, circular
** dependency detection, version constraint validation, and symbol
** import/export checking.
*/
#include "dependency_resolver.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/*  SemVer utilities                                                   */
/* ------------------------------------------------------------------ */

bool semver_parse(const char *str, SemVer *out) {
    if (str == NULL || out == NULL) return false;
    memset(out, 0, sizeof(*out));

    /* Parse major.minor.patch */
    const char *p = str;
    char *end;

    unsigned long maj = strtoul(p, &end, 10);
    if (end == p || *end != '.') return false;
    out->major = (uint32_t)maj;
    p = end + 1;

    unsigned long min = strtoul(p, &end, 10);
    if (end == p || (*end != '.' && *end != '\0' && *end != '-')) return false;
    out->minor = (uint32_t)min;

    if (*end == '.') {
        p = end + 1;
        unsigned long pat = strtoul(p, &end, 10);
        if (end == p) return false;
        out->patch = (uint32_t)pat;
    }

    /* Optional prerelease: "-alpha.1" etc. */
    if (*end == '-') {
        p = end + 1;
        if (*p == '\0') return false;
        size_t len = strlen(p);
        out->prerelease = malloc(len + 1);
        if (out->prerelease == NULL) return false;
        memcpy(out->prerelease, p, len + 1);
    } else if (*end != '\0') {
        return false;
    }

    return true;
}

void semver_destroy(SemVer *v) {
    if (v == NULL) return;
    free(v->prerelease);
    v->prerelease = NULL;
}

/*
** Compare prerelease strings.  NULL (release) > any prerelease.
** Among prerelease strings, compare dot-separated identifiers:
** numeric identifiers compare as integers; lexicographic otherwise.
*/
static int compare_prerelease(const char *a, const char *b) {
    /* Release (NULL) is greater than any prerelease. */
    if (a == NULL && b == NULL) return 0;
    if (a == NULL) return 1; /* a is release, b is prerelease */
    if (b == NULL) return -1;

    /* Compare dot-separated identifiers */
    while (*a && *b) {
        /* Check if both start with digits */
        int a_num = isdigit((unsigned char)*a);
        int b_num = isdigit((unsigned char)*b);

        if (a_num && b_num) {
            unsigned long va = strtoul(a, NULL, 10);
            unsigned long vb = strtoul(b, NULL, 10);
            if (va < vb) return -1;
            if (va > vb) return 1;
            /* Skip past digits */
            while (isdigit((unsigned char)*a))
                a++;
            while (isdigit((unsigned char)*b))
                b++;
        } else if (a_num) {
            return -1; /* Numeric identifiers have lower precedence */
        } else if (b_num) {
            return 1;
        } else {
            /* Lexicographic compare until '.' or end */
            while (*a && *b && *a != '.' && *b != '.') {
                if (*a < *b) return -1;
                if (*a > *b) return 1;
                a++;
                b++;
            }
            if (*a != '.' && *a != '\0') return 1;
            if (*b != '.' && *b != '\0') return -1;
        }

        if (*a == '.' && *b == '.') {
            a++;
            b++;
        } else if (*a == '.') {
            return 1; /* a has more identifiers */
        } else if (*b == '.') {
            return -1;
        }
    }

    if (*a) return 1;
    if (*b) return -1;
    return 0;
}

int semver_compare(const SemVer *a, const SemVer *b) {
    if (a->major != b->major) return (a->major < b->major) ? -1 : 1;
    if (a->minor != b->minor) return (a->minor < b->minor) ? -1 : 1;
    if (a->patch != b->patch) return (a->patch < b->patch) ? -1 : 1;

    return compare_prerelease(a->prerelease, b->prerelease);
}

bool semver_satisfies(const SemVer *ver, const VersionConstraint *constraint) {
    if (ver == NULL || constraint == NULL) return false;

    int cmp = semver_compare(ver, &constraint->version);

    switch (constraint->op) {
    case VERSION_OP_EQ:
        return cmp == 0;
    case VERSION_OP_GTE:
        return cmp >= 0;
    case VERSION_OP_LTE:
        return cmp <= 0;
    case VERSION_OP_GT:
        return cmp > 0;
    case VERSION_OP_LT:
        return cmp < 0;

    case VERSION_OP_CARET:
        /* ^1.2.3 means >=1.2.3, <2.0.0  (same major)
            ** ^0.2.3 means >=0.2.3, <0.3.0  (same major.minor when major==0)
            ** ^0.0.3 means >=0.0.3, <0.0.4  (exact patch when 0.0.x) */
        if (cmp < 0) return false;
        if (constraint->version.major != 0) {
            return ver->major == constraint->version.major;
        } else if (constraint->version.minor != 0) {
            return ver->major == 0 && ver->minor == constraint->version.minor;
        } else {
            return ver->major == 0 && ver->minor == 0 && ver->patch == constraint->version.patch;
        }

    case VERSION_OP_TILDE:
        /* ~1.2.3 means >=1.2.3, <1.3.0  (same major.minor) */
        if (cmp < 0) return false;
        return ver->major == constraint->version.major && ver->minor == constraint->version.minor;
    }

    return false;
}

/* ------------------------------------------------------------------ */
/*  Module/dependency lifecycle helpers                                 */
/* ------------------------------------------------------------------ */

void parser_dependency_destroy_contents(ParserDependency *dep) {
    if (dep == NULL) return;
    free(dep->module_name);
    dep->module_name = NULL;
    for (uint32_t i = 0; i < dep->nconstraints; i++) {
        semver_destroy(&dep->constraints[i].version);
    }
    free(dep->constraints);
    dep->constraints = NULL;
    dep->nconstraints = 0;
}

void parser_module_destroy_contents(ParserModule *mod) {
    if (mod == NULL) return;
    free(mod->name);
    mod->name = NULL;
    semver_destroy(&mod->version);

    for (uint32_t i = 0; i < mod->ndependencies; i++) {
        parser_dependency_destroy_contents(&mod->dependencies[i]);
    }
    free(mod->dependencies);
    mod->dependencies = NULL;
    mod->ndependencies = 0;

    for (uint32_t i = 0; i < mod->nexports; i++) {
        free(mod->exports[i]);
    }
    free(mod->exports);
    mod->exports = NULL;
    mod->nexports = 0;

    for (uint32_t i = 0; i < mod->nimports; i++) {
        free(mod->imports[i]);
    }
    free(mod->imports);
    mod->imports = NULL;
    mod->nimports = 0;
}

/* ------------------------------------------------------------------ */
/*  DepError                                                           */
/* ------------------------------------------------------------------ */

void dep_error_destroy(DepError *err) {
    if (err == NULL) return;
    free(err->message);
    free(err->module_a);
    free(err->module_b);
    err->message = NULL;
    err->module_a = NULL;
    err->module_b = NULL;
}

static void set_error(DepError *err, DepResolveResult code, const char *msg, const char *mod_a,
                      const char *mod_b) {
    if (err == NULL) return;
    err->code = code;
    err->message = msg ? strdup(msg) : NULL;
    err->module_a = mod_a ? strdup(mod_a) : NULL;
    err->module_b = mod_b ? strdup(mod_b) : NULL;
}

/* ------------------------------------------------------------------ */
/*  DependencyGraph                                                    */
/* ------------------------------------------------------------------ */

DependencyGraph *dep_graph_create(void) {
    DependencyGraph *g = calloc(1, sizeof(DependencyGraph));
    return g;
}

void dep_graph_destroy(DependencyGraph *g) {
    if (g == NULL) return;
    free(g->modules);
    free(g->edges);
    free(g);
}

static bool graph_add_edge(DependencyGraph *g, uint32_t from, uint32_t to, bool optional) {
    if (g->nedges == g->edge_capacity) {
        uint32_t new_cap = g->edge_capacity ? g->edge_capacity * 2 : 16;
        DepEdge *new_edges = realloc(g->edges, new_cap * sizeof(DepEdge));
        if (new_edges == NULL) return false;
        g->edges = new_edges;
        g->edge_capacity = new_cap;
    }
    g->edges[g->nedges++] = (DepEdge){ from, to, optional };
    return true;
}

/* Find a module by name in the graph. Returns index or UINT32_MAX. */
static uint32_t find_module_index(DependencyGraph *g, const char *name) {
    for (uint32_t i = 0; i < g->nmodules; i++) {
        if (strcmp(g->modules[i]->name, name) == 0) {
            return i;
        }
    }
    return UINT32_MAX;
}

/* ------------------------------------------------------------------ */
/*  build_dependency_graph                                             */
/* ------------------------------------------------------------------ */

DepResolveResult build_dependency_graph(ParserModule **modules, uint32_t nmodules,
                                        DependencyGraph *graph, DepError *err) {
    if (graph == NULL) return DEP_ERR_ALLOC;

    /* Store module pointers (borrowed) */
    graph->modules = malloc(nmodules * sizeof(ParserModule *));
    if (nmodules > 0 && graph->modules == NULL) {
        set_error(err, DEP_ERR_ALLOC, "failed to allocate module array", NULL, NULL);
        return DEP_ERR_ALLOC;
    }
    for (uint32_t i = 0; i < nmodules; i++) {
        graph->modules[i] = modules[i];
    }
    graph->nmodules = nmodules;

    /* Check for duplicate module names */
    for (uint32_t i = 0; i < nmodules; i++) {
        for (uint32_t j = i + 1; j < nmodules; j++) {
            if (strcmp(modules[i]->name, modules[j]->name) == 0) {
                char buf[256];
                snprintf(buf, sizeof(buf), "duplicate module name '%s' at indices %u and %u",
                         modules[i]->name, i, j);
                set_error(err, DEP_ERR_DUPLICATE_MODULE, buf, modules[i]->name, modules[j]->name);
                return DEP_ERR_DUPLICATE_MODULE;
            }
        }
    }

    /* Build edges from each module's dependency list */
    for (uint32_t i = 0; i < nmodules; i++) {
        ParserModule *mod = modules[i];
        for (uint32_t d = 0; d < mod->ndependencies; d++) {
            ParserDependency *dep = &mod->dependencies[d];
            uint32_t target = find_module_index(graph, dep->module_name);

            if (target == UINT32_MAX) {
                if (dep->optional) continue;
                char buf[256];
                snprintf(buf, sizeof(buf),
                         "module '%s' requires dependency '%s' which is not "
                         "in the module set",
                         mod->name, dep->module_name);
                set_error(err, DEP_ERR_MISSING_DEP, buf, mod->name, dep->module_name);
                return DEP_ERR_MISSING_DEP;
            }

            if (!graph_add_edge(graph, i, target, dep->optional)) {
                set_error(err, DEP_ERR_ALLOC, "failed to allocate dependency edge", mod->name,
                          dep->module_name);
                return DEP_ERR_ALLOC;
            }
        }
    }

    return DEP_OK;
}

/* ------------------------------------------------------------------ */
/*  Cycle detection and topological sort (Kahn's algorithm)            */
/* ------------------------------------------------------------------ */

/*
** Internal DFS-based cycle detection.  Uses a colour array:
**   0 = white (unvisited), 1 = grey (in current path), 2 = black (done)
** If a cycle is found, builds a human-readable path string.
*/
static bool dfs_find_cycle(const DependencyGraph *g, uint32_t node, uint8_t *colour,
                           uint32_t *parent, uint32_t *cycle_start, uint32_t *cycle_end) {
    colour[node] = 1; /* grey */

    for (uint32_t e = 0; e < g->nedges; e++) {
        if (g->edges[e].from_index != node) continue;
        uint32_t next = g->edges[e].to_index;

        if (colour[next] == 1) {
            /* Back edge: cycle found */
            *cycle_start = next;
            *cycle_end = node;
            parent[next] = node; /* close the loop for path reconstruction */
            return true;
        }
        if (colour[next] == 0) {
            parent[next] = node;
            if (dfs_find_cycle(g, next, colour, parent, cycle_start, cycle_end)) {
                return true;
            }
        }
    }

    colour[node] = 2; /* black */
    return false;
}

static char *build_cycle_path(const DependencyGraph *g, uint32_t start, uint32_t end,
                              const uint32_t *parent) {
    /* Reconstruct cycle: walk from end back through parent to start */
    uint32_t path[256];
    uint32_t path_len = 0;

    path[path_len++] = end;
    uint32_t cur = end;
    while (cur != start && path_len < 255) {
        cur = parent[cur];
        path[path_len++] = cur;
    }

    /* Build string: "A -> B -> C -> A" */
    size_t buf_size = 0;
    for (uint32_t i = 0; i < path_len; i++) {
        buf_size += strlen(g->modules[path[i]]->name) + 4;
    }
    buf_size += strlen(g->modules[start]->name) + 1;

    char *buf = malloc(buf_size);
    if (buf == NULL) return NULL;

    char *p = buf;
    /* Print in reverse order (from start towards end) */
    for (int i = (int)path_len - 1; i >= 0; i--) {
        size_t nlen = strlen(g->modules[path[i]]->name);
        memcpy(p, g->modules[path[i]]->name, nlen);
        p += nlen;
        memcpy(p, " -> ", 4);
        p += 4;
    }
    /* Close the cycle */
    size_t slen = strlen(g->modules[start]->name);
    memcpy(p, g->modules[start]->name, slen);
    p += slen;
    *p = '\0';

    return buf;
}

bool has_circular_dependencies(const DependencyGraph *graph, char **cycle_path) {
    if (graph == NULL || graph->nmodules == 0) return false;

    uint8_t *colour = calloc(graph->nmodules, sizeof(uint8_t));
    uint32_t *parent = calloc(graph->nmodules, sizeof(uint32_t));
    if (colour == NULL || parent == NULL) {
        free(colour);
        free(parent);
        return false;
    }

    uint32_t cycle_start = UINT32_MAX, cycle_end = UINT32_MAX;
    bool found = false;

    for (uint32_t i = 0; i < graph->nmodules && !found; i++) {
        if (colour[i] == 0) {
            parent[i] = UINT32_MAX;
            found = dfs_find_cycle(graph, i, colour, parent, &cycle_start, &cycle_end);
        }
    }

    if (found && cycle_path != NULL) {
        *cycle_path = build_cycle_path(graph, cycle_start, cycle_end, parent);
    }

    free(colour);
    free(parent);
    return found;
}

/* ------------------------------------------------------------------ */
/*  resolve_dependencies -- topological sort via Kahn's algorithm      */
/* ------------------------------------------------------------------ */

DepResolveResult resolve_dependencies(const DependencyGraph *graph, uint32_t **order_out,
                                      uint32_t *norder, DepError *err) {
    if (graph == NULL || order_out == NULL || norder == NULL) {
        set_error(err, DEP_ERR_ALLOC, "NULL argument to resolve_dependencies", NULL, NULL);
        return DEP_ERR_ALLOC;
    }

    uint32_t n = graph->nmodules;
    if (n == 0) {
        *order_out = NULL;
        *norder = 0;
        return DEP_OK;
    }

    /* Compute in-degrees */
    uint32_t *in_degree = calloc(n, sizeof(uint32_t));
    uint32_t *queue = malloc(n * sizeof(uint32_t));
    uint32_t *result = malloc(n * sizeof(uint32_t));
    if (in_degree == NULL || queue == NULL || result == NULL) {
        free(in_degree);
        free(queue);
        free(result);
        set_error(err, DEP_ERR_ALLOC, "allocation failure in topological sort", NULL, NULL);
        return DEP_ERR_ALLOC;
    }

    for (uint32_t e = 0; e < graph->nedges; e++) {
        in_degree[graph->edges[e].from_index]++;
    }

    /* Seed queue with nodes that have no incoming edges (no dependencies) */
    uint32_t head = 0, tail = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (in_degree[i] == 0) {
            queue[tail++] = i;
        }
    }

    uint32_t count = 0;
    while (head < tail) {
        uint32_t node = queue[head++];
        result[count++] = node;

        /* For each edge from node (node depends on someone), reduce
        ** in-degree of node's dependents.
        ** Wait -- edges go from_index -> to_index where from_index depends
        ** on to_index.  So "node is depended upon" means there exist edges
        ** with to_index == node.  We need the reverse: for Kahn's, we
        ** process nodes with in_degree 0 first (leaves / no dependencies).
        **
        ** Re-examine: edges[e].from_index depends on edges[e].to_index.
        ** So in_degree of from_index should count edges *into* from_index,
        ** which means edges where from_index appears as the dependent.
        ** Actually our edge means from -> to (from depends on to).
        ** In the dependency DAG, the arrow goes from dependent to
        ** dependency.  For topological sort we want dependencies first,
        ** so we want to process to_index before from_index.
        **
        ** Kahn's: in_degree[v] counts edges pointing TO v.
        ** Our edges: from_index -> to_index means "from depends on to".
        ** Reverse the perspective: this is an edge from `to` to `from`
        ** in the "must come before" graph.
        **
        ** So in_degree[from_index]++ is correct: from has a dependency.
        ** When we process a node (all its deps satisfied), we look for
        ** edges where to_index == node, and decrement in_degree of
        ** from_index.
        */
        for (uint32_t e = 0; e < graph->nedges; e++) {
            if (graph->edges[e].to_index == node) {
                uint32_t dependent = graph->edges[e].from_index;
                in_degree[dependent]--;
                if (in_degree[dependent] == 0) {
                    queue[tail++] = dependent;
                }
            }
        }
    }

    free(in_degree);
    free(queue);

    if (count < n) {
        /* Cycle detected -- find it for the error message */
        free(result);
        char *cycle = NULL;
        has_circular_dependencies(graph, &cycle);
        char buf[512];
        if (cycle != NULL) {
            snprintf(buf, sizeof(buf), "circular dependency detected: %s", cycle);
            free(cycle);
        } else {
            snprintf(buf, sizeof(buf), "circular dependency detected");
        }
        set_error(err, DEP_ERR_CIRCULAR, buf, NULL, NULL);
        return DEP_ERR_CIRCULAR;
    }

    *order_out = result;
    *norder = count;
    return DEP_OK;
}

/* ------------------------------------------------------------------ */
/*  validate_versions                                                  */
/* ------------------------------------------------------------------ */

DepResolveResult validate_versions(const DependencyGraph *graph, DepError *err) {
    if (graph == NULL) return DEP_OK;

    for (uint32_t e = 0; e < graph->nedges; e++) {
        DepEdge *edge = &graph->edges[e];
        ParserModule *from = graph->modules[edge->from_index];
        ParserModule *to = graph->modules[edge->to_index];

        /* Find the dependency declaration in from that references to */
        for (uint32_t d = 0; d < from->ndependencies; d++) {
            ParserDependency *dep = &from->dependencies[d];
            if (strcmp(dep->module_name, to->name) != 0) continue;

            /* Check all version constraints */
            for (uint32_t c = 0; c < dep->nconstraints; c++) {
                if (!semver_satisfies(&to->version, &dep->constraints[c])) {
                    char ver_buf[64];
                    snprintf(ver_buf, sizeof(ver_buf), "%u.%u.%u", to->version.major,
                             to->version.minor, to->version.patch);

                    char constraint_buf[64];
                    const char *op_str = "==";
                    switch (dep->constraints[c].op) {
                    case VERSION_OP_EQ:
                        op_str = "==";
                        break;
                    case VERSION_OP_GTE:
                        op_str = ">=";
                        break;
                    case VERSION_OP_LTE:
                        op_str = "<=";
                        break;
                    case VERSION_OP_GT:
                        op_str = ">";
                        break;
                    case VERSION_OP_LT:
                        op_str = "<";
                        break;
                    case VERSION_OP_CARET:
                        op_str = "^";
                        break;
                    case VERSION_OP_TILDE:
                        op_str = "~";
                        break;
                    }
                    snprintf(constraint_buf, sizeof(constraint_buf), "%s%u.%u.%u", op_str,
                             dep->constraints[c].version.major, dep->constraints[c].version.minor,
                             dep->constraints[c].version.patch);

                    char buf[512];
                    snprintf(buf, sizeof(buf), "module '%s' requires '%s' %s but found version %s",
                             from->name, to->name, constraint_buf, ver_buf);
                    set_error(err, DEP_ERR_VERSION_MISMATCH, buf, from->name, to->name);
                    return DEP_ERR_VERSION_MISMATCH;
                }
            }
        }
    }

    return DEP_OK;
}

/* ------------------------------------------------------------------ */
/*  validate_composition                                               */
/* ------------------------------------------------------------------ */

DepResolveResult validate_composition(const DependencyGraph *graph, const uint32_t *order,
                                      uint32_t norder, DepError *err) {
    if (graph == NULL || norder == 0) return DEP_OK;

    /*
    ** Walk the topological order.  For each module, accumulate the set
    ** of exported symbols available from all preceding modules (its
    ** transitive dependencies).  Then check that all of the module's
    ** imports are in that set.
    **
    ** For efficiency with small module counts, we use a flat array of
    ** (name, provider) pairs.  A hash set would be better for large N.
    */
    typedef struct {
        const char *name;
        uint32_t provider_index;
    } ExportEntry;

    uint32_t export_cap = 64;
    uint32_t export_count = 0;
    ExportEntry *exports = malloc(export_cap * sizeof(ExportEntry));
    if (exports == NULL) {
        set_error(err, DEP_ERR_ALLOC, "allocation failure in validate_composition", NULL, NULL);
        return DEP_ERR_ALLOC;
    }

    for (uint32_t i = 0; i < norder; i++) {
        uint32_t idx = order[i];
        ParserModule *mod = graph->modules[idx];

        /* Check imports against available exports */
        for (uint32_t imp = 0; imp < mod->nimports; imp++) {
            bool found = false;

            /* Check if this symbol is exported by any dependency */
            for (uint32_t ex = 0; ex < export_count; ex++) {
                if (strcmp(mod->imports[imp], exports[ex].name) == 0) {
                    /* Verify the provider is actually a dependency */
                    uint32_t prov = exports[ex].provider_index;
                    for (uint32_t e = 0; e < graph->nedges; e++) {
                        if (graph->edges[e].from_index == idx && graph->edges[e].to_index == prov) {
                            found = true;
                            break;
                        }
                    }
                    /* Also accept transitive dependencies: the provider
                    ** appears earlier in topo order which means it is
                    ** reachable through the dependency chain. */
                    if (!found) {
                        for (uint32_t j = 0; j < i; j++) {
                            if (order[j] == prov) {
                                found = true;
                                break;
                            }
                        }
                    }
                    if (found) break;
                }
            }

            if (!found) {
                char buf[256];
                snprintf(buf, sizeof(buf),
                         "module '%s' imports symbol '%s' which is not "
                         "exported by any of its dependencies",
                         mod->name, mod->imports[imp]);
                set_error(err, DEP_ERR_SYMBOL_MISSING, buf, mod->name, NULL);
                free(exports);
                return DEP_ERR_SYMBOL_MISSING;
            }
        }

        /* Add this module's exports to the available set */
        for (uint32_t ex = 0; ex < mod->nexports; ex++) {
            if (export_count == export_cap) {
                export_cap *= 2;
                ExportEntry *new_exp = realloc(exports, export_cap * sizeof(ExportEntry));
                if (new_exp == NULL) {
                    free(exports);
                    set_error(err, DEP_ERR_ALLOC, "allocation failure growing export table", NULL,
                              NULL);
                    return DEP_ERR_ALLOC;
                }
                exports = new_exp;
            }
            exports[export_count++] = (ExportEntry){
                .name = mod->exports[ex],
                .provider_index = idx,
            };
        }
    }

    free(exports);
    return DEP_OK;
}
