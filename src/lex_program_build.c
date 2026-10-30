/*
** src/lex_program_build.c -- compile .lex text into a LimeLexProgram,
** in process (include/lime_lexer.h).  liblime_compiler: compose time.
**
** Same pipeline `lime -X` runs (lex_parse -> lex_resolve -> lex_compile:
** per-state NFA -> DFA -> minimise), so the tables equal the generated
** scanner's tables; then the DFAs are copied into the immutable layout
** the interpreter (src/lex_program.c, liblime_parser) reads.
*/
#include "lex_ast.h"
#include "lex_compile.h"
#include "lex_parse.h"
#include "lex_resolve.h"
#include "lexicon.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *err_dup(const char *what, int n) {
    char buf[160];
    snprintf(buf, sizeof buf, "lime_lex_program_compile: %d %s error(s) (details on stderr)", n,
             what);
    return strdup(buf);
}

/* Rule names + <<EOF>> flags in global compile order (the order
** lime_lex_compile numbers rules in).  Names are sanitised the way
** lex_emit.c's <P>RuleNames[] are, so they match the generated scanner. */
typedef struct {
    char **names;
    unsigned char *is_eof;
    int n, cap, oom;
} RuleVec;

static int rule_visit(const LimeLexRule *r, void *user) {
    RuleVec *v = user;
    if (v->n == v->cap) {
        int nc = v->cap ? v->cap * 2 : 64;
        char **nn = realloc(v->names, (size_t)nc * sizeof *nn);
        if (nn) v->names = nn;
        unsigned char *ne = realloc(v->is_eof, (size_t)nc);
        if (ne) v->is_eof = ne;
        if (!nn || !ne) return v->oom = 1, -1;
        v->cap = nc;
    }
    char *s = strdup(r->name ? r->name : "anon");
    if (!s) return v->oom = 1, -1;
    for (char *c = s; *c; c++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') ||
              *c == '_'))
            *c = '_';
    v->names[v->n] = s;
    v->is_eof[v->n] = (unsigned char)(r->is_eof != 0);
    v->n++;
    return 0;
}

/* Copy one compiled state's DFA (accept = global rule index). */
static int copy_state(const LimeLexCompiledState *cs, LimeLexProgState *out) {
    const LimeDfa *d = cs->dfa;
    out->start = 0;
    out->nstates = 0;
    if (!d || d->n_states == 0) return 0;
    if (d->n_states > INT16_MAX) return -1;
    out->trans = malloc((size_t)d->n_states * sizeof *out->trans);
    out->accept = malloc((size_t)d->n_states * sizeof *out->accept);
    if (!out->trans || !out->accept) return -1;
    for (int q = 0; q < d->n_states; q++) {
        for (int b = 0; b < 256; b++) out->trans[q][b] = (int16_t)d->states[q].trans[b];
        int local = d->states[q].is_accept ? d->states[q].accept_rule : -1;
        out->accept[q] =
            (int16_t)((local >= 0 && local < cs->n_rules) ? cs->rule_indices[local] : -1);
    }
    out->start = d->start;
    out->nstates = d->n_states;
    return 0;
}

LimeLexProgram *lime_lex_program_compile(const char *src, size_t len, char **error) {
    if (error) *error = NULL;
    if (src == NULL) {
        if (error) *error = strdup("lime_lex_program_compile: NULL source");
        return NULL;
    }
    LimeLexSpec *spec = lime_lex_parse("<runtime>", src, len);
    if (!spec) {
        if (error) *error = strdup("lime_lex_program_compile: out of memory");
        return NULL;
    }
    if (spec->error_count > 0) {
        if (error) *error = err_dup("parse", spec->error_count);
        lime_lex_spec_free(spec);
        return NULL;
    }
    if (lime_lex_resolve_patterns(spec) != 0 || spec->error_count > 0) {
        if (error) *error = err_dup("pattern", spec->error_count ? spec->error_count : 1);
        lime_lex_spec_free(spec);
        return NULL;
    }
    LimeLexCompiled *c = lime_lex_compile(spec);
    if (!c || c->error_count > 0) {
        if (error) *error = c ? err_dup("regex", c->error_count) : strdup("lime_lex_program_compile: out of memory");
        lime_lex_compiled_free(c);
        lime_lex_spec_free(spec);
        return NULL;
    }

    LimeLexProgram *p = calloc(1, sizeof *p);
    RuleVec rv = { 0 };
    int ok = p != NULL && lime_lex_walk_rules(spec, rule_visit, &rv) == 0 && !rv.oom;
    int nrules = rv.n;
    if (p) {
        /* owned by p from here on (freed by lime_lex_program_destroy) */
        p->nrules = nrules;
        p->rule_names = rv.names;
        p->rule_is_eof = rv.is_eof;
    } else {
        for (int i = 0; i < rv.n; i++) free(rv.names[i]);
        free(rv.names);
        free(rv.is_eof);
    }
    if (ok) {
        p->nstates = c->n_states;
        p->st = calloc((size_t)(c->n_states > 0 ? c->n_states : 1), sizeof *p->st);
        p->state_names = calloc((size_t)(c->n_states > 0 ? c->n_states : 1), sizeof(char *));
        p->eof_rule = malloc((size_t)(c->n_states > 0 ? c->n_states : 1) * sizeof(int32_t));
        ok = p->st && p->state_names && p->eof_rule;
        if (!ok) p->nstates = 0; /* destroy must not walk unallocated arrays */
    }
    for (int i = 0; ok && i < c->n_states; i++) {
        const LimeLexCompiledState *cs = &c->states[i];
        p->state_names[i] = strdup(cs->state_name);
        ok = p->state_names[i] != NULL && copy_state(cs, &p->st[i]) == 0;
        /* the first <<EOF>> rule in this state's rule list, as lex_emit.c does */
        p->eof_rule[i] = -1;
        for (int k = 0; ok && k < cs->n_rules; k++) {
            int g = cs->rule_indices[k];
            if (g >= 0 && g < nrules && p->rule_is_eof[g]) {
                p->eof_rule[i] = g;
                break;
            }
        }
    }
    lime_lex_compiled_free(c);
    lime_lex_spec_free(spec);
    if (!ok) {
        lime_lex_program_destroy(p);
        if (error) *error = strdup("lime_lex_program_compile: out of memory");
        return NULL;
    }
    return p;
}
