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
** both are identical across all orders.  Exit 0 = order-independent,
** 1 = order-dependent (prints which property differed), 77 = skip.
*/
#define _POSIX_C_SOURCE 200809L
#include "extension.h"
#include "parser.h"
#include "snapshot.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

int main(void) {
    struct stat st;
    const char *bin = getenv("LIME_BIN");
    if (!(bin && stat(bin, &st) == 0)) {
        printf("  [SKIP] LIME_BIN not set; this test needs the rebuild pipeline\n");
        return 77;
    }
    if (stat("limpar.c", &st) == 0) setenv("LIME_TEMPLATE", "limpar.c", 1);

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
    if (tables_same && codes_same) {
        printf("  RESULT: composition is a function of the extension SET.\n");
        return 0;
    }
    printf("  RESULT: composition depends on LOAD ORDER.\n");
    return 1;
}
