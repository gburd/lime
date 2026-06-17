/*
** tests/test_compose_first_token.c -- Lime-Letter (PG Track B) repro
** + regression: a composed snapshot must accept a valid BASE query,
** and every base terminal must keep its external token code across
** composition.
**
** PG reported "SELECT 1+2; -> rc=-1 against the composed snapshot
** while base-only accepts."  Their base grammar declares
** %first_token 257.  The arith rebuild test (first_token == 0) passes,
** so %first_token is the untested differentiator.  This test pins it:
**
**   1. token-code stability: each base terminal's external code in
**      the composed snapshot equals its code in the base snapshot
**      (probed via the admissibility oracle at the start state).
**   2. base query acceptance: a base-only token stream that the base
**      snapshot accepts is also accepted by the composed snapshot.
**
** The base snapshot is produced by `lime -n` from ft_base_grammar.lime
** (see tests/meson.build), which declares %first_token 257.
*/
#include "test_compat.h"

#include "parse_context.h"
#include "snapshot.h"
#include "parser.h"
#include "extension.h"
#include "conflict.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, msg) do {                                            \
    if (!(cond)) {                                                       \
        fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__);          \
        fail++;                                                          \
    } else {                                                            \
        printf("  ok: %s\n", msg);                                      \
    }                                                                   \
} while (0)

/* Built by `lime -n` from ft_base_grammar.lime (embeds grammar_source
** so publish_modified_snapshot takes the in-process rebuild path). */
extern ParserSnapshot *FtbBuildSnapshot(void);

/* External token codes from ft_base_grammar.h.  %first_token 257
** offsets external = internal + 257, and internal terminal indices
** start at 1 (index 0 is the implicit $ / EOF marker), so the first
** declared terminal SELECT is 258, NOT 257.  This off-by-one (the
** offset is added to a 1-based internal index) is exactly the kind of
** thing that bites composition consumers -- the generated .h is the
** source of truth. */
enum { FTB_SELECT = 258, FTB_NUM = 259, FTB_PLUS = 260, FTB_SEMI = 261 };

/* The extension adds one terminal (KW_EXT) and one rule using it. */
static const char *ext_rhs[] = { "expr", "KW_EXT", NULL };
static GrammarModification ext_mods[2];

static bool ext_get_mods(void *user, const struct ParserSnapshot *base,
                         GrammarModification **out, uint32_t *n) {
    (void)user; (void)base;
    ext_mods[0].type = MOD_ADD_TOKEN;
    ext_mods[0].description = "ext token KW_EXT";
    ext_mods[0].u.add_token.name = "KW_EXT";
    ext_mods[0].u.add_token.lexeme = "ext";
    ext_mods[0].u.add_token.token_code = -1;

    ext_mods[1].type = MOD_ADD_RULE;
    ext_mods[1].description = "expr ::= expr KW_EXT";
    ext_mods[1].u.add_rule.lhs = "expr";
    ext_mods[1].u.add_rule.rhs = ext_rhs;
    ext_mods[1].u.add_rule.nrhs = 2;
    ext_mods[1].u.add_rule.code = "{ (void)E; (void)L; }";
    ext_mods[1].u.add_rule.precedence = -1;

    *out = ext_mods;
    *n = 2;
    return true;
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

static int admissible(ParserSnapshot *s, int code) {
    return lime_token_admissible_in_state(s, 0, code) != LIME_TOK_NONE;
}

int main(void) {
    int fail = 0;

    /* The subprocess fallback (if path A is unavailable) needs lime +
    ** template; point them at the build tree as test_extension_rebuild
    ** does. */
    setenv("LIME_TEMPLATE", "limpar.c", 0);

    ParserSnapshot *base = FtbBuildSnapshot();
    CHECK(base != NULL, "FtbBuildSnapshot");
    if (!base) return 1;
    CHECK(lime_snapshot_first_token(base) == 257, "base carries %first_token 257");
    CHECK(base->grammar_source != NULL && base->grammar_source_len > 0,
          "base carries grammar_source (in-process rebuild path reachable)");

    /* Base accepts "SELECT NUM + NUM ;" at external codes. */
    int base_query[] = { FTB_SELECT, FTB_NUM, FTB_PLUS, FTB_NUM, FTB_SEMI };
    CHECK(parse_seq(base, base_query, 5) == 1,
          "base snapshot accepts SELECT NUM + NUM ;");

    /* Compose: register + load + publish. */
    ExtensionRegistry *reg = create_extension_registry();
    CHECK(reg != NULL, "create_extension_registry");
    ExtensionInfo info = { .name = "ext", .version = "1.0.0",
                           .get_modifications = ext_get_mods };
    ExtensionID id = 0;
    CHECK(register_extension(reg, &info, &id), "register ext");
    char *err = NULL;
    CHECK(load_extension(reg, id, base, &err), "load ext");
    free(err); err = NULL;

    ParserSnapshot *composed = NULL;
    ConflictSet *cs = NULL;
    bool ok = publish_modified_snapshot(reg, base, &composed, &cs, &err);
    if (!ok) printf("  publish error: %s\n", err ? err : "(none)");
    free(err); err = NULL;
    if (cs) { printf("  conflicts: %u\n", cs->count); conflict_set_destroy(cs); }
    CHECK(ok && composed != NULL, "publish_modified_snapshot succeeds");

    if (composed) {
        CHECK(lime_snapshot_first_token(composed) == 257,
              "composed snapshot preserves %first_token 257");

        /* (1) token-code stability: every base terminal admissible in
        ** the base start state must be admissible (same external code)
        ** in the composed start state.  SELECT leads a statement in
        ** both. */
        CHECK(admissible(base, FTB_SELECT), "base: SELECT(258) admissible at start");
        CHECK(admissible(composed, FTB_SELECT),
              "composed: SELECT(258) STILL admissible at start (code stable)");

        /* (2) the reported bug: a valid BASE query must parse through
        ** the COMPOSED snapshot, not just the base. */
        CHECK(parse_seq(composed, base_query, 5) == 1,
              "composed snapshot accepts SELECT NUM + NUM ; (base query)");

        snapshot_release(composed);
    }

    destroy_extension_registry(reg);
    snapshot_release(base);

    printf("test_compose_first_token: %s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}
