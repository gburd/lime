/*
** test_extension_reduce_e2e.c -- does an extension's LimeReduceFn
** actually run at parse time?
**
** docs/EXTENSIONS.md says plainly (see the LimeReduceFn section,
** "Current status" paragraph) that the runtime engine does not yet
** invoke extension-supplied reduce callbacks -- the callback shape
** is type-checked at registration time only.  This test proves that
** claim end to end with a real grammar, a real extension, and a real
** parse, instead of relying on a source-reading argument.
**
** Strategy (mirrors tests/test_extension_rebuild.c's working
** rebuild flow):
**
**   1. Build a base snapshot from the tiny arithmetic grammar via
**      ArithBuildSnapshot() (embeds grammar_source via `lime -n`).
**
**   2. Register + load an extension that adds:
**        - a BANG token
**        - a rule  expr ::= expr BANG .
**      with a non-NULL `reduce` callback (`counting_reduce`) that
**      increments a counter and writes a sentinel into *lhs_out
**      whenever it fires.  `.code` is also supplied (documentation /
**      generator-time fallback per docs/EXTENSIONS.md) so the
**      modification is still serializable via
**      lime_modifications_to_grammar_text() and the subprocess
**      rebuild in publish_modified_snapshot() succeeds -- exactly
**      the same shape test_extension_rebuild.c exercises.
**
**   3. publish_modified_snapshot() rebuilds via the lime+cc
**      subprocess pipeline, producing a real ParserSnapshot whose
**      action tables include the new BANG token and rule.
**
**   4. Drive parse_token() with "NUM BANG EOF" -- input that must
**      reduce through the new rule to reach accept.
**
**   5. Check whether `counting_reduce` actually ran (counter > 0,
**      sentinel observed).  The parse itself is expected to ACCEPT
**      either way (the runtime engine performs the LR shift/reduce
**      transitions purely off the action tables; it does not need
**      the callback to know *that* a reduction happened, only the
**      callback tells us whether anything ran as a *result*).  What
**      this test asserts is whether the side effect fired -- that is
**      the actual claim being tested, not just "did the parser
**      accept".
**
** Exit codes (deliberately NOT the usual 0/1 pass/fail convention --
** see the comment above main() and the meson test registration in
** tests/meson.build for why):
**
**   0  Real success: parse accepted AND the reduce callback fired.
**      (If this ever happens, Goal B semantic-action dispatch has
**      landed -- update docs/EXTENSIONS.md's "Current status" note.)
**   1  Broken/crashed: something we did NOT expect went wrong (parse
**      unexpectedly rejected, snapshot build failed, extension APIs
**      returned false, etc). A real regression.
**   2  Confirmed gap: parse accepted (the table-driven engine is
**      fine) but the reduce callback never ran. This is the
**      documented, known state of the runtime today.
**   77 Skipped: `lime`/`cc` unavailable in this environment (the
**      subprocess rebuild pipeline could not even be attempted).
*/

#define _POSIX_C_SOURCE 200809L

#include "parser.h"
#include "extension.h"
#include "snapshot.h"
#include "snapshot_modify.h"
#include "parse_context.h"

#include "bench_arith_grammar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <io.h>
#define dup2 _dup2
#define close _close
#define open _open
#else
#include <unistd.h>
#endif

extern ParserSnapshot *ArithBuildSnapshot(void);

/* ------------------------------------------------------------------ */
/*  The extension: expr ::= expr BANG, with a real reduce callback.   */
/* ------------------------------------------------------------------ */

#define REDUCE_SENTINEL 0x1234

static int g_reduce_fire_count = 0;
static int g_last_nrhs = -1;
static void *g_lhs_out_seen = NULL;

/*
** counting_reduce -- the observable side effect a real extension
** would perform (build an AST node, run semantic checks, etc).  This
** test only needs proof-of-invocation, so it increments a counter
** and writes a recognizable sentinel through lhs_out.
*/
static void counting_reduce(void *user_data, void *extra_arg, int nrhs, const void *rhs_values,
                            const int *rhs_locs, void *lhs_out) {
    (void)extra_arg;
    (void)rhs_values;
    (void)rhs_locs;
    int *counter = (int *)user_data;
    if (counter != NULL) (*counter)++;
    g_reduce_fire_count++;
    g_last_nrhs = nrhs;
    g_lhs_out_seen = lhs_out;
    if (lhs_out != NULL) {
        *(int *)lhs_out = REDUCE_SENTINEL;
    }
}

static int g_ext_counter = 0;

static GrammarModification ext_mods[2];
static const char *fact_rhs[] = {"expr", "BANG", NULL};

static bool fact_get_modifications(void *user_data, const struct ParserSnapshot *base,
                                   GrammarModification **mods_out, uint32_t *nmods_out) {
    (void)user_data;
    (void)base;
    ext_mods[0].type = MOD_ADD_TOKEN;
    ext_mods[0].description = "reduce-e2e extension: add BANG terminal";
    ext_mods[0].u.add_token.name = "BANG";
    ext_mods[0].u.add_token.lexeme = "!";
    ext_mods[0].u.add_token.token_code = -1;

    ext_mods[1].type = MOD_ADD_RULE;
    ext_mods[1].description = "reduce-e2e extension: expr ::= expr BANG";
    ext_mods[1].u.add_rule.lhs = "expr";
    ext_mods[1].u.add_rule.rhs = fact_rhs;
    ext_mods[1].u.add_rule.nrhs = 2;
    /* `.code` is kept as a harmless placeholder so
    ** lime_modifications_to_grammar_text() can still serialize this
    ** rule for the subprocess rebuild (see mod_serialize.c:
    ** emit_add_rule() skips serialization when .reduce != NULL AND
    ** .code == NULL).  Per docs/EXTENSIONS.md, `.reduce` is supposed
    ** to take precedence over `.code` at parse time when both are
    ** set -- this test's whole point is to check whether that
    ** actually happens. */
    ext_mods[1].u.add_rule.code = "{ /* generator-time fallback, not the point of this test */ }";
    ext_mods[1].u.add_rule.reduce = counting_reduce;
    ext_mods[1].u.add_rule.reduce_user = &g_ext_counter;
    ext_mods[1].u.add_rule.precedence = -1;

    *mods_out = ext_mods;
    *nmods_out = 2;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static bool tool_available(const char *cmd) {
    char buf[256];
    snprintf(buf, sizeof(buf), "command -v %s >/dev/null 2>&1", cmd);
    return system(buf) == 0;
}

/* Token codes, same declaration-order convention as
** test_extension_rebuild.c: NUM=1, PLUS=2, MINUS=3, STAR=4, SLASH=5,
** LP=6, RP=7, and the extension-added BANG=8. */
#define TOK_NUM  1
#define TOK_BANG 8

int main(void) {
    printf("Lime extension reduce-callback dispatch end-to-end test\n");
    printf("=========================================================\n");

    if (!tool_available("cc")) {
        printf("  [SKIP] cc not on PATH; run inside `nix develop` to enable.\n");
        return 77;
    }
    struct stat st;
    if (stat("builddir/lime", &st) == 0) {
        setenv("LIME_BIN", "builddir/lime", 1);
    } else if (stat("../builddir/lime", &st) == 0) {
        setenv("LIME_BIN", "../builddir/lime", 1);
    } else if (!tool_available("lime")) {
        printf("  [SKIP] lime not in builddir/ or on PATH; "
               "build with `ninja -C builddir lime` first.\n");
        return 77;
    }
    if (stat("limpar.c", &st) == 0) {
        setenv("LIME_TEMPLATE", "limpar.c", 1);
    } else if (stat("../limpar.c", &st) == 0) {
        setenv("LIME_TEMPLATE", "../limpar.c", 1);
    }

    /* Step 1: base snapshot. */
    ParserSnapshot *base = ArithBuildSnapshot();
    if (base == NULL) {
        fprintf(stderr, "  [BROKEN] ArithBuildSnapshot returned NULL\n");
        return 1;
    }
    if (base->grammar_source == NULL || base->grammar_source_len == 0) {
        fprintf(stderr, "  [BROKEN] base snapshot has no grammar_source; "
                        "subprocess rebuild path cannot run\n");
        snapshot_release(base);
        return 1;
    }

    /* Step 2: register + load the extension. */
    ExtensionRegistry *reg = create_extension_registry();
    if (reg == NULL) {
        fprintf(stderr, "  [BROKEN] create_extension_registry failed\n");
        snapshot_release(base);
        return 1;
    }

    ExtensionInfo info = {
        .name = "reduce-callback-e2e",
        .version = "1.0.0",
        .get_modifications = fact_get_modifications,
    };
    ExtensionID id = 0;
    if (!register_extension(reg, &info, &id)) {
        fprintf(stderr, "  [BROKEN] register_extension failed\n");
        destroy_extension_registry(reg);
        snapshot_release(base);
        return 1;
    }

    char *err = NULL;
    if (!load_extension(reg, id, base, &err)) {
        fprintf(stderr, "  [BROKEN] load_extension failed: %s\n", err ? err : "(none)");
        free(err);
        destroy_extension_registry(reg);
        snapshot_release(base);
        return 1;
    }
    free(err);
    err = NULL;

    /* Step 3: publish (rebuild) the modified snapshot. */
    ParserSnapshot *modified = NULL;
    ConflictSet *cs = NULL;
    bool published = publish_modified_snapshot(reg, base, &modified, &cs, &err);
    if (cs != NULL) {
        printf("  conflicts: %u\n", cs->count);
        conflict_set_destroy(cs);
    }
    if (!published || modified == NULL) {
        fprintf(stderr, "  [BROKEN] publish_modified_snapshot failed: %s\n",
                err ? err : "(none)");
        free(err);
        destroy_extension_registry(reg);
        snapshot_release(base);
        return 1;
    }
    free(err);
    err = NULL;

    printf("  Modified snapshot: states=%u terminals=%u rules=%u "
           "action_count=%u ntoken=%u\n",
           modified->nstate, modified->nterminal, modified->nrule, modified->action_count,
           modified->yy_ntoken);

    if (modified->nterminal <= base->nterminal || modified->nrule <= base->nrule) {
        fprintf(stderr, "  [BROKEN] modified snapshot did not gain the new "
                        "token/rule as expected\n");
        snapshot_release(modified);
        destroy_extension_registry(reg);
        snapshot_release(base);
        return 1;
    }

    /* Step 4: parse "NUM BANG EOF" -- must go through the new rule to
    ** reach accept. */
    g_reduce_fire_count = 0;
    g_ext_counter = 0;
    g_last_nrhs = -1;
    g_lhs_out_seen = NULL;

    ParseContext *ctx = parse_begin(modified);
    if (ctx == NULL) {
        fprintf(stderr, "  [BROKEN] parse_begin returned NULL\n");
        snapshot_release(modified);
        destroy_extension_registry(reg);
        snapshot_release(base);
        return 1;
    }

    int rc = parse_token(ctx, TOK_NUM, NULL, -1);
    if (rc == 0) rc = parse_token(ctx, TOK_BANG, NULL, -1);
    if (rc == 0) rc = parse_token(ctx, 0, NULL, -1); /* EOF */
    parse_end(ctx);

    bool parse_accepted = (rc == 1);
    bool callback_fired = (g_reduce_fire_count > 0 && g_ext_counter > 0);

    printf("\n  parse result: rc=%d (%s)\n", rc, parse_accepted ? "ACCEPT" : "REJECT/ERROR");
    printf("  reduce callback fired: %s (count=%d, ext_counter=%d, last_nrhs=%d)\n",
           callback_fired ? "YES" : "NO", g_reduce_fire_count, g_ext_counter, g_last_nrhs);

    snapshot_release(modified);
    destroy_extension_registry(reg);
    snapshot_release(base);

    if (!parse_accepted) {
        /* We built a grammar specifically so that "NUM BANG" is only
        ** parseable via the new rule.  If the table-driven engine
        ** can't even get through the shift/reduce sequence, that is
        ** a real regression in the runtime engine or the rebuild
        ** pipeline -- not the known reduce-dispatch gap. */
        fprintf(stderr,
                "\n  [BROKEN] parser rejected \"NUM BANG\" even though the rebuilt "
                "snapshot has the new rule reachable -- this is NOT the known "
                "reduce-dispatch gap, something else regressed.\n");
        return 1;
    }

    if (callback_fired) {
        printf("\n  RESULT: extension reduce callback WAS invoked by the runtime "
               "engine.\n");
        printf("  Goal B semantic-action dispatch appears to be wired up -- if you "
               "see\n  this message, update docs/EXTENSIONS.md's LimeReduceFn "
               "\"Current status\"\n  paragraph, it is now stale.\n");
        return 0;
    }

    printf("\n  CONFIRMED GAP: extension reduce callback was never invoked "
          "(Goal B semantic-action dispatch not yet wired). Parse accepted via "
          "the table-driven engine alone; src/parse_engine.c's reduce() has no "
          "path to a GrammarModification's `reduce`/`reduce_user` pair -- that "
          "data isn't even retained on the ParserSnapshot after "
          "publish_modified_snapshot() runs. See docs/REDUCE_DISPATCH.md.\n");
    return 2;
}
