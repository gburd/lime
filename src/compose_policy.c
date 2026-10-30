/*
** compose_policy.c -- compose-time conflict policy (include/lime_policy.h).
**
** Snapshot-free: it builds the merged grammar text, records which
** owner (base or extension) every line belongs to, computes the base's
** own conflict keys once per base text (cached), and supplies the
** LimeCompileHook that FindActions() consults for every conflict
** precedence could not settle.  The hook:
**
**   - a key the base alone already has    -> left alone (judged by the
**                                             base's own %expect, never
**                                             by the extensions)
**   - a key the policy resolves           -> SH/RD_RESOLVED, recorded
**   - anything else                        -> left unresolved, recorded;
**                                             lime_compose_finish() > 0
**
** Linked into liblime_compiler and into the `lime` CLI (--explain-
** compose).  Nothing here runs at parse time.
*/
#include "lime_policy.h"
#include "lime_compiler.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lime_threads.h"

/* ------------------------------------------------------------------ */
/*  Small helpers                                                      */
/* ------------------------------------------------------------------ */

static char *xstrdup(const char *s) {
    if (s == NULL) return NULL;
    size_t n = strlen(s) + 1;
    char *r = malloc(n);
    if (r) memcpy(r, s, n);
    return r;
}

static char *xfmt(const char *a, const char *b, const char *c, const char *d) {
    size_t n = strlen(a) + strlen(b) + strlen(c) + strlen(d) + 4;
    char *r = malloc(n);
    if (r) snprintf(r, n, "%s;%s;%s;%s", a, b, c, d);
    return r;
}

/* Symbolic conflict key: kind + rule identities + lookahead; never a state. */
static char *conflict_key(const LimeConflictInfo *c) {
    switch (c->kind) {
    case LIME_CK_SR: {
        size_t n = strlen(c->rule_b ? c->rule_b : "") + strlen(c->lookahead) + 8;
        char *r = malloc(n);
        if (r) snprintf(r, n, "SR;%s;%s", c->rule_b ? c->rule_b : "", c->lookahead);
        return r;
    }
    case LIME_CK_RR: {
        const char *x = c->rule_a ? c->rule_a : "", *y = c->rule_b ? c->rule_b : "";
        if (strcmp(x, y) > 0) { const char *t = x; x = y; y = t; }
        return xfmt("RR", x, y, c->lookahead);
    }
    default: {
        size_t n = strlen(c->lookahead) + 4;
        char *r = malloc(n);
        if (r) snprintf(r, n, "SS;%s", c->lookahead);
        return r;
    }
    }
}

/* Sorted string set. */
typedef struct StrSet { char **v; uint32_t n, cap; } StrSet;

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}
static int strset_add(StrSet *s, char *owned) {
    if (s->n == s->cap) {
        uint32_t nc = s->cap ? s->cap * 2 : 64;
        char **nv = realloc(s->v, nc * sizeof(*nv));
        if (!nv) { free(owned); return -1; }
        s->v = nv; s->cap = nc;
    }
    s->v[s->n++] = owned;
    return 0;
}
static void strset_seal(StrSet *s) {  /* sort + dedup */
    if (s->n == 0) return;
    qsort(s->v, s->n, sizeof(char *), cmp_str);
    uint32_t w = 1;
    for (uint32_t r = 1; r < s->n; r++) {
        if (strcmp(s->v[r], s->v[w - 1]) == 0) free(s->v[r]);
        else s->v[w++] = s->v[r];
    }
    s->n = w;
}
static int strset_has(const StrSet *s, const char *k) {
    return s->n && bsearch(&k, s->v, s->n, sizeof(char *), cmp_str) != NULL;
}
static void strset_free(StrSet *s) {
    for (uint32_t i = 0; i < s->n; i++) free(s->v[i]);
    free(s->v);
    memset(s, 0, sizeof(*s));
}

/* ------------------------------------------------------------------ */
/*  Base conflict-key cache                                            */
/* ------------------------------------------------------------------ */
/* shortcut: process-global and never evicted (one entry, ~100 KB for
** PG, per distinct base text); add eviction if a host cycles many bases.
**
** The base's own keys cost one full LALR build.  A process composes
** the same base many times (each extension-set change), so cache the
** set by a 64-bit FNV-1a of the base text plus its length; a hash
** collision would need two different base texts of equal length in
** one process.  Small, never evicted: one entry per distinct base.
*/
typedef struct BaseKeys {
    uint64_t hash;
    size_t len;
    StrSet keys;
    int nconflict; /* occurrences, as lime counts them */
    int nexpect;
    struct BaseKeys *next;
} BaseKeys;

static BaseKeys *g_base_cache = NULL;
static pthread_mutex_t g_base_mu = PTHREAD_MUTEX_INITIALIZER;

static uint64_t fnv64(const char *p, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) { h ^= (unsigned char)p[i]; h *= 1099511628211ULL; }
    return h;
}

static LimeConflictChoice collect_key(void *user, const LimeConflictInfo *c) {
    char *k = conflict_key(c);
    if (k) strset_add((StrSet *)user, k);
    return LIME_CHOOSE_NONE;
}

/* Returns a borrowed, immutable entry (never freed), or NULL + *error. */
static const BaseKeys *base_keys(const char *text, size_t len, char **error) {
    uint64_t h = fnv64(text, len);
    pthread_mutex_lock(&g_base_mu);
    for (BaseKeys *b = g_base_cache; b; b = b->next) {
        if (b->hash == h && b->len == len) { pthread_mutex_unlock(&g_base_mu); return b; }
    }
    pthread_mutex_unlock(&g_base_mu);

    BaseKeys *b = calloc(1, sizeof(*b));
    if (!b) { if (error) *error = xstrdup("compose: out of memory"); return NULL; }
    LimeCompileHook hk;
    memset(&hk, 0, sizeof hk);
    hk.on_conflict = collect_key;
    hk.user = &b->keys;
    LimeCompileStats st;
    char *e = NULL;
    if (lime_compile_grammar_in_process_ex2(text, len, &hk, NULL, &st, &e) != 0) {
        if (error) {
            size_t n = strlen(e ? e : "?") + 32;
            *error = malloc(n);
            if (*error) snprintf(*error, n, "base grammar: %s", e ? e : "?");
        }
        free(e);
        strset_free(&b->keys);
        free(b);
        return NULL;
    }
    strset_seal(&b->keys);
    b->hash = h; b->len = len; b->nexpect = st.nexpect; b->nconflict = st.nconflict;

    pthread_mutex_lock(&g_base_mu);
    for (BaseKeys *o = g_base_cache; o; o = o->next) {  /* lost a race: keep the first */
        if (o->hash == h && o->len == len) {
            pthread_mutex_unlock(&g_base_mu);
            strset_free(&b->keys);
            free(b);
            return o;
        }
    }
    b->next = g_base_cache;
    g_base_cache = b;
    pthread_mutex_unlock(&g_base_mu);
    return b;
}

/* ------------------------------------------------------------------ */
/*  Session                                                            */
/* ------------------------------------------------------------------ */

typedef struct Segment {
    const char *owner, *file;
    int first_line, last_line; /* 1-based lines of the merged text */
} Segment;

struct LimeComposeSession {
    char *merged;
    size_t merged_len;
    Segment *seg;
    uint32_t nseg;
    const BaseKeys *base;
    const LimeComposePolicy *policy;
    LimeCompileHook hook;
    LimeComposeEntry *ent;
    uint32_t nent, capent;
    char **owner_names; /* owned copies, so the report outlives the units */
    uint32_t nowner;
    LimePrecOverride *prec;
    StrSet in_conflict;      /* rule identities that took part in a new conflict */
    int in_conflict_sealed;
    LimeComposeSide *dead;
    uint32_t ndead, capdead;
};

static int count_lines(const char *p, size_t n) {
    int k = 0;
    for (size_t i = 0; i < n; i++) k += p[i] == '\n';
    return k;
}

static const Segment *segment_of(const LimeComposeSession *s, int line) {
    for (uint32_t i = 0; i < s->nseg; i++)
        if (line >= s->seg[i].first_line && line <= s->seg[i].last_line) return &s->seg[i];
    return &s->seg[0];
}

static void fill_side(const LimeComposeSession *s, LimeComposeSide *d, const char *rule, int line) {
    if (rule == NULL) return;
    const Segment *g = segment_of(s, line);
    d->owner = xstrdup(g->owner);
    d->file = xstrdup(g->file);
    d->rule = xstrdup(rule);
    d->line = line ? line - g->first_line + 1 : 0;
}

/* Does policy line `p` name conflict `c`? */
static int policy_matches(const LimePolicyConflict *p, const LimeConflictInfo *c) {
    if (p->kind != c->kind) return 0;
    if (p->lookahead && strcmp(p->lookahead, c->lookahead) != 0) return 0;
    if (p->rule == NULL) return 0;
    if (c->kind == LIME_CK_SR) return c->rule_b && strcmp(p->rule, c->rule_b) == 0;
    return (c->rule_a && strcmp(p->rule, c->rule_a) == 0)
        || (c->rule_b && strcmp(p->rule, c->rule_b) == 0);
}

static char *policy_line(const LimePolicyConflict *p) {
    char buf[1024];
    const char *pref = p->prefer == LIME_PREFER_SHIFT ? "shift"
                     : p->prefer == LIME_PREFER_REDUCE ? "reduce"
                     : (p->owner ? p->owner : "?");
    if (p->lookahead)
        snprintf(buf, sizeof buf, "conflict %s \"%s\" on %s prefer %s.",
                 p->kind == LIME_CK_SR ? "sr" : "rr", p->rule, p->lookahead, pref);
    else
        snprintf(buf, sizeof buf, "conflict %s \"%s\" prefer %s.",
                 p->kind == LIME_CK_SR ? "sr" : "rr", p->rule, pref);
    return xstrdup(buf);
}

/* Apply policy line `p` to conflict `c`; LIME_CHOOSE_NONE if it cannot decide. */
static LimeConflictChoice policy_choice(const LimeComposeSession *s, const LimePolicyConflict *p,
                                        const LimeConflictInfo *c) {
    if (c->kind == LIME_CK_SR) {
        if (p->prefer == LIME_PREFER_SHIFT) return LIME_CHOOSE_SHIFT;
        if (p->prefer == LIME_PREFER_REDUCE) return LIME_CHOOSE_REDUCE;
        if (p->prefer == LIME_PREFER_OWNER && p->owner) {
            int ra = c->rule_a && strcmp(segment_of(s, c->line_a)->owner, p->owner) == 0;
            int rb = c->rule_b && strcmp(segment_of(s, c->line_b)->owner, p->owner) == 0;
            if (ra && !rb) return LIME_CHOOSE_SHIFT;
            if (rb && !ra) return LIME_CHOOSE_REDUCE;
        }
        return LIME_CHOOSE_NONE;
    }
    if (c->kind == LIME_CK_RR) {
        if (p->prefer == LIME_PREFER_REDUCE)
            return (c->rule_a && strcmp(p->rule, c->rule_a) == 0) ? LIME_CHOOSE_KEEP_A
                                                                  : LIME_CHOOSE_KEEP_B;
        if (p->prefer == LIME_PREFER_OWNER && p->owner) {
            int ra = strcmp(segment_of(s, c->line_a)->owner, p->owner) == 0;
            int rb = strcmp(segment_of(s, c->line_b)->owner, p->owner) == 0;
            if (ra && !rb) return LIME_CHOOSE_KEEP_A;
            if (rb && !ra) return LIME_CHOOSE_KEEP_B;
        }
    }
    return LIME_CHOOSE_NONE;
}

int lime_policy_rank(const LimeComposePolicy *p, const char *owner) {
    if (p == NULL || owner == NULL || strcmp(owner, "base") == 0) return 0;
    for (uint32_t i = 0; i < p->nranks; i++)
        if (p->ranks[i].owner && strcmp(p->ranks[i].owner, owner) == 0) return p->ranks[i].rank;
    return 0;
}

int lime_policy_trusts(const LimeComposePolicy *p, const char *owner) {
    for (uint32_t i = 0; p && owner && i < p->ntrusted; i++)
        if (p->trusted[i] && strcmp(p->trusted[i], owner) == 0) return 1;
    return 0;
}

/* `rank` and `default`, for a conflict no `conflict` line settled.  Side a
** is the shift (SR) or Lemon's default reduce (RR); side b the other reduce.
** Rank applies only when the two sides have different owners and ranks;
** equal ranks between two different owners make it an error (*tie). */
static LimeConflictChoice rank_choice(const LimeComposeSession *s, const LimeConflictInfo *c,
                                      char **why, const char **tie) {
    const LimeComposePolicy *p = s->policy;
    const char *oa = segment_of(s, c->line_a)->owner, *ob = segment_of(s, c->line_b)->owner;
    int a_base = strcmp(oa, "base") == 0, b_base = strcmp(ob, "base") == 0;
    LimeConflictChoice keep_a = c->kind == LIME_CK_SR ? LIME_CHOOSE_SHIFT : LIME_CHOOSE_KEEP_A;
    LimeConflictChoice keep_b = c->kind == LIME_CK_SR ? LIME_CHOOSE_REDUCE : LIME_CHOOSE_KEEP_B;
    char buf[512];
    if (strcmp(oa, ob) == 0) return LIME_CHOOSE_NONE; /* one owner: only a conflict line decides */
    int ra = lime_policy_rank(p, oa), rb = lime_policy_rank(p, ob);
    int ranked = p->nranks > 0 && ra != rb;
    if (ranked && (p->default_mode == LIME_DEFAULT_PREFER_RANK || !(a_base || b_base))) {
        snprintf(buf, sizeof buf, "rank %s %d > %s %d.", ra > rb ? oa : ob, ra > rb ? ra : rb,
                 ra > rb ? ob : oa, ra > rb ? rb : ra);
        *why = xstrdup(buf);
        return ra > rb ? keep_a : keep_b;
    }
    if (!a_base && !b_base && ra == rb && (p->nranks > 0 || p->default_mode == LIME_DEFAULT_PREFER_RANK)) {
        *tie = "equal rank";
        return LIME_CHOOSE_NONE;
    }
    if (p->default_mode == LIME_DEFAULT_PREFER_BASE && (a_base != b_base)) {
        *why = xstrdup("default prefer-base.");
        return a_base ? keep_a : keep_b;
    }
    if (p->default_mode == LIME_DEFAULT_PREFER_EXTENSION && (a_base != b_base)) {
        *why = xstrdup("default prefer-extension.");
        return a_base ? keep_b : keep_a;
    }
    if (p->default_mode == LIME_DEFAULT_PREFER_RANK && ranked) {
        snprintf(buf, sizeof buf, "rank %s %d > %s %d.", ra > rb ? oa : ob, ra > rb ? ra : rb,
                 ra > rb ? ob : oa, ra > rb ? rb : ra);
        *why = xstrdup(buf);
        return ra > rb ? keep_a : keep_b;
    }
    return LIME_CHOOSE_NONE;
}

static LimeComposeEntry *entry_for(LimeComposeSession *s, const char *key) {
    for (uint32_t i = 0; i < s->nent; i++)  /* shortcut: linear; tens of entries in practice */
        if (strcmp(s->ent[i].key, key) == 0) return &s->ent[i];
    if (s->nent == s->capent) {
        uint32_t nc = s->capent ? s->capent * 2 : 16;
        LimeComposeEntry *ne = realloc(s->ent, nc * sizeof(*ne));
        if (!ne) return NULL;
        s->ent = ne; s->capent = nc;
    }
    LimeComposeEntry *e = &s->ent[s->nent++];
    memset(e, 0, sizeof(*e));
    return e;
}

static int on_dead_rule(void *user, const char *rule, int line) {
    LimeComposeSession *s = user;
    /* sealed lazily: on_conflict calls are over once FindActions checks
    ** reducibility */
    if (!s->in_conflict_sealed) { strset_seal(&s->in_conflict); s->in_conflict_sealed = 1; }
    if (!strset_has(&s->in_conflict, rule)) return 0; /* a real grammar error */
    if (s->ndead == s->capdead) {
        uint32_t nc = s->capdead ? s->capdead * 2 : 8;
        LimeComposeSide *nd = realloc(s->dead, nc * sizeof(*nd));
        if (!nd) return 0;
        s->dead = nd;
        s->capdead = nc;
    }
    memset(&s->dead[s->ndead], 0, sizeof(s->dead[0]));
    fill_side(s, &s->dead[s->ndead], rule, line);
    s->ndead++;
    return 1;
}

static LimeConflictChoice on_conflict(void *user, const LimeConflictInfo *c) {
    LimeComposeSession *s = user;
    char *key = conflict_key(c);
    if (key == NULL) return LIME_CHOOSE_NONE;
    if (strset_has(&s->base->keys, key)) { free(key); return LIME_CHOOSE_NONE; }
    if (c->rule_a) strset_add(&s->in_conflict, xstrdup(c->rule_a));
    if (c->rule_b) strset_add(&s->in_conflict, xstrdup(c->rule_b));

    const LimePolicyConflict *hit = NULL;
    LimeConflictChoice ch = LIME_CHOOSE_NONE;
    char *by_rank = NULL; /* resolution text when rank/default decided */
    const char *tie = NULL; /* set when equal ranks make it an error */
    if (s->policy) {
        for (uint32_t i = 0; i < s->policy->nconflicts && ch == LIME_CHOOSE_NONE; i++) {
            const LimePolicyConflict *p = &s->policy->conflicts[i];
            if (!policy_matches(p, c)) continue;
            ch = policy_choice(s, p, c);
            if (ch != LIME_CHOOSE_NONE) hit = p;
        }
        if (ch == LIME_CHOOSE_NONE && c->kind != LIME_CK_SS && c->rule_a && c->rule_b)
            ch = rank_choice(s, c, &by_rank, &tie);
    }

    LimeComposeEntry *e = entry_for(s, key);
    if (e == NULL) { free(key); return LIME_CHOOSE_NONE; }
    if (e->key == NULL) {
        e->key = key;
        e->kind = c->kind;
        e->lookahead = xstrdup(c->lookahead);
        fill_side(s, &e->a, c->rule_a, c->line_a);
        fill_side(s, &e->b, c->rule_b, c->line_b);
        if (c->kind == LIME_CK_SR && c->rule_a) {
            /* show the dot in the shift rule: "x ::= A . B C" */
            const char *r = c->rule_a, *eq = strstr(r, "::=");
            if (eq) {
                size_t n = strlen(r) + 4;
                char *t = malloc(n);
                if (t) {
                    const char *q = eq + 3;
                    int d = 0;
                    size_t off = (size_t)(q - r);
                    memcpy(t, r, off);
                    while (*q) {
                        if (*q == ' ') {
                            if (d == c->dot_a) { memcpy(t + off, " .", 2); off += 2; }
                            d++;
                        }
                        t[off++] = *q++;
                    }
                    t[off] = '\0';
                    free(e->a.rule);
                    e->a.rule = t;
                }
            }
        }
        e->example = xstrdup(c->example);
        e->prec_lookahead = c->prec_lookahead;
        e->prec_b = c->prec_b;
        e->resolution = hit ? policy_line(hit) : by_rank;
        by_rank = NULL;
        if (tie) e->tie = xstrdup(tie);
    } else {
        free(key);
        /* Keep the shortest example across states. */
        if (c->example && (!e->example || strlen(c->example) < strlen(e->example))) {
            free(e->example);
            e->example = xstrdup(c->example);
        }
        /* A key is resolved only if EVERY occurrence was. */
        if (!hit && !by_rank && e->resolution) { free(e->resolution); e->resolution = NULL; }
        if (tie && !e->tie) e->tie = xstrdup(tie);
    }
    free(by_rank);
    e->nstates++;
    return ch;
}

LimeComposeSession *lime_compose_begin(const char *base_text, size_t base_len,
                                       const char *base_file, const LimeComposeUnit *units,
                                       uint32_t nunits, const LimeComposePolicy *policy,
                                       char **error) {
    if (error) *error = NULL;
    if (base_text == NULL || base_len == 0 || (nunits && units == NULL)) {
        if (error) *error = xstrdup("compose: bad arguments");
        return NULL;
    }
    LimeComposeSession *s = calloc(1, sizeof(*s));
    if (!s) goto oom;
    s->policy = policy;
    s->base = base_keys(base_text, base_len, error);
    if (!s->base) { lime_compose_session_free(s); return NULL; }

    /* canonical order: by name (load order must not matter) */
    const LimeComposeUnit **ord = malloc((nunits ? nunits : 1) * sizeof(*ord));
    if (!ord) goto oom;
    for (uint32_t i = 0; i < nunits; i++) ord[i] = &units[i];
    for (uint32_t a = 1; a < nunits; a++) {
        const LimeComposeUnit *x = ord[a];
        uint32_t b = a;
        while (b > 0 && strcmp(ord[b - 1]->name ? ord[b - 1]->name : "",
                               x->name ? x->name : "") > 0) {
            ord[b] = ord[b - 1];
            b--;
        }
        ord[b] = x;
    }
    size_t total = base_len + 1;
    for (uint32_t i = 0; i < nunits; i++) total += units[i].len + 1;
    s->merged = malloc(total + 1);
    s->seg = calloc(nunits + 1, sizeof(Segment));
    s->owner_names = calloc(2 * nunits + 3, sizeof(char *));
    if (!s->merged || !s->seg || !s->owner_names) { free(ord); goto oom; }

    size_t off = 0;
    int line = 1;
    memcpy(s->merged, base_text, base_len);
    off = base_len;
    s->merged[off++] = '\n';
    s->seg[0].owner = "base";
    s->seg[0].file = s->owner_names[s->nowner++] = xstrdup(base_file ? base_file : "base");
    s->seg[0].first_line = 1;
    line += count_lines(base_text, base_len) + 1;
    s->seg[0].last_line = line - 1;
    s->nseg = 1;
    for (uint32_t i = 0; i < nunits; i++) {
        const LimeComposeUnit *u = ord[i];
        char *nm = xstrdup(u->name ? u->name : "?");
        char *fl = xstrdup(u->file ? u->file : (u->name ? u->name : "?"));
        s->owner_names[s->nowner++] = nm;
        s->owner_names[s->nowner++] = fl;
        Segment *g = &s->seg[s->nseg++];
        g->owner = nm;
        g->file = fl;
        g->first_line = line;
        if (u->len) memcpy(s->merged + off, u->text, u->len);
        off += u->len;
        s->merged[off++] = '\n';
        line += count_lines(u->text, u->len) + 1;
        g->last_line = line - 1;
    }
    s->merged[off] = '\0';
    s->merged_len = off;
    free(ord);
    s->hook.on_conflict = on_conflict;
    s->hook.on_dead_rule = on_dead_rule;
    s->hook.user = s;
    if (policy && policy->ntokens) {
        /* `token T prec...` lines -> precedence overrides applied before
        ** FindRulePrecedences: the same effect as a %left/%right line. */
        s->prec = calloc(policy->ntokens, sizeof(*s->prec));
        if (!s->prec) goto oom;
        for (uint32_t i = 0; i < policy->ntokens; i++) {
            s->prec[i].token = policy->tokens[i].token;
            s->prec[i].like = policy->tokens[i].like;
            s->prec[i].level = policy->tokens[i].level;
            s->prec[i].assoc = policy->tokens[i].assoc;
        }
        s->hook.prec = s->prec;
        s->hook.nprec = policy->ntokens;
    }
    return s;
oom:
    if (error && *error == NULL) *error = xstrdup("compose: out of memory");
    lime_compose_session_free(s);
    return NULL;
}

const char *lime_compose_merged_text(const LimeComposeSession *s, size_t *len) {
    if (len) *len = s ? s->merged_len : 0;
    return s ? s->merged : NULL;
}

const LimeCompileHook *lime_compose_hook(LimeComposeSession *s) { return s ? &s->hook : NULL; }

/* Rewrite "<grammar text>:N:" in a compiler diagnostic as "owner-file:M:",
** so a grammar error points into the extension that caused it. */
char *lime_compose_locate_errors(const LimeComposeSession *s, const char *msg) {
    static const char tag[] = "<grammar text>:";
    if (s == NULL || msg == NULL) return msg ? xstrdup(msg) : NULL;
    size_t cap = strlen(msg) * 2 + 64, n = 0;
    char *out = malloc(cap);
    if (out == NULL) return NULL;
    const char *p = msg;
    while (*p) {
        const char *hit = strstr(p, tag);
        if (hit == NULL) hit = p + strlen(p);
        size_t pre = (size_t)(hit - p);
        if (n + pre + 1 > cap) break;
        memcpy(out + n, p, pre);
        n += pre;
        p = hit;
        if (*p == '\0') break;
        char *end;
        long line = strtol(p + sizeof(tag) - 1, &end, 10);
        if (line <= 0) { /* no position: keep the text as is */
            size_t t = (size_t)(end - p);
            if (n + t + 1 > cap) break;
            memcpy(out + n, p, t);
            n += t;
            p = end;
            continue;
        }
        const Segment *g = segment_of(s, (int)line);
        char buf[512];
        int k = snprintf(buf, sizeof buf, "%s:%ld", g->file, line - g->first_line + 1);
        if (k < 0 || n + (size_t)k + 1 > cap) break;
        memcpy(out + n, buf, (size_t)k);
        n += (size_t)k;
        p = end;
    }
    out[n] = '\0';
    return out;
}

static int cmp_entry(const void *a, const void *b) {
    return strcmp(((const LimeComposeEntry *)a)->key, ((const LimeComposeEntry *)b)->key);
}

uint32_t lime_compose_finish(LimeComposeSession *s, LimeComposeReport **report) {
    if (report) *report = NULL;
    if (s == NULL) return 0;
    uint32_t nun = 0;
    for (uint32_t i = 0; i < s->nent; i++) nun += s->ent[i].resolution == NULL;
    if (report) {
        LimeComposeReport *r = calloc(1, sizeof(*r));
        if (r) {
            if (s->nent) qsort(s->ent, s->nent, sizeof(*s->ent), cmp_entry);
            r->entries = s->ent;
            r->n = s->nent;
            r->nunresolved = nun;
            r->nresolved = s->nent - nun;
            r->base_nconflict = s->base->nconflict;
            r->base_nexpect = s->base->nexpect;
            r->nextensions = s->nseg - 1;
            r->dead = s->dead;
            r->ndead = s->ndead;
            s->dead = NULL;
            s->ndead = s->capdead = 0;
            s->ent = NULL;
            s->nent = s->capent = 0;
            *report = r;
        }
    }
    return nun;
}

static void free_side(LimeComposeSide *d) {
    free(d->owner); free(d->rule); free(d->file);
}
static void free_entries(LimeComposeEntry *e, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        free(e[i].key); free(e[i].lookahead); free(e[i].example); free(e[i].resolution);
        free(e[i].tie);
        free_side(&e[i].a); free_side(&e[i].b);
    }
    free(e);
}

void lime_compose_session_free(LimeComposeSession *s) {
    if (s == NULL) return;
    free(s->prec);
    strset_free(&s->in_conflict);
    for (uint32_t i = 0; i < s->ndead; i++) free_side(&s->dead[i]);
    free(s->dead);
    free(s->merged);
    free(s->seg);
    for (uint32_t i = 0; i < s->nowner; i++) free(s->owner_names[i]);
    free(s->owner_names);
    free_entries(s->ent, s->nent);
    free(s);
}

void lime_compose_report_free(LimeComposeReport *r) {
    if (r == NULL) return;
    free_entries(r->entries, r->n);
    for (uint32_t i = 0; i < r->ndead; i++) free_side(&r->dead[i]);
    free(r->dead);
    free(r);
}

/* ------------------------------------------------------------------ */
/*  Report text                                                        */
/* ------------------------------------------------------------------ */

typedef struct Buf { char *p; size_t n, cap; } Buf;
static void bput(Buf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (k < 0) return;
    if (b->n + (size_t)k + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 1024;
        while (nc < b->n + (size_t)k + 1) nc *= 2;
        char *np = realloc(b->p, nc);
        if (!np) return;
        b->p = np; b->cap = nc;
    }
    va_start(ap, fmt);
    vsnprintf(b->p + b->n, b->cap - b->n, fmt, ap);
    va_end(ap);
    b->n += (size_t)k;
}

static void side_line(Buf *b, const char *tag, const LimeComposeSide *d) {
    if (d->rule == NULL) return;
    if (d->line > 0)
        bput(b, "  %-7s %s    %s:%d\n", tag, d->rule, d->file, d->line);
    else
        bput(b, "  %-7s %s    %s\n", tag, d->rule, d->file);
}

/* Root cause = (kind, owners, side-a rule, lookahead): one new operator
** meeting N base rules is one cause, not N problems. */
typedef struct Group { const LimeComposeEntry *first; uint32_t n, nunres; } Group;

static int same_group(const LimeComposeEntry *x, const LimeComposeEntry *y) {
#define SEQ(p, q) ((p) == (q) || ((p) && (q) && strcmp((p), (q)) == 0))
    return x->kind == y->kind && SEQ(x->lookahead, y->lookahead) && SEQ(x->a.rule, y->a.rule)
        && SEQ(x->a.owner, y->a.owner) && SEQ(x->b.owner, y->b.owner);
#undef SEQ
}

static int cmp_group(const void *p, const void *q) {
    const Group *a = p, *b = q;
    if (a->n != b->n) return a->n > b->n ? -1 : 1;
    return strcmp(a->first->key, b->first->key);
}

char *lime_compose_report_format(const LimeComposeReport *r) {
    Buf b = {0};
    if (r == NULL) return xstrdup("");
    for (uint32_t i = 0; i < r->n; i++) {
        const LimeComposeEntry *e = &r->entries[i];
        const char *kind = e->kind == LIME_CK_SR ? "shift/reduce"
                         : e->kind == LIME_CK_RR ? "reduce/reduce" : "shift/shift";
        const char *oa = e->a.owner ? e->a.owner : (e->b.owner ? e->b.owner : "?");
        const char *ob = e->b.owner ? e->b.owner : oa;
        bput(&b, "NEW %s  [%s vs %s]  lookahead %s  (%u state%s)\n", kind, oa, ob,
             e->lookahead, e->nstates, e->nstates == 1 ? "" : "s");
        if (e->kind == LIME_CK_SR) {
            side_line(&b, "shift :", &e->a);
            side_line(&b, "reduce:", &e->b);
        } else {
            side_line(&b, "reduce:", &e->a);
            side_line(&b, "reduce:", &e->b);
        }
        if (e->example) bput(&b, "  input : %s\n", e->example);
        if (e->tie && !e->resolution)
            bput(&b, "  error : %s and %s have equal rank; a tie on a real conflict is an "
                     "error\n", e->a.owner ? e->a.owner : "?", e->b.owner ? e->b.owner : "?");
        if (e->resolution) {
            bput(&b, "  policy: %s\n", e->resolution);
        } else if (e->kind == LIME_CK_SR && e->b.rule) {
            bput(&b, "  fix   : conflict sr \"%s\" on %s prefer shift.\n", e->b.rule, e->lookahead);
        } else if (e->kind == LIME_CK_RR && e->a.rule) {
            bput(&b, "  fix   : conflict rr \"%s\" on %s prefer reduce.\n", e->a.rule, e->lookahead);
        }
    }

    /* Root causes, largest first. */
    Group *g = calloc(r->n ? r->n : 1, sizeof(*g));
    uint32_t ng = 0;
    for (uint32_t i = 0; g && i < r->n; i++) {
        const LimeComposeEntry *e = &r->entries[i];
        uint32_t k = 0;
        while (k < ng && !same_group(g[k].first, e)) k++;
        if (k == ng) g[ng++].first = e;
        g[k].n++;
        g[k].nunres += e->resolution == NULL;
    }
    if (g && ng && ng < r->n) {
        qsort(g, ng, sizeof(*g), cmp_group);
        bput(&b, "ROOT CAUSES: %u (by side-a rule and lookahead)\n", ng);
        for (uint32_t k = 0; k < ng; k++) {
            const LimeComposeEntry *e = g[k].first;
            /* precedence can settle an SR only if both sides have one */
            int prec_ok = 1, any_la = 0;
            for (uint32_t i = 0; i < r->n; i++) {
                const LimeComposeEntry *x = &r->entries[i];
                if (!same_group(x, e)) continue;
                if (x->prec_b < 0) prec_ok = 0;
                if (x->prec_lookahead >= 0) any_la = 1;
            }
            bput(&b, "  %4u  %s %s on %s  [%s vs %s]%s\n", g[k].n,
                 e->kind == LIME_CK_SR ? "shift" : "reduce", e->a.rule ? e->a.rule : "?",
                 e->lookahead, e->a.owner ? e->a.owner : "?", e->b.owner ? e->b.owner : "?",
                 g[k].nunres ? "" : "  (resolved)");
            if (g[k].nunres && e->kind == LIME_CK_SR) {
                int base_tok = e->a.owner && strcmp(e->a.owner, "base") == 0;
                if (!prec_ok)
                    bput(&b, "        a reduce rule here has no precedence, so no precedence line "
                             "settles this: use conflict lines, or fix the base\n");
                else if (!any_la && base_tok)
                    bput(&b, "        the base gives %s no precedence: only a base change (or "
                             "rejecting the extension) settles this\n", e->lookahead);
                else if (!any_la)
                    bput(&b, "        %s has no precedence: a precedence for it settles this\n",
                         e->lookahead);
            }
        }
    }
    free(g);

    for (uint32_t i = 0; i < r->ndead; i++)
        bput(&b, "DEAD rule (loses every conflict it is in; never reduced): %s    %s:%d\n",
             r->dead[i].rule, r->dead[i].file, r->dead[i].line);
    bput(&b, "UNRESOLVED: %u   resolved by policy: %u   base conflicts (judged by base %%expect",
         r->nunresolved, r->nresolved);
    if (r->base_nexpect >= 0) bput(&b, " %d", r->base_nexpect);
    bput(&b, "): %d", r->base_nconflict);
    if ((r->base_nexpect >= 0 && r->base_nconflict != r->base_nexpect)
        || (r->base_nexpect < 0 && r->base_nconflict > 0))
        bput(&b, "  (base does not match its %%expect)");
    bput(&b, "\n");
    if (b.p == NULL) return xstrdup("");
    return b.p;
}

/* ------------------------------------------------------------------ */
/*  Policy text parser                                                 */
/* ------------------------------------------------------------------ */

typedef struct PTok { char *s; int quoted; int line; } PTok;

/* Split into words / "quoted strings" / '.', skipping C comments.  A word
** ends at whitespace or '.', except that "::=" and digits are kept whole. */
static int ptokenize(const char *t, size_t n, PTok **out, uint32_t *nout, char **error) {
    PTok *v = NULL;
    uint32_t nv = 0, cap = 0;
    int line = 1;
    size_t i = 0;
    while (i < n) {
        char c = t[i];
        if (c == '\n') { line++; i++; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { i++; continue; }
        if (c == '/' && i + 1 < n && t[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(t[i] == '*' && t[i + 1] == '/')) { line += t[i] == '\n'; i++; }
            i += 2;
            continue;
        }
        if (c == '/' && i + 1 < n && t[i + 1] == '/') { while (i < n && t[i] != '\n') i++; continue; }
        size_t st = i;
        int q = 0;
        if (c == '"') {
            q = 1;
            st = ++i;
            while (i < n && t[i] != '"' && t[i] != '\n') i++;
            if (i >= n || t[i] != '"') {
                char buf[64];
                snprintf(buf, sizeof buf, "policy:%d: unterminated string", line);
                if (error) *error = xstrdup(buf);
                for (uint32_t k = 0; k < nv; k++) free(v[k].s);
                free(v);
                return -1;
            }
        } else if (c == '.' || c == '{' || c == '}') {
            i++;
        } else {
            while (i < n && t[i] != ' ' && t[i] != '\t' && t[i] != '\n' && t[i] != '\r'
                   && t[i] != '.' && t[i] != '"' && t[i] != '{' && t[i] != '}') i++;
        }
        if (nv == cap) {
            cap = cap ? cap * 2 : 64;
            PTok *nvv = realloc(v, cap * sizeof(*v));
            if (!nvv) { if (error) *error = xstrdup("policy: out of memory"); return -1; }
            v = nvv;
        }
        size_t len = (q ? i : i) - st;
        v[nv].s = malloc(len + 1);
        memcpy(v[nv].s, t + st, len);
        v[nv].s[len] = 0;
        v[nv].quoted = q;
        v[nv].line = line;
        nv++;
        if (q) i++; /* closing quote */
    }
    *out = v;
    *nout = nv;
    return 0;
}

#define GROW(arr, n, cap)                                                        \
    do {                                                                         \
        if ((n) == (cap)) {                                                      \
            uint32_t nc_ = (cap) ? (cap) * 2 : 8;                                \
            void *np_ = realloc((void *)(arr), nc_ * sizeof(*(arr)));            \
            if (!np_) goto oom;                                                  \
            (arr) = np_;                                                         \
            (cap) = nc_;                                                         \
        }                                                                        \
    } while (0)

void lime_policy_free(LimeComposePolicy *p) {
    if (p == NULL) return;
    for (uint32_t i = 0; i < p->nconflicts; i++) {
        free((char *)p->conflicts[i].rule);
        free((char *)p->conflicts[i].lookahead);
        free((char *)p->conflicts[i].owner);
    }
    for (uint32_t i = 0; i < p->nranks; i++) free((char *)p->ranks[i].owner);
    for (uint32_t i = 0; i < p->ntokens; i++) {
        free((char *)p->tokens[i].token);
        free((char *)p->tokens[i].like);
    }
    for (uint32_t i = 0; i < p->nrejects; i++) {
        free((char *)p->rejects[i].what);
        free((char *)p->rejects[i].owner);
    }
    for (uint32_t i = 0; i < p->nkeywords; i++) {
        free((char *)p->keywords[i].lexeme);
        free((char *)p->keywords[i].owner);
    }
    for (uint32_t i = 0; i < p->ntrusted; i++) free((char *)p->trusted[i]);
    free((void *)p->conflicts);
    free((void *)p->ranks);
    free((void *)p->tokens);
    free((void *)p->rejects);
    free((void *)p->keywords);
    free((void *)p->trusted);
    free(p);
}

LimeComposePolicy *lime_policy_parse(const char *text, size_t len, char **error) {
    if (error) *error = NULL;
    if (text == NULL) { if (error) *error = xstrdup("policy: NULL text"); return NULL; }
    /* `%policy { ... }` inside a grammar: read only the block. */
    const char *blk = NULL;
    for (size_t i = 0; i + 7 <= len; i++) {
        if (memcmp(text + i, "%policy", 7) == 0 && (i == 0 || text[i - 1] == '\n')) { blk = text + i; break; }
    }
    int base_line = 0;
    if (blk) {
        const char *o = memchr(blk, '{', len - (size_t)(blk - text));
        if (o == NULL) { if (error) *error = xstrdup("policy: %policy without '{'"); return NULL; }
        for (const char *q = text; q < o; q++) base_line += *q == '\n';
        int depth = 1;
        const char *e = o + 1;
        while (e < text + len && depth) { depth += (*e == '{') - (*e == '}'); e++; }
        if (depth) { if (error) *error = xstrdup("policy: unterminated %policy block"); return NULL; }
        text = o + 1;
        len = (size_t)(e - 1 - text);
    }
    PTok *tk = NULL;
    uint32_t nt = 0;
    if (ptokenize(text, len, &tk, &nt, error) != 0) return NULL;

    LimeComposePolicy *p = calloc(1, sizeof(*p));
    LimePolicyConflict *cf = NULL; uint32_t ccf = 0;
    LimePolicyRank *rk = NULL; uint32_t crk = 0;
    LimePolicyToken *to = NULL; uint32_t cto = 0;
    LimePolicyReject *rj = NULL; uint32_t crj = 0;
    LimePolicyKeyword *kw = NULL; uint32_t ckw = 0;
    char **tr = NULL; uint32_t ctr = 0;
    char ebuf[256] = {0};
    if (!p) goto oom;
    uint32_t i = 0;
#define AT(k) ((i + (k)) < nt ? tk[i + (k)].s : "")
#define IS(k, w) (!tk[i + (k) < nt ? i + (k) : 0].quoted && strcmp(AT(k), (w)) == 0)
#define LINE (tk[i < nt ? i : nt - 1].line + base_line)
#define BAD(msg) do { snprintf(ebuf, sizeof ebuf, "policy:%d: %s", LINE, (msg)); goto bad; } while (0)
    while (i < nt) {
        uint32_t st = i;
        /* find the statement end */
        uint32_t end = i;
        while (end < nt && !(tk[end].s[0] == '.' && tk[end].s[1] == 0 && !tk[end].quoted)) end++;
        if (end == nt) BAD("statement without a terminating '.'");
        uint32_t nw = end - st;
        if (IS(0, "rank")) {
            char *ep;
            long r = nw == 3 ? strtol(AT(2), &ep, 10) : 0;
            if (nw != 3 || *ep) BAD("expected: rank NAME N.");
            GROW(rk, p->nranks, crk);
            rk[p->nranks].owner = xstrdup(AT(1));
            rk[p->nranks].rank = (int)r;
            p->nranks++;
        } else if (IS(0, "default")) {
            if (nw != 2) BAD("expected: default reject|prefer-base|prefer-extension|prefer-rank.");
            if (IS(1, "reject")) p->default_mode = LIME_DEFAULT_REJECT;
            else if (IS(1, "prefer-base")) p->default_mode = LIME_DEFAULT_PREFER_BASE;
            else if (IS(1, "prefer-extension")) p->default_mode = LIME_DEFAULT_PREFER_EXTENSION;
            else if (IS(1, "prefer-rank")) p->default_mode = LIME_DEFAULT_PREFER_RANK;
            else BAD("unknown default mode");
        } else if (nw == 1 && IS(0, "prefer-base")) {
            p->default_mode = LIME_DEFAULT_PREFER_BASE;
        } else if (nw == 1 && IS(0, "prefer-extension")) {
            p->default_mode = LIME_DEFAULT_PREFER_EXTENSION;
        } else if (IS(0, "token")) {
            /* token T prec-like U [assoc A].   token T prec N [assoc A]. */
            if (nw != 4 && nw != 6) BAD("expected: token T prec-like U|prec N [assoc left|right|nonassoc].");
            GROW(to, p->ntokens, cto);
            LimePolicyToken *t = &to[p->ntokens];
            memset(t, 0, sizeof(*t));
            t->token = xstrdup(AT(1));
            p->ntokens++;
            if (IS(2, "prec-like")) t->like = xstrdup(AT(3));
            else if (IS(2, "prec")) {
                char *ep;
                t->level = (int)strtol(AT(3), &ep, 10);
                if (*ep || t->level <= 0) BAD("prec N needs a positive integer");
            } else BAD("expected prec-like or prec");
            if (nw == 6) {
                if (!IS(4, "assoc")) BAD("expected assoc");
                t->assoc = IS(5, "left") ? LIME_ASSOC_LEFT : IS(5, "right") ? LIME_ASSOC_RIGHT
                         : IS(5, "nonassoc") ? LIME_ASSOC_NONASSOC : -1;
                if (t->assoc < 0) BAD("assoc must be left, right or nonassoc");
            } else if (!t->like) {
                t->assoc = LIME_ASSOC_LEFT;
            }
        } else if (IS(0, "conflict")) {
            /* conflict sr|rr "rule" [on LA] prefer X. */
            if (nw != 5 && nw != 7) BAD("expected: conflict sr|rr \"rule\" [on LOOKAHEAD] prefer shift|reduce|EXT.");
            int kind = IS(1, "sr") ? LIME_CK_SR : IS(1, "rr") ? LIME_CK_RR
                     : IS(1, "rule") ? LIME_DK_DUP_RULE_ACTION : 0;
            if (!kind) BAD("conflict kind must be sr, rr or rule");
            if (!tk[i + 2].quoted) BAD("the rule must be a \"quoted\" rule identity");
            uint32_t k = 3;
            const char *la = NULL;
            if (nw == 7) {
                if (!IS(3, "on")) BAD("expected 'on LOOKAHEAD'");
                la = AT(4);
                k = 5;
            }
            if (!IS(k, "prefer")) BAD("expected 'prefer'");
            GROW(cf, p->nconflicts, ccf);
            LimePolicyConflict *c = &cf[p->nconflicts];
            memset(c, 0, sizeof(*c));
            c->kind = kind;
            c->rule = xstrdup(AT(2));
            c->lookahead = la ? xstrdup(la) : NULL;
            p->nconflicts++;
            if (kind == LIME_DK_DUP_RULE_ACTION && (nw != 5 || IS(k + 1, "shift") || IS(k + 1, "reduce")))
                BAD("expected: conflict rule \"rule\" prefer EXT.");
            if (IS(k + 1, "shift")) {
                if (kind != LIME_CK_SR) BAD("prefer shift applies to sr only");
                c->prefer = LIME_PREFER_SHIFT;
            } else if (IS(k + 1, "reduce")) {
                c->prefer = LIME_PREFER_REDUCE;
            } else {
                c->prefer = LIME_PREFER_OWNER;
                c->owner = xstrdup(AT(k + 1));
            }
        } else if (IS(0, "reject")) {
            /* reject "rule" from EXT.   reject token T from EXT. */
            int is_tok = IS(1, "token");
            uint32_t w = is_tok ? 2 : 1;
            if (nw != w + 3 || !IS(w + 1, "from") || (!is_tok && !tk[i + 1].quoted))
                BAD("expected: reject \"rule\" from EXT.  or  reject token T from EXT.");
            GROW(rj, p->nrejects, crj);
            rj[p->nrejects].is_token = is_tok;
            rj[p->nrejects].what = xstrdup(AT(w));
            rj[p->nrejects].owner = xstrdup(AT(w + 2));
            p->nrejects++;
        } else if (IS(0, "keyword")) {
            if (nw != 5 || !tk[i + 1].quoted || !IS(2, "owner") || !(IS(4, "soft") || IS(4, "reserved")))
                BAD("expected: keyword \"lexeme\" owner EXT soft|reserved.");
            GROW(kw, p->nkeywords, ckw);
            kw[p->nkeywords].lexeme = xstrdup(AT(1));
            kw[p->nkeywords].owner = xstrdup(AT(3));
            kw[p->nkeywords].soft = IS(4, "soft");
            p->nkeywords++;
        } else if (IS(0, "trust-extension-proposals")) {
            if (nw != 2) BAD("expected: trust-extension-proposals EXT.");
            GROW(tr, p->ntrusted, ctr);
            tr[p->ntrusted++] = xstrdup(AT(1));
        } else {
            BAD("unknown policy statement");
        }
        /* publish after each statement so lime_policy_free sees it on error */
        p->conflicts = cf; p->ranks = rk; p->tokens = to; p->rejects = rj;
        p->keywords = kw; p->trusted = (const char *const *)tr;
        i = end + 1;
    }
#undef AT
#undef IS
#undef LINE
#undef BAD
    p->conflicts = cf; p->ranks = rk; p->tokens = to; p->rejects = rj;
    p->keywords = kw; p->trusted = (const char *const *)tr;
    for (uint32_t k = 0; k < nt; k++) free(tk[k].s);
    free(tk);
    /* Two lines that rank the same owner differently is a policy error. */
    for (uint32_t a = 0; a < p->nranks; a++)
        for (uint32_t b2 = a + 1; b2 < p->nranks; b2++)
            if (strcmp(p->ranks[a].owner, p->ranks[b2].owner) == 0 && p->ranks[a].rank != p->ranks[b2].rank) {
                char buf[256];
                snprintf(buf, sizeof buf, "policy: %s ranked twice (%d and %d)", p->ranks[a].owner,
                         p->ranks[a].rank, p->ranks[b2].rank);
                if (error) *error = xstrdup(buf);
                lime_policy_free(p);
                return NULL;
            }
    return p;
bad:
    if (p) { p->conflicts = cf; p->ranks = rk; p->tokens = to; p->rejects = rj;
             p->keywords = kw; p->trusted = (const char *const *)tr; }
    if (error) *error = xstrdup(ebuf);
    for (uint32_t k = 0; k < nt; k++) free(tk[k].s);
    free(tk);
    lime_policy_free(p);
    return NULL;
oom:
    if (p) { p->conflicts = cf; p->ranks = rk; p->tokens = to; p->rejects = rj;
             p->keywords = kw; p->trusted = (const char *const *)tr; }
    if (error) *error = xstrdup("policy: out of memory");
    for (uint32_t k = 0; k < nt; k++) free(tk[k].s);
    free(tk);
    lime_policy_free(p);
    return NULL;
}
