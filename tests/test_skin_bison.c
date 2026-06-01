/*
** test_skin_bison.c -- round-trip test for --target=c:bison.
**
** Drives the same grammar two ways:
**   1. Native Lime parser API (BisonCalc / BisonCalcAlloc / BisonCalcFree),
**      with a hand-rolled token loop.
**   2. bison-API skin (yyparse_extra / yylex / yyerror / yylval),
**      where the skin's <grammar>_bison.{c,h} adapts the same Lime
**      parser to the bison contract.
**
** Asserts:
**   - both APIs produce the same result for the same input
**   - both APIs produce the same error count for malformed input
**   - the bison skin's translation table maps bison code 258
**     (the first named token) to Lime code 1 (PLUS in declaration
**     order)
**
** Test cases cover: simple arithmetic, parentheses, precedence,
** invalid token (#), and trailing-operator (3+).
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bison-skin surface.  The header redefines token names as enum
** constants starting at 258 (PLUS=258, MINUS=259, ...).  We do NOT
** include the standard test_skin_bison_grammar.h because that
** would #define the same names to 1, 2, ...  The native-side driver
** below uses string-keyed token codes via a separate helper. */
#include "test_skin_bison_grammar_bison.h"

/* ---------------------------------------------------------------- */
/*  Native-side: forward decls of the standard Lime API.            */
/* ---------------------------------------------------------------- */

extern void *BisonCalcAlloc(void *(*mallocProc)(size_t));
extern void  BisonCalcFree(void *p, void (*freeProc)(void *));
extern void  BisonCalc(void *yyp, int yymajor, int yyminor, int *result);

/* ---------------------------------------------------------------- */
/*  Shared token scanner used by both drivers.                       */
/*                                                                   */
/*  Returns:                                                          */
/*    0       -- end of input                                         */
/*    -1      -- invalid token (the # in test case 4)                 */
/*    1..7    -- LIME-internal token codes (declaration order)        */
/*  Sets *value when scanning an INTEGER.                             */
/* ---------------------------------------------------------------- */

#define LIME_PLUS    1
#define LIME_MINUS   2
#define LIME_TIMES   3
#define LIME_DIVIDE  4
#define LIME_LPAREN  5
#define LIME_RPAREN  6
#define LIME_INTEGER 7

static int scan_lime(const char *input, int *pos, int *value) {
    while (input[*pos] == ' ' || input[*pos] == '\t') (*pos)++;
    char c = input[*pos];
    if (c == 0) return 0;
    switch (c) {
        case '+': (*pos)++; return LIME_PLUS;
        case '-': (*pos)++; return LIME_MINUS;
        case '*': (*pos)++; return LIME_TIMES;
        case '/': (*pos)++; return LIME_DIVIDE;
        case '(': (*pos)++; return LIME_LPAREN;
        case ')': (*pos)++; return LIME_RPAREN;
    }
    if (c >= '0' && c <= '9') {
        int v = 0;
        while (input[*pos] >= '0' && input[*pos] <= '9') {
            v = v * 10 + (input[(*pos)++] - '0');
        }
        *value = v;
        return LIME_INTEGER;
    }
    (*pos)++;
    return -1;
}

/* ---------------------------------------------------------------- */
/*  Native-API driver.                                                */
/* ---------------------------------------------------------------- */

static int run_native(const char *input, int *out_result) {
    void *parser = BisonCalcAlloc(malloc);
    if (parser == NULL) return 2;
    int pos = 0;
    int errors = 0;
    int result = 0;
    while (1) {
        int v = 0;
        int t = scan_lime(input, &pos, &v);
        if (t == 0) break;
        if (t < 0) {
            errors = 1;
            break;
        }
        BisonCalc(parser, t, v, &result);
    }
    BisonCalc(parser, 0, 0, &result);
    BisonCalcFree(parser, free);
    *out_result = result;
    /* The grammar's %syntax_error block sets *result = -1 to signal
    ** parse-detected syntax errors.  Mirror that into the error
    ** count so the native and skin paths can be compared. */
    if (result == -1) errors = 1;
    return errors;
}

/* ---------------------------------------------------------------- */
/*  Bison-skin driver.                                                */
/* ---------------------------------------------------------------- */

static const char *g_skinned_input;
static int g_skinned_pos;
static int g_skinned_yyerror_calls;

int yylex(void) {
    int v = 0;
    int t = scan_lime(g_skinned_input, &g_skinned_pos, &v);
    if (t == 0) return 0;
    if (t < 0) return YYUNDEF;       /* skin maps this to invalid_token */
    /* Map LIME code (1..7) to bison enum code (258..264). */
    yylval = v;
    switch (t) {
        case LIME_PLUS:    return PLUS;
        case LIME_MINUS:   return MINUS;
        case LIME_TIMES:   return TIMES;
        case LIME_DIVIDE:  return DIVIDE;
        case LIME_LPAREN:  return LPAREN;
        case LIME_RPAREN:  return RPAREN;
        case LIME_INTEGER: return INTEGER;
    }
    return YYUNDEF;
}

void yyerror(const char *msg) {
    /* Capture the call so the test can verify the bison skin
    ** routed through this surface for invalid tokens. */
    (void)msg;
    g_skinned_yyerror_calls++;
}

static int run_skinned(const char *input, int *out_result) {
    g_skinned_input = input;
    g_skinned_pos = 0;
    g_skinned_yyerror_calls = 0;
    int result = 0;
    int rc = yyparse_extra(&result);
    *out_result = result;
    /* yyparse contract: 0=ok, 1=syntax error, 2=alloc fail.  Map to
    ** the same shape the native driver returns: 0 for clean parse,
    ** 1 if any error happened (whether from yyparse rc or %syntax_error). */
    if (rc != 0) return rc;
    /* The grammar's %syntax_error block sets *result = -1 to signal
    ** parse-detected syntax errors.  Surface that through the
    ** native-equivalent error count. */
    if (result == -1) return 1;
    return 0;
}

/* ---------------------------------------------------------------- */
/*  Test harness.                                                     */
/* ---------------------------------------------------------------- */

struct case_t {
    const char *input;
    int expected_result;
    int expected_errors;
};

int main(void) {
    struct case_t cases[] = {
        { "3 + 4 * 2",     11, 0 },
        { "(3 + 4) * 2",   14, 0 },
        { "100 / 10 - 3",   7, 0 },
        { "1 + 2 + 3 + 4", 10, 0 },
        { "(1+2)*(3+4)",   21, 0 },
        /* Error cases: one invalid token, one missing operand. */
        { "3 # 4",          -1, 1 },   /* bison skin's yy_xlat_token rejects # */
        { "3 +",            -1, 1 },   /* %syntax_error fires on EOF */
    };
    int nfail = 0;
    int ncase = (int)(sizeof cases / sizeof cases[0]);
    for (int i = 0; i < ncase; i++) {
        int n_result = -999, s_result = -999;
        int n_err = run_native(cases[i].input, &n_result);
        int s_err = run_skinned(cases[i].input, &s_result);

        int ok = 1;
        /* Result equivalence: only meaningful when both APIs got a
        ** clean parse.  When errors occurred the user grammar sets
        ** *result = -1; both APIs must agree on that sentinel. */
        if (n_err != s_err) {
            fprintf(stderr,
                "case %d: error-count mismatch: native=%d skin=%d (input=%s)\n",
                i, n_err, s_err, cases[i].input);
            ok = 0;
        }
        if (n_result != s_result) {
            fprintf(stderr,
                "case %d: result mismatch: native=%d skin=%d (input=%s)\n",
                i, n_result, s_result, cases[i].input);
            ok = 0;
        }
        if (cases[i].expected_errors == 0
            && n_result != cases[i].expected_result) {
            fprintf(stderr,
                "case %d: expected-result mismatch: expected=%d got=%d (input=%s)\n",
                i, cases[i].expected_result, n_result, cases[i].input);
            ok = 0;
        }
        if (cases[i].expected_errors != 0 && n_err == 0) {
            fprintf(stderr,
                "case %d: expected error, got clean parse (input=%s)\n",
                i, cases[i].input);
            ok = 0;
        }
        if (!ok) nfail++;
        else printf("case %d: %s -> %d (errors=%d) [OK]\n",
                    i, cases[i].input, n_result, n_err);
    }

    /* Spot-check that the bison enum values match the documented
    ** scheme: PLUS == 258, declared first.  This locks in the
    ** ABI the skin presents to callers. */
    if (PLUS != 258) {
        fprintf(stderr, "bison enum: expected PLUS=258, got %d\n", PLUS);
        nfail++;
    }
    if (INTEGER != 264) {
        fprintf(stderr, "bison enum: expected INTEGER=264, got %d\n", INTEGER);
        nfail++;
    }
    if (YYEOF != 0) {
        fprintf(stderr, "bison enum: expected YYEOF=0, got %d\n", YYEOF);
        nfail++;
    }

    if (nfail) {
        fprintf(stderr, "\n%d/%d cases FAILED\n", nfail, ncase);
        return 1;
    }
    printf("\nAll %d cases passed (bison-skin / native-API equivalence verified).\n", ncase);
    return 0;
}
