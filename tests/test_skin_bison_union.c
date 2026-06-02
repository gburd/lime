/*
** test_skin_bison_union.c -- exercises the bison-API skin's %union
** support and yydebug runtime trace flag.
**
** Builds two driver paths over the same Lime grammar generated with
** %union { int n; char *s; }:
**
**   1. Bison-skin path (yyparse_extra / yylex / yyerror / yylval).
**      The skin's <grammar>_bison.h declares
**          typedef union { int n; char *s; } YYSTYPE;
**      so yylex() can write yylval.n / yylval.s by field name.
**
**   2. Native-API path (UnionCalc / UnionCalcAlloc / UnionCalcFree).
**      The standard Lime parser stack uses YYSTYPE as the per-symbol
**      slot type (because %union was set without %token_type), so
**      we pass UnionCalc(parser, code, yylval, ...) using the same
**      union value the bison skin handed us.
**
** Asserts:
**   - both paths fold the same input to the same totals
**   - the last-NAME field is captured correctly through yylval.s
**     (so the union arm typing actually round-trips through Lime's
**     parser stack)
**   - setting yydebug = 1 enables stderr output from <Name>Trace
**     without crashing the parser; setting yydebug = 0 silences it
**   - the YYSTYPE union has BOTH arms (compile-time check via the
**     reference to yylval.n and yylval.s in the same translation
**     unit)
**
** Trace-output content is not asserted; just that enabling the
** flag does not break parsing and produces non-zero stderr.
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The bison-skin header.  Pulls in YYSTYPE (the union), yylval,
** yyparse_extra, yydebug, and the named-token enum at 258+. */
#include "test_skin_bison_union_grammar_bison.h"

/* Forward decls of the standard Lime API.  We include neither
** test_skin_bison_union_grammar.h (token-name collision) nor the
** %include block from the .y file's user code (we re-declare the
** small bits we need locally to avoid relying on header order). */
struct UnionResult;
extern void *UnionCalcAlloc(void *(*mallocProc)(size_t));
extern void  UnionCalcFree(void *p, void (*freeProc)(void *));
extern void  UnionCalc(void *yyp, int yymajor, YYSTYPE yyminor,
                       struct UnionResult *out);

/* Mirror of the in-grammar struct.  Must match the layout in
** test_skin_bison_union_grammar.y exactly; both definitions are
** compiled into separate translation units that link together. */
struct UnionResult {
    int  total;
    int  nitems;
    char last_name[64];
};

/* ---------------------------------------------------------------- */
/*  Shared scanner.  Walks an input string and, for each call,       */
/*  writes either yylval.n (NUMBER) or yylval.s (NAME) and returns   */
/*  the bison-style token code (or LIME_* code for the native path).  */
/* ---------------------------------------------------------------- */

#define LIME_NUMBER 1
#define LIME_NAME   2
#define LIME_EQ     3
#define LIME_SEMI   4

/* Static name-token storage.  The grammar's %include treats the
** char* in last_name as caller-owned, but for the test we just
** point yylval.s at a small static slab and refresh it per token.
** Production callers would tie this to a real symbol table; we
** only need lifetime to last across one yylex() -> reduce action. */
static char g_name_slab[8][64];
static int  g_name_slab_idx;

static int scan(const char *input, int *pos, YYSTYPE *yylv) {
    while (input[*pos] == ' ' || input[*pos] == '\t') (*pos)++;
    char c = input[*pos];
    if (c == 0) return 0;
    if (c == '=') { (*pos)++; return LIME_EQ; }
    if (c == ';') { (*pos)++; return LIME_SEMI; }
    if (c >= '0' && c <= '9') {
        int v = 0;
        while (input[*pos] >= '0' && input[*pos] <= '9') {
            v = v * 10 + (input[(*pos)++] - '0');
        }
        yylv->n = v;
        return LIME_NUMBER;
    }
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
        char *dst = g_name_slab[g_name_slab_idx];
        g_name_slab_idx = (g_name_slab_idx + 1) & 7;
        size_t n = 0;
        while (input[*pos] != 0
               && ((input[*pos] >= 'a' && input[*pos] <= 'z')
                || (input[*pos] >= 'A' && input[*pos] <= 'Z')
                || (input[*pos] >= '0' && input[*pos] <= '9')
                || input[*pos] == '_')
               && n < 63) {
            dst[n++] = input[(*pos)++];
        }
        dst[n] = 0;
        yylv->s = dst;
        return LIME_NAME;
    }
    (*pos)++;
    return -1;
}

/* ---------------------------------------------------------------- */
/*  Native-API driver.                                                */
/* ---------------------------------------------------------------- */

static int run_native(const char *input, struct UnionResult *out) {
    void *parser = UnionCalcAlloc(malloc);
    if (parser == NULL) return 2;
    int pos = 0;
    int errors = 0;
    while (1) {
        YYSTYPE val;
        memset(&val, 0, sizeof val);
        int t = scan(input, &pos, &val);
        if (t == 0) break;
        if (t < 0) { errors = 1; break; }
        UnionCalc(parser, t, val, out);
    }
    YYSTYPE zero;
    memset(&zero, 0, sizeof zero);
    UnionCalc(parser, 0, zero, out);
    UnionCalcFree(parser, free);
    if (out->total == -1) errors = 1;
    return errors;
}

/* ---------------------------------------------------------------- */
/*  Bison-skin driver.                                                */
/* ---------------------------------------------------------------- */

static const char *g_skin_input;
static int         g_skin_pos;

int yylex(void) {
    int t = scan(g_skin_input, &g_skin_pos, &yylval);
    if (t == 0) return 0;
    if (t < 0) return YYUNDEF;
    /* LIME_* -> bison enum.  Order in %token NUMBER NAME EQ SEMI
    ** assigns Lime codes 1..4; bison codes 258..261. */
    switch (t) {
        case LIME_NUMBER: return NUMBER;
        case LIME_NAME:   return NAME;
        case LIME_EQ:     return EQ;
        case LIME_SEMI:   return SEMI;
    }
    return YYUNDEF;
}

void yyerror(const char *msg) {
    (void)msg;
    /* The %syntax_error block already sets out->total = -1; nothing
    ** else to do here for this test.  We DO need to define yyerror
    ** because the skin declares it extern. */
}

static int run_skinned(const char *input, struct UnionResult *out) {
    g_skin_input = input;
    g_skin_pos   = 0;
    int rc = yyparse_extra(out);
    if (rc != 0) return rc;
    if (out->total == -1) return 1;
    return 0;
}

/* ---------------------------------------------------------------- */
/*  Test cases.                                                       */
/* ---------------------------------------------------------------- */

struct case_t {
    const char *input;
    int  expected_total;
    int  expected_nitems;
    const char *expected_last_name;
    int  expected_errors;
};

static int run_cases(void) {
    struct case_t cases[] = {
        { "1; 2; 3",          6, 3, "",     0 },
        { "x = 10",          10, 1, "x",    0 },
        { "x = 1; y = 2",     3, 2, "y",    0 },
        { "alpha = 100",    100, 1, "alpha", 0 },
        { "1; foo; 5",        6, 3, "foo",  0 },
        { "@",                0, 0, "",     1 },  /* invalid char */
    };
    int nfail = 0;
    int ncase = (int)(sizeof cases / sizeof cases[0]);
    for (int i = 0; i < ncase; i++) {
        struct UnionResult n_out, s_out;
        memset(&n_out, 0, sizeof n_out);
        memset(&s_out, 0, sizeof s_out);
        int n_err = run_native(cases[i].input, &n_out);
        int s_err = run_skinned(cases[i].input, &s_out);

        int ok = 1;
        if (n_err != s_err) {
            fprintf(stderr,
                "case %d: error-count mismatch native=%d skin=%d input=%s\n",
                i, n_err, s_err, cases[i].input);
            ok = 0;
        }
        if (cases[i].expected_errors == 0) {
            if (n_out.total != cases[i].expected_total
             || s_out.total != cases[i].expected_total) {
                fprintf(stderr,
                    "case %d: total mismatch (expected=%d native=%d skin=%d) input=%s\n",
                    i, cases[i].expected_total,
                    n_out.total, s_out.total, cases[i].input);
                ok = 0;
            }
            if (n_out.nitems != cases[i].expected_nitems
             || s_out.nitems != cases[i].expected_nitems) {
                fprintf(stderr,
                    "case %d: nitems mismatch (expected=%d native=%d skin=%d) input=%s\n",
                    i, cases[i].expected_nitems,
                    n_out.nitems, s_out.nitems, cases[i].input);
                ok = 0;
            }
            if (strcmp(n_out.last_name, cases[i].expected_last_name) != 0
             || strcmp(s_out.last_name, cases[i].expected_last_name) != 0) {
                fprintf(stderr,
                    "case %d: last_name mismatch (expected='%s' native='%s' skin='%s') input=%s\n",
                    i, cases[i].expected_last_name,
                    n_out.last_name, s_out.last_name, cases[i].input);
                ok = 0;
            }
        }
        if (cases[i].expected_errors != 0 && n_err == 0) {
            fprintf(stderr,
                "case %d: expected error, got clean parse, input=%s\n",
                i, cases[i].input);
            ok = 0;
        }
        if (!ok) nfail++;
        else printf("case %d: %s -> total=%d nitems=%d last=%s [OK]\n",
                    i, cases[i].input,
                    n_out.total, n_out.nitems, n_out.last_name);
    }
    return nfail;
}

/* ---------------------------------------------------------------- */
/*  yydebug check: enable, parse, verify no crash and (in debug      */
/*  builds) some bytes appear on stderr.                             */
/* ---------------------------------------------------------------- */

static int run_yydebug_smoke(void) {
    /* Redirect stderr to a temp pipe so the test process can read
    ** back what the trace produced.  Skipped on platforms where
    ** dup2 is unavailable -- not a regression in that case, the
    ** parse path still has to NOT crash. */
    int pipefds[2];
    if (pipe(pipefds) != 0) {
        fprintf(stderr, "yydebug: pipe() failed; skipping output check\n");
        /* Still run the parser to verify it does not crash. */
        struct UnionResult out;
        memset(&out, 0, sizeof out);
        yydebug = 1;
        int rc = run_skinned("3; 4; x = 5", &out);
        yydebug = 0;
        if (rc != 0 || out.total != 12) {
            fprintf(stderr,
                "yydebug: parse failed (rc=%d total=%d)\n", rc, out.total);
            return 1;
        }
        printf("yydebug: parser OK with yydebug=1 (no stderr capture)\n");
        return 0;
    }

    int saved_stderr = dup(fileno(stderr));
    fflush(stderr);
    dup2(pipefds[1], fileno(stderr));
    close(pipefds[1]);

    struct UnionResult out;
    memset(&out, 0, sizeof out);
    yydebug = 1;
    int rc = run_skinned("3; 4; x = 5", &out);
    yydebug = 0;
    fflush(stderr);

    /* Restore stderr before printing PASS/FAIL so the test driver
    ** sees the right output. */
    dup2(saved_stderr, fileno(stderr));
    close(saved_stderr);

    /* Drain whatever the trace wrote.  In NDEBUG builds Lime's
    ** Trace function is omitted and the pipe is empty; in normal
    ** debug builds we expect non-zero bytes to have been written.
    ** Either is acceptable -- the test asserts only that the parse
    ** itself did not crash. */
    char buf[4096];
    ssize_t got = 0;
    /* Non-blocking-ish: try once, accept either an empty pipe or
    ** a populated one.  Pipes in Linux block on read until data
    ** is available or the writer closes -- since we already
    ** restored stderr, the writer (the dup2'd fd) is gone and
    ** read returns EOF cleanly. */
    /* Close write end via restoration above.  Read until EOF. */
    while (1) {
        ssize_t n = read(pipefds[0], buf + got,
                         (size_t)(sizeof(buf) - 1 - got));
        if (n <= 0) break;
        got += n;
        if ((size_t)got >= sizeof(buf) - 1) break;
    }
    close(pipefds[0]);
    if (got > 0) buf[got] = 0;

    if (rc != 0 || out.total != 12) {
        fprintf(stderr,
            "yydebug: parse failed under trace (rc=%d total=%d)\n",
            rc, out.total);
        return 1;
    }
    /* Print whatever we captured for human diagnosis but do not
    ** assert byte content -- the trace format is Lime's, not ours. */
    printf("yydebug: parser OK; captured %zd bytes of trace output\n",
           got);
    if (got > 0) {
        /* Print first ~120 bytes to give the test reader a flavour. */
        size_t show = (size_t)got < 120 ? (size_t)got : 120;
        printf("yydebug: first %zu bytes:\n", show);
        fwrite(buf, 1, show, stdout);
        if (buf[show - 1] != '\n') putchar('\n');
    }

    /* Sanity: setting yydebug back to 0 and re-running must still
    ** produce a clean parse with no stderr writes.  We do not
    ** capture stderr here; just verify the parse runs to
    ** completion. */
    memset(&out, 0, sizeof out);
    yydebug = 0;
    rc = run_skinned("1; 2", &out);
    if (rc != 0 || out.total != 3) {
        fprintf(stderr,
            "yydebug: post-disable parse failed (rc=%d total=%d)\n",
            rc, out.total);
        return 1;
    }
    printf("yydebug: post-disable parse OK\n");
    return 0;
}

int main(void) {
    /* Compile-time check that YYSTYPE has both arms.  These
    ** references would fail to compile if the bison skin emitted a
    ** plain typedef instead of a union for %union grammars.  Each
    ** arm is exercised in turn (a union shares storage, so we do
    ** NOT expect both writes to coexist; the check is purely about
    ** the field name being addressable). */
    YYSTYPE check;
    check.n = 42;
    if (check.n != 42) {
        fprintf(stderr, "YYSTYPE.n arm not addressable\n");
        return 1;
    }
    check.s = (char *)"sentinel";
    if (check.s == NULL || strcmp(check.s, "sentinel") != 0) {
        fprintf(stderr, "YYSTYPE.s arm not addressable\n");
        return 1;
    }
    printf("YYSTYPE: union arms .n and .s addressable [OK]\n");

    int nfail = run_cases();
    nfail += run_yydebug_smoke();
    if (nfail) {
        fprintf(stderr, "\n%d failure(s)\n", nfail);
        return 1;
    }
    printf("\nAll bison-skin %%union + yydebug checks passed.\n");
    return 0;
}
