/*
** tests/test_emit_rust_skin_lalrpop.c -- regression coverage for
** v1.3.0's lalrpop-API-compatibility Rust skin
** (`lime --target=rust:lalrpop`).
**
** What this test asserts:
**
**   1. The skin emits two files: <stem>.rs (standard Rust output)
**      AND <stem>_lalrpop.rs (the wrapper).
**
**   2. The wrapper's surface contains the lalrpop-shaped public
**      API: ParseError enum with the four variants
**      (InvalidToken, UnrecognizedEof, UnrecognizedToken,
**      ExtraToken), a <PascalName>Parser struct, an impl block
**      with new() + parse(), and Default impl.
**
**   3. The wrapper imports the standard sibling module via
**      `use super::<bare>::*;` and aliases the inner parser.
**
**   4. The PascalCase rename works: `%name calc` -> `CalcParser`.
**
**   5. The wrapper compiles cleanly via rustc (only available on
**      systems where rustc is on PATH; otherwise sub-test 5 is
**      skipped with a stderr note).
**
** This is a 'shape' test, not a 'cargo test' -- it asserts the
** emitted Rust LOOKS right.  The cargo-driven runtime test that
** confirms parse() actually parses lives in
** examples/rust_calc_lalrpop/ and in CI's rust_compare job.
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
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
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

    /* Stage a tmp grammar. */
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir) tmpdir = "/tmp";
    char dir[256];
    snprintf(dir, sizeof(dir), "%s/test_lalrpop_skin", tmpdir);
    mkdir(dir, 0755);

    char gpath[512], rspath[512], skinpath[512];
    snprintf(gpath,    sizeof(gpath),    "%s/calc.lime", dir);
    snprintf(rspath,   sizeof(rspath),   "%s/calc.rs", dir);
    snprintf(skinpath, sizeof(skinpath), "%s/calc_lalrpop.rs", dir);

    {
        FILE *f = fopen(gpath, "w");
        if (!f) { perror(gpath); return 2; }
        fputs(
            "%name calc\n"
            "%token NUM PLUS.\n"
            "%token_type {i64}\n"
            "%type expr {i64}\n"
            "%start_symbol prog\n"
            "prog ::= expr.\n"
            "expr ::= NUM(N).            { ($$) = N; }\n"
            "expr ::= expr(L) PLUS NUM(N).   { ($$) = L + N; }\n",
            f);
        fclose(f);
    }

    /* Run lime --target=rust:lalrpop. */
    {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "cd %s && %s --target=rust:lalrpop calc.lime",
                 dir, lime);
        int rc = system(cmd);
        CHECK(WIFEXITED(rc) && WEXITSTATUS(rc) == 0,
              "lime --target=rust:lalrpop returns 0");
    }

    /* --- 1. Both files emitted ----------------------------- */
    {
        struct stat st;
        CHECK(stat(rspath,   &st) == 0, "calc.rs is emitted");
        CHECK(stat(skinpath, &st) == 0, "calc_lalrpop.rs is emitted");
    }

    /* --- 2. Wrapper has the lalrpop-shaped surface --------- */
    CHECK(file_contains(skinpath, "pub enum ParseError"),
          "ParseError enum is emitted");
    CHECK(file_contains(skinpath, "InvalidToken"),
          "ParseError::InvalidToken variant");
    CHECK(file_contains(skinpath, "UnrecognizedEof"),
          "ParseError::UnrecognizedEof variant");
    CHECK(file_contains(skinpath, "UnrecognizedToken"),
          "ParseError::UnrecognizedToken variant");
    CHECK(file_contains(skinpath, "ExtraToken"),
          "ParseError::ExtraToken variant");
    CHECK(file_contains(skinpath, "User"),
          "ParseError::User variant");
    CHECK(file_contains(skinpath, "pub struct CalcParser"),
          "CalcParser struct (PascalCase from %name calc)");
    CHECK(file_contains(skinpath, "impl CalcParser"),
          "impl block on CalcParser");
    CHECK(file_contains(skinpath, "pub fn new()"),
          "CalcParser::new()");
    CHECK(file_contains(skinpath, "pub fn parse<I>"),
          "CalcParser::parse<I>()");
    CHECK(file_contains(skinpath, "Result<Value, ParseError"),
          "parse returns Result<Value, ParseError>");
    CHECK(file_contains(skinpath, "impl Default for CalcParser"),
          "impl Default for CalcParser");

    /* --- 2b. v1.4.0 enrichment surface --------------------- */
    CHECK(file_contains(skinpath, "pub struct Token"),
          "v1.4.0: strongly-typed Token struct emitted");
    CHECK(file_contains(skinpath, "impl From<Token> for (usize, u16, usize, Value)"),
          "v1.4.0: Token -> quadruple From impl");
    CHECK(file_contains(skinpath, "impl From<(usize, u16, usize, Value)> for Token"),
          "v1.4.0: quadruple -> Token From impl");
    CHECK(file_contains(skinpath, "fn expected_at"),
          "v1.4.0: expected_at() helper emitted");
    CHECK(file_contains(skinpath, "expected: expected_at(&p)"),
          "v1.4.0: expected field populated (not Vec::new())");
    CHECK(file_contains(rspath, "pub fn current_state"),
          "v1.4.0: parser exposes pub current_state()");

    /* --- 3. Module imports referenced ---------------------- */
    CHECK(file_contains(skinpath, "use super::calc::*"),
          "imports super::calc::* (sibling module wildcard)");
    CHECK(file_contains(skinpath, "calcParser as InnerParser"),
          "aliases inner calcParser to InnerParser");

    /* --- 4. Rustc compile check (best-effort) -------------- */
    {
        int has_rustc = (system("rustc --version >/dev/null 2>&1") == 0);
        if (has_rustc) {
            /* Build a tiny lib that imports both files; rustc --crate-type lib
            ** + --emit metadata exits 0 only if both modules typecheck
            ** together. */
            char libpath[512];
            snprintf(libpath, sizeof(libpath), "%s/lib.rs", dir);
            FILE *f = fopen(libpath, "w");
            if (f) {
                fputs("#[allow(non_snake_case)]\n"
                      "#[path = \"calc.rs\"] pub mod calc;\n"
                      "#[path = \"calc_lalrpop.rs\"] pub mod calc_lalrpop;\n",
                      f);
                fclose(f);
                char cmd[1024];
                /* --emit=metadata short-circuits codegen; we only need
                ** type-checking. */
                snprintf(cmd, sizeof(cmd),
                    "cd %s && rustc --edition=2021 --crate-type lib "
                    "--emit=metadata --out-dir . lib.rs 2>&1 | tail -5",
                    dir);
                int rc = system(cmd);
                CHECK(WIFEXITED(rc) && WEXITSTATUS(rc) == 0,
                      "rustc compiles standard.rs + skin.rs together");
            }
        } else {
            fprintf(stderr,
                "[lalrpop_skin] note: rustc not on PATH; sub-test 5 skipped\n");
        }
    }

    if (fail) {
        fprintf(stderr, "FAIL: %d check(s) failed (artefacts in %s)\n",
                fail, dir);
        return 1;
    }
    fprintf(stdout, "OK: emit_rust_skin_lalrpop\n");
    return 0;
}
