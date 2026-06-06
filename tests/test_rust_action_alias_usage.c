/*
** tests/test_rust_action_alias_usage.c -- regression for c0d68d0
** "fix(rust): scan %rust_action body for RHS alias usage".
**
** Bug: when a rule has a %rust_action body that references RHS
** aliases (e.g. expr(L) PLUS NUM(N) with body using L and N) but
** NO inline {...} C body, the alias-usage scanner only looked at
** the inline C body (which is empty/missing), so all RHS aliases
** were classified as "unused" and emitted with underscore prefix
** (`_L`, `_N`).  The user's Rust body then referenced `L` / `N`
** unprefixed and rustc rejected the file with "cannot find value
** `L` in this scope" + "the underscore-prefixed binding is named
** `_L`".
**
** Fix: lime_rust_ident_used() scans BOTH the inline C body AND
** the %rust_action body.  This test exercises the regression by:
**
**   1. Generating a parser with --target=rust from a grammar that
**      has %rust_action bodies referencing RHS aliases.
**   2. Asserting the emitted .rs has `let L: Value =` (no
**      underscore) NOT `let _L: Value =`.
**   3. Asserting rustc compiles the result (best-effort, skipped
**      when rustc not on PATH).
*/
#include "test_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define CHECK(cond, msg) do {                                            \
    if (!(cond)) {                                                       \
        fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__);          \
        fail++;                                                          \
    }                                                                    \
} while (0)

static int file_contains(const char *path, const char *needle) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return 0; }
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = 0;
    fclose(f);
    int hit = (strstr(buf, needle) != NULL);
    free(buf);
    return hit;
}

int main(int argc, char **argv) {
    int fail = 0;
    if (argc < 2) {
        fprintf(stderr, "usage: %s <path-to-lime>\n", argv[0]);
        return 2;
    }
    const char *lime = argv[1];
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir) tmpdir = "/tmp";
    char dir[256];
    snprintf(dir, sizeof(dir), "%s/test_rust_action_alias", tmpdir);
    mkdir(dir, 0755);

    char gpath[512], rspath[512];
    snprintf(gpath,  sizeof(gpath),  "%s/g.lime", dir);
    snprintf(rspath, sizeof(rspath), "%s/g.rs",   dir);

    /* Grammar: a rule with %rust_action body using L and N aliases
    ** but no inline {...} body.  PRE-FIX: emitted `_L` and `_N`
    ** (causing rustc errors).  POST-FIX: emits `L` and `N`. */
    {
        FILE *f = fopen(gpath, "w");
        if (!f) { perror(gpath); return 2; }
        fputs(
            "%name g\n"
            "%token_type {i64}\n"
            "%default_type {i64}\n"
            "%token NUM PLUS.\n"
            "%start_symbol prog\n"
            "prog ::= expr.\n"
            "expr(R) ::= NUM(N).\n"
            "    %rust_action { R = N; }\n"
            "expr(R) ::= expr(L) PLUS NUM(N).\n"
            "    %rust_action { R = L + N; }\n",
            f);
        fclose(f);
    }

    {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "cd %s && %s --target=rust g.lime",
                 dir, lime);
        int rc = system(cmd);
        CHECK(WIFEXITED(rc) && WEXITSTATUS(rc) == 0,
              "lime --target=rust returns 0");
    }

    /* PRE-FIX assertion: the emitted file would contain `let _L: Value`
    ** and `let _N: Value` because the alias scanner missed the
    ** %rust_action body.  POST-FIX: must contain unprefixed bindings. */
    CHECK(file_contains(rspath, "let L: Value"),
          "L is bound unprefixed (used by %rust_action body)");
    CHECK(file_contains(rspath, "let N: Value"),
          "N is bound unprefixed (used by %rust_action body)");
    CHECK(file_contains(rspath, "let mut R: Value"),
          "R (LHS) is the mutable lhs slot");
    CHECK(!file_contains(rspath, "let _L:"),
          "L is NOT underscore-prefixed (regression guard)");
    CHECK(!file_contains(rspath, "let _N:"),
          "N is NOT underscore-prefixed (regression guard)");

    /* Rustc compile-check (best-effort). */
    {
        int has_rustc = (system("rustc --version >/dev/null 2>&1") == 0);
        if (has_rustc) {
            char cmd[1024];
            snprintf(cmd, sizeof(cmd),
                "cd %s && rustc --edition=2021 --crate-type lib "
                "--emit=metadata --out-dir . g.rs 2>&1 | tail -10",
                dir);
            int rc = system(cmd);
            CHECK(WIFEXITED(rc) && WEXITSTATUS(rc) == 0,
                  "rustc compiles g.rs (would fail PRE-FIX with E0425)");
        } else {
            fprintf(stderr,
                "[rust_action_alias] note: rustc not on PATH; compile-check skipped\n");
        }
    }

    if (fail) {
        fprintf(stderr, "FAIL: %d check(s) failed (artefacts in %s)\n", fail, dir);
        return 1;
    }
    fprintf(stdout, "OK: rust_action_alias_usage\n");
    return 0;
}
