/*
** test_jit_parse_equivalence.c -- proof that the JIT-accelerated and
** table-driven parse_token paths produce identical outcomes on the
** same input.
**
** Drives the same input sequences through two ParseContext instances:
**   - one pinned to the base snapshot (table-driven)
**   - one pinned to a JIT-attached copy of that snapshot
**
** For every well-formed and malformed sequence, the two contexts
** must agree on accept (rc == 1), syntax-error (rc < 0), and
** mid-parse (rc == 0).
*/

#define _POSIX_C_SOURCE 200809L

#include "parser.h"
#include "parse_context.h"
#include "snapshot.h"
#include "snapshot_modify.h"

#include "bench_arith_grammar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

extern ParserSnapshot *ArithBuildSnapshot(void);

static int n_pass = 0, n_fail = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (cond) {                                                                                \
            printf("  [PASS] %s\n", msg);                                                          \
            n_pass++;                                                                              \
        } else {                                                                                   \
            printf("  [FAIL] %s\n", msg);                                                          \
            n_fail++;                                                                              \
        }                                                                                          \
    } while (0)

typedef struct Case {
    const char *label;
    const int *toks;
    int n;
    int expect;  /* 1 = accept, -1 = error */
} Case;

static int run_seq(ParserSnapshot *snap, const int *toks, int n) {
    ParseContext *ctx = parse_begin(snap);
    if (ctx == NULL) return -2;
    int rc = 0;
    for (int i = 0; i < n; i++) {
        rc = parse_token(ctx, toks[i], NULL, -1);
        if (rc != 0) break;
    }
    if (rc == 0) rc = parse_token(ctx, 0, NULL, -1);
    parse_end(ctx);
    return rc;
}

int main(void) {
    printf("Lime JIT-vs-table parse equivalence test\n");
    printf("========================================\n");

    /* Build the base snapshot (table-driven) and a JIT-attached one. */
    ParserSnapshot *table_snap = ArithBuildSnapshot();
    CHECK(table_snap != NULL, "ArithBuildSnapshot for table-driven snap");
    if (table_snap == NULL) return 1;

    ParserSnapshot *jit_snap = ArithBuildSnapshot();
    CHECK(jit_snap != NULL, "ArithBuildSnapshot for JIT-armed snap");
    if (jit_snap == NULL) {
        snapshot_release(table_snap);
        return 1;
    }

    int rc = lime_jit_compile(jit_snap);
    if (rc != 0) {
        printf("  [SKIP] lime_jit_compile returned %d (LLVM unavailable?)\n", rc);
        snapshot_release(jit_snap);
        snapshot_release(table_snap);
        return 77;
    }
    CHECK(jit_snap->jit_ctx != NULL, "JIT context attached to snap");

    /* Token codes: 1=NUM 2=PLUS 3=MINUS 4=STAR 5=SLASH 6=LP 7=RP. */
    static const int single_num[]    = {ARITH_NUM};
    static const int simple_add[]    = {ARITH_NUM, ARITH_PLUS, ARITH_NUM};
    static const int prec[]          = {ARITH_NUM, ARITH_PLUS, ARITH_NUM,
                                        ARITH_STAR, ARITH_NUM};
    static const int parens[]        = {ARITH_LP, ARITH_NUM, ARITH_PLUS, ARITH_NUM, ARITH_RP,
                                        ARITH_STAR, ARITH_NUM};
    static const int division[]      = {ARITH_NUM, ARITH_SLASH, ARITH_NUM, ARITH_SLASH, ARITH_NUM};
    static const int unbalanced[]    = {ARITH_LP, ARITH_NUM, ARITH_PLUS, ARITH_NUM};
    static const int double_op[]     = {ARITH_NUM, ARITH_PLUS, ARITH_PLUS, ARITH_NUM};
    static const int trailing_op[]   = {ARITH_NUM, ARITH_PLUS};
    static const int leading_op[]    = {ARITH_PLUS, ARITH_NUM};

    Case cases[] = {
        {"single NUM",         single_num,  1, 1},
        {"NUM + NUM",          simple_add,  3, 1},
        {"NUM + NUM * NUM",    prec,        5, 1},
        {"(NUM+NUM)*NUM",      parens,      7, 1},
        {"NUM/NUM/NUM",        division,    5, 1},
        {"(NUM+NUM unbalanced",unbalanced,  4, -1},
        {"NUM + + NUM",        double_op,   4, -1},
        {"NUM +",              trailing_op, 2, -1},
        {"+ NUM",              leading_op,  2, -1},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const Case *c = &cases[i];
        int t_rc = run_seq(table_snap, c->toks, c->n);
        int j_rc = run_seq(jit_snap, c->toks, c->n);

        bool agree =
            (t_rc == 1 && j_rc == 1) || (t_rc < 0 && j_rc < 0) || (t_rc == 0 && j_rc == 0);
        char buf[160];
        snprintf(buf, sizeof(buf), "%-22s  table=%d jit=%d (expected %s)", c->label, t_rc, j_rc,
                 c->expect == 1 ? "accept" : "error");
        CHECK(agree, buf);
        if (c->expect == 1) {
            CHECK(t_rc == 1, "  table accepted as expected");
        } else {
            CHECK(t_rc < 0, "  table rejected as expected");
        }
    }

    snapshot_release(jit_snap);
    snapshot_release(table_snap);

    printf("\n=== Summary === Pass: %d Fail: %d\n", n_pass, n_fail);
    return n_fail == 0 ? 0 : 1;
}
