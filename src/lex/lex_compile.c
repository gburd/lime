/*
** src/lex/lex_compile.c -- per-state DFA compilation.
**
** Walks the spec's rule lists (top-level + every ruleset),
** assigns each rule a stable compile-order index, then for each
** state (declared + implicit INITIAL) collects the rules that
** apply, builds per-rule NFAs, combines them, subset-constructs,
** and minimizes.
*/

#include "lex_compile.h"
#include "lex_ast.h"
#include "lex_dfa.h"
#include "lex_nfa.h"
#include "lex_regex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
** Rule collection
** ============================================================ */

/* Flatten all rules in the spec (top-level + every ruleset) into
** a single array, preserving declaration order.  The compile-
** order index is the array index. */
typedef struct {
    const LimeLexRule **rules;
    int                 n;
    int                 cap;
} RuleVec;

static int rv_push(RuleVec *v, const LimeLexRule *r) {
    if (v->n == v->cap) {
        int nc = v->cap ? v->cap * 2 : 16;
        const LimeLexRule **nr = realloc(v->rules, nc * sizeof(*nr));
        if (!nr) return -1;
        v->rules = nr;
        v->cap = nc;
    }
    v->rules[v->n++] = r;
    return 0;
}

static int rv_collect(const LimeLexSpec *spec, RuleVec *out) {
    /* Top-level rules first, in declaration order. */
    for (const LimeLexRule *r = spec->rules; r; r = r->next) {
        if (rv_push(out, r) < 0) return -1;
    }
    /* Then ruleset rules, by ruleset declaration order. */
    for (const LimeLexRuleset *rs = spec->rulesets; rs; rs = rs->next) {
        for (const LimeLexRule *r = rs->rules; r; r = r->next) {
            if (rv_push(out, r) < 0) return -1;
        }
    }
    return 0;
}

/* Predicate: does rule `r` fire in state `state_name`?
** state_exclusive flags whether this state is exclusive.  Rules
** with no qualifier fire in INITIAL and inclusive states; rules
** with a qualifier fire only where their state list says.
**
** state_name is "INITIAL" for the implicit start state. */
static int rule_applies(const LimeLexRule *r,
                        const char *state_name,
                        int state_exclusive) {
    int initial = (strcmp(state_name, "INITIAL") == 0);
    if (r->n_states == 0) {
        /* Unqualified rule: fires in INITIAL and inclusive states. */
        if (initial)        return 1;
        if (!state_exclusive) return 1;
        return 0;
    }
    /* Qualified rule: fires only where its state list names. */
    for (int i = 0; i < r->n_states; i++) {
        if (strcmp(r->states[i], state_name) == 0) return 1;
    }
    return 0;
}

/* ============================================================
** Per-state compile
** ============================================================ */

/* Build a single state's DFA.  Returns NULL if the state has no
** applicable rules (caller represents this as an empty
** LimeLexCompiledState rather than calling here). */
static LimeDfa *compile_state_rules(const LimeLexSpec *spec,
                                    const LimeLexRule **rules,
                                    int n_rules,
                                    int *err_count_out) {
    if (n_rules == 0) return NULL;

    /* Build per-rule NFAs.  The accept_rule is the rule's
    ** position in this state's list (NOT the global compile
    ** index) so the caller can map back via rule_indices[]. */
    LimeNfa **per_rule = calloc(n_rules, sizeof(*per_rule));
    if (!per_rule) return NULL;
    int ok = 1;
    for (int i = 0; i < n_rules; i++) {
        const LimeLexRule *r = rules[i];
        if (r->is_eof) {
            /* EOF rules don't have a regex pattern; they are
            ** dispatched at LexFeedEOF time, not from the DFA.
            ** Skip; create a placeholder that matches nothing. */
            per_rule[i] = NULL;
            continue;
        }
        const char *src = r->expanded_pattern ?
                          r->expanded_pattern : r->pattern;
        if (!src) {
            fprintf(stderr,
                "%s:%d: rule '%s' has no pattern source\n",
                spec->filename ? spec->filename : "<input>",
                r->line, r->name);
            (*err_count_out)++;
            ok = 0;
            continue;
        }
        char *err = NULL;
        LimeReNode *re = lime_lex_regex_parse(src, &err);
        if (!re) {
            fprintf(stderr,
                "%s:%d: rule '%s' regex parse failed: %s\n",
                spec->filename ? spec->filename : "<input>",
                r->line, r->name, err ? err : "(no msg)");
            free(err);
            (*err_count_out)++;
            ok = 0;
            continue;
        }
        LimeNfa *nfa = lime_lex_nfa_from_regex(re);
        lime_lex_regex_free(re);
        if (!nfa) {
            fprintf(stderr,
                "%s:%d: rule '%s' NFA construction failed\n",
                spec->filename ? spec->filename : "<input>",
                r->line, r->name);
            (*err_count_out)++;
            ok = 0;
            continue;
        }
        nfa->states[nfa->accept].accept_rule = i;
        per_rule[i] = nfa;
    }

    if (!ok) {
        for (int i = 0; i < n_rules; i++) lime_lex_nfa_free(per_rule[i]);
        free(per_rule);
        return NULL;
    }

    /* Compact out the EOF placeholders. */
    LimeNfa **non_eof = calloc(n_rules, sizeof(*non_eof));
    if (!non_eof) {
        for (int i = 0; i < n_rules; i++) lime_lex_nfa_free(per_rule[i]);
        free(per_rule);
        return NULL;
    }
    int n_non_eof = 0;
    for (int i = 0; i < n_rules; i++) {
        if (per_rule[i]) non_eof[n_non_eof++] = per_rule[i];
    }
    free(per_rule);
    if (n_non_eof == 0) {
        free(non_eof);
        return NULL;
    }

    LimeNfa *combined = lime_lex_nfa_combine(non_eof, n_non_eof);
    free(non_eof);
    if (!combined) {
        (*err_count_out)++;
        return NULL;
    }

    LimeDfa *dfa = lime_lex_nfa_to_dfa(combined);
    lime_lex_nfa_free(combined);
    if (!dfa) {
        (*err_count_out)++;
        return NULL;
    }

    LimeDfa *minimized = lime_lex_dfa_minimize(dfa);
    lime_lex_dfa_free(dfa);
    if (!minimized) {
        (*err_count_out)++;
        return NULL;
    }
    return minimized;
}

/* ============================================================
** Per-spec compile
** ============================================================ */

/* Add a compiled state to the result.  Returns 0 on success,
** -1 on alloc failure. */
static int compiled_push(LimeLexCompiled *c, LimeLexCompiledState s) {
    int newcap = c->n_states + 1;
    LimeLexCompiledState *ns = realloc(c->states,
                                       newcap * sizeof(*ns));
    if (!ns) return -1;
    c->states = ns;
    c->states[c->n_states++] = s;
    return 0;
}

/* For one named state (INITIAL or a %state declaration), gather
** applicable rules and compile.  Appends the result to `out`. */
static int compile_one(const LimeLexSpec *spec,
                       const RuleVec *all_rules,
                       const char *state_name,
                       int exclusive,
                       LimeLexCompiled *out) {
    /* Filter rules. */
    const LimeLexRule **selected = calloc(all_rules->n,
                                          sizeof(*selected));
    int *indices = calloc(all_rules->n, sizeof(int));
    if (!selected || !indices) {
        free(selected); free(indices);
        return -1;
    }
    int n_sel = 0;
    for (int i = 0; i < all_rules->n; i++) {
        if (rule_applies(all_rules->rules[i], state_name, exclusive)) {
            selected[n_sel] = all_rules->rules[i];
            indices[n_sel]  = i;
            n_sel++;
        }
    }

    LimeLexCompiledState s = {0};
    s.state_name   = strdup(state_name);
    s.exclusive    = exclusive;
    s.n_rules      = n_sel;
    s.rule_indices = indices;
    if (n_sel > 0) {
        s.dfa = compile_state_rules(spec, selected, n_sel, &out->error_count);
    }
    free(selected);
    if (!s.state_name) {
        free(indices);
        return -1;
    }
    if (compiled_push(out, s) < 0) {
        free(s.state_name);
        free(indices);
        if (s.dfa) lime_lex_dfa_free(s.dfa);
        return -1;
    }
    return 0;
}

LimeLexCompiled *lime_lex_compile(const LimeLexSpec *spec) {
    if (!spec) return NULL;

    LimeLexCompiled *out = calloc(1, sizeof(*out));
    if (!out) return NULL;

    RuleVec all = {0};
    if (rv_collect(spec, &all) < 0) {
        free(all.rules);
        lime_lex_compiled_free(out);
        return NULL;
    }

    /* Always compile INITIAL first. */
    if (compile_one(spec, &all, "INITIAL", /*exclusive=*/0, out) < 0) {
        free(all.rules);
        lime_lex_compiled_free(out);
        return NULL;
    }

    /* Then every declared state in declaration order. */
    for (const LimeLexState *st = spec->states; st; st = st->next) {
        if (compile_one(spec, &all, st->name, st->exclusive, out) < 0) {
            free(all.rules);
            lime_lex_compiled_free(out);
            return NULL;
        }
    }

    free(all.rules);
    return out;
}

void lime_lex_compiled_free(LimeLexCompiled *c) {
    if (!c) return;
    if (c->states) {
        for (int i = 0; i < c->n_states; i++) {
            free(c->states[i].state_name);
            free(c->states[i].rule_indices);
            if (c->states[i].dfa) lime_lex_dfa_free(c->states[i].dfa);
        }
        free(c->states);
    }
    free(c);
}

const LimeLexCompiledState *lime_lex_compiled_find_state(
    const LimeLexCompiled *c, const char *state_name) {
    if (!c || !state_name) return NULL;
    for (int i = 0; i < c->n_states; i++) {
        if (strcmp(c->states[i].state_name, state_name) == 0) {
            return &c->states[i];
        }
    }
    return NULL;
}
