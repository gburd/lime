/*
** src/lex/lex_dfa.h -- DFA construction (subset construction)
** for the Lime .lex compiler (M2.3).
**
** Converts an NFA (LimeNfa from lex_nfa.h) into a deterministic
** finite automaton suitable for the M3 runtime template's hot
** path.  Each DFA state corresponds to a set of NFA states
** (powerset construction); each DFA transition is a byte-keyed
** lookup yielding the next state id (or -1 for "no transition").
**
** The current implementation uses a flat 256-entry transition
** table per state (1 KB / state with 4-byte ints).  Practical
** for the audit-corpus scanners (each DFA stays well under 1k
** states).  M2.4 will minimize via Hopcroft's algorithm;
** further compression for very large grammars (e.g., scan.l
** with ~600 states) is a future optimization.
**
** Anchors (^/\$) are still NFA-level concepts; M2.5 will lift
** them into DFA-level state attributes when accept-action
** mapping is added.
*/
#ifndef LIME_LEX_DFA_H
#define LIME_LEX_DFA_H

#include "lex_nfa.h"

#include <stddef.h>

typedef struct {
    int id;
    int trans[256]; /* trans[byte] = next state id, or -1 */
    int is_accept;
    int accept_rule; /* populated by M2.5; zero before that */
} LimeDfaState;

typedef struct {
    LimeDfaState *states;
    int n_states;
    int cap;
    int start;
} LimeDfa;

/* Build a DFA from the supplied NFA.  Returns NULL on alloc
** failure or if the NFA exceeds 4096 states (the bitmap-based
** subset construction's hard cap).  The returned DFA owns its
** state array; release with lime_lex_dfa_free. */
LimeDfa *lime_lex_nfa_to_dfa(const LimeNfa *nfa);

void lime_lex_dfa_free(LimeDfa *dfa);

/* DFA simulator.  Returns 1 if the entire input is accepted, 0
** otherwise.  Mostly a testing utility; the M3 runtime will
** drive the table directly. */
int lime_lex_dfa_match(const LimeDfa *dfa, const char *bytes, size_t n);

/* v0.9 per-token DFA: compute the set of input bytes that have a
** non-error transition from the DFA's start state.  byte_set[256]
** is overwritten with 1 (byte b can start a match) or 0 (cannot).
** Used by per-token leading-byte dispatch tables. */
void lime_lex_dfa_leading_bytes(const LimeDfa *dfa, unsigned char byte_set[256]);

/* Returns the unique byte that triggers a single-byte match, or -1
** if the DFA is not a single-byte acceptor.  A single-byte acceptor
** has exactly one byte b with trans[start][b] -> accept-state.
** Used to emit direct-dispatch arms for trivial structural tokens
** like '{', '}', '[', ']', ',', ':' which need no DFA walk. */
int lime_lex_dfa_single_byte(const LimeDfa *dfa);

/* If the DFA accepts exactly one fixed-length string, populates
** out_str (caller-provided buffer of >= 256 bytes) and *out_len
** with the string and its length, then returns 1.  Returns 0 if
** the DFA is not a fixed-string acceptor (has alternatives, repeats,
** or a self-loop).  Used to emit direct byte-comparison arms for
** keyword tokens like "true", "false", "null". */
int lime_lex_dfa_fixed_string(const LimeDfa *dfa, unsigned char out_str[256], int *out_len);

/* Number of DFA states.  Useful for testing and audit checks. */
int lime_lex_dfa_state_count(const LimeDfa *dfa);

/* Minimize a DFA using Moore's algorithm.  Returns a freshly
** allocated DFA accepting the same language with the minimum
** number of states.  The input is not modified.  Returns NULL
** on alloc failure. */
LimeDfa *lime_lex_dfa_minimize(const LimeDfa *src);

#endif /* LIME_LEX_DFA_H */
