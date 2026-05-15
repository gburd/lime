/*
** src/lex/lex_nfa.h -- NFA construction (Thompson's algorithm)
** for the Lime .lex DFA compiler (M2.2).
**
** Walks a regex AST (LimeReNode from lex_regex.h) and produces
** an NFA suitable for input to the M2.3 subset-construction step.
**
** Each AST node yields a small NFA fragment with a single start
** and a single accept state.  Combinators (concat, alt, *, +, ?,
** {n,m}) glue fragments together via epsilon-edges, growing the
** state array as needed.
**
** A bounded NFA simulator is included for testing -- runs the
** NFA against an input byte string and returns whether the
** input is in the language.  Useful in tests but NOT the
** intended runtime path; the M3 runtime uses the M2.3+ DFA.
**
** Anchors (^ and $) are recorded in the NFA but are no-ops in
** the simulator (which assumes the input is the full token).
** The DFA construction phase decides what to do with them.
*/
#ifndef LIME_LEX_NFA_H
#define LIME_LEX_NFA_H

#include "lex_regex.h"

#include <stddef.h>

typedef enum {
    LIME_NFA_EPS = 1,         /* no input consumed */
    LIME_NFA_BYTE,            /* single byte */
    LIME_NFA_CLASS            /* character class (256-bit bitmap) */
} LimeNfaEdgeKind;

typedef struct LimeNfaEdge LimeNfaEdge;
struct LimeNfaEdge {
    LimeNfaEdgeKind kind;
    int             target;     /* target state id */
    union {
        unsigned char byte;
        struct {
            unsigned char bits[32];
            int           negate;
        } char_class;
    } u;
    LimeNfaEdge    *next;       /* singly-linked list of outgoing edges */
};

typedef struct {
    int           id;
    LimeNfaEdge  *edges;
    int           is_accept;    /* set on the unique accept state */
    int           anchor_start; /* set on states reachable only after ^ */
    int           anchor_end;   /* set if this state requires end-of-input */
} LimeNfaState;

typedef struct {
    LimeNfaState *states;       /* array of states, indexed by id */
    int           n_states;
    int           cap;
    int           start;        /* start state id */
    int           accept;       /* accept state id */
} LimeNfa;

/* Build an NFA from the regex AST.  Returns NULL on alloc
** failure.  The returned NFA owns all its states and edges;
** release with lime_lex_nfa_free. */
LimeNfa *lime_lex_nfa_from_regex(const LimeReNode *root);

void lime_lex_nfa_free(LimeNfa *nfa);

/* Bounded NFA simulator.  Returns 1 if the entire input
** (bytes[0..n-1]) is in the language defined by the NFA, 0
** otherwise.  Anchors are treated as zero-width matches that
** consult the position rather than consuming bytes.
**
** Soft cap: a working state-set bitmap of YYNFA_MAX_STATES /
** 8 bytes; if the NFA has more than 4096 states we abort and
** return -1 (used in tests; production parsers compile the
** NFA to a DFA first via M2.3 and don't simulate).
*/
int lime_lex_nfa_match(const LimeNfa *nfa,
                       const char *bytes, size_t n);

#endif /* LIME_LEX_NFA_H */
