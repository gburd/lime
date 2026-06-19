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

#include <stdio.h>
#include <stdlib.h>
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

    snapshot_release(my);
    snapshot_release(ora);

    printf("test_multi_grammar: %s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}
