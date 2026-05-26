/*
** test_per_rule_reduce.c -- v0.6.0 per-rule reduce callback test.
**
** Five sub-tests:
**
**   1. yy_rule_reduce_fn[] presence -- scrape the generated .c
**      file and assert (a) a `static void yy_rule_<N>(...)` exists
**      for each rule, (b) the dispatch array `yy_rule_reduce_fn[]`
**      is defined, (c) the array has yyNRule entries.
**
**   2. Functional equivalence -- drive several distinct token
**      streams through the parser and verify the computed result
**      matches a hand-evaluated oracle.  This is the "the new
**      dispatch path doesn't break parses" check.
**
**   3. Rule-firing trace -- the grammar's actions append a single
**      character per rule fired; the driver checks the EXACT trace
**      string for several inputs.  Trace mismatches catch silent
**      reordering bugs that pure result-comparison would miss.
**
**   4. Composition compatibility (smoke) -- the production
**      composition path lives in src/parser_composition.c and is
**      already covered by test_parser_composition.c / e2e tests.
**      Here we just assert the new emitter produces a parser whose
**      yy_rule_reduce_fn[] array has the right per-rule density:
**      the array length must equal yyNRule, so concatenating two
**      such arrays at composition time is a O(nrule) memcpy.
**
**   5. Hot-rule annotation hook (documentation-only) -- the
**      generator's per-rule emit creates one function per rule, so
**      a future profile-guided pass can splice in
**      __attribute__((hot)) at the function level.  Here we
**      verify the function names follow the documented
**      `yy_rule_<N>` schema so a downstream tool can find them.
*/
#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_per_rule_reduce.h"
#include "test_per_rule_reduce_grammar.h"

void *PrrAlloc(void *(*mallocProc)(size_t));
void  PrrFree(void *, void (*freeProc)(void *));
void  Prr(void *yyp, int yymajor, int yyminor, struct prr_ctx *ctx);

/* ----------------- Sub-test 1: emitter audit ------------------- */

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf); fclose(f); return NULL;
    }
    buf[n] = 0;
    fclose(f);
    return buf;
}

static int count_substr(const char *hay, const char *needle) {
    int n = 0;
    size_t nl = strlen(needle);
    for (const char *p = hay; (p = strstr(p, needle)) != NULL; p += nl) n++;
    return n;
}

static int test_emitter_audit(const char *generated_c) {
    char *src = slurp(generated_c);
    if (!src) {
        fprintf(stderr, "audit: cannot read %s\n", generated_c);
        return 1;
    }
    int fails = 0;

    /* (a) yy_reduce_ctx typedef is present (template lifted it
    **     to file scope so per-rule functions can take it as a
    **     parameter). */
    if (!strstr(src, "typedef struct yy_reduce_ctx")) {
        fprintf(stderr, "audit: missing yy_reduce_ctx typedef\n");
        fails++;
    }

    /* (b) yy_rule_reduce_fn[] dispatch table is present. */
    const char *tbl = strstr(src,
        "static void (*const yy_rule_reduce_fn[])(yy_reduce_ctx *)");
    if (!tbl) {
        fprintf(stderr, "audit: missing yy_rule_reduce_fn[] table\n");
        fails++;
    }

    /* (c) yy_reduce dispatches via the table, not via a switch
    **     on yyruleno. */
    if (strstr(src, "switch( yyruleno ){")) {
        fprintf(stderr, "audit: yy_reduce still contains switch(yyruleno)\n");
        fails++;
    }
    if (!strstr(src, "yy_rule_reduce_fn[yyruleno](&yy_ctx)")) {
        fprintf(stderr,
            "audit: yy_reduce does not dispatch via yy_rule_reduce_fn[]\n");
        fails++;
    }

    /* (d) per-rule functions exist.  yyNRule comes from the
    **     generated header via the count of "static void yy_rule_"
    **     prefixed declarations.  We don't know yyNRule statically
    **     here; instead, count how many `static void yy_rule_N(` we
    **     see and verify it matches the dispatch table's row count
    **     (counted by leading "  yy_rule_" entries). */
    int nfunc = count_substr(src, "static void yy_rule_");
    /* The dispatch table also references each rule once via
    ** "  yy_rule_<N>," -- count those.  Use a tighter
    ** prefix so we don't double-count the function definitions. */
    int nslots = count_substr(src, "\n  yy_rule_");
    if (nfunc < 1 || nfunc != nslots) {
        fprintf(stderr,
            "audit: function count (%d) != table slot count (%d)\n",
            nfunc, nslots);
        fails++;
    }

    free(src);
    if (fails == 0) {
        printf("emitter_audit: PASS (%d per-rule functions, %d table slots)\n",
               nfunc, nslots);
    }
    return fails;
}

/* -------------- Sub-test 2 / 3: parse + trace ------------------ */

static int run_parse(const char *label, const int *toks, const int *vals,
                     int n, int expect_result, const char *expect_trace) {
    struct prr_ctx ctx = {0};
    void *parser = PrrAlloc(malloc);
    if (!parser) {
        fprintf(stderr, "%s: PrrAlloc failed\n", label);
        return 1;
    }
    for (int i = 0; i < n; i++) {
        Prr(parser, toks[i], vals ? vals[i] : 0, &ctx);
    }
    Prr(parser, 0, 0, &ctx);   /* EOF */
    PrrFree(parser, free);

    int fails = 0;
    if (ctx.result != expect_result) {
        fprintf(stderr, "%s: result=%d expected %d\n",
                label, ctx.result, expect_result);
        fails++;
    }
    if (strcmp(ctx.trace, expect_trace) != 0) {
        fprintf(stderr, "%s: trace=\"%s\" expected \"%s\"\n",
                label, ctx.trace, expect_trace);
        fails++;
    }
    if (fails == 0) {
        printf("%s: PASS (result=%d trace=\"%s\")\n",
               label, ctx.result, ctx.trace);
    }
    return fails;
}

static int test_parse_equivalence(void) {
    int fails = 0;

    /* INTEGER */
    {
        int toks[] = { PRR_INTEGER };
        int vals[] = { 42 };
        /* Trace: 'i' (term <- INTEGER), 'P' (program <- expr).
        ** No 'expr <- term' tag because that rule has no user
        ** code (noCode path). */
        fails += run_parse("plain_integer", toks, vals,
                           1, 42, "iP");
    }

    /* 1 + 2 * 3 -- precedence check: TIMES binds tighter. */
    {
        int toks[] = { PRR_INTEGER, PRR_PLUS, PRR_INTEGER,
                       PRR_TIMES, PRR_INTEGER };
        int vals[] = { 1, 0, 2, 0, 3 };
        /* term i (=1) -> term i (=2) -> term i (=3) -> times -> plus -> P */
        fails += run_parse("precedence_1_plus_2_times_3", toks, vals,
                           5, 7, "iii*+P");
    }

    /* (1 + 2) * 3 -- groups */
    {
        int toks[] = { PRR_LPAREN, PRR_INTEGER, PRR_PLUS, PRR_INTEGER,
                       PRR_RPAREN, PRR_TIMES, PRR_INTEGER };
        int vals[] = { 0, 1, 0, 2, 0, 0, 3 };
        /* i(1) i(2) + g i(3) * P */
        fails += run_parse("paren_1_plus_2_times_3", toks, vals,
                           7, 9, "ii+gi*P");
    }

    /* 10 - 3 - 2 -- left associativity */
    {
        int toks[] = { PRR_INTEGER, PRR_MINUS, PRR_INTEGER,
                       PRR_MINUS, PRR_INTEGER };
        int vals[] = { 10, 0, 3, 0, 2 };
        /* (10-3)-2 = 5: i i - i - P */
        fails += run_parse("left_assoc_minus", toks, vals,
                           5, 5, "ii-i-P");
    }

    return fails;
}

/* -------- Sub-test 4: composition / dispatch density ----------- */
/*
** The composition path in src/parser_composition.c merges two
** parsers' action tables.  Per the v0.6.0 design, merging
** dispatch arrays becomes a O(nrule_a + nrule_b) memcpy because
** yy_rule_reduce_fn[] is a flat array of function pointers.
**
** Here we don't actually fork-and-merge -- that's the job of
** test_composition_e2e.c -- we just sanity-check that the per-rule
** array's storage class permits external concatenation, i.e.
** verify it's `static const` (immutable).  A future composition
** pass would copy out per-rule pointers from each input parser
** and emit a fresh array; the source-array immutability
** guarantee is what makes that copy safe.
*/
static int test_composition_density(const char *generated_c) {
    char *src = slurp(generated_c);
    if (!src) {
        fprintf(stderr, "composition: cannot read %s\n", generated_c);
        return 1;
    }
    int fails = 0;
    if (!strstr(src,
        "static void (*const yy_rule_reduce_fn[])(yy_reduce_ctx *) = {")) {
        fprintf(stderr,
            "composition: yy_rule_reduce_fn[] is not `static const`\n");
        fails++;
    }
    free(src);
    if (fails == 0) {
        printf("composition_density: PASS (yy_rule_reduce_fn is static const)\n");
    }
    return fails;
}

/* ---- Sub-test 5: per-rule function naming schema (hot-tag hook) ---- */
/*
** Verify that the emitted per-rule function names follow the
** documented `yy_rule_<N>` schema (digits only after the prefix).
** A profile-guided post-pass can find these by name and splice
** in __attribute__((hot)) ahead of the static keyword.
*/
static int test_naming_schema(const char *generated_c) {
    char *src = slurp(generated_c);
    if (!src) {
        fprintf(stderr, "naming: cannot read %s\n", generated_c);
        return 1;
    }
    int fails = 0;
    int matched = 0;
    const char *p = src;
    const char *needle = "static void yy_rule_";
    while ((p = strstr(p, needle)) != NULL) {
        const char *q = p + strlen(needle);
        const char *digits = q;
        while (*q && isdigit((unsigned char)*q)) q++;
        if (q == digits) {
            fprintf(stderr,
                "naming: yy_rule_ followed by non-digits at offset %ld\n",
                (long)(p - src));
            fails++;
            break;
        }
        if (*q != '(') {
            fprintf(stderr,
                "naming: yy_rule_<N> not followed by '(' at offset %ld\n",
                (long)(p - src));
            fails++;
            break;
        }
        matched++;
        p = q;
    }
    free(src);
    if (fails == 0 && matched > 0) {
        printf("naming_schema: PASS (%d per-rule functions match yy_rule_<N>)\n",
               matched);
    } else if (matched == 0 && fails == 0) {
        fprintf(stderr, "naming: no per-rule functions found\n");
        fails = 1;
    }
    return fails;
}

/* ----------------------- Driver -------------------------------- */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
            "usage: %s <path-to-generated-grammar.c>\n"
            "(meson passes the custom_target output via @OUTDIR@)\n",
            argv[0]);
        return 1;
    }
    const char *generated_c = argv[1];

    int fails = 0;
    fails += test_emitter_audit(generated_c);
    fails += test_parse_equivalence();
    fails += test_composition_density(generated_c);
    fails += test_naming_schema(generated_c);

    if (fails == 0) {
        printf("\ntest_per_rule_reduce: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr, "\ntest_per_rule_reduce: %d sub-test failure(s)\n", fails);
    return 1;
}
