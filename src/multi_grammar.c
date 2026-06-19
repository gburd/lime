/*
** multi_grammar.c -- Tiers 1 & 3 of runtime overlap disambiguation.
**
** Tier 1 (lime_mg_resolve): rank candidate grammars by simulating each
** over the upcoming token stream (lime_simulate_parse), preferring the
** one that reaches accept / gets furthest.  Built entirely on the
** read-only engine simulation in parse_engine.c -- no parser cloning,
** no actions, no mutation of any live parse.
**
** Tier 3 (LimeDialectRegistry): a name -> composed-snapshot map plus a
** leading-@dialect sigil parser, for mode selection when no parse-time
** method can separate the dialects.
**
** Tier 2 (LimeBayesStore) is implemented in strategy_bayesian.c and
** consulted here only as a tie-break.
*/

#include "multi_grammar.h"
#include "snapshot.h"

#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/*  Tier 1: full-statement fork-resolve                                */
/* ================================================================== */

/* Strict Tier-1 comparison of two trials.  Returns >0 if A is the
** better parse, <0 if B is, 0 if they are indistinguishable on the
** Tier-1 metrics (reached_accept, error_count, tokens_consumed). */
static int trial_cmp(const LimeForkTrial *a, const LimeForkTrial *b) {
    if (a->reached_accept != b->reached_accept) {
        return a->reached_accept ? 1 : -1;
    }
    if (a->error_count != b->error_count) {
        /* fewer errors is better */
        return (a->error_count < b->error_count) ? 1 : -1;
    }
    if (a->tokens_consumed != b->tokens_consumed) {
        /* got furthest is better */
        return (a->tokens_consumed > b->tokens_consumed) ? 1 : -1;
    }
    return 0;
}

int lime_mg_resolve(const LimeForkCandidate *cands, uint32_t ncands, const int *lookahead,
                    uint32_t nlook, struct LimeBayesStore *bayes, uint16_t bayes_state,
                    uint16_t bayes_token, LimeForkRank *out_ranks) {
    if (cands == NULL || ncands == 0) return -1;

    /* Simulate every candidate over the same lookahead buffer. */
    LimeForkTrial *trials = (LimeForkTrial *)calloc(ncands, sizeof(LimeForkTrial));
    if (trials == NULL) return -1;
    for (uint32_t i = 0; i < ncands; i++) {
        trials[i] = lime_simulate_parse(cands[i].snapshot, lookahead, nlook);
        if (out_ranks != NULL) {
            out_ranks[i].trial = trials[i];
            out_ranks[i].tied_winner = false;
        }
    }

    /* Find the best on Tier-1 metrics. */
    int best = 0;
    for (uint32_t i = 1; i < ncands; i++) {
        if (trial_cmp(&trials[i], &trials[best]) > 0) best = (int)i;
    }

    /* Gather the set tied with `best` on Tier-1 metrics. */
    uint32_t tie_count = 0;
    for (uint32_t i = 0; i < ncands; i++) {
        if (trial_cmp(&trials[i], &trials[best]) == 0) {
            if (out_ranks != NULL) out_ranks[i].tied_winner = true;
            tie_count++;
        }
    }

    /* If the best parse failed outright for everyone (no accept, all
    ** errored on token 0), there is no usable winner. */
    if (!trials[best].reached_accept && trials[best].tokens_consumed == 0
        && trials[best].error_count > 0) {
        free(trials);
        return -1;
    }

    if (tie_count <= 1) {
        free(trials);
        return best;
    }

    /* --- Tie among >=2 candidates: Tier 2 then priority then ext_id. */

    /* Tier 2: Bayesian posterior over the tied arms (if a store was
    ** supplied).  Only the tied candidates are eligible. */
    if (bayes != NULL) {
        uint32_t *ids = (uint32_t *)malloc(tie_count * sizeof(uint32_t));
        int *map = (int *)malloc(tie_count * sizeof(int));
        if (ids != NULL && map != NULL) {
            uint32_t k = 0;
            for (uint32_t i = 0; i < ncands; i++) {
                if (trial_cmp(&trials[i], &trials[best]) == 0) {
                    ids[k] = cands[i].ext_id;
                    map[k] = (int)i;
                    k++;
                }
            }
            float conf = 0.0f;
            int bi = lime_bayes_rank(bayes, bayes_state, bayes_token, ids, tie_count, &conf);
            /* Only trust the Bayesian pick once it has real evidence
            ** (posterior mean moved off the 0.5 uniform prior); with no
            ** evidence fall through to the deterministic tie-breaks so
            ** behaviour is stable and reproducible. */
            if (bi >= 0 && conf > 0.5f) {
                int winner = map[bi];
                free(ids); free(map); free(trials);
                return winner;
            }
        }
        free(ids);
        free(map);
    }

    /* Tier-1.5: lowest priority value among the tied. */
    int prio_best = -1;
    for (uint32_t i = 0; i < ncands; i++) {
        if (trial_cmp(&trials[i], &trials[best]) != 0) continue;
        if (prio_best < 0 || cands[i].priority < cands[prio_best].priority) {
            prio_best = (int)i;
        }
    }
    /* Final deterministic tie-break: among those sharing the lowest
    ** priority, the lowest ext_id. */
    int winner = prio_best;
    for (uint32_t i = 0; i < ncands; i++) {
        if (trial_cmp(&trials[i], &trials[best]) != 0) continue;
        if (cands[i].priority != cands[prio_best].priority) continue;
        if (cands[i].ext_id < cands[winner].ext_id) winner = (int)i;
    }

    free(trials);
    return winner;
}

/* ================================================================== */
/*  Tier 3: dialect mode selection                                     */
/* ================================================================== */

typedef struct DialectEntry {
    char *name;             /* owned */
    ParserSnapshot *snap;   /* reference held */
} DialectEntry;

struct LimeDialectRegistry {
    DialectEntry *entries;
    uint32_t count;
    uint32_t capacity;
};

LimeDialectRegistry *lime_dialect_registry_create(void) {
    return (LimeDialectRegistry *)calloc(1, sizeof(LimeDialectRegistry));
}

void lime_dialect_registry_destroy(LimeDialectRegistry *reg) {
    if (reg == NULL) return;
    for (uint32_t i = 0; i < reg->count; i++) {
        free(reg->entries[i].name);
        if (reg->entries[i].snap != NULL) snapshot_release(reg->entries[i].snap);
    }
    free(reg->entries);
    free(reg);
}

bool lime_dialect_register(LimeDialectRegistry *reg, const char *name, ParserSnapshot *snap) {
    if (reg == NULL || name == NULL || name[0] == '\0' || snap == NULL) return false;

    /* Replace an existing entry with the same name. */
    for (uint32_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->entries[i].name, name) == 0) {
            ParserSnapshot *acq = snapshot_acquire(snap);
            if (acq == NULL) return false;
            if (reg->entries[i].snap != NULL) snapshot_release(reg->entries[i].snap);
            reg->entries[i].snap = acq;
            return true;
        }
    }

    if (reg->count == reg->capacity) {
        uint32_t ncap = reg->capacity == 0 ? 8 : reg->capacity * 2;
        DialectEntry *grown = (DialectEntry *)realloc(reg->entries, ncap * sizeof(DialectEntry));
        if (grown == NULL) return false;
        reg->entries = grown;
        reg->capacity = ncap;
    }
    char *namedup = strdup(name);
    if (namedup == NULL) return false;
    ParserSnapshot *acq = snapshot_acquire(snap);
    if (acq == NULL) { free(namedup); return false; }
    reg->entries[reg->count].name = namedup;
    reg->entries[reg->count].snap = acq;
    reg->count++;
    return true;
}

ParserSnapshot *lime_dialect_select(const LimeDialectRegistry *reg, const char *name) {
    if (reg == NULL || name == NULL) return NULL;
    for (uint32_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->entries[i].name, name) == 0) return reg->entries[i].snap;
    }
    return NULL;
}

static int is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

bool lime_dialect_parse_sigil(const LimeDialectRegistry *reg, const char *input,
                              ParserSnapshot **out_snap, size_t *out_offset) {
    if (out_snap) *out_snap = NULL;
    if (out_offset) *out_offset = 0;
    if (reg == NULL || input == NULL) return false;

    const char *p = input;
    /* Optional leading whitespace before the sigil. */
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '@') return false;
    const char *name_start = p + 1;
    const char *q = name_start;
    while (is_ident_char(*q)) q++;
    if (q == name_start) return false; /* bare '@' is not a sigil */

    size_t name_len = (size_t)(q - name_start);
    char namebuf[128];
    if (name_len >= sizeof(namebuf)) return false;
    memcpy(namebuf, name_start, name_len);
    namebuf[name_len] = '\0';

    ParserSnapshot *snap = lime_dialect_select(reg, namebuf);
    if (snap == NULL) return false; /* unknown dialect: caller uses default */

    /* Skip whitespace after the sigil to the statement body. */
    while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;

    if (out_snap) *out_snap = snap;
    if (out_offset) *out_offset = (size_t)(q - input);
    return true;
}
