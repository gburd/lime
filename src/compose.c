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
**   2. declaration-level conflicts: the host policy first (C4), then the
**      legacy check, where extensions' deprecated on_conflict answers are
**      proposals applied only without a host policy or when trusted (C5);
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
#include "multi_grammar.h"
#include "parser.h"
#include "snapshot.h"

#include <stdarg.h>
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
char *compose_rule_identity(const GrammarModification *mod);
static char *mod_rule_identity(const GrammarModification *mod) { return compose_rule_identity(mod); }
char *compose_rule_identity(const GrammarModification *mod) {
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
** rules -- and compute ext_action[] (see snapshot.h).  Rules with no
** callback, or whose identity the rebuilt grammar doesn't contain (e.g.
** eliminated as a unit rule), are left unbound: they still parse, they
** just run no extension action.  Best-effort by design -- a binding
** failure must not fail an otherwise-good publish.
*/
static void bind_ext_reduce_callbacks(ParserSnapshot *snap, const GrammarModification *mods,
                                      uint32_t nmods) {
    if (snap == NULL || snap->rule_ids == NULL || snap->nrule == 0) return;
    LimeExtReduceBinding *tab = NULL;
    uint8_t *act = NULL;
    for (uint32_t m = 0; m < nmods; m++) {
        if (mods[m].type != MOD_ADD_RULE || mods[m].u.add_rule.reduce == NULL) continue;
        char *id = mod_rule_identity(&mods[m]);
        if (id == NULL) continue;
        int ruleno = lime_snapshot_rule_by_id(snap, id);
        free(id);
        if (ruleno < 0 || (uint32_t)ruleno >= snap->nrule) continue;
        if (tab == NULL) {
            /* one block: the bindings, then the per-rule action bytes */
            tab = calloc(1, snap->nrule * (sizeof(*tab) + 1));
            if (tab == NULL) return;
            act = (uint8_t *)(tab + snap->nrule);
        }
        tab[ruleno].reduce = mods[m].u.add_rule.reduce;
        tab[ruleno].user_data = mods[m].u.add_rule.reduce_user;
        act[ruleno] = 1;
    }
    if (tab == NULL) return;
    /* Every unbound rule with an RHS passes $1 through to $$ (so an
    ** extension value can climb through base unit rules); epsilon rules
    ** produce NULL.  Exactly the behaviour reduce() had before the table. */
    for (uint32_t r = 0; r < snap->nrule; r++)
        if (act[r] == 0 && snap->yy_rule_info_nrhs[r] != 0) act[r] = 2;
    free(snap->ext_reduce);
    snap->ext_reduce = tab;
    snap->ext_reduce_count = snap->nrule;
    snap->ext_action = act;
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


/* ------------------------------------------------------------------ */
/*  Declaration-level conflicts (C4)                                   */
/* ------------------------------------------------------------------ */
/*
** Settled before generation; each becomes a plain edit of the
** modification list, so nothing reaches the tables but ordinary
** tokens/rules/precedence:
**
**   (shared)       one token name+lexeme, or one rule with one action,
**                  declared by two extensions: merged, not a conflict.
**   TOKEN_LEXEME   two different tokens claim one lexeme (ASCII case-
**                  folded): `keyword "x" owner E soft|reserved.` -- E's
**                  token keeps the lexeme; the others keep their terminal
**                  but no lexeme reaches it (their rules can only be
**                  reached by a scanner that emits that token by name).
**                  `soft` makes E's token fall back to the base's
**                  identifier token (its own fallback_to, else "ID");
**                  `reserved` clears the fallback.  Different ranks also
**                  settle it; equal ranks, or no decision, fail.
**   DUP_RULE       one rule identity, two different actions (code or
**                  reduce callback): `conflict rule "<r>" prefer E.` or
**                  rank keeps one copy; equal ranks fail.
**   PREC_CLASH     two different MOD_MODIFY_PRECEDENCE for one terminal:
**                  a host `token T ...` line replaces both; else rank;
**                  equal ranks fail.
** Conflicts the policy does not address (no keyword/conflict/token line,
** no ranks) other than TOKEN_LEXEME fall through to the legacy check and
** the deprecated on_conflict callbacks (step 2b).
**
** SEAM(Track B): keyword ownership.  The policy is the only authority on
** who owns a lexeme; Track B's keyword table (kw_table, built by
** lime_lex_compose at the SEAM in publish_modified_snapshot) must not
** re-decide.  The decision is handed over in the modification list that
** reaches lime_lex_compose (`combined`, after step 2a): for each lexeme
** exactly one MOD_ADD_TOKEN carries it (losers have lexeme == NULL), and
** a soft owner carries fallback_to.  B1's builder already reads base
** %keyword + MOD_ADD_TOKEN.lexeme, so it needs no extra input.  Two
** claims on a BASE keyword (an extension token reusing a base lexeme) are
** for B to report, since only the lexer side knows the base keyword set:
** it should fail the compose unless `keyword "x" owner E` names E.
*/
typedef struct DeclOwner { const char *owner; int rank; } DeclOwner;

static int lex_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return 0;
    }
    return *a == *b;
}

static int same_action(const GrammarModification *a, const GrammarModification *b) {
    if (a->u.add_rule.reduce != b->u.add_rule.reduce) return 0;
    if (a->u.add_rule.reduce && a->u.add_rule.reduce_user != b->u.add_rule.reduce_user) return 0;
    const char *x = a->u.add_rule.code, *y = b->u.add_rule.code;
    return (x == NULL && y == NULL) || (x && y && strcmp(x, y) == 0);
}

static const char *owner_of(const ExtSlice *sl, uint32_t ns, uint32_t m) {
    for (uint32_t i = 0; i < ns; i++)
        if (m >= sl[i].first && m < sl[i].first + sl[i].n) return sl[i].e->name;
    return "?";
}

static void appendf(char **buf, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int add = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    size_t old = *buf ? strlen(*buf) : 0;
    char *n = add >= 0 ? realloc(*buf, old + (size_t)add + 1) : NULL;
    if (n) {
        vsnprintf(n + old, (size_t)add + 1, fmt, ap);
        *buf = n;
    }
    va_end(ap);
}

/* Who wins between owners x and y under p: 1 = x, 2 = y, 0 = no ranks,
** -1 = equal-rank tie. */
static int rank_winner(const LimeComposePolicy *p, const char *x, const char *y) {
    if (!p || !p->nranks) return 0;
    int rx = lime_policy_rank(p, x), ry = lime_policy_rank(p, y);
    return rx > ry ? 1 : rx < ry ? 2 : -1;
}

/*
** Apply the declaration-level policy to mods[0..*total): drops losing
** modifications (and lexemes) in place.  Conflicts the policy does not
** address are left for the legacy check + on_conflict (step 2b), except
** TOKEN_LEXEME, which only the policy can settle, and equal-rank ties,
** which are errors.  Returns 0, or -1 with *err listing what failed.
*/
static int compose_decide_declarations(const LimeComposePolicy *p, GrammarModification *mods,
                                       ExtensionID *ids, uint32_t *total, ExtSlice *sl,
                                       uint32_t ns, char **err) {
    uint32_t n = *total;
    char *drop = calloc(n ? n : 1, 1);
    if (!drop) return -1;
    int bad = 0;
    for (uint32_t i = 0; i < n; i++) {
        for (uint32_t j = i + 1; j < n; j++) {
            GrammarModification *a = &mods[i], *b = &mods[j];
            const char *oa = owner_of(sl, ns, i), *ob = owner_of(sl, ns, j);
            if (drop[i] || drop[j] || a->type != b->type) continue;
            int same_owner = strcmp(oa, ob) == 0;

            if (a->type == MOD_ADD_TOKEN) {
                const char *na = a->u.add_token.name, *nb = b->u.add_token.name;
                const char *la = a->u.add_token.lexeme, *lb = b->u.add_token.lexeme;
                int same_name = na && nb && strcmp(na, nb) == 0;
                int same_lex = la && lb && lex_eq(la, lb);
                if (same_name && (same_lex || (!la && !lb))) {
                    /* one token declared twice: shared, not a conflict */
                    drop[j] = 1;
                    continue;
                }
                if (!same_lex || same_name || same_owner) continue; /* TOKEN_NAME: legacy check */
                /* TOKEN_LEXEME: two tokens claim one lexeme */
                const LimePolicyKeyword *kw = NULL;
                for (uint32_t k = 0; p && k < p->nkeywords; k++)
                    if (lex_eq(p->keywords[k].lexeme, la)) kw = &p->keywords[k];
                int w = kw ? (strcmp(kw->owner, oa) == 0 ? 1 : strcmp(kw->owner, ob) == 0 ? 2 : 0)
                           : rank_winner(p, oa, ob);
                if (w > 0) {
                    GrammarModification *win = w == 1 ? a : b, *lose = w == 1 ? b : a;
                    lose->u.add_token.lexeme = NULL; /* keeps its terminal; no lexeme reaches it */
                    if (kw && kw->soft && !win->u.add_token.fallback_to) win->u.add_token.fallback_to = "ID";
                    if (kw && !kw->soft) win->u.add_token.fallback_to = NULL;
                    continue;
                }
                bad = 1;
                appendf(err, "TOKEN_LEXEME  \"%s\" claimed by %s (%s) and %s (%s)%s\n"
                             "  fix   : keyword \"%s\" owner <ext> soft|reserved.\n",
                        la, na, oa, nb, ob, w < 0 ? "; equal rank is an error" : "", la);
                continue;
            }
            if (a->type == MOD_ADD_RULE) {
                char *ida = compose_rule_identity(a), *idb = compose_rule_identity(b);
                int same = ida && idb && strcmp(ida, idb) == 0;
                if (same && same_action(a, b)) drop[j] = 1; /* one rule, not a conflict */
                if (!same || same_action(a, b) || same_owner) { free(ida); free(idb); continue; }
                /* DUP_RULE_ACTION */
                int w = 0;
                for (uint32_t k = 0; p && k < p->nconflicts; k++) {
                    const LimePolicyConflict *c = &p->conflicts[k];
                    if (c->kind == LIME_DK_DUP_RULE_ACTION && c->rule && c->owner &&
                        strcmp(c->rule, ida) == 0)
                        w = strcmp(c->owner, oa) == 0 ? 1 : strcmp(c->owner, ob) == 0 ? 2 : 0;
                }
                if (!w) w = rank_winner(p, oa, ob);
                if (w > 0) {
                    drop[w == 1 ? j : i] = 1;
                } else if (w < 0) {
                    bad = 1;
                    appendf(err, "DUP_RULE_ACTION  \"%s\" from %s and %s with different actions; "
                                 "equal rank is an error\n  fix   : conflict rule \"%s\" prefer <ext>.\n",
                            ida, oa, ob, ida);
                }
                free(ida);
                free(idb);
                continue;
            }
            if (a->type == MOD_MODIFY_PRECEDENCE) {
                const char *sa = a->u.modify_prec.symbol, *sb = b->u.modify_prec.symbol;
                if (!sa || !sb || strcmp(sa, sb) != 0) continue;
                if (a->u.modify_prec.new_precedence == b->u.modify_prec.new_precedence &&
                    a->u.modify_prec.new_assoc == b->u.modify_prec.new_assoc) {
                    drop[j] = 1;
                    continue;
                }
                if (same_owner) continue;
                int overridden = 0;
                for (uint32_t k = 0; p && k < p->ntokens; k++)
                    if (strcmp(p->tokens[k].token, sa) == 0) overridden = 1;
                /* the host's `token T ...` line replaces both proposals */
                if (overridden) { drop[i] = drop[j] = 1; continue; }
                int w = rank_winner(p, oa, ob);
                if (w > 0) {
                    drop[w == 1 ? j : i] = 1;
                } else if (w < 0) {
                    bad = 1;
                    appendf(err, "PREC_CLASH    %s given different precedences by %s and %s; "
                                 "equal rank is an error\n  fix   : token %s prec-like <U>.\n",
                            sa, oa, ob, sa);
                }
            }
        }
    }
    if (!bad) {
        uint32_t w = 0, r = 0, pos = 0;
        for (uint32_t i = 0; i < ns; i++) {
            uint32_t kept = 0;
            for (uint32_t m = 0; m < sl[i].n; m++, r++) {
                if (drop[r]) continue;
                mods[w] = mods[r];
                ids[w] = ids[r];
                w++;
                kept++;
            }
            sl[i].first = pos;
            sl[i].n = kept;
            pos += kept;
        }
        *total = w;
    }
    free(drop);
    return bad ? -1 : 0;
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

    /* 2a. Declaration-level conflicts settled by the host policy (C4). */
    {
        char *derr = NULL;
        if (compose_decide_declarations(policy, combined, ext_id_per_mod, &total, slice, ns,
                                        &derr) != 0) {
            size_t n = (derr ? strlen(derr) : 0) + 96;
            cm_err = malloc(n);
            if (cm_err)
                snprintf(cm_err, n,
                         "publish_modified_snapshot: declaration conflicts not resolved by the "
                         "compose policy:\n%s", derr ? derr : "");
            free(derr);
            goto out;
        }
    }

    /* 2b. What the policy left: the legacy declaration check (same token
    ** NAME, identical rule, precedence clash).  Extensions' on_conflict
    ** answers are PROPOSALS (deprecated, C5): KEEP_EXISTING = keep the
    ** earlier modification (prefer-base, in the old naming), USE_NEW =
    ** keep the later one (prefer-extension), MERGE = unsupported.  A
    ** proposal applies when the host set no policy at all (legacy mode,
    ** one release) or trusts that extension (`trust-extension-proposals
    ** E.`); otherwise it is ignored, listed in the error, and the conflict
    ** stays unresolved. */
    cs = conflict_set_create();
    if (cs == NULL) {
        cm_err = strdup("publish_modified_snapshot: conflict_set_create failed");
        goto out;
    }
    detect_conflicts(combined, total, cs);
    char *drop = calloc(total ? total : 1, 1);
    char *ignored = NULL;
    if (drop == NULL) {
        cm_err = strdup("publish_modified_snapshot: out of memory");
        goto out;
    }
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
        const Extension *who[2] = {find_extension(reg, id_a), find_extension(reg, id_b)};
        for (int w = 0; w < 2 && !c->resolved; w++) {
            const Extension *e = who[w];
            if (!e || !e->on_conflict) continue;
            ConflictResolution ans = e->on_conflict(e->user_data, &info);
            if (ans == CONFLICT_UNRESOLVED) continue;
            const char *line = ans == CONFLICT_KEEP_EXISTING ? "prefer-base"
                             : ans == CONFLICT_USE_NEW ? "prefer-extension" : "merge (unsupported)";
            if (policy && !lime_policy_trusts(policy, e->name)) {
                appendf(&ignored, "  proposal from %s for %s: %s -- ignored (add "
                                  "`trust-extension-proposals %s.` or a policy line)\n",
                        e->name, c->description ? c->description : "?", line, e->name);
                continue;
            }
            if (ans == CONFLICT_KEEP_EXISTING) drop[c->mod_index_b] = 1;
            else if (ans == CONFLICT_USE_NEW) drop[c->mod_index_a] = 1;
            else if (policy) continue; /* MERGE: no meaning under a host policy */
            c->resolved = true;
        }
    }
    uint32_t unresolved = conflict_set_unresolved_count(cs);
    if (unresolved > 0) {
        size_t n = (ignored ? strlen(ignored) : 0) + 128;
        cm_err = malloc(n);
        if (cm_err)
            snprintf(cm_err, n, "%u unresolved conflict(s) detected%s%s", unresolved,
                     ignored ? "\n" : "", ignored ? ignored : "");
        free(ignored);
        free(drop);
        if (out_conflicts) {
            *out_conflicts = cs;
            cs = NULL;
        }
        goto out;
    }
    free(ignored);
    {   /* apply the accepted proposals: drop the losing modifications */
        uint32_t w = 0, r = 0, pos = 0;
        for (uint32_t i = 0; i < ns; i++) {
            uint32_t kept = 0;
            for (uint32_t m = 0; m < slice[i].n; m++, r++) {
                if (drop[r]) continue;
                combined[w] = combined[r];
                ext_id_per_mod[w] = ext_id_per_mod[r];
                w++;
                kept++;
            }
            slice[i].first = pos;
            slice[i].n = kept;
            pos += kept;
        }
        total = w;
    }
    free(drop);

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

/* lime_mg_resolve's priority from the compose policy's ranks: one
** authority for composition and runtime multi-grammar ties. */
void lime_mg_candidates_rank_from_policy(LimeForkCandidate *cands, uint32_t ncands,
                                         const LimeComposePolicy *policy) {
    for (uint32_t i = 0; cands && policy && i < ncands; i++)
        if (cands[i].owner) cands[i].priority = -lime_policy_rank(policy, cands[i].owner);
}
