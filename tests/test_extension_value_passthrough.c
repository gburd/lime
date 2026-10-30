/*
** test_extension_value_passthrough.c -- extension values climb through base
** unit rules, in recognition mode (no host_reduce), through ext_action[].
**
** Base: bench/bench_arith_grammar.y
**   program ::= expr.  expr ::= term.  term ::= factor.  factor ::= NUM.
** Extension (no .code-only rules: every rule has a LimeReduceFn):
**   factor ::= HASH NUM.        make_hash: $$ = SENTINEL
**   expr   ::= expr AT term.    at: records what it sees in $1 and $3
**
**  1. "HASH NUM": SENTINEL must pass through term ::= factor,
**     expr ::= term and program ::= expr (3 unit rules) to parse_result().
**  2. "NUM@7 AT HASH NUM": `at` must see $3 == SENTINEL (it climbed
**     factor -> term) and $1 == the NUM token's value with loc 7 (the
**     leftmost token's value/loc climbs factor ::= NUM, term ::= factor,
**     expr ::= term -- the behaviour reduce() has always had once any
**     callback is bound).
**  3. control: the base snapshot (no bindings) yields no value.
** Negative control (by hand, see commit): force ext_action to 0 for
** expr ::= term and cases 1 and 2 fail.
*/
#include "extension.h"
#include "lime_compiler.h"
#include "parse_context.h"
#include "parser.h"
#include "snapshot.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern ParserSnapshot *ArithBuildSnapshot(void);

static int n_pass, n_fail;
#define CHECK(c, msg)                                                                     \
    do {                                                                                  \
        if (c) { n_pass++; printf("  ok   %s\n", msg); }                                  \
        else   { n_fail++; printf("  FAIL %s  (%s:%d)\n", msg, __FILE__, __LINE__); }     \
    } while (0)

#define SENTINEL ((void *)(uintptr_t)0xC0FFEE)
#define NUMVAL ((void *)(uintptr_t)0x4E554D)

static int g_hash_calls, g_at_calls;
static void *g_at_v1, *g_at_v3;
static int g_at_l1;

static void make_hash(void *u, void *x, int n, const void *v, const int *l, void *out) {
    (void)u; (void)x; (void)n; (void)v; (void)l;
    g_hash_calls++;
    *(void **)out = SENTINEL;
}
static void at(void *u, void *x, int n, const void *v, const int *l, void *out) {
    (void)u; (void)x;
    void *const *vals = v;
    g_at_calls++;
    g_at_v1 = n >= 1 ? vals[0] : NULL;
    g_at_v3 = n >= 3 ? vals[2] : NULL;
    g_at_l1 = l ? l[0] : -1;
    *(void **)out = g_at_v3;
}

static GrammarModification g_mods[4];
static const char *rhs_hash[] = {"HASH", "NUM", NULL};
static const char *rhs_at[] = {"expr", "AT", "term", NULL};
static bool get_mods(void *ud, const struct ParserSnapshot *b, GrammarModification **o, uint32_t *n) {
    (void)ud; (void)b;
    memset(g_mods, 0, sizeof g_mods);
    g_mods[0].type = MOD_ADD_TOKEN;
    g_mods[0].u.add_token.name = "HASH";
    g_mods[0].u.add_token.token_code = -1;
    g_mods[1].type = MOD_ADD_TOKEN;
    g_mods[1].u.add_token.name = "AT";
    g_mods[1].u.add_token.token_code = -1;
    g_mods[2].type = MOD_ADD_RULE;
    g_mods[2].u.add_rule.lhs = "factor";
    g_mods[2].u.add_rule.rhs = rhs_hash;
    g_mods[2].u.add_rule.nrhs = 2;
    g_mods[2].u.add_rule.code = "{ }";
    g_mods[2].u.add_rule.reduce = make_hash;
    g_mods[2].u.add_rule.precedence = -1;
    g_mods[3].type = MOD_ADD_RULE;
    g_mods[3].u.add_rule.lhs = "expr";
    g_mods[3].u.add_rule.rhs = rhs_at;
    g_mods[3].u.add_rule.nrhs = 3;
    g_mods[3].u.add_rule.code = "{ }";
    g_mods[3].u.add_rule.reduce = at;
    g_mods[3].u.add_rule.precedence = -1;
    *o = g_mods;
    *n = 4;
    return true;
}

/* tokens by name, with values and locations; returns parse_result or
** (void*)-1 on reject */
static void *parse(ParserSnapshot *s, const char *const *toks, void *const *vals, const int *locs) {
    ParseContext *ctx = parse_begin(s);
    int rc = 0;
    for (int i = 0; toks[i] && rc == 0; i++)
        rc = parse_token(ctx, lime_snapshot_token_code(s, toks[i]), vals[i], locs[i]);
    if (rc == 0) rc = parse_token(ctx, 0, NULL, -1);
    void *r = rc == 1 ? parse_result(ctx) : (void *)-1;
    parse_end(ctx);
    return r;
}

int main(void) {
    ParserSnapshot *base = ArithBuildSnapshot();
    if (!base || !base->grammar_source) { printf("  [SKIP] no base source\n"); return 77; }
    ExtensionRegistry *reg = create_extension_registry();
    ExtensionInfo info = {.name = "pt", .version = "1", .get_modifications = get_mods};
    ExtensionID id;
    char *err = NULL;
    register_extension(reg, &info, &id);
    load_extension(reg, id, NULL, NULL);
    ParserSnapshot *s = NULL;
    if (!publish_modified_snapshot(reg, base, &s, NULL, &err) || !s) {
        printf("publish: %s\n", err ? err : "?");
        return 1;
    }
    CHECK(s->ext_action != NULL, "composed snapshot has ext_action[]");
    int r_et = lime_snapshot_rule_by_id(s, "expr ::= term");
    int r_tf = lime_snapshot_rule_by_id(s, "term ::= factor");
    int r_hash = lime_snapshot_rule_by_id(s, "factor ::= HASH NUM");
    CHECK(r_et >= 0 && r_tf >= 0 && r_hash >= 0 && s->ext_action[r_hash] == 1 &&
              s->ext_action[r_et] == 2 && s->ext_action[r_tf] == 2,
          "ext_action: callback rule = 1, unit rules = 2");

    printf("-- 1. HASH NUM: SENTINEL climbs 3 unit rules to the accept\n");
    const char *t1[] = {"HASH", "NUM", NULL};
    void *v1[] = {NULL, NUMVAL};
    int l1[] = {0, 5};
    g_hash_calls = 0;
    void *res = parse(s, t1, v1, l1);
    CHECK(g_hash_calls == 1, "make_hash ran once");
    CHECK(res == SENTINEL, "parse_result() == SENTINEL (through term::=factor, expr::=term, program::=expr)");

    printf("-- 2. NUM AT HASH NUM: `at` sees both values\n");
    const char *t2[] = {"NUM", "AT", "HASH", "NUM", NULL};
    void *v2[] = {NUMVAL, NULL, NULL, NUMVAL};
    int l2[] = {7, 9, 11, 13};
    g_at_calls = 0;
    res = parse(s, t2, v2, l2);
    CHECK(g_at_calls == 1, "`at` ran once");
    CHECK(g_at_v3 == SENTINEL, "$3 == SENTINEL (climbed term ::= factor)");
    CHECK(g_at_v1 == NUMVAL && g_at_l1 == 7,
          "$1 == the NUM token's value at loc 7 (climbed factor::=NUM, term::=factor, expr::=term)");
    CHECK(res == SENTINEL, "and `at`'s result reaches parse_result()");

    printf("-- 3. control: base snapshot, no bindings\n");
    const char *t3[] = {"NUM", NULL};
    void *v3[] = {NUMVAL};
    int l3[] = {1};
    CHECK(base->ext_action == NULL && parse(base, t3, v3, l3) == NULL,
          "recognition-only base: no value (parse_result NULL)");

    snapshot_release(s);
    destroy_extension_registry(reg);
    snapshot_release(base);
    free(err);
    printf("\n=== Summary === Pass: %d Fail: %d\n", n_pass, n_fail);
    return n_fail ? 1 : 0;
}
