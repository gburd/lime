/*
** test_extension_rebuild.c -- end-to-end exercise of runtime
** LALR(1) automaton rebuild via the subprocess pipeline.
**
** Strategy:
**
**   1. Build a base snapshot from a tiny arithmetic grammar via
**      ArithBuildSnapshot() (which now embeds the grammar source
**      thanks to lime -n).
**
**   2. Register an extension that adds a new postfix-factorial
**      operator: a token "BANG" and a rule
**         expr ::= expr BANG.
**      (Postfix factorial doesn't conflict with any existing rule
**      in the base grammar.)
**
**   3. publish_modified_snapshot() takes the subprocess path because
**      the base has grammar_source.  The merged grammar text is
**      handed to lime + cc, the resulting .so is dlopen()ed, and
**      the new ParserSnapshot has fully recomputed action tables.
**
**   4. Drive parse_token() against the modified snapshot with input
**      that exercises the new rule.  Verify the parser accepts.
**
**   5. As a control, drive the SAME input against the base snapshot
**      and verify it fails (proving the new rule is what enabled
**      the parse, not just the base).
**
** Skipped at runtime when `lime` or `cc` is unreachable.
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

/* ------------------------------------------------------------------ */
/*  Extension that adds a unary postfix factorial: expr ::= expr BANG */
/* ------------------------------------------------------------------ */

static GrammarModification ext_mods[2];
static const char *fact_rhs[] = {"expr", "BANG", NULL};

static bool fact_get_modifications(void *user_data, const struct ParserSnapshot *base,
                                   GrammarModification **mods_out, uint32_t *nmods_out) {
    (void)user_data;
    (void)base;
    ext_mods[0].type = MOD_ADD_TOKEN;
    ext_mods[0].description = "factorial extension: add BANG terminal";
    ext_mods[0].u.add_token.name = "BANG";
    ext_mods[0].u.add_token.lexeme = "!";
    ext_mods[0].u.add_token.token_code = -1;

    ext_mods[1].type = MOD_ADD_RULE;
    ext_mods[1].description = "factorial extension: expr ::= expr BANG";
    ext_mods[1].u.add_rule.lhs = "expr";
    ext_mods[1].u.add_rule.rhs = fact_rhs;
    ext_mods[1].u.add_rule.nrhs = 2;
    ext_mods[1].u.add_rule.code = "{ E = -L; (void)T; }"; /* placeholder action */
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

/*
** Map a token name (as it appears in the base grammar) to the code
** the parser snapshot expects.  For runtime-built snapshots the codes
** are assigned in declaration order: NUM=1, PLUS=2, MINUS=3, STAR=4,
** SLASH=5, LP=6, RP=7.  The new BANG token added by the extension
** is whatever comes next (8).
*/
#define TOK_NUM   1
#define TOK_PLUS  2
#define TOK_MINUS 3
#define TOK_STAR  4
#define TOK_SLASH 5
#define TOK_LP    6
#define TOK_RP    7
#define TOK_BANG  8 /* added by the extension */

static int parse_seq(ParserSnapshot *snap, const int *codes, int n) {
    ParseContext *ctx = parse_begin(snap);
    if (ctx == NULL) return -2;
    int rc = 0;
    for (int i = 0; i < n; i++) {
        rc = parse_token(ctx, codes[i], NULL, -1);
        if (rc < 0) break;
    }
    if (rc >= 0) rc = parse_token(ctx, 0, NULL, -1);
    parse_end(ctx);
    return rc;
}

/* ------------------------------------------------------------------ */

int main(void) {
    printf("Lime in-process LALR rebuild test (subprocess path)\n");
    printf("===================================================\n");

    if (!tool_available("cc")) {
        printf("  [SKIP] cc not on PATH; run inside `nix develop` to enable.\n");
        return 77;
    }
    /* Locate the local lime binary the same way test_snapshot_create
    ** does, so we don't depend on a system install. */
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
    CHECK(base != NULL, "ArithBuildSnapshot returned a base snapshot");
    if (base == NULL) return 1;

    CHECK(base->grammar_source != NULL && base->grammar_source_len > 0,
          "base snapshot carries grammar_source (lime -n embedded it)");
    if (base->grammar_source) {
        printf("  Grammar source length: %u bytes\n", base->grammar_source_len);
        printf("  First 60 bytes: %.60s...\n", base->grammar_source);
    }

    /* Step 2: register the extension. */
    ExtensionRegistry *reg = create_extension_registry();
    CHECK(reg != NULL, "create_extension_registry");

    ExtensionInfo info = {
        .name = "factorial-postfix",
        .version = "1.0.0",
        .get_modifications = fact_get_modifications,
    };
    ExtensionID id = 0;
    CHECK(register_extension(reg, &info, &id), "register factorial extension");

    char *err = NULL;
    CHECK(load_extension(reg, id, base, &err), "load factorial extension");
    free(err);
    err = NULL;

    /* Step 3: publish a modified snapshot.  publish_modified_snapshot
    ** picks the subprocess path because base has grammar_source. */
    ParserSnapshot *modified = NULL;
    ConflictSet *cs = NULL;
    bool ok = publish_modified_snapshot(reg, base, &modified, &cs, &err);
    if (!ok) {
        printf("  publish_modified_snapshot error: %s\n", err ? err : "(none)");
    }
    free(err);
    err = NULL;
    if (cs != NULL) {
        printf("  conflicts: %u\n", cs->count);
        conflict_set_destroy(cs);
    }
    CHECK(ok && modified != NULL, "publish_modified_snapshot succeeds via subprocess");

    if (modified == NULL) {
        destroy_extension_registry(reg);
        snapshot_release(base);
        printf("\n=== Summary === Pass: %d Fail: %d\n", n_pass, n_fail);
        return n_fail == 0 ? 0 : 1;
    }

    printf("\n  Modified snapshot: states=%u terminals=%u rules=%u "
           "action_count=%u ntoken=%u\n",
           modified->nstate, modified->nterminal, modified->nrule, modified->action_count,
           modified->yy_ntoken);

    /* Modified should have one extra terminal (BANG) and one extra rule. */
    CHECK(modified->nterminal > base->nterminal,
          "modified snapshot has more terminals than base");
    CHECK(modified->nrule > base->nrule, "modified snapshot has more rules than base");

    /* Step 4: parse "NUM !" through the modified snapshot. */
    {
        int seq[] = {TOK_NUM, TOK_BANG};
        int rc = parse_seq(modified, seq, 2);
        CHECK(rc == 1, "modified snapshot accepts: NUM BANG");
    }
    /* And a slightly more involved case: "(1 + 2) !" */
    {
        int seq[] = {TOK_LP, TOK_NUM, TOK_PLUS, TOK_NUM, TOK_RP, TOK_BANG};
        int rc = parse_seq(modified, seq, 6);
        CHECK(rc == 1, "modified snapshot accepts: ( NUM + NUM ) BANG");
    }

    /* Step 5: control -- the same NUM BANG sequence should be a
    ** syntax error against the BASE snapshot, because BANG is not a
    ** known token there.  Note: the base snapshot's BANG code (8) is
    ** out of range, so parse_token may either reject explicitly or
    ** the parser's first action lookup fails -- either way, rc < 0. */
    {
        int seq[] = {TOK_NUM, TOK_BANG};
        int rc = parse_seq(base, seq, 2);
        CHECK(rc < 0, "base snapshot rejects: NUM BANG (unknown token)");
    }

    snapshot_release(modified);
    destroy_extension_registry(reg);
    snapshot_release(base);

    printf("\n=== Summary === Pass: %d Fail: %d\n", n_pass, n_fail);
    return n_fail == 0 ? 0 : 1;
}
