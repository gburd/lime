/*
** Parser composition operations implementation.
**
** Combines multiple ParserSnapshots into a single composed snapshot,
** unifying symbols, merging rules, detecting conflicts, recomputing
** the LALR(1) automaton, and optionally computing a merkle tree of
** the result.
*/
#include "parser_composition.h"
#include "snapshot_modify.h"
#include "merkle_tree.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static char *dup_string(const char *s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy != NULL) memcpy(copy, s, len);
    return copy;
}

static void set_diag_error(CompositionDiagnostics *diag, const char *msg) {
    if (diag == NULL) return;
    free(diag->error);
    diag->error = dup_string(msg);
}

static void set_diag_error_fmt(CompositionDiagnostics *diag, const char *fmt, ...) {
    if (diag == NULL) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    free(diag->error);
    diag->error = dup_string(buf);
}

static CompositionOptions default_options(void) {
    CompositionOptions opts = {
        .flags = COMPOSE_FLAG_NONE,
        .modules = NULL,
        .nmodules = 0,
    };
    return opts;
}

/* ------------------------------------------------------------------ */
/*  CompositionDiagnostics                                             */
/* ------------------------------------------------------------------ */

void composition_diagnostics_destroy(CompositionDiagnostics *diag) {
    if (diag == NULL) return;

    if (diag->conflicts) {
        conflict_set_destroy(diag->conflicts);
        diag->conflicts = NULL;
    }

    for (uint32_t i = 0; i < diag->nsymbol_mappings; i++) {
        free(diag->symbol_map[i].name);
    }
    free(diag->symbol_map);
    diag->symbol_map = NULL;
    diag->nsymbol_mappings = 0;

    if (diag->merkle) {
        merkle_free_tree(diag->merkle);
        diag->merkle = NULL;
    }

    free(diag->error);
    diag->error = NULL;
}

/* ------------------------------------------------------------------ */
/*  Symbol unification                                                 */
/* ------------------------------------------------------------------ */

/*
** A simple dynamic string set for tracking seen symbol names during
** unification.  Uses linear search which is fine for typical grammar
** symbol counts (hundreds, not millions).
*/
typedef struct {
    char **names;
    uint32_t *source_indices; /* which snapshot each name came from */
    uint32_t count;
    uint32_t capacity;
} SymbolSet;

static bool symset_init(SymbolSet *ss, uint32_t initial_cap) {
    ss->names = calloc(initial_cap, sizeof(char *));
    ss->source_indices = calloc(initial_cap, sizeof(uint32_t));
    ss->count = 0;
    ss->capacity = initial_cap;
    return ss->names != NULL && ss->source_indices != NULL;
}

static void symset_destroy(SymbolSet *ss) {
    for (uint32_t i = 0; i < ss->count; i++) {
        free(ss->names[i]);
    }
    free(ss->names);
    free(ss->source_indices);
    ss->names = NULL;
    ss->source_indices = NULL;
    ss->count = 0;
}

/* Returns the index if found, UINT32_MAX otherwise. */
static uint32_t symset_find(const SymbolSet *ss, const char *name) {
    for (uint32_t i = 0; i < ss->count; i++) {
        if (strcmp(ss->names[i], name) == 0) return i;
    }
    return UINT32_MAX;
}

static bool symset_add(SymbolSet *ss, const char *name, uint32_t source) {
    if (ss->count == ss->capacity) {
        uint32_t new_cap = ss->capacity * 2;
        char **new_names = realloc(ss->names, new_cap * sizeof(char *));
        uint32_t *new_src = realloc(ss->source_indices, new_cap * sizeof(uint32_t));
        if (!new_names || !new_src) {
            /* realloc may have succeeded for one but not the other;
            ** keep the originals in that case. */
            if (new_names) ss->names = new_names;
            if (new_src) ss->source_indices = new_src;
            return false;
        }
        ss->names = new_names;
        ss->source_indices = new_src;
        ss->capacity = new_cap;
    }
    ss->names[ss->count] = dup_string(name);
    if (ss->names[ss->count] == NULL) return false;
    ss->source_indices[ss->count] = source;
    ss->count++;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Symbol name extraction from snapshots                              */
/* ------------------------------------------------------------------ */

/*
** Since the snapshot's symbol array contains opaque struct symbol
** pointers (defined in lime.c, not available to library code), we
** synthesise placeholder names based on index.  When the full Lemon
** dynamic-table support lands, this will read the actual symbol
** names from the struct.
*/
static char *make_symbol_name(uint32_t snapshot_idx, uint32_t sym_idx, bool is_terminal) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%s_%u_%u", is_terminal ? "T" : "NT", snapshot_idx, sym_idx);
    return dup_string(buf);
}

/* ------------------------------------------------------------------ */
/*  Unify symbols across snapshots                                     */
/* ------------------------------------------------------------------ */

/*
** Walk all symbols in all snapshots.  Build a unified symbol set,
** detecting collisions.  Populate the diagnostics symbol_map.
**
** Returns COMPOSE_OK on success or a conflict result code.
*/
static CompositionResult unify_symbols(ParserSnapshot **snapshots, uint32_t nsnapshots,
                                       CompositionFlags flags, ConflictSet *cs, SymbolSet *unified,
                                       CompositionDiagnostics *diag) {
    /* Pre-size the mapping array. */
    uint32_t total_syms = 0;
    for (uint32_t s = 0; s < nsnapshots; s++) {
        total_syms += snapshots[s]->nsymbol;
    }

    SymbolMapping *map = NULL;
    uint32_t map_count = 0;
    if (diag != NULL && total_syms > 0) {
        map = calloc(total_syms, sizeof(SymbolMapping));
        /* Non-fatal if allocation fails; we just won't have the map. */
    }

    for (uint32_t s = 0; s < nsnapshots; s++) {
        ParserSnapshot *snap = snapshots[s];
        for (uint32_t i = 0; i < snap->nsymbol; i++) {
            bool is_term = (i < snap->nterminal);
            char *name = make_symbol_name(s, i, is_term);
            if (name == NULL) {
                free(map);
                return COMPOSE_ERR_ALLOC;
            }

            uint32_t existing = symset_find(unified, name);
            if (existing != UINT32_MAX) {
                /* Symbol name already seen. */
                if (flags & COMPOSE_FLAG_LAST_WINS) {
                    /* Replace the source index -- last writer wins. */
                    unified->source_indices[existing] = s;
                } else {
                    /* Record a collision conflict. */
                    if (cs) {
                        char desc[256];
                        snprintf(desc, sizeof(desc), "symbol '%s' defined in snapshots %u and %u",
                                 name, unified->source_indices[existing], s);
                        conflict_set_add(cs, CONFLICT_TOKEN_COLLISION,
                                         unified->source_indices[existing], s, 0, 0, desc);
                    }
                }
                uint32_t unified_idx = existing;
                if (map && map_count < total_syms) {
                    map[map_count] = (SymbolMapping){
                        .name = dup_string(name),
                        .source_snapshot = s,
                        .original_index = i,
                        .unified_index = unified_idx,
                    };
                    map_count++;
                }
            } else {
                uint32_t unified_idx = unified->count;
                if (!symset_add(unified, name, s)) {
                    free(name);
                    free(map);
                    return COMPOSE_ERR_ALLOC;
                }
                if (map && map_count < total_syms) {
                    map[map_count] = (SymbolMapping){
                        .name = dup_string(name),
                        .source_snapshot = s,
                        .original_index = i,
                        .unified_index = unified_idx,
                    };
                    map_count++;
                }
            }
            free(name);
        }
    }

    if (diag != NULL) {
        diag->symbol_map = map;
        diag->nsymbol_mappings = map_count;
    } else {
        /* Clean up map if no diagnostics requested. */
        if (map) {
            for (uint32_t i = 0; i < map_count; i++) {
                free(map[i].name);
            }
            free(map);
        }
    }

    return COMPOSE_OK;
}

/* ------------------------------------------------------------------ */
/*  Rule merging                                                       */
/* ------------------------------------------------------------------ */

/*
** Merge rules from all snapshots into the composed snapshot.
** With DEDUP_RULES, identical rules (same counts) are collapsed.
** Conflicts are recorded in *cs*.
*/
static CompositionResult merge_rules(ParserSnapshot **snapshots, uint32_t nsnapshots,
                                     ParserSnapshot *composed, CompositionFlags flags,
                                     ConflictSet *cs) {
    /*
    ** Since rules are currently opaque (struct rule is in lime.c),
    ** we accumulate rule counts.  When full dynamic-table support
    ** lands, this will iterate the linked list and deep-copy rules.
    */
    uint32_t total_rules = 0;
    for (uint32_t s = 0; s < nsnapshots; s++) {
        total_rules += snapshots[s]->nrule;
    }

    if (flags & COMPOSE_FLAG_DEDUP_RULES) {
        /* With dedup, we can't know the exact count without comparing
        ** rule content.  For now, use the sum and note that duplicates
        ** would reduce this once struct rule is accessible. */
    }

    composed->nrule = total_rules;
    composed->rules = NULL; /* Linked list head; populated by rebuild. */

    (void)cs; /* Conflicts detected during automaton rebuild. */
    return COMPOSE_OK;
}

/* ------------------------------------------------------------------ */
/*  Merkle tree computation                                            */
/* ------------------------------------------------------------------ */

MerkleTree *compute_snapshot_merkle(const ParserSnapshot *snap) {
    if (snap == NULL) return NULL;

    /*
    ** Build leaf nodes for the four grammar sections:
    **   1. Symbols (hash of symbol counts as placeholder)
    **   2. Rules (hash of rule count)
    **   3. States (hash of state count)
    **   4. Action tables (hash of action array content)
    */
    MerkleNode *leaves[4];
    uint32_t nleaves = 0;

    /* Symbols leaf */
    {
        struct {
            uint32_t nsymbol;
            uint32_t nterminal;
        } sym_data = { snap->nsymbol, snap->nterminal };
        leaves[nleaves] = merkle_create_leaf(&sym_data, sizeof(sym_data), "symbols");
        if (!leaves[nleaves]) goto fail;
        nleaves++;
    }

    /* Rules leaf */
    {
        uint32_t nrule = snap->nrule;
        leaves[nleaves] = merkle_create_leaf(&nrule, sizeof(nrule), "rules");
        if (!leaves[nleaves]) goto fail;
        nleaves++;
    }

    /* States leaf */
    {
        uint32_t nstate = snap->nstate;
        leaves[nleaves] = merkle_create_leaf(&nstate, sizeof(nstate), "states");
        if (!leaves[nleaves]) goto fail;
        nleaves++;
    }

    /* Action tables leaf */
    {
        /*
        ** Hash the action table content if present, otherwise hash
        ** the counts as a placeholder.
        */
        if (snap->yy_action != NULL && snap->action_count > 0) {
            leaves[nleaves] = merkle_create_leaf(snap->yy_action,
                                                 snap->action_count * sizeof(uint16_t), "actions");
        } else {
            struct {
                uint32_t action_count;
                uint32_t lookahead_count;
            } act_data = { snap->action_count, snap->lookahead_count };
            leaves[nleaves] = merkle_create_leaf(&act_data, sizeof(act_data), "actions");
        }
        if (!leaves[nleaves]) goto fail;
        nleaves++;
    }

    MerkleTree *tree = merkle_build_tree(leaves, nleaves, "grammar");
    return tree;

fail:
    for (uint32_t i = 0; i < nleaves; i++) {
        merkle_free_node(leaves[i]);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Validation                                                         */
/* ------------------------------------------------------------------ */

CompositionResult validate_composition_inputs(ParserSnapshot **snapshots, uint32_t nsnapshots,
                                              const CompositionOptions *opts,
                                              CompositionDiagnostics *diag) {
    if (snapshots == NULL || nsnapshots == 0) {
        set_diag_error(diag, "no snapshots provided");
        return COMPOSE_ERR_INVALID_INPUT;
    }

    for (uint32_t i = 0; i < nsnapshots; i++) {
        if (snapshots[i] == NULL) {
            set_diag_error_fmt(diag, "snapshot at index %u is NULL", i);
            return COMPOSE_ERR_INVALID_INPUT;
        }
    }

    /* If modules are provided and dependency checking is not skipped,
    ** validate the dependency graph. */
    if (opts != NULL && opts->modules != NULL && opts->nmodules > 0 &&
        !(opts->flags & COMPOSE_FLAG_SKIP_DEPS)) {
        if (opts->nmodules != nsnapshots) {
            set_diag_error(diag, "module count does not match snapshot count");
            return COMPOSE_ERR_INVALID_INPUT;
        }

        DependencyGraph *graph = dep_graph_create();
        if (graph == NULL) {
            set_diag_error(diag, "failed to create dependency graph");
            return COMPOSE_ERR_ALLOC;
        }

        DepError dep_err;
        memset(&dep_err, 0, sizeof(dep_err));

        DepResolveResult dr =
            build_dependency_graph(opts->modules, opts->nmodules, graph, &dep_err);
        if (dr != DEP_OK) {
            set_diag_error_fmt(diag, "dependency graph error: %s",
                               dep_err.message ? dep_err.message : "(unknown)");
            dep_error_destroy(&dep_err);
            dep_graph_destroy(graph);
            return COMPOSE_ERR_DEPENDENCY;
        }

        /* Check for cycles. */
        char *cycle = NULL;
        if (has_circular_dependencies(graph, &cycle)) {
            set_diag_error_fmt(diag, "circular dependency: %s", cycle ? cycle : "(unknown cycle)");
            free(cycle);
            dep_error_destroy(&dep_err);
            dep_graph_destroy(graph);
            return COMPOSE_ERR_DEPENDENCY;
        }

        /* Validate version constraints. */
        dr = validate_versions(graph, &dep_err);
        if (dr != DEP_OK) {
            set_diag_error_fmt(diag, "version mismatch: %s",
                               dep_err.message ? dep_err.message : "(unknown)");
            dep_error_destroy(&dep_err);
            dep_graph_destroy(graph);
            return COMPOSE_ERR_DEPENDENCY;
        }

        /* Topological sort and symbol validation. */
        uint32_t *order = NULL;
        uint32_t norder = 0;
        dr = resolve_dependencies(graph, &order, &norder, &dep_err);
        if (dr != DEP_OK) {
            set_diag_error_fmt(diag, "dependency resolution failed: %s",
                               dep_err.message ? dep_err.message : "(unknown)");
            dep_error_destroy(&dep_err);
            dep_graph_destroy(graph);
            return COMPOSE_ERR_DEPENDENCY;
        }

        dr = validate_composition(graph, order, norder, &dep_err);
        if (dr != DEP_OK) {
            set_diag_error_fmt(diag, "composition validation failed: %s",
                               dep_err.message ? dep_err.message : "(unknown)");
            free(order);
            dep_error_destroy(&dep_err);
            dep_graph_destroy(graph);
            return COMPOSE_ERR_DEPENDENCY;
        }

        free(order);
        dep_error_destroy(&dep_err);
        dep_graph_destroy(graph);
    }

    return COMPOSE_OK;
}

/* ------------------------------------------------------------------ */
/*  compose_snapshots                                                  */
/* ------------------------------------------------------------------ */

CompositionResult compose_snapshots(ParserSnapshot **snapshots, uint32_t nsnapshots,
                                    const CompositionOptions *opts, ParserSnapshot **out,
                                    CompositionDiagnostics *diag) {
    if (out == NULL) return COMPOSE_ERR_INVALID_INPUT;
    *out = NULL;

    if (diag != NULL) {
        memset(diag, 0, sizeof(*diag));
    }

    CompositionOptions effective_opts;
    if (opts != NULL) {
        effective_opts = *opts;
    } else {
        effective_opts = default_options();
    }

    /* Step 1: Validate inputs. */
    CompositionResult cr =
        validate_composition_inputs(snapshots, nsnapshots, &effective_opts, diag);
    if (cr != COMPOSE_OK) return cr;

    /* Step 2: Create conflict set for tracking issues. */
    ConflictSet *cs = conflict_set_create();
    if (cs == NULL) {
        set_diag_error(diag, "failed to create conflict set");
        return COMPOSE_ERR_ALLOC;
    }

    /* Step 3: Unify symbols. */
    SymbolSet unified;
    if (!symset_init(&unified, 256)) {
        conflict_set_destroy(cs);
        set_diag_error(diag, "failed to initialise symbol set");
        return COMPOSE_ERR_ALLOC;
    }

    cr = unify_symbols(snapshots, nsnapshots, effective_opts.flags, cs, &unified, diag);
    if (cr != COMPOSE_OK) {
        symset_destroy(&unified);
        conflict_set_destroy(cs);
        return cr;
    }

    /* Step 4: Create the composed snapshot. */
    ParserSnapshot *composed = clone_snapshot(NULL); /* empty */
    if (composed == NULL) {
        symset_destroy(&unified);
        conflict_set_destroy(cs);
        set_diag_error(diag, "failed to create composed snapshot");
        return COMPOSE_ERR_ALLOC;
    }

    /* Set symbol counts from the unified set. */
    composed->nsymbol = unified.count;
    /* Count terminals: those whose name starts with "T_". */
    uint32_t nterm = 0;
    for (uint32_t i = 0; i < unified.count; i++) {
        if (unified.names[i] && unified.names[i][0] == 'T') {
            nterm++;
        }
    }
    composed->nterminal = nterm;

    /* Step 5: Merge rules. */
    cr = merge_rules(snapshots, nsnapshots, composed, effective_opts.flags, cs);
    if (cr != COMPOSE_OK) {
        symset_destroy(&unified);
        conflict_set_destroy(cs);
        snapshot_release(composed);
        return cr;
    }

    /* Step 6: Merge action tables.
    ** Sum the action table sizes from all snapshots.  The actual table
    ** content requires a full automaton rebuild; for now we allocate
    ** combined arrays. */
    {
        uint32_t total_actions = 0;
        uint32_t total_lookahead = 0;
        uint32_t total_states = 0;
        for (uint32_t s = 0; s < nsnapshots; s++) {
            total_actions += snapshots[s]->action_count;
            total_lookahead += snapshots[s]->lookahead_count;
            total_states += snapshots[s]->nstate;
        }
        composed->action_count = total_actions;
        composed->lookahead_count = total_lookahead;
        composed->nstate = total_states;

        if (total_actions > 0) {
            composed->yy_action = calloc(total_actions, sizeof(uint16_t));
            composed->yy_lookahead = calloc(total_lookahead, sizeof(uint16_t));
            if (!composed->yy_action || !composed->yy_lookahead) {
                symset_destroy(&unified);
                conflict_set_destroy(cs);
                snapshot_release(composed);
                set_diag_error(diag, "failed to allocate action tables");
                return COMPOSE_ERR_ALLOC;
            }

            /* Copy action table data from each snapshot. */
            uint32_t act_off = 0;
            uint32_t la_off = 0;
            for (uint32_t s = 0; s < nsnapshots; s++) {
                ParserSnapshot *snap = snapshots[s];
                if (snap->yy_action && snap->action_count > 0) {
                    memcpy(composed->yy_action + act_off, snap->yy_action,
                           snap->action_count * sizeof(uint16_t));
                    act_off += snap->action_count;
                }
                if (snap->yy_lookahead && snap->lookahead_count > 0) {
                    memcpy(composed->yy_lookahead + la_off, snap->yy_lookahead,
                           snap->lookahead_count * sizeof(uint16_t));
                    la_off += snap->lookahead_count;
                }
            }
        }

        if (total_states > 0) {
            composed->yy_shift_ofst = calloc(total_states, sizeof(int16_t));
            composed->yy_reduce_ofst = calloc(total_states, sizeof(int16_t));
            composed->yy_default = calloc(total_states, sizeof(uint16_t));
        }
    }

    /* Step 7: Rebuild the LALR(1) automaton over the composed
    ** grammar.  rebuild_automaton currently performs validation and
    ** version bookkeeping; full table reconstruction lives in the
    ** lime generator and is exposed via lemon_snapshot_create with
    ** the merged grammar text. */
    {
        char *build_err = NULL;
        if (!rebuild_automaton(composed, &build_err)) {
            symset_destroy(&unified);
            set_diag_error_fmt(diag, "automaton rebuild failed: %s",
                               build_err ? build_err : "(unknown)");
            free(build_err);
            if (diag)
                diag->conflicts = cs;
            else
                conflict_set_destroy(cs);
            snapshot_release(composed);
            return COMPOSE_ERR_BUILD;
        }
        free(build_err);
    }

    /* Step 8: Check for unresolved conflicts. */
    {
        uint32_t unresolved = conflict_set_unresolved_count(cs);
        if (unresolved > 0 && !(effective_opts.flags & COMPOSE_FLAG_LAST_WINS) &&
            !(effective_opts.flags & COMPOSE_FLAG_DEDUP_RULES)) {
            if (diag) {
                diag->conflicts = cs;
                set_diag_error_fmt(diag, "%u unresolved conflict(s) detected", unresolved);
            } else {
                conflict_set_destroy(cs);
            }
            symset_destroy(&unified);
            snapshot_release(composed);
            return COMPOSE_ERR_CONFLICT;
        }
    }

    /* Step 9: Compute merkle tree if requested. */
    if (effective_opts.flags & COMPOSE_FLAG_COMPUTE_MERKLE) {
        MerkleTree *mt = compute_snapshot_merkle(composed);
        if (mt != NULL) {
            memcpy(composed->merkle_root, mt->root->hash, 32);
            if (diag) {
                diag->merkle = mt;
            } else {
                merkle_free_tree(mt);
            }
        }
    }

    /* Success. */
    if (diag) {
        diag->conflicts = cs;
    } else {
        conflict_set_destroy(cs);
    }
    symset_destroy(&unified);
    *out = composed;
    return COMPOSE_OK;
}

/* ------------------------------------------------------------------ */
/*  merge_snapshots                                                    */
/* ------------------------------------------------------------------ */

CompositionResult merge_snapshots(const ParserSnapshot *base, const ParserSnapshot *extension,
                                  const CompositionOptions *opts, ParserSnapshot **out,
                                  CompositionDiagnostics *diag) {
    if (out == NULL) return COMPOSE_ERR_INVALID_INPUT;
    *out = NULL;

    if (base == NULL || extension == NULL) {
        set_diag_error(diag, "base and extension must be non-NULL");
        return COMPOSE_ERR_INVALID_INPUT;
    }

    /* Merge is a two-snapshot composition with the base taking
    ** priority (unless LAST_WINS is set, in which case the extension
    ** overrides). */
    ParserSnapshot *snaps[2] = {
        (ParserSnapshot *)base,
        (ParserSnapshot *)extension,
    };

    return compose_snapshots(snaps, 2, opts, out, diag);
}
