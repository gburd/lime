/*
** tests/test_rust_action_dispatch.c -- regression for v1.3.1's deeper
** alt-group propagation fix.
**
** Bug history:
**
**   v1.3.0 had two related bugs that conspired to silently drop the
**   user's `%rust_action { ... }` body at runtime:
**
**   (a) iRule-numbering predicate `rp->code ? i++ : -1` ignored
**       `rust_code`, so a rule with ONLY a %rust_action body got
**       iRule=-1.  emit_rust's lookup tables (lime_emit_rust_rule_info,
**       lime_emit_rust_rule_rust_code) key by iRule, so they emitted
**       the default `lhs = rhs0` body instead of the user's text.
**
**   (b) parseonetoken's WAITING_FOR_DECL_OR_RULE state machine cleared
**       `psp->alt_group_head = 0` pre-emptively when it saw the `%`
**       token, BEFORE %rust_action's parser had a chance to call
**       propagate_alt_group_attach.  For an alt-group like
**       `e ::= NUM(N) | ID(N). %rust_action {...}`, propagate then
**       early-returned with head==NULL, leaving the FIRST alternative
**       with rust_code=NULL while the LAST alternative had it.  At
**       runtime, parse(NUM=5) returned 5 (default passthrough);
**       parse(ID=5) returned 105 (correct).  The customer's
**       e544d96 patch attempted (a) but missed (b); the fix was
**       reverted in v1.3.1 and replaced with the deeper repair:
**       drop the over-eager state-machine clear so propagate runs
**       on the head alternatives too, plus the (a) predicate
**       update plus a noCode=0 clear in the %rust_action branch
**       (so SHIFTREDUCE doesn't collapse the single-rule case).
**
** This test exercises BOTH cases against a runtime cargo build:
**
**   1. Single-rule case: `expr(R) ::= NUM(N). %rust_action { R = N+1; }`
**      parse(NUM=5) must return 6.  Pre-fix: returned 5 (action
**      dropped due to (a)).
**
**   2. Alt-group case: `e(R) ::= NUM(N) | ID(N). %rust_action { R = N+100; }`
**      parse(NUM=5) AND parse(ID=5) must BOTH return 105.  Pre-fix:
**      parse(NUM)=5 (head alt missed propagation due to (b)).
**
** SKIPs cleanly with rc=77 when cargo isn't on PATH.
*/
#include "test_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define CHECK(cond, msg) do {                                             \
    if (!(cond)) {                                                        \
        fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__);           \
        fail++;                                                           \
    }                                                                    \
} while (0)

/* Run a shell command and capture exit status; append cargo's combined
** output to a logfile so failure traces survive into the test report. */
static int run_capture(const char *cmd, const char *logpath) {
    char wrap[2048];
    snprintf(wrap, sizeof(wrap), "%s >> %s 2>&1", cmd, logpath);
    int rc = system(wrap);
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : 1;
}

static void dump_log_on_fail(const char *logpath) {
    fprintf(stderr, "----- cargo log (%s) -----\n", logpath);
    FILE *log = fopen(logpath, "r");
    if (log) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), log)) > 0) {
            fwrite(buf, 1, n, stderr);
        }
        fclose(log);
    }
    fprintf(stderr, "--------------------------\n");
}

int main(int argc, char **argv) {
    int fail = 0;
    if (argc < 2) {
        fprintf(stderr, "usage: %s <path-to-lime>\n", argv[0]);
        return 2;
    }
    const char *lime = argv[1];

    if (system("cargo --version >/dev/null 2>&1") != 0) {
        fprintf(stderr,
            "[rust_action_dispatch] cargo not on PATH; runtime regression "
            "skipped (rc=77).\n");
        return 77;
    }

    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir) tmpdir = "/tmp";
    char dir[256];
    snprintf(dir, sizeof(dir), "%s/test_rust_action_dispatch", tmpdir);
    mkdir(dir, 0755);

    /* Single shared cargo crate.  Two grammars compiled into different
    ** module names (single.rs / alt.rs); both #[path]'d into lib.rs. */
    char single_grammar[512], alt_grammar[512];
    char single_rs[512], alt_rs[512];
    char libpath[512], cargopath[512], logpath[512];
    snprintf(single_grammar, sizeof(single_grammar), "%s/single.lime", dir);
    snprintf(alt_grammar,    sizeof(alt_grammar),    "%s/alt.lime",    dir);
    snprintf(single_rs,      sizeof(single_rs),      "%s/single.rs",   dir);
    snprintf(alt_rs,         sizeof(alt_rs),         "%s/alt.rs",      dir);
    snprintf(libpath,        sizeof(libpath),        "%s/lib.rs",      dir);
    snprintf(cargopath,      sizeof(cargopath),      "%s/Cargo.toml",  dir);
    snprintf(logpath,        sizeof(logpath),        "%s/cargo.log",   dir);
    /* Reset log between runs. */
    { FILE *f = fopen(logpath, "w"); if (f) fclose(f); }

    /* Single-rule grammar -- the simpler %rust_action case. */
    {
        FILE *f = fopen(single_grammar, "w");
        if (!f) { perror(single_grammar); return 2; }
        fputs(
            "%name single\n"
            "%token_type {i64}\n"
            "%default_type {i64}\n"
            "%token NUM.\n"
            "%start_symbol prog\n"
            "prog ::= expr.\n"
            "expr(R) ::= NUM(N).\n"
            "    %rust_action { R = N + 1; }\n",
            f);
        fclose(f);
    }

    /* Alt-group grammar -- the `e ::= a | b. %rust_action {...}` case
    ** where propagate_alt_group_attach must reach all alternatives. */
    {
        FILE *f = fopen(alt_grammar, "w");
        if (!f) { perror(alt_grammar); return 2; }
        fputs(
            "%name alt\n"
            "%token_type {i64}\n"
            "%default_type {i64}\n"
            "%token NUM ID.\n"
            "%start_symbol prog\n"
            "prog ::= e.\n"
            "e(R) ::= NUM(N) | ID(N).\n"
            "    %rust_action { R = N + 100; }\n",
            f);
        fclose(f);
    }

    /* Generate both .rs files. */
    {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd),
                 "cd %s && %s --target=rust single.lime && "
                 "%s --target=rust alt.lime",
                 dir, lime, lime);
        int rc = run_capture(cmd, logpath);
        CHECK(rc == 0, "lime --target=rust succeeded for both grammars");
    }

    /* Cargo crate scaffolding. */
    {
        FILE *f = fopen(cargopath, "w");
        if (!f) { perror(cargopath); return 2; }
        fputs(
            "[package]\n"
            "name = \"rust_action_dispatch_test\"\n"
            "version = \"0.0.0\"\n"
            "edition = \"2021\"\n"
            "\n"
            "[lib]\n"
            "path = \"lib.rs\"\n"
            "\n"
            "[dependencies]\n",
            f);
        fclose(f);
    }
    {
        FILE *f = fopen(libpath, "w");
        if (!f) { perror(libpath); return 2; }
        fputs(
            "#![allow(non_snake_case)]\n"
            "#![allow(non_camel_case_types)]\n"
            "#![allow(dead_code)]\n"
            "#![allow(unused_variables)]\n"
            "#![allow(unused_assignments)]\n"
            "pub mod single;\n"
            "pub mod alt;\n"
            "\n"
            "#[cfg(test)]\n"
            "mod tests {\n"
            "    /// Single-rule case: %rust_action body must fire and\n"
            "    /// return N+1.\n"
            "    #[test]\n"
            "    fn single_rule_action_fires() {\n"
            "        use super::single::*;\n"
            "        const NUM: u16 = 1;\n"
            "        let mut p = singleParser::new();\n"
            "        p.push(NUM, 5).unwrap();\n"
            "        p.push(0, 0).unwrap();\n"
            "        p.finalize().unwrap();\n"
            "        assert_eq!(p.final_value, 6,\n"
            "                   \"single-rule: expected 6 (R = N+1), got {}\",\n"
            "                   p.final_value);\n"
            "    }\n"
            "    /// Alt-group case, NUM alternative: head-of-group must\n"
            "    /// have rust_code propagated and fire R = N+100.\n"
            "    #[test]\n"
            "    fn alt_group_head_alt_fires() {\n"
            "        use super::alt::*;\n"
            "        const NUM: u16 = 1;\n"
            "        let mut p = altParser::new();\n"
            "        p.push(NUM, 5).unwrap();\n"
            "        p.push(0, 0).unwrap();\n"
            "        p.finalize().unwrap();\n"
            "        assert_eq!(p.final_value, 105,\n"
            "                   \"alt-group head (NUM): expected 105, got {}\",\n"
            "                   p.final_value);\n"
            "    }\n"
            "    /// Alt-group case, ID alternative: tail-of-group already\n"
            "    /// had rust_code attached pre-fix; this test guards\n"
            "    /// against future regressions to the tail path.\n"
            "    #[test]\n"
            "    fn alt_group_tail_alt_fires() {\n"
            "        use super::alt::*;\n"
            "        const ID: u16 = 2;\n"
            "        let mut p = altParser::new();\n"
            "        p.push(ID, 5).unwrap();\n"
            "        p.push(0, 0).unwrap();\n"
            "        p.finalize().unwrap();\n"
            "        assert_eq!(p.final_value, 105,\n"
            "                   \"alt-group tail (ID): expected 105, got {}\",\n"
            "                   p.final_value);\n"
            "    }\n"
            "}\n",
            f);
        fclose(f);
    }

    /* Build + run the three cargo tests. */
    {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "cd %s && cargo test --quiet", dir);
        int rc = run_capture(cmd, logpath);
        if (rc != 0) {
            fprintf(stderr, "FAIL: cargo test (line %d)\n", __LINE__);
            dump_log_on_fail(logpath);
            fail++;
        }
    }

    if (fail) {
        fprintf(stderr, "FAIL: %d check(s) failed (artefacts in %s)\n",
                fail, dir);
        return 1;
    }
    fprintf(stdout, "OK: rust_action_dispatch (3 sub-tests)\n");
    return 0;
}
