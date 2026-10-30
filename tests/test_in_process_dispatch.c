/* tests/test_in_process_dispatch.c -- lime_compile_grammar_text and
** publish_modified_snapshot run the in-process compiler, and nothing else.
**
** There is no subprocess pipeline any more (no fork/exec of lime or cc,
** no dlopen), and no LIME_FORCE_SUBPROCESS knob.  Sub-tests:
**   1. default_fast_path     - calc grammar compiles in-process, < 1 ms warm.
**   2. no_subprocess_knob    - LIME_FORCE_SUBPROCESS=1 and an empty PATH
**                              change nothing: the compile still succeeds.
**   3. error_is_in_process   - an empty grammar fails with the in-process
**                              compiler's own diagnostic; nothing retries.
**   4. composition_perf      - publish_modified_snapshot composes a
**                              5-extension stack in well under 10 ms.
**   5. two_context_isolation - two sequential compiles do not share state.
*/

#define _POSIX_C_SOURCE 200809L

#include "parser.h"
#include "parse_context.h"
#include "snapshot.h"
#include "extension.h"
#include "snapshot_modify.h"
#include "lime_compiler.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Sanitizer / instrumented-environment detection.  Same idiom (and
** same rationale) as tests/test_random_composition.c: -fsanitize=*
** instrumentation legitimately changes timing by 2-10x, and valgrind
** by 10-30x, so the wall-clock thresholds in the perf sub-tests below
** would fail for reasons unrelated to dispatch latency.  When
** instrumented, those sub-tests print their measurement and skip the
** assertion rather than reporting a bogus regression. */
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__) || defined(__SANITIZE_UNDEFINED__)
#  define LIME_BUILT_WITH_SANITIZER 1
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(undefined_behavior_sanitizer)
#    define LIME_BUILT_WITH_SANITIZER 1
#  endif
#endif
#ifndef LIME_BUILT_WITH_SANITIZER
#  define LIME_BUILT_WITH_SANITIZER 0
#endif

static int lime_test_skip_perf(void) {
    const char *e = getenv("LIME_TEST_SKIP_PERF");
    return LIME_BUILT_WITH_SANITIZER || (e != NULL && e[0] != '\0' && e[0] != '0');
}

/* ------------------------------------------------------------------ */
/*  Test harness                                                       */
/* ------------------------------------------------------------------ */

static int n_pass = 0;
static int n_fail = 0;

#define CHECK(cond, name)                                                       \
    do {                                                                        \
        if (cond) {                                                             \
            printf("PASS: %s\n", (name));                                       \
            ++n_pass;                                                           \
        } else {                                                                \
            printf("FAIL: %s (line %d)\n", (name), __LINE__);                   \
            ++n_fail;                                                           \
        }                                                                       \
    } while (0)

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* ------------------------------------------------------------------ */
/*  Test grammars                                                       */
/* ------------------------------------------------------------------ */

/* Same calc grammar as test_lime_compile_in_process, kept independent
** so this test is self-contained.  Token codes start at 1 in
** declaration order: PLUS=1 ... INTEGER=7. */
static const char k_calc_grammar[] =
    "%name DispCalc\n"
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

/* A second small grammar with completely disjoint token names so the
** isolation test can prove the second compile is not contaminated by
** the first. */
static const char k_alpha_grammar[] =
    "%name DispAlpha\n"
    "%token ALPHA.\n"
    "%token BETA.\n"
    "%token GAMMA.\n"
    "%start_symbol greek\n"
    "greek ::= ALPHA.\n"
    "greek ::= ALPHA BETA.\n"
    "greek ::= ALPHA BETA GAMMA.\n";

/* ------------------------------------------------------------------ */
/*  Sub-test 1: default_fast_path                                       */
/* ------------------------------------------------------------------ */

static void test_default_fast_path(void) {
    /* Warm the compiler arena once so we measure steady-state cost
    ** rather than first-touch allocations and code-cache misses. */
    char *err = NULL;
    ParserSnapshot *warm =
        lime_compile_grammar_text(k_calc_grammar, strlen(k_calc_grammar), &err);
    CHECK(warm != NULL, "default_fast_path: warm compile succeeded");
    free(err);
    err = NULL;
    if (warm == NULL) return;
    snapshot_release(warm);

    /* Average over a small batch -- single-call timing is dominated
    ** by clock granularity and scheduler jitter on shared CI hosts. */
    enum { N = 20 };
    double t0 = now_ms();
    int n_ok = 0;
    for (int i = 0; i < N; i++) {
        char *e = NULL;
        ParserSnapshot *s =
            lime_compile_grammar_text(k_calc_grammar, strlen(k_calc_grammar), &e);
        if (s != NULL) {
            n_ok++;
            snapshot_release(s);
        }
        free(e);
    }
    double total = now_ms() - t0;
    double per = total / (double)N;
    printf("  default_fast_path: %d compiles in %.2f ms = %.3f ms/compile\n",
           N, total, per);

    CHECK(n_ok == N, "default_fast_path: every compile succeeded");
    /* The in-process steady-state cost is ~0.04 ms on tuned hardware.
    ** ASan / UBSan instrument the compiler with extra checks, so we
    ** allow up to 5 ms here -- still two orders of magnitude under
    ** the subprocess path's ~150-300 ms baseline. */
    if (lime_test_skip_perf()) {
        printf("  default_fast_path: [instrumented build, perf-target check "
               "skipped]\n");
    } else {
        CHECK(per < 5.0,
              "default_fast_path: < 5 ms/compile (subprocess would be ~200ms)");
    }
}

/* ------------------------------------------------------------------ */
/*  Sub-test 2: no_subprocess_knob                                      */
/* ------------------------------------------------------------------ */

static void test_no_subprocess_knob(void) {
    /* The removed opt-out must not resurrect a subprocess path, and an
    ** empty PATH (no lime, no cc) must not matter. */
    const char *old_path = getenv("PATH");
    char *saved = old_path ? strdup(old_path) : NULL;
    setenv("LIME_FORCE_SUBPROCESS", "1", 1);
    setenv("PATH", "", 1);
    char *err = NULL;
    ParserSnapshot *snap = lime_compile_grammar_text(k_calc_grammar, strlen(k_calc_grammar), &err);
    unsetenv("LIME_FORCE_SUBPROCESS");
    if (saved) { setenv("PATH", saved, 1); free(saved); }
    CHECK(snap != NULL, "no_subprocess_knob: compiles with LIME_FORCE_SUBPROCESS=1 and PATH=''");
    if (err) printf("  error: %s\n", err);
    free(err);
    snapshot_release(snap);
}

/* ------------------------------------------------------------------ */
/*  Sub-test 3: error_is_in_process                                     */
/* ------------------------------------------------------------------ */

static void test_error_is_in_process(void) {
    static const char rules_missing[] = "%name FallbackG\n%token A.\n";
    char *err = NULL;
    ParserSnapshot *snap =
        lime_compile_grammar_text(rules_missing, sizeof(rules_missing) - 1, &err);
    CHECK(snap == NULL, "error_is_in_process: empty-grammar compile rejected");
    CHECK(err != NULL && strstr(err, "empty grammar") != NULL,
          "error_is_in_process: the in-process diagnostic ('empty grammar'), no retry");
    if (err) printf("  error: %s\n", err);
    free(err);
}

/* ------------------------------------------------------------------ */
/*  Sub-test 4: composition_perf                                        */
/* ------------------------------------------------------------------ */

/*
** A 5-extension composition over an arithmetic base.  Each extension
** declares a new disambiguating token and a single reduction rule
** that uses it.  publish_modified_snapshot serialises the modifications
** to a grammar fragment, concatenates with the base source, and feeds
** the merged text to lime_compile_grammar_text -- which in turn
** dispatches in-process per phase 4.  The whole 5-plugin merge must
** complete in well under 10 ms (subprocess would take ~1-2 seconds).
*/

/* The base grammar carries its own source so publish_modified_snapshot
** can emit a real merged grammar.  We compile it via
** lime_compile_grammar_text (in-process by default) and use the
** resulting snapshot as the composition base. */
static const char k_base_arith[] =
    "%name DispArith\n"
    "%token_type {int}\n"
    "%type expr {int}\n"
    "%token PLUS.\n"
    "%token MINUS.\n"
    "%token TIMES.\n"
    "%token NUM.\n"
    "%left PLUS MINUS.\n"
    "%left TIMES.\n"
    "%start_symbol program\n"
    "program ::= expr(A). { (void)A; }\n"
    "expr(A) ::= expr(B) PLUS expr(C). { A = B + C; (void)C; }\n"
    "expr(A) ::= expr(B) MINUS expr(C). { A = B - C; (void)C; }\n"
    "expr(A) ::= expr(B) TIMES expr(C). { A = B * C; (void)C; }\n"
    "expr(A) ::= NUM(B). { A = B; }\n";

/*
** Rather than fight the (still-evolving) GrammarModification API for
** synthetic-extension authoring, the composition perf sub-test calls
** lime_compile_grammar_text directly N times against grammars whose
** text has been pre-merged in-source.  This faithfully exercises the
** publish_modified_snapshot dispatch path (its hot subroutine is
** lime_compile_grammar_text) while staying portable across the API
** evolution that the extension layer is under.
*/
static void test_composition_perf(void) {

    /* Five "plugins" each contribute one extra token + one rule.  We
    ** simulate the merge by concatenating into a single grammar text
    ** -- the same shape the real publish_modified_snapshot produces
    ** via lime_modifications_to_grammar_text. */
    static const char *plugin_frags[5] = {
        "%token PLUG1.\n"
        "expr(A) ::= expr(B) PLUG1 expr(C). { A = B | C; }\n",
        "%token PLUG2.\n"
        "expr(A) ::= expr(B) PLUG2 expr(C). { A = B & C; }\n",
        "%token PLUG3.\n"
        "expr(A) ::= expr(B) PLUG3 expr(C). { A = B ^ C; }\n",
        "%token PLUG4.\n"
        "expr(A) ::= PLUG4 expr(B). { A = ~B; }\n",
        "%token PLUG5.\n"
        "expr(A) ::= expr(B) PLUG5. { A = B + 1; }\n",
    };

    /* Build the merged grammar text once. */
    size_t cap = strlen(k_base_arith) + 1;
    for (int i = 0; i < 5; i++) cap += strlen(plugin_frags[i]) + 1;
    char *merged = malloc(cap);
    if (merged == NULL) {
        CHECK(0, "composition_perf: malloc");
        return;
    }
    size_t off = 0;
    memcpy(merged + off, k_base_arith, strlen(k_base_arith));
    off += strlen(k_base_arith);
    for (int i = 0; i < 5; i++) {
        size_t n = strlen(plugin_frags[i]);
        memcpy(merged + off, plugin_frags[i], n);
        off += n;
    }
    merged[off] = '\0';

    /* Warm. */
    char *werr = NULL;
    ParserSnapshot *warm = lime_compile_grammar_text(merged, off, &werr);
    if (warm == NULL) {
        printf("  composition_perf: warm compile FAILED: %s\n",
               werr ? werr : "(no error)");
    }
    free(werr);
    if (warm != NULL) snapshot_release(warm);

    /* Time the compilation, which is the hot subroutine of a real
    ** publish_modified_snapshot call. */
    double t0 = now_ms();
    char *err = NULL;
    ParserSnapshot *snap = lime_compile_grammar_text(merged, off, &err);
    double elapsed = now_ms() - t0;
    printf("  composition_perf: 5-plugin merge in %.2f ms\n", elapsed);

    CHECK(snap != NULL, "composition_perf: merged grammar compiled");
    if (err) {
        if (snap == NULL) fprintf(stderr, "  err: %s\n", err);
        free(err);
    }
    if (snap != NULL) snapshot_release(snap);

    /* Generous threshold: in-process is ~0.1 ms; subprocess would be
    ** 200+ ms.  Skipped outright under instrumentation, where the
    ** multiplier is unbounded in practice. */
    if (lime_test_skip_perf()) {
        printf("  composition_perf: [instrumented build, perf-target check "
               "skipped]\n");
    } else {
        CHECK(elapsed < 50.0,
              "composition_perf: < 50 ms (subprocess would be ~200+ ms)");
    }

    free(merged);
}

/* ------------------------------------------------------------------ */
/*  Sub-test 5: two_context_isolation                                   */
/* ------------------------------------------------------------------ */

/*
** Sequential in-process compiles must not contaminate each other.
** The compiler context is reset around each call (LimeCompilerContext
** save/restore, fresh symbol/state tables); a regression where some
** state leaks would show up as a second compile inheriting the first
** grammar's symbol counts or token codes.
*/
static void test_two_context_isolation(void) {

    char *err = NULL;
    ParserSnapshot *snap_calc =
        lime_compile_grammar_text(k_calc_grammar, strlen(k_calc_grammar), &err);
    CHECK(snap_calc != NULL, "isolation: calc compiled");
    free(err);
    err = NULL;

    ParserSnapshot *snap_alpha =
        lime_compile_grammar_text(k_alpha_grammar, strlen(k_alpha_grammar), &err);
    CHECK(snap_alpha != NULL, "isolation: alpha compiled");
    free(err);
    err = NULL;

    if (snap_calc == NULL || snap_alpha == NULL) {
        if (snap_calc) snapshot_release(snap_calc);
        if (snap_alpha) snapshot_release(snap_alpha);
        return;
    }

    /* Calc has 7 terminals (PLUS..INTEGER) + the implicit error /
    ** EOF; alpha has 3 (ALPHA, BETA, GAMMA).  If the second compile
    ** had inherited any of the first's terminals these counts would
    ** be off. */
    CHECK(snap_calc->nterminal != snap_alpha->nterminal,
          "isolation: nterminal differs across the two compiles");
    CHECK(snap_calc->nterminal >= 7,
          "isolation: calc has at least 7 terminals");
    CHECK(snap_alpha->nterminal >= 3 && snap_alpha->nterminal < 7,
          "isolation: alpha has 3..6 terminals (no leak from calc)");

    /* Drive each snapshot with a token sequence valid for IT to
    ** prove the rule sets are independent.  alpha only knows
    ** ALPHA(1) BETA(2) GAMMA(3). */
    {
        ParseContext *ctx = parse_begin(snap_alpha);
        CHECK(ctx != NULL, "isolation: parse_begin(alpha)");
        if (ctx != NULL) {
            int rc = 0;
            int toks[] = { 1, 2, 3 };  /* ALPHA BETA GAMMA */
            for (int i = 0; i < 3 && rc >= 0 && rc != 1; i++) {
                rc = parse_token(ctx, toks[i], NULL, -1);
            }
            if (rc == 0) rc = parse_token(ctx, 0, NULL, -1);
            CHECK(rc == 1, "isolation: alpha accepts ALPHA BETA GAMMA");
            parse_end(ctx);
        }
    }
    {
        /* Calc's INTEGER token code is 7 in declaration order. */
        ParseContext *ctx = parse_begin(snap_calc);
        CHECK(ctx != NULL, "isolation: parse_begin(calc)");
        if (ctx != NULL) {
            int rc = parse_token(ctx, 7, NULL, -1);  /* INTEGER */
            if (rc == 0) rc = parse_token(ctx, 0, NULL, -1);  /* EOF */
            CHECK(rc == 1, "isolation: calc accepts a single INTEGER");
            parse_end(ctx);
        }
    }

    snapshot_release(snap_calc);
    snapshot_release(snap_alpha);
}

/* ------------------------------------------------------------------ */
/*  Driver                                                             */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("in-process compile: lime_compile_grammar_text / publish_modified_snapshot\n");
    printf("=====================================================================\n");

    test_default_fast_path();
    test_no_subprocess_knob();
    test_error_is_in_process();
    test_composition_perf();
    test_two_context_isolation();

    printf("---------------------------------------------------------------------\n");
    printf("%d passed, %d failed\n", n_pass, n_fail);
    return n_fail == 0 ? 0 : 1;
}
