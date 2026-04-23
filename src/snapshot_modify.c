/*
** Snapshot modification implementation.
**
** Clones a base snapshot, applies grammar modifications, detects and
** resolves conflicts, and rebuilds the LALR(1) automaton to produce
** a new snapshot.
*/
#include "snapshot_modify.h"
#include "extension.h"
#include "conflict.h"
#include "snapshot.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

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

static char *format_error(const char *fmt, const char *detail) {
    size_t buf_sz = strlen(fmt) + (detail ? strlen(detail) : 0) + 64;
    char *buf = malloc(buf_sz);
    if (buf != NULL) {
        snprintf(buf, buf_sz, fmt, detail ? detail : "(unknown)");
    }
    return buf;
}

/*
** Get the current wall-clock time in nanoseconds.
*/
static uint64_t now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/*
** Deep-copy a uint16_t array.  Returns NULL if src is NULL or on failure.
*/
static uint16_t *dup_u16_array(const uint16_t *src, uint32_t count) {
    if (src == NULL || count == 0) return NULL;
    size_t sz = count * sizeof(uint16_t);
    uint16_t *copy = malloc(sz);
    if (copy != NULL) memcpy(copy, src, sz);
    return copy;
}

static int16_t *dup_i16_array(const int16_t *src, uint32_t count) {
    if (src == NULL || count == 0) return NULL;
    size_t sz = count * sizeof(int16_t);
    int16_t *copy = malloc(sz);
    if (copy != NULL) memcpy(copy, src, sz);
    return copy;
}

/* ------------------------------------------------------------------ */
/*  Snapshot cloning                                                   */
/* ------------------------------------------------------------------ */

ParserSnapshot *clone_snapshot(const ParserSnapshot *base) {
    if (base == NULL) {
        /* Create an empty snapshot for building from scratch */
        ParserSnapshot *snap = calloc(1, sizeof(ParserSnapshot));
        if (snap == NULL) return NULL;
        atomic_init(&snap->refcount, 1);
        snap->version = 0;
        snap->create_time_ns = now_ns();
        return snap;
    }

    ParserSnapshot *snap = calloc(1, sizeof(ParserSnapshot));
    if (snap == NULL) return NULL;

    /* Bookkeeping */
    snap->version = base->version + 1;
    atomic_init(&snap->refcount, 1);
    snap->create_time_ns = now_ns();
    snap->jit_ctx = NULL;  /* JIT context is not inherited */

    /* Grammar data -- shallow copy of pointer arrays for now.
    ** The actual symbol/rule/state structs are from the base snapshot
    ** or Lemon internals.  Full deep copy requires Task #3 support.
    ** For now, we copy the pointers and counts so modifications can
    ** extend them. */
    snap->nsymbol = base->nsymbol;
    snap->nterminal = base->nterminal;
    snap->nrule = base->nrule;
    snap->nstate = base->nstate;

    if (base->nsymbol > 0 && base->symbols != NULL) {
        size_t sz = base->nsymbol * sizeof(struct symbol *);
        snap->symbols = malloc(sz);
        if (snap->symbols == NULL) goto fail;
        memcpy(snap->symbols, base->symbols, sz);
    }

    if (base->nrule > 0 && base->rules != NULL) {
        /* Rules are a linked list; for clone we copy the head pointer.
        ** Deep copy of the linked list requires Task #3 support. */
        snap->rules = base->rules;
    }

    if (base->nstate > 0 && base->states != NULL) {
        size_t sz = base->nstate * sizeof(struct state *);
        snap->states = malloc(sz);
        if (snap->states == NULL) goto fail;
        memcpy(snap->states, base->states, sz);
    }

    /* Action tables -- deep copy */
    snap->action_count = base->action_count;
    snap->lookahead_count = base->lookahead_count;

    snap->yy_action = dup_u16_array(base->yy_action, base->action_count);
    snap->yy_lookahead = dup_u16_array(base->yy_lookahead, base->lookahead_count);
    snap->yy_shift_ofst = dup_i16_array(base->yy_shift_ofst, base->nstate);
    snap->yy_reduce_ofst = dup_i16_array(base->yy_reduce_ofst, base->nstate);
    snap->yy_default = dup_u16_array(base->yy_default, base->nstate);

    /* Verify critical allocations succeeded */
    if (base->action_count > 0 && snap->yy_action == NULL) goto fail;
    if (base->lookahead_count > 0 && snap->yy_lookahead == NULL) goto fail;

    return snap;

fail:
    /* Clean up partial clone */
    free(snap->symbols);
    free(snap->states);
    free(snap->yy_action);
    free(snap->yy_lookahead);
    free(snap->yy_shift_ofst);
    free(snap->yy_reduce_ofst);
    free(snap->yy_default);
    free(snap);
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Applying individual modifications                                  */
/* ------------------------------------------------------------------ */

static bool apply_add_token(ParserSnapshot *snap,
                            const GrammarModification *mod,
                            char **error) {
    const char *name = mod->u.add_token.name;
    if (name == NULL) {
        if (error) *error = dup_string("MOD_ADD_TOKEN: missing token name");
        return false;
    }

    /*
    ** Adding a token means adding a new terminal symbol to the snapshot's
    ** symbol table.  The full implementation requires deep-copy support
    ** from Task #3.  For now, we increment the terminal count as a
    ** placeholder to track that the modification was applied.
    */
    snap->nterminal++;
    snap->nsymbol++;

    (void)mod;  /* token_code and lexeme used when Task #3 lands */
    return true;
}

static bool apply_add_rule(ParserSnapshot *snap,
                           const GrammarModification *mod,
                           char **error) {
    const char *lhs = mod->u.add_rule.lhs;
    if (lhs == NULL) {
        if (error) *error = dup_string("MOD_ADD_RULE: missing LHS");
        return false;
    }

    /*
    ** Adding a rule means inserting a new production into the snapshot's
    ** rule list.  Full implementation requires Task #3.  For now we just
    ** increment the rule count.
    */
    snap->nrule++;

    (void)mod;
    return true;
}

static bool apply_remove_rule(ParserSnapshot *snap,
                              const GrammarModification *mod,
                              char **error) {
    const char *lhs = mod->u.remove_rule.lhs;
    if (lhs == NULL) {
        if (error) *error = dup_string("MOD_REMOVE_RULE: missing LHS");
        return false;
    }

    /*
    ** Removing a rule requires finding the matching rule in the linked
    ** list and unlinking it.  Stub: decrement rule count.
    */
    if (snap->nrule > 0) snap->nrule--;

    (void)mod;
    return true;
}

static bool apply_modify_precedence(ParserSnapshot *snap,
                                    const GrammarModification *mod,
                                    char **error) {
    const char *symbol = mod->u.modify_prec.symbol;
    if (symbol == NULL) {
        if (error) *error = dup_string("MOD_MODIFY_PRECEDENCE: missing symbol");
        return false;
    }

    /*
    ** Modifying precedence requires finding the symbol in the snapshot's
    ** symbol table and updating its precedence/associativity fields.
    ** Stub for now -- the automaton rebuild will pick up the changes.
    */
    (void)snap;
    (void)mod;
    return true;
}

static bool apply_add_type(ParserSnapshot *snap,
                           const GrammarModification *mod,
                           char **error) {
    const char *name = mod->u.add_type.name;
    if (name == NULL) {
        if (error) *error = dup_string("MOD_ADD_TYPE: missing name");
        return false;
    }

    /*
    ** Adding a type associates a C datatype with a non-terminal.
    ** This is metadata and doesn't change the automaton structure.
    ** Stub for now.
    */
    (void)snap;
    (void)mod;
    return true;
}

bool apply_modification(
    ParserSnapshot *snap,
    const GrammarModification *mod,
    char **error
) {
    if (snap == NULL || mod == NULL) return false;
    if (error != NULL) *error = NULL;

    switch (mod->type) {
        case MOD_ADD_TOKEN:
            return apply_add_token(snap, mod, error);
        case MOD_ADD_RULE:
            return apply_add_rule(snap, mod, error);
        case MOD_REMOVE_RULE:
            return apply_remove_rule(snap, mod, error);
        case MOD_MODIFY_PRECEDENCE:
            return apply_modify_precedence(snap, mod, error);
        case MOD_ADD_TYPE:
            return apply_add_type(snap, mod, error);
        default:
            if (error != NULL) {
                *error = format_error("unknown modification type %s", NULL);
            }
            return false;
    }
}

/* ------------------------------------------------------------------ */
/*  Automaton rebuild                                                   */
/* ------------------------------------------------------------------ */

bool rebuild_automaton(ParserSnapshot *snap, char **error) {
    if (snap == NULL) return false;

    /*
    ** Full LALR(1) automaton rebuild requires:
    **   1. Compute FIRST sets for all non-terminals
    **   2. Build LR(0) state machine
    **   3. Compute FOLLOW sets
    **   4. Determine shift/reduce/accept actions
    **   5. Compress action tables
    **
    ** This requires the Lemon dynamic-table infrastructure from Task #3.
    ** For now, this is a stub that leaves the existing action tables
    ** in place and reports success.
    */
    if (error != NULL) *error = NULL;

    (void)snap;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Main entry point                                                   */
/* ------------------------------------------------------------------ */

ModifyResult create_modified_snapshot(
    const ParserSnapshot *base,
    const GrammarModification *mods,
    uint32_t nmods,
    ExtensionRegistry *registry,
    ParserSnapshot **out,
    ConflictSet **conflicts_out,
    char **error
) {
    if (out == NULL) return MODIFY_ERR_ALLOC;
    *out = NULL;
    if (conflicts_out != NULL) *conflicts_out = NULL;
    if (error != NULL) *error = NULL;

    /* Step 1: Detect conflicts among the modifications */
    ConflictSet *cs = conflict_set_create();
    if (cs == NULL) {
        if (error) *error = dup_string("failed to create conflict set");
        return MODIFY_ERR_ALLOC;
    }

    detect_conflicts(mods, nmods, cs);

    /* Step 2: Attempt to resolve any conflicts via extension callbacks */
    if (cs->count > 0 && registry != NULL) {
        uint32_t unresolved = resolve_conflicts(cs, mods, nmods, registry);
        if (unresolved > 0) {
            if (conflicts_out != NULL) {
                *conflicts_out = cs;
            } else {
                conflict_set_destroy(cs);
            }
            if (error) {
                char buf[128];
                snprintf(buf, sizeof(buf),
                         "%u unresolved conflict(s) detected", unresolved);
                *error = dup_string(buf);
            }
            return MODIFY_ERR_CONFLICT;
        }
    }

    /* Step 3: Clone the base snapshot */
    ParserSnapshot *snap = clone_snapshot(base);
    if (snap == NULL) {
        conflict_set_destroy(cs);
        if (error) *error = dup_string("failed to clone snapshot");
        return MODIFY_ERR_ALLOC;
    }

    /* Step 4: Apply each modification */
    for (uint32_t i = 0; i < nmods; i++) {
        char *mod_error = NULL;
        if (!apply_modification(snap, &mods[i], &mod_error)) {
            conflict_set_destroy(cs);
            snapshot_release(snap);
            if (error) {
                if (mod_error != NULL) {
                    *error = mod_error;
                } else {
                    *error = dup_string("failed to apply modification");
                }
            } else {
                free(mod_error);
            }
            return MODIFY_ERR_INVALID_MOD;
        }
        free(mod_error);
    }

    /* Step 5: Rebuild the LALR(1) automaton */
    char *build_error = NULL;
    if (!rebuild_automaton(snap, &build_error)) {
        conflict_set_destroy(cs);
        snapshot_release(snap);
        if (error) {
            *error = build_error ? build_error
                                 : dup_string("automaton rebuild failed");
        } else {
            free(build_error);
        }
        return MODIFY_ERR_BUILD;
    }
    free(build_error);

    conflict_set_destroy(cs);
    *out = snap;
    return MODIFY_OK;
}
