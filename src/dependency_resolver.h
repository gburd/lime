/*
** Dependency resolver for parser module composition.
**
** Given a set of ParserModule nodes, the resolver builds a directed
** acyclic graph (DAG) of dependencies and produces a topological
** ordering that guarantees every module is loaded after all of its
** required dependencies.  Circular dependencies are detected and
** reported with clear error messages showing the cycle path.
**
** The resolver also validates:
**   - All required dependencies are present in the module set
**   - Version constraints are satisfied
**   - Exported/imported symbols are consistent
*/
#ifndef DEPENDENCY_RESOLVER_H
#define DEPENDENCY_RESOLVER_H

#include "snapshot.h"

#include <stdint.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/*  Result codes                                                       */
/* ------------------------------------------------------------------ */

typedef enum DepResolveResult {
    DEP_OK = 0,               /* Resolution succeeded               */
    DEP_ERR_ALLOC,            /* Memory allocation failure          */
    DEP_ERR_MISSING_DEP,      /* Required dependency not found      */
    DEP_ERR_VERSION_MISMATCH, /* Version constraint not satisfied   */
    DEP_ERR_CIRCULAR,         /* Circular dependency detected       */
    DEP_ERR_SYMBOL_MISSING,   /* Imported symbol not exported by any module */
    DEP_ERR_DUPLICATE_MODULE, /* Two modules with the same name     */
} DepResolveResult;

/* ------------------------------------------------------------------ */
/*  Dependency graph                                                   */
/* ------------------------------------------------------------------ */

/*
** An edge in the dependency graph: module at from_index depends on
** module at to_index.
*/
typedef struct DepEdge {
    uint32_t from_index; /* Index of the dependent module      */
    uint32_t to_index;   /* Index of the dependency            */
    bool optional;       /* True if this is an optional dep    */
} DepEdge;

/*
** The dependency graph owns the module array and the edge list.
** After build_dependency_graph() the caller can inspect the graph
** or pass it to resolve_dependencies() for topological sorting.
*/
typedef struct DependencyGraph {
    ParserModule **modules; /* Borrowed pointers to modules       */
    uint32_t nmodules;

    DepEdge *edges; /* Dynamic array of edges             */
    uint32_t nedges;
    uint32_t edge_capacity;
} DependencyGraph;

/* ------------------------------------------------------------------ */
/*  Validation detail                                                  */
/* ------------------------------------------------------------------ */

/*
** On failure, the resolver can populate a DepError with a human-readable
** message and context about the specific modules involved.
*/
typedef struct DepError {
    DepResolveResult code;
    char *message;  /* malloc'd, caller must free         */
    char *module_a; /* Name of first involved module      */
    char *module_b; /* Name of second module (or NULL)    */
} DepError;

/*
** Free resources owned by a DepError.  Does not free the struct itself.
*/
void dep_error_destroy(DepError *err);

/* ------------------------------------------------------------------ */
/*  API                                                                */
/* ------------------------------------------------------------------ */

/*
** Create an empty dependency graph.  Returns NULL on allocation failure.
*/
DependencyGraph *dep_graph_create(void);

/*
** Destroy a dependency graph and free all owned memory.
*/
void dep_graph_destroy(DependencyGraph *g);

/*
** Build a dependency graph from an array of modules.  For each module
** the function resolves its declared dependencies against the set of
** modules provided.  Missing optional dependencies are silently
** skipped; missing required dependencies produce an error.
**
** Parameters:
**   modules   - Array of module pointers (borrowed, not freed by graph)
**   nmodules  - Number of modules
**   graph     - Pre-created graph to populate
**   err       - On failure, receives error details (may be NULL)
**
** Returns DEP_OK on success.
*/
DepResolveResult build_dependency_graph(ParserModule **modules, uint32_t nmodules,
                                        DependencyGraph *graph, DepError *err);

/*
** Topological sort of the dependency graph.  On success, *order_out
** receives a malloc'd array of module indices in dependency order
** (dependencies before dependents) and *norder receives the count.
**
** If the graph contains a cycle, DEP_ERR_CIRCULAR is returned and
** *err describes the cycle.
**
** Parameters:
**   graph      - A populated dependency graph
**   order_out  - Receives malloc'd index array; caller must free
**   norder     - Receives count of indices
**   err        - On failure, receives error details (may be NULL)
**
** Returns DEP_OK on success.
*/
DepResolveResult resolve_dependencies(const DependencyGraph *graph, uint32_t **order_out,
                                      uint32_t *norder, DepError *err);

/*
** Check whether the dependency graph contains any cycles.
** Returns true if at least one cycle exists.  If cycle_path is
** non-NULL and a cycle is found, *cycle_path is set to a malloc'd
** human-readable description of the cycle (caller must free).
*/
bool has_circular_dependencies(const DependencyGraph *graph, char **cycle_path);

/*
** Validate that all version constraints in the graph are satisfied.
** Returns DEP_OK if every dependency's version constraints are met
** by the target module's declared version.
**
** Parameters:
**   graph  - A populated dependency graph
**   err    - On failure, receives error details (may be NULL)
*/
DepResolveResult validate_versions(const DependencyGraph *graph, DepError *err);

/*
** Validate that every symbol imported by a module is exported by at
** least one of its (transitive) dependencies.
**
** Parameters:
**   graph      - A populated dependency graph
**   order      - Topological order from resolve_dependencies()
**   norder     - Number of entries in order
**   err        - On failure, receives error details (may be NULL)
*/
DepResolveResult validate_composition(const DependencyGraph *graph, const uint32_t *order,
                                      uint32_t norder, DepError *err);

#endif /* DEPENDENCY_RESOLVER_H */
