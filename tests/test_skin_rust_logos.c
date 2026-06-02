/*
 * tests/test_skin_rust_logos.c -- regression for the
 * --target=rust:logos skin emit.  Drives lime end-to-end on a small
 * lex fixture, asserts both <stem>_lex.rs and <stem>_lex_logos.rs
 * land alongside it, and grep-checks the skin file for the API
 * surface documented in docs/SKINS.md.
 *
 * We deliberately do NOT shell out to cargo here -- the meson test
 * environment isn't guaranteed to have a compatible Rust toolchain
 * with offline registry access, and the cargo-build smoke test
 * lives in bench/rust_compare anyway.  The textual checks below
 * are sufficient to catch breakage in src/lex/emit_rust_skin_logos.c
 * and the lex_main.c wiring.
 *
 * Skipped at runtime when LIME_BIN isn't on $PATH.
 */

#include "test_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Toy JSON-ish lex fixture.  Mirrors the rule names from
** bench/rust_compare/json.lex closely enough that the emitted enum
** variants match what a downstream consumer of json.lex would see. */
static const char *kLexFixture =
    "%name_prefix Toy.\n"
    "rule lbrace   matches /\\{/        { /* */ }\n"
    "rule rbrace   matches /\\}/        { /* */ }\n"
    "rule lbracket matches /\\[/        { /* */ }\n"
    "rule rbracket matches /\\]/        { /* */ }\n"
    "rule comma    matches /,/         { /* */ }\n"
    "rule colon    matches /:/         { /* */ }\n"
    "rule num      matches /[0-9]+/    { /* */ }\n"
    "rule ident    matches /[a-z]+/    { /* */ }\n"
    "rule ws       matches /[ \\t\\n]+/ { /* */ }\n";

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* Slurp a file into a heap buffer (NUL-terminated).  Returns NULL on
** error; caller frees. */
static char *slurp(const char *path, size_t *len_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = 0;
    if (len_out) *len_out = got;
    return buf;
}

/* Minimal substring assertion that prints which needle was missing. */
static int expect_contains(const char *hay, const char *needle, const char *label) {
    if (strstr(hay, needle) != NULL) {
        printf("  PASS: %s contains %s\n", label, needle);
        return 1;
    }
    printf("  FAIL: %s missing %s\n", label, needle);
    return 0;
}

int main(int argc, char **argv) {
    const char *lime_bin = (argc > 1) ? argv[1] : getenv("LIME_BIN");
    if (lime_bin == NULL || lime_bin[0] == 0) {
        fprintf(stderr, "SKIP: no LIME_BIN\n");
        return 77;
    }

    char tmpdir[256];
    if (test_compat_tmpdir("lime_skin_logos", tmpdir, sizeof(tmpdir)) != 0) {
        fprintf(stderr, "FAIL: tmpdir\n");
        return 1;
    }
    if (chdir(tmpdir) != 0) { perror("chdir"); return 1; }

    FILE *f = fopen("toy.lex", "wb");
    if (!f) { perror("fopen"); return 1; }
    fputs(kLexFixture, f);
    fclose(f);

    /* Drive lime --target=rust:logos -X toy.lex.  -d. is implicit. */
    char *child_argv[] = {
        (char *)lime_bin, "-X", "-d.", "--target=rust:logos", "toy.lex", NULL
    };
    int rc = 0;
    if (test_compat_run(child_argv, &rc) != 0) {
        fprintf(stderr, "FAIL: spawn lime\n");
        return 1;
    }
    if (rc != 0) {
        fprintf(stderr, "FAIL: lime exited rc=%d\n", rc);
        return 1;
    }

    int total = 0, pass = 0;

    total++;
    if (file_exists("toy_lex.rs")) { printf("  PASS: toy_lex.rs exists\n"); pass++; }
    else                            { printf("  FAIL: toy_lex.rs missing\n"); }

    total++;
    if (file_exists("toy_lex_logos.rs")) { printf("  PASS: toy_lex_logos.rs exists\n"); pass++; }
    else                                  { printf("  FAIL: toy_lex_logos.rs missing\n"); }

    if (file_exists("toy_lex_logos.rs")) {
        size_t len = 0;
        char *body = slurp("toy_lex_logos.rs", &len);
        if (!body) {
            printf("  FAIL: slurp toy_lex_logos.rs\n");
        } else {
            /* Surface API checks: enum, iterator impl, mapper, sibling
            ** import, the Token::lexer constructor.  These are the
            ** invariants documented in docs/SKINS.md and in the brief
            ** for the rust:logos skin. */
            const struct { const char *needle; } needles[] = {
                { "use super::toy_lex as lime_lex;" },
                { "pub enum Token {" },
                { "Lbrace," },        /* PascalCase variant for `lbrace` */
                { "Lbracket," },
                { "Ident," },
                { "Ws," },
                { "Error," },         /* sentinel variant */
                { "pub struct Lexer<'source>" },
                { "impl<'source> Iterator for Lexer<'source>" },
                { "type Item = Result<Token, ()>;" },
                { "pub fn lexer<'source>(input: &'source str) -> Lexer<'source>" },
                { "fn map_lime_to_logos(rule_id: u16) -> Result<Token, ()>" },
                { "lime_lex::TOY_RULE_LBRACE => Ok(Token::Lbrace)," },
                { "lime_lex::TOY_RULE_IDENT => Ok(Token::Ident)," },
                { "pub const N_RULES: u16 = 9;" },
                { "pub fn span(&self) -> core::ops::Range<usize>" },
                { "pub fn slice(&self) -> &'source str" },
            };
            for (size_t i = 0; i < sizeof(needles)/sizeof(needles[0]); i++) {
                total++;
                pass += expect_contains(body, needles[i].needle, "toy_lex_logos.rs");
            }
            free(body);
        }
    }

    test_compat_chdir_temp();
    test_compat_rmdir_recursive(tmpdir);

    printf("\nResults: %d/%d passed\n", pass, total);
    return pass == total ? 0 : 1;
}
