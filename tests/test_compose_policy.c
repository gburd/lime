/*
** test_compose_policy.c -- C3: the compose policy, one case per verb.
**
** Base:  expr ::= expr PLUS expr | NUM   (%left PLUS)
** Each case composes one or two small extensions under a policy given as
** TEXT (lime_policy_parse) and checks the compose outcome and, where it
** succeeds, what the composed tables actually do on a real parse:
**
**   default (reject)      new conflict fails the compose
**   conflict sr shift     NUM BANG BANG (postfix vs prefix) -> composes
**   conflict sr reduce    the other choice; parse outcome differs
**   conflict rr reduce    keep a named rule of two reduce rules
**   conflict ... <ext>    owner preference
**   prefer-base           base vs extension: keep the base's action
**   prefer-extension      ... keep the extension's
**   rank (different)      ext vs ext: higher rank wins
**   rank (equal)          ext vs ext tie on a real conflict -> ERROR,
**                         even with default prefer-rank
**   token prec-like       gives an extension operator a precedence:
**                         the conflicts go away (1-2*3 parses as 1-(2*3))
**   reject rule / token   drops an extension item before generation
**   %policy { } block     the same text inside a grammar file
**   parse errors          bad statements report policy:LINE
** plus: rank decisions are order-independent (both load orders give the
** same tables).
*/
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
    "%token_type {int}\n"
    "%token NUM PLUS.\n"
    "%left PLUS.\n"
    "prog ::= expr.\n"
    "expr ::= expr PLUS expr.\n"
    "expr ::= NUM.\n";
/* no operator: two extensions' BANG rules meet only each other */
static const char *BASE0 =
    "%token_type {int}\n"
    "%token NUM.\n"
    "prog ::= expr.\n"
    "expr ::= NUM.\n";
static const char *g_base;

/* compose with policy text; returns rc, *snap on success */
static int compose(const char *policy_text, const LimeComposeUnit *u, uint32_t n,
                   ParserSnapshot **snap, LimeComposeReport **rep) {
    char *err = NULL;
    LimeComposePolicy *pol = NULL;
    if (policy_text) {
        pol = lime_policy_parse(policy_text, strlen(policy_text), &err);
        if (!pol) {
            printf("     policy error: %s\n", err ? err : "?");
            free(err);
            return -2;
        }
    }
    int rc = lime_compose_text(g_base, strlen(g_base), u, n, pol, snap, rep, &err);
    if (rc != 0 && err) printf("     compose: %.*s\n", (int)strcspn(err, "\n"), err);
    free(err);
    lime_policy_free(pol);
    return rc;
}

/* parse a token-name sequence; 1 accept, 0 reject.  Records the rules
** reduced, as rule identities, into trace (for "how did it parse"). */
static char g_trace[2048];
static int trace_reduce(void *user, int ruleno, const void *rhs, const int *locs, int nrhs,
                        void *lhs, int *lhs_loc) {
    (void)rhs; (void)locs; (void)nrhs; (void)lhs; (void)lhs_loc;
    const char *id = lime_snapshot_rule_id((const ParserSnapshot *)user, ruleno);
    /* leaves and the start rule say nothing about grouping */
    if (id && (strcmp(id, "expr ::= NUM") == 0 || strncmp(id, "prog ", 5) == 0)) return 0;
    size_t l = strlen(g_trace);
    snprintf(g_trace + l, sizeof g_trace - l, "%s%s", l ? " | " : "", id ? id : "?");
    return 0;
}
static int parse(ParserSnapshot *s, const char *const *toks) {
    g_trace[0] = 0;
    ParseContext *ctx = parse_begin(s);
    parse_set_host_reduce(ctx, trace_reduce, s);
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

static uint64_t fp(const ParserSnapshot *s) {
    uint64_t h = 1469598103934665603ULL;
#define H(p, n) do { const unsigned char *b_ = (const void *)(p); \
        for (size_t i_ = 0; i_ < (n); i_++) { h ^= b_[i_]; h *= 1099511628211ULL; } } while (0)
    H(&s->nstate, sizeof s->nstate);
    H(s->yy_action, s->action_count * 2);
    H(s->yy_lookahead, s->lookahead_count * 2);
    H(s->yy_shift_ofst, s->nstate * 4);
    H(s->yy_reduce_ofst, s->nstate * 4);
    H(s->yy_default, s->nstate * 2);
#undef H
    return h;
}

#define U(name, text) {name, name ".lime", text, sizeof(text) - 1}

int main(void) {
    ParserSnapshot *s = NULL;
    LimeComposeReport *r = NULL;

    /* Postfix BANG vs prefix BANG: expr ::= expr BANG / expr ::= BANG expr.
    ** "BANG NUM BANG" is ambiguous: (BANG NUM) BANG or BANG (NUM BANG). */
    static const char POST[] = "%token BANG.\nexpr ::= expr BANG.\n";
    static const char PRE[] = "%token BANG.\nexpr ::= BANG expr.\n";
    LimeComposeUnit post = U("post", POST), pre = U("pre", PRE);
    LimeComposeUnit both[2] = {post, pre};
    const char *bnb[] = {"BANG", "NUM", "BANG", NULL};

    g_base = BASE0;
    printf("-- default: reject\n");
    int rc = compose(NULL, both, 2, &s, &r);
    CHECK(rc == 1 && s == NULL && r && r->nunresolved == 1, "default fails with 1 new conflict");
    if (r && r->n) printf("     key: %s\n", r->entries[0].key);
    lime_compose_report_free(r); r = NULL;
    rc = compose("default reject.", both, 2, &s, &r);
    CHECK(rc == 1, "explicit `default reject.` is the same");
    lime_compose_report_free(r); r = NULL;

    printf("-- conflict sr prefer shift / reduce\n");
    rc = compose("conflict sr \"expr ::= BANG expr\" on BANG prefer shift.", both, 2, &s, &r);
    CHECK(rc == 0 && s && r->nresolved == 1, "prefer shift composes");
    if (s) {
        CHECK(parse(s, bnb), "BANG NUM BANG accepted");
        CHECK(strstr(g_trace, "expr ::= expr BANG | expr ::= BANG expr") != NULL,
              "shift: postfix binds first: BANG (NUM BANG)");
        printf("     trace: %s\n", g_trace);
    }
    snapshot_release(s); s = NULL; lime_compose_report_free(r); r = NULL;
    rc = compose("conflict sr \"expr ::= BANG expr\" on BANG prefer reduce.", both, 2, &s, &r);
    CHECK(rc == 0 && s, "prefer reduce composes");
    if (s) {
        CHECK(parse(s, bnb), "BANG NUM BANG accepted");
        CHECK(strstr(g_trace, "expr ::= BANG expr | expr ::= expr BANG") != NULL,
              "reduce: prefix binds first: (BANG NUM) BANG");
        printf("     trace: %s\n", g_trace);
    }
    snapshot_release(s); s = NULL; lime_compose_report_free(r); r = NULL;

    printf("-- conflict sr prefer <owner>\n");
    rc = compose("conflict sr \"expr ::= BANG expr\" on BANG prefer post.", both, 2, &s, &r);
    CHECK(rc == 0 && s && parse(s, bnb) && strstr(g_trace, "expr ::= expr BANG | expr ::= BANG expr"),
          "prefer post == keep post's shift");
    snapshot_release(s); s = NULL; lime_compose_report_free(r); r = NULL;
    rc = compose("conflict sr \"expr ::= BANG expr\" on BANG prefer pre.", both, 2, &s, &r);
    CHECK(rc == 0 && s && parse(s, bnb) && strstr(g_trace, "expr ::= BANG expr | expr ::= expr BANG"),
          "prefer pre == keep pre's reduce");
    snapshot_release(s); s = NULL; lime_compose_report_free(r); r = NULL;

    printf("-- rank\n");
    rc = compose("rank post 20. rank pre 10.", both, 2, &s, &r);
    CHECK(rc == 0 && s && parse(s, bnb) && strstr(g_trace, "expr ::= expr BANG | expr ::= BANG expr"),
          "post ranks higher -> post's shift");
    if (r && r->n) printf("     resolution: %s\n", r->entries[0].resolution ? r->entries[0].resolution : "-");
    uint64_t f1 = s ? fp(s) : 0;
    snapshot_release(s); s = NULL; lime_compose_report_free(r); r = NULL;
    LimeComposeUnit rev[2] = {pre, post};
    rc = compose("rank pre 10. rank post 20.", rev, 2, &s, &r);
    CHECK(rc == 0 && s && fp(s) == f1, "order-independent: reversed units + lines, same tables");
    snapshot_release(s); s = NULL; lime_compose_report_free(r); r = NULL;
    rc = compose("rank post 10. rank pre 20.", both, 2, &s, &r);
    CHECK(rc == 0 && s && parse(s, bnb) && strstr(g_trace, "expr ::= BANG expr | expr ::= expr BANG"),
          "pre ranks higher -> pre's reduce");
    snapshot_release(s); s = NULL; lime_compose_report_free(r); r = NULL;
    rc = compose("rank post 10. rank pre 10.", both, 2, &s, &r);
    CHECK(rc == 1 && s == NULL && r && r->n == 1 && r->entries[0].tie,
          "equal rank on a real conflict = error");
    lime_compose_report_free(r); r = NULL;
    rc = compose("rank post 10. rank pre 10. default prefer-rank.", both, 2, &s, &r);
    CHECK(rc == 1 && s == NULL, "equal rank is an error even under default prefer-rank");
    lime_compose_report_free(r); r = NULL;
    rc = compose("rank post 10. rank pre 10. conflict sr \"expr ::= BANG expr\" on BANG prefer shift.",
                 both, 2, &s, &r);
    CHECK(rc == 0 && s, "a conflict line overrides an equal-rank tie");
    snapshot_release(s); s = NULL; lime_compose_report_free(r); r = NULL;

    /* Base vs extension: MINUS prefix rule with no precedence conflicts
    ** with base `expr PLUS expr` on PLUS (shift = base's rule). */
    g_base = BASE;
    static const char NEG[] = "%token MINUS.\nexpr ::= MINUS expr.\n";
    LimeComposeUnit neg = U("neg", NEG);
    const char *mnpn[] = {"MINUS", "NUM", "PLUS", "NUM", NULL};
    printf("-- prefer-base / prefer-extension\n");
    rc = compose(NULL, &neg, 1, &s, &r);
    CHECK(rc == 1, "neg alone conflicts with base on PLUS");
    lime_compose_report_free(r); r = NULL;
    rc = compose("default prefer-base.", &neg, 1, &s, &r);
    CHECK(rc == 0 && s && parse(s, mnpn) && strstr(g_trace, "expr ::= expr PLUS expr | expr ::= MINUS expr"),
          "prefer-base keeps the base's shift: MINUS (NUM PLUS NUM)");
    snapshot_release(s); s = NULL; lime_compose_report_free(r); r = NULL;
    rc = compose("prefer-extension.", &neg, 1, &s, &r);
    CHECK(rc == 0 && s && parse(s, mnpn) && strstr(g_trace, "expr ::= MINUS expr | expr ::= expr PLUS expr"),
          "prefer-extension keeps neg's reduce: (MINUS NUM) PLUS NUM");
    snapshot_release(s); s = NULL; lime_compose_report_free(r); r = NULL;

    printf("-- token prec-like / prec\n");
    static const char TIMES[] = "%token TIMES.\nexpr ::= expr TIMES expr.\n";
    LimeComposeUnit times = U("times", TIMES);
    const char *npnt[] = {"NUM", "PLUS", "NUM", "TIMES", "NUM", NULL};
    rc = compose(NULL, &times, 1, &s, &r);
    CHECK(rc == 1 && r && r->n >= 2, "TIMES without precedence: new conflicts");
    lime_compose_report_free(r); r = NULL;
    rc = compose("token TIMES prec-like PLUS.", &times, 1, &s, &r);
    CHECK(rc == 0 && s && r && r->n == 0, "prec-like PLUS: precedence settles every conflict");
    if (s) CHECK(parse(s, npnt) && strstr(g_trace, "expr ::= expr PLUS expr | expr ::= expr TIMES expr"),
                 "same level, left assoc: (NUM PLUS NUM) TIMES NUM");
    snapshot_release(s); s = NULL; lime_compose_report_free(r); r = NULL;
    rc = compose("token TIMES prec 2 assoc left.", &times, 1, &s, &r);
    CHECK(rc == 0 && s && parse(s, npnt) && strstr(g_trace, "expr ::= expr TIMES expr | expr ::= expr PLUS expr"),
          "prec 2 (above PLUS's 1): NUM PLUS (NUM TIMES NUM)");
    printf("     trace: %s\n", g_trace);
    snapshot_release(s); s = NULL; lime_compose_report_free(r); r = NULL;
    rc = compose("token NOSUCH prec-like PLUS.", &times, 1, &s, &r);
    CHECK(rc < 0 && s == NULL, "precedence for an unknown terminal is an error");
    lime_compose_report_free(r); r = NULL;

    printf("-- conflict rr prefer reduce\n");
    /* Two extensions reduce NUM to expr by different routes. */
    static const char ATOM[] = "expr ::= atom.\natom ::= NUM.\n";
    LimeComposeUnit atom = U("atom", ATOM);
    rc = compose(NULL, &atom, 1, &s, &r);
    CHECK(rc == 1 && r && r->n >= 1 && r->entries[0].kind == LIME_CK_RR, "atom: new RR on NUM's reduce");
    if (r && r->n) printf("     key: %s\n", r->entries[0].key);
    lime_compose_report_free(r); r = NULL;
    rc = compose("conflict rr \"atom ::= NUM\" prefer reduce.", &atom, 1, &s, &r);
    const char *n1[] = {"NUM", NULL};
    CHECK(rc == 0 && s && parse(s, n1) && strstr(g_trace, "atom ::= NUM"),
          "keep atom ::= NUM: NUM reduces through atom");
    CHECK(r && r->ndead == 1 && strcmp(r->dead[0].rule, "expr ::= NUM") == 0,
          "the loser, expr ::= NUM, is reported as a dead rule (not an error)");
    snapshot_release(s); s = NULL; lime_compose_report_free(r); r = NULL;
    rc = compose("conflict rr \"expr ::= NUM\" prefer reduce.", &atom, 1, &s, &r);
    CHECK(rc == 0 && s && parse(s, n1) && !strstr(g_trace, "atom ::= NUM"),
          "keep expr ::= NUM: atom is never used");
    snapshot_release(s); s = NULL; lime_compose_report_free(r); r = NULL;

    printf("-- reject (text compose refuses; publish applies it: test_compose_order)\n");
    rc = compose("reject \"expr ::= BANG expr\" from pre.", both, 2, &s, &r);
    CHECK(rc == -1 && s == NULL, "lime_compose_text refuses reject lines instead of ignoring them");
    lime_compose_report_free(r); r = NULL;

    printf("-- %%policy { } block and parse errors\n");
    char *err = NULL;
    LimeComposePolicy *p = lime_policy_parse(
        "%token_type {int}\n%policy {\n  rank a 5. /* c */ rank b 7.\n"
        "  keyword \"connect\" owner a soft.\n  trust-extension-proposals b.\n}\nprog ::= X.\n",
        200, &err);
    CHECK(p && p->nranks == 2 && lime_policy_rank(p, "b") == 7 && lime_policy_rank(p, "base") == 0 &&
              p->nkeywords == 1 && p->keywords[0].soft && lime_policy_trusts(p, "b") &&
              !lime_policy_trusts(p, "a"),
          "%policy block parsed (rank, keyword, trust)");
    lime_policy_free(p);
    free(err); err = NULL;
    static const char *bad[] = {
        "rank a.",                          "conflict sr expr prefer shift.",
        "conflict xx \"a ::= B\" prefer shift.", "default sometimes.",
        "token T prec 0.",                  "rank a 1",
        "frobnicate.",                      "rank a 1. rank a 2.",
        "conflict rr \"a ::= B\" prefer shift.",
    };
    int nbad = 0;
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        LimeComposePolicy *q = lime_policy_parse(bad[i], strlen(bad[i]), &err);
        if (q == NULL && err && strncmp(err, "policy", 6) == 0) nbad++;
        else printf("     accepted: %s\n", bad[i]);
        lime_policy_free(q);
        free(err); err = NULL;
    }
    CHECK(nbad == (int)(sizeof bad / sizeof bad[0]), "every malformed statement is rejected with policy:...");
    p = lime_policy_parse("\n\nrank a 1.\nfrobnicate.\n", 25, &err);
    CHECK(p == NULL && err && strstr(err, "policy:4:"), "error names the line");
    if (err) printf("     %s\n", err);
    free(err);

    printf("\n=== Summary === Pass: %d Fail: %d\n", n_pass, n_fail);
    return n_fail ? 1 : 0;
}
