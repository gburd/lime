/* tests/test_lime_compile_in_process.c -- ROADMAP item 1, phase 3.
**
** Phase 3 of the in-process LALR rebuild library.  Adds the public
** function lime_compile_grammar_in_process(text, len, &snap, &err)
** which takes grammar text and produces a ParserSnapshot directly
** in-process, bypassing the lime + cc + dlopen subprocess pipeline
** used today by lime_compile_grammar_text() in src/snapshot_create.c.
**
** Sub-tests (8 total):
**   1. basic_compile           - calc grammar -> snap, drive 1+2*3
**   2. error_syntax            - malformed grammar -> NULL snap, error
**   3. error_conflict          - unresolved LALR conflict surfaces
**   4. error_bad_directive     - %unknown_directive -> error
**   5. functional_equivalence  - in-process vs subprocess parse results
**                                across a 6-stream test corpus.  Skipped
**                                when LIME_BIN/`lime` is unavailable.
**   6. isolation               - back-to-back compiles produce
**                                independently-parsing snapshots.
**   7. perf_smoke              - 100 compiles in a tight loop;
**                                reports ms/compile so a regression
**                                vs the subprocess path is obvious.
**   8. bad_arguments           - NULL/zero-length args rejected cleanly.
**
** Build: -DLIME_TEST_HARNESS -DLIME_HAVE_SNAPSHOT_BUILD ; #include "../lime.c"
** Link:  lime_parser_dep (snapshot_build, parse_engine, snapshot_create)
**        lime_lex_compiler_lib (resolves the .lex frontend stub).
*/

#include "../lime.c"

/* The active-context macros at the top of lime.c rewire bare
** identifiers like memChunkList onto field accesses through
** lime_active_ctx; they would mangle test-side struct member
** references.  Drop them now that lime.c is fully compiled. */
#undef memChunkList
#undef x1a
#undef x2a
#undef x3a
#undef x4a
#undef plink_freelist
#undef actionfreelist
#undef current
#undef currentend
#undef basis
#undef basisend
#undef nDefine
#undef nDefineUsed
#undef azDefine
#undef bDefineUsed

#include "lime_compiler.h"
#include "snapshot.h"
#include "parse_context.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int n_pass = 0;
static int n_fail = 0;

#define CHECK(cond, name) do {                                          \
    if (cond) {                                                         \
        printf("PASS: %s\n", (name));                                   \
        ++n_pass;                                                       \
    } else {                                                            \
        printf("FAIL: %s (line %d)\n", (name), __LINE__);               \
        ++n_fail;                                                       \
    }                                                                   \
} while (0)

/* Forward decl for the subprocess path used by test 5. */
extern ParserSnapshot *lime_compile_grammar_text(const char *grammar_text,
                                                 size_t len, char **error);

/* ------------------------------------------------------------------ */
/*  Test grammars                                                       */
/* ------------------------------------------------------------------ */

/* Minimal calc grammar.  Token codes start at 1 in declaration
** order: PLUS=1, MINUS=2, TIMES=3, DIVIDE=4, LPAREN=5, RPAREN=6,
** INTEGER=7. */
static const char k_calc_grammar[] =
    "%name TestCalc\n"
    "%token_type {int}\n"
    "%type expr {int}\n"
    "%token PLUS.\n"
    "%token MINUS.\n"
    "%token TIMES.\n"
    "%token DIVIDE.\n"
    "%token LPAREN.\n"
    "%token RPAREN.\n"
    "%token INTEGER.\n"
    "%left PLUS MINUS.\n"
    "%left TIMES DIVIDE.\n"
    "%right UMINUS.\n"
    "%start_symbol program\n"
    "program ::= expr(A). { (void)A; }\n"
    "expr(A) ::= expr(B) PLUS expr(C). { A = B + C; (void)C; }\n"
    "expr(A) ::= expr(B) MINUS expr(C). { A = B - C; (void)C; }\n"
    "expr(A) ::= expr(B) TIMES expr(C). { A = B * C; (void)C; }\n"
    "expr(A) ::= expr(B) DIVIDE expr(C). { A = B / C; (void)C; }\n"
    "expr(A) ::= MINUS expr(B). [UMINUS] { A = -B; }\n"
    "expr(A) ::= LPAREN expr(B) RPAREN. { A = B; }\n"
    "expr(A) ::= INTEGER(B). { A = B; }\n";

#define CALC_PLUS    1
#define CALC_MINUS   2
#define CALC_TIMES   3
#define CALC_DIVIDE  4
#define CALC_LPAREN  5
#define CALC_RPAREN  6
#define CALC_INTEGER 7

/* Distinct two-token grammar used in test 6 (isolation).  Token
** codes also start at 1; FOO=1, BAR=2. */
static const char k_foobar_grammar[] =
    "%name TestFooBar\n"
    "%token FOO.\n"
    "%token BAR.\n"
    "%start_symbol program\n"
    "program ::= seq.\n"
    "seq ::= seq FOO.\n"
    "seq ::= seq BAR.\n"
    "seq ::= FOO.\n"
    "seq ::= BAR.\n";

#define FOOBAR_FOO 1
#define FOOBAR_BAR 2

/* ------------------------------------------------------------------ */
/*  Parse driver                                                        */
/* ------------------------------------------------------------------ */

/* Drive a token sequence through a snapshot and return the final
** parse_token outcome: 1 = accept, < 0 = error, 0 = mid-parse
** (treated as caller-supplied EOF). */
static int run_parse(ParserSnapshot *snap, const int *tokens, int ntokens) {
    ParseContext *ctx = parse_begin(snap);
    if (ctx == NULL) return -1;
    int rc = 0;
    for (int i = 0; i < ntokens; i++) {
        rc = parse_token(ctx, tokens[i], NULL, -1);
        if (rc < 0 || rc == 1) break;
    }
    if (rc == 0) rc = parse_token(ctx, 0, NULL, -1);
    parse_end(ctx);
    return rc;
}

/* ------------------------------------------------------------------ */
/*  Sub-test 1: basic compile + drive a parse                           */
/* ------------------------------------------------------------------ */

static void test_basic_compile(void) {
    ParserSnapshot *snap = NULL;
    char *err = NULL;
    int rc = lime_compile_grammar_in_process(k_calc_grammar,
                                             strlen(k_calc_grammar),
                                             &snap, &err);
    CHECK(rc == 0, "basic_compile: rc == 0");
    CHECK(snap != NULL, "basic_compile: snap non-NULL");
    CHECK(err == NULL, "basic_compile: err NULL on success");
    if (rc != 0 || snap == NULL) {
        if (err) { fprintf(stderr, "  unexpected error: %s\n", err); free(err); }
        return;
    }
    /* Sanity-check the snapshot's invariant fields. */
    CHECK(snap->nstate > 0, "basic_compile: nstate > 0");
    CHECK(snap->nrule > 0, "basic_compile: nrule > 0");
    CHECK(snap->nterminal > 0, "basic_compile: nterminal > 0");

    /* "1 + 2 * 3" -- arithmetic grammar with precedence. */
    int toks[] = {
        CALC_INTEGER, CALC_PLUS, CALC_INTEGER, CALC_TIMES, CALC_INTEGER,
    };
    int rcp = run_parse(snap, toks, sizeof(toks)/sizeof(toks[0]));
    CHECK(rcp == 1, "basic_compile: 1 + 2 * 3 accepts");

    /* "1 +" -- truncated, should fail. */
    int bad[] = { CALC_INTEGER, CALC_PLUS };
    int rcb = run_parse(snap, bad, sizeof(bad)/sizeof(bad[0]));
    CHECK(rcb < 0, "basic_compile: '1 +' rejects");

    snapshot_release(snap);
}

/* ------------------------------------------------------------------ */
/*  Sub-test 2: syntactically broken grammar                            */
/* ------------------------------------------------------------------ */

static void test_error_syntax(void) {
    /* Stray garbage after a directive; LHS that does not start with
    ** a letter -- both flagged by parse_top_level_buffer. */
    static const char bad[] =
        "%name BadG\n"
        "%token A.\n"
        "9bad ::= A.\n";
    ParserSnapshot *snap = (ParserSnapshot*)0xdeadbeef;
    char *err = (char*)0xdeadbeef;
    int rc = lime_compile_grammar_in_process(bad, sizeof(bad)-1, &snap, &err);
    CHECK(rc != 0, "error_syntax: rc != 0");
    CHECK(snap == NULL, "error_syntax: snap NULL on error");
    CHECK(err != NULL && err != (char*)0xdeadbeef,
          "error_syntax: err populated");
    if (err && err != (char*)0xdeadbeef) {
        CHECK(strstr(err, "parse failed") != NULL ||
              strstr(err, ":") != NULL,
              "error_syntax: error message describes parse failure");
        free(err);
    }
}

/* ------------------------------------------------------------------ */
/*  Sub-test 3: LALR conflict                                           */
/* ------------------------------------------------------------------ */

static void test_error_conflict(void) {
    /* Classic dangling-else without %expect: ambiguous, FindActions
    ** flags the unresolved shift/reduce as an error. */
    static const char ambig[] =
        "%name AmbigG\n"
        "%token IF.\n"
        "%token THEN.\n"
        "%token ELSE.\n"
        "%token EXPR.\n"
        "%start_symbol stmt\n"
        "stmt ::= IF EXPR THEN stmt.\n"
        "stmt ::= IF EXPR THEN stmt ELSE stmt.\n"
        "stmt ::= EXPR.\n";
    ParserSnapshot *snap = NULL;
    char *err = NULL;
    int rc = lime_compile_grammar_in_process(ambig, sizeof(ambig)-1,
                                             &snap, &err);
    CHECK(rc != 0, "error_conflict: rc != 0");
    CHECK(snap == NULL, "error_conflict: snap NULL");
    CHECK(err != NULL, "error_conflict: err populated");
    if (err) {
        /* Diagnostic surfaced via stderr capture should mention
        ** either "conflict" or the high-level fail reason. */
        CHECK(strstr(err, "conflict") != NULL ||
              strstr(err, "Parsing") != NULL ||
              strstr(err, "shift") != NULL,
              "error_conflict: message names the conflict");
        free(err);
    }
}

/* ------------------------------------------------------------------ */
/*  Sub-test 4: unknown %directive                                      */
/* ------------------------------------------------------------------ */

static void test_error_bad_directive(void) {
    static const char bad[] =
        "%name BadDirG\n"
        "%unknown_directive foo\n"
        "%token A.\n"
        "start ::= A.\n";
    ParserSnapshot *snap = NULL;
    char *err = NULL;
    int rc = lime_compile_grammar_in_process(bad, sizeof(bad)-1, &snap, &err);
    CHECK(rc != 0, "error_bad_directive: rc != 0");
    CHECK(snap == NULL, "error_bad_directive: snap NULL");
    CHECK(err != NULL, "error_bad_directive: err populated");
    if (err) free(err);
}

/* ------------------------------------------------------------------ */
/*  Sub-test 5: functional equivalence with the subprocess pipeline     */
/* ------------------------------------------------------------------ */

/* Token stream + expected parse outcome for the equivalence corpus. */
typedef struct {
    const char *name;
    const int  *tokens;
    int         ntokens;
} TokenStream;

static void test_functional_equivalence(void) {
    /* Feed the same calc grammar through both pipelines.  If
    ** lime_compile_grammar_text fails (no `lime` binary on PATH,
    ** no cc, or otherwise -- common in restricted CI),
    ** report the test as skipped without flagging a fail. */
    char *err_sub = NULL;
    ParserSnapshot *snap_sub =
        lime_compile_grammar_text(k_calc_grammar, strlen(k_calc_grammar), &err_sub);
    if (snap_sub == NULL) {
        printf("SKIP: functional_equivalence (subprocess path unavailable: %s)\n",
               err_sub ? err_sub : "no error message");
        if (err_sub) free(err_sub);
        return;
    }
    if (err_sub) { free(err_sub); err_sub = NULL; }

    char *err_in = NULL;
    ParserSnapshot *snap_in = NULL;
    int rc = lime_compile_grammar_in_process(k_calc_grammar, strlen(k_calc_grammar),
                                             &snap_in, &err_in);
    CHECK(rc == 0 && snap_in != NULL,
          "func_equiv: in-process compile succeeded");
    if (rc != 0 || snap_in == NULL) {
        if (err_in) { fprintf(stderr, "  err: %s\n", err_in); free(err_in); }
        snapshot_release(snap_sub);
        return;
    }

    /* Test corpus: 6 token streams, mix of accept and reject. */
    int s1[] = { CALC_INTEGER };
    int s2[] = { CALC_INTEGER, CALC_PLUS, CALC_INTEGER };
    int s3[] = { CALC_INTEGER, CALC_PLUS, CALC_INTEGER, CALC_TIMES, CALC_INTEGER };
    int s4[] = { CALC_LPAREN, CALC_INTEGER, CALC_PLUS, CALC_INTEGER, CALC_RPAREN,
                 CALC_TIMES, CALC_INTEGER };
    int s5[] = { CALC_INTEGER, CALC_PLUS };               /* truncated */
    int s6[] = { CALC_INTEGER, CALC_PLUS, CALC_PLUS,      /* double-op */
                 CALC_INTEGER };

    TokenStream corpus[] = {
        { "single INTEGER",       s1, sizeof(s1)/sizeof(s1[0]) },
        { "1 + 2",                s2, sizeof(s2)/sizeof(s2[0]) },
        { "1 + 2 * 3",            s3, sizeof(s3)/sizeof(s3[0]) },
        { "(1 + 2) * 3",          s4, sizeof(s4)/sizeof(s4[0]) },
        { "1 +",                  s5, sizeof(s5)/sizeof(s5[0]) },
        { "1 + + 2",              s6, sizeof(s6)/sizeof(s6[0]) },
    };
    int n_corpus = sizeof(corpus)/sizeof(corpus[0]);
    int all_match = 1;
    for (int i = 0; i < n_corpus; i++) {
        int rc_sub = run_parse(snap_sub, corpus[i].tokens, corpus[i].ntokens);
        int rc_in  = run_parse(snap_in,  corpus[i].tokens, corpus[i].ntokens);
        /* Compare classification (accept / reject) rather than the
        ** exact integer return: a non-zero rc means "not accepted",
        ** the precise sub-zero value (-1 vs SYNTAX_ERROR_CODE) is a
        ** parse-engine internal. */
        int sub_accept = (rc_sub == 1);
        int in_accept  = (rc_in == 1);
        if (sub_accept != in_accept) {
            printf("  divergence on '%s': sub=%d in=%d\n",
                   corpus[i].name, rc_sub, rc_in);
            all_match = 0;
        }
    }
    CHECK(all_match, "func_equiv: all 6 token streams agree on accept/reject");

    snapshot_release(snap_in);
    snapshot_release(snap_sub);
}

/* ------------------------------------------------------------------ */
/*  Sub-test 6: isolation                                              */
/* ------------------------------------------------------------------ */

static void test_isolation(void) {
    ParserSnapshot *snap_calc = NULL;
    ParserSnapshot *snap_foo = NULL;
    char *err = NULL;
    int rc;

    rc = lime_compile_grammar_in_process(k_calc_grammar, strlen(k_calc_grammar),
                                         &snap_calc, &err);
    CHECK(rc == 0 && snap_calc != NULL, "isolation: calc compiled");
    if (err) { free(err); err = NULL; }

    rc = lime_compile_grammar_in_process(k_foobar_grammar, strlen(k_foobar_grammar),
                                         &snap_foo, &err);
    CHECK(rc == 0 && snap_foo != NULL, "isolation: foobar compiled");
    if (err) { free(err); err = NULL; }

    if (!snap_calc || !snap_foo) {
        if (snap_calc) snapshot_release(snap_calc);
        if (snap_foo) snapshot_release(snap_foo);
        return;
    }

    /* Calc accepts integer arithmetic; foobar rejects integer
    ** (its codes 1 and 2 are FOO/BAR, not arithmetic ops). */
    int calc_seq[] = { CALC_INTEGER, CALC_PLUS, CALC_INTEGER };
    int rc_calc = run_parse(snap_calc, calc_seq, 3);
    CHECK(rc_calc == 1, "isolation: calc accepts 1+2");

    /* Foobar accepts FOO BAR sequences; drive that through it. */
    int foo_seq[] = { FOOBAR_FOO, FOOBAR_BAR, FOOBAR_FOO };
    int rc_foo = run_parse(snap_foo, foo_seq, 3);
    CHECK(rc_foo == 1, "isolation: foobar accepts FOO BAR FOO");

    /* Cross-check: the calc snapshot's nterminal differs from the
    ** foobar one's.  If the two compilations had cross-contaminated
    ** state, we'd see identical counts. */
    CHECK(snap_calc->nterminal != snap_foo->nterminal,
          "isolation: nterminal differs between independent snapshots");

    snapshot_release(snap_calc);
    snapshot_release(snap_foo);
}

/* ------------------------------------------------------------------ */
/*  Sub-test 7: performance smoke                                       */
/* ------------------------------------------------------------------ */

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static void test_perf_smoke(void) {
    enum { N_ITERS = 100 };
    double t0 = now_ms();
    int n_ok = 0;
    for (int i = 0; i < N_ITERS; i++) {
        ParserSnapshot *snap = NULL;
        char *err = NULL;
        int rc = lime_compile_grammar_in_process(k_calc_grammar,
                                                 strlen(k_calc_grammar),
                                                 &snap, &err);
        if (rc == 0 && snap != NULL) {
            n_ok++;
            snapshot_release(snap);
        }
        if (err) free(err);
    }
    double t1 = now_ms();
    double total_ms = t1 - t0;
    double per_compile_ms = total_ms / (double)N_ITERS;
    printf("  perf_smoke: in-process %d compiles in %.2f ms = %.3f ms/compile\n",
           N_ITERS, total_ms, per_compile_ms);

    /* Side-by-side comparison with the subprocess path when it is
    ** available.  We do far fewer subprocess iterations because
    ** each one forks lime + cc + dlopen and easily dominates the
    ** test's wall-clock budget. */
    enum { N_SUB = 3 };
    char *err_probe = NULL;
    ParserSnapshot *probe =
        lime_compile_grammar_text(k_calc_grammar, strlen(k_calc_grammar),
                                  &err_probe);
    if (probe == NULL) {
        if (err_probe) free(err_probe);
        printf("  perf_smoke: subprocess path unavailable, skipping comparison\n");
    } else {
        snapshot_release(probe);
        if (err_probe) free(err_probe);
        double s0 = now_ms();
        int sub_ok = 0;
        for (int i = 0; i < N_SUB; i++) {
            char *e = NULL;
            ParserSnapshot *s =
                lime_compile_grammar_text(k_calc_grammar,
                                          strlen(k_calc_grammar), &e);
            if (s) { sub_ok++; snapshot_release(s); }
            if (e) free(e);
        }
        double s1 = now_ms();
        double sub_per = (s1 - s0) / (double)N_SUB;
        printf("  perf_smoke: subprocess %d compiles in %.2f ms = %.3f ms/compile\n",
               N_SUB, s1 - s0, sub_per);
        printf("  perf_smoke: in-process speedup vs subprocess: %.1fx\n",
               sub_per / per_compile_ms);
        CHECK(sub_per > per_compile_ms,
              "perf_smoke: in-process strictly faster than subprocess");
    }

    CHECK(n_ok == N_ITERS,
          "perf_smoke: every compile succeeded");
    /* Subprocess pipeline costs ~150-300 ms per compile (fork +
    ** lime + cc + dlopen).  Anything sub-50 ms here is order-of-
    ** magnitude proof.  Generous threshold avoids flaky CI. */
    CHECK(per_compile_ms < 50.0,
          "perf_smoke: < 50 ms/compile (sub-millisecond on tuned machines)");
}

/* ------------------------------------------------------------------ */
/*  Sub-test 8: bad arguments                                           */
/* ------------------------------------------------------------------ */

static void test_bad_arguments(void) {
    ParserSnapshot *snap = (ParserSnapshot*)0x1;
    char *err = NULL;
    int rc;

    rc = lime_compile_grammar_in_process(NULL, 10, &snap, &err);
    CHECK(rc != 0, "bad_args: NULL text rejected");
    CHECK(snap == NULL, "bad_args: NULL text leaves snap NULL");
    CHECK(err != NULL, "bad_args: NULL text populates err");
    if (err) { free(err); err = NULL; }

    snap = (ParserSnapshot*)0x1;
    rc = lime_compile_grammar_in_process(k_calc_grammar, 0, &snap, &err);
    CHECK(rc != 0, "bad_args: zero-length rejected");
    CHECK(snap == NULL, "bad_args: zero-length leaves snap NULL");
    if (err) { free(err); err = NULL; }

    /* NULL out_snapshot is also a hard reject; passing NULL for
    ** error is fine (caller doesn't want diagnostics). */
    rc = lime_compile_grammar_in_process(k_calc_grammar,
                                         strlen(k_calc_grammar), NULL, NULL);
    CHECK(rc != 0, "bad_args: NULL out_snapshot rejected");
}

/* ------------------------------------------------------------------ */
/*  main                                                                */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("ROADMAP-1 phase 3: lime_compile_grammar_in_process()\n");
    printf("====================================================\n");
    test_basic_compile();
    test_error_syntax();
    test_error_conflict();
    test_error_bad_directive();
    test_functional_equivalence();
    test_isolation();
    test_perf_smoke();
    test_bad_arguments();
    printf("\n%d passed, %d failed\n", n_pass, n_fail);
    return n_fail == 0 ? 0 : 1;
}
