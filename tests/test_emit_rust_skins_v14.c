/*
** tests/test_emit_rust_skins_v14.c -- runtime regression for the
** v1.4.0 nom / pest / chumsky Rust-API-compatibility skins
** (`lime --target=rust:nom|pest|chumsky`).
**
** Unlike the lalrpop shape-test, this is cargo-driven: it generates
** the standard parser plus all three skins for a small calc grammar,
** assembles a crate that #[path]'s them as sibling modules, and runs
** cargo tests that actually PARSE `1 + 2 + 3` through each skin's
** public API and assert the result is 6.  It also exercises each
** skin's error path.
**
** Sub-tests (inside the generated cargo crate):
**   nom_parses        : calc(&toks) -> Ok((rem, 6)), rem empty
**   nom_error         : leading PLUS -> Err(Err::Error{Tag})
**   pest_parses       : CalcParserPest::parse -> Pairs, EOI value 6
**   chumsky_parses    : calc().parse(&toks) -> Ok(6)
**   chumsky_error     : leading PLUS -> Err(vec![Simple{at:0}])
**
** SKIPs cleanly (rc=77) when cargo isn't on PATH.
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
        fprintf(stderr, "[skins_v14] cargo not on PATH; runtime regression "
                        "skipped (rc=77).\n");
        return 77;
    }

    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir) tmpdir = "/tmp";
    char dir[256], src[300];
    snprintf(dir, sizeof(dir), "%s/test_skins_v14", tmpdir);
    mkdir(dir, 0755);
    snprintf(src, sizeof(src), "%s/src", dir);
    mkdir(src, 0755);

    char grammar[512], cargopath[512], libpath[512], logpath[512];
    snprintf(grammar,   sizeof(grammar),   "%s/calc.lime",  dir);
    snprintf(cargopath, sizeof(cargopath), "%s/Cargo.toml", dir);
    snprintf(libpath,   sizeof(libpath),   "%s/src/lib.rs", dir);
    snprintf(logpath,   sizeof(logpath),   "%s/cargo.log",  dir);
    { FILE *f = fopen(logpath, "w"); if (f) fclose(f); }

    /* Calc grammar with %rust_action reduce bodies. */
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

    /* Generate standard parser + all three skins into src/. */
    {
        char cmd[1500];
        snprintf(cmd, sizeof(cmd),
                 "cd %s && %s --target=rust:nom calc.lime && "
                 "%s --target=rust:pest calc.lime && "
                 "%s --target=rust:chumsky calc.lime && "
                 "mv calc.rs calc_nom.rs calc_pest.rs calc_chumsky.rs src/",
                 dir, lime, lime, lime);
        int rc = run_capture(cmd, logpath);
        if (rc != 0) {
            fprintf(stderr, "FAIL: lime skin generation (line %d)\n", __LINE__);
            dump_log(logpath);
            return 1;
        }
    }

    /* Cargo.toml */
    {
        FILE *f = fopen(cargopath, "w");
        if (!f) { perror(cargopath); return 2; }
        fputs(
            "[package]\n"
            "name = \"skins_v14_test\"\n"
            "version = \"0.0.0\"\n"
            "edition = \"2021\"\n"
            "\n"
            "[lib]\n"
            "path = \"src/lib.rs\"\n",
            f);
        fclose(f);
    }

    /* lib.rs with module decls + the runtime tests. */
    {
        FILE *f = fopen(libpath, "w");
        if (!f) { perror(libpath); return 2; }
        fputs(
            "#![allow(non_snake_case, non_camel_case_types, dead_code,\n"
            "         unused_variables, unused_assignments)]\n"
            "pub mod calc;\n"
            "pub mod calc_nom;\n"
            "pub mod calc_pest;\n"
            "pub mod calc_chumsky;\n"
            "\n"
            "#[cfg(test)]\n"
            "mod tests {\n"
            "    use super::calc::{NUM, PLUS};\n"
            "\n"
            "    #[test]\n"
            "    fn nom_parses() {\n"
            "        use super::calc_nom::{calc, LimeTok};\n"
            "        let toks = vec![\n"
            "            LimeTok { code: NUM,  value: 1 },\n"
            "            LimeTok { code: PLUS, value: 0 },\n"
            "            LimeTok { code: NUM,  value: 2 },\n"
            "            LimeTok { code: PLUS, value: 0 },\n"
            "            LimeTok { code: NUM,  value: 3 },\n"
            "        ];\n"
            "        let (rem, val) = calc(&toks).expect(\"nom parse ok\");\n"
            "        assert_eq!(val, 6, \"nom: 1+2+3 should be 6, got {}\", val);\n"
            "        assert!(rem.is_empty(), \"nom: remainder empty\");\n"
            "    }\n"
            "\n"
            "    #[test]\n"
            "    fn nom_error() {\n"
            "        use super::calc_nom::{calc, LimeTok, Err, ErrorKind};\n"
            "        let toks = vec![ LimeTok { code: PLUS, value: 0 } ];\n"
            "        match calc(&toks) {\n"
            "            Err(Err::Error(e)) => assert_eq!(e.code, ErrorKind::Tag),\n"
            "            other => panic!(\"nom: expected Tag error, got {:?}\", other),\n"
            "        }\n"
            "    }\n"
            "\n"
            "    #[test]\n"
            "    fn pest_parses() {\n"
            "        use super::calc_pest::{CalcParserPest, Rule};\n"
            "        let toks = vec![ (NUM, 1i64), (PLUS, 0), (NUM, 2), (PLUS, 0), (NUM, 3) ];\n"
            "        let mut pairs = CalcParserPest::parse(Rule::Start, &toks)\n"
            "            .expect(\"pest parse ok\");\n"
            "        let p = pairs.next().expect(\"pest: at least one pair\");\n"
            "        assert_eq!(p.as_rule(), Rule::EOI);\n"
            "        assert_eq!(*p.value(), 6, \"pest: 1+2+3 should be 6\");\n"
            "    }\n"
            "\n"
            "    #[test]\n"
            "    fn chumsky_parses() {\n"
            "        use super::calc_chumsky::calc;\n"
            "        let toks = vec![ (NUM, 1i64), (PLUS, 0), (NUM, 2), (PLUS, 0), (NUM, 3) ];\n"
            "        let val = calc().parse(&toks).expect(\"chumsky parse ok\");\n"
            "        assert_eq!(val, 6, \"chumsky: 1+2+3 should be 6, got {}\", val);\n"
            "    }\n"
            "\n"
            "    #[test]\n"
            "    fn chumsky_error() {\n"
            "        use super::calc_chumsky::calc;\n"
            "        let toks = vec![ (PLUS, 0i64) ];\n"
            "        let errs = calc().parse(&toks).expect_err(\"chumsky should fail\");\n"
            "        assert_eq!(errs.len(), 1);\n"
            "        assert_eq!(errs[0].at, 0);\n"
            "    }\n"
            "}\n",
            f);
        fclose(f);
    }

    /* Build + run. */
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
    fprintf(stdout, "OK: emit_rust_skins_v14 (5 sub-tests: nom/pest/chumsky)\n");
    return 0;
}
