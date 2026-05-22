/*
** src/lex/lex_dfa_min.c -- DFA minimization (Moore's algorithm).
**
** Iteratively refines a partition over DFA states.  Two states
** are in the same group iff they're both accepting (or both
** non-accepting) AND for every input byte their transitions go
** to states in the same group.  When no further splits happen,
** the partition is the equivalence relation; the minimized DFA
** has one state per group.
**
** Hopcroft's algorithm is asymptotically faster but Moore's is
** simpler to implement and audit, and the audit-corpus DFA
** sizes (50-200 states post-subset-construction) don't need
** Hopcroft's complexity advantage.  If scan.l-shape grammars
** push the DFA into the thousands of states, swap to Hopcroft.
*/

#include "lex_dfa.h"

#include <stdlib.h>
#include <string.h>

/* Per-state signature: one int per byte (256) plus the current
** group id.  Two states with the same signature are equivalent
** at this iteration. */
typedef struct {
    int group;
    int trans_groups[256];
} Sig;

/* Compare two signatures.  Returns 0 if equal. */
static int sig_cmp(const Sig *a, const Sig *b) {
    if (a->group != b->group) return a->group - b->group;
    return memcmp(a->trans_groups, b->trans_groups, sizeof(a->trans_groups));
}

LimeDfa *lime_lex_dfa_minimize(const LimeDfa *src) {
    if (!src || src->n_states <= 0) return NULL;

    int n = src->n_states;
    /* Hint to gcc that n is positive so it doesn't analyse the
    ** allocation sizes below as potentially-huge ranges via
    ** signed-int wraparound. */
    if (n <= 0) return NULL;
    size_t bytes_n = (size_t)n * sizeof(int);
    int *group = malloc(bytes_n);
    int *next_group = malloc(bytes_n);
    Sig *sigs = malloc((size_t)n * sizeof(Sig));
    if (!group || !next_group || !sigs) {
        free(group);
        free(next_group);
        free(sigs);
        return NULL;
    }

    /* Initial partition.  Non-accepting states in group 0;
    ** accepting states partitioned by accept_rule (each distinct
    ** rule id gets its own group).  This prevents minimization
    ** from merging two accept states that report different
    ** rules -- crucial for multi-rule DFAs from the M2.5
    ** combiner where each rule's accept must remain
    ** distinguishable. */
    int max_rule = -1;
    for (int i = 0; i < n; i++) {
        if (src->states[i].is_accept && src->states[i].accept_rule > max_rule) {
            max_rule = src->states[i].accept_rule;
        }
    }
    for (int i = 0; i < n; i++) {
        if (!src->states[i].is_accept) {
            group[i] = 0;
        } else {
            group[i] = 1 + src->states[i].accept_rule;
        }
    }
    int max_groups = (max_rule >= 0) ? max_rule + 2 : 1;

    /* Iterate. */
    int changed = 1;
    while (changed) {
        changed = 0;
        /* Compute each state's signature using the current
        ** group assignment. */
        for (int i = 0; i < n; i++) {
            sigs[i].group = group[i];
            for (int b = 0; b < 256; b++) {
                int t = src->states[i].trans[b];
                sigs[i].trans_groups[b] = (t < 0) ? -1 : group[t];
            }
        }
        /* Within each existing group, split states by signature.
        ** O(n^2) per iteration; fine for our sizes. */
        memset(next_group, -1, bytes_n);
        int new_max = 0;
        for (int i = 0; i < n; i++) {
            if (next_group[i] >= 0) continue;
            int g = new_max++;
            next_group[i] = g;
            for (int j = i + 1; j < n; j++) {
                if (next_group[j] >= 0) continue;
                if (sig_cmp(&sigs[i], &sigs[j]) == 0) {
                    next_group[j] = g;
                }
            }
        }
        if (new_max != max_groups) changed = 1;
        for (int i = 0; i < n && !changed; i++) {
            if (group[i] != next_group[i]) changed = 1;
        }
        max_groups = new_max;
        memcpy(group, next_group, bytes_n);
    }

    /* Build the minimized DFA.  One state per group; transitions
    ** are inherited from any representative state in that group
    ** (they all transition to the same target group by
    ** definition). */
    LimeDfa *out = calloc(1, sizeof(*out));
    if (!out) {
        free(group);
        free(next_group);
        free(sigs);
        return NULL;
    }
    out->states = calloc(max_groups, sizeof(*out->states));
    if (!out->states) {
        free(out);
        free(group);
        free(next_group);
        free(sigs);
        return NULL;
    }
    out->cap = out->n_states = max_groups;
    out->start = group[src->start];

    /* Pick a representative for each group: first state seen. */
    int *rep = malloc(max_groups * sizeof(int));
    if (!rep) {
        lime_lex_dfa_free(out);
        free(group);
        free(next_group);
        free(sigs);
        return NULL;
    }
    for (int g = 0; g < max_groups; g++)
        rep[g] = -1;
    for (int i = 0; i < n; i++) {
        if (rep[group[i]] < 0) rep[group[i]] = i;
    }
    for (int g = 0; g < max_groups; g++) {
        out->states[g].id = g;
        for (int b = 0; b < 256; b++) {
            int t = src->states[rep[g]].trans[b];
            out->states[g].trans[b] = (t < 0) ? -1 : group[t];
        }
        out->states[g].is_accept = src->states[rep[g]].is_accept;
        out->states[g].accept_rule = src->states[rep[g]].accept_rule;
    }

    free(rep);
    free(group);
    free(next_group);
    free(sigs);
    return out;
}
