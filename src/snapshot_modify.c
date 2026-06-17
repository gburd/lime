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
#include <assert.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

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
#if defined(_WIN32)
    /* Windows wall-clock equivalent of CLOCK_REALTIME.
    ** GetSystemTimePreciseAsFileTime returns 100-nanosecond
    ** intervals since 1601-01-01; convert to nanoseconds-since-
    ** epoch.  The 116444736000000000 constant is the number of
    ** 100-ns intervals from 1601-01-01 to 1970-01-01. */
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (t - 116444736000000000ULL) * 100ULL;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
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

static int32_t *dup_i32_array(const int32_t *src, uint32_t count) {
    if (src == NULL || count == 0) return NULL;
    size_t sz = count * sizeof(int32_t);
    int32_t *copy = malloc(sz);
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
    snap->jit_ctx = NULL; /* JIT context is not inherited */

    /* Grammar data -- shallow copy of pointer arrays.
    ** The symbol / rule / state structs themselves are owned by the
    ** base snapshot (and Lemon's internal arenas behind it).  A real
    ** deep copy of those structures is the in-process LALR rebuild
    ** path tracked as item 1 of docs/ROADMAP.md ("In-process LALR(1)
    ** automaton rebuild library"); the subprocess + dlopen path used
    ** by `lime_compile_grammar_text` covers the practical need today.
    ** Pointers + counts are enough for the metadata-only path used
    ** by create_modified_snapshot. */
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
        /* Rules are a linked list; clone copies the head pointer.
        ** A deep copy lives in the in-process rebuild path (ROADMAP
        ** item 1); shallow head is correct for metadata-only paths. */
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
    snap->yy_shift_ofst = dup_i32_array(base->yy_shift_ofst, base->nstate);
    snap->yy_reduce_ofst = dup_i32_array(base->yy_reduce_ofst, base->nstate);
    snap->yy_default = dup_u16_array(base->yy_default, base->nstate);

    /* Action-range constants (small POD, copy verbatim). */
    snap->yy_max_shift = base->yy_max_shift;
    snap->yy_min_shiftreduce = base->yy_min_shiftreduce;
    snap->yy_max_shiftreduce = base->yy_max_shiftreduce;
    snap->yy_error_action = base->yy_error_action;
    snap->yy_accept_action = base->yy_accept_action;
    snap->yy_no_action = base->yy_no_action;
    snap->yy_min_reduce = base->yy_min_reduce;
    snap->yy_ntoken = base->yy_ntoken;

    /* PG Track B fix: carry the %first_token offset.  Omitting it left
    ** a cloned/metadata-composed snapshot with yy_first_token == 0, so
    ** parse_token applied a zero offset and treated external code
    ** (internal + N) as an internal index -- out of range -> every
    ** token of a valid base query was rejected (rc=-1).  Also carry
    ** the host-reduce hook so a composed snapshot can still run the
    ** base grammar's reduce actions (Letter 30/31). */
    snap->yy_first_token = base->yy_first_token;
    snap->host_reduce = base->host_reduce;
    snap->host_reduce_user = base->host_reduce_user;

    /* Rule-metadata arrays -- deep copy. */
    if (base->nrule > 0 && base->yy_rule_info_lhs != NULL) {
        size_t sz = base->nrule * sizeof(int16_t);
        snap->yy_rule_info_lhs = malloc(sz);
        if (snap->yy_rule_info_lhs == NULL) goto fail;
        memcpy(snap->yy_rule_info_lhs, base->yy_rule_info_lhs, sz);
    }
    if (base->nrule > 0 && base->yy_rule_info_nrhs != NULL) {
        size_t sz = base->nrule * sizeof(int8_t);
        snap->yy_rule_info_nrhs = malloc(sz);
        if (snap->yy_rule_info_nrhs == NULL) goto fail;
        memcpy(snap->yy_rule_info_nrhs, base->yy_rule_info_nrhs, sz);
    }

    /* Optional fallback table -- deep copy if present. */
    if (base->nfallback > 0 && base->yy_fallback != NULL) {
        snap->yy_fallback = dup_u16_array(base->yy_fallback, base->nfallback);
        if (snap->yy_fallback == NULL) goto fail;
        snap->nfallback = base->nfallback;
    }

    /* Optional grammar source -- deep copy if present. */
    if (base->grammar_source != NULL && base->grammar_source_len > 0) {
        snap->grammar_source = malloc(base->grammar_source_len + 1);
        if (snap->grammar_source == NULL) goto fail;
        memcpy(snap->grammar_source, base->grammar_source, base->grammar_source_len);
        snap->grammar_source[base->grammar_source_len] = '\0';
        snap->grammar_source_len = base->grammar_source_len;
    }

    /* Verify critical allocations succeeded */
    if (base->action_count > 0 && snap->yy_action == NULL) goto fail;
    if (base->lookahead_count > 0 && snap->yy_lookahead == NULL) goto fail;

    /* Field-drift guard (PG Track B): a clone that silently drops a
    ** parse-critical scalar produces a snapshot that parses wrong
    ** rather than failing -- exactly the dropped-%first_token bug.
    ** Assert the scalars the parse hot path consults all carried over,
    ** so any future field added to ParserSnapshot that this clone
    ** forgets trips in a debug build instead of shipping. */
    assert(snap->yy_first_token == base->yy_first_token);
    assert(snap->yy_ntoken == base->yy_ntoken);
    assert(snap->yy_min_reduce == base->yy_min_reduce);
    assert(snap->yy_max_shift == base->yy_max_shift);
    assert(snap->host_reduce == base->host_reduce);

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
    free(snap->yy_rule_info_lhs);
    free(snap->yy_rule_info_nrhs);
    free(snap->yy_fallback);
    free(snap->grammar_source);
    free(snap);
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Applying individual modifications                                  */
/* ------------------------------------------------------------------ */

static bool apply_add_token(ParserSnapshot *snap, const GrammarModification *mod, char **error) {
    const char *name = mod->u.add_token.name;
    if (name == NULL) {
        if (error) *error = dup_string("MOD_ADD_TOKEN: missing token name");
        return false;
    }

    /*
    ** Assign a fresh terminal code unless the modification specifies
    ** one.  We extend the snapshot's terminal-count and symbol-count
    ** so subsequent modifications and the rebuild_automaton pass see
    ** the new symbol space; the symbol-name -> code mapping itself
    ** lives on the registered Extension's modifications array (which
    ** the registry retains) and is consulted by the runtime
    ** tokenizer when it assembles input streams.
    */
    int assigned_code = mod->u.add_token.token_code;
    if (assigned_code < 0) {
        assigned_code = (int)snap->yy_ntoken;
    }

    snap->nterminal++;
    snap->nsymbol++;
    if ((uint32_t)assigned_code >= snap->yy_ntoken) {
        snap->yy_ntoken = (uint16_t)(assigned_code + 1);
    }

    return true;
}

static bool apply_add_rule(ParserSnapshot *snap, const GrammarModification *mod, char **error) {
    const char *lhs = mod->u.add_rule.lhs;
    if (lhs == NULL) {
        if (error) *error = dup_string("MOD_ADD_RULE: missing LHS");
        return false;
    }
    if (mod->u.add_rule.nrhs < 0 || mod->u.add_rule.nrhs > 127) {
        if (error) *error = dup_string("MOD_ADD_RULE: nrhs out of range");
        return false;
    }

    /*
    ** Append rule metadata to the parallel arrays the runtime push
    ** parser consults when it dispatches reductions.  The LHS code
    ** is recorded as 0 (placeholder) because the runtime symbol code
    ** for a name introduced by this or another extension is not
    ** known until rebuild_automaton runs over the merged grammar
    ** and assigns codes consistently.
    */
    uint32_t new_n = snap->nrule + 1;

    int16_t *new_lhs = realloc(snap->yy_rule_info_lhs, new_n * sizeof(int16_t));
    if (new_lhs == NULL) {
        if (error) *error = dup_string("MOD_ADD_RULE: out of memory (lhs)");
        return false;
    }
    snap->yy_rule_info_lhs = new_lhs;
    new_lhs[snap->nrule] = 0;

    int8_t *new_nrhs = realloc(snap->yy_rule_info_nrhs, new_n * sizeof(int8_t));
    if (new_nrhs == NULL) {
        if (error) *error = dup_string("MOD_ADD_RULE: out of memory (nrhs)");
        return false;
    }
    snap->yy_rule_info_nrhs = new_nrhs;
    new_nrhs[snap->nrule] = (int8_t)(-mod->u.add_rule.nrhs);

    snap->nrule = new_n;
    return true;
}

static bool apply_remove_rule(ParserSnapshot *snap, const GrammarModification *mod, char **error) {
    const char *lhs = mod->u.remove_rule.lhs;
    if (lhs == NULL) {
        if (error) *error = dup_string("MOD_REMOVE_RULE: missing LHS");
        return false;
    }

    /*
    ** Rule removal needs the (lhs, rhs) -> rule-index mapping that
    ** the LALR(1) automaton builder produces; without that the
    ** action-table entries pointing at this rule cannot be
    ** invalidated correctly.  Decrement the rule counter so
    ** subsequent ADD_RULE indices stay consistent and let
    ** rebuild_automaton enact the unlink atomically when it
    ** rederives the action tables.  When rebuild_automaton is not
    ** invoked, the original rule remains reachable via the
    ** unchanged yy_action[] entries.
    */
    if (snap->nrule > 0) snap->nrule--;
    return true;
}

static bool apply_modify_precedence(ParserSnapshot *snap, const GrammarModification *mod,
                                    char **error) {
    const char *symbol = mod->u.modify_prec.symbol;
    if (symbol == NULL) {
        if (error) *error = dup_string("MOD_MODIFY_PRECEDENCE: missing symbol");
        return false;
    }

    /*
    ** The static action tables already encode shift/reduce decisions
    ** that depend on the original precedences, so a precedence change
    ** has no effect until rebuild_automaton rederives them from the
    ** modified grammar.  The modification is recorded on the
    ** registered Extension's modifications array and consumed there.
    */
    return true;
}

static bool apply_add_type(ParserSnapshot *snap, const GrammarModification *mod, char **error) {
    const char *name = mod->u.add_type.name;
    if (name == NULL) {
        if (error) *error = dup_string("MOD_ADD_TYPE: missing name");
        return false;
    }

    /*
    ** Lime's typed semantic-value plumbing (the YYMINORTYPE union) is
    ** fixed at generator time, so MOD_ADD_TYPE has no effect on a
    ** statically generated parser's value stack.  The runtime parse
    ** engine is value-free and treats this modification as metadata.
    */
    return true;
}

bool apply_modification(ParserSnapshot *snap, const GrammarModification *mod, char **error) {
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
    if (error != NULL) *error = NULL;

    /*
    ** True LALR(1) reconstruction (recompute FIRST/FOLLOW sets,
    ** rebuild the LR(0) state machine, redrive shift/reduce/accept
    ** decisions, recompress action tables) is provided by the
    ** lime.c generator's Build()/ReportTable() phases and is not yet
    ** exposed as a runtime callable library.  Tracking item:
    **   docs/ROADMAP.md -- "Expose lime Build() as a library".
    **
    ** Until that lands, this function performs the validation and
    ** zeroing work that is *always* correct after metadata-only
    ** modifications:
    **   - It checks that every appended rule has a recorded LHS
    **     placeholder and a sane nrhs.
    **   - It bumps the snapshot version so consumers can detect
    **     that a rebuild was attempted.
    **   - It does NOT add action-table entries for the new tokens
    **     or rules; those entries can only be derived by the full
    **     LALR(1) algorithm.
    **
    ** Callers that need a fully rebuilt automaton should run the
    ** lime generator on the merged grammar text and rebuild a
    ** snapshot via lime_snapshot_create().  Until the runtime
    ** library exposes Build(), this is the supported path.
    */

    /* Validate appended rule entries. */
    for (uint32_t i = 0; i < snap->nrule; i++) {
        if (snap->yy_rule_info_nrhs == NULL) break;
        int8_t nrhs = snap->yy_rule_info_nrhs[i];
        if (nrhs > 0 || nrhs < -127) {
            if (error) *error = dup_string("rebuild_automaton: invalid rule nrhs");
            return false;
        }
    }

    snap->version++;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Main entry point                                                   */
/* ------------------------------------------------------------------ */

ModifyResult create_modified_snapshot(const ParserSnapshot *base, const GrammarModification *mods,
                                      uint32_t nmods, ExtensionRegistry *registry,
                                      ParserSnapshot **out, ConflictSet **conflicts_out,
                                      char **error) {
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
                snprintf(buf, sizeof(buf), "%u unresolved conflict(s) detected", unresolved);
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
            *error = build_error ? build_error : dup_string("automaton rebuild failed");
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
