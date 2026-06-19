/*
** tests/test_multi_grammar.c -- Tiers 1-3 of overlapping-grammar
** disambiguation (the PG >90%-overlap question).
**
** Tier 1 (lime_mg_resolve): two dialects whose SELECT statements
**   DIVERGE mid-statement.  Simulating the real token stream against
**   each must pick the one that reaches accept -- full-statement
**   matching, not one-token lookahead.
** Tier 2 (LimeBayesStore): a Tier-1 tie (both dialects accept the
**   identical stream) is broken by the learned posterior; the store
**   round-trips through serialize/deserialize.
** Tier 3 (LimeDialectRegistry): name -> snapshot selection and the
**   @dialect leading-sigil parser.
**
** Grammars are compiled in-process from text (no cc), like
** test_lime_compile_in_process, so the test exercises the real path.
*/
#include "test_compat.h"

#include "multi_grammar.h"
#include "parse_context.h"
#include "snapshot.h"
#include "lime_compiler.h"
#include "lime_threads.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define CHECK(cond, msg) do {                                            \
    if (!(cond)) {                                                       \
        fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__);          \
        fail++;                                                         \
    }                                                                    \
} while (0)

/* Token codes shared by both dialect grammars (declaration order:
** SELECT=1, STAR=2, FROM=3, NAME=4, LIMIT=5, ROWS=6). */
enum { T_SELECT = 1, T_STAR = 2, T_FROM = 3, T_NAME = 4, T_LIMIT = 5, T_ROWS = 6 };

/* "MySQL-ish": SELECT * FROM name [LIMIT name]  -- LIMIT then a bare
** count. */
static const char k_mysql[] =
    "%name MyG\n"
    "%token SELECT STAR FROM NAME LIMIT ROWS.\n"
    "%start_symbol stmt\n"
    "stmt ::= SELECT STAR FROM NAME.\n"
    "stmt ::= SELECT STAR FROM NAME LIMIT NAME.\n";

/* "Oracle-ish": SELECT * FROM name [ROWS name] -- a different trailing
** clause keyword.  Overlaps the base SELECT * FROM name exactly, but
** diverges on the trailing clause. */
static const char k_oracle[] =
    "%name OrG\n"
    "%token SELECT STAR FROM NAME LIMIT ROWS.\n"
    "%start_symbol stmt\n"
    "stmt ::= SELECT STAR FROM NAME.\n"
    "stmt ::= SELECT STAR FROM NAME ROWS NAME.\n";

static ParserSnapshot *compile(const char *g) {
    ParserSnapshot *s = NULL;
    char *err = NULL;
    if (lime_compile_grammar_in_process(g, strlen(g), &s, &err) != 0) {
        fprintf(stderr, "compile failed: %s\n", err ? err : "?");
        free(err);
        return NULL;
    }
    return s;
}

/* Drive a token sequence; return 1=accept, <0=error, 0=mid-parse. */
static int parse_seq(ParserSnapshot *snap, const int *toks, int n) {
    ParseContext *ctx = parse_begin(snap);
    if (!ctx) return -1;
    int rc = 0;
    for (int i = 0; i < n; i++) {
        rc = parse_token(ctx, toks[i], NULL, -1);
        if (rc < 0 || rc == 1) break;
    }
    if (rc == 0) rc = parse_token(ctx, 0, NULL, -1);
    parse_end(ctx);
    return rc;
}

/* Letter 37: host_reduce dispatcher for the unit-production compose
** test.  Mirrors hu_grammar's arithmetic so we can SEE which rulenos
** fire.  Internal rulenos for the grammar below (declaration order):
**   0: prog ::= list
**   1: list ::= attr             -> L = A + 1000   (the UNIT rule)
**   2: list ::= list COMMA attr  -> L = B + A       (cons)
**   3: attr ::= ATTR             -> A = T
** value type is intptr_t boxed in void*. */
static int ul_host_reduce(void *u, int ruleno, const void *vals, const int *locs, int n,
                          void *out, int *lo) {
    (void)u; (void)locs; (void)lo; (void)n;
    void *const *v = (void *const *)vals;
    intptr_t r = 0;
    switch (ruleno) {
        case 0: r = (intptr_t)v[0]; break;                  /* prog ::= list */
        case 1: r = (intptr_t)v[0] + 1000; break;           /* list ::= attr (UNIT) */
        case 2: r = (intptr_t)v[0] + (intptr_t)v[2]; break; /* cons */
        case 3: r = (intptr_t)v[0]; break;                  /* attr ::= ATTR */
        default: r = 0; break;
    }
    *(void **)out = (void *)r;
    return 0;
}

/* Parse with a session host_reduce bound; leaf ATTR carries value 5. */
static intptr_t ul_run(ParserSnapshot *snap, const int *toks, int n) {
    ParseContext *ctx = parse_begin(snap);
    if (!ctx) return -1;
    parse_set_host_reduce(ctx, ul_host_reduce, NULL);
    int rc = 0;
    for (int i = 0; i < n; i++) {
        rc = parse_token(ctx, toks[i], (void *)(intptr_t)5, -1);
        if (rc < 0 || rc == 1) break;
    }
    if (rc == 0) rc = parse_token(ctx, 0, NULL, -1);
    intptr_t res = (intptr_t)parse_result(ctx);
    parse_end(ctx);
    return res;
}

/* Q3 (Letter 35): per-backend snapshot safety.  Each thread parses a
** SHARED composed snapshot concurrently -- the "one snapshot per
** backend, swapped by pointer" model.  A ParserSnapshot is read-only
** during a parse (the engine only reads its tables; the sole written
** field is the atomic refcount), so this must run race-free under TSan
** with every parse succeeding. */
#define Q3_THREADS 8
#define Q3_ITERS   1500
typedef struct {
    ParserSnapshot *snap;
    const int *toks;
    int ntoks;
    int failures;
} Q3Arg;

static void *q3_worker(void *p) {
    Q3Arg *a = (Q3Arg *)p;
    for (int i = 0; i < Q3_ITERS; i++) {
        ParserSnapshot *ref = snapshot_acquire(a->snap); /* per-backend pin */
        if (parse_seq(ref, a->toks, a->ntoks) != 1) a->failures++;
        snapshot_release(ref);
    }
    return NULL;
}

int main(void) {
    int fail = 0;

    ParserSnapshot *my = compile(k_mysql);
    ParserSnapshot *ora = compile(k_oracle);
    CHECK(my != NULL, "compile MySQL-ish dialect");
    CHECK(ora != NULL, "compile Oracle-ish dialect");
    if (!my || !ora) return 1;

    /* ---- Tier 1: divergent statement resolves to the right dialect -- */
    {
        /* "SELECT * FROM t LIMIT n" -- only MySQL accepts the LIMIT
        ** clause; Oracle errors at LIMIT.  Both share the first 4
        ** tokens, so a 1-token peek at SELECT can't tell them apart;
        ** full-statement simulation can. */
        int toks[] = { T_SELECT, T_STAR, T_FROM, T_NAME, T_LIMIT, T_NAME };
        LimeForkCandidate cands[2] = {
            { my,  0, 1 },   /* ext_id 1 */
            { ora, 0, 2 },   /* ext_id 2 */
        };
        LimeForkRank ranks[2];
        int w = lime_mg_resolve(cands, 2, toks, 6, NULL, 0, 0, ranks);
        CHECK(w == 0, "Tier1: LIMIT stream -> MySQL dialect (index 0) wins");
        CHECK(ranks[0].trial.reached_accept, "Tier1: MySQL reached accept");
        CHECK(!ranks[1].trial.reached_accept, "Tier1: Oracle did NOT accept LIMIT");

        /* And the mirror: "SELECT * FROM t ROWS n" -> Oracle. */
        int toks2[] = { T_SELECT, T_STAR, T_FROM, T_NAME, T_ROWS, T_NAME };
        int w2 = lime_mg_resolve(cands, 2, toks2, 6, NULL, 0, 0, NULL);
        CHECK(w2 == 1, "Tier1: ROWS stream -> Oracle dialect (index 1) wins");
    }

    /* ---- Tier 1 tie: both dialects accept the shared prefix --------- */
    {
        /* "SELECT * FROM t" is accepted IDENTICALLY by both -- a true
        ** Tier-1 tie (same accept, same tokens, same errors). */
        int toks[] = { T_SELECT, T_STAR, T_FROM, T_NAME };
        LimeForkCandidate cands[2] = { { my, 0, 1 }, { ora, 0, 2 } };
        LimeForkRank ranks[2];
        int w = lime_mg_resolve(cands, 2, toks, 4, NULL, 0, 0, ranks);
        CHECK(ranks[0].tied_winner && ranks[1].tied_winner,
              "Tier1: shared SELECT*FROM is a tie (both accept)");
        /* With no Bayesian store and equal priority, lowest ext_id wins
        ** deterministically. */
        CHECK(w == 0, "Tier1 tie: deterministic fallback picks lowest ext_id");

        /* ---- Tier 2: the Bayesian store breaks the same tie ---------- */
        LimeBayesStore *bs = lime_bayes_create();
        CHECK(bs != NULL, "Tier2: create bayes store");
        /* Teach it that ext_id 2 (Oracle) is the right call here. */
        for (int i = 0; i < 5; i++) lime_bayes_observe(bs, 0, T_SELECT, 2, true);
        lime_bayes_observe(bs, 0, T_SELECT, 1, false);
        int wb = lime_mg_resolve(cands, 2, toks, 4, bs, 0, T_SELECT, NULL);
        CHECK(wb == 1, "Tier2: learned posterior flips the tie to Oracle (ext 2)");

        /* Round-trip the store. */
        size_t need = lime_bayes_serialize(bs, NULL, 0);
        CHECK(need > 0, "Tier2: serialize reports a size");
        void *blob = malloc(need);
        size_t wrote = lime_bayes_serialize(bs, blob, need);
        CHECK(wrote == need, "Tier2: serialize writes the reported size");
        LimeBayesStore *bs2 = lime_bayes_deserialize(blob, wrote);
        CHECK(bs2 != NULL, "Tier2: deserialize round-trips");
        int wb2 = lime_mg_resolve(cands, 2, toks, 4, bs2, 0, T_SELECT, NULL);
        CHECK(wb2 == 1, "Tier2: restored store still flips the tie to Oracle");
        free(blob);
        lime_bayes_destroy(bs);
        lime_bayes_destroy(bs2);
    }

    /* ---- Tier 3: dialect registry + @sigil -------------------------- */
    {
        LimeDialectRegistry *reg = lime_dialect_registry_create();
        CHECK(reg != NULL, "Tier3: create dialect registry");
        CHECK(lime_dialect_register(reg, "mysql", my), "Tier3: register mysql");
        CHECK(lime_dialect_register(reg, "oracle", ora), "Tier3: register oracle");

        CHECK(lime_dialect_select(reg, "mysql") == my, "Tier3: select mysql");
        CHECK(lime_dialect_select(reg, "oracle") == ora, "Tier3: select oracle");
        CHECK(lime_dialect_select(reg, "duckdb") == NULL, "Tier3: unknown dialect -> NULL");

        /* @sigil parsing. */
        ParserSnapshot *sel = NULL;
        size_t off = 0;
        bool got = lime_dialect_parse_sigil(reg, "@oracle SELECT * FROM t", &sel, &off);
        CHECK(got && sel == ora, "Tier3: @oracle sigil selects Oracle");
        CHECK(off == 8, "Tier3: sigil offset points past '@oracle '");

        /* No sigil -> false, offset 0 (caller uses a default). */
        ParserSnapshot *sel2 = NULL; size_t off2 = 99;
        bool got2 = lime_dialect_parse_sigil(reg, "SELECT * FROM t", &sel2, &off2);
        CHECK(!got2 && sel2 == NULL && off2 == 0, "Tier3: no sigil -> default path");

        /* Unknown dialect sigil -> false (caller falls back). */
        bool got3 = lime_dialect_parse_sigil(reg, "@duckdb SELECT 1", &sel2, &off2);
        CHECK(!got3, "Tier3: unknown @dialect -> caller default");

        lime_dialect_registry_destroy(reg);
    }

    /* ---- Q3 (Letter 35): per-backend snapshot safety ---------------
    ** N threads each parse the SHARED composed `my` snapshot
    ** concurrently (acquire / parse / release), the per-backend model.
    ** Under TSan this proves a composed snapshot is read-only during a
    ** parse and concurrently usable by pointer with no shared mutable
    ** state beyond the atomic refcount. */
    {
        int toks[] = { T_SELECT, T_STAR, T_FROM, T_NAME };
        Q3Arg args[Q3_THREADS];
        pthread_t th[Q3_THREADS];
        for (int i = 0; i < Q3_THREADS; i++) {
            args[i].snap = my;
            args[i].toks = toks;
            args[i].ntoks = 4;
            args[i].failures = 0;
        }
        int spawned = 0;
        for (int i = 0; i < Q3_THREADS; i++) {
            if (pthread_create(&th[i], NULL, q3_worker, &args[i]) == 0) spawned++;
            else break;
        }
        for (int i = 0; i < spawned; i++) pthread_join(th[i], NULL);
        CHECK(spawned == Q3_THREADS, "Q3: spawned all worker threads");
        int total_fail = 0;
        for (int i = 0; i < spawned; i++) total_fail += args[i].failures;
        CHECK(total_fail == 0,
              "Q3: concurrent parses of a shared composed snapshot all succeed");
    }

    snapshot_release(my);
    snapshot_release(ora);

    /* ---- Q1 (Letter 35): compose-time conflict reporting ----------
    ** A genuine reduce/reduce conflict (duplicate production: same RHS,
    ** same LHS) and a shift/reduce conflict are resolved SILENTLY by
    ** lemon (keep-first / keep-shift) -- the plain compile returns 0
    ** and builds a snapshot.  The _ex variant surfaces the conflict
    ** count so the host can refuse / warn instead of shipping a
    ** silently mis-parsing grammar. */
    {
        const char *clean =
            "%name OK\n%token A B.\n%start_symbol s\ns ::= A B.\n";
        /* Dangling-else: a genuine, lemon-counted shift/reduce conflict,
        ** resolved keep-shift -- builds successfully, nconflict > 0. */
        const char *sr =
            "%name DE\n%token IF THEN ELSE X.\n%start_symbol prog\n"
            "prog ::= stmt.\n"
            "stmt ::= IF X THEN stmt.\n"
            "stmt ::= IF X THEN stmt ELSE stmt.\n"
            "stmt ::= X.\n";
        /* Two nonterminals deriving A both reachable before A: a
        ** reduce/reduce that makes one rule unreducible -- this one
        ** HARD-FAILS the in-process compile (rc != 0, error string),
        ** because lemon flags the unreducible rule via errorcnt. */
        const char *rr =
            "%name RR2\n%token A.\n%start_symbol prog\n"
            "prog ::= a A.\nprog ::= b A.\na ::= A.\nb ::= A.\n";

        ParserSnapshot *s = NULL; char *e = NULL; int nc = -1;

        int rc = lime_compile_grammar_in_process_ex(clean, strlen(clean), &s, &e, &nc);
        CHECK(rc == 0 && s != NULL, "Q1: clean grammar compiles");
        CHECK(nc == 0, "Q1: clean grammar reports 0 conflicts");
        free(e); e = NULL; if (s) snapshot_release(s); s = NULL;

        /* Shift/reduce: silently resolved -> builds, but the conflict
        ** count is now SURFACED so the host can warn/refuse. */
        nc = -1;
        rc = lime_compile_grammar_in_process_ex(sr, strlen(sr), &s, &e, &nc);
        CHECK(rc == 0 && s != NULL, "Q1: shift/reduce still builds (resolved keep-shift)");
        CHECK(nc > 0, "Q1: shift/reduce conflict count is surfaced (> 0)");
        free(e); e = NULL; if (s) snapshot_release(s); s = NULL;

        /* Reduce/reduce that renders a rule unreducible: HARD error,
        ** reported via rc != 0 + a non-NULL error string (not silent). */
        nc = -1;
        rc = lime_compile_grammar_in_process_ex(rr, strlen(rr), &s, &e, &nc);
        CHECK(rc != 0 && s == NULL, "Q1: unreducible reduce/reduce hard-fails");
        CHECK(e != NULL && e[0] != '\0', "Q1: hard failure carries an error string");
        free(e); e = NULL; if (s) snapshot_release(s); s = NULL;
    }

    /* ---- Letter 37: unit-production reduce on the IN-PROCESS COMPOSE
    ** path (not AOT).  The same unit rule that fires on the AOT path
    ** (v1.8.2 hu_grammar test) must also fire when the grammar is
    ** compiled by lime_compile_grammar_in_process_ex with a host_reduce
    ** bound -- otherwise a type-changing unit rule (Node* -> List*)
    ** leaves the wrong value on the stack for the parent rule. ------- */
    {
        const char *g =
            "%name UL\n%token_type {void*}\n"
            "%token ATTR COMMA.\n%start_symbol prog\n"
            "prog ::= list.\n"
            "list ::= attr.\n"               /* unit, type-changing in PG */
            "list ::= list COMMA attr.\n"    /* cons */
            "attr ::= ATTR.\n";
        ParserSnapshot *s = NULL; char *e = NULL; int nc = 0;
        int rc = lime_compile_grammar_in_process_ex(g, strlen(g), &s, &e, &nc);
        CHECK(rc == 0 && s != NULL, "L37: compose base+unit+cons in-process");
        free(e);
        if (s) {
            int one[] = { 1 /*ATTR*/ };
            CHECK(ul_run(s, one, 1) == 1005,
                  "L37: unit rule list::=attr FIRES on compose path (5+1000)");
            int two[] = { 1, 2 /*COMMA*/, 1 };
            CHECK(ul_run(s, two, 3) == 1010,
                  "L37: cons reads the List* the unit rule built (1005+5)");
            snapshot_release(s);
        }
    }

    printf("test_multi_grammar: %s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}
