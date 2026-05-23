/*
** strategy_bayesian.c -- Beta-Bernoulli Bayesian disambiguation.
**
** Tracks an independent Beta(alpha, beta) posterior for every
** (token, state, extension_id) tuple it has seen, where:
**
**   alpha = 1 + (#parses where this option won and the parse succeeded)
**   beta  = 1 + (#parses where this option won and the parse failed)
**
** The leading +1 is the Beta(1, 1) = Uniform(0, 1) Jeffreys-style prior
** -- before any evidence, every option has posterior mean 0.5.
**
** resolve() picks the option with the highest posterior mean
**   p = alpha / (alpha + beta)
** breaking ties by ext_id (smallest wins, deterministic).
**
** update() increments alpha or beta on every option that won the
** most recent resolve() -- the strategy assumes the parse outcome
** reflects on every disambiguation decision made during it.  This
** is the standard "credit assignment over the whole episode"
** treatment; it has known bias issues (a single bad arm in a chain
** of good ones gets blamed for the whole failure) but it is the
** simplest correct implementation and matches what an extension
** registry can practically observe -- per-decision feedback would
** require parser-engine instrumentation we don't have.
**
** Storage: array of BayesianEntry sorted by (token, state, ext_id)
** lookup by linear scan + sorted insert.  N is bounded by the
** number of distinct conflict points an extension actually triggers
** -- in practice tens to hundreds, so O(N) is fine.  If a deployment
** wants more, swap the array for a hash table -- the public API
** stays the same.
**
** This is in-memory only.  Persisting the posterior across process
** restarts is out of scope; the user is expected to either (a)
** re-train at startup or (b) layer their own persistence on top by
** wrapping the strategy.
*/

#include "disambiguation.h"
#include "extension.h"
#include "conflict.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/*  Per-(token, state, ext_id) posterior entry                         */
/* ------------------------------------------------------------------ */

typedef struct BayesianEntry {
    uint16_t token;
    int32_t state;
    uint32_t ext_id;
    uint32_t alpha; /* successes + 1 (Beta prior) */
    uint32_t beta;  /* failures  + 1 (Beta prior) */
} BayesianEntry;

/* Cap on remembered "last resolution" arms -- enough for a single
** parse step's conflict (typically 2-3 colliding extensions). */
#define BAYESIAN_LAST_CAP 16

typedef struct BayesianContext {
    BayesianEntry *entries;
    size_t count;
    size_t capacity;

    /* The arms that won the most recent resolve() call.  update()
    ** credits each of them with the parse outcome. */
    BayesianEntry *last_chosen[BAYESIAN_LAST_CAP];
    size_t last_count;
} BayesianContext;

/* ------------------------------------------------------------------ */
/*  Entry lookup / insert                                              */
/* ------------------------------------------------------------------ */

static int entry_cmp(uint16_t at, int32_t as, uint32_t ax, const BayesianEntry *b) {
    if (at != b->token) return (at < b->token) ? -1 : 1;
    if (as != b->state) return (as < b->state) ? -1 : 1;
    if (ax != b->ext_id) return (ax < b->ext_id) ? -1 : 1;
    return 0;
}

/*
** Find or insert the entry for (token, state, ext_id).  Returns NULL
** only on allocation failure.  Entries are kept sorted so that
** deterministic resolution is preserved across runs.
*/
static BayesianEntry *find_or_insert(BayesianContext *bc, uint16_t token, int32_t state,
                                     uint32_t ext_id) {
    /* Binary search on sorted entries. */
    size_t lo = 0, hi = bc->count;
    while (lo < hi) {
        size_t mid = (lo + hi) >> 1;
        int cmp = entry_cmp(token, state, ext_id, &bc->entries[mid]);
        if (cmp == 0) return &bc->entries[mid];
        if (cmp < 0) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }

    /* Not found; grow if needed and insert at lo. */
    if (bc->count == bc->capacity) {
        size_t new_cap = bc->capacity == 0 ? 16 : bc->capacity * 2;
        BayesianEntry *grown = realloc(bc->entries, new_cap * sizeof(BayesianEntry));
        if (grown == NULL) return NULL;
        bc->entries = grown;
        bc->capacity = new_cap;
    }
    if (lo < bc->count) {
        memmove(&bc->entries[lo + 1], &bc->entries[lo],
                (bc->count - lo) * sizeof(BayesianEntry));
    }
    BayesianEntry *e = &bc->entries[lo];
    e->token = token;
    e->state = state;
    e->ext_id = ext_id;
    e->alpha = 1; /* Beta(1, 1) prior = uniform */
    e->beta = 1;
    bc->count++;
    return e;
}

/* ------------------------------------------------------------------ */
/*  vtable callbacks                                                   */
/* ------------------------------------------------------------------ */

static void *bayesian_init(const Extension *const *extensions, uint32_t nextensions) {
    (void)extensions;
    (void)nextensions;

    BayesianContext *bc = calloc(1, sizeof(BayesianContext));
    if (bc == NULL) return NULL;
    /* Defer entry allocation until first resolve() -- saves the
    ** memory for callers that create the strategy but never actually
    ** trigger a conflict. */
    return bc;
}

static bool bayesian_resolve(void *strategy_context, const ConflictPoint *conflict,
                             struct ParseContext *parse_ctx, int lookahead,
                             StrategyResult *result) {
    (void)parse_ctx;
    (void)lookahead;

    if (strategy_context == NULL || conflict == NULL || result == NULL) return false;
    if (conflict->ncontexts <= 0 || conflict->contexts == NULL) return false;

    BayesianContext *bc = (BayesianContext *)strategy_context;
    bc->last_count = 0;

    /* Score each option by posterior mean.  Track the best score
    ** and the matching context index.  Ties break by ext_id (lower
    ** wins) -- inherited from sorted entries. */
    int best_idx = -1;
    float best_score = -1.0f;
    BayesianEntry *best_entry = NULL;

    for (int i = 0; i < conflict->ncontexts; i++) {
        const LimeContext *cx = &conflict->contexts[i];
        BayesianEntry *e = find_or_insert(bc, conflict->token, conflict->state, cx->ext_id);
        if (e == NULL) return false;
        float p = (float)e->alpha / (float)(e->alpha + e->beta);
        if (p > best_score) {
            best_score = p;
            best_idx = i;
            best_entry = e;
        }
    }

    if (best_idx < 0 || best_entry == NULL) return false;

    /* Fill the result. */
    result->winning_contexts = malloc(sizeof(LimeContext));
    if (result->winning_contexts == NULL) return false;
    result->winning_contexts[0] = conflict->contexts[best_idx];
    result->nwinners = 1;
    result->confidence = best_score;

    char buf[160];
    snprintf(buf, sizeof(buf),
             "bayesian: ext_id=%u wins (alpha=%u beta=%u, posterior mean=%.4f)",
             best_entry->ext_id, best_entry->alpha, best_entry->beta, (double)best_score);
    result->explanation = strdup(buf);

    /* Remember the chosen arm so update() can credit it.  We only
    ** remember the winner here -- per-arm credit assignment for
    ** every option in conflict->contexts would over-count failures
    ** against options we didn't actually use. */
    bc->last_chosen[0] = best_entry;
    bc->last_count = 1;

    return true;
}

static void bayesian_update(void *strategy_context, struct ExtensionRegistry *registry,
                            bool success) {
    (void)registry;

    if (strategy_context == NULL) return;
    BayesianContext *bc = (BayesianContext *)strategy_context;

    for (size_t i = 0; i < bc->last_count; i++) {
        BayesianEntry *e = bc->last_chosen[i];
        if (e == NULL) continue;
        if (success) {
            /* Saturate at UINT32_MAX - 1 to keep alpha + beta from
            ** overflowing.  Realistically unreachable on any human
            ** workload but cheap to guard. */
            if (e->alpha < UINT32_MAX - 1) e->alpha++;
        } else {
            if (e->beta < UINT32_MAX - 1) e->beta++;
        }
    }
    bc->last_count = 0;
}

static void bayesian_destroy(void *strategy_context) {
    if (strategy_context == NULL) return;
    BayesianContext *bc = (BayesianContext *)strategy_context;
    free(bc->entries);
    free(bc);
}

/* ------------------------------------------------------------------ */
/*  Exported vtable                                                    */
/* ------------------------------------------------------------------ */

const DisambiguationStrategyVTable strategy_bayesian_vtable = {
    .init = bayesian_init,
    .resolve = bayesian_resolve,
    .update = bayesian_update,
    .destroy = bayesian_destroy,
};
