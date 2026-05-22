/*
** Enhanced conflict detection for multi-grammar scenarios.
**
** When multiple grammar extensions are loaded simultaneously, ambiguity
** can arise at three levels:
**
**   1. Token-level:    Same lexeme maps to different tokens across grammars.
**   2. Rule-level:     Same token sequence parseable by multiple grammars.
**   3. Semantic-level: Same syntax has different semantic actions.
**
** This module extends the basic conflict detection in conflict.c with
** multi-grammar awareness, querying the extension registry to identify
** which grammars can handle a given token in a given parser state.
*/
#include "conflict.h"
#include "extension.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static char *dup_str(const char *s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy != NULL) memcpy(copy, s, len);
    return copy;
}

#define INITIAL_CTX_CAP 4
#define INITIAL_POINT_CAP 8

/* ------------------------------------------------------------------ */
/*  ConflictPoint lifecycle                                            */
/* ------------------------------------------------------------------ */

void conflict_point_init(ConflictPoint *cp, uint16_t token, int state, ConflictLevel level) {
    if (cp == NULL) return;
    memset(cp, 0, sizeof(*cp));
    cp->token = token;
    cp->state = state;
    cp->level = level;
    cp->contexts = NULL;
    cp->ncontexts = 0;
    cp->capacity = 0;
    cp->description = NULL;
}

void conflict_point_destroy(ConflictPoint *cp) {
    if (cp == NULL) return;
    free(cp->contexts);
    cp->contexts = NULL;
    cp->ncontexts = 0;
    cp->capacity = 0;
    free(cp->description);
    cp->description = NULL;
}

bool conflict_point_add_context(ConflictPoint *cp, const LimeContext *ctx) {
    if (cp == NULL || ctx == NULL) return false;

    /* Grow if needed */
    if (cp->ncontexts >= cp->capacity) {
        int new_cap = cp->capacity == 0 ? INITIAL_CTX_CAP : cp->capacity * 2;
        LimeContext *p = realloc(cp->contexts, (size_t)new_cap * sizeof(LimeContext));
        if (p == NULL) return false;
        cp->contexts = p;
        cp->capacity = new_cap;
    }

    cp->contexts[cp->ncontexts] = *ctx;
    cp->ncontexts++;
    return true;
}

/* ------------------------------------------------------------------ */
/*  MultiGrammarConflictResult lifecycle                               */
/* ------------------------------------------------------------------ */

MultiGrammarConflictResult *multi_conflict_result_create(void) {
    MultiGrammarConflictResult *r = calloc(1, sizeof(*r));
    if (r == NULL) return NULL;

    r->points = calloc(INITIAL_POINT_CAP, sizeof(ConflictPoint));
    if (r->points == NULL) {
        free(r);
        return NULL;
    }
    r->capacity = INITIAL_POINT_CAP;
    r->npoints = 0;
    r->token_conflicts = 0;
    r->rule_conflicts = 0;
    r->semantic_conflicts = 0;
    return r;
}

void multi_conflict_result_destroy(MultiGrammarConflictResult *result) {
    if (result == NULL) return;
    for (uint32_t i = 0; i < result->npoints; i++) {
        conflict_point_destroy(&result->points[i]);
    }
    free(result->points);
    free(result);
}

/*
** Add a conflict point to the result set.  Returns a pointer to the
** newly added (and initialised) ConflictPoint, or NULL on failure.
*/
static ConflictPoint *result_add_point(MultiGrammarConflictResult *result, uint16_t token,
                                       int state, ConflictLevel level) {
    if (result == NULL) return NULL;

    if (result->npoints >= result->capacity) {
        uint32_t new_cap = result->capacity * 2;
        ConflictPoint *p = realloc(result->points, new_cap * sizeof(ConflictPoint));
        if (p == NULL) return NULL;
        result->points = p;
        result->capacity = new_cap;
    }

    ConflictPoint *cp = &result->points[result->npoints];
    conflict_point_init(cp, token, state, level);
    result->npoints++;
    return cp;
}

/* ------------------------------------------------------------------ */
/*  Extension scanning helpers                                         */
/* ------------------------------------------------------------------ */

/*
** Collect all token names added by loaded extensions.  For each unique
** token name, track which extensions define it.
**
** Uses a simple linear scan -- extension counts are small enough that
** O(n*m) where n=extensions and m=tokens-per-extension is fine.
*/
typedef struct TokenEntry {
    const char *name;          /* Points into modification data (not owned) */
    ExtensionID ext_ids[16];   /* Extensions that define this token */
    const char *ext_names[16]; /* Grammar names for each (not owned) */
    int next;                  /* Number of entries in ext_ids[] */
} TokenEntry;

typedef struct TokenCollector {
    TokenEntry *entries;
    uint32_t count;
    uint32_t capacity;
} TokenCollector;

static bool collector_init(TokenCollector *tc, uint32_t cap) {
    tc->entries = calloc(cap, sizeof(TokenEntry));
    tc->count = 0;
    tc->capacity = cap;
    return tc->entries != NULL;
}

static void collector_destroy(TokenCollector *tc) {
    free(tc->entries);
    tc->entries = NULL;
    tc->count = 0;
}

/*
** Find or create an entry for the given token name.
** Returns the index, or UINT32_MAX on allocation failure.
*/
static uint32_t collector_find_or_add(TokenCollector *tc, const char *name) {
    /* Linear search for existing entry */
    for (uint32_t i = 0; i < tc->count; i++) {
        if (strcmp(tc->entries[i].name, name) == 0) {
            return i;
        }
    }

    /* Need a new entry */
    if (tc->count >= tc->capacity) {
        uint32_t new_cap = tc->capacity * 2;
        TokenEntry *p = realloc(tc->entries, new_cap * sizeof(TokenEntry));
        if (p == NULL) return UINT32_MAX;
        memset(&p[tc->capacity], 0, (new_cap - tc->capacity) * sizeof(TokenEntry));
        tc->entries = p;
        tc->capacity = new_cap;
    }

    uint32_t idx = tc->count;
    memset(&tc->entries[idx], 0, sizeof(TokenEntry));
    tc->entries[idx].name = name;
    tc->count++;
    return idx;
}

/*
** Build a description string for a conflict point.
*/
static char *build_description(ConflictLevel level, uint16_t token, int state, int ncontexts) {
    const char *level_str;
    switch (level) {
    case CONFLICT_LEVEL_TOKEN:
        level_str = "token";
        break;
    case CONFLICT_LEVEL_RULE:
        level_str = "rule";
        break;
    case CONFLICT_LEVEL_SEMANTIC:
        level_str = "semantic";
        break;
    default:
        level_str = "unknown";
        break;
    }

    char buf[256];
    if (state >= 0) {
        snprintf(buf, sizeof(buf), "%s-level conflict: token %u in state %d has %d interpretations",
                 level_str, (unsigned)token, state, ncontexts);
    } else {
        snprintf(buf, sizeof(buf), "%s-level conflict: token %u has %d interpretations", level_str,
                 (unsigned)token, ncontexts);
    }
    return dup_str(buf);
}

/* ------------------------------------------------------------------ */
/*  Token-level conflict detection                                     */
/* ------------------------------------------------------------------ */

uint32_t detect_token_conflicts(ExtensionRegistry *reg, MultiGrammarConflictResult *result) {
    if (reg == NULL || result == NULL) return 0;

    uint32_t found = 0;
    TokenCollector tc;
    if (!collector_init(&tc, 64)) return 0;

    /*
    ** Walk every loaded extension and collect all MOD_ADD_TOKEN
    ** modifications, grouping by token name.
    */
    pthread_rwlock_rdlock(&reg->lock);

    for (uint32_t i = 0; i < reg->count; i++) {
        const Extension *ext = &reg->extensions[i];
        if (ext->state != EXT_LOADED) continue;

        for (uint32_t m = 0; m < ext->nmodifications; m++) {
            const GrammarModification *mod = &ext->modifications[m];
            if (mod->type != MOD_ADD_TOKEN) continue;
            if (mod->u.add_token.name == NULL) continue;

            uint32_t idx = collector_find_or_add(&tc, mod->u.add_token.name);
            if (idx == UINT32_MAX) continue;

            TokenEntry *te = &tc.entries[idx];
            if (te->next < 16) {
                te->ext_ids[te->next] = ext->id;
                te->ext_names[te->next] = ext->name;
                te->next++;
            }
        }
    }

    pthread_rwlock_unlock(&reg->lock);

    /*
    ** Any token name claimed by more than one extension is a conflict.
    */
    for (uint32_t i = 0; i < tc.count; i++) {
        TokenEntry *te = &tc.entries[i];
        if (te->next <= 1) continue;

        ConflictPoint *cp = result_add_point(result, 0, -1, CONFLICT_LEVEL_TOKEN);
        if (cp == NULL) break;

        for (int k = 0; k < te->next; k++) {
            LimeContext ctx = {
                .ext_id = te->ext_ids[k],
                .token = 0, /* token code unknown at this level */
                .state = -1,
                .priority = 0,
                .grammar_name = te->ext_names[k],
            };
            conflict_point_add_context(cp, &ctx);
        }

        cp->description = build_description(CONFLICT_LEVEL_TOKEN, 0, -1, te->next);

        found++;
        result->token_conflicts++;
    }

    collector_destroy(&tc);
    return found;
}

/* ------------------------------------------------------------------ */
/*  Rule-level conflict detection                                      */
/* ------------------------------------------------------------------ */

uint32_t detect_rule_conflicts(ExtensionRegistry *reg, uint16_t token, int state,
                               MultiGrammarConflictResult *result) {
    if (reg == NULL || result == NULL) return 0;

    /*
    ** For rule-level detection, we check which loaded extensions have
    ** rules that could handle the given token.  A rule "handles" a
    ** token if:
    **   - The extension adds that token (MOD_ADD_TOKEN with a matching
    **     name or code), AND
    **   - The extension adds rules whose RHS references that token.
    **
    ** Since we don't have full parser-state-to-rule mapping at this
    ** level (that requires the LALR(1) automaton), we approximate by
    ** checking which extensions contribute rules that reference the
    ** token.  The state parameter is recorded for downstream use by
    ** disambiguation strategies.
    */

    uint32_t found = 0;
    int ncontexts = 0;
    ConflictPoint *cp = NULL;

    pthread_rwlock_rdlock(&reg->lock);

    for (uint32_t i = 0; i < reg->count; i++) {
        const Extension *ext = &reg->extensions[i];
        if (ext->state != EXT_LOADED) continue;

        /*
        ** Check if this extension defines rules that reference the token.
        ** We look for MOD_ADD_RULE modifications where any RHS symbol
        ** matches a token name that this extension also adds.
        */
        bool has_relevant_rule = false;
        const char *token_name = NULL;

        /* First, find the token name for the given token code */
        for (uint32_t m = 0; m < ext->nmodifications; m++) {
            const GrammarModification *mod = &ext->modifications[m];
            if (mod->type == MOD_ADD_TOKEN && mod->u.add_token.token_code == (int)token) {
                token_name = mod->u.add_token.name;
                break;
            }
        }

        if (token_name == NULL) continue;

        /* Now check for rules referencing this token */
        for (uint32_t m = 0; m < ext->nmodifications; m++) {
            const GrammarModification *mod = &ext->modifications[m];
            if (mod->type != MOD_ADD_RULE) continue;

            for (int r = 0; r < mod->u.add_rule.nrhs; r++) {
                if (mod->u.add_rule.rhs[r] != NULL &&
                    strcmp(mod->u.add_rule.rhs[r], token_name) == 0) {
                    has_relevant_rule = true;
                    break;
                }
            }
            if (has_relevant_rule) break;
        }

        if (!has_relevant_rule) continue;

        /* This extension can handle the token -- record as a context */
        if (cp == NULL) {
            cp = result_add_point(result, token, state, CONFLICT_LEVEL_RULE);
            if (cp == NULL) break;
        }

        LimeContext ctx = {
            .ext_id = ext->id,
            .token = token,
            .state = state,
            .priority = 0,
            .grammar_name = ext->name,
        };
        conflict_point_add_context(cp, &ctx);
        ncontexts++;
    }

    pthread_rwlock_unlock(&reg->lock);

    /* Only count as a conflict if more than one grammar handles it */
    if (cp != NULL && ncontexts > 1) {
        cp->description = build_description(CONFLICT_LEVEL_RULE, token, state, ncontexts);
        found = 1;
        result->rule_conflicts++;
    } else if (cp != NULL && ncontexts <= 1) {
        /* Not a conflict -- remove the point we added */
        conflict_point_destroy(cp);
        result->npoints--;
    }

    return found;
}

/* ------------------------------------------------------------------ */
/*  Semantic-level conflict detection                                  */
/* ------------------------------------------------------------------ */

uint32_t detect_semantic_conflicts(ExtensionRegistry *reg, uint16_t token, int state,
                                   MultiGrammarConflictResult *result) {
    if (reg == NULL || result == NULL) return 0;

    /*
    ** Semantic conflicts arise when multiple extensions define rules
    ** that produce the same LHS from the same RHS pattern, but with
    ** different reduction actions (C code).  This means the parse
    ** structure is identical but the resulting AST/behaviour differs.
    **
    ** Detection strategy: for each pair of loaded extensions, compare
    ** MOD_ADD_RULE modifications that share the same LHS and RHS
    ** symbols but have different action code.
    */

    uint32_t found = 0;

    pthread_rwlock_rdlock(&reg->lock);

    /* Collect all loaded extensions into a local array for pairwise comparison */
    uint32_t n_loaded = 0;
    for (uint32_t i = 0; i < reg->count; i++) {
        if (reg->extensions[i].state == EXT_LOADED) n_loaded++;
    }

    if (n_loaded < 2) {
        pthread_rwlock_unlock(&reg->lock);
        return 0;
    }

    /* Index array for loaded extensions */
    uint32_t *loaded_idx = calloc(n_loaded, sizeof(uint32_t));
    if (loaded_idx == NULL) {
        pthread_rwlock_unlock(&reg->lock);
        return 0;
    }
    {
        uint32_t k = 0;
        for (uint32_t i = 0; i < reg->count; i++) {
            if (reg->extensions[i].state == EXT_LOADED) {
                loaded_idx[k++] = i;
            }
        }
    }

    /* Pairwise comparison of loaded extensions */
    for (uint32_t a = 0; a < n_loaded; a++) {
        const Extension *ext_a = &reg->extensions[loaded_idx[a]];

        for (uint32_t b = a + 1; b < n_loaded; b++) {
            const Extension *ext_b = &reg->extensions[loaded_idx[b]];

            /* Compare every rule from ext_a against every rule from ext_b */
            for (uint32_t ma = 0; ma < ext_a->nmodifications; ma++) {
                const GrammarModification *mod_a = &ext_a->modifications[ma];
                if (mod_a->type != MOD_ADD_RULE) continue;

                for (uint32_t mb = 0; mb < ext_b->nmodifications; mb++) {
                    const GrammarModification *mod_b = &ext_b->modifications[mb];
                    if (mod_b->type != MOD_ADD_RULE) continue;

                    /* Check if LHS matches */
                    if (mod_a->u.add_rule.lhs == NULL || mod_b->u.add_rule.lhs == NULL) continue;
                    if (strcmp(mod_a->u.add_rule.lhs, mod_b->u.add_rule.lhs) != 0) continue;

                    /* Check if RHS matches */
                    if (mod_a->u.add_rule.nrhs != mod_b->u.add_rule.nrhs) continue;

                    bool rhs_match = true;
                    for (int r = 0; r < mod_a->u.add_rule.nrhs; r++) {
                        const char *sa = mod_a->u.add_rule.rhs[r];
                        const char *sb = mod_b->u.add_rule.rhs[r];
                        if (sa == NULL || sb == NULL) {
                            if (sa != sb) {
                                rhs_match = false;
                                break;
                            }
                            continue;
                        }
                        if (strcmp(sa, sb) != 0) {
                            rhs_match = false;
                            break;
                        }
                    }
                    if (!rhs_match) continue;

                    /* Same LHS and RHS -- check if actions differ */
                    const char *code_a = mod_a->u.add_rule.code;
                    const char *code_b = mod_b->u.add_rule.code;

                    /* If both NULL or identical, no semantic conflict */
                    if (code_a == NULL && code_b == NULL) continue;
                    if (code_a != NULL && code_b != NULL && strcmp(code_a, code_b) == 0) continue;

                    /* Semantic conflict: same rule, different actions */
                    ConflictPoint *cp =
                        result_add_point(result, token, state, CONFLICT_LEVEL_SEMANTIC);
                    if (cp == NULL) goto done;

                    LimeContext ctx_a = {
                        .ext_id = ext_a->id,
                        .token = token,
                        .state = state,
                        .priority = 0,
                        .grammar_name = ext_a->name,
                    };
                    LimeContext ctx_b = {
                        .ext_id = ext_b->id,
                        .token = token,
                        .state = state,
                        .priority = 0,
                        .grammar_name = ext_b->name,
                    };
                    conflict_point_add_context(cp, &ctx_a);
                    conflict_point_add_context(cp, &ctx_b);

                    cp->description = build_description(CONFLICT_LEVEL_SEMANTIC, token, state, 2);

                    found++;
                    result->semantic_conflicts++;
                }
            }
        }
    }

done:
    free(loaded_idx);
    pthread_rwlock_unlock(&reg->lock);
    return found;
}

/* ------------------------------------------------------------------ */
/*  Combined single-point detection                                    */
/* ------------------------------------------------------------------ */

ConflictPoint detect_conflict(ExtensionRegistry *reg, uint16_t token, int state) {
    ConflictPoint cp;
    conflict_point_init(&cp, token, state, CONFLICT_LEVEL_TOKEN);

    if (reg == NULL) return cp;

    /*
    ** Walk all loaded extensions and check which ones can handle this
    ** token.  We look for:
    **   1. Extensions that define this token (MOD_ADD_TOKEN with
    **      matching token_code)
    **   2. Extensions that have rules referencing this token
    */

    pthread_rwlock_rdlock(&reg->lock);

    for (uint32_t i = 0; i < reg->count; i++) {
        const Extension *ext = &reg->extensions[i];
        if (ext->state != EXT_LOADED) continue;

        bool can_handle = false;
        const char *token_name = NULL;

        /* Check for token definition */
        for (uint32_t m = 0; m < ext->nmodifications; m++) {
            const GrammarModification *mod = &ext->modifications[m];
            if (mod->type == MOD_ADD_TOKEN && mod->u.add_token.token_code == (int)token) {
                can_handle = true;
                token_name = mod->u.add_token.name;
                break;
            }
        }

        /* If no direct token match, check for rules that could produce
        ** this token in the given state (approximate check) */
        if (!can_handle && state >= 0) {
            for (uint32_t m = 0; m < ext->nmodifications; m++) {
                const GrammarModification *mod = &ext->modifications[m];
                if (mod->type != MOD_ADD_RULE) continue;

                for (int r = 0; r < mod->u.add_rule.nrhs; r++) {
                    if (mod->u.add_rule.rhs[r] != NULL && token_name != NULL &&
                        strcmp(mod->u.add_rule.rhs[r], token_name) == 0) {
                        can_handle = true;
                        break;
                    }
                }
                if (can_handle) break;
            }
        }

        if (!can_handle) continue;

        LimeContext ctx = {
            .ext_id = ext->id,
            .token = token,
            .state = state,
            .priority = 0,
            .grammar_name = ext->name,
        };
        conflict_point_add_context(&cp, &ctx);
    }

    pthread_rwlock_unlock(&reg->lock);

    /* Determine the conflict level based on what we found */
    if (cp.ncontexts > 1) {
        if (state < 0) {
            cp.level = CONFLICT_LEVEL_TOKEN;
        } else {
            cp.level = CONFLICT_LEVEL_RULE;
        }
        cp.description = build_description(cp.level, token, state, cp.ncontexts);
    }

    return cp;
}

/* ------------------------------------------------------------------ */
/*  Full scan across all extensions                                    */
/* ------------------------------------------------------------------ */

bool detect_all_multi_grammar_conflicts(ExtensionRegistry *reg,
                                        MultiGrammarConflictResult *result) {
    if (reg == NULL || result == NULL) return false;

    /* Phase 1: Detect token-level conflicts */
    detect_token_conflicts(reg, result);

    /* Phase 2: Detect semantic-level conflicts (rule-pair comparison).
    ** We pass token=0, state=-1 since this is a full scan. */
    detect_semantic_conflicts(reg, 0, -1, result);

    /*
    ** Phase 3: Detect rule-level conflicts.
    **
    ** For a full scan, we need to enumerate all tokens defined by
    ** loaded extensions and check each one.  Since we don't have a
    ** full LALR(1) automaton available here, we check each token
    ** with state=-1 (state-independent).
    */
    pthread_rwlock_rdlock(&reg->lock);

    /* Collect all distinct token codes from loaded extensions */
    uint16_t *token_codes = NULL;
    uint32_t n_tokens = 0;
    uint32_t token_cap = 0;

    for (uint32_t i = 0; i < reg->count; i++) {
        const Extension *ext = &reg->extensions[i];
        if (ext->state != EXT_LOADED) continue;

        for (uint32_t m = 0; m < ext->nmodifications; m++) {
            const GrammarModification *mod = &ext->modifications[m];
            if (mod->type != MOD_ADD_TOKEN) continue;
            if (mod->u.add_token.token_code < 0) continue;

            uint16_t code = (uint16_t)mod->u.add_token.token_code;

            /* Check for duplicate */
            bool dup = false;
            for (uint32_t t = 0; t < n_tokens; t++) {
                if (token_codes[t] == code) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;

            /* Add to list */
            if (n_tokens >= token_cap) {
                uint32_t new_cap = token_cap == 0 ? 32 : token_cap * 2;
                uint16_t *p = realloc(token_codes, new_cap * sizeof(uint16_t));
                if (p == NULL) break;
                token_codes = p;
                token_cap = new_cap;
            }
            token_codes[n_tokens++] = code;
        }
    }

    pthread_rwlock_unlock(&reg->lock);

    /* Check each token for rule-level conflicts */
    for (uint32_t t = 0; t < n_tokens; t++) {
        detect_rule_conflicts(reg, token_codes[t], -1, result);
    }

    free(token_codes);

    return (result->npoints > 0);
}
