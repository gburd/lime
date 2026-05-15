/*
** src/lex/lex_nfa.c -- Thompson's NFA construction.
**
** Each regex AST node yields a fragment with one entry and one
** exit state.  Combinators glue fragments via epsilon-edges.
** State growth is by doubling; edges are heap-allocated and
** linked off each state.
*/

#include "lex_nfa.h"
#include "lex_regex.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================
** State / edge allocation
** ============================================================ */

/* Allocate a new state in the NFA; returns its id, or -1 on
** alloc failure. */
static int new_state(LimeNfa *n) {
    if (n->n_states == n->cap) {
        int newcap = n->cap ? n->cap * 2 : 16;
        LimeNfaState *ns = realloc(n->states, newcap * sizeof(*ns));
        if (!ns) return -1;
        memset(ns + n->cap, 0, (newcap - n->cap) * sizeof(*ns));
        n->states = ns;
        n->cap = newcap;
    }
    int id = n->n_states++;
    n->states[id].id = id;
    n->states[id].edges = NULL;
    n->states[id].is_accept = 0;
    n->states[id].anchor_start = 0;
    n->states[id].anchor_end = 0;
    return id;
}

/* Add an outgoing edge to state `from`.  Returns 0 on success,
** -1 on alloc failure. */
static int add_edge(LimeNfa *n, int from, LimeNfaEdgeKind kind,
                    int target, const void *data) {
    LimeNfaEdge *e = calloc(1, sizeof(*e));
    if (!e) return -1;
    e->kind   = kind;
    e->target = target;
    if (kind == LIME_NFA_BYTE) {
        e->u.byte = *(const unsigned char *)data;
    } else if (kind == LIME_NFA_CLASS) {
        const unsigned char *src = data;
        memcpy(e->u.char_class.bits, src, 32);
        e->u.char_class.negate = src[32];
    }
    e->next = n->states[from].edges;
    n->states[from].edges = e;
    return 0;
}

static int add_eps(LimeNfa *n, int from, int target) {
    return add_edge(n, from, LIME_NFA_EPS, target, NULL);
}

static int add_byte(LimeNfa *n, int from, int target, unsigned char b) {
    return add_edge(n, from, LIME_NFA_BYTE, target, &b);
}

static int add_class(LimeNfa *n, int from, int target,
                     const unsigned char *bits32, int negate) {
    unsigned char tmp[33];
    memcpy(tmp, bits32, 32);
    tmp[32] = (unsigned char)(negate ? 1 : 0);
    return add_edge(n, from, LIME_NFA_CLASS, target, tmp);
}

/* Free state-array contents (edges + array). */
static void free_states(LimeNfa *n) {
    if (!n->states) return;
    for (int i = 0; i < n->n_states; i++) {
        LimeNfaEdge *e = n->states[i].edges;
        while (e) {
            LimeNfaEdge *next = e->next;
            free(e);
            e = next;
        }
    }
    free(n->states);
    n->states = NULL;
    n->n_states = n->cap = 0;
}

void lime_lex_nfa_free(LimeNfa *nfa) {
    if (!nfa) return;
    free_states(nfa);
    free(nfa);
}

/* ============================================================
** Thompson construction
**
** Each helper writes a fragment with start = entry state and
** end = exit state.  Returns 0 on success, -1 on alloc
** failure (caller's NFA is left in a partial state but
** lime_lex_nfa_free still releases everything).
** ============================================================ */

typedef struct {
    int start;
    int end;
} Frag;

static int build(LimeNfa *n, const LimeReNode *node, Frag *out);

/* Literal -- two states with a byte edge between. */
static int build_literal(LimeNfa *n, unsigned char byte, Frag *out) {
    int s = new_state(n); if (s < 0) return -1;
    int e = new_state(n); if (e < 0) return -1;
    if (add_byte(n, s, e, byte) < 0) return -1;
    out->start = s; out->end = e;
    return 0;
}

/* Char class -- two states with a class edge. */
static int build_char_class(LimeNfa *n, const unsigned char *bits,
                            int negate, Frag *out) {
    int s = new_state(n); if (s < 0) return -1;
    int e = new_state(n); if (e < 0) return -1;
    if (add_class(n, s, e, bits, negate) < 0) return -1;
    out->start = s; out->end = e;
    return 0;
}

/* Wildcard . -- char class equivalent to [^\n]. */
static int build_any(LimeNfa *n, Frag *out) {
    unsigned char bits[32];
    memset(bits, 0, 32);
    /* Set bit for '\n' and negate -- equivalent to all bytes
    ** EXCEPT '\n'. */
    bits['\n' >> 3] |= (unsigned char)(1u << ('\n' & 7));
    return build_char_class(n, bits, /*negate=*/1, out);
}

/* Empty -- single state with epsilon to itself; effectively a
** zero-width match.  Implemented as start == end. */
static int build_empty(LimeNfa *n, Frag *out) {
    int s = new_state(n); if (s < 0) return -1;
    out->start = s; out->end = s;
    return 0;
}

/* Anchor start (^) -- a single state with anchor_start flag.
** The DFA construction will lift the flag into a state property;
** the simulator treats it as zero-width. */
static int build_anchor_start(LimeNfa *n, Frag *out) {
    int s = new_state(n); if (s < 0) return -1;
    int e = new_state(n); if (e < 0) return -1;
    n->states[s].anchor_start = 1;
    if (add_eps(n, s, e) < 0) return -1;
    out->start = s; out->end = e;
    return 0;
}

/* Anchor end (\$) -- analogous to start. */
static int build_anchor_end(LimeNfa *n, Frag *out) {
    int s = new_state(n); if (s < 0) return -1;
    int e = new_state(n); if (e < 0) return -1;
    n->states[e].anchor_end = 1;
    if (add_eps(n, s, e) < 0) return -1;
    out->start = s; out->end = e;
    return 0;
}

/* Concatenation: build left, build right, eps from left.end
** to right.start.  Result frag is {left.start, right.end}. */
static int build_concat(LimeNfa *n, const LimeReNode *node, Frag *out) {
    Frag a, b;
    if (build(n, node->u.binary.left, &a)  < 0) return -1;
    if (build(n, node->u.binary.right, &b) < 0) return -1;
    if (add_eps(n, a.end, b.start) < 0) return -1;
    out->start = a.start; out->end = b.end;
    return 0;
}

/* Alternation: new start, eps to left.start and right.start;
** left.end and right.end eps to new end. */
static int build_alt(LimeNfa *n, const LimeReNode *node, Frag *out) {
    int s = new_state(n); if (s < 0) return -1;
    int e = new_state(n); if (e < 0) return -1;
    Frag a, b;
    if (build(n, node->u.binary.left, &a)  < 0) return -1;
    if (build(n, node->u.binary.right, &b) < 0) return -1;
    if (add_eps(n, s, a.start) < 0) return -1;
    if (add_eps(n, s, b.start) < 0) return -1;
    if (add_eps(n, a.end, e)   < 0) return -1;
    if (add_eps(n, b.end, e)   < 0) return -1;
    out->start = s; out->end = e;
    return 0;
}

/* Star, plus, question. */
static int build_star(LimeNfa *n, const LimeReNode *node, Frag *out) {
    int s = new_state(n); if (s < 0) return -1;
    int e = new_state(n); if (e < 0) return -1;
    Frag a;
    if (build(n, node->u.unary.child, &a) < 0) return -1;
    if (add_eps(n, s, a.start) < 0) return -1;
    if (add_eps(n, s, e)       < 0) return -1;
    if (add_eps(n, a.end, a.start) < 0) return -1;
    if (add_eps(n, a.end, e)       < 0) return -1;
    out->start = s; out->end = e;
    return 0;
}

static int build_plus(LimeNfa *n, const LimeReNode *node, Frag *out) {
    Frag a;
    if (build(n, node->u.unary.child, &a) < 0) return -1;
    int e = new_state(n); if (e < 0) return -1;
    if (add_eps(n, a.end, a.start) < 0) return -1;
    if (add_eps(n, a.end, e)       < 0) return -1;
    out->start = a.start; out->end = e;
    return 0;
}

static int build_question(LimeNfa *n, const LimeReNode *node, Frag *out) {
    int s = new_state(n); if (s < 0) return -1;
    int e = new_state(n); if (e < 0) return -1;
    Frag a;
    if (build(n, node->u.unary.child, &a) < 0) return -1;
    if (add_eps(n, s, a.start) < 0) return -1;
    if (add_eps(n, s, e)       < 0) return -1;
    if (add_eps(n, a.end, e)   < 0) return -1;
    out->start = s; out->end = e;
    return 0;
}

/* Repeat {min,max}: unroll min copies of the child, then if max
** > min, append (max - min) optional copies; if max == -1, append
** a single star instead. */
static int build_repeat(LimeNfa *n, const LimeReNode *node, Frag *out) {
    int min = node->u.repeat.min;
    int max = node->u.repeat.max;
    Frag acc;
    int have_acc = 0;

    /* Handle min copies. */
    if (min == 0) {
        /* Need a placeholder start so subsequent fragments can
        ** chain.  Use an empty fragment. */
        if (build_empty(n, &acc) < 0) return -1;
        have_acc = 1;
    } else {
        for (int i = 0; i < min; i++) {
            Frag a;
            if (build(n, node->u.repeat.child, &a) < 0) return -1;
            if (!have_acc) {
                acc = a;
                have_acc = 1;
            } else {
                if (add_eps(n, acc.end, a.start) < 0) return -1;
                acc.end = a.end;
            }
        }
    }

    /* Handle the (max - min) optional copies, or unbounded star. */
    if (max == -1) {
        /* Append a star fragment built around the child. */
        Frag a;
        if (build(n, node->u.repeat.child, &a) < 0) return -1;
        int s = new_state(n); if (s < 0) return -1;
        int e = new_state(n); if (e < 0) return -1;
        if (add_eps(n, s, a.start) < 0) return -1;
        if (add_eps(n, s, e)       < 0) return -1;
        if (add_eps(n, a.end, a.start) < 0) return -1;
        if (add_eps(n, a.end, e)       < 0) return -1;
        if (add_eps(n, acc.end, s) < 0) return -1;
        acc.end = e;
    } else {
        for (int i = 0; i < max - min; i++) {
            Frag a;
            if (build(n, node->u.repeat.child, &a) < 0) return -1;
            int s = new_state(n); if (s < 0) return -1;
            int e = new_state(n); if (e < 0) return -1;
            if (add_eps(n, s, a.start) < 0) return -1;
            if (add_eps(n, s, e)       < 0) return -1;
            if (add_eps(n, a.end, e)   < 0) return -1;
            if (add_eps(n, acc.end, s) < 0) return -1;
            acc.end = e;
        }
    }

    *out = acc;
    return 0;
}

static int build(LimeNfa *n, const LimeReNode *node, Frag *out) {
    switch (node->kind) {
        case LIME_RE_LITERAL:
            return build_literal(n, node->u.literal, out);
        case LIME_RE_CHAR_CLASS:
            return build_char_class(n, node->u.char_class.bits,
                                    node->u.char_class.negate, out);
        case LIME_RE_ANY:
            return build_any(n, out);
        case LIME_RE_CONCAT:
            return build_concat(n, node, out);
        case LIME_RE_ALT:
            return build_alt(n, node, out);
        case LIME_RE_STAR:
            return build_star(n, node, out);
        case LIME_RE_PLUS:
            return build_plus(n, node, out);
        case LIME_RE_QUESTION:
            return build_question(n, node, out);
        case LIME_RE_REPEAT:
            return build_repeat(n, node, out);
        case LIME_RE_ANCHOR_START:
            return build_anchor_start(n, out);
        case LIME_RE_ANCHOR_END:
            return build_anchor_end(n, out);
        case LIME_RE_EMPTY:
            return build_empty(n, out);
    }
    return -1;
}

LimeNfa *lime_lex_nfa_from_regex(const LimeReNode *root) {
    if (!root) return NULL;
    LimeNfa *n = calloc(1, sizeof(*n));
    if (!n) return NULL;
    Frag f;
    if (build(n, root, &f) < 0) {
        lime_lex_nfa_free(n);
        return NULL;
    }
    n->start = f.start;
    n->accept = f.end;
    n->states[f.end].is_accept = 1;
    return n;
}

/* ============================================================
** NFA simulator (testing utility)
** ============================================================ */

#define LIME_NFA_MAX_STATES 4096
#define LIME_NFA_BMAP_BYTES (LIME_NFA_MAX_STATES / 8)

static void bmap_set(unsigned char *b, int i)   { b[i >> 3] |= (unsigned char)(1u << (i & 7)); }
static int  bmap_has(const unsigned char *b, int i) { return (b[i >> 3] >> (i & 7)) & 1; }

/* Compute epsilon closure of `set`, in place.  Adds all states
** reachable via epsilon-edges from any state in `set`. */
static void eps_closure(const LimeNfa *nfa, unsigned char *set) {
    /* Worklist algorithm.  Use a simple queue (the set itself
    ** indicates membership; we walk it linearly each pass until
    ** no growth). */
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

int lime_lex_nfa_match(const LimeNfa *nfa, const char *bytes, size_t n) {
    if (!nfa) return 0;
    if (nfa->n_states > LIME_NFA_MAX_STATES) return -1;

    unsigned char cur[LIME_NFA_BMAP_BYTES];
    unsigned char nxt[LIME_NFA_BMAP_BYTES];
    memset(cur, 0, sizeof(cur));
    bmap_set(cur, nfa->start);
    eps_closure(nfa, cur);

    for (size_t i = 0; i < n; i++) {
        unsigned char b = (unsigned char)bytes[i];
        memset(nxt, 0, sizeof(nxt));
        for (int s = 0; s < nfa->n_states; s++) {
            if (!bmap_has(cur, s)) continue;
            for (LimeNfaEdge *e = nfa->states[s].edges; e; e = e->next) {
                if (e->kind == LIME_NFA_BYTE && e->u.byte == b) {
                    bmap_set(nxt, e->target);
                } else if (e->kind == LIME_NFA_CLASS && class_matches(e, b)) {
                    bmap_set(nxt, e->target);
                }
            }
        }
        eps_closure(nfa, nxt);
        memcpy(cur, nxt, sizeof(cur));
    }
    return bmap_has(cur, nfa->accept) ? 1 : 0;
}
