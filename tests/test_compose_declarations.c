/*
** test_compose_declarations.c -- C4: declaration-level conflicts settled
** by the compose policy (DESIGN.md 4.4 worked examples (a) and (c)).
**
** Base: stmt ::= SELECT ID.  plus  expr/ID rules, identifier token ID.
**
** (a) Two extensions claim keyword "connect": oracle_compat adds CONNECT,
**     graphx adds CONNECT_G, both with lexeme "connect".
**       no policy                        -> FAIL, report names both
**       keyword "connect" owner oracle_compat soft.
**                                        -> composes; CONNECT keeps the
**                                           lexeme and falls back to ID
**                                           (SELECT CONNECT parses as an
**                                           identifier); CONNECT_G keeps
**                                           its terminal but no lexeme
**       ... reserved.                    -> CONNECT does not fall back
**       rank graphx 20. rank oracle 10.  -> graphx owns it
**       equal ranks                      -> FAIL
**       same name + same lexeme          -> shared, not a conflict
** (c) Two extensions both add `expr ::= expr DPIPE expr` with different
**     reduce callbacks:
**       no policy                        -> FAIL (DUP_RULE_ACTION)
**       conflict rule "..." prefer vec.  -> vec's callback runs at parse
**       rank                             -> the higher rank's runs
**       equal ranks                      -> FAIL
**       same callback in both            -> one rule, composes
**     Different terminals for the lexeme "||" is case (a).
** Precedence clash: two extensions set different precedence for DPIPE ->
**   no policy FAIL; `token DPIPE prec-like PLUS.` replaces both; rank picks.
** Negative controls are in the C4 commit message.
*/
#include "extension.h"
#include "lime_compiler.h"
#include "lime_policy.h"
#include "parse_context.h"
#include "parser.h"
#include "snapshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int n_pass, n_fail;
#define CHECK(c, msg)                                                                     \
    do {                                                                                  \
        if (c) { n_pass++; printf("  ok   %s\n", msg); }                                  \
        else   { n_fail++; printf("  FAIL %s  (%s:%d)\n", msg, __FILE__, __LINE__); }     \
    } while (0)

static const char *BASE =
    "%token_type {void*}\n"
    "%token SELECT ID PLUS.\n"
    "%left PLUS.\n"
    "%start_symbol stmt\n"
    "stmt ::= SELECT expr.\n"
    "expr ::= expr PLUS expr.\n"
    "expr ::= ID.\n";

/* One extension = up to 3 modifications. */
typedef struct X {
    const char *name;
    GrammarModification mods[3];
    uint32_t n;
} X;

static bool get_mods(void *ud, const struct ParserSnapshot *b, GrammarModification **out,
                     uint32_t *n) {
    (void)b;
    X *x = ud;
    *out = x->mods;
    *n = x->n;
    return true;
}

static void tok(X *x, const char *name, const char *lexeme, const char *fallback) {
    GrammarModification *m = &x->mods[x->n++];
    memset(m, 0, sizeof(*m));
    m->type = MOD_ADD_TOKEN;
    m->u.add_token.name = name;
    m->u.add_token.lexeme = lexeme;
    m->u.add_token.token_code = -1;
    m->u.add_token.fallback_to = fallback;
}
static void rule(X *x, const char *lhs, const char **rhs, int nrhs, LimeReduceFn fn, void *ud) {
    GrammarModification *m = &x->mods[x->n++];
    memset(m, 0, sizeof(*m));
    m->type = MOD_ADD_RULE;
    m->u.add_rule.lhs = lhs;
    m->u.add_rule.rhs = rhs;
    m->u.add_rule.nrhs = nrhs;
    m->u.add_rule.code = "{ }";
    m->u.add_rule.reduce = fn;
    m->u.add_rule.reduce_user = ud;
    m->u.add_rule.precedence = -1;
}
static void prec(X *x, const char *sym, int level, int assoc) {
    GrammarModification *m = &x->mods[x->n++];
    memset(m, 0, sizeof(*m));
    m->type = MOD_MODIFY_PRECEDENCE;
    m->u.modify_prec.symbol = sym;
    m->u.modify_prec.new_precedence = level;
    m->u.modify_prec.new_assoc = assoc;
}

static ParserSnapshot *g_base;
static char *g_err;

/* compose the given extensions under policy text; snapshot or NULL */
static ParserSnapshot *compose(X *const *xs, int n, const char *policy) {
    ExtensionRegistry *reg = create_extension_registry();
    LimeComposePolicy *pol = NULL;
    free(g_err);
    g_err = NULL;
    if (policy) {
        pol = lime_policy_parse(policy, strlen(policy), &g_err);
        if (!pol) { printf("     policy: %s\n", g_err); return NULL; }
    }
    extension_registry_set_policy(reg, pol);
    for (int i = 0; i < n; i++) {
        ExtensionInfo info = {.name = xs[i]->name, .version = "1",
                              .get_modifications = get_mods, .user_data = xs[i]};
        ExtensionID id;
        char *e = NULL;
        register_extension(reg, &info, &id);
        load_extension(reg, id, NULL, &e);
        free(e);
    }
    ParserSnapshot *out = NULL;
    if (!publish_modified_snapshot(reg, g_base, &out, NULL, &g_err)) out = NULL;
    destroy_extension_registry(reg);
    lime_policy_free(pol);
    return out;
}

static int parse(ParserSnapshot *s, const char *const *toks) {
    ParseContext *ctx = parse_begin(s);
    int rc = 0;
    for (int i = 0; toks[i] && rc == 0; i++) {
        int c = lime_snapshot_token_code(s, toks[i]);
        if (c < 0) { rc = -1; break; }
        rc = parse_token(ctx, c, NULL, -1);
    }
    if (rc == 0) rc = parse_token(ctx, 0, NULL, -1);
    parse_end(ctx);
    return rc == 1;
}

/* which extension's DPIPE callback ran */
static const char *g_ran;
static void red_vec(void *u, void *e, int n, const void *v, const int *l, void *out) {
    (void)u; (void)e; (void)n; (void)v; (void)l;
    g_ran = "vec";
    *(void **)out = NULL;
}
static void red_str(void *u, void *e, int n, const void *v, const int *l, void *out) {
    (void)u; (void)e; (void)n; (void)v; (void)l;
    g_ran = "str";
    *(void **)out = NULL;
}

int main(void) {
    char *err = NULL;
    if (lime_compile_grammar_in_process(BASE, strlen(BASE), &g_base, &err) != 0) {
        printf("base: %s\n", err);
        return 1;
    }
    static const char *rhs_conn[] = {"CONNECT", "ID", NULL};
    static const char *rhs_conng[] = {"CONNECT_G", "ID", NULL};
    const char *sel_conn[] = {"SELECT", "CONNECT", NULL};   /* connect used as a column */
    const char *conn_id[] = {"CONNECT", "ID", NULL};

    printf("-- (a) two extensions claim keyword \"connect\"\n");
    X ora = {"oracle_compat", {{0}}, 0}, gx = {"graphx", {{0}}, 0};
    tok(&ora, "CONNECT", "connect", NULL);
    rule(&ora, "stmt", rhs_conn, 2, NULL, NULL);
    tok(&gx, "CONNECT_G", "connect", NULL);
    rule(&gx, "stmt", rhs_conng, 2, NULL, NULL);
    X *a2[] = {&ora, &gx};

    ParserSnapshot *s = compose(a2, 2, NULL);
    CHECK(s == NULL && g_err && strstr(g_err, "TOKEN_LEXEME") && strstr(g_err, "CONNECT") &&
              strstr(g_err, "CONNECT_G"),
          "no policy: FAIL, report names CONNECT and CONNECT_G");
    if (g_err) printf("     %.*s\n", (int)strcspn(strchr(g_err, '\n') ? strchr(g_err, '\n') + 1 : g_err, "\n"),
                      strchr(g_err, '\n') ? strchr(g_err, '\n') + 1 : g_err);
    snapshot_release(s);

    s = compose(a2, 2, "keyword \"connect\" owner oracle_compat soft.");
    CHECK(s != NULL, "keyword ... owner oracle_compat soft: composes");
    if (s) {
        CHECK(parse(s, conn_id), "CONNECT ID parses (oracle_compat's syntax)");
        CHECK(parse(s, sel_conn), "soft: SELECT CONNECT parses (falls back to ID)");
        CHECK(s->yy_fallback && s->yy_fallback[lime_snapshot_token_code(s, "CONNECT")] ==
                  lime_snapshot_token_code(s, "ID"),
              "CONNECT's %fallback is ID");
        CHECK(lime_snapshot_token_code(s, "CONNECT_G") > 0, "CONNECT_G stays a terminal (rules intact)");
    }
    snapshot_release(s);
    s = compose(a2, 2, "keyword \"connect\" owner oracle_compat reserved.");
    CHECK(s && parse(s, conn_id) && !parse(s, sel_conn), "reserved: SELECT CONNECT is rejected");
    snapshot_release(s);
    s = compose(a2, 2, "keyword \"Connect\" owner oracle_compat reserved.");
    CHECK(s != NULL, "lexemes compare case-insensitively");
    snapshot_release(s);
    s = compose(a2, 2, "rank graphx 20. rank oracle_compat 10.");
    CHECK(s != NULL, "rank: graphx (20) owns the lexeme");
    snapshot_release(s);
    s = compose(a2, 2, "rank graphx 10. rank oracle_compat 10.");
    CHECK(s == NULL && g_err && strstr(g_err, "equal rank"), "equal ranks: FAIL");
    snapshot_release(s);
    /* same name + same lexeme from two extensions: shared */
    X o2 = {"oracle_compat", {{0}}, 0}, g2 = {"graphx", {{0}}, 0};
    tok(&o2, "CONNECT", "connect", NULL);
    rule(&o2, "stmt", rhs_conn, 2, NULL, NULL);
    tok(&g2, "CONNECT", "connect", NULL);
    X *a3[] = {&o2, &g2};
    s = compose(a3, 2, NULL);
    CHECK(s && parse(s, conn_id), "same token name and lexeme: shared, composes with no policy");
    snapshot_release(s);

    printf("-- (c) two extensions add `expr ::= expr DPIPE expr` with different actions\n");
    static const char *rhs_dp[] = {"expr", "DPIPE", "expr", NULL};
    X vec = {"vec", {{0}}, 0}, str = {"str", {{0}}, 0};
    tok(&vec, "DPIPE", "||", NULL);
    rule(&vec, "expr", rhs_dp, 3, red_vec, NULL);
    tok(&str, "DPIPE", "||", NULL);
    rule(&str, "expr", rhs_dp, 3, red_str, NULL);
    X *c2[] = {&vec, &str};
    const char *dp[] = {"SELECT", "ID", "DPIPE", "ID", NULL};
    s = compose(c2, 2, NULL);
    CHECK(s == NULL && g_err != NULL, "no policy: FAIL (same rule, two actions)");
    if (g_err) printf("     %.*s\n", (int)strcspn(g_err, "\n"), g_err);
    snapshot_release(s);
    /* the rule also conflicts with itself on DPIPE; settle that once */
    static const char *POL_C =
        "conflict rule \"expr ::= expr DPIPE expr\" prefer vec.\n"
        "token DPIPE prec-like PLUS.\n";
    s = compose(c2, 2, POL_C);
    CHECK(s != NULL, "conflict rule ... prefer vec: composes");
    if (s) {
        g_ran = NULL;
        CHECK(parse(s, dp) && g_ran && strcmp(g_ran, "vec") == 0, "vec's callback runs at parse time");
    }
    snapshot_release(s);
    s = compose(c2, 2, "conflict rule \"expr ::= expr DPIPE expr\" prefer str.\ntoken DPIPE prec-like PLUS.\n");
    g_ran = NULL;
    CHECK(s && parse(s, dp) && g_ran && strcmp(g_ran, "str") == 0, "prefer str: str's callback runs");
    snapshot_release(s);
    s = compose(c2, 2, "rank str 5. rank vec 9.\ntoken DPIPE prec-like PLUS.\n");
    g_ran = NULL;
    CHECK(s && parse(s, dp) && g_ran && strcmp(g_ran, "vec") == 0, "rank: vec (9) wins");
    snapshot_release(s);
    s = compose(c2, 2, "rank str 5. rank vec 5.\ntoken DPIPE prec-like PLUS.\n");
    CHECK(s == NULL && g_err && strstr(g_err, "equal rank"), "equal ranks: FAIL");
    snapshot_release(s);
    X v2 = {"vec", {{0}}, 0}, s2 = {"str", {{0}}, 0};
    tok(&v2, "DPIPE", "||", NULL);
    rule(&v2, "expr", rhs_dp, 3, red_vec, NULL);
    tok(&s2, "DPIPE", "||", NULL);
    rule(&s2, "expr", rhs_dp, 3, red_vec, NULL);
    X *c3[] = {&v2, &s2};
    s = compose(c3, 2, "token DPIPE prec-like PLUS.");
    CHECK(s != NULL, "same rule, same callback: one rule, composes");
    snapshot_release(s);

    printf("-- precedence clash\n");
    X pa = {"pa", {{0}}, 0}, pb = {"pb", {{0}}, 0};
    tok(&pa, "DPIPE", "||", NULL);
    rule(&pa, "expr", rhs_dp, 3, red_vec, NULL);
    prec(&pa, "DPIPE", 5, 1);
    tok(&pb, "DPIPE", "||", NULL);
    prec(&pb, "DPIPE", 7, 2);
    X *p2[] = {&pa, &pb};
    s = compose(p2, 2, NULL);
    CHECK(s == NULL, "no policy: FAIL");
    if (g_err) printf("     %.*s\n", (int)strcspn(g_err, "\n"), g_err);
    snapshot_release(s);
    s = compose(p2, 2, "token DPIPE prec-like PLUS.");
    CHECK(s != NULL, "host `token DPIPE prec-like PLUS.` replaces both proposals");
    snapshot_release(s);
    s = compose(p2, 2, "rank pa 3. rank pb 3.");
    CHECK(s == NULL && g_err && strstr(g_err, "equal rank"), "equal ranks: FAIL");
    snapshot_release(s);

    free(g_err);
    snapshot_release(g_base);
    printf("\n=== Summary === Pass: %d Fail: %d\n", n_pass, n_fail);
    return n_fail ? 1 : 0;
}
