/*
** tests/test_lex_program.c -- B4: the runtime DFA interpreter.
**
** tests/pg_scan_core.lex (PostgreSQL's scanner, actions replaced by one
** shared PG-free action function, tests/pg_scan_core_act.h) runs two
** ways on the same input:
**
**   generated:   `lime -X pg_scan_core.lex` at build time, driven by its
**                CoreLexFeedBytes (actions = PGSC(rule) bodies);
**   interpreted: lime_lex_program_compile(pg_scan_core.lex text) AT RUN
**                TIME (no compiler, no dlopen), driven by
**                lime_lex_program_match + the same action function.
**
** Every token (kind, start, end) must agree, for every file given on
** the command line.  Then a few unit checks of the interpreter itself:
** longest match, rule-order tie break, states, <<EOF>> rules, bad
** source.
**
** BENCH_REPS=N (default 0 = off) times both engines over the files
** (CPU time, alternating order, median + range) and the in-process
** build of pg_scan_core.lex.
**
** Negative controls (by hand, see the commit): the interpreter stops at
** the FIRST accepting state instead of the last; the builder reports
** the state-local rule index instead of the global one.  Each must fail.
*/
#define _POSIX_C_SOURCE 200809L
#include "lime_lexer.h"
#include "pg_scan_core_act.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int fails = 0;
#define CHECK(cond, ...)                                                                     \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);                                    \
            printf(__VA_ARGS__);                                                             \
            printf("\n");                                                                    \
            fails++;                                                                         \
        }                                                                                    \
    } while (0)

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) {
        free(b);
        b = NULL;
    }
    if (b) b[n] = 0;
    fclose(f);
    *len = (size_t)n;
    return b;
}

/* ---- token sink ---- */
typedef struct {
    int *kind;
    size_t *start, *end;
    size_t n, cap;
} TokVec;

static void sink(void *u, int kind, size_t s, size_t e) {
    TokVec *v = u;
    if (v->n == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 1024;
        v->kind = realloc(v->kind, v->cap * sizeof *v->kind);
        v->start = realloc(v->start, v->cap * sizeof *v->start);
        v->end = realloc(v->end, v->cap * sizeof *v->end);
    }
    v->kind[v->n] = kind;
    v->start[v->n] = s;
    v->end[v->n] = e;
    v->n++;
}

static void tv_free(TokVec *v) {
    free(v->kind);
    free(v->start);
    free(v->end);
    memset(v, 0, sizeof *v);
}

#define PGT_UNMATCHED 99

/* ---- the two engines ---- */
static void run_generated(const char *in, size_t len, TokVec *out) {
    PgscCtx cx = { 0 };
    cx.sink = sink;
    cx.sink_user = out;
    cx.base = in;
    CoreLexer *l = CoreLexAlloc(malloc);
    if (CoreLexFeedBytes(l, in, len, NULL, &cx) != CORE_LEX_OK && !cx.error)
        sink(out, PGT_UNMATCHED, 0, 0);
    CoreLexFree(l, free);
}

static void run_interpreted(const LimeLexProgram *p, const char *in, size_t len, TokVec *out) {
    PgscCtx cx = { 0 };
    cx.sink = sink;
    cx.sink_user = out;
    cx.base = in;
    int state = CORE_STATE_INITIAL;
    size_t pos = 0;
    for (;;) {
        PgscOut o;
        if (pos >= len) {
            int er = lime_lex_program_eof_rule(p, state);
            if (er >= 0) pgsc_action(&cx, er, in + len, 0, state, &o);
            break;
        }
        int rule;
        size_t n;
        if (!lime_lex_program_match(p, state, in + pos, len - pos, &rule, &n) || n == 0) {
            sink(out, PGT_UNMATCHED, 0, 0);
            break;
        }
        pgsc_action(&cx, rule, in + pos, n, state, &o);
        pos += o.keep;
        state = o.new_state;
        if (cx.error) break;
    }
}

/* The program's rule and state numbering must be the generated
** scanner's, or the shared action function would misfire. */
static int numbering_agrees(const LimeLexProgram *p) {
    int ng = 0;
    while (CoreRuleNames[ng]) ng++;
    int ok = lime_lex_program_rule_count(p) == ng;
    for (int i = 0; ok && i < ng; i++)
        ok = strcmp(lime_lex_program_rule_name(p, i), CoreRuleNames[i]) == 0;
    static const struct {
        const char *n;
        int v;
    } st[] = { { "INITIAL", CORE_STATE_INITIAL }, { "XB", CORE_STATE_XB },   { "XC", CORE_STATE_XC },
               { "XD", CORE_STATE_XD },           { "XH", CORE_STATE_XH },   { "XQ", CORE_STATE_XQ },
               { "XQS", CORE_STATE_XQS },         { "XE", CORE_STATE_XE },   { "XDOLQ", CORE_STATE_XDOLQ },
               { "XUI", CORE_STATE_XUI },         { "XUS", CORE_STATE_XUS }, { "XEU", CORE_STATE_XEU } };
    for (size_t i = 0; ok && i < sizeof st / sizeof st[0]; i++)
        ok = lime_lex_program_state_by_name(p, st[i].n) == st[i].v;
    return ok && lime_lex_program_state_count(p) == 12;
}

static long compare(const LimeLexProgram *p, const char *name, const char *in, size_t len,
                    long *bad) {
    TokVec a = { 0 }, b = { 0 };
    run_generated(in, len, &a);
    run_interpreted(p, in, len, &b);
    size_t m = a.n < b.n ? a.n : b.n, i;
    for (i = 0; i < m; i++)
        if (a.kind[i] != b.kind[i] || a.start[i] != b.start[i] || a.end[i] != b.end[i]) break;
    if (i < m || a.n != b.n) {
        if ((*bad)++ < 5) {
            if (i < m)
                printf("  MISMATCH %s token %zu: generated %d [%zu,%zu) vs interpreted %d [%zu,%zu)\n",
                       name, i, a.kind[i], a.start[i], a.end[i], b.kind[i], b.start[i], b.end[i]);
            else
                printf("  MISMATCH %s: %zu vs %zu tokens\n", name, a.n, b.n);
        }
    }
    long n = (long)a.n;
    tv_free(&a);
    tv_free(&b);
    return n;
}

/* ---- unit checks of the interpreter ---- */
static void unit_checks(void) {
    static const char lex[] =
        "%name_prefix U.\n"
        "%exclusive_state STR.\n"
        "rule kw_if matches /if/ { }\n"
        "rule ident matches /[a-z]+/ { }\n"
        "rule le matches /<=/ { }\n"
        "rule lt matches /</ { }\n"
        "rule ws matches /[ ]+/ { }\n"
        "rule q matches /\"/ { }\n"
        "<STR> rule body matches /[^\"]+/ { }\n"
        "<STR> rule endq matches /\"/ { }\n"
        "<STR> rule str_eof matches <<EOF>> { }\n";
    char *err = NULL;
    LimeLexProgram *p = lime_lex_program_compile(lex, sizeof lex - 1, &err);
    CHECK(p != NULL, "unit program: %s", err ? err : "");
    free(err);
    if (!p) return;
    int r = -1;
    size_t n = 0;
    int KW = lime_lex_program_rule_by_name(p, "kw_if"), ID = lime_lex_program_rule_by_name(p, "ident");
    int LE = lime_lex_program_rule_by_name(p, "le"), LT = lime_lex_program_rule_by_name(p, "lt");
    int STR = lime_lex_program_state_by_name(p, "STR");
    CHECK(lime_lex_program_match(p, 0, "if x", 4, &r, &n) && r == KW && n == 2, "tie -> earlier rule");
    CHECK(lime_lex_program_match(p, 0, "iffy", 4, &r, &n) && r == ID && n == 4, "longest wins");
    CHECK(lime_lex_program_match(p, 0, "<=>", 3, &r, &n) && r == LE && n == 2, "<= longest");
    CHECK(lime_lex_program_match(p, 0, "<a", 2, &r, &n) && r == LT && n == 1, "< alone");
    CHECK(!lime_lex_program_match(p, 0, "#", 1, &r, &n), "no rule matched #");
    CHECK(lime_lex_program_match(p, STR, "ab c\"", 5, &r, &n) &&
              r == lime_lex_program_rule_by_name(p, "body") && n == 4,
          "exclusive state STR");
    CHECK(!lime_lex_program_match(p, STR, "", 0, &r, &n), "empty input");
    CHECK(lime_lex_program_eof_rule(p, STR) == lime_lex_program_rule_by_name(p, "str_eof"),
          "eof rule of STR");
    CHECK(lime_lex_program_eof_rule(p, 0) == -1, "INITIAL has no eof rule");
    CHECK(lime_lex_program_rule_is_eof(p, lime_lex_program_rule_by_name(p, "str_eof")) == 1, "is_eof");
    CHECK(!lime_lex_program_match(p, 99, "if", 2, &r, &n), "bad state");
    lime_lex_program_free(p);

    static const char bad[] = "rule x matches /[a-/ { }\n";
    p = lime_lex_program_compile(bad, sizeof bad - 1, &err);
    CHECK(p == NULL && err != NULL, "bad regex accepted");
    free(err);
    lime_lex_program_free(NULL);
}

static double cpu_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
    return (double)t.tv_sec * 1e9 + (double)t.tv_nsec;
}
static int dcmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}
static void report(const char *what, double *v, int n, const char *unit) {
    qsort(v, (size_t)n, sizeof *v, dcmp);
    printf("  %-30s median %8.3f  min %8.3f  max %8.3f %s (n=%d)\n", what, v[n / 2], v[0], v[n - 1],
           unit, n);
}

static void count_sink(void *u, int kind, size_t s, size_t e) {
    (void)kind;
    *(size_t *)u += 1 + (e - s);
}

int main(int argc, char **argv) {
    unit_checks();

    /* the fixture's source: compile it at run time */
    const char *lex_path = getenv("PG_SCAN_CORE_LEX");
    size_t lex_len = 0;
    char *lex_src = lex_path ? slurp(lex_path, &lex_len) : NULL;
    if (!lex_src) {
        printf("  [BROKEN] set PG_SCAN_CORE_LEX to tests/pg_scan_core.lex\n");
        return 1;
    }
    char *err = NULL;
    LimeLexProgram *p = lime_lex_program_compile(lex_src, lex_len, &err);
    if (!p) {
        printf("  [BROKEN] compile: %s\n", err ? err : "?");
        return 1;
    }
    CHECK(numbering_agrees(p), "program rule/state numbering differs from the generated scanner");

    long ntok = 0, bad = 0;
    size_t bytes = 0;
    int nfiles = 0;
    char **bufs = calloc((size_t)argc, sizeof *bufs);
    size_t *lens = calloc((size_t)argc, sizeof *lens);
    for (int a = 1; a < argc; a++) {
        bufs[a] = slurp(argv[a], &lens[a]);
        if (!bufs[a]) {
            CHECK(0, "cannot read %s", argv[a]);
            continue;
        }
        ntok += compare(p, argv[a], bufs[a], lens[a], &bad);
        bytes += lens[a];
        nfiles++;
    }
    /* edge inputs: unterminated literals (EOF rules), nesting, dollar tags */
    static const char *edge[] = {
        "/* a /* b */ c */ x",  "'abc",       "e'\\u00e9\\uD83D\\uDE00'", "$a$ $b$ $a$",
        "$a$ unterminated",     "x'0F' b'01'", "U&\"x\" u&'y'",            "1..2 1.e 0x 0b2 $1x",
        "a<=-b @-> !=- ~~*",    "\"unterminated", "'a'\n  'b' 'c'",        "/* unterminated",
        "",                     "--c\n'x'\n'y'", "n'x' n", "e'\\uD800x'",
    };
    for (size_t i = 0; i < sizeof edge / sizeof edge[0]; i++)
        ntok += compare(p, "edge", edge[i], strlen(edge[i]), &bad);
    CHECK(bad == 0, "%ld input(s) lexed differently", bad);
    printf("  generated vs interpreted: %d files (%zu bytes) + %zu edge inputs, %ld tokens, %ld "
           "mismatching inputs\n",
           nfiles, bytes, sizeof edge / sizeof edge[0], ntok, bad);

    int reps = getenv("BENCH_REPS") ? atoi(getenv("BENCH_REPS")) : 0;
    if (reps > 0 && bad == 0 && nfiles > 0) {
        /* three variants, rotated each rep:
        **   0 generated, its own CoreLexFeedBytes runtime (what a user gets)
        **   1 generated Core_match + the driver loop below
        **   2 interpreter lime_lex_program_match + the same driver loop
        ** 1 vs 2 is the matcher-only comparison. */
        double *g = malloc(sizeof(double) * (size_t)reps), *it = malloc(sizeof(double) * (size_t)reps);
        double *gm = malloc(sizeof(double) * (size_t)reps), *rm = malloc(sizeof(double) * (size_t)reps);
        double *ra = malloc(sizeof(double) * (size_t)reps), *bt = malloc(sizeof(double) * (size_t)reps);
        volatile size_t sinkv = 0;
        for (int r = -2; r < reps; r++) {
            for (int w = 0; w < 3; w++) {
                int variant = (w + (r < 0 ? 0 : r)) % 3;
                int interp = variant;
                size_t acc = 0;
                double t0 = cpu_ns();
                for (int a = 1; a < argc; a++) {
                    if (!bufs[a]) continue;
                    PgscCtx cx = { 0 };
                    cx.sink = count_sink;
                    cx.sink_user = &acc;
                    cx.base = bufs[a];
                    if (interp == 0) {
                        CoreLexer *l = CoreLexAlloc(malloc);
                        CoreLexFeedBytes(l, bufs[a], lens[a], NULL, &cx);
                        CoreLexFree(l, free);
                    } else if (interp == 1) {
                        const char *in = bufs[a];
                        size_t len = lens[a], pos = 0;
                        int state = 0;
                        while (pos < len) {
                            int rule;
                            size_t n;
                            PgscOut o;
                            if (!Core_match(state, in + pos, len - pos, &rule, &n)) break;
                            pgsc_action(&cx, rule, in + pos, n, state, &o);
                            pos += o.keep;
                            state = o.new_state;
                            if (cx.error) break;
                        }
                    } else {
                        const char *in = bufs[a];
                        size_t len = lens[a], pos = 0;
                        int state = 0;
                        while (pos < len) {
                            int rule;
                            size_t n;
                            PgscOut o;
                            if (!lime_lex_program_match(p, state, in + pos, len - pos, &rule, &n)) break;
                            pgsc_action(&cx, rule, in + pos, n, state, &o);
                            pos += o.keep;
                            state = o.new_state;
                            if (cx.error) break;
                        }
                    }
                }
                double ns = (cpu_ns() - t0) / (double)bytes;
                sinkv += acc;
                if (r >= 0) (interp == 0 ? g : interp == 1 ? gm : it)[r] = ns;
            }
            if (r >= 0) {
                ra[r] = it[r] / gm[r];
                rm[r] = it[r] / g[r];
            }
        }
        for (int r = 0; r < reps; r++) {
            double t0 = cpu_ns();
            LimeLexProgram *q = lime_lex_program_compile(lex_src, lex_len, NULL);
            bt[r] = (cpu_ns() - t0) / 1e6;
            lime_lex_program_free(q);
        }
        (void)sinkv;
        printf("per input byte, same actions (CPU time):\n");
        report("generated, CoreLexFeedBytes", g, reps, "ns/B");
        report("generated Core_match + loop", gm, reps, "ns/B");
        report("interpreter + same loop", it, reps, "ns/B");
        report("ratio interp / Core_match", ra, reps, "");
        report("ratio interp / FeedBytes", rm, reps, "");
        report("build pg_scan_core.lex", bt, reps, "ms");
        free(gm);
        free(rm);
        free(g);
        free(it);
        free(ra);
        free(bt);
    }
    for (int a = 0; a < argc; a++) free(bufs[a]);
    free(bufs);
    free(lens);
    lime_lex_program_free(p);
    free(lex_src);
    printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "all lex-program checks passed", fails,
           fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
