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

#include <stdint.h>
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
    uint32_t hash;      /* FNV-1a hash of `set` */
    int next;           /* next record index in same bucket, or -1 */
} DfaSetRec;

typedef struct {
    const LimeNfa *nfa;
    size_t nfa_bytes;
    LimeDfa *dfa;

    /* Set-record table parallel to dfa->states. */
    DfaSetRec *records;
    int rec_cap;

    /* Hash table for dedup.  Keyed on FNV-1a of the bitmap;
    ** buckets[h] is the head of a singly-linked list through
    ** records[].next.  Per .agent/notes/c-perf-audit.md item
    ** #3: replaces an O(n_states) memcmp linear scan with a
    ** O(1)-on-average hash bucket scan.  Doubles for power-of-2
    ** efficiency when load factor exceeds 4. */
    int *buckets;
    int n_buckets; /* always power of 2 */

    /* Worklist queue (DFA state ids to process). */
    int *worklist;
    int work_head;
    int work_tail;
    int work_cap;
} Build;

/* ============================================================
** NFA helpers (epsilon-closure, byte-step)
**
** B4: worklist closure (each NFA state pushed once) instead of
** iterating to a fixed point over every NFA state, and the move step
** walks the member list of the current set instead of scanning all
** NFA states.  Both produce exactly the same sets as before; the
** subset construction and therefore the DFA are unchanged
** (tests/test_lex_dfa_identity checks a table hash).
** ============================================================ */

/* Scratch shared by closure/move for one construction. */
typedef struct {
    int *stack;   /* worklist, nfa->n_states slots */
    int *members; /* member list of the set being moved, n_states slots */
} Scratch;

static void eps_closure(const LimeNfa *nfa, unsigned char *set, Scratch *sc) {
    int top = 0;
    for (int i = 0; i < nfa->n_states; i++)
        if (bmap_has(set, i)) sc->stack[top++] = i;
    while (top > 0) {
        int i = sc->stack[--top];
        for (LimeNfaEdge *e = nfa->states[i].edges; e; e = e->next) {
            if (e->kind == LIME_NFA_EPS && !bmap_has(set, e->target)) {
                bmap_set(set, e->target);
                sc->stack[top++] = e->target;
            }
        }
    }
}

static int class_matches(const LimeNfaEdge *e, unsigned char byte) {
    int idx = byte;
    int has = (e->u.char_class.bits[idx >> 3] >> (idx & 7)) & 1;
    return e->u.char_class.negate ? !has : has;
}

/* Compute the move() set: states reachable from any of the `nm`
** member states via a non-epsilon edge consuming byte `b`.  Then
** apply epsilon-closure.  Returns 1 if the result is non-empty. */
static int move_then_eps(const LimeNfa *nfa, const int *members, int nm, unsigned char *out,
                         size_t bytes, unsigned char byte, Scratch *sc) {
    memset(out, 0, bytes);
    int any = 0;
    for (int k = 0; k < nm; k++) {
        for (LimeNfaEdge *e = nfa->states[members[k]].edges; e; e = e->next) {
            if (e->kind == LIME_NFA_BYTE && e->u.byte == byte) {
                bmap_set(out, e->target);
                any = 1;
            } else if (e->kind == LIME_NFA_CLASS && class_matches(e, byte)) {
                bmap_set(out, e->target);
                any = 1;
            }
        }
    }
    if (any) eps_closure(nfa, out, sc);
    return any;
}

/* Byte equivalence classes: two bytes are equivalent when every NFA
** edge treats them alike (both match it or neither does).  Then every
** set moves identically on them, so subset construction computes one
** move per class instead of per byte.  cls[b] = class id; rep[c] =
** the smallest byte of class c.  Returns the number of classes.
** Refinement: start with one class, split by each edge's byte set. */
static int byte_classes(const LimeNfa *nfa, int cls[256], int rep[256]) {
    for (int b = 0; b < 256; b++) cls[b] = 0;
    int ncls = 1;
    unsigned char in[256];
    for (int i = 0; i < nfa->n_states; i++) {
        for (LimeNfaEdge *e = nfa->states[i].edges; e; e = e->next) {
            if (e->kind == LIME_NFA_EPS) continue;
            for (int b = 0; b < 256; b++)
                in[b] = e->kind == LIME_NFA_BYTE ? (e->u.byte == b)
                                                 : (unsigned char)class_matches(e, (unsigned char)b);
            /* split every class that has bytes both in and out */
            int newid[256];
            for (int c = 0; c < ncls; c++) newid[c] = -1;
            for (int b = 0; b < 256; b++) {
                if (!in[b]) continue;
                int c = cls[b];
                if (newid[c] == -1) {
                    /* does class c have a byte outside the edge set? */
                    int split = 0;
                    for (int x = 0; x < 256 && !split; x++) split = cls[x] == c && !in[x];
                    newid[c] = split ? ncls++ : c;
                }
                cls[b] = newid[c];
            }
        }
    }
    for (int c = 0; c < ncls; c++) rep[c] = -1;
    for (int b = 0; b < 256; b++)
        if (rep[cls[b]] < 0) rep[cls[b]] = b;
    return ncls;
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
    for (int i = bld->rec_cap; i < newcap; i++) {
        nr[i].set = NULL;
        nr[i].hash = 0;
        nr[i].next = -1;
    }
    bld->records = nr;
    bld->rec_cap = newcap;
    return 0;
}

/* FNV-1a 32-bit hash of a bitmap.  Cheap, decent distribution
** for set-bit patterns; collisions resolved via memcmp. */
static uint32_t fnv1a_hash(const unsigned char *p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h ^= (uint32_t)p[i];
        h *= 16777619u;
    }
    return h;
}

/* Initialise / grow the bucket table to at least `min` slots.
** Always rounds up to a power of 2 so we can mask instead of
** mod.  Rehashes existing records into the new buckets. */
static int buckets_ensure(Build *bld, int min) {
    if (bld->buckets && bld->n_buckets >= min) return 0;
    int newcap = bld->n_buckets ? bld->n_buckets : 64;
    while (newcap < min) newcap *= 2;
    int *nb = realloc(bld->buckets, newcap * sizeof(*nb));
    if (!nb) return -1;
    for (int i = 0; i < newcap; i++) nb[i] = -1;
    /* Rehash existing records. */
    for (int i = 0; i < bld->dfa->n_states; i++) {
        bld->records[i].next = nb[bld->records[i].hash & (uint32_t)(newcap - 1)];
        nb[bld->records[i].hash & (uint32_t)(newcap - 1)] = i;
    }
    bld->buckets = nb;
    bld->n_buckets = newcap;
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
** one.  Returns the DFA state id, or -1 on alloc failure.
** Uses the bucket-keyed hash table (Build.buckets) to avoid the
** former O(n_states) linear memcmp scan. */
static int intern_state(Build *bld, const unsigned char *set, int *created_out) {
    if (created_out) *created_out = 0;
    /* Make sure the bucket table is sized for the current
    ** record count plus the new candidate (load factor <= 4). */
    if (buckets_ensure(bld, (bld->dfa->n_states + 1) * 2) < 0) return -1;
    uint32_t h = fnv1a_hash(set, bld->nfa_bytes);
    uint32_t mask = (uint32_t)(bld->n_buckets - 1);
    int b = (int)(h & mask);
    /* Walk the bucket chain; only memcmp on hash collision. */
    for (int i = bld->buckets[b]; i >= 0; i = bld->records[i].next) {
        if (bld->records[i].hash != h) continue;
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
    bld->records[id].hash = h;
    /* Re-fetch bucket index in case buckets grew through buckets_ensure
    ** above (it didn't here since we already passed buckets_ensure --
    ** but defensive in case future edits move things). */
    b = (int)(h & (uint32_t)(bld->n_buckets - 1));
    bld->records[id].next = bld->buckets[b];
    bld->buckets[b] = id;
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

    Scratch sc;
    sc.stack = malloc((size_t)(nfa->n_states > 0 ? nfa->n_states : 1) * sizeof(int));
    sc.members = malloc((size_t)(nfa->n_states > 0 ? nfa->n_states : 1) * sizeof(int));
    int cls[256], rep[256];
    int ncls = byte_classes(nfa, cls, rep);

    /* Seed with epsilon-closure of {nfa.start}. */
    unsigned char *seed = calloc(bld.nfa_bytes, 1);
    if (!seed || !sc.stack || !sc.members) {
        free(seed);
        free(sc.stack);
        free(sc.members);
        goto fail;
    }
    bmap_set(seed, nfa->start);
    eps_closure(nfa, seed, &sc);

    int created = 0;
    int s0 = intern_state(&bld, seed, &created);
    free(seed);
    if (s0 < 0) goto fail_sc;
    dfa->start = s0;
    if (worklist_push(&bld, s0) < 0) goto fail_sc;

    /* Process worklist.  Per DFA state: one move per byte class (the
    ** class representative stands for all its bytes), then the target
    ** is written for every byte of the class.  Interning happens in
    ** byte order of the representatives, which is the same order the
    ** per-byte loop discovered new states in, so state numbering is
    ** unchanged. */
    int cur_id;
    unsigned char *next_set = malloc(bld.nfa_bytes);
    if (!next_set) goto fail_sc;
    while (worklist_pop(&bld, &cur_id)) {
        int nm = 0;
        {
            const unsigned char *cur_set = bld.records[cur_id].set;
            for (int i = 0; i < nfa->n_states; i++)
                if (bmap_has(cur_set, i)) sc.members[nm++] = i;
        }
        int cls_target[256];
        for (int c = 0; c < ncls; c++) cls_target[c] = -2; /* not computed */
        for (int b = 0; b < 256; b++) {
            int c = cls[b];
            if (cls_target[c] == -2) {
                cls_target[c] = -1;
                if (move_then_eps(nfa, sc.members, nm, next_set, bld.nfa_bytes,
                                  (unsigned char)rep[c], &sc)) {
                    int new_state = 0;
                    int next_id = intern_state(&bld, next_set, &new_state);
                    if (next_id < 0 || (new_state && worklist_push(&bld, next_id) < 0)) {
                        free(next_set);
                        goto fail_sc;
                    }
                    cls_target[c] = next_id;
                }
            }
            dfa->states[cur_id].trans[b] = cls_target[c];
        }
    }
    free(next_set);
    free(sc.stack);
    free(sc.members);

    /* Free records (no longer needed once construction completes). */
    for (int i = 0; i < bld.dfa->n_states; i++)
        free(bld.records[i].set);
    free(bld.records);
    free(bld.buckets);
    free(bld.worklist);
    return dfa;

fail_sc:
    free(sc.stack);
    free(sc.members);
fail:
    if (bld.records) {
        for (int i = 0; i < bld.dfa->n_states; i++)
            free(bld.records[i].set);
        free(bld.records);
    }
    free(bld.buckets);
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
