/*
** test_compose_failure_keeps_snapshot.c -- C7: a compose that fails returns
** an error, publishes nothing, and the snapshot the host is serving stays
** active and correct.
**
** The host serves through a ParserManager (an atomic active-snapshot
** slot).  It composes extension A (a postfix BANG rule) and activates it.
** Then four composes fail, each a different way, and after each one
** we check: publish returned false, *error is set, *out is NULL, the
** active snapshot is still the same pointer, and it still parses
** "NUM BANG" (A's syntax) and rejects the failed extension's syntax:
**
**   1. the extension's rule names an undeclared lowercase symbol that
**      has no rules (the rebuild hard-fails);
**   2. the extension adds a conflict the policy does not resolve;
**   3. the base snapshot has no grammar source (cannot be rebuilt;
**      the old code published a metadata-only clone here, tables
**      WITHOUT the extension);
**   4. a second extension duplicates a token name (declaration-level
**      conflict, unresolved).
**
** Negative control (by hand): restore the metadata-only fallback (or let
** the manager swap on failure) -- case 3 then "succeeds" with tables that
** reject NUM HASH and the test fails.
*/
#include "conflict.h"
#include "extension.h"
#include "parse_context.h"
#include "parser.h"
#include "parser_manager.h"
#include "snapshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern ParserSnapshot *ArithBuildSnapshot(void);
extern int lime_snapshot_token_code(const ParserSnapshot *snap, const char *name);

static int n_pass, n_fail;
#define CHECK(c, msg)                                                                     \
    do {                                                                                  \
        if (c) { n_pass++; printf("  ok   %s\n", msg); }                                  \
        else   { n_fail++; printf("  FAIL %s  (%s:%d)\n", msg, __FILE__, __LINE__); }     \
    } while (0)

/* One extension: token T, rule  lhs ::= rhs[0] rhs[1]. */
typedef struct Ext {
    const char *name, *tok, *lhs;
    const char *rhs[3];
    const char *tok2; /* optional second token (case 4) */
    GrammarModification mods[3];
} Ext;

static bool get_mods(void *ud, const struct ParserSnapshot *b, GrammarModification **out,
                     uint32_t *n) {
    (void)b;
    Ext *e = ud;
    uint32_t k = 0;
    memset(e->mods, 0, sizeof e->mods);
    e->mods[k].type = MOD_ADD_TOKEN;
    e->mods[k].u.add_token.name = e->tok;
    e->mods[k].u.add_token.token_code = -1;
    k++;
    if (e->tok2) {
        e->mods[k].type = MOD_ADD_TOKEN;
        e->mods[k].u.add_token.name = e->tok2;
        e->mods[k].u.add_token.token_code = -1;
        k++;
    }
    e->mods[k].type = MOD_ADD_RULE;
    e->mods[k].u.add_rule.lhs = e->lhs;
    e->mods[k].u.add_rule.rhs = e->rhs;
    e->mods[k].u.add_rule.nrhs = 2;
    e->mods[k].u.add_rule.code = "{ }";
    e->mods[k].u.add_rule.precedence = -1;
    k++;
    *out = e->mods;
    *n = k;
    return true;
}

/* 1 = accept, 0 = reject */
static int parses(ParserSnapshot *s, const char *t1, const char *t2) {
    int c1 = lime_snapshot_token_code(s, t1), c2 = t2 ? lime_snapshot_token_code(s, t2) : 0;
    if (c1 < 0 || c2 < 0) return 0;
    ParseContext *ctx = parse_begin(s);
    int rc = parse_token(ctx, c1, NULL, -1);
    if (rc == 0 && t2) rc = parse_token(ctx, c2, NULL, -1);
    if (rc == 0) rc = parse_token(ctx, 0, NULL, -1);
    parse_end(ctx);
    return rc == 1;
}

static ExtensionRegistry *registry_with(Ext *const *exts, int n) {
    ExtensionRegistry *reg = create_extension_registry();
    for (int i = 0; i < n; i++) {
        ExtensionInfo info = {.name = exts[i]->name, .version = "1.0.0",
                              .get_modifications = get_mods, .user_data = exts[i]};
        ExtensionID id;
        char *err = NULL;
        if (!register_extension(reg, &info, &id) || !load_extension(reg, id, NULL, &err)) {
            printf("  [BROKEN] load %s: %s\n", exts[i]->name, err ? err : "?");
            exit(1);
        }
        free(err);
    }
    return reg;
}

/* Attempt a compose that must fail; the manager's snapshot must not move. */
static void must_fail(const char *what, ParserManager *mgr, ParserSnapshot *active,
                      ExtensionRegistry *reg, const ParserSnapshot *base, const char *bad1,
                      const char *bad2) {
    printf("-- %s\n", what);
    ParserSnapshot *out = (ParserSnapshot *)0x1; /* must be overwritten with NULL */
    ConflictSet *cs = NULL;
    char *err = NULL;
    bool ok = publish_modified_snapshot(reg, base, &out, &cs, &err);
    if (ok) parser_manager_set_snapshot(mgr, out); /* a host would swap on success */
    CHECK(!ok, "publish returns false");
    CHECK(err != NULL && err[0] != '\0', "with an error message");
    CHECK(out == NULL, "and no snapshot");
    if (err) printf("     error: %.*s\n", (int)strcspn(err, "\n"), err);
    free(err);
    conflict_set_destroy(cs);
    if (ok && out) snapshot_release(out);

    ParserSnapshot *cur = parser_manager_get_snapshot(mgr);
    CHECK(cur == active, "the active snapshot is unchanged");
    CHECK(parses(cur, "NUM", "BANG"), "active still parses NUM BANG");
    if (bad1) CHECK(!parses(cur, bad1, bad2), "active does not have the failed extension");
    snapshot_release(cur);
}

int main(void) {
    ParserSnapshot *base = ArithBuildSnapshot();
    if (!base || !base->grammar_source) { printf("  [SKIP] no base source\n"); return 77; }

    static Ext bang = {"a_bang", "BANG", "expr", {"expr", "BANG", NULL}, NULL, {{0}}};
    Ext *set1[] = {&bang};
    ExtensionRegistry *reg = registry_with(set1, 1);
    ParserSnapshot *active = NULL;
    char *err = NULL;
    CHECK(publish_modified_snapshot(reg, base, &active, NULL, &err) && active, "compose A");
    free(err);
    err = NULL;
    destroy_extension_registry(reg);
    if (!active) return 1;
    CHECK(parses(active, "NUM", "BANG"), "A parses NUM BANG");

    ParserManager *mgr = parser_manager_create(NULL);
    parser_manager_set_snapshot(mgr, active);

    /* 1. rebuild hard-fails: `nosuchsym` has no rules */
    static Ext broken = {"b_broken", "HASH", "expr", {"nosuchsym", "HASH", NULL}, NULL, {{0}}};
    Ext *set2[] = {&bang, &broken};
    reg = registry_with(set2, 2);
    must_fail("1. extension grammar error", mgr, active, reg, base, "NUM", "HASH");
    destroy_extension_registry(reg);

    /* 2. unresolved conflict: expr ::= expr HASH  vs  expr ::= HASH expr */
    static Ext pre = {"c_pre", "HASH", "expr", {"HASH", "expr", NULL}, NULL, {{0}}};
    static Ext post = {"d_post", "HASH2", "expr", {"expr", "HASH", NULL}, NULL, {{0}}};
    Ext *set3[] = {&bang, &pre, &post};
    reg = registry_with(set3, 3);
    must_fail("2. unresolved extension conflict", mgr, active, reg, base, "HASH", "NUM");
    destroy_extension_registry(reg);

    /* 3. base without grammar source (the metadata-only fallback case) */
    static Ext hash = {"e_hash", "HASH", "expr", {"expr", "HASH", NULL}, NULL, {{0}}};
    Ext *set4[] = {&bang, &hash};
    reg = registry_with(set4, 2);
    char *saved_src = base->grammar_source;
    uint32_t saved_len = base->grammar_source_len;
    base->grammar_source = NULL;
    base->grammar_source_len = 0;
    must_fail("3. base has no grammar source", mgr, active, reg, base, "NUM", "HASH");
    base->grammar_source = saved_src;
    base->grammar_source_len = saved_len;
    /* control: the same set composes when the source is there */
    ParserSnapshot *ok4 = NULL;
    CHECK(publish_modified_snapshot(reg, base, &ok4, NULL, &err) && ok4 &&
              parses(ok4, "NUM", "HASH"),
          "control: same set with source composes and parses NUM HASH");
    free(err);
    err = NULL;
    snapshot_release(ok4);
    destroy_extension_registry(reg);

    /* 4. declaration-level: two extensions declare token BANG */
    static Ext bang2 = {"f_bang2", "BANG", "expr", {"BANG", "expr", NULL}, NULL, {{0}}};
    Ext *set5[] = {&bang, &bang2};
    reg = registry_with(set5, 2);
    must_fail("4. duplicate token across extensions", mgr, active, reg, base, "BANG", "NUM");
    destroy_extension_registry(reg);

    parser_manager_destroy(mgr);
    snapshot_release(active);
    snapshot_release(base);
    printf("\n=== Summary === Pass: %d Fail: %d\n", n_pass, n_fail);
    return n_fail ? 1 : 0;
}
