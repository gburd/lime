/*
** Conflict detection implementation.
**
** Scans modification arrays for conflicts between extensions and
** provides resolution through extension callbacks.
*/
#include "conflict.h"
#include "extension.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static char *dup_string(const char *s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy != NULL) memcpy(copy, s, len);
    return copy;
}

/*
** Build a human-readable conflict description.  Returns a malloc'd string.
*/
static char *format_conflict(ConflictType type, const char *detail_a, const char *detail_b) {
    const char *type_str;
    switch (type) {
    case CONFLICT_TOKEN_COLLISION:
        type_str = "token collision";
        break;
    case CONFLICT_DUPLICATE_RULE:
        type_str = "duplicate rule";
        break;
    case CONFLICT_PRECEDENCE_CLASH:
        type_str = "precedence clash";
        break;
    case CONFLICT_SHIFT_REDUCE:
        type_str = "shift/reduce";
        break;
    case CONFLICT_REDUCE_REDUCE:
        type_str = "reduce/reduce";
        break;
    default:
        type_str = "unknown conflict";
        break;
    }

    /* Upper bound on output size */
    size_t a_len = detail_a ? strlen(detail_a) : 0;
    size_t b_len = detail_b ? strlen(detail_b) : 0;
    size_t buf_sz = strlen(type_str) + a_len + b_len + 64;
    char *buf = malloc(buf_sz);
    if (buf == NULL) return NULL;

    snprintf(buf, buf_sz, "%s: '%s' vs '%s'", type_str, detail_a ? detail_a : "(unknown)",
             detail_b ? detail_b : "(unknown)");
    return buf;
}

/* ------------------------------------------------------------------ */
/*  ConflictSet                                                        */
/* ------------------------------------------------------------------ */

#define CS_INITIAL_CAPACITY 8

ConflictSet *conflict_set_create(void) {
    ConflictSet *cs = calloc(1, sizeof(ConflictSet));
    if (cs == NULL) return NULL;

    cs->conflicts = calloc(CS_INITIAL_CAPACITY, sizeof(Conflict));
    if (cs->conflicts == NULL) {
        free(cs);
        return NULL;
    }
    cs->capacity = CS_INITIAL_CAPACITY;
    cs->count = 0;
    return cs;
}

void conflict_set_destroy(ConflictSet *cs) {
    if (cs == NULL) return;
    for (uint32_t i = 0; i < cs->count; i++) {
        free(cs->conflicts[i].description);
    }
    free(cs->conflicts);
    free(cs);
}

bool conflict_set_add(ConflictSet *cs, ConflictType type, uint32_t mod_index_a,
                      uint32_t mod_index_b, ExtensionID ext_id_a, ExtensionID ext_id_b,
                      const char *description) {
    if (cs == NULL) return false;

    /* Grow if needed */
    if (cs->count >= cs->capacity) {
        uint32_t new_cap = cs->capacity * 2;
        Conflict *p = realloc(cs->conflicts, new_cap * sizeof(Conflict));
        if (p == NULL) return false;
        cs->conflicts = p;
        cs->capacity = new_cap;
    }

    Conflict *c = &cs->conflicts[cs->count];
    c->type = type;
    c->mod_index_a = mod_index_a;
    c->mod_index_b = mod_index_b;
    c->ext_id_a = ext_id_a;
    c->ext_id_b = ext_id_b;
    c->description = dup_string(description);
    c->resolved = false;

    cs->count++;
    return true;
}

uint32_t conflict_set_unresolved_count(const ConflictSet *cs) {
    if (cs == NULL) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < cs->count; i++) {
        if (!cs->conflicts[i].resolved) n++;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/*  Conflict detection                                                 */
/* ------------------------------------------------------------------ */

/*
** Check if two MOD_ADD_RULE modifications produce identical rules.
*/
static bool rules_identical(const GrammarModification *a, const GrammarModification *b) {
    if (a->type != MOD_ADD_RULE || b->type != MOD_ADD_RULE) return false;

    const char *lhs_a = a->u.add_rule.lhs;
    const char *lhs_b = b->u.add_rule.lhs;
    if (lhs_a == NULL || lhs_b == NULL) return false;
    if (strcmp(lhs_a, lhs_b) != 0) return false;

    if (a->u.add_rule.nrhs != b->u.add_rule.nrhs) return false;

    for (int i = 0; i < a->u.add_rule.nrhs; i++) {
        const char *sa = a->u.add_rule.rhs[i];
        const char *sb = b->u.add_rule.rhs[i];
        if (sa == NULL || sb == NULL) return sa == sb;
        if (strcmp(sa, sb) != 0) return false;
    }
    return true;
}

bool detect_conflicts(const GrammarModification *mods, uint32_t nmods, ConflictSet *cs) {
    if (mods == NULL || nmods == 0 || cs == NULL) return false;

    bool found = false;

    /* O(n^2) pairwise comparison -- acceptable for extension counts */
    for (uint32_t i = 0; i < nmods; i++) {
        for (uint32_t j = i + 1; j < nmods; j++) {
            const GrammarModification *a = &mods[i];
            const GrammarModification *b = &mods[j];

            /* Token collision: same token name from different mods */
            if (a->type == MOD_ADD_TOKEN && b->type == MOD_ADD_TOKEN) {
                const char *name_a = a->u.add_token.name;
                const char *name_b = b->u.add_token.name;
                if (name_a != NULL && name_b != NULL && strcmp(name_a, name_b) == 0) {
                    char *desc = format_conflict(CONFLICT_TOKEN_COLLISION, name_a, name_b);
                    conflict_set_add(cs, CONFLICT_TOKEN_COLLISION, i, j, 0, 0, desc);
                    free(desc);
                    found = true;
                }
            }

            /* Duplicate rule */
            if (a->type == MOD_ADD_RULE && b->type == MOD_ADD_RULE) {
                if (rules_identical(a, b)) {
                    const char *lhs = a->u.add_rule.lhs;
                    char *desc = format_conflict(CONFLICT_DUPLICATE_RULE, lhs, lhs);
                    conflict_set_add(cs, CONFLICT_DUPLICATE_RULE, i, j, 0, 0, desc);
                    free(desc);
                    found = true;
                }
            }

            /* Precedence clash: same symbol, different precedence */
            if (a->type == MOD_MODIFY_PRECEDENCE && b->type == MOD_MODIFY_PRECEDENCE) {
                const char *sym_a = a->u.modify_prec.symbol;
                const char *sym_b = b->u.modify_prec.symbol;
                if (sym_a != NULL && sym_b != NULL && strcmp(sym_a, sym_b) == 0) {
                    if (a->u.modify_prec.new_precedence != b->u.modify_prec.new_precedence ||
                        a->u.modify_prec.new_assoc != b->u.modify_prec.new_assoc) {
                        char *desc = format_conflict(CONFLICT_PRECEDENCE_CLASH, sym_a, sym_b);
                        conflict_set_add(cs, CONFLICT_PRECEDENCE_CLASH, i, j, 0, 0, desc);
                        free(desc);
                        found = true;
                    }
                }
            }
        }
    }

    return found;
}

/* ------------------------------------------------------------------ */
/*  Conflict resolution                                                */
/* ------------------------------------------------------------------ */

uint32_t resolve_conflicts(ConflictSet *cs, const GrammarModification *mods, uint32_t nmods,
                           ExtensionRegistry *registry) {
    if (cs == NULL || mods == NULL || registry == NULL) {
        return cs ? conflict_set_unresolved_count(cs) : 0;
    }

    (void)nmods; /* used implicitly via mod indices */

    for (uint32_t i = 0; i < cs->count; i++) {
        Conflict *c = &cs->conflicts[i];
        if (c->resolved) continue;

        /* Try extension A's on_conflict callback */
        if (c->ext_id_a != 0) {
            const Extension *ext_a = find_extension(registry, c->ext_id_a);
            if (ext_a != NULL && ext_a->on_conflict != NULL) {
                ConflictInfo info = {
                    .existing_ext = c->ext_id_a,
                    .new_ext = c->ext_id_b,
                    .existing_mod = &mods[c->mod_index_a],
                    .new_mod = &mods[c->mod_index_b],
                };
                ConflictResolution res = ext_a->on_conflict(ext_a->user_data, &info);
                if (res != CONFLICT_UNRESOLVED) {
                    c->resolved = true;
                    continue;
                }
            }
        }

        /* Try extension B's on_conflict callback */
        if (c->ext_id_b != 0) {
            const Extension *ext_b = find_extension(registry, c->ext_id_b);
            if (ext_b != NULL && ext_b->on_conflict != NULL) {
                ConflictInfo info = {
                    .existing_ext = c->ext_id_a,
                    .new_ext = c->ext_id_b,
                    .existing_mod = &mods[c->mod_index_a],
                    .new_mod = &mods[c->mod_index_b],
                };
                ConflictResolution res = ext_b->on_conflict(ext_b->user_data, &info);
                if (res != CONFLICT_UNRESOLVED) {
                    c->resolved = true;
                    continue;
                }
            }
        }
    }

    return conflict_set_unresolved_count(cs);
}
