/*
** tests/test_rust_ident_scan.c -- regression for the Rust-aware
** identifier scan in lime_rust_ident_used() (src/emit_rust.c).
**
** The alias-usage scanner decides whether an RHS alias binding is
** referenced by a rule action body.  If referenced, the binding is
** emitted with its real name; if not, it is underscore-prefixed to
** suppress rustc's unused-variable warning.
**
** A FALSE POSITIVE (claiming used when not) is harmless: it just
** keeps the name.  A FALSE NEGATIVE (claiming unused when used) is a
** bug: it underscore-prefixes a live binding and rustc rejects the
** file with E0425.
**
** The original scanner used a plain word-boundary substring scan, so
** an alias name appearing ONLY inside a string literal, char literal,
** or comment was a (harmless) false positive.  The Rust-aware scan
** walks lexer state and skips those contexts, so an alias used ONLY
** inside a literal/comment is correctly classified as unused and
** underscore-prefixed.
**
** This test exercises:
**
**   1. An alias (N) that is genuinely used in code  -> unprefixed.
**   2. An alias (M) that appears ONLY inside a string/comment in the
**      action body -> underscore-prefixed (the lexical-context skip).
**   3. rustc compiles the result (best-effort; skipped without rustc).
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
    snprintf(dir, sizeof(dir), "%s/test_rust_ident_scan", tmpdir);
    mkdir(dir, 0755);

    char gpath[512], rspath[512];
    snprintf(gpath,  sizeof(gpath),  "%s/g.lime", dir);
    snprintf(rspath, sizeof(rspath), "%s/g.rs",   dir);

    /* Grammar: a two-alias rule whose %rust_action body uses N in real
    ** code, but mentions M ONLY inside a string literal and a comment.
    ** The Rust-aware scan must skip the literal/comment occurrences and
    ** classify M as unused, so it is emitted as `_M`.  N is genuinely
    ** used and must stay `N`. */
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
            "expr(R) ::= NUM(N) PLUS NUM(M).\n"
            "    %rust_action {\n"
            "        // M is mentioned here in a comment only\n"
            "        let s = \"M plus M\"; let _ = s;\n"
            "        R = N; /* and again: M */\n"
            "    }\n",
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

    /* N is used in code: must be bound unprefixed. */
    CHECK(file_contains(rspath, "let N: Value"),
          "N is bound unprefixed (used in code)");
    CHECK(!file_contains(rspath, "let _N:"),
          "N is NOT underscore-prefixed (used in code)");

    /* M appears ONLY inside a string literal and comments: the
    ** Rust-aware scan must treat it as unused and underscore-prefix it. */
    CHECK(file_contains(rspath, "let _M: Value"),
          "M is underscore-prefixed (only in string/comment)");
    CHECK(!file_contains(rspath, "let M: Value"),
          "M is NOT bound unprefixed (only in string/comment)");

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
                  "rustc compiles g.rs");
        } else {
            fprintf(stderr,
                "[rust_ident_scan] note: rustc not on PATH; compile-check skipped\n");
        }
    }

    if (fail) {
        fprintf(stderr, "FAIL: %d check(s) failed (artefacts in %s)\n", fail, dir);
        return 1;
    }
    fprintf(stdout, "OK: rust_ident_scan\n");
    return 0;
}
