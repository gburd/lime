/*
** test_compose_lexicon.c -- the B<->C seam: publish_modified_snapshot
** composes the lexer tables (lime_lex_compose) as part of the publish, and
** a lexer conflict fails the publish like a parse-table conflict does.
**
** The host serves through a ParserManager.  Base: a tiny SQL-ish grammar
** with a lexicon (keywords select/from, operator "<=").  Extension
** oracle_compat (CONNECT "connect", ARROW "->>") composes and is
** activated.  Then each bad set must fail the publish, name the
** extensions involved, return no snapshot, and leave the active snapshot
** (and its lexicon) unchanged:
**
**   1. two extensions claim lexeme "connect" with different tokens
**      (oracle_compat CONNECT, graphx CONNECT_G).  The C4 policy check
**      reports it first; with that check disabled (by hand, see below)
**      the lexer seam still catches it.
**   2. an extension token reuses a BASE spelling: graphx2 spells LE2
**      "<=", which the base lexicon gives to LE.  Only the lexer knows
**      the base lexicon, so only the seam can catch this.
**   3. two extensions declare the same MOD_ADD_LEX_RULE name.
** Plus the positive half: after a good publish the composed snapshot has
** its own lexicon, with the extension's keyword and operator in it.
**
** Negative controls (by hand, see the C9 commit message):
**   - delete the lime_lex_compose call in publish_modified_snapshot:
**     the lexicon checks and cases 2 and 3 fail;
**   - make compose_decide_declarations skip TOKEN_LEXEME: case 1 still
**     fails the publish (now from the lexer), naming both extensions.
*/
#include "extension.h"
#include "lime_compiler.h"
#include "lime_lexer.h"
#include "parse_context.h"
#include "parser.h"
#include "parser_manager.h"
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

static const char BASE[] =
    "%name LX\n%token_type { int }\n"
    "%token SELECT FROM ID LE OP.\n"
    "stmt ::= SELECT expr FROM ID.\n"
    "expr ::= ID.\n"
    "expr ::= ID LE ID.\n";

/* One extension: up to 4 modifications. */
typedef struct X {
    const char *name;
    GrammarModification m[4];
    uint32_t n;
} X;

static bool get_mods(void *ud, const struct ParserSnapshot *b, GrammarModification **o,
                     uint32_t *n) {
    (void)b;
    X *x = ud;
    *o = x->m;
    *n = x->n;
    return true;
}
static void tok(X *x, const char *name, const char *lexeme) {
    GrammarModification *m = &x->m[x->n++];
    memset(m, 0, sizeof *m);
    m->type = MOD_ADD_TOKEN;
    m->u.add_token.name = name;
    m->u.add_token.lexeme = lexeme;
    m->u.add_token.token_code = -1;
}
static void rule(X *x, const char *lhs, const char **rhs, int nrhs) {
    GrammarModification *m = &x->m[x->n++];
    memset(m, 0, sizeof *m);
    m->type = MOD_ADD_RULE;
    m->u.add_rule.lhs = lhs;
    m->u.add_rule.rhs = rhs;
    m->u.add_rule.nrhs = nrhs;
    m->u.add_rule.code = "{ }";
    m->u.add_rule.precedence = -1;
}
static void lexrule(X *x, const char *name, const char *pattern, const char *token) {
    GrammarModification *m = &x->m[x->n++];
    memset(m, 0, sizeof *m);
    m->type = MOD_ADD_LEX_RULE;
    m->u.add_lex_rule.name = name;
    m->u.add_lex_rule.pattern = pattern;
    m->u.add_lex_rule.token = token;
}

static ExtensionRegistry *registry(X *const *xs, int n) {
    ExtensionRegistry *reg = create_extension_registry();
    for (int i = 0; i < n; i++) {
        ExtensionInfo info = {.name = xs[i]->name, .version = "1",
                              .get_modifications = get_mods, .user_data = xs[i]};
        ExtensionID id;
        char *e = NULL;
        if (!register_extension(reg, &info, &id) || !load_extension(reg, id, NULL, &e)) {
            printf("  [BROKEN] load %s: %s\n", xs[i]->name, e ? e : "?");
            exit(1);
        }
        free(e);
    }
    return reg;
}

static int kw(const ParserSnapshot *s, const char *w) {
    return lime_kw_lookup(lime_snapshot_lexicon(s), w, strlen(w));
}

/* A publish that must fail and name every extension in want[]; the
** active snapshot must not move or change. */
static void must_fail(const char *what, ParserManager *mgr, ParserSnapshot *active,
                      const ParserSnapshot *base, X *const *xs, int n, const char *const *want,
                      const char *must_mention) {
    printf("-- %s\n", what);
    ExtensionRegistry *reg = registry(xs, n);
    ParserSnapshot *out = (ParserSnapshot *)0x1;
    char *err = NULL;
    bool ok = publish_modified_snapshot(reg, base, &out, NULL, &err);
    if (ok) parser_manager_set_snapshot(mgr, out); /* a host swaps on success */
    destroy_extension_registry(reg);
    CHECK(!ok && out == NULL, "publish fails and returns no snapshot");
    if (err) printf("     error: %s\n", err);
    int named = err != NULL;
    for (int i = 0; want[i] && named; i++) named = strstr(err, want[i]) != NULL;
    CHECK(named, "the error names every extension involved");
    if (must_mention) CHECK(err && strstr(err, must_mention), must_mention);
    free(err);
    if (ok && out) snapshot_release(out);
    ParserSnapshot *cur = parser_manager_get_snapshot(mgr);
    CHECK(cur == active, "the active snapshot is unchanged");
    CHECK(kw(cur, "connect") == lime_snapshot_token_code(cur, "CONNECT"),
          "and still lexes \"connect\" as CONNECT");
    snapshot_release(cur);
}

int main(void) {
    char *err = NULL;
    ParserSnapshot *base = NULL;
    if (lime_compile_grammar_in_process(BASE, sizeof BASE - 1, &base, &err) != 0) {
        printf("base: %s\n", err);
        return 1;
    }
    static const LimeLexItem kws[] = {{"select", "SELECT"}, {"from", "FROM"}};
    static const LimeLexItem ops[] = {{"<=", "LE"}};
    err = lime_snapshot_set_lexicon(base, kws, 2, ops, 1, LIME_LEX_FOLD_CASE);
    if (err) { printf("lexicon: %s\n", err); return 1; }

    static const char *rhs_conn[] = {"CONNECT", "ID", NULL};
    static const char *rhs_arrow[] = {"ID", "ARROW", "ID", NULL};
    static const char *rhs_conng[] = {"CONNECT_G", "ID", NULL};
    static const char *rhs_le2[] = {"ID", "LE2", "ID", NULL};
    static const char *rhs_tmpl[] = {"TMPL", NULL};

    X ora = {"oracle_compat", {{0}}, 0};
    tok(&ora, "CONNECT", "connect");
    tok(&ora, "ARROW", "->>");
    rule(&ora, "stmt", rhs_conn, 2);
    rule(&ora, "expr", rhs_arrow, 3);

    printf("-- good publish: the lexicon is composed with the tables\n");
    X *good[] = {&ora};
    ExtensionRegistry *reg = registry(good, 1);
    ParserSnapshot *active = NULL;
    bool ok = publish_modified_snapshot(reg, base, &active, NULL, &err);
    destroy_extension_registry(reg);
    CHECK(ok && active, "oracle_compat composes");
    if (!active) { printf("  %s\n", err ? err : "?"); return 1; }
    free(err);
    err = NULL;
    const LimeLexicon *bl = lime_snapshot_lexicon(base), *cl = lime_snapshot_lexicon(active);
    CHECK(cl != NULL && cl != bl, "the composed snapshot has its own lexicon");
    CHECK(kw(active, "CONNECT") == lime_snapshot_token_code(active, "CONNECT"),
          "\"connect\" (case-folded) lexes as CONNECT");
    CHECK(kw(active, "select") == lime_snapshot_token_code(active, "SELECT"),
          "base keywords kept");
    int code = -1;
    CHECK(lime_op_match(cl, "->>x", 4, &code) == 3 && code == lime_snapshot_token_code(active, "ARROW"),
          "\"->>\" lexes as ARROW");
    CHECK(kw(base, "connect") == -1, "the base lexicon is untouched");

    ParserManager *mgr = parser_manager_create(NULL);
    parser_manager_set_snapshot(mgr, active);

    X gx = {"graphx", {{0}}, 0};
    tok(&gx, "CONNECT_G", "connect");
    rule(&gx, "stmt", rhs_conng, 2);
    X *s1[] = {&ora, &gx};
    const char *w1[] = {"oracle_compat", "graphx", NULL};
    must_fail("1. two extensions, one lexeme, two tokens", mgr, active, base, s1, 2, w1,
              "connect");

    X gx2 = {"graphx2", {{0}}, 0};
    tok(&gx2, "LE2", "<=");
    rule(&gx2, "expr", rhs_le2, 3);
    X *s2[] = {&ora, &gx2};
    const char *w2[] = {"graphx2", NULL};
    must_fail("2. an extension reuses the base operator \"<=\"", mgr, active, base, s2, 2, w2,
              "lexer:");

    X t1 = {"tmpl_a", {{0}}, 0}, t2 = {"tmpl_b", {{0}}, 0};
    tok(&t1, "TMPL", NULL);
    rule(&t1, "expr", rhs_tmpl, 1);
    lexrule(&t1, "template", "#\\{[^}]*\\}", "TMPL");
    lexrule(&t2, "template", "@[a-z]+", "TMPL");
    X *s3[] = {&ora, &t1, &t2};
    const char *w3[] = {"tmpl_a", "tmpl_b", NULL};
    must_fail("3. two extensions declare lex rule \"template\"", mgr, active, base, s3, 3, w3,
              "declared twice");

    parser_manager_destroy(mgr);
    snapshot_release(active);
    snapshot_release(base);
    printf("\n=== Summary === Pass: %d Fail: %d\n", n_pass, n_fail);
    return n_fail ? 1 : 0;
}
