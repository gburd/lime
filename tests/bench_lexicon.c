/*
** tests/bench_lexicon.c -- Track B measurements: keyword lookup,
** operator split, DFA interpreter and leading-byte gate.  Each mode
** checks that the two sides agree on every input BEFORE timing them.
** Timing is per-thread CPU time; each rep alternates the variants, and
** the report gives the median and min..max of N reps.
**
**   bench_lexicon kw <kwlist.txt> <sql files...>
**       PostgreSQL's 494 keywords (lines "text TOKEN") in a snapshot
**       lexicon vs the same set in today's TokenTable (rwlock per
**       lookup).  The input is every identifier-shaped word in the
**       given files, a realistic keyword/non-keyword mix.  Reports
**       ns/lookup with 1 thread and with 4 threads.
**
**   bench_lexicon op <sql files...>
**       PG's 10 multi-char operator tokens (+ -> ->>) and 17 self chars
**       in the snapshot trie vs the same decisions as hand-written byte
**       compares (what scan.l's fixed rules compile to).  Splits every
**       operator-character run in the files.
**
** Environment: BENCH_REPS (default 21), BENCH_CHECK_ONLY=1 (check
** agreement and exit; that is what the meson test runs).
*/
#define _POSIX_C_SOURCE 200809L
#include "lime_lexer.h"
#include "parser.h"
#include "snapshot.h"
#include "token_table.h"

#include "lime_threads.h"  /* pthreads, or the Win32 shim */
#include "lime_time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern int lime_compile_grammar_in_process(const char *, size_t, ParserSnapshot **, char **);
extern int lime_snapshot_token_code(const ParserSnapshot *snap, const char *name);

/* Per-thread CPU time; the timed runs are by hand, on Linux.  Elsewhere
** (Windows) fall back to the monotonic clock: the meson test only runs
** the agreement check (BENCH_CHECK_ONLY=1), which does not time. */
static double thread_cpu_ns(void) {
#if defined(CLOCK_THREAD_CPUTIME_ID) && !defined(_WIN32)
    struct timespec t;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return (double)t.tv_sec * 1e9 + (double)t.tv_nsec;
#else
    return (double)lime_now_ns();
#endif
}

static int dcmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static void report(const char *what, double *v, int n) {
    qsort(v, (size_t)n, sizeof *v, dcmp);
    printf("  %-34s median %8.3f  min %8.3f  max %8.3f  (n=%d)\n", what, v[n / 2], v[0],
           v[n - 1], n);
}

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

static int env_int(const char *k, int d) {
    const char *v = getenv(k);
    return v && *v ? atoi(v) : d;
}

/* ------------------------------------------------------------------ */
/*  kw: snapshot keyword table vs TokenTable                           */
/* ------------------------------------------------------------------ */

typedef struct {
    const char **s;
    size_t *n;
    size_t count;
} Words;

static int is_id0(unsigned char c) {
    return c == '_' || c >= 0x80 || (unsigned)(c | 0x20) - 'a' < 26u;
}
static int is_id(unsigned char c) {
    return is_id0(c) || (c >= '0' && c <= '9') || c == '$';
}

static void words_from(Words *w, size_t *cap, const char *b, size_t len) {
    for (size_t i = 0; i < len;) {
        if (!is_id0((unsigned char)b[i])) {
            i++;
            continue;
        }
        size_t j = i;
        while (j < len && is_id((unsigned char)b[j])) j++;
        if (w->count == *cap) {
            *cap = *cap ? *cap * 2 : 4096;
            w->s = realloc(w->s, *cap * sizeof *w->s);
            w->n = realloc(w->n, *cap * sizeof *w->n);
        }
        w->s[w->count] = b + i;
        w->n[w->count++] = j - i;
        i = j;
    }
}

typedef struct {
    const LimeLexicon *lx;
    TokenTable *tt;
    const Words *w;
    int use_tt;
    int iters;
    double ns;
    long sink;
} KwJob;

static void *kw_job(void *arg) {
    KwJob *j = arg;
    long sink = 0;
    double t0 = thread_cpu_ns();
    for (int it = 0; it < j->iters; it++) {
        if (j->use_tt)
            for (size_t i = 0; i < j->w->count; i++)
                sink += lookup_token(j->tt, j->w->s[i], j->w->n[i]);
        else
            for (size_t i = 0; i < j->w->count; i++)
                sink += lime_kw_lookup(j->lx, j->w->s[i], j->w->n[i]);
    }
    j->ns = (thread_cpu_ns() - t0) / ((double)j->iters * (double)j->w->count);
    j->sink = sink;
    return NULL;
}

/* One rep with `nthreads` threads; returns mean per-thread ns/lookup. */
static double kw_rep(const LimeLexicon *lx, TokenTable *tt, const Words *w, int use_tt,
                     int nthreads, int iters) {
    pthread_t th[8];
    KwJob jobs[8];
    for (int t = 0; t < nthreads; t++) {
        jobs[t] = (KwJob){ lx, tt, w, use_tt, iters, 0, 0 };
        pthread_create(&th[t], NULL, kw_job, &jobs[t]);
    }
    double sum = 0;
    for (int t = 0; t < nthreads; t++) {
        pthread_join(th[t], NULL);
        sum += jobs[t].ns;
    }
    return sum / nthreads;
}

static int mode_kw(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: kw <kwlist> <files...>\n");
        return 2;
    }
    /* keyword list */
    size_t kl;
    char *kb = slurp(argv[2], &kl);
    if (!kb) {
        fprintf(stderr, "cannot read %s\n", argv[2]);
        return 2;
    }
    /* Accepts either "text TOKEN" lines or examples/pg/pg_kwlist.h
    ** entries ('    {"text", TOKEN , ...},'). */
    LimeLexItem items[1024];
    size_t nk = 0;
    for (char *line = strtok(kb, "\n"); line && nk < 1024; line = strtok(NULL, "\n")) {
        char *t, *k;
        if (strncmp(line, "    {\"", 6) == 0) {
            t = line + 6;
            char *q = strchr(t, '"');
            if (!q) continue;
            *q = 0;
            k = q + 1;
            while (*k == ',' || *k == ' ') k++;
            char *end = k;
            while ((*end >= 'A' && *end <= 'Z') || (*end >= '0' && *end <= '9') || *end == '_')
                end++;
            *end = 0;
        } else {
            char *sp = strchr(line, ' ');
            if (!sp || line[0] == '/' || line[0] == '#' || line[0] == ' ') continue;
            *sp = 0;
            t = line;
            k = sp + 1;
        }
        if (!*t || !*k || k[0] < 'A' || k[0] > 'Z' || strspn(k, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != strlen(k))
            continue; /* not a "text TOKEN" pair */
        items[nk].text = t;
        items[nk].token = k;
        nk++;
    }
    /* grammar declaring every keyword token */
    size_t gcap = 128 + kl * 2;
    char *g = malloc(gcap);
    size_t gn = (size_t)snprintf(g, gcap, "%%name KWB\n%%token_type { int }\n%%token IDENT");
    for (size_t i = 0; i < nk; i++) gn += (size_t)snprintf(g + gn, gcap - gn, " %s", items[i].token);
    snprintf(g + gn, gcap - gn, ".\nstmt ::= IDENT.\n");
    ParserSnapshot *snap = NULL;
    char *err = NULL;
    if (lime_compile_grammar_in_process(g, strlen(g), &snap, &err) != 0 || !snap) {
        fprintf(stderr, "compile failed: %s\n", err ? err : "?");
        return 1;
    }
    free(err);
    char *e = lime_snapshot_set_lexicon(snap, items, nk, NULL, 0, LIME_LEX_FOLD_CASE);
    if (e) {
        fprintf(stderr, "set_lexicon: %s\n", e);
        return 1;
    }
    const LimeLexicon *lx = lime_snapshot_lexicon(snap);
    TokenTable *tt = create_token_table(1024);
    for (size_t i = 0; i < nk; i++)
        add_token(tt, items[i].text, lime_snapshot_token_code(snap, items[i].token), 0);

    free(g);
    Words w = { 0 };
    size_t cap = 0, bytes = 0;
    char **bufs = calloc((size_t)argc, sizeof *bufs);
    for (int a = 3; a < argc; a++) {
        size_t n;
        char *b = slurp(argv[a], &n);
        if (!b) continue;
        bufs[a] = b; /* kept alive: words point into it */
        words_from(&w, &cap, b, n);
        bytes += n;
    }
    size_t hits = 0, bad = 0;
    for (size_t i = 0; i < w.count; i++) {
        int x = lime_kw_lookup(lx, w.s[i], w.n[i]);
        int y = lookup_token(tt, w.s[i], w.n[i]);
        hits += x >= 0;
        if (x != y) {
            if (bad++ < 5) fprintf(stderr, "  MISMATCH '%.*s': %d vs %d\n", (int)w.n[i], w.s[i], x, y);
        }
    }
    printf("kw: %zu keywords, %zu identifiers (%zu keyword hits, %.1f%%) from %d files, %zu bytes; "
           "%zu mismatches\n",
           nk, w.count, hits, 100.0 * (double)hits / (double)w.count, argc - 3, bytes, bad);
    int rc = bad ? 1 : 0;
    if (bad || env_int("BENCH_CHECK_ONLY", 0)) goto done;

    int reps = env_int("BENCH_REPS", 21);
    int iters = env_int("BENCH_ITERS", 20);
    double *a1 = malloc(sizeof(double) * (size_t)reps), *b1 = malloc(sizeof(double) * (size_t)reps);
    double *a4 = malloc(sizeof(double) * (size_t)reps), *b4 = malloc(sizeof(double) * (size_t)reps);
    kw_rep(lx, tt, &w, 0, 1, 2); /* warm-up */
    kw_rep(lx, tt, &w, 1, 1, 2);
    for (int r = 0; r < reps; r++) {
        /* alternate the order each rep so drift hits both equally */
        if (r & 1) {
            b1[r] = kw_rep(lx, tt, &w, 1, 1, iters);
            a1[r] = kw_rep(lx, tt, &w, 0, 1, iters);
        } else {
            a1[r] = kw_rep(lx, tt, &w, 0, 1, iters);
            b1[r] = kw_rep(lx, tt, &w, 1, 1, iters);
        }
    }
    for (int r = 0; r < reps; r++) {
        if (r & 1) {
            b4[r] = kw_rep(lx, tt, &w, 1, 4, iters);
            a4[r] = kw_rep(lx, tt, &w, 0, 4, iters);
        } else {
            a4[r] = kw_rep(lx, tt, &w, 0, 4, iters);
            b4[r] = kw_rep(lx, tt, &w, 1, 4, iters);
        }
    }
    double ratio1[64], ratio4[64];
    for (int r = 0; r < reps && r < 64; r++) {
        ratio1[r] = a1[r] / b1[r];
        ratio4[r] = a4[r] / b4[r];
    }
    printf("ns per identifier (CPU time):\n");
    report("snapshot kw table, 1 thread", a1, reps);
    report("TokenTable lookup_token, 1 thread", b1, reps);
    report("ratio kw/TokenTable, 1 thread", ratio1, reps < 64 ? reps : 64);
    report("snapshot kw table, 4 threads", a4, reps);
    report("TokenTable lookup_token, 4 threads", b4, reps);
    report("ratio kw/TokenTable, 4 threads", ratio4, reps < 64 ? reps : 64);
    free(a1);
    free(b1);
    free(a4);
    free(b4);
done:
    destroy_token_table(tt);
    snapshot_release(snap);
    for (int a = 0; a < argc; a++) free(bufs[a]);
    free(bufs);
    free(w.s);
    free(w.n);
    free(kb);
    return rc;
}

/* ------------------------------------------------------------------ */
/*  op: operator trie vs a hand-coded scan.l-style 2-char check        */
/* ------------------------------------------------------------------ */

/* The 2-char tokens PG's scan.l returns as their own terminals, the
** way a hand-written scanner decides them: byte compares in a chain.
** Codes: 1..9 for the 2-char tokens, 100 + byte for a 1-char self
** token, 0 = none.  Same answers as the trie built from OPS2/SELF. */
static size_t hand_op(const char *s, size_t n, int *code) {
    if (n >= 2) {
        char a = s[0], b = s[1];
        int c = 0;
        if (a == ':' && b == ':') c = 1;
        else if (a == '.' && b == '.') c = 2;
        else if (a == ':' && b == '=') c = 3;
        else if (a == '=' && b == '>') c = 4;
        else if (a == '<' && b == '=') c = 5;
        else if (a == '>' && b == '=') c = 6;
        else if (a == '<' && b == '>') c = 7;
        else if (a == '!' && b == '=') c = 7;
        else if (a == '-' && b == '>') c = 8;
        if (c) {
            if (c == 8 && n >= 3 && s[2] == '>') return *code = 9, 3;
            return *code = c, 2;
        }
    }
    if (n >= 1 && strchr(",()[].;:+-*/%^<>=", s[0]) && s[0]) return *code = 100 + (unsigned char)s[0], 1;
    return 0;
}

typedef struct {
    const char **s;
    size_t *n;
    size_t count;
} Spans;

static int op_byte(unsigned char c) {
    return c && strchr("~!@#^&|`?+-*/%<>=:.,()[];", c) != NULL;
}

static int mode_op(int argc, char **argv) {
    static const char *const two[] = { "::", "..", ":=", "=>", "<=", ">=", "<>", "!=", "->", "->>" };
    static const int two_code[] = { 1, 2, 3, 4, 5, 6, 7, 7, 8, 9 };
    static const char self[] = ",()[].;:+-*/%^<>=";
    /* grammar: T1..T9 + S<byte> terminals; codes mapped back below */
    char g[2048];
    size_t gn = (size_t)snprintf(g, sizeof g, "%%name OPB\n%%token_type { int }\n%%token T1 T2 T3 T4 T5 T6 T7 T8 T9");
    for (const char *p = self; *p; p++) gn += (size_t)snprintf(g + gn, sizeof g - gn, " S%d", (unsigned char)*p);
    snprintf(g + gn, sizeof g - gn, ".\nstmt ::= T1.\n");
    ParserSnapshot *snap = NULL;
    if (lime_compile_grammar_in_process(g, strlen(g), &snap, NULL) != 0 || !snap) return 1;
    LimeLexItem items[32];
    char names[32][8];
    size_t ni = 0;
    for (size_t i = 0; i < 10; i++) {
        snprintf(names[ni], sizeof names[ni], "T%d", two_code[i]);
        items[ni] = (LimeLexItem){ two[i], names[ni] };
        ni++;
    }
    char selfs[32][2];
    for (const char *p = self; *p; p++) {
        selfs[ni][0] = *p;
        selfs[ni][1] = 0;
        snprintf(names[ni], sizeof names[ni], "S%d", (unsigned char)*p);
        items[ni] = (LimeLexItem){ selfs[ni], names[ni] };
        ni++;
    }
    char *e = lime_snapshot_set_lexicon(snap, NULL, 0, items, ni, 0);
    if (e) {
        fprintf(stderr, "set_lexicon: %s\n", e);
        return 1;
    }
    const LimeLexicon *lx = lime_snapshot_lexicon(snap);
    int map[4096];
    for (int i = 0; i < 4096; i++) map[i] = -1;
    for (int i = 1; i <= 9; i++) {
        char nm[8];
        snprintf(nm, sizeof nm, "T%d", i);
        map[lime_snapshot_token_code(snap, nm)] = i;
    }
    for (const char *p = self; *p; p++) {
        char nm[8];
        snprintf(nm, sizeof nm, "S%d", (unsigned char)*p);
        map[lime_snapshot_token_code(snap, nm)] = 100 + (unsigned char)*p;
    }

    Spans sp = { 0 };
    size_t cap = 0, bytes = 0;
    char **bufs = calloc((size_t)argc, sizeof *bufs);
    for (int a = 2; a < argc; a++) {
        size_t n;
        char *b = slurp(argv[a], &n);
        if (!b) continue;
        bufs[a] = b;
        bytes += n;
        for (size_t i = 0; i < n;) {
            if (!op_byte((unsigned char)b[i])) {
                i++;
                continue;
            }
            size_t j = i;
            while (j < n && op_byte((unsigned char)b[j])) j++;
            if (sp.count == cap) {
                cap = cap ? cap * 2 : 4096;
                sp.s = realloc(sp.s, cap * sizeof *sp.s);
                sp.n = realloc(sp.n, cap * sizeof *sp.n);
            }
            sp.s[sp.count] = b + i;
            sp.n[sp.count++] = j - i;
            i = j;
        }
    }
    /* agreement: split every span with both, token by token */
    size_t ntok = 0, bad = 0;
    for (size_t k = 0; k < sp.count; k++) {
        const char *s = sp.s[k];
        size_t n = sp.n[k];
        while (n) {
            int c1 = 0, c2 = -1;
            size_t m1 = hand_op(s, n, &c1);
            size_t m2 = lime_op_match(lx, s, n, &c2);
            int c2m = m2 ? map[c2] : 0;
            if (m1 != m2 || (m1 && c1 != c2m)) {
                if (bad++ < 5) fprintf(stderr, "  MISMATCH '%.*s'\n", (int)n, s);
            }
            size_t adv = m1 ? m1 : 1;
            s += adv;
            n -= adv;
            ntok++;
        }
    }
    printf("op: %zu operators (10 multi-char), %zu spans, %zu splits from %d files, %zu bytes; %zu mismatches\n",
           ni, sp.count, ntok, argc - 2, bytes, bad);
    int rc = bad ? 1 : 0;
    if (!bad && !env_int("BENCH_CHECK_ONLY", 0)) {
        int reps = env_int("BENCH_REPS", 21), iters = env_int("BENCH_ITERS", 20);
        double *ta = malloc(sizeof(double) * (size_t)reps), *tb = malloc(sizeof(double) * (size_t)reps);
        double *ra = malloc(sizeof(double) * (size_t)reps);
        volatile long sink = 0;
        for (int r = -2; r < reps; r++) {
            for (int which = 0; which < 2; which++) {
                int trie = (r & 1) ? which : !which;
                long acc = 0;
                double t0 = thread_cpu_ns();
                for (int it = 0; it < iters; it++)
                    for (size_t k = 0; k < sp.count; k++) {
                        const char *s = sp.s[k];
                        size_t n = sp.n[k];
                        while (n) {
                            int c = 0;
                            size_t m = trie ? lime_op_match(lx, s, n, &c) : hand_op(s, n, &c);
                            if (!m) m = 1;
                            acc += c;
                            s += m;
                            n -= m;
                        }
                    }
                double ns = (thread_cpu_ns() - t0) / ((double)iters * (double)ntok);
                sink += acc;
                if (r >= 0) (trie ? ta : tb)[r] = ns;
            }
            if (r >= 0) ra[r] = ta[r] / tb[r];
        }
        (void)sink;
        printf("ns per operator split (CPU time):\n");
        report("snapshot op trie", ta, reps);
        report("hand-coded scan.l compares", tb, reps);
        report("ratio trie/hand", ra, reps);
        free(ta);
        free(tb);
        free(ra);
    }
    for (int a = 0; a < argc; a++) free(bufs[a]);
    free(bufs);
    free(sp.s);
    free(sp.n);
    snapshot_release(snap);
    return rc;
}

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "kw") == 0) return mode_kw(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "op") == 0) return mode_op(argc, argv);
    fprintf(stderr, "usage: bench_lexicon kw <kwlist> <files...> | op <files...>\n");
    return 2;
}
