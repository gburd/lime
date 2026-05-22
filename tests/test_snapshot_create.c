/*
** test_snapshot_create.c -- end-to-end test of
** lemon_snapshot_create("foo.y", &err).
**
** Drives the full subprocess-based grammar-to-snapshot pipeline:
**
**   1. Write a tiny grammar file to a temp path.
**   2. Call lemon_snapshot_create() on it.
**   3. Verify the returned ParserSnapshot is populated and that
**      parse_begin/parse_token/parse_end can drive it.
**
** This test exercises the runtime LALR rebuild path that is the
** answer to docs/ROADMAP.md item 1 (in-process for the v1 path is
** subprocess; in-process is the long-term replacement).  The
** subprocess path requires `lime` and `cc` to be available; the
** test detects their absence and skips with a clear message rather
** than failing.
*/

#define _POSIX_C_SOURCE 200809L

#include "parser.h"
#include "parse_context.h"
#include "snapshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

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

/*
** A minimal arithmetic grammar identical in shape to the one used by
** test_runtime_parse + bench/bench_arith_grammar.y, written here as
** a string so the test is self-contained.  The token codes that
** lemon_snapshot_create produces are arbitrary -- we don't import
** the generated header -- so we feed numeric codes that we know are
** valid (1..nterminal) and check the parse outcome.
*/
static const char *kGrammarText =
    "%name_prefix Tinyparse\n"
    "%token_prefix TINY_\n"
    "%token_type   { int }\n"
    "%type program { int }\n"
    "%type expr    { int }\n"
    "%type term    { int }\n"
    "%type factor  { int }\n"
    "%extra_argument { int *result_out }\n"
    "%start_symbol program\n"
    "\n"
    "%token NUM PLUS MINUS STAR SLASH LP RP.\n"
    "\n"
    "program(P) ::= expr(E).            { P = E; *result_out = E; }\n"
    "expr(E) ::= term(T).               { E = T; }\n"
    "expr(E) ::= expr(L) PLUS  term(R). { E = L + R; }\n"
    "expr(E) ::= expr(L) MINUS term(R). { E = L - R; }\n"
    "term(T) ::= factor(F).               { T = F; }\n"
    "term(T) ::= term(L) STAR  factor(R). { T = L * R; }\n"
    "term(T) ::= term(L) SLASH factor(R). { T = (R != 0) ? L / R : 0; }\n"
    "factor(F) ::= NUM(N).             { F = N; }\n"
    "factor(F) ::= LP expr(E) RP.      { F = E; }\n";

static int write_grammar_to(const char *path) {
    FILE *fp = fopen(path, "w");
    if (fp == NULL) return -1;
    size_t n = strlen(kGrammarText);
    size_t w = fwrite(kGrammarText, 1, n, fp);
    fclose(fp);
    return (w == n) ? 0 : -1;
}

static bool tool_available(const char *cmd) {
    char buf[256];
    snprintf(buf, sizeof(buf), "command -v %s >/dev/null 2>&1", cmd);
    return system(buf) == 0;
}

int main(void) {
    printf("Lime lemon_snapshot_create() end-to-end test\n");
    printf("============================================\n");

    if (!tool_available("cc")) {
        printf("  [SKIP] cc not on PATH; "
               "run inside `nix develop` or install a C compiler.\n");
        return 77;
    }

    /* Locate the lime binary -- prefer the one in the local builddir
    ** so we don't accidentally pick up a stale system install.  Fall
    ** back to PATH if neither builddir variant exists. */
    struct stat st;
    bool have_lime = false;
    if (stat("builddir/lime", &st) == 0) {
        setenv("LIME_BIN", "builddir/lime", 1);
        have_lime = true;
    } else if (stat("../builddir/lime", &st) == 0) {
        setenv("LIME_BIN", "../builddir/lime", 1);
        have_lime = true;
    } else if (tool_available("lime")) {
        have_lime = true;
    }
    if (!have_lime) {
        printf("  [SKIP] lime not in builddir/ or on PATH; "
               "build with `ninja -C builddir lime` first.\n");
        return 77;
    }
    /* Same trick for the limpar.c template. */
    if (stat("limpar.c", &st) == 0) {
        setenv("LIME_TEMPLATE", "limpar.c", 1);
    } else if (stat("../limpar.c", &st) == 0) {
        setenv("LIME_TEMPLATE", "../limpar.c", 1);
    }

    /* Step 1: write the grammar to a temp file. */
    char path[256] = "/tmp/lime_snap_test_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) {
        fprintf(stderr, "  [SKIP] mkstemp failed: %s\n", strerror(errno));
        return 77;
    }
    close(fd);
    /* mkstemp gives us a regular file; rename to .y so lime accepts it. */
    char ypath[300];
    snprintf(ypath, sizeof(ypath), "%s.y", path);
    if (rename(path, ypath) != 0) {
        fprintf(stderr, "  [SKIP] rename failed: %s\n", strerror(errno));
        return 77;
    }
    if (write_grammar_to(ypath) != 0) {
        fprintf(stderr, "  [FAIL] failed to write grammar to %s\n", ypath);
        unlink(ypath);
        return 1;
    }

    printf("  Grammar written to: %s\n", ypath);

    /* Step 2: call lemon_snapshot_create. */
    char *err = NULL;
    ParserSnapshot *snap = lemon_snapshot_create(ypath, &err);
    if (snap == NULL) {
        printf("  Error: %s\n", err ? err : "(none)");
        free(err);
        unlink(ypath);
        CHECK(snap != NULL, "lemon_snapshot_create returned a snapshot");
        return n_fail == 0 ? 0 : 1;
    }
    free(err);

    CHECK(snap != NULL, "lemon_snapshot_create returned a snapshot");
    CHECK(snap->yy_action != NULL, "snapshot has yy_action populated");
    CHECK(snap->yy_default != NULL, "snapshot has yy_default populated");
    CHECK(snap->yy_rule_info_nrhs != NULL, "snapshot has rule metadata");
    CHECK(snap->nrule == 9, "snapshot has 9 rules (matches grammar)");
    CHECK(snap->nterminal == 8, "snapshot has 8 terminals (NUM PLUS ... + EOF)");

    printf("\n  Snapshot: states=%u terminals=%u rules=%u action_count=%u\n",
           snap->nstate, snap->nterminal, snap->nrule, snap->action_count);

    /* Step 3: drive a parse via the runtime engine.  The lime
    ** generator assigns terminal codes 1..N in declaration order; we
    ** feed NUM (1) which should accept as a single-NUM expression. */
    ParseContext *ctx = parse_begin(snap);
    CHECK(ctx != NULL, "parse_begin");
    if (ctx) {
        int rc1 = parse_token(ctx, 1, NULL, -1);  /* NUM */
        int rc2 = parse_token(ctx, 0, NULL, -1);  /* EOF */
        parse_end(ctx);
        CHECK(rc1 == 0 && rc2 == 1,
              "parse_token: NUM EOF accepts via the freshly built snapshot");
    }

    snapshot_release(snap);
    unlink(ypath);

    printf("\n=== Summary === Pass: %d Fail: %d\n", n_pass, n_fail);
    return n_fail == 0 ? 0 : 1;
}
