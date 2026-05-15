/*
** src/lex/lex_ast.c -- AST node lifecycle for the .lex frontend.
**
** Allocation strategy: every node is an independent heap object;
** the spec owns the tree via singly-linked lists.  Freeing the
** spec frees everything reachable.  String fields are heap-
** allocated copies (NULL-terminated); ownership is the node's.
**
** No attempt at arena allocation in M1 -- a .lex source file is
** small (< 10k LOC the largest PG scanner; corresponding AST
** << 1MB) and the lex compiler runs once per build.  Lifetime
** is short.  malloc/free is fine.
*/

#include "lex_ast.h"

#include <stdlib.h>
#include <string.h>

/* ----- helpers ----- */

static void free_string_array(char **arr, int n) {
    if (!arr) return;
    for (int i = 0; i < n; i++) free(arr[i]);
    free(arr);
}

static void free_pattern_list(LimeLexPattern *p) {
    while (p) {
        LimeLexPattern *next = p->next;
        free(p->name);
        free(p->regex);
        free(p);
        p = next;
    }
}

static void free_state_list(LimeLexState *s) {
    while (s) {
        LimeLexState *next = s->next;
        free(s->name);
        free(s->local_body);
        free(s->destructor);
        free(s);
        s = next;
    }
}

static void free_keyword_table_list(LimeLexKeywordTable *k) {
    while (k) {
        LimeLexKeywordTable *next = k->next;
        free(k->name);
        free(k->prefix);
        free_string_array(k->keywords, k->n_keywords);
        free(k);
        k = next;
    }
}

static void free_literal_buffer_list(LimeLexLiteralBuffer *b) {
    while (b) {
        LimeLexLiteralBuffer *next = b->next;
        free(b->name);
        free(b->element_type);
        free(b->grow_policy);
        free(b->alloc_fn);
        free(b->realloc_fn);
        free(b->free_fn);
        free(b);
        b = next;
    }
}

static void free_rule_list(LimeLexRule *r) {
    while (r) {
        LimeLexRule *next = r->next;
        free(r->name);
        free_string_array(r->states, r->n_states);
        free(r->pattern);
        free(r->action);
        free(r);
        r = next;
    }
}

static void free_ruleset_list(LimeLexRuleset *r) {
    while (r) {
        LimeLexRuleset *next = r->next;
        free(r->name);
        free_rule_list(r->rules);
        free(r);
        r = next;
    }
}

/* ----- public API ----- */

LimeLexSpec *lime_lex_spec_new(const char *filename) {
    LimeLexSpec *spec = calloc(1, sizeof(*spec));
    if (!spec) return NULL;
    spec->filename = filename;   /* borrowed, not owned */
    spec->line_count = 0;
    spec->error_count = 0;
    return spec;
}

void lime_lex_spec_free(LimeLexSpec *spec) {
    if (!spec) return;
    free(spec->name_prefix);
    free(spec->token_prefix);
    free(spec->token_type);
    free(spec->location_type);
    free(spec->extra_argument);
    free(spec->include_block);
    free_pattern_list(spec->patterns);
    free_state_list(spec->states);
    free_keyword_table_list(spec->keyword_tables);
    free_literal_buffer_list(spec->literal_buffers);
    free_ruleset_list(spec->rulesets);
    free_rule_list(spec->rules);
    free_string_array(spec->lexer_includes, spec->n_lexer_includes);
    free(spec);
}
