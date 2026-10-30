/*
** compose.c -- publish_modified_snapshot(): compose the loaded extension
** set onto a base snapshot.  Built into liblime_compiler, not
** liblime_parser: composing rebuilds the LALR(1) tables in-process, so it
** links the compiler explicitly.  No subprocess, no compiler, no shared
** object: a consumer that composes without liblime_compiler gets a link
** error, never a runtime fallback.
**
** Steps:
**   1. collect each loaded extension's modifications (registry read lock),
**      ordered by extension NAME so the result is a function of the set;
**   2. declaration-level checks (detect_conflicts) + the deprecated
**      on_conflict callbacks;
**   3. serialise each extension to its own text unit and compose with
**      lime_compose_* (src/compose_policy.c): any LALR conflict the base
**      compiled alone does not have must be resolved by the host policy
**      (extension_registry_set_policy), or the compose FAILS -- whatever
**      the base's %expect says;
**   4. bind MOD_ADD_RULE reduce callbacks by rule identity.
** On any failure -- no grammar source on the base, a grammar error in an
** extension, an unresolved conflict, a later compose stage -- it returns
** false with *error set and *out_snap NULL, so whatever snapshot the caller
** is serving stays active.  There is no fallback: the old metadata-only
** path published tables that did not contain the extension.
*/
#include "extension.h"
#include "conflict.h"
#include "lime_compiler.h"
#include "lime_policy.h"
#include "mod_serialize.h"
#include "parser.h"
#include "snapshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int lime_snapshot_rule_by_id(const ParserSnapshot *snap, const char *rule_id);

/* The snapshot carries extension callbacks in a struct spelled out in
** snapshot.h (to keep that header free of extension.h).  Guarantee the
** two function-pointer types stay identical, so the assignment in
** bind_ext_reduce_callbacks() can never become a silent ABI mismatch. */
#if defined(__GNUC__) || defined(__clang__)
_Static_assert(__builtin_types_compatible_p(LimeReduceFn,
                                            __typeof__(((LimeExtReduceBinding *)0)->reduce)),
               "LimeReduceFn and LimeExtReduceBinding.reduce must be the same type");
#endif

/*
** Build "lhs ::= rhs1 rhs2 ..." -- the canonical rule identity that
** lime.c's ruleIdentityString() writes into snapshot->rule_ids[] and that
** lime_snapshot_rule_by_id() matches on.  Caller frees.  NULL on OOM or
** a malformed modification.
*/
static char *mod_rule_identity(const GrammarModification *mod) {
    const char *lhs = mod->u.add_rule.lhs;
    if (lhs == NULL) return NULL;
    size_t len = strlen(lhs) + 4; /* " ::=" */
    for (int i = 0; i < mod->u.add_rule.nrhs; i++) {
        const char *r = mod->u.add_rule.rhs ? mod->u.add_rule.rhs[i] : NULL;
        if (r == NULL) return NULL;
        len += 1 + strlen(r);
    }
    char *buf = malloc(len + 1);
    if (buf == NULL) return NULL;
    char *p = buf;
    p += sprintf(p, "%s ::=", lhs);
    for (int i = 0; i < mod->u.add_rule.nrhs; i++) p += sprintf(p, " %s", mod->u.add_rule.rhs[i]);
    return buf;
}

/*
** Attach each MOD_ADD_RULE's runtime `reduce` callback to the composed
** snapshot, keyed by rule identity -- not by (ruleno - base_nrule), which
** shifts whenever the extension set or unit-rule elimination renumbers
** rules.  Rules with no callback, or whose identity the rebuilt grammar
** doesn't contain (e.g. eliminated as a unit rule), are left unbound:
** they still parse, they just run no extension action.  Best-effort by
** design -- a binding failure must not fail an otherwise-good publish.
*/
static void bind_ext_reduce_callbacks(ParserSnapshot *snap, const GrammarModification *mods,
                                      uint32_t nmods) {
    if (snap == NULL || snap->rule_ids == NULL || snap->nrule == 0) return;
    LimeExtReduceBinding *tab = NULL;
    for (uint32_t m = 0; m < nmods; m++) {
        if (mods[m].type != MOD_ADD_RULE || mods[m].u.add_rule.reduce == NULL) continue;
        char *id = mod_rule_identity(&mods[m]);
        if (id == NULL) continue;
        int ruleno = lime_snapshot_rule_by_id(snap, id);
        free(id);
        if (ruleno < 0 || (uint32_t)ruleno >= snap->nrule) continue;
        if (tab == NULL) {
            tab = calloc(snap->nrule, sizeof(*tab));
            if (tab == NULL) return;
        }
        tab[ruleno].reduce = mods[m].u.add_rule.reduce;
        tab[ruleno].user_data = mods[m].u.add_rule.reduce_user;
    }
    if (tab != NULL) {
        free(snap->ext_reduce);
        snap->ext_reduce = tab;
        snap->ext_reduce_count = snap->nrule;
    }
}

/* ------------------------------------------------------------------ */
/*  publish_modified_snapshot                                          */
/* ------------------------------------------------------------------ */

typedef struct ExtSlice {
    const Extension *e;
    uint32_t first, n; /* range in `combined` */
} ExtSlice;

/* Does a `reject` line drop modification `m` of extension `owner`? */
static int policy_rejects(const LimeComposePolicy *p, const char *owner,
                          const GrammarModification *m) {
    for (uint32_t i = 0; p && owner && i < p->nrejects; i++) {
        const LimePolicyReject *r = &p->rejects[i];
        if (!r->owner || strcmp(r->owner, owner) != 0 || !r->what) continue;
        if (r->is_token && m->type == MOD_ADD_TOKEN && m->u.add_token.name &&
            strcmp(m->u.add_token.name, r->what) == 0)
            return 1;
        if (!r->is_token && m->type == MOD_ADD_RULE) {
            char *id = mod_rule_identity(m);
            int hit = id && strcmp(id, r->what) == 0;
            free(id);
            if (hit) return 1;
        }
    }
    return 0;
}

static ExtensionID ext_id_of(const ExtSlice *sl, uint32_t n, const char *owner) {
    for (uint32_t i = 0; owner && i < n; i++)
        if (sl[i].e->name && strcmp(sl[i].e->name, owner) == 0) return sl[i].e->id;
    return 0; /* base */
}

static int cmp_slice(const void *a, const void *b) {
    const Extension *x = ((const ExtSlice *)a)->e, *y = ((const ExtSlice *)b)->e;
    int c = strcmp(x->name ? x->name : "", y->name ? y->name : "");
    return c ? c : (x->id > y->id) - (x->id < y->id);
}

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

    /* 1. Collect every loaded extension's modifications, in canonical
    ** NAME order (not load order): the merged grammar text -- and so the
    ** token numbering of extension terminals -- is built in this order
    ** (tests/test_compose_order.c).  Names are unique in a registry; ties
    ** fall back to id for a total order. */
    pthread_rwlock_rdlock(&reg->lock);
    uint32_t total = 0, nloaded = 0;
    for (uint32_t i = 0; i < reg->count; i++) {
        if (reg->extensions[i].state == EXT_LOADED) {
            total += reg->extensions[i].nmodifications;
            nloaded++;
        }
    }
    GrammarModification *combined = malloc((total ? total : 1) * sizeof(*combined));
    ExtensionID *ext_id_per_mod = malloc((total ? total : 1) * sizeof(*ext_id_per_mod));
    ExtSlice *slice = calloc(nloaded ? nloaded : 1, sizeof(*slice));
    if (combined == NULL || ext_id_per_mod == NULL || slice == NULL) {
        LIME_RWLOCK_RDUNLOCK(&reg->lock);
        free(combined);
        free(ext_id_per_mod);
        free(slice);
        if (error) *error = strdup("publish_modified_snapshot: out of memory");
        return false;
    }
    uint32_t ns = 0;
    for (uint32_t i = 0; i < reg->count; i++)
        if (reg->extensions[i].state == EXT_LOADED) slice[ns++].e = &reg->extensions[i];
    if (ns > 1) qsort(slice, ns, sizeof(*slice), cmp_slice);
    /* `reject` lines drop an extension's rule or token before generation. */
    const LimeComposePolicy *policy = reg->policy;
    uint32_t k = 0, nrejected = 0;
    for (uint32_t i = 0; i < ns; i++) {
        slice[i].first = k;
        for (uint32_t m = 0; m < slice[i].e->nmodifications; m++) {
            if (policy_rejects(policy, slice[i].e->name, &slice[i].e->modifications[m])) {
                nrejected++;
                continue;
            }
            combined[k] = slice[i].e->modifications[m];
            ext_id_per_mod[k] = slice[i].e->id;
            k++;
        }
        slice[i].n = k - slice[i].first;
    }
    total = k;
    LIME_RWLOCK_RDUNLOCK(&reg->lock);
    (void)nrejected;

    bool ok = false;
    ConflictSet *cs = NULL;
    LimeComposeUnit *units = NULL;
    char **texts = NULL;
    LimeComposeSession *sess = NULL;
    ParserSnapshot *new_snap = NULL;
    char *cm_err = NULL;

    /* 2. Declaration-level conflicts (same token, identical rule,
    ** precedence clash), then the deprecated on_conflict callbacks. */
    cs = conflict_set_create();
    if (cs == NULL) {
        cm_err = strdup("publish_modified_snapshot: conflict_set_create failed");
        goto out;
    }
    detect_conflicts(combined, total, cs);
    for (uint32_t i = 0; i < cs->count; i++) {
        Conflict *c = &cs->conflicts[i];
        if (c->resolved) continue;
        ExtensionID id_a = (c->mod_index_a < total) ? ext_id_per_mod[c->mod_index_a] : 0;
        ExtensionID id_b = (c->mod_index_b < total) ? ext_id_per_mod[c->mod_index_b] : 0;
        c->ext_id_a = id_a;
        c->ext_id_b = id_b;
        ConflictInfo info = {
            .existing_ext = id_a,
            .new_ext = id_b,
            .existing_mod = &combined[c->mod_index_a],
            .new_mod = &combined[c->mod_index_b],
        };
        const Extension *ea = find_extension(reg, id_a);
        if (ea && ea->on_conflict && ea->on_conflict(ea->user_data, &info) != CONFLICT_UNRESOLVED) {
            c->resolved = true;
            continue;
        }
        const Extension *eb = find_extension(reg, id_b);
        if (eb && eb->on_conflict && eb->on_conflict(eb->user_data, &info) != CONFLICT_UNRESOLVED)
            c->resolved = true;
    }
    uint32_t unresolved = conflict_set_unresolved_count(cs);
    if (unresolved > 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "%u unresolved conflict(s) detected", unresolved);
        cm_err = strdup(buf);
        if (out_conflicts) {
            *out_conflicts = cs;
            cs = NULL;
        }
        goto out;
    }

    /* 3. One text unit per extension, so the report can name owners. */
    units = calloc(ns ? ns : 1, sizeof(*units));
    texts = calloc(ns ? ns : 1, sizeof(*texts));
    if (units == NULL || texts == NULL) {
        cm_err = strdup("publish_modified_snapshot: out of memory");
        goto out;
    }
    for (uint32_t i = 0; i < ns; i++) {
        uint32_t skipped = 0;
        char *ferr = NULL;
        texts[i] = lime_modifications_to_grammar_text(combined + slice[i].first, slice[i].n,
                                                      &skipped, &ferr);
        if (texts[i] == NULL) {
            cm_err = ferr ? ferr : strdup("lime_modifications_to_grammar_text returned NULL");
            goto out;
        }
        free(ferr);
        units[i].name = slice[i].e->name;
        units[i].text = texts[i];
        units[i].len = strlen(texts[i]);
    }

    if (base == NULL || base->grammar_source == NULL || base->grammar_source_len == 0) {
        /* Composition rebuilds the tables from source text; there is no
        ** substitute that would not publish tables lacking the extension. */
        cm_err = strdup("publish_modified_snapshot: the base snapshot carries no grammar "
                        "source; build it with `lime -n` or lime_compile_grammar_in_process");
        goto out;
    }
    sess = lime_compose_begin(base->grammar_source, base->grammar_source_len, "base", units, ns,
                              policy, &cm_err);
    if (sess == NULL) goto out;
    {
        size_t mlen = 0;
        const char *mtext = lime_compose_merged_text(sess, &mlen);
        LimeCompileStats st;
        if (lime_compile_grammar_in_process_ex2(mtext, mlen, lime_compose_hook(sess), &new_snap,
                                                &st, &cm_err) != 0) {
            char *where = lime_compose_locate_errors(sess, cm_err);
            if (where) { free(cm_err); cm_err = where; }
            goto out;
        }
    }
    {
        LimeComposeReport *rep = NULL;
        uint32_t nun = lime_compose_finish(sess, &rep);
        if (nun > 0) {
            /* Every extension-introduced conflict must be resolved by the
            ** host policy; the base's %expect never absorbs one. */
            char *txt = lime_compose_report_format(rep);
            size_t n = (txt ? strlen(txt) : 0) + 128;
            cm_err = malloc(n);
            if (cm_err)
                snprintf(cm_err, n,
                         "publish_modified_snapshot: %u extension-introduced LALR conflict(s) "
                         "not resolved by the compose policy:\n%s",
                         nun, txt ? txt : "");
            free(txt);
            if (out_conflicts) {
                for (uint32_t i = 0; rep && i < rep->n; i++) {
                    const LimeComposeEntry *e = &rep->entries[i];
                    if (e->resolution) continue;
                    conflict_set_add(cs, e->kind == LIME_CK_RR ? CONFLICT_REDUCE_REDUCE
                                                               : CONFLICT_SHIFT_REDUCE,
                                     UINT32_MAX, UINT32_MAX, ext_id_of(slice, ns, e->a.owner),
                                     ext_id_of(slice, ns, e->b.owner), e->key);
                }
                *out_conflicts = cs;
                cs = NULL;
            }
            lime_compose_report_free(rep);
            goto out;
        }
        lime_compose_report_free(rep);
    }

    /* SEAM(Track B): lime_lex_compose goes here.  Any non-NULL error from
    ** it must `goto out` (fail the publish; the caller's snapshot stays). */

    /* 4. Bind extension reduce callbacks by rule identity.  Must happen
    ** before `combined` is freed: it holds the only callback pointers. */
    bind_ext_reduce_callbacks(new_snap, combined, total);
    *out_snap = new_snap;
    new_snap = NULL;
    ok = true;

out:
    if (new_snap) snapshot_release(new_snap);
    lime_compose_session_free(sess);
    if (texts)
        for (uint32_t i = 0; i < ns; i++) free(texts[i]);
    free(texts);
    free(units);
    conflict_set_destroy(cs);
    free(slice);
    free(combined);
    free(ext_id_per_mod);
    if (!ok) {
        if (error) *error = cm_err ? cm_err : strdup("publish_modified_snapshot failed");
        else free(cm_err);
    } else {
        free(cm_err);
    }
    return ok;
}

/* ------------------------------------------------------------------ */
/*  lime_compose_text: compose plain grammar texts                     */
/* ------------------------------------------------------------------ */

int lime_compose_text(const char *base_text, size_t base_len, const LimeComposeUnit *units,
                      uint32_t nunits, const LimeComposePolicy *policy,
                      struct ParserSnapshot **out, LimeComposeReport **report, char **error) {
    if (out) *out = NULL;
    if (report) *report = NULL;
    if (error) *error = NULL;
    if (out == NULL) {
        if (error) *error = strdup("lime_compose_text: out is required");
        return -1;
    }
    if (policy && policy->nrejects) {
        /* reject filters an extension's modifications; text units have none */
        if (error) *error = strdup("lime_compose_text: `reject` lines need publish_modified_snapshot "
                                   "(text units carry no modification list)");
        return -1;
    }
    LimeComposeSession *s =
        lime_compose_begin(base_text, base_len, "base", units, nunits, policy, error);
    if (s == NULL) return -1;
    size_t mlen = 0;
    const char *m = lime_compose_merged_text(s, &mlen);
    LimeCompileStats st;
    ParserSnapshot *snap = NULL;
    if (lime_compile_grammar_in_process_ex2(m, mlen, lime_compose_hook(s), &snap, &st, error) != 0) {
        if (error && *error) {
            char *where = lime_compose_locate_errors(s, *error);
            if (where) { free(*error); *error = where; }
        }
        lime_compose_session_free(s);
        return -1;
    }
    uint32_t nun = lime_compose_finish(s, report);
    lime_compose_session_free(s);
    if (nun > 0) {
        snapshot_release(snap);
        if (error) {
            char buf[128];
            snprintf(buf, sizeof buf,
                     "%u extension-introduced LALR conflict(s) not resolved by the compose policy",
                     nun);
            *error = strdup(buf);
        }
        return 1;
    }
    *out = snap;
    return 0;
}
