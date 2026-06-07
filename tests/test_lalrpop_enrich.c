/*
** tests/test_lalrpop_enrich.c -- runtime regression for the v1.4.0
** lalrpop-skin enrichment:
**
**   1. A strongly-typed `Token { start, code, end, value }` struct
**      is emitted with bidirectional `From` impls to/from the
**      `(usize, u16, usize, Value)` quadruple `parse()` consumes.
**      The tuple form keeps working (LTS compat).
**
**   2. `ParseError`'s `expected: Vec<String>` is now POPULATED (was
**      always empty in v1.3.0) by calling the parser's new public
**      `current_state()` + the standard `expected_tokens_in_state`
**      + `token_name` introspection.
**
** Cargo-driven: generates calc + the lalrpop skin, assembles a
** crate, and runs four cargo tests.  SKIPs (rc=77) when cargo is
** absent.
*/
#include "test_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>

static int run_capture(const char *cmd, const char *logpath) {
    char wrap[2048];
    snprintf(wrap, sizeof(wrap), "%s >> %s 2>&1", cmd, logpath);
    int rc = system(wrap);
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : 1;
}

static void dump_log(const char *logpath) {
    fprintf(stderr, "----- cargo log (%s) -----\n", logpath);
    FILE *log = fopen(logpath, "r");
    if (log) {
        char buf[4096]; size_t n;
        while ((n = fread(buf, 1, sizeof(buf), log)) > 0) fwrite(buf, 1, n, stderr);
        fclose(log);
    }
    fprintf(stderr, "--------------------------\n");
}

int main(int argc, char **argv) {
    int fail = 0;
    if (argc < 2) { fprintf(stderr, "usage: %s <path-to-lime>\n", argv[0]); return 2; }
    const char *lime = argv[1];

    if (system("cargo --version >/dev/null 2>&1") != 0) {
        fprintf(stderr, "[lalrpop_enrich] cargo not on PATH; skipped (rc=77).\n");
        return 77;
    }

    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir) tmpdir = "/tmp";
    char dir[256], srcdir[300];
    snprintf(dir, sizeof(dir), "%s/test_lalrpop_enrich", tmpdir);
    mkdir(dir, 0755);
    snprintf(srcdir, sizeof(srcdir), "%s/src", dir);
    mkdir(srcdir, 0755);

    char grammar[512], cargopath[512], libpath[512], logpath[512];
    snprintf(grammar,   sizeof(grammar),   "%s/calc.lime",  dir);
    snprintf(cargopath, sizeof(cargopath), "%s/Cargo.toml", dir);
    snprintf(libpath,   sizeof(libpath),   "%s/src/lib.rs", dir);
    snprintf(logpath,   sizeof(logpath),   "%s/cargo.log",  dir);
    { FILE *f = fopen(logpath, "w"); if (f) fclose(f); }

    {
        FILE *f = fopen(grammar, "w");
        if (!f) { perror(grammar); return 2; }
        fputs(
            "%name calc\n"
            "%token_type {i64}\n"
            "%default_type {i64}\n"
            "%token NUM PLUS.\n"
            "%start_symbol prog\n"
            "prog ::= expr.\n"
            "expr(A) ::= expr(B) PLUS NUM(C). %rust_action { A = B + C; }\n"
            "expr(A) ::= NUM(N). %rust_action { A = N; }\n",
            f);
        fclose(f);
    }

    {
        char cmd[900];
        snprintf(cmd, sizeof(cmd),
                 "cd %s && %s --target=rust:lalrpop calc.lime && "
                 "mv calc.rs calc_lalrpop.rs src/",
                 dir, lime);
        int rc = run_capture(cmd, logpath);
        if (rc != 0) {
            fprintf(stderr, "FAIL: lime --target=rust:lalrpop (line %d)\n", __LINE__);
            dump_log(logpath);
            return 1;
        }
    }

    {
        FILE *f = fopen(cargopath, "w");
        if (!f) { perror(cargopath); return 2; }
        fputs(
            "[package]\n"
            "name = \"lalrpop_enrich_test\"\n"
            "version = \"0.0.0\"\n"
            "edition = \"2021\"\n"
            "\n"
            "[lib]\n"
            "path = \"src/lib.rs\"\n",
            f);
        fclose(f);
    }

    {
        FILE *f = fopen(libpath, "w");
        if (!f) { perror(libpath); return 2; }
        fputs(
            "#![allow(non_snake_case, non_camel_case_types, dead_code,\n"
            "         unused_variables, unused_assignments)]\n"
            "pub mod calc;\n"
            "pub mod calc_lalrpop;\n"
            "\n"
            "#[cfg(test)]\n"
            "mod tests {\n"
            "    use super::calc::{NUM, PLUS};\n"
            "    use super::calc_lalrpop::*;\n"
            "\n"
            "    #[test]\n"
            "    fn parse_via_tuple() {\n"
            "        let p = CalcParser::new();\n"
            "        let toks = vec![(0, NUM, 1, 1i64), (2, PLUS, 3, 0), (4, NUM, 5, 2)];\n"
            "        assert_eq!(p.parse(toks).unwrap(), 3);\n"
            "    }\n"
            "\n"
            "    #[test]\n"
            "    fn parse_via_token_struct() {\n"
            "        let p = CalcParser::new();\n"
            "        let typed = vec![\n"
            "            Token { start: 0, code: NUM,  end: 1, value: 1i64 },\n"
            "            Token { start: 2, code: PLUS, end: 3, value: 0 },\n"
            "            Token { start: 4, code: NUM,  end: 5, value: 2 },\n"
            "        ];\n"
            "        let quads: Vec<(usize, u16, usize, i64)> =\n"
            "            typed.into_iter().map(Into::into).collect();\n"
            "        assert_eq!(p.parse(quads).unwrap(), 3);\n"
            "    }\n"
            "\n"
            "    #[test]\n"
            "    fn tuple_into_token_roundtrip() {\n"
            "        let q = (7usize, NUM, 8usize, 42i64);\n"
            "        let t: Token = q.into();\n"
            "        assert_eq!((t.start, t.code, t.end, t.value), (7, NUM, 8, 42));\n"
            "        let back: (usize, u16, usize, i64) = t.into();\n"
            "        assert_eq!(back, q);\n"
            "    }\n"
            "\n"
            "    #[test]\n"
            "    fn expected_is_populated_on_error() {\n"
            "        // Leading PLUS is illegal; expected should name NUM.\n"
            "        let p = CalcParser::new();\n"
            "        let toks = vec![(0, PLUS, 1, 0i64)];\n"
            "        match p.parse(toks) {\n"
            "            Err(ParseError::UnrecognizedToken { token, expected }) => {\n"
            "                assert_eq!(token.1, PLUS);\n"
            "                assert!(!expected.is_empty(),\n"
            "                        \"expected should be populated, got empty\");\n"
            "                assert!(expected.iter().any(|s| s == \"NUM\"),\n"
            "                        \"expected should contain NUM, got {:?}\", expected);\n"
            "            }\n"
            "            other => panic!(\"expected UnrecognizedToken, got {:?}\", other),\n"
            "        }\n"
            "    }\n"
            "}\n",
            f);
        fclose(f);
    }

    {
        char cmd[600];
        snprintf(cmd, sizeof(cmd), "cd %s && cargo test --quiet", dir);
        int rc = run_capture(cmd, logpath);
        if (rc != 0) {
            fprintf(stderr, "FAIL: cargo test (line %d)\n", __LINE__);
            dump_log(logpath);
            fail++;
        }
    }

    if (fail) {
        fprintf(stderr, "FAIL: %d check(s) failed (artefacts in %s)\n", fail, dir);
        return 1;
    }
    fprintf(stdout, "OK: lalrpop_enrich (4 sub-tests)\n");
    return 0;
}
