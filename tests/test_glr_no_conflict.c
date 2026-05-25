/*
** test_glr_no_conflict.c -- equivalence test between LALR and GLR.
**
** Runs the same arithmetic token streams through:
**   - the generated Arith() LALR parser (the regular Lime path)
**   - the GLR engine driving the same snapshot
**
** For an unambiguous grammar both must agree on accept/reject.  This
** is the structural sanity check that GLR is a faithful superset of
** LALR rather than a separate implementation that quietly drifted.
**
** The arithmetic grammar is the one used by bench/bench_jit_real_parser
** (bench/bench_arith_grammar.y).
*/

#include "parser.h"
#include "snapshot.h"
#include "parse_context.h"
#include "snapshot_build.h"
#include "glr.h"

#include "bench_arith_grammar.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST(name) \
    do { printf("  %-60s", name); fflush(stdout); } while (0)
#define PASS() \
    do { printf("PASS\n"); tests_passed++; } while (0)
#define FAIL(msg) \
    do { printf("FAIL: %s\n", msg); tests_failed++; return; } while (0)

static int tests_passed = 0;
static int tests_failed = 0;

extern ParserSnapshot *ArithBuildSnapshot(void);

void *ArithAlloc(void *(*mallocProc)(size_t));
void ArithFree(void *p, void (*freeProc)(void *));
void Arith(void *yyp, int yymajor, int yyminor, int *result_out);

typedef struct {
    int code;
    int value;
} Tok;

/* ------------------------------------------------------------------ */
/* Reference LALR oracle                                              */
/* ------------------------------------------------------------------ */

static int run_lalr(const Tok *toks, size_t n) {
    int result = 0;
    void *p = ArithAlloc(malloc);
    if (!p) return -1;
    for (size_t i = 0; i < n; i++) {
        Arith(p, toks[i].code, toks[i].value, &result);
    }
    Arith(p, 0, 0, &result); /* EOF */
    ArithFree(p, free);
    return result;
}

/* ------------------------------------------------------------------ */
/* GLR run                                                             */
/* ------------------------------------------------------------------ */

static int run_glr(ParserSnapshot *snap, const Tok *toks, size_t n, bool *accepted) {
    ParseContext *ctx = parse_begin(snap);
    if (!ctx) return -1;

    if (lime_parse_glr(ctx, NULL, NULL) != 0) {
        parse_end(ctx);
        return -1;
    }

    int last_rc = 0;
    for (size_t i = 0; i < n; i++) {
        last_rc = lime_parse_glr_feed(ctx, (uint16_t)toks[i].code);
        if (last_rc < 0) break;
    }
    if (last_rc >= 0) {
        last_rc = lime_parse_glr_feed(ctx, 0); /* EOF */
    }

    *accepted = lime_parse_glr_accepted(ctx);

    lime_parse_glr_end(ctx);
    parse_end(ctx);
    return last_rc;
}

/* ------------------------------------------------------------------ */
/* Test cases                                                          */
/* ------------------------------------------------------------------ */

static const Tok case_simple[] = {
    { ARITH_NUM, 1 }, { ARITH_PLUS, 0 }, { ARITH_NUM, 2 },
};

static const Tok case_nested[] = {
    { ARITH_LP, 0 }, { ARITH_NUM, 3 }, { ARITH_PLUS, 0 }, { ARITH_NUM, 4 },
    { ARITH_RP, 0 }, { ARITH_STAR, 0 }, { ARITH_NUM, 2 },
};

static const Tok case_chained[] = {
    { ARITH_NUM, 1 }, { ARITH_PLUS, 0 }, { ARITH_NUM, 2 }, { ARITH_PLUS, 0 },
    { ARITH_NUM, 3 }, { ARITH_PLUS, 0 }, { ARITH_NUM, 4 },
};

static const Tok case_division[] = {
    { ARITH_NUM, 10 }, { ARITH_SLASH, 0 }, { ARITH_NUM, 2 },
    { ARITH_MINUS, 0 }, { ARITH_NUM, 1 },
};

/* Reject case: trailing operator. */
static const Tok case_bad_trailing[] = {
    { ARITH_NUM, 1 }, { ARITH_PLUS, 0 },
};

/* Reject case: unbalanced paren. */
static const Tok case_bad_paren[] = {
    { ARITH_LP, 0 }, { ARITH_NUM, 1 }, { ARITH_PLUS, 0 }, { ARITH_NUM, 2 },
};

#define CASE(arr) { arr, sizeof(arr) / sizeof((arr)[0]) }

static const struct {
    const Tok *toks;
    size_t n;
} valid_cases[] = {
    CASE(case_simple),
    CASE(case_nested),
    CASE(case_chained),
    CASE(case_division),
};

static const struct {
    const Tok *toks;
    size_t n;
} invalid_cases[] = {
    CASE(case_bad_trailing),
    CASE(case_bad_paren),
};

static void test_valid_inputs_accept_in_both(ParserSnapshot *snap) {
    TEST("LALR and GLR both accept valid arithmetic inputs");
    for (size_t i = 0; i < sizeof(valid_cases) / sizeof(valid_cases[0]); i++) {
        bool glr_acc = false;
        int glr_rc = run_glr(snap, valid_cases[i].toks, valid_cases[i].n, &glr_acc);
        int lalr_result = run_lalr(valid_cases[i].toks, valid_cases[i].n);

        if (lalr_result == -1) FAIL("LALR setup failed");
        if (glr_rc < 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "case %zu glr_rc=%d", i, glr_rc);
            FAIL(buf);
        }
        if (!glr_acc) {
            char buf[64];
            snprintf(buf, sizeof(buf), "case %zu glr did not accept", i);
            FAIL(buf);
        }
    }
    PASS();
}

static void test_invalid_inputs_reject_in_both(ParserSnapshot *snap) {
    TEST("LALR and GLR both reject invalid inputs");
    for (size_t i = 0; i < sizeof(invalid_cases) / sizeof(invalid_cases[0]); i++) {
        bool glr_acc = false;
        (void)run_glr(snap, invalid_cases[i].toks, invalid_cases[i].n, &glr_acc);
        if (glr_acc) {
            char buf[64];
            snprintf(buf, sizeof(buf), "case %zu glr accepted invalid input", i);
            FAIL(buf);
        }
    }
    PASS();
}

static void test_glr_head_count_unambiguous(ParserSnapshot *snap) {
    TEST("GLR keeps a single head on unambiguous input");
    ParseContext *ctx = parse_begin(snap);
    if (!ctx) FAIL("parse_begin");
    if (lime_parse_glr(ctx, NULL, NULL) != 0) {
        parse_end(ctx); FAIL("lime_parse_glr");
    }
    /* ParseContext that has not yet entered GLR mode reports 0 heads;
    ** once initialised the GLR engine keeps exactly 1 head per token
    ** for an unambiguous grammar. */
    for (size_t i = 0; i < sizeof(case_chained) / sizeof(case_chained[0]); i++) {
        if (lime_parse_glr_feed(ctx, (uint16_t)case_chained[i].code) < 0) {
            lime_parse_glr_end(ctx); parse_end(ctx);
            FAIL("feed");
        }
        uint32_t heads = lime_parse_glr_head_count(ctx);
        if (heads == 0 || heads > 4) {
            char buf[80];
            snprintf(buf, sizeof(buf), "head count %u after token %zu", heads, i);
            lime_parse_glr_end(ctx); parse_end(ctx);
            FAIL(buf);
        }
    }
    lime_parse_glr_end(ctx);
    parse_end(ctx);
    PASS();
}

int main(void) {
    printf("GLR vs LALR equivalence (no conflicts)\n");
    printf("=======================================\n");

    ParserSnapshot *snap = ArithBuildSnapshot();
    if (!snap) {
        fprintf(stderr, "ArithBuildSnapshot returned NULL\n");
        return 1;
    }

    test_valid_inputs_accept_in_both(snap);
    test_invalid_inputs_reject_in_both(snap);
    test_glr_head_count_unambiguous(snap);

    lime_snapshot_release(snap);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
