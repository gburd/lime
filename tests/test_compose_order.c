/*
** test_compose_order.c -- does the composed grammar depend on the order
** extensions were loaded in?
**
** Hickey's review point, made executable.  Three independent extensions
** each add one token and one rule to the arithmetic base.  We compose all
** 3! = 6 load orders through publish_modified_snapshot() and compare:
**
**   (a) the parse tables (fingerprint of yy_action/yy_lookahead/
**       yy_shift_ofst/yy_reduce_ofst/yy_default + rule metadata), and
**   (b) the token code each extension's keyword is assigned.
**
** A composition is a pure function of (base, SET of extensions) only if
** both are identical across all orders.
**
** Part 2 (C3): the same with a compose POLICY that has to decide real
** conflicts.  Three more extensions are loaded: two that make the same
** BANG token both postfix and prefix (a conflict between extensions,
** settled by rank; prefix-vs-base-operator settled by a conflict line), and
** one that adds `expr ::= expr BANG BANG` which the policy rejects.  All 6 orders of these three must give identical tables
** and token codes; and the result must differ from the no-reject
** composition (the reject took effect).
**
** Exit 0 = order-independent, 1 = order-dependent (prints which property
** differed), 77 = skip.
*/
#define _POSIX_C_SOURCE 200809L
#include "extension.h"
#include "parser.h"
#include "snapshot.h"
#include "lime_policy.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern ParserSnapshot *ArithBuildSnapshot(void);
extern int lime_snapshot_token_code(const ParserSnapshot *snap, const char *name);

typedef struct ExtDef {
    const char *name;   /* extension name */
    const char *tok;    /* token it adds */
    const char *lexeme;
    GrammarModification mods[2];
    const char *rhs[3];
} ExtDef;

static ExtDef g_ext[3] = {
    {"ext_bang",  "BANG",  "!",  {{0}}, {"expr", "BANG",  NULL}},
    {"ext_hash",  "HASH",  "#",  {{0}}, {"expr", "HASH",  NULL}},
    {"ext_tilde", "TILDE", "~",  {{0}}, {"expr", "TILDE", NULL}},
};

static bool get_mods(void *ud, const struct ParserSnapshot *base, GrammarModification **out,
                     uint32_t *nout) {
    (void)base;
    ExtDef *e = (ExtDef *)ud;
    memset(e->mods, 0, sizeof(e->mods));
    e->mods[0].type = MOD_ADD_TOKEN;
    e->mods[0].u.add_token.name = e->tok;
    e->mods[0].u.add_token.lexeme = e->lexeme;
    e->mods[0].u.add_token.token_code = -1;
    e->mods[1].type = MOD_ADD_RULE;
    e->mods[1].u.add_rule.lhs = "expr";
    e->mods[1].u.add_rule.rhs = e->rhs;
    e->mods[1].u.add_rule.nrhs = 2;
    e->mods[1].u.add_rule.code = "{ }";
    e->mods[1].u.add_rule.precedence = -1;
    *out = e->mods;
    *nout = 2;
    return true;
}

static uint64_t fnv(uint64_t h, const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ULL; }
    return h;
}

/* Fingerprint everything that determines parse behaviour. */
static uint64_t table_fingerprint(const ParserSnapshot *s) {
    uint64_t h = 1469598103934665603ULL;
    h = fnv(h, &s->nstate, sizeof s->nstate);
    h = fnv(h, &s->nrule, sizeof s->nrule);
    h = fnv(h, &s->nterminal, sizeof s->nterminal);
    if (s->yy_action)    h = fnv(h, s->yy_action,    s->action_count * sizeof(uint16_t));
    if (s->yy_lookahead) h = fnv(h, s->yy_lookahead, s->lookahead_count * sizeof(uint16_t));
    if (s->yy_shift_ofst) h = fnv(h, s->yy_shift_ofst, s->nstate * sizeof(int32_t));
    if (s->yy_reduce_ofst) h = fnv(h, s->yy_reduce_ofst, s->nstate * sizeof(int32_t));
    if (s->yy_default)   h = fnv(h, s->yy_default,   s->nstate * sizeof(uint16_t));
    if (s->yy_rule_info_lhs)  h = fnv(h, s->yy_rule_info_lhs,  s->nrule * sizeof(int16_t));
    if (s->yy_rule_info_nrhs) h = fnv(h, s->yy_rule_info_nrhs, s->nrule * sizeof(int8_t));
    return h;
}

static const int perms[6][3] = {{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};

/* Part 2 extensions: each adds token BANG (same name) and one rule. */
typedef struct PExt {
    const char *name;
    const char *rhs[4];
    int nrhs;
    GrammarModification mods[2];
} PExt;
static PExt g_pext[3] = {
    {"p_post",   {"expr", "BANG", NULL, NULL}, 2, {{0}}},
    {"p_pre",    {"BANG", "expr", NULL, NULL}, 2, {{0}}},
    {"p_double", {"expr", "BANG", "BANG", NULL}, 3, {{0}}},
};
static bool get_pmods(void *ud, const struct ParserSnapshot *base, GrammarModification **out,
                      uint32_t *nout) {
    (void)base;
    PExt *e = ud;
    memset(e->mods, 0, sizeof(e->mods));
    e->mods[0].type = MOD_ADD_TOKEN;
    e->mods[0].u.add_token.name = "BANG";
    e->mods[0].u.add_token.token_code = -1;
    e->mods[1].type = MOD_ADD_RULE;
    e->mods[1].u.add_rule.lhs = "expr";
    e->mods[1].u.add_rule.rhs = e->rhs;
    e->mods[1].u.add_rule.nrhs = e->nrhs;
    e->mods[1].u.add_rule.code = "{ }";
    e->mods[1].u.add_rule.precedence = -1;
    *out = e->mods;
    *nout = 2;
    return true;
}
/* the same token name from three extensions is a declaration-level
** collision; the extensions intend to share it */
static ConflictResolution share_token(void *ud, const ConflictInfo *ci) {
    (void)ud;
    return ci->existing_mod->type == MOD_ADD_TOKEN ? CONFLICT_KEEP_EXISTING : CONFLICT_UNRESOLVED;
}

/* compose part-2 set in order `ord` (NULL = all three in index order,
** n = how many) under `pol`; returns fingerprint, 0 on failure */
static uint64_t compose_p(ParserSnapshot *base, const int *ord, int n, const LimeComposePolicy *pol,
                          int *bang_code, char **err_out) {
    ExtensionRegistry *reg = create_extension_registry();
    extension_registry_set_policy(reg, pol);
    for (int k = 0; k < n; k++) {
        PExt *e = &g_pext[ord[k]];
        ExtensionInfo info = {0};
        info.name = e->name;
        info.version = "1.0.0";
        info.get_modifications = get_pmods;
        info.on_conflict = share_token;
        info.user_data = e;
        ExtensionID id;
        char *err = NULL;
        if (!register_extension(reg, &info, &id) || !load_extension(reg, id, NULL, &err)) {
            free(err);
            destroy_extension_registry(reg);
            return 0;
        }
        free(err);
    }
    ParserSnapshot *out = NULL;
    char *err = NULL;
    uint64_t h = 0;
    if (publish_modified_snapshot(reg, base, &out, NULL, &err) && out) {
        h = table_fingerprint(out);
        if (bang_code) *bang_code = lime_snapshot_token_code(out, "BANG");
        snapshot_release(out);
        free(err);
    } else if (err_out) {
        *err_out = err;
    } else {
        free(err);
    }
    destroy_extension_registry(reg);
    return h;
}

int main(void) {
    ParserSnapshot *base = ArithBuildSnapshot();
    if (base == NULL || base->grammar_source == NULL) {
        printf("  [SKIP] base snapshot has no grammar_source\n");
        return 77;
    }

    uint64_t fp[6];
    int code[6][3];
    for (int p = 0; p < 6; p++) {
        ExtensionRegistry *reg = create_extension_registry();
        for (int k = 0; k < 3; k++) {
            ExtDef *e = &g_ext[perms[p][k]];
            ExtensionInfo info = {0};
            info.name = e->name;
            info.version = "1.0.0";
            info.get_modifications = get_mods;
            info.user_data = e;
            ExtensionID id;
            char *err = NULL;
            if (!register_extension(reg, &info, &id) || !load_extension(reg, id, NULL, &err)) {
                fprintf(stderr, "  [BROKEN] load %s: %s\n", e->name, err ? err : "?");
                return 1;
            }
            free(err);
        }
        ParserSnapshot *out = NULL;
        struct ConflictSet *cs = NULL;
        char *err = NULL;
        if (!publish_modified_snapshot(reg, base, &out, &cs, &err) || out == NULL) {
            fprintf(stderr, "  [BROKEN] publish (order %d): %s\n", p, err ? err : "?");
            return 1;
        }
        free(err);
        fp[p] = table_fingerprint(out);
        for (int t = 0; t < 3; t++) code[p][t] = lime_snapshot_token_code(out, g_ext[t].tok);
        printf("  load order %-28s tables=%016llx  BANG=%d HASH=%d TILDE=%d\n",
               p == 0 ? "bang,hash,tilde" : p == 1 ? "bang,tilde,hash" :
               p == 2 ? "hash,bang,tilde" : p == 3 ? "hash,tilde,bang" :
               p == 4 ? "tilde,bang,hash" : "tilde,hash,bang",
               (unsigned long long)fp[p], code[p][0], code[p][1], code[p][2]);
        snapshot_release(out);
        destroy_extension_registry(reg);
    }
    snapshot_release(base);

    int tables_same = 1, codes_same = 1;
    for (int p = 1; p < 6; p++) {
        if (fp[p] != fp[0]) tables_same = 0;
        for (int t = 0; t < 3; t++) if (code[p][t] != code[0][t]) codes_same = 0;
    }
    printf("\n  parse tables identical across all 6 orders: %s\n", tables_same ? "YES" : "NO");
    printf("  token codes identical across all 6 orders:  %s\n", codes_same ? "YES" : "NO");

    /* ---- Part 2: with a policy ---- */
    static const char POL[] =
        "rank p_post 20.  rank p_pre 10.\n"                      /* post vs pre on BANG */
        "conflict sr \"expr ::= BANG expr\" prefer reduce.\n"   /* prefix binds tighter than base +/- */
        "reject \"expr ::= expr BANG BANG\" from p_double.\n";
    char *perr = NULL;
    LimeComposePolicy *pol = lime_policy_parse(POL, sizeof POL - 1, &perr);
    if (pol == NULL) {
        fprintf(stderr, "  [BROKEN] policy: %s\n", perr ? perr : "?");
        return 1;
    }
    base = ArithBuildSnapshot();
    printf("\n  part 2: post/pre/double under `%s`\n", "rank p_post 20. rank p_pre 10. reject ...");
    uint64_t pf[6];
    int pc[6] = {0};
    int p_same = 1;
    for (int p = 0; p < 6; p++) {
        char *e = NULL;
        pf[p] = compose_p(base, perms[p], 3, pol, &pc[p], &e);
        printf("  order %d%d%d  tables=%016llx  BANG=%d%s%s\n", perms[p][0], perms[p][1], perms[p][2],
               (unsigned long long)pf[p], pc[p], e ? "  error: " : "", e ? e : "");
        free(e);
        if (pf[p] == 0 || pf[p] != pf[0] || pc[p] != pc[0]) p_same = 0;
    }
    /* the reject took effect: = composing post+pre only */
    static const int two[2] = {0, 1};
    uint64_t f_two = compose_p(base, two, 2, pol, NULL, NULL);
    int rejected = pf[0] != 0 && pf[0] == f_two;
    /* without the policy the same set must FAIL (the conflict is real) */
    char *e0 = NULL;
    uint64_t f_nopol = compose_p(base, perms[0], 3, NULL, NULL, &e0);
    int needs_policy = f_nopol == 0;
    printf("  with policy, identical across 6 orders: %s\n", p_same ? "YES" : "NO");
    printf("  reject applied (tables == post+pre only):  %s\n", rejected ? "YES" : "NO");
    printf("  no policy -> compose fails:                %s%s%.60s\n", needs_policy ? "YES" : "NO",
           e0 ? "  (" : "", e0 ? e0 : "");
    free(e0);
    lime_policy_free(pol);
    snapshot_release(base);

    if (tables_same && codes_same && p_same && rejected && needs_policy) {
        printf("  RESULT: composition is a function of the extension SET (and the policy).\n");
        return 0;
    }
    printf("  RESULT: composition depends on LOAD ORDER (or the policy did not apply).\n");
    return 1;
}
