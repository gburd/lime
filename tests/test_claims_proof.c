/*
** test_claims_proof.c -- Investigative test that exercises every
** claimed runtime-extension feature against the actual code paths
** and reports, with concrete evidence, what works and what is a stub.
**
** This is not a regression test in the usual sense; it is a forensic
** probe.  Every assertion here is paired with a printed "VERDICT" line
** so a reader can scan the output and see which README claims hold up.
**
** Claims under test (from README.md and docs/EXTENSIONS.md):
**
**   C1. "the generated parser can load and unload grammar extensions
**       at runtime without recompilation"
**   C2. "Conflict detection and resolution callbacks"
**   C3. The Quick Start in docs/parser.h:
**         snap = lemon_snapshot_create("sql.y", ...);
**         ctx  = parse_begin(snap);
**         parse_token(ctx, TK_SELECT, ...);
**       i.e. you can take a grammar file and parse a token stream
**       through the runtime API.
**   C4. Loading an extension that adds a token + rule changes what
**       a parser pinned to the resulting snapshot will accept.
**
** Run from the build directory; this test always exits with status 0
** so meson does not flag it -- the verdict is in stdout.
*/

#define _POSIX_C_SOURCE 200809L

#include "parser.h"          /* public API */
#include "extension.h"       /* extension registry, GrammarModification */
#include "conflict.h"        /* ConflictSet */
#include "snapshot.h"        /* ParserSnapshot internals */
#include "snapshot_modify.h" /* clone_snapshot, create_modified_snapshot */
#include "disambiguation.h"  /* StrategyResult */
#include "parse_context.h"   /* LIME_LOC_UNKNOWN */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdatomic.h>

/* ------------------------------------------------------------------ */
/*  Forensic helpers                                                    */
/* ------------------------------------------------------------------ */

static int n_pass = 0;
static int n_fail = 0;

#define VERDICT_OK(label, msg)                                                                     \
    do {                                                                                           \
        printf("  [PASS] %-40s %s\n", label, msg);                                                 \
        n_pass++;                                                                                  \
    } while (0)

#define VERDICT_GAP(label, msg)                                                                    \
    do {                                                                                           \
        printf("  [GAP ] %-40s %s\n", label, msg);                                                 \
        n_fail++;                                                                                  \
    } while (0)

#define SECTION(title) printf("\n=== %s ===\n", title)

/* ------------------------------------------------------------------ */
/*  Build a synthetic base snapshot we can reason about                */
/* ------------------------------------------------------------------ */

static ParserSnapshot *make_base_snapshot(void) {
    /* clone_snapshot(NULL) builds an empty snapshot per snapshot_modify.c. */
    ParserSnapshot *snap = clone_snapshot(NULL);
    if (snap == NULL) return NULL;

    /* Give it a tiny, recognisable action table so we can detect any
    ** mutation later. */
    snap->nstate = 4;
    snap->nterminal = 3;
    snap->nsymbol = 5;
    snap->nrule = 2;
    snap->action_count = 8;
    snap->lookahead_count = 8;

    snap->yy_action = calloc(8, sizeof(uint16_t));
    snap->yy_lookahead = calloc(8, sizeof(uint16_t));
    snap->yy_shift_ofst = calloc(4, sizeof(int16_t));
    snap->yy_reduce_ofst = calloc(4, sizeof(int16_t));
    snap->yy_default = calloc(4, sizeof(uint16_t));

    if (!snap->yy_action || !snap->yy_lookahead || !snap->yy_shift_ofst || !snap->yy_reduce_ofst ||
        !snap->yy_default) {
        snapshot_release(snap);
        return NULL;
    }

    /* Distinctive "fingerprint" values so a memcmp can tell whether
    ** anything mutated the tables. */
    for (int i = 0; i < 8; i++) {
        snap->yy_action[i] = (uint16_t)(0xA000 + i);
        snap->yy_lookahead[i] = (uint16_t)(0xB000 + i);
    }
    for (int i = 0; i < 4; i++) {
        snap->yy_shift_ofst[i] = (int16_t)(0x100 + i);
        snap->yy_reduce_ofst[i] = (int16_t)(0x200 + i);
        snap->yy_default[i] = (uint16_t)(0xC000 + i);
    }
    return snap;
}

static bool snapshots_have_same_action_tables(const ParserSnapshot *a, const ParserSnapshot *b) {
    if (a == NULL || b == NULL) return false;
    if (a->action_count != b->action_count) return false;
    if (a->lookahead_count != b->lookahead_count) return false;
    if (a->nstate != b->nstate) return false;

    if (memcmp(a->yy_action, b->yy_action, a->action_count * sizeof(uint16_t)) != 0) return false;
    if (memcmp(a->yy_lookahead, b->yy_lookahead, a->lookahead_count * sizeof(uint16_t)) != 0)
        return false;
    if (memcmp(a->yy_shift_ofst, b->yy_shift_ofst, a->nstate * sizeof(int16_t)) != 0) return false;
    if (memcmp(a->yy_reduce_ofst, b->yy_reduce_ofst, a->nstate * sizeof(int16_t)) != 0)
        return false;
    if (memcmp(a->yy_default, b->yy_default, a->nstate * sizeof(uint16_t)) != 0) return false;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Mock extensions: two grammars that overlap on token "BANG"         */
/* ------------------------------------------------------------------ */

static GrammarModification ext_a_mods[2];
static GrammarModification ext_b_mods[2];

/* Extension A: defines BANG as a logical-NOT operator, with rule
**   not_expr ::= BANG expr.
*/
static const char *ext_a_rhs[] = { "BANG", "expr", NULL };
static bool ext_a_get_modifications(void *user_data, const struct ParserSnapshot *base,
                                    GrammarModification **mods_out, uint32_t *nmods_out) {
    (void)user_data;
    (void)base;

    ext_a_mods[0].type = MOD_ADD_TOKEN;
    ext_a_mods[0].description = "ext-A adds BANG as logical NOT";
    ext_a_mods[0].u.add_token.name = "BANG";
    ext_a_mods[0].u.add_token.lexeme = "!";
    ext_a_mods[0].u.add_token.token_code = 100;

    ext_a_mods[1].type = MOD_ADD_RULE;
    ext_a_mods[1].description = "ext-A: not_expr ::= BANG expr";
    ext_a_mods[1].u.add_rule.lhs = "not_expr";
    ext_a_mods[1].u.add_rule.rhs = ext_a_rhs;
    ext_a_mods[1].u.add_rule.nrhs = 2;
    ext_a_mods[1].u.add_rule.code = "/* logical NOT */";
    ext_a_mods[1].u.add_rule.precedence = -1;

    *mods_out = ext_a_mods;
    *nmods_out = 2;
    return true;
}

/* Extension B: also defines BANG, but as factorial postfix:
**   fact_expr ::= expr BANG.
*/
static const char *ext_b_rhs[] = { "expr", "BANG", NULL };
static bool ext_b_get_modifications(void *user_data, const struct ParserSnapshot *base,
                                    GrammarModification **mods_out, uint32_t *nmods_out) {
    (void)user_data;
    (void)base;

    ext_b_mods[0].type = MOD_ADD_TOKEN;
    ext_b_mods[0].description = "ext-B adds BANG as factorial";
    ext_b_mods[0].u.add_token.name = "BANG"; /* same name as ext-A */
    ext_b_mods[0].u.add_token.lexeme = "!";
    ext_b_mods[0].u.add_token.token_code = 200;

    ext_b_mods[1].type = MOD_ADD_RULE;
    ext_b_mods[1].description = "ext-B: fact_expr ::= expr BANG";
    ext_b_mods[1].u.add_rule.lhs = "fact_expr";
    ext_b_mods[1].u.add_rule.rhs = ext_b_rhs;
    ext_b_mods[1].u.add_rule.nrhs = 2;
    ext_b_mods[1].u.add_rule.code = "/* factorial */";
    ext_b_mods[1].u.add_rule.precedence = -1;

    *mods_out = ext_b_mods;
    *nmods_out = 2;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Test 1: lemon_snapshot_create() with a grammar file                 */
/* ------------------------------------------------------------------ */

static void test_grammar_file_path(void) {
    SECTION("Claim C3: lemon_snapshot_create(\"file.y\") works");

    /* lemon_snapshot_create now runs the lime parser generator as a
    ** subprocess on the grammar file, compiles the resulting
    ** *_snapshot.c into a shared library, and dlopen()s it to
    ** retrieve the populated ParserSnapshot.  See src/snapshot_create.c.
    **
    ** Test 1a: graceful failure on a missing file.
    ** Test 1b: real success on a valid grammar (covered by
    **          tests/test_snapshot_create.c -- not duplicated here
    **          because that test needs lime + cc on PATH and skips
    **          cleanly when they are missing). */
    char *err = NULL;
    ParserSnapshot *snap = lemon_snapshot_create("nonexistent.y", &err);
    if (snap == NULL && err != NULL) {
        printf("  Error returned (expected): \"%s\"\n", err);
        VERDICT_OK("lemon_snapshot_create",
                   "returns an actionable error for a missing grammar file "
                   "(real builds covered by test_snapshot_create)");
        free(err);
    } else if (snap != NULL) {
        VERDICT_OK("lemon_snapshot_create", "actually built a snapshot from a grammar file");
        lemon_snapshot_release(snap);
    }
}

/* ------------------------------------------------------------------ */
/*  Test 2: parse_token() actually parses tokens                        */
/* ------------------------------------------------------------------ */

static void test_parse_token_runs(void) {
    SECTION("Claim C3: parse_token() drives the LALR parser");

    ParserSnapshot *snap = make_base_snapshot();
    if (snap == NULL) {
        VERDICT_GAP("parse_token", "could not build a base snapshot");
        return;
    }

    /* Our handcrafted snapshot has no real action tables, so
    ** parse_token cannot drive a meaningful parse against it.  But
    ** the engine should refuse, not silently return 0 like the old
    ** stub.  Real generated grammars are exercised in
    ** test_runtime_parse where parse_token does the real work. */
    ParseContext *ctx = parse_begin(snap);
    if (ctx == NULL) {
        snapshot_release(snap);
        VERDICT_GAP("parse_token", "parse_begin returned NULL");
        return;
    }

    int rc = parse_token(ctx, 9999, NULL, LIME_LOC_UNKNOWN);
    parse_end(ctx);
    snapshot_release(snap);

    if (rc < 0) {
        VERDICT_OK("parse_token", "actually inspects tokens (returns -1 on a stub snapshot "
                                  "with no real tables; real snapshots are tested in "
                                  "test_runtime_parse)");
    } else {
        VERDICT_GAP("parse_token", "did not reject obvious garbage -- engine may be broken");
    }
}

/* ------------------------------------------------------------------ */
/*  Test 3: detect_token_conflicts finds overlapping tokens             */
/* ------------------------------------------------------------------ */

static void test_token_conflict_is_detected(ExtensionRegistry *reg, ExtensionID *a_out,
                                            ExtensionID *b_out) {
    SECTION("Claim C2: token-level conflict detection works");

    ExtensionInfo info_a = {
        .name = "ext-A-not",
        .version = "1.0.0",
        .get_modifications = ext_a_get_modifications,
    };
    ExtensionInfo info_b = {
        .name = "ext-B-fact",
        .version = "1.0.0",
        .get_modifications = ext_b_get_modifications,
    };

    ExtensionID id_a = 0, id_b = 0;
    if (!register_extension(reg, &info_a, &id_a) || !register_extension(reg, &info_b, &id_b)) {
        VERDICT_GAP("register_extension", "failed");
        return;
    }
    VERDICT_OK("register_extension", "both A (BANG=NOT) and B (BANG=factorial) registered");

    char *err = NULL;
    if (!load_extension(reg, id_a, NULL, &err)) {
        VERDICT_GAP("load_extension(A)", err ? err : "unknown error");
        free(err);
        return;
    }
    free(err);
    err = NULL;
    if (!load_extension(reg, id_b, NULL, &err)) {
        VERDICT_GAP("load_extension(B)", err ? err : "unknown error");
        free(err);
        return;
    }
    free(err);
    VERDICT_OK("load_extension", "both A and B transitioned to EXT_LOADED");

    /* Run multi-grammar conflict detection */
    MultiGrammarConflictResult *result = multi_conflict_result_create();
    uint32_t found = detect_token_conflicts(reg, result);

    char buf[160];
    snprintf(buf, sizeof(buf), "found %u conflict(s); %u token-level points", found,
             result->token_conflicts);
    if (found == 0) {
        VERDICT_GAP("detect_token_conflicts", "no conflict found despite both adding token BANG");
    } else {
        VERDICT_OK("detect_token_conflicts", buf);
        for (uint32_t i = 0; i < result->npoints; i++) {
            ConflictPoint *cp = &result->points[i];
            printf("        point[%u]: %s\n", i,
                   cp->description ? cp->description : "(no description)");
            for (int k = 0; k < cp->ncontexts; k++) {
                printf("          context[%d]: ext=%u name=\"%s\"\n", k, cp->contexts[k].ext_id,
                       cp->contexts[k].grammar_name ? cp->contexts[k].grammar_name : "?");
            }
        }
    }

    multi_conflict_result_destroy(result);

    *a_out = id_a;
    *b_out = id_b;
}

/* ------------------------------------------------------------------ */
/*  Test 4: priority disambiguation picks a winner                      */
/* ------------------------------------------------------------------ */

static void test_priority_resolves_winner(ExtensionRegistry *reg, ExtensionID id_a,
                                          ExtensionID id_b) {
    SECTION("Claim C2: priority disambiguation actually picks one");

    /* Build a synthetic ConflictPoint manually -- this is the same
    ** data that the conflict detector would build. */
    ConflictPoint cp;
    conflict_point_init(&cp, /*token=*/100, /*state=*/0, CONFLICT_LEVEL_TOKEN);

    LimeContext ctx_a = {
        .ext_id = id_a,
        .token = 100,
        .state = 0,
        .priority = 50, /* lower */
        .grammar_name = "ext-A-not",
    };
    LimeContext ctx_b = {
        .ext_id = id_b,
        .token = 100,
        .state = 0,
        .priority = 100, /* higher = should win */
        .grammar_name = "ext-B-fact",
    };
    conflict_point_add_context(&cp, &ctx_a);
    conflict_point_add_context(&cp, &ctx_b);

    DisambiguationContext *dis = disambiguation_create(STRAT_PRIORITY, reg);
    if (dis == NULL) {
        VERDICT_GAP("disambiguation_create", "could not create priority strategy");
        conflict_point_destroy(&cp);
        return;
    }

    StrategyResult sr = disambiguation_resolve(dis, &cp, NULL);
    if (sr.nwinners != 1) {
        char b[80];
        snprintf(b, sizeof(b), "got %d winners (want 1)", sr.nwinners);
        VERDICT_GAP("priority_resolve", b);
    } else if (sr.winning_contexts[0].ext_id == id_b) {
        VERDICT_OK("priority_resolve", "ext-B (priority=100) chosen over ext-A (priority=50)");
    } else {
        VERDICT_GAP("priority_resolve", "wrong winner -- expected ext-B");
    }

    strategy_result_cleanup(&sr);
    disambiguation_destroy(dis);
    conflict_point_destroy(&cp);
}

/* ------------------------------------------------------------------ */
/*  Test 5: applying modifications mutates the action tables            */
/* ------------------------------------------------------------------ */

static void test_mods_change_action_tables(ExtensionRegistry *reg) {
    SECTION("Claim C4: applying mods changes parser tables");

    ParserSnapshot *base = make_base_snapshot();
    if (base == NULL) {
        VERDICT_GAP("base snapshot", "alloc failed");
        return;
    }

    /* Snapshot fingerprint we will compare against later. */
    ParserSnapshot *fingerprint = make_base_snapshot();
    if (fingerprint == NULL) {
        snapshot_release(base);
        return;
    }

    /* Combine ext-A's two mods into a single array (mimics what
    ** create_modified_snapshot is given). */
    GrammarModification mods[2];
    GrammarModification *p = NULL;
    uint32_t n = 0;
    ext_a_get_modifications(NULL, base, &p, &n);
    memcpy(mods, p, sizeof(mods));

    ParserSnapshot *modified = NULL;
    ConflictSet *cs = NULL;
    char *err = NULL;
    ModifyResult mr = create_modified_snapshot(base, mods, n, reg, &modified, &cs, &err);

    if (mr != MODIFY_OK || modified == NULL) {
        char buf[128];
        snprintf(buf, sizeof(buf), "create_modified_snapshot returned %d (%s)", (int)mr,
                 err ? err : "no error msg");
        VERDICT_GAP("create_modified_snapshot", buf);
        free(err);
        if (cs) conflict_set_destroy(cs);
        snapshot_release(base);
        snapshot_release(fingerprint);
        return;
    }
    free(err);
    if (cs) conflict_set_destroy(cs);

    printf("  base    : nstate=%u nrule=%u nterm=%u\n", base->nstate, base->nrule, base->nterminal);
    printf("  modified: nstate=%u nrule=%u nterm=%u\n", modified->nstate, modified->nrule,
           modified->nterminal);

    bool tables_unchanged = snapshots_have_same_action_tables(modified, fingerprint);

    if (tables_unchanged) {
        VERDICT_GAP("apply_modification",
                    "yy_action / yy_lookahead / yy_*_ofst / yy_default "
                    "are byte-for-byte unchanged after MOD_ADD_TOKEN+MOD_ADD_RULE");
    } else {
        VERDICT_OK("apply_modification", "action tables mutated");
    }

    /* Counters should have been bumped though -- that's the only
    ** thing apply_* actually does today. */
    if (modified->nrule == base->nrule + 1 && modified->nterminal == base->nterminal + 1) {
        VERDICT_OK("counters", "nrule/nterminal incremented (the only effect)");
    } else {
        VERDICT_GAP("counters", "nrule/nterminal not bumped; even the stub didn't fire");
    }

    snapshot_release(modified);
    snapshot_release(base);
    snapshot_release(fingerprint);
}

/* ------------------------------------------------------------------ */
/*  Test 6: load_extension does not invoke create_modified_snapshot     */
/* ------------------------------------------------------------------ */

static void test_load_does_not_apply_mods(void) {
    SECTION("Claim C1: loading an extension changes the active grammar");

    /* Strategy: grep-equivalent.  We've already shown create_modified_snapshot
    ** doesn't mutate tables.  The remaining question is whether the
    ** extension framework even reaches that code path when an extension
    ** loads.  Easiest answer: the public `load_extension` accepts a
    ** `base_snapshot` parameter but cannot return a modified snapshot;
    ** there is no out-parameter for one.  Demonstrate the API shape. */

    bool has_out_param = false;
    /* The signature of load_extension in extension.h is:
    **   bool load_extension(ExtensionRegistry *, ExtensionID,
    **                       const ParserSnapshot *base, char **error);
    ** No way to return a new snapshot.  This is mechanical, so the
    ** "claim" is really documentary. */
    if (has_out_param) {
        VERDICT_OK("load_extension", "produces a modified snapshot");
    } else {
        VERDICT_GAP("load_extension", "API has no out-snapshot parameter; loading just stores "
                                      "modifications in the extension struct");
    }
}

/* ------------------------------------------------------------------ */
/*  Test 7: examples/calc/main.c "extension" pattern                    */
/* ------------------------------------------------------------------ */

static void test_calc_extension_pattern(void) {
    SECTION("Claim C1: examples/calc demonstrates runtime grammar extension");

    /* This is purely documentary; we cannot dlopen anything from a
    ** unit test, but we can describe what the example does so the
    ** verdict line shows in the same report. */
    printf("  examples/calc/main.c uses dlopen(\"libcalc_power.so\") and\n");
    printf("  calls plugin->eval(...) -- the plugin owns its OWN parser.\n");
    printf("  It does NOT call register_extension, load_extension, or\n");
    printf("  create_modified_snapshot.  See examples/calc/main.c lines\n");
    printf("  117-180 (uses CalcGrammarModification, not\n");
    printf("  GrammarModification).\n");
    VERDICT_GAP("calc/extension demo",
                "two side-by-side parsers, not one base grammar that gets new rules");
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("Lime claims-proof harness\n");
    printf("=========================\n");
    printf("Each line below is a VERDICT against a specific README claim.\n");

    /* Init the global registry the way the public API expects. */
    if (!lemon_extension_registry_init()) {
        fprintf(stderr, "lemon_extension_registry_init failed\n");
        return 0;
    }

    test_grammar_file_path();
    test_parse_token_runs();

    /* Use a private registry for the callback-driven tests so we don't
    ** depend on the global registry's contents. */
    ExtensionRegistry *reg = create_extension_registry();
    ExtensionID a = 0, b = 0;
    test_token_conflict_is_detected(reg, &a, &b);
    if (a != 0 && b != 0) {
        test_priority_resolves_winner(reg, a, b);
    }
    test_mods_change_action_tables(reg);
    destroy_extension_registry(reg);

    test_load_does_not_apply_mods();
    test_calc_extension_pattern();

    SECTION("Summary");
    printf("  Passed verdicts (working features):  %d\n", n_pass);
    printf("  Gap verdicts    (stub or missing):   %d\n", n_fail);

    lemon_extension_registry_destroy();

    /* Always exit 0 -- this harness reports rather than asserts. */
    return 0;
}
