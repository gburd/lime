/*
** test_compose_conflict_gate.c -- C1: an extension's LALR conflicts must be
** resolved explicitly, or the compose fails, whatever the base's %expect.
**
** Composes tests/compose_fixtures/dialect_ext.lime (30 rules) onto:
**
**   pg_grammar.lime  (precedence-complete PG, 0 conflicts)
**       -> FAILS with exactly 1 new conflict:
**          SR "upsert_cols ::=" on LPAREN;
**       -> composes with  conflict sr "upsert_cols ::=" on LPAREN prefer shift.
**
**   gram.lime        (no precedence; %expect 1665 hides 1665 conflicts)
**       -> FAILS with 219 new conflicts, though the merged grammar
**          would satisfy any count-based check that trusted the base's
**          %expect (the base's own 1665 are not counted against the
**          extension, and are not reported as new).
**
** Also checks that a plain base compose (no extension) succeeds, so the
** gate fails only on what the extension introduced.
**
** Negative control (run by hand, recorded in the commit): make
** compose_policy.c's on_conflict() treat every key as base-owned (the gate
** disabled) and this test fails on both bases.
**
** usage: test_compose_conflict_gate <pg_grammar.lime> <gram.lime> <dialect_ext.lime>
*/
#include "lime_compiler.h"
#include "lime_policy.h"
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

static char *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb");
    if (!f) { perror(p); exit(77); }
    fseek(f, 0, SEEK_END);
    long m = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)m + 1);
    if (!b || fread(b, 1, (size_t)m, f) != (size_t)m) exit(1);
    b[m] = 0;
    fclose(f);
    *n = (size_t)m;
    return b;
}

/* Run one compose; returns rc, fills report (caller frees). */
static int compose(const char *base, size_t nb, const char *ext, size_t ne,
                   const LimeComposePolicy *pol, ParserSnapshot **out, LimeComposeReport **rep) {
    LimeComposeUnit u = {"dialect_ext", "dialect_ext.lime", ext, ne};
    char *err = NULL;
    int rc = lime_compose_text(base, nb, &u, 1, pol, out, rep, &err);
    if (rc < 0) printf("  error: %s\n", err ? err : "?");
    free(err);
    return rc;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s pg_grammar.lime gram.lime dialect_ext.lime\n", argv[0]);
        return 2;
    }
    size_t npg, ngr, next;
    char *pg = slurp(argv[1], &npg), *gr = slurp(argv[2], &ngr), *ext = slurp(argv[3], &next);
    ParserSnapshot *snap = NULL;
    LimeComposeReport *rep = NULL;

    printf("-- pg_grammar.lime + dialect_ext.lime, default policy\n");
    int rc = compose(pg, npg, ext, next, NULL, &snap, &rep);
    uint32_t nnew = rep ? rep->n : 0;
    CHECK(rc == 1 && snap == NULL, "compose FAILS (rc=1, no snapshot)");
    CHECK(nnew == 1, "exactly 1 extension-introduced conflict");
    if (rep && rep->n == 1) {
        const LimeComposeEntry *e = &rep->entries[0];
        printf("     key: %s\n", e->key);
        CHECK(e->kind == LIME_CK_SR, "it is shift/reduce");
        CHECK(strcmp(e->key, "SR;upsert_cols ::=;LPAREN") == 0,
              "symbolic key SR;upsert_cols ::=;LPAREN (no state number)");
        CHECK(e->b.owner && strcmp(e->b.owner, "dialect_ext") == 0, "owned by dialect_ext");
        CHECK(e->resolution == NULL, "unresolved");
        CHECK(rep->base_nconflict == 0, "base alone has 0 conflicts");
        char *txt = lime_compose_report_format(rep);
        printf("%s", txt);
        free(txt);
    }
    lime_compose_report_free(rep);
    rep = NULL;
    snapshot_release(snap);
    snap = NULL;

    printf("-- same, with policy: conflict sr \"upsert_cols ::=\" on LPAREN prefer shift.\n");
    LimePolicyConflict line = {LIME_CK_SR, "upsert_cols ::=", "LPAREN", LIME_PREFER_SHIFT, NULL};
    LimeComposePolicy pol = {&line, 1};
    rc = compose(pg, npg, ext, next, &pol, &snap, &rep);
    CHECK(rc == 0 && snap != NULL, "compose succeeds with the policy line");
    CHECK(rep && rep->nunresolved == 0 && rep->nresolved == 1, "1 resolved by policy, 0 unresolved");
    if (snap) {
        int code = lime_snapshot_token_code(snap, "UPSERT_K");
        CHECK(code > 0, "composed snapshot has the extension token UPSERT_K");
    }
    lime_compose_report_free(rep);
    rep = NULL;
    snapshot_release(snap);
    snap = NULL;

    printf("-- gram.lime (%%expect 1665) + dialect_ext.lime, default policy\n");
    rc = compose(gr, ngr, ext, next, NULL, &snap, &rep);
    nnew = rep ? rep->n : 0;
    CHECK(rc == 1 && snap == NULL, "compose FAILS despite %expect 1665");
    CHECK(nnew == 219, "exactly 219 extension-introduced conflicts");
    if (rep) {
        printf("     base conflicts %d (base %%expect %d), new %u\n", rep->base_nconflict,
               rep->base_nexpect, rep->n);
        CHECK(rep->base_nexpect == 1665 && rep->base_nconflict == 1665,
              "base's own 1665 are judged by its %expect, not reported as new");
        uint32_t arrows = 0;
        for (uint32_t i = 0; i < rep->n; i++)
            if (strstr(rep->entries[i].lookahead, "JSON_ARROW")) arrows++;
        printf("     on JSON_ARROW/JSON_ARROW2: %u\n", arrows);
    }
    lime_compose_report_free(rep);
    rep = NULL;
    snapshot_release(snap);
    snap = NULL;

    printf("-- control: the extension-free compose of each base succeeds\n");
    rc = lime_compose_text(pg, npg, NULL, 0, NULL, &snap, NULL, NULL);
    CHECK(rc == 0 && snap != NULL, "pg_grammar.lime alone composes");
    snapshot_release(snap);
    snap = NULL;
    rc = lime_compose_text(gr, ngr, NULL, 0, NULL, &snap, NULL, NULL);
    CHECK(rc == 0 && snap != NULL, "gram.lime alone composes (its 1665 are the base's)");
    snapshot_release(snap);

    free(pg);
    free(gr);
    free(ext);
    printf("\n=== Summary === Pass: %d Fail: %d\n", n_pass, n_fail);
    return n_fail ? 1 : 0;
}
