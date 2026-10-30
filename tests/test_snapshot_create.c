/*
** test_snapshot_create.c -- end-to-end test of
** lime_snapshot_create("foo.y", &err).
**
**   1. Write a tiny grammar file to a temp path.
**   2. Call lime_snapshot_create() on it: read + in-process LALR(1)
**      build (liblime_compiler).  No `lime` or `cc` on PATH is needed,
**      and the test runs with PATH emptied to prove it.
**   3. Verify the returned ParserSnapshot is populated and that
**      parse_begin/parse_token/parse_end can drive it.
**   4. A missing file returns NULL with an error naming it.
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
#if defined(_WIN32)
#include <io.h>
#define dup2 _dup2
#define close _close
#define open _open
#else
#include <unistd.h>
#endif

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
** lime_snapshot_create produces are arbitrary -- we don't import
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


int main(void) {
    printf("Lime lime_snapshot_create() end-to-end test\n");
    printf("============================================\n");

    /* Nothing on PATH: the old pipeline exec'd `lime` and `cc` from it. */
    setenv("PATH", "", 1);

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

    /* Step 2: call lime_snapshot_create. */
    char *err = NULL;
    ParserSnapshot *snap = lime_snapshot_create(ypath, &err);
    if (snap == NULL) {
        printf("  Error: %s\n", err ? err : "(none)");
        free(err);
        unlink(ypath);
        CHECK(snap != NULL, "lime_snapshot_create returned a snapshot");
        return n_fail == 0 ? 0 : 1;
    }
    free(err);

    CHECK(snap != NULL, "lime_snapshot_create returned a snapshot");
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

    /* Step 4: a missing file is an error, not a crash or a fallback. */
    err = NULL;
    snap = lime_snapshot_create(ypath, &err);
    CHECK(snap == NULL && err != NULL && strstr(err, ypath) != NULL,
          "missing file: NULL + error naming the path");
    free(err);

    printf("\n=== Summary === Pass: %d Fail: %d\n", n_pass, n_fail);
    return n_fail == 0 ? 0 : 1;
}
