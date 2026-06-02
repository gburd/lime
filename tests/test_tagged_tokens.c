/*
** test_tagged_tokens.c -- v0.9.3 bison-style %token<field> tagged
** tokens.  Compiles the grammar with `--target=c:bison` so both the
** native Lime parser AND the bison-skin header are produced from
** one source; this driver exercises both surfaces and asserts the
** tag information round-trips through:
**
**   1. The grammar lexer / parser (the .y is accepted -- nothing
**      else does so today besides Lime).
**   2. The generated <name>_bison.h (the per-token enum carries a
**      `/yylval.<field>/` comment beside each tagged constant; we
**      cannot easily check comments in the binary header, but we
**      DO verify the YYSTYPE union arms compile with both .n and
**      .s addressable).
**   3. End-to-end parsing: a small input is folded to a known
**      total via the bison skin's yyparse_extra() plus a hand-
**      rolled yylex() that writes yylval.n / yylval.s by arm.
**
** Untagged tokens (EQ, SEMI) coexist with tagged ones in the same
** %token block, exercising the parser's tag-reset-on-`.` logic.
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_tagged_tokens_grammar_bison.h"

struct TaggedResult {
    int  total;
    int  nitems;
    int  ntagged_n;
    int  ntagged_s;
    char last_id[64];
};

/* Bison-skin yylex() writes to the named YYSTYPE arm.  Because the
** grammar declared %token<n> NUM and %token<s> ID, the user's
** yylex() picks the matching arm before returning the token code.
** The generated bison.h documents this contract via the
** `/yylval.<field>/` comments next to each enum constant. */
static const char *g_input;
static int         g_pos;

static char g_id_slab[8][64];
static int  g_id_slab_idx;

int yylex(void) {
    while (g_input[g_pos] == ' ' || g_input[g_pos] == '\t') g_pos++;
    char c = g_input[g_pos];
    if (c == 0) return 0;
    if (c == '=') { g_pos++; return EQ; }
    if (c == ';') { g_pos++; return SEMI; }
    if (c >= '0' && c <= '9') {
        int v = 0;
        while (g_input[g_pos] >= '0' && g_input[g_pos] <= '9') {
            v = v * 10 + (g_input[g_pos++] - '0');
        }
        yylval.n = v;     /* tagged %token<n> NUM */
        return NUM;
    }
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
        char *dst = g_id_slab[g_id_slab_idx];
        g_id_slab_idx = (g_id_slab_idx + 1) & 7;
        size_t n = 0;
        while (g_input[g_pos] != 0
               && ((g_input[g_pos] >= 'a' && g_input[g_pos] <= 'z')
                || (g_input[g_pos] >= 'A' && g_input[g_pos] <= 'Z')
                || (g_input[g_pos] >= '0' && g_input[g_pos] <= '9')
                || g_input[g_pos] == '_')
               && n < 63) {
            dst[n++] = g_input[g_pos++];
        }
        dst[n] = 0;
        yylval.s = dst;   /* tagged %token<s> ID */
        return ID;
    }
    g_pos++;
    return YYUNDEF;
}

void yyerror(const char *msg) {
    (void)msg;
    /* %syntax_error already sets out->total = -1. */
}

static int run_parse(const char *input, struct TaggedResult *out) {
    g_input = input;
    g_pos   = 0;
    int rc = yyparse_extra(out);
    return rc;
}

struct case_t {
    const char *input;
    int  expected_total;
    int  expected_nitems;
    int  expected_ntagged_n;
    int  expected_ntagged_s;
    const char *expected_last_id;
    int  expect_error;
};

int main(void) {
    /* Compile-time check: YYSTYPE has both arms.  If the bison skin
    ** had emitted a non-union typedef for some reason these
    ** assignments would not compile. */
    YYSTYPE chk;
    chk.n = 99;
    if (chk.n != 99) {
        fprintf(stderr, "YYSTYPE.n arm not addressable\n");
        return 1;
    }
    chk.s = (char *)"sentinel";
    if (chk.s == NULL || strcmp(chk.s, "sentinel") != 0) {
        fprintf(stderr, "YYSTYPE.s arm not addressable\n");
        return 1;
    }

    struct case_t cases[] = {
        /* "x = 7"  -> total 7, 1 item, 1 .n token, 1 .s token, last "x" */
        { "x = 7",            7, 1, 1, 1, "x",     0 },
        /* "1; 2; 3" -- three NUM-only items, no IDs. */
        { "1; 2; 3",          6, 3, 3, 0, "",      0 },
        /* mixed: x = 1; y = 2 -- two assigns, two NUMs, two IDs. */
        { "x = 1; y = 2",     3, 2, 2, 2, "y",     0 },
        /* bare-id chains: alpha; beta; gamma -- three IDs, no NUMs. */
        { "alpha; beta; gamma", 0, 3, 0, 3, "gamma", 0 },
        /* invalid char triggers %syntax_error -> out->total = -1. */
        { "@",                0, 0, 0, 0, "",      1 },
    };

    int nfail = 0;
    int ncase = (int)(sizeof cases / sizeof cases[0]);
    for (int i = 0; i < ncase; i++) {
        struct TaggedResult out;
        memset(&out, 0, sizeof out);
        int rc = run_parse(cases[i].input, &out);
        int got_error = (rc != 0) || (out.total == -1);
        int ok = 1;
        if (cases[i].expect_error) {
            if (!got_error) {
                fprintf(stderr,
                    "case %d (%s): expected error but parse succeeded\n",
                    i, cases[i].input);
                ok = 0;
            }
        } else {
            if (got_error) {
                fprintf(stderr,
                    "case %d (%s): unexpected error rc=%d total=%d\n",
                    i, cases[i].input, rc, out.total);
                ok = 0;
            } else {
                if (out.total != cases[i].expected_total) {
                    fprintf(stderr,
                        "case %d (%s): total mismatch (expected=%d got=%d)\n",
                        i, cases[i].input,
                        cases[i].expected_total, out.total);
                    ok = 0;
                }
                if (out.nitems != cases[i].expected_nitems) {
                    fprintf(stderr,
                        "case %d (%s): nitems mismatch (expected=%d got=%d)\n",
                        i, cases[i].input,
                        cases[i].expected_nitems, out.nitems);
                    ok = 0;
                }
                if (out.ntagged_n != cases[i].expected_ntagged_n) {
                    fprintf(stderr,
                        "case %d (%s): ntagged_n mismatch (expected=%d got=%d)\n",
                        i, cases[i].input,
                        cases[i].expected_ntagged_n, out.ntagged_n);
                    ok = 0;
                }
                if (out.ntagged_s != cases[i].expected_ntagged_s) {
                    fprintf(stderr,
                        "case %d (%s): ntagged_s mismatch (expected=%d got=%d)\n",
                        i, cases[i].input,
                        cases[i].expected_ntagged_s, out.ntagged_s);
                    ok = 0;
                }
                if (strcmp(out.last_id, cases[i].expected_last_id) != 0) {
                    fprintf(stderr,
                        "case %d (%s): last_id mismatch (expected='%s' got='%s')\n",
                        i, cases[i].input,
                        cases[i].expected_last_id, out.last_id);
                    ok = 0;
                }
            }
        }
        if (!ok) {
            nfail++;
        } else {
            printf("case %d: %-22s -> total=%d items=%d n=%d s=%d last=%s [OK]\n",
                   i, cases[i].input, out.total, out.nitems,
                   out.ntagged_n, out.ntagged_s, out.last_id);
        }
    }

    if (nfail) {
        fprintf(stderr, "\n%d failure(s)\n", nfail);
        return 1;
    }
    printf("\nAll tagged-token checks passed.\n");
    return 0;
}
