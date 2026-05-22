/*
** test_extension_e2e.c -- end-to-end exercise of the runtime
** extension framework against a real generated parser snapshot.
**
** Drives the full pipeline:
**
**   1. Build a real ParserSnapshot via ArithBuildSnapshot()
**      (i.e. exactly what `lime -n grammar.y` emits).
**
**   2. Register two extensions that both add a token named "POW"
**      with conflicting interpretations.
**
**   3. Run publish_modified_snapshot() with ext-B's on_conflict
**      resolver returning CONFLICT_USE_NEW so the conflict resolves
**      and a modified snapshot is published.
**
**   4. Verify the produced snapshot has bumped counters
**      (showing modifications were dispatched).
**
**   5. Exercise parse_begin / parse_token / parse_end against the
**      modified snapshot to confirm parse stability after mutation.
*/

#include "parser.h"
#include "extension.h"
#include "snapshot.h"
#include "snapshot_modify.h"
#include "parse_context.h"

#include "bench_arith_grammar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern ParserSnapshot *ArithBuildSnapshot(void);

static int n_pass = 0, n_fail = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (cond) {                                                                                \
            printf("  [PASS] %s\n", msg);                                                          \
            n_pass++;                                                                              \
        } else {                                                                                   \
            printf("  [FAIL] %s\n", msg);                                                          \
            n_fail++;                                                                              \
        }                                                                                          \
    } while (0)

static GrammarModification ext_a_mods[1];
static GrammarModification ext_b_mods[1];

static bool ext_a_get_modifications(void *user_data, const struct ParserSnapshot *base,
                                    GrammarModification **mods_out, uint32_t *nmods_out) {
    (void)user_data;
    (void)base;
    ext_a_mods[0].type = MOD_ADD_TOKEN;
    ext_a_mods[0].description = "ext-A: POW (right-associative ^)";
    ext_a_mods[0].u.add_token.name = "POW";
    ext_a_mods[0].u.add_token.lexeme = "^";
    ext_a_mods[0].u.add_token.token_code = -1;
    *mods_out = ext_a_mods;
    *nmods_out = 1;
    return true;
}

static bool ext_b_get_modifications(void *user_data, const struct ParserSnapshot *base,
                                    GrammarModification **mods_out, uint32_t *nmods_out) {
    (void)user_data;
    (void)base;
    ext_b_mods[0].type = MOD_ADD_TOKEN;
    ext_b_mods[0].description = "ext-B: POW (factorial-ish !)";
    ext_b_mods[0].u.add_token.name = "POW";  /* Same name -- conflict */
    ext_b_mods[0].u.add_token.lexeme = "!";
    ext_b_mods[0].u.add_token.token_code = -1;
    *mods_out = ext_b_mods;
    *nmods_out = 1;
    return true;
}

/* ext-B's resolver wins all token conflicts -- use_new picks the
** newly-loaded extension. */
static ConflictResolution ext_b_on_conflict(void *user_data, const ConflictInfo *info) {
    (void)user_data;
    (void)info;
    return CONFLICT_USE_NEW;
}

int main(void) {
    printf("Lime runtime extension end-to-end test\n");
    printf("======================================\n");

    ParserSnapshot *base = ArithBuildSnapshot();
    CHECK(base != NULL, "ArithBuildSnapshot returned a real snapshot");
    if (base == NULL) return 1;

    uint32_t base_nrule = base->nrule;
    uint32_t base_nterm = base->nterminal;
    uint16_t base_ntoken = base->yy_ntoken;

    printf("\n--- Base snapshot ---\n");
    printf("  states=%u terminals=%u rules=%u action_count=%u ntoken=%u\n", base->nstate,
           base->nterminal, base->nrule, base->action_count, base->yy_ntoken);

    {
        ParseContext *ctx = parse_begin(base);
        int rc1 = parse_token(ctx, ARITH_NUM, NULL, -1);
        int rc2 = parse_token(ctx, 0, NULL, -1);
        parse_end(ctx);
        CHECK(rc1 == 0 && rc2 == 1, "base snapshot: parse_token accepts a single NUM");
    }

    ExtensionRegistry *reg = create_extension_registry();
    CHECK(reg != NULL, "create_extension_registry succeeded");

    ExtensionInfo info_a = {
        .name = "ext-A-pow",
        .version = "1.0.0",
        .get_modifications = ext_a_get_modifications,
    };
    ExtensionInfo info_b = {
        .name = "ext-B-pow",
        .version = "1.0.0",
        .get_modifications = ext_b_get_modifications,
        .on_conflict = ext_b_on_conflict,
    };

    ExtensionID a = 0, b = 0;
    CHECK(register_extension(reg, &info_a, &a), "registered ext-A");
    CHECK(register_extension(reg, &info_b, &b), "registered ext-B");

    char *err = NULL;
    CHECK(load_extension(reg, a, base, &err), "loaded ext-A");
    free(err);
    err = NULL;
    CHECK(load_extension(reg, b, base, &err), "loaded ext-B");
    free(err);
    err = NULL;

    ParserSnapshot *modified = NULL;
    ConflictSet *cs = NULL;
    bool published = publish_modified_snapshot(reg, base, &modified, &cs, &err);

    printf("\n--- publish_modified_snapshot ---\n");
    printf("  result=%s error=%s\n", published ? "OK" : "ERR", err ? err : "(none)");
    if (cs != NULL) {
        printf("  conflicts: %u\n", cs->count);
        conflict_set_destroy(cs);
    }
    free(err);
    err = NULL;

    CHECK(published && modified != NULL, "publish_modified_snapshot succeeded with resolver");

    if (modified != NULL) {
        CHECK(modified->nterminal > base_nterm, "modified snapshot's nterminal grew");
        CHECK(modified->yy_ntoken > base_ntoken, "modified snapshot's yy_ntoken grew");
        CHECK(modified->nrule == base_nrule,
              "modified snapshot's nrule unchanged (no MOD_ADD_RULE here)");

        ParseContext *ctx = parse_begin(modified);
        int rc1 = parse_token(ctx, ARITH_NUM, NULL, -1);
        int rc2 = parse_token(ctx, 0, NULL, -1);
        parse_end(ctx);
        CHECK(rc1 == 0 && rc2 == 1,
              "modified snapshot: parse_token still accepts a single NUM");

        snapshot_release(modified);
    }

    destroy_extension_registry(reg);
    snapshot_release(base);

    printf("\n=== Summary === Pass: %d Fail: %d\n", n_pass, n_fail);
    return n_fail == 0 ? 0 : 1;
}
