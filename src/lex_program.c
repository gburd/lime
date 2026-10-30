/*
** src/lex_program.c -- the runtime DFA interpreter for LimeLexProgram
** (include/lime_lexer.h).  liblime_parser: per-token code, no
** allocation, no lock.  The builder is src/lex_program_build.c
** (liblime_compiler).
**
** The inner loop is the generated scanner's scalar <P>_match() with
** the tables read through a pointer instead of being static arrays.
*/
#include "lexicon.h"

#include <string.h>

int lime_lex_program_match(const LimeLexProgram *p, int state, const char *s, size_t n,
                           int *rule, size_t *len) {
    if (p == NULL || state < 0 || state >= p->nstates) return 0;
    const LimeLexProgState *st = &p->st[state];
    if (st->nstates == 0) return 0;
    const int16_t(*trans)[256] = (const int16_t(*)[256])st->trans;
    const int16_t *accept = st->accept;
    const unsigned char *b = (const unsigned char *)s;
    int q = st->start;
    int best = accept[q];
    size_t best_len = 0;
    for (size_t i = 0; i < n; i++) {
        int next = trans[q][b[i]];
        if (next < 0) break;
        q = next;
        if (accept[q] >= 0) {
            best = accept[q];
            best_len = i + 1;
        }
    }
    if (best < 0) return 0;
    *rule = best;
    *len = best_len;
    return 1;
}

int lime_lex_program_rule_count(const LimeLexProgram *p) {
    return p ? p->nrules : -1;
}

int lime_lex_program_state_count(const LimeLexProgram *p) {
    return p ? p->nstates : -1;
}

const char *lime_lex_program_rule_name(const LimeLexProgram *p, int rule) {
    return (p && rule >= 0 && rule < p->nrules) ? p->rule_names[rule] : NULL;
}

int lime_lex_program_rule_by_name(const LimeLexProgram *p, const char *name) {
    for (int i = 0; p && name && i < p->nrules; i++)
        if (strcmp(p->rule_names[i], name) == 0) return i;
    return -1;
}

int lime_lex_program_state_by_name(const LimeLexProgram *p, const char *name) {
    for (int i = 0; p && name && i < p->nstates; i++)
        if (strcmp(p->state_names[i], name) == 0) return i;
    return -1;
}

int lime_lex_program_rule_is_eof(const LimeLexProgram *p, int rule) {
    return (p && rule >= 0 && rule < p->nrules) ? p->rule_is_eof[rule] : -1;
}

int lime_lex_program_eof_rule(const LimeLexProgram *p, int state) {
    return (p && state >= 0 && state < p->nstates) ? p->eof_rule[state] : -1;
}

void lime_lex_program_free(LimeLexProgram *p) {
    lime_lex_program_destroy(p);
}
