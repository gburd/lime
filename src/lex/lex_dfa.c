/*
** src/lex/lex_dfa.c -- subset construction (NFA -> DFA).
**
** Standard powerset algorithm:
**   - DFA state 0 = epsilon-closure({nfa.start}).
**   - For each unprocessed DFA state and each byte b in 0..255:
**       move = { t : (s, b, t) in NFA edges, s in current set }
**       move = epsilon-closure(move)
**       if move is non-empty, look up an existing DFA state with
**         the same NFA set or create one.
**   - Mark any DFA state whose NFA set contains nfa.accept as
**     accepting.
**
** State set representation: 256-entry bit array per NFA state
** count (rounded up to bytes).  Linear scan for dedup -- fine
** for the 6 audit-corpus PG scanners (each DFA is bounded;
** scan.l weighs in around 600 NFA states post-Thompson, which
** subset-constructs to a few hundred DFA states).
*/

#include "lex_dfa.h"
#include "lex_nfa.h"

#include <stdlib.h>
#include <string.h>

#define MAX_NFA_STATES 4096

/* Bitmap utilities -- size in bytes is rounded up. */
static size_t bitmap_bytes_for(int nstates) {
    return (size_t)((nstates + 7) / 8);
}

static void bmap_set(unsigned char *b, int i) {
    b[i >> 3] |= (unsigned char)(1u << (i & 7));
}

static int bmap_has(const unsigned char *b, int i) {
    return (b[i >> 3] >> (i & 7)) & 1;
}

/* ============================================================
** Subset construction state
** ============================================================ */

/* Per-DFA-state record of the NFA state set it represents.
** Used for deduplication. */
typedef struct {
    unsigned char *set; /* bitmap, length = nfa_bytes */
} DfaSetRec;

typedef struct {
    const LimeNfa *nfa;
    size_t nfa_bytes;
    LimeDfa *dfa;

    /* Set-record table parallel to dfa->states. */
    DfaSetRec *records;
    int rec_cap;

    /* Worklist queue (DFA state ids to process). */
    int *worklist;
    int work_head;
    int work_tail;
    int work_cap;
} Build;

/* ============================================================
** NFA helpers (epsilon-closure, byte-step)
** ============================================================ */

static void eps_closure(const LimeNfa *nfa, unsigned char *set) {
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int i = 0; i < nfa->n_states; i++) {
            if (!bmap_has(set, i)) continue;
            for (LimeNfaEdge *e = nfa->states[i].edges; e; e = e->next) {
                if (e->kind == LIME_NFA_EPS && !bmap_has(set, e->target)) {
                    bmap_set(set, e->target);
                    changed = 1;
                }
            }
        }
    }
}

static int class_matches(const LimeNfaEdge *e, unsigned char byte) {
    int idx = byte;
    int has = (e->u.char_class.bits[idx >> 3] >> (idx & 7)) & 1;
    return e->u.char_class.negate ? !has : has;
}

/* Compute the move() set: states reachable from any state in
** `cur` via a non-epsilon edge consuming byte `b`.  Then apply
** epsilon-closure.  Returns 1 if the result is non-empty. */
static int move_then_eps(const LimeNfa *nfa, const unsigned char *cur, unsigned char *out,
                         size_t bytes, unsigned char byte) {
    memset(out, 0, bytes);
    int any = 0;
    for (int s = 0; s < nfa->n_states; s++) {
        if (!bmap_has(cur, s)) continue;
        for (LimeNfaEdge *e = nfa->states[s].edges; e; e = e->next) {
            if (e->kind == LIME_NFA_BYTE && e->u.byte == byte) {
                bmap_set(out, e->target);
                any = 1;
            } else if (e->kind == LIME_NFA_CLASS && class_matches(e, byte)) {
                bmap_set(out, e->target);
                any = 1;
            }
        }
    }
    if (any) eps_closure(nfa, out);
    return any;
}

/* ============================================================
** DFA state allocation + dedup
** ============================================================ */

static int dfa_grow(LimeDfa *dfa, int needed) {
    if (dfa->cap >= needed) return 0;
    int newcap = dfa->cap ? dfa->cap : 16;
    while (newcap < needed)
        newcap *= 2;
    LimeDfaState *ns = realloc(dfa->states, newcap * sizeof(*ns));
    if (!ns) return -1;
    for (int i = dfa->cap; i < newcap; i++) {
        memset(&ns[i], 0, sizeof(ns[i]));
        for (int b = 0; b < 256; b++)
            ns[i].trans[b] = -1;
    }
    dfa->states = ns;
    dfa->cap = newcap;
    return 0;
}

static int recs_grow(Build *bld, int needed) {
    if (bld->rec_cap >= needed) return 0;
    int newcap = bld->rec_cap ? bld->rec_cap : 16;
    while (newcap < needed)
        newcap *= 2;
    DfaSetRec *nr = realloc(bld->records, newcap * sizeof(*nr));
    if (!nr) return -1;
    for (int i = bld->rec_cap; i < newcap; i++)
        nr[i].set = NULL;
    bld->records = nr;
    bld->rec_cap = newcap;
    return 0;
}

static int worklist_push(Build *bld, int id) {
    if (bld->work_tail == bld->work_cap) {
        int newcap = bld->work_cap ? bld->work_cap * 2 : 16;
        int *nw = realloc(bld->worklist, newcap * sizeof(int));
        if (!nw) return -1;
        bld->worklist = nw;
        bld->work_cap = newcap;
    }
    bld->worklist[bld->work_tail++] = id;
    return 0;
}

static int worklist_pop(Build *bld, int *out) {
    if (bld->work_head >= bld->work_tail) return 0;
    *out = bld->worklist[bld->work_head++];
    return 1;
}

/* Find an existing DFA state matching the NFA set, or create
** one.  Returns the DFA state id, or -1 on alloc failure. */
static int intern_state(Build *bld, const unsigned char *set, int *created_out) {
    if (created_out) *created_out = 0;
    /* Linear scan for dedup.  Fine for the corpus size. */
    for (int i = 0; i < bld->dfa->n_states; i++) {
        if (memcmp(bld->records[i].set, set, bld->nfa_bytes) == 0) {
            return i;
        }
    }
    /* Create new. */
    if (dfa_grow(bld->dfa, bld->dfa->n_states + 1) < 0) return -1;
    if (recs_grow(bld, bld->dfa->n_states + 1) < 0) return -1;
    int id = bld->dfa->n_states++;
    bld->dfa->states[id].id = id;
    /* trans[] already initialised to -1 by dfa_grow. */
    bld->records[id].set = malloc(bld->nfa_bytes);
    if (!bld->records[id].set) return -1;
    memcpy(bld->records[id].set, set, bld->nfa_bytes);
    /* Determine accept status: a DFA state is accepting iff any
    ** NFA state in its set is accepting.  Among accepting NFA
    ** states, pick the LOWEST rule id (declaration order =
    ** priority on length ties; rule 0 wins over rule 1).  When
    ** the NFA has a single accept state (single-rule input from
    ** lime_lex_nfa_from_regex), this is just "accept_rule = 0". */
    {
        int best_rule = -1;
        int any_accept = 0;
        for (int s = 0; s < bld->nfa->n_states; s++) {
            if (!bmap_has(set, s)) continue;
            if (!bld->nfa->states[s].is_accept) continue;
            int r = bld->nfa->states[s].accept_rule;
            if (!any_accept || r < best_rule) {
                best_rule = r;
                any_accept = 1;
            }
        }
        if (any_accept) {
            bld->dfa->states[id].is_accept = 1;
            bld->dfa->states[id].accept_rule = best_rule;
        }
    }
    if (created_out) *created_out = 1;
    return id;
}

/* ============================================================
** Public entry point
** ============================================================ */

LimeDfa *lime_lex_nfa_to_dfa(const LimeNfa *nfa) {
    if (!nfa) return NULL;
    if (nfa->n_states > MAX_NFA_STATES) return NULL;

    LimeDfa *dfa = calloc(1, sizeof(*dfa));
    if (!dfa) return NULL;

    Build bld = { 0 };
    bld.nfa = nfa;
    bld.nfa_bytes = bitmap_bytes_for(nfa->n_states);
    bld.dfa = dfa;

    /* Seed with epsilon-closure of {nfa.start}. */
    unsigned char *seed = calloc(bld.nfa_bytes, 1);
    if (!seed) goto fail;
    bmap_set(seed, nfa->start);
    eps_closure(nfa, seed);

    int created = 0;
    int s0 = intern_state(&bld, seed, &created);
    free(seed);
    if (s0 < 0) goto fail;
    dfa->start = s0;
    if (worklist_push(&bld, s0) < 0) goto fail;

    /* Process worklist. */
    int cur_id;
    unsigned char *next_set = malloc(bld.nfa_bytes);
    if (!next_set) goto fail;
    while (worklist_pop(&bld, &cur_id)) {
        const unsigned char *cur_set = bld.records[cur_id].set;
        for (int b = 0; b < 256; b++) {
            if (!move_then_eps(nfa, cur_set, next_set, bld.nfa_bytes, (unsigned char)b)) {
                continue;
            }
            int new_state = 0;
            int next_id = intern_state(&bld, next_set, &new_state);
            if (next_id < 0) {
                free(next_set);
                goto fail;
            }
            dfa->states[cur_id].trans[b] = next_id;
            if (new_state) {
                if (worklist_push(&bld, next_id) < 0) {
                    free(next_set);
                    goto fail;
                }
            }
        }
    }
    free(next_set);

    /* Free records (no longer needed once construction completes). */
    for (int i = 0; i < bld.dfa->n_states; i++)
        free(bld.records[i].set);
    free(bld.records);
    free(bld.worklist);
    return dfa;

fail:
    if (bld.records) {
        for (int i = 0; i < bld.dfa->n_states; i++)
            free(bld.records[i].set);
        free(bld.records);
    }
    free(bld.worklist);
    lime_lex_dfa_free(dfa);
    return NULL;
}

void lime_lex_dfa_free(LimeDfa *dfa) {
    if (!dfa) return;
    free(dfa->states);
    free(dfa);
}

int lime_lex_dfa_match(const LimeDfa *dfa, const char *bytes, size_t n) {
    if (!dfa) return 0;
    int s = dfa->start;
    for (size_t i = 0; i < n; i++) {
        s = dfa->states[s].trans[(unsigned char)bytes[i]];
        if (s < 0) return 0;
    }
    return dfa->states[s].is_accept ? 1 : 0;
}

int lime_lex_dfa_state_count(const LimeDfa *dfa) {
    return dfa ? dfa->n_states : 0;
}

void lime_lex_dfa_leading_bytes(const LimeDfa *dfa, unsigned char byte_set[256]) {
    for (int i = 0; i < 256; i++) byte_set[i] = 0;
    if (!dfa || dfa->n_states <= 0) return;
    /* Anchor handling: lime's NFA-to-DFA construction encodes
    ** ANCHOR_START / ANCHOR_END as edges keyed on synthetic byte
    ** values; those don't appear in the trans[256] table.  So
    ** scanning trans[0..255] gives the actual reachable input
    ** bytes correctly even for anchored rules.
    **
    ** A rule with `^` only fires at start-of-input -- the leading
    ** byte set still describes which BYTES advance the DFA, but
    ** the runtime must additionally verify the input position is
    ** 0 before treating this rule as a candidate.  That's a
    ** runtime concern; we only compute the byte set here. */
    const int s = dfa->start;
    if (s < 0 || s >= dfa->n_states) return;
    const int *tr = dfa->states[s].trans;
    for (int b = 0; b < 256; b++) {
        if (tr[b] >= 0) byte_set[b] = 1;
    }
}

int lime_lex_dfa_single_byte(const LimeDfa *dfa) {
    if (!dfa || dfa->n_states < 2) return -1;
    const LimeDfaState *start = &dfa->states[dfa->start];
    if (start->is_accept) return -1;  /* empty match = not single-byte */
    int found = -1;
    for (int b = 0; b < 256; b++) {
        if (start->trans[b] < 0) continue;
        const LimeDfaState *next = &dfa->states[start->trans[b]];
        /* Must transition to an accepting state with no further
        ** transitions out (so the DFA accepts EXACTLY this byte). */
        if (!next->is_accept) return -1;
        for (int b2 = 0; b2 < 256; b2++) {
            if (next->trans[b2] >= 0) return -1;
        }
        if (found != -1) return -1;  /* multiple bytes accepted */
        found = b;
    }
    return found;
}

int lime_lex_dfa_fixed_string(const LimeDfa *dfa, unsigned char out_str[256], int *out_len) {
    if (!dfa || dfa->n_states < 2) return 0;
    if (out_len) *out_len = 0;
    int s = dfa->start;
    int len = 0;
    while (1) {
        const LimeDfaState *st = &dfa->states[s];
        if (st->is_accept) {
            /* Accepting -- if this state ALSO has outgoing transitions,
            ** the DFA accepts a prefix and a longer extension, which
            ** is not a fixed-string. */
            for (int b = 0; b < 256; b++) {
                if (st->trans[b] >= 0) return 0;
            }
            if (out_len) *out_len = len;
            return len > 0 ? 1 : 0;
        }
        /* Non-accept: must have exactly ONE outgoing transition for
        ** a fixed-string DFA.  Multiple transitions = alternation. */
        int b_found = -1;
        int t_found = -1;
        for (int b = 0; b < 256; b++) {
            if (st->trans[b] < 0) continue;
            if (b_found != -1) return 0;  /* multiple outgoing */
            b_found = b;
            t_found = st->trans[b];
        }
        if (b_found < 0) return 0;
        if (len >= 255) return 0;  /* string too long for our buffer */
        if (out_str) out_str[len] = (unsigned char)b_found;
        len++;
        if (t_found == s) return 0;  /* self-loop = not fixed */
        s = t_found;
    }
}
